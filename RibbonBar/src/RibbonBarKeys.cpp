#include "RibbonBarData.h"

#include <algorithm>
#include <cwctype>

// The keyboard: key tips shown by Alt, a level at a time (the tab row and the quick access bar, then a tab's items,
// then a folded group's), and moving a focus between them with the arrow keys. The ribbon takes the focus meanwhile
// (a popup has it while one is open), so the keys come to KeyDown.

using Kind = RibKeyTarget::Kind;

namespace {
	// the character a key types, as a key tip's (capitals, digits); 0 for none
	wchar_t KeyChar(UINT vk) {
		if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
			return (wchar_t)(L'0' + (vk - VK_NUMPAD0));
		UINT c = ::MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR);
		if (c == 0 || (c & 0x80000000))    // a dead key
			return 0;
		auto ch = (wchar_t)std::towupper((wint_t)(c & 0xFFFF));
		return std::iswalnum(ch) ? ch : 0;
	}

	bool InRow(const RibKeyTarget& t) {
		return t.kind == Kind::Tab || t.kind == Kind::Qat;
	}
}

// ---- Entering and leaving ---------------------------------------------------------------------------

BOOL CRibbonBarCtrl::PreTranslateMessage(MSG* pMsg) {
	auto& d = *m_Data;
	if (!IsWindow() || !pMsg->hwnd)
		return FALSE;
	UINT msg = pMsg->message;
	if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_NCLBUTTONDOWN || msg == WM_NCRBUTTONDOWN) {
		d.altPending = false;
		ExitKeyMode(true);    // a click anywhere ends it (one in a popup keeps the popup's focus)
		return FALSE;
	}
	if (msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP && msg != WM_KEYDOWN)
		return FALSE;
	// only the keys of the ribbon's window and its popups
	HWND root = ::GetAncestor(pMsg->hwnd, GA_ROOT);
	if (root != ::GetAncestor(m_hWnd, GA_ROOT) && !IsOurWindow(root))
		return FALSE;
	UINT vk = (UINT)pMsg->wParam;
	switch (msg) {
		case WM_SYSKEYDOWN:
			if (vk == VK_MENU) {
				if (!(pMsg->lParam & (1 << 30)))    // not a repeat
					d.altPending = true;
				return FALSE;
			}
			d.altPending = false;
			if (d.keyMode != RibKeyMode::Off || !IsWindowVisible() || !IsWindowEnabled())
				return FALSE;    // the ribbon has the focus: its own WM_SYSKEYDOWN
			if (vk == VK_F10 && ::GetKeyState(VK_SHIFT) >= 0) {
				ShowKeyTips();
				return TRUE;
			}
			if (pMsg->lParam & (1 << 29)) {
				// Alt+a tab's (or a quick access button's) key tip: straight there
				if (wchar_t ch = KeyChar(vk)) {
					int count = 0;
					RibMatchKeyTip(RowTips(), std::wstring(1, ch), &count);
					if (count > 0) {
						ShowKeyTips();
						KeyDown(WM_SYSKEYDOWN, vk, pMsg->lParam);
						d.eatAltUp = true;
						return TRUE;
					}
				}
			}
			return FALSE;
		case WM_SYSKEYUP:
			if (vk == VK_MENU) {
				bool pending = d.altPending, eat = d.eatAltUp;
				d.altPending = d.eatAltUp = false;
				if (pending && IsWindowVisible() && IsWindowEnabled()) {
					// Alt alone: on, or off again
					if (d.keyMode == RibKeyMode::Off)
						ShowKeyTips();
					else
						ExitKeyMode(true);
					return TRUE;
				}
				// not the system menu's
				return eat || d.keyMode != RibKeyMode::Off;
			}
			return FALSE;
		case WM_KEYDOWN:
			d.altPending = false;
			return FALSE;
	}
	return FALSE;
}

void CRibbonBarCtrl::ShowKeyTips(bool show) {
	if (show)
		EnterKeyMode();
	else
		ExitKeyMode(true);
}

bool CRibbonBarCtrl::IsKeyboardActive() const {
	return m_Data->keyMode != RibKeyMode::Off;
}

