// Unit tests for the DataGrid model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number of failed
// checks. Only the pure parts are covered: the columns and their order, laying them out (fill and frozen columns),
// scrolling and the scroll bar's mapping, hit testing, where a dragged column goes, moving the caret, formatting
// values, the store, the virtual grid's cache and the row map. Anything that needs an HWND is checked by hand with
// DataGridDemo.

#include "DataGridModel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

static void CheckInt(long long actual, long long expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: %lld, want: %lld\n", g_currentTest, line, expr, actual, expected);
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
#define CHECK_STR(a, b)  CheckStr((a), (b), #a " == " #b, __LINE__)
#define CHECK_INT(a, b)  CheckInt((long long)(a), (long long)(b), #a " == " #b, __LINE__)

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

static DgColumn Col(const wchar_t* title, int width, int fill = 0, int minWidth = 24) {
	DgColumn c;
	c.title = title;
	c.width = width;
	c.fill = fill;
	c.minWidth = minWidth;
	return c;
}

// three columns of 100, 50 and 80 at 96 DPI, no row header
static DgColumnSet ThreeColumns() {
	DgColumnSet set;
	set.Insert(Col(L"A", 100));
	set.Insert(Col(L"B", 50));
	set.Insert(Col(L"C", 80));
	return set;
}

static DgLayoutParams Params(int clientWidth, int rowHeader = 0, int frozen = 0) {
	DgLayoutParams p;
	p.clientWidth = clientWidth;
	p.rowHeader = rowHeader;
	p.header = 20;
	p.rowHeight = 10;
	p.frozen = frozen;
	return p;
}

// ---- Columns ----------------------------------------------------------------------

TEST(Columns) {
	DgColumnSet set;
	int a = set.Insert(Col(L"A", 100));
	int b = set.Insert(Col(L"B", 100));
	int c = set.Insert(Col(L"C", 100), 0);   // first
	CHECK(a > 0 && b > a && c > b);
	CHECK(set.Order() == (std::vector<int>{ c, a, b }));
	CHECK_INT(set.Find(b)->slot, 1);
	CHECK_INT(set.DisplayIndexOf(b), 2);
	CHECK(set.Move(b, c));
	CHECK(set.Order() == (std::vector<int>{ b, c, a }));
	CHECK(set.Move(b, 0));   // to the end
	CHECK(set.Order() == (std::vector<int>{ c, a, b }));
	CHECK(set.Move(a, a));
	CHECK(!set.Move(99, a));
	CHECK(!set.SetOrder({ a, b }));          // not all of them
	CHECK(!set.SetOrder({ a, a, b }));       // twice
	CHECK(set.SetOrder({ a, b, c }));
	set.Find(b)->flags |= DgColFlag::Hidden;
	auto visible = set.Visible();
	CHECK_INT(visible.size(), 2);
	CHECK_INT(visible[1]->id, c);
	CHECK(set.Erase(a));
	CHECK(!set.Erase(a));
	CHECK(set.Order() == (std::vector<int>{ b, c }));
	int d = set.Insert(Col(L"D", 100));
	CHECK_INT(set.Find(d)->slot, 3);         // slots aren't reused
	CHECK_INT(set.SlotCount(), 4);
}

TEST(EnumItems) {
	auto items = DgSplitEnumItems(L"Low\0Medium\0High\0");
	CHECK_INT(items.size(), 3);
	CHECK_STR(items[2], L"High");
	CHECK(DgSplitEnumItems(nullptr).empty());
	CHECK(DgSplitEnumItems(L"").empty());
}

// ---- Layout -------------------------------------------------------------------------

TEST(LayoutFixed) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(500, 30));
	CHECK_INT(L.cols.size(), 3);
	CHECK_INT(L.cols[1].left, 100);
	CHECK_INT(L.cols[2].left, 150);
	CHECK_INT(L.totalWidth, 230);
	CHECK_INT(L.Left(1, 0), 130);            // after the row header
	CHECK_INT(L.Left(1, 40), 90);            // scrolled
	CHECK_INT(L.IndexOf(set.Order()[2]), 2);
	CHECK_INT(L.IndexOf(42), -1);

	// at 144 DPI
	auto p = Params(500);
	p.dpi = 144;
	L = DgLayoutColumns(set, p);
	CHECK_INT(L.cols[0].width, 150);
	CHECK_INT(L.cols[1].width, 75);
	CHECK_INT(DgUnscale(150, 144), 100);

	// under its minimum
	set.Find(set.Order()[1])->width = 10;
	L = DgLayoutColumns(set, Params(500));
	CHECK_INT(L.cols[1].width, 24);
}

