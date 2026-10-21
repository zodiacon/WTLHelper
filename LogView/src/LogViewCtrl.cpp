#include "LogView.h"
#include "LogViewModel.h"

#include <algorithm>
#include <climits>

// ---- The text area ----------------------------------------------------------------
//
// A child window, so the scroll bars run beside the text only (a toolbar goes above it). Its messages are
// handled in the control's ALT_MSG_MAP(1).

class LgTextWindow : public ATL::CWindowImpl<LgTextWindow> {
public:
	DECLARE_WND_CLASS_EX(L"LgTextView", CS_DBLCLKS, -1)

	explicit LgTextWindow(CLogViewCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(LgTextWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 1)
	END_MSG_MAP()

private:
	CLogViewCtrl* m_Owner;
};

// ---- Helpers -----------------------------------------------------------------------

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static LGVSTYLE DefaultLevelStyle(BYTE level) {
	LGVSTYLE s;
	switch (level) {
		case LGVL_TRACE:
		case LGVL_DEBUG:
			s.flags = LGVSF_DIM;
			break;
		case LGVL_WARNING:
			s.crText = s.crMarker = RGB(0xD0, 0x8C, 0x00);
			break;
		case LGVL_ERROR:
			s.crText = s.crMarker = RGB(0xE0, 0x40, 0x40);
			break;
		case LGVL_CRITICAL:
			s.crText = RGB(0xFF, 0xFF, 0xFF);
			s.crBack = s.crMarker = RGB(0xC0, 0x20, 0x20);
			s.flags = LGVSF_BOLD;
			break;
	}
	return s;
}

static int Digits(ULONGLONG n) {
	int digits = 1;
	while (n >= 10) {
		n /= 10;
		digits++;
	}
	return digits;
}

// ---- Construction ------------------------------------------------------------------

CLogViewCtrl::CLogViewCtrl() : m_Store(std::make_unique<LgStore>()), m_Text(std::make_unique<LgTextWindow>(this)),
	m_Pending(std::make_unique<LgBatch>()) {
	for (BYTE level = 0; level < 32; level++)
		m_LevelStyles[level] = DefaultLevelStyle(level);
}

CLogViewCtrl::~CLogViewCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CLogViewCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_ThreadId = ::GetCurrentThreadId();
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	// the scroll bars are the text area's
	ModifyStyle(WS_VSCROLL | WS_HSCROLL, WS_CLIPCHILDREN, SWP_FRAMECHANGED);
	UpdateFonts();
	UpdateMetrics();
	m_Text->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS);
	Layout();
	Flush();   // lines appended before the window existed
	return 0;
}

LRESULT CLogViewCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_ThreadId = 0;   // appending threads queue from now on
	bHandled = FALSE;
	return 0;
}

HWND CLogViewCtrl::GetTextWindow() const {
	return m_Text->m_hWnd;
}

LRESULT CLogViewCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Text->IsWindow())
		m_Text->SetFocus();
	return 0;
}

LRESULT CLogViewCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // the text area covers it all, and paints through a memory DC
}

// ---- Metrics and fonts -------------------------------------------------------------

int CLogViewCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CLogViewCtrl::UpdateFonts() {
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
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	for (int i = 0; i < 3; i++) {
		auto variant = lf;
		if (i != 1)
			variant.lfWeight = FW_BOLD;
		if (i != 0)
			variant.lfItalic = TRUE;
		if (!m_Fonts[i].IsNull())
			m_Fonts[i].DeleteObject();
		m_Fonts[i].CreateFontIndirect(&variant);
	}
}

void CLogViewCtrl::UpdateMetrics() {
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	SIZE digit{};
	dc.GetTextExtent(L"0", 1, &digit);
	dc.SelectFont(old);

	m_LineHeight = tm.tmHeight + Scale(1);
	m_TextOffset = (m_LineHeight - tm.tmHeight) / 2;
	m_CharWidth = std::max(1, (int)tm.tmAveCharWidth);
	m_FixedPitch = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH) == 0;   // the bit set means variable pitch
	m_DigitWidth = std::max(1, (int)digit.cx);
	m_Pad = Scale(4);
	m_MarkerWidth = Scale(3);
}

