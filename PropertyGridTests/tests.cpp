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
static std::wstring Rows(const PgModel& m, UINT view) {
	std::vector<PgRow> rows;
	m.BuildRows(view, rows);
	std::wstring text;
	for (auto& r : rows) {
		if (!text.empty())
			text += L' ';
		text += r.node->name + L'/' + std::to_wstring(r.level);
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
	Run_ValueFits();
	Run_ValuesEqual();
	Run_Format_Numbers();
	Run_Format_EnumAndFlags();
	Run_Format_Other();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
