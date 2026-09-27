#include "DockWindowList.h"
#include "DockStrings.h"
#include <algorithm>

namespace WTLDock {

std::wstring DockWindowList::TypeText(const DockPane& pane) {
	return DockText(pane.Kind() == PaneKind::Document ? Str::TypeDocument : Str::TypeToolWindow);
}

std::wstring DockWindowList::StateText(const DockPane& pane) {
	auto group = pane.Group();
	const bool sided = group && group->Side();
	switch (pane.State()) {
		case PaneState::Document:
			return DockText(pane.Pinned() ? Str::StateOpenPinned : pane.Preview ? Str::StateOpenPreview : Str::StateOpen);
		case PaneState::Docked:
			return sided ? DockText(Str::StateDockedAt, { DockSideText(*group->Side()) }) : DockText(Str::StateDocked);
		case PaneState::AutoHide:
			return sided ? DockText(Str::StateAutoHiddenAt, { DockSideText(*group->Side()) }) : DockText(Str::StateAutoHidden);
		case PaneState::Floating:
			return DockText(Str::StateFloating);
		default:
			return DockText(Str::StateHidden);
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
