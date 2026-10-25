#pragma once

// The breadcrumb bar's window-free parts: measuring the segments, laying out the bar (the back, forward and up
// buttons, the box with the root icon, the root chevron, the segments with their chevrons, the room to click into
// editing, the history arrow and the action buttons), folding the leftmost segments into the root chevron's menu when
// they don't fit, hit testing, the back/forward history, the recent locations, path text and segments, and the
// suggestions shown while typing. Sizes are in pixels; the control scales the metrics for its DPI. Covered by
// BreadcrumbTests.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ---- Segments ---------------------------------------------------------------------------------

struct BrcMetrics {
	int height = 26;           // the bar's
	int navButton = 26;        // back, forward and up, each
	int recentButton = 14;     // the recent locations' arrow beside forward
	int navGap = 4;            // the nav buttons to the box
	int border = 1;            // the box's frame
	int icon = 16;
	int iconPad = 4;           // the root icon, each side
	int textPad = 6;           // a segment's text, each side
	int chevron = 16;          // a segment's arrow part, and the root chevron
	int minSegment = 40;       // the last segment's text part, when even it doesn't fit
	int minEmpty = 24;         // room kept after the segments to click into editing
	int boxButton = 24;        // the history arrow and the action buttons, in the box

	BrcMetrics Scaled(int dpi) const;
};

struct BrcSegment {
	std::wstring text;         // shown
	std::wstring name;         // its part of the path text; empty: none (a "This PC" above "C:")
	int    image = -1;
	bool   noChildren = false; // no chevron
	LPARAM lParam = 0;

	// BrcMeasureSegments'
	int    textWidth = 0;
};

using BrcMeasure = std::function<int(std::wstring_view)>;    // a text's width

void BrcMeasureSegments(std::vector<BrcSegment>& segments, const BrcMeasure& measure);
// the text part's width (the text and its padding) and the chevron's
int BrcTextPart(const BrcSegment& segment, const BrcMetrics& m);
int BrcChevronPart(const BrcSegment& segment, const BrcMetrics& m);

// ---- Layout -------------------------------------------------------------------------------------

// What the bar shows.
struct BrcParts {
	bool nav = true;           // back, forward and the recent arrow
	bool up = true;
	bool icon = true;          // the root icon
	bool rootChevron = true;   // the root's children (with folded segments, the chevron is shown anyway)
	bool history = true;       // the history arrow at the box's right
	int  actions = 0;          // the app's buttons at the box's right
};

struct BrcLayout {
	RECT back{}, forward{}, recent{}, up{};    // empty if not shown
	RECT box{};                // the frame
	RECT icon{};
	RECT overflow{};           // the root chevron; empty if not shown
	int  firstVisible = 0;     // segments before it are folded into the root chevron's menu
	bool truncated = false;    // the last segment's text is cut
	std::vector<RECT> text;    // by segment; empty if folded
	std::vector<RECT> chevron; // by segment; empty if folded or without one
	RECT empty{};              // after the segments: a click there edits
	RECT edit{};               // where the edit box goes
	RECT history{};
	std::vector<RECT> actions;

	bool Overflowed() const {
		return firstVisible > 0;
	}
};

// The bar in width (its height is m.height): the segments that fit, kept from the last one back; the last is
// always shown, its text cut if it doesn't fit.
BrcLayout BrcLayoutBar(const std::vector<BrcSegment>& segments, const BrcParts& parts, int width, const BrcMetrics& m);

// The folded segments, the deepest first (the root chevron's menu lists them before the root's children).
std::vector<int> BrcFoldedSegments(const BrcLayout& layout);

// ---- Hit testing --------------------------------------------------------------------------------

enum class BrcPart : uint8_t { None, Back, Forward, Recent, Up, Icon, Overflow, Text, Chevron, Empty, History, Action };

struct BrcHit {
	BrcPart part = BrcPart::None;
	int index = -1;            // the segment's (Text, Chevron) or the action button's
};

BrcHit BrcHitTest(const BrcLayout& layout, POINT pt);

// ---- History ------------------------------------------------------------------------------------

struct BrcLocation {
	std::wstring path;         // what the app navigates by
	std::wstring text;         // shown in menus
	int    image = -1;
};

// Paths are compared ignoring case.
bool BrcSamePath(std::wstring_view a, std::wstring_view b);

// The back/forward list: the locations visited, the current one among them.
class BrcHistory {
public:
	explicit BrcHistory(size_t limit = 100) : m_Limit(limit) {
	}

	// a new location: the ones ahead of the current one go, the oldest past the limit too; the current one again
	// changes nothing (but its text and image)
	void Navigate(const BrcLocation& location);
	// offset < 0 goes back, > 0 forward; false if there's no such entry
	bool Go(int offset);
	// the current entry, as the app showed it (after going back to a path it then wrote differently); none: Navigate
	void Replace(const BrcLocation& location);
	bool CanGo(int offset) const;
	const BrcLocation* At(int offset) const;    // nullptr if none
	const BrcLocation* Current() const {
		return At(0);
	}

	// The recent locations menu's entries, top to bottom: the farthest forward first, then the current one, then
	// back. At most count, the current one kept as near the middle as the ends allow.
	std::vector<int> MenuOffsets(int count) const;

	void Clear();
	size_t Size() const {
		return m_Entries.size();
	}

private:
	std::vector<BrcLocation> m_Entries;
	int    m_Current = -1;
	size_t m_Limit;
};

// The history arrow's list: the locations most recently gone to, the newest first, no path twice.
class BrcRecent {
public:
	explicit BrcRecent(size_t limit = 25) : m_Limit(limit) {
	}

	void Add(std::wstring_view path);      // to the front
	bool Remove(std::wstring_view path);
	void Clear() {
		m_Paths.clear();
	}
	const std::vector<std::wstring>& Paths() const {
		return m_Paths;
	}

	// one path a line
	std::wstring Save() const;
	void Load(std::wstring_view text);

private:
	std::vector<std::wstring> m_Paths;
	size_t m_Limit;
};

// ---- Path text --------------------------------------------------------------------------------

// The segments' names joined with separator (the first of separators), skipping empty names and never doubling
// a separator one already ends with ("C:\" + "Users").
std::wstring BrcJoinPath(const std::vector<BrcSegment>& segments, std::wstring_view separators, size_t count = SIZE_MAX);

// A typed path's components, trimmed, empty ones dropped; leading separators stay on the first ("\\server").
std::vector<std::wstring> BrcSplitPath(std::wstring_view text, std::wstring_view separators);

// Typed text split at its last separator: the parent (the separator kept) and what's being typed after it.
void BrcSplitLast(std::wstring_view text, std::wstring_view separators, std::wstring& parent, std::wstring& leaf);

// The suggestions for typed text: the recent paths that start with it (newest first), then the candidates that do
// (in order: the app's), none twice, at most count. Nothing typed suggests the recent paths.
std::vector<std::wstring> BrcSuggest(std::wstring_view typed, const std::vector<std::wstring>& candidates,
	const std::vector<std::wstring>& recent, size_t count = 20);

// The longest start the suggestions share past typed (ignoring case; in the first's case), for completing it.
std::wstring BrcCommonPrefix(std::wstring_view typed, const std::vector<std::wstring>& suggestions);
