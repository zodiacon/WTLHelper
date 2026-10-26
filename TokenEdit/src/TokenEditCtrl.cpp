#include "TokenEditData.h"

#include <commctrl.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

using namespace TkeDraw;

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

	UINT FlagsOf(const TkeToken& t) {
		return (t.invalid ? TKEF_INVALID : 0) | (t.locked ? TKEF_LOCKED : 0) | (t.selected ? TKEF_SELECTED : 0);
	}

	void SetFlags(TkeToken& t, UINT flags) {
		t.invalid = (flags & TKEF_INVALID) != 0;
		t.locked = (flags & TKEF_LOCKED) != 0;
		t.selected = (flags & TKEF_SELECTED) != 0;
	}
}

// ---- Construction ------------------------------------------------------------------

CTokenEditCtrl::CTokenEditCtrl() : m_Data(std::make_unique<TkeData>()), m_Edit(this, 1) {
}

CTokenEditCtrl::~CTokenEditCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hBold)
		::DeleteObject(m_hBold);
}

LRESULT CTokenEditCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_CLIPCHILDREN);
	// a real "Edit" (CContainedWindow::Create would make an "ATL:Edit" superclass, which the system's accessibility
	// doesn't know)
	HWND edit = ::CreateWindowExW(0, WC_EDITW, nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd, nullptr,
		nullptr, nullptr);
	m_Edit.SubclassWindow(edit);
	m_Edit.SetMargins(0, 0);
	if (ReadOnly())
		m_Edit.ShowWindow(SW_HIDE);
	UpdateFonts();
	UpdateMetrics();
	EnsureLayout();
	m_LastHeight = GetIdealHeight();
	return 0;
}

LRESULT CTokenEditCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.list) {
		if (d.list->IsWindow())
			d.list->DestroyWindow();
		d.list.reset();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CTokenEditCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	CloseSuggestions();   // they drop from where the typing area was
	Changed(false);
	return 0;
}

LRESULT CTokenEditCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (wParam == GWL_STYLE) {
		const bool readOnly = ReadOnly();
		if (m_Edit.IsWindow()) {
			const bool hadFocus = ::GetFocus() == m_Edit.m_hWnd;
			m_Edit.ShowWindow(readOnly ? SW_HIDE : SW_SHOW);
			if (readOnly && hadFocus)
				SetFocus();
		}
		m_Data->hot = m_Data->pressed = {};
		Changed(false);
	}
	return 0;
}

LRESULT CTokenEditCtrl::OnEnable(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (m_Edit.IsWindow())
		m_Edit.EnableWindow((BOOL)wParam);
	m_Data->hot = m_Data->pressed = {};
	Invalidate();
	return 0;
}

LRESULT CTokenEditCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

void CTokenEditCtrl::NotifyChanged() {
	NMHDR nm{};
	FireNotify(&nm, TKEN_CHANGED);
}

void CTokenEditCtrl::NotifySelection() {
	auto& d = *m_Data;
	std::vector<UINT> ids;
	for (auto& t : d.tokens.Tokens())
		if (t.selected)
			ids.push_back(t.id);
	if (ids == d.lastSelection)
		return;
	d.lastSelection = std::move(ids);
	if (IsWindow()) {
		NMHDR nm{};
		FireNotify(&nm, TKEN_SELCHANGED);
	}
}

bool CTokenEditCtrl::ReadOnly() const {
	return IsWindow() && (GetStyle() & TKES_READONLY);
}

// ---- Metrics and fonts -------------------------------------------------------------

int CTokenEditCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CTokenEditCtrl::UpdateFonts() {
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
	if (m_Edit.IsWindow()) {
		m_Edit.SetFont(m_hFont);
		m_Edit.SetMargins(0, 0);
	}
	CloseSuggestions();
}

