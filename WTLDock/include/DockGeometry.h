#pragma once

#include "DockNode.h"
#include "DockTheme.h"
#include <vector>

namespace WTLDock {

// Where the parts of a group window go, in the coordinates of the group window's client area.
// Pure geometry: shared by the painting code, the hit testing and the tests.
struct GroupParts {
	RECT Caption{};
	RECT Tabs{};
	RECT Content{};
	bool HasCaption{};
	bool HasTabs{};
};

// Tool groups have a caption; their tab strip (only with two or more panes) is at the bottom or below the caption.
// Document groups have no caption and always show their tab strip at the top. The only group of a floating
// window has no caption either: the window's title bar takes its place.
// 'tabRows': how many rows the tab strip has (more than one with multi-row tabs; see LayoutTabRows)
GroupParts ComputeGroupParts(const DockGroup& group, const RECT& client, const DockMetrics& metrics, int tabRows = 1);

// The buttons on a tool group's caption, from the right end: close, pin (auto-hide / dock), menu. Those that a group
// does not show are left out and the others move up, so the close button is always the rightmost one.
struct CaptionButtons {
	RECT Close{};
	RECT Pin{};
	RECT Menu{};
	bool HasClose{};
	bool HasPin{};
	bool HasMenu{};
	int TextRight{};		// the caption text has to end here
};
CaptionButtons ComputeCaptionButtons(const RECT& caption, bool close, bool pin, bool menu, const DockMetrics& metrics);

RECT CloseButtonRect(const RECT& caption, const DockMetrics& metrics);

//
// The tab strip
//

// What a tab needs to know to get its size.
struct TabSpec {
	int TextWidth{};
	bool HasIcon{};
	bool Closable{};		// has a close button
	bool Marked{};			// shows a modified mark of its own (a tab with a close button shows it in the button's place)
};

struct TabStrip {
	std::vector<RECT> Tabs;			// the visible tabs, in order
	std::vector<RECT> Close;		// their close buttons (empty rectangle for tabs that have none)
	std::vector<RECT> Mark;			// the modified mark of those tabs that have one and no close button
	int First{};					// index of Tabs[0] among all tabs
	bool Overflow{};				// not all tabs fit: the first ones may be scrolled out and there is a drop-down button
	RECT OverflowButton{};
};

// Lays the tabs out from left to right. When they do not fit, the drop-down button takes the right end of the
// strip and only a run of whole tabs is shown, starting at 'first' but moved as far as needed to keep the
// 'active' tab (pass -1 for none) in view, and back so that no room is wasted at the end.
TabStrip LayoutTabStrip(const std::vector<TabSpec>& tabs, const RECT& strip, const DockMetrics& metrics, int first, int active);

// Tabs in several rows instead of a scrolling row: the tabs are packed left to right, a new row starts when the next tab
// does not fit, and the row of the active tab is the one next to the content (the last for a strip above it, the first
// for one below it), as Visual Studio does. 'strip' is the whole strip; the result has a rectangle for every tab
// (First 0, no overflow button).
int CountTabRows(const std::vector<TabSpec>& tabs, int width, const DockMetrics& metrics);
TabStrip LayoutTabRows(const std::vector<TabSpec>& tabs, const RECT& strip, const DockMetrics& metrics, int active, bool stripAtBottom);

int TabWidth(const TabSpec& tab, const DockMetrics& metrics);
// the size of the modified dot
int MarkSize(const DockMetrics& metrics);
RECT TabIconRect(const RECT& tab, const DockMetrics& metrics);
RECT TabTextRect(const RECT& tab, const TabSpec& spec, const DockMetrics& metrics);

//
// The window switcher: two columns (documents, tool windows), each a header above a list of rows, and a footer that
// describes the selected item. Lists longer than MaxRows scroll.
//
inline constexpr int NavigatorMaxRows = 14;

struct NavigatorLayout {
	SIZE Size{};				// of the client area
	RECT Header[2]{};
	RECT Column[2]{};			// the rows' area
	std::vector<RECT> Rows[2];	// the visible rows (their index is First[c] + i)
	int First[2]{};
	RECT Footer{};
};
// 'counts': the number of items in each column. 'first' is the first visible row of each column as of last time; the
// result's First keeps 'selected' (the row of the selected column, -1 for none) in view.
NavigatorLayout ComputeNavigatorLayout(const int counts[2], const int first[2], int selectedColumn, int selectedRow, const DockMetrics& metrics);

// If the top of the rectangle (its title bar) is out of reach on every monitor, moves it onto the nearest one, keeping
// its size if it fits. Returns whether it moved the rectangle.
bool KeepRectOnScreen(RECT& rect, int reachable = 40);

}
