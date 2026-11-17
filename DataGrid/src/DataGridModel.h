#pragma once

// The DataGrid's window-free parts, covered by DataGridTests: the columns and their order, laying them out (fill
// columns, frozen ones), scrolling (rows whole, mapped onto a 32-bit scroll bar), hit testing, moving the caret with
// the keys, formatting values, the store, the cache of a virtual grid's cells and the map from rows in view to rows
// in the data. The values here mirror DataGrid.h's (DGVVT_*, DGVCT_*, DGVA_*, DGVCF_*, DGVHT_*); DataGridCtrl.cpp
// checks that they agree.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---- Values ------------------------------------------------------------------------------

namespace DgType {        // DGVVT_*
	enum : int { Empty, Text, Int, Float, Bool, DateTime };
}
namespace DgColType {     // DGVCT_*
	enum : int { Text, Integer, Float, Bool, DateTime, Enum, Progress, ImageText, Link };
}
namespace DgAlign {       // DGVA_*
	enum : int { Default, Left, Center, Right };
}
namespace DgColFlag {     // DGVCF_*
	enum : unsigned { Hidden = 1, NoResize = 2, NoReorder = 4, GroupDigits = 8, NoSort = 16, ReadOnly = 32, NoHide = 64 };
}
namespace DgHitFlag {     // DGVHT_*
	enum : unsigned { Nowhere = 0, Cell = 1, Header = 2, HeaderDivider = 4, RowHeader = 8, Corner = 0x10, Empty = 0x20, FilterRow = 0x40 };
}
namespace DgAggKind {     // DGVAGG_*: what a column shows on group rows
	enum : int { None, Sum, Avg, Min, Max, Count };
}
namespace DgFilterKind {  // DGVFK_*
	enum : int { Contains, Range, Values };
}

struct DgValue {
	int type = DgType::Empty;
	int64_t i = 0;          // Int, Bool, DateTime (a FILETIME's 100ns ticks)
	double d = 0;           // Float
	std::wstring s;         // Text

	static DgValue Text(std::wstring text);
	static DgValue Int(int64_t value);
	static DgValue Float(double value);
	static DgValue Bool(bool value);
	static DgValue DateTime(int64_t ticks);
	bool operator==(const DgValue&) const = default;
};

struct DgCell {
	DgValue value;
	int image = -1;
};

// ---- Columns -------------------------------------------------------------------------------

struct DgColumn {
	int id = 0;
	int slot = 0;           // where the store keeps its values (never reused)
	std::wstring title;
	int type = DgColType::Text;
	int width = 100;        // DIPs
	int minWidth = 24;      // DIPs
	int fill = 0;           // > 0: a share of the spare width
	int align = DgAlign::Default;
	int decimals = 2;
	unsigned flags = 0;
	std::vector<std::wstring> enumItems;
	intptr_t param = 0;
	int aggregate = DgAggKind::None;

	bool Hidden() const {
		return (flags & DgColFlag::Hidden) != 0;
	}
};

// "a\0b\0\0" to { "a", "b" }
std::vector<std::wstring> DgSplitEnumItems(const wchar_t* items);

class DgColumnSet {
public:
	int Insert(DgColumn column, int displayIndex = -1);   // gives it an ID and a slot; returns the ID
	bool Erase(int id);
	DgColumn* Find(int id);
	const DgColumn* Find(int id) const;
	int DisplayIndexOf(int id) const;                      // among all; -1 if none
	const std::vector<int>& Order() const {
		return m_Order;
	}
	bool SetOrder(const std::vector<int>& ids);            // all the IDs, each once
	bool Move(int id, int beforeId);                       // beforeId 0: to the end
	int Count() const {
		return (int)m_Order.size();
	}
	int SlotCount() const {
		return m_NextSlot;
	}
	std::vector<const DgColumn*> Visible() const;          // in display order

private:
	std::vector<DgColumn> m_Columns;
	std::vector<int> m_Order;
	int m_NextId = 1;
	int m_NextSlot = 0;
};

// ---- Layout ------------------------------------------------------------------------------------

struct DgColLayout {
	int id = 0;
	int left = 0;           // pixels from the first column's left edge, unscrolled
	int width = 0;
};

