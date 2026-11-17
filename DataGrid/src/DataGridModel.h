#pragma once

// The DataGrid's window-free parts, covered by DataGridTests: the columns and their order, laying them out (fill
// columns, frozen ones), scrolling (rows whole, mapped onto a 32-bit scroll bar), hit testing, moving the caret with
// the keys, formatting values, the store, the cache of a virtual grid's cells and the map from rows in view to rows
// in the data. The values here mirror DataGrid.h's (DGVVT_*, DGVCT_*, DGVA_*, DGVCF_*, DGVHT_*); DataGridCtrl.cpp
// checks that they agree.

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <unordered_map>
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
	enum : unsigned { Hidden = 1, NoResize = 2, NoReorder = 4, GroupDigits = 8, NoSort = 16 };
}
namespace DgHitFlag {     // DGVHT_*
	enum : unsigned { Nowhere = 0, Cell = 1, Header = 2, HeaderDivider = 4, RowHeader = 8, Corner = 0x10, Empty = 0x20 };
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
	int header = 0;         // the header's height (0: none)
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
	int header = 0;
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

// The identity until an order is set (sorting and filtering, later); then the rows in view are those in the order.
class DgRowMap {
public:
	void Reset(int64_t count);
	void SetOrder(std::vector<int64_t> order, int64_t dataCount);
	int64_t Count() const {
		return m_Identity ? m_Count : (int64_t)m_Order.size();
	}
	bool Identity() const {
		return m_Identity;
	}
	int64_t ToData(int64_t view) const;           // -1 if out of range
	int64_t ToView(int64_t data) const;           // -1 if not in view

private:
	bool m_Identity = true;
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

// ---- Templates ----------------------------------------------------------------------------------------


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
			std::vector<std::wstring> row;
			for (int c : cols)
				row.push_back(selection.Contains(r, c) ? text(r, c) : std::wstring());
			table.push_back(std::move(row));
		}
	return table;
}