HFONT CLogViewCtrl::FontFor(UINT flags) const {
	switch (flags & (LGVSF_BOLD | LGVSF_ITALIC)) {
		case LGVSF_BOLD: return m_Fonts[0];
		case LGVSF_ITALIC: return m_Fonts[1];
		case LGVSF_BOLD | LGVSF_ITALIC: return m_Fonts[2];
	}
	return m_hFont;
}

LRESULT CLogViewCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	UpdateScrollBars();
	if (lParam && m_Text->IsWindow())
		m_Text->Invalidate();
	return 0;
}

LRESULT CLogViewCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CLogViewCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	Layout();
	bHandled = FALSE;
	return 0;
}

LRESULT CLogViewCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	bHandled = FALSE;
	return 0;
}

// ---- Layout and scrolling ----------------------------------------------------------

void CLogViewCtrl::Layout() {
	if (!IsWindow() || !m_Text->IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	m_Text->SetWindowPos(nullptr, &rc, SWP_NOZORDER | SWP_NOACTIVATE);
	UpdateScrollBars();
	m_Text->Invalidate();
}

LRESULT CLogViewCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	return 0;
}

LRESULT CLogViewCtrl::OnTextSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateScrollBars();
	m_Text->Invalidate();
	return 0;
}

RECT CLogViewCtrl::GetTextRect() const {
	RECT rc{};
	if (m_Text->IsWindow())
		m_Text->GetClientRect(&rc);
	return rc;
}

int CLogViewCtrl::GutterWidth() const {
	if (!m_LineNumbers)
		return m_MarkerWidth + m_Pad;
	size_t count = GetLineCount();
	int digits = std::max(4, Digits(count ? LineNumber(count - 1) : 1));
	return m_MarkerWidth + m_Pad + digits * m_DigitWidth + 2 * m_Pad;
}

int CLogViewCtrl::GetPageLines() const {
	auto rc = GetTextRect();
	return std::max(1, (int)(rc.bottom - rc.top) / m_LineHeight);
}

int CLogViewCtrl::GetVisibleLineCount() const {
	return GetPageLines();
}

size_t CLogViewCtrl::MaxTopLine() const {
	size_t count = GetLineCount();
	size_t page = GetPageLines();
	return count > page ? count - page : 0;
}

size_t CLogViewCtrl::MaxColumns() const {
	return m_Virtual ? m_SeenColumns : m_Store->MaxColumns();
}

int CLogViewCtrl::MaxLeft() const {
	auto rc = GetTextRect();
	auto content = (long long)MaxColumns() * m_CharWidth + 2 * m_Pad;
	auto width = (long long)rc.right - GutterWidth();
	return (int)std::clamp(content - width, 0LL, (long long)INT_MAX);
}

void CLogViewCtrl::UpdateScrollBars() {
	if (!m_Text->IsWindow() || m_UpdateCount > 0)
		return;
	m_TopLine = m_Follow ? MaxTopLine() : std::min(m_TopLine, MaxTopLine());
	m_Left = std::clamp(m_Left, 0, MaxLeft());

	// the scroll bar's range is an int: past INT_MAX lines the bottom can't be reached with it
	SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
	si.nMax = (int)std::min<size_t>(GetLineCount(), INT_MAX) - 1;
	si.nPage = GetPageLines();
	si.nPos = (int)std::min<size_t>(m_TopLine, INT_MAX);
	m_Text->SetScrollInfo(SB_VERT, &si, TRUE);

	auto rc = GetTextRect();
	si.nMax = (int)std::min<long long>((long long)MaxColumns() * m_CharWidth + 2 * m_Pad, INT_MAX) - 1;
	si.nPage = std::max(0, (int)rc.right - GutterWidth());
	si.nPos = m_Left;
	m_Text->SetScrollInfo(SB_HORZ, &si, TRUE);
}

