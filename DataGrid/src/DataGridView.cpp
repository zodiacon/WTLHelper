#include "DataGridData.h"

#include <algorithm>
#include <thread>

// The rows in view: the store's rows filtered, sorted and grouped (DgBuildView) from a snapshot of the columns that
// matter, inline or on a worker thread past BackgroundRows; the groups' rows opened and closed (DgFlatten); the
// filters, the filter row's edit box and the titles' menu.

namespace {
	// the user's order: case ignored, digits as numbers ("file2" before "file10"); safe on the worker thread
	int UserCompare(const std::wstring& a, const std::wstring& b) {
		int r = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), (int)a.size(),
			b.c_str(), (int)b.size(), nullptr, nullptr, 0);
		return r ? r - CSTR_EQUAL : DgOrdinalCompare(a, b);
	}

	std::wstring WindowText(HWND hwnd) {
		std::wstring text(::GetWindowTextLengthW(hwnd), L'\0');
		::GetWindowTextW(hwnd, text.data(), (int)text.size() + 1);
		return text;
	}

	enum : UINT {
		MenuSortAscending = 1, MenuSortDescending, MenuClearSort, MenuGroup, MenuUngroup, MenuUngroupAll, MenuClearFilter,
		MenuClearAllFilters, MenuFilterRow, MenuAllValues = 20, MenuBlanks, MenuValue = 100,
	};
}

// ---- Rebuilding the rows in view ---------------------------------------------------------------------------

void CDataGridCtrl::ApplyOrder() {
	auto& d = *m_Data;
	if (d.job) {   // a newer one replaces it
		d.job->cancel = true;
		d.job.reset();
	}
	d.generation++;
	int64_t count = d.DataRowCount();
	if (d.virtualMode || (d.filters.empty() && d.sortKeys.empty() && d.groupIds.empty())) {
		DgViewResult all;
		all.identity = true;
		if (d.virtualMode)
			d.cache.Clear();
		ShowView(std::move(all));
		return;
	}
	auto job = std::make_shared<DgData::Job>();
	job->id = d.generation;
	auto& in = job->input;
	in.count = count;
	in.sortKeys = d.sortKeys;
	in.filters = d.filters;
	in.groupIds = d.groupIds;
	in.nf = d.numberFormat;
	in.compare = UserCompare;
	in.text = DgValueText;
	// a snapshot of the columns it needs, so the worker reads nothing the UI thread changes
	std::vector<int> ids;
	for (auto& k : d.sortKeys)
		ids.push_back(k.id);
	for (auto& f : d.filters)
		ids.push_back(f.id);
	for (int id : d.groupIds)
		ids.push_back(id);
	if (!d.groupIds.empty())
		for (int id : d.columns.Order())
			if (auto c = d.columns.Find(id); c && c->aggregate != DgAggKind::None)
				ids.push_back(id);
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	for (int id : ids) {
		auto column = d.columns.Find(id);
		if (!column)
			continue;
		in.columns.push_back(*column);
		auto& values = in.values.emplace_back();
		values.reserve((size_t)count);
		for (int64_t row = 0; row < count; row++) {
			auto cell = d.store.Get(row, column->slot);
			values.push_back(cell ? cell->value : DgValue{});
		}
	}
	if (count < DgData::BackgroundRows) {
		ShowView(DgBuildView(in));
		return;
	}
	d.job = job;
	HWND hwnd = m_hWnd;
	std::thread([job, hwnd] {
		job->result = DgBuildView(job->input, &job->cancel);
		job->input = DgViewInput();   // the snapshot's memory back now
		if (!job->cancel)
			::PostMessage(hwnd, WM_DGV_VIEWDONE, (WPARAM)job->id, 0);
	}).detach();
	Invalidate();   // the "Working" note; the last rows stay meanwhile
}

LRESULT CDataGridCtrl::OnViewDone(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (!d.job || (WPARAM)d.job->id != wParam)
		return 0;   // an older one's
	auto view = std::move(d.job->result);
	d.job.reset();
	ShowView(std::move(view));
	NMHDR nm{};
	FireNotify(&nm, DGVN_VIEWREADY);
	return 0;
}

