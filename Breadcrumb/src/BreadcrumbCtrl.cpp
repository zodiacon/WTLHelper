#include "BreadcrumbData.h"

#include <commctrl.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

using namespace BrcDraw;

// ---- Helpers -----------------------------------------------------------------------

namespace {
	std::wstring Text(LPCWSTR s) {
		return s ? std::wstring(s) : std::wstring();
	}

	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	int CopyText(const std::wstring& s, LPWSTR text, int cch) {
		if (text && cch > 0) {
			size_t n = std::min(s.size(), (size_t)cch - 1);
			s.copy(text, n);
			text[n] = 0;
		}
		return (int)s.size() + 1;
	}

	BrcChild ChildOf(const BRCITEM& item) {
		BrcChild c;
		c.text = Text(item.pszText);
		c.name = item.pszName ? item.pszName : c.text;
		c.image = item.iImage;
		c.flags = item.flags;
		c.lParam = item.lParam;
		return c;
	}
}

// ---- Construction ------------------------------------------------------------------

CBreadcrumbCtrl::CBreadcrumbCtrl() : m_Data(std::make_unique<BrcData>()), m_Edit(this, 1) {
}

CBreadcrumbCtrl::~CBreadcrumbCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hBold)
		::DeleteObject(m_hBold);
}

LRESULT CBreadcrumbCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_CLIPCHILDREN);
	// a real "Edit" (CContainedWindow::Create would make an "ATL:Edit" superclass, which the system's accessibility
	// doesn't know), shown while editing
	HWND edit = ::CreateWindowExW(0, WC_EDITW, nullptr, WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	m_Edit.SubclassWindow(edit);
	UpdateFonts();
	UpdateMetrics();
	m_LastHeight = GetIdealHeight();
	return 0;
}

LRESULT CBreadcrumbCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.list) {
		d.list->Close();
		if (d.list->IsWindow())
			d.list->DestroyWindow();
		d.list.reset();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CBreadcrumbCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	CloseDropDown();   // it drops from where its segment was
	Changed(false);
	return 0;
}

LRESULT CBreadcrumbCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (wParam == GWL_STYLE) {   // the parts shown
		if (GetStyle() & BRCS_NOEDIT)
			EndEditing(true);
		CloseDropDown();
		m_Data->hot = m_Data->pressed = {};
		Changed(false);
	}
	return 0;
}

void CBreadcrumbCtrl::SetOwner(HWND owner) {
	m_Owner = owner;
}

HWND CBreadcrumbCtrl::Owner() const {
	return m_Owner ? m_Owner : GetParent().m_hWnd;
}

LRESULT CBreadcrumbCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

// ---- Metrics and fonts -------------------------------------------------------------

int CBreadcrumbCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CBreadcrumbCtrl::UpdateFonts() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	if (m_hBold)
		::DeleteObject(m_hBold);
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	lf.lfWeight = FW_BOLD;
	m_hBold = ::CreateFontIndirectW(&lf);
	if (m_Edit.IsWindow())
		m_Edit.SetFont(m_hFont);
}

void CBreadcrumbCtrl::UpdateMetrics() {
	if (IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		m_TextHeight = tm.tmHeight;
	}
	auto m = BrcMetrics{}.Scaled(m_Dpi);
	int cx, cy;
	if (m_Images && ::ImageList_GetIconSize(m_Images, &cx, &cy))
		m.icon = cx;
	m.height = std::max({ m.height, m_TextHeight + Scale(10), m.icon + Scale(8) });
	m.navButton = m.height;
	m_Data->m = m;
	Changed(true);
}

LRESULT CBreadcrumbCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
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

LRESULT CBreadcrumbCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CBreadcrumbCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// before the parent too: its WM_DPICHANGED lays the window out with the height for the new DPI. A font set with
	// WM_SETFONT is the app's to replace.
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		m_Dpi = dpi;
		CloseDropDown();
		UpdateFonts();
		UpdateMetrics();
	}
	NotifyHeight();
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {   // the message font
		UpdateFonts();
		UpdateMetrics();
		NotifyHeight();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	if (m_Edit.IsWindow())
		m_Edit.Invalidate();
	if (m_Data->list)
		m_Data->list->Invalidate();
	bHandled = FALSE;
	return 0;
}

