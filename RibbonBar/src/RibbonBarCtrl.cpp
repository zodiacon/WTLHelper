#include "RibbonBarData.h"

#include <commctrl.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

static_assert(RIBIK_SEPARATOR == (int)RibKind::Separator && RIBIK_WINDOW == (int)RibKind::Window && RIBIK_SPLIT == (int)RibKind::Split);
static_assert(RIBSZ_ICON == (int)RibSize::Icon && RIBSZ_SMALL == (int)RibSize::Small);
static_assert(RIBIS_DISABLED == RibStateDisabled && RIBIS_CHECKED == RibStateChecked && RIBIS_HIDDEN == RibStateHidden);
static_assert(RIBGF_NOCOLLAPSE == RibGroupNoCollapse && RIBGF_LAUNCHER == RibGroupLauncher);

// ---- Helpers -----------------------------------------------------------------------

namespace {
	std::wstring Text(LPCWSTR s) {
		return s ? std::wstring(s) : std::wstring();
	}

	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	// what's under a point: a tab (by its place in the row) or a part of the selected tab's groups
	void HitAt(RibData& d, int tabHeight, POINT pt, int& tab, RibHit& hit) {
		tab = -1;
		hit = {};
		if (pt.y >= 0 && pt.y < tabHeight) {
			for (int i = 0; i < (int)d.tabRects.size(); i++)
				if (::PtInRect(&d.tabRects[i], pt))
					tab = i;
			return;
		}
		if (auto sel = d.Selected(); sel && ::PtInRect(&d.panel, pt))
			hit = RibHitTest(sel->groups, d.places, { pt.x - d.originX, pt.y - d.panel.top }, d.m);
	}
}

// ---- Construction ------------------------------------------------------------------

CRibbonBarCtrl::CRibbonBarCtrl() : m_Data(std::make_unique<RibData>()) {
}

CRibbonBarCtrl::~CRibbonBarCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CRibbonBarCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_CLIPCHILDREN);
	UpdateFonts();
	UpdateMetrics();
	m_LastHeight = GetIdealHeight();
	return 0;
}

LRESULT CRibbonBarCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CRibbonBarCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Changed(false);
	return 0;
}

void CRibbonBarCtrl::SetOwner(HWND owner) {
	m_Owner = owner;
}

// ---- Metrics and fonts -------------------------------------------------------------

int CRibbonBarCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CRibbonBarCtrl::UpdateFonts() {
	if (!m_OwnFont)
		return;
	if (m_hFont)
		::DeleteObject(m_hFont);
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
	m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
}

void CRibbonBarCtrl::UpdateMetrics() {
	if (IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		m_TextHeight = tm.tmHeight;
	}
	auto m = RibMetrics{}.Scaled(m_Dpi);
	int cx, cy;
	if (m_Large && ::ImageList_GetIconSize(m_Large, &cx, &cy))
		m.largeIcon = cx;
	if (m_Small && ::ImageList_GetIconSize(m_Small, &cx, &cy))
		m.smallIcon = cx;
	m.labelHeight = std::max(m.labelHeight, m_TextHeight + Scale(3));
	m.rowHeight = std::max({ m.rowHeight, m.smallIcon + Scale(6), m_TextHeight + Scale(6) });
	// three rows hold a large item: the image and two lines
	int large = 2 * m.pad + m.largeIcon + Scale(2) + 2 * (m_TextHeight - Scale(2));
	m.rowHeight = std::max(m.rowHeight, (large + 2) / 3);
	m_Data->m = m;
	m_TabHeight = m_TextHeight + Scale(12);
	Changed(true);
}

LRESULT CRibbonBarCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	NotifyHeight();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CRibbonBarCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CRibbonBarCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// before the parent too: its WM_DPICHANGED lays the window out with the height for the new DPI. A font set with
	// WM_SETFONT is the app's to replace.
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		m_Dpi = dpi;
		UpdateFonts();
		UpdateMetrics();
	}
	NotifyHeight();
	bHandled = FALSE;
	return 0;
}

