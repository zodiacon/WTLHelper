#pragma once

#include "DockLayout.h"
#include <string>
#include <vector>

namespace WTLDock {

//
// The state of the window switcher (Ctrl+Tab): two lists, the documents and the tool windows, each with the most
// recently used first, and a selection in one of them. Pure data; the window that shows it is the host's business.
//
class DockNavigator {
public:
	enum class Column { Documents, Tools };

	// Fills the lists with the panes that are placed (docked, floating, auto-hidden, in a document group). 'mru' holds
	// pane ids, most recent first; panes it does not mention follow in the order of the layout.
	void Build(const DockLayout& layout, const std::vector<std::wstring>& mru);

	// Puts the selection where Ctrl+Tab starts: on the document used before the active one (or, going backwards, on
	// the least recently used). Without documents it is a tool window. 'active' may be null.
	void Start(const DockPane* active, bool forward);

	bool Empty() const {
		return m_Documents.empty() && m_Tools.empty();
	}
	const std::vector<DockPane*>& Items(Column column) const {
		return column == Column::Documents ? m_Documents : m_Tools;
	}
	Column CurrentColumn() const {
		return m_Column;
	}
	int Row() const {
		return m_Row;
	}
	DockPane* Selected() const;

	// one row down (or up); wraps around
	void MoveRow(int delta);
	// to the other list (if it has anything), keeping the row as far as it goes
	void MoveColumn();
	bool Select(Column column, int row);
	bool Select(const DockPane* pane);

private:
	std::vector<DockPane*> m_Documents, m_Tools;
	Column m_Column{ Column::Documents };
	int m_Row{};
};

}
