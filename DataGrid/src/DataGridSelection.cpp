#include "DataGridData.h"

#include <algorithm>

// The selection (rectangles of rows in view and visible columns, in DgSelection), copying it, and sorting the store.

namespace {
	// the user's order: case ignored, digits as numbers ("file2" before "file10")
	int UserCompare(const std::wstring& a, const std::wstring& b) {
		int r = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), (int)a.size(),
			b.c_str(), (int)b.size(), nullptr, nullptr, 0);
		return r ? r - CSTR_EQUAL : DgOrdinalCompare(a, b);
	}

	bool SetClipboard(UINT format, const void* data, size_t bytes) {
		HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
		if (!h)
			return false;
		memcpy(::GlobalLock(h), data, bytes);
		::GlobalUnlock(h);
		if (::SetClipboardData(format, h))
			return true;
		::GlobalFree(h);
		return false;
	}

	constexpr int64_t MaxCopyCells = 1'000'000;
}

// ---- Changing the selection ------------------------------------------------------------------------

void CDataGridCtrl::Select(SelOp op, LONGLONG viewRow, int displayCol, bool user, bool wholeRows) {
	auto& d = *m_Data;
	int n = d.ColCount();
	if (n == 0 || d.RowCount() == 0)
		return;
	DgPos pos{ std::clamp<int64_t>(viewRow, 0, d.RowCount() - 1), std::clamp(displayCol, 0, n - 1) };
	auto before = d.selection.Ranges();
	switch (op) {
		case SelOp::Reset:  d.selection.Reset(pos, n, wholeRows); break;
		case SelOp::Extend: d.selection.Extend(pos, n, wholeRows); break;
		case SelOp::Add:    d.selection.Add(pos, n, wholeRows); break;
		case SelOp::Toggle: d.selection.Toggle(pos, n); break;
	}
	MoveCaret(pos.row, pos.col, user);
	EnsureVisible(d.rows.ToData(pos.row), d.layout.cols[pos.col].id);
	if (d.selection.Ranges() != before) {
		Invalidate();
		if (user)
			FireSelChanged();
	}
}

void CDataGridCtrl::ResetSelection() {
	auto& d = *m_Data;
	if (d.caretRow < 0 || d.RowCount() == 0 || d.ColCount() == 0)
		d.selection.Clear();
	else
		d.selection.Reset(d.CaretPos(), d.ColCount());
	if (m_hWnd)
		Invalidate();
}

void CDataGridCtrl::FireSelChanged() {
	NMHDR nm{};
	FireNotify(&nm, DGVN_SELCHANGED);
}

void CDataGridCtrl::SetSelectionMode(int mode) {
	auto& d = *m_Data;
	auto m = mode == DGVSM_ROWS ? DgSelMode::Rows : mode == DGVSM_SINGLE ? DgSelMode::Single : DgSelMode::Cells;
	d.selection.SetMode(m, d.CaretPos(), d.ColCount());
	ResetSelection();
}

int CDataGridCtrl::GetSelectionMode() const {
	switch (m_Data->selection.Mode()) {
		case DgSelMode::Rows:   return DGVSM_ROWS;
		case DgSelMode::Single: return DGVSM_SINGLE;
	}
	return DGVSM_CELLS;
}

void CDataGridCtrl::SelectAll() {
	auto& d = *m_Data;
	if (d.selection.Mode() == DgSelMode::Single)
		return;
	d.selection.SelectAll(d.RowCount(), d.ColCount());
	Invalidate();
}

void CDataGridCtrl::ClearSelection() {
	if (m_Data->selection.Mode() == DgSelMode::Rows) {
		m_Data->selection.Clear();
		Invalidate();
	}
	else {
		ResetSelection();
	}
}

BOOL CDataGridCtrl::SelectRange(const DGVRANGE& range, bool add) {
	auto& d = *m_Data;
	int64_t rows = d.RowCount();
	int n = d.ColCount();
	if (rows == 0 || n == 0 || range.iTopRow < 0 || range.iTopRow >= rows || range.iLeftColumn < 0 || range.iLeftColumn >= n)
		return FALSE;
	DgPos from{ range.iTopRow, range.iLeftColumn };
	DgPos to{ std::clamp<int64_t>(range.iBottomRow, range.iTopRow, rows - 1), std::clamp(range.iRightColumn, range.iLeftColumn, n - 1) };
	if (add)
		d.selection.Add(from, n);
	else
		d.selection.Reset(from, n);
	d.selection.Extend(to, n);
	Invalidate();
	return TRUE;
}

int CDataGridCtrl::GetSelectedRanges(DGVRANGE* ranges, int count) const {
	auto& all = m_Data->selection.Ranges();
	for (int i = 0; ranges && i < count && i < (int)all.size(); i++)
		ranges[i] = { all[i].top, all[i].bottom, all[i].left, all[i].right };
	return (int)all.size();
}

