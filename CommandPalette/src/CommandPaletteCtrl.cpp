#include "CommandPalette.h"
#include "CommandPaletteList.h"
#include "CommandPaletteModel.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

// ---- Helpers -----------------------------------------------------------------------

namespace {
	enum : UINT { BoxId = 1 };

	COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
		dc.SetBkColor(color);
		dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	std::wstring Text(LPCWSTR s) {
		return s ? std::wstring(s) : std::wstring();
	}
}

// ---- Construction ------------------------------------------------------------------

CCommandPaletteCtrl::CCommandPaletteCtrl() : m_Recent(std::make_unique<CmpRecent>()), m_Search(std::make_unique<CmpSearch>()), m_Box(this, 1),
	m_List(std::make_unique<CmpListWindow>(this)) {
}

CCommandPaletteCtrl::~CCommandPaletteCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CCommandPaletteCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_CLIPCHILDREN);
	ModifyStyleEx(0, WS_EX_CONTROLPARENT);
	UpdateFonts();
	UpdateMetrics();
	// a real "Edit" (CContainedWindow::Create would make an "ATL:Edit" superclass, which the system's accessibility
	// for edits doesn't know), subclassed
	HWND box = ::CreateWindowExW(0, WC_EDITW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
		m_hWnd, (HMENU)(UINT_PTR)BoxId, ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
	m_Box.SubclassWindow(box);
	m_Box.SetFont(m_hFont);
	m_Box.SetCueBannerText(m_Placeholder, TRUE);
	m_List->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPSIBLINGS);
	UpdateScrollBarTheme();
	NameAccessible();
	Layout();
	Filter();
	return 0;
}

LRESULT CCommandPaletteCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	DisconnectAccessible();
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through memory DCs
}

LRESULT CCommandPaletteCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Box.IsWindow())
		m_Box.SetFocus();
	return 0;
}

HWND CCommandPaletteCtrl::GetSearchBox() const {
	return m_Box.m_hWnd;
}

void CCommandPaletteCtrl::SetOwner(HWND owner) {
	m_Owner = owner;
}

// ---- Metrics and fonts -------------------------------------------------------------

int CCommandPaletteCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CCommandPaletteCtrl::UpdateFonts() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	lf.lfWeight = FW_BOLD;
	if (!m_BoldFont.IsNull())
		m_BoldFont.DeleteObject();
	m_BoldFont.CreateFontIndirect(&lf);
	if (m_Box.IsWindow())
		m_Box.SetFont(m_hFont);
}

void CCommandPaletteCtrl::UpdateMetrics() {
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);
	m_TextHeight = tm.tmHeight;
	m_Pad = Scale(6);
	m_IconSize = ::GetSystemMetricsForDpi(SM_CXSMICON, m_Dpi);
	m_RowHeight = std::max(m_TextHeight, m_IconSize) + Scale(8);
	m_BoxHeight = m_TextHeight + Scale(12);
}

LRESULT CCommandPaletteCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	Layout();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CCommandPaletteCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CCommandPaletteCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	Layout();
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {   // the message font
		UpdateFonts();
		UpdateMetrics();
		Layout();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UpdateScrollBarTheme();
	Invalidate();
	if (m_List->IsWindow())
		m_List->Invalidate();
	if (m_Box.IsWindow())
		m_Box.Invalidate();
	bHandled = FALSE;
	return 0;
}

int CCommandPaletteCtrl::GetIdealHeight(int rows) const {
	return m_Pad + m_BoxHeight + m_Pad / 2 + std::max(rows, 1) * m_RowHeight + m_Pad / 2;
}

// ---- Layout ------------------------------------------------------------------------------

RECT CCommandPaletteCtrl::GetBoxRect() const {
	RECT rc;
	GetClientRect(&rc);
	return { m_Pad, m_Pad, std::max((int)rc.right - m_Pad, m_Pad), m_Pad + m_BoxHeight };
}

void CCommandPaletteCtrl::Layout() {
	if (!IsWindow() || !m_Box.IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	auto box = GetBoxRect();
	// the edit inside the frame, its text centered
	int inset = Scale(8), height = m_TextHeight + Scale(2);
	int top = box.top + (box.bottom - box.top - height) / 2;
	m_Box.SetWindowPos(nullptr, box.left + inset, top, std::max(0, (int)(box.right - box.left) - 2 * inset), height, SWP_NOZORDER | SWP_NOACTIVATE);
	int listTop = box.bottom + m_Pad / 2;
	m_List->SetWindowPos(nullptr, 0, listTop, rc.right, std::max(0, (int)rc.bottom - listTop - m_Pad / 2), SWP_NOZORDER | SWP_NOACTIVATE);
	UpdateScrollBar();
	Invalidate();
	m_List->Invalidate();
}

LRESULT CCommandPaletteCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateScrollBar();
	EnsureVisible(m_Selected);
	m_List->Invalidate();
	return 0;
}

int CCommandPaletteCtrl::PageRows() const {
	RECT rc{};
	if (m_List->IsWindow())
		m_List->GetClientRect(&rc);
	return std::max(1, (int)rc.bottom / m_RowHeight);
}

void CCommandPaletteCtrl::UpdateScrollBar() {
	if (!m_List->IsWindow())
		return;
	int count = (int)Results().size(), page = PageRows();
	m_Top = std::clamp(m_Top, 0, std::max(0, count - page));
	SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL };
	si.nMax = std::max(0, count - 1);
	si.nPage = page;
	si.nPos = m_Top;
	m_List->SetScrollInfo(SB_VERT, &si, TRUE);
	// no scroll bar when everything fits
	m_List->ShowScrollBar(SB_VERT, count > page);
}

