#include "DiffView.h"
#include "DiffViewModel.h"

#include <uxtheme.h>

#include <algorithm>
#include <climits>
#include <thread>

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

CDiffViewCtrl::CDiffViewCtrl() : m_Doc(std::make_unique<DfDocument>()), m_Undo(std::make_unique<DfUndoStack>()) {
}

CDiffViewCtrl::~CDiffViewCtrl() {
	CancelCompare();
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

LRESULT CDiffViewCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	CancelCompare();   // its result has no window to go to
	bHandled = FALSE;
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
	if (m_HasCaret)
		CreateSystemCaret();   // the row's height
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
	m_Undo->items.clear();
	Compare(DfLines(View(left, cchLeft)), DfLines(View(right, cchRight)));
}

void CDiffViewCtrl::SetText(bool right, LPCWSTR text, int cch) {
	m_Undo->items.clear();
	DfLines lines(View(text, cch)), other = Latest().Lines(!right);
	if (right)
		Compare(std::move(other), std::move(lines));
	else
		Compare(std::move(lines), std::move(other));
}

ATL::CString CDiffViewCtrl::GetText(bool right) const {
	auto text = Latest().Lines(right).Join(L"\r\n");
	return ATL::CString(text.c_str(), (int)std::min<size_t>(text.size(), INT_MAX));
}

size_t CDiffViewCtrl::GetLineCount(bool right) const {
	return Latest().Lines(right).size();
}

ATL::CString CDiffViewCtrl::GetLineText(bool right, size_t line) const {
	auto& lines = Latest().Lines(right);
	if (line >= lines.size())
		return {};
	auto text = lines[line];
	return ATL::CString(text.data(), (int)std::min<size_t>(text.size(), INT_MAX));
}

void CDiffViewCtrl::Clear() {
	m_Undo->items.clear();
	m_TopRow = 0;
	m_Left = 0;
	Compare(DfLines(), DfLines());
}

