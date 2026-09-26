#include "DockDrop.h"
#include <algorithm>

namespace WTLDock {

namespace {

RECT CenteredSquare(POINT center, int size) {
	return { center.x - size / 2, center.y - size / 2, center.x - size / 2 + size, center.y - size / 2 + size };
}

// 'length' is what the new group is to get along the axis; 0: half of the target
RECT SidePreview(const RECT& r, DockPosition pos, int length = 0) {
	RECT p = r;
	const int w = length > 0 ? std::clamp(length, 1, Width(r) - 1) : Width(r) / 2;
	const int h = length > 0 ? std::clamp(length, 1, Height(r) - 1) : Height(r) / 2;
	switch (pos) {
		case DockPosition::Left: p.right = r.left + w; break;
		case DockPosition::Right: p.left = r.right - w; break;
		case DockPosition::Top: p.bottom = r.top + h; break;
		case DockPosition::Bottom: p.top = r.bottom - h; break;
		default: break;
	}
	return p;
}

RECT EdgePreview(const RECT& bounds, DockSide side, int length) {
	RECT p = bounds;
	const int limit = Length(bounds, AxisOf(side)) / 2;
	length = std::clamp(length, 1, std::max(1, limit));
	switch (side) {
		case DockSide::Left: p.right = bounds.left + length; break;
		case DockSide::Right: p.left = bounds.right - length; break;
		case DockSide::Top: p.bottom = bounds.top + length; break;
		case DockSide::Bottom: p.top = bounds.bottom - length; break;
	}
	return p;
}

DockGroup* SourceGroup(const DropContext& c) {
	return c.Pane ? c.Pane->Group() : nullptr;
}

bool CanTab(const DropContext& c, const DockGroup* hover) {
	if (c.WholeGroup)
		return c.Layout->CanMoveGroupTo(SourceGroup(c), hover, DockPosition::Tab);
	return c.Layout->CanDockTo(c.Pane, hover, DockPosition::Tab);
}

bool CanSide(const DropContext& c, const DockGroup* hover, DockPosition pos) {
	if (c.WholeGroup)
		return c.Layout->CanMoveGroupTo(SourceGroup(c), hover, pos);
	return c.Layout->CanDockTo(c.Pane, hover, pos);
}

bool CanEdge(const DropContext& c) {
	return c.WholeGroup ? c.Layout->CanMoveGroupToEdge(SourceGroup(c)) : c.Layout->CanDockToEdge(c.Pane);
}

int PreferredLength(const DropContext& c, DockSide side) {
	// what a floating group had is what it gets (the drop does the same)
	if (auto source = SourceGroup(c))
		if (const int floated = c.Layout->LengthWhenDocked(*source, side); floated > 0)
			return floated;
	const int length = Along(c.Pane->PreferredSize, AxisOf(side));
	return length > 0 ? length : 250;
}

// where among the tabs of the hovered group a tab dropped at x would go
int TabIndexAt(const DropContext& c, POINT cursor) {
	if (c.HoverTabs.empty())
		return -1;
	bool onTabs = false;
	for (auto& tab : c.HoverTabs)
		onTabs |= cursor.y >= tab.top && cursor.y < tab.bottom;
	if (!onTabs)
		return -1;
	int index = c.HoverFirstTab;
	for (auto& tab : c.HoverTabs) {
		if (cursor.x > (tab.left + tab.right) / 2)
			index++;
		else
			break;
	}
	return index;
}

}

std::vector<Guide> BuildGuides(const DropContext& c) {
	std::vector<Guide> guides;
	if (!c.Layout || !c.Pane || !c.DockingAllowed)
		return guides;

	const int size = 2 * c.Metrics.ButtonSize;
	const int step = size + c.Metrics.ButtonMargin;

	if (c.Hover) {
		const POINT center{ (c.HoverRect.left + c.HoverRect.right) / 2, (c.HoverRect.top + c.HoverRect.bottom) / 2 };

		// dropping a tab onto its own group would only move it around: the centre is left out
		if (c.Hover != SourceGroup(c) && CanTab(c, c.Hover)) {
			Guide g;
			g.Target.Type = DropTarget::Kind::Tab;
			g.Target.Group = c.Hover;
			g.Target.Position = DockPosition::Tab;
			g.Target.Preview = c.HoverRect;
			g.Rect = CenteredSquare(center, size);
			guides.push_back(g);
		}

		struct Arm {
			DockPosition Pos;
			int Dx, Dy;
		};
		for (auto arm : { Arm{ DockPosition::Left, -step, 0 }, Arm{ DockPosition::Right, step, 0 },
			Arm{ DockPosition::Top, 0, -step }, Arm{ DockPosition::Bottom, 0, step } }) {
			if (!CanSide(c, c.Hover, arm.Pos))
				continue;
			Guide g;
			g.Target.Type = DropTarget::Kind::Side;
			g.Target.Group = c.Hover;
			g.Target.Position = arm.Pos;
			int length = 0;
			if (auto source = SourceGroup(c); source && c.Hover)
				length = c.Layout->LengthBeside(*source, *c.Hover, arm.Pos);
			g.Target.Preview = SidePreview(c.HoverRect, arm.Pos, length);
			g.Rect = CenteredSquare({ center.x + arm.Dx, center.y + arm.Dy }, size);
			guides.push_back(g);
		}
	}

	if (!IsRectEmpty(&c.MainBounds) && CanEdge(c)) {
		const RECT& b = c.MainBounds;
		const int margin = size / 2;
		const int midX = (b.left + b.right) / 2, midY = (b.top + b.bottom) / 2;
		struct Edge {
			DockSide Side;
			POINT Center;
		};
		for (auto e : { Edge{ DockSide::Left, { b.left + margin + size / 2, midY } }, Edge{ DockSide::Right, { b.right - margin - size / 2, midY } },
			Edge{ DockSide::Top, { midX, b.top + margin + size / 2 } }, Edge{ DockSide::Bottom, { midX, b.bottom - margin - size / 2 } } }) {
			Guide g;
			g.Target.Type = DropTarget::Kind::Edge;
			g.Target.Edge = e.Side;
			g.Target.Preview = EdgePreview(b, e.Side, PreferredLength(c, e.Side));
			g.Rect = CenteredSquare(e.Center, size);
			guides.push_back(g);
		}
	}
	return guides;
}

DropTarget PickTarget(const DropContext& c, const std::vector<Guide>& guides, POINT cursor) {
	if (c.DockingAllowed) {
		for (auto& g : guides)
			if (PtInRect(&g.Rect, cursor))
				return g.Target;

		// the caption and the tab strip of the hovered group mean "as a tab"
		if (c.Hover && CanTab(c, c.Hover)) {
			for (auto& zone : c.HoverZones) {
				if (PtInRect(&zone, cursor)) {
					DropTarget t;
					t.Type = DropTarget::Kind::Tab;
					t.Group = c.Hover;
					t.Position = DockPosition::Tab;
					t.TabIndex = TabIndexAt(c, cursor);
					t.Preview = c.HoverRect;
					return t;
				}
			}
		}
	}

	DropTarget none;
	if (!IsRectEmpty(&c.Ghost)) {
		none.Type = DropTarget::Kind::Float;
		none.Preview = c.Ghost;
	}
	return none;
}

bool ApplyDrop(DockLayout& layout, DockPane* pane, bool wholeGroup, const DropTarget& target) {
	if (!pane || target.Type == DropTarget::Kind::None)
		return false;
	DockGroup* source = pane->Group();
	if (!source)
		return false;
	// the model wants non-const nodes; the target was picked from this layout, so it still is one
	DockGroup* group = const_cast<DockGroup*>(target.Group);

	switch (target.Type) {
		case DropTarget::Kind::Tab:
			return wholeGroup ? layout.MoveGroupTo(source, group, DockPosition::Tab) : layout.DockTo(pane, group, DockPosition::Tab, target.TabIndex);
		case DropTarget::Kind::Side:
			return wholeGroup ? layout.MoveGroupTo(source, group, target.Position) : layout.DockTo(pane, group, target.Position);
		case DropTarget::Kind::Edge:
			return wholeGroup ? layout.MoveGroupToEdge(source, target.Edge) : layout.DockToEdge(pane, target.Edge);
		case DropTarget::Kind::Float:
			return wholeGroup ? layout.FloatGroup(source, target.Preview) : layout.Float(pane, target.Preview);
		default:
			return false;
	}
}

}
