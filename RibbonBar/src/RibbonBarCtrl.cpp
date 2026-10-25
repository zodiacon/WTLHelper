#include "RibbonBarData.h"

#include <commctrl.h>

#include <algorithm>
#include <climits>

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

	void Adopt(HWND window, HWND parent) {
		if (!::IsWindow(window) || !::IsWindow(parent))
			return;
		LONG style = ::GetWindowLongW(window, GWL_STYLE);
		if ((style & (WS_CHILD | WS_POPUP)) != WS_CHILD)
			::SetWindowLongW(window, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
		if (::GetParent(window) != parent)
			::SetParent(window, parent);
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
	m_Data->main.hwnd = m_hWnd;
	m_hTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	::SendMessage(m_hTip, TTM_SETMAXTIPWIDTH, 0, Scale(320));    // a description wraps
	AddTipTool(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	m_LastHeight = GetIdealHeight();
	for (auto& tab : m_Data->tabs)
		for (auto& g : tab.groups)
			for (auto& item : g.items)
				if (item.window) {
					Adopt(item.window, m_hWnd);
					::ShowWindow(item.window, SW_HIDE);   // until its group is placed
				}
	return 0;
}

LRESULT CRibbonBarCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	ExitKeyMode(false);
	HidePopups(0);
	DisconnectAccessible(m_Data->acc);
	DestroyHidden();
	bHandled = FALSE;
	return 0;
}

LRESULT CRibbonBarCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CRibbonBarCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	ExitKeyMode(true);   // the items move and fold
	HidePopups(0);   // a minimized tab's is the ribbon's width
	Changed(false);
	return 0;
}

void CRibbonBarCtrl::SetOwner(HWND owner) {
	m_Owner = owner;
}

HWND CRibbonBarCtrl::Owner() const {
	return m_Owner ? m_Owner : GetParent().m_hWnd;
}

LRESULT CRibbonBarCtrl::OnForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (uMsg == WM_NOTIFY && TipNotify((NMHDR*)lParam))
		return 0;
	// a hosted window's notifications and colors are the parent's to answer
	return ::SendMessage(GetParent(), uMsg, wParam, lParam);
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
	HidePopups(0);
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
	for (auto& p : m_Data->popups)
		if (!p->hidden)
			p->Invalidate();
	bHandled = FALSE;
	return 0;
}

int CRibbonBarCtrl::GetIdealHeight() const {
	return m_TabHeight + (m_Minimized ? 0 : m_Data->m.BodyHeight()) + 1;
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
	if (measure) {
		m_Data->measure = true;
		if (!m_InUpdate)
			ExitKeyMode(true);   // items added, removed, shown or hidden: the targets aren't those
	}
	m_Data->layout = true;
	if (IsWindow() && !m_NoInvalidate) {
		Invalidate(FALSE);
		for (auto& p : m_Data->popups)
			if (!p->hidden)
				p->Invalidate(FALSE);
	}
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
	int x = Scale(4), context = 0;
	LayoutQat(x);
	for (int t : d.visible) {
		if (d.tabs[t].context != context) {
			context = d.tabs[t].context;
			x += Scale(6);    // a contextual set apart
		}
		int w = (t < (int)d.tabWidths.size() ? d.tabWidths[t] : 0) + Scale(20);
		d.tabRects.push_back({ x, 0, x + w, m_TabHeight });
		x += w;
	}
	d.minimizeButton = (GetStyle() & RIBS_NOMINIMIZE) ? RECT{} : RECT{ rc.right - m_TabHeight, 0, rc.right, m_TabHeight };

	auto& s = d.main;
	s.hwnd = m_hWnd;
	s.tab = m_Minimized ? -1 : d.selected;
	s.group = -1;
	s.panel = { 0, m_TabHeight, rc.right, m_TabHeight + (m_Minimized ? 0 : d.m.BodyHeight()) };
	s.originX = Scale(4);
	LayoutSurface(s);
	for (auto& p : d.popups)
		if (!p->hidden) {
			LayoutSurface(p->surface);
			p->Invalidate(FALSE);
		}
	d.layout = false;
	PlaceWindows();
	NotifyAccessible(m_hWnd, EVENT_OBJECT_REORDER, CHILDID_SELF);
}

