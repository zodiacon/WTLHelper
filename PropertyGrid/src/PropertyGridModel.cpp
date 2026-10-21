#include "PropertyGridModel.h"

#include <algorithm>
#include <charconv>
#include <cmath>

// ---- Values -------------------------------------------------------------------

void PgStoredValue::Assign(const PRGVALUE& v) {
	if (v.type == PRGVT_STRING) {
		std::wstring s = v.pszVal ? v.pszVal : L"";   // copied first: v may point into text
		text = std::move(s);
		value = {};
		value.type = PRGVT_STRING;
		value.pszVal = text.c_str();
	}
	else {
		value = v;
		text.clear();
	}
}

UINT PgValueTypeOf(UINT kind) {
	switch (kind) {
		case PRGK_STRING:
		case PRGK_PATH:     return PRGVT_STRING;
		case PRGK_INTEGER:
		case PRGK_ENUM:
		case PRGK_FLAGS:    return PRGVT_INT;
		case PRGK_REAL:     return PRGVT_REAL;
		case PRGK_BOOL:     return PRGVT_BOOL;
		case PRGK_COLOR:    return PRGVT_COLOR;
		case PRGK_FONT:     return PRGVT_FONT;
		case PRGK_DATETIME: return PRGVT_DATETIME;
	}
	return PRGVT_EMPTY;
}

bool PgValueFits(UINT kind, const PRGVALUE& value) {
	if (value.type == PRGVT_EMPTY)
		return true;
	if (value.type > PRGVT_DATETIME || kind == PRGK_CATEGORY)
		return false;
	return kind == PRGK_CUSTOM || value.type == PgValueTypeOf(kind);
}

bool PgValuesEqual(const PRGVALUE& a, const PRGVALUE& b) {
	if (a.type != b.type)
		return false;
	switch (a.type) {
		case PRGVT_EMPTY:    return true;
		case PRGVT_STRING:   return wcscmp(a.pszVal ? a.pszVal : L"", b.pszVal ? b.pszVal : L"") == 0;
		case PRGVT_INT:      return a.llVal == b.llVal;
		case PRGVT_REAL:     return a.dblVal == b.dblVal;
		case PRGVT_BOOL:     return !a.boolVal == !b.boolVal;
		case PRGVT_COLOR:    return a.clrVal == b.clrVal;
		case PRGVT_DATETIME: return CompareFileTime(&a.ftVal, &b.ftVal) == 0;
		case PRGVT_FONT: {
			// field by field: the bytes after the face name's terminator are undefined
			auto& x = a.lfVal;
			auto& y = b.lfVal;
			return x.lfHeight == y.lfHeight && x.lfWidth == y.lfWidth && x.lfEscapement == y.lfEscapement
				&& x.lfOrientation == y.lfOrientation && x.lfWeight == y.lfWeight && x.lfItalic == y.lfItalic
				&& x.lfUnderline == y.lfUnderline && x.lfStrikeOut == y.lfStrikeOut && x.lfCharSet == y.lfCharSet
				&& x.lfOutPrecision == y.lfOutPrecision && x.lfClipPrecision == y.lfClipPrecision
				&& x.lfQuality == y.lfQuality && x.lfPitchAndFamily == y.lfPitchAndFamily
				&& _wcsnicmp(x.lfFaceName, y.lfFaceName, LF_FACESIZE) == 0;
		}
	}
	return false;
}

bool PgIsAncestor(const PgNode* ancestor, const PgNode* node) {
	for (auto n = node ? node->parent : nullptr; n; n = n->parent)
		if (n == ancestor)
			return true;
	return false;
}

// ---- Tree -----------------------------------------------------------------------

