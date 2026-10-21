#pragma once

// The property grid's data: the item tree, the rows a view shows and value formatting.
// No window here, so PropertyGridTests can cover it.

#include "PropertyGrid.h"

#include <string>
#include <vector>
#include <memory>

struct PgEnum {
	std::wstring name;
	LONGLONG     value;
};

// A value with its own string storage: value.pszVal points into text for PRGVT_STRING.
struct PgStoredValue {
	PRGVALUE      value{};
	std::wstring text;

	PgStoredValue() = default;
	PgStoredValue(const PgStoredValue&) = delete;
	PgStoredValue& operator=(const PgStoredValue&) = delete;

	void Assign(const PRGVALUE& v);
};

struct PgNode {
	UINT         kind = PRGK_STRING;
	std::wstring name;
	std::wstring description;
	PgStoredValue value;
	PgStoredValue defValue;
	UINT         flags = 0;
	LPARAM       lParam = 0;
	LONGLONG     minVal = LLONG_MIN;
	LONGLONG     maxVal = LLONG_MAX;
	int          precision = -1;
	std::vector<PgEnum> enumItems;
	std::wstring filter;          // Path: '\0' separated, as given (without the final terminator)
	bool         expanded = true;

	PgNode*      parent = nullptr;
	std::vector<std::unique_ptr<PgNode>> children;

	bool IsCategory() const { return kind == PRGK_CATEGORY; }
};

// One line of the grid: an item, or one flag of a Flags item (a check box below it).
struct PgRow {
	PgNode* node;
	int     level;     // properties right below a category (or at the top) are level 0
	int     flag = -1; // >= 0: the row of node->enumItems[flag]
};

struct PgModel {
	PgNode root;     // sentinel: its children are the top-level items

	PgNode* Insert(PgNode* parent, const PRGITEMDESC& desc, PgNode* after);
	bool    Delete(PgNode* node);
	void    Clear();
	PgNode* Find(LPCWSTR path, PgNode* start) const;
	int     Count() const;

	// the visible rows for a view (PRGV_*): hidden items and the children of collapsed items left out,
	// categories without a visible property left out. With a filter: only the items (and flags) whose name or
	// shown value contains it, case-insensitively, and the items above them, expanded; or all of a category
	// whose name contains it.
	void    BuildRows(UINT view, std::vector<PgRow>& rows, const std::wstring& filter = {}) const;
};

// the value type a kind holds (PRGK_CUSTOM: PRGVT_EMPTY, meaning any)
UINT PgValueTypeOf(UINT kind);
bool PgValueFits(UINT kind, const PRGVALUE& value);
bool PgValuesEqual(const PRGVALUE& a, const PRGVALUE& b);
bool PgIsAncestor(const PgNode* ancestor, const PgNode* node);
bool PgContains(const std::wstring& text, const std::wstring& part);   // case-insensitive
bool PgMatches(const PgNode* node, const std::wstring& filter);        // name or shown value
// rows below the item when expanded: shown children, or a Flags item's flags (those with nonzero values)
bool PgHasSubRows(const PgNode* node);
bool PgIsFlagSet(LONGLONG mask, LONGLONG flag);   // all of its bits
LONGLONG PgSetFlag(LONGLONG mask, LONGLONG flag, bool set);

// the display text of a node's value (or of another value formatted the node's way)
std::wstring PgFormatValue(const PgNode& node);
std::wstring PgFormatValue(const PgNode& node, const PRGVALUE& value);

// the value type an edit of the node produces: the kind's type, or for PRGK_CUSTOM the type of its value
// (or default), else PRGVT_STRING
UINT PgEditType(const PgNode& node);
// the text an editor starts with: the display text, but a password unmasked
std::wstring PgEditText(const PgNode& node);
// text typed by the user to a value of PgEditType(node), range checked; false with a message on bad input
bool PgParseValue(const PgNode& node, const std::wstring& text, PgStoredValue& value, std::wstring& error);
