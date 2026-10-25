#pragma once

// The ribbon's window-free parts: measuring items, the steps a group shrinks through as the ribbon narrows (large
// items to small, small to icons, then the group collapsed into one button), placing a group's items in columns,
// choosing each group's step so a tab fits, hit testing, the order of the tabs shown (contextual ones last), key
// tips, radio groups, moving between items with the arrow keys, and the saved state's text. Sizes are in pixels;
// the control scales the metrics for its DPI. Covered by RibbonBarTests.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ---- Items, groups, tabs ----------------------------------------------------------------------

// The sizes an item is shown at, largest first: a group shrinks by taking its items down them.
enum class RibSize : uint8_t { Large, Small, Icon };

enum class RibKind : uint8_t { Button, Toggle, Radio, DropDown, Split, Window, Separator };

// item states (the public RIBIS_* values)
constexpr UINT RibStateDisabled = 0x1;
constexpr UINT RibStateChecked = 0x2;
constexpr UINT RibStateHidden = 0x4;

// group flags (the public RIBGF_* values)
constexpr UINT RibGroupNoCollapse = 0x1;
constexpr UINT RibGroupLauncher = 0x2;        // a dialog launcher beside the label

struct RibMetrics {
	int rowHeight = 22;        // a small item's; the items area is three rows
	int largeIcon = 32;
	int smallIcon = 16;
	int pad = 3;               // inside an item, each side
	int gap = 4;               // icon to text, text to arrow
	int arrow = 7;             // a drop-down arrow
	int groupPad = 4;          // a group's edge to its items, each side
	int columnGap = 2;
	int labelHeight = 18;      // the group's label, under its items
	int launcher = 12;
	int separator = 9;         // a separator's column

	RibMetrics Scaled(int dpi) const;
	int ItemsHeight() const {
		return 3 * rowHeight;
	}
	int BodyHeight() const {   // a tab's groups, labels included
		return 2 * groupPad + ItemsHeight() + labelHeight;
	}
};

struct RibItem {
	UINT   id = 0;
	RibKind kind = RibKind::Button;
	std::wstring text;
	std::wstring tip;
	std::wstring keyTip;       // the app's, else one is assigned
	int    image = -1;         // in the large image list
	int    smallImage = -1;    // in the small one
	UINT   state = 0;          // RibState*
	int    radio = 0;          // radio items with the same number in a tab are one group
	RibSize size = RibSize::Large;      // the largest it's shown at
	RibSize minSize = RibSize::Icon;    // the smallest
	int    rows = 1;           // a window's height in rows (1 to 3)
	int    windowWidth = 0;    // a window's width
	int    windowDips = 0;     // the app's, in DIPs: the control scales it into windowWidth
	HWND   window = nullptr;
	HMENU  menu = nullptr;     // a drop-down's or split button's
	LPARAM lParam = 0;

	// RibMeasureItem's
	int    width[3] = {};      // by RibSize
	std::wstring line1, line2; // the label of the large size

	bool Hidden() const {
		return (state & RibStateHidden) != 0;
	}
	bool HasArrow() const {
		return kind == RibKind::DropDown || kind == RibKind::Split;
	}
	bool HasImage() const {
		return image >= 0 || smallImage >= 0;
	}
	RibSize Largest() const;   // size, as far as the kind allows
	RibSize Smallest() const;  // minSize, as far as the kind allows (an icon needs an image)
	int Rows(RibSize size) const;
};

// A group at one width: the items' sizes, or collapsed into one button.
struct RibStep {
	std::vector<RibSize> sizes;    // by item
	int  width = 0;
	bool collapsed = false;
};

struct RibGroup {
	UINT   id = 0;
	std::wstring text;
	std::wstring keyTip;
	int    image = -1;         // the collapsed button's, in the large image list
	UINT   flags = 0;          // RibGroup*
	int    priority = 0;       // lower shrinks first: all the way, before any group of a higher one starts
	UINT   launcherId = 0;     // the launcher's command
	bool   hidden = false;
	std::vector<RibItem> items;

	// RibMeasureGroup's
	int    labelWidth = 0;     // the label and the launcher
	int    collapsedWidth = 0;
	std::wstring line1, line2; // the collapsed button's label
	std::vector<RibStep> steps;    // widest first; empty: nothing to show

	bool Visible() const {
		return !hidden && !steps.empty();
	}
};

struct RibTab {
	UINT   id = 0;
	std::wstring text;
	std::wstring keyTip;
	int    context = 0;        // 0: always shown; else the contextual set it belongs to
	bool   hidden = false;
	std::vector<RibGroup> groups;
};

// A set of contextual tabs, shown while the app says (a picture selected: "Picture Tools").
struct RibContext {
	int    id = 0;
	std::wstring name;
	COLORREF color = 0;
	bool   shown = false;
};

// ---- Measuring ----------------------------------------------------------------------------------

using RibMeasure = std::function<int(std::wstring_view)>;    // a text's width

