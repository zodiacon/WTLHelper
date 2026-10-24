#include "DiffView.h"
#include "DiffViewModel.h"

#include <algorithm>
#include <climits>
#include <cmath>

// The caret and the selection, the mouse and the keys, copying.

namespace {
	enum : UINT_PTR { DragTimer = 1 };

	DfPos ToDf(const DFVPOS& pos) {
		return { pos.right != FALSE, pos.line, pos.column };
	}

	DFVPOS ToDfv(const DfPos& pos) {
		return { pos.right ? TRUE : FALSE, pos.line, pos.column };
	}

	bool Same(const DFVPOS& a, const DFVPOS& b) {
		return (a.right != FALSE) == (b.right != FALSE) && a.line == b.line && a.column == b.column;
	}

	bool PutOnClipboard(HWND hWnd, const std::wstring& text) {
		if (!::OpenClipboard(hWnd))
			return false;
		::EmptyClipboard();
		bool ok = false;
		size_t bytes = (text.size() + 1) * sizeof(wchar_t);
		if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
			memcpy(::GlobalLock(mem), text.c_str(), bytes);
			::GlobalUnlock(mem);
			ok = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
			if (!ok)
				::GlobalFree(mem);
		}
		::CloseClipboard();
		return ok;
	}

	size_t Indent(std::wstring_view line) {
		size_t i = 0;
		while (i < line.size() && (line[i] == L' ' || line[i] == L'\t'))
			i++;
		return i;
	}
}

// ---- Positions ------------------------------------------------------------------------

DFVPOS CDiffViewCtrl::GetCaret() const {
	return m_Caret;
}

void CDiffViewCtrl::SetCaret(DFVPOS pos, bool extend) {
	MoveCaret(pos, extend, false);
}

void CDiffViewCtrl::GetSelection(DFVPOS* pStart, DFVPOS* pEnd) const {
	bool backward = m_Doc->Less(ToDf(m_Caret), ToDf(m_Anchor));
	if (pStart)
		*pStart = backward ? m_Caret : m_Anchor;
	if (pEnd)
		*pEnd = backward ? m_Anchor : m_Caret;
}

void CDiffViewCtrl::SetSelection(DFVPOS anchor, DFVPOS caret) {
	MoveCaret(anchor, false, false);
	MoveCaret(caret, true, false);
}

bool CDiffViewCtrl::HasSelection() const {
	return !Same(m_Anchor, m_Caret);
}

void CDiffViewCtrl::SelectAll() {
	DfPos start, end;
	m_Doc->All(m_Caret.right != FALSE, start, end);
	m_Anchor = ToDfv(start);
	m_Caret = ToDfv(end);
	m_DesiredX = -1;
	UpdateCaret();
	if (IsWindow())
		Invalidate();
}

ATL::CString CDiffViewCtrl::GetSelectedText() const {
	if (!HasSelection())
		return {};
	auto text = m_Doc->Text(ToDf(m_Anchor), ToDf(m_Caret));
	return ATL::CString(text.c_str(), (int)std::min<size_t>(text.size(), INT_MAX));
}

BOOL CDiffViewCtrl::Copy() {
	std::wstring text;
	if (HasSelection())
		text = m_Doc->Text(ToDf(m_Anchor), ToDf(m_Caret));
	else {
		auto& lines = m_Doc->Lines(m_Caret.right != FALSE);
		if (m_Caret.line >= lines.size())
			return FALSE;
		text = lines[m_Caret.line];
		text += L"\r\n";
	}
	return PutOnClipboard(m_hWnd, text);
}

void CDiffViewCtrl::ClampPositions() {
	m_Caret = ToDfv(m_Doc->Clamp(ToDf(m_Caret)));
	m_Anchor = ToDfv(m_Doc->Clamp(ToDf(m_Anchor)));
	if (m_Layout == DFVL_SIDEBYSIDE && (m_Anchor.right != FALSE) != (m_Caret.right != FALSE))
		m_Anchor = m_Caret;
	m_DesiredX = -1;
	UpdateCaret();
}

int CDiffViewCtrl::CaretRow() const {
	return m_Doc->RowOf(m_Caret.right != FALSE, m_Caret.line);
}