void CDataGridCtrl::ShowView(DgViewResult view) {
	auto& d = *m_Data;
	d.view = std::move(view);
	Reflatten(0);
	if (d.ensureCaret) {
		d.ensureCaret = false;
		if (d.caretRow >= 0)
			EnsureVisible(d.caretRow, d.caretId);
	}
}

void CDataGridCtrl::Reflatten(LONGLONG from) {
	auto& d = *m_Data;
	int64_t count = d.DataRowCount();
	if (d.view.identity)
		d.rows.Reset(count);
	else
		d.rows.SetOrder(DgFlatten(d.view, d.collapsed), count);
	// the caret's row filtered out or in a closed group: the first row shown from "from"
	if (d.caretRow >= 0 && d.rows.ToView(d.caretRow) < 0) {
		int64_t row = DgSkipGroupRows(from, 1, d.RowCount(), [&](int64_t r) { return d.rows.GroupOf(r) >= 0; });
		d.caretRow = row >= 0 ? d.rows.ToData(row) : -1;
	}
	ResetSelection();
	Relayout();
}

bool CDataGridCtrl::IsBusy() const {
	return m_Data->job != nullptr;
}

LONGLONG CDataGridCtrl::GetViewRowCount() const {
	return m_Data->RowCount();
}

LONGLONG CDataGridCtrl::GetFilteredRowCount() const {
	auto& d = *m_Data;
	return d.view.identity ? d.DataRowCount() : (LONGLONG)d.view.order.size();
}

// ---- Groups -------------------------------------------------------------------------------------------------

bool CDataGridCtrl::ChangeGroups(std::vector<int> ids, bool user) {
	auto& d = *m_Data;
	if (d.virtualMode || !CommitOrCancel())
		return false;
	for (int id : ids)
		if (!d.columns.Find(id))
			return false;
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	if (ids == d.groupIds)
		return true;
	d.groupIds = std::move(ids);
	d.ensureCaret = false;   // the new groups from the top
	d.topRow = 0;
	ApplyOrder();
	if (user) {
		NMHDR nm{};
		FireNotify(&nm, DGVN_GROUPCHANGED);
	}
	return true;
}

BOOL CDataGridCtrl::SetGroupColumns(const int* ids, int count) {
	return ChangeGroups(ids && count > 0 ? std::vector<int>(ids, ids + count) : std::vector<int>(), false);
}

int CDataGridCtrl::GetGroupColumns(int* ids, int count) const {
	auto& all = m_Data->groupIds;
	for (int i = 0; ids && i < count && i < (int)all.size(); i++)
		ids[i] = all[i];
	return (int)all.size();
}

bool CDataGridCtrl::IsGroupRow(LONGLONG viewRow) const {
	return m_Data->rows.GroupOf(viewRow) >= 0;
}

void CDataGridCtrl::ToggleGroup(LONGLONG viewRow, bool user) {
	auto& d = *m_Data;
	int64_t g = d.rows.GroupOf(viewRow);
	if (g < 0 || g >= (int64_t)d.view.groups.size())
		return;
	auto& group = d.view.groups[(size_t)g];
	bool expand = d.collapsed.count(group.key) != 0;
	if (expand)
		d.collapsed.erase(group.key);
	else
		d.collapsed.insert(group.key);
	Reflatten(viewRow);   // a hidden caret: the first row after the group
	if (user) {
		NMDGVGROUP nm{};
		nm.iViewRow = viewRow;
		nm.idColumn = group.id;
		nm.fExpanded = expand;
		FireNotify(&nm.hdr, DGVN_GROUPEXPANDED);
	}
}

BOOL CDataGridCtrl::ExpandGroup(LONGLONG viewRow, bool expand) {
	auto& d = *m_Data;
	int64_t g = d.rows.GroupOf(viewRow);
	if (g < 0 || g >= (int64_t)d.view.groups.size())
		return FALSE;
	bool expanded = !d.collapsed.count(d.view.groups[(size_t)g].key);
	if (expanded != expand)
		ToggleGroup(viewRow, false);
	return TRUE;
}