void CCommandPaletteCtrl::ScrollTo(int top) {
	int count = (int)Results().size();
	top = std::clamp(top, 0, std::max(0, count - PageRows()));
	if (top == m_Top)
		return;
	m_Top = top;
	UpdateScrollBar();
	m_List->Invalidate();
}

void CCommandPaletteCtrl::EnsureVisible(int row) {
	if (row < 0)
		return;
	int page = PageRows();
	if (row < m_Top)
		ScrollTo(row);
	else if (row >= m_Top + page)
		ScrollTo(row - page + 1);
}

LRESULT CCommandPaletteCtrl::OnListVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	int top = m_Top, page = PageRows();
	switch (LOWORD(wParam)) {
		case SB_LINEUP: top--; break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP: top -= page; break;
		case SB_PAGEDOWN: top += page; break;
		case SB_TOP: top = 0; break;
		case SB_BOTTOM: top = (int)Results().size(); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			m_List->GetScrollInfo(SB_VERT, &si);
			top = si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollTo(top);
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	if (lines == WHEEL_PAGESCROLL)
		lines = PageRows();
	m_WheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
	int steps = m_WheelRemainder / WHEEL_DELTA;
	m_WheelRemainder -= steps * WHEEL_DELTA;
	ScrollTo(m_Top - steps * (int)lines);
	return 0;
}

// ---- Commands -------------------------------------------------------------------------

int CCommandPaletteCtrl::FindItem(UINT id) const {
	for (size_t i = 0; i < m_Items.size(); i++)
		if (m_Items[i].id == id)
			return (int)i;
	return -1;
}

BOOL CCommandPaletteCtrl::AddCommand(const CMDPITEM* item) {
	if (!item || !item->pszName || !*item->pszName)
		return FALSE;
	CmpItem cmd;
	cmd.id = item->id;
	cmd.name = item->pszName;
	cmd.category = Text(item->pszCategory);
	cmd.shortcut = Text(item->pszShortcut);
	cmd.image = item->iImage;
	cmd.state = item->state;
	cmd.lParam = item->lParam;
	cmd.UpdateLabel();
	int index = FindItem(item->id);
	if (index >= 0)
		m_Items[index] = std::move(cmd);
	else
		m_Items.push_back(std::move(cmd));
	Filter();
	return TRUE;
}

BOOL CCommandPaletteCtrl::AddCommand(UINT id, LPCWSTR name, LPCWSTR category, LPCWSTR shortcut, int image) {
	CMDPITEM item;
	item.id = id;
	item.pszName = name;
	item.pszCategory = category;
	item.pszShortcut = shortcut;
	item.iImage = image;
	return AddCommand(&item);
}

BOOL CCommandPaletteCtrl::RemoveCommand(UINT id) {
	int index = FindItem(id);
	if (index < 0)
		return FALSE;
	m_Items.erase(m_Items.begin() + index);
	Filter();
	return TRUE;
}

void CCommandPaletteCtrl::ClearCommands() {
	m_Items.clear();
	m_Menus.clear();
	Filter();
}

int CCommandPaletteCtrl::GetCommandCount() const {
	return (int)m_Items.size();
}

BOOL CCommandPaletteCtrl::SetCommandState(UINT id, UINT state) {
	int index = FindItem(id);
	if (index < 0)
		return FALSE;
	if (m_Items[index].state != state) {
		m_Items[index].state = state;
		Filter();
	}
	return TRUE;
}

UINT CCommandPaletteCtrl::GetCommandState(UINT id) const {
	int index = FindItem(id);
	return index < 0 ? (UINT)-1 : m_Items[index].state;
}

void CCommandPaletteCtrl::ReadMenu(HMENU menu, std::vector<std::wstring>& path, int source) {
	int count = ::GetMenuItemCount(menu);
	for (int i = 0; i < count; i++) {
		MENUITEMINFOW mii{ sizeof(mii) };
		mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING;
		if (!::GetMenuItemInfoW(menu, i, TRUE, &mii) || (mii.fType & MFT_SEPARATOR))
			continue;
		std::wstring text(mii.cch, L'\0');
		if (mii.cch) {
			mii.cch++;
			mii.dwTypeData = text.data();
			::GetMenuItemInfoW(menu, i, TRUE, &mii);
		}
		std::wstring name, shortcut;
		CmpParseMenuText(text, name, shortcut);
		if (name.empty())
			continue;   // owner drawn, or no text
		if (mii.hSubMenu) {
			path.push_back(name);
			ReadMenu(mii.hSubMenu, path, source);
			path.pop_back();
			continue;
		}
		if (mii.wID == 0)
			continue;
		CmpItem cmd;
		cmd.id = mii.wID;
		cmd.name = std::move(name);
		cmd.category = CmpJoinPath(path);
		cmd.shortcut = std::move(shortcut);
		cmd.state = ((mii.fState & MFS_DISABLED) ? CMDPIS_DISABLED : 0) | ((mii.fState & MFS_CHECKED) ? CMDPIS_CHECKED : 0);
		cmd.source = source;
		cmd.UpdateLabel();
		int index = FindItem(cmd.id);
		if (index >= 0) {
			cmd.image = m_Items[index].image;     // the app's image and data stay
			cmd.lParam = m_Items[index].lParam;
			m_Items[index] = std::move(cmd);
		}
		else {
			m_Items.push_back(std::move(cmd));
		}
	}
}