void CTokenEditCtrl::UpdateMetrics() {
	if (IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		m_TextHeight = tm.tmHeight;
	}
	auto m = TkeMetrics{}.Scaled(m_Dpi);
	int cx, cy;
	if (m_Images && ::ImageList_GetIconSize(m_Images, &cx, &cy))
		m.icon = cx;
	m.tokenHeight = std::max({ m.tokenHeight, m_TextHeight + Scale(6), m.icon + Scale(4) });
	m.close = std::max(m.close, m.tokenHeight - Scale(6));   // a larger font, a larger button
	m_Data->m = m;
	Changed(true);
}

LRESULT CTokenEditCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CTokenEditCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CTokenEditCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// before the parent too: its WM_DPICHANGED lays the window out with the height for the new DPI. A font set with
	// WM_SETFONT is the app's to replace.
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		m_Dpi = dpi;
		UpdateFonts();
		UpdateMetrics();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {   // the message font
		UpdateFonts();
		UpdateMetrics();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	if (m_Edit.IsWindow())
		m_Edit.Invalidate();
	if (m_Data->list)
		m_Data->list->Invalidate();
	bHandled = FALSE;
	return 0;
}

void CTokenEditCtrl::SetImageList(HIMAGELIST images) {
	m_Images = images;
	UpdateMetrics();
}

void CTokenEditCtrl::SetLineLimits(int minLines, int maxLines) {
	auto& d = *m_Data;
	d.minLines = std::max(1, minLines);
	d.maxLines = std::max(0, maxLines);
	if (d.maxLines)
		d.maxLines = std::max(d.maxLines, d.minLines);
	Changed(false);
}

int CTokenEditCtrl::GetIdealHeight() const {
	auto& d = *m_Data;
	const_cast<CTokenEditCtrl*>(this)->EnsureLayout();
	return TkeBoxHeight(d.layout.lines, d.minLines, d.maxLines, d.m);
}

void CTokenEditCtrl::NotifyHeight() {
	if (!IsWindow())
		return;
	int height = GetIdealHeight();
	if (height == m_LastHeight)
		return;
	m_LastHeight = height;
	NMHDR nm{};
	FireNotify(&nm, TKEN_HEIGHTCHANGED);
}

// ---- Colors ------------------------------------------------------------------------------

void CTokenEditCtrl::SetColors(const TKECOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
	if (m_Edit.IsWindow())
		m_Edit.Invalidate();
}

TKECOLORS CTokenEditCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	TKECOLORS c;
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	const bool dark = IsDark(window);
	c.crBack = dark ? Blend(window, text, 9) : window;
	c.crBorder = Blend(c.crBack, text, dark ? 22 : 26);
	c.crAccent = dark ? Blend(highlight, RGB(255, 255, 255), 30) : highlight;
	c.crText = text;
	c.crCue = Blend(c.crBack, text, 45);
	c.crTokenBack = Blend(c.crBack, text, dark ? 14 : 8);
	c.crTokenText = text;
	c.crTokenHot = Blend(c.crBack, text, dark ? 22 : 14);
	c.crSelectedBack = Blend(c.crBack, c.crAccent, dark ? 40 : 22);
	c.crSelectedText = text;
	c.crInvalid = dark ? RGB(0xFF, 0x99, 0xA4) : RGB(0xC4, 0x2B, 0x1C);
	c.crInvalidBack = Blend(c.crBack, c.crInvalid, dark ? 16 : 10);
	return c;
}

// ---- Tokens ------------------------------------------------------------------------------------

bool CTokenEditCtrl::Duplicate(const std::wstring& text, int except) const {
	if (!IsWindow() || !(GetStyle() & TKES_NODUPLICATES))
		return false;
	int i = m_Data->tokens.FindText(text);
	return i >= 0 && i != except;
}

void CTokenEditCtrl::ClearHistory() {
	m_Data->history.Clear();
}

UINT CTokenEditCtrl::InsertToken(const TKETOKEN* token, int index) {
	auto& d = *m_Data;
	if (!token || !token->pszText || !*token->pszText || Duplicate(token->pszText))
		return 0;
	TkeToken t;
	t.text = token->pszText;
	t.tip = Text(token->pszTip);
	t.image = token->iImage;
	t.back = token->crBack;
	t.fore = token->crText;
	SetFlags(t, token->flags);
	t.lParam = token->lParam;
	t.id = token->id;
	const size_t count = d.tokens.Size();
	const size_t at = d.tokens.Insert(index < 0 ? count : (size_t)index, std::move(t));
	if (d.editAt >= 0 && (int)at <= d.editAt)
		d.editAt++;
	ClearHistory();
	Changed(true);
	NotifySelection();
	return d.tokens[at].id;
}

