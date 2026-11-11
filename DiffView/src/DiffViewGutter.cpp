#include "DiffView.h"
#include "DiffViewModel.h"

#include <algorithm>
#include <climits>

// The gutter's extras: a "+" over the line number of the line under the mouse (a click starts a comment: the app is
// told, it makes the annotation) and icons that mark lines. The range a comment covers is DfCommentRange (the model).

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static unsigned long long IconKey(bool right, size_t line) {
	return ((unsigned long long)right << 63) | (line & INT64_MAX);
}

void CDiffViewCtrl::EnableGutterAdd(bool enable) {
	if (enable == m_GutterAdd)
		return;
	m_GutterAdd = enable;
	m_HotGutterRow = SIZE_MAX;
	UpdateScrollBars();   // a gutter without numbers is wider for the "+"
	if (IsWindow())
		Invalidate();
}

bool CDiffViewCtrl::IsGutterAddEnabled() const {
	return m_GutterAdd;
}

void CDiffViewCtrl::SetGutterIcon(bool right, size_t line, HICON icon) {
	bool had = !m_GutterIcons.empty();
	if (icon)
		m_GutterIcons[IconKey(right, line)] = icon;
	else
		m_GutterIcons.erase(IconKey(right, line));
	if (had != !m_GutterIcons.empty())
		UpdateScrollBars();   // the room for icons comes and goes
	if (IsWindow())
		Invalidate();
}

void CDiffViewCtrl::ClearGutterIcons() {
	if (m_GutterIcons.empty())
		return;
	m_GutterIcons.clear();
	UpdateScrollBars();
	if (IsWindow())
		Invalidate();
}

int CDiffViewCtrl::GutterExtra() const {
	return m_GutterIcons.empty() ? 0 : m_RowHeight;
}

int CDiffViewCtrl::GutterAddSize() const {
	return std::max(Scale(10), m_RowHeight - Scale(3));
}

// The "+" is a square right-aligned where the line's number ends, so it covers the number's last digits.
bool CDiffViewCtrl::GutterAddRect(size_t row, bool rightSide, RECT* prc) const {
	auto& rows = m_Doc->rows;
	if (row < m_TopRow || row >= rows.size())
		return false;
	auto& r = rows[row];
	int line = rightSide ? r.right : r.left;
	if (r.kind == DfRowKind::Fold || r.kind == DfRowKind::Annotation || line < 0)
		return false;
	const bool unified = m_Layout == DFVL_UNIFIED;
	auto rc = GetRowsRect();
	int y = rc.top + (int)(row - m_TopRow) * m_RowHeight;
	if (y >= rc.bottom)
		return false;
	int size = GutterAddSize();
	auto pane = GetPane(unified ? 0 : rightSide ? 1 : 0);
	int digits = 3;   // as the numbers are drawn: at least three, or as many as the largest line number
	for (size_t n = std::max(m_Doc->left.size(), m_Doc->right.size()) / 1000; n > 0; n /= 10)
		digits++;
	int numberWidth = digits * m_DigitWidth;
	int edge = pane.left + m_MarkerWidth + m_Pad;
	if (!m_LineNumbers)
		edge += size;
	else if (unified)
		edge += rightSide ? 2 * numberWidth + m_Pad : numberWidth;
	else
		edge += numberWidth;
	int top = y + (m_RowHeight - size) / 2;
	*prc = { edge - size, top, edge, top + size };
	return true;
}

// the gutter of a row with a line on the side the point is over (unified: the right's if the row has one)
bool CDiffViewCtrl::GutterRowAt(POINT pt, size_t& row, bool& rightSide) const {
	if (!m_GutterAdd || m_Job || m_Doc->rows.empty())
		return false;
	auto rc = GetRowsRect();
	if (!::PtInRect(&rc, pt))
		return false;
	row = m_TopRow + (size_t)(pt.y - rc.top) / m_RowHeight;
	if (row >= m_Doc->rows.size())
		return false;
	const bool unified = m_Layout == DFVL_UNIFIED;
	rightSide = unified ? m_Doc->rows[row].right >= 0 : PaneSideAt(pt.x);
	auto pane = GetPane(unified ? 0 : rightSide ? 1 : 0);
	RECT r;
	return pt.x >= pane.left && pt.x < pane.text && GutterAddRect(row, rightSide, &r);
}