int CCommandPaletteCtrl::AddFromMenu(HMENU menu, LPCWSTR category) {
	if (!menu || !::IsMenu(menu))
		return 0;
	size_t before = m_Items.size();
	m_Menus.push_back({ menu, Text(category) });
	std::vector<std::wstring> path;
	if (category && *category)
		path.push_back(category);
	ReadMenu(menu, path, (int)m_Menus.size() - 1);
	Filter();
	return (int)(m_Items.size() - before);
}

void CCommandPaletteCtrl::InitMenus(HWND window, HMENU menu) {
	int count = ::GetMenuItemCount(menu);
	for (int i = 0; i < count; i++) {
		if (HMENU sub = ::GetSubMenu(menu, i)) {
			::SendMessage(window, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(i, FALSE));
			InitMenus(window, sub);
		}
	}
}

void CCommandPaletteCtrl::RefreshMenus(HWND initMenus) {
	if (initMenus) {
		for (auto& source : m_Menus) {
			if (!::IsMenu(source.menu))
				continue;
			// a popup menu given itself gets it too; a menu bar doesn't (it isn't a popup)
			if (::GetMenu(initMenus) != source.menu)
				::SendMessage(initMenus, WM_INITMENUPOPUP, (WPARAM)source.menu, 0);
			InitMenus(initMenus, source.menu);
		}
	}
	// the menus' commands as they are now: some may have gone, others come, states changed. Each menu is read into a
	// list of its own, then put back where its commands were, so the order stays.
	UINT selected = GetSelectedId();
	auto old = std::move(m_Items);
	std::vector<std::vector<CmpItem>> fresh(m_Menus.size());
	for (size_t i = 0; i < m_Menus.size(); i++) {
		m_Items.clear();
		if (::IsMenu(m_Menus[i].menu)) {   // else the menu is gone, and so are its commands
			std::vector<std::wstring> path;
			if (!m_Menus[i].category.empty())
				path.push_back(m_Menus[i].category);
			ReadMenu(m_Menus[i].menu, path, (int)i);
		}
		// the app's images and data stay
		for (auto& item : m_Items) {
			auto it = std::find_if(old.begin(), old.end(), [&](const CmpItem& o) { return o.id == item.id && o.source == (int)i; });
			if (it != old.end()) {
				item.image = it->image;
				item.lParam = it->lParam;
			}
		}
		fresh[i] = std::move(m_Items);
	}
	m_Items.clear();
	std::vector<bool> placed(m_Menus.size());
	auto place = [&](size_t source) {
		placed[source] = true;
		for (auto& cmd : fresh[source])
			m_Items.push_back(std::move(cmd));
	};
	for (auto& item : old) {
		if (item.source < 0)
			m_Items.push_back(std::move(item));
		else if (!placed[item.source])
			place(item.source);
	}
	for (size_t i = 0; i < m_Menus.size(); i++)   // menus that had no commands before
		if (!placed[i])
			place(i);
	// as when the menu was added: a menu's command replaces the app's with the same id
	std::vector<UINT> menuIds;
	for (auto& item : m_Items)
		if (item.source >= 0)
			menuIds.push_back(item.id);
	std::sort(menuIds.begin(), menuIds.end());
	m_Items.erase(std::remove_if(m_Items.begin(), m_Items.end(), [&](const CmpItem& item) {
		return item.source < 0 && std::binary_search(menuIds.begin(), menuIds.end(), item.id);
	}), m_Items.end());
	Filter();
	for (int row = 0; row < (int)Results().size(); row++) {
		if (GetResultId(row) == selected) {
			Select(row, false);
			break;
		}
	}
}

void CCommandPaletteCtrl::SetImageList(HIMAGELIST images) {
	m_Images = images;
	if (m_List->IsWindow())
		m_List->Invalidate();
}

HIMAGELIST CCommandPaletteCtrl::GetImageList() const {
	return m_Images;
}

// ---- The active list ---------------------------------------------------------------------

namespace {
	enum : UINT_PTR { SearchTimer = 1 };
	constexpr DWORD SearchSlice = 25;   // ms of matching at a time: typing and painting go on in between
}

std::vector<CmpResult>& CCommandPaletteCtrl::Results() const {
	return m_Search->Results();
}

CmpProvider* CCommandPaletteCtrl::FindProvider(int id) const {
	for (auto& p : m_Providers)
		if (p->id == id)
			return p.get();
	return nullptr;
}

const CmpItem* CCommandPaletteCtrl::ItemOf(size_t index) {
	if (m_Mode == 0)
		return index < m_Items.size() ? &m_Items[index] : nullptr;
	if (m_Mode < 0)
		return index < m_Help.size() ? &m_Help[index] : nullptr;
	auto p = FindProvider(m_Mode);
	if (!p || index >= p->count)
		return nullptr;
	if (!p->fetched[index]) {
		// asked for once, kept until the provider changes
		NMCMDPGETITEM nm{};
		nm.provider = p->id;
		nm.index = index;
		nm.iImage = -1;
		if (p->getItem)
			p->getItem(p->context, &nm);
		else
			FireNotify(&nm.hdr, CMDPN_GETITEM);
		auto& item = p->items[index];
		item.name = Text(nm.pszName);
		item.category = Text(nm.pszCategory);
		item.shortcut = Text(nm.pszShortcut);
		item.image = nm.iImage;
		item.lParam = nm.lParam;
		item.UpdateLabel();
		p->fetched[index] = 1;
	}
	return &p->items[index];
}