void CDiffViewCtrl::MoveCaret(DFVPOS pos, bool extend, bool notify) {
	pos = ToDfv(m_Doc->Clamp(ToDf(pos)));
	auto oldCaret = m_Caret, oldAnchor = m_Anchor;
	// side by side, a selection is in one pane
	if (extend && m_Layout == DFVL_SIDEBYSIDE && (pos.right != FALSE) != (m_Anchor.right != FALSE))
		extend = false;
	m_Caret = pos;
	if (!extend)
		m_Anchor = pos;
	if (Same(oldCaret, m_Caret) && Same(oldAnchor, m_Anchor))
		return;
	if (IsWindow()) {
		RECT rc;
		GetClientRect(&rc);
		rc.top = GetRowsRect().top;
		InvalidateRect(&rc, FALSE);
		EnsureCaretVisible();
		UpdateCaret();
	}
	if (notify)
		FireSelChanged();
}


int CDiffViewCtrl::GetCurrentChange() const {
	int row = CaretRow();
	if (row < 0 || m_Doc->rows[row].kind != DfRowKind::Changed)
		return -1;
	return m_Doc->ChangeOfBlock(m_Doc->rows[row].block);
}

BOOL CDiffViewCtrl::GoToChange(size_t index) {
	if (index >= m_Doc->changeRows.size())
		return FALSE;
	auto& cr = m_Doc->changeRows[index];
	// side by side: the caret's pane if the change has lines there
	bool side = m_Caret.right != FALSE;
	if (m_Layout == DFVL_SIDEBYSIDE && !(side ? cr.right : cr.left))
		side = !side;
	DfPos pos;
	for (size_t r = cr.first; r < cr.first + cr.count; r++) {
		if (m_Doc->LineOfRow(r, side, pos)) {
			MoveCaret(ToDfv(pos), false, false);
			break;
		}
	}
	EnsureChangeVisible(index);
	return TRUE;
}

BOOL CDiffViewCtrl::NextChange(bool forward) {
	int row = DfNextChange(m_Doc->rows, CaretRow(), forward);
	if (row < 0)
		return FALSE;
	int change = m_Doc->ChangeOfBlock(m_Doc->rows[row].block);
	return change >= 0 && GoToChange(change);
}

// ---- The system caret -----------------------------------------------------------------------

void CDiffViewCtrl::CreateSystemCaret() {
	UINT width = 1;
	::SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0);
	::CreateCaret(m_hWnd, nullptr, std::max(1, Scale((int)width) + (m_Dpi > 96 ? 1 : 0)), m_RowHeight);
	m_HasCaret = true;
	UpdateCaret();
	::ShowCaret(m_hWnd);
}

void CDiffViewCtrl::UpdateCaret() {
	if (!m_HasCaret)
		return;
	POINT at{ -1000, -1000 };   // out of view
	int row = CaretRow();
	auto rc = GetRowsRect();
	if (row >= 0 && (size_t)row >= m_TopRow && m_Doc->rows[row].kind != DfRowKind::Fold) {
		int y = rc.top + (int)((size_t)row - m_TopRow) * m_RowHeight;
		auto pane = GetPane(m_Layout == DFVL_SIDEBYSIDE && m_Caret.right ? 1 : 0);
		auto& lines = m_Doc->Lines(m_Caret.right != FALSE);
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		int x = pane.text + m_Pad - m_Left + ColumnToX(dc.m_hDC, lines[m_Caret.line], m_Caret.column);
		dc.SelectFont(old);
		if (y < rc.bottom && x >= pane.text && x < pane.right)
			at = { x, y };
	}
	::SetCaretPos(at.x, at.y);
}

void CDiffViewCtrl::EnsureCaretVisible() {
	int row = CaretRow();
	if (row < 0)
		return;
	if (m_Doc->rows[row].kind == DfRowKind::Fold) {
		ExpandFold(row, false);   // a position in a fold shows its lines
		row = CaretRow();
		if (row < 0)
			return;
	}
	size_t top = m_TopRow, page = PageRows();
	if ((size_t)row < top)
		top = row;
	else if ((size_t)row >= top + page)
		top = row - page + 1;

	auto& lines = m_Doc->Lines(m_Caret.right != FALSE);
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	int x = ColumnToX(dc.m_hDC, lines[m_Caret.line], m_Caret.column);
	dc.SelectFont(old);
	int left = m_Left, width = TextWidth() - 2 * m_Pad;
	if (x < left)
		left = std::max(0, x - 4 * m_CharWidth);
	else if (x > left + width - m_CharWidth)
		left = x - width + 4 * m_CharWidth;
	ScrollTo(top, left);
}

