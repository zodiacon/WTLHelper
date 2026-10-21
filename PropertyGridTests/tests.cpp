// Unit tests for the PropertyGrid model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the item tree, the rows of each view, value storage,
// comparison and formatting. Anything that needs an HWND is checked by hand with PropertyGridDemo.

#include "PropertyGridModel.h"

#include <cstdio>
#include <string>

// ---- Tiny harness ------------------------------------------------------------

static int g_checks = 0;
static int g_failures = 0;
static const wchar_t* g_currentTest = L"";

static void Check(bool ok, const char* expr, int line) {
	g_checks++;
	if (!ok) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
	}
}

static void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got:  \"%s\"\n        want: \"%s\"\n", g_currentTest, line, expr, actual.c_str(), expected.c_str());
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
#define CHECK_STR(a, b)  CheckStr((a), (b), #a " == " #b, __LINE__)

#define TEST(name)															\
	static void name();														\
	static void Run_##name() {												\
		g_currentTest = L## #name;											\
		const int before = g_failures;										\
		name();																\
		wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name);	\
	}																		\
	static void name()

// ---- Helpers --------------------------------------------------------------------

static PgNode* Add(PgModel& m, PgNode* parent, LPCWSTR name, UINT kind = PRGK_STRING, UINT flags = 0) {
	PRGITEMDESC d;
	d.kind = kind;
	d.pszName = name;
	d.flags = flags;
	return m.Insert(parent, d, PRG_LAST);
}

static PgNode* AddCat(PgModel& m, LPCWSTR name, UINT flags = 0) {
	return Add(m, nullptr, name, PRGK_CATEGORY, flags);
}

// rows as "name/level" joined by spaces
static std::wstring Rows(const PgModel& m, UINT view, const std::wstring& filter = {}) {
	std::vector<PgRow> rows;
	m.BuildRows(view, rows, filter);
	std::wstring text;
	for (auto& r : rows) {
		if (!text.empty())
			text += L' ';
		// a flag row shows as [flag]
		auto name = r.flag >= 0 ? L"[" + r.node->enumItems[r.flag].name + L"]" : r.node->name;
		text += name + L'/' + std::to_wstring(r.level);
	}
	return text;
}

static std::wstring Format(UINT kind, const PRGVALUE& v, UINT flags = 0, int precision = -1,
	std::vector<PgEnum> items = {}) {
	PgNode node;
	node.kind = kind;
	node.flags = flags;
	node.precision = precision;
	node.enumItems = std::move(items);
	return PgFormatValue(node, v);
}

// ---- Tree ---------------------------------------------------------------------------

TEST(Insert_RejectsInvalidDescriptions) {
	PgModel m;
	auto cat = AddCat(m, L"Cat");
	CHECK(cat != nullptr);
	CHECK(Add(m, cat, L"Nested", PRGK_CATEGORY) == nullptr);          // categories only at the top
	CHECK(Add(m, cat, nullptr) == nullptr);                          // no name

	PRGITEMDESC d;
	d.kind = PRGK_INTEGER;
	d.pszName = L"Wrong";
	d.value = PrgMakeString(L"text");
	CHECK(m.Insert(cat, d, PRG_LAST) == nullptr);                     // value type doesn't fit
	d.value = PrgMakeInt(1);
	d.defValue = PrgMakeReal(1);
	CHECK(m.Insert(cat, d, PRG_LAST) == nullptr);                     // default doesn't fit either
	d.defValue = {};
	d.kind = 99;
	CHECK(m.Insert(cat, d, PRG_LAST) == nullptr);                     // unknown kind

	auto other = AddCat(m, L"Other");
	auto p = Add(m, other, L"P");
	d.kind = PRGK_INTEGER;
	CHECK(m.Insert(cat, d, p) == nullptr);                           // insertAfter isn't a child of the parent
	CHECK(m.Insert(cat, d, p) == nullptr);
	CHECK(m.Count() == 3);
}