const CmpItem& CCommandPaletteCtrl::RowItem(int row) const {
	size_t index = Results()[row].index;
	if (m_Mode == 0)
		return m_Items[index];
	if (m_Mode < 0)
		return m_Help[index];
	return FindProvider(m_Mode)->items[index];   // asked for when it was matched
}

void CCommandPaletteCtrl::BuildHelp() {
	m_Help.clear();
	CmpItem commands;
	commands.name = L"Commands";
	commands.UpdateLabel();
	m_Help.push_back(commands);
	for (auto& p : m_Providers) {
		CmpItem item;
		item.name = p->name.empty() ? p->prefix : p->name;
		item.shortcut = p->prefix;
		item.lParam = p->id;
		item.UpdateLabel();
		m_Help.push_back(std::move(item));
	}
}

void CCommandPaletteCtrl::Filter() {
	ATL::CString text;
	if (m_Box.IsWindow())
		m_Box.GetWindowText(text);
	std::wstring_view all(text, text.GetLength());
	// the old results go before the list changes: they index the old one (and CMDPN_QUERY may let the app paint)
	UINT selected = GetSelectedId();
	m_Search->Start({}, 0);
	m_Selected = -1;

	// the list the prefix picks
	const DWORD style = IsWindow() ? GetStyle() : 0;
	std::vector<std::wstring> prefixes;
	for (auto& p : m_Providers)
		prefixes.push_back(p->prefix);
	const bool help = !(style & CMDPS_NOHELP) && !m_Providers.empty();
	if (help)
		prefixes.push_back(L"?");
	std::wstring_view rest;
	int picked = CmpPickProvider(prefixes, all, rest);
	m_Mode = picked < 0 ? 0 : picked == (int)m_Providers.size() ? -1 : m_Providers[picked]->id;

	size_t count = m_Items.size();
	bool filter = true;
	if (m_Mode < 0) {
		BuildHelp();
		count = m_Help.size();
	}
	else if (m_Mode > 0) {
		auto p = FindProvider(m_Mode);
		// the app hears the text (a provider that isn't matched sets its items for it now)
		if (m_QueryProvider != m_Mode || m_QueryText != rest) {
			m_QueryProvider = m_Mode;
			m_QueryText = rest;
			NMCMDPQUERY nm{};
			nm.provider = m_Mode;
			nm.pszText = m_QueryText.c_str();
			m_InQuery = true;
			FireNotify(&nm.hdr, CMDPN_QUERY);
			m_InQuery = false;
			p = FindProvider(m_Mode);
		}
		count = p ? p->count : 0;
		filter = p && !(p->flags & CMDPPF_NOFILTER);
	}
	else {
		m_QueryProvider = 0;
	}

	m_Search->Start(rest, count, filter);
	m_Top = 0;
	m_Hot = -1;
	m_Selected = -1;
	m_UserSelected = false;
	Search(SearchSlice);
	// typing starts again at the best match
	if (m_Mode == 0 && selected != GetResultId(0) && m_Selected >= 0) {
		NotifyItem(CMDPN_SELCHANGED, m_Selected);
	}
	if (IsWindow()) {
		if (m_Search->Running())
			SetTimer(SearchTimer, USER_TIMER_MINIMUM);
		else
			KillTimer(SearchTimer);
	}
}

void CCommandPaletteCtrl::Search(DWORD ms) {
	const DWORD style = IsWindow() ? GetStyle() : 0;
	const bool hideDisabled = (style & CMDPS_HIDEDISABLED) != 0;
	const bool useRecent = m_Mode == 0 && (!(style & CMDPS_NORECENT) || m_Search->Filtering());
	auto label = [&](size_t i) -> std::wstring_view {
		auto item = ItemOf(i);
		if (!item || (item->state & CMDPIS_HIDDEN) || (hideDisabled && (item->state & CMDPIS_DISABLED)))
			return {};
		return item->label;
	};
	auto recent = [&](size_t i) { return useRecent ? m_Recent->Rank(m_Items[i].id) : -1; };

	// the item the user went to stays selected as more results come in
	bool keep = m_UserSelected && m_Selected >= 0;
	size_t keepIndex = keep ? Results()[m_Selected].index : 0;

	ULONGLONG start = ::GetTickCount64();
	while (!m_Search->Step(256, label, recent) && ::GetTickCount64() - start < ms)
		;
	m_Search->Sort();

	auto& results = Results();
	int row = results.empty() ? -1 : 0;
	if (keep) {
		auto it = std::find_if(results.begin(), results.end(), [&](const CmpResult& r) { return r.index == keepIndex; });
		if (it != results.end())
			row = (int)(it - results.begin());
	}
	bool moved = row != m_Selected;
	m_Selected = row;
	EnsureVisible(row);
	UpdateScrollBar();
	if (m_List->IsWindow())
		m_List->Invalidate();
	NotifyResultsAccessible();
	if (moved)
		NotifySelectionAccessible();
	if (IsWindow()) {
		// the progress line under the box
		auto box = GetBoxRect();
		box.top = box.bottom - Scale(1);
		box.bottom += Scale(3);
		InvalidateRect(&box, FALSE);
		NMHDR hdr{};
		FireNotify(&hdr, CMDPN_RESULTSCHANGED);
	}
}