void CRibbonBarCtrl::LayoutSurface(RibSurface& s) {
	auto& d = *m_Data;
	s.total = 0;
	if (s.tab < 0 || s.tab >= (int)d.tabs.size()) {
		s.places.clear();
		return;
	}
	auto& groups = d.tabs[s.tab].groups;
	if (s.group >= 0) {
		// a folded group opened: alone, at its widest
		s.places.assign(groups.size(), RibGroupPlace{});
		if (s.group < (int)groups.size()) {
			std::vector<RibGroup> one{ groups[s.group] };
			s.places[s.group] = RibLayoutTab(one, INT_MAX / 2, d.m, &s.total)[0];
		}
	}
	else
		s.places = RibLayoutTab(groups, s.Avail(), d.m, &s.total);
	s.scroll = std::clamp(s.scroll, 0, s.MaxScroll());
}

RECT CRibbonBarCtrl::ItemRect(const RibSurface& s, int group, int item) const {
	RECT rc = s.places[group].items[item].rc;
	::OffsetRect(&rc, s.GroupX(group), s.panel.top);
	return rc;
}

void CRibbonBarCtrl::PlaceWindows() {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	auto shows = [&](const RibSurface& s, int t, int g) {
		return s.tab == t && g < (int)s.places.size() && s.places[g].width > 0 && !s.places[g].collapsed;
	};
	for (int t = 0; t < (int)d.tabs.size(); t++)
		for (int g = 0; g < (int)d.tabs[t].groups.size(); g++) {
			auto& items = d.tabs[t].groups[g].items;
			for (int i = 0; i < (int)items.size(); i++) {
				auto& item = items[i];
				if (item.kind != RibKind::Window || !::IsWindow(item.window))
					continue;
				// the top popup showing its group, else the ribbon
				const RibSurface* target = nullptr;
				for (auto p = d.popups.rbegin(); p != d.popups.rend() && !target; ++p)
					if (!(*p)->hidden && shows((*p)->surface, t, g))
						target = &(*p)->surface;
				if (!target && shows(d.main, t, g))
					target = &d.main;
				if (!target || item.Hidden()) {
					if (::IsWindowVisible(item.window))
						::ShowWindow(item.window, SW_HIDE);
					Adopt(item.window, m_hWnd);
					continue;
				}
				RECT rc = ItemRect(*target, g, i);
				Adopt(item.window, target->hwnd);
				int x = rc.right - item.windowWidth, height = rc.bottom - rc.top;
				wchar_t cls[32] = L"";
				::GetClassNameW(item.window, cls, (int)std::size(cls));
				bool combo = ::_wcsicmp(cls, WC_COMBOBOXW) == 0 || ::_wcsicmp(cls, WC_COMBOBOXEXW) == 0;
				int ask = combo ? Scale(240) : height - Scale(2);     // a combo box: its list's height
				::SetWindowPos(item.window, nullptr, x, rc.top + Scale(1), item.windowWidth, ask, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
				RECT wr;
				::GetWindowRect(item.window, &wr);
				if (wr.bottom - wr.top != ask)    // its own height: centered in its rows
					::SetWindowPos(item.window, nullptr, x, rc.top + (height - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				bool enable = !(item.state & RibStateDisabled);
				if ((::IsWindowEnabled(item.window) != FALSE) != enable)
					::EnableWindow(item.window, enable);
			}
		}
}

// ---- Tabs -------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddTab(const RIBTAB* tab, int insertAt) {
	auto& d = *m_Data;
	if (!tab || tab->id == 0 || d.FindTab(tab->id) >= 0)
		return FALSE;
	HidePopups(0);
	RibTab t;
	t.id = tab->id;
	t.text = Text(tab->pszText);
	t.keyTip = Text(tab->pszKeyTip);
	t.context = tab->context;
	if (insertAt < 0 || insertAt > (int)d.tabs.size())
		insertAt = (int)d.tabs.size();
	d.tabs.insert(d.tabs.begin() + insertAt, std::move(t));
	if (d.selected >= insertAt)
		d.selected++;
	d.main.hot = d.main.pressed = {};
	Changed(true);
	return TRUE;
}

BOOL CRibbonBarCtrl::AddTab(UINT id, LPCWSTR text, int context) {
	RIBTAB tab;
	tab.id = id;
	tab.pszText = text;
	tab.context = context;
	return AddTab(&tab);
}

BOOL CRibbonBarCtrl::RemoveTab(UINT id) {
	auto& d = *m_Data;
	int t = d.FindTab(id);
	if (t < 0)
		return FALSE;
	HidePopups(0);
	for (auto& g : d.tabs[t].groups)
		for (auto& item : g.items)
			if (::IsWindow(item.window))
				::ShowWindow(item.window, SW_HIDE);
	d.tabs.erase(d.tabs.begin() + t);
	if (d.selected > t || d.selected >= (int)d.tabs.size())
		d.selected--;
	d.main.hot = d.main.pressed = {};
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
	auto visible = RibVisibleTabs(d.tabs, d.contexts);
	if (t < 0 || std::find(visible.begin(), visible.end(), t) == visible.end())
		return FALSE;
	if (t != d.selected) {
		HidePopups(0);
		d.selected = t;
		d.main.hot = d.main.pressed = {};
		d.main.scroll = 0;
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
	d.main.hot = d.main.pressed = {};
	d.main.scroll = 0;
	Changed(false);
	RibKeyTarget tab;
	tab.kind = RibKeyTarget::Kind::Tab;
	tab.index = visible;
	tab.hwnd = m_hWnd;
	NotifyAccessible(m_hWnd, EVENT_OBJECT_SELECTION, AccChildId(tab));
	NMRIBTAB nm{};
	nm.id = d.tabs[d.selected].id;
	FireNotify(&nm.hdr, RIBN_TABCHANGED);
}

void CRibbonBarCtrl::TabClicked(int visible) {
	auto& d = *m_Data;
	if (!m_Minimized) {
		UserSelectTab(visible);
		return;
	}
	// minimized: the tab opens in a popup, and a click on it again closes it
	int t = d.visible[visible];
	bool open = std::any_of(d.popups.begin(), d.popups.end(), [](auto& p) { return !p->hidden && p->surface.group < 0; });
	if (open && t == d.selected) {
		HidePopups(0);
		return;
	}
	if (JustClosed(m_hWnd, t, -1))
		return;
	HidePopups(0);
	UserSelectTab(visible);
	OpenTabPopup();
}

int CRibbonBarCtrl::TabAt(POINT pt) const {
	auto& d = *m_Data;
	if (pt.y < 0 || pt.y >= m_TabHeight)
		return -1;
	if (::PtInRect(&d.minimizeButton, pt))
		return RibData::MinimizeButton;
	for (int i = 0; i < (int)d.tabRects.size(); i++)
		if (::PtInRect(&d.tabRects[i], pt))
			return i;
	return -1;
}

void CRibbonBarCtrl::SetHotTab(int tab) {
	auto& d = *m_Data;
	if (tab == d.hotTab)
		return;
	bool button = tab == RibData::MinimizeButton || d.hotTab == RibData::MinimizeButton;
	d.hotTab = tab;
	if (button)
		UpdateTip(m_hWnd);
	RECT row;
	GetClientRect(&row);
	row.bottom = m_TabHeight;
	InvalidateRect(&row, FALSE);
}

// ---- Contexts ------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddContext(int id, LPCWSTR name, COLORREF color) {
	auto& d = *m_Data;
	if (id <= 0 || d.FindContext(id))
		return FALSE;
	RibContext c;
	c.id = id;
	c.name = Text(name);
	c.color = color;
	d.contexts.push_back(std::move(c));
	return TRUE;
}

BOOL CRibbonBarCtrl::ShowContext(int id, bool show, bool select) {
	auto& d = *m_Data;
	auto it = std::find_if(d.contexts.begin(), d.contexts.end(), [id](const RibContext& c) { return c.id == id; });
	if (it == d.contexts.end())
		return FALSE;
	if (it->shown != show) {
		if (!show && d.Selected() && d.Selected()->context == id)
			HidePopups(0);
		it->shown = show;
		Changed(false);
	}
	if (show && select)
		for (int t = 0; t < (int)d.tabs.size(); t++)
			if (d.tabs[t].context == id && !d.tabs[t].hidden) {
				SelectTab(d.tabs[t].id);
				break;
			}
	return TRUE;
}

bool CRibbonBarCtrl::IsContextShown(int id) const {
	auto c = m_Data->FindContext(id);
	return c && c->shown;
}

// ---- Minimized -------------------------------------------------------------------------------

void CRibbonBarCtrl::SetMinimized(bool minimized) {
	if (minimized == m_Minimized)
		return;
	m_Minimized = minimized;
	HidePopups(0);
	Changed(false);
	NotifyHeight();
}

bool CRibbonBarCtrl::IsMinimized() const {
	return m_Minimized;
}

// ---- Groups -------------------------------------------------------------------------------

BOOL CRibbonBarCtrl::AddGroup(UINT tabId, const RIBGROUP* group) {
	auto& d = *m_Data;
	int t = d.FindTab(tabId);
	if (t < 0 || !group || group->id == 0 || d.FindGroup(group->id))
		return FALSE;
	HidePopups(0);
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
				HidePopups(0);
				for (auto& item : tab.groups[g].items)
					if (::IsWindow(item.window))
						::ShowWindow(item.window, SW_HIDE);
				tab.groups.erase(tab.groups.begin() + g);
				d.main.hot = d.main.pressed = {};
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
	if (item->kind == RIBIK_WINDOW && !::IsWindow(item->hwnd))
		return FALSE;
	HidePopups(0);
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
	i.window = item->kind == RIBIK_WINDOW ? item->hwnd : nullptr;
	i.windowDips = item->cxWindow;
	i.rows = item->cRows;
	i.lParam = item->lParam;
	if (i.window && IsWindow()) {
		Adopt(i.window, m_hWnd);
		::ShowWindow(i.window, SW_HIDE);   // until its group is placed
	}
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
	HidePopups(0);
	for (auto& tab : d.tabs)
		for (auto& g : tab.groups) {
			auto end = std::remove_if(g.items.begin(), g.items.end(), [id](const RibItem& i) {
				if (i.id != id || i.kind == RibKind::Separator)
					return false;
				if (::IsWindow(i.window))
					::ShowWindow(i.window, SW_HIDE);
				return true;
			});
			found = found || end != g.items.end();
			g.items.erase(end, g.items.end());
		}
	if (found) {
		d.main.hot = d.main.pressed = {};
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
	if (changed) {
		Changed(hidden);   // shown or hidden: the groups shrink differently (a window enabled: placed again)
		if (!hidden)
			NotifyItemState(id);
	}
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

BOOL CRibbonBarCtrl::SetItemMenu(UINT id, HMENU menu) {
	bool found = false;
	m_Data->ForEachItem(id, [&](int, int, int, RibItem& item) {
		item.menu = menu;
		found = true;
	});
	return found;
}

void CRibbonBarCtrl::NotifyItemState(UINT id) {
	auto& d = *m_Data;
	if (!d.acc && std::none_of(d.popups.begin(), d.popups.end(), [](auto& p) { return p->acc != nullptr; }))
		return;
	for (HWND hwnd : { m_hWnd }) {
		auto kids = AccChildren(hwnd);
		for (size_t k = 0; k < kids.size(); k++)
			if (auto item = AccItem(kids[k]); item && item->id == id)
				NotifyAccessible(hwnd, EVENT_OBJECT_STATECHANGE, (long)k + 1);
	}
	for (auto& p : d.popups)
		if (!p->hidden && p->acc) {
			auto kids = AccChildren(p->m_hWnd);
			for (size_t k = 0; k < kids.size(); k++)
				if (auto item = AccItem(kids[k]); item && item->id == id)
					NotifyAccessible(p->m_hWnd, EVENT_OBJECT_STATECHANGE, (long)k + 1);
		}
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

void CRibbonBarCtrl::Execute(int tab, int group, int item) {
	auto& d = *m_Data;
	if (tab < 0 || tab >= (int)d.tabs.size() || group < 0 || group >= (int)d.tabs[tab].groups.size() ||
		item < 0 || item >= (int)d.tabs[tab].groups[group].items.size())
		return;
	auto& i = d.tabs[tab].groups[group].items[item];
	if (i.state & RibStateDisabled)
		return;
	UINT id = i.id;
	auto kind = i.kind;
	LPARAM lParam = i.lParam;
	HidePopups(0);   // an item run from a popup closes it
	if (kind == RibKind::Toggle)
		SetItemState(id, GetItemState(id) ^ RibStateChecked, RibStateChecked);
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
	::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(id, 0), 0);
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
		Changed(false);
}

// ---- Mouse ---------------------------------------------------------------------------------------

RibHit CRibbonBarCtrl::HitSurface(const RibSurface& s, POINT pt) const {
	auto& d = *m_Data;
	RibHit hit;
	if (s.tab < 0 || s.tab >= (int)d.tabs.size() || !::PtInRect(&s.panel, pt))
		return hit;
	int button = Scale(14);
	if (s.scroll > 0 && pt.x < s.panel.left + button)
		hit.part = RibPart::ScrollLeft;
	else if (s.scroll < s.MaxScroll() && pt.x >= s.panel.right - button)
		hit.part = RibPart::ScrollRight;
	else
		hit = RibHitTest(d.tabs[s.tab].groups, s.places, { pt.x - (s.panel.left + s.originX - s.scroll), pt.y - s.panel.top }, d.m);
	return hit;
}

LRESULT CRibbonBarCtrl::SurfaceMouse(RibSurface& s, UINT msg, WPARAM, LPARAM lParam) {
	auto& d = *m_Data;
	EnsureLayout();
	POINT pt = PointOf(lParam);
	auto disabled = [&](const RibHit& hit) {
		return hit.item >= 0 && (d.tabs[s.tab].groups[hit.group].items[hit.item].state & RibStateDisabled);
	};
	auto setHot = [&](const RibHit& hit) {
		if (!s.menu && !Same(hit, s.hot)) {
			s.hot = hit;
			::InvalidateRect(s.hwnd, nullptr, FALSE);
		}
	};
	auto setTip = [&](RibHit hit) {
		if (hit.part == RibPart::Arrow)
			hit.part = RibPart::Item;
		if (!Same(hit, s.tip)) {
			s.tip = hit;
			UpdateTip(s.hwnd);
		}
	};
	switch (msg) {
		case WM_MOUSEMOVE: {
			if (!s.tracking) {
				TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, s.hwnd };
				s.tracking = ::TrackMouseEvent(&tme) != FALSE;
			}
			auto hit = HitSurface(s, pt);
			setTip(hit);
			if (disabled(hit))
				hit = { RibPart::None, hit.group, -1 };   // a disabled item isn't hot
			if (::GetCapture() == s.hwnd && !Same(hit, s.pressed))
				hit = {};    // pressed: hot only while over it
			setHot(hit);
			return 0;
		}
		case WM_MOUSELEAVE:
			s.tracking = false;
			setTip({});
			if (::GetCapture() != s.hwnd)
				setHot({});
			return 0;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK: {
			ExitKeyMode(true);
			auto hit = HitSurface(s, pt);
			if (disabled(hit))
				return 0;
			switch (hit.part) {
				case RibPart::ScrollLeft:
				case RibPart::ScrollRight: {
					int step = std::max(1, s.Avail() * 2 / 3);
					s.scroll = std::clamp(s.scroll + (hit.part == RibPart::ScrollLeft ? -step : step), 0, s.MaxScroll());
					s.hot = {};
					::InvalidateRect(s.hwnd, nullptr, FALSE);
					PlaceWindows();
					return 0;
				}
				case RibPart::Group:
					if (!JustClosed(s.hwnd, s.tab, hit.group))
						OpenGroupPopup(s, hit.group);
					return 0;
				case RibPart::Arrow:
					DropDown(s, hit.group, hit.item);
					return 0;
				case RibPart::Item:
					if (d.tabs[s.tab].groups[hit.group].items[hit.item].kind == RibKind::DropDown) {
						DropDown(s, hit.group, hit.item);
						return 0;
					}
					break;
				case RibPart::Launcher:
					break;
				default:
					return 0;
			}
			s.pressed = s.hot = hit;
			::SetCapture(s.hwnd);
			::InvalidateRect(s.hwnd, nullptr, FALSE);
			return 0;
		}
		case WM_LBUTTONUP: {
			if (::GetCapture() != s.hwnd)
				return 0;
			auto pressed = s.pressed;
			auto hit = HitSurface(s, pt);
			::ReleaseCapture();
			setHot(hit);
			if (!Same(hit, pressed))
				return 0;
			if (hit.part == RibPart::Item)
				Execute(s.tab, hit.group, hit.item);
			else if (hit.part == RibPart::Launcher)
				RunLauncher(s, hit.group);
			return 0;
		}
		case WM_CAPTURECHANGED:
			s.pressed = {};
			::InvalidateRect(s.hwnd, nullptr, FALSE);
			return 0;
	}
	return 0;
}

void CRibbonBarCtrl::RunLauncher(const RibSurface& s, int group) {
	auto& g = m_Data->tabs[s.tab].groups[group];
	UINT id = g.launcherId ? g.launcherId : g.id;
	HidePopups(0);
	::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

LRESULT CRibbonBarCtrl::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	EnsureLayout();
	POINT pt = PointOf(lParam);
	bool captured = ::GetCapture() == m_hWnd;
	SetHotTab(captured ? -1 : TabAt(pt));
	if (!d.qatMenu) {
		int q = QatAt(pt);
		SetHotQat(captured ? (d.pressedQat >= 0 && q == d.pressedQat ? q : -1) : q);   // pressed: hot only while over it
	}
	return SurfaceMouse(d.main, uMsg, wParam, lParam);
}

LRESULT CRibbonBarCtrl::OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	SetHotTab(-1);
	if (!m_Data->qatMenu && ::GetCapture() != m_hWnd)
		SetHotQat(-1);
	return SurfaceMouse(m_Data->main, uMsg, wParam, lParam);
}

LRESULT CRibbonBarCtrl::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	EnsureLayout();
	ExitKeyMode(true);    // a click ends the keyboard's
	int q = QatAt(PointOf(lParam));
	if (q == RibData::Customize) {
		CustomizeMenu();
		return 0;
	}
	if (q >= 0) {
		QatMouseDown(q);
		return 0;
	}
	int tab = TabAt(PointOf(lParam));
	if (tab == RibData::MinimizeButton) {
		SetMinimized(!m_Minimized);
		return 0;
	}
	if (tab >= 0) {
		TabClicked(tab);
		return 0;
	}
	return SurfaceMouse(m_Data->main, uMsg, wParam, lParam);
}

LRESULT CRibbonBarCtrl::OnLButtonDblClk(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	EnsureLayout();
	int tab = TabAt(PointOf(lParam));
	if (tab >= 0 && !(GetStyle() & RIBS_NOMINIMIZE)) {
		SetMinimized(!m_Minimized);
		return 0;
	}
	return OnLButtonDown(WM_LBUTTONDOWN, wParam, lParam, bHandled);
}

LRESULT CRibbonBarCtrl::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.pressedQat >= 0 && ::GetCapture() == m_hWnd) {
		int q = d.pressedQat;
		::ReleaseCapture();
		if (QatAt(PointOf(lParam)) == q)
			RunQat(q);
		return 0;
	}
	return SurfaceMouse(d.main, uMsg, wParam, lParam);
}