struct DgLayout {
	int rowHeader = 0;      // the row numbers' width
	int header = 0;         // the header's height (0: none), with the filter row
	int titles = 0;         // the titles' part of it (the filter row is under them)
	int rowHeight = 1;
	int frozenCount = 0;    // the first columns that don't scroll
	int frozenWidth = 0;
	int totalWidth = 0;     // all the columns
	std::vector<DgColLayout> cols;   // the visible columns, in display order

	int Left(int index, int scrollX) const {   // in client coordinates
		return rowHeader + cols[index].left - (index < frozenCount ? 0 : scrollX);
	}
	int Right(int index, int scrollX) const {
		return Left(index, scrollX) + cols[index].width;
	}
	int ScrollLeft() const {                   // where the scrolling columns start
		return rowHeader + frozenWidth;
	}
	int IndexOf(int id) const;
};

struct DgLayoutParams {
	int clientWidth = 0;
	int dpi = 96;
	int rowHeader = 0;
	int header = 0;         // the titles
	int filterRow = 0;      // under them (0: none)
	int rowHeight = 1;
	int frozen = 0;
};

// Fixed columns get their width (never under their minimum); fill columns share what's left by their weights, each at
// least its minimum. Frozen columns that would leave no room to scroll the others aren't frozen.
DgLayout DgLayoutColumns(const DgColumnSet& columns, const DgLayoutParams& params);

int DgScale(int dips, int dpi);
int DgUnscale(int pixels, int dpi);

// ---- Scrolling ---------------------------------------------------------------------------------

int DgMaxScrollX(const DgLayout& layout, int clientWidth);
int DgScrollXToShow(const DgLayout& layout, int scrollX, int clientWidth, int index);   // index into layout.cols
int64_t DgFullRows(const DgLayout& layout, int clientHeight);                          // rows that fit whole (>= 1)
int64_t DgMaxTopRow(int64_t rowCount, int64_t fullRows);
int64_t DgTopToShow(int64_t top, int64_t fullRows, int64_t row);

// A row count past a 32-bit scroll bar's range is scaled onto it; the ends map exactly.
struct DgScrollMap {
	static constexpr int MaxPos = 0x3FFFFFFF;
	static int Range(int64_t maxTop);                      // the scroll bar's maximum position
	static int ToPos(int64_t top, int64_t maxTop);
	static int64_t FromPos(int pos, int64_t maxTop);
};

// ---- Hit testing ------------------------------------------------------------------------------

struct DgHit {
	unsigned flags = DgHitFlag::Nowhere;
	int64_t row = -1;       // a row in view
	int col = -1;           // an index into layout.cols
};

struct DgView {
	int scrollX = 0;
	int64_t topRow = 0;
	int64_t rowCount = 0;
	int clientWidth = 0;
	int clientHeight = 0;
};

DgHit DgHitTest(const DgLayout& layout, const DgView& view, int x, int y, int dividerZone);

// where a column dragged to x would go: an index into layout.cols (cols.size(): after the last)
int DgDropIndex(const DgLayout& layout, int scrollX, int x);

// the cell under a point, or the nearest one in view when it's outside (dragging a selection past the edges)
struct DgPos;
DgPos DgPosFromPoint(const DgLayout& layout, const DgView& view, int x, int y);

// ---- The caret -----------------------------------------------------------------------------------

enum class DgKey { Left, Right, Up, Down, PageUp, PageDown, Home, End };

struct DgPos {
	int64_t row = 0;        // a row in view
	int col = 0;            // an index into the visible columns
	bool operator==(const DgPos&) const = default;
};

// Ctrl: the arrows go to the first or last row or column, Home and End to the first or last cell.
DgPos DgNavigate(DgPos pos, DgKey key, bool ctrl, int64_t rowCount, int colCount, int64_t page);

// ---- Formatting --------------------------------------------------------------------------------

struct DgNumberFormat {
	std::wstring thousands = L",";
	std::wstring decimal = L".";
};

std::wstring DgFormatInt(int64_t value, bool group, const DgNumberFormat& nf);
std::wstring DgFormatFloat(double value, int decimals, bool group, const DgNumberFormat& nf);
std::wstring DgFormatDateTime(int64_t ticks);   // "2026-11-17 13:05" (DataGridCtrl uses the user's format)
// The text a value shows in a column of a type. A check box column's is "true" or "false"; a progress one's a percent.
std::wstring DgFormat(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf);
double DgProgress(const DgValue& value);        // 0-1; -1 if the value isn't one
bool DgChecked(const DgValue& value);
int DgEffectiveAlign(const DgColumn& column);    // DgAlign::Left, Center or Right