UINT CTokenEditCtrl::AddToken(LPCWSTR text, int image, LPARAM lParam) {
	TKETOKEN t;
	t.pszText = text;
	t.iImage = image;
	t.lParam = lParam;
	return InsertToken(&t);
}

BOOL CTokenEditCtrl::SetToken(const TKETOKEN* token) {
	auto& d = *m_Data;
	int i = token ? d.tokens.Find(token->id) : -1;
	if (i < 0)
		return FALSE;
	auto& t = d.tokens[i];
	if (token->mask & TKETF_TEXT) {
		if (!token->pszText || !*token->pszText || Duplicate(token->pszText, i))
			return FALSE;
		t.text = token->pszText;
	}
	if (token->mask & TKETF_TIP)
		t.tip = Text(token->pszTip);
	if (token->mask & TKETF_IMAGE)
		t.image = token->iImage;
	if (token->mask & TKETF_COLORS) {
		t.back = token->crBack;
		t.fore = token->crText;
	}
	if (token->mask & TKETF_FLAGS)
		SetFlags(t, token->flags);
	if (token->mask & TKETF_PARAM)
		t.lParam = token->lParam;
	ClearHistory();
	Changed((token->mask & TKETF_TEXT) != 0);
	NotifySelection();
	return TRUE;
}

BOOL CTokenEditCtrl::GetToken(UINT id, TKETOKEN* token) const {
	auto& d = *m_Data;
	int i = d.tokens.Find(id);
	if (i < 0 || !token)
		return FALSE;
	auto& t = d.tokens[i];
	token->mask = TKETF_ALL;
	token->id = t.id;
	token->pszText = t.text.c_str();
	token->pszTip = t.tip.empty() ? nullptr : t.tip.c_str();
	token->iImage = t.image;
	token->crBack = t.back;
	token->crText = t.fore;
	token->flags = FlagsOf(t);
	token->lParam = t.lParam;
	return TRUE;
}

BOOL CTokenEditCtrl::RemoveToken(UINT id) {
	auto& d = *m_Data;
	int i = d.tokens.Find(id);
	if (i < 0)
		return FALSE;
	d.tokens.Remove(i);
	if (d.editAt > i)
		d.editAt--;
	if (d.focus >= i)
		d.focus--;
	d.hot = d.pressed = {};
	ClearHistory();
	Changed(true);
	NotifySelection();
	return TRUE;
}

void CTokenEditCtrl::RemoveAll() {
	auto& d = *m_Data;
	d.tokens.Clear();
	d.editAt = d.focus = -1;
	d.hot = d.pressed = {};
	d.scroll = 0;
	ClearHistory();
	Changed(true);
	NotifySelection();
}

int CTokenEditCtrl::GetTokenCount() const {
	return (int)m_Data->tokens.Size();
}

UINT CTokenEditCtrl::GetTokenId(int index) const {
	auto& tokens = m_Data->tokens;
	return index >= 0 && index < (int)tokens.Size() ? tokens[index].id : 0;
}

int CTokenEditCtrl::GetTokenIndex(UINT id) const {
	return m_Data->tokens.Find(id);
}

UINT CTokenEditCtrl::FindToken(LPCWSTR text) const {
	int i = text ? m_Data->tokens.FindText(text) : -1;
	return i >= 0 ? m_Data->tokens[i].id : 0;
}

BOOL CTokenEditCtrl::MoveToken(UINT id, int index) {
	auto& d = *m_Data;
	int i = d.tokens.Find(id);
	if (i < 0 || index < 0)
		return FALSE;
	// Move puts it before the token at to, counted before it's taken out
	size_t to = std::min((size_t)index, d.tokens.Size() - 1);
	d.tokens.Move({ (size_t)i }, to >= (size_t)i ? to + 1 : to);
	ClearHistory();
	Changed(false);
	return TRUE;
}