LRESULT CDiffViewCtrl::OnFocus(UINT uMsg, WPARAM, LPARAM, BOOL&) {
	if (uMsg == WM_SETFOCUS)
		CreateSystemCaret();
	else {
		::DestroyCaret();
		m_HasCaret = false;
	}
	Invalidate();   // the selection's colors
	return 0;
}

// ---- Hit testing --------------------------------------------------------------------------

bool CDiffViewCtrl::PaneSideAt(int x) const {
	auto p0 = GetPane(0), p1 = GetPane(1);
	return x >= (p0.right + p1.left) / 2;
}

size_t CDiffViewCtrl::IndexAtX(bool rightSide, size_t line, int x) const {
	auto& lines = m_Doc->Lines(rightSide);
	if (line >= lines.size())
		return 0;
	auto text = lines[line];
	if (m_FixedPitch)
		return DfIndexAtColumn(text, (double)x / m_CharWidth, m_TabSize);
	// a proportional font: the boundary nearest to x
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	size_t best = 0;
	int prev = 0;
	for (size_t i = 1; i <= std::min<size_t>(text.size(), 4096); i++) {
		int next = ColumnToX(dc.m_hDC, text, i);
		if (x < (prev + next) / 2)
			break;
		best = i;
		prev = next;
	}
	dc.SelectFont(old);
	return best;
}

DFVPOS CDiffViewCtrl::PosFromPoint(POINT pt, bool rightSide, UINT* pPart) const {
	UINT part = DFVHT_NOWHERE;
	DfPos pos{ rightSide, 0, 0 };
	auto& rows = m_Doc->rows;
	if (!rows.empty()) {
		auto rc = GetRowsRect();
		long long at = (long long)m_TopRow + (long long)std::floor((double)(pt.y - rc.top) / m_RowHeight);
		size_t row = (size_t)std::clamp<long long>(at, 0, (long long)rows.size() - 1);
		auto& r = rows[row];
		const bool unified = m_Layout == DFVL_UNIFIED;
		auto pane = GetPane(unified ? 0 : rightSide ? 1 : 0);
		if (r.kind == DfRowKind::Fold) {
			part = DFVHT_FOLD;
			bool side = unified ? false : rightSide;
			pos = { side, (size_t)(side ? r.right : r.left), 0 };
		}
		else if (m_Doc->LineOfRow(row, rightSide, pos)) {
			part = pt.x < pane.text ? DFVHT_GUTTER : DFVHT_TEXT;
			if (part == DFVHT_TEXT)
				pos.column = IndexAtX(pos.right, pos.line, pt.x - (pane.text + m_Pad - m_Left));
		}
		else {
			// a filler: the nearest line of the side
			part = DFVHT_FILLER;
			int nearest = m_Doc->StepRow((int)row, 0, rightSide);
			if (nearest >= 0)
				m_Doc->LineOfRow(nearest, rightSide, pos);
		}
	}
	if (pPart)
		*pPart = part;
	return ToDfv(pos);
}

// ---- Mouse ---------------------------------------------------------------------------------

LRESULT CDiffViewCtrl::OnLButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	auto overview = GetOverviewRect();
	if (m_ShowOverview && ::PtInRect(&overview, pt)) {
		m_Drag = Drag::Overview;
		SetCapture();
		ScrollToOverview(pt.y);
		return 0;
	}
	auto rc = GetRowsRect();
	if (!::PtInRect(&rc, pt) || m_Doc->rows.empty())
		return 0;
	size_t row = m_TopRow + (pt.y - rc.top) / m_RowHeight;
	if (row >= m_Doc->rows.size()) {
		// below the last row: the end
		DfPos start, end;
		m_Doc->All(m_Caret.right != FALSE, start, end);
		MoveCaret(ToDfv(end), (wParam & MK_SHIFT) != 0, true);
		return 0;
	}
	if (m_Doc->rows[row].kind == DfRowKind::Fold) {
		ExpandFold(row, true);   // a click on a fold shows its lines
		return 0;
	}
	bool shift = (wParam & MK_SHIFT) != 0;
	bool side = m_Layout == DFVL_SIDEBYSIDE && (shift ? m_Anchor.right != FALSE : PaneSideAt(pt.x));
	UINT part;
	auto pos = PosFromPoint(pt, side, &part);
	m_DesiredX = -1;
	if (part == DFVHT_GUTTER && !shift) {
		// the gutter selects the line, and dragging there whole lines
		auto line = ToDf(pos);
		line.column = 0;
		DfPos next = line;
		int r = m_Doc->RowOf(line), below = m_Doc->StepRow(r, 1, line.right);
		if (below != r && below >= 0)
			m_Doc->LineOfRow(below, line.right, next);
		else
			next.column = m_Doc->Lines(line.right)[line.line].size();
		m_DragStart = ToDfv(line);
		m_DragEnd = ToDfv(next);
		MoveCaret(m_DragStart, false, false);
		MoveCaret(m_DragEnd, true, true);
		m_Drag = Drag::Lines;
	}
	else {
		MoveCaret(pos, shift, true);
		m_Drag = Drag::Select;
	}
	SetCapture();
	return 0;
}

