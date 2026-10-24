#include "DiffView.h"
#include "DiffViewModel.h"

#include <uxtheme.h>

#include <algorithm>
#include <climits>

#pragma comment(lib, "uxtheme.lib")

// ---- Helpers -----------------------------------------------------------------------

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

static bool IsDark(COLORREF color) {
	return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
}

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static int Digits(size_t n) {
	int digits = 1;
	while (n >= 10) {
		n /= 10;
		digits++;
	}
	return digits;
}

static DfOptions MakeOptions(UINT flags) {
	DfOptions options;
	options.ignoreCase = (flags & DFVF_IGNORECASE) != 0;
	options.whitespace = (flags & DFVF_IGNORESPACE) ? DfWhitespace::All : (flags & DFVF_IGNORESPACECHANGE) ? DfWhitespace::Change : DfWhitespace::Exact;
	options.intraLine = (flags & DFVF_NOINTRALINE) == 0;
	return options;
}

static std::wstring_view View(LPCWSTR text, int cch) {
	if (!text)
		return {};
	return { text, cch < 0 ? wcslen(text) : (size_t)cch };
}

// ---- Construction ------------------------------------------------------------------

CDiffViewCtrl::CDiffViewCtrl() : m_Doc(std::make_unique<DfDocument>()) {
}

CDiffViewCtrl::~CDiffViewCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CDiffViewCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_VSCROLL | WS_HSCROLL, SWP_FRAMECHANGED);
	UpdateFonts();
	UpdateMetrics();
	UpdateScrollBarTheme();
	UpdateScrollBars();
	return 0;
}

LRESULT CDiffViewCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CDiffViewCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS;
}

// ---- Metrics and fonts -------------------------------------------------------------

int CDiffViewCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CDiffViewCtrl::UpdateFonts() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		LOGFONTW lf{};
		lf.lfHeight = -::MulDiv(10, m_Dpi, 72);
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = DEFAULT_CHARSET;
		lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
		wcscpy_s(lf.lfFaceName, L"Consolas");
		m_hFont = ::CreateFontIndirectW(&lf);
	}
	// the titles': the message font
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
	if (!m_UiFont.IsNull())
		m_UiFont.DeleteObject();
	m_UiFont.CreateFontIndirect(&ncm.lfMessageFont);
}

void CDiffViewCtrl::UpdateMetrics() {
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	SIZE digit{};
	dc.GetTextExtent(L"0", 1, &digit);
	dc.SelectFont(m_UiFont);
	TEXTMETRICW tmUi{};
	dc.GetTextMetrics(&tmUi);
	dc.SelectFont(old);
	m_UiTextHeight = tmUi.tmHeight;

	m_RowHeight = tm.tmHeight + Scale(1);
	m_TextOffset = (m_RowHeight - tm.tmHeight) / 2;
	m_CharWidth = std::max(1, (int)tm.tmAveCharWidth);
	m_FixedPitch = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) == 0;   // the bit set means variable pitch
	m_DigitWidth = std::max(1, (int)digit.cx);
	m_Pad = Scale(4);
	m_MarkerWidth = Scale(3);
}

LRESULT CDiffViewCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	UpdateScrollBars();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CDiffViewCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CDiffViewCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	UpdateScrollBars();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CDiffViewCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UpdateScrollBarTheme();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CDiffViewCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {   // the titles' font
		UpdateFonts();
		UpdateMetrics();
		UpdateScrollBars();
		Invalidate();
	}
	bHandled = FALSE;
	return 0;
}

// ---- Texts ---------------------------------------------------------------------------

void CDiffViewCtrl::SetTexts(LPCWSTR left, LPCWSTR right, int cchLeft, int cchRight) {
	m_Doc->left = DfLines(View(left, cchLeft));
	m_Doc->right = DfLines(View(right, cchRight));
	Recompute();
}

void CDiffViewCtrl::SetText(bool right, LPCWSTR text, int cch) {
	(right ? m_Doc->right : m_Doc->left) = DfLines(View(text, cch));
	Recompute();
}

ATL::CString CDiffViewCtrl::GetText(bool right) const {
	auto text = (right ? m_Doc->right : m_Doc->left).Join(L"\r\n");
	return ATL::CString(text.c_str(), (int)std::min<size_t>(text.size(), INT_MAX));
}

size_t CDiffViewCtrl::GetLineCount(bool right) const {
	return (right ? m_Doc->right : m_Doc->left).size();
}