// ---- The store -----------------------------------------------------------------------------------

class DgStore {
public:
	int64_t Count() const {
		return (int64_t)m_Rows.size();
	}
	void Resize(int64_t count);
	int64_t Insert(int64_t at, int64_t count);   // at -1 or past the end: at the end; returns the first one's index
	bool Erase(int64_t at, int64_t count);
	bool Set(int64_t row, int slot, DgCell cell);
	const DgCell* Get(int64_t row, int slot) const;   // nullptr: empty
	void ClearSlot(int slot);
	void Clear() {
		m_Rows.clear();
	}
	std::vector<DgCell> Row(int64_t row) const;                 // its cells by slot (empty if none)
	bool InsertRow(int64_t at, std::vector<DgCell> cells);      // at: 0 to Count()

private:
	std::vector<std::vector<DgCell>> m_Rows;
};

// ---- A virtual grid's cache ------------------------------------------------------------------------

class DgCellCache {
public:
	const DgCell* Find(int64_t row, int id) const;
	void Put(int64_t row, int id, DgCell cell);
	void Drop(int64_t first, int64_t last);       // the rows between, both included
	void Trim(int64_t first, int64_t last);       // all but those
	void Clear() {
		m_Rows.clear();
	}
	size_t RowCount() const {
		return m_Rows.size();
	}

private:
	std::unordered_map<int64_t, std::vector<std::pair<int, DgCell>>> m_Rows;
};

// ---- Rows in view and in the data ------------------------------------------------------------------

// The identity until an order is set (sorted, filtered, grouped); then the rows in view are its entries: a data row
// (>= 0), or a group's row (-1 - the group's index in DgViewResult::groups). With the new row's placeholder on, it's
// one more row in view, after all the others, whose data row is NewRow.
class DgRowMap {
public:
	static constexpr int64_t NewRow = -2;         // DGV_NEWROW
	void Reset(int64_t count);
	void SetOrder(std::vector<int64_t> order, int64_t dataCount);
	void SetNewRow(bool on) {
		m_NewRow = on;
	}
	bool HasNewRow() const {
		return m_NewRow;
	}
	int64_t Count() const {
		return Entries() + (m_NewRow ? 1 : 0);
	}
	bool Identity() const {
		return m_Identity;
	}
	bool IsNewRow(int64_t view) const {
		return m_NewRow && view == Entries();
	}
	int64_t ToData(int64_t view) const;           // -1 if out of range or a group's row; NewRow for the placeholder
	int64_t ToView(int64_t data) const;           // -1 if not in view
	int64_t GroupOf(int64_t view) const;          // the group a group row shows; -1 for a data row

private:
	int64_t Entries() const {
		return m_Identity ? m_Count : (int64_t)m_Order.size();
	}
	bool m_Identity = true;
	bool m_NewRow = false;
	int64_t m_Count = 0;
	std::vector<int64_t> m_Order;
	std::vector<int64_t> m_Inverse;               // per data row; -1 if not in view
};

// ---- Selection ----------------------------------------------------------------------------------

// Rectangles of rows in view and visible columns' indexes, both ends included. The last one is what Shift extends,
// from the anchor. In Rows mode every range spans all the columns; in Single mode there's one cell, the caret's.
struct DgRange {
	int64_t top = 0, bottom = 0;
	int left = 0, right = 0;
	bool operator==(const DgRange&) const = default;
	bool Contains(int64_t row, int col) const {
		return row >= top && row <= bottom && col >= left && col <= right;
	}
	static DgRange Span(DgPos a, DgPos b);
};

enum class DgSelMode { Single, Cells, Rows };

