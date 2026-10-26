#pragma once

// The calendar view's window-free parts: dates (and the month grid a month view will need), the items and the store
// that holds them (the control's own, or a virtual mode's cache of the range shown, with the days it lacks), splitting
// items into the pieces a range of day columns shows (timed pieces per day, all-day and long ones in the strip), placing
// overlapping pieces side by side, packing the strip's rows, the day view's boxes and their parts under the mouse,
// dragging (moving, resizing, between the strip and the days, selecting slots), scrolling, zooming, the items' tab
// order, and undo. Everything is in days and minutes, so a month view is another layout over the same pieces. Sizes are
// in pixels; the control scales the metrics for its DPI. Covered by CalendarViewTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// ---- Time ------------------------------------------------------------------------------------------
//
// Times are minutes since 1601-01-01 00:00 (FILETIME's epoch), local, with no time zones; days count from that day, a
// Monday.

using ClvTime = int64_t;
constexpr int64_t ClvDay = 1440;   // minutes

struct ClvDate {
	int year = 1601, month = 1, day = 1;
};

int64_t ClvDayOf(ClvTime t);                                  // the day t is in (before 1601: negative)
int     ClvMinuteOf(ClvTime t);                               // t's minute in its day, 0 to 1439
ClvTime ClvTimeOf(int64_t day, int minute = 0);
ClvDate ClvDateOf(int64_t day);
int64_t ClvDayFromDate(int year, int month, int day);
int     ClvWeekday(int64_t day);                              // 0 Sunday to 6 Saturday
int     ClvDaysInMonth(int year, int month);
int64_t ClvWeekStart(int64_t day, int firstWeekday);          // the first day of day's week (firstWeekday 0: Sunday)
int64_t ClvMonthStart(int64_t day);
int64_t ClvAddMonths(int64_t day, int months);                // the same day of the month, or the month's last
ClvTime ClvFromSystemTime(const SYSTEMTIME& st);
SYSTEMTIME ClvToSystemTime(ClvTime t);

// The weeks a month view shows: from the week holding the month's first day, as many as hold the month (4 to 6).
struct ClvMonthGrid {
	int64_t first = 0;
	int weeks = 0;
};

ClvMonthGrid ClvMonthGridOf(int64_t day, int firstWeekday);

// The days shown: day columns, or a month view's grid.
struct ClvRange {
	int64_t first = 0;
	int days = 1;

	int64_t Last() const {
		return first + days - 1;
	}
	ClvTime Start() const {
		return first * ClvDay;
	}
	ClvTime End() const {
		return (first + days) * ClvDay;
	}
	bool Contains(int64_t day) const {
		return day >= first && day < first + days;
	}
};

// The parts of want that have isn't (none, one, or two: before and after it).
std::vector<ClvRange> ClvMissing(ClvRange want, ClvRange have);

// The working week, shaded apart from the rest of the time.
struct ClvWorkWeek {
	BYTE days = 0x3E;              // a bit by weekday, Sunday's the lowest: Monday to Friday
	int  start = 8 * 60;           // a working day's working minutes: [start, end)
	int  end = 17 * 60;

	bool IsWorkingDay(int64_t day) const {
		return (days & (1 << ClvWeekday(day))) != 0;
	}
	bool IsWork(int64_t day, int minute) const {
		return IsWorkingDay(day) && minute >= start && minute < end;
	}
};

// ---- Items -----------------------------------------------------------------------------------------

enum class ClvContent : uint8_t {
	Default,                       // the control draws the item
	Custom,                        // the app draws it (custom draw)
	Window,                        // the app's window shows it, under a caption strip the control draws
};

struct ClvItem {
	uint64_t id = 0;
	ClvTime start = 0, end = 0;    // [start, end); an all-day item's are the starts of its first and next days
	bool allDay = false;
	std::wstring title;
	std::wstring tip;
	COLORREF color = CLR_INVALID;  // CLR_INVALID: the control's
	ClvContent content = ClvContent::Default;
	HWND hwnd = nullptr;           // ClvContent::Window's
	bool readOnly = false;         // not moved or resized by the user
	LPARAM lParam = 0;
};

// All-day items, and timed ones a day or longer, go in the strip above the days.
inline bool ClvInStrip(const ClvItem& item) {
	return item.allDay || item.end - item.start >= ClvDay;
}

// An item's first and last day (an empty item's: the day it's at).
int64_t ClvFirstDay(const ClvItem& item);
int64_t ClvLastDay(const ClvItem& item);

class ClvItemStore {
public:
	using State = std::vector<ClvItem>;

	bool Add(const ClvItem& item);                            // false if its id is 0 or taken, or it ends before it starts
	bool Set(const ClvItem& item);                            // the item with its id, replaced; false if there's none
	bool Remove(uint64_t id);
	void Clear() {
		m_Items.clear();
	}
	ClvItem* Find(uint64_t id);
	const ClvItem* Find(uint64_t id) const;
	int  Index(uint64_t id) const;                            // in Items; -1 if none
	const std::vector<ClvItem>& Items() const {
		return m_Items;
	}

