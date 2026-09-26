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
GroupParts ComputeGroupParts(const DockGroup& group, const RECT& client, const DockMetrics& metrics);

RECT CloseButtonRect(const RECT& caption, const DockMetrics& metrics);

//
// The tab strip
//

// What a tab needs to know to get its size.
struct TabSpec {
	int TextWidth{};
	bool HasIcon{};
	bool Closable{};		// has a close button
};

struct TabStrip {
	std::vector<RECT> Tabs;			// the visible tabs, in order
	std::vector<RECT> Close;		// their close buttons (empty rectangle for tabs that have none)
	int First{};					// index of Tabs[0] among all tabs
	bool Overflow{};				// not all tabs fit: the first ones may be scrolled out and there is a drop-down button
	RECT OverflowButton{};
};

// Lays the tabs out from left to right. When they do not fit, the drop-down button takes the right end of the
// strip and only a run of whole tabs is shown, starting at 'first' but moved as far as needed to keep the
// 'active' tab (pass -1 for none) in view, and back so that no room is wasted at the end.
TabStrip LayoutTabStrip(const std::vector<TabSpec>& tabs, const RECT& strip, const DockMetrics& metrics, int first, int active);

int TabWidth(const TabSpec& tab, const DockMetrics& metrics);
RECT TabIconRect(const RECT& tab, const DockMetrics& metrics);
RECT TabTextRect(const RECT& tab, const TabSpec& spec, const DockMetrics& metrics);

}