class DgSelection {
public:
	void SetMode(DgSelMode mode, DgPos at, int colCount);
	DgSelMode Mode() const {
		return m_Mode;
	}
	// wholeRows: the rows across all the columns (the row numbers'), whatever the mode but Single
	void Reset(DgPos pos, int colCount, bool wholeRows = false);   // only this cell (Rows: its row)
	void Extend(DgPos pos, int colCount, bool wholeRows = false);  // the last range from the anchor to pos
	void Add(DgPos pos, int colCount, bool wholeRows = false);     // a new range (Ctrl+click); Single: as Reset
	void Toggle(DgPos pos, int colCount);                    // Rows: a selected row leaves the selection, else Add
	void SelectAll(int64_t rowCount, int colCount);
	void Clear() {
		m_Ranges.clear();
	}
	void Clamp(int64_t rowCount, int colCount);              // after rows or columns went away
	bool Contains(int64_t row, int col) const;
	bool RowTouched(int64_t row) const;                      // any of its cells
	bool ColTouched(int col) const;
	bool Empty() const {
		return m_Ranges.empty();
	}
	const std::vector<DgRange>& Ranges() const {
		return m_Ranges;
	}
	DgPos Anchor() const {
		return m_Anchor;
	}
	std::vector<std::pair<int64_t, int64_t>> Rows() const;   // the rows touched, as sorted, merged intervals
	std::vector<int> Cols() const;                           // the columns touched, sorted
	int64_t RowCount() const;                                // how many rows are touched

private:
	DgRange Single(DgPos pos, int colCount, bool wholeRows = false) const;
	DgSelMode m_Mode = DgSelMode::Cells;
	std::vector<DgRange> m_Ranges;
	DgPos m_Anchor;
};

// ---- Sorting ----------------------------------------------------------------------------------------

struct DgSortKey {
	int id = 0;
	bool descending = false;
	bool operator==(const DgSortKey&) const = default;
};

// A click on a title: unsorted, ascending, descending, unsorted again; add (Shift): the column joins the keys, or turns.
std::vector<DgSortKey> DgClickSort(const std::vector<DgSortKey>& keys, int id, bool add);

using DgTextCompare = int (*)(const std::wstring& a, const std::wstring& b);   // < 0, 0, > 0
int DgOrdinalCompare(const std::wstring& a, const std::wstring& b);

// Numbers (and check boxes) before dates before text, each in their order; empty cells go last however it's sorted.
int DgCompareValues(const DgValue& a, const DgValue& b, DgTextCompare compare);

// The data rows in sorted order, stable. value(row, key) gives the value to compare (nullptr: empty).
template<typename ValueOf>
std::vector<int64_t> DgSortOrder(int64_t count, const std::vector<bool>& descending, ValueOf value, DgTextCompare compare);

// ---- Copying ----------------------------------------------------------------------------------------

std::wstring DgTsvField(const std::wstring& text);              // quoted if it has a tab, a line break or a quote
std::wstring DgBuildTsv(const std::vector<std::vector<std::wstring>>& rows);   // CRLF after each row
std::wstring DgHtmlEscape(const std::wstring& text);
std::wstring DgBuildHtmlTable(const std::vector<std::vector<std::wstring>>& rows, bool headerRow);
std::string DgUtf8(const std::wstring& text);
std::string DgCfHtml(const std::string& fragmentUtf8);          // the clipboard's "HTML Format", with its offsets

// The selection as a table: the rows and the columns it touches, cells it doesn't left empty, titles first if wanted.
template<typename TextOf, typename TitleOf>
std::vector<std::vector<std::wstring>> DgSelectionTable(const DgSelection& selection, TextOf text, TitleOf title, bool titles);


// ---- Editing --------------------------------------------------------------------------------------------

// Text to a column's value: empty text is an empty cell; numbers take the user's separators; dates are ISO
// ("2026-11-17", "2026-11-17 13:05[:09]", a 'T' allowed), or what parseDate (the user's format) makes of them.
struct DgParsed {
	bool ok = false;
	DgValue value;
	std::wstring error;   // when not ok: what to enter
};
using DgDateParser = bool (*)(const std::wstring& text, int64_t& ticks);
DgParsed DgParseValue(const std::wstring& text, const DgColumn& column, const DgNumberFormat& nf, DgDateParser parseDate = nullptr);
bool DgParseIsoDateTime(const std::wstring& text, int64_t& ticks);

// The text an editor starts with (F2): numbers without grouping and with all their digits.
std::wstring DgEditText(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf);

// Tab-separated text (Excel's: quoted fields may hold tabs, line breaks and doubled quotes) to rows of fields; a
// last line break doesn't make an empty row.
std::vector<std::vector<std::wstring>> DgParseTsv(const std::wstring& text);

// Where pasted fields go: one field fills every selected cell; more go from the top-left of the last range, cut at
// the grid's edges. At most max cells.
struct DgPasteCell {
	int64_t row = 0;
	int col = 0;
	const std::wstring* text = nullptr;
};
std::vector<DgPasteCell> DgPasteTargets(const std::vector<std::vector<std::wstring>>& fields, const DgSelection& selection,
	int64_t rowCount, int colCount, size_t max);

