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

// ---- Selection ---------------------------------------------------------------------------------

TEST(PosFromPoint) {
	auto set = ThreeColumns();
	auto L = DgLayoutColumns(set, Params(300, 20, 1));   // A frozen at 20-120; B 120-170, C 170-250
	DgView view{ 0, 5, 100, 300, 125 };
	CHECK(DgPosFromPoint(L, view, 200, 45) == (DgPos{ 7, 2 }));
	CHECK(DgPosFromPoint(L, view, 200, -50) == (DgPos{ 5, 2 }));    // above: the top row in view
	CHECK(DgPosFromPoint(L, view, 200, 500) == (DgPos{ 15, 2 }));   // below: the last row in view
	CHECK(DgPosFromPoint(L, view, 290, 45) == (DgPos{ 7, 2 }));     // past the columns: the last
	CHECK(DgPosFromPoint(L, view, 5, 45) == (DgPos{ 7, 0 }));       // the row numbers: the first
	view.topRow = 98;
	CHECK(DgPosFromPoint(L, view, 200, 120) == (DgPos{ 99, 2 }));   // past the last row
	view.rowCount = 0;
	CHECK_INT(DgPosFromPoint(L, view, 200, 45).row, 0);

	auto scrolled = DgLayoutColumns(set, Params(300, 20, 0));
	DgView v2{ 110, 0, 10, 300, 125 };                              // A at -90..10, B 10..60
	CHECK_INT(DgPosFromPoint(scrolled, v2, 5, 45).col, 1);          // the first one in view
}

TEST(SelectionCells) {
	DgSelection s;
	s.Reset({ 2, 1 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 2, 2, 1, 1 } }));
	s.Extend({ 5, 3 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 2, 5, 1, 3 } }));
	CHECK(s.Contains(4, 2));
	CHECK(!s.Contains(6, 2));
	CHECK(!s.Contains(3, 0));
	s.Extend({ 0, 0 }, 4);                                          // from the anchor, the other way
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 0, 2, 0, 1 } }));
	s.Extend({ 5, 3 }, 4);
	s.Add({ 8, 0 }, 4);
	CHECK_INT(s.Ranges().size(), 2);
	CHECK(s.Anchor() == (DgPos{ 8, 0 }));
	s.Extend({ 9, 1 }, 4);
	CHECK(s.Ranges()[0] == (DgRange{ 2, 5, 1, 3 }));                // the first stays
	CHECK(s.Ranges()[1] == (DgRange{ 8, 9, 0, 1 }));
	CHECK(s.Rows() == (std::vector<std::pair<int64_t, int64_t>>{ { 2, 5 }, { 8, 9 } }));
	CHECK_INT(s.RowCount(), 6);
	CHECK(s.Cols() == (std::vector<int>{ 0, 1, 2, 3 }));
	CHECK(s.RowTouched(9));
	CHECK(!s.RowTouched(7));
	CHECK(s.ColTouched(0));
	s.Add({ 6, 0 }, 4);                                             // next to the first: merged
	CHECK(s.Rows() == (std::vector<std::pair<int64_t, int64_t>>{ { 2, 6 }, { 8, 9 } }));

	s.Reset({ 4, 2 }, 4, true);                                     // the row numbers': whole rows
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 4, 4, 0, 3 } }));
	s.Extend({ 6, 2 }, 4, true);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 4, 6, 0, 3 } }));

	s.SelectAll(10, 3);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 0, 9, 0, 2 } }));
	s.SelectAll(0, 3);
	CHECK(s.Empty());
}