LRESULT CCommandPaletteCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != SearchTimer) {
		bHandled = FALSE;
		return 0;
	}
	Search(SearchSlice);
	if (!m_Search->Running()) {
		KillTimer(SearchTimer);
		Invalidate(FALSE);   // the progress line goes
	}
	return 0;
}

bool CCommandPaletteCtrl::IsSearching() const {
	return m_Search->Running();
}

int CCommandPaletteCtrl::GetActiveProvider() const {
	return m_Mode;
}

// ---- Providers -----------------------------------------------------------------------------

int CCommandPaletteCtrl::AddProvider(const CMDPPROVIDER* provider) {
	if (!provider || !provider->pszPrefix || !*provider->pszPrefix)
		return 0;
	std::wstring prefix = provider->pszPrefix;
	if (prefix == L"?" && !(IsWindow() && (GetStyle() & CMDPS_NOHELP)))
		return 0;
	for (auto& p : m_Providers)
		if (p->prefix == prefix)
			return 0;
	auto p = std::make_unique<CmpProvider>();
	p->id = m_NextProvider++;
	p->prefix = std::move(prefix);
	p->name = Text(provider->pszName);
	p->placeholder = Text(provider->pszPlaceholder);
	p->flags = provider->flags;
	p->lParam = provider->lParam;
	p->getItem = provider->pfnGetItem;
	p->context = provider->pvContext;
	int id = p->id;
	m_Providers.push_back(std::move(p));
	Filter();   // the text may pick it now
	return id;
}

BOOL CCommandPaletteCtrl::RemoveProvider(int id) {
	auto it = std::find_if(m_Providers.begin(), m_Providers.end(), [&](const std::unique_ptr<CmpProvider>& p) { return p->id == id; });
	if (it == m_Providers.end())
		return FALSE;
	m_Providers.erase(it);
	if (m_QueryProvider == id)
		m_QueryProvider = 0;
	Filter();
	return TRUE;
}

BOOL CCommandPaletteCtrl::SetProviderItemCount(int id, size_t count) {
	auto p = FindProvider(id);
	if (!p)
		return FALSE;
	p->count = count;
	p->items.assign(count, {});
	p->fetched.assign(count, 0);
	if (m_Mode == id && !m_InQuery)
		Filter();
	return TRUE;
}

BOOL CCommandPaletteCtrl::InvalidateProvider(int id) {
	auto p = FindProvider(id);
	if (!p)
		return FALSE;
	p->fetched.assign(p->count, 0);
	if (m_Mode == id && !m_InQuery)
		Filter();
	return TRUE;
}

// ---- Search and selection --------------------------------------------------------------

void CCommandPaletteCtrl::SetSearchText(LPCWSTR text) {
	if (!m_Box.IsWindow())
		return;
	m_SettingText = true;
	m_Box.SetWindowText(text ? text : L"");   // EN_CHANGE filters
	m_SettingText = false;
	m_Box.SetSel(-1, -1);
}

ATL::CString CCommandPaletteCtrl::GetSearchText() const {
	ATL::CString text;
	if (m_Box.IsWindow())
		m_Box.GetWindowText(text);
	return text;
}

void CCommandPaletteCtrl::SetPlaceholder(LPCWSTR text) {
	m_Placeholder = text ? text : L"";
	if (m_Box.IsWindow())
		m_Box.SetCueBannerText(m_Placeholder, TRUE);
}

int CCommandPaletteCtrl::GetResultCount() const {
	return (int)Results().size();
}

UINT CCommandPaletteCtrl::GetResultId(int row) const {
	return m_Mode == 0 && row >= 0 && row < (int)Results().size() ? m_Items[Results()[row].index].id : 0;
}

int CCommandPaletteCtrl::GetSelectedRow() const {
	return m_Selected;
}

void CCommandPaletteCtrl::SetSelectedRow(int row) {
	Select(row, false);
}

UINT CCommandPaletteCtrl::GetSelectedId() const {
	return GetResultId(m_Selected);
}

LRESULT CCommandPaletteCtrl::NotifyItem(UINT code, int row) {
	NMCMDPITEM nm{};
	if (row >= 0 && row < (int)Results().size()) {
		auto& item = RowItem(row);
		nm.id = m_Mode == 0 ? item.id : 0;
		nm.lParam = item.lParam;
		nm.provider = m_Mode > 0 ? m_Mode : 0;
		nm.index = Results()[row].index;
	}
	return FireNotify(&nm.hdr, code);
}

void CCommandPaletteCtrl::Select(int row, bool notify) {
	if (row < -1 || row >= (int)Results().size())
		row = -1;
	if (row == m_Selected)
		return;
	m_Selected = row;
	EnsureVisible(row);
	if (m_List->IsWindow())
		m_List->Invalidate();
	NotifySelectionAccessible();
	if (notify) {
		m_UserSelected = true;
		NotifyItem(CMDPN_SELCHANGED, row);
	}
}

BOOL CCommandPaletteCtrl::ExecuteSelected() {
	if (m_Selected < 0 || (RowItem(m_Selected).state & CMDPIS_DISABLED))
		return FALSE;
	Execute(m_Selected);
	return TRUE;
}