void CLogViewCtrl::ScrollTo(size_t topLine, int left, bool byUser) {
	topLine = std::min(topLine, MaxTopLine());
	left = std::clamp(left, 0, MaxLeft());
	if (topLine == m_TopLine && left == m_Left)
		return;
	bool vertical = topLine != m_TopLine;
	m_TopLine = topLine;
	m_Left = left;
	// following means being at the end: scrolling there turns it on, away from there off
	if (vertical)
		SetFollow(m_TopLine == MaxTopLine(), byUser);
	UpdateScrollBars();
	m_Text->Invalidate();
}

void CLogViewCtrl::SetFollow(bool follow, bool notify) {
	if (follow == m_Follow)
		return;
	m_Follow = follow;
	if (notify) {
		NMHDR hdr{};
		FireNotify(&hdr, LGVN_FOLLOWCHANGED);
	}
}

void CLogViewCtrl::SetFollowTail(bool follow) {
	SetFollow(follow, false);
	if (follow && m_Text->IsWindow()) {
		UpdateScrollBars();   // to the end
		m_Text->Invalidate();
	}
}

bool CLogViewCtrl::IsFollowingTail() const {
	return m_Follow;
}

void CLogViewCtrl::SetTopLine(size_t line) {
	ScrollTo(line, m_Left, false);
}

size_t CLogViewCtrl::GetTopLine() const {
	return m_TopLine;
}

void CLogViewCtrl::EnsureVisible(size_t line) {
	if (line >= GetLineCount())
		return;
	size_t page = GetPageLines();
	if (line < m_TopLine)
		ScrollTo(line, m_Left, false);
	else if (line >= m_TopLine + page)
		ScrollTo(line - page + 1, m_Left, false);
}

void CLogViewCtrl::BeginUpdate() {
	if (m_UpdateCount++ == 0 && m_Text->IsWindow())
		m_Text->SetRedraw(FALSE);
}

void CLogViewCtrl::EndUpdate() {
	if (m_UpdateCount == 0 || --m_UpdateCount > 0)
		return;
	if (m_Text->IsWindow()) {
		m_Text->SetRedraw(TRUE);
		UpdateScrollBars();
		m_Text->Invalidate();
	}
}

void CLogViewCtrl::ShowLineNumbers(bool show) {
	m_LineNumbers = show;
	UpdateScrollBars();
	if (m_Text->IsWindow())
		m_Text->Invalidate();
}

bool CLogViewCtrl::AreLineNumbersShown() const {
	return m_LineNumbers;
}

void CLogViewCtrl::SetTabSize(int columns) {
	columns = std::clamp(columns, 1, 16);
	if (columns == m_TabSize)
		return;
	m_TabSize = columns;
	m_Store->SetTabSize(columns);
	m_SeenColumns = 0;
	UpdateScrollBars();
	if (m_Text->IsWindow())
		m_Text->Invalidate();
}

int CLogViewCtrl::GetTabSize() const {
	return m_TabSize;
}

// ---- Lines ---------------------------------------------------------------------------

bool CLogViewCtrl::OnWindowThread() const {
	DWORD id = m_ThreadId;
	return id != 0 && id == ::GetCurrentThreadId();
}

void CLogViewCtrl::PostFlush() {
	if (m_FlushPosted.exchange(true))
		return;
	HWND hWnd = m_hWnd;
	if (!hWnd || !::PostMessage(hWnd, WM_LGV_FLUSH, 0, 0))
		m_FlushPosted = false;   // no window yet: OnCreate takes the lines in
}