TEST(SelectionModes) {
	DgSelection s;
	s.SetMode(DgSelMode::Rows, { 3, 2 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 3, 3, 0, 3 } }));
	s.Extend({ 1, 0 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 1, 3, 0, 3 } }));
	s.Toggle({ 2, 1 }, 4);                                          // out of the middle
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 1, 1, 0, 3 }, { 3, 3, 0, 3 } }));
	CHECK(!s.RowTouched(2));
	s.Toggle({ 2, 1 }, 4);                                          // back in
	CHECK(s.RowTouched(2));
	CHECK_INT(s.RowCount(), 3);
	s.Toggle({ 1, 0 }, 4);
	s.Toggle({ 2, 0 }, 4);
	s.Toggle({ 3, 0 }, 4);
	CHECK(s.Empty());                                               // rows may have none

	s.SetMode(DgSelMode::Single, { 1, 1 }, 4);
	s.Extend({ 5, 3 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 5, 5, 3, 3 } }));
	s.Add({ 7, 0 }, 4, true);                                       // whole rows don't apply
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 7, 7, 0, 0 } }));
	s.Toggle({ 2, 2 }, 4);
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 2, 2, 2, 2 } }));
}

TEST(SelectionClamp) {
	DgSelection s;
	s.Reset({ 2, 1 }, 4);
	s.Extend({ 5, 3 }, 4);
	s.Add({ 8, 0 }, 4);
	s.Extend({ 9, 1 }, 4);
	s.Clamp(7, 2);                                                  // fewer rows and columns
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 2, 5, 1, 1 } }));
	CHECK(s.Anchor() == (DgPos{ 6, 0 }));
	s.Reset({ 8, 3 }, 4);
	s.Clamp(5, 2);                                                  // nothing left: the anchor's cell
	CHECK(s.Ranges() == (std::vector<DgRange>{ { 4, 4, 1, 1 } }));
	s.Clamp(0, 2);
	CHECK(s.Empty());

	DgSelection rows;
	rows.SetMode(DgSelMode::Rows, { 1, 0 }, 4);
	rows.Clamp(5, 6);                                               // more columns: still whole rows
	CHECK(rows.Ranges() == (std::vector<DgRange>{ { 1, 1, 0, 5 } }));
}

// ---- Sorting -------------------------------------------------------------------------------------

TEST(ClickSort) {
	std::vector<DgSortKey> k;
	k = DgClickSort(k, 3, false);
	CHECK(k == (std::vector<DgSortKey>{ { 3, false } }));
	k = DgClickSort(k, 3, false);
	CHECK(k == (std::vector<DgSortKey>{ { 3, true } }));
	k = DgClickSort(k, 3, false);
	CHECK(k.empty());                                               // and back to unsorted
	k = DgClickSort({ { 3, false } }, 5, false);
	CHECK(k == (std::vector<DgSortKey>{ { 5, false } }));
	k = DgClickSort({ { 3, false } }, 5, true);
	CHECK(k == (std::vector<DgSortKey>{ { 3, false }, { 5, false } }));
	k = DgClickSort(k, 5, true);
	CHECK(k == (std::vector<DgSortKey>{ { 3, false }, { 5, true } }));
	k = DgClickSort(k, 3, true);
	CHECK(k == (std::vector<DgSortKey>{ { 3, true }, { 5, true } }));
	k = DgClickSort(k, 3, false);                                   // a plain click: that column alone
	CHECK(k == (std::vector<DgSortKey>{ { 3, false } }));
}

TEST(CompareValues) {
	CHECK_INT(DgCompareValues(DgValue::Int(2), DgValue::Float(2.5), nullptr), -1);
	CHECK_INT(DgCompareValues(DgValue::Float(2.0), DgValue::Int(2), nullptr), 0);
	CHECK_INT(DgCompareValues(DgValue::Bool(true), DgValue::Int(0), nullptr), 1);
	CHECK_INT(DgCompareValues(DgValue::Int(INT64_MAX), DgValue::Int(INT64_MAX - 1), nullptr), 1);   // not through doubles
	CHECK_INT(DgCompareValues(DgValue::Int(99), DgValue::Text(L"1"), nullptr), -1);   // numbers, dates, text
	CHECK_INT(DgCompareValues(DgValue::DateTime(5), DgValue::Text(L""), nullptr), -1);
	CHECK_INT(DgCompareValues(DgValue::Int(5), DgValue::DateTime(1), nullptr), -1);
	CHECK_INT(DgCompareValues(DgValue::Float(std::nan("")), DgValue::Int(5), nullptr), 1);
	CHECK_INT(DgCompareValues(DgValue::Text(L"B"), DgValue::Text(L"a"), nullptr), -1);   // ordinal
	auto noCase = [](const std::wstring& a, const std::wstring& b) {
		return _wcsicmp(a.c_str(), b.c_str()) < 0 ? -1 : _wcsicmp(a.c_str(), b.c_str()) > 0 ? 1 : 0;
	};
	CHECK_INT(DgCompareValues(DgValue::Text(L"B"), DgValue::Text(L"a"), noCase), 1);
}