PgNode* PgModel::Insert(PgNode* parent, const PRGITEMDESC& desc, PgNode* after) {
	if (!parent)
		parent = &root;
	if (desc.kind > PRGK_CUSTOM || !desc.pszName)
		return nullptr;
	if (desc.kind == PRGK_CATEGORY && parent != &root)
		return nullptr;
	if (!PgValueFits(desc.kind, desc.value) || !PgValueFits(desc.kind, desc.defValue))
		return nullptr;
	if (desc.enumCount < 0 || (desc.enumCount > 0 && !desc.enumItems))
		return nullptr;

	auto& siblings = parent->children;
	size_t pos;
	if (after == PRG_LAST)
		pos = siblings.size();
	else if (after == PRG_FIRST)
		pos = 0;
	else {
		auto it = std::find_if(siblings.begin(), siblings.end(), [=](auto& c) { return c.get() == after; });
		if (it == siblings.end())
			return nullptr;
		pos = it - siblings.begin() + 1;
	}

	auto node = std::make_unique<PgNode>();
	node->kind = desc.kind;
	node->name = desc.pszName;
	if (desc.pszDescription)
		node->description = desc.pszDescription;
	node->value.Assign(desc.value);
	node->defValue.Assign(desc.defValue);
	node->flags = desc.flags;
	node->lParam = desc.lParam;
	node->minVal = desc.minVal;
	node->maxVal = desc.maxVal;
	node->precision = desc.precision;
	node->enumItems.reserve(desc.enumCount);
	for (int i = 0; i < desc.enumCount; i++)
		node->enumItems.push_back({ desc.enumItems[i].pszName ? desc.enumItems[i].pszName : L"", desc.enumItems[i].value });
	if (desc.pszFilter) {
		// up to the double terminator
		auto p = desc.pszFilter;
		while (*p)
			p += wcslen(p) + 1;
		node->filter.assign(desc.pszFilter, p - desc.pszFilter);
	}
	node->expanded = (desc.flags & PRGF_COLLAPSED) == 0;
	node->parent = parent;

	auto result = node.get();
	siblings.insert(siblings.begin() + pos, std::move(node));
	return result;
}

bool PgModel::Delete(PgNode* node) {
	if (!node || node == &root || !node->parent)
		return false;
	auto& siblings = node->parent->children;
	auto it = std::find_if(siblings.begin(), siblings.end(), [=](auto& c) { return c.get() == node; });
	if (it == siblings.end())
		return false;
	siblings.erase(it);
	return true;
}

void PgModel::Clear() {
	root.children.clear();
}

PgNode* PgModel::Find(LPCWSTR path, PgNode* start) const {
	if (!path)
		return nullptr;
	auto node = start ? start : const_cast<PgNode*>(&root);
	std::wstring_view rest(path);
	bool found = false;
	while (!rest.empty()) {
		auto slash = rest.find(L'/');
		auto name = rest.substr(0, slash);
		rest = slash == rest.npos ? std::wstring_view() : rest.substr(slash + 1);
		if (name.empty())
			continue;
		auto it = std::find_if(node->children.begin(), node->children.end(), [&](auto& c) {
			return c->name.size() == name.size() && _wcsnicmp(c->name.c_str(), name.data(), name.size()) == 0;
			});
		if (it == node->children.end())
			return nullptr;
		node = it->get();
		found = true;
	}
	return found ? node : nullptr;
}

static int CountNodes(const PgNode& node) {
	int count = 0;
	for (auto& c : node.children)
		count += 1 + CountNodes(*c);
	return count;
}

int PgModel::Count() const {
	return CountNodes(root);
}

// ---- Rows -----------------------------------------------------------------------

static void AddWithChildren(const PgNode* node, int level, std::vector<PgRow>& rows) {
	rows.push_back({ const_cast<PgNode*>(node), level });
	if (!node->expanded)
		return;
	for (auto& c : node->children)
		if ((c->flags & PRGF_HIDDEN) == 0)
			AddWithChildren(c.get(), level + 1, rows);
}

static bool IsShown(const PgNode* node) {
	return (node->flags & PRGF_HIDDEN) == 0;
}

void PgModel::BuildRows(UINT view, std::vector<PgRow>& rows) const {
	rows.clear();
	if (view == PRGV_ALPHABETICAL) {
		std::vector<const PgNode*> props;
		for (auto& c : root.children) {
			if (!IsShown(c.get()))
				continue;
			if (c->IsCategory()) {
				for (auto& p : c->children)
					if (IsShown(p.get()))
						props.push_back(p.get());
			}
			else
				props.push_back(c.get());
		}
		std::stable_sort(props.begin(), props.end(), [](auto a, auto b) {
			return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
				a->name.c_str(), (int)a->name.size(), b->name.c_str(), (int)b->name.size(), nullptr, nullptr, 0) == CSTR_LESS_THAN;
			});
		for (auto p : props)
			AddWithChildren(p, 0, rows);
		return;
	}

	// properties outside any category first, so they don't look like part of the category above them
	for (auto& c : root.children)
		if (!c->IsCategory() && IsShown(c.get()))
			AddWithChildren(c.get(), 0, rows);
	for (auto& c : root.children) {
		if (!c->IsCategory() || !IsShown(c.get()))
			continue;
		if (std::none_of(c->children.begin(), c->children.end(), [](auto& p) { return IsShown(p.get()); }))
			continue;
		rows.push_back({ c.get(), 0 });
		if (c->expanded)
			for (auto& p : c->children)
				if (IsShown(p.get()))
					AddWithChildren(p.get(), 0, rows);
	}
}

