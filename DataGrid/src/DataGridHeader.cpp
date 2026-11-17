#include "DataGridData.h"

#include <algorithm>

// The header's mouse: resizing a column by its divider, clicking a title, dragging a column to another place. Fitting
// a column measures its title and the rows in view.

bool CDataGridCtrl::HeaderMouseDown(POINT pt, const DGVHITTEST& hit, int displayCol) {
	auto& d = *m_Data;
	DWORD style = GetStyle();
	if (displayCol < 0)
		return false;
	auto column = d.columns.Find(d.layout.cols[displayCol].id);
	if (!column)
		return false;
	if (hit.flags & DGVHT_HEADERDIVIDER) {
		if ((style & DGVS_NOCOLUMNRESIZE) || (column->flags & DgColFlag::NoResize))
			return false;
		d.drag = DgData::Drag::Resize;
		d.dragStartWidth = d.layout.cols[displayCol].width;
	}
	else if (hit.flags & DGVHT_HEADER) {
		d.drag = DgData::Drag::Press;
		d.dragOffset = pt.x - d.layout.Left(displayCol, d.scrollX);
	}
	else {
		return false;
	}
	d.dragCol = displayCol;
	d.dragStartX = d.dragX = pt.x;
	d.dropIndex = -1;
	SetCapture();
	Invalidate();
	return true;
}

void CDataGridCtrl::HeaderMouseMove(POINT pt) {
	auto& d = *m_Data;
	if (d.dragCol < 0 || d.dragCol >= (int)d.layout.cols.size())
		return;
	auto column = d.columns.Find(d.layout.cols[d.dragCol].id);
	if (!column)
		return;
	switch (d.drag) {
		case DgData::Drag::Resize: {
			int width = std::max(Scale(column->minWidth), d.dragStartWidth + (int)pt.x - d.dragStartX);
			if (width != d.layout.cols[d.dragCol].width) {
				column->width = Unscale(width);
				column->fill = 0;
				Relayout();
				UpdateWindow();
			}
			break;
		}
		case DgData::Drag::Press:
			if (std::abs(pt.x - d.dragStartX) < ::GetSystemMetrics(SM_CXDRAG) || (GetStyle() & DGVS_NOCOLUMNREORDER)
				|| (column->flags & DgColFlag::NoReorder))
				break;
			d.drag = DgData::Drag::Reorder;
			[[fallthrough]];
		case DgData::Drag::Reorder:
			d.dragX = pt.x;
			d.dropIndex = DgDropIndex(d.layout, d.scrollX, pt.x);
			// near an edge of the scrolling part: scroll toward it
			if (pt.x > d.clientWidth - Scale(16))
				ScrollX(d.scrollX + Scale(16));
			else if (pt.x < d.layout.ScrollLeft() + Scale(16) && pt.x >= d.layout.ScrollLeft())
				ScrollX(d.scrollX - Scale(16));
			Invalidate();
			break;
	}
}

void CDataGridCtrl::HeaderMouseUp(POINT pt) {
	auto& d = *m_Data;
	auto drag = d.drag;
	int col = d.dragCol;
	d.drag = DgData::Drag::None;
	d.dragCol = -1;
	if (::GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate();
	if (col < 0 || col >= (int)d.layout.cols.size())
		return;
	int id = d.layout.cols[col].id;
	NMDGVCOLUMN nm{};
	nm.idColumn = id;
	switch (drag) {
		case DgData::Drag::Resize:
			nm.iDisplayIndex = d.columns.DisplayIndexOf(id);
			nm.cxWidth = GetColumnWidth(id);
			FireNotify(&nm.hdr, DGVN_COLUMNRESIZED);
			break;
		case DgData::Drag::Press: {
			DGVHITTEST hit{ pt };
			int at;
			Hit(pt, hit, at);
			if ((hit.flags & DGVHT_HEADER) && at == col) {
				nm.iDisplayIndex = d.columns.DisplayIndexOf(id);
				nm.cxWidth = GetColumnWidth(id);
				// non-zero: the app handled it, no sorting
				bool handled = FireNotify(&nm.hdr, DGVN_COLUMNCLICK) != 0;
				auto column = d.columns.Find(id);
				if (!handled && column && !(GetStyle() & DGVS_NOSORT) && !(column->flags & DgColFlag::NoSort))
					Sort(DgClickSort(d.sortKeys, id, ::GetKeyState(VK_SHIFT) < 0), true);
			}
			break;
		}
		case DgData::Drag::Reorder: {
			int drop = d.dropIndex;
			d.dropIndex = -1;
			int n = (int)d.layout.cols.size();
			if (drop < 0 || drop == col || drop == col + 1)
				break;   // where it is
			int beforeId = drop < n ? d.layout.cols[drop].id : 0;
			nm.iOldIndex = d.columns.DisplayIndexOf(id);
			d.columns.Move(id, beforeId);
			Relayout();
			nm.iDisplayIndex = d.columns.DisplayIndexOf(id);
			nm.cxWidth = GetColumnWidth(id);
			FireNotify(&nm.hdr, DGVN_COLUMNMOVED);
			break;
		}
		default:
			break;
	}
}

int CDataGridCtrl::MeasureColumn(int displayIndex) {
	auto& d = *m_Data;
	auto column = d.columns.Find(d.layout.cols[displayIndex].id);
	if (!column)
		return 0;
	int pad = Scale(6);
	CClientDC dc(m_hWnd);
	HFONT old = dc.SelectFont(m_hBold);
	SIZE size{};
	dc.GetTextExtent(column->title.c_str(), (int)column->title.size(), &size);
	int width = size.cx + 2 * pad;
	dc.SelectFont(m_hFont);
	int64_t first = d.topRow;
	int64_t last = std::min<int64_t>(d.RowCount(), first + DgFullRows(d.layout, d.clientHeight) + 1);
	for (int64_t r = first; r < last; r++) {
		DgCell cell;
		if (!Fetch(d.rows.ToData(r), column->id, cell))
			continue;
		int cx = 0;
		switch (column->type) {
			case DgColType::Bool:
				cx = Scale(13);
				break;
			case DgColType::Progress:
				cx = Scale(60);
				break;
			default: {
				auto text = DgCellText(cell, *column, d.numberFormat);
				if (column->type == DgColType::Link)
					dc.SelectFont(m_hUnderline);
				dc.GetTextExtent(text.c_str(), (int)text.size(), &size);
				dc.SelectFont(m_hFont);
				cx = size.cx;
				if (column->type == DgColType::ImageText && d.images && cell.image >= 0) {
					int ix = 0, iy = 0;
					::ImageList_GetIconSize(d.images, &ix, &iy);
					cx += ix + Scale(4);
				}
				break;
			}
		}
		width = std::max(width, cx + 2 * pad);
	}
	dc.SelectFont(old);
	return std::max(width, Scale(column->minWidth));
}