TEST(SortOrder) {
	std::vector<DgValue> col{ DgValue::Int(3), DgValue{}, DgValue::Int(1), DgValue::Int(3), DgValue::Text(L"a"), DgValue::Float(2) };
	auto value = [&](int64_t row, size_t) -> const DgValue* { return &col[(size_t)row]; };
	CHECK(DgSortOrder(6, { false }, value, nullptr) == (std::vector<int64_t>{ 2, 5, 0, 3, 4, 1 }));
	CHECK(DgSortOrder(6, { true }, value, nullptr) == (std::vector<int64_t>{ 4, 0, 3, 5, 2, 1 }));   // stable; empty still last
	CHECK(DgSortOrder(6, {}, value, nullptr) == (std::vector<int64_t>{ 0, 1, 2, 3, 4, 5 }));
	auto none = [](int64_t, size_t) -> const DgValue* { return nullptr; };
	CHECK(DgSortOrder(3, { false }, none, nullptr) == (std::vector<int64_t>{ 0, 1, 2 }));

	// two keys: the first ascending, the second descending
	std::vector<DgValue> k0{ DgValue::Int(1), DgValue::Int(1), DgValue::Int(2), DgValue::Int(2) };
	std::vector<DgValue> k1{ DgValue::Text(L"b"), DgValue::Text(L"a"), DgValue::Text(L"a"), DgValue::Text(L"b") };
	auto two = [&](int64_t row, size_t key) -> const DgValue* { return key == 0 ? &k0[(size_t)row] : &k1[(size_t)row]; };
	CHECK(DgSortOrder(4, { false, true }, two, nullptr) == (std::vector<int64_t>{ 0, 1, 3, 2 }));
	CHECK(DgSortOrder(4, { true, false }, two, nullptr) == (std::vector<int64_t>{ 2, 3, 1, 0 }));
}

// ---- Copying -----------------------------------------------------------------------------------------

TEST(Tsv) {
	CHECK_STR(DgTsvField(L"plain"), L"plain");
	CHECK_STR(DgTsvField(L"a\tb"), L"\"a\tb\"");
	CHECK_STR(DgTsvField(L"say \"hi\""), L"\"say \"\"hi\"\"\"");
	CHECK_STR(DgTsvField(L"two\r\nlines"), L"\"two\r\nlines\"");
	CHECK_STR(DgBuildTsv({ { L"a", L"b" }, { L"", L"d" } }), L"a\tb\r\n\td\r\n");
	CHECK_STR(DgBuildTsv({}), L"");
}