TEST(Insert_FirstLastAndAfter) {
	PgModel m;
	auto cat = AddCat(m, L"Cat");
	auto b = Add(m, cat, L"B");
	PRGITEMDESC d;
	d.pszName = L"A";
	m.Insert(cat, d, PRG_FIRST);
	d.pszName = L"C";
	m.Insert(cat, d, b);
	d.pszName = L"D";
	m.Insert(cat, d, PRG_LAST);
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"Cat/0 A/0 B/0 C/0 D/0");
}

TEST(Insert_CopiesEverything) {
	PgModel m;
	wchar_t name[] = L"Name";
	wchar_t text[] = L"value";
	PRGENUMITEM items[] = { { L"One", 1 }, { L"Two", 2 } };
	PRGITEMDESC d;
	d.kind = PRGK_ENUM;
	d.pszName = name;
	d.value = PrgMakeInt(2);
	d.enumItems = items;
	d.enumCount = 2;
	d.pszFilter = L"Text\0*.txt\0All\0*.*\0";
	auto n = m.Insert(nullptr, d, PRG_LAST);
	name[0] = L'X';
	CHECK_STR(n->name, L"Name");
	CHECK(n->enumItems.size() == 2 && n->enumItems[1].name == L"Two");
	CHECK(n->filter == std::wstring(L"Text\0*.txt\0All\0*.*", 19));

	d.kind = PRGK_STRING;
	d.value = PrgMakeString(text);
	d.enumItems = nullptr;
	d.enumCount = 0;
	auto s = m.Insert(nullptr, d, PRG_LAST);
	text[0] = L'X';
	CHECK_STR(s->value.value.pszVal, L"value");
}

TEST(StoredValue_AssignFromItself) {
	PgStoredValue v;
	v.Assign(PrgMakeString(L"some text long enough to be on the heap"));
	v.Assign(v.value);
	CHECK_STR(v.value.pszVal, L"some text long enough to be on the heap");
	v.Assign(PrgMakeString(nullptr));
	CHECK(v.value.type == PRGVT_STRING);
	CHECK_STR(v.value.pszVal, L"");
	v.Assign(PrgMakeInt(5));
	CHECK(v.value.type == PRGVT_INT && v.value.llVal == 5);
}

TEST(Delete_RemovesTheSubtree) {
	PgModel m;
	auto cat = AddCat(m, L"Cat");
	auto p = Add(m, cat, L"P");
	Add(m, p, L"Sub1");
	Add(m, p, L"Sub2");
	Add(m, cat, L"Q");
	CHECK(m.Count() == 5);
	CHECK(m.Delete(p));
	CHECK(m.Count() == 2);
	CHECK(!m.Delete(&m.root));
	CHECK(!m.Delete(nullptr));
	m.Clear();
	CHECK(m.Count() == 0);
}

TEST(Find_ByPath) {
	PgModel m;
	auto cat = AddCat(m, L"Appearance");
	auto font = Add(m, cat, L"Font", PRGK_FONT);
	auto size = Add(m, font, L"Size", PRGK_REAL);
	CHECK(m.Find(L"Appearance", nullptr) == cat);
	CHECK(m.Find(L"appearance/FONT", nullptr) == font);
	CHECK(m.Find(L"Appearance/Font/Size", nullptr) == size);
	CHECK(m.Find(L"/Appearance//Font/", nullptr) == font);
	CHECK(m.Find(L"Size", font) == size);
	CHECK(m.Find(L"Appearance/Fon", nullptr) == nullptr);
	CHECK(m.Find(L"Size", nullptr) == nullptr);
	CHECK(m.Find(L"", nullptr) == nullptr);
	CHECK(m.Find(nullptr, nullptr) == nullptr);
}

TEST(IsAncestor) {
	PgModel m;
	auto cat = AddCat(m, L"Cat");
	auto p = Add(m, cat, L"P");
	auto s = Add(m, p, L"S");
	CHECK(PgIsAncestor(cat, s));
	CHECK(PgIsAncestor(p, s));
	CHECK(!PgIsAncestor(s, s));
	CHECK(!PgIsAncestor(s, p));
	CHECK(!PgIsAncestor(p, nullptr));
}