ATL::CString CDiffViewCtrl::GetLineText(bool right, size_t line) const {
	auto& lines = right ? m_Doc->right : m_Doc->left;
	if (line >= lines.size())
		return {};
	auto text = lines[line];
	return ATL::CString(text.data(), (int)std::min<size_t>(text.size(), INT_MAX));
}

void CDiffViewCtrl::Clear() {
	m_Doc->left = DfLines();
	m_Doc->right = DfLines();
	m_TopRow = 0;
	m_Left = 0;
	Recompute();
}

void CDiffViewCtrl::SetTitles(LPCWSTR left, LPCWSTR right) {
	m_Titles[0] = left;
	m_Titles[1] = right;
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

// ---- The diff ------------------------------------------------------------------------

CDiffViewCtrl::Anchor CDiffViewCtrl::GetAnchor() const {
	auto& rows = m_Doc->rows;
	if (rows.empty())
		return {};
	auto& row = rows[std::min(m_TopRow, rows.size() - 1)];
	if (row.left >= 0)
		return { false, (size_t)row.left };
	return { true, (size_t)row.right };
}

void CDiffViewCtrl::SetAnchor(const Anchor& anchor) {
	if (anchor.line == DFV_NOLINE) {
		m_TopRow = 0;
		return;
	}
	int row = DfRowOfLine(m_Doc->rows, anchor.right, anchor.line);
	m_TopRow = row < 0 ? m_Doc->rows.size() : (size_t)row;   // clamped by UpdateScrollBars
}

void CDiffViewCtrl::Recompute() {
	auto anchor = GetAnchor();
	m_Doc->Compute(MakeOptions(m_Options), m_TabSize);
	m_Doc->rows = DfBuildRows(m_Doc->diff, m_Layout == DFVL_UNIFIED ? DfLayout::Unified : DfLayout::SideBySide, m_Context, m_Doc->expanded);
	SetAnchor(anchor);
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

void CDiffViewCtrl::RebuildRows() {
	auto anchor = GetAnchor();
	m_Doc->rows = DfBuildRows(m_Doc->diff, m_Layout == DFVL_UNIFIED ? DfLayout::Unified : DfLayout::SideBySide, m_Context, m_Doc->expanded);
	SetAnchor(anchor);
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

void CDiffViewCtrl::SetOptions(UINT flags) {
	if (flags == m_Options)
		return;
	m_Options = flags;
	Recompute();
}

UINT CDiffViewCtrl::GetOptions() const {
	return m_Options;
}

void CDiffViewCtrl::GetInfo(DFVINFO* pInfo) const {
	if (!pInfo)
		return;
	auto& diff = m_Doc->diff;
	pInfo->leftLines = m_Doc->left.size();
	pInfo->rightLines = m_Doc->right.size();
	pInfo->changes = diff.changes;
	pInfo->deleted = diff.deleted;
	pInfo->inserted = diff.inserted;
}

size_t CDiffViewCtrl::GetChangeCount() const {
	return m_Doc->changeBlocks.size();
}

BOOL CDiffViewCtrl::GetChange(size_t index, DFVCHANGE* pChange) const {
	if (!pChange || index >= m_Doc->changeBlocks.size())
		return FALSE;
	auto& b = m_Doc->diff.blocks[m_Doc->changeBlocks[index]];
	pChange->leftLine = b.left;
	pChange->leftCount = b.leftCount;
	pChange->rightLine = b.right;
	pChange->rightCount = b.rightCount;
	return TRUE;
}

int CDiffViewCtrl::GetChangeOfLine(bool right, size_t line) const {
	size_t block = DfBlockOfLine(m_Doc->diff, right, line);
	return block < m_Doc->diff.blocks.size() ? m_Doc->ChangeOfBlock(block) : -1;
}

ATL::CString CDiffViewCtrl::GetUnifiedDiff(int context, LPCWSTR leftName, LPCWSTR rightName) const {
	auto text = DfUnifiedText(m_Doc->left, m_Doc->right, m_Doc->diff, context, leftName ? leftName : L"a", rightName ? rightName : L"b", L"\r\n");
	return ATL::CString(text.c_str(), (int)std::min<size_t>(text.size(), INT_MAX));
}

// ---- View ------------------------------------------------------------------------------

void CDiffViewCtrl::SetLayout(DFVLAYOUT layout) {
	if (layout == m_Layout)
		return;
	m_Layout = layout;
	RebuildRows();
}

DFVLAYOUT CDiffViewCtrl::GetLayout() const {
	return m_Layout;
}

void CDiffViewCtrl::SetContextLines(int lines) {
	lines = std::max(lines, DFV_ALLCONTEXT);
	if (lines == m_Context)
		return;
	m_Context = lines;
	m_Doc->expanded.clear();
	RebuildRows();
}

int CDiffViewCtrl::GetContextLines() const {
	return m_Context;
}

void CDiffViewCtrl::ShowLineNumbers(bool show) {
	m_LineNumbers = show;
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

bool CDiffViewCtrl::AreLineNumbersShown() const {
	return m_LineNumbers;
}

void CDiffViewCtrl::SetTabSize(int columns) {
	columns = std::clamp(columns, 1, 16);
	if (columns == m_TabSize)
		return;
	m_TabSize = columns;
	m_Doc->UpdateColumns(columns);
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

int CDiffViewCtrl::GetTabSize() const {
	return m_TabSize;
}

size_t CDiffViewCtrl::GetRowCount() const {
	return m_Doc->rows.size();
}

void CDiffViewCtrl::SetTopRow(size_t row) {
	ScrollTo(row, m_Left);
}

size_t CDiffViewCtrl::GetTopRow() const {
	return m_TopRow;
}

int CDiffViewCtrl::GetVisibleRowCount() const {
	return PageRows();
}

void CDiffViewCtrl::EnsureLineVisible(bool right, size_t line) {
	int row = DfRowOfLine(m_Doc->rows, right, line);
	if (row < 0)
		return;
	size_t page = PageRows();
	if ((size_t)row < m_TopRow)
		ScrollTo(row, m_Left);
	else if ((size_t)row >= m_TopRow + page)
		ScrollTo(row - page + 1, m_Left);
}

void CDiffViewCtrl::EnsureChangeVisible(size_t index) {
	if (index >= m_Doc->changeBlocks.size())
		return;
	auto block = (uint32_t)m_Doc->changeBlocks[index];
	auto& rows = m_Doc->rows;
	auto first = std::find_if(rows.begin(), rows.end(), [&](const DfRow& r) { return r.block == block; });
	if (first == rows.end())
		return;
	size_t start = first - rows.begin(), end = start;
	while (end < rows.size() && rows[end].block == block)
		end++;
	size_t page = PageRows();
	if (start >= m_TopRow && end <= m_TopRow + page)
		return;
	// a third of the room left above it
	size_t height = end - start;
	ScrollTo(height >= page ? start : start - std::min(start, (page - height) / 3), m_Left);
}

// ---- Layout and scrolling ----------------------------------------------------------

int CDiffViewCtrl::TitleHeight() const {
	if (m_Titles[0].IsEmpty() && m_Titles[1].IsEmpty())
		return 0;
	return m_UiTextHeight + 2 * Scale(4) + 1;
}

RECT CDiffViewCtrl::GetRowsRect() const {
	RECT rc{};
	if (IsWindow())
		GetClientRect(&rc);
	rc.top = std::min(rc.bottom, (LONG)TitleHeight());
	return rc;
}

int CDiffViewCtrl::GutterWidth() const {
	int digits = std::max(3, Digits(std::max(m_Doc->left.size(), m_Doc->right.size())));
	int numbers = m_LineNumbers ? digits * m_DigitWidth + m_Pad : 0;
	if (m_Layout == DFVL_UNIFIED)
		return m_MarkerWidth + m_Pad + 2 * numbers + m_DigitWidth + 2 * m_Pad;   // two numbers, then the sign
	return m_MarkerWidth + m_Pad + numbers + m_Pad;
}

CDiffViewCtrl::Pane CDiffViewCtrl::GetPane(int index) const {
	auto rc = GetRowsRect();
	int gutter = GutterWidth();
	if (m_Layout == DFVL_UNIFIED)
		return { 0, std::min(gutter, (int)rc.right), (int)rc.right };
	int center = Scale(6);
	int half = std::max(0, ((int)rc.right - center) / 2);
	if (index == 0)
		return { 0, std::min(gutter, half), half };
	int left = half + center;
	return { left, std::min(left + gutter, (int)rc.right), std::max(left, (int)rc.right) };
}

int CDiffViewCtrl::PageRows() const {
	auto rc = GetRowsRect();
	return std::max(1, (int)(rc.bottom - rc.top) / m_RowHeight);
}

size_t CDiffViewCtrl::MaxTopRow() const {
	size_t count = m_Doc->rows.size();
	size_t page = PageRows();
	return count > page ? count - page : 0;
}

int CDiffViewCtrl::TextWidth() const {
	auto pane = GetPane(0);
	return std::max(0, pane.right - pane.text);
}

int CDiffViewCtrl::MaxLeft() const {
	auto content = (long long)m_Doc->maxColumns * m_CharWidth + 2 * m_Pad;
	return (int)std::clamp(content - TextWidth(), 0LL, (long long)INT_MAX);
}

void CDiffViewCtrl::UpdateScrollBars() {
	if (!IsWindow())
		return;
	m_TopRow = std::min(m_TopRow, MaxTopRow());
	m_Left = std::clamp(m_Left, 0, MaxLeft());

	// the scroll bar's range is an int: past INT_MAX rows the bottom can't be reached with it
	SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
	si.nMax = (int)std::min<size_t>(m_Doc->rows.size(), INT_MAX) - 1;
	si.nPage = PageRows();
	si.nPos = (int)std::min<size_t>(m_TopRow, INT_MAX);
	SetScrollInfo(SB_VERT, &si, TRUE);

	si.nMax = (int)std::min<long long>((long long)m_Doc->maxColumns * m_CharWidth + 2 * m_Pad, INT_MAX) - 1;
	si.nPage = TextWidth();
	si.nPos = m_Left;
	SetScrollInfo(SB_HORZ, &si, TRUE);
}

void CDiffViewCtrl::ScrollTo(size_t topRow, int left) {
	topRow = std::min(topRow, MaxTopRow());
	left = std::clamp(left, 0, MaxLeft());
	if (topRow == m_TopRow && left == m_Left)
		return;
	m_TopRow = topRow;
	m_Left = left;
	UpdateScrollBars();
	auto rc = GetRowsRect();
	InvalidateRect(&rc, FALSE);
}

LRESULT CDiffViewCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateScrollBars();
	Invalidate();
	return 0;
}

LRESULT CDiffViewCtrl::OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	bool horizontal = uMsg == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT);
	if (horizontal) {
		UINT chars = 3;
		::SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &chars, 0);
		if (uMsg == WM_MOUSEWHEEL)
			delta = -delta;   // Shift+wheel down goes right
		m_HWheelRemainder += delta;
		int steps = m_HWheelRemainder / WHEEL_DELTA;
		m_HWheelRemainder -= steps * WHEEL_DELTA;
		ScrollTo(m_TopRow, m_Left + steps * (int)chars * m_CharWidth);
		return 0;
	}
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	if (lines == WHEEL_PAGESCROLL)
		lines = PageRows();
	m_WheelRemainder += delta;
	int steps = m_WheelRemainder / WHEEL_DELTA;
	m_WheelRemainder -= steps * WHEEL_DELTA;
	long long move = -(long long)steps * lines;
	size_t top = move < 0 ? m_TopRow - std::min<size_t>(m_TopRow, (size_t)-move) : m_TopRow + (size_t)move;
	ScrollTo(top, m_Left);
	return 0;
}

LRESULT CDiffViewCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	size_t page = PageRows();
	size_t top = m_TopRow;
	switch (LOWORD(wParam)) {
		case SB_LINEUP: top -= std::min<size_t>(top, 1); break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP: top -= std::min(top, page); break;
		case SB_PAGEDOWN: top += page; break;
		case SB_TOP: top = 0; break;
		case SB_BOTTOM: top = MaxTopRow(); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_VERT, &si);
			top = (size_t)si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollTo(top, m_Left);
	return 0;
}