void CCommandPaletteCtrl::Execute(int row) {
	if (row < 0 || row >= (int)Results().size())
		return;
	auto& item = RowItem(row);
	if (item.state & CMDPIS_DISABLED) {
		::MessageBeep(MB_OK);
		return;
	}
	if (m_Mode < 0) {
		// '?': the provider's prefix typed (the commands: nothing), as the user would
		auto p = FindProvider((int)item.lParam);
		ATL::CString text = p ? p->prefix.c_str() : L"";
		m_Box.SetWindowText(text);
		m_Box.SetSel(-1, -1);
		return;
	}
	if (m_Mode > 0) {
		NotifyItem(CMDPN_EXECUTE, row);   // a provider's item: the app's to run
		return;
	}
	UINT id = item.id;
	m_Recent->Use(id);
	if (NotifyItem(CMDPN_EXECUTE, row))
		return;   // the app ran it
	// posted: the command may well close or change the palette
	HWND owner = m_Owner ? m_Owner : GetParent().m_hWnd;
	::PostMessage(owner, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

// ---- Recently used -------------------------------------------------------------------------

ATL::CString CCommandPaletteCtrl::GetRecent() const {
	return m_Recent->Save().c_str();
}

BOOL CCommandPaletteCtrl::SetRecent(LPCWSTR text) {
	BOOL ok = m_Recent->Load(text ? text : L"");
	Filter();
	return ok;
}

void CCommandPaletteCtrl::ClearRecent() {
	m_Recent->Clear();
	Filter();
}

void CCommandPaletteCtrl::SetRecentLimit(int count) {
	m_Recent->SetLimit((size_t)std::max(count, 0));
	Filter();
}

// ---- The search box ---------------------------------------------------------------------

LRESULT CCommandPaletteCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)lParam != m_Box.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	if (HIWORD(wParam) == EN_CHANGE) {
		Filter();
		if (!m_SettingText) {
			NMHDR hdr{};
			FireNotify(&hdr, CMDPN_TEXTCHANGED);
		}
	}
	return 0;
}

LRESULT CCommandPaletteCtrl::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)lParam != m_Box.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	auto c = GetColors();
	if (m_BoxBrushColor != c.crBoxBack) {
		if (!m_BoxBrush.IsNull())
			m_BoxBrush.DeleteObject();
		m_BoxBrush.CreateSolidBrush(c.crBoxBack);
		m_BoxBrushColor = c.crBoxBack;
	}
	WTL::CDCHandle dc((HDC)wParam);
	dc.SetTextColor(c.crBoxText);
	dc.SetBkColor(c.crBoxBack);
	return (LRESULT)m_BoxBrush.m_hBrush;
}