// What a key does to an open editor. Typed: it was opened by typing, so the arrows commit and move (Excel's "enter"
// mode); after F2 they move in the text.
enum class DgEditAction { None, Cancel, CommitStay, CommitDown, CommitUp, CommitRight, CommitLeft };
DgEditAction DgEditKey(unsigned key, bool shift, bool typed);

// ---- Undo --------------------------------------------------------------------------------------------------

struct DgCellChange {
	int64_t row = 0;        // a data row
	int id = 0;
	DgCell before, after;
};

struct DgRowChange {
	int64_t row = 0;        // a data row
	std::vector<DgCell> cells;   // by slot: what a deleted row held
};

// A user's change: rows deleted, then rows inserted, then cells changed (done in that order, undone the other way).
// deleted: their indexes before the step, ascending; inserted: their indexes after it, ascending; the cells' rows are
// those after the rows changed.
struct DgEditStep {
	std::vector<DgCellChange> changes;
	int reason = 0;         // DGVCR_*
	std::vector<DgRowChange> inserted, deleted;
	bool HasRows() const {
		return !inserted.empty() || !deleted.empty();
	}
};

// A step's rows in the store: forward deletes then inserts (empty rows), backward takes the inserted ones out and
// puts the deleted ones back with their cells. The cells' changes are the caller's (after, before).
void DgApplyRowChanges(DgStore& store, const DgEditStep& step, bool forward);

// Where a data row is once a step's rows are done (forward) or undone: moved by the rows before it; a row taken out
// goes to the index it had (the next row's now).
int64_t DgRowAfter(int64_t row, const DgEditStep& step, bool forward);

// Steps done, undone and redone; a new step drops what was undone. Rows inserted or deleted by the app (their indexes
// move under the steps) clear it; the user's go through steps.
class DgUndoStack {
public:
	explicit DgUndoStack(size_t limit = 100) : m_Limit(limit) {
	}
	void Push(DgEditStep step);
	bool CanUndo() const {
		return m_Done > 0;
	}
	bool CanRedo() const {
		return m_Done < m_Steps.size();
	}
	const DgEditStep* Undo();   // the step to take back (apply its befores); nullptr if none
	const DgEditStep* Redo();   // the step to do again (apply its afters)
	void Clear() {
		m_Steps.clear();
		m_Done = 0;
	}
	size_t Count() const {
		return m_Steps.size();
	}

private:
	std::vector<DgEditStep> m_Steps;
	size_t m_Done = 0;
	size_t m_Limit;
};


// ---- Filtering, sorting and grouping: the rows in view -------------------------------------------------------

// The text a value shows, for "contains" and value filters (DataGridCtrl's gives dates in the user's format).
using DgValueTextFn = std::wstring (*)(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf);

struct DgFilter {
	int id = 0;                       // the column
	int kind = DgFilterKind::Contains;
	std::wstring text;                // Contains: anywhere in the shown text, case ignored; else what was typed, if it was
	DgValue min, max;                 // Range: both ends included, an empty end open; numbers or dates
	std::vector<std::wstring> values; // Values: the shown texts let through (sorted); typed (text set): lowercase, case ignored
	bool blanks = false;              // Values: empty cells too
};

std::wstring DgLower(std::wstring text);

// What's typed in the filter row: ">100", ">=", "<", "<=" and "a..b" (either end open) on numbers and dates give a
// range (a date without a time stands for its whole day; ">" and "<" leave the value out), "=x" the cells equal to x
// (a range for numbers and dates, else a value filter of x's shown text); anything else, or what doesn't parse, finds
// the text anywhere ("contains"). The filter keeps the typed text.
DgFilter DgParseFilterText(const std::wstring& text, const DgColumn& column, const DgNumberFormat& nf, DgDateParser parseDate = nullptr);

bool DgFilterMatch(const DgFilter& filter, const DgValue& value, const DgColumn& column, const DgNumberFormat& nf,
	DgValueTextFn text = nullptr);

struct DgAggregate {
	int id = 0;
	int kind = DgAggKind::None;
	DgValue value;                    // empty when nothing to aggregate
};

