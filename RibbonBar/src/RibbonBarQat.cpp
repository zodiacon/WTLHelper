#include "RibbonBarData.h"

#include <commctrl.h>

#include <algorithm>

// The quick access bar, the ribbon's own menus (right-click, the bar's button) and the saved state.

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	constexpr UINT QatLast = 0xFEFF;   // RIBCMD_QATFIRST's range
}

// ---- API ------------------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddToQat(UINT id, int insertAt) {
	auto& d = *m_Data;
	if (!d.QatAllowed(id) || IsInQat(id) || d.qat.size() >= QatLast - RIBCMD_QATFIRST)
		return FALSE;
	if (insertAt < 0 || insertAt > (int)d.qat.size())
		insertAt = (int)d.qat.size();
	d.qat.insert(d.qat.begin() + insertAt, id);
	d.hotQat = d.pressedQat = -1;
	Changed(false);
	return TRUE;
}

BOOL CRibbonBarCtrl::RemoveFromQat(UINT id) {
	auto& d = *m_Data;
	auto it = std::find(d.qat.begin(), d.qat.end(), id);
	if (it == d.qat.end())
		return FALSE;
	d.qat.erase(it);
	d.hotQat = d.pressedQat = -1;
	Changed(false);
	return TRUE;
}

bool CRibbonBarCtrl::IsInQat(UINT id) const {
	auto& qat = m_Data->qat;
	return std::find(qat.begin(), qat.end(), id) != qat.end();
}

int CRibbonBarCtrl::GetQatItems(UINT* ids, int count) const {
	auto& qat = m_Data->qat;
	if (ids)
		for (int i = 0; i < count && i < (int)qat.size(); i++)
			ids[i] = qat[i];
	return (int)qat.size();
}

void CRibbonBarCtrl::NotifyQat() {
	NMHDR nm{};
	FireNotify(&nm, RIBN_QATCHANGED);
}

// ---- State -------------------------------------------------------------------------------------------

int CRibbonBarCtrl::GetState(LPWSTR text, int cch) const {
	RibState state;
	state.tab = GetSelectedTab();
	state.minimized = m_Minimized;
	state.qat = m_Data->qat;
	auto s = RibSaveState(state);
	if (text && cch > 0)
		::wcsncpy_s(text, cch, s.c_str(), _TRUNCATE);
	return (int)s.size() + 1;
}

BOOL CRibbonBarCtrl::SetState(LPCWSTR text) {
	auto& d = *m_Data;
	RibState state;
	if (!text || !RibLoadState(text, state))
		return FALSE;
	d.qat.clear();
	for (UINT id : state.qat)
		if (d.QatAllowed(id) && !IsInQat(id))
			d.qat.push_back(id);
	d.hotQat = d.pressedQat = -1;
	if (state.tab)
		SelectTab(state.tab);
	SetMinimized(state.minimized && !(GetStyle() & RIBS_NOMINIMIZE));
	Changed(false);
	return TRUE;
}

// ---- Layout --------------------------------------------------------------------------------------------

void CRibbonBarCtrl::LayoutQat(int& x) {
	auto& d = *m_Data;
	d.qatRects.assign(d.qat.size(), RECT{});
	d.qatCustomize = {};
	if (!IsWindow() || (GetStyle() & RIBS_NOQAT))
		return;
	auto& m = d.m;
	int top = Scale(3), bottom = m_TabHeight - Scale(3);
	int square = std::max(m.smallIcon + 2 * m.pad + Scale(2), bottom - top);
	std::unique_ptr<WTL::CClientDC> dc;    // only for a button without an image: its text
	HFONT old = nullptr;
	for (size_t q = 0; q < d.qat.size(); q++) {
		auto item = d.FindItem(d.qat[q]);
		if (!item || item->Hidden())
			continue;
		int w = square;
		if (item->smallImage < 0) {
			if (!dc) {
				dc = std::make_unique<WTL::CClientDC>(m_hWnd);
				old = dc->SelectFont(m_hFont);
			}
			SIZE size{};
			dc->GetTextExtent(item->text.c_str(), (int)item->text.size(), &size);
			w = std::max(square, (int)size.cx + 4 * m.pad);
		}
		d.qatRects[q] = { x, top, x + w, bottom };
		x += w;
	}
	if (dc)
		dc->SelectFont(old);
	d.qatCustomize = { x, top, x + Scale(16), bottom };
	x = d.qatCustomize.right + Scale(6);
	d.qatLine = x;
	x += 1 + Scale(4);
}