LRESULT CDiffViewCtrl::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	int left = m_Left;
	int page = std::max(m_CharWidth, TextWidth());
	switch (LOWORD(wParam)) {
		case SB_LINELEFT: left -= 4 * m_CharWidth; break;
		case SB_LINERIGHT: left += 4 * m_CharWidth; break;
		case SB_PAGELEFT: left -= page; break;
		case SB_PAGERIGHT: left += page; break;
		case SB_LEFT: left = 0; break;
		case SB_RIGHT: left = MaxLeft(); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_HORZ, &si);
			left = si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollTo(m_TopRow, left);
	return 0;
}

// ---- Input ---------------------------------------------------------------------------------

LRESULT CDiffViewCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	auto rc = GetRowsRect();
	if (!::PtInRect(&rc, pt))
		return 0;
	size_t row = m_TopRow + (pt.y - rc.top) / m_RowHeight;
	auto& rows = m_Doc->rows;
	if (row < rows.size() && rows[row].kind == DfRowKind::Fold) {
		// a click on a fold shows its lines
		auto block = rows[row].block;
		if (m_Doc->expanded.size() <= block)
			m_Doc->expanded.resize(block + 1);
		m_Doc->expanded[block] = 1;
		size_t top = m_TopRow;
		m_Doc->rows = DfBuildRows(m_Doc->diff, m_Layout == DFVL_UNIFIED ? DfLayout::Unified : DfLayout::SideBySide, m_Context, m_Doc->expanded);
		m_TopRow = top;   // the lines open below the click, the rows above stay put
		UpdateScrollBars();
		Invalidate();
	}
	return 0;
}