// ---- Rows ---------------------------------------------------------------------------

TEST(Rows_CategorizedPutsLoosePropertiesFirst) {
	PgModel m;
	auto a = AddCat(m, L"A");
	Add(m, a, L"A1");
	Add(m, nullptr, L"Loose");
	auto b = AddCat(m, L"B");
	auto b1 = Add(m, b, L"B1");
	Add(m, b1, L"Sub");
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"Loose/0 A/0 A1/0 B/0 B1/0 Sub/1");
}

TEST(Rows_CollapsedAndHidden) {
	PgModel m;
	auto a = AddCat(m, L"A");
	auto a1 = Add(m, a, L"A1");
	Add(m, a1, L"Sub");
	Add(m, a, L"A2", PRGK_STRING, PRGF_HIDDEN);
	auto b = AddCat(m, L"B", PRGF_COLLAPSED);
	Add(m, b, L"B1");
	auto c = AddCat(m, L"Empty");
	Add(m, c, L"C1", PRGK_STRING, PRGF_HIDDEN);
	AddCat(m, L"None");
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"A/0 A1/0 Sub/1 B/0");

	a1->expanded = false;
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"A/0 A1/0 B/0");
	a->expanded = false;
	b->expanded = true;
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"A/0 B/0 B1/0");
	a->flags |= PRGF_HIDDEN;
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"B/0 B1/0");
}

TEST(Rows_FlagsExpandIntoFlagRows) {
	PgModel m;
	auto cat = AddCat(m, L"Cat");
	PRGENUMITEM items[] = { { L"None", 0 }, { L"Read", 1 }, { L"Write", 2 } };
	PRGITEMDESC d;
	d.kind = PRGK_FLAGS;
	d.pszName = L"Access";
	d.value = PrgMakeInt(1);
	d.enumItems = items;
	d.enumCount = 3;
	auto access = m.Insert(cat, d, PRG_LAST);
	Add(m, access, L"Child");
	Add(m, cat, L"After");
	// collapsed at first, whatever PRGF_COLLAPSED says
	CHECK(!access->expanded);
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"Cat/0 Access/0 After/0");
	// the flags (not the zero one) come before real children
	access->expanded = true;
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"Cat/0 Access/0 [Read]/1 [Write]/1 Child/1 After/0");
	CHECK_STR(Rows(m, PRGV_ALPHABETICAL), L"Access/0 [Read]/1 [Write]/1 Child/1 After/0");
}

TEST(Rows_Filter) {
	PgModel m;
	auto app = AddCat(m, L"Appearance");
	auto color = Add(m, app, L"Back Color");
	color->value.Assign(PrgMakeString(L"blue"));
	auto font = Add(m, app, L"Font");
	auto size = Add(m, font, L"Size");
	size->value.Assign(PrgMakeString(L"12"));
	font->expanded = false;
	auto behavior = AddCat(m, L"Behavior", PRGF_COLLAPSED);
	Add(m, behavior, L"Name");
	auto secret = Add(m, behavior, L"Secret", PRGK_STRING, PRGF_PASSWORD);
	secret->value.Assign(PrgMakeString(L"color"));
	Add(m, behavior, L"Gone", PRGK_STRING, PRGF_HIDDEN);
	Add(m, nullptr, L"Colorless");

	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"color"), L"Colorless/0 Appearance/0 Back Color/0");   // a password's value never matches
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"BLUE"), L"Appearance/0 Back Color/0");                // by the value
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"size"), L"Appearance/0 Font/0 Size/1");               // collapsed Font opened on the way
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"name"), L"Behavior/0 Name/0");                        // and a collapsed category
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"behav"), L"Behavior/0 Name/0 Secret/0");              // a matching category shows all it has
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"gone"), L"");                                         // hidden stays hidden
	CHECK_STR(Rows(m, PRGV_ALPHABETICAL, L"color"), L"Back Color/0 Colorless/0");
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"zzz"), L"");
	// no filter: as before
	CHECK_STR(Rows(m, PRGV_CATEGORIZED), L"Colorless/0 Appearance/0 Back Color/0 Font/0 Behavior/0");

	PRGENUMITEM items[] = { { L"Read", 1 }, { L"Write", 2 } };
	PRGITEMDESC d;
	d.kind = PRGK_FLAGS;
	d.pszName = L"Access";
	d.value = PrgMakeInt(0);
	d.enumItems = items;
	d.enumCount = 2;
	m.Insert(app, d, PRG_LAST);
	CHECK_STR(Rows(m, PRGV_CATEGORIZED, L"writ"), L"Appearance/0 Access/0 [Write]/1");          // a flag's name
}

