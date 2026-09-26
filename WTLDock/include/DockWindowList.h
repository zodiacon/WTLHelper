#pragma once

#include "DockLayout.h"
#include <string>
#include <vector>

namespace WTLDock {

//
// The rows of the Windows dialog (Window > Windows...): the panes that are placed, with what the dialog shows about
// each and the sort orders it offers. Pure data.
//
class DockWindowList {
public:
	enum class Column { Name, Type, State, Modified };

	struct Row {
		DockPane* Pane{};
		std::wstring Name;		// the title
		std::wstring Type;		// "Document" or "Tool window"
		std::wstring State;		// "Open", "Docked left", "Auto-hidden bottom", "Floating"
		bool Modified{};
	};

	// Lists the placed documents and, if asked for, tool windows. In the order of the layout until sorted.
	void Build(const DockLayout& layout, bool includeTools);
	// Sorts by a column (case does not matter; modified ones first when ascending); equal rows keep their order.
	void Sort(Column column, bool ascending = true);

	const std::vector<Row>& Rows() const {
		return m_Rows;
	}
	int IndexOf(const DockPane* pane) const;

	static std::wstring StateText(const DockPane& pane);
	static std::wstring TypeText(const DockPane& pane);

private:
	std::vector<Row> m_Rows;
};

// The control ids of the Windows dialog, for applications that drive or restyle it.
namespace WindowsDialogIds {
	inline constexpr int List = 1001;
	inline constexpr int IncludeTools = 1002;
	inline constexpr int Activate = 1003;
	inline constexpr int Save = 1004;
	inline constexpr int CloseWindows = 1005;
	inline constexpr int Close = 2;		// IDCANCEL
}

}