void CRibbonBarCtrl::EnterKeyMode() {
	auto& d = *m_Data;
	if (!IsWindow() || !IsWindowVisible() || !IsWindowEnabled())
		return;
	if (d.keyMode == RibKeyMode::Off) {
		HWND focus = ::GetFocus();
		d.restoreFocus = focus == m_hWnd || IsOurWindow(focus) ? nullptr : focus;
		HidePopups(0);
	}
	if (m_hTip)
		::SendMessage(m_hTip, TTM_POP, 0, 0);
	d.keyMode = RibKeyMode::Tips;
	KeyLevel(nullptr);
	SetFocus();    // the keys come here
}

HWND CRibbonBarCtrl::ExitKeyMode(bool restore) {
	auto& d = *m_Data;
	if (d.keyMode == RibKeyMode::Off)
		return nullptr;
	HWND focus = d.restoreFocus;
	d.keyMode = RibKeyMode::Off;
	d.restoreFocus = nullptr;
	d.keyLevel = nullptr;
	DestroyTips();
	d.targets.clear();
	d.typed.clear();
	d.focus = -1;
	d.hotTab = d.hotQat = -1;
	d.ForEachSurface([](RibSurface& s) {
		if (!s.menu)
			s.hot = {};
	});
	InvalidateAll();
	if (restore)
		RestoreFocus(focus);
	return focus;
}

void CRibbonBarCtrl::RestoreFocus(HWND window) {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	for (auto& p : d.popups)
		if (!p->hidden)
			return;    // the popup has it
	// only from the ribbon (or the window it went to as a popup closed): not from where a command put it
	HWND focus = ::GetFocus(), root = ::GetAncestor(m_hWnd, GA_ROOT);
	if (focus && focus != m_hWnd && focus != root)
		return;
	if (::IsWindow(window) && ::IsWindowVisible(window) && ::IsWindowEnabled(window))
		::SetFocus(window);
	else if (focus == m_hWnd)
		::SetFocus(root);
}

LRESULT CRibbonBarCtrl::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (!KeyDown(uMsg, (UINT)wParam, lParam))
		bHandled = FALSE;
	return 0;
}

LRESULT CRibbonBarCtrl::OnKillFocus(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// the focus gone anywhere but a popup of ours (opened by a key tip)
	if (m_Data->keyMode != RibKeyMode::Off && !IsOurWindow((HWND)wParam))
		ExitKeyMode(false);
	bHandled = FALSE;
	return 0;
}

LRESULT CRibbonBarCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	if (m_Data->keyMode != RibKeyMode::Off)
		return DLGC_WANTALLKEYS;
	bHandled = FALSE;
	return 0;
}

// ---- Keys ---------------------------------------------------------------------------------------------------

bool CRibbonBarCtrl::KeyDown(UINT msg, UINT vk, LPARAM lParam) {
	auto& d = *m_Data;
	bool on = d.keyMode != RibKeyMode::Off;
	if (msg == WM_SYSCHAR)
		return on;    // not the system menu's mnemonic (a beep)
	if (msg == WM_SYSKEYUP) {
		if (vk == VK_MENU) {
			// without PreTranslateMessage: Alt alone ends it
			bool pending = d.altPending, eat = d.eatAltUp;
			d.altPending = d.eatAltUp = false;
			if (pending && on)
				ExitKeyMode(true);
			return on || eat;
		}
		return on;
	}
	if (vk == VK_MENU) {
		if (!(lParam & (1 << 30)))
			d.altPending = true;
		return on;
	}
	d.altPending = false;
	if (!on)
		return false;
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	bool tips = d.keyMode == RibKeyMode::Tips;
	switch (vk) {
		case VK_ESCAPE:
			KeyBack();
			return true;
		case VK_F10:
			if (shift)
				return false;    // the context menu
			ExitKeyMode(true);
			return true;
		case VK_SHIFT: case VK_CONTROL: case VK_LWIN: case VK_RWIN: case VK_CAPITAL:
			return true;
		case VK_APPS:
			return false;    // WM_CONTEXTMENU
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
			if (tips)
				StartNavigating(vk);
			else
				MoveFocus(vk);
			return true;
		case VK_TAB:
			if (tips)
				StartNavigating(0);
			else if (int n = (int)d.targets.size())
				SetFocusTarget(d.focus < 0 ? 0 : (d.focus + (shift ? n - 1 : 1)) % n);
			return true;
		case VK_RETURN: case VK_SPACE:
			if (!tips && d.focus >= 0 && d.focus < (int)d.targets.size())
				ActivateTarget(d.focus);
			return true;
		case VK_BACK:
			if (tips && !d.typed.empty()) {
				d.typed.pop_back();
				FilterTips();
			}
			return true;
	}
	wchar_t ch = tips ? KeyChar(vk) : 0;
	if (!ch)
		return msg == WM_KEYDOWN;    // Alt+F4 and the like go on
	std::vector<std::wstring> all;
	all.reserve(d.targets.size());
	for (auto& t : d.targets)
		all.push_back(t.tip);
	int count = 0;
	int hit = RibMatchKeyTip(all, d.typed + ch, &count);
	if (count == 0) {
		::MessageBeep(MB_OK);    // no tip starts so
		return true;
	}
	d.typed += ch;
	if (hit >= 0)
		ActivateTarget(hit);
	else
		FilterTips();    // the tips that start so
	return true;
}