TEST(Contains) {
	CHECK(PgContains(L"Back Color", L"color"));
	CHECK(PgContains(L"Back Color", L""));
	CHECK(!PgContains(L"Back", L"Back Color"));
	CHECK(PgContains(L"STRASSE", L"strasse"));
}

TEST(Flags_Helpers) {
	CHECK(PgIsFlagSet(3, 1));
	CHECK(PgIsFlagSet(3, 3));
	CHECK(!PgIsFlagSet(1, 3));   // all its bits
	CHECK(!PgIsFlagSet(7, 0));   // a zero flag is never "set"
	CHECK(PgSetFlag(1, 6, true) == 7);
	CHECK(PgSetFlag(7, 6, false) == 1);
	CHECK(PgSetFlag(1, 3, false) == 0);

	PgModel m;
	PRGENUMITEM zero[] = { { L"None", 0 } };
	PRGITEMDESC d;
	d.kind = PRGK_FLAGS;
	d.pszName = L"F";
	d.enumItems = zero;
	d.enumCount = 1;
	auto f = m.Insert(nullptr, d, PRG_LAST);
	CHECK(!PgHasSubRows(f));     // only a zero flag: nothing to show
	auto p = Add(m, nullptr, L"P");
	CHECK(!PgHasSubRows(p));
	Add(m, p, L"Hidden", PRGK_STRING, PRGF_HIDDEN);
	CHECK(!PgHasSubRows(p));
	Add(m, p, L"Shown");
	CHECK(PgHasSubRows(p));
}

TEST(Rows_AlphabeticalDropsCategories) {
	PgModel m;
	auto a = AddCat(m, L"A");
	Add(m, a, L"item10");
	auto i2 = Add(m, a, L"Item2");
	Add(m, i2, L"zeta");
	Add(m, i2, L"alpha");
	auto b = AddCat(m, L"B", PRGF_COLLAPSED);   // a collapsed category doesn't hide its items here
	Add(m, b, L"beta");
	Add(m, b, L"Hidden", PRGK_STRING, PRGF_HIDDEN);
	Add(m, nullptr, L"Loose");
	// numbers compare as numbers; sub-properties keep their insertion order
	CHECK_STR(Rows(m, PRGV_ALPHABETICAL), L"beta/0 Item2/0 zeta/1 alpha/1 item10/0 Loose/0");
}

// ---- Values ---------------------------------------------------------------------------

TEST(ValueFits) {
	CHECK(PgValueFits(PRGK_STRING, PrgMakeString(L"x")));
	CHECK(PgValueFits(PRGK_PATH, PrgMakeString(L"x")));
	CHECK(PgValueFits(PRGK_FLAGS, PrgMakeInt(3)));
	CHECK(!PgValueFits(PRGK_BOOL, PrgMakeInt(1)));
	CHECK(PgValueFits(PRGK_BOOL, PRGVALUE{}));
	CHECK(PgValueFits(PRGK_CUSTOM, PrgMakeReal(1)));
	CHECK(PgValueFits(PRGK_CUSTOM, PrgMakeColor(0)));
	CHECK(PgValueFits(PRGK_CATEGORY, PRGVALUE{}));
	CHECK(!PgValueFits(PRGK_CATEGORY, PrgMakeString(L"x")));
	PRGVALUE bad{};
	bad.type = 42;
	CHECK(!PgValueFits(PRGK_CUSTOM, bad));
}