int CBreadcrumbCtrl::GetIdealHeight() const {
	return m_Data->m.height;
}

void CBreadcrumbCtrl::NotifyHeight() {
	if (!IsWindow())
		return;
	int height = GetIdealHeight();
	if (height == m_LastHeight)
		return;
	m_LastHeight = height;
	NMHDR nm{};
	FireNotify(&nm, BRCN_HEIGHTCHANGED);
}

void CBreadcrumbCtrl::SetImageList(HIMAGELIST images) {
	m_Images = images;
	UpdateMetrics();
	NotifyHeight();
}

// ---- Colors ------------------------------------------------------------------------------

void CBreadcrumbCtrl::SetColors(const BRCCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
	if (m_Edit.IsWindow())
		m_Edit.Invalidate();
	if (m_Data->list)
		m_Data->list->Invalidate();
}

BRCCOLORS CBreadcrumbCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	BRCCOLORS c;
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	bool dark = IsDark(window);
	c.crBack = Blend(window, text, dark ? 3 : 4);
	c.crBox = dark ? Blend(window, text, 9) : window;
	c.crBorder = Blend(c.crBox, text, dark ? 20 : 22);
	c.crHotBorder = Blend(c.crBox, text, dark ? 40 : 42);
	c.crText = text;
	c.crDimText = Blend(c.crBack, text, 40);
	c.crHotBack = Blend(c.crBox, text, dark ? 12 : 8);
	c.crPressedBack = Blend(c.crBox, text, dark ? 20 : 15);
	c.crListBack = dark ? Blend(window, text, 7) : window;
	c.crListHotBack = Blend(c.crListBack, highlight, dark ? 32 : 16);
	c.crAccent = dark ? Blend(highlight, RGB(255, 255, 255), 30) : highlight;
	return c;
}

// ---- The path ----------------------------------------------------------------------------------

BOOL CBreadcrumbCtrl::SetPath(const BRCITEM* items, int count, LPCWSTR location) {
	if (count < 0 || (count > 0 && !items))
		return FALSE;
	CloseDropDown();
	auto& d = *m_Data;
	d.segments.clear();
	d.segments.reserve(count);
	for (int i = 0; i < count; i++) {
		BrcSegment s;
		s.text = Text(items[i].pszText);
		s.name = items[i].pszName ? items[i].pszName : s.text;
		s.image = items[i].iImage;
		s.noChildren = (items[i].flags & BRCIF_NOCHILDREN) != 0;
		s.lParam = items[i].lParam;
		d.segments.push_back(std::move(s));
	}
	d.hot = d.pressed = {};
	d.editText.clear();   // the app's text was for the path it replaces
	if (count > 0) {
		BrcLocation loc{ location ? std::wstring(location) : PathTo(-1), d.segments.back().text, d.segments.back().image };
		if (d.pendingOffset && d.backForward.Go(d.pendingOffset))
			d.backForward.Replace(loc);   // back or forward: there, as the app wrote it now
		else
			d.backForward.Navigate(loc);
		d.pendingOffset = 0;
	}
	Changed(true);
	return TRUE;
}

// ---- Back, forward, up ------------------------------------------------------------------------------

bool CBreadcrumbCtrl::CanGo(int offset) const {
	return offset != 0 && m_Data->backForward.CanGo(offset);
}

BOOL CBreadcrumbCtrl::Go(int offset) {
	auto& d = *m_Data;
	auto loc = offset ? d.backForward.At(offset) : nullptr;
	if (!loc)
		return FALSE;
	auto path = loc->path, text = loc->text;
	NMBRCNAVIGATE nm{};
	nm.source = BRCNS_BACKFORWARD;
	nm.index = offset;
	nm.pszPath = path.c_str();
	nm.pszText = text.c_str();
	d.pendingOffset = offset;   // the app's SetPath goes along the list
	FireNotify(&nm.hdr, BRCN_NAVIGATE);
	d.pendingOffset = 0;
	return TRUE;
}