// ---- Levels -------------------------------------------------------------------------------------------------

void CRibbonBarCtrl::AddRowTargets(std::vector<RibKeyTarget>& out, std::vector<std::wstring>* labels, std::vector<std::wstring>* fixed) {
	auto& d = *m_Data;
	for (int v = 0; v < (int)d.visible.size(); v++) {
		RibKeyTarget t;
		t.kind = Kind::Tab;
		t.index = v;
		t.hwnd = m_hWnd;
		out.push_back(t);
		if (labels) {
			labels->push_back(d.tabs[d.visible[v]].text);
			fixed->push_back(d.tabs[d.visible[v]].keyTip);
		}
	}
	int shown = 0;
	for (int q = 0; q < (int)d.qatRects.size(); q++) {
		if (::IsRectEmpty(&d.qatRects[q]))
			continue;
		RibKeyTarget t;
		t.kind = Kind::Qat;
		t.index = q;
		t.hwnd = m_hWnd;
		out.push_back(t);
		if (labels) {
			labels->push_back({});
			fixed->push_back(RibQatKeyTip(shown));
		}
		shown++;
	}
}

void CRibbonBarCtrl::AddSurfaceTargets(std::vector<RibKeyTarget>& out, RibSurface& s, std::vector<std::wstring>* labels,
	std::vector<std::wstring>* fixed) {
	auto& d = *m_Data;
	if (s.tab < 0 || s.tab >= (int)d.tabs.size())
		return;
	auto& groups = d.tabs[s.tab].groups;
	auto add = [&](Kind kind, int g, int i, const std::wstring& label, const std::wstring& tip) {
		RibKeyTarget t;
		t.kind = kind;
		t.group = g;
		t.item = i;
		t.hwnd = s.hwnd;
		out.push_back(t);
		if (labels) {
			labels->push_back(label);
			fixed->push_back(tip);
		}
	};
	// those in view (scrolled: some aren't)
	auto inView = [&](const RECT& rc) {
		return rc.right > s.panel.left && rc.left < s.panel.right;
	};
	for (int g = 0; g < (int)s.places.size() && g < (int)groups.size(); g++) {
		auto& place = s.places[g];
		auto& group = groups[g];
		if (place.width <= 0)
			continue;
		int x = s.GroupX(g);
		if (place.collapsed) {
			if (inView({ x, 0, x + place.width, 1 }))
				add(Kind::Group, g, -1, group.text, group.keyTip);
			continue;
		}
		for (int i = 0; i < (int)group.items.size() && i < (int)place.items.size(); i++) {
			auto& item = group.items[i];
			if (item.Hidden() || item.kind == RibKind::Separator || ::IsRectEmpty(&place.items[i].rc))
				continue;
			if (inView(ItemRect(s, g, i)))
				add(Kind::Item, g, i, item.text, item.keyTip);
		}
		if (!::IsRectEmpty(&place.launcher) && inView({ x + place.launcher.left, 0, x + place.launcher.right, 1 }))
			add(Kind::Launcher, g, -1, group.text, {});
	}
}