TEST(LayoutFill) {
	DgColumnSet set;
	set.Insert(Col(L"Fixed", 100));
	set.Insert(Col(L"One", 0, 1));
	set.Insert(Col(L"Two", 0, 2));
	auto L = DgLayoutColumns(set, Params(400));
	CHECK_INT(L.cols[1].width, 100);
	CHECK_INT(L.cols[2].width, 200);
	CHECK_INT(L.totalWidth, 400);            // exactly the width

	L = DgLayoutColumns(set, Params(401));   // the odd pixel goes somewhere
	CHECK_INT(L.totalWidth, 401);

	L = DgLayoutColumns(set, Params(110));   // no room: their minimums
	CHECK_INT(L.cols[1].width, 24);
	CHECK_INT(L.cols[2].width, 24);
	CHECK_INT(L.totalWidth, 148);
}

TEST(LayoutHidden) {
	auto set = ThreeColumns();
	set.Find(set.Order()[0])->flags |= DgColFlag::Hidden;
	auto L = DgLayoutColumns(set, Params(500));
	CHECK_INT(L.cols.size(), 2);
	CHECK_INT(L.cols[0].id, set.Order()[1]);
	CHECK_INT(L.cols[0].left, 0);
	CHECK_INT(L.totalWidth, 130);
}

TEST(LayoutFrozen) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(300, 20, 2));
	CHECK_INT(L.frozenCount, 2);
	CHECK_INT(L.frozenWidth, 150);
	CHECK_INT(L.ScrollLeft(), 170);
	CHECK_INT(L.Left(1, 50), 120);           // frozen: doesn't move
	CHECK_INT(L.Left(2, 50), 120);           // scrolled under them

	L = DgLayoutColumns(set, Params(160, 20, 2));   // no room to scroll the rest: fewer frozen
	CHECK_INT(L.frozenCount, 1);
	L = DgLayoutColumns(set, Params(100, 20, 2));
	CHECK_INT(L.frozenCount, 0);
	L = DgLayoutColumns(set, Params(500, 0, 9));    // more than there are
	CHECK_INT(L.frozenCount, 3);                    // all three: 230 still leaves room
}

// ---- Scrolling -------------------------------------------------------------------------

TEST(ScrollX) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(120));
	CHECK_INT(DgMaxScrollX(L, 120), 110);
	CHECK_INT(DgScrollXToShow(L, 0, 120, 0), 0);
	CHECK_INT(DgScrollXToShow(L, 0, 120, 2), 110);    // its right edge at the view's
	CHECK_INT(DgScrollXToShow(L, 0, 120, 1), 30);
	CHECK_INT(DgScrollXToShow(L, 110, 120, 0), 0);
	CHECK_INT(DgScrollXToShow(L, 60, 120, 1), 60);    // already in view

	L = DgLayoutColumns(set, Params(50));             // wider than the view: its left edge
	CHECK_INT(DgScrollXToShow(L, 0, 50, 2), 150);
	CHECK_INT(DgMaxScrollX(DgLayoutColumns(set, Params(500)), 500), 0);

	L = DgLayoutColumns(set, Params(200, 0, 1));      // frozen ones never scroll
	CHECK_INT(DgScrollXToShow(L, 30, 200, 0), 30);
	CHECK_INT(DgScrollXToShow(L, 0, 200, 2), 30);     // 50+80 in a view of 100
}

TEST(ScrollRows) {
	auto L = DgLayoutColumns(ThreeColumns(), Params(500));   // header 20, rows of 10
	CHECK_INT(DgFullRows(L, 125), 10);
	CHECK_INT(DgFullRows(L, 5), 1);
	CHECK_INT(DgMaxTopRow(100, 10), 90);
	CHECK_INT(DgMaxTopRow(5, 10), 0);
	CHECK_INT(DgTopToShow(20, 10, 25), 20);
	CHECK_INT(DgTopToShow(20, 10, 30), 21);
	CHECK_INT(DgTopToShow(20, 10, 5), 5);
}