LRESULT CRibbonBarCtrl::OnCaptureChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.pressedQat >= 0) {
		d.pressedQat = -1;
		InvalidateTabRow();
	}
	return SurfaceMouse(d.main, uMsg, wParam, lParam);
}

LRESULT CRibbonBarCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
	// the wheel over the ribbon goes through the tabs
	auto& d = *m_Data;
	EnsureLayout();
	auto it = std::find(d.visible.begin(), d.visible.end(), d.selected);
	if (it == d.visible.end() || m_Minimized)
		return 0;
	int v = (int)(it - d.visible.begin()) + (GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
	UserSelectTab(std::clamp(v, 0, (int)d.visible.size() - 1));
	return 0;
}

// ---- Drop-downs --------------------------------------------------------------------------------

void CRibbonBarCtrl::DropDown(RibSurface& s, int group, int item) {
	auto& d = *m_Data;
	auto& i = d.tabs[s.tab].groups[group].items[item];
	RECT rc = ItemRect(s, group, item);
	// pressed while the menu is open
	s.pressed = s.hot = { i.kind == RibKind::Split ? RibPart::Arrow : RibPart::Item, group, item };
	s.menu = true;
	::InvalidateRect(s.hwnd, nullptr, FALSE);
	::UpdateWindow(s.hwnd);
	UINT cmd = ItemMenu(i.id, i.menu, i.lParam, s.hwnd, rc);
	s.menu = false;
	s.pressed = s.hot = {};
	::InvalidateRect(s.hwnd, nullptr, FALSE);
	if (cmd) {
		HidePopups(0);
		::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
	}
}