LRESULT CRibbonBarCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {   // the message font
		UpdateFonts();
		UpdateMetrics();
		NotifyHeight();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CRibbonBarCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

int CRibbonBarCtrl::GetIdealHeight() const {
	return m_TabHeight + m_Data->m.BodyHeight() + 1;
}

void CRibbonBarCtrl::NotifyHeight() {
	if (!IsWindow())
		return;
	int height = GetIdealHeight();
	if (height == m_LastHeight)
		return;
	m_LastHeight = height;
	NMHDR nm{};
	FireNotify(&nm, RIBN_HEIGHTCHANGED);
}

// ---- Layout ---------------------------------------------------------------------------

void CRibbonBarCtrl::Changed(bool measure) {
	if (measure)
		m_Data->measure = true;
	m_Data->layout = true;
	if (IsWindow() && !m_NoInvalidate)
		Invalidate(FALSE);
}

void CRibbonBarCtrl::EnsureLayout() {
	auto& d = *m_Data;
	if (d.measure && IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		RibMeasure measure = [&](std::wstring_view s) {
			SIZE size{};
			dc.GetTextExtent(s.data(), (int)s.size(), &size);
			return (int)size.cx;
		};
		d.tabWidths.resize(d.tabs.size());
		for (size_t t = 0; t < d.tabs.size(); t++) {
			d.tabWidths[t] = measure(d.tabs[t].text);
			for (auto& g : d.tabs[t].groups) {
				for (auto& item : g.items)
					item.windowWidth = Scale(item.windowDips);
				RibMeasureGroup(g, d.m, measure);
			}
		}
		dc.SelectFont(old);
		d.measure = false;
		d.layout = true;
	}
	if (!d.layout)
		return;
	RECT rc{};
	if (IsWindow())
		GetClientRect(&rc);
	d.visible = RibVisibleTabs(d.tabs, d.contexts);
	if (std::find(d.visible.begin(), d.visible.end(), d.selected) == d.visible.end())
		d.selected = d.visible.empty() ? -1 : d.visible[0];
	d.tabRects.clear();
	int x = Scale(4);
	for (int t : d.visible) {
		int w = (t < (int)d.tabWidths.size() ? d.tabWidths[t] : 0) + Scale(20);
		d.tabRects.push_back({ x, 0, x + w, m_TabHeight });
		x += w;
	}
	d.panel = { 0, m_TabHeight, rc.right, m_TabHeight + d.m.BodyHeight() };
	d.originX = Scale(4);
	d.total = 0;
	if (auto sel = d.Selected())
		d.places = RibLayoutTab(sel->groups, std::max(0, (int)rc.right - 2 * d.originX), d.m, &d.total);
	else
		d.places.clear();
	d.layout = false;
}

// ---- Tabs -------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddTab(const RIBTAB* tab, int insertAt) {
	auto& d = *m_Data;
	if (!tab || tab->id == 0 || d.FindTab(tab->id) >= 0)
		return FALSE;
	RibTab t;
	t.id = tab->id;
	t.text = Text(tab->pszText);
	t.keyTip = Text(tab->pszKeyTip);
	if (insertAt < 0 || insertAt > (int)d.tabs.size())
		insertAt = (int)d.tabs.size();
	d.tabs.insert(d.tabs.begin() + insertAt, std::move(t));
	if (d.selected >= insertAt)
		d.selected++;
	d.hot = d.pressed = {};
	Changed(true);
	return TRUE;
}

BOOL CRibbonBarCtrl::AddTab(UINT id, LPCWSTR text) {
	RIBTAB tab;
	tab.id = id;
	tab.pszText = text;
	return AddTab(&tab);
}

BOOL CRibbonBarCtrl::RemoveTab(UINT id) {
	auto& d = *m_Data;
	int t = d.FindTab(id);
	if (t < 0)
		return FALSE;
	d.tabs.erase(d.tabs.begin() + t);
	if (d.selected > t || d.selected >= (int)d.tabs.size())
		d.selected--;
	d.hot = d.pressed = {};
	d.hotTab = -1;
	Changed(true);
	return TRUE;
}

int CRibbonBarCtrl::GetTabCount() const {
	return (int)m_Data->tabs.size();
}

