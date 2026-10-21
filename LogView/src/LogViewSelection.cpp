// The log view's caret and selection: positions, the mouse, the keys, copying.

#include "LogView.h"
#include "LogViewModel.h"
#include "LogViewText.h"

#include <algorithm>
#include <climits>

static bool Same(const LGVPOS& a, const LGVPOS& b) {
	return a.line == b.line && a.column == b.column;
}

static bool Before(const LGVPOS& a, const LGVPOS& b) {
	return a.line < b.line || (a.line == b.line && a.column < b.column);
}

constexpr UINT_PTR DragScrollTimer = 1;

// ---- Positions -------------------------------------------------------------------------

size_t CLogViewCtrl::LineLength(size_t line) const {
	LineData data;
	return GetLine(line, data) ? data.length : 0;
}

LGVPOS CLogViewCtrl::ClampPos(LGVPOS pos) const {
	size_t rows = RowCount();
	if (rows == 0)
		return { 0, 0 };
	// on a line the filter hides: the next one shown (or the last)
	if (!IsLineShown(pos.line))
		pos.line = LineOfRow(std::min(RowOfLine(pos.line), rows - 1));
	pos.column = std::min(pos.column, LineLength(pos.line));
	return pos;
}

LGVPOS CLogViewCtrl::LineStartOrNext(size_t line, bool next) const {
	if (!next)
		return { line, 0 };
	size_t row = RowOfLine(line);
	if (row + 1 < RowCount())
		return { LineOfRow(row + 1), 0 };
	return { line, LineLength(line) };   // the last line has no break to take
}

int CLogViewCtrl::TextX(std::wstring_view text, UINT flags, size_t index) const {
	size_t col = LgColumnOf(text, index, m_TabSize);
	if (m_FixedPitch)
		return (int)std::min<size_t>(col * m_CharWidth, INT_MAX);
	std::wstring expanded;
	LgExpandTabs(text, m_TabSize, expanded);
	col = std::min<size_t>(col, 32 * 1024);   // as drawn
	if (col == 0 || !m_Text->IsWindow())
		return 0;
	WTL::CClientDC dc(m_Text->m_hWnd);
	auto old = dc.SelectFont(FontFor(flags));
	SIZE size{};
	dc.GetTextExtent(expanded.c_str(), (int)col, &size);
	dc.SelectFont(old);
	return size.cx;
}

size_t CLogViewCtrl::TextIndexAt(std::wstring_view text, UINT flags, int x) const {
	if (m_FixedPitch)
		return LgIndexAtColumn(text, (double)x / m_CharWidth, m_TabSize);
	std::wstring expanded;
	LgExpandTabs(text, m_TabSize, expanded);
	int n = (int)std::min<size_t>(expanded.size(), 32 * 1024);
	if (n == 0 || !m_Text->IsWindow())
		return 0;
	// the nearest boundary in the expanded text, then back to a character index
	std::vector<int> widths(n);
	WTL::CClientDC dc(m_Text->m_hWnd);
	auto old = dc.SelectFont(FontFor(flags));
	SIZE size{};
	::GetTextExtentExPointW(dc, expanded.c_str(), n, INT_MAX, nullptr, widths.data(), &size);
	dc.SelectFont(old);
	int column = n, prev = 0;
	for (int i = 0; i < n; i++) {
		if (x < (prev + widths[i]) / 2) {
			column = i;
			break;
		}
		prev = widths[i];
	}
	return LgIndexAtColumn(text, column, m_TabSize);
}

int CLogViewCtrl::IndexToX(size_t line, size_t index) const {
	LineData data;
	if (!GetLine(line, data))
		return 0;
	return TextX({ data.text, data.length }, EffectiveStyle(data.level, data.style).flags, index);
}

size_t CLogViewCtrl::XToIndex(size_t line, int x) const {
	LineData data;
	if (!GetLine(line, data))
		return 0;
	return TextIndexAt({ data.text, data.length }, EffectiveStyle(data.level, data.style).flags, x);
}

