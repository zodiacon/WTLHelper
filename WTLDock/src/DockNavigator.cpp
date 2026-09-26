#include "DockNavigator.h"
#include <algorithm>

namespace WTLDock {

void DockNavigator::Build(const DockLayout& layout, const std::vector<std::wstring>& mru) {
	m_Documents.clear();
	m_Tools.clear();
	std::vector<DockPane*> ordered;
	auto add = [&](DockPane* pane) {
		if (pane && pane->Group() && std::find(ordered.begin(), ordered.end(), pane) == ordered.end())
			ordered.push_back(pane);
	};
	for (auto& id : mru)
		add(layout.FindPane(id));
	for (auto& p : layout.Panes())
		add(p.get());

	for (auto pane : ordered)
		(pane->Kind() == PaneKind::Document ? m_Documents : m_Tools).push_back(pane);
	m_Column = m_Documents.empty() ? Column::Tools : Column::Documents;
	m_Row = 0;
}

void DockNavigator::Start(const DockPane* active, bool forward) {
	m_Column = m_Documents.empty() ? Column::Tools : Column::Documents;
	const auto& items = Items(m_Column);
	if (items.empty()) {
		m_Row = 0;
		return;
	}
	const int n = (int)items.size();
	const bool activeIsFirst = active && items[0] == active;
	if (forward)
		m_Row = activeIsFirst && n > 1 ? 1 : 0;
	else
		m_Row = n - 1;
}

DockPane* DockNavigator::Selected() const {
	const auto& items = Items(m_Column);
	return m_Row >= 0 && m_Row < (int)items.size() ? items[m_Row] : nullptr;
}

void DockNavigator::MoveRow(int delta) {
	const int n = (int)Items(m_Column).size();
	if (n == 0)
		return;
	m_Row = ((m_Row + delta) % n + n) % n;
}

void DockNavigator::MoveColumn() {
	const Column other = m_Column == Column::Documents ? Column::Tools : Column::Documents;
	const int n = (int)Items(other).size();
	if (n == 0)
		return;
	m_Column = other;
	m_Row = std::min(m_Row, n - 1);
}

bool DockNavigator::Select(Column column, int row) {
	if (row < 0 || row >= (int)Items(column).size())
		return false;
	m_Column = column;
	m_Row = row;
	return true;
}

bool DockNavigator::Select(const DockPane* pane) {
	for (auto column : { Column::Documents, Column::Tools }) {
		const auto& items = Items(column);
		auto it = std::find(items.begin(), items.end(), pane);
		if (it != items.end())
			return Select(column, (int)(it - items.begin()));
	}
	return false;
}

}