void CDataGridCtrl::ExpandAllGroups(bool expand) {
	auto& d = *m_Data;
	if (expand)
		d.collapsed.clear();
	else
		for (auto& g : d.view.groups)
			d.collapsed.insert(g.key);
	Reflatten();
}

// ---- Filters ----------------------------------------------------------------------------------------------

bool CDataGridCtrl::ChangeFilter(int id, const DgFilter* filter, bool user) {
	auto& d = *m_Data;
	if (!CommitOrCancel())
		return false;
	// the app sees it as the API's
	DGVFILTER pub{};
	std::vector<LPCWSTR> values;
	std::wstring minText = filter ? filter->min.s : L"", maxText = filter ? filter->max.s : L"";
	if (filter) {
		pub.idColumn = filter->id;
		pub.kind = filter->kind;
		pub.pszText = filter->text.c_str();
		pub.minValue = DgToPublic(filter->min, minText.c_str());
		pub.maxValue = DgToPublic(filter->max, maxText.c_str());
		for (auto& v : filter->values)
			values.push_back(v.c_str());
		pub.ppszValues = values.data();
		pub.cValues = (int)values.size();
		pub.fBlanks = filter->blanks;
	}
	NMDGVFILTER nm{};
	nm.idColumn = id;
	nm.pFilter = filter ? &pub : nullptr;
	if (user && FireNotify(&nm.hdr, DGVN_FILTERCHANGING))
		return false;
	if (filter || id != 0)
		std::erase_if(d.filters, [&](auto& f) { return f.id == id; });
	else
		d.filters.clear();
	if (filter)
		d.filters.push_back(*filter);
	if (d.virtualMode)
		Invalidate();   // shown only: the app filters
	else {
		d.topRow = 0;   // what passes, from the top
		ApplyOrder();
	}
	if (user)
		FireNotify(&nm.hdr, DGVN_FILTERCHANGED);
	return true;
}

BOOL CDataGridCtrl::SetFilter(const DGVFILTER& filter) {
	if (!m_Data->columns.Find(filter.idColumn))
		return FALSE;
	DgFilter f;
	f.id = filter.idColumn;
	f.kind = std::clamp(filter.kind, (int)DGVFK_CONTAINS, (int)DGVFK_VALUES);
	f.text = filter.pszText ? filter.pszText : L"";
	f.min = DgFromPublic(filter.minValue);
	f.max = DgFromPublic(filter.maxValue);
	for (int i = 0; filter.ppszValues && i < filter.cValues; i++)
		if (filter.ppszValues[i])
			f.values.push_back(filter.ppszValues[i]);
	std::sort(f.values.begin(), f.values.end());
	f.values.erase(std::unique(f.values.begin(), f.values.end()), f.values.end());
	f.blanks = filter.fBlanks != FALSE;
	return ChangeFilter(f.id, &f, false);
}

BOOL CDataGridCtrl::ClearFilter(int id) {
	if (!HasFilter(id))
		return TRUE;
	return ChangeFilter(id, nullptr, false);
}

bool CDataGridCtrl::HasFilter(int id) const {
	auto& filters = m_Data->filters;
	return id == 0 ? !filters.empty() : std::any_of(filters.begin(), filters.end(), [&](auto& f) { return f.id == id; });
}

std::wstring CDataGridCtrl::FilterSummary(int id) const {
	auto& d = *m_Data;
	auto f = std::find_if(d.filters.begin(), d.filters.end(), [&](auto& x) { return x.id == id; });
	auto column = d.columns.Find(id);
	if (f == d.filters.end() || !column)
		return {};
	switch (f->kind) {
		case DgFilterKind::Contains:
			return f->text;
		case DgFilterKind::Range: {
			auto end = [&](const DgValue& v) { return v.type == DgType::Empty ? std::wstring() : DgValueText(v, *column, d.numberFormat); };
			return end(f->min) + L" \x2013 " + end(f->max);
		}
		case DgFilterKind::Values:
			if (f->values.size() == 1 && !f->blanks)
				return f->values[0];
			if (f->values.empty())
				return f->blanks ? L"(Blanks)" : L"(Nothing)";
			return std::to_wstring(f->values.size()) + L" values" + (f->blanks ? L" and blanks" : L"");
	}
	return {};
}