LGVPOS CLogViewCtrl::PosFromPoint(POINT pt) const {
	size_t count = RowCount();
	if (count == 0)
		return { 0, 0 };
	long long offset = pt.y >= 0 ? pt.y / m_LineHeight : -((-pt.y + m_LineHeight - 1) / m_LineHeight);
	long long row = (long long)m_TopRow + offset;
	if (row < 0)
		return { LineOfRow(0), 0 };
	if ((size_t)row >= count) {
		size_t last = LineOfRow(count - 1);
		return { last, LineLength(last) };
	}
	size_t line = LineOfRow((size_t)row);
	return { line, XToIndex(line, pt.x - GutterWidth() - m_Pad + m_Left) };
}

LGVPOS CLogViewCtrl::WordLeft(LGVPOS pos) const {
	if (pos.column == 0) {
		size_t row = RowOfLine(pos.line);
		if (row == 0)
			return pos;
		size_t prev = LineOfRow(row - 1);
		return { prev, LineLength(prev) };
	}
	LineData data;
	GetLine(pos.line, data);
	return { pos.line, LgWordLeft({ data.text, data.length }, pos.column) };
}

LGVPOS CLogViewCtrl::WordRight(LGVPOS pos) const {
	LineData data;
	GetLine(pos.line, data);
	if (pos.column >= data.length) {
		size_t row = RowOfLine(pos.line);
		return row + 1 < RowCount() ? LGVPOS{ LineOfRow(row + 1), 0 } : pos;
	}
	return { pos.line, LgWordRight({ data.text, data.length }, pos.column) };
}

// ---- Selecting ---------------------------------------------------------------------------

void CLogViewCtrl::Select(LGVPOS anchor, LGVPOS caret, bool notify, bool keepDesiredX) {
	anchor = ClampPos(anchor);
	caret = ClampPos(caret);
	if (!keepDesiredX)
		m_DesiredX = -1;
	if (Same(anchor, m_Anchor) && Same(caret, m_Caret))
		return;
	bool hadSelection = HasSelection();
	m_Anchor = anchor;
	m_Caret = caret;
	if ((hadSelection || HasSelection()) && m_Text->IsWindow())
		m_Text->Invalidate();
	UpdateCaret();
	NotifyCaretAccessible();
	if (notify) {
		NMHDR hdr{};
		FireNotify(&hdr, LGVN_SELCHANGED);
	}
}

void CLogViewCtrl::MoveCaret(LGVPOS pos, bool extend, bool notify, bool keepDesiredX) {
	Select(extend ? m_Anchor : pos, pos, notify, keepDesiredX);
}

LGVPOS CLogViewCtrl::GetCaret() const {
	return m_Caret;
}

void CLogViewCtrl::SetCaret(LGVPOS pos, bool extend) {
	MoveCaret(pos, extend, false);
	EnsureCaretVisible(false);
}

void CLogViewCtrl::GetSelection(LGVPOS* pStart, LGVPOS* pEnd) const {
	bool reversed = Before(m_Caret, m_Anchor);
	if (pStart)
		*pStart = reversed ? m_Caret : m_Anchor;
	if (pEnd)
		*pEnd = reversed ? m_Anchor : m_Caret;
}

void CLogViewCtrl::SetSelection(LGVPOS anchor, LGVPOS caret) {
	Select(anchor, caret, false);
	EnsureCaretVisible(false);
}

void CLogViewCtrl::SelectAll() {
	if (size_t rows = RowCount()) {
		size_t last = LineOfRow(rows - 1);
		Select({ LineOfRow(0), 0 }, { last, LineLength(last) }, false);
	}
}

bool CLogViewCtrl::HasSelection() const {
	return !Same(m_Anchor, m_Caret);
}

bool CLogViewCtrl::SelectionInLine(size_t line, size_t& start, size_t& end, bool& lineBreak) const {
	if (!HasSelection())
		return false;
	LGVPOS s, e;
	GetSelection(&s, &e);
	if (line < s.line || line > e.line)
		return false;
	start = line == s.line ? s.column : 0;
	end = line == e.line ? e.column : SIZE_MAX;
	lineBreak = line < e.line;
	return start < end || lineBreak;
}