std::vector<std::wstring> CRibbonBarCtrl::RowTips() {
	// the tab row's tips, without changing the targets shown
	EnsureLayout();
	std::vector<RibKeyTarget> row;
	std::vector<std::wstring> labels, fixed;
	AddRowTargets(row, &labels, &fixed);
	return RibAssignKeyTips(labels, fixed);
}

void CRibbonBarCtrl::KeyLevel(HWND level) {
	auto& d = *m_Data;
	EnsureLayout();
	d.keyMode = RibKeyMode::Tips;
	d.keyLevel = level;
	d.targets.clear();
	d.typed.clear();
	d.focus = -1;
	std::vector<std::wstring> labels, fixed;
	if (!level)
		AddRowTargets(d.targets, &labels, &fixed);
	else if (auto s = SurfaceOf(level))
		AddSurfaceTargets(d.targets, *s, &labels, &fixed);
	auto tips = RibAssignKeyTips(labels, fixed);
	for (size_t k = 0; k < d.targets.size() && k < tips.size(); k++)
		d.targets[k].tip = tips[k];
	InvalidateAll();
	ShowTips();
}

void CRibbonBarCtrl::NavLevel(HWND level) {
	auto& d = *m_Data;
	EnsureLayout();
	d.keyMode = RibKeyMode::Navigate;
	d.keyLevel = level;
	d.targets.clear();
	d.typed.clear();
	d.focus = -1;
	if (level) {
		if (auto s = SurfaceOf(level))
			AddSurfaceTargets(d.targets, *s, nullptr, nullptr);
	}
	else {
		AddRowTargets(d.targets, nullptr, nullptr);
		if (!m_Minimized)
			AddSurfaceTargets(d.targets, d.main, nullptr, nullptr);
	}
	DestroyTips();
	InvalidateAll();
}

void CRibbonBarCtrl::StartNavigating(UINT vk) {
	// from the key tips: the focus where they were
	auto& d = *m_Data;
	HWND level = d.keyLevel;
	if (level && level != m_hWnd) {
		NavLevel(level);
		SetFocusTarget(d.targets.empty() ? -1 : 0);
		return;
	}
	NavLevel(nullptr);
	if (level == m_hWnd) {
		auto it = std::find_if(d.targets.begin(), d.targets.end(), [](const RibKeyTarget& t) { return !InRow(t); });
		if (it != d.targets.end()) {
			SetFocusTarget((int)(it - d.targets.begin()));
			return;
		}
	}
	auto sel = std::find(d.visible.begin(), d.visible.end(), d.selected);
	SetFocusTarget(sel == d.visible.end() ? (d.targets.empty() ? -1 : 0) : FindTarget((int)Kind::Tab, (int)(sel - d.visible.begin()), -1, m_hWnd));
	if (vk)
		MoveFocus(vk);
}

void CRibbonBarCtrl::KeyBack() {
	auto& d = *m_Data;
	HWND level = d.keyLevel;
	bool navigating = d.keyMode == RibKeyMode::Navigate;
	if (!level || level == m_hWnd) {
		if (!navigating && level == m_hWnd)
			KeyLevel(nullptr);    // a tab's tips: back to the tabs'
		else
			ExitKeyMode(true);
		return;
	}
	// a popup: closed, back to where it opened from
	size_t k = 0;
	while (k < d.popups.size() && d.popups[k]->m_hWnd != level)
		k++;
	if (k == d.popups.size()) {
		ExitKeyMode(true);
		return;
	}
	HWND opener = d.popups[k]->opener;
	int group = d.popups[k]->surface.group;
	bool tabPopup = opener == m_hWnd && group < 0;    // a minimized ribbon's tab
	HWND back = opener == m_hWnd ? nullptr : opener;
	int focus = -1;
	if (navigating) {
		NavLevel(back);
		auto sel = std::find(d.visible.begin(), d.visible.end(), d.selected);
		focus = tabPopup ? (sel == d.visible.end() ? -1 : FindTarget((int)Kind::Tab, (int)(sel - d.visible.begin()), -1, m_hWnd))
			: FindTarget((int)Kind::Group, -1, group, opener);
	}
	else
		KeyLevel(back ? back : tabPopup ? nullptr : m_hWnd);
	HidePopups(k);
	if (!back)
		SetFocus();
	if (navigating)
		SetFocusTarget(focus);
}