// ---- The filter row's edit box ------------------------------------------------------------------------------

bool CDataGridCtrl::FilterRect(int displayCol, RECT& rc) const {
	auto& d = *m_Data;
	auto& L = d.layout;
	if (displayCol < 0 || displayCol >= (int)L.cols.size() || L.header <= L.titles)
		return false;
	RECT cell{ L.Left(displayCol, d.scrollX), L.titles, L.Right(displayCol, d.scrollX), L.header };
	RECT area{ displayCol < L.frozenCount ? L.rowHeader : L.ScrollLeft(), L.titles,
		displayCol < L.frozenCount ? L.ScrollLeft() : d.clientWidth, L.header };
	return ::IntersectRect(&rc, &cell, &area) != FALSE;
}

void CDataGridCtrl::OpenFilterEdit(int displayCol) {
	auto& d = *m_Data;
	if (!(GetStyle() & DGVS_FILTERROW) || !CommitOrCancel())
		return;
	CloseFilterEdit(true);
	if (displayCol < 0 || displayCol >= d.ColCount())
		return;
	int id = d.layout.cols[displayCol].id;
	ScrollX(DgScrollXToShow(d.layout, d.scrollX, d.clientWidth, displayCol));
	RECT rc;
	if (!FilterRect(displayCol, rc))
		return;
	auto f = std::find_if(d.filters.begin(), d.filters.end(), [&](auto& x) { return x.id == id; });
	std::wstring text = f != d.filters.end() && f->kind == DgFilterKind::Contains ? f->text : L"";
	HWND edit = ::CreateWindowExW(0, WC_EDITW, text.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, rc.left + 1, rc.top + 1,
		rc.right - rc.left - 2, rc.bottom - rc.top - 2, m_hWnd, nullptr, nullptr, nullptr);
	::SendMessage(edit, WM_SETFONT, (WPARAM)m_hFont, 0);
	int margin = Scale(6) - 1;
	::SendMessage(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(margin, margin));
	::SendMessage(edit, EM_SETSEL, 0, -1);
	d.filterEditId = id;
	m_FilterEdit.SubclassWindow(edit);
	::SetFocus(edit);
}

void CDataGridCtrl::CloseFilterEdit(bool apply) {
	auto& d = *m_Data;
	if (!m_FilterEdit.m_hWnd)
		return;
	KillTimer(DgData::FilterTimer);
	std::wstring text = WindowText(m_FilterEdit.m_hWnd);
	int id = d.filterEditId;
	d.filterEditId = 0;
	HWND edit = m_FilterEdit.UnsubclassWindow();
	if (::GetFocus() == edit)
		SetFocus();
	::DestroyWindow(edit);
	if (apply)
		ApplyFilterText(id, text);
	Invalidate();
}

void CDataGridCtrl::ApplyFilterText(int id, const std::wstring& text) {
	auto& d = *m_Data;
	auto f = std::find_if(d.filters.begin(), d.filters.end(), [&](auto& x) { return x.id == id; });
	bool has = f != d.filters.end();
	if (has && f->kind == DgFilterKind::Contains && f->text == text)
		return;   // as it is
	if (text.empty()) {
		if (has && f->kind == DgFilterKind::Contains)
			ChangeFilter(id, nullptr, true);
		return;
	}
	DgFilter filter;
	filter.id = id;
	filter.kind = DgFilterKind::Contains;
	filter.text = text;
	ChangeFilter(id, &filter, true);
}