int CRibbonBarCtrl::QatAt(POINT pt) const {
	auto& d = *m_Data;
	if (::PtInRect(&d.qatCustomize, pt))
		return RibData::Customize;
	for (int q = 0; q < (int)d.qatRects.size(); q++)
		if (::PtInRect(&d.qatRects[q], pt))
			return q;
	return -1;
}

void CRibbonBarCtrl::InvalidateTabRow() {
	if (!IsWindow())
		return;
	RECT row;
	GetClientRect(&row);
	row.bottom = m_TabHeight;
	InvalidateRect(&row, FALSE);
}

void CRibbonBarCtrl::SetHotQat(int index) {
	auto& d = *m_Data;
	if (index == d.hotQat)
		return;
	d.hotQat = index;
	InvalidateTabRow();
}

// ---- Mouse ------------------------------------------------------------------------------------------------

void CRibbonBarCtrl::QatMouseDown(int index) {
	auto& d = *m_Data;
	auto item = d.FindItem(d.qat[index]);
	if (!item || (item->state & RibStateDisabled))
		return;
	if (item->kind == RibKind::DropDown) {
		QatDropDown(index);
		return;
	}
	d.pressedQat = d.hotQat = index;
	SetCapture();
	InvalidateTabRow();
}

void CRibbonBarCtrl::QatDropDown(int index) {
	auto& d = *m_Data;
	auto item = d.FindItem(d.qat[index]);
	if (!item)
		return;
	// pressed while the menu is open
	d.hotQat = d.pressedQat = index;
	d.qatMenu = true;
	InvalidateTabRow();
	UpdateWindow();
	HidePopups(0);
	UINT cmd = ItemMenu(item->id, item->menu, item->lParam, m_hWnd, d.qatRects[index]);
	d.qatMenu = false;
	d.hotQat = d.pressedQat = -1;
	InvalidateTabRow();
	if (cmd)
		::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

void CRibbonBarCtrl::RunQat(int index) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.qat.size())
		return;
	int t, g, i;
	auto item = d.FindItem(d.qat[index], &t, &g, &i);
	if (!item || (item->state & RibStateDisabled))
		return;
	if (item->kind == RibKind::DropDown)
		QatDropDown(index);
	else
		Execute(t, g, i);    // a split button: its command
}

// ---- Menus ---------------------------------------------------------------------------------------------------

void CRibbonBarCtrl::CustomizeMenu() {
	auto& d = *m_Data;
	EnsureLayout();
	HidePopups(0);
	HMENU menu = ::CreatePopupMenu();
	::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Customize Quick Access Toolbar");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	for (size_t q = 0; q < d.qat.size(); q++)
		if (auto item = d.FindItem(d.qat[q])) {
			auto& text = item->text.empty() ? item->tip : item->text;
			::AppendMenuW(menu, MF_STRING | MF_CHECKED, RIBCMD_QATFIRST + (UINT)q, text.c_str());
		}
	if (!(GetStyle() & RIBS_NOMINIMIZE)) {
		::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(menu, MF_STRING | (m_Minimized ? MF_CHECKED : 0), RIBCMD_MINIMIZE, L"Minimize the Ribbon");
	}
	RECT rc = d.qatCustomize;
	POINT screen{ rc.left, rc.bottom };
	ClientToScreen(&screen);
	d.hotQat = RibData::Customize;
	d.qatMenu = true;
	InvalidateTabRow();
	UpdateWindow();
	RunMenu(menu, RIBCM_CUSTOMIZE, 0, m_hWnd, rc, screen);
	d.qatMenu = false;
	d.hotQat = -1;
	InvalidateTabRow();
}

LRESULT CRibbonBarCtrl::OnContextMenu(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_hWnd) {
		bHandled = FALSE;    // a hosted window's
		return 0;
	}
	ContextMenu(m_hWnd, lParam);
	return 0;
}