// ---- Targets ---------------------------------------------------------------------------------------------------

RibSurface* CRibbonBarCtrl::SurfaceOf(HWND hwnd) const {
	auto& d = *m_Data;
	if (hwnd == m_hWnd)
		return &d.main;
	for (auto& p : d.popups)
		if (!p->hidden && p->m_hWnd == hwnd)
			return &p->surface;
	return nullptr;
}

bool CRibbonBarCtrl::IsOurWindow(HWND hwnd) const {
	if (!hwnd)
		return false;
	if (hwnd == m_hWnd)
		return true;
	for (auto& p : m_Data->popups)
		if (!p->hidden && p->m_hWnd == hwnd)
			return true;
	return false;
}

int CRibbonBarCtrl::FindTarget(int kind, int index, int group, HWND hwnd) const {
	auto& targets = m_Data->targets;
	for (int k = 0; k < (int)targets.size(); k++) {
		auto& t = targets[k];
		if ((int)t.kind != kind || t.hwnd != hwnd)
			continue;
		bool same = t.kind == Kind::Tab || t.kind == Kind::Qat ? t.index == index
			: t.kind == Kind::Item ? t.group == group && t.item == index : t.group == group;
		if (same)
			return k;
	}
	return -1;
}

RECT CRibbonBarCtrl::TargetRect(const RibKeyTarget& t) const {
	auto& d = *m_Data;
	if (t.kind == Kind::Tab)
		return t.index >= 0 && t.index < (int)d.tabRects.size() ? d.tabRects[t.index] : RECT{};
	if (t.kind == Kind::Qat)
		return t.index >= 0 && t.index < (int)d.qatRects.size() ? d.qatRects[t.index] : RECT{};
	if (t.kind == Kind::Customize)
		return d.qatCustomize;
	if (t.kind == Kind::Minimize)
		return d.minimizeButton;
	auto s = SurfaceOf(t.hwnd);
	if (!s || t.group < 0 || t.group >= (int)s->places.size())
		return {};
	auto& place = s->places[t.group];
	RECT rc{};
	switch (t.kind) {
		case Kind::Item:
			if (t.item >= 0 && t.item < (int)place.items.size())
				rc = ItemRect(*s, t.group, t.item);
			break;
		case Kind::Group:
			rc = RibCollapsedRect(place.width, d.m);
			::OffsetRect(&rc, s->GroupX(t.group), s->panel.top);
			break;
		default:
			rc = place.launcher;
			::OffsetRect(&rc, s->GroupX(t.group), s->panel.top);
			break;
	}
	return rc;
}

void CRibbonBarCtrl::DestroyTips() {
	auto& tips = m_Data->tips;
	for (auto& w : tips)
		if (w && w->IsWindow())
			w->DestroyWindow();
	tips.clear();
}