BOOL CTokenEditCtrl::EnsureVisible(UINT id) {
	auto& d = *m_Data;
	int i = d.tokens.Find(id);
	if (i < 0 || !IsWindow())
		return FALSE;
	EnsureLayout();
	ScrollToShow(d.layout.tokens[i].rc);
	return TRUE;
}

// ---- Selection -----------------------------------------------------------------------------------

BOOL CTokenEditCtrl::SelectToken(UINT id, BOOL select) {
	auto& d = *m_Data;
	int i = d.tokens.Find(id);
	if (i < 0)
		return FALSE;
	d.tokens[i].selected = select != FALSE;
	if (select)
		d.tokens.SetAnchor(i);
	d.lastSelection.clear();   // the API's: not told
	for (auto& t : d.tokens.Tokens())
		if (t.selected)
			d.lastSelection.push_back(t.id);
	if (IsWindow())
		Invalidate(FALSE);
	return TRUE;
}

void CTokenEditCtrl::SelectAll(BOOL select) {
	auto& d = *m_Data;
	d.tokens.SelectAll(select != FALSE);
	d.lastSelection.clear();
	for (auto& t : d.tokens.Tokens())
		if (t.selected)
			d.lastSelection.push_back(t.id);
	if (IsWindow())
		Invalidate(FALSE);
}

int CTokenEditCtrl::GetSelectedCount() const {
	return (int)m_Data->tokens.SelectedCount();
}

int CTokenEditCtrl::GetSelection(UINT* ids, int count) const {
	int n = 0;
	for (auto& t : m_Data->tokens.Tokens())
		if (t.selected) {
			if (ids && n < count)
				ids[n] = t.id;
			n++;
		}
	return n;
}

// ---- Text ----------------------------------------------------------------------------------------

int CTokenEditCtrl::GetText(LPWSTR text, int cch) const {
	auto& d = *m_Data;
	std::vector<std::wstring> texts;
	texts.reserve(d.tokens.Size());
	for (auto& t : d.tokens.Tokens())
		texts.push_back(t.text);
	return CopyText(TkeJoin(texts, d.separators, std::wstring(1, d.separators[0]) + L" "), text, cch);
}

void CTokenEditCtrl::SetText(LPCWSTR text) {
	auto& d = *m_Data;
	d.tokens.Clear();
	d.editAt = -1;
	d.hot = d.pressed = {};
	d.scroll = 0;
	for (auto& piece : TkeSplit(Text(text), d.separators)) {
		if (Duplicate(piece))
			continue;
		TkeToken t;
		t.text = std::move(piece);
		d.tokens.Insert(d.tokens.Size(), std::move(t));
	}
	ClearHistory();
	Changed(true);
	NotifySelection();
}

void CTokenEditCtrl::SetSeparators(LPCWSTR separators) {
	m_Data->separators = separators && *separators ? separators : L",;";
}

void CTokenEditCtrl::SetCueBanner(LPCWSTR text) {
	m_Data->cue = Text(text);
	m_Data->cueApplied = false;   // the new text, either way
	UpdateCue();
}

void CTokenEditCtrl::SetMaxTokens(int count) {
	m_Data->maxTokens = std::max(0, count);
}

// ---- Layout ---------------------------------------------------------------------------

void CTokenEditCtrl::UpdateCue() {
	// the edit shows it whenever it's empty: only while there are no tokens
	auto& d = *m_Data;
	const bool show = d.tokens.Empty() && !d.cue.empty();
	if (!m_Edit.IsWindow() || (show == d.cueShown && d.cueApplied))
		return;
	d.cueShown = show;
	d.cueApplied = true;
	m_Edit.SetCueBannerText(show ? d.cue.c_str() : L"", TRUE);
}

void CTokenEditCtrl::Changed(bool measure) {
	auto& d = *m_Data;
	if (measure)
		d.measure = true;
	d.dirty = true;
	if (!IsWindow())
		return;
	// at once rather than at the next paint: the height notification shouldn't come from WM_PAINT
	EnsureLayout();
	Invalidate(FALSE);
	NotifyHeight();
}