// ---- Formatting -----------------------------------------------------------------

static std::wstring Widen(const char* first, const char* last) {
	return std::wstring(first, last);
}

static std::wstring FormatInt(LONGLONG value, bool hex) {
	wchar_t buf[32];
	if (hex)
		swprintf_s(buf, L"0x%llX", (ULONGLONG)value);
	else
		swprintf_s(buf, L"%lld", value);
	return buf;
}

static std::wstring FormatReal(double value, int precision) {
	char buf[400];
	auto result = precision < 0
		? std::to_chars(buf, buf + sizeof(buf), value)
		: std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, precision);
	return Widen(buf, result.ptr);
}

static std::wstring FormatFlags(const PgNode& node, LONGLONG value) {
	bool hex = (node.flags & PRGF_HEX) != 0;
	if (value == 0) {
		for (auto& e : node.enumItems)
			if (e.value == 0)
				return e.name;
		return FormatInt(0, hex);
	}
	std::wstring text;
	auto rest = value;
	for (auto& e : node.enumItems) {
		if (e.value == 0 || (value & e.value) != e.value)
			continue;
		if (!text.empty())
			text += L" | ";
		text += e.name;
		rest &= ~e.value;
	}
	if (rest) {
		// bits no item names: always hex, they're a mask
		if (!text.empty())
			text += L" | ";
		text += FormatInt(rest, true);
	}
	return text;
}

static std::wstring FormatFont(const LOGFONTW& lf) {
	auto height = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
	auto points = std::round(height * 72.0 / 96 * 100) / 100;
	std::wstring text(lf.lfFaceName, wcsnlen(lf.lfFaceName, LF_FACESIZE));
	text += L", " + FormatReal(points, -1) + L"pt";
	if (lf.lfWeight >= FW_BOLD)
		text += L", Bold";
	if (lf.lfItalic)
		text += L", Italic";
	return text;
}

static std::wstring FormatDateTime(const FILETIME& ft, bool dateOnly) {
	SYSTEMTIME utc, local;
	if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
		return {};
	wchar_t buf[128];
	if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, buf, _countof(buf), nullptr))
		return {};
	std::wstring text = buf;
	if (!dateOnly && GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, nullptr, buf, _countof(buf)))
		text += L" " + std::wstring(buf);
	return text;
}

std::wstring PgFormatValue(const PgNode& node, const PRGVALUE& value) {
	bool hex = (node.flags & PRGF_HEX) != 0;
	switch (value.type) {
		case PRGVT_STRING: {
			std::wstring text = value.pszVal ? value.pszVal : L"";
			if (node.flags & PRGF_PASSWORD)
				return std::wstring(text.size(), L'\x25CF');
			return text;
		}
		case PRGVT_INT:
			if (node.kind == PRGK_FLAGS)
				return FormatFlags(node, value.llVal);
			if (node.kind == PRGK_ENUM)
				for (auto& e : node.enumItems)
					if (e.value == value.llVal)
						return e.name;
			return FormatInt(value.llVal, hex);
		case PRGVT_REAL:
			return FormatReal(value.dblVal, node.precision);
		case PRGVT_BOOL:
			return value.boolVal ? L"True" : L"False";
		case PRGVT_COLOR: {
			wchar_t buf[16];
			swprintf_s(buf, L"#%02X%02X%02X", GetRValue(value.clrVal), GetGValue(value.clrVal), GetBValue(value.clrVal));
			return buf;
		}
		case PRGVT_FONT:
			return FormatFont(value.lfVal);
		case PRGVT_DATETIME:
			return FormatDateTime(value.ftVal, (node.flags & PRGF_DATEONLY) != 0);
	}
	return {};
}

std::wstring PgFormatValue(const PgNode& node) {
	return PgFormatValue(node, node.value.value);
}