BOOL CBreadcrumbCtrl::GoUp() {
	auto& d = *m_Data;
	const int parent = (int)d.segments.size() - 2;
	if (parent < 0)
		return FALSE;
	auto path = PathTo(parent + 1);
	auto text = d.segments[parent].text;
	NMBRCNAVIGATE nm{};
	nm.source = BRCNS_UP;
	nm.index = parent;
	nm.pszPath = path.c_str();
	nm.pszText = text.c_str();
	nm.lParam = d.segments[parent].lParam;
	FireNotify(&nm.hdr, BRCN_NAVIGATE);
	return TRUE;
}

void CBreadcrumbCtrl::ClearBackForward() {
	auto& d = *m_Data;
	auto current = d.backForward.Current() ? *d.backForward.Current() : BrcLocation{};
	bool had = d.backForward.Current() != nullptr;
	d.backForward.Clear();
	if (had)
		d.backForward.Navigate(current);
	if (IsWindow())
		Invalidate(FALSE);
}

int CBreadcrumbCtrl::GetItemCount() const {
	return (int)m_Data->segments.size();
}

int CBreadcrumbCtrl::GetItemText(int index, LPWSTR text, int cch) const {
	auto& s = m_Data->segments;
	if (index < 0 || index >= (int)s.size())
		return 0;
	return CopyText(s[index].text, text, cch);
}

LPARAM CBreadcrumbCtrl::GetItemParam(int index) const {
	auto& s = m_Data->segments;
	return index >= 0 && index < (int)s.size() ? s[index].lParam : 0;
}

std::wstring CBreadcrumbCtrl::PathTo(int count) const {
	return BrcJoinPath(m_Data->segments, m_Data->separators, count < 0 ? SIZE_MAX : (size_t)count);
}

int CBreadcrumbCtrl::GetPathText(LPWSTR text, int cch, int count) const {
	return CopyText(PathTo(count), text, cch);
}

void CBreadcrumbCtrl::SetSeparators(LPCWSTR separators) {
	m_Data->separators = separators && *separators ? separators : L"\\";
}

BOOL CBreadcrumbCtrl::AddChild(const BRCITEM* item) {
	auto& d = *m_Data;
	if (!d.collecting || !item)
		return FALSE;
	d.collecting->push_back(ChildOf(*item));
	return TRUE;
}

BOOL CBreadcrumbCtrl::AddChild(LPCWSTR text, int image, LPARAM lParam, LPCWSTR name) {
	BRCITEM item;
	item.pszText = text;
	item.pszName = name;
	item.iImage = image;
	item.lParam = lParam;
	return AddChild(&item);
}

// ---- Buttons -------------------------------------------------------------------------------------

BOOL CBreadcrumbCtrl::AddButton(const BRCBUTTON* button, int insertAt) {
	auto& b = m_Data->buttons;
	if (!button || std::any_of(b.begin(), b.end(), [&](auto& x) { return x.id == button->id; }))
		return FALSE;
	BrcButton x;
	x.id = button->id;
	x.image = button->iImage;
	x.glyph = button->glyph;
	x.tip = Text(button->pszTip);
	x.state = button->state;
	if (insertAt < 0 || insertAt > (int)b.size())
		insertAt = (int)b.size();
	b.insert(b.begin() + insertAt, std::move(x));
	m_Data->hot = m_Data->pressed = {};
	Changed(false);
	return TRUE;
}

BOOL CBreadcrumbCtrl::RemoveButton(UINT id) {
	auto& b = m_Data->buttons;
	auto it = std::find_if(b.begin(), b.end(), [&](auto& x) { return x.id == id; });
	if (it == b.end())
		return FALSE;
	b.erase(it);
	m_Data->hot = m_Data->pressed = {};
	Changed(false);
	return TRUE;
}

BOOL CBreadcrumbCtrl::SetButtonState(UINT id, UINT state) {
	for (auto& x : m_Data->buttons)
		if (x.id == id) {
			if (x.state != state) {
				x.state = state;
				if (IsWindow())
					Invalidate(FALSE);
			}
			return TRUE;
		}
	return FALSE;
}

