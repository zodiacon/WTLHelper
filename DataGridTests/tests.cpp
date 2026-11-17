// Unit tests for the DataGrid model, with Catch2 (vcpkg: catch2, the static triplets).
//
// A console exe; Catch2's command line picks what runs: DataGridTests.exe "[edit]" runs the editing tests,
// DataGridTests.exe Undo one test, --list-tests lists them. Only the pure parts are covered (DataGridModel.h): the
// columns and their order, layout (fill, frozen columns, the filter row), scrolling and the scroll bar's mapping, hit
// testing, drop places, the caret's moves, formatting, the store, the virtual grid's cache, the row map, the
// selection, sorting, copying, parsing what's typed and pasted, undo, filters, groups and their aggregates. Anything
// that needs an HWND is checked by hand with DataGridDemo.

#include "DataGridModel.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// the checks' shorthands: wide strings compared as std::wstring, integers of any type as long long
#define CHECK_STR(a, b)  CHECK(std::wstring(a) == std::wstring(b))
#define CHECK_INT(a, b)  CHECK(static_cast<long long>(a) == static_cast<long long>(b))

int main(int argc, char* argv[]) {
	return Catch::Session().run(argc, argv);
}

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

TEST_CASE("Columns", "[data]") {
	DgColumnSet set;
	int a = set.Insert(Col(L"A", 100));
	int b = set.Insert(Col(L"B", 100));
	int c = set.Insert(Col(L"C", 100), 0);   // first
	CHECK((a > 0 && b > a && c > b));
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

TEST_CASE("EnumItems", "[format]") {
	auto items = DgSplitEnumItems(L"Low\0Medium\0High\0");
	CHECK_INT(items.size(), 3);
	CHECK_STR(items[2], L"High");
	CHECK(DgSplitEnumItems(nullptr).empty());
	CHECK(DgSplitEnumItems(L"").empty());
}

// ---- Layout -------------------------------------------------------------------------

TEST_CASE("LayoutFixed", "[layout]") {
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

TEST_CASE("LayoutFill", "[layout]") {
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

TEST_CASE("LayoutHidden", "[layout]") {
	auto set = ThreeColumns();
	set.Find(set.Order()[0])->flags |= DgColFlag::Hidden;
	auto L = DgLayoutColumns(set, Params(500));
	CHECK_INT(L.cols.size(), 2);
	CHECK_INT(L.cols[0].id, set.Order()[1]);
	CHECK_INT(L.cols[0].left, 0);
	CHECK_INT(L.totalWidth, 130);
}

TEST_CASE("LayoutFrozen", "[layout]") {
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

TEST_CASE("ScrollX", "[layout]") {
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

TEST_CASE("ScrollRows", "[layout]") {
	auto L = DgLayoutColumns(ThreeColumns(), Params(500));   // header 20, rows of 10
	CHECK_INT(DgFullRows(L, 125), 10);
	CHECK_INT(DgFullRows(L, 5), 1);
	CHECK_INT(DgMaxTopRow(100, 10), 90);
	CHECK_INT(DgMaxTopRow(5, 10), 0);
	CHECK_INT(DgTopToShow(20, 10, 25), 20);
	CHECK_INT(DgTopToShow(20, 10, 30), 21);
	CHECK_INT(DgTopToShow(20, 10, 5), 5);
}

TEST_CASE("ScrollMap", "[layout]") {
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
	CHECK((mid > DgScrollMap::MaxPos / 2 - 2 && mid < DgScrollMap::MaxPos / 2 + 2));
	int64_t back = DgScrollMap::FromPos(mid, huge);
	CHECK((back > huge / 2 - 10000 && back < huge / 2 + 10000));
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

TEST_CASE("HitTest", "[layout]") {
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

TEST_CASE("DropIndex", "[layout]") {
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

TEST_CASE("Navigate", "[caret]") {
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

TEST_CASE("FormatNumbers", "[format]") {
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

TEST_CASE("FormatDates", "[format]") {
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

TEST_CASE("FormatByColumn", "[format]") {
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

TEST_CASE("Align", "[format]") {
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

TEST_CASE("Store", "[data]") {
	DgStore s;
	s.Resize(3);
	CHECK(s.Set(1, 2, DgCell{ DgValue::Text(L"b"), 4 }));
	CHECK(!s.Set(3, 0, DgCell{}));
	CHECK(!s.Set(0, -1, DgCell{}));
	CHECK((s.Get(1, 2) && s.Get(1, 2)->value.s == L"b" && s.Get(1, 2)->image == 4));
	CHECK(s.Get(1, 7) == nullptr);
	CHECK(s.Get(0, 0) == nullptr);
	CHECK_INT(s.Insert(1, 2), 1);              // two before it
	CHECK_INT(s.Count(), 5);
	CHECK((s.Get(3, 2) && s.Get(3, 2)->value.s == L"b"));
	CHECK_INT(s.Insert(-1, 1), 5);             // at the end
	CHECK_INT(s.Insert(99, 1), 6);
	CHECK(s.Erase(0, 3));
	CHECK((s.Get(0, 2) && s.Get(0, 2)->value.s == L"b"));
	CHECK(!s.Erase(2, 9));
	s.ClearSlot(2);
	CHECK((s.Get(0, 2) && s.Get(0, 2)->value.type == DgType::Empty));
	s.Resize(1);
	CHECK_INT(s.Count(), 1);
}

TEST_CASE("Cache", "[data]") {
	DgCellCache c;
	c.Put(10, 1, DgCell{ DgValue::Int(1) });
	c.Put(10, 2, DgCell{ DgValue::Int(2) });
	c.Put(11, 1, DgCell{ DgValue::Int(3) });
	c.Put(10, 1, DgCell{ DgValue::Int(4) });   // replaced
	CHECK((c.Find(10, 1) && c.Find(10, 1)->value.i == 4));
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

TEST_CASE("RowMap", "[data]") {
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

TEST_CASE("PosFromPoint", "[layout]") {
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

TEST_CASE("SelectionCells", "[selection]") {
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

TEST_CASE("SelectionModes", "[selection]") {
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

TEST_CASE("SelectionClamp", "[selection]") {
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

TEST_CASE("ClickSort", "[sort]") {
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

TEST_CASE("CompareValues", "[sort]") {
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

TEST_CASE("SortOrder", "[sort]") {
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

TEST_CASE("Tsv", "[clipboard]") {
	CHECK_STR(DgTsvField(L"plain"), L"plain");
	CHECK_STR(DgTsvField(L"a\tb"), L"\"a\tb\"");
	CHECK_STR(DgTsvField(L"say \"hi\""), L"\"say \"\"hi\"\"\"");
	CHECK_STR(DgTsvField(L"two\r\nlines"), L"\"two\r\nlines\"");
	CHECK_STR(DgBuildTsv({ { L"a", L"b" }, { L"", L"d" } }), L"a\tb\r\n\td\r\n");
	CHECK_STR(DgBuildTsv({}), L"");
}

TEST_CASE("Html", "[clipboard]") {
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

TEST_CASE("SelectionTable", "[selection]") {
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

TEST_CASE("ParseNumbers", "[edit]") {
	DgNumberFormat nf;
	auto in = TypedColumn(DgColType::Integer);
	auto p = DgParseValue(L"1,234", in, nf);
	CHECK((p.ok && p.value == DgValue::Int(1234)));
	p = DgParseValue(L" -12 ", in, nf);
	CHECK((p.ok && p.value == DgValue::Int(-12)));
	p = DgParseValue(L"-9223372036854775808", in, nf);
	CHECK((p.ok && p.value.i == INT64_MIN));
	CHECK(!DgParseValue(L"9223372036854775808", in, nf).ok);       // too big
	CHECK(!DgParseValue(L"1.5", in, nf).ok);
	CHECK_STR(DgParseValue(L"x", in, nf).error, L"Enter a whole number.");
	CHECK(!DgParseValue(L"-", in, nf).ok);
	p = DgParseValue(L"   ", in, nf);                                // nothing: an empty cell
	CHECK((p.ok && p.value.type == DgType::Empty));

	auto fl = TypedColumn(DgColType::Float);
	p = DgParseValue(L"3.25", fl, nf);
	CHECK((p.ok && p.value == DgValue::Float(3.25)));
	p = DgParseValue(L"1e3", fl, nf);
	CHECK((p.ok && p.value.d == 1000));
	p = DgParseValue(L"1,234.5", fl, nf);
	CHECK((p.ok && p.value.d == 1234.5));
	CHECK(!DgParseValue(L"inf", fl, nf).ok);
	CHECK(!DgParseValue(L"nan", fl, nf).ok);
	CHECK(!DgParseValue(L"0x10", fl, nf).ok);
	CHECK(!DgParseValue(L"1.2.3", fl, nf).ok);

	DgNumberFormat german{ L".", L"," };
	p = DgParseValue(L"1.234,5", fl, german);
	CHECK((p.ok && p.value.d == 1234.5));
	p = DgParseValue(L"1.234", in, german);
	CHECK((p.ok && p.value.i == 1234));
	DgNumberFormat french{ L"\x202F", L"," };                         // grouped with a narrow no-break space
	p = DgParseValue(L"1\x202F" L"234,5", fl, french);
	CHECK((p.ok && p.value.d == 1234.5));
}

TEST_CASE("ParseOthers", "[edit]") {
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

TEST_CASE("ParseDates", "[edit]") {
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
	CHECK((DgParseIsoDateTime(L"1601-01-01", t) && t == 0));
	CHECK(!DgParseIsoDateTime(L"2025-02-29", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-31", t));
	CHECK(!DgParseIsoDateTime(L"2026-13-01", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 24:00", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 13", t));
	CHECK(!DgParseIsoDateTime(L"2026-11-17 13:05:09x", t));
	CHECK(!DgParseIsoDateTime(L"1600-12-31", t));                   // before FILETIME's epoch

	CHECK(!DgParseValue(L"17/11/2026", dt, nf).ok);
	auto p = DgParseValue(L"17/11/2026", dt, nf, SlashDate);        // the user's format
	CHECK((p.ok && DgFormatDateTime(p.value.i) == L"2026-11-17 00:00:00"));
	CHECK(DgParseValue(L"2026-11-17", dt, nf, SlashDate).ok);        // ISO first
}

TEST_CASE("EditText", "[edit]") {
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
		CHECK((back.ok && back.value == v));
	}
}

TEST_CASE("ParseTsv", "[edit]") {
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

TEST_CASE("PasteTargets", "[edit]") {
	std::vector<std::vector<std::wstring>> two{ { L"a", L"b" }, { L"c", L"d" } };
	DgSelection s;
	s.Reset({ 2, 1 }, 3);
	auto cells = DgPasteTargets(two, s, 4, 3, 100);
	CHECK_INT(cells.size(), 4);
	CHECK((cells[3].row == 3 && cells[3].col == 2 && *cells[3].text == L"d"));
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
	CHECK((cells[0].row == 0 && cells[0].col == 0));
	CHECK_INT(DgPasteTargets({}, s, 4, 3, 100).size(), 0);
}

TEST_CASE("EditKeys", "[edit]") {
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

TEST_CASE("Undo", "[edit]") {
	auto step = [](int64_t row) {
		DgEditStep s;
		s.changes.push_back({ row, 1, DgCell{}, DgCell{ DgValue::Int(row) } });
		return s;
	};
	DgUndoStack u(3);
	CHECK((!u.CanUndo() && !u.CanRedo()));
	CHECK(u.Undo() == nullptr);
	u.Push(step(1));
	u.Push(step(2));
	u.Push(step(3));
	auto s = u.Undo();
	CHECK((s && s->changes[0].row == 3));
	s = u.Undo();
	CHECK((s && s->changes[0].row == 2));
	CHECK(u.CanRedo());
	s = u.Redo();
	CHECK((s && s->changes[0].row == 2));
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

// ---- Filtering and grouping -----------------------------------------------------------------------------

TEST_CASE("FilterRowLayout", "[layout]") {
	auto set = ThreeColumns();
	auto p = Params(300, 20);
	p.filterRow = 15;
	auto L = DgLayoutColumns(set, p);                               // titles 0-20, the filter row 20-35
	CHECK_INT(L.header, 35);
	CHECK_INT(L.titles, 20);
	DgView view{ 0, 0, 100, 300, 200 };
	CHECK_INT(DgHitTest(L, view, 60, 10, 3).flags, DgHitFlag::Header);
	auto hit = DgHitTest(L, view, 60, 25, 3);
	CHECK_INT(hit.flags, DgHitFlag::FilterRow);
	CHECK_INT(hit.col, 0);
	CHECK_INT(DgHitTest(L, view, 121, 25, 3).flags, DgHitFlag::FilterRow);   // no dividers there
	CHECK_INT(DgHitTest(L, view, 10, 25, 3).flags, DgHitFlag::Corner);
	CHECK_INT(DgHitTest(L, view, 290, 25, 3).flags, DgHitFlag::Empty);
	hit = DgHitTest(L, view, 60, 36, 3);                            // the first row starts under it
	CHECK_INT(hit.flags, DgHitFlag::Cell);
	CHECK_INT(hit.row, 0);
	CHECK_INT(DgFullRows(L, 135), 10);
}

TEST_CASE("RowMapGroups", "[data]") {
	DgRowMap m;
	m.SetOrder({ -1, 3, 1, -2, 0 }, 4);
	CHECK_INT(m.Count(), 5);
	CHECK_INT(m.ToData(0), -1);
	CHECK_INT(m.GroupOf(0), 0);
	CHECK_INT(m.ToData(1), 3);
	CHECK_INT(m.GroupOf(1), -1);
	CHECK_INT(m.GroupOf(3), 1);
	CHECK_INT(m.ToView(0), 4);
	CHECK_INT(m.ToView(2), -1);                                     // in a collapsed group or filtered out
	m.Reset(3);
	CHECK_INT(m.GroupOf(0), -1);
}

TEST_CASE("FilterMatch", "[view]") {
	DgNumberFormat nf;
	DgColumn text;
	DgFilter contains;
	contains.text = L"ELL";
	CHECK(DgFilterMatch(contains, DgValue::Text(L"Hello"), text, nf));
	CHECK(!DgFilterMatch(contains, DgValue::Text(L"Help"), text, nf));
	CHECK(!DgFilterMatch(contains, DgValue{}, text, nf));
	contains.text.clear();
	CHECK(DgFilterMatch(contains, DgValue{}, text, nf));            // nothing to look for: everything
	DgColumn grouped;
	grouped.type = DgColType::Integer;
	grouped.flags = DgColFlag::GroupDigits;
	contains.text = L"1,2";                                         // the text as shown
	CHECK(DgFilterMatch(contains, DgValue::Int(1234), grouped, nf));

	DgFilter range;
	range.kind = DgFilterKind::Range;
	range.min = DgValue::Int(10);
	range.max = DgValue::Float(20.5);
	CHECK(DgFilterMatch(range, DgValue::Int(10), grouped, nf));
	CHECK(DgFilterMatch(range, DgValue::Float(20.5), grouped, nf));
	CHECK(!DgFilterMatch(range, DgValue::Int(21), grouped, nf));
	CHECK(!DgFilterMatch(range, DgValue::Int(9), grouped, nf));
	CHECK(!DgFilterMatch(range, DgValue::Text(L"15"), grouped, nf)); // not a number
	CHECK(!DgFilterMatch(range, DgValue{}, grouped, nf));
	range.max = DgValue{};                                          // open above
	CHECK(DgFilterMatch(range, DgValue::Int(1000), grouped, nf));
	DgFilter dates;
	dates.kind = DgFilterKind::Range;
	dates.min = DgValue::DateTime(100);
	CHECK(DgFilterMatch(dates, DgValue::DateTime(150), grouped, nf));
	CHECK(!DgFilterMatch(dates, DgValue::Int(150), grouped, nf));

	DgFilter values;
	values.kind = DgFilterKind::Values;
	values.values = { L"a", L"c" };
	CHECK(DgFilterMatch(values, DgValue::Text(L"c"), text, nf));
	CHECK(!DgFilterMatch(values, DgValue::Text(L"b"), text, nf));
	CHECK(!DgFilterMatch(values, DgValue{}, text, nf));
	values.blanks = true;
	CHECK(DgFilterMatch(values, DgValue{}, text, nf));
}

// six rows: a category (1), a quantity summed (2), a price averaged (3), a flag (4)
static DgViewInput Products() {
	DgViewInput in;
	in.count = 6;
	DgColumn cat, qty, price, flag;
	cat.id = 1;
	qty.id = 2;
	qty.type = DgColType::Integer;
	qty.aggregate = DgAggKind::Sum;
	price.id = 3;
	price.type = DgColType::Float;
	price.aggregate = DgAggKind::Avg;
	flag.id = 4;
	flag.type = DgColType::Bool;
	in.columns = { cat, qty, price, flag };
	auto T = [](const wchar_t* s) { return DgValue::Text(s); };
	in.values = {
		{ T(L"A"), T(L"B"), T(L"A"), DgValue{}, T(L"B"), T(L"A") },
		{ DgValue::Int(5), DgValue::Int(3), DgValue::Int(1), DgValue::Int(7), DgValue::Int(2), DgValue::Int(4) },
		{ DgValue::Float(1), DgValue::Float(2), DgValue::Float(3), DgValue::Float(4), DgValue::Float(5), DgValue::Float(6) },
		{ DgValue::Bool(true), DgValue::Bool(false), DgValue::Bool(true), DgValue::Bool(true), DgValue::Bool(false), DgValue::Bool(false) },
	};
	return in;
}

TEST_CASE("BuildViewFilterSort", "[view]") {
	using Rows = std::vector<int64_t>;
	auto in = Products();
	CHECK(DgBuildView(in).identity);
	DgFilter f;
	f.id = 1;
	f.text = L"a";
	in.filters = { f };
	auto v = DgBuildView(in);
	CHECK(!v.identity);
	CHECK(v.order == (Rows{ 0, 2, 5 }));
	CHECK(DgFlatten(v, {}) == (Rows{ 0, 2, 5 }));
	in.sortKeys = { { 2, false } };
	CHECK(DgBuildView(in).order == (Rows{ 2, 5, 0 }));
	in.filters.clear();
	CHECK(DgBuildView(in).order == (Rows{ 2, 4, 1, 5, 0, 3 }));
	DgFilter none;
	none.id = 1;
	none.text = L"zzz";
	in.filters = { none };
	v = DgBuildView(in);
	CHECK((v.order.empty() && !v.identity));                          // nothing left, not "everything"
	std::atomic<bool> stop{ true };
	CHECK(DgBuildView(in, &stop).canceled);
}

TEST_CASE("BuildViewGroups", "[view]") {
	using Rows = std::vector<int64_t>;
	auto in = Products();
	in.groupIds = { 1 };
	auto v = DgBuildView(in);
	CHECK(v.order == (Rows{ 0, 2, 5, 1, 4, 3 }));                  // A, B, then the blank one; stable inside
	CHECK_INT(v.groups.size(), 3);
	auto& a = v.groups[0];
	CHECK(a.value == DgValue::Text(L"A"));
	CHECK_STR(a.key, L"1=A");
	CHECK_INT(a.count, 3);
	CHECK((a.first == 0 && a.end == 3 && a.subtreeEnd == 1));
	CHECK_INT(a.aggregates.size(), 2);
	CHECK((a.aggregates[0].id == 2 && a.aggregates[0].value == DgValue::Int(10)));   // 5 + 1 + 4
	CHECK((a.aggregates[1].value.type == DgType::Float && std::fabs(a.aggregates[1].value.d - 10.0 / 3) < 1e-12));
	CHECK(v.groups[2].value.type == DgType::Empty);
	CHECK(DgFlatten(v, {}) == (Rows{ -1, 0, 2, 5, -2, 1, 4, -3, 3 }));
	CHECK(DgFlatten(v, { L"1=B" }) == (Rows{ -1, 0, 2, 5, -2, -3, 3 }));

	in.sortKeys = { { 1, true }, { 2, true } };                     // the group's direction from its key; inside, by quantity
	v = DgBuildView(in);
	CHECK(v.order == (Rows{ 1, 4, 0, 5, 2, 3 }));
	CHECK(v.groups[0].value == DgValue::Text(L"B"));
}

TEST_CASE("BuildViewNested", "[view]") {
	using Rows = std::vector<int64_t>;
	auto in = Products();
	in.groupIds = { 1, 4 };
	auto v = DgBuildView(in);
	// A (F: 5; T: 0, 2), B (F: 1, 4), blank (T: 3)
	CHECK_INT(v.groups.size(), 7);
	CHECK((v.groups[0].level == 0 && v.groups[0].subtreeEnd == 3));
	CHECK((v.groups[1].level == 1 && v.groups[1].count == 1 && v.groups[1].subtreeEnd == 2));
	CHECK_STR(v.groups[1].key, L"1=A\x1F" L"4=false");
	CHECK((v.groups[3].value == DgValue::Text(L"B") && v.groups[3].subtreeEnd == 5));
	CHECK_INT(v.groups[3].count, 2);
	CHECK(v.groups[4].aggregates[0].value == DgValue::Int(5));     // B, false: 3 + 2
	CHECK(DgFlatten(v, {}) == (Rows{ -1, -2, 5, -3, 0, 2, -4, -5, 1, 4, -6, -7, 3 }));
	CHECK(DgFlatten(v, { L"1=A" }) == (Rows{ -1, -4, -5, 1, 4, -6, -7, 3 }));
	CHECK(DgFlatten(v, { L"1=A\x1F" L"4=true" }) == (Rows{ -1, -2, 5, -3, -4, -5, 1, 4, -6, -7, 3 }));

	DgFilter f;                                                     // filtering first: the groups only have what's left
	f.id = 2;
	f.kind = DgFilterKind::Range;
	f.min = DgValue::Int(4);
	in.filters = { f };
	v = DgBuildView(in);
	CHECK(DgFlatten(v, {}) == (Rows{ -1, -2, 5, -3, 0, -4, -5, 3 }));
}

TEST_CASE("Aggregates", "[view]") {
	DgNumberFormat nf;
	DgColumn qty;
	qty.type = DgColType::Integer;
	qty.flags = DgColFlag::GroupDigits;
	CHECK_STR(DgAggregateText({ 2, DgAggKind::Sum, DgValue::Int(12345) }, qty, nf), L"\x03A3 12,345");
	CHECK_STR(DgAggregateText({ 2, DgAggKind::Avg, DgValue::Float(10.0 / 3) }, qty, nf), L"avg 3.33");
	CHECK_STR(DgAggregateText({ 2, DgAggKind::Count, DgValue::Int(1500) }, qty, nf), L"n 1,500");
	CHECK_STR(DgAggregateText({ 2, DgAggKind::Max, DgValue::Int(7) }, qty, nf), L"max 7");
	CHECK_STR(DgAggregateText({ 2, DgAggKind::Sum, DgValue{} }, qty, nf), L"");

	auto in = Products();                                           // count, min and max, mixed with empties
	in.columns[1].aggregate = DgAggKind::Max;
	in.columns[3].aggregate = DgAggKind::Count;
	in.values[1][2] = DgValue{};
	in.groupIds = { 1 };
	auto v = DgBuildView(in);
	auto& a = v.groups[0].aggregates;                               // A: rows 0, 2, 5
	CHECK_INT(a.size(), 3);
	CHECK((a[0].kind == DgAggKind::Max && a[0].value == DgValue::Int(5)));
	CHECK((a[2].kind == DgAggKind::Count && a[2].value == DgValue::Int(3)));
}

TEST_CASE("DistinctTexts", "[view]") {
	DgNumberFormat nf;
	DgColumn text;
	std::vector<DgValue> values{ DgValue::Text(L"B"), DgValue::Text(L"A"), DgValue{}, DgValue::Text(L"A"), DgValue::Text(L"C") };
	auto at = [&](int64_t row) -> const DgValue* { return &values[(size_t)row]; };
	bool blanks, more;
	auto texts = DgDistinctTexts(5, at, text, nf, nullptr, 10, blanks, more);
	CHECK(texts == (std::vector<std::wstring>{ L"A", L"B", L"C" }));
	CHECK((blanks && !more));
	texts = DgDistinctTexts(5, at, text, nf, nullptr, 2, blanks, more);
	CHECK(texts == (std::vector<std::wstring>{ L"A", L"B" }));
	CHECK(more);
	std::vector<DgValue> numbers{ DgValue::Int(10), DgValue::Int(9), DgValue::Int(100) };   // by value, not text
	auto atNumber = [&](int64_t row) -> const DgValue* { return &numbers[(size_t)row]; };
	DgColumn in;
	in.type = DgColType::Integer;
	texts = DgDistinctTexts(3, atNumber, in, nf, nullptr, 10, blanks, more);
	CHECK(texts == (std::vector<std::wstring>{ L"9", L"10", L"100" }));
	CHECK(!blanks);
}

TEST_CASE("SkipGroupRows", "[caret]") {
	auto isGroup = [](int64_t row) { return row == 0 || row == 3; };
	CHECK_INT(DgSkipGroupRows(0, 1, 5, isGroup), 1);
	CHECK_INT(DgSkipGroupRows(3, 1, 5, isGroup), 4);
	CHECK_INT(DgSkipGroupRows(3, -1, 5, isGroup), 2);
	CHECK_INT(DgSkipGroupRows(0, -1, 5, isGroup), 1);               // nothing above: the other way
	CHECK_INT(DgSkipGroupRows(4, 1, 5, isGroup), 4);
	CHECK_INT(DgSkipGroupRows(9, 1, 5, isGroup), 4);                // clamped
	CHECK_INT(DgSkipGroupRows(0, 1, 2, [](int64_t) { return true; }), -1);
	CHECK_INT(DgSkipGroupRows(0, 1, 0, isGroup), -1);
}


// ---- Phase 5: the new row, finding, conditional formats, the state -----------------------------------------

TEST_CASE("RowMapNewRow", "[data]") {
	DgRowMap m;
	m.Reset(3);
	m.SetNewRow(true);
	CHECK_INT(m.Count(), 4);
	CHECK(m.IsNewRow(3));
	CHECK(!m.IsNewRow(2));
	CHECK_INT(m.ToData(3), DgRowMap::NewRow);
	CHECK_INT(m.ToView(DgRowMap::NewRow), 3);
	CHECK_INT(m.GroupOf(3), -1);
	CHECK_INT(m.ToData(4), -1);

	// after the groups' rows and the data rows in view, whatever the order
	m.SetOrder({ -1, 2, 0 }, 3);
	CHECK_INT(m.Count(), 4);
	CHECK_INT(m.ToData(3), DgRowMap::NewRow);
	CHECK_INT(m.ToView(DgRowMap::NewRow), 3);
	CHECK_INT(m.ToView(1), -1);

	m.SetNewRow(false);
	CHECK_INT(m.Count(), 3);
	CHECK_INT(m.ToView(DgRowMap::NewRow), -1);
	CHECK_INT(m.ToData(3), -1);

	DgRowMap empty;
	empty.SetNewRow(true);
	CHECK_INT(empty.Count(), 1);
	CHECK_INT(empty.ToData(0), DgRowMap::NewRow);
}

TEST_CASE("TextMatches", "[find]") {
	CHECK(DgTextMatches(L"Hello World", L"world", 0));
	CHECK(!DgTextMatches(L"Hello World", L"world", DgFindFlag::MatchCase));
	CHECK(DgTextMatches(L"Hello World", L"World", DgFindFlag::MatchCase));
	CHECK(!DgTextMatches(L"Hello World", L"hello", DgFindFlag::WholeCell));
	CHECK(DgTextMatches(L"Hello", L"hELLO", DgFindFlag::WholeCell));
	CHECK(!DgTextMatches(L"Hello", L"hELLO", DgFindFlag::WholeCell | DgFindFlag::MatchCase));
	CHECK(!DgTextMatches(L"Hello", L"", 0));                                // nothing to look for
}

namespace {
	// a grid of 4 rows and 3 columns where the cells listed match
	struct FindGrid {
		std::vector<DgPos> hits;
		bool operator()(int64_t r, int c) const {
			return std::find(hits.begin(), hits.end(), DgPos{ r, c }) != hits.end();
		}
	};

	DgPos FindIn(const FindGrid& g, DgPos start, bool inclusive, unsigned flags, int first = 0, int last = 2, int64_t maxRows = 100) {
		DgPos found{ -1, -1 };
		if (!DgFindCell(start, inclusive, flags, 4, first, last, maxRows, g, found))
			return { -1, -1 };
		return found;
	}
}

TEST_CASE("FindCell", "[find]") {
	FindGrid g{ { { 0, 1 }, { 2, 0 }, { 3, 2 } } };
	CHECK(FindIn(g, { 0, 1 }, false, 0) == DgPos{ 2, 0 });               // across, then down
	CHECK(FindIn(g, { 0, 1 }, true, 0) == DgPos{ 0, 1 });                // the start counts
	CHECK(FindIn(g, { 2, 0 }, false, 0) == DgPos{ 3, 2 });
	CHECK(FindIn(g, { 3, 2 }, false, 0) == DgPos{ 0, 1 });               // wraps past the end
	CHECK(FindIn(g, { 3, 2 }, false, DgFindFlag::NoWrap) == DgPos{ -1, -1 });
	CHECK(FindIn(g, { 2, 0 }, false, DgFindFlag::Backward) == DgPos{ 0, 1 });   // left, then up
	CHECK(FindIn(g, { 0, 1 }, false, DgFindFlag::Backward) == DgPos{ 3, 2 });   // wraps past the start
	CHECK(FindIn(g, { 0, 1 }, false, DgFindFlag::Backward | DgFindFlag::NoWrap) == DgPos{ -1, -1 });
	CHECK(FindIn(g, { 1, 1 }, false, 0, 2, 2) == DgPos{ 3, 2 });         // in one column

	// the only match is the start: found last, after every other cell
	FindGrid one{ { { 1, 1 } } };
	CHECK(FindIn(one, { 1, 1 }, false, 0) == DgPos{ 1, 1 });
	CHECK(FindIn(one, { 1, 1 }, false, DgFindFlag::Backward) == DgPos{ 1, 1 });
	CHECK(FindIn(one, { 1, 2 }, false, 0) == DgPos{ 1, 1 });             // the start's row again, up to it
	CHECK(FindIn(one, { 1, 0 }, false, DgFindFlag::Backward) == DgPos{ 1, 1 });

	// every cell is looked at once
	int calls = 0;
	DgPos found;
	DgFindCell({ 1, 1 }, false, 0, 4, 0, 2, 100, [&](int64_t, int) { calls++; return false; }, found);
	CHECK(calls == 12);
	calls = 0;
	DgFindCell({ 1, 1 }, true, DgFindFlag::Backward, 4, 0, 2, 100, [&](int64_t, int) { calls++; return false; }, found);
	CHECK(calls == 12);

	// at most so many rows
	int64_t looked = 0;
	CHECK(!DgFindCell({ 0, 0 }, true, 0, 4, 0, 2, 2, FindGrid{ { { 3, 0 } } }, found, &looked));
	CHECK_INT(looked, 2);
	CHECK(!DgFindCell({ 0, 0 }, true, 0, 0, 0, 2, 100, g, found));         // no rows

	// stopped (out of time): asked before each row
	int asked = 0;
	CHECK(!DgFindCell({ 0, 0 }, true, 0, 4, 0, 2, 100, FindGrid{ { { 3, 0 } } }, found, &looked, [&] { return ++asked > 2; }));
	CHECK_INT(looked, 2);
	CHECK(DgFindCell({ 0, 0 }, true, 0, 4, 0, 2, 100, FindGrid{ { { 1, 0 } } }, found, &looked, [] { return false; }));
}

TEST_CASE("RuleMatches", "[format]") {
	DgNumberFormat nf;
	DgColumn number;
	number.type = DgColType::Integer;
	DgFormatRule r;
	r.op = DgCondOp::Greater;
	r.a = DgValue::Int(10);
	CHECK(DgRuleMatches(r, DgValue::Int(11), number, nf));
	CHECK(DgRuleMatches(r, DgValue::Float(10.5), number, nf));             // numbers with numbers
	CHECK(!DgRuleMatches(r, DgValue::Int(10), number, nf));
	CHECK(!DgRuleMatches(r, DgValue{}, number, nf));                         // empty: not compared
	r.op = DgCondOp::LessEqual;
	CHECK(DgRuleMatches(r, DgValue::Int(10), number, nf));
	CHECK(!DgRuleMatches(r, DgValue::DateTime(5), number, nf));              // a date isn't a number

	r.op = DgCondOp::Between;
	r.b = DgValue::Int(20);
	CHECK(DgRuleMatches(r, DgValue::Int(10), number, nf));
	CHECK(DgRuleMatches(r, DgValue::Int(20), number, nf));
	CHECK(!DgRuleMatches(r, DgValue::Int(21), number, nf));

	DgColumn text;
	r.op = DgCondOp::Equal;
	r.a = DgValue::Text(L"apple");
	CHECK(DgRuleMatches(r, DgValue::Text(L"APPLE"), text, nf));              // case ignored
	CHECK(!DgRuleMatches(r, DgValue::Text(L"apples"), text, nf));
	r.op = DgCondOp::NotEqual;
	CHECK(DgRuleMatches(r, DgValue::Text(L"pear"), text, nf));
	CHECK(DgRuleMatches(r, DgValue{}, text, nf));                            // an empty cell isn't "apple"
	CHECK(!DgRuleMatches(r, DgValue::Text(L"Apple"), text, nf));

	DgColumn priority;
	priority.type = DgColType::Enum;
	priority.enumItems = { L"Low", L"High" };
	r.op = DgCondOp::Equal;
	r.a = DgValue::Text(L"high");
	CHECK(DgRuleMatches(r, DgValue::Int(1), priority, nf));                  // text against an enum's name
	CHECK(!DgRuleMatches(r, DgValue::Int(0), priority, nf));

	r.op = DgCondOp::Contains;
	r.a = DgValue::Text(L"IG");
	CHECK(DgRuleMatches(r, DgValue::Int(1), priority, nf));
	CHECK(!DgRuleMatches(r, DgValue{}, priority, nf));
	DgColumn grouped = number;
	grouped.flags = DgColFlag::GroupDigits;
	r.a = DgValue::Text(L"1,2");
	CHECK(DgRuleMatches(r, DgValue::Int(1234), grouped, nf));                // in the shown text

	r.op = DgCondOp::Empty;
	CHECK(DgRuleMatches(r, DgValue{}, text, nf));
	CHECK(!DgRuleMatches(r, DgValue::Text(L"x"), text, nf));
	r.op = DgCondOp::NotEmpty;
	CHECK(DgRuleMatches(r, DgValue::Bool(false), text, nf));

	DgColumn date;
	date.type = DgColType::DateTime;
	r.op = DgCondOp::Less;
	r.a = DgValue::DateTime(1000);
	CHECK(DgRuleMatches(r, DgValue::DateTime(999), date, nf));
	CHECK(!DgRuleMatches(r, DgValue::DateTime(1000), date, nf));
}

TEST_CASE("ResolveLook", "[format]") {
	DgNumberFormat nf;
	DgColumn qty;
	qty.id = 1;
	qty.type = DgColType::Integer;
	DgColumn name;
	name.id = 2;
	std::vector<DgFormatRule> rules(3);
	rules[0].column = 1;                    // negative: red text, bold
	rules[0].op = DgCondOp::Less;
	rules[0].a = DgValue::Int(0);
	rules[0].text = 0x0000FF;
	rules[0].bold = true;
	rules[1].column = 1;                    // under 10: the whole row yellow, gray text
	rules[1].op = DgCondOp::Less;
	rules[1].a = DgValue::Int(10);
	rules[1].wholeRow = true;
	rules[1].back = 0x00FFFF;
	rules[1].text = 0x808080;
	rules[2].column = 2;                    // a name with "x": green
	rules[2].op = DgCondOp::Contains;
	rules[2].a = DgValue::Text(L"x");
	rules[2].back = 0x00FF00;

	DgValue q, n = DgValue::Text(L"box");
	auto valueOf = [&](int id) -> const DgValue* { return id == 1 ? &q : id == 2 ? &n : nullptr; };
	auto columnOf = [&](int id) -> const DgColumn* { return id == 1 ? &qty : id == 2 ? &name : nullptr; };

	q = DgValue::Int(-5);
	auto look = DgResolveLook(rules, 1, valueOf, columnOf, nf);
	CHECK(look.text == 0x0000FF);           // the first rule's, which comes first
	CHECK(look.back == 0x00FFFF);           // the second's: the first leaves it
	CHECK(look.bold);
	look = DgResolveLook(rules, 2, valueOf, columnOf, nf);
	CHECK(look.text == 0x808080);           // the row's rule; the first rule is only the quantity's
	CHECK(look.back == 0x00FFFF);           // before the name's own rule
	CHECK(!look.bold);

	q = DgValue::Int(50);
	look = DgResolveLook(rules, 2, valueOf, columnOf, nf);
	CHECK(look == DgCellLook{ DgNoColor, 0x00FF00, false });
	look = DgResolveLook(rules, 1, valueOf, columnOf, nf);
	CHECK(look == DgCellLook{});

	q = DgValue{};                          // empty: no number rule matches
	CHECK(DgResolveLook(rules, 1, valueOf, columnOf, nf) == DgCellLook{});
	auto gone = [](int) -> const DgColumn* { return nullptr; };
	q = DgValue::Int(-5);
	CHECK(DgResolveLook(rules, 1, valueOf, gone, nf) == DgCellLook{});   // the rules' columns are gone
}

TEST_CASE("StateRoundTrip", "[state]") {
	DgState s;
	s.columns = { { 3, 120, 0, false }, { 1, 80, 2, false }, { 2, 60, 0, true } };
	s.frozen = 1;
	s.sortKeys = { { 2, false }, { 3, true } };
	s.groupIds = { 1 };
	DgFilter contains;
	contains.id = 3;
	contains.text = L"a;b,c:d=e%f\tg";      // every separator, escaped
	DgFilter range;
	range.id = 1;
	range.kind = DgFilterKind::Range;
	range.min = DgValue::Float(0.1);
	range.max = DgValue{};
	DgFilter values;
	values.id = 2;
	values.kind = DgFilterKind::Values;
	values.values = { L"", L"x:y", L"\x00E9t\x00E9" };
	values.blanks = true;
	DgFilter dates;
	dates.id = 4;
	dates.kind = DgFilterKind::Range;
	dates.min = DgValue::DateTime(133000000000000000);
	dates.max = DgValue::Int(-7);
	s.filters = { contains, range, values, dates };

	auto text = DgSaveState(s);
	CHECK(text.find(L'\t') == std::wstring::npos);
	CHECK(text.rfind(L"dgv1;cols=3:120,1:80:f2,2:60:h;frozen=1;sort=2,3d;group=1;filter=", 0) == 0);

	DgState back;
	REQUIRE(DgLoadState(text, back));
	CHECK(back.columns == s.columns);
	CHECK(back.frozen == 1);
	CHECK(back.sortKeys == s.sortKeys);
	CHECK(back.groupIds == s.groupIds);
	REQUIRE(back.filters.size() == 4);
	CHECK(back.filters[0].kind == DgFilterKind::Contains);
	CHECK_STR(back.filters[0].text, contains.text);
	CHECK(back.filters[1].min == range.min);   // all the digits
	CHECK(back.filters[1].max.type == DgType::Empty);
	CHECK(back.filters[2].values == values.values);
	CHECK(back.filters[2].blanks);
	CHECK(back.filters[3].min == dates.min);
	CHECK(back.filters[3].max == dates.max);
	CHECK(DgSaveState(back) == text);

	// empty lists, and keys a later version might add
	DgState none;
	REQUIRE(DgLoadState(L"dgv1;cols=;frozen=0;sort=;group=;future=1", none));
	CHECK(none.columns.empty());
	CHECK(none.sortKeys.empty());

	// not a state: nothing changes
	DgState kept = s;
	CHECK(!DgLoadState(L"", kept));
	CHECK(!DgLoadState(L"dgv2;cols=1:2", kept));
	CHECK(!DgLoadState(L"dgv1;cols=x:2", kept));
	CHECK(!DgLoadState(L"dgv1;cols=1:2:q", kept));
	CHECK(!DgLoadState(L"dgv1;frozen=-1", kept));
	CHECK(!DgLoadState(L"dgv1;filter=1:c:%00", kept));       // a cut escape
	CHECK(!DgLoadState(L"dgv1;filter=1:r:i1", kept));
	CHECK(!DgLoadState(L"dgv1;filter=1:z:x", kept));
	CHECK(!DgLoadState(L"dgv1;sort=99999999999", kept));
	CHECK(kept.columns == s.columns);
}

// ---- Phase 6: rows in undo steps, the filter row's syntax --------------------------------------------------------

namespace {
	// a store of rows whose slot 0 holds their names
	DgStore Named(std::initializer_list<const wchar_t*> names) {
		DgStore s;
		s.Resize((int64_t)names.size());
		int64_t r = 0;
		for (auto n : names)
			s.Set(r++, 0, DgCell{ DgValue::Text(n) });
		return s;
	}

	std::wstring Names(const DgStore& s) {
		std::wstring all;
		for (int64_t r = 0; r < s.Count(); r++) {
			auto c = s.Get(r, 0);
			all += c && c->value.type == DgType::Text ? c->value.s : L"_";
		}
		return all;
	}
}

TEST_CASE("RowSteps", "[edit]") {
	auto s = Named({ L"a", L"b", L"c", L"d", L"e" });
	DgEditStep del;
	del.deleted = { { 1, s.Row(1) }, { 3, s.Row(3) } };   // b and d
	DgApplyRowChanges(s, del, true);
	CHECK_STR(Names(s), L"ace");
	DgApplyRowChanges(s, del, false);
	CHECK_STR(Names(s), L"abcde");                          // back, with their cells

	DgEditStep ins;
	ins.inserted = { { 0, {} }, { 2, {} }, { 7, {} } };      // at their final places
	DgApplyRowChanges(s, ins, true);
	CHECK_STR(Names(s), L"_a_bcde_");
	DgApplyRowChanges(s, ins, false);
	CHECK_STR(Names(s), L"abcde");

	DgEditStep both;                                         // deleted first, then inserted
	both.deleted = { { 0, s.Row(0) } };
	both.inserted = { { 4, {} } };
	DgApplyRowChanges(s, both, true);
	CHECK_STR(Names(s), L"bcde_");
	DgApplyRowChanges(s, both, false);
	CHECK_STR(Names(s), L"abcde");

	CHECK(s.Row(9).empty());
	CHECK(!s.InsertRow(9, {}));
	CHECK(s.InsertRow(5, {}));
	CHECK_INT(s.Count(), 6);
}

TEST_CASE("RowAfter", "[edit]") {
	DgEditStep del;
	del.deleted = { { 1, {} }, { 3, {} } };
	CHECK_INT(DgRowAfter(0, del, true), 0);
	CHECK_INT(DgRowAfter(2, del, true), 1);
	CHECK_INT(DgRowAfter(4, del, true), 2);
	CHECK_INT(DgRowAfter(3, del, true), 2);                 // deleted: where the next row is now
	CHECK_INT(DgRowAfter(2, del, false), 4);                // undone: the rows come back before it
	CHECK_INT(DgRowAfter(0, del, false), 0);
	CHECK_INT(DgRowAfter(1, del, false), 2);                // "c": b comes back before it

	DgEditStep ins;
	ins.inserted = { { 0, {} }, { 2, {} } };
	CHECK_INT(DgRowAfter(0, ins, true), 1);
	CHECK_INT(DgRowAfter(1, ins, true), 3);
	CHECK_INT(DgRowAfter(3, ins, false), 1);
	CHECK_INT(DgRowAfter(-1, ins, true), -1);               // no caret stays none
}

TEST_CASE("FilterText", "[view]") {
	DgNumberFormat nf;
	DgColumn qty;
	qty.id = 4;
	qty.type = DgColType::Integer;
	auto f = DgParseFilterText(L">100", qty, nf);
	CHECK(f.kind == DgFilterKind::Range);
	CHECK(f.id == 4);
	CHECK_STR(f.text, L">100");                               // kept for the filter row
	CHECK(f.min == DgValue::Int(101));
	CHECK(f.max.type == DgType::Empty);
	f = DgParseFilterText(L" <= 1,000 ", qty, nf);
	CHECK(f.max == DgValue::Int(1000));
	CHECK(f.min.type == DgType::Empty);
	f = DgParseFilterText(L"<5", qty, nf);
	CHECK(f.max == DgValue::Int(4));
	f = DgParseFilterText(L"=7", qty, nf);
	CHECK(f.min == DgValue::Int(7));
	CHECK(f.max == DgValue::Int(7));
	f = DgParseFilterText(L"10..20", qty, nf);
	CHECK(f.min == DgValue::Int(10));
	CHECK(f.max == DgValue::Int(20));
	f = DgParseFilterText(L"..20", qty, nf);
	CHECK(f.min.type == DgType::Empty);
	CHECK(f.max == DgValue::Int(20));
	f = DgParseFilterText(L"10..", qty, nf);
	CHECK(f.min == DgValue::Int(10));
	CHECK(f.max.type == DgType::Empty);
	CHECK(DgParseFilterText(L"12", qty, nf).kind == DgFilterKind::Contains);   // no operator: the digits anywhere
	CHECK(DgParseFilterText(L">abc", qty, nf).kind == DgFilterKind::Contains); // doesn't parse
	CHECK(DgParseFilterText(L"..", qty, nf).kind == DgFilterKind::Contains);
	CHECK(DgParseFilterText(L">", qty, nf).kind == DgFilterKind::Contains);

	DgColumn price;
	price.type = DgColType::Float;
	f = DgParseFilterText(L">4000", price, nf);
	REQUIRE(f.kind == DgFilterKind::Range);
	CHECK(f.min.d > 4000.0);                                 // the next double: 4000 itself is out
	CHECK(DgFilterMatch(f, DgValue::Float(4000.01), price, nf));
	CHECK(!DgFilterMatch(f, DgValue::Float(4000), price, nf));
	CHECK(!DgFilterMatch(f, DgValue::Int(4000), price, nf));

	DgColumn date;
	date.type = DgColType::DateTime;
	int64_t day = 0;
	REQUIRE(DgParseIsoDateTime(L"2026-11-17", day));
	constexpr int64_t Day = 864'000'000'000;
	f = DgParseFilterText(L"=2026-11-17", date, nf);         // the whole day
	CHECK(f.min == DgValue::DateTime(day));
	CHECK(f.max == DgValue::DateTime(day + Day - 1));
	f = DgParseFilterText(L">2026-11-17", date, nf);         // after it
	CHECK(f.min == DgValue::DateTime(day + Day));
	f = DgParseFilterText(L"<=2026-11-17", date, nf);
	CHECK(f.max == DgValue::DateTime(day + Day - 1));
	f = DgParseFilterText(L"<2026-11-17", date, nf);
	CHECK(f.max == DgValue::DateTime(day - 1));
	f = DgParseFilterText(L"2026-11-01..2026-11-17", date, nf);
	CHECK(f.min == DgValue::DateTime(day - 16 * Day));
	CHECK(f.max == DgValue::DateTime(day + Day - 1));
	f = DgParseFilterText(L">2026-11-17 13:00", date, nf);   // a time: just after it
	CHECK(f.min == DgValue::DateTime(day + 13 * Day / 24 + 1));

	DgColumn category;
	category.type = DgColType::Enum;
	category.enumItems = { L"Hardware", L"Tools" };
	f = DgParseFilterText(L"=tOOls", category, nf);           // by its name, case ignored
	CHECK(f.kind == DgFilterKind::Values);
	CHECK(f.values == std::vector<std::wstring>{ L"tools" });
	CHECK(DgFilterMatch(f, DgValue::Int(1), category, nf));
	CHECK(!DgFilterMatch(f, DgValue::Int(0), category, nf));
	CHECK(DgParseFilterText(L">tools", category, nf).kind == DgFilterKind::Contains);   // not ordered

	DgColumn text;
	f = DgParseFilterText(L"=Cog 3", text, nf);
	CHECK(f.kind == DgFilterKind::Values);
	CHECK(f.values == std::vector<std::wstring>{ L"cog 3" });
	CHECK(DgFilterMatch(f, DgValue::Text(L"COG 3"), text, nf));
	CHECK(!DgFilterMatch(f, DgValue::Text(L"Cog 33"), text, nf));
	DgFilter exact;   // the menu's (no text): exact
	exact.kind = DgFilterKind::Values;
	exact.values = { L"Cog 3" };
	CHECK(!DgFilterMatch(exact, DgValue::Text(L"cog 3"), text, nf));
	CHECK(DgParseFilterText(L"=", text, nf).kind == DgFilterKind::Contains);
	f = DgParseFilterText(L"a..b", text, nf);
	CHECK(f.kind == DgFilterKind::Contains);
	CHECK_STR(f.text, L"a..b");
}

TEST_CASE("StateTypedFilters", "[state]") {
	DgNumberFormat nf;
	DgColumn qty;
	qty.id = 4;
	qty.type = DgColType::Integer;
	DgColumn category;
	category.id = 3;
	category.type = DgColType::Enum;
	category.enumItems = { L"Hardware", L"Tools" };
	DgState s;
	s.filters = { DgParseFilterText(L">100", qty, nf), DgParseFilterText(L"=Tools", category, nf) };
	DgFilter menu;   // from the menu: no text
	menu.id = 5;
	menu.kind = DgFilterKind::Values;
	menu.values = { L"x" };
	s.filters.push_back(menu);
	auto text = DgSaveState(s);
	CHECK(text.find(L"filter=4:r:i101:e:>100") != std::wstring::npos);
	CHECK(text.find(L"filter=3:V:0:%003DTools:tools") != std::wstring::npos);
	CHECK(text.find(L"filter=5:v:0:x") != std::wstring::npos);
	DgState back;
	REQUIRE(DgLoadState(text, back));
	REQUIRE(back.filters.size() == 3);
	CHECK_STR(back.filters[0].text, L">100");
	CHECK(back.filters[0].min == DgValue::Int(101));
	CHECK_STR(back.filters[1].text, L"=Tools");
	CHECK(back.filters[1].values == std::vector<std::wstring>{ L"tools" });
	CHECK(back.filters[2].text.empty());
	CHECK(DgSaveState(back) == text);
	CHECK(!DgLoadState(L"dgv1;filter=1:V:0", back));          // "V" needs its text
	CHECK(!DgLoadState(L"dgv1;filter=1:r:i1:e:", back));      // an empty text isn't written
}