void CLogViewCtrl::Enqueue(const LGVLINE* lines, int count, LPCWSTR text, int cch, BYTE level, BYTE style) {
	auto length = [](LPCWSTR s, int n) { return s ? (n < 0 ? wcslen(s) : (size_t)n) : 0; };
	if (OnWindowThread()) {
		if (m_Virtual)
			return;
		if (lines) {
			for (int i = 0; i < count; i++)
				m_Store->Append(lines[i].pszText, length(lines[i].pszText, lines[i].cchText), lines[i].level, lines[i].style, lines[i].lParam);
		}
		else {
			LgSplitLines({ text, length(text, cch) }, [&](size_t start, size_t n) {
				m_Store->Append(text + start, n, level, style, 0);
			});
		}
		// laid out by the posted flush, so a loop of appends costs one layout
		m_LinesChanged = true;
	}
	else {
		::AcquireSRWLockExclusive(&m_PendingLock);
		if (lines) {
			for (int i = 0; i < count; i++)
				m_Pending->Add(lines[i].pszText, length(lines[i].pszText, lines[i].cchText), lines[i].level, lines[i].style, lines[i].lParam);
		}
		else {
			m_Pending->AddText(text, length(text, cch), level, style);
		}
		::ReleaseSRWLockExclusive(&m_PendingLock);
	}
	PostFlush();
}

void CLogViewCtrl::AppendLine(LPCWSTR text, BYTE level, BYTE style, LPARAM lParam) {
	LGVLINE line;
	line.pszText = text ? text : L"";
	line.level = level;
	line.style = style;
	line.lParam = lParam;
	Enqueue(&line, 1, nullptr, 0, 0, 0);
}

void CLogViewCtrl::AppendLines(const LGVLINE* lines, int count) {
	if (lines && count > 0)
		Enqueue(lines, count, nullptr, 0, 0, 0);
}

void CLogViewCtrl::AppendText(LPCWSTR text, int cch, BYTE level, BYTE style) {
	if (text)
		Enqueue(nullptr, 0, text, cch, level, style);
}

LRESULT CLogViewCtrl::OnFlush(UINT, WPARAM, LPARAM, BOOL&) {
	m_FlushPosted = false;   // before taking the lines: a line queued from now on posts again
	Flush();
	return 0;
}

void CLogViewCtrl::Flush() {
	LgBatch batch;
	::AcquireSRWLockExclusive(&m_PendingLock);
	std::swap(batch, *m_Pending);
	::ReleaseSRWLockExclusive(&m_PendingLock);
	if (!m_Virtual && !batch.Empty()) {
		m_Store->Append(batch);
		m_LinesChanged = true;
	}
	if (m_LinesChanged) {
		m_LinesChanged = false;
		LinesAdded();
	}
}

void CLogViewCtrl::LinesAdded() {
	// the lines the limit dropped were above the view: what's shown stays put unless following
	size_t dropped = m_Store->TakeDropped();
	if (!m_Follow)
		m_TopLine -= std::min(dropped, m_TopLine);
	if (!m_Text->IsWindow())
		return;
	UpdateScrollBars();
	m_Text->Invalidate();
}

void CLogViewCtrl::Clear() {
	::AcquireSRWLockExclusive(&m_PendingLock);
	m_Pending->Clear();
	::ReleaseSRWLockExclusive(&m_PendingLock);
	m_Store->Clear();
	m_LinesChanged = false;
	m_Virtual = false;
	m_VirtualCount = 0;
	m_SeenColumns = 0;
	m_TopLine = 0;
	m_Left = 0;
	if (m_Text->IsWindow()) {
		UpdateScrollBars();
		m_Text->Invalidate();
	}
}

size_t CLogViewCtrl::GetLineCount() const {
	return m_Virtual ? m_VirtualCount : m_Store->Count();
}

ULONGLONG CLogViewCtrl::LineNumber(size_t line) const {
	return m_Virtual ? line + 1 : m_Store->Number(line);
}