UINT CRibbonBarCtrl::ItemMenu(UINT id, HMENU menu, LPARAM lParam, HWND hwnd, const RECT& rc) {
	RECT screen = rc;
	::MapWindowPoints(hwnd, nullptr, (POINT*)&screen, 2);
	NMRIBDROPDOWN nm{};
	nm.id = id;
	nm.hMenu = menu;
	nm.rcItem = screen;
	nm.lParam = lParam;
	if (FireNotify(&nm.hdr, RIBN_DROPDOWN) || !nm.hMenu)
		return 0;
	return TrackMenu(nm.hMenu, hwnd, rc, { screen.left, screen.bottom });
}

UINT CRibbonBarCtrl::TrackMenu(HMENU menu, HWND hwnd, const RECT& rc, POINT screen) {
	RECT button = rc;
	::MapWindowPoints(hwnd, nullptr, (POINT*)&button, 2);
	TPMPARAMS tpm{ sizeof(tpm), button };
	bool under = !::IsRectEmpty(&rc);
	// the owner gets the menu's messages (WM_INITMENUPOPUP: an app's CUpdateUI)
	UINT cmd = (UINT)::TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON | (under ? TPM_VERTICAL : 0),
		screen.x, screen.y, Owner(), under ? &tpm : nullptr);
	// the click that closed the menu over its button isn't one that opens it again
	MSG msg;
	if (under && ::PeekMessage(&msg, hwnd, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_NOREMOVE)) {
		POINT p = PointOf(msg.lParam);
		if (::PtInRect(&rc, p))
			::PeekMessage(&msg, hwnd, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE);
	}
	return cmd;
}

