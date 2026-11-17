#include "DataGridData.h"

#include <algorithm>

// The mouse over the cells (the header's is in DataGridHeader.cpp), the keys and the focus.

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}
}

// ---- The mouse ------------------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnLButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	POINT pt = PointOf(lParam);
	if (!CommitOrCancel())   // a value that can't be kept: the editor stays, the click does nothing
		return 0;
	if (::GetFocus() != m_hWnd)
		SetFocus();
	DGVHITTEST hit{ pt };
	int col;
	Hit(pt, hit, col);
	if (hit.flags & DGVHT_HEADERBUTTON) {
		ShowHeaderMenu(col);
		return 0;
	}
	if (hit.flags & (DGVHT_HEADER | DGVHT_HEADERDIVIDER)) {
		HeaderMouseDown(pt, hit, col);
		return 0;
	}
	if (hit.flags & DGVHT_FILTERROW) {
		OpenFilterEdit(col);
		return 0;
	}
	if (hit.flags & DGVHT_GROUP) {   // opens or closes it
		ToggleGroup(hit.iViewRow, true);
		return 0;
	}
	if (hit.flags & DGVHT_CORNER) {
		if (d.selection.Mode() != DgSelMode::Single) {
			SelectAll();
			FireSelChanged();
		}
		return 0;
	}
	// Shift extends from the anchor, Ctrl adds a range (whole rows: toggles a row), else only this cell
	bool shift = (wParam & MK_SHIFT) != 0, ctrl = (wParam & MK_CONTROL) != 0;
	SelOp op = shift ? SelOp::Extend : ctrl ? (d.selection.Mode() == DgSelMode::Rows ? SelOp::Toggle : SelOp::Add) : SelOp::Reset;
	int64_t view = hit.iViewRow;   // the new row's placeholder too
	if (hit.flags & DGVHT_CELL) {
		Select(op, view, col, true);
		if (op != SelOp::Toggle)
			StartSelectDrag(false);
		auto column = d.columns.Find(hit.idColumn);
		if (op == SelOp::Reset && column && column->type == DgColType::Link && hit.iRow >= 0) {
			POINT screen = pt;
			ClientToScreen(&screen);
			NotifyCell(DGVN_LINKCLICK, hit, screen);
		}
		if (op == SelOp::Reset && column && column->type == DgColType::Bool && OnCheckBox(view, col, pt))
			ToggleChecks(view, col, false);
	}
	else if (hit.flags & DGVHT_ROWHEADER) {
		Select(op, view, std::max(0, d.layout.IndexOf(d.caretId)), true, true);
		if (op != SelOp::Toggle)
			StartSelectDrag(true);
	}
	return 0;
}

LRESULT CDataGridCtrl::OnLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt = PointOf(lParam);
	DGVHITTEST hit{ pt };
	int col;
	Hit(pt, hit, col);
	if (hit.flags & DGVHT_HEADERDIVIDER) {
		auto column = m_Data->columns.Find(hit.idColumn);
		if (!column || (GetStyle() & DGVS_NOCOLUMNRESIZE) || (column->flags & DgColFlag::NoResize))
			return 0;
		AutoFitColumn(hit.idColumn);
		NMDGVCOLUMN nm{};
		nm.idColumn = hit.idColumn;
		nm.iDisplayIndex = m_Data->columns.DisplayIndexOf(hit.idColumn);
		nm.cxWidth = GetColumnWidth(hit.idColumn);
		FireNotify(&nm.hdr, DGVN_COLUMNRESIZED);
		return 0;
	}
	if (hit.flags & (DGVHT_HEADER | DGVHT_GROUP)) {   // as a second click
		BOOL handled;
		return OnLButtonDown(WM_LBUTTONDOWN, 0, lParam, handled);
	}
	if (hit.flags & (DGVHT_HEADERBUTTON | DGVHT_FILTERROW))
		return 0;
	if (hit.flags & DGVHT_CELL) {
		auto& d = *m_Data;
		auto column = d.columns.Find(hit.idColumn);
		int64_t view = d.rows.ToView(hit.iRow);
		if (column && column->type == DgColType::Bool && OnCheckBox(view, col, pt)) {   // a second click on it
			if (CommitOrCancel())
				ToggleChecks(view, col, false);
			return 0;
		}
		if (column && column->type != DgColType::Bool && CanEditCell(hit.iRow, hit.idColumn)) {   // editable: an editor
			StartEdit(hit.iRow, hit.idColumn, nullptr);
			return 0;
		}
		if (hit.iRow < 0)
			return 0;
		POINT screen = pt;
		ClientToScreen(&screen);
		NotifyCell(DGVN_ACTIVATE, hit, screen);
	}
	return 0;
}