	// The items over [from, to) (an empty item: at a time in it), by start, the longer first, then by id: indexes.
	std::vector<int> InRange(ClvTime from, ClvTime to) const;
	// Keeps the items over [from, to) and returns the others, removed (a virtual cache's that left the range).
	std::vector<ClvItem> Keep(ClvTime from, ClvTime to);

	State Save() const {
		return m_Items;
	}
	void Restore(State state) {
		m_Items = std::move(state);
	}

private:
	std::vector<ClvItem> m_Items;
};

// ---- Pieces ----------------------------------------------------------------------------------------
//
// An item as a range shows it: a timed item a piece per day it touches, a strip item one piece over its days.

struct ClvPiece {
	int item = 0;                  // the index in the store's Items
	int day = 0;                   // the column, from the range's first day (strip: its first)
	int lastDay = 0;               // strip: its last column (timed: day)
	int start = 0, end = 0;        // timed: its minutes in the day, [start, end), end up to 1440
	bool before = false;           // the item goes on before the piece
	bool after = false;            // and after it
};

// The pieces of the items (indexes, by InRange) in the range: timed, by day then as given, and the strip's, as given.
void ClvSplit(const ClvItemStore& store, const std::vector<int>& items, ClvRange range,
	std::vector<ClvPiece>& timed, std::vector<ClvPiece>& strip);

// Where a piece goes among those it overlaps: a column of columns, widened over span columns to the right while none of
// them has a piece overlapping it.
struct ClvPlace {
	int column = 0, span = 1, columns = 1;
};

// The places of a day's pieces, as their [start, end) spans (the caller makes short ones long enough to see), in the
// spans' order: those overlapping, one after another, form a cluster; each goes in its cluster's first free column,
// taken by start, the longer first.
std::vector<ClvPlace> ClvPlaceOverlaps(const std::vector<std::pair<int, int>>& spans);

// The strip's row of each piece (each in the first row free over its days, by first day, the longer first); returns
// how many rows there are.
int ClvPackStrip(const std::vector<ClvPiece>& pieces, std::vector<int>& rows);

// ---- The day view ----------------------------------------------------------------------------------

struct ClvMetrics {
	int slotHeight = 22;           // a slot's pixels at the zoom's start
	int rulerWidth = 56;           // the time ruler at the left of the days
	int headerHeight = 26;         // the days' names
	int stripRow = 22;             // an all-day row
	int stripMaxRows = 3;          // the strip grows to, then scrolls
	int stripPad = 3;              // above and below the strip's rows
	int itemGap = 8;               // the free band at a day's right, to click its slots
	int itemPad = 1;               // between items
	int caption = 18;              // a window item's caption strip
	int minItem = 18;              // an item's least height
	int minBody = 16;              // a window item's least window height, else it's hidden
	int edge = 4;                  // the band at an item's ends that resizes it

	ClvMetrics Scaled(int dpi) const;
};

// The days' time, top to bottom: y 0 is midnight.
struct ClvAxis {
	int slotMinutes = 30;
	int slotHeight = 22;

	int Y(int minute) const {
		return (int)(((int64_t)minute * slotHeight + slotMinutes / 2) / slotMinutes);
	}
	int Minute(int y) const;                                  // the minute at y, 0 to 1440
	int Height() const {
		return Y((int)ClvDay);
	}
	int Minutes(int pixels) const;                            // the minutes pixels take, rounded up
};

// The day columns, side by side.
struct ClvColumns {
	int left = 0, width = 0, count = 1;

	int Left(int i) const {
		return left + (int)((int64_t)width * i / count);
	}
	int Right(int i) const {
		return Left(i + 1);
	}
	int At(int x) const;                                      // the column at x; -1 if none
};

// A piece on screen: the days' in content coordinates (y 0 at midnight), the strip's in the strip's (y 0 at the first
// row's top).
struct ClvBox {
	uint64_t id = 0;
	int  item = 0;                 // the index in the store's Items
	int  day = 0, lastDay = 0;     // columns
	int  row = 0;                  // the strip's
	RECT rc{};
	RECT caption{};                // a window item's caption strip (else empty)
	RECT body{};                   // where its window goes (empty: hidden; not a window item: rc)
	bool before = false, after = false;
	bool resizable = true;
};

struct ClvDayLayout {
	ClvRange range;
	ClvColumns columns;
	ClvAxis axis;
	std::vector<ClvBox> timed;     // by day, then by start, the longer first (later ones go on top)
	std::vector<ClvBox> strip;
	int stripRows = 0;