TEST(Html) {
	CHECK_STR(DgHtmlEscape(L"<a & \"b\">\r\nx\ny"), L"&lt;a &amp; &quot;b&quot;&gt;<br>x<br>y");
	CHECK_STR(DgBuildHtmlTable({ { L"A" }, { L"1" } }, true), L"<table><tr><th>A</th></tr><tr><td>1</td></tr></table>");
	CHECK_STR(DgBuildHtmlTable({ { L"1", L"<" } }, false), L"<table><tr><td>1</td><td>&lt;</td></tr></table>");
	CHECK(DgUtf8(L"a\x00e9\x20ac") == "a\xC3\xA9\xE2\x82\xAC");
	CHECK(DgUtf8(L"\xD83D\xDE00") == "\xF0\x9F\x98\x80");          // a surrogate pair
	CHECK(DgUtf8(L"\xD83Dx") == "\xEF\xBF\xBDx");                   // a lone one

	std::string fragment = "<table><tr><td>\xC3\xA9</td></tr></table>";
	auto cf = DgCfHtml(fragment);
	auto number = [&](const char* name) {
		auto at = cf.find(name);
		return at == std::string::npos ? -1 : atoi(cf.c_str() + at + strlen(name));
	};
	int startHtml = number("StartHTML:"), endHtml = number("EndHTML:");
	int startFragment = number("StartFragment:"), endFragment = number("EndFragment:");
	CHECK(cf.compare(0, 12, "Version:0.9\r") == 0);
	CHECK(cf.compare((size_t)startHtml, 6, "<html>") == 0);
	CHECK_INT(endHtml, cf.size());
	CHECK(cf.substr((size_t)startFragment, (size_t)(endFragment - startFragment)) == fragment);
}

TEST(SelectionTable) {
	DgSelection s;
	s.Reset({ 1, 0 }, 3);
	s.Extend({ 2, 1 }, 3);
	s.Add({ 4, 2 }, 3);
	auto text = [](int64_t r, int c) { return std::to_wstring(r) + L":" + std::to_wstring(c); };
	auto title = [](int c) { return L"T" + std::to_wstring(c); };
	auto table = DgSelectionTable(s, text, title, true);
	CHECK_INT(table.size(), 4);                                     // titles, rows 1, 2 and 4
	CHECK(table[0] == (std::vector<std::wstring>{ L"T0", L"T1", L"T2" }));
	CHECK(table[1] == (std::vector<std::wstring>{ L"1:0", L"1:1", L"" }));
	CHECK(table[3] == (std::vector<std::wstring>{ L"", L"", L"4:2" }));
	CHECK_INT(DgSelectionTable(s, text, title, false).size(), 3);
}

// ---- Editing -----------------------------------------------------------------------------------------

static DgColumn TypedColumn(int type) {
	DgColumn c;
	c.type = type;
	return c;
}

static bool SlashDate(const std::wstring& text, int64_t& ticks) {   // "17/11/2026", for the user's format
	int d, m, y;
	if (swscanf_s(text.c_str(), L"%d/%d/%d", &d, &m, &y) != 3)
		return false;
	wchar_t iso[16];
	swprintf_s(iso, L"%04d-%02d-%02d", y, m, d);
	return DgParseIsoDateTime(iso, ticks);
}

TEST(ParseNumbers) {
	DgNumberFormat nf;
	auto in = TypedColumn(DgColType::Integer);
	auto p = DgParseValue(L"1,234", in, nf);
	CHECK(p.ok && p.value == DgValue::Int(1234));
	p = DgParseValue(L" -12 ", in, nf);
	CHECK(p.ok && p.value == DgValue::Int(-12));
	p = DgParseValue(L"-9223372036854775808", in, nf);
	CHECK(p.ok && p.value.i == INT64_MIN);
	CHECK(!DgParseValue(L"9223372036854775808", in, nf).ok);       // too big
	CHECK(!DgParseValue(L"1.5", in, nf).ok);
	CHECK_STR(DgParseValue(L"x", in, nf).error, L"Enter a whole number.");
	CHECK(!DgParseValue(L"-", in, nf).ok);
	p = DgParseValue(L"   ", in, nf);                                // nothing: an empty cell
	CHECK(p.ok && p.value.type == DgType::Empty);

	auto fl = TypedColumn(DgColType::Float);
	p = DgParseValue(L"3.25", fl, nf);
	CHECK(p.ok && p.value == DgValue::Float(3.25));
	p = DgParseValue(L"1e3", fl, nf);
	CHECK(p.ok && p.value.d == 1000);
	p = DgParseValue(L"1,234.5", fl, nf);
	CHECK(p.ok && p.value.d == 1234.5);
	CHECK(!DgParseValue(L"inf", fl, nf).ok);
	CHECK(!DgParseValue(L"nan", fl, nf).ok);
	CHECK(!DgParseValue(L"0x10", fl, nf).ok);
	CHECK(!DgParseValue(L"1.2.3", fl, nf).ok);

	DgNumberFormat german{ L".", L"," };
	p = DgParseValue(L"1.234,5", fl, german);
	CHECK(p.ok && p.value.d == 1234.5);
	p = DgParseValue(L"1.234", in, german);
	CHECK(p.ok && p.value.i == 1234);
	DgNumberFormat french{ L"\x202F", L"," };                         // grouped with a narrow no-break space
	p = DgParseValue(L"1\x202F" L"234,5", fl, french);
	CHECK(p.ok && p.value.d == 1234.5);
}

