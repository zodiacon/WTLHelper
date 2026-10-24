#include "DiffView.h"
#include "DiffViewModel.h"

#include <algorithm>

// Merging changes: the arrows between the panes, the keys, undo.

namespace {
	COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}
}

// ---- API -------------------------------------------------------------------------------------

void CDiffViewCtrl::EnableMerge(UINT flags) {
	flags &= DFVMF_TORIGHT | DFVMF_TOLEFT;
	if (flags == m_MergeFlags)
		return;
	m_MergeFlags = flags;
	m_HotMerge = -1;
	UpdateScrollBars();   // the strip between the panes is wider
	if (IsWindow())
		Invalidate();
}

UINT CDiffViewCtrl::GetMergeFlags() const {
	return m_MergeFlags;
}

BOOL CDiffViewCtrl::MergeChange(size_t index, bool toRight) {
	if (m_Job || index >= m_Doc->changeBlocks.size())
		return FALSE;
	auto& block = m_Doc->diff.blocks[m_Doc->changeBlocks[index]];
	DfLines left = m_Doc->left, right = m_Doc->right;
	m_Undo->items.push_back(DfMergeWithUndo(left, right, block, toRight));
	m_HotMerge = -1;
	Compare(std::move(left), std::move(right));
	return TRUE;
}

bool CDiffViewCtrl::CanUndo() const {
	return !m_Job && !m_Undo->items.empty();
}

BOOL CDiffViewCtrl::Undo() {
	if (!CanUndo())
		return FALSE;
	DfLines left = m_Doc->left, right = m_Doc->right;
	DfUndoMerge(left, right, m_Undo->items.back());
	m_Undo->items.pop_back();
	Compare(std::move(left), std::move(right));
	return TRUE;
}

bool CDiffViewCtrl::IsModified(bool right) const {
	return std::any_of(m_Undo->items.begin(), m_Undo->items.end(), [&](const DfMergeUndo& u) { return u.right == right; });
}

// ---- The arrows ------------------------------------------------------------------------------

int CDiffViewCtrl::CenterWidth() const {
	return m_Layout == DFVL_SIDEBYSIDE && m_MergeFlags ? Scale(40) : Scale(6);
}

bool CDiffViewCtrl::GetMergeRect(size_t change, bool toRight, RECT* prc) const {
	if (m_Layout != DFVL_SIDEBYSIDE || !(m_MergeFlags & (toRight ? DFVMF_TORIGHT : DFVMF_TOLEFT)) || change >= m_Doc->changeRows.size())
		return false;
	auto& cr = m_Doc->changeRows[change];
	if (cr.count == 0 || cr.first < m_TopRow || cr.first > m_TopRow + PageRows())
		return false;
	auto rc = GetRowsRect();
	auto p0 = GetPane(0), p1 = GetPane(1);
	int y = rc.top + (int)(cr.first - m_TopRow) * m_RowHeight;
	int gap = Scale(2), mid = (p0.right + p1.left) / 2;
	// the change's first row: to the right on the left half, to the left on the right half
	*prc = toRight ? RECT{ p0.right + gap, y, mid - gap / 2, y + m_RowHeight } : RECT{ mid + gap / 2, y, p1.left - gap, y + m_RowHeight };
	return prc->bottom > rc.top && prc->top < rc.bottom;
}

bool CDiffViewCtrl::MergeHitTest(POINT pt, int& change, bool& toRight) const {
	if (m_Layout != DFVL_SIDEBYSIDE || !m_MergeFlags || m_Job)
		return false;
	auto& changes = m_Doc->changeRows;
	auto it = std::lower_bound(changes.begin(), changes.end(), m_TopRow, [](const DfChangeRows& cr, size_t row) { return cr.first < row; });
	for (; it != changes.end() && it->first <= m_TopRow + PageRows(); ++it) {
		for (bool right : { true, false }) {
			RECT rc;
			if (GetMergeRect(it - changes.begin(), right, &rc) && ::PtInRect(&rc, pt)) {
				change = (int)(it - changes.begin());
				toRight = right;
				return true;
			}
		}
	}
	return false;
}