bool CDataGridCtrl::IsCellSelected(LONGLONG row, int id) const {
	auto& d = *m_Data;
	return d.selection.Contains(d.rows.ToView(row), d.layout.IndexOf(id));
}

LONGLONG CDataGridCtrl::GetSelectedRowCount() const {
	return m_Data->selection.RowCount();
}

LONGLONG CDataGridCtrl::GetSelectedRows(LONGLONG* rows, LONGLONG count) const {
	auto& d = *m_Data;
	LONGLONG written = 0;
	for (auto [first, last] : d.selection.Rows())
		for (int64_t r = first; r <= last && rows && written < count; r++)
			rows[written++] = d.rows.ToData(r);
	return d.selection.RowCount();
}

LONGLONG CDataGridCtrl::GetDataRow(LONGLONG viewRow) const {
	return m_Data->rows.ToData(viewRow);
}

LONGLONG CDataGridCtrl::GetViewRow(LONGLONG dataRow) const {
	return m_Data->rows.ToView(dataRow);
}

int CDataGridCtrl::GetVisibleColumn(int index) const {
	auto& cols = m_Data->layout.cols;
	return index >= 0 && index < (int)cols.size() ? cols[index].id : 0;
}

int CDataGridCtrl::GetVisibleColumnIndex(int id) const {
	return m_Data->layout.IndexOf(id);
}

// ---- Dragging a selection ---------------------------------------------------------------------------

void CDataGridCtrl::StartSelectDrag(bool rowHeader) {
	auto& d = *m_Data;
	if (d.selection.Mode() == DgSelMode::Single)
		return;
	d.drag = DgData::Drag::Select;
	d.selectRows = rowHeader;
	SetCapture();
	SetTimer(DgData::ScrollTimer, 50);
}

void CDataGridCtrl::SelectDragTo(POINT pt) {
	auto& d = *m_Data;
	auto pos = DgPosFromPoint(d.layout, d.View(), pt.x, pt.y);
	if (d.selectRows)
		pos.col = std::max(0, d.layout.IndexOf(d.caretId));
	if (pos.row == d.rows.ToView(d.caretRow) && pos.col == d.layout.IndexOf(d.caretId))
		return;
	Select(SelOp::Extend, pos.row, pos.col, true, d.selectRows);
}

void CDataGridCtrl::EndSelectDrag() {
	auto& d = *m_Data;
	KillTimer(DgData::ScrollTimer);
	d.drag = DgData::Drag::None;
	if (::GetCapture() == m_hWnd)
		ReleaseCapture();
}

LRESULT CDataGridCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (wParam == DgData::ErrorTimer) {
		HideError();
		return 0;
	}
	if (wParam != DgData::ScrollTimer) {
		bHandled = FALSE;
		return 0;
	}
	if (d.drag != DgData::Drag::Select) {
		KillTimer(DgData::ScrollTimer);
		return 0;
	}
	// past an edge: scroll toward it, faster the further the mouse is
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	int step = Scale(16);
	if (pt.y < d.layout.header)
		ScrollRows(d.topRow - 1 - (d.layout.header - pt.y) / step);
	else if (pt.y >= d.clientHeight)
		ScrollRows(d.topRow + 1 + (pt.y - d.clientHeight) / step);
	if (!d.selectRows) {
		if (pt.x < d.layout.ScrollLeft() && d.scrollX > 0 && pt.x < d.layout.rowHeader + (d.layout.frozenCount ? d.layout.frozenWidth : 0))
			ScrollX(d.scrollX - Scale(20));
		else if (pt.x >= d.clientWidth)
			ScrollX(d.scrollX + Scale(20));
	}
	SelectDragTo(pt);
	return 0;
}

// ---- Keys ------------------------------------------------------------------------------------------

bool CDataGridCtrl::SelectionKey(WPARAM key, bool ctrl, bool shift) {
	auto& d = *m_Data;
	auto caret = d.CaretPos();
	switch (key) {
		case 'A':
			if (!ctrl || shift)
				return false;
			if (d.selection.Mode() != DgSelMode::Single) {
				SelectAll();
				FireSelChanged();
			}
			return true;
		case 'C':
		case VK_INSERT:
			if (!ctrl)
				return false;
			Copy(shift ? DGVCOPY_TITLES : 0);
			return true;
		case VK_SPACE:
			if (d.caretRow < 0 || d.RowCount() == 0 || d.ColCount() == 0)
				return true;
			if (ctrl && shift) {
				SelectAll();
				FireSelChanged();
			}
			else if (shift) {     // the caret's row
				Select(SelOp::Reset, caret.row, caret.col, true, true);
			}
			else if (ctrl) {
				if (d.selection.Mode() == DgSelMode::Rows) {
					Select(SelOp::Toggle, caret.row, caret.col, true);
				}
				else if (d.selection.Mode() == DgSelMode::Cells) {   // the caret's column
					d.selection.Reset({ 0, caret.col }, d.ColCount());
					d.selection.Extend({ d.RowCount() - 1, caret.col }, d.ColCount());
					Invalidate();
					FireSelChanged();
				}
			}
			else {
				return false;
			}
			return true;
	}
	return false;
}