TEST(ParseOthers) {
	DgNumberFormat nf;
	auto bo = TypedColumn(DgColType::Bool);
	CHECK(DgParseValue(L"Yes", bo, nf).value == DgValue::Bool(true));
	CHECK(DgParseValue(L"0", bo, nf).value == DgValue::Bool(false));
	CHECK(!DgParseValue(L"maybe", bo, nf).ok);

	auto en = TypedColumn(DgColType::Enum);
	en.enumItems = { L"Low", L"High" };
	CHECK(DgParseValue(L"high", en, nf).value == DgValue::Int(1));
	CHECK(DgParseValue(L"0", en, nf).value == DgValue::Int(0));
	auto bad = DgParseValue(L"7", en, nf);
	CHECK(!bad.ok);
	CHECK_STR(bad.error, L"Enter one of: Low, High.");

	auto pr = TypedColumn(DgColType::Progress);
	CHECK(DgParseValue(L"42%", pr, nf).value == DgValue::Int(42));
	CHECK(DgParseValue(L"42", pr, nf).value == DgValue::Int(42));
	CHECK(DgParseValue(L"0.5", pr, nf).value == DgValue::Float(0.5));
	CHECK(DgParseValue(L"50.4 %", pr, nf).value == DgValue::Int(50));
	CHECK(!DgParseValue(L"150", pr, nf).ok);
	CHECK(!DgParseValue(L"1.5", pr, nf).ok);
	CHECK(!DgParseValue(L"-1%", pr, nf).ok);

	auto tx = TypedColumn(DgColType::Text);
	CHECK(DgParseValue(L"  hi ", tx, nf).value == DgValue::Text(L"  hi "));   // text as typed
	CHECK(DgParseValue(L"x", TypedColumn(DgColType::Link), nf).value == DgValue::Text(L"x"));
}