TEST(ValuesEqual) {
	CHECK(PgValuesEqual(PrgMakeString(L"a"), PrgMakeString(L"a")));
	CHECK(!PgValuesEqual(PrgMakeString(L"a"), PrgMakeString(L"A")));
	CHECK(PgValuesEqual(PrgMakeString(nullptr), PrgMakeString(L"")));
	CHECK(!PgValuesEqual(PrgMakeInt(1), PrgMakeReal(1)));
	CHECK(PgValuesEqual(PrgMakeBool(true), [] { auto v = PrgMakeBool(true); v.boolVal = 7; return v; }()));
	CHECK(PgValuesEqual(PRGVALUE{}, PRGVALUE{}));

	LOGFONTW a{}, b{};
	a.lfHeight = b.lfHeight = -12;
	wcscpy_s(a.lfFaceName, L"Segoe UI");
	memset(b.lfFaceName, 0x55, sizeof(b.lfFaceName));   // garbage after the terminator
	wcscpy_s(b.lfFaceName, L"segoe ui");
	CHECK(PgValuesEqual(PrgMakeFont(a), PrgMakeFont(b)));
	b.lfItalic = TRUE;
	CHECK(!PgValuesEqual(PrgMakeFont(a), PrgMakeFont(b)));
}

// ---- Formatting ---------------------------------------------------------------------------

TEST(Format_Numbers) {
	CHECK_STR(Format(PRGK_INTEGER, PrgMakeInt(-42)), L"-42");
	CHECK_STR(Format(PRGK_INTEGER, PrgMakeInt(255), PRGF_HEX), L"0xFF");
	CHECK_STR(Format(PRGK_INTEGER, PrgMakeInt(-1), PRGF_HEX), L"0xFFFFFFFFFFFFFFFF");
	CHECK_STR(Format(PRGK_REAL, PrgMakeReal(0.1)), L"0.1");
	CHECK_STR(Format(PRGK_REAL, PrgMakeReal(2.0)), L"2");
	CHECK_STR(Format(PRGK_REAL, PrgMakeReal(3.14159), 0, 2), L"3.14");
	CHECK_STR(Format(PRGK_REAL, PrgMakeReal(2.0), 0, 3), L"2.000");
}

TEST(Format_EnumAndFlags) {
	std::vector<PgEnum> items{ { L"None", 0 }, { L"Read", 1 }, { L"Write", 2 }, { L"ReadWrite", 3 }, { L"Exec", 4 } };
	CHECK_STR(Format(PRGK_ENUM, PrgMakeInt(2), 0, -1, items), L"Write");
	CHECK_STR(Format(PRGK_ENUM, PrgMakeInt(9), 0, -1, items), L"9");
	CHECK_STR(Format(PRGK_ENUM, PrgMakeInt(16), PRGF_HEX, -1, items), L"0x10");

	// every item whose bits are all set, in item order
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(5), 0, -1, items), L"Read | Exec");
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(3), 0, -1, items), L"Read | Write | ReadWrite");
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(0), 0, -1, items), L"None");
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(0x21), 0, -1, items), L"Read | 0x20");
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(0x30), 0, -1, items), L"0x30");
	CHECK_STR(Format(PRGK_FLAGS, PrgMakeInt(0), 0, -1, { { L"A", 1 } }), L"0");
}