void CTokenEditCtrl::EnsureLayout() {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	if (d.measure) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TkeMeasureTokens(d.tokens.Tokens(), [&](std::wstring_view s) {
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
	d.dirty = false;
	RECT rc;
	GetClientRect(&rc);
	auto m = d.m;
	const bool readOnly = ReadOnly();
	if (readOnly)
		m.minEdit = m.midEdit = 0;   // no typing area
	TkeLayoutInput in;
	in.width = rc.right;
	in.editAt = readOnly ? -1 : d.editAt;
	in.editWidth = readOnly ? 0 : EditWidth();
	in.closeButtons = !(GetStyle() & TKES_NOCLOSE) && !readOnly;
	in.icons = m_Images != nullptr;
	d.layout = TkeLayoutTokens(d.tokens.Tokens(), in, m);
	UpdateCue();
	d.scroll = TkeClampScroll(d.scroll, rc.bottom, d.layout.height);
	PlaceEdit();
}

void CTokenEditCtrl::ScrollTo(int y) {
	auto& d = *m_Data;
	RECT rc;
	GetClientRect(&rc);
	y = TkeClampScroll(y, rc.bottom, d.layout.height);
	if (y == d.scroll)
		return;
	d.scroll = y;
	PlaceEdit();
	Invalidate(FALSE);
}

void CTokenEditCtrl::ScrollToShow(const RECT& r) {
	auto& d = *m_Data;
	RECT rc;
	GetClientRect(&rc);
	ScrollTo(TkeScrollToShow(d.scroll, r.top - d.m.padding, r.bottom + d.m.padding, rc.bottom, d.layout.height));
}

// ---- Mouse ---------------------------------------------------------------------------------------

TkeHit CTokenEditCtrl::HitTest(POINT pt) {
	auto& d = *m_Data;
	EnsureLayout();
	pt.y += d.scroll;
	auto hit = TkeHitTest(d.layout, pt);
	if (hit.part == TkePart::Close && (ReadOnly() || d.tokens[hit.index].locked))
		hit.part = TkePart::Token;
	return hit;
}

void CTokenEditCtrl::SetHot(const TkeHit& hit) {
	auto& d = *m_Data;
	if (!(hit == d.hot)) {
		d.hot = hit;
		Invalidate(FALSE);
	}
}

LRESULT CTokenEditCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_hWnd || LOWORD(lParam) != HTCLIENT) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	auto hit = HitTest(pt);
	const bool text = hit.part != TkePart::Token && hit.part != TkePart::Close && !ReadOnly();
	::SetCursor(::LoadCursor(nullptr, text ? IDC_IBEAM : IDC_ARROW));
	return TRUE;
}

LRESULT CTokenEditCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (!d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	SetHot(HitTest(PointOf(lParam)));
	return 0;
}

LRESULT CTokenEditCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_Data->tracking = false;
	SetHot({});
	return 0;
}

LRESULT CTokenEditCtrl::OnLButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	const POINT pt = PointOf(lParam);
	auto hit = HitTest(pt);
	switch (hit.part) {
		case TkePart::Close:
			d.pressed = hit;
			SetCapture();
			Invalidate(FALSE);
			break;

		case TkePart::Token: {
			const bool ctrl = (wParam & MK_CONTROL) != 0, shift = (wParam & MK_SHIFT) != 0;
			d.tokens.Select(hit.index, ctrl && shift ? TkeSelect::ExtendAdd : shift ? TkeSelect::Extend : ctrl ? TkeSelect::Toggle
				: TkeSelect::Only);
			d.focus = hit.index;
			SetFocus();
			ScrollToShow(d.layout.tokens[hit.index].rc);
			Invalidate(FALSE);
			NotifySelection();
			break;
		}

		default:
			if (ReadOnly()) {
				d.tokens.SelectAll(false);
				SetFocus();
				Invalidate(FALSE);
				NotifySelection();
				break;
			}
			// the caret goes where it was clicked among the tokens
			{
				POINT content{ pt.x, pt.y + d.scroll };
				int at = TkeDropIndex(d.layout, content);
				SetEditAt(at >= (int)d.tokens.Size() ? -1 : at);
			}
			FocusEdit();
			CaretToEnd();
			break;
	}
	return 0;
}