LRESULT CDiffViewCtrl::OnLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	auto rc = GetRowsRect();
	if (!::PtInRect(&rc, pt))
		return 0;
	bool side = m_Layout == DFVL_SIDEBYSIDE && PaneSideAt(pt.x);
	UINT part;
	auto pos = PosFromPoint(pt, side, &part);
	if (part != DFVHT_TEXT)
		return 0;
	// the word, and dragging on by words
	size_t start, end;
	DfWordAt(m_Doc->Lines(pos.right != FALSE)[pos.line], pos.column, start, end);
	m_DragStart = { pos.right, pos.line, start };
	m_DragEnd = { pos.right, pos.line, end };
	MoveCaret(m_DragStart, false, false);
	MoveCaret(m_DragEnd, true, true);
	m_Drag = Drag::Words;
	SetCapture();
	return 0;
}

void CDiffViewCtrl::DragTo(POINT pt) {
	if (m_Drag == Drag::Overview) {
		ScrollToOverview(pt.y);
		return;
	}
	// side by side, the pane the selection started in
	bool side = m_Layout == DFVL_SIDEBYSIDE && (m_Drag == Drag::Select ? m_Anchor.right : m_DragStart.right) != FALSE;
	UINT part;
	auto pos = PosFromPoint(pt, side, &part);
	switch (m_Drag) {
		case Drag::Select:
			MoveCaret(pos, true, true);
			break;
		case Drag::Words: {
			size_t start, end;
			DfWordAt(m_Doc->Lines(pos.right != FALSE)[pos.line], pos.column, start, end);
			bool before = m_Doc->Less(ToDf(pos), ToDf(m_DragStart));
			m_Anchor = before ? m_DragEnd : m_DragStart;
			MoveCaret({ pos.right, pos.line, before ? start : end }, true, true);
			break;
		}
		case Drag::Lines: {
			auto line = ToDf(pos);
			line.column = 0;
			if (m_Doc->Less(line, ToDf(m_DragStart))) {
				m_Anchor = m_DragEnd;
				MoveCaret(ToDfv(line), true, true);
			}
			else {
				DfPos next = line;
				int r = m_Doc->RowOf(line), below = m_Doc->StepRow(r, 1, line.right);
				if (below != r && below >= 0)
					m_Doc->LineOfRow(below, line.right, next);
				else
					next.column = m_Doc->Lines(line.right)[line.line].size();
				m_Anchor = m_DragStart;
				MoveCaret(ToDfv(next), true, true);
			}
			break;
		}
		default:
			break;
	}
}

LRESULT CDiffViewCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag == Drag::None || ::GetCapture() != m_hWnd)
		return 0;
	m_DragPoint = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
	DragTo(m_DragPoint);
	// past the edges: scroll on a timer
	auto rc = GetRowsRect();
	bool outside = m_Drag != Drag::Overview && (m_DragPoint.y < rc.top || m_DragPoint.y >= rc.bottom || m_DragPoint.x < 0 || m_DragPoint.x >= rc.right);
	if (outside)
		SetTimer(DragTimer, 50);
	else
		KillTimer(DragTimer);
	return 0;
}