void CDiffViewCtrl::PaintMerge(WTL::CDCHandle dc, const DFVCOLORS& c) {
	if (m_Job)
		return;   // the arrows are for the diff that's in
	auto& changes = m_Doc->changeRows;
	auto it = std::lower_bound(changes.begin(), changes.end(), m_TopRow, [](const DfChangeRows& cr, size_t row) { return cr.first < row; });
	for (; it != changes.end() && it->first <= m_TopRow + PageRows(); ++it) {
		int change = (int)(it - changes.begin());
		for (bool right : { true, false }) {
			RECT rc;
			if (!GetMergeRect(change, right, &rc))
				continue;
			bool hot = m_HotMerge == change && m_HotToRight == right;
			if (hot) {
				WTL::CBrush back;
				back.CreateSolidBrush(Blend(c.crGutterBack, c.crText, 16));
				dc.FillRect(&rc, back);
			}
			// a triangle pointing the way the lines go
			COLORREF color = hot ? c.crText : Blend(c.crGutterBack, c.crText, 55);
			int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
			int half = std::max(2, (m_RowHeight - Scale(4)) / 2);
			int w = half * 3 / 4;
			POINT points[3];
			if (right) {
				points[0] = { cx - w, cy - half };
				points[1] = { cx + w, cy };
				points[2] = { cx - w, cy + half };
			}
			else {
				points[0] = { cx + w, cy - half };
				points[1] = { cx - w, cy };
				points[2] = { cx + w, cy + half };
			}
			WTL::CBrush brush;
			brush.CreateSolidBrush(color);
			WTL::CPen pen;
			pen.CreatePen(PS_SOLID, 1, color);
			auto oldBrush = dc.SelectBrush(brush);
			auto oldPen = dc.SelectPen(pen);
			dc.Polygon(points, 3);
			dc.SelectBrush(oldBrush);
			dc.SelectPen(oldPen);
		}
	}
}

void CDiffViewCtrl::PaintComparing(WTL::CDCHandle dc, const DFVCOLORS& c) {
	// a note at the top right of the rows
	auto rc = GetRowsRect();
	const wchar_t text[] = L"Comparing\x2026";
	auto old = dc.SelectFont(m_UiFont);
	SIZE size{};
	dc.GetTextExtent(text, (int)wcslen(text), &size);
	int pad = Scale(6);
	RECT box{ rc.right - size.cx - 2 * pad - Scale(8), rc.top + Scale(6), rc.right - Scale(8), rc.top + Scale(6) + size.cy + pad };
	WTL::CBrush back, frame;
	back.CreateSolidBrush(c.crFoldBack);
	frame.CreateSolidBrush(Blend(c.crFoldBack, c.crText, 40));
	dc.FillRect(&box, back);
	dc.FrameRect(&box, frame);
	dc.SetTextColor(c.crText);
	dc.DrawText(text, -1, &box, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	dc.SelectFont(old);
}

void CDiffViewCtrl::UpdateHotMerge(POINT pt) {
	int change = -1;
	bool toRight = false;
	if (!MergeHitTest(pt, change, toRight))
		change = -1;
	if (change == m_HotMerge && (change < 0 || toRight == m_HotToRight))
		return;
	RECT rc;
	if (m_HotMerge >= 0 && GetMergeRect(m_HotMerge, m_HotToRight, &rc))
		InvalidateRect(&rc, FALSE);
	m_HotMerge = change;
	m_HotToRight = toRight;
	if (change >= 0 && GetMergeRect(change, toRight, &rc))
		InvalidateRect(&rc, FALSE);
	if (change >= 0 && !m_TrackingMouse) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
}

LRESULT CDiffViewCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingMouse = false;
	UpdateHotMerge({ -1, -1 });
	return 0;
}

// ---- The user's merges ---------------------------------------------------------------------

void CDiffViewCtrl::UserMerge(size_t change, bool toRight) {
	if (m_Job || change >= m_Doc->changeBlocks.size())
		return;
	NMDFVMERGE nm{};
	nm.change = change;
	nm.toRight = toRight;
	GetChange(change, &nm.lines);
	if (FireNotify(&nm.hdr, DFVN_MERGE))
		return;   // the app said no
	if (MergeChange(change, toRight)) {
		NMHDR hdr{};
		FireNotify(&hdr, DFVN_TEXTCHANGED);
	}
}

LRESULT CDiffViewCtrl::OnSysKeyDown(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// Alt+Right and Alt+Left merge the caret's change
	bool alt = (lParam & (1 << 29)) != 0;
	if (alt && (wParam == VK_RIGHT || wParam == VK_LEFT)) {
		bool toRight = wParam == VK_RIGHT;
		int change = GetCurrentChange();
		if ((m_MergeFlags & (toRight ? DFVMF_TORIGHT : DFVMF_TOLEFT)) && change >= 0) {
			UserMerge(change, toRight);
			return 0;
		}
	}
	bHandled = FALSE;
	return 0;
}
