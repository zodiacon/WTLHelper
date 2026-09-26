#include "DockWindowList.h"
#include <algorithm>

namespace WTLDock {

std::wstring DockWindowList::TypeText(const DockPane& pane) {
	return pane.Kind() == PaneKind::Document ? L"Document" : L"Tool window";
}

std::wstring DockWindowList::StateText(const DockPane& pane) {
	static const wchar_t* const sides[] = { L"left", L"right", L"top", L"bottom" };
	auto group = pane.Group();
	switch (pane.State()) {
		case PaneState::Document:
			return L"Open";
		case PaneState::Docked:
			return group && group->Side() ? std::wstring(L"Docked ") + sides[(int)*group->Side()] : L"Docked";
		case PaneState::AutoHide:
			return group && group->Side() ? std::wstring(L"Auto-hidden ") + sides[(int)*group->Side()] : L"Auto-hidden";
		case PaneState::Floating:
			return L"Floating";
		default:
			return L"Hidden";
	}
}

void DockWindowList::Build(const DockLayout& layout, bool includeTools) {
	m_Rows.clear();
	for (auto& p : layout.Panes()) {
		if (!p->Group() || (p->Kind() == PaneKind::Tool && !includeTools))
			continue;
		m_Rows.push_back({ p.get(), p->Title, TypeText(*p), StateText(*p), p->Modified });
	}
}

void DockWindowList::Sort(Column column, bool ascending) {
	auto less = [](const std::wstring& a, const std::wstring& b) {
		// as the user reads it: case does not matter and Untitled2 comes before Untitled10
		return ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), (int)a.size(),
			b.c_str(), (int)b.size(), nullptr, nullptr, 0) == CSTR_LESS_THAN;
	};
	auto before = [&](const Row& a, const Row& b) {
		switch (column) {
			case Column::Name: return less(a.Name, b.Name);
			case Column::Type: return less(a.Type, b.Type);
			case Column::State: return less(a.State, b.State);
			case Column::Modified: return a.Modified && !b.Modified;
		}
		return false;
	};
	std::stable_sort(m_Rows.begin(), m_Rows.end(), [&](const Row& a, const Row& b) {
		return ascending ? before(a, b) : before(b, a);
		});
}

int DockWindowList::IndexOf(const DockPane* pane) const {
	for (size_t i = 0; i < m_Rows.size(); i++)
		if (m_Rows[i].Pane == pane)
			return (int)i;
	return -1;
}

}