LRESULT CTokenEditCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part != TkePart::Close)
		return 0;
	auto pressed = d.pressed;
	d.pressed = {};
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate(FALSE);
	if (HitTest(PointOf(lParam)) == pressed) {
		RemoveByUser({ (size_t)pressed.index });
		FocusEdit();
	}
	return 0;
}

LRESULT CTokenEditCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part != TkePart::None) {
		d.pressed = {};
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CTokenEditCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	RECT rc;
	GetClientRect(&rc);
	if (d.layout.height <= rc.bottom) {
		bHandled = FALSE;   // the parent's to scroll
		return 0;
	}
	const int lines = -GET_WHEEL_DELTA_WPARAM(wParam) * 3 / WHEEL_DELTA;
	ScrollTo(d.scroll + lines * (d.m.tokenHeight + d.m.lineGap) / 3);
	return 0;
}

// ---- Keyboard ----------------------------------------------------------------------------------

LRESULT CTokenEditCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (!KeyDown((UINT)wParam))
		bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnChar(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if (wParam >= L' ' && !ReadOnly() && m_Edit.IsWindow()) {
		// typing goes to the typing area
		d.tokens.SelectAll(false);
		Invalidate(FALSE);
		NotifySelection();
		FocusEdit();
		CaretToEnd();
		m_Edit.SendMessage(WM_CHAR, wParam, lParam);
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// with no token selected the focus belongs in the typing area (tabbing into the control)
	if (!m_Data->tokens.SelectedCount() && !ReadOnly() && m_Edit.IsWindow())
		m_Edit.SetFocus();
	Invalidate(FALSE);
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnKillFocus(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if ((HWND)wParam != m_Edit.m_hWnd)
		LeaveTyping();
	if (IsWindow())
		Invalidate(FALSE);
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

// ---- Removing ---------------------------------------------------------------------------------

namespace {
	NMTKETOKEN NotifyOf(const TkeToken& t, int index) {
		NMTKETOKEN nm{};
		nm.id = t.id;
		nm.index = index;
		nm.pszText = t.text.c_str();
		nm.pszTip = t.tip.c_str();
		nm.iImage = t.image;
		nm.crBack = t.back;
		nm.crText = t.fore;
		nm.flags = FlagsOf(t);
		nm.lParam = t.lParam;
		return nm;
	}
}

bool CTokenEditCtrl::RemoveByUser(std::vector<size_t> indexes) {
	auto& d = *m_Data;
	std::vector<UINT> ids;
	ids.reserve(indexes.size());
	for (auto i : indexes)
		if (i < d.tokens.Size())
			ids.push_back(d.tokens[i].id);
	auto before = d.tokens.Save();
	bool removed = false;
	for (auto id : ids) {
		int i = d.tokens.Find(id);
		if (i < 0 || d.tokens[i].locked)
			continue;
		auto nm = NotifyOf(d.tokens[i], i);
		if (FireNotify(&nm.hdr, TKEN_TOKENREMOVING) || !IsWindow())
			continue;
		i = d.tokens.Find(id);   // the app may have changed them meanwhile
		if (i < 0)
			continue;
		TkeToken gone = d.tokens[i];
		d.tokens.Remove(i);
		if (d.editAt > i)
			d.editAt--;
		if (d.focus >= i)
			d.focus--;
		removed = true;
		nm = NotifyOf(gone, i);
		FireNotify(&nm.hdr, TKEN_TOKENREMOVED);
		if (!IsWindow())
			return true;
	}
	if (!removed)
		return false;
	d.hot = d.pressed = {};
	d.history.Push(std::move(before), d.tokens.Save());
	Changed(true);
	NotifyChanged();
	NotifySelection();
	return true;
}
