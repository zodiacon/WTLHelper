#pragma once

#include <windows.h>
#include <cstdint>

namespace WTLDock {

enum class DockSide { Left, Right, Top, Bottom };
inline constexpr int SideCount = 4;

// Horizontal: children are placed side by side (left to right). Vertical: children are stacked (top to bottom).
enum class Axis { Horizontal, Vertical };

// Where a pane is placed relative to a target group. Tab means "as another tab of the target group".
enum class DockPosition { Left, Right, Top, Bottom, Tab };

enum class PaneKind { Tool, Document };
enum class PaneState { Hidden, Docked, Document, AutoHide, Floating };

enum class PaneCaps : uint32_t {
	None = 0,
	CanFloat = 1,
	CanAutoHide = 2,
	CanClose = 4,	// a UI hint (close button); Hide() always works programmatically
	All = CanFloat | CanAutoHide | CanClose,
};
DEFINE_ENUM_FLAG_OPERATORS(PaneCaps)

constexpr bool Has(PaneCaps caps, PaneCaps flag) {
	return (caps & flag) == flag;
}

constexpr Axis AxisOf(DockSide side) {
	return side == DockSide::Left || side == DockSide::Right ? Axis::Horizontal : Axis::Vertical;
}

constexpr bool IsBefore(DockSide side) {
	return side == DockSide::Left || side == DockSide::Top;
}

constexpr DockSide ToSide(DockPosition pos) {
	return pos == DockPosition::Left ? DockSide::Left : pos == DockPosition::Right ? DockSide::Right :
		pos == DockPosition::Top ? DockSide::Top : DockSide::Bottom;
}

inline int Width(const RECT& r) {
	return r.right - r.left;
}

inline int Height(const RECT& r) {
	return r.bottom - r.top;
}

inline int Length(const RECT& r, Axis axis) {
	return axis == Axis::Horizontal ? Width(r) : Height(r);
}

inline int Along(const SIZE& size, Axis axis) {
	return axis == Axis::Horizontal ? size.cx : size.cy;
}

// How much of its parent split's space a node gets, along the split's axis.
// Px nodes keep their size when the window is resized; Star nodes share what is left in proportion to their weight.
struct SizeSpec {
	enum class Unit { Px, Star };

	Unit Kind{ Unit::Star };
	double Value{ 1 };

	static SizeSpec Px(double px) {
		return { Unit::Px, px };
	}
	static SizeSpec Star(double weight = 1) {
		return { Unit::Star, weight };
	}
	bool IsStar() const {
		return Kind == Unit::Star;
	}
};

struct LayoutMetrics {
	int SplitterThickness{ 6 };
	int AutoHideBarThickness{ 24 };
	SIZE MinGroupSize{ 80, 60 };	// floor for every group; a pane's own MinSize can raise it
};

}