UINT CRibbonBarCtrl::GetTabId(int index) const {
	auto& tabs = m_Data->tabs;
	return index >= 0 && index < (int)tabs.size() ? tabs[index].id : 0;
}

BOOL CRibbonBarCtrl::SelectTab(UINT id) {
	auto& d = *m_Data;
	int t = d.FindTab(id);
	if (t < 0)
		return FALSE;
	if (t != d.selected) {
		d.selected = t;
		d.hot = d.pressed = {};
		Changed(false);
	}
	return TRUE;
}

UINT CRibbonBarCtrl::GetSelectedTab() const {
	auto sel = m_Data->Selected();
	return sel ? sel->id : 0;
}

void CRibbonBarCtrl::UserSelectTab(int visible) {
	auto& d = *m_Data;
	if (visible < 0 || visible >= (int)d.visible.size() || d.visible[visible] == d.selected)
		return;
	d.selected = d.visible[visible];
	d.hot = d.pressed = {};
	Changed(false);
	NMRIBTAB nm{};
	nm.id = d.tabs[d.selected].id;
	FireNotify(&nm.hdr, RIBN_TABCHANGED);
}

// ---- Groups -------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddGroup(UINT tabId, const RIBGROUP* group) {
	auto& d = *m_Data;
	int t = d.FindTab(tabId);
	if (t < 0 || !group || group->id == 0 || d.FindGroup(group->id))
		return FALSE;
	RibGroup g;
	g.id = group->id;
	g.text = Text(group->pszText);
	g.keyTip = Text(group->pszKeyTip);
	g.image = group->iImage;
	g.flags = group->flags;
	g.priority = group->priority;
	g.launcherId = group->uLauncherId;
	d.tabs[t].groups.push_back(std::move(g));
	Changed(true);
	return TRUE;
}

BOOL CRibbonBarCtrl::AddGroup(UINT tabId, UINT id, LPCWSTR text, UINT flags) {
	RIBGROUP group;
	group.id = id;
	group.pszText = text;
	group.flags = flags;
	return AddGroup(tabId, &group);
}

BOOL CRibbonBarCtrl::RemoveGroup(UINT id) {
	auto& d = *m_Data;
	for (auto& tab : d.tabs)
		for (size_t g = 0; g < tab.groups.size(); g++)
			if (tab.groups[g].id == id) {
				tab.groups.erase(tab.groups.begin() + g);
				d.hot = d.pressed = {};
				Changed(true);
				return TRUE;
			}
	return FALSE;
}

// ---- Items ----------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddItem(UINT groupId, const RIBITEM* item) {
	auto& d = *m_Data;
	auto group = d.FindGroup(groupId);
	if (!group || !item || item->kind > RIBIK_SEPARATOR || item->size > RIBSZ_ICON || item->minSize > RIBSZ_ICON)
		return FALSE;
	if (item->kind != RIBIK_SEPARATOR && item->id == 0)
		return FALSE;
	RibItem i;
	i.id = item->id;
	i.kind = (RibKind)item->kind;
	i.text = Text(item->pszText);
	i.tip = Text(item->pszTip);
	i.keyTip = Text(item->pszKeyTip);
	i.image = item->iImage;
	i.smallImage = item->iSmallImage;
	i.state = item->state;
	i.radio = item->radio;
	i.size = (RibSize)item->size;
	i.minSize = (RibSize)item->minSize;
	i.menu = item->hMenu;
	i.window = item->hwnd;
	i.windowDips = item->cxWindow;
	i.rows = item->cRows;
	i.lParam = item->lParam;
	group->items.push_back(std::move(i));
	Changed(true);
	return TRUE;
}

BOOL CRibbonBarCtrl::AddButton(UINT groupId, UINT id, LPCWSTR text, int image, UINT size, UINT kind) {
	RIBITEM item;
	item.id = id;
	item.kind = kind;
	item.pszText = text;
	item.iImage = item.iSmallImage = image;   // the same index in both lists
	item.size = size;
	return AddItem(groupId, &item);
}

