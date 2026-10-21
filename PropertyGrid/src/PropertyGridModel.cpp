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

// ---- Parsing ----------------------------------------------------------------------

UINT PgEditType(const PgNode& node) {
	if (node.kind != PRGK_CUSTOM)
		return PgValueTypeOf(node.kind);
	if (node.value.value.type != PRGVT_EMPTY)
		return node.value.value.type;
	if (node.defValue.value.type != PRGVT_EMPTY)
		return node.defValue.value.type;
	return PRGVT_STRING;
}

std::wstring PgEditText(const PgNode& node) {
	auto& v = node.value.value;
	if (v.type == PRGVT_STRING)
		return v.pszVal ? v.pszVal : L"";
	return PgFormatValue(node);
}

static std::wstring Trim(const std::wstring& s) {
	auto first = s.find_first_not_of(L" \t");
	if (first == s.npos)
		return {};
	return s.substr(first, s.find_last_not_of(L" \t") - first + 1);
}

static std::vector<std::wstring> Split(const std::wstring& s, const wchar_t* separators) {
	std::vector<std::wstring> parts;
	size_t start = 0;
	for (;;) {
		auto pos = s.find_first_of(separators, start);
		parts.push_back(Trim(s.substr(start, pos == s.npos ? s.npos : pos - start)));
		if (pos == s.npos)
			return parts;
		start = pos + 1;
	}
}

static bool ToAscii(const std::wstring& s, std::string& out) {
	out.clear();
	out.reserve(s.size());
	for (auto c : s) {
		if (c > 127)
			return false;
		out += (char)c;
	}
	return true;
}

// decimal, or hex with 0x (or bare with allowBareHex), signed; positive hex may use all 64 bits,
// as -1 is shown 0xFFFFFFFFFFFFFFFF
static bool ParseInt(const std::wstring& text, bool allowBareHex, LONGLONG& value) {
	std::string s;
	if (!ToAscii(text, s) || s.empty())
		return false;
	size_t i = 0;
	bool negative = false;
	if (s[0] == '+' || s[0] == '-') {
		negative = s[0] == '-';
		i = 1;
	}
	int base = allowBareHex ? 16 : 10;
	if (s.size() - i > 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
		base = 16;
		i += 2;
	}
	if (i == s.size())
		return false;
	ULONGLONG magnitude;
	auto [ptr, ec] = std::from_chars(s.data() + i, s.data() + s.size(), magnitude, base);
	if (ec != std::errc() || ptr != s.data() + s.size())
		return false;
	if (negative) {
		if (magnitude > (ULONGLONG)LLONG_MAX + 1)
			return false;
		value = (LONGLONG)(0 - magnitude);
	}
	else {
		if (base == 10 && magnitude > (ULONGLONG)LLONG_MAX)
			return false;
		value = (LONGLONG)magnitude;
	}
	return true;
}

static bool ParseReal(const std::wstring& text, double& value) {
	std::string s;
	if (!ToAscii(text, s) || s.empty())
		return false;
	auto first = s.data() + (s[0] == '+' ? 1 : 0);
	auto [ptr, ec] = std::from_chars(first, s.data() + s.size(), value);
	return ec == std::errc() && ptr == s.data() + s.size();
}

static bool ParseColor(const std::wstring& text, COLORREF& color) {
	auto s = text[0] == L'#' ? text.substr(1) : text;
	if (s.size() == 6 && s.find_first_not_of(L"0123456789abcdefABCDEF") == s.npos) {
		auto rgb = wcstoul(s.c_str(), nullptr, 16);
		color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
		return true;
	}
	auto parts = Split(text, L",");
	if (parts.size() != 3)
		return false;
	LONGLONG c[3];
	for (int i = 0; i < 3; i++)
		if (!ParseInt(parts[i], false, c[i]) || c[i] < 0 || c[i] > 255)
			return false;
	color = RGB(c[0], c[1], c[2]);
	return true;
}

static const PgEnum* FindEnum(const PgNode& node, const std::wstring& name) {
	for (auto& e : node.enumItems)
		if (_wcsicmp(e.name.c_str(), name.c_str()) == 0)
			return &e;
	return nullptr;
}

static std::wstring RangeMessage(const PgNode& node) {
	bool hex = (node.flags & PRGF_HEX) != 0;
	if (node.minVal != LLONG_MIN && node.maxVal != LLONG_MAX)
		return L"Enter a number from " + FormatInt(node.minVal, hex) + L" to " + FormatInt(node.maxVal, hex) + L".";
	if (node.minVal != LLONG_MIN)
		return L"Enter a number of at least " + FormatInt(node.minVal, hex) + L".";
	return L"Enter a number of at most " + FormatInt(node.maxVal, hex) + L".";
}

bool PgParseValue(const PgNode& node, const std::wstring& text, PgStoredValue& value, std::wstring& error) {
	auto s = Trim(text);
	bool hex = (node.flags & PRGF_HEX) != 0;
	switch (PgEditType(node)) {
		case PRGVT_STRING:
			value.Assign(PrgMakeString(text.c_str()));   // as typed, spaces included
			return true;

		case PRGVT_INT: {
			LONGLONG v = 0;
			if (node.kind == PRGK_ENUM) {
				if (auto e = FindEnum(node, s))
					v = e->value;
				else if (!ParseInt(s, hex, v) || ((node.flags & PRGF_EDITABLE) == 0
					&& std::none_of(node.enumItems.begin(), node.enumItems.end(), [=](auto& e) { return e.value == v; }))) {
					error = L"\"" + s + L"\" is not one of the values.";
					return false;
				}
			}
			else if (node.kind == PRGK_FLAGS) {
				for (auto& part : Split(s, L"|,")) {
					if (part.empty())
						continue;
					LONGLONG bits;
					if (auto e = FindEnum(node, part))
						bits = e->value;
					else if (!ParseInt(part, hex, bits)) {
						error = L"\"" + part + L"\" is not a known flag.";
						return false;
					}
					v |= bits;
				}
			}
			else {
				if (!ParseInt(s, hex, v)) {
					error = L"\"" + s + L"\" is not a valid number.";
					return false;
				}
				if (v < node.minVal || v > node.maxVal) {
					error = RangeMessage(node);
					return false;
				}
			}
			value.Assign(PrgMakeInt(v));
			return true;
		}

		case PRGVT_REAL: {
			double v;
			if (!ParseReal(s, v)) {
				error = L"\"" + s + L"\" is not a valid number.";
				return false;
			}
			value.Assign(PrgMakeReal(v));
			return true;
		}

		case PRGVT_BOOL: {
			static const wchar_t* const yes[] = { L"true", L"yes", L"on", L"1" };
			static const wchar_t* const no[] = { L"false", L"no", L"off", L"0" };
			for (int i = 0; i < 4; i++) {
				if (_wcsicmp(s.c_str(), yes[i]) == 0 || _wcsicmp(s.c_str(), no[i]) == 0) {
					value.Assign(PrgMakeBool(_wcsicmp(s.c_str(), yes[i]) == 0));
					return true;
				}
			}
			error = L"Enter True or False.";
			return false;
		}

		case PRGVT_COLOR: {
			COLORREF color;
			if (s.empty() || !ParseColor(s, color)) {
				error = L"Enter a color as #RRGGBB or R, G, B.";
				return false;
			}
			value.Assign(PrgMakeColor(color));
			return true;
		}
	}
	error = L"This value can't be typed in.";
	return false;
}
