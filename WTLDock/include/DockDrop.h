#pragma once

#include "DockLayout.h"
#include "DockTheme.h"
#include <vector>

namespace WTLDock {

//
// Drag and drop docking, without windows: which drop targets a dragged pane or group is offered, which one the
// cursor is on, what the drop would look like, and how to carry it out. All rectangles are in screen coordinates.
//
// The dragged thing is either a single pane (dragged by its tab) or a whole group (dragged by its caption).
// The targets are:
//   - the compass around the group under the cursor: a tab of that group (centre) or a split on one of its sides,
//     and dropping on the group's caption or tab strip is a tab too;
//   - guides at the four edges of the main window: dock at that edge;
//   - anywhere else: a floating window where the ghost outline is.
// Only what the model allows is offered.
//

struct DropTarget {
	enum class Kind { None, Tab, Side, Edge, Float };

	Kind Type{ Kind::None };
	const DockGroup* Group{};						// Tab, Side: the group that is docked to
	DockPosition Position{ DockPosition::Tab };		// Tab, Side
	DockSide Edge{ DockSide::Left };				// Edge
	int TabIndex{ -1 };								// Tab: where among the tabs (-1: last)
	RECT Preview{};									// what the drop would occupy

	bool SameTargetAs(const DropTarget& other) const {
		return Type == other.Type && Group == other.Group && Position == other.Position && Edge == other.Edge;
	}
};

// A marker shown for a target.
struct Guide {
	DropTarget Target;
	RECT Rect;
};

struct DropContext {
	const DockLayout* Layout{};
	const DockPane* Pane{};				// what is dragged: this pane...
	bool WholeGroup{};					// ...or the whole group it is in

	const DockGroup* Hover{};			// the group under the cursor
	RECT HoverRect{};
	std::vector<RECT> HoverZones;		// its caption and tab strip: dropping there means "as a tab"
	std::vector<RECT> HoverTabs;		// its visible tabs
	int HoverFirstTab{};				// the index of the first of them

	RECT MainBounds{};					// the docking area of the main window; empty: no edge guides
	RECT Ghost{};						// where a floating window would appear; empty: floating is not possible
	DockMetrics Metrics{};
	bool DockingAllowed{ true };		// false while the user holds Ctrl: only floating is left
};

std::vector<Guide> BuildGuides(const DropContext& context);
DropTarget PickTarget(const DropContext& context, const std::vector<Guide>& guides, POINT cursor);

// Carries the drop out. The layout changes only if the model allows it; returns whether it did.
bool ApplyDrop(DockLayout& layout, DockPane* pane, bool wholeGroup, const DropTarget& target);

}