TEST(Format_Other) {
	CHECK_STR(Format(PRGK_STRING, PrgMakeString(L"text")), L"text");
	CHECK_STR(Format(PRGK_STRING, PrgMakeString(L"abc"), PRGF_PASSWORD), L"\x25CF\x25CF\x25CF");
	CHECK_STR(Format(PRGK_STRING, PRGVALUE{}), L"");
	CHECK_STR(Format(PRGK_BOOL, PrgMakeBool(true)), L"True");
	CHECK_STR(Format(PRGK_BOOL, PrgMakeBool(false)), L"False");
	CHECK_STR(Format(PRGK_COLOR, PrgMakeColor(RGB(0x12, 0xAB, 0xFF))), L"#12ABFF");

	LOGFONTW lf{};
	wcscpy_s(lf.lfFaceName, L"Segoe UI");
	lf.lfHeight = -12;
	CHECK_STR(Format(PRGK_FONT, PrgMakeFont(lf)), L"Segoe UI, 9pt");
	lf.lfHeight = -15;
	lf.lfWeight = FW_BOLD;
	lf.lfItalic = TRUE;
	CHECK_STR(Format(PRGK_FONT, PrgMakeFont(lf)), L"Segoe UI, 11.25pt, Bold, Italic");

	// locale dependent: only that something comes out, and the date-only form is shorter
	SYSTEMTIME st{ 2026, 10, 3, 21, 12, 30, 0, 0 };
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft);
	auto full = Format(PRGK_DATETIME, PrgMakeDateTime(ft));
	auto date = Format(PRGK_DATETIME, PrgMakeDateTime(ft), PRGF_DATEONLY);
	CHECK(!date.empty());
	CHECK(full.size() > date.size());
}

// ---- Parsing ----------------------------------------------------------------------------

// parses text for a node of the kind; returns the error, or "" with the value in *out
static std::wstring Parse(const PgNode& node, const std::wstring& text, PRGVALUE* out = nullptr) {
	static PgStoredValue value;   // keeps a parsed string alive for the check
	std::wstring error;
	if (!PgParseValue(node, text, value, error))
		return error.empty() ? L"(no message)" : error;
	if (out)
		*out = value.value;
	return L"";
}

// a PgNode can't be copied or moved (its stored strings point into it)
static std::unique_ptr<PgNode> MakeNode(UINT kind, UINT flags = 0, std::vector<PgEnum> items = {}) {
	auto node = std::make_unique<PgNode>();
	node->kind = kind;
	node->flags = flags;
	node->enumItems = std::move(items);
	return node;
}

TEST(Parse_Integers) {
	auto owned = MakeNode(PRGK_INTEGER);
	auto& node = *owned;
	PRGVALUE v{};
	CHECK_STR(Parse(node, L" 42 ", &v), L"");
	CHECK(v.type == PRGVT_INT && v.llVal == 42);
	CHECK_STR(Parse(node, L"-0x10", &v), L"");
	CHECK(v.llVal == -16);
	CHECK_STR(Parse(node, L"+7", &v), L"");
	CHECK(v.llVal == 7);
	CHECK_STR(Parse(node, L"9223372036854775807", &v), L"");
	CHECK(v.llVal == LLONG_MAX);
	CHECK_STR(Parse(node, L"-9223372036854775808", &v), L"");
	CHECK(v.llVal == LLONG_MIN);
	CHECK(Parse(node, L"9223372036854775808") != L"");   // decimal overflow
	CHECK_STR(Parse(node, L"0xFFFFFFFFFFFFFFFF", &v), L"");   // as -1 is shown in hex
	CHECK(v.llVal == -1);
	CHECK_STR(Parse(node, L"12abc"), L"\"12abc\" is not a valid number.");
	CHECK(Parse(node, L"") != L"");
	CHECK(Parse(node, L"0x") != L"");
	CHECK(Parse(node, L"1F") != L"");                     // bare hex only with PRGF_HEX
	CHECK(Parse(node, L"\x0661") != L"");                 // non-ASCII digit

	auto hexNode = MakeNode(PRGK_INTEGER, PRGF_HEX);
	auto& hex = *hexNode;
	CHECK_STR(Parse(hex, L"1F", &v), L"");
	CHECK(v.llVal == 0x1F);
	CHECK_STR(Parse(hex, L"0x7FF612340000", &v), L"");
	CHECK(v.llVal == 0x7FF612340000);
}