bool CDataGridCtrl::OnCheckBox(LONGLONG viewRow, int displayCol, POINT pt) const {
	auto& d = *m_Data;
	auto& L = d.layout;
	if (displayCol < 0 || displayCol >= (int)L.cols.size() || viewRow < d.topRow)
		return false;
	auto column = d.columns.Find(L.cols[displayCol].id);
	if (!column || !CanEdit(d.rows.ToData(viewRow), column->id))
		return false;
	int y = L.header + (int)(viewRow - d.topRow) * L.rowHeight;
	RECT cell{ L.Left(displayCol, d.scrollX), y, L.Right(displayCol, d.scrollX), y + L.rowHeight };
	RECT box = CheckRect(cell, DgEffectiveAlign(*column));
	::InflateRect(&box, Scale(3), Scale(3));   // a little slack around it
	return ::PtInRect(&box, pt) != FALSE;
}

LRESULT CDataGridCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Data->drag == DgData::Drag::Select)
		EndSelectDrag();
	else if (m_Data->drag != DgData::Drag::None)
		HeaderMouseUp(PointOf(lParam));
	return 0;
}

LRESULT CDataGridCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.drag == DgData::Drag::Select) {
		KillTimer(DgData::ScrollTimer);
		d.drag = DgData::Drag::None;
	}
	else if (d.drag != DgData::Drag::None) {   // lost: no click, no move
		d.drag = DgData::Drag::None;
		d.dragCol = -1;
		d.dropIndex = -1;
		Invalidate();
	}
	return 0;
}

LRESULT CDataGridCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	POINT pt = PointOf(lParam);
	if (d.drag == DgData::Drag::Select) {
		SelectDragTo(pt);
		return 0;
	}
	if (d.drag != DgData::Drag::None) {
		HeaderMouseMove(pt);
		return 0;
	}
	if (!d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	DGVHITTEST hit{ pt };
	int col;
	Hit(pt, hit, col);
	int hot = (hit.flags & (DGVHT_HEADER | DGVHT_HEADERBUTTON)) ? col : -1;
	int hotFilter = (hit.flags & DGVHT_FILTERROW) ? col : -1;
	if (hot != d.hotCol || hotFilter != d.hotFilterCol) {
		d.hotCol = hot;
		d.hotFilterCol = hotFilter;
		RECT rc{ 0, 0, d.clientWidth, d.layout.header };
		InvalidateRect(&rc, FALSE);
	}
	return 0;
}

LRESULT CDataGridCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	d.tracking = false;
	if (d.hotCol >= 0 || d.hotFilterCol >= 0) {
		d.hotCol = d.hotFilterCol = -1;
		RECT rc{ 0, 0, d.clientWidth, d.layout.header };
		InvalidateRect(&rc, FALSE);
	}
	return 0;
}

LRESULT CDataGridCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if ((HWND)wParam != m_hWnd || LOWORD(lParam) != HTCLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (d.drag == DgData::Drag::Resize) {
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
		return TRUE;
	}
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	DGVHITTEST hit{ pt };
	int col;
	Hit(pt, hit, col);
	if (hit.flags & DGVHT_HEADERDIVIDER) {
		auto column = d.columns.Find(hit.idColumn);
		if (column && !(GetStyle() & DGVS_NOCOLUMNRESIZE) && !(column->flags & DgColFlag::NoResize)) {
			::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
			return TRUE;
		}
	}
	if (hit.flags & DGVHT_CELL) {
		auto column = d.columns.Find(hit.idColumn);
		if (column && column->type == DgColType::Link) {
			::SetCursor(::LoadCursor(nullptr, IDC_HAND));
			return TRUE;
		}
	}
	if (hit.flags & DGVHT_FILTERROW) {
		::SetCursor(::LoadCursor(nullptr, IDC_IBEAM));
		return TRUE;
	}
	::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
	return TRUE;
}

LRESULT CDataGridCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	// the caret goes to the cell, as in Excel; DefWindowProc sends WM_CONTEXTMENU on the button's release
	auto& d = *m_Data;
	POINT pt = PointOf(lParam);
	if (!CommitOrCancel())
		return 0;
	if (::GetFocus() != m_hWnd)
		SetFocus();
	DGVHITTEST hit{ pt };
	int col;
	Hit(pt, hit, col);
	if (hit.flags & DGVHT_CELL) {   // inside the selection it stays, as in Excel
		int64_t view = d.rows.ToView(hit.iRow);
		if (d.selection.Contains(view, col))
			MoveCaret(view, col, true);
		else
			Select(SelOp::Reset, view, col, true);
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CDataGridCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	POINT screen = PointOf(lParam);
	DGVHITTEST hit{};
	if (screen.x == -1 && screen.y == -1) {   // the keyboard's: under the caret's cell
		hit.flags = DGVHT_CELL;
		hit.iRow = d.caretRow;
		hit.idColumn = d.caretId;
		int64_t view = d.rows.ToView(d.caretRow);
		int col = d.layout.IndexOf(d.caretId);
		POINT pt{ d.layout.rowHeader, d.layout.header };
		if (view >= d.topRow && col >= 0)
			pt = { d.layout.Left(col, d.scrollX), d.layout.header + (int)(view - d.topRow + 1) * d.layout.rowHeight };
		if (d.caretRow < 0)
			hit.flags = DGVHT_NOWHERE;
		screen = pt;
		ClientToScreen(&screen);
	}
	else {
		POINT pt = screen;
		ScreenToClient(&pt);
		hit.pt = pt;
		int col;
		Hit(pt, hit, col);
	}
	NotifyCell(DGVN_CONTEXTMENU, hit, screen);
	return 0;
}