void CRibbonBarCtrl::ContextMenu(HWND hwnd, LPARAM lParam) {
	auto& d = *m_Data;
	EnsureLayout();
	UINT where = RIBCM_RIBBON, id = 0;
	POINT screen = PointOf(lParam), pt = screen;
	if (screen.x == -1 && screen.y == -1) {
		// the keyboard's (Apps, Shift+F10): for the focus while navigating, else at the groups' top left
		pt = { 0, m_TabHeight };
		if (d.keyMode == RibKeyMode::Navigate && d.focus >= 0 && d.focus < (int)d.targets.size()) {
			auto& t = d.targets[d.focus];
			RECT rc = TargetRect(t);
			hwnd = t.hwnd;
			pt = { rc.left, rc.bottom };
			if (t.kind == RibKeyTarget::Kind::Qat && t.index < (int)d.qat.size()) {
				where = RIBCM_QAT;
				id = d.qat[t.index];
			}
			else if (t.kind == RibKeyTarget::Kind::Item)
				if (auto s = SurfaceOf(t.hwnd)) {
					where = RIBCM_ITEM;
					id = d.tabs[s->tab].groups[t.group].items[t.item].id;
				}
		}
		screen = pt;
		::ClientToScreen(hwnd, &screen);
	}
	else {
		::ScreenToClient(hwnd, &pt);
		int q = hwnd == m_hWnd ? QatAt(pt) : -1;
		if (q == RibData::Customize) {
			CustomizeMenu();
			return;
		}
		if (q >= 0) {
			where = RIBCM_QAT;
			id = d.qat[q];
		}
		else if (auto s = SurfaceOf(hwnd)) {
			auto hit = HitSurface(*s, pt);
			if ((hit.part == RibPart::Item || hit.part == RibPart::Arrow) && s->tab >= 0) {
				where = RIBCM_ITEM;
				id = d.tabs[s->tab].groups[hit.group].items[hit.item].id;
			}
		}
	}
	HWND focus = ExitKeyMode(false);
	HMENU menu = ::CreatePopupMenu();
	if (!(GetStyle() & RIBS_NOQAT)) {
		if (where == RIBCM_QAT)
			::AppendMenuW(menu, MF_STRING, RIBCMD_REMOVEFROMQAT, L"Remove from Quick Access Toolbar");
		else if (where == RIBCM_ITEM && d.QatAllowed(id))
			::AppendMenuW(menu, MF_STRING, IsInQat(id) ? RIBCMD_REMOVEFROMQAT : RIBCMD_ADDTOQAT,
				IsInQat(id) ? L"Remove from Quick Access Toolbar" : L"Add to Quick Access Toolbar");
	}
	if (!(GetStyle() & RIBS_NOMINIMIZE)) {
		if (::GetMenuItemCount(menu) > 0)
			::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		::AppendMenuW(menu, MF_STRING | (m_Minimized ? MF_CHECKED : 0), RIBCMD_MINIMIZE, L"Minimize the Ribbon");
	}
	RunMenu(menu, where, id, hwnd, RECT{}, screen);
	RestoreFocus(focus);
}

void CRibbonBarCtrl::RunMenu(HMENU menu, UINT where, UINT id, HWND hwnd, const RECT& rc, POINT screen) {
	auto& d = *m_Data;
	NMRIBCONTEXTMENU nm{};
	nm.where = where;
	nm.id = id;
	nm.hMenu = menu;
	nm.pt = screen;
	if (FireNotify(&nm.hdr, RIBN_CONTEXTMENU) || ::GetMenuItemCount(menu) <= 0) {
		::DestroyMenu(menu);
		return;
	}
	UINT cmd = TrackMenu(menu, hwnd, rc, screen);
	::DestroyMenu(menu);
	switch (cmd) {
		case 0:
			return;
		case RIBCMD_ADDTOQAT:
			if (AddToQat(id))
				NotifyQat();
			return;
		case RIBCMD_REMOVEFROMQAT:
			if (RemoveFromQat(id))
				NotifyQat();
			return;
		case RIBCMD_MINIMIZE:
			SetMinimized(!m_Minimized);
			return;
	}
	if (cmd >= RIBCMD_QATFIRST && cmd <= QatLast) {
		size_t q = cmd - RIBCMD_QATFIRST;
		if (q < d.qat.size() && RemoveFromQat(d.qat[q]))
			NotifyQat();
		return;
	}
	HidePopups(0);
	::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);   // the app's
}