// Splits a large item's label into two lines at the space that makes the wider one narrowest (extra2: the room
// something after the second line takes, such as an arrow); a label of one word stays on the first line.
void RibSplitLabel(std::wstring_view text, const RibMeasure& measure, int extra2, std::wstring& line1, std::wstring& line2);

void RibMeasureItem(RibItem& item, const RibMetrics& m, const RibMeasure& measure);
// the items, the label and the collapsed button, then the steps
void RibMeasureGroup(RibGroup& group, const RibMetrics& m, const RibMeasure& measure);
// the steps again, from the measured items (when one is hidden or shown)
void RibGroupSteps(RibGroup& group, const RibMetrics& m);

// ---- Layout -----------------------------------------------------------------------------------------

struct RibItemPlace {
	RECT    rc{};              // relative to the group's left and the body's top; empty if hidden
	RibSize size = RibSize::Large;
};

struct RibGroupPlace {
	int  x = 0;                // in the tab
	int  width = 0;
	int  step = 0;
	bool collapsed = false;
	std::vector<RibItemPlace> items;   // by item, relative to x
	RECT label{};              // relative to x
	RECT launcher{};           // relative to x; empty without one
};

// the items in columns of three rows (a large item or a separator takes a whole column); width: the group's
std::vector<RibItemPlace> RibPlaceItems(const RibGroup& group, const std::vector<RibSize>& sizes, const RibMetrics& m, int* width = nullptr);

// Each visible group's step for a tab to fit in width: groups of the lowest priority shrink first, a step at a time
// from the right, round after round, then collapse from the right (none collapses while another of its priority can
// still shrink); then the next priority. total: the tab's width then (more than the width if
// even the smallest steps don't fit). Hidden groups get -1.
std::vector<int> RibFitTab(const std::vector<RibGroup>& groups, int width, const RibMetrics& m, int* total = nullptr);

// The groups placed from x = 0, a line's pixel between them; hidden ones get width 0.
std::vector<RibGroupPlace> RibLayoutTab(const std::vector<RibGroup>& groups, int width, const RibMetrics& m, int* total = nullptr);

// ---- Hit testing ----------------------------------------------------------------------------------

enum class RibPart : uint8_t { None, Item, Arrow, Group, Launcher };     // Arrow: a split button's; Group: collapsed

struct RibHit {
	RibPart part = RibPart::None;
	int group = -1;
	int item = -1;
};

// pt relative to the tab's x = 0 and the body's top
RibHit RibHitTest(const std::vector<RibGroup>& groups, const std::vector<RibGroupPlace>& places, POINT pt, const RibMetrics& m);
// a split button's two parts: large, the image above and the label below; else the arrow at the right
void RibSplitRects(const RECT& rc, RibSize size, const RibMetrics& m, RECT& main, RECT& arrow);
// a collapsed group's button, relative to the group's x
RECT RibCollapsedRect(int width, const RibMetrics& m);

// ---- Tabs ---------------------------------------------------------------------------------------------

// The tabs shown, in order: those always shown, then each shown context's, in the contexts' order.
std::vector<int> RibVisibleTabs(const std::vector<RibTab>& tabs, const std::vector<RibContext>& contexts);

// ---- Key tips -------------------------------------------------------------------------------------------

// Key tips for labels, unique and none the start of another (typing one never leaves a longer one unreachable).
// fixed[i] not empty keeps it (in capitals); the others get, in this order: a word's first letter, another letter of
// the label, a digit, then two characters.
std::vector<std::wstring> RibAssignKeyTips(const std::vector<std::wstring>& labels, const std::vector<std::wstring>& fixed);
// The quick access bar's: 1 to 9, then 09, 08 ... 01, then 0A, 0B ...
std::wstring RibQatKeyTip(int index);
// The tip typed exactly, -1 if none; prefixCount: the tips that start with typed (0: a wrong key).
int RibMatchKeyTip(const std::vector<std::wstring>& tips, std::wstring_view typed, int* prefixCount = nullptr);

// ---- Items -----------------------------------------------------------------------------------------------

// Checks a radio item and unchecks the others of its group in the tab; true if anything changed.
bool RibCheckRadio(std::vector<RibGroup>& groups, int group, int item);

// The rect to go to from rects[from] with an arrow key (VK_LEFT ...): the nearest in that direction, those lined up
// with it first; -1 if none. Empty rects are skipped.
int RibNearest(const std::vector<RECT>& rects, int from, UINT vk);

// ---- State ---------------------------------------------------------------------------------------------------

struct RibState {
	UINT tab = 0;              // the selected tab's id; 0: none saved
	bool minimized = false;
	std::vector<UINT> qat;     // the quick access bar's items, in order
};

std::wstring RibSaveState(const RibState& state);             // "tab=3;min=0;qat=101,102"
bool RibLoadState(std::wstring_view text, RibState& state);   // false if it isn't such a text; unknown keys are skipped