void CLogViewCtrl::MovePositionsUp(size_t dropped) {
	if (dropped == 0)
		return;
	auto move = [=](LGVPOS& pos) {
		if (pos.line >= dropped)
			pos.line -= dropped;
		else
			pos = { 0, 0 };   // its line is gone
	};
	move(m_Anchor);
	move(m_Caret);
	m_DragLine = m_DragLine >= dropped ? m_DragLine - dropped : 0;
}

ATL::CString CLogViewCtrl::GetSelectedText() const {
	if (!HasSelection())
		return {};
	LGVPOS s, e;
	GetSelection(&s, &e);
	// the rows shown from start to end: an end on a hidden line takes the whole row before it
	size_t startRow = RowOfLine(s.line), endRow = RowOfLine(e.line);
	size_t startCol = IsLineShown(s.line) ? s.column : 0, endCol = e.column;
	if (!IsLineShown(e.line)) {
		if (endRow == 0)
			return {};
		endRow--;
		endCol = SIZE_MAX;
	}
	if (startRow > endRow || endRow >= RowCount())
		return {};
	std::wstring text;
	LgJoinRange(startRow, startCol, endRow, endCol, [this](size_t row) {
		// used before the next line is asked for (a virtual line's text is replaced then)
		LineData data;
		GetLine(LineOfRow(row), data);
		return std::wstring_view(data.text, data.length);
	}, text);
	return ATL::CString(text.c_str(), (int)std::min<size_t>(text.size(), INT_MAX));
}

BOOL CLogViewCtrl::Copy() {
	ATL::CString text;
	if (HasSelection())
		text = GetSelectedText();
	else if (IsLineShown(m_Caret.line))
		text = GetLineText(m_Caret.line) + L"\r\n";   // nothing selected: the caret's line
	else
		return FALSE;
	if (!::OpenClipboard(m_Text->IsWindow() ? m_Text->m_hWnd : m_hWnd))
		return FALSE;
	::EmptyClipboard();
	size_t bytes = ((size_t)text.GetLength() + 1) * sizeof(wchar_t);
	BOOL ok = FALSE;
	if (auto mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		memcpy(::GlobalLock(mem), (LPCWSTR)text, bytes);
		::GlobalUnlock(mem);
		ok = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		if (!ok)
			::GlobalFree(mem);
	}
	::CloseClipboard();
	return ok;
}

// ---- The caret ------------------------------------------------------------------------------

void CLogViewCtrl::CreateSystemCaret() {
	if (m_HasCaret)
		::DestroyCaret();
	DWORD width = 1;
	::SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
	::CreateCaret(m_Text->m_hWnd, nullptr, std::max((int)width, Scale(1)), m_LineHeight);
	m_HasCaret = true;
	m_CaretShown = false;
	UpdateCaret();
}

void CLogViewCtrl::UpdateCaret() {
	if (!m_HasCaret)
		return;
	// an empty view shows it at the start; a line the filter hides (while it's still going) doesn't
	size_t row = RowCount() == 0 ? 0 : RowOfLine(m_Caret.line);
	bool visible = (RowCount() == 0 || IsLineShown(m_Caret.line)) && row >= m_TopRow && row <= m_TopRow + GetPageLines();
	POINT pt{};
	if (visible) {
		int gutter = GutterWidth();
		pt.x = gutter + m_Pad - m_Left + IndexToX(m_Caret.line, m_Caret.column);
		pt.y = (int)(row - m_TopRow) * m_LineHeight;
		visible = pt.x >= gutter && pt.x < GetTextRect().right;
	}
	if (visible) {
		::SetCaretPos(pt.x, pt.y);
		if (!m_CaretShown)
			::ShowCaret(m_Text->m_hWnd);
		m_CaretShown = true;
	}
	else if (m_CaretShown) {
		::HideCaret(m_Text->m_hWnd);
		m_CaretShown = false;
	}
}