// A group of rows with the same value in a grouping column, inside its parent group (pre-order in DgViewResult).
struct DgGroup {
	int level = 0;                    // 0: grouped by the first column
	int id = 0;                       // the grouping column
	DgValue value;
	std::wstring key;                 // the path of values, to keep it collapsed across rebuilds
	int64_t count = 0;                // its rows
	size_t first = 0, end = 0;        // its rows in DgViewResult::order
	size_t subtreeEnd = 0;            // the index past its last subgroup in DgViewResult::groups
	std::vector<DgAggregate> aggregates;
};

struct DgViewInput {
	int64_t count = 0;                             // data rows
	std::vector<DgColumn> columns;                 // those the sort keys, filters, groups and aggregates name
	std::vector<std::vector<DgValue>> values;      // each column's values by data row (missing ones are empty)
	std::vector<DgSortKey> sortKeys;
	std::vector<DgFilter> filters;
	std::vector<int> groupIds;                     // outermost first
	DgNumberFormat nf;
	DgTextCompare compare = nullptr;
	DgValueTextFn text = nullptr;
};

struct DgViewResult {
	std::vector<int64_t> order;       // the data rows that pass the filters: sorted, grouped
	std::vector<DgGroup> groups;
	bool identity = false;            // nothing filtered, sorted or grouped: every row in order (order left empty)
	bool canceled = false;
};

// Filters, then sorts (by the group columns first, ascending unless a sort key gives their direction, then the
// sort keys), then groups, with each group's aggregates. A set cancel flag stops it (canceled).
DgViewResult DgBuildView(const DgViewInput& input, const std::atomic<bool>* cancel = nullptr);

// The entries of the rows in view (DgRowMap): each group's row, then its subgroups or rows, unless its key is collapsed.
std::vector<int64_t> DgFlatten(const DgViewResult& view, const std::unordered_set<std::wstring>& collapsed);

// The shown texts of a column's values, in the values' order, at most max (more: there were others); blanks: empty
// cells were seen.
template<typename ValueAt>
std::vector<std::wstring> DgDistinctTexts(int64_t count, ValueAt valueAt, const DgColumn& column, const DgNumberFormat& nf,
	DgValueTextFn text, size_t max, bool& blanks, bool& more);

// From a group's row, the nearest data row in a direction (+1, -1), else the other way; -1 if there's none.
template<typename IsGroup>
int64_t DgSkipGroupRows(int64_t row, int direction, int64_t count, IsGroup isGroup);

// The aggregate's text on a group's row ("Σ 1,234", "avg 3.50").
std::wstring DgAggregateText(const DgAggregate& aggregate, const DgColumn& column, const DgNumberFormat& nf, DgValueTextFn text = nullptr);

// ---- Finding ----------------------------------------------------------------------------------------------

namespace DgFindFlag {    // DGVFF_*
	enum : unsigned { MatchCase = 1, WholeCell = 2, Backward = 4, Column = 8, NoWrap = 0x10 };
}

// Whether a cell's shown text matches what's looked for (case ignored unless MatchCase; the whole text with WholeCell).
bool DgTextMatches(const std::wstring& text, const std::wstring& what, unsigned flags);

// From a cell, the next one (across, then down; Backward: the other way) that matches, among the columns first to last,
// wrapping past the end unless NoWrap; inclusive: the start cell counts too. At most maxRows rows are looked at, and
// none once stop() says so (asked before each row). Returns false if none; looked: the rows looked at.
template<typename Matches, typename Stop>
bool DgFindCell(DgPos start, bool inclusive, unsigned flags, int64_t rowCount, int firstCol, int lastCol, int64_t maxRows,
	Matches matches, DgPos& found, int64_t* looked, Stop stop);
template<typename Matches>
bool DgFindCell(DgPos start, bool inclusive, unsigned flags, int64_t rowCount, int firstCol, int lastCol, int64_t maxRows,
	Matches matches, DgPos& found, int64_t* looked = nullptr) {
	return DgFindCell(start, inclusive, flags, rowCount, firstCol, lastCol, maxRows, matches, found, looked, [] { return false; });
}

// ---- Conditional formats -----------------------------------------------------------------------------------

namespace DgCondOp {      // DGVCO_*
	enum : int { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, Between, Contains, Empty, NotEmpty };
}

constexpr uint32_t DgNoColor = 0xFF000000;   // CLR_DEFAULT: the property is left as it is