bool CLogViewCtrl::GetLine(size_t line, LineData& data) const {
	if (line >= GetLineCount())
		return false;
	if (!m_Virtual) {
		auto& rec = m_Store->Line(line);
		auto text = m_Store->Text(line);
		data = { text.data(), text.size(), rec.level, rec.style, rec.lParam };
		return true;
	}
	NMLGVDISPINFO nm{};
	nm.line = line;
	nm.cchText = -1;
	FireNotify(&nm.hdr, LGVN_GETDISPINFO);
	// the parent's text lasts only until it returns
	if (nm.pszText)
		m_VirtualText.assign(nm.pszText, nm.cchText < 0 ? wcslen(nm.pszText) : (size_t)nm.cchText);
	else
		m_VirtualText.clear();
	data = { m_VirtualText.c_str(), m_VirtualText.size(), nm.level, nm.style, nm.lParam };
	return true;
}

ATL::CString CLogViewCtrl::GetLineText(size_t line) const {
	LineData data;
	if (!GetLine(line, data))
		return {};
	return ATL::CString(data.text, (int)std::min<size_t>(data.length, INT_MAX));
}

BOOL CLogViewCtrl::GetLineInfo(size_t line, LGVLINEINFO* pInfo) const {
	LineData data;
	if (!pInfo || !GetLine(line, data))
		return FALSE;
	pInfo->number = LineNumber(line);
	pInfo->level = data.level;
	pInfo->style = data.style;
	pInfo->lParam = data.lParam;
	pInfo->length = data.length;
	return TRUE;
}

BOOL CLogViewCtrl::SetLineStyle(size_t line, BYTE level, BYTE style) {
	if (m_Virtual || line >= m_Store->Count())
		return FALSE;
	auto& rec = m_Store->Line(line);
	rec.level = level;
	rec.style = style;
	InvalidateLines(line, 1);
	return TRUE;
}

void CLogViewCtrl::SetLineLimit(size_t maxLines) {
	m_Store->SetLimit(maxLines);
	LinesAdded();
}

size_t CLogViewCtrl::GetLineLimit() const {
	return m_Store->Limit();
}

// ---- Virtual mode ----------------------------------------------------------------------

void CLogViewCtrl::SetVirtualLineCount(size_t count) {
	if (!m_Virtual) {
		::AcquireSRWLockExclusive(&m_PendingLock);
		m_Pending->Clear();
		::ReleaseSRWLockExclusive(&m_PendingLock);
		m_Store->Clear();
		m_LinesChanged = false;
		m_Virtual = true;
		m_SeenColumns = 0;
	}
	m_VirtualCount = count;
	if (m_Text->IsWindow()) {
		UpdateScrollBars();
		m_Text->Invalidate();
	}
}

bool CLogViewCtrl::IsVirtual() const {
	return m_Virtual;
}

void CLogViewCtrl::InvalidateLines(size_t first, size_t count) {
	if (!m_Text->IsWindow() || count == 0)
		return;
	size_t last = first + count - 1;   // inclusive
	size_t end = m_TopLine + GetPageLines();   // a part line shows below the whole ones
	if (last < m_TopLine || first > end)
		return;
	auto rc = GetTextRect();
	rc.top = (int)(std::max(first, m_TopLine) - m_TopLine) * m_LineHeight;
	rc.bottom = (int)(std::min(last, end) - m_TopLine + 1) * m_LineHeight;
	m_Text->InvalidateRect(&rc, FALSE);
}

// ---- Hit testing ---------------------------------------------------------------------

size_t CLogViewCtrl::HitTest(POINT pt, UINT* pPart) const {
	UINT part = LGVHT_NOWHERE;
	size_t line = LGV_NOLINE;
	if (m_Text->IsWindow()) {
		MapWindowPoints(m_Text->m_hWnd, &pt, 1);
		auto rc = GetTextRect();
		if (::PtInRect(&rc, pt)) {
			size_t hit = m_TopLine + pt.y / m_LineHeight;
			if (hit >= GetLineCount())
				part = LGVHT_BELOW;
			else {
				line = hit;
				part = pt.x < GutterWidth() ? LGVHT_GUTTER : LGVHT_TEXT;
			}
		}
	}
	if (pPart)
		*pPart = part;
	return line;
}