// ---- Popups ------------------------------------------------------------------------------------

bool CRibbonBarCtrl::JustClosed(HWND from, int tab, int group) const {
	auto& d = *m_Data;
	return d.closedFrom == from && d.closedTab == tab && d.closedGroup == group && ::GetTickCount64() - d.closedTick < 300;
}

void CRibbonBarCtrl::OpenGroupPopup(RibSurface& from, int group) {
	auto& d = *m_Data;
	// those open over the one it opens from close
	if (from.hwnd == m_hWnd)
		HidePopups(0);
	else
		for (size_t k = 0; k < d.popups.size(); k++)
			if (d.popups[k]->m_hWnd == from.hwnd) {
				HidePopups(k + 1);
				break;
			}
	auto popup = std::make_unique<RibPopup>(this);
	auto& s = popup->surface;
	s.tab = from.tab;
	s.group = group;
	LayoutSurface(s);
	// under the folded button, below the strip it's in
	RECT button = RibCollapsedRect(from.places[group].width, d.m);
	::OffsetRect(&button, from.GroupX(group), from.panel.top);
	POINT pt{ button.left, from.panel.bottom };
	::ClientToScreen(from.hwnd, &pt);
	RECT rc{ pt.x, pt.y, pt.x + s.total + 2, pt.y + d.m.BodyHeight() + 2 };
	popup->opener = from.hwnd;
	from.opened = group;
	::InvalidateRect(from.hwnd, nullptr, FALSE);
	ShowPopup(std::move(popup), from.hwnd == m_hWnd ? ::GetAncestor(m_hWnd, GA_ROOT) : from.hwnd, rc);
}