LRESULT CCommandPaletteCtrl::OnBoxKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	int count = (int)Results().size();
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	switch (wParam) {
		case VK_UP:
		case VK_DOWN:
			// around past either end
			if (count)
				Select(m_Selected < 0 ? 0 : (m_Selected + (wParam == VK_DOWN ? 1 : count - 1)) % count, true);
			return 0;
		case VK_PRIOR:
		case VK_NEXT:
			if (count) {
				int page = std::max(1, PageRows() - 1);
				Select(std::clamp(m_Selected + (wParam == VK_NEXT ? page : -page), 0, count - 1), true);
			}
			return 0;
		case VK_HOME:
		case VK_END:
			if (!ctrl || !count)
				break;   // in the box's text
			Select(wParam == VK_HOME ? 0 : count - 1, true);
			return 0;
		case VK_RETURN:
			if (!ExecuteSelected() && m_Selected >= 0)
				::MessageBeep(MB_OK);   // disabled
			return 0;
		case VK_ESCAPE: {
			NMHDR hdr{};
			FireNotify(&hdr, CMDPN_CANCEL);
			return 0;
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnBoxChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// Enter and Escape were handled as keys: no beep
	if (wParam == L'\r' || wParam == 27 || wParam == L'\n')
		return 0;
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnBoxGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	// the keys (Enter and Escape too, in a dialog), but Tab moves on
	LRESULT code = m_Box.DefWindowProc(uMsg, wParam, lParam);
	if (wParam != VK_TAB)
		code |= DLGC_WANTALLKEYS;
	return code;
}

LRESULT CCommandPaletteCtrl::OnBoxFocus(UINT uMsg, WPARAM, LPARAM, BOOL& bHandled) {
	auto box = GetBoxRect();
	InvalidateRect(&box, FALSE);   // the frame's focus color
	if (uMsg == WM_SETFOCUS)
		NotifySelectionAccessible();   // after the box's own focus event: the selected row
	bHandled = FALSE;
	return 0;
}

// ---- Mouse ---------------------------------------------------------------------------------

int CCommandPaletteCtrl::RowAt(POINT pt) const {
	RECT rc;
	m_List->GetClientRect(&rc);
	if (!::PtInRect(&rc, pt))
		return -1;
	int row = m_Top + pt.y / m_RowHeight;
	return row < (int)Results().size() ? row : -1;
}

LRESULT CCommandPaletteCtrl::OnListMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	int row = RowAt({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	if (row != m_Hot) {
		m_Hot = row;
		m_List->Invalidate();
	}
	if (!m_TrackingMouse) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_List->m_hWnd };
		m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingMouse = false;
	if (m_Hot >= 0) {
		m_Hot = -1;
		m_List->Invalidate();
	}
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	// the focus stays in the box
	if (m_Box.IsWindow() && ::GetFocus() != m_Box.m_hWnd)
		m_Box.SetFocus();
	m_Pressed = RowAt({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	if (m_Pressed >= 0)
		Select(m_Pressed, true);
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	// a click: down and up on the same row runs it
	int row = RowAt({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	if (row >= 0 && row == m_Pressed)
		Execute(row);
	m_Pressed = -1;
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListMouseActivate(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	bHandled = FALSE;
	return 0;
}

// ---- Colors ----------------------------------------------------------------------------------

void CCommandPaletteCtrl::SetColors(const CMDPCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	UpdateScrollBarTheme();
	if (IsWindow()) {
		Invalidate();
		m_List->Invalidate();
		m_Box.Invalidate();
	}
}

void CCommandPaletteCtrl::ForceHighContrast(int on) {
	m_ForceHighContrast = on < 0 ? -1 : on ? 1 : 0;
	if (IsWindow()) {
		Invalidate();
		m_List->Invalidate();
		m_Box.Invalidate();
	}
}

bool CCommandPaletteCtrl::IsHighContrast() const {
	if (m_ForceHighContrast >= 0)
		return m_ForceHighContrast != 0;
	HIGHCONTRASTW hc{ sizeof(hc) };
	return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

CMDPCOLORS CCommandPaletteCtrl::GetColors() const {
	CMDPCOLORS c;
	if (IsHighContrast()) {
		// the system's colors only; the matched letters are bold in the row's own color
		c.crBack = c.crBoxBack = ::GetSysColor(COLOR_WINDOW);
		c.crText = c.crBoxText = c.crMatch = c.crBoxBorder = ::GetSysColor(COLOR_WINDOWTEXT);
		c.crDimText = ::GetSysColor(COLOR_GRAYTEXT);
		c.crSelBack = c.crBoxFocusBorder = ::GetSysColor(COLOR_HIGHLIGHT);
		c.crSelText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
		c.crHotBack = c.crBack;
		return c;
	}
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
	bool dark = IsDark(c.crBack);
	COLORREF highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	c.crDimText = Blend(c.crBack, c.crText, 55);
	c.crMatch = dark ? RGB(0x4E, 0xA8, 0xFF) : RGB(0x00, 0x5F, 0xB8);
	c.crSelBack = Blend(c.crBack, highlight, dark ? 40 : 22);
	c.crSelText = c.crText;
	c.crHotBack = Blend(c.crBack, c.crText, 6);
	c.crBoxBack = Blend(c.crBack, c.crText, dark ? 8 : 3);
	c.crBoxText = c.crText;
	c.crBoxBorder = Blend(c.crBack, c.crText, 30);
	c.crBoxFocusBorder = highlight;
	return c;
}

void CCommandPaletteCtrl::UpdateScrollBarTheme() {
	// the dark Explorer theme makes the list's scroll bar dark. Set only when it changes: SetWindowTheme sends
	// WM_THEMECHANGED.
	if (!m_List->IsWindow())
		return;
	int dark = IsDark(GetColors().crBack) ? 1 : 0;
	if (dark == m_DarkScrollBar)
		return;
	m_DarkScrollBar = dark;
	::SetWindowTheme(m_List->m_hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	::SetWindowTheme(m_Box.m_hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
}

// ---- Painting ---------------------------------------------------------------------------

LRESULT CCommandPaletteCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto paint = [&](WTL::CDCHandle dc) {
		auto c = GetColors();
		RECT rc;
		GetClientRect(&rc);
		FillSolid(dc, rc, c.crBack);
		// the search box's frame, rounded
		auto box = GetBoxRect();
		bool focused = ::GetFocus() == m_Box.m_hWnd;
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, Scale(1)), focused ? c.crBoxFocusBorder : c.crBoxBorder);
		WTL::CBrush brush;
		brush.CreateSolidBrush(c.crBoxBack);
		auto oldPen = dc.SelectPen(pen);
		auto oldBrush = dc.SelectBrush(brush);
		int radius = Scale(8);
		dc.RoundRect(&box, { radius, radius });
		dc.SelectPen(oldPen);
		dc.SelectBrush(oldBrush);
		// going through a long list: how far, a line under the box
		if (m_Search->Running() && m_Search->Count() > 0) {
			int width = box.right - box.left - radius;
			RECT bar{ box.left + radius / 2, box.bottom + Scale(1), 0, box.bottom + Scale(3) };
			bar.right = bar.left + (int)((double)m_Search->Done() / m_Search->Count() * width);
			FillSolid(dc, bar, c.crBoxFocusBorder);
		}
	};
	if (uMsg == WM_PRINTCLIENT) {
		paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	paint(mdc.m_hDC);
	return 0;
}

LRESULT CCommandPaletteCtrl::OnListPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto paint = [&](WTL::CDCHandle dc, const RECT& rcPaint) {
		auto c = GetColors();
		m_PaintHighContrast = IsHighContrast();
		RECT rc;
		m_List->GetClientRect(&rc);
		auto oldFont = dc.SelectFont(m_hFont);
		dc.SetBkMode(TRANSPARENT);
		int row = m_Top + std::max(0, (int)rcPaint.top) / m_RowHeight;
		int y = (row - m_Top) * m_RowHeight;
		for (; y < rcPaint.bottom && row < (int)Results().size(); row++, y += m_RowHeight)
			PaintRow(dc, row, { 0, y, rc.right, y + m_RowHeight }, c);
		if (y < rcPaint.bottom)
			FillSolid(dc, { 0, y, (int)rc.right, (int)rcPaint.bottom }, c.crBack);
		if (Results().empty()) {
			// say why the list is empty (or what to type)
			LPCWSTR why = m_Items.empty() ? L"No commands" : L"No matching commands";
			if (m_Mode > 0) {
				auto p = FindProvider(m_Mode);
				if (m_Search->Running())
					why = L"Searching\x2026";
				else if (p && m_QueryText.empty() && !p->placeholder.empty())
					why = p->placeholder.c_str();
				else
					why = L"No matching items";
			}
			RECT text{ m_Pad * 2, 0, (int)rc.right - m_Pad * 2, m_RowHeight };
			dc.SetTextColor(c.crDimText);
			dc.DrawText(why, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		dc.SelectFont(oldFont);
	};
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		m_List->GetClientRect(&rc);
		paint((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_List->m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	paint(mdc.m_hDC, dc.m_ps.rcPaint);
	return 0;
}

void CCommandPaletteCtrl::PaintRow(WTL::CDCHandle dc, int row, const RECT& rc, const CMDPCOLORS& c) {
	auto& result = Results()[row];
	auto& item = RowItem(row);
	const bool selected = row == m_Selected, disabled = (item.state & CMDPIS_DISABLED) != 0;

	// the row's back, inset and rounded when selected or under the mouse
	FillSolid(dc, rc, c.crBack);
	if (selected || row == m_Hot) {
		RECT back{ rc.left + m_Pad, rc.top + Scale(1), rc.right - m_Pad, rc.bottom - Scale(1) };
		WTL::CBrush brush;
		brush.CreateSolidBrush(selected ? c.crSelBack : c.crHotBack);
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, 1, selected ? c.crSelBack : c.crHotBack);
		auto oldBrush = dc.SelectBrush(brush);
		auto oldPen = dc.SelectPen(pen);
		int radius = Scale(6);
		dc.RoundRect(&back, { radius, radius });
		dc.SelectBrush(oldBrush);
		dc.SelectPen(oldPen);
	}
	COLORREF text = disabled ? c.crDimText : selected ? c.crSelText : c.crText;
	// high contrast: the selected row all in the highlight's text color
	COLORREF dim = m_PaintHighContrast && selected ? c.crSelText : c.crDimText;
	COLORREF match = m_PaintHighContrast && selected ? c.crSelText : c.crMatch;
	int x = rc.left + 2 * m_Pad;
	const int textY = rc.top + (rc.bottom - rc.top - m_TextHeight) / 2;

	// the icon, or a check mark
	if (m_Images && item.image >= 0) {
		int cx = 0, cy = 0;
		::ImageList_GetIconSize(m_Images, &cx, &cy);
		::ImageList_DrawEx(m_Images, item.image, dc, x + (m_IconSize - cx) / 2, rc.top + (rc.bottom - rc.top - cy) / 2, 0, 0,
			CLR_NONE, CLR_NONE, disabled ? ILD_TRANSPARENT | ILD_BLEND50 : ILD_TRANSPARENT);
	}
	else if (item.state & CMDPIS_CHECKED) {
		dc.SetTextColor(text);
		RECT check{ x, rc.top, x + m_IconSize, rc.bottom };
		dc.DrawText(L"\x2713", 1, &check, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	}
	x += m_IconSize + m_Pad;

	// the right: the shortcut, and the group a row starts with nothing typed
	int right = rc.right - 2 * m_Pad;
	auto rightText = [&](const std::wstring& s) {
		if (s.empty())
			return;
		SIZE size{};
		dc.GetTextExtent(s.c_str(), (int)s.size(), &size);
		right -= size.cx;
		dc.SetTextColor(dim);
		dc.ExtTextOut(right, textY, 0, nullptr, s.c_str(), (UINT)s.size(), nullptr);
		right -= 2 * m_Pad;
	};
	rightText(item.shortcut);
	if (m_Mode == 0 && GetSearchText().IsEmpty()) {
		bool recent = result.recent >= 0;
		bool first = row == 0 || (Results()[row - 1].recent >= 0) != recent;
		if (first && (recent || (row > 0 && m_Recent->Count() > 0)))
			rightText(recent ? L"recently used" : L"other commands");
	}

	// the label: the category dimmed, the matched letters bold in their color
	RECT clip{ x, rc.top, std::max(x, right), rc.bottom };
	auto& label = item.label;
	const size_t nameStart = item.NameStart();
	std::vector<char> matched(label.size(), 0);
	for (auto p : result.positions)
		if (p < matched.size())
			matched[p] = 1;
	for (size_t i = 0; i < label.size() && x < clip.right;) {
		// a run of the same kind: matched or not, in the category or the name
		size_t j = i + 1;
		while (j < label.size() && matched[j] == matched[i] && (j < nameStart) == (i < nameStart))
			j++;
		dc.SelectFont(matched[i] ? (HFONT)m_BoldFont : m_hFont);
		dc.SetTextColor(matched[i] && !disabled ? match : i < nameStart ? dim : text);
		SIZE size{};
		dc.GetTextExtent(label.c_str() + i, (int)(j - i), &size);
		dc.ExtTextOut(x, textY, ETO_CLIPPED, &clip, label.c_str() + i, (UINT)(j - i), nullptr);
		x += size.cx;
		i = j;
	}
	dc.SelectFont(m_hFont);
}

// ---- Notifications -----------------------------------------------------------------------

LRESULT CCommandPaletteCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}