TEST(ScrollMap) {
	CHECK_INT(DgScrollMap::Range(1000), 1000);
	CHECK_INT(DgScrollMap::ToPos(500, 1000), 500);
	CHECK_INT(DgScrollMap::FromPos(500, 1000), 500);

	const int64_t huge = 5'000'000'000'000LL;
	CHECK_INT(DgScrollMap::Range(huge), DgScrollMap::MaxPos);
	CHECK_INT(DgScrollMap::ToPos(0, huge), 0);
	CHECK_INT(DgScrollMap::ToPos(huge, huge), DgScrollMap::MaxPos);
	CHECK_INT(DgScrollMap::FromPos(0, huge), 0);
	CHECK_INT(DgScrollMap::FromPos(DgScrollMap::MaxPos, huge), huge);
	int mid = DgScrollMap::ToPos(huge / 2, huge);
	CHECK(mid > DgScrollMap::MaxPos / 2 - 2 && mid < DgScrollMap::MaxPos / 2 + 2);
	int64_t back = DgScrollMap::FromPos(mid, huge);
	CHECK(back > huge / 2 - 10000 && back < huge / 2 + 10000);
	CHECK(DgScrollMap::FromPos(1, huge) > 0);                  // off the top
	CHECK(DgScrollMap::FromPos(DgScrollMap::MaxPos - 1, huge) < huge);
	// monotonic
	int64_t previous = -1;
	for (int pos = 0; pos <= DgScrollMap::MaxPos; pos += DgScrollMap::MaxPos / 64) {
		int64_t top = DgScrollMap::FromPos(pos, huge);
		CHECK(top >= previous);
		previous = top;
	}
}

// ---- Hit testing ----------------------------------------------------------------------------

TEST(HitTest) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(300, 20, 1));   // A frozen at 20-120; B 120-170, C 170-250
	DgView view{ 0, 5, 100, 300, 125 };
	auto hit = DgHitTest(L, view, 10, 10, 3);
	CHECK_INT(hit.flags, DgHitFlag::Corner);
	hit = DgHitTest(L, view, 60, 10, 3);
	CHECK_INT(hit.flags, DgHitFlag::Header);
	CHECK_INT(hit.col, 0);
	hit = DgHitTest(L, view, 121, 10, 3);
	CHECK_INT(hit.flags, DgHitFlag::HeaderDivider);
	CHECK_INT(hit.col, 0);
	hit = DgHitTest(L, view, 168, 10, 3);
	CHECK_INT(hit.flags, DgHitFlag::HeaderDivider);
	CHECK_INT(hit.col, 1);
	hit = DgHitTest(L, view, 280, 10, 3);
	CHECK_INT(hit.flags, DgHitFlag::Empty);
	hit = DgHitTest(L, view, 10, 45, 3);
	CHECK_INT(hit.flags, DgHitFlag::RowHeader);
	CHECK_INT(hit.row, 7);                                 // top 5, the third row
	hit = DgHitTest(L, view, 200, 45, 3);
	CHECK_INT(hit.flags, DgHitFlag::Cell);
	CHECK_INT(hit.col, 2);
	CHECK_INT(hit.row, 7);
	hit = DgHitTest(L, view, 260, 45, 3);
	CHECK_INT(hit.flags, DgHitFlag::Empty);
	CHECK_INT(hit.row, 7);
	hit = DgHitTest(L, view, -1, 45, 3);
	CHECK_INT(hit.flags, DgHitFlag::Nowhere);

	view.rowCount = 6;                                     // past the last row
	hit = DgHitTest(L, view, 200, 45, 3);
	CHECK_INT(hit.flags, DgHitFlag::Empty);
	CHECK_INT(hit.row, -1);

	// scrolled: B (120-170) moves under A; a click there is A's
	view.rowCount = 100;
	view.scrollX = 40;                                     // B at 80-130, C at 130-210
	hit = DgHitTest(L, view, 100, 45, 3);
	CHECK_INT(hit.col, 0);
	hit = DgHitTest(L, view, 125, 45, 3);
	CHECK_INT(hit.col, 1);
	hit = DgHitTest(L, view, 131, 10, 3);                  // B's divider
	CHECK_INT(hit.flags, DgHitFlag::HeaderDivider);
	CHECK_INT(hit.col, 1);

	// two dividers in reach: the nearest, then the later one
	view.scrollX = 0;
	set.Find(set.Order()[1])->width = 24;
	auto narrow = DgLayoutColumns(set, Params(300, 20, 0));   // dividers at 120, 144
	hit = DgHitTest(narrow, view, 132, 10, 12);
	CHECK_INT(hit.col, 1);
	hit = DgHitTest(narrow, view, 130, 10, 12);
	CHECK_INT(hit.col, 0);
}