BOOL CLogViewCtrl::GetLineRect(size_t line, RECT* prc) const {
	if (!prc || !m_Text->IsWindow() || line >= GetLineCount() || line < m_TopLine || line > m_TopLine + GetPageLines())
		return FALSE;
	auto rc = GetTextRect();
	rc.top = (int)(line - m_TopLine) * m_LineHeight;
	rc.bottom = rc.top + m_LineHeight;
	m_Text->MapWindowPoints(m_hWnd, &rc);
	*prc = rc;
	return TRUE;
}

// ---- Colors and styles ------------------------------------------------------------------

void CLogViewCtrl::SetColors(const LGVCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (m_Text->IsWindow())
		m_Text->Invalidate();
}

LGVCOLORS CLogViewCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	LGVCOLORS c;
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
	c.crGutterBack = Blend(c.crBack, c.crText, 5);
	c.crGutterText = Blend(c.crBack, c.crText, 45);
	c.crSelBack = ::GetSysColor(COLOR_HIGHLIGHT);
	c.crSelText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	c.crSelBackInactive = Blend(c.crBack, c.crText, 18);
	c.crSelTextInactive = c.crText;
	return c;
}

BOOL CLogViewCtrl::SetStyle(BYTE index, const LGVSTYLE* style) {
	if (index == 0)
		return FALSE;
	m_Styles[index] = style ? *style : LGVSTYLE{};
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	return TRUE;
}

BOOL CLogViewCtrl::GetStyle(BYTE index, LGVSTYLE* style) const {
	if (index == 0 || !style)
		return FALSE;
	*style = m_Styles[index];
	return TRUE;
}

BOOL CLogViewCtrl::SetLevelStyle(BYTE level, const LGVSTYLE* style) {
	if (level > LGVL_MAX)
		return FALSE;
	m_LevelStyles[level] = style ? *style : DefaultLevelStyle(level);
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	return TRUE;
}

BOOL CLogViewCtrl::GetLevelStyle(BYTE level, LGVSTYLE* style) const {
	if (level > LGVL_MAX || !style)
		return FALSE;
	*style = m_LevelStyles[level];
	return TRUE;
}

LGVSTYLE CLogViewCtrl::EffectiveStyle(BYTE level, BYTE style) const {
	return style ? m_Styles[style] : m_LevelStyles[level & LGVL_MAX];
}

// ---- Painting ---------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnTextPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	size_t seen = m_SeenColumns;
	if (uMsg == WM_PRINTCLIENT) {
		Paint((HDC)wParam, GetTextRect());
		return 0;
	}
	{
		WTL::CPaintDC dc(m_Text->m_hWnd);
		WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
		Paint(mdc.m_hDC, dc.m_ps.rcPaint);
	}
	// a virtual line wider than any before: the horizontal range grows
	if (m_SeenColumns > seen)
		UpdateScrollBars();
	return 0;
}

void CLogViewCtrl::Paint(WTL::CDCHandle dc, const RECT& rcPaint) {
	auto c = GetColors();
	auto rc = GetTextRect();
	int gutter = GutterWidth();
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	size_t count = GetLineCount();
	size_t line = m_TopLine + std::max(0, (int)rcPaint.top) / m_LineHeight;
	int y = (int)(line - m_TopLine) * m_LineHeight;
	for (; y < rcPaint.bottom && line < count; line++, y += m_LineHeight)
		PaintLine(dc, line, { 0, y, rc.right, y + m_LineHeight }, c, gutter);
	if (y < rcPaint.bottom) {
		FillSolid(dc, { 0, y, gutter, rcPaint.bottom }, c.crGutterBack);
		FillSolid(dc, { gutter, y, std::max(gutter, (int)rc.right), rcPaint.bottom }, c.crBack);
	}
	dc.SelectFont(oldFont);
}