void CLogViewCtrl::EnsureCaretVisible(bool byUser) {
	if (!m_Text->IsWindow() || !IsLineShown(m_Caret.line))
		return;
	size_t row = RowOfLine(m_Caret.line);
	size_t page = GetPageLines();
	size_t top = m_TopRow;
	if (row < top)
		top = row;
	else if (row >= top + page)
		top = row - page + 1;

	int x = IndexToX(m_Caret.line, m_Caret.column);
	int width = std::max(m_CharWidth, (int)GetTextRect().right - GutterWidth() - 2 * m_Pad);
	int left = m_Left;
	if (x < left)
		left = std::max(0, x - 8 * m_CharWidth);
	else if (x > left + width - m_CharWidth)
		left = x - width + 8 * m_CharWidth;
	ScrollTo(top, left, byUser);
}

// ---- Keys ------------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnTextGetDlgCode(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto msg = (MSG*)lParam;
	LRESULT code = DLGC_WANTARROWS | DLGC_WANTCHARS;
	if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
		code |= DLGC_WANTMESSAGE;   // Enter activates the line, not the dialog's default button
	return code;
}

void CLogViewCtrl::HandleCaretKey(WPARAM key, bool ctrl, bool shift) {
	size_t count = RowCount();
	if (count == 0)
		return;
	size_t page = GetPageLines();
	LGVPOS pos = ClampPos(m_Caret);
	LGVPOS start, end;
	GetSelection(&start, &end);
	bool keepX = false;

	// Up, Down, Page Up and Page Down keep to the column the caret started from
	auto vertical = [&](long long lines) {
		int x = m_DesiredX >= 0 ? m_DesiredX : IndexToX(pos.line, pos.column);
		size_t line = LineOfRow((size_t)std::clamp((long long)RowOfLine(pos.line) + lines, 0LL, (long long)count - 1));
		pos = { line, XToIndex(line, x) };
		m_DesiredX = x;
		keepX = true;
	};

	switch (key) {
		case VK_LEFT:
			if (HasSelection() && !shift)
				pos = start;
			else if (ctrl)
				pos = WordLeft(pos);
			else if (pos.column > 0)
				pos.column--;
			else if (size_t row = RowOfLine(pos.line); row > 0)
				pos = { LineOfRow(row - 1), LineLength(LineOfRow(row - 1)) };
			break;
		case VK_RIGHT:
			if (HasSelection() && !shift)
				pos = end;
			else if (ctrl)
				pos = WordRight(pos);
			else if (pos.column < LineLength(pos.line))
				pos.column++;
			else if (size_t row = RowOfLine(pos.line); row + 1 < count)
				pos = { LineOfRow(row + 1), 0 };
			break;
		case VK_UP:
		case VK_DOWN:
			if (ctrl) {
				// the view only
				ScrollTo(key == VK_UP ? m_TopRow - std::min<size_t>(m_TopRow, 1) : m_TopRow + 1, m_Left, true);
				return;
			}
			vertical(key == VK_UP ? -1 : 1);
			break;
		case VK_PRIOR:
		case VK_NEXT:
			// the view moves a page too, so the caret keeps its place on the screen
			ScrollTo(key == VK_PRIOR ? m_TopRow - std::min(m_TopRow, page) : m_TopRow + page, m_Left, true);
			vertical(key == VK_PRIOR ? -(long long)page : (long long)page);
			break;
		case VK_HOME:
			if (ctrl) {
				pos = { LineOfRow(0), 0 };
			}
			else {
				// smart Home: the first non-blank, then the start of the line
				LineData data;
				GetLine(pos.line, data);
				size_t first = 0;
				while (first < data.length && (data.text[first] == L' ' || data.text[first] == L'\t'))
					first++;
				pos.column = pos.column == first ? 0 : first;
			}
			break;
		case VK_END:
			if (ctrl)
				pos.line = LineOfRow(count - 1);
			pos.column = LineLength(pos.line);
			break;
		default:
			return;
	}
	MoveCaret(pos, shift, true, keepX);
	EnsureCaretVisible(true);
	if (key == VK_END && ctrl)
		SetFollow(true, true);   // even when already at the end
}