TEST(DropIndex) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(500));   // 0-100, 100-150, 150-230
	CHECK_INT(DgDropIndex(L, 0, 10), 0);
	CHECK_INT(DgDropIndex(L, 0, 60), 1);
	CHECK_INT(DgDropIndex(L, 0, 130), 2);
	CHECK_INT(DgDropIndex(L, 0, 200), 3);
	CHECK_INT(DgDropIndex(L, 0, 400), 3);

	L = DgLayoutColumns(set, Params(300, 0, 1));  // A frozen; B under it when scrolled
	CHECK_INT(DgDropIndex(L, 60, 80), 1);         // past A's middle, over the frozen part
	CHECK_INT(DgDropIndex(L, 60, 101), 2);        // B is 40-90: C's 90-170 starts the visible part
	CHECK_INT(DgDropIndex(L, 60, 40), 0);
}

// ---- The caret -------------------------------------------------------------------------------

TEST(Navigate) {
	DgPos p{ 5, 1 };
	CHECK(DgNavigate(p, DgKey::Left, false, 10, 3, 4) == (DgPos{ 5, 0 }));
	CHECK(DgNavigate({ 5, 0 }, DgKey::Left, false, 10, 3, 4) == (DgPos{ 5, 0 }));
	CHECK(DgNavigate(p, DgKey::Right, false, 10, 3, 4) == (DgPos{ 5, 2 }));
	CHECK(DgNavigate({ 5, 2 }, DgKey::Right, false, 10, 3, 4) == (DgPos{ 5, 2 }));
	CHECK(DgNavigate(p, DgKey::Up, false, 10, 3, 4) == (DgPos{ 4, 1 }));
	CHECK(DgNavigate(p, DgKey::Down, true, 10, 3, 4) == (DgPos{ 9, 1 }));
	CHECK(DgNavigate(p, DgKey::Up, true, 10, 3, 4) == (DgPos{ 0, 1 }));
	CHECK(DgNavigate(p, DgKey::Right, true, 10, 3, 4) == (DgPos{ 5, 2 }));
	CHECK(DgNavigate(p, DgKey::PageDown, false, 10, 3, 4) == (DgPos{ 9, 1 }));
	CHECK(DgNavigate({ 2, 1 }, DgKey::PageUp, false, 10, 3, 4) == (DgPos{ 0, 1 }));
	CHECK(DgNavigate(p, DgKey::Home, false, 10, 3, 4) == (DgPos{ 5, 0 }));
	CHECK(DgNavigate(p, DgKey::End, false, 10, 3, 4) == (DgPos{ 5, 2 }));
	CHECK(DgNavigate(p, DgKey::Home, true, 10, 3, 4) == (DgPos{ 0, 0 }));
	CHECK(DgNavigate(p, DgKey::End, true, 10, 3, 4) == (DgPos{ 9, 2 }));
	CHECK(DgNavigate(p, DgKey::Down, false, 0, 3, 4) == (DgPos{ 0, 0 }));   // nothing to move in
	CHECK(DgNavigate({ 50, 7 }, DgKey::Down, false, 10, 3, 4) == (DgPos{ 9, 2 }));   // clamped
	const int64_t big = 3'000'000'000LL;
	CHECK(DgNavigate({ big - 2, 0 }, DgKey::PageDown, false, big, 1, 100) == (DgPos{ big - 1, 0 }));
}

// ---- Formatting -----------------------------------------------------------------------------