BOOL CRibbonBarCtrl::AddSeparator(UINT groupId) {
	RIBITEM item;
	item.kind = RIBIK_SEPARATOR;
	return AddItem(groupId, &item);
}

BOOL CRibbonBarCtrl::RemoveItem(UINT id) {
	auto& d = *m_Data;
	bool found = false;
	for (auto& tab : d.tabs)
		for (auto& g : tab.groups) {
			auto end = std::remove_if(g.items.begin(), g.items.end(), [id](const RibItem& i) {
				return i.id == id && i.kind != RibKind::Separator;
			});
			found = found || end != g.items.end();
			g.items.erase(end, g.items.end());
		}
	if (found) {
		d.hot = d.pressed = {};
		Changed(true);
	}
	return found;
}

BOOL CRibbonBarCtrl::SetItemState(UINT id, UINT state, UINT mask) {
	bool found = false, changed = false, hidden = false;
	m_Data->ForEachItem(id, [&](int, int, int, RibItem& item) {
		found = true;
		UINT s = (item.state & ~mask) | (state & mask);
		if (s == item.state)
			return;
		changed = true;
		hidden = hidden || ((s ^ item.state) & RibStateHidden);
		item.state = s;
	});
	if (changed)
		Changed(hidden);   // shown or hidden: the groups shrink differently
	return found;
}

UINT CRibbonBarCtrl::GetItemState(UINT id) const {
	UINT state = (UINT)-1;
	m_Data->ForEachItem(id, [&](int, int, int, RibItem& item) {
		if (state == (UINT)-1)
			state = item.state;
	});
	return state;
}

BOOL CRibbonBarCtrl::EnableItem(UINT id, bool enable) {
	return SetItemState(id, enable ? 0 : RIBIS_DISABLED, RIBIS_DISABLED);
}

BOOL CRibbonBarCtrl::CheckItem(UINT id, bool check) {
	auto& d = *m_Data;
	bool radio = false;
	d.ForEachItem(id, [&](int, int, int, RibItem& item) {
		radio = radio || item.kind == RibKind::Radio;
	});
	if (!radio || !check)
		return SetItemState(id, check ? RIBIS_CHECKED : 0, RIBIS_CHECKED);
	bool changed = false;
	d.ForEachItem(id, [&](int t, int g, int i, RibItem& item) {
		if (item.kind == RibKind::Radio)
			changed = RibCheckRadio(d.tabs[t].groups, g, i) || changed;
		else
			item.state |= RibStateChecked;
	});
	if (changed)
		Changed(false);
	return TRUE;
}

BOOL CRibbonBarCtrl::SetItemText(UINT id, LPCWSTR text) {
	bool found = false;
	m_Data->ForEachItem(id, [&](int, int, int, RibItem& item) {
		item.text = Text(text);
		found = true;
	});
	if (found)
		Changed(true);
	return found;
}

void CRibbonBarCtrl::UpdateUI() {
	SendUpdateUI(false);
}

void CRibbonBarCtrl::SendUpdateUI(bool painting) {
	auto sel = m_Data->Selected();
	if (m_InUpdate || !sel || !IsWindow())
		return;
	m_InUpdate = true;
	m_NoInvalidate = painting;
	NMRIBTAB nm{};
	nm.id = sel->id;
	FireNotify(&nm.hdr, RIBN_UPDATEUI);
	m_NoInvalidate = false;
	m_InUpdate = false;
}