UINT CBreadcrumbCtrl::GetButtonState(UINT id) const {
	for (auto& x : m_Data->buttons)
		if (x.id == id)
			return x.state;
	return (UINT)-1;
}

// ---- Layout ---------------------------------------------------------------------------

void CBreadcrumbCtrl::Changed(bool measure) {
	if (measure)
		m_Data->measure = true;
	m_Data->dirty = true;
	if (IsWindow())
		Invalidate(FALSE);
}

void CBreadcrumbCtrl::EnsureLayout() {
	auto& d = *m_Data;
	if (d.measure && IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		BrcMeasureSegments(d.segments, [&](std::wstring_view s) {
			SIZE size{};
			dc.GetTextExtent(s.data(), (int)s.size(), &size);
			return (int)size.cx;
		});
		dc.SelectFont(old);
		d.measure = false;
		d.dirty = true;
	}
	if (!d.dirty)
		return;
	RECT rc{};
	if (IsWindow())
		GetClientRect(&rc);
	DWORD style = IsWindow() ? GetStyle() : 0;
	BrcParts parts;
	parts.nav = !(style & BRCS_NONAV);
	parts.up = !(style & BRCS_NOUP);
	parts.icon = !(style & BRCS_NOICON);
	parts.rootChevron = !(style & BRCS_NOROOTCHEVRON);
	parts.history = !(style & (BRCS_NOHISTORY | BRCS_NOEDIT));
	parts.actions = (int)d.buttons.size();
	auto m = d.m;
	if (rc.bottom > 0)
		m.height = rc.bottom;   // the box fills the window's height
	d.layout = BrcLayoutBar(d.segments, parts, rc.right, m);
	d.dirty = false;
	if (d.editing)
		PlaceEdit();
}

// ---- Mouse ---------------------------------------------------------------------------------------

BrcHit CBreadcrumbCtrl::HitTest(POINT pt) {
	EnsureLayout();
	auto hit = BrcHitTest(m_Data->layout, pt);
	switch (hit.part) {
		case BrcPart::Text:
		case BrcPart::Chevron:
		case BrcPart::Overflow:
			break;
		case BrcPart::Action:
			if (m_Data->buttons[hit.index].state & BRCBS_DISABLED)
				hit = {};
			break;
		case BrcPart::Empty:
		case BrcPart::Icon:
		case BrcPart::History:
			if (GetStyle() & BRCS_NOEDIT)
				hit = {};
			break;
		case BrcPart::Back:
		case BrcPart::Forward:
		case BrcPart::Recent:
		case BrcPart::Up:
			if (!Enabled(hit))
				hit = {};
			break;
		default:
			hit = {};
			break;
	}
	return hit;
}

void CBreadcrumbCtrl::SetHot(const BrcHit& hit) {
	auto& d = *m_Data;
	if (!(hit == d.hot)) {
		d.hot = hit;
		Invalidate(FALSE);
	}
}

LRESULT CBreadcrumbCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (!d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	POINT pt = PointOf(lParam);
	SetHot(HitTest(pt));
	bool inBox = ::PtInRect(&d.layout.box, pt) != FALSE;
	if (inBox != d.hotBox) {
		d.hotBox = inBox;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CBreadcrumbCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	d.tracking = false;
	if (!d.list || !d.list->IsOpen())
		SetHot({});
	if (d.hotBox) {
		d.hotBox = false;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CBreadcrumbCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	auto hit = HitTest(PointOf(lParam));
	switch (hit.part) {
		case BrcPart::Chevron:
		case BrcPart::Overflow:
		case BrcPart::Recent:
			OpenDropDown(hit, true);
			break;
		case BrcPart::Text:
		case BrcPart::Action:
		case BrcPart::Back:
		case BrcPart::Forward:
		case BrcPart::Up:
			d.pressed = hit;
			SetHot(hit);
			SetCapture();
			Invalidate(FALSE);
			break;
		case BrcPart::Empty:
		case BrcPart::Icon:
			BeginEdit();
			break;
		case BrcPart::History:
			if (d.list && d.list->IsOpen() && d.listKind == BrcList::History)
				d.list->Close();
			else
				ShowHistory();
			break;
		default:
			break;
	}
	return 0;
}

LRESULT CBreadcrumbCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part == BrcPart::None)
		return 0;
	auto pressed = d.pressed;
	auto hit = HitTest(PointOf(lParam));
	d.pressed = {};
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate(FALSE);
	if (!(hit == pressed))
		return 0;
	switch (pressed.part) {
		case BrcPart::Text: Navigate(BRCNS_SEGMENT, pressed.index, -1); break;
		case BrcPart::Action: ClickButton(pressed.index); break;
		case BrcPart::Back: Go(-1); break;
		case BrcPart::Forward: Go(1); break;
		case BrcPart::Up: GoUp(); break;
		default: break;
	}
	return 0;
}