// ---- Colors ----------------------------------------------------------------------------------

void CDiffViewCtrl::SetColors(const DFVCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	UpdateScrollBarTheme();
	if (IsWindow())
		Invalidate();
}

DFVCOLORS CDiffViewCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	DFVCOLORS c;
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
	bool dark = IsDark(c.crBack);
	const COLORREF red = RGB(0xE8, 0x48, 0x48), green = RGB(0x3C, 0xB8, 0x4C);
	c.crGutterBack = Blend(c.crBack, c.crText, 5);
	c.crGutterText = Blend(c.crBack, c.crText, 45);
	c.crDeletedBack = Blend(c.crBack, red, dark ? 15 : 15);
	c.crDeletedMark = Blend(c.crBack, red, dark ? 42 : 36);
	c.crInsertedBack = Blend(c.crBack, green, dark ? 13 : 15);
	c.crInsertedMark = Blend(c.crBack, green, dark ? 38 : 36);
	c.crFiller = Blend(c.crBack, c.crText, 14);
	c.crFoldBack = Blend(c.crBack, ::GetSysColor(COLOR_HIGHLIGHT), 10);
	c.crFoldText = Blend(c.crBack, c.crText, 55);
	c.crTitleBack = Blend(c.crBack, c.crText, 8);
	c.crTitleText = c.crText;
	return c;
}