TEST(FormatNumbers) {
	DgNumberFormat nf;
	CHECK_STR(DgFormatInt(0, true, nf), L"0");
	CHECK_STR(DgFormatInt(999, true, nf), L"999");
	CHECK_STR(DgFormatInt(1000, true, nf), L"1,000");
	CHECK_STR(DgFormatInt(-1234567, true, nf), L"-1,234,567");
	CHECK_STR(DgFormatInt(1234567, false, nf), L"1234567");
	CHECK_STR(DgFormatInt(INT64_MIN, true, nf), L"-9,223,372,036,854,775,808");
	CHECK_STR(DgFormatFloat(1234.5, 2, true, nf), L"1,234.50");
	CHECK_STR(DgFormatFloat(-0.126, 2, false, nf), L"-0.13");
	CHECK_STR(DgFormatFloat(-0.001, 2, false, nf), L"0.00");   // no "-0.00"
	CHECK_STR(DgFormatFloat(2.6, 0, false, nf), L"3");
	CHECK_STR(DgFormatFloat(std::nan(""), 2, false, nf), L"NaN");
	DgNumberFormat german{ L".", L"," };
	CHECK_STR(DgFormatFloat(1234567.891, 1, true, german), L"1.234.567,9");
	CHECK_STR(DgFormatInt(-1234, true, german), L"-1.234");
}

TEST(FormatDates) {
	// days after 1970-01-01, past FILETIME's 1601 epoch
	const int64_t TicksPerSecond = 10'000'000;
	const int64_t epoch = 116444736000000000LL;   // 1970-01-01 as a FILETIME
	CHECK_STR(DgFormatDateTime(epoch), L"1970-01-01 00:00:00");
	int64_t t = epoch + (int64_t)20409 * 86400 * TicksPerSecond + (13 * 3600 + 5 * 60 + 9) * TicksPerSecond;
	CHECK_STR(DgFormatDateTime(t), L"2025-11-17 13:05:09");
	CHECK_STR(DgFormatDateTime(0), L"1601-01-01 00:00:00");
	int64_t leap = epoch + (int64_t)11016 * 86400 * TicksPerSecond;   // 2000-02-29
	CHECK_STR(DgFormatDateTime(leap), L"2000-02-29 00:00:00");
}

TEST(FormatByColumn) {
	DgNumberFormat nf;
	DgColumn text;
	CHECK_STR(DgFormat(DgValue::Text(L"hi"), text, nf), L"hi");
	CHECK_STR(DgFormat(DgValue{}, text, nf), L"");
	CHECK_STR(DgFormat(DgValue::Int(1234), text, nf), L"1234");
	text.flags = DgColFlag::GroupDigits;
	CHECK_STR(DgFormat(DgValue::Int(1234), text, nf), L"1,234");
	CHECK_STR(DgFormat(DgValue::Bool(true), text, nf), L"true");

	DgColumn fl;
	fl.type = DgColType::Float;
	fl.decimals = 3;
	CHECK_STR(DgFormat(DgValue::Int(2), fl, nf), L"2.000");
	CHECK_STR(DgFormat(DgValue::Float(3.14159), fl, nf), L"3.142");

	DgColumn in;
	in.type = DgColType::Integer;
	CHECK_STR(DgFormat(DgValue::Float(2.6), in, nf), L"3");

	DgColumn en;
	en.type = DgColType::Enum;
	en.enumItems = { L"Low", L"High" };
	CHECK_STR(DgFormat(DgValue::Int(1), en, nf), L"High");
	CHECK_STR(DgFormat(DgValue::Int(7), en, nf), L"7");     // out of range: the number
	CHECK_STR(DgFormat(DgValue::Text(L"x"), en, nf), L"x");

	DgColumn pr;
	pr.type = DgColType::Progress;
	CHECK_STR(DgFormat(DgValue::Int(42), pr, nf), L"42%");
	CHECK_STR(DgFormat(DgValue::Float(0.5), pr, nf), L"50%");
	CHECK_STR(DgFormat(DgValue::Int(150), pr, nf), L"100%");

	DgColumn bo;
	bo.type = DgColType::Bool;
	CHECK_STR(DgFormat(DgValue::Int(0), bo, nf), L"false");
	CHECK(DgChecked(DgValue::Bool(true)));
	CHECK(DgChecked(DgValue::Int(5)));
	CHECK(!DgChecked(DgValue::Text(L"true")));
	CHECK(DgProgress(DgValue::Text(L"x")) < 0);
	CHECK(DgProgress(DgValue::Float(-3)) == 0);
}