bool CDiffViewCtrl::GutterAddAt(POINT pt, size_t& row, bool& rightSide) const {
	RECT r;
	return GutterRowAt(pt, row, rightSide) && row == m_HotGutterRow && rightSide == m_HotGutterRight &&
		GutterAddRect(row, rightSide, &r) && ::PtInRect(&r, pt);
}

void CDiffViewCtrl::UpdateHotGutter(POINT pt) {
	size_t row = SIZE_MAX;
	bool right = false;
	if (!GutterRowAt(pt, row, right))
		row = SIZE_MAX;
	if (row == m_HotGutterRow && (row == SIZE_MAX || right == m_HotGutterRight))
		return;
	auto invalidate = [&](size_t r) {
		if (r == SIZE_MAX || r < m_TopRow)
			return;
		auto rc = GetRowsRect();
		int y = rc.top + (int)(r - m_TopRow) * m_RowHeight;
		RECT row{ 0, y, rc.right, y + m_RowHeight };
		InvalidateRect(&row, FALSE);
	};
	invalidate(m_HotGutterRow);
	m_HotGutterRow = row;
	m_HotGutterRight = right;
	invalidate(row);
	if (row != SIZE_MAX && !m_TrackingMouse) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
}

BOOL CDiffViewCtrl::StartComment() {
	int row = CaretRow();
	if (m_Job || row < 0 || m_Doc->left.size() + m_Doc->right.size() == 0)
		return FALSE;
	auto& r = m_Doc->rows[row];
	// as the "+" of the row: unified, the right line if there is one
	bool rightSide = m_Layout == DFVL_UNIFIED ? r.right >= 0 : m_Caret.right != FALSE;
	if ((rightSide ? r.right : r.left) < 0)
		return FALSE;
	UserGutterAdd((size_t)row, rightSide);
	return TRUE;
}

void CDiffViewCtrl::UserGutterAdd(size_t row, bool rightSide) {
	auto& r = m_Doc->rows[row];
	DfPos clicked{ rightSide, (size_t)(rightSide ? r.right : r.left), 0 };
	DfPos start{}, end{};
	bool selected = HasSelection();
	if (selected) {
		DFVPOS s, e;
		GetSelection(&s, &e);
		start = { s.right != FALSE, s.line, s.column };
		end = { e.right != FALSE, e.line, e.column };
	}
	NMDFVGUTTERADD nm{};
	nm.right = rightSide;
	size_t first, last;
	DfCommentRange(selected, start, end, clicked, first, last);
	nm.firstLine = first;
	nm.lastLine = last;
	FireNotify(&nm.hdr, DFVN_GUTTERADD);
}

// ---- Painting ---------------------------------------------------------------------------------

void CDiffViewCtrl::PaintGutterExtras(WTL::CDCHandle dc, size_t index, int y, int pane, const DFVCOLORS& c) {
	auto& row = m_Doc->rows[index];
	if (!m_GutterIcons.empty()) {
		HICON icon = nullptr;
		auto find = [&](bool right, int line) {
			if (!icon && line >= 0) {
				auto it = m_GutterIcons.find(IconKey(right, (size_t)line));
				if (it != m_GutterIcons.end())
					icon = it->second;
			}
		};
		if (pane >= 0) {
			find(pane == 1, pane == 1 ? row.right : row.left);
		}
		else {
			find(false, row.left);
			find(true, row.right);
		}
		if (icon) {
			auto p = GetPane(pane < 0 ? 0 : pane);
			int size = std::max(1, m_RowHeight - Scale(2));
			dc.DrawIconEx(p.text - GutterExtra() + (GutterExtra() - size) / 2, y + (m_RowHeight - size) / 2, icon, size, size, 0, nullptr, DI_NORMAL);
		}
	}
	if (m_HotGutterRow == index && (pane < 0 || m_HotGutterRight == (pane == 1))) {
		RECT r;
		if (GutterAddRect(index, m_HotGutterRight, &r)) {
			FillSolid(dc, r, c.crSelBack);
			// a plus in the selection's text color
			int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
			int arm = std::max(2, (int)(r.right - r.left) / 4), thick = std::max(1, Scale(2));
			FillSolid(dc, { cx - arm, cy - thick / 2, cx + arm + 1, cy - thick / 2 + thick }, c.crSelText);
			FillSolid(dc, { cx - thick / 2, cy - arm, cx - thick / 2 + thick, cy + arm + 1 }, c.crSelText);
		}
	}
}