void CRibbonBarCtrl::OpenTabPopup() {
	auto& d = *m_Data;
	if (d.selected < 0)
		return;
	auto popup = std::make_unique<RibPopup>(this);
	auto& s = popup->surface;
	s.tab = d.selected;
	s.originX = Scale(4) - 1;
	RECT rc;
	GetClientRect(&rc);
	rc.top = m_TabHeight;
	rc.bottom = rc.top + d.m.BodyHeight() + 2;
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&rc, 2);
	popup->opener = m_hWnd;
	ShowPopup(std::move(popup), ::GetAncestor(m_hWnd, GA_ROOT), rc);
}

RibPopup* CRibbonBarCtrl::ShowPopup(std::unique_ptr<RibPopup> popup, HWND owner, RECT rc) {
	auto& d = *m_Data;
	DestroyHidden();
	// on the monitor
	MONITORINFO mi{ sizeof(mi) };
	if (::GetMonitorInfoW(::MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST), &mi)) {
		auto& work = mi.rcWork;
		if (rc.right > work.right)
			::OffsetRect(&rc, work.right - rc.right, 0);
		if (rc.left < work.left)
			::OffsetRect(&rc, work.left - rc.left, 0);
		if (rc.bottom > work.bottom)
			::OffsetRect(&rc, 0, work.bottom - rc.bottom);
	}
	if (!popup->Create(owner, rc, nullptr))
		return nullptr;
	auto& s = popup->surface;
	s.hwnd = popup->m_hWnd;
	s.panel = { 1, 1, rc.right - rc.left - 1, rc.bottom - rc.top - 1 };
	AddTipTool(s.hwnd);
	LayoutSurface(s);
	auto raw = popup.get();
	d.popups.push_back(std::move(popup));
	PlaceWindows();
	raw->ShowWindow(SW_SHOW);    // active: a hosted window in it can have the focus
	return raw;
}