LRESULT CDiffViewCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != DragTimer) {
		bHandled = FALSE;
		return 0;
	}
	if (m_Drag == Drag::None) {
		KillTimer(DragTimer);
		return 0;
	}
	auto rc = GetRowsRect();
	auto pt = m_DragPoint;
	size_t top = m_TopRow;
	int left = m_Left;
	if (pt.y < rc.top)
		top -= std::min<size_t>(top, 1 + (rc.top - pt.y) / m_RowHeight);
	else if (pt.y >= rc.bottom)
		top += 1 + (pt.y - rc.bottom) / m_RowHeight;
	auto pane = GetPane(m_Layout == DFVL_SIDEBYSIDE && m_Anchor.right ? 1 : 0);
	if (pt.x < pane.text)
		left -= 4 * m_CharWidth;
	else if (pt.x >= pane.right)
		left += 4 * m_CharWidth;
	ScrollTo(top, left);
	DragTo(pt);
	return 0;
}

LRESULT CDiffViewCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	return 0;
}

LRESULT CDiffViewCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Drag = Drag::None;
	KillTimer(DragTimer);
	return 0;
}

LRESULT CDiffViewCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam != m_hWnd || LOWORD(lParam) != HTCLIENT) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	DFVHITTEST hit{ pt };
	HitTest(&hit);
	LPCWSTR cursor = IDC_ARROW;
	if (hit.part == DFVHT_TEXT)
		cursor = IDC_IBEAM;
	else if (hit.part == DFVHT_FOLD)
		cursor = IDC_HAND;
	::SetCursor(::LoadCursor(nullptr, cursor));
	return TRUE;
}

LRESULT CDiffViewCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	// outside the selection the caret moves there first, so the menu is about what was clicked
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	DFVHITTEST hit{ pt };
	HitTest(&hit);
	if (hit.part != DFVHT_TEXT && hit.part != DFVHT_GUTTER)
		return 0;
	DFVPOS start, end;
	GetSelection(&start, &end);
	bool inside = HasSelection() && (hit.pos.right != FALSE) == (start.right != FALSE) &&
		!m_Doc->Less(ToDf(hit.pos), ToDf(start)) && m_Doc->Less(ToDf(hit.pos), ToDf(end));
	if (!inside)
		MoveCaret(hit.pos, false, true);
	return 0;
}

LRESULT CDiffViewCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	NMDFVCONTEXTMENU nm{};
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: below the caret
		EnsureCaretVisible();
		int row = CaretRow();
		auto rc = GetRowsRect();
		auto pane = GetPane(m_Layout == DFVL_SIDEBYSIDE && m_Caret.right ? 1 : 0);
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		int x = pane.text + m_Pad - m_Left + ColumnToX(dc.m_hDC, m_Doc->Lines(m_Caret.right != FALSE)[m_Caret.line], m_Caret.column);
		dc.SelectFont(old);
		pt = { std::clamp(x, pane.text, pane.right), rc.top + (int)((size_t)std::max(row, 0) - m_TopRow + 1) * m_RowHeight };
		ClientToScreen(&pt);
		nm.part = DFVHT_TEXT;
		nm.pos = m_Caret;
	}
	else {
		DFVHITTEST hit{ pt };
		ScreenToClient(&hit.pt);
		HitTest(&hit);
		nm.part = hit.part;
		nm.pos = hit.pos;
	}
	nm.pt = pt;
	FireNotify(&nm.hdr, DFVN_CONTEXTMENU);
	return 0;
}

// ---- Keys ----------------------------------------------------------------------------------

