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

// One line of the grid.
struct PgRow {
	PgNode* node;
	int     level;   // properties right below a category (or at the top) are level 0
};

struct PgModel {
	PgNode root;     // sentinel: its children are the top-level items

	PgNode* Insert(PgNode* parent, const PRGITEMDESC& desc, PgNode* after);
	bool    Delete(PgNode* node);
	void    Clear();
	PgNode* Find(LPCWSTR path, PgNode* start) const;
	int     Count() const;

	// the visible rows for a view (PRGV_*): hidden items and the children of collapsed items left out,
	// categories without a visible property left out
	void    BuildRows(UINT view, std::vector<PgRow>& rows) const;
};

// the value type a kind holds (PRGK_CUSTOM: PRGVT_EMPTY, meaning any)
UINT PgValueTypeOf(UINT kind);
bool PgValueFits(UINT kind, const PRGVALUE& value);
bool PgValuesEqual(const PRGVALUE& a, const PRGVALUE& b);
bool PgIsAncestor(const PgNode* ancestor, const PgNode* node);

// the display text of a node's value (or of another value formatted the node's way)
std::wstring PgFormatValue(const PgNode& node);
std::wstring PgFormatValue(const PgNode& node, const PRGVALUE& value);