TEST(Parse_IntegerRange) {
	auto owned = MakeNode(PRGK_INTEGER);
	auto& node = *owned;
	node.minVal = 1;
	node.maxVal = 1000;
	CHECK_STR(Parse(node, L"1"), L"");
	CHECK_STR(Parse(node, L"1000"), L"");
	CHECK_STR(Parse(node, L"0"), L"Enter a number from 1 to 1000.");
	CHECK_STR(Parse(node, L"1001"), L"Enter a number from 1 to 1000.");
	node.maxVal = LLONG_MAX;
	CHECK_STR(Parse(node, L"-5"), L"Enter a number of at least 1.");
	node.minVal = LLONG_MIN;
	node.maxVal = 0x10;
	node.flags = PRGF_HEX;
	CHECK_STR(Parse(node, L"20"), L"Enter a number of at most 0x10.");
}

TEST(Parse_Reals) {
	auto owned = MakeNode(PRGK_REAL);
	auto& node = *owned;
	PRGVALUE v{};
	CHECK_STR(Parse(node, L"0.85", &v), L"");
	CHECK(v.type == PRGVT_REAL && v.dblVal == 0.85);
	CHECK_STR(Parse(node, L"-1e3", &v), L"");
	CHECK(v.dblVal == -1000);
	CHECK_STR(Parse(node, L"+2.5", &v), L"");
	CHECK(v.dblVal == 2.5);
	CHECK(Parse(node, L"1,5") != L"");   // '.' only, as shown
	CHECK(Parse(node, L"abc") != L"");
	// what the grid shows parses back to the same value
	node.precision = -1;
	auto shown = PgFormatValue(node, PrgMakeReal(0.1 + 0.2));
	CHECK_STR(Parse(node, shown, &v), L"");
	CHECK(v.dblVal == 0.1 + 0.2);
}

TEST(Parse_EnumAndFlags) {
	std::vector<PgEnum> items{ { L"None", 0 }, { L"Read", 1 }, { L"Write", 2 }, { L"Exec", 4 } };
	auto eNode = MakeNode(PRGK_ENUM, 0, items);
	auto& e = *eNode;
	PRGVALUE v{};
	CHECK_STR(Parse(e, L"write", &v), L"");
	CHECK(v.llVal == 2);
	CHECK_STR(Parse(e, L"4", &v), L"");                 // an item's number
	CHECK(v.llVal == 4);
	CHECK_STR(Parse(e, L"7"), L"\"7\" is not one of the values.");
	CHECK(Parse(e, L"Other") != L"");
	e.flags = PRGF_EDITABLE;
	CHECK_STR(Parse(e, L"7", &v), L"");                 // editable: any number
	CHECK(v.llVal == 7);
	CHECK(Parse(e, L"Other") != L"");                    // but still not any name

	auto fNode = MakeNode(PRGK_FLAGS, 0, items);
	auto& f = *fNode;
	CHECK_STR(Parse(f, L"Read | Exec | 0x100", &v), L"");
	CHECK(v.llVal == (1 | 4 | 0x100));
	CHECK_STR(Parse(f, L"read,write", &v), L"");
	CHECK(v.llVal == 3);
	CHECK_STR(Parse(f, L"", &v), L"");
	CHECK(v.llVal == 0);
	CHECK_STR(Parse(f, L"None", &v), L"");
	CHECK(v.llVal == 0);
	CHECK_STR(Parse(f, L"Read | Bogus"), L"\"Bogus\" is not a known flag.");
	// round trip of the display text
	auto shown = PgFormatValue(f, PrgMakeInt(5 | 0x20));
	CHECK_STR(Parse(f, shown, &v), L"");
	CHECK(v.llVal == (5 | 0x20));
}