	int StripHeight(const ClvMetrics& m) const;              // the strip's content: its rows (0: none)
	int StripView(const ClvMetrics& m) const;                // the strip as shown: its pads and rows up to the maximum
	ClvTime TimeAt(int column, int minute) const {
		return ClvTimeOf(range.first + column, minute);
	}
	const ClvBox* FindTimed(uint64_t id, int day) const;
	const ClvBox* FindStrip(uint64_t id) const;
};

ClvDayLayout ClvLayoutDay(const ClvItemStore& store, ClvRange range, const ClvColumns& columns, const ClvAxis& axis,
	const ClvMetrics& m);

// Item: a default or custom item's body (a window item's caption); Caption: a window item's caption strip; Body: where
// its window is (the window has the mouse there); Start, End: the edges that resize it (the strip's left and right);
// Slot: no item, a day's time.
enum class ClvPart : uint8_t { None, Item, Caption, Body, Start, End, Slot, Ruler };

struct ClvHit {
	ClvPart part = ClvPart::None;
	int box = -1;                  // in timed or strip
	int day = -1;                  // the column
	int minute = 0;                // the days' (the strip's: 0)

	uint64_t Id(const ClvDayLayout& layout, bool strip) const;
};

// pt in content coordinates; top: an item drawn over the others (the selected), looked at first
ClvHit ClvHitDays(const ClvDayLayout& layout, POINT pt, const ClvMetrics& m, uint64_t top = 0);
// pt in the strip's coordinates
ClvHit ClvHitStrip(const ClvDayLayout& layout, POINT pt, const ClvMetrics& m, uint64_t top = 0);

// ---- Dragging --------------------------------------------------------------------------------------

struct ClvSpan {
	ClvTime start = 0, end = 0;
	bool allDay = false;
};

inline ClvSpan ClvSpanOf(const ClvItem& item) {
	return { item.start, item.end, item.allDay };
}

ClvTime ClvSnap(ClvTime t, int snap);                         // to the nearest multiple of snap minutes (0, 1: t)

// Moved in the days: the start moves as the time under the mouse did (grab at the press, at now), snapped; a timed item
// keeps its length, an all-day one becomes timed, defaultMinutes long, starting at the slot under the mouse.
ClvSpan ClvDragMove(ClvSpan item, ClvTime grab, ClvTime at, int snap, int defaultMinutes);
// Moved in the strip: by days (grabDay at the press, day now); an all-day item keeps its days, a timed one becomes all
// day over the days it touched (at least one).
ClvSpan ClvDragMoveStrip(ClvSpan item, int64_t grabDay, int64_t day);
// An end dragged to at, snapped, at least minMinutes from the other end (a strip item: by days, at least one).
ClvSpan ClvDragStart(ClvSpan item, ClvTime at, int snap, int minMinutes);
ClvSpan ClvDragEnd(ClvSpan item, ClvTime at, int snap, int minMinutes);
ClvSpan ClvDragStripStart(ClvSpan item, int64_t day);
ClvSpan ClvDragStripEnd(ClvSpan item, int64_t day);
// The slots from anchor's to at's, both in (slot minutes long, aligned).
ClvSpan ClvSlotRange(ClvTime anchor, ClvTime at, int slot);
// The days from anchor to day, both in, as an all-day span.
ClvSpan ClvDayRange(int64_t anchor, int64_t day);
// A slot moved by days and slots, kept in its day (0 to the day's last slot).
ClvTime ClvMoveSlot(ClvTime t, int days, int slots, int slot);

// ---- Scrolling, zoom and the tab order -------------------------------------------------------------

int ClvClampScroll(int scroll, int content, int view);
int ClvScrollToShow(int scroll, int top, int bottom, int view, int content);
// The slot height steps zoom steps (each 1.25 times, or back), between min and max.
int ClvZoom(int slotHeight, int steps, int minHeight, int maxHeight);

// The items shown, in the order Tab goes through them: the strip's by first day, then the timed ones by start, the
// longer first; each once.
std::vector<uint64_t> ClvTabOrder(const ClvDayLayout& layout, const ClvItemStore& store);
// The item after (or before) current; 0 past the ends. With current 0 (or not there): the first (or last).
uint64_t ClvNextItem(const std::vector<uint64_t>& order, uint64_t current, bool forward);

// ---- Undo ------------------------------------------------------------------------------------------

// The items before and after each change, to go back and forth (the control's own items only: a virtual mode's app
// keeps its own undo).
class ClvHistory {
public:
	explicit ClvHistory(size_t limit = 100) : m_Limit(limit) {
	}
	void Push(ClvItemStore::State before, ClvItemStore::State after);
	bool Undo(ClvItemStore& store);
	bool Redo(ClvItemStore& store);
	bool CanUndo() const {
		return m_Done > 0;
	}
	bool CanRedo() const {
		return m_Done < m_Changes.size();
	}
	void Clear() {
		m_Changes.clear();
		m_Done = 0;
	}

private:
	struct Change {
		ClvItemStore::State before, after;
	};
	std::vector<Change> m_Changes;
	size_t m_Done = 0;
	size_t m_Limit;
};