void CRibbonBarCtrl::HidePopups(size_t from) {
	auto& d = *m_Data;
	// all marked first: hiding the active one activates another, whose handler comes back here
	std::vector<RibPopup*> closing;
	for (size_t k = from; k < d.popups.size(); k++)
		if (!d.popups[k]->hidden) {
			d.popups[k]->hidden = true;
			closing.push_back(d.popups[k].get());
		}
	if (d.keyMode != RibKeyMode::Off && d.keyLevel)
		for (auto p : closing)
			if (p->m_hWnd == d.keyLevel) {
				ExitKeyMode(false);
				break;
			}
	for (auto it = closing.rbegin(); it != closing.rend(); ++it) {
		auto p = *it;
		// its hosted windows back to the ribbon first: destroying the popup would destroy them
		for (auto& tab : d.tabs)
			for (auto& g : tab.groups)
				for (auto& item : g.items)
					if (item.window && ::GetParent(item.window) == p->m_hWnd) {
						::ShowWindow(item.window, SW_HIDE);
						Adopt(item.window, m_hWnd);
					}
		// the button it opened from isn't pressed now
		auto release = [&](RibSurface& s) {
			if (s.hwnd == p->opener && s.opened >= 0) {
				s.opened = -1;
				::InvalidateRect(s.hwnd, nullptr, FALSE);
			}
		};
		release(d.main);
		for (auto& q : d.popups)
			release(q->surface);
		if (p->IsWindow())
			p->ShowWindow(SW_HIDE);
	}
	if (!closing.empty() && m_hTip)
		::SendMessage(m_hTip, TTM_POP, 0, 0);
	if (!closing.empty() && IsWindow()) {
		PlaceWindows();
		PostMessage(WM_RIB_CLOSEPOPUPS);
	}
}