// ---- Copying ---------------------------------------------------------------------------------------

BOOL CDataGridCtrl::Copy(UINT flags) {
	auto& d = *m_Data;
	if (d.selection.Empty())
		return FALSE;
	auto cols = d.selection.Cols();
	if (d.selection.RowCount() > MaxCopyCells / std::max<int64_t>(1, (int64_t)cols.size())) {
		::MessageBeep(MB_ICONWARNING);
		return FALSE;
	}
	bool titles = (flags & DGVCOPY_TITLES) != 0;
	auto table = DgSelectionTable(d.selection,
		[&](int64_t r, int c) {
			int id = d.layout.cols[c].id;
			auto column = d.columns.Find(id);
			DgCell cell;
			if (!column || !Fetch(d.rows.ToData(r), id, cell))
				return std::wstring();
			return DgCellText(cell, *column, d.numberFormat);
		},
		[&](int c) {
			auto column = d.columns.Find(d.layout.cols[c].id);
			return column ? column->title : std::wstring();
		}, titles);
	auto tsv = DgBuildTsv(table);
	auto html = DgCfHtml(DgUtf8(DgBuildHtmlTable(table, titles)));
	if (!::OpenClipboard(m_hWnd))
		return FALSE;
	::EmptyClipboard();
	bool ok = SetClipboard(CF_UNICODETEXT, tsv.c_str(), (tsv.size() + 1) * sizeof(wchar_t));
	static const UINT htmlFormat = ::RegisterClipboardFormatW(L"HTML Format");
	SetClipboard(htmlFormat, html.c_str(), html.size() + 1);
	::CloseClipboard();
	return ok;
}

// ---- Sorting -----------------------------------------------------------------------------------------

void CDataGridCtrl::Sort(std::vector<DgSortKey> keys, bool user) {
	auto& d = *m_Data;
	if (!CommitOrCancel())
		return;
	std::vector<DGVSORTKEY> pub;
	for (auto& k : keys)
		pub.push_back({ k.id, k.descending });
	NMDGVSORT nm{};
	nm.cKeys = (int)pub.size();
	nm.pKeys = pub.data();
	if (user && FireNotify(&nm.hdr, DGVN_SORTCHANGING))
		return;
	d.sortKeys = std::move(keys);
	ApplyOrder();
	if (d.caretRow >= 0)
		EnsureVisible(d.caretRow, d.caretId);
	if (user)
		FireNotify(&nm.hdr, DGVN_SORTCHANGED);
}

void CDataGridCtrl::ApplyOrder() {
	auto& d = *m_Data;
	int64_t count = d.DataRowCount();
	if (d.virtualMode || d.sortKeys.empty()) {
		d.rows.Reset(count);
		d.cache.Clear();
	}
	else {
		std::vector<int> slots;
		std::vector<bool> descending;
		for (auto& k : d.sortKeys) {
			auto column = d.columns.Find(k.id);
			slots.push_back(column ? column->slot : -1);
			descending.push_back(k.descending);
		}
		auto order = DgSortOrder(count, descending,
			[&](int64_t row, size_t k) -> const DgValue* {
				auto cell = d.store.Get(row, slots[k]);
				return cell ? &cell->value : nullptr;
			}, UserCompare);
		d.rows.SetOrder(std::move(order), count);
	}
	ResetSelection();
	Relayout();
}

BOOL CDataGridCtrl::SetSortKeys(const DGVSORTKEY* keys, int count) {
	std::vector<DgSortKey> list;
	for (int i = 0; keys && i < count; i++) {
		if (!m_Data->columns.Find(keys[i].idColumn))
			return FALSE;
		list.push_back({ keys[i].idColumn, keys[i].fDescending != FALSE });
	}
	Sort(std::move(list), false);
	return TRUE;
}

int CDataGridCtrl::GetSortKeys(DGVSORTKEY* keys, int count) const {
	auto& all = m_Data->sortKeys;
	for (int i = 0; keys && i < count && i < (int)all.size(); i++)
		keys[i] = { all[i].id, all[i].descending };
	return (int)all.size();
}

void CDataGridCtrl::Resort() {
	ApplyOrder();
}