void CDiffViewCtrl::SetTitles(LPCWSTR left, LPCWSTR right) {
	m_Titles[0] = left;
	m_Titles[1] = right;
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

// ---- Comparing ---------------------------------------------------------------------------

// A compare on the worker thread. The thread holds it too: a compare stopped or replaced runs on into nothing.
struct DfJob {
	std::unique_ptr<DfDocument> doc = std::make_unique<DfDocument>();
	DfOptions options;
	int tabSize = 4;
	std::atomic<bool> cancel{ false };
	HWND hwnd = nullptr;
	WPARAM number = 0;
};

const DfDocument& CDiffViewCtrl::Latest() const {
	return m_Job ? *m_Job->doc : *m_Doc;
}

void CDiffViewCtrl::CancelCompare() {
	if (m_Job) {
		m_Job->cancel = true;
		m_Job.reset();
	}
}

void CDiffViewCtrl::Compare(DfLines&& left, DfLines&& right) {
	bool comparing = m_Job != nullptr;
	CancelCompare();
	if (IsWindow() && left.size() + right.size() > m_BackgroundThreshold) {
		// the texts the thread compares are its own; the view keeps the last diff until it's done
		auto job = std::make_shared<DfJob>();
		job->doc->left = std::move(left);
		job->doc->right = std::move(right);
		job->options = MakeOptions(m_Options);
		job->options.cancel = &job->cancel;
		job->tabSize = m_TabSize;
		job->hwnd = m_hWnd;
		job->number = ++m_JobNumber;
		m_Job = job;
		std::thread([job] {
			job->doc->Compute(job->options, job->tabSize);
			if (!job->cancel)
				::PostMessage(job->hwnd, WM_DFV_DIFFDONE, job->number, 0);
		}).detach();
		m_HotMerge = -1;
		Invalidate();   // the note, and no arrows meanwhile
		return;
	}
	m_Doc->left = std::move(left);
	m_Doc->right = std::move(right);
	Recompute();
	if (comparing && IsWindow())
		Invalidate();
}

LRESULT CDiffViewCtrl::OnDiffDone(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (!m_Job || wParam != m_Job->number)
		return 0;   // stopped or replaced meanwhile
	auto anchor = GetAnchor();
	m_Doc = std::move(m_Job->doc);
	m_Job.reset();
	BuildRows();
	SetAnchor(anchor);
	ClampPositions();
	UpdateScrollBars();
	Invalidate();
	NMHDR hdr{};
	FireNotify(&hdr, DFVN_DIFFDONE);
	return 0;
}

void CDiffViewCtrl::SetBackgroundThreshold(size_t lines) {
	m_BackgroundThreshold = lines;
}

size_t CDiffViewCtrl::GetBackgroundThreshold() const {
	return m_BackgroundThreshold;
}

bool CDiffViewCtrl::IsComparing() const {
	return m_Job != nullptr;
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
	int row = m_Doc->RowOf(anchor.right, anchor.line);
	if (row < 0)
		row = DfRowOfLine(m_Doc->rows, anchor.right, anchor.line);   // past the end of the side
	m_TopRow = row < 0 ? m_Doc->rows.size() : (size_t)row;   // clamped by UpdateScrollBars
}

void CDiffViewCtrl::BuildRows() {
	m_Doc->BuildRows(m_Layout == DFVL_UNIFIED ? DfLayout::Unified : DfLayout::SideBySide, m_Context);
}

void CDiffViewCtrl::Recompute() {
	auto anchor = GetAnchor();
	m_Doc->Compute(MakeOptions(m_Options), m_TabSize);
	BuildRows();
	SetAnchor(anchor);
	ClampPositions();
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

void CDiffViewCtrl::RebuildRows() {
	auto anchor = GetAnchor();
	BuildRows();
	SetAnchor(anchor);
	ClampPositions();
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

void CDiffViewCtrl::ExpandFold(size_t row, bool notify) {
	auto& rows = m_Doc->rows;
	if (row >= rows.size() || rows[row].kind != DfRowKind::Fold)
		return;
	auto block = rows[row].block;
	if (m_Doc->expanded.size() <= block)
		m_Doc->expanded.resize(block + 1);
	m_Doc->expanded[block] = 1;
	size_t top = m_TopRow;
	BuildRows();
	m_TopRow = top;   // the lines open below the top, the rows above stay put
	UpdateScrollBars();
	Invalidate();
	if (notify) {
		NMHDR hdr{};
		FireNotify(&hdr, DFVN_FOLDEXPANDED);
	}
}

void CDiffViewCtrl::ExpandAll() {
	m_Doc->expanded.assign(m_Doc->diff.blocks.size(), 1);
	RebuildRows();
}

void CDiffViewCtrl::SetOptions(UINT flags) {
	if (flags == m_Options)
		return;
	m_Options = flags;
	auto& latest = Latest();
	Compare(DfLines(latest.left), DfLines(latest.right));
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
	int row = m_Doc->RowOf(right, line);
	if (row < 0)
		return;
	size_t page = PageRows();
	if ((size_t)row < m_TopRow)
		ScrollTo(row, m_Left);
	else if ((size_t)row >= m_TopRow + page)
		ScrollTo(row - page + 1, m_Left);
}

void CDiffViewCtrl::EnsureChangeVisible(size_t index) {
	if (index >= m_Doc->changeRows.size())
		return;
	size_t start = m_Doc->changeRows[index].first, end = start + m_Doc->changeRows[index].count;
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
	rc.right = std::max(0L, rc.right - OverviewWidth());
	return rc;
}

int CDiffViewCtrl::OverviewWidth() const {
	return m_ShowOverview ? Scale(14) : 0;
}

RECT CDiffViewCtrl::GetOverviewRect() const {
	RECT rc{};
	if (IsWindow())
		GetClientRect(&rc);
	rc.top = std::min(rc.bottom, (LONG)TitleHeight());
	rc.left = std::max(0L, rc.right - OverviewWidth());
	return rc;
}

void CDiffViewCtrl::ShowOverview(bool show) {
	if (show == m_ShowOverview)
		return;
	m_ShowOverview = show;
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

bool CDiffViewCtrl::IsOverviewShown() const {
	return m_ShowOverview;
}

void CDiffViewCtrl::ScrollToOverview(int y) {
	auto rc = GetOverviewRect();
	int height = std::max(1, (int)(rc.bottom - rc.top));
	double at = std::clamp((double)(y - rc.top) / height, 0.0, 1.0) * m_Doc->rows.size();
	size_t page = PageRows();
	ScrollTo((size_t)std::max(0.0, at - page / 2.0), m_Left);
}

BOOL CDiffViewCtrl::HitTest(DFVHITTEST* pHit) const {
	if (!pHit)
		return FALSE;
	pHit->part = DFVHT_NOWHERE;
	pHit->row = DFV_NOLINE;
	pHit->pos = {};
	pHit->change = -1;
	pHit->toRight = FALSE;
	POINT pt = pHit->pt;
	RECT client{};
	GetClientRect(&client);
	if (!::PtInRect(&client, pt))
		return TRUE;
	auto rc = GetRowsRect();
	auto overview = GetOverviewRect();
	if (pt.y < rc.top) {
		pHit->part = DFVHT_TITLE;
		return TRUE;
	}
	if (::PtInRect(&overview, pt)) {
		pHit->part = DFVHT_OVERVIEW;
		int height = std::max(1, (int)(overview.bottom - overview.top));
		if (!m_Doc->rows.empty())
			pHit->row = std::min(m_Doc->rows.size() - 1, (size_t)((double)(pt.y - overview.top) / height * m_Doc->rows.size()));
		return TRUE;
	}
	int change;
	bool toRight;
	if (MergeHitTest(pt, change, toRight)) {
		pHit->part = DFVHT_MERGE;
		pHit->change = change;
		pHit->toRight = toRight;
		return TRUE;
	}
	size_t row = m_TopRow + (pt.y - rc.top) / m_RowHeight;
	if (row >= m_Doc->rows.size()) {
		pHit->part = DFVHT_BELOW;
		return TRUE;
	}
	pHit->row = row;
	bool rightSide = m_Layout == DFVL_SIDEBYSIDE && PaneSideAt(pt.x);
	UINT part;
	pHit->pos = PosFromPoint(pt, rightSide, &part);
	pHit->part = part;
	return TRUE;
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
	int center = CenterWidth();
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
	UpdateCaret();
}

void CDiffViewCtrl::ScrollTo(size_t topRow, int left) {
	topRow = std::min(topRow, MaxTopRow());
	left = std::clamp(left, 0, MaxLeft());
	if (topRow == m_TopRow && left == m_Left)
		return;
	m_TopRow = topRow;
	m_Left = left;
	UpdateScrollBars();
	// the rows and the overview's frame
	RECT rc;
	GetClientRect(&rc);
	rc.top = GetRowsRect().top;
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
	c.crSelBack = ::GetSysColor(COLOR_HIGHLIGHT);
	c.crSelText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	c.crSelBackInactive = Blend(c.crBack, c.crText, 18);
	c.crSelTextInactive = c.crText;
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
	auto rc = GetRowsRect();
	// the filler's hatching lines up across paints of parts of the window
	dc.SetBrushOrg((8 - rcPaint.left % 8) % 8, (8 - rcPaint.top % 8) % 8);
	dc.SetBkMode(TRANSPARENT);
	if (rcPaint.top < rc.top)
		PaintTitles(dc, c);

	// unified, the selection is in rows
	m_SelRows[0] = m_SelRows[1] = -1;
	if (m_Layout == DFVL_UNIFIED && HasSelection()) {
		DFVPOS start, end;
		GetSelection(&start, &end);
		m_SelRows[0] = m_Doc->RowOf(start.right != FALSE, start.line);
		m_SelRows[1] = m_Doc->RowOf(end.right != FALSE, end.line);
	}

	auto oldFont = dc.SelectFont(m_hFont);
	auto& rows = m_Doc->rows;
	size_t row = m_TopRow + std::max(0, (int)(rcPaint.top - rc.top)) / m_RowHeight;
	int y = rc.top + (int)(row - m_TopRow) * m_RowHeight;
	for (; y < rcPaint.bottom && row < rows.size(); row++, y += m_RowHeight)
		PaintRow(dc, row, y, c, rc.right);
	if (y < rcPaint.bottom) {
		// below the last row: the gutters and the background
		int panes = m_Layout == DFVL_UNIFIED ? 1 : 2;
		FillSolid(dc, { 0, y, (int)rc.right, (int)rcPaint.bottom }, c.crGutterBack);
		for (int i = 0; i < panes; i++) {
			auto pane = GetPane(i);
			FillSolid(dc, { pane.text, y, pane.right, (int)rcPaint.bottom }, c.crBack);
		}
	}
	if (m_Layout == DFVL_SIDEBYSIDE && m_MergeFlags)
		PaintMerge(dc, c);
	if (m_Job)
		PaintComparing(dc, c);
	dc.SelectFont(oldFont);
	if (m_ShowOverview && rcPaint.right > rc.right)
		PaintOverview(dc, c);
}

void CDiffViewCtrl::PaintOverview(WTL::CDCHandle dc, const DFVCOLORS& c) {
	auto rc = GetOverviewRect();
	if (rc.right <= rc.left || rc.bottom <= rc.top)
		return;
	FillSolid(dc, rc, c.crGutterBack);
	FillSolid(dc, { rc.left, rc.top, rc.left + 1, rc.bottom }, Blend(c.crGutterBack, c.crText, 15));
	size_t count = m_Doc->rows.size();
	if (count == 0)
		return;
	const int height = rc.bottom - rc.top;
	auto yOf = [&](size_t row) { return (int)rc.top + (int)((double)row / count * height); };
	const int left = rc.left + Scale(3), right = rc.right - Scale(2), mid = (left + right) / 2;
	const int least = std::max(2, Scale(2));
	for (auto& cr : m_Doc->changeRows) {
		if (cr.count == 0)
			continue;
		int y0 = yOf(cr.first), y1 = std::max(yOf(cr.first + cr.count), y0 + least);
		// a change with lines on both sides: half and half
		if (cr.left)
			FillSolid(dc, { left, y0, cr.right ? mid : right, y1 }, c.crDeletedMark);
		if (cr.right)
			FillSolid(dc, { cr.left ? mid : left, y0, right, y1 }, c.crInsertedMark);
	}
	// the rows in view
	int y0 = yOf(m_TopRow), y1 = std::max(yOf(std::min(count, m_TopRow + PageRows())), y0 + least);
	WTL::CBrush frame;
	frame.CreateSolidBrush(Blend(c.crGutterBack, c.crText, 55));
	RECT view{ rc.left + 1, y0, rc.right, std::min(y1, (int)rc.bottom) };
	dc.FrameRect(&view, frame);
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
		PaintText(dc, text, lines[line], spansOf(rightSide, line), c.crText, rightSide ? c.crInsertedMark : c.crDeletedMark, index, rightSide, c);
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

int CDiffViewCtrl::ColumnToX(WTL::CDCHandle dc, std::wstring_view line, size_t index) const {
	if (m_FixedPitch)
		return (int)std::min<size_t>(DfColumnOf(line, index, m_TabSize) * m_CharWidth, INT_MAX / 2);
	DfExpandTabs(line.substr(0, std::min(index, line.size())), m_TabSize, m_MeasureBuffer);
	SIZE size{};
	dc.GetTextExtent(m_MeasureBuffer.c_str(), (int)std::min<size_t>(m_MeasureBuffer.size(), 32 * 1024), &size);
	return (int)size.cx;
}

bool CDiffViewCtrl::SelectionIn(size_t row, bool rightSide, size_t length, size_t& from, size_t& to, bool& lineBreak) const {
	if (!HasSelection())
		return false;
	DFVPOS start, end;
	GetSelection(&start, &end);
	DfPos pos;
	if (m_Layout == DFVL_UNIFIED) {
		if ((int)row < m_SelRows[0] || (int)row > m_SelRows[1])
			return false;
		from = (int)row == m_SelRows[0] ? start.column : 0;
		to = (int)row == m_SelRows[1] ? end.column : length;
		lineBreak = (int)row < m_SelRows[1];
	}
	else {
		if (!m_Doc->LineOfRow(row, rightSide, pos) || (start.right != FALSE) != rightSide || pos.line < start.line || pos.line > end.line)
			return false;
		from = pos.line == start.line ? start.column : 0;
		to = pos.line == end.line ? end.column : length;
		lineBreak = pos.line < end.line;
	}
	from = std::min(from, length);
	to = std::min(to, length);
	return true;
}

void CDiffViewCtrl::PaintText(WTL::CDCHandle dc, const RECT& rc, std::wstring_view line, const std::vector<DfSpan>* spans, COLORREF text, COLORREF markBack,
	size_t row, bool rightSide, const DFVCOLORS& c) {
	if (rc.right <= rc.left)
		return;
	DfExpandTabs(line, m_TabSize, m_DrawBuffer);
	const int origin = rc.left + m_Pad - m_Left;
	auto clipped = [&](int x0, int x1) {
		return RECT{ std::max((int)rc.left, origin + x0), rc.top, std::min((int)rc.right, origin + x1), rc.bottom };
	};

	// the changed words under the text
	if (spans) {
		for (auto& span : *spans) {
			auto r = clipped(ColumnToX(dc, line, span.start), ColumnToX(dc, line, span.start + span.length));
			if (r.right > r.left)
				FillSolid(dc, r, markBack);
		}
	}

	// the selection over them; a selected line break shows as a character's width past the end
	size_t selFrom, selTo;
	bool selBreak;
	RECT sel{};
	bool focused = ::GetFocus() == m_hWnd;
	if (SelectionIn(row, rightSide, line.size(), selFrom, selTo, selBreak)) {
		sel = clipped(ColumnToX(dc, line, selFrom), ColumnToX(dc, line, selTo) + (selBreak ? m_CharWidth : 0));
		if (sel.right > sel.left)
			FillSolid(dc, sel, focused ? c.crSelBack : c.crSelBackInactive);
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
	auto draw = [&](COLORREF color, const RECT& clip) {
		dc.SetTextColor(color);
		dc.ExtTextOut(origin + (int)first * m_CharWidth, rc.top + m_TextOffset, ETO_CLIPPED, &clip, m_DrawBuffer.c_str() + first, (UINT)(last - first), nullptr);
	};
	draw(text, rc);
	if (sel.right > sel.left)
		draw(focused ? c.crSelText : c.crSelTextInactive, sel);
}