void CRibbonBarCtrl::ShowTips() {
	auto& d = *m_Data;
	DestroyTips();
	if (d.keyMode != RibKeyMode::Tips || !IsWindow())
		return;
	auto& m = d.m;
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	HWND root = ::GetAncestor(m_hWnd, GA_ROOT);
	d.tips.resize(d.targets.size());
	for (size_t k = 0; k < d.targets.size(); k++) {
		auto& t = d.targets[k];
		if (t.tip.empty())
			continue;
		RECT rc = TargetRect(t);
		// under the middle of a tab or a large button, under a small one's image, at a window's left
		bool disabled = false;
		POINT at{ (rc.left + rc.right) / 2, rc.bottom - Scale(2) };
		if (t.kind == Kind::Tab || t.kind == Kind::Qat)
			at.y = rc.bottom;
		else if (t.kind == Kind::Item) {
			if (auto s = SurfaceOf(t.hwnd)) {
				auto& item = d.tabs[s->tab].groups[t.group].items[t.item];
				disabled = (item.state & RibStateDisabled) != 0;
				auto size = s->places[t.group].items[t.item].size;
				if (item.kind == RibKind::Window)
					at = { rc.right - item.windowWidth + Scale(8), rc.bottom - Scale(1) };
				else if (size != RibSize::Large)
					at.x = rc.left + m.pad + m.smallIcon / 2 + Scale(4);
			}
		}
		SIZE size{};
		dc.GetTextExtent(t.tip.c_str(), (int)t.tip.size(), &size);
		int h = m_TextHeight + Scale(2), w = std::max((int)size.cx + Scale(8), h);
		RECT box{ at.x - w / 2, at.y - h / 2, at.x - w / 2 + w, at.y - h / 2 + h };
		// across, in the surface's window
		RECT client;
		::GetClientRect(t.hwnd, &client);
		if (box.right > client.right)
			::OffsetRect(&box, client.right - box.right, 0);
		if (box.left < client.left)
			::OffsetRect(&box, client.left - box.left, 0);
		::MapWindowPoints(t.hwnd, nullptr, (POINT*)&box, 2);
		auto tip = std::make_unique<RibTipWindow>(this);
		tip->text = t.tip;
		tip->disabled = disabled;
		// owned by the surface's window: over it
		if (!tip->Create(t.hwnd == m_hWnd ? root : t.hwnd, box, nullptr))
			continue;
		HRGN rgn = ::CreateRoundRectRgn(0, 0, w + 1, h + 1, Scale(4), Scale(4));
		tip->SetWindowRgn(rgn, FALSE);    // the window owns it
		d.tips[k] = std::move(tip);
	}
	dc.SelectFont(old);
	FilterTips();
}

void CRibbonBarCtrl::FilterTips() {
	auto& d = *m_Data;
	for (size_t k = 0; k < d.tips.size() && k < d.targets.size(); k++)
		if (auto& w = d.tips[k]; w && w->IsWindow()) {
			bool show = d.targets[k].tip.starts_with(d.typed);
			if ((w->IsWindowVisible() != FALSE) != show)
				w->SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
		}
}

void CRibbonBarCtrl::InvalidateAll() {
	if (!IsWindow())
		return;
	Invalidate(FALSE);
	for (auto& p : m_Data->popups)
		if (!p->hidden && p->IsWindow())
			p->Invalidate(FALSE);
}

// ---- Navigating ---------------------------------------------------------------------------------------------------

void CRibbonBarCtrl::SetFocusTarget(int index) {
	// the focus is drawn hot (and framed)
	auto& d = *m_Data;
	d.focus = index >= 0 && index < (int)d.targets.size() ? index : -1;
	d.hotTab = d.hotQat = -1;
	d.ForEachSurface([](RibSurface& s) {
		s.hot = {};
	});
	if (d.focus >= 0) {
		auto& t = d.targets[d.focus];
		auto s = SurfaceOf(t.hwnd);
		switch (t.kind) {
			case Kind::Tab: d.hotTab = t.index; break;
			case Kind::Qat: d.hotQat = t.index; break;
			case Kind::Item: if (s) s->hot = { RibPart::Item, t.group, t.item }; break;
			case Kind::Group: if (s) s->hot = { RibPart::Group, t.group, -1 }; break;
			case Kind::Launcher: if (s) s->hot = { RibPart::Launcher, t.group, -1 }; break;
			default: break;
		}
		NotifyAccessible(t.hwnd, EVENT_OBJECT_FOCUS, AccChildId(t));
	}
	InvalidateAll();
}