void CRibbonBarCtrl::DestroyHidden() {
	auto& popups = m_Data->popups;
	for (auto& p : popups)
		if (p->hidden && p->IsWindow()) {
			RemoveTipTool(p->m_hWnd);
			p->DestroyWindow();
		}
	popups.erase(std::remove_if(popups.begin(), popups.end(), [](auto& p) { return p->hidden; }), popups.end());
}

void CRibbonBarCtrl::ClosePopups() {
	HidePopups(0);
}

LRESULT CRibbonBarCtrl::OnClosePopups(UINT, WPARAM, LPARAM, BOOL&) {
	DestroyHidden();
	return 0;
}

// ---- The popup window --------------------------------------------------------------------------

LRESULT RibPopup::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	m_Ribbon->SendUpdateUI(true);
	m_Ribbon->EnsureLayout();
	auto c = m_Ribbon->PaintColors();
	RECT rc;
	GetClientRect(&rc);
	WTL::CBrush line;
	line.CreateSolidBrush(c.crLine);
	auto old = mdc.SelectFont(m_Ribbon->m_hFont);
	mdc.SetBkMode(TRANSPARENT);
	m_Ribbon->PaintSurface(mdc.m_hDC, surface, c);
	m_Ribbon->PaintKeys(mdc.m_hDC, m_hWnd, c);
	mdc.FrameRect(&rc, line);
	mdc.SelectFont(old);
	return 0;
}

LRESULT RibPopup::OnMouse(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (uMsg != WM_MOUSEMOVE && uMsg != WM_LBUTTONDOWN && uMsg != WM_LBUTTONUP && uMsg != WM_LBUTTONDBLCLK &&
		uMsg != WM_MOUSELEAVE && uMsg != WM_CAPTURECHANGED)
		return 0;
	return m_Ribbon->SurfaceMouse(surface, uMsg, wParam, lParam);
}

LRESULT RibPopup::OnActivate(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	bHandled = FALSE;
	HWND root = ::GetAncestor(m_Ribbon->m_hWnd, GA_ROOT);
	if (LOWORD(wParam) != WA_INACTIVE) {
		::SendMessage(root, WM_NCACTIVATE, TRUE, 0);   // the window's caption stays active
		return 0;
	}
	if (hidden)
		return 0;
	auto& d = *m_Ribbon->m_Data;
	HWND other = (HWND)lParam;
	for (auto& p : d.popups)
		if (!p->hidden && p->m_hWnd == other)
			return 0;   // one opened from this one
	// the activation goes elsewhere: they all close. If a click on the button that opened them did it, that click
	// mustn't open them again.
	for (auto& p : d.popups)
		if (!p->hidden) {
			d.closedFrom = p->opener;
			d.closedTab = p->surface.tab;
			d.closedGroup = p->surface.group;
			d.closedTick = ::GetTickCount64();
			break;
		}
	if (other != root)
		::SendMessage(root, WM_NCACTIVATE, FALSE, 0);
	m_Ribbon->HidePopups(0);
	return 0;
}

LRESULT RibPopup::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_Ribbon->KeyDown(uMsg, (UINT)wParam, lParam))
		return 0;
	if (uMsg != WM_KEYDOWN || wParam != VK_ESCAPE) {
		bHandled = FALSE;
		return 0;
	}
	// this one and those above it
	auto& popups = m_Ribbon->m_Data->popups;
	for (size_t k = 0; k < popups.size(); k++)
		if (popups[k].get() == this) {
			m_Ribbon->HidePopups(k);
			break;
		}
	return 0;
}

LRESULT RibPopup::OnContextMenu(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_hWnd) {
		bHandled = FALSE;    // a hosted window's
		return 0;
	}
	m_Ribbon->ContextMenu(m_hWnd, lParam);
	return 0;
}

LRESULT RibPopup::OnForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (uMsg == WM_NOTIFY && m_Ribbon->TipNotify((NMHDR*)lParam))
		return 0;
	return ::SendMessage(m_Ribbon->GetParent(), uMsg, wParam, lParam);
}

// ---- Notifications -----------------------------------------------------------------------

LRESULT CRibbonBarCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}