void CDataGridCtrl::PositionFilterEdit() {
	auto& d = *m_Data;
	if (!m_FilterEdit.m_hWnd)
		return;
	RECT rc;
	if (!FilterRect(d.layout.IndexOf(d.filterEditId), rc)) {   // gone from view: closed once this is over
		m_FilterEdit.ShowWindow(SW_HIDE);
		PostMessage(WM_DGV_ENDFILTER, 1);
		return;
	}
	m_FilterEdit.SetWindowPos(nullptr, rc.left + 1, rc.top + 1, rc.right - rc.left - 2, rc.bottom - rc.top - 2, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CDataGridCtrl::OnFilterKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	switch (wParam) {
		case VK_RETURN:
		case VK_ESCAPE:
		case VK_DOWN:
			CloseFilterEdit(true);
			return 0;
		case VK_TAB: {   // the next column's filter
			int col = d.layout.IndexOf(d.filterEditId) + (::GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
			CloseFilterEdit(true);
			if (col >= 0 && col < d.ColCount())
				OpenFilterEdit(col);
			return 0;
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CDataGridCtrl::OnFilterGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTALLKEYS | m_FilterEdit.DefWindowProc();
}

LRESULT CDataGridCtrl::OnFilterKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	bHandled = FALSE;
	PostMessage(WM_DGV_ENDFILTER);
	return 0;
}

LRESULT CDataGridCtrl::OnEndFilterPosted(UINT, WPARAM force, LPARAM, BOOL&) {
	if (m_FilterEdit.m_hWnd && (force || ::GetFocus() != m_FilterEdit.m_hWnd))
		CloseFilterEdit(true);
	return 0;
}

// ---- The titles' menu ---------------------------------------------------------------------------------------

void CDataGridCtrl::ShowHeaderMenu(int displayCol) {
	auto& d = *m_Data;
	if (displayCol < 0 || displayCol >= d.ColCount() || !CommitOrCancel())
		return;
	int id = d.layout.cols[displayCol].id;
	auto column = d.columns.Find(id);
	if (!column)
		return;
	DWORD style = GetStyle();
	bool canSort = !(style & DGVS_NOSORT) && !(column->flags & DgColFlag::NoSort);
	bool alone = d.sortKeys.size() == 1 && d.sortKeys[0].id == id;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | (canSort ? 0 : MF_GRAYED) | (alone && !d.sortKeys[0].descending ? MF_CHECKED : 0), MenuSortAscending, L"Sort &Ascending");
	menu.AppendMenu(MF_STRING | (canSort ? 0 : MF_GRAYED) | (alone && d.sortKeys[0].descending ? MF_CHECKED : 0), MenuSortDescending, L"Sort &Descending");
	menu.AppendMenu(MF_STRING | (d.sortKeys.empty() ? MF_GRAYED : 0), MenuClearSort, L"&Unsorted");
	if (!d.virtualMode) {
		menu.AppendMenu(MF_SEPARATOR);
		bool grouped = std::find(d.groupIds.begin(), d.groupIds.end(), id) != d.groupIds.end();
		if (grouped)
			menu.AppendMenu(MF_STRING, MenuUngroup, L"Stop &Grouping by This Column");
		else
			menu.AppendMenu(MF_STRING, MenuGroup, d.groupIds.empty() ? L"&Group by This Column" : L"Also &Group by This Column");
		if (!d.groupIds.empty())
			menu.AppendMenu(MF_STRING, MenuUngroupAll, L"Stop Grouping");
	}
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | (HasFilter(id) ? 0 : MF_GRAYED), MenuClearFilter, L"Clear &Filter");
	menu.AppendMenu(MF_STRING | (HasFilter(0) ? 0 : MF_GRAYED), MenuClearAllFilters, L"Clear All Filters");
	if (style & DGVS_FILTERROW)
		menu.AppendMenu(MF_STRING, MenuFilterRow, L"&Type a Filter");

	// the column's values, to pick which show (the store's: virtual mode has none to go through)
	std::vector<std::wstring> texts;
	bool blanks = false, more = false;
	auto current = std::find_if(d.filters.begin(), d.filters.end(), [&](auto& f) { return f.id == id && f.kind == DgFilterKind::Values; });
	bool filtered = current != d.filters.end();
	if (!d.virtualMode) {
		texts = DgDistinctTexts(d.store.Count(), [&](int64_t row) -> const DgValue* {
			auto cell = d.store.Get(row, column->slot);
			return cell ? &cell->value : nullptr;
		}, *column, d.numberFormat, DgValueText, 50, blanks, more);
		CMenu values;
		values.CreatePopupMenu();
		values.AppendMenu(MF_STRING | (filtered ? 0 : MF_CHECKED), MenuAllValues, L"(All)");
		values.AppendMenu(MF_SEPARATOR);
		if (blanks)
			values.AppendMenu(MF_STRING | (!filtered || current->blanks ? MF_CHECKED : 0), MenuBlanks, L"(Blanks)");
		for (size_t i = 0; i < texts.size(); i++) {
			bool on = !filtered || std::binary_search(current->values.begin(), current->values.end(), texts[i]);
			std::wstring item = texts[i].empty() ? L" " : texts[i];
			std::replace(item.begin(), item.end(), L'&', L'+');   // no mnemonics
			values.AppendMenu(MF_STRING | (on ? MF_CHECKED : 0), MenuValue + (UINT)i, item.c_str());
		}
		if (more) {
			values.AppendMenu(MF_SEPARATOR);
			values.AppendMenu(MF_STRING | MF_GRAYED, (UINT)0, L"More values: a click shows only that one");
		}
		menu.AppendMenu(MF_POPUP, values.Detach(), L"Show &Values");
	}

	POINT pt{ d.layout.Right(displayCol, d.scrollX) - Scale(18), d.layout.titles };
	ClientToScreen(&pt);
	UINT cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	switch (cmd) {
		case MenuSortAscending:
		case MenuSortDescending:
			Sort({ { id, cmd == MenuSortDescending } }, true);
			break;
		case MenuClearSort:
			Sort({}, true);
			break;
		case MenuGroup: {
			auto ids = d.groupIds;
			ids.push_back(id);
			ChangeGroups(std::move(ids), true);
			break;
		}
		case MenuUngroup: {
			auto ids = d.groupIds;
			std::erase(ids, id);
			ChangeGroups(std::move(ids), true);
			break;
		}
		case MenuUngroupAll:
			ChangeGroups({}, true);
			break;
		case MenuClearFilter:
		case MenuAllValues:
			if (HasFilter(id))
				ChangeFilter(id, nullptr, true);
			break;
		case MenuClearAllFilters:
			ChangeFilter(0, nullptr, true);
			break;
		case MenuFilterRow:
			OpenFilterEdit(displayCol);
			break;
		default: {
			if (cmd != MenuBlanks && (cmd < MenuValue || cmd >= MenuValue + texts.size()))
				break;
			DgFilter f;
			f.id = id;
			f.kind = DgFilterKind::Values;
			bool isBlanks = cmd == MenuBlanks;
			if (more) {   // not every value is known: only the one picked
				if (!isBlanks)
					f.values = { texts[cmd - MenuValue] };
				f.blanks = isBlanks;
			}
			else {        // the one picked turns, the rest stay as they are
				f.values = filtered ? current->values : texts;
				std::sort(f.values.begin(), f.values.end());   // texts is in the values' order: searched below
				f.blanks = filtered ? current->blanks : blanks;
				if (isBlanks) {
					f.blanks = !f.blanks;
				}
				else {
					auto& text = texts[cmd - MenuValue];
					auto at = std::lower_bound(f.values.begin(), f.values.end(), text);
					if (at != f.values.end() && *at == text)
						f.values.erase(at);
					else
						f.values.insert(at, text);
				}
				std::sort(f.values.begin(), f.values.end());
				auto all = texts;
				std::sort(all.begin(), all.end());
				if (f.values == all && f.blanks == blanks) {   // everything again: no filter
					if (filtered)
						ChangeFilter(id, nullptr, true);
					break;
				}
			}
			std::sort(f.values.begin(), f.values.end());
			ChangeFilter(id, &f, true);
			break;
		}
	}
}