void CDiffViewCtrl::UpdateScrollBarTheme() {
	// the dark Explorer theme makes the scroll bars dark. Set only when it changes: SetWindowTheme sends
	// WM_THEMECHANGED, which comes back here.
	if (!IsWindow())
		return;
	int dark = IsDark(GetColors().crBack) ? 1 : 0;
	if (dark == m_DarkScrollBars)
		return;
	m_DarkScrollBars = dark;
	::SetWindowTheme(m_hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// ---- Painting ---------------------------------------------------------------------------

LRESULT CDiffViewCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		GetClientRect(&rc);
		Paint((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC, dc.m_ps.rcPaint);
	return 0;
}

void CDiffViewCtrl::Paint(WTL::CDCHandle dc, const RECT& rcPaint) {
	auto c = GetColors();
	RECT client;
	GetClientRect(&client);
	auto rc = GetRowsRect();
	// the filler's hatching lines up across paints of parts of the window
	dc.SetBrushOrg((8 - rcPaint.left % 8) % 8, (8 - rcPaint.top % 8) % 8);
	dc.SetBkMode(TRANSPARENT);
	if (rcPaint.top < rc.top)
		PaintTitles(dc, c);

	auto oldFont = dc.SelectFont(m_hFont);
	auto& rows = m_Doc->rows;
	size_t row = m_TopRow + std::max(0, (int)(rcPaint.top - rc.top)) / m_RowHeight;
	int y = rc.top + (int)(row - m_TopRow) * m_RowHeight;
	for (; y < rcPaint.bottom && row < rows.size(); row++, y += m_RowHeight)
		PaintRow(dc, row, y, c, client.right);
	if (y < rcPaint.bottom) {
		// below the last row: the gutters and the background
		int panes = m_Layout == DFVL_UNIFIED ? 1 : 2;
		FillSolid(dc, { 0, y, (int)client.right, (int)rcPaint.bottom }, c.crGutterBack);
		for (int i = 0; i < panes; i++) {
			auto pane = GetPane(i);
			FillSolid(dc, { pane.text, y, pane.right, (int)rcPaint.bottom }, c.crBack);
		}
	}
	dc.SelectFont(oldFont);
}

void CDiffViewCtrl::PaintTitles(WTL::CDCHandle dc, const DFVCOLORS& c) {
	RECT client;
	GetClientRect(&client);
	int height = TitleHeight();
	FillSolid(dc, { 0, 0, (int)client.right, height - 1 }, c.crTitleBack);
	FillSolid(dc, { 0, height - 1, (int)client.right, height }, Blend(c.crTitleBack, c.crTitleText, 20));
	auto old = dc.SelectFont(m_UiFont);
	dc.SetTextColor(c.crTitleText);
	auto draw = [&](const ATL::CString& text, int left, int right) {
		RECT r{ left + m_Pad, 0, right - m_Pad, height - 1 };
		dc.DrawText(text, text.GetLength(), &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	};
	if (m_Layout == DFVL_UNIFIED) {
		ATL::CString text = m_Titles[0];
		if (!m_Titles[1].IsEmpty())
			text += (text.IsEmpty() ? L"" : L"  \x2192  ") + m_Titles[1];
		auto pane = GetPane(0);
		draw(text, pane.left, pane.right);
	}
	else {
		for (int i = 0; i < 2; i++) {
			auto pane = GetPane(i);
			draw(m_Titles[i], pane.left, pane.right);
		}
	}
	dc.SelectFont(old);
}

void CDiffViewCtrl::PaintNumber(WTL::CDCHandle dc, int right, int y, int line, COLORREF color) {
	wchar_t number[24];
	_itow_s(line + 1, number, 10);
	dc.SetTextColor(color);
	dc.SetTextAlign(TA_RIGHT | TA_TOP);
	dc.ExtTextOut(right, y + m_TextOffset, 0, nullptr, number, (UINT)wcslen(number), nullptr);
	dc.SetTextAlign(TA_LEFT | TA_TOP);
}

void CDiffViewCtrl::FillHatch(WTL::CDCHandle dc, const RECT& rc, COLORREF back, COLORREF hatch) {
	FillSolid(dc, rc, back);
	WTL::CBrush brush;
	brush.CreateHatchBrush(HS_BDIAGONAL, hatch);
	dc.FillRect(&rc, brush);   // transparent background mode: only the lines
}

void CDiffViewCtrl::PaintRow(WTL::CDCHandle dc, size_t index, int y, const DFVCOLORS& c, int right) {
	auto& row = m_Doc->rows[index];
	auto& diff = m_Doc->diff;
	const int bottom = y + m_RowHeight;
	const bool unified = m_Layout == DFVL_UNIFIED;
	const int panes = unified ? 1 : 2;
	const int digits = std::max(3, Digits(std::max(m_Doc->left.size(), m_Doc->right.size())));
	const int numberWidth = digits * m_DigitWidth;

	// side by side: the strip between the panes
	if (!unified) {
		auto p0 = GetPane(0), p1 = GetPane(1);
		FillSolid(dc, { p0.right, y, p1.left, bottom }, c.crGutterBack);
		FillSolid(dc, { p1.right, y, right, bottom }, c.crBack);
	}

	if (row.kind == DfRowKind::Fold) {
		wchar_t text[64];
		swprintf_s(text, L"\x2026  %u unchanged lines", row.hidden);
		for (int i = 0; i < panes; i++) {
			auto pane = GetPane(i);
			RECT r{ pane.left, y, pane.right, bottom };
			FillSolid(dc, r, c.crFoldBack);
			dc.SetTextColor(c.crFoldText);
			r.left = pane.text + m_Pad;
			dc.ExtTextOut(r.left, y + m_TextOffset, ETO_CLIPPED, &r, text, (UINT)wcslen(text), nullptr);
		}
		return;
	}

	const bool changed = row.kind == DfRowKind::Changed;
	auto spansOf = [&](bool rightSide, int line) -> const std::vector<DfSpan>* {
		if (!row.paired || line < 0)
			return nullptr;
		int pair = rightSide ? diff.rightPair[line] : diff.leftPair[line];
		if (pair < 0)
			return nullptr;
		return rightSide ? &diff.pairs[pair].rightSpans : &diff.pairs[pair].leftSpans;
	};
	auto cell = [&](const Pane& pane, bool rightSide, int line) {
		RECT text{ pane.text, y, pane.right, bottom };
		if (line < 0) {
			FillHatch(dc, text, c.crBack, c.crFiller);
			return;
		}
		auto& lines = rightSide ? m_Doc->right : m_Doc->left;
		COLORREF back = !changed ? c.crBack : rightSide ? c.crInsertedBack : c.crDeletedBack;
		FillSolid(dc, text, back);
		PaintText(dc, text, lines[line], spansOf(rightSide, line), c.crText, rightSide ? c.crInsertedMark : c.crDeletedMark);
	};

	if (unified) {
		auto pane = GetPane(0);
		FillSolid(dc, { pane.left, y, pane.text, bottom }, c.crGutterBack);
		bool rightSide = row.left < 0;
		if (changed)
			FillSolid(dc, { pane.left, y, pane.left + m_MarkerWidth, bottom }, rightSide ? c.crInsertedMark : c.crDeletedMark);
		int x = pane.left + m_MarkerWidth + m_Pad;
		if (m_LineNumbers) {
			x += numberWidth;
			if (row.left >= 0)
				PaintNumber(dc, x, y, row.left, c.crGutterText);
			x += m_Pad + numberWidth;
			if (row.right >= 0)
				PaintNumber(dc, x, y, row.right, c.crGutterText);
			x += m_Pad;
		}
		if (changed) {
			dc.SetTextColor(c.crText);
			RECT r{ x, y, pane.text, bottom };
			dc.ExtTextOut(x, y + m_TextOffset, ETO_CLIPPED, &r, rightSide ? L"+" : L"-", 1, nullptr);
		}
		cell(pane, rightSide, rightSide ? row.right : row.left);
		return;
	}

	for (int side = 0; side < 2; side++) {
		auto pane = GetPane(side);
		int line = side ? row.right : row.left;
		FillSolid(dc, { pane.left, y, pane.text, bottom }, c.crGutterBack);
		if (changed && line >= 0)
			FillSolid(dc, { pane.left, y, pane.left + m_MarkerWidth, bottom }, side ? c.crInsertedMark : c.crDeletedMark);
		if (m_LineNumbers && line >= 0)
			PaintNumber(dc, pane.left + m_MarkerWidth + m_Pad + numberWidth, y, line, c.crGutterText);
		cell(pane, side == 1, line);
	}
}

void CDiffViewCtrl::PaintText(WTL::CDCHandle dc, const RECT& rc, std::wstring_view line, const std::vector<DfSpan>* spans, COLORREF text, COLORREF markBack) {
	if (line.empty() || rc.right <= rc.left)
		return;
	DfExpandTabs(line, m_TabSize, m_DrawBuffer);
	const int origin = rc.left + m_Pad - m_Left;
	auto columnX = [&](size_t column) {
		if (m_FixedPitch)
			return (int)std::min<size_t>(column * m_CharWidth, INT_MAX / 2);
		SIZE size{};
		dc.GetTextExtent(m_DrawBuffer.c_str(), (int)std::min<size_t>(column, 32 * 1024), &size);
		return (int)size.cx;
	};

	// the changed words under the text
	if (spans) {
		for (auto& span : *spans) {
			RECT r{ origin + columnX(DfColumnOf(line, span.start, m_TabSize)), rc.top,
				origin + columnX(DfColumnOf(line, span.start + span.length, m_TabSize)), rc.bottom };
			r.left = std::max(r.left, rc.left);
			r.right = std::min(r.right, rc.right);
			if (r.right > r.left)
				FillSolid(dc, r, markBack);
		}
	}

	// the columns in view only (with a fixed pitch), so a very long line costs no more than a short one
	size_t first = 0, last = m_DrawBuffer.size();
	if (m_FixedPitch) {
		int width = rc.right - rc.left;
		first = std::min<size_t>(last, (size_t)std::max(0, m_Left - m_Pad) / m_CharWidth);
		last = std::min<size_t>(last, (size_t)(std::max(0, m_Left - m_Pad) + width) / m_CharWidth + 2);
	}
	else {
		last = std::min<size_t>(last, 32 * 1024);   // GDI gets slow, then fails, on very long strings
	}
	if (first >= last)
		return;
	dc.SetTextColor(text);
	dc.ExtTextOut(origin + (int)first * m_CharWidth, rc.top + m_TextOffset, ETO_CLIPPED, &rc, m_DrawBuffer.c_str() + first, (UINT)(last - first), nullptr);
}