// ---- The keys -------------------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0;
	if (FindKey(wParam, ctrl, shift) || EditKey(wParam, ctrl, shift) || SelectionKey(wParam, ctrl, shift))
		return 0;
	DgKey key;
	switch (wParam) {
		case VK_LEFT:  key = DgKey::Left; break;
		case VK_RIGHT: key = DgKey::Right; break;
		case VK_UP:    key = DgKey::Up; break;
		case VK_DOWN:  key = DgKey::Down; break;
		case VK_PRIOR: key = DgKey::PageUp; break;
		case VK_NEXT:  key = DgKey::PageDown; break;
		case VK_HOME:  key = DgKey::Home; break;
		case VK_END:   key = DgKey::End; break;
		case VK_SUBTRACT:
		case VK_OEM_MINUS:
		case VK_ADD:
		case VK_OEM_PLUS: {   // Ctrl+-: close the caret's group; Ctrl++: open them all
			if (!ctrl || d.view.groups.empty()) {
				bHandled = FALSE;
				return 0;
			}
			if (wParam == VK_ADD || wParam == VK_OEM_PLUS) {
				ExpandAllGroups(true);
				return 0;
			}
			// the innermost group's row above the caret: the nearest one
			for (int64_t r = d.rows.ToView(d.caretRow); r >= 0; r--)
				if (d.rows.GroupOf(r) >= 0) {
					ToggleGroup(r, true);
					break;
				}
			return 0;
		}
		case VK_RETURN:
			if (d.caretRow >= 0) {
				DGVHITTEST hit{};
				hit.flags = DGVHT_CELL;
				hit.iRow = d.caretRow;
				hit.idColumn = d.caretId;
				NotifyCell(DGVN_ACTIVATE, hit, POINT{});
			}
			return 0;
		default:
			bHandled = FALSE;
			return 0;
	}
	int colCount = (int)d.layout.cols.size();
	int64_t rowCount = d.RowCount();
	if (rowCount == 0 || colCount == 0)
		return 0;
	DgPos pos;
	int64_t caretView = d.rows.ToView(d.caretRow);
	int caretCol = d.layout.IndexOf(d.caretId);
	if (caretView < 0 || caretCol < 0) {   // no caret yet: it starts at the first cell
		pos = { std::max<int64_t>(0, caretView), std::max(0, caretCol) };
	}
	else {
		int64_t page = std::max<int64_t>(1, DgFullRows(d.layout, d.clientHeight) - 1);
		pos = DgNavigate({ caretView, caretCol }, key, ctrl, rowCount, colCount, page);
		if (key == DgKey::PageUp || key == DgKey::PageDown) {   // the view moves with the caret, as a list view's
			int64_t delta = pos.row - caretView;
			ScrollRows(d.topRow + delta);
		}
	}
	// past the groups' rows: the caret is on data rows only
	int direction = key == DgKey::Up || key == DgKey::PageUp || (key == DgKey::Home && ctrl) ? -1 : 1;
	pos.row = DgSkipGroupRows(pos.row, direction, rowCount, [&](int64_t r) { return d.rows.GroupOf(r) >= 0; });
	if (pos.row < 0)
		return 0;
	Select(shift ? SelOp::Extend : SelOp::Reset, pos.row, pos.col, true);
	return 0;
}

LRESULT CDataGridCtrl::OnFocus(UINT msg, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	// to the editor: still the grid's focus, as far as the colors go
	d.focused = msg == WM_SETFOCUS || (d.edit.active && (HWND)wParam == m_Editor.m_hWnd);
	if (d.focused && !d.HasCaret() && d.RowCount() > 0 && !d.layout.cols.empty()) {   // somewhere for the keys to start
		int64_t row = DgSkipGroupRows(d.topRow, 1, d.RowCount(), [&](int64_t r) { return d.rows.GroupOf(r) >= 0; });
		d.caretRow = row >= 0 ? d.rows.ToData(row) : -1;
		if (d.layout.IndexOf(d.caretId) < 0)
			d.caretId = d.layout.cols.front().id;
		ResetSelection();
	}
	Invalidate();   // the selection's color and the caret's frame change with the focus
	if (msg == WM_SETFOCUS)
		NotifyCaretAccessible();
	return 0;
}

LRESULT CDataGridCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}