TEST(Parse_Other) {
	PRGVALUE v{};
	auto bNode = MakeNode(PRGK_BOOL);
	auto& b = *bNode;
	CHECK_STR(Parse(b, L"TRUE", &v), L"");
	CHECK(v.type == PRGVT_BOOL && v.boolVal);
	CHECK_STR(Parse(b, L"no", &v), L"");
	CHECK(!v.boolVal);
	CHECK_STR(Parse(b, L"maybe"), L"Enter True or False.");

	auto cNode = MakeNode(PRGK_COLOR);
	auto& c = *cNode;
	CHECK_STR(Parse(c, L"#12ABff", &v), L"");
	CHECK(v.clrVal == RGB(0x12, 0xAB, 0xFF));
	CHECK_STR(Parse(c, L"336699", &v), L"");
	CHECK(v.clrVal == RGB(0x33, 0x66, 0x99));
	CHECK_STR(Parse(c, L"255, 0, 10", &v), L"");
	CHECK(v.clrVal == RGB(255, 0, 10));
	CHECK(Parse(c, L"256, 0, 0") != L"");
	CHECK(Parse(c, L"#12345") != L"");
	CHECK(Parse(c, L"") != L"");

	auto sNode = MakeNode(PRGK_STRING);
	auto& s = *sNode;
	CHECK_STR(Parse(s, L"  spaced  ", &v), L"");
	CHECK_STR(v.pszVal, L"  spaced  ");                // strings as typed

	CHECK(Parse(*MakeNode(PRGK_FONT), L"Arial") != L"");
	CHECK(Parse(*MakeNode(PRGK_DATETIME), L"today") != L"");
}

TEST(Parse_CustomUsesTheValueType) {
	auto owned = MakeNode(PRGK_CUSTOM);
	auto& node = *owned;
	CHECK(PgEditType(node) == PRGVT_STRING);
	node.defValue.Assign(PrgMakeReal(1));
	CHECK(PgEditType(node) == PRGVT_REAL);
	node.value.Assign(PrgMakeInt(5));
	CHECK(PgEditType(node) == PRGVT_INT);
	PRGVALUE v{};
	CHECK_STR(Parse(node, L"12", &v), L"");
	CHECK(v.type == PRGVT_INT && v.llVal == 12);
}

TEST(EditText_UnmasksPasswords) {
	auto owned = MakeNode(PRGK_STRING, PRGF_PASSWORD);
	auto& node = *owned;
	node.value.Assign(PrgMakeString(L"secret"));
	CHECK_STR(PgEditText(node), L"secret");
	CHECK_STR(PgFormatValue(node), L"\x25CF\x25CF\x25CF\x25CF\x25CF\x25CF");
	auto nNode = MakeNode(PRGK_INTEGER, PRGF_HEX);
	auto& n = *nNode;
	n.value.Assign(PrgMakeInt(255));
	CHECK_STR(PgEditText(n), L"0xFF");
}

int wmain() {
	Run_Insert_RejectsInvalidDescriptions();
	Run_Insert_FirstLastAndAfter();
	Run_Insert_CopiesEverything();
	Run_StoredValue_AssignFromItself();
	Run_Delete_RemovesTheSubtree();
	Run_Find_ByPath();
	Run_IsAncestor();
	Run_Rows_CategorizedPutsLoosePropertiesFirst();
	Run_Rows_CollapsedAndHidden();
	Run_Rows_AlphabeticalDropsCategories();
	Run_Rows_FlagsExpandIntoFlagRows();
	Run_Flags_Helpers();
	Run_Rows_Filter();
	Run_Contains();
	Run_ValueFits();
	Run_ValuesEqual();
	Run_Format_Numbers();
	Run_Format_EnumAndFlags();
	Run_Format_Other();
	Run_Parse_Integers();
	Run_Parse_IntegerRange();
	Run_Parse_Reals();
	Run_Parse_EnumAndFlags();
	Run_Parse_Other();
	Run_Parse_CustomUsesTheValueType();
	Run_EditText_UnmasksPasswords();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