struct DgFormatRule {
	int id = 0;
	int column = 0;                   // whose value is tested
	int op = DgCondOp::Equal;
	DgValue a, b;                     // compared to (Between: both ends, included)
	bool wholeRow = false;            // the row's cells, not only the column's
	bool bold = false;
	uint32_t text = DgNoColor, back = DgNoColor;
};

// Numbers with numbers, dates with dates, text with text (case ignored); a text rule's value is compared to the shown
// text of a value of another kind (an enum's name). Contains: in the shown text, case ignored.
bool DgRuleMatches(const DgFormatRule& rule, const DgValue& value, const DgColumn& column, const DgNumberFormat& nf,
	DgValueTextFn text = nullptr);

struct DgCellLook {
	uint32_t text = DgNoColor, back = DgNoColor;
	bool bold = false;
	bool operator==(const DgCellLook&) const = default;
};

// A cell's looks from the rules that match, in their order: the first one to set a color wins. valueOf(id): the row's
// value in a column (nullptr: empty); columnOf(id): the column (nullptr: gone).
template<typename ValueOf, typename ColumnOf>
DgCellLook DgResolveLook(const std::vector<DgFormatRule>& rules, int columnId, ValueOf valueOf, ColumnOf columnOf,
	const DgNumberFormat& nf, DgValueTextFn text = nullptr);

// ---- Saving the state --------------------------------------------------------------------------------------

// What the user changes in how the grid shows its rows: the columns' order, widths and hiding, the frozen columns,
// the sort, the groups and the filters. Columns by ID.
struct DgState {
	struct Column {
		int id = 0;
		int width = 0;                // DIPs
		int fill = 0;
		bool hidden = false;
		bool operator==(const Column&) const = default;
	};
	std::vector<Column> columns;      // in display order
	int frozen = 0;
	std::vector<DgSortKey> sortKeys;
	std::vector<int> groupIds;
	std::vector<DgFilter> filters;
};

// One line of text ("dgv1;cols=1:120,2:80:f1,3:100:h;frozen=1;sort=2,3d;group=4;filter=..."), text escaped with %XXXX.
std::wstring DgSaveState(const DgState& state);
bool DgLoadState(const std::wstring& text, DgState& state);   // false if it isn't one (state untouched)

// ---- Templates ----------------------------------------------------------------------------------------

template<typename Matches, typename Stop>
bool DgFindCell(DgPos start, bool inclusive, unsigned flags, int64_t rowCount, int firstCol, int lastCol, int64_t maxRows,
	Matches matches, DgPos& found, int64_t* looked, Stop stop) {
	if (looked)
		*looked = 0;
	if (rowCount <= 0 || firstCol > lastCol)
		return false;
	bool back = (flags & DgFindFlag::Backward) != 0, wrap = !(flags & DgFindFlag::NoWrap);
	int64_t row = std::clamp<int64_t>(start.row, 0, rowCount - 1);
	int col = std::clamp(start.col, firstCol, lastCol);
	int step = back ? -1 : 1;
	int64_t rowsSeen = 0;
	// every cell once: the start's row is looked at again at the end for the cells before (after) the start
	for (int64_t n = 0; n <= rowCount && rowsSeen < maxRows && !stop(); n++) {
		int64_t r = row + step * n;
		if (r < 0 || r >= rowCount) {
			if (!wrap)
				break;
			r = (r % rowCount + rowCount) % rowCount;
		}
		rowsSeen++;
		int from = n == 0 ? col + (inclusive ? 0 : step) : back ? lastCol : firstCol;
		// the start's row again (wrapped): up to the start, which comes last when it didn't count first
		int to = n == rowCount ? col - (inclusive ? step : 0) : back ? firstCol : lastCol;
		for (int c = from; back ? c >= to : c <= to; c += step) {
			if (c < firstCol || c > lastCol)
				continue;
			if (matches(r, c)) {
				found = { r, c };
				if (looked)
					*looked = rowsSeen;
				return true;
			}
		}
	}
	if (looked)
		*looked = rowsSeen;
	return false;
}