void CLogViewCtrl::PaintLine(WTL::CDCHandle dc, size_t line, const RECT& rc, const LGVCOLORS& c, int gutter) {
	LineData data;
	if (!GetLine(line, data))
		return;
	auto style = EffectiveStyle(data.level, data.style);

	// gutter: the level's marker, then the number
	FillSolid(dc, { rc.left, rc.top, gutter, rc.bottom }, c.crGutterBack);
	if (style.crMarker != CLR_NONE && style.crMarker != CLR_DEFAULT)
		FillSolid(dc, { rc.left, rc.top, rc.left + m_MarkerWidth, rc.bottom }, style.crMarker);
	if (m_LineNumbers) {
		wchar_t number[24];
		_ui64tow_s(LineNumber(line), number, _countof(number), 10);
		dc.SetTextColor(c.crGutterText);
		dc.SetTextAlign(TA_RIGHT | TA_TOP);
		RECT clip{ rc.left, rc.top, gutter, rc.bottom };
		dc.ExtTextOut(gutter - 2 * m_Pad, rc.top + m_TextOffset, ETO_CLIPPED, &clip, number, (UINT)wcslen(number), nullptr);
		dc.SetTextAlign(TA_LEFT | TA_TOP);
	}

	// text
	COLORREF back = style.crBack == CLR_DEFAULT ? c.crBack : style.crBack;
	COLORREF text = style.crText == CLR_DEFAULT ? c.crText : style.crText;
	if (style.flags & LGVSF_DIM)
		text = Blend(text, back, 50);
	RECT clip{ gutter, rc.top, std::max(gutter, (int)rc.right), rc.bottom };
	FillSolid(dc, clip, back);
	if (data.length == 0)
		return;

	LgExpandTabs({ data.text, data.length }, m_TabSize, m_DrawBuffer);
	if (m_Virtual)
		m_SeenColumns = std::max(m_SeenColumns, m_DrawBuffer.size());
	size_t first = 0, last = m_DrawBuffer.size();
	if (m_FixedPitch) {
		// the columns in view only, so a very long line costs no more than a short one
		int width = clip.right - clip.left;
		first = std::min<size_t>(last, (size_t)std::max(0, m_Left - m_Pad) / m_CharWidth);
		last = std::min<size_t>(last, (size_t)(std::max(0, m_Left - m_Pad) + width) / m_CharWidth + 2);
	}
	else {
		last = std::min<size_t>(last, 32 * 1024);   // GDI gets slow, then fails, on very long strings
	}
	if (first >= last)
		return;
	auto old = dc.SelectFont(FontFor(style.flags));
	dc.SetTextColor(text);
	int x = gutter + m_Pad - m_Left + (int)first * m_CharWidth;
	dc.ExtTextOut(x, rc.top + m_TextOffset, ETO_CLIPPED, &clip, m_DrawBuffer.c_str() + first, (UINT)(last - first), nullptr);
	dc.SelectFont(old);
}

// ---- Input ---------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnTextFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Text->Invalidate();   // the selection's colors (to come) follow the focus
	bHandled = FALSE;
	return 0;
}

LRESULT CLogViewCtrl::OnTextGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