TEST(ParseDates) {
	DgNumberFormat nf;
	auto dt = TypedColumn(DgColType::DateTime);
	int64_t t = 0;
	CHECK(DgParseIsoDateTime(L"2026-11-17", t));
	CHECK_STR(DgFormatDateTime(t), L"2026-11-17 00:00:00");
	CHECK(DgParseIsoDateTime(L"2026-11-17 13:05", t));
	CHECK_STR(DgFormatDateTime(t), L"2026-11-17 13:05:00");
	CHECK(DgParseIsoDateTime(L" 2026-11-17T13:05:09 ", t));
	CHECK_STR(DgFormatDateTime(t), L"2026-11-17 13:05:09");
	CHECK(DgParseIsoDateTime(L"2024-02-29", t));
	CHECK(DgParseIsoDateTime(L"1601-01-01", t) && t == 0);
	CHECK(!DgParseIsoDateTime(L"2025-02-29", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-31", t));
	CHECK(!DgParseIsoDateTime(L"2026-13-01", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 24:00", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 13", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 13:05:09x", t));
	CHECK(!DgParseIsoDateTime(L"1600-12-31", t));                   // before FILETIME's epoch

	CHECK(!DgParseValue(L"17/11/2026", dt, nf).ok);
	auto p = DgParseValue(L"17/11/2026", dt, nf, SlashDate);        // the user's format
	CHECK(p.ok && DgFormatDateTime(p.value.i) == L"2026-11-17 00:00:00");
	CHECK(DgParseValue(L"2026-11-17", dt, nf, SlashDate).ok);        // ISO first
}

TEST(EditText) {
	DgNumberFormat nf;
	DgColumn in = TypedColumn(DgColType::Integer);
	in.flags = DgColFlag::GroupDigits;
	CHECK_STR(DgEditText(DgValue::Int(1234), in, nf), L"1234");     // no grouping to edit
	CHECK_STR(DgEditText(DgValue::Float(2.6), in, nf), L"3");
	auto fl = TypedColumn(DgColType::Float);
	fl.decimals = 2;
	CHECK_STR(DgEditText(DgValue::Float(0.1), fl, nf), L"0.1");
	CHECK_STR(DgEditText(DgValue::Float(3.14159), fl, nf), L"3.14159");   // all the digits
	DgNumberFormat german{ L".", L"," };
	CHECK_STR(DgEditText(DgValue::Float(0.25), fl, german), L"0,25");
	auto en = TypedColumn(DgColType::Enum);
	en.enumItems = { L"Low", L"High" };
	CHECK_STR(DgEditText(DgValue::Int(1), en, nf), L"High");
	CHECK_STR(DgEditText(DgValue::Int(42), TypedColumn(DgColType::Progress), nf), L"42%");
	auto dt = TypedColumn(DgColType::DateTime);
	int64_t t;
	DgParseIsoDateTime(L"2026-11-17 13:05", t);
	CHECK_STR(DgEditText(DgValue::DateTime(t), dt, nf), L"2026-11-17 13:05");
	DgParseIsoDateTime(L"2026-11-17 13:05:09", t);
	CHECK_STR(DgEditText(DgValue::DateTime(t), dt, nf), L"2026-11-17 13:05:09");
	CHECK_STR(DgEditText(DgValue{}, dt, nf), L"");
	// what's edited parses back to the same value
	for (auto v : { DgValue::Float(1e-7), DgValue::Float(-123456.789), DgValue::Float(1.0 / 3) }) {
		auto back = DgParseValue(DgEditText(v, fl, german), fl, german);
		CHECK(back.ok && back.value == v);
	}
}

TEST(ParseTsv) {
	using Rows = std::vector<std::vector<std::wstring>>;
	CHECK(DgParseTsv(L"a\tb\r\nc\td\r\n") == (Rows{ { L"a", L"b" }, { L"c", L"d" } }));
	CHECK(DgParseTsv(L"a\tb\nc") == (Rows{ { L"a", L"b" }, { L"c" } }));
	CHECK(DgParseTsv(L"a\t\"x\ty\"\n\"say \"\"hi\"\"\"") == (Rows{ { L"a", L"x\ty" }, { L"say \"hi\"" } }));
	CHECK(DgParseTsv(L"\"1\r\n2\"\tz") == (Rows{ { L"1\r\n2", L"z" } }));
	CHECK(DgParseTsv(L"a\"b\tc") == (Rows{ { L"a\"b", L"c" } }));       // a quote inside: as it is
	CHECK(DgParseTsv(L"a\t") == (Rows{ { L"a", L"" } }));
	CHECK(DgParseTsv(L"") == Rows{});
	CHECK(DgParseTsv(L"\r\n") == (Rows{ { L"" } }));
	Rows table{ { L"x\ty", L"" }, { L"\"q\"", L"two\r\nlines" } };     // round trip with copying
	CHECK(DgParseTsv(DgBuildTsv(table)) == table);
}

TEST(PasteTargets) {
	std::vector<std::vector<std::wstring>> two{ { L"a", L"b" }, { L"c", L"d" } };
	DgSelection s;
	s.Reset({ 2, 1 }, 3);
	auto cells = DgPasteTargets(two, s, 4, 3, 100);
	CHECK_INT(cells.size(), 4);
	CHECK(cells[3].row == 3 && cells[3].col == 2 && *cells[3].text == L"d");
	s.Reset({ 3, 2 }, 3);                                           // cut at the edges
	cells = DgPasteTargets(two, s, 4, 3, 100);
	CHECK_INT(cells.size(), 1);
	CHECK(*cells[0].text == L"a");

	std::vector<std::vector<std::wstring>> one{ { L"z" } };
	s.Reset({ 0, 0 }, 3);
	s.Extend({ 1, 1 }, 3);
	s.Add({ 1, 1 }, 3);                                             // overlapping: once each
	cells = DgPasteTargets(one, s, 4, 3, 100);
	CHECK_INT(cells.size(), 4);
	CHECK(*cells[2].text == L"z");
	CHECK_INT(DgPasteTargets(one, s, 4, 3, 3).size(), 3);           // at most max
	s.Reset({ 1, 1 }, 3);                                           // one cell, one field
	CHECK_INT(DgPasteTargets(one, s, 4, 3, 100).size(), 1);
	s.Reset({ 0, 0 }, 3);
	s.Extend({ 1, 1 }, 3);                                          // several fields: from the top-left
	cells = DgPasteTargets(two, s, 4, 3, 100);
	CHECK(cells[0].row == 0 && cells[0].col == 0);
	CHECK_INT(DgPasteTargets({}, s, 4, 3, 100).size(), 0);
}

TEST(EditKeys) {
	CHECK(DgEditKey(0x1B, false, false) == DgEditAction::Cancel);
	CHECK(DgEditKey(0x0D, false, false) == DgEditAction::CommitDown);
	CHECK(DgEditKey(0x0D, true, false) == DgEditAction::CommitUp);
	CHECK(DgEditKey(0x09, false, true) == DgEditAction::CommitRight);
	CHECK(DgEditKey(0x09, true, true) == DgEditAction::CommitLeft);
	CHECK(DgEditKey(0x27, false, true) == DgEditAction::CommitRight);   // typed: the arrows move on
	CHECK(DgEditKey(0x27, false, false) == DgEditAction::None);         // F2: they move in the text
	CHECK(DgEditKey(0x28, false, true) == DgEditAction::CommitDown);
	CHECK(DgEditKey('A', false, true) == DgEditAction::None);
}

TEST(Undo) {
	auto step = [](int64_t row) {
		DgEditStep s;
		s.changes.push_back({ row, 1, DgCell{}, DgCell{ DgValue::Int(row) } });
		return s;
	};
	DgUndoStack u(3);
	CHECK(!u.CanUndo() && !u.CanRedo());
	CHECK(u.Undo() == nullptr);
	u.Push(step(1));
	u.Push(step(2));
	u.Push(step(3));
	auto s = u.Undo();
	CHECK(s && s->changes[0].row == 3);
	s = u.Undo();
	CHECK(s && s->changes[0].row == 2);
	CHECK(u.CanRedo());
	s = u.Redo();
	CHECK(s && s->changes[0].row == 2);
	u.Push(step(4));                                                // drops the undone 3
	CHECK(!u.CanRedo());
	CHECK_INT(u.Count(), 3);
	u.Push(step(5));                                                // past the limit: the oldest goes
	CHECK_INT(u.Count(), 3);
	CHECK(u.Undo()->changes[0].row == 5);
	CHECK(u.Undo()->changes[0].row == 4);
	CHECK(u.Undo()->changes[0].row == 2);
	CHECK(!u.CanUndo());
	u.Clear();
	CHECK(!u.CanRedo());
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
	Run_PosFromPoint();
	Run_SelectionCells();
	Run_SelectionModes();
	Run_SelectionClamp();
	Run_ClickSort();
	Run_CompareValues();
	Run_SortOrder();
	Run_Tsv();
	Run_Html();
	Run_SelectionTable();
	Run_ParseNumbers();
	Run_ParseOthers();
	Run_ParseDates();
	Run_EditText();
	Run_ParseTsv();
	Run_PasteTargets();
	Run_EditKeys();
	Run_Undo();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