LRESULT CLogViewCtrl::OnTextKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	switch (wParam) {
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
		case VK_PRIOR: case VK_NEXT: case VK_HOME: case VK_END:
			HandleCaretKey(wParam, ctrl, shift);
			break;
		case 'A':
			if (!ctrl)
				bHandled = FALSE;
			else if (size_t rows = RowCount())
				Select({ LineOfRow(0), 0 }, { LineOfRow(rows - 1), LineLength(LineOfRow(rows - 1)) }, true);
			break;
		case 'C':
		case VK_INSERT:
			if (!ctrl)
				bHandled = FALSE;
			else
				Copy();
			break;
		case VK_ESCAPE:
			if (IsSearching())
				CancelSearch();
			else
				MoveCaret(m_Caret, false, true);
			break;
		case 'F':
		case 'L':
			if (ctrl && m_ShowToolbar)
				FocusBox(wParam == 'F');
			else
				bHandled = FALSE;
			break;
		case VK_F3:
			if (ctrl) {
				// the selected text (one line of it), or the word at the caret
				LineData data;
				if (!GetLine(m_Caret.line, data))
					break;
				LGVPOS s, e;
				GetSelection(&s, &e);
				size_t start = s.column, end = e.column;
				if (s.line != e.line || start == end)
					LgWordAt({ data.text, data.length }, m_Caret.column, start, end);
				start = std::min(start, data.length);
				end = std::min(end, data.length);
				std::wstring text(data.text + start, end - start);
				if (text.empty())
					break;
				LGVSEARCH search;
				search.pszText = text.c_str();
				SetSearch(&search);
			}
			if (!m_Search->Empty())
				StartFind(!shift, true);
			break;
		case VK_RETURN:
			if (IsLineShown(m_Caret.line)) {
				EnsureCaretVisible(true);
				POINT pt{ GutterWidth() + m_Pad - m_Left + IndexToX(m_Caret.line, m_Caret.column),
					(int)(RowOfLine(m_Caret.line) - m_TopRow + 1) * m_LineHeight };
				m_Text->ClientToScreen(&pt);
				NotifyLine(LGVN_ACTIVATE, m_Caret.line, pt);
			}
			break;
		default:
			bHandled = FALSE;
	}
	return 0;
}

// ---- Mouse --------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnTextLButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	m_Text->SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	bool shift = (wParam & MK_SHIFT) != 0;
	if (RowCount() == 0)
		return 0;
	if (pt.x < GutterWidth()) {
		// whole lines
		size_t line = PosFromPoint(pt).line;
		if (shift) {
			m_DragLine = m_Anchor.line;
			MoveCaret(line >= m_Anchor.line ? LineStartOrNext(line, true) : LGVPOS{ line, 0 }, true, true);
		}
		else {
			m_DragLine = line;
			Select({ line, 0 }, LineStartOrNext(line, true), true);
		}
		m_Drag = Drag::Gutter;
	}
	else {
		MoveCaret(PosFromPoint(pt), shift, true);
		m_Drag = Drag::Text;
	}
	m_DragPoint = pt;
	m_Text->SetCapture();
	return 0;
}

void CLogViewCtrl::DragTo(POINT pt) {
	if (m_Drag == Drag::Text) {
		MoveCaret(PosFromPoint(pt), true, true);
	}
	else if (m_Drag == Drag::Gutter) {
		size_t line = PosFromPoint(pt).line;
		if (line >= m_DragLine)
			Select({ m_DragLine, 0 }, LineStartOrNext(line, true), true);
		else
			Select(LineStartOrNext(m_DragLine, true), { line, 0 }, true);
	}
}