LRESULT CLogViewCtrl::OnTextKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	size_t page = GetPageLines();
	switch (wParam) {
		case VK_UP:
			ScrollTo(m_TopLine - std::min<size_t>(m_TopLine, 1), m_Left, true);
			break;
		case VK_DOWN:
			ScrollTo(m_TopLine + 1, m_Left, true);
			break;
		case VK_PRIOR:
			ScrollTo(m_TopLine - std::min(m_TopLine, page), m_Left, true);
			break;
		case VK_NEXT:
			ScrollTo(m_TopLine + page, m_Left, true);
			break;
		case VK_HOME:
			ScrollTo(ctrl ? 0 : m_TopLine, 0, true);
			break;
		case VK_END:
			if (ctrl) {
				ScrollTo(MaxTopLine(), m_Left, true);
				SetFollow(true, true);   // even when already at the end
			}
			else {
				ScrollTo(m_TopLine, MaxLeft(), true);
			}
			break;
		case VK_LEFT:
			ScrollTo(m_TopLine, m_Left - 4 * m_CharWidth, true);
			break;
		case VK_RIGHT:
			ScrollTo(m_TopLine, m_Left + 4 * m_CharWidth, true);
			break;
		default:
			bHandled = FALSE;
	}
	return 0;
}

LRESULT CLogViewCtrl::OnTextLButtonDown(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text->SetFocus();
	return 0;
}

LRESULT CLogViewCtrl::OnTextLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	m_Text->MapWindowPoints(m_hWnd, &pt, 1);
	size_t line = HitTest(pt);
	if (line != LGV_NOLINE) {
		ClientToScreen(&pt);
		NotifyLine(LGVN_ACTIVATE, line, pt);
	}
	return 0;
}

LRESULT CLogViewCtrl::OnTextRButtonDown(UINT, WPARAM, LPARAM, BOOL&) {
	m_Text->SetFocus();
	return 0;
}

LRESULT CLogViewCtrl::OnTextContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	size_t line;
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: the top line
		line = m_TopLine < GetLineCount() ? m_TopLine : LGV_NOLINE;
		pt = { GutterWidth(), m_LineHeight };
		m_Text->ClientToScreen(&pt);
	}
	else {
		POINT client = pt;
		ScreenToClient(&client);
		line = HitTest(client);
	}
	NotifyLine(LGVN_CONTEXTMENU, line, pt);
	return 0;
}

LRESULT CLogViewCtrl::OnTextMouseWheel(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
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
		ScrollTo(m_TopLine, m_Left + steps * (int)chars * m_CharWidth, true);
		return 0;
	}
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	if (lines == WHEEL_PAGESCROLL)
		lines = GetPageLines();
	m_WheelRemainder += delta;
	int steps = m_WheelRemainder / WHEEL_DELTA;
	m_WheelRemainder -= steps * WHEEL_DELTA;
	long long move = -(long long)steps * lines;
	size_t top = move < 0 ? m_TopLine - std::min<size_t>(m_TopLine, (size_t)-move) : m_TopLine + (size_t)move;
	ScrollTo(top, m_Left, true);
	return 0;
}

LRESULT CLogViewCtrl::OnTextVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	size_t page = GetPageLines();
	size_t top = m_TopLine;
	switch (LOWORD(wParam)) {
		case SB_LINEUP: top -= std::min<size_t>(top, 1); break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP: top -= std::min(top, page); break;
		case SB_PAGEDOWN: top += page; break;
		case SB_TOP: top = 0; break;
		case SB_BOTTOM: top = MaxTopLine(); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			m_Text->GetScrollInfo(SB_VERT, &si);
			top = (size_t)si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollTo(top, m_Left, true);
	return 0;
}

LRESULT CLogViewCtrl::OnTextHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	int left = m_Left;
	int page = std::max(m_CharWidth, (int)GetTextRect().right - GutterWidth());
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
			m_Text->GetScrollInfo(SB_HORZ, &si);
			left = si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollTo(m_TopLine, left, true);
	return 0;
}

// ---- Notifications -----------------------------------------------------------------------

LRESULT CLogViewCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

void CLogViewCtrl::NotifyLine(UINT code, size_t line, POINT ptScreen) {
	NMLGVLINE nm{};
	nm.line = line;
	nm.pt = ptScreen;
	FireNotify(&nm.hdr, code);
}