template<typename ValueOf, typename ColumnOf>
DgCellLook DgResolveLook(const std::vector<DgFormatRule>& rules, int columnId, ValueOf valueOf, ColumnOf columnOf,
	const DgNumberFormat& nf, DgValueTextFn text) {
	DgCellLook look;
	static const DgValue empty;
	for (auto& rule : rules) {
		if (!rule.wholeRow && rule.column != columnId)
			continue;
		if (look.text != DgNoColor && look.back != DgNoColor && look.bold)
			break;   // nothing left to set
		const DgColumn* column = columnOf(rule.column);
		if (!column)
			continue;
		const DgValue* v = valueOf(rule.column);
		if (!DgRuleMatches(rule, v ? *v : empty, *column, nf, text))
			continue;
		if (look.text == DgNoColor)
			look.text = rule.text;
		if (look.back == DgNoColor)
			look.back = rule.back;
		look.bold = look.bold || rule.bold;
	}
	return look;
}


template<typename ValueOf>
std::vector<int64_t> DgSortOrder(int64_t count, const std::vector<bool>& descending, ValueOf value, DgTextCompare compare) {
	std::vector<int64_t> order((size_t)std::max<int64_t>(0, count));
	std::iota(order.begin(), order.end(), (int64_t)0);
	std::stable_sort(order.begin(), order.end(), [&](int64_t a, int64_t b) {
		for (size_t k = 0; k < descending.size(); k++) {
			const DgValue* va = value(a, k);
			const DgValue* vb = value(b, k);
			bool ea = !va || va->type == DgType::Empty, eb = !vb || vb->type == DgType::Empty;
			if (ea || eb) {
				if (ea != eb)
					return eb;   // empty last, either way
				continue;
			}
			int c = DgCompareValues(*va, *vb, compare);
			if (c != 0)
				return descending[k] ? c > 0 : c < 0;
		}
		return false;
	});
	return order;
}

template<typename TextOf, typename TitleOf>
std::vector<std::vector<std::wstring>> DgSelectionTable(const DgSelection& selection, TextOf text, TitleOf title, bool titles) {
	return DgSelectionTable(selection, [](int64_t) { return true; }, text, title, titles);
}

// keep(row): whether a row goes in (not a group's)
template<typename Keep, typename TextOf, typename TitleOf>
std::vector<std::vector<std::wstring>> DgSelectionTable(const DgSelection& selection, Keep keep, TextOf text, TitleOf title, bool titles) {
	std::vector<std::vector<std::wstring>> table;
	auto cols = selection.Cols();
	if (titles) {
		std::vector<std::wstring> row;
		for (int c : cols)
			row.push_back(title(c));
		table.push_back(std::move(row));
	}
	for (auto [first, last] : selection.Rows())
		for (int64_t r = first; r <= last; r++) {
			if (!keep(r))
				continue;
			std::vector<std::wstring> row;
			for (int c : cols)
				row.push_back(selection.Contains(r, c) ? text(r, c) : std::wstring());
			table.push_back(std::move(row));
		}
	return table;
}

template<typename ValueAt>
std::vector<std::wstring> DgDistinctTexts(int64_t count, ValueAt valueAt, const DgColumn& column, const DgNumberFormat& nf,
	DgValueTextFn text, size_t max, bool& blanks, bool& more) {
	blanks = more = false;
	std::unordered_map<std::wstring, DgValue> seen;   // a value of each text, to order them by
	for (int64_t row = 0; row < count; row++) {
		const DgValue* v = valueAt(row);
		if (!v || v->type == DgType::Empty) {
			blanks = true;
			continue;
		}
		auto shown = text ? text(*v, column, nf) : DgFormat(*v, column, nf);
		if (seen.count(shown))
			continue;
		if (seen.size() >= max) {
			more = true;
			continue;
		}
		seen.emplace(std::move(shown), *v);
	}
	std::vector<std::pair<std::wstring, DgValue>> list(seen.begin(), seen.end());
	std::sort(list.begin(), list.end(), [](auto& a, auto& b) {
		int c = DgCompareValues(a.second, b.second, nullptr);
		return c != 0 ? c < 0 : a.first < b.first;
	});
	std::vector<std::wstring> texts;
	for (auto& [t, v] : list)
		texts.push_back(t);
	return texts;
}

template<typename IsGroup>
int64_t DgSkipGroupRows(int64_t row, int direction, int64_t count, IsGroup isGroup) {
	if (count <= 0)
		return -1;
	row = std::clamp<int64_t>(row, 0, count - 1);
	for (int pass = 0; pass < 2; pass++, direction = -direction)
		for (int64_t r = row; r >= 0 && r < count; r += direction)
			if (!isGroup(r))
				return r;
	return -1;
}