void CRibbonBarCtrl::Execute(int group, int item) {
	auto sel = m_Data->Selected();
	if (!sel || group < 0 || group >= (int)sel->groups.size() || item < 0 || item >= (int)sel->groups[group].items.size())
		return;
	auto& i = sel->groups[group].items[item];
	if (i.state & RibStateDisabled)
		return;
	UINT id = i.id;
	auto kind = i.kind;
	LPARAM lParam = i.lParam;
	if (kind == RibKind::Toggle)
		SetItemState(id, i.state ^ RibStateChecked, RibStateChecked);
	else if (kind == RibKind::Radio)
		CheckItem(id, true);
	NMRIBITEM nm{};
	nm.id = id;
	nm.kind = (UINT)kind;
	nm.state = GetItemState(id);
	nm.lParam = lParam;
	if (FireNotify(&nm.hdr, RIBN_EXECUTE))
		return;   // the app ran it
	// posted, as a menu's: the command may well change the ribbon
	::PostMessage(m_Owner ? m_Owner : GetParent().m_hWnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

// ---- Images and colors -----------------------------------------------------------------------

void CRibbonBarCtrl::SetImageLists(HIMAGELIST large, HIMAGELIST smallImages) {
	m_Large = large;
	m_Small = smallImages;
	UpdateMetrics();   // their sizes are the items'
	NotifyHeight();
}

void CRibbonBarCtrl::SetColors(const RIBCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

// ---- Mouse ---------------------------------------------------------------------------------------

void CRibbonBarCtrl::SetHot(int tab, int group, int item, int part) {
	auto& d = *m_Data;
	RibHit hit{ (RibPart)part, group, item };
	if (tab == d.hotTab && Same(hit, d.hot))
		return;
	d.hotTab = tab;
	d.hot = hit;
	Invalidate(FALSE);
}

LRESULT CRibbonBarCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	EnsureLayout();
	if (!m_TrackingMouse) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
	int tab;
	RibHit hit;
	HitAt(d, m_TabHeight, PointOf(lParam), tab, hit);
	if (hit.item >= 0 && (d.Selected()->groups[hit.group].items[hit.item].state & RibStateDisabled))
		hit = { RibPart::None, hit.group, -1 };   // a disabled item isn't hot
	if (::GetCapture() == m_hWnd && !Same(hit, d.pressed))
		hit = {};    // pressed: hot only while over it
	SetHot(::GetCapture() == m_hWnd ? -1 : tab, hit.group, hit.item, (int)hit.part);
	return 0;
}

LRESULT CRibbonBarCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingMouse = false;
	if (::GetCapture() != m_hWnd)
		SetHot(-1, -1, -1, 0);
	return 0;
}

LRESULT CRibbonBarCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	EnsureLayout();
	int tab;
	RibHit hit;
	HitAt(d, m_TabHeight, PointOf(lParam), tab, hit);
	if (tab >= 0) {
		UserSelectTab(tab);
		return 0;
	}
	if (hit.part == RibPart::None)
		return 0;
	if (hit.item >= 0 && (d.Selected()->groups[hit.group].items[hit.item].state & RibStateDisabled))
		return 0;
	d.pressed = d.hot = hit;
	SetCapture();
	Invalidate(FALSE);
	return 0;
}

LRESULT CRibbonBarCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (::GetCapture() != m_hWnd)
		return 0;
	auto pressed = d.pressed;
	POINT pt = PointOf(lParam);
	int tab;
	RibHit hit;
	HitAt(d, m_TabHeight, pt, tab, hit);
	::ReleaseCapture();
	SetHot(tab, hit.group, hit.item, (int)hit.part);
	if (!Same(hit, pressed))
		return 0;
	switch (hit.part) {
		case RibPart::Item:
			if (d.Selected()->groups[hit.group].items[hit.item].kind != RibKind::DropDown)
				Execute(hit.group, hit.item);
			break;
		case RibPart::Launcher: {
			auto& group = d.Selected()->groups[hit.group];
			::PostMessage(m_Owner ? m_Owner : GetParent().m_hWnd, WM_COMMAND, MAKEWPARAM(group.launcherId ? group.launcherId : group.id, 0), 0);
			break;
		}
		default:
			break;
	}
	return 0;
}

LRESULT CRibbonBarCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Data->pressed = {};
	Invalidate(FALSE);
	return 0;
}

LRESULT CRibbonBarCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
	// the wheel over the ribbon goes through the tabs
	auto& d = *m_Data;
	EnsureLayout();
	auto it = std::find(d.visible.begin(), d.visible.end(), d.selected);
	if (it == d.visible.end())
		return 0;
	int v = (int)(it - d.visible.begin()) + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
	UserSelectTab(std::clamp(v, 0, (int)d.visible.size() - 1));
	return 0;
}

// ---- Notifications -----------------------------------------------------------------------

LRESULT CRibbonBarCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}