TEST(Align) {
	DgColumn c;
	CHECK_INT(DgEffectiveAlign(c), DgAlign::Left);
	c.type = DgColType::Float;
	CHECK_INT(DgEffectiveAlign(c), DgAlign::Right);
	c.type = DgColType::Bool;
	CHECK_INT(DgEffectiveAlign(c), DgAlign::Center);
	c.align = DgAlign::Left;
	CHECK_INT(DgEffectiveAlign(c), DgAlign::Left);
	c.align = 42;
	CHECK_INT(DgEffectiveAlign(c), DgAlign::Center);
}

// ---- The store, the cache, the row map --------------------------------------------------------

TEST(Store) {
	DgStore s;
	s.Resize(3);
	CHECK(s.Set(1, 2, DgCell{ DgValue::Text(L"b"), 4 }));
	CHECK(!s.Set(3, 0, DgCell{}));
	CHECK(!s.Set(0, -1, DgCell{}));
	CHECK(s.Get(1, 2) && s.Get(1, 2)->value.s == L"b" && s.Get(1, 2)->image == 4);
	CHECK(s.Get(1, 7) == nullptr);
	CHECK(s.Get(0, 0) == nullptr);
	CHECK_INT(s.Insert(1, 2), 1);              // two before it
	CHECK_INT(s.Count(), 5);
	CHECK(s.Get(3, 2) && s.Get(3, 2)->value.s == L"b");
	CHECK_INT(s.Insert(-1, 1), 5);             // at the end
	CHECK_INT(s.Insert(99, 1), 6);
	CHECK(s.Erase(0, 3));
	CHECK(s.Get(0, 2) && s.Get(0, 2)->value.s == L"b");
	CHECK(!s.Erase(2, 9));
	s.ClearSlot(2);
	CHECK(s.Get(0, 2) && s.Get(0, 2)->value.type == DgType::Empty);
	s.Resize(1);
	CHECK_INT(s.Count(), 1);
}

TEST(Cache) {
	DgCellCache c;
	c.Put(10, 1, DgCell{ DgValue::Int(1) });
	c.Put(10, 2, DgCell{ DgValue::Int(2) });
	c.Put(11, 1, DgCell{ DgValue::Int(3) });
	c.Put(10, 1, DgCell{ DgValue::Int(4) });   // replaced
	CHECK(c.Find(10, 1) && c.Find(10, 1)->value.i == 4);
	CHECK(c.Find(10, 3) == nullptr);
	CHECK(c.Find(12, 1) == nullptr);
	CHECK_INT(c.RowCount(), 2);
	c.Put(50, 1, DgCell{});
	c.Trim(0, 20);
	CHECK(c.Find(50, 1) == nullptr);
	CHECK(c.Find(11, 1) != nullptr);
	c.Drop(11, 11);
	CHECK(c.Find(11, 1) == nullptr);
	CHECK(c.Find(10, 2) != nullptr);
	c.Clear();
	CHECK_INT(c.RowCount(), 0);
}

TEST(RowMap) {
	DgRowMap m;
	m.Reset(5);
	CHECK(m.Identity());
	CHECK_INT(m.ToData(3), 3);
	CHECK_INT(m.ToData(5), -1);
	CHECK_INT(m.ToView(-1), -1);
	m.SetOrder({ 4, 2, 0 }, 5);                // sorted and filtered
	CHECK(!m.Identity());
	CHECK_INT(m.Count(), 3);
	CHECK_INT(m.ToData(0), 4);
	CHECK_INT(m.ToView(0), 2);
	CHECK_INT(m.ToView(1), -1);                // filtered out
	CHECK_INT(m.ToView(9), -1);
	m.Reset(2);
	CHECK_INT(m.ToView(1), 1);
	const int64_t huge = 10'000'000'000LL;     // an identity needs no memory
	m.Reset(huge);
	CHECK_INT(m.ToData(huge - 1), huge - 1);
}

int wmain() {
	wprintf(L"DataGrid model tests\n\n");

	Run_Columns();
	Run_EnumItems();
	Run_LayoutFixed();
	Run_LayoutFill();
	Run_LayoutHidden();
	Run_LayoutFrozen();
	Run_ScrollX();
	Run_ScrollRows();
	Run_ScrollMap();
	Run_HitTest();
	Run_DropIndex();
	Run_Navigate();
	Run_FormatNumbers();
	Run_FormatDates();
	Run_FormatByColumn();
	Run_Align();
	Run_Store();
	Run_Cache();
	Run_RowMap();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
