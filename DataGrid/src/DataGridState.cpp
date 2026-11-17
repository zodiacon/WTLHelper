#include "DataGridData.h"

#include <algorithm>

// Saving and restoring the state (DgState), conditional formats (DgFormatRule) and the new row's placeholder.

static_assert(DgCondOp::Between == DGVCO_BETWEEN && DgCondOp::NotEmpty == DGVCO_NOTEMPTY && DgNoColor == CLR_DEFAULT);
static_assert(DgFindFlag::WholeCell == DGVFF_WHOLECELL && DgFindFlag::NoWrap == DGVFF_NOWRAP && DgRowMap::NewRow == DGV_NEWROW);

// ---- The state ----------------------------------------------------------------------------------------------

int CDataGridCtrl::GetState(LPWSTR text, int cch) const {
	auto& d = *m_Data;
	DgState state;
	for (int id : d.columns.Order())
		if (auto c = d.columns.Find(id))
			state.columns.push_back({ id, c->width, c->fill, c->Hidden() });
	state.frozen = d.frozen;
	state.sortKeys = d.sortKeys;
	state.groupIds = d.groupIds;
	state.filters = d.filters;
	auto s = DgSaveState(state);
	if (text && cch > 0) {
		size_t n = std::min(s.size(), (size_t)cch - 1);
		s.copy(text, n);
		text[n] = 0;
	}
	return (int)s.size() + 1;
}

BOOL CDataGridCtrl::SetState(LPCWSTR text) {
	auto& d = *m_Data;
	DgState state;
	if (!text || !DgLoadState(text, state) || !CommitOrCancel())
		return FALSE;
	// the columns it names, as it has them, then the others as they are
	std::vector<int> order;
	for (auto& c : state.columns)
		if (d.columns.Find(c.id) && std::find(order.begin(), order.end(), c.id) == order.end())
			order.push_back(c.id);
	for (int id : d.columns.Order())
		if (std::find(order.begin(), order.end(), id) == order.end())
			order.push_back(id);
	d.columns.SetOrder(order);
	for (auto& c : state.columns) {
		auto column = d.columns.Find(c.id);
		if (!column)
			continue;
		column->width = c.width;
		column->fill = c.fill;
		if (c.hidden)
			column->flags |= DgColFlag::Hidden;
		else
			column->flags &= ~DgColFlag::Hidden;
	}
	d.frozen = state.frozen;
	auto known = [&](int id) { return d.columns.Find(id) != nullptr; };
	d.sortKeys.clear();
	for (auto& k : state.sortKeys)
		if (known(k.id))
			d.sortKeys.push_back(k);
	d.filters.clear();
	for (auto& f : state.filters)
		if (known(f.id) && std::none_of(d.filters.begin(), d.filters.end(), [&](auto& x) { return x.id == f.id; }))
			d.filters.push_back(f);
	d.groupIds.clear();
	if (!d.virtualMode)
		for (int id : state.groupIds)
			if (known(id) && std::find(d.groupIds.begin(), d.groupIds.end(), id) == d.groupIds.end())
				d.groupIds.push_back(id);
	CloseFilterEdit(false);
	if (auto caret = d.columns.Find(d.caretId); !caret || caret->Hidden()) {
		auto visible = d.columns.Visible();
		d.caretId = visible.empty() ? 0 : visible.front()->id;
	}
	d.ensureCaret = false;
	d.topRow = 0;
	ApplyOrder();   // the rows again, and the layout
	return TRUE;
}

// ---- Conditional formats ---------------------------------------------------------------------------------------

int CDataGridCtrl::AddFormatRule(const DGVFORMATRULE& rule, int index) {
	auto& d = *m_Data;
	if (!d.columns.Find(rule.idColumn))
		return 0;
	DgFormatRule r;
	r.id = d.nextRuleId++;
	r.column = rule.idColumn;
	r.op = std::clamp(rule.op, (int)DGVCO_EQUAL, (int)DGVCO_NOTEMPTY);
	r.a = DgFromPublic(rule.value1);
	r.b = DgFromPublic(rule.value2);
	r.wholeRow = (rule.flags & DGVFR_WHOLEROW) != 0;
	r.bold = (rule.flags & DGVFR_BOLD) != 0;
	r.text = rule.crText;
	r.back = rule.crBack;
	int id = r.id;
	if (index < 0 || index >= (int)d.rules.size())
		d.rules.push_back(std::move(r));
	else
		d.rules.insert(d.rules.begin() + index, std::move(r));
	if (m_hWnd)
		Invalidate();
	return id;
}

BOOL CDataGridCtrl::DeleteFormatRule(int id) {
	auto& d = *m_Data;
	if (id == 0)
		d.rules.clear();
	else if (!std::erase_if(d.rules, [&](auto& r) { return r.id == id; }))
		return FALSE;
	if (m_hWnd)
		Invalidate();
	return TRUE;
}

int CDataGridCtrl::GetFormatRules(int* ids, int count) const {
	auto& rules = m_Data->rules;
	for (int i = 0; ids && i < count && i < (int)rules.size(); i++)
		ids[i] = rules[i].id;
	return (int)rules.size();
}

// ---- The new row ------------------------------------------------------------------------------------------------

bool CDataGridCtrl::CanEditCell(LONGLONG row, int id) const {
	auto& d = *m_Data;
	if (row != DgRowMap::NewRow)
		return CanEdit(row, id);
	auto column = d.columns.Find(id);
	// check boxes need a row to toggle: a value typed elsewhere makes it
	return m_hWnd && d.rows.HasNewRow() && column && !(column->flags & DgColFlag::ReadOnly) && column->type != DgColType::Bool;
}

LONGLONG CDataGridCtrl::AddNewRow(int id) {
	auto& d = *m_Data;
	NMDGVROWADD nm{};
	nm.iRow = d.DataRowCount();
	nm.idColumn = id;
	if (FireNotify(&nm.hdr, DGVN_ROWADDING))
		return -1;
	// at the end: no row's index moves, so the undo stack stays
	int64_t row = d.virtualMode ? d.virtualCount++ : d.store.Insert(-1, 1);
	d.caretRow = row;
	d.ensureCaret = true;   // wherever it's sorted, once the rows are shown
	ApplyOrder();
	nm.iRow = row;
	FireNotify(&nm.hdr, DGVN_ROWADDED);
	return row;
}