LRESULT CLogViewCtrl::OnTextMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag == Drag::None)
		return 0;
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	m_DragPoint = pt;
	DragTo(pt);
	// outside the text: the view scrolls toward the mouse
	auto rc = GetTextRect();
	bool outside = pt.y < 0 || pt.y >= rc.bottom || (m_Drag == Drag::Text && (pt.x < GutterWidth() || pt.x >= rc.right));
	if (outside)
		m_Text->SetTimer(DragScrollTimer, 40);
	else
		m_Text->KillTimer(DragScrollTimer);
	return 0;
}

LRESULT CLogViewCtrl::OnTextTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != DragScrollTimer) {
		bHandled = FALSE;
		return 0;
	}
	if (m_Drag == Drag::None) {
		m_Text->KillTimer(DragScrollTimer);
		return 0;
	}
	auto rc = GetTextRect();
	auto pt = m_DragPoint;
	long long lines = 0;
	if (pt.y < 0)
		lines = -(1 + (-pt.y) / m_LineHeight);
	else if (pt.y >= rc.bottom)
		lines = 1 + (pt.y - rc.bottom) / m_LineHeight;
	int dx = 0;
	if (m_Drag == Drag::Text) {
		if (pt.x < GutterWidth())
			dx = -4 * m_CharWidth;
		else if (pt.x >= rc.right)
			dx = 4 * m_CharWidth;
	}
	if (lines == 0 && dx == 0) {
		m_Text->KillTimer(DragScrollTimer);
		return 0;
	}
	size_t top = lines < 0 ? m_TopRow - std::min<size_t>(m_TopRow, (size_t)-lines) : m_TopRow + (size_t)lines;
	ScrollTo(top, m_Left + dx, true);
	DragTo(pt);
	return 0;
}

LRESULT CLogViewCtrl::OnTextLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Drag != Drag::None)
		::ReleaseCapture();   // WM_CAPTURECHANGED ends the drag
	return 0;
}

LRESULT CLogViewCtrl::OnTextCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Drag = Drag::None;
	m_Text->KillTimer(DragScrollTimer);
	return 0;
}

LRESULT CLogViewCtrl::OnTextLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	if (RowCount() == 0)
		return 0;
	LGVPOS pos;
	if (pt.x < GutterWidth()) {
		pos = PosFromPoint(pt);
		Select({ pos.line, 0 }, LineStartOrNext(pos.line, true), true);
	}
	else {
		// the character under the mouse: the boundary half a character to the left of it
		pos = PosFromPoint({ pt.x - m_CharWidth / 2, pt.y });
		LineData data;
		GetLine(pos.line, data);
		size_t start, end;
		LgWordAt({ data.text, data.length }, pos.column, start, end);
		Select({ pos.line, start }, { pos.line, end }, true);
	}
	m_Text->ClientToScreen(&pt);
	NotifyLine(LGVN_ACTIVATE, pos.line, pt);
	return 0;
}

LRESULT CLogViewCtrl::OnTextRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	m_Text->SetFocus();
	// outside the selection the caret moves to the click; inside, the menu is for the selection
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	if (RowCount() == 0 || pt.x < GutterWidth())
		return 0;
	auto pos = PosFromPoint(pt);
	LGVPOS s, e;
	GetSelection(&s, &e);
	if (!HasSelection() || Before(pos, s) || Before(e, pos))
		MoveCaret(pos, false, true);
	return 0;
}

LRESULT CLogViewCtrl::OnTextSetCursor(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	if (LOWORD(lParam) != HTCLIENT) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt;
	::GetCursorPos(&pt);
	m_Text->ScreenToClient(&pt);
	::SetCursor(::LoadCursor(nullptr, pt.x < GutterWidth() ? IDC_ARROW : IDC_IBEAM));
	return TRUE;
}

LRESULT CLogViewCtrl::OnTextFocus(UINT uMsg, WPARAM, LPARAM, BOOL& bHandled) {
	if (uMsg == WM_SETFOCUS) {
		CreateSystemCaret();
		NotifyCaretAccessible();
	}
	else if (m_HasCaret) {
		::DestroyCaret();
		m_HasCaret = m_CaretShown = false;
	}
	m_Text->Invalidate();   // the selection's colors follow the focus
	bHandled = FALSE;
	return 0;
}