LRESULT CBreadcrumbCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part != BrcPart::None) {
		d.pressed = {};
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CBreadcrumbCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.list && d.list->IsOpen())
		d.list->Wheel(GET_WHEEL_DELTA_WPARAM(wParam));
	else
		bHandled = FALSE;
	return 0;
}

// ---- Keyboard ----------------------------------------------------------------------------------

LRESULT CBreadcrumbCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	const UINT vk = (UINT)wParam;
	if (d.list && d.list->IsOpen()) {
		// Left and Right go to the next list, as on a menu bar
		if (d.listKind == BrcList::Children && (vk == VK_LEFT || vk == VK_RIGHT) && DropAdjacent(vk == VK_LEFT ? -1 : 1))
			return 0;
		if (!d.list->KeyDown(vk))
			bHandled = FALSE;
		return 0;
	}
	if (d.editing || !KeyDown(vk))
		bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.list && d.list->IsOpen())
		d.list->Char((wchar_t)wParam);
	else if (wParam > L' ' && !d.editing && BeginEdit()) {   // (Space is Enter's)
		// typing edits the path, starting with what was typed
		SetText(std::wstring(1, (wchar_t)wParam), false);
		Suggest(false);
	}
	else
		bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate(FALSE);   // the keys' part framed
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	Invalidate(FALSE);
	if (d.list && d.list->IsOpen() && !d.switching) {
		d.focusBefore = nullptr;   // the focus went somewhere: it stays there
		CloseDropDown();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	return d.list && d.list->IsOpen() ? DLGC_WANTALLKEYS : DLGC_WANTARROWS | DLGC_WANTCHARS;
}

// ---- Going places -----------------------------------------------------------------------------

void CBreadcrumbCtrl::Navigate(UINT source, int index, int child) {
	auto& d = *m_Data;
	NMBRCNAVIGATE nm{};
	nm.source = source;
	nm.index = index;
	std::wstring path, text;
	if (child >= 0 && child < (int)d.children.size()) {
		auto& c = d.children[child];
		std::vector<BrcSegment> to(d.segments.begin(), d.segments.begin() + std::clamp(index + 1, 0, (int)d.segments.size()));
		BrcSegment s;
		s.name = c.name;
		to.push_back(std::move(s));
		path = BrcJoinPath(to, d.separators);
		text = c.text;
		nm.lParam = c.lParam;
	}
	else if (index >= 0 && index < (int)d.segments.size()) {
		path = PathTo(index + 1);
		text = d.segments[index].text;
		nm.lParam = d.segments[index].lParam;
	}
	else
		return;
	nm.pszPath = path.c_str();
	nm.pszText = text.c_str();
	FireNotify(&nm.hdr, BRCN_NAVIGATE);
}

void CBreadcrumbCtrl::ClickButton(int index) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.buttons.size())
		return;
	NMBRCBUTTON nm{};
	nm.id = d.buttons[index].id;
	nm.rcButton = d.layout.actions[index];
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&nm.rcButton, 2);
	UINT id = nm.id;
	if (!FireNotify(&nm.hdr, BRCN_BUTTONCLICK))
		::PostMessage(Owner(), WM_COMMAND, MAKEWPARAM(id, 0), 0);
}