void CRibbonBarCtrl::MoveFocus(UINT vk) {
	auto& d = *m_Data;
	if (d.focus < 0 || d.focus >= (int)d.targets.size()) {
		SetFocusTarget(d.targets.empty() ? -1 : 0);
		return;
	}
	auto f = d.targets[d.focus];
	bool row = InRow(f);
	if (row && vk == VK_DOWN && f.kind == Kind::Tab && m_Minimized) {
		ActivateTarget(d.focus);    // its popup
		return;
	}
	// the tab row: left and right in it, down into the groups; the groups: among themselves
	std::vector<RECT> rects{ TargetRect(f) };
	std::vector<int> map{ d.focus };
	for (int k = 0; k < (int)d.targets.size(); k++) {
		if (k == d.focus)
			continue;
		auto& t = d.targets[k];
		bool candidate = row ? (vk == VK_LEFT || vk == VK_RIGHT ? InRow(t) : vk == VK_DOWN && !InRow(t)) : !InRow(t);
		if (candidate) {
			rects.push_back(TargetRect(t));
			map.push_back(k);
		}
	}
	int n = RibNearest(rects, 0, vk);
	int to = n > 0 ? map[n] : -1;
	if (to < 0 && !row && vk == VK_UP && !d.keyLevel) {
		// up from the top row of items: the selected tab
		auto sel = std::find(d.visible.begin(), d.visible.end(), d.selected);
		if (sel != d.visible.end())
			to = FindTarget((int)Kind::Tab, (int)(sel - d.visible.begin()), -1, m_hWnd);
	}
	if (to < 0)
		return;
	auto& t = d.targets[to];
	if (t.kind == Kind::Tab && !m_Minimized && d.visible[t.index] != d.selected) {
		// along the tabs: each selected, its groups the ones below
		int v = t.index;
		UserSelectTab(v);
		NavLevel(nullptr);
		SetFocusTarget(FindTarget((int)Kind::Tab, v, -1, m_hWnd));
		return;
	}
	SetFocusTarget(to);
}

void CRibbonBarCtrl::ActivateTarget(int index) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.targets.size())
		return;
	auto t = d.targets[index];    // a copy: the targets change
	bool navigating = d.keyMode == RibKeyMode::Navigate;
	// a level opened: its tips, or the focus on its first
	auto into = [&](HWND level) {
		if (!navigating) {
			KeyLevel(level);
			return;
		}
		NavLevel(level == m_hWnd ? nullptr : level);
		auto it = std::find_if(d.targets.begin(), d.targets.end(), [](const RibKeyTarget& x) { return !InRow(x); });
		SetFocusTarget(it == d.targets.end() ? -1 : (int)(it - d.targets.begin()));
	};
	switch (t.kind) {
		case Kind::Tab: {
			UserSelectTab(t.index);
			EnsureLayout();
			if (!m_Minimized) {
				into(m_hWnd);
				return;
			}
			HidePopups(0);
			OpenTabPopup();
			if (!d.popups.empty() && !d.popups.back()->hidden)
				into(d.popups.back()->m_hWnd);
			return;
		}
		case Kind::Group: {
			auto s = SurfaceOf(t.hwnd);
			if (!s)
				return;
			OpenGroupPopup(*s, t.group);
			if (!d.popups.empty() && !d.popups.back()->hidden)
				into(d.popups.back()->m_hWnd);
			return;
		}
		case Kind::Qat: {
			HWND focus = ExitKeyMode(false);
			RunQat(t.index);
			RestoreFocus(focus);
			return;
		}
		case Kind::Launcher: {
			auto s = SurfaceOf(t.hwnd);
			HWND focus = ExitKeyMode(false);
			if (s)
				RunLauncher(*s, t.group);
			RestoreFocus(focus);
			return;
		}
		default:
			return;
		case Kind::Item: {
			auto s = SurfaceOf(t.hwnd);
			if (!s || s->tab < 0)
				return;
			auto& item = d.tabs[s->tab].groups[t.group].items[t.item];
			if (item.state & RibStateDisabled) {
				::MessageBeep(MB_OK);
				d.typed.clear();
				FilterTips();
				return;
			}
			HWND focus = ExitKeyMode(false);
			if (item.kind == RibKind::Window) {
				if (::IsWindow(item.window))
					::SetFocus(item.window);    // typing goes there now
				return;
			}
			// a split button's tip opens its menu (Enter on it runs its command)
			if (item.kind == RibKind::DropDown || (item.kind == RibKind::Split && !navigating))
				DropDown(*s, t.group, t.item);
			else
				Execute(s->tab, t.group, t.item);
			RestoreFocus(focus);
			return;
		}
	}
}