LRESULT CDiffViewCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool shift = ::GetKeyState(VK_SHIFT) < 0, ctrl = ::GetKeyState(VK_CONTROL) < 0;
	switch (wParam) {
		case VK_F7:
			if (NextChange(!shift))
				FireSelChanged();
			return 0;
		case VK_F6:
			if (m_Layout == DFVL_SIDEBYSIDE) {
				// the other pane, at its line on the caret's row (or the nearest)
				bool side = !m_Caret.right;
				int row = m_Doc->StepRow(CaretRow(), 0, side);
				DfPos pos;
				if (row >= 0 && m_Doc->LineOfRow(row, side, pos)) {
					pos.column = m_Caret.column;
					MoveCaret(ToDfv(pos), false, true);
				}
			}
			return 0;
		case VK_RETURN: {
			int row = CaretRow();
			auto& rows = m_Doc->rows;
			if (row + 1 < (int)rows.size() && rows[row + 1].kind == DfRowKind::Fold)
				ExpandFold(row + 1, true);
			else if (row > 0 && rows[row - 1].kind == DfRowKind::Fold)
				ExpandFold(row - 1, true);
			return 0;
		}
		case 'A':
			if (!ctrl)
				break;
			SelectAll();
			FireSelChanged();
			return 0;
		case 'C':
		case VK_INSERT:
			if (ctrl)
				Copy();
			return 0;
		case VK_UP:
		case VK_DOWN:
			if (ctrl) {
				size_t top = m_TopRow;
				ScrollTo(wParam == VK_UP ? top - std::min<size_t>(top, 1) : top + 1, m_Left);
				return 0;
			}
			[[fallthrough]];
		case VK_LEFT:
		case VK_RIGHT:
		case VK_HOME:
		case VK_END:
		case VK_PRIOR:
		case VK_NEXT:
			MoveByKey((UINT)wParam, shift, ctrl);
			return 0;
	}
	bHandled = FALSE;
	return 0;
}

void CDiffViewCtrl::MoveByKey(UINT vk, bool shift, bool ctrl) {
	auto& rows = m_Doc->rows;
	if (rows.empty())
		return;
	auto pos = ToDf(m_Caret);
	const bool side = pos.right;
	auto text = [&](const DfPos& p) { return m_Doc->Lines(p.right)[p.line]; };
	int row = CaretRow();
	if (row < 0)
		row = m_Doc->StepRow(0, 0, side);
	// a row's line (side by side, on the caret's side)
	auto lineOf = [&](int r, DfPos& p) { return r >= 0 && m_Doc->LineOfRow(r, side, p); };
	auto vertical = [&](int delta) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		if (m_DesiredX < 0)
			m_DesiredX = ColumnToX(dc.m_hDC, text(pos), pos.column);
		dc.SelectFont(old);
		DfPos p;
		if (lineOf(m_Doc->StepRow(row, delta, side), p)) {
			p.column = IndexAtX(p.right, p.line, m_DesiredX);
			pos = p;
		}
	};
	bool keepX = false;
	switch (vk) {
		case VK_LEFT:
			if (pos.column > 0)
				pos.column = ctrl ? DfWordLeft(text(pos), pos.column) : pos.column - 1;
			else {
				int up = m_Doc->StepRow(row, -1, side);
				DfPos p;
				if (up != row && lineOf(up, p)) {
					p.column = text(p).size();
					pos = p;
				}
			}
			break;
		case VK_RIGHT:
			if (pos.column < text(pos).size())
				pos.column = ctrl ? DfWordRight(text(pos), pos.column) : pos.column + 1;
			else {
				int down = m_Doc->StepRow(row, 1, side);
				DfPos p;
				if (down != row && lineOf(down, p))
					pos = p;
			}
			break;
		case VK_HOME:
			if (ctrl) {
				DfPos p;
				if (lineOf(m_Doc->StepRow(0, 0, side), p))
					pos = p;
			}
			else {
				// the indentation first, then the line's start
				size_t indent = Indent(text(pos));
				pos.column = pos.column == indent ? 0 : indent;
			}
			break;
		case VK_END:
			if (ctrl) {
				DfPos p;
				if (lineOf(m_Doc->StepRow((int)rows.size() - 1, 0, side), p))
					pos = p;
			}
			pos.column = text(pos).size();
			break;
		case VK_UP:
		case VK_DOWN:
			vertical(vk == VK_UP ? -1 : 1);
			keepX = true;
			break;
		case VK_PRIOR:
		case VK_NEXT: {
			int page = std::max(1, PageRows() - 1);
			size_t top = m_TopRow;
			ScrollTo(vk == VK_PRIOR ? top - std::min<size_t>(top, page) : top + page, m_Left);
			vertical(vk == VK_PRIOR ? -page : page);
			keepX = true;
			break;
		}
	}
	int desired = m_DesiredX;
	MoveCaret(ToDfv(pos), shift, true);
	m_DesiredX = keepX ? desired : -1;
}

// ---- Notifications -----------------------------------------------------------------------

LRESULT CDiffViewCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

void CDiffViewCtrl::FireSelChanged() {
	NMHDR hdr{};
	FireNotify(&hdr, DFVN_SELCHANGED);
}
