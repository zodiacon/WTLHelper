// Unit tests for the calendar view model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: dates and the month grid, the item store and a virtual cache's
// missing days, splitting items into pieces, placing overlaps, packing the strip, the day view's boxes and hit testing,
// dragging, scrolling and zoom, the tab order and undo. Anything that needs an HWND is checked by hand with
// CalendarViewDemo.

#include "CalendarViewModel.h"

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

static void CheckInt(long long actual, long long expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: %lld, want: %lld\n", g_currentTest, line, expr, actual, expected);
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
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

static const int64_t D0 = ClvDayFromDate(2026, 10, 26);   // a Monday

// D0 at h:mm, plus days
static ClvTime At(int days, int h, int m = 0) {
	return ClvTimeOf(D0 + days, h * 60 + m);
}

static ClvItem Timed(uint64_t id, ClvTime start, ClvTime end) {
	ClvItem item;
	item.id = id;
	item.start = start;
	item.end = end;
	return item;
}

static ClvItem AllDay(uint64_t id, int firstDay, int days) {
	ClvItem item;
	item.id = id;
	item.start = At(firstDay, 0);
	item.end = At(firstDay + days, 0);
	item.allDay = true;
	return item;
}

static bool Same(const RECT& rc, int left, int top, int right, int bottom) {
	return rc.left == left && rc.top == top && rc.right == right && rc.bottom == bottom;
}

static std::vector<uint64_t> Ids(const ClvItemStore& store, const std::vector<int>& indexes) {
	std::vector<uint64_t> ids;
	for (int i : indexes)
		ids.push_back(store.Items()[i].id);
	return ids;
}

// ---- Tests -----------------------------------------------------------------------

TEST(Dates) {
	CHECK_INT(ClvDayFromDate(1601, 1, 1), 0);
	CHECK_INT(ClvWeekday(0), 1);
	CHECK_INT(ClvWeekday(D0), 1);
	auto date = ClvDateOf(D0);
	CHECK(date.year == 2026 && date.month == 10 && date.day == 26);
	CHECK_INT(ClvMinuteOf(At(0, 9, 30)), 570);
	CHECK_INT(ClvDayOf(-1), -1);
	CHECK_INT(ClvMinuteOf(-1), 1439);

	CHECK_INT(ClvDaysInMonth(2024, 2), 29);
	CHECK_INT(ClvDaysInMonth(2100, 2), 28);
	CHECK_INT(ClvDaysInMonth(2000, 2), 29);
	CHECK_INT(ClvDaysInMonth(2026, 4), 30);
	CHECK_INT(ClvDaysInMonth(2026, 12), 31);

	CHECK_INT(ClvWeekStart(D0 + 3, 0), D0 - 1);   // a Thursday's Sunday
	CHECK_INT(ClvWeekStart(D0 + 3, 1), D0);
	CHECK_INT(ClvWeekStart(D0 - 1, 1), D0 - 7);   // a Sunday's Monday
	CHECK_INT(ClvWeekStart(D0 - 1, 0), D0 - 1);
	CHECK_INT(ClvMonthStart(D0), ClvDayFromDate(2026, 10, 1));
	CHECK_INT(ClvAddMonths(ClvDayFromDate(2026, 1, 31), 1), ClvDayFromDate(2026, 2, 28));
	CHECK_INT(ClvAddMonths(ClvDayFromDate(2026, 1, 15), -1), ClvDayFromDate(2025, 12, 15));
	CHECK_INT(ClvAddMonths(ClvDayFromDate(2026, 3, 31), 11), ClvDayFromDate(2027, 2, 28));
	CHECK_INT(ClvAddMonths(ClvDayFromDate(2026, 3, 31), -13), ClvDayFromDate(2025, 2, 28));

	auto st = ClvToSystemTime(At(2, 14, 45));
	CHECK(st.wYear == 2026 && st.wMonth == 10 && st.wDay == 28 && st.wDayOfWeek == 3);
	CHECK(st.wHour == 14 && st.wMinute == 45);
	CHECK_INT(ClvFromSystemTime(st), At(2, 14, 45));
}

TEST(MonthGrid) {
	// October 2026 starts on a Thursday
	auto grid = ClvMonthGridOf(D0, 0);
	CHECK_INT(grid.first, ClvDayFromDate(2026, 9, 27));
	CHECK_INT(grid.weeks, 5);
	grid = ClvMonthGridOf(D0, 1);
	CHECK_INT(grid.first, ClvDayFromDate(2026, 9, 28));
	CHECK_INT(grid.weeks, 5);
	// February 2026 starts on a Sunday: four weeks exactly
	grid = ClvMonthGridOf(ClvDayFromDate(2026, 2, 14), 0);
	CHECK_INT(grid.first, ClvDayFromDate(2026, 2, 1));
	CHECK_INT(grid.weeks, 4);
	// August 2026 starts on a Saturday: six
	grid = ClvMonthGridOf(ClvDayFromDate(2026, 8, 31), 0);
	CHECK_INT(grid.first, ClvDayFromDate(2026, 7, 26));
	CHECK_INT(grid.weeks, 6);
}

TEST(Ranges) {
	ClvRange r{ D0, 3 };
	CHECK_INT(r.Last(), D0 + 2);
	CHECK_INT(r.Start(), At(0, 0));
	CHECK_INT(r.End(), At(3, 0));
	CHECK(r.Contains(D0 + 2) && !r.Contains(D0 + 3) && !r.Contains(D0 - 1));

	auto m = ClvMissing({ 10, 7 }, { 12, 3 });
	CHECK_INT(m.size(), 2);
	CHECK(m.size() == 2 && m[0].first == 10 && m[0].days == 2 && m[1].first == 15 && m[1].days == 2);
	m = ClvMissing({ 10, 7 }, { 0, 0 });
	CHECK(m.size() == 1 && m[0].first == 10 && m[0].days == 7);
	CHECK(ClvMissing({ 10, 7 }, { 5, 20 }).empty());
	m = ClvMissing({ 10, 7 }, { 14, 10 });
	CHECK(m.size() == 1 && m[0].first == 10 && m[0].days == 4);
	m = ClvMissing({ 10, 7 }, { 20, 3 });
	CHECK(m.size() == 1 && m[0].first == 10 && m[0].days == 7);
	m = ClvMissing({ 10, 7 }, { 8, 4 });
	CHECK(m.size() == 1 && m[0].first == 12 && m[0].days == 5);

	ClvWorkWeek w;
	CHECK(w.IsWork(D0, 8 * 60));
	CHECK(!w.IsWork(D0, 17 * 60));
	CHECK(!w.IsWork(D0, 7 * 60 + 59));
	CHECK(!w.IsWork(D0 + 5, 10 * 60));   // Saturday
}

TEST(Store) {
	ClvItemStore s;
	CHECK(!s.Add(Timed(0, At(0, 9), At(0, 10))));
	CHECK(s.Add(Timed(1, At(0, 9), At(0, 10))));
	CHECK(!s.Add(Timed(1, At(0, 11), At(0, 12))));
	CHECK(!s.Add(Timed(2, At(0, 11), At(0, 10))));
	CHECK(s.Add(Timed(2, At(0, 9), At(0, 11))));
	CHECK(s.Add(Timed(3, At(0, 8), At(0, 9))));
	CHECK(s.Add(Timed(4, At(1, 12), At(1, 12))));   // empty
	CHECK(s.Add(AllDay(5, 0, 2)));
	CHECK(s.Add(Timed(6, At(1, 0), At(1, 0))));     // empty, at midnight
	CHECK_INT(s.Items().size(), 6);
	CHECK_INT(s.Index(3), 2);
	CHECK_INT(s.Index(99), -1);
	CHECK(s.Find(2) && s.Find(2)->end == At(0, 11));

	auto ids = Ids(s, s.InRange(At(0, 0), At(1, 0)));
	CHECK((ids == std::vector<uint64_t>{ 5, 3, 2, 1 }));
	ids = Ids(s, s.InRange(At(0, 0), At(2, 0)));
	CHECK((ids == std::vector<uint64_t>{ 5, 3, 2, 1, 6, 4 }));
	ids = Ids(s, s.InRange(At(1, 0), At(2, 0)));
	CHECK((ids == std::vector<uint64_t>{ 5, 6, 4 }));
	ids = Ids(s, s.InRange(At(0, 9), At(0, 10)));   // 3 ends as it starts
	CHECK((ids == std::vector<uint64_t>{ 5, 2, 1 }));

	auto item = *s.Find(1);
	item.title = L"Changed";
	CHECK(s.Set(item));
	CHECK(s.Find(1)->title == L"Changed");
	item.id = 99;
	CHECK(!s.Set(item));
	item.id = 1;
	item.end = item.start - 1;
	CHECK(!s.Set(item));

	CHECK(s.Remove(3));
	CHECK(!s.Remove(3));
	CHECK(!s.Find(3));

	// a virtual cache moving on to the next day keeps what's over it
	auto removed = s.Keep(At(1, 0), At(2, 0));
	CHECK_INT(removed.size(), 2);
	CHECK(removed.size() == 2 && removed[0].id == 1 && removed[1].id == 2);
	CHECK_INT(s.Items().size(), 3);
	CHECK(s.Find(5) && s.Find(4) && s.Find(6));
}

TEST(Split) {
	ClvItemStore s;
	s.Add(Timed(1, At(0, 22), At(1, 2)));           // over midnight
	s.Add(AllDay(2, 1, 2));
	s.Add(AllDay(3, -1, 5));                        // past both ends
	s.Add(Timed(4, At(0, 10), At(1, 12)));          // 26 hours: the strip's
	s.Add(Timed(5, At(5, 10), At(5, 11)));          // not shown
	s.Add(Timed(6, At(2, 23), At(3, 1)));           // cut at the end
	ClvRange range{ D0, 3 };
	std::vector<ClvPiece> timed, strip;
	ClvSplit(s, s.InRange(range.Start(), range.End()), range, timed, strip);

	CHECK_INT(timed.size(), 3);
	CHECK_INT(strip.size(), 3);
	if (timed.size() == 3) {
		CHECK(timed[0].day == 0 && timed[0].start == 22 * 60 && timed[0].end == 1440 && !timed[0].before && timed[0].after);
		CHECK(timed[1].day == 1 && timed[1].start == 0 && timed[1].end == 120 && timed[1].before && !timed[1].after);
		CHECK(timed[2].day == 2 && timed[2].start == 23 * 60 && timed[2].end == 1440 && timed[2].after);
		CHECK_INT(s.Items()[timed[0].item].id, 1);
	}
	if (strip.size() == 3) {
		// by start: 3 (the day before), 4 (D0 10:00), 2 (D1)
		CHECK_INT(s.Items()[strip[0].item].id, 3);
		CHECK(strip[0].day == 0 && strip[0].lastDay == 2 && strip[0].before && strip[0].after);
		CHECK_INT(s.Items()[strip[1].item].id, 4);
		CHECK(strip[1].day == 0 && strip[1].lastDay == 1 && !strip[1].before && !strip[1].after);
		CHECK_INT(s.Items()[strip[2].item].id, 2);
		CHECK(strip[2].day == 1 && strip[2].lastDay == 2 && !strip[2].after);
	}

	ClvItem item = Timed(9, At(0, 10), At(0, 10));
	CHECK_INT(ClvFirstDay(item), D0);
	CHECK_INT(ClvLastDay(item), D0);
	CHECK(!ClvInStrip(item));
	CHECK_INT(ClvLastDay(AllDay(9, 0, 2)), D0 + 1);
	CHECK(ClvInStrip(Timed(9, At(0, 10), At(1, 10))));
	CHECK(!ClvInStrip(Timed(9, At(0, 10), At(1, 9, 59))));
}

TEST(Overlaps) {
	auto p = ClvPlaceOverlaps({ { 0, 60 }, { 30, 90 }, { 60, 120 } });
	CHECK(p[0].column == 0 && p[0].columns == 2 && p[0].span == 1);
	CHECK(p[1].column == 1 && p[1].columns == 2 && p[1].span == 1);
	CHECK(p[2].column == 0 && p[2].columns == 2 && p[2].span == 1);

	// the longer first among those starting together
	p = ClvPlaceOverlaps({ { 0, 60 }, { 0, 120 }, { 60, 120 } });
	CHECK(p[1].column == 0 && p[0].column == 1 && p[2].column == 1);
	CHECK(p[0].columns == 2 && p[1].columns == 2 && p[2].columns == 2);

	// widened over a free column
	p = ClvPlaceOverlaps({ { 0, 180 }, { 0, 60 }, { 0, 60 }, { 60, 120 } });
	CHECK(p[0].column == 0 && p[0].span == 1 && p[0].columns == 3);
	CHECK(p[1].column == 1 && p[1].span == 1);
	CHECK(p[2].column == 2 && p[2].span == 1);
	CHECK(p[3].column == 1 && p[3].span == 2);

	// clusters apart
	p = ClvPlaceOverlaps({ { 0, 30 }, { 10, 40 }, { 100, 130 } });
	CHECK(p[0].columns == 2 && p[1].columns == 2);
	CHECK(p[2].columns == 1 && p[2].column == 0 && p[2].span == 1);
	p = ClvPlaceOverlaps({ { 0, 30 }, { 30, 60 } });   // touching isn't overlapping
	CHECK(p[0].columns == 1 && p[1].columns == 1);

	// widened on the left side's column too: the last column of a long cluster
	p = ClvPlaceOverlaps({ { 0, 60 }, { 0, 30 }, { 30, 60 } });
	CHECK(p[0].column == 0 && p[1].column == 1 && p[2].column == 1 && p[0].columns == 2);
	CHECK(ClvPlaceOverlaps({}).empty());
}

TEST(Strip) {
	std::vector<ClvPiece> pieces(4);
	pieces[0].day = 0; pieces[0].lastDay = 2;   // A
	pieces[1].day = 1; pieces[1].lastDay = 1;   // B
	pieces[2].day = 3; pieces[2].lastDay = 4;   // C
	pieces[3].day = 0; pieces[3].lastDay = 0;   // D
	std::vector<int> rows;
	CHECK_INT(ClvPackStrip(pieces, rows), 2);
	CHECK(rows[0] == 0 && rows[1] == 1 && rows[2] == 0 && rows[3] == 1);
	CHECK_INT(ClvPackStrip({}, rows), 0);
}

static ClvItemStore LayoutStore() {
	ClvItemStore s;
	s.Add(Timed(1, At(0, 9), At(0, 10)));
	s.Add(Timed(2, At(0, 9, 30), At(0, 11)));
	auto w = Timed(3, At(1, 13), At(1, 13, 5));     // too short for its window
	w.content = ClvContent::Window;
	s.Add(w);
	w = Timed(4, At(2, 10), At(2, 12));
	w.content = ClvContent::Window;
	s.Add(w);
	s.Add(AllDay(5, 0, 1));
	auto r = Timed(6, At(2, 14), At(2, 15));
	r.readOnly = true;
	s.Add(r);
	return s;
}

static ClvDayLayout Layout(const ClvItemStore& s) {
	return ClvLayoutDay(s, { D0, 3 }, { 56, 300, 3 }, { 30, 22 }, ClvMetrics{});
}

TEST(Geometry) {
	ClvColumns c{ 56, 300, 3 };
	CHECK(c.Left(0) == 56 && c.Left(1) == 156 && c.Left(2) == 256 && c.Right(2) == 356);
	CHECK(c.At(55) == -1 && c.At(56) == 0 && c.At(155) == 0 && c.At(156) == 1 && c.At(355) == 2 && c.At(356) == -1);
	ClvColumns odd{ 0, 100, 3 };
	CHECK(odd.Left(1) == 33 && odd.Left(2) == 66 && odd.At(32) == 0 && odd.At(33) == 1 && odd.At(99) == 2);

	ClvAxis a{ 30, 22 };
	CHECK_INT(a.Height(), 1056);
	CHECK_INT(a.Y(60), 44);
	CHECK_INT(a.Y(45), 33);
	CHECK_INT(a.Minute(44), 60);
	CHECK_INT(a.Minute(45), 61);
	CHECK_INT(a.Minute(-5), 0);
	CHECK_INT(a.Minute(5000), 1440);
	CHECK_INT(a.Minutes(18), 25);
	CHECK_INT(a.Minutes(22), 30);

	auto m = ClvMetrics{}.Scaled(144);
	CHECK_INT(m.slotHeight, 33);
	CHECK_INT(m.rulerWidth, 84);
	CHECK_INT(m.stripMaxRows, 3);
	CHECK_INT(m.itemPad, 2);
	CHECK_INT(ClvMetrics{}.Scaled(96).caption, 18);
}

TEST(DayLayout) {
	auto s = LayoutStore();
	auto l = Layout(s);
	ClvMetrics m;
	CHECK_INT(l.timed.size(), 5);
	CHECK_INT(l.strip.size(), 1);
	if (l.timed.size() != 5 || l.strip.size() != 1)
		return;
	CHECK(l.timed[0].id == 1 && l.timed[1].id == 2 && l.timed[2].id == 3 && l.timed[3].id == 4 && l.timed[4].id == 6);

	// day 0: 92 wide after the gap, two columns
	CHECK(Same(l.timed[0].rc, 56, 396, 101, 439));
	CHECK(Same(l.timed[0].body, 56, 396, 101, 439));
	CHECK(IsRectEmpty(&l.timed[0].caption));
	CHECK(Same(l.timed[1].rc, 102, 418, 147, 483));

	// a short window item: its least height, all caption, no window
	CHECK(Same(l.timed[2].rc, 156, 572, 247, 589));
	CHECK(Same(l.timed[2].caption, 156, 572, 247, 589));
	CHECK(IsRectEmpty(&l.timed[2].body));

	// a window item with room: caption and window
	CHECK(Same(l.timed[3].rc, 256, 440, 347, 527));
	CHECK(Same(l.timed[3].caption, 256, 440, 347, 458));
	CHECK(Same(l.timed[3].body, 256, 458, 347, 527));
	CHECK(!l.timed[4].resizable);

	CHECK(Same(l.strip[0].rc, 58, 1, 154, 21));
	CHECK_INT(l.stripRows, 1);
	CHECK_INT(l.StripHeight(m), 22);
	CHECK_INT(l.StripView(m), 28);
	CHECK(l.FindTimed(4, 2) == &l.timed[3]);
	CHECK(l.FindTimed(4, 1) == nullptr);
	CHECK(l.FindStrip(5) == &l.strip[0]);
	CHECK_INT(l.TimeAt(2, 600), At(2, 10));

	// rows past the maximum: the strip shows three and scrolls
	for (uint64_t id = 10; id < 15; id++)
		s.Add(AllDay(id, 0, 1));
	l = Layout(s);
	CHECK_INT(l.stripRows, 6);
	CHECK_INT(l.StripView(m), 6 + 3 * 22);
	CHECK_INT(l.StripHeight(m), 6 * 22);

	// a window item in the strip: its caption only
	ClvItemStore w;
	auto item = AllDay(1, 1, 1);
	item.content = ClvContent::Window;
	w.Add(item);
	l = Layout(w);
	CHECK(l.strip.size() == 1 && Same(l.strip[0].caption, 158, 1, 254, 21) && IsRectEmpty(&l.strip[0].body));

	// a piece going on past the range's ends: no inset there
	ClvItemStore past;
	past.Add(AllDay(1, -2, 3));
	past.Add(Timed(2, At(-1, 20), At(0, 1)));
	l = Layout(past);
	CHECK(l.strip.size() == 1 && Same(l.strip[0].rc, 56, 1, 154, 21) && l.strip[0].before);
	CHECK(l.timed.size() == 1 && l.timed[0].before && l.timed[0].rc.top == 0);
}

TEST(HitTest) {
	auto s = LayoutStore();
	auto l = Layout(s);
	ClvMetrics m;

	auto hit = ClvHitDays(l, { 60, 420 }, m);
	CHECK(hit.part == ClvPart::Item && hit.box == 0 && hit.day == 0 && hit.minute == 572);
	CHECK_INT(hit.Id(l, false), 1);
	CHECK(ClvHitDays(l, { 60, 397 }, m).part == ClvPart::Start);
	CHECK(ClvHitDays(l, { 60, 437 }, m).part == ClvPart::End);
	hit = ClvHitDays(l, { 60, 440 }, m);
	CHECK(hit.part == ClvPart::Slot && hit.box == -1 && hit.day == 0);
	CHECK_INT(hit.Id(l, false), 0);
	CHECK_INT(ClvHitDays(l, { 120, 430 }, m).Id(l, false), 2);
	CHECK(ClvHitDays(l, { 150, 430 }, m).part == ClvPart::Slot);   // the gap
	hit = ClvHitDays(l, { 40, 100 }, m);
	CHECK(hit.part == ClvPart::Ruler && hit.day == -1);
	CHECK(ClvHitDays(l, { 60, -1 }, m).part == ClvPart::None);
	CHECK(ClvHitDays(l, { 60, 1056 }, m).part == ClvPart::None);
	CHECK(ClvHitDays(l, { 400, 100 }, m).part == ClvPart::None);

	CHECK(ClvHitDays(l, { 300, 441 }, m).part == ClvPart::Start);
	CHECK(ClvHitDays(l, { 300, 445 }, m).part == ClvPart::Caption);
	CHECK(ClvHitDays(l, { 300, 500 }, m).part == ClvPart::Body);
	CHECK(ClvHitDays(l, { 200, 580 }, m).part == ClvPart::Caption);   // all caption
	CHECK(ClvHitDays(l, { 300, 617 }, m).part == ClvPart::Item);      // read only: no edges
	CHECK(ClvHitDays(l, { 300, 657 }, m).part == ClvPart::Item);

	hit = ClvHitStrip(l, { 60, 10 }, m);
	CHECK(hit.part == ClvPart::Start && hit.Id(l, true) == 5);
	CHECK(ClvHitStrip(l, { 100, 10 }, m).part == ClvPart::Item);
	CHECK(ClvHitStrip(l, { 152, 10 }, m).part == ClvPart::End);
	hit = ClvHitStrip(l, { 200, 10 }, m);
	CHECK(hit.part == ClvPart::Slot && hit.day == 1);

	// the top item first, else the last drawn
	ClvDayLayout two;
	two.columns = { 0, 100, 1 };
	ClvBox a, b;
	a.id = 1;
	b.id = 2;
	a.rc = b.rc = { 10, 10, 90, 90 };
	a.body = b.body = a.rc;
	two.timed = { a, b };
	CHECK_INT(ClvHitDays(two, { 50, 50 }, m).Id(two, false), 2);
	CHECK_INT(ClvHitDays(two, { 50, 50 }, m, 1).Id(two, false), 1);
	// a tiny item: the edges take a third each
	two.timed[1].rc = { 10, 10, 90, 16 };
	CHECK(ClvHitDays(two, { 50, 11 }, m, 2).part == ClvPart::Start);
	CHECK(ClvHitDays(two, { 50, 13 }, m, 2).part == ClvPart::Item);
	CHECK(ClvHitDays(two, { 50, 15 }, m, 2).part == ClvPart::End);
}

TEST(Dragging) {
	CHECK_INT(ClvSnap(At(0, 9, 14), 15), At(0, 9, 15));
	CHECK_INT(ClvSnap(At(0, 9, 7), 15), At(0, 9));
	CHECK_INT(ClvSnap(At(0, 9, 8), 15), At(0, 9, 15));
	CHECK_INT(ClvSnap(At(0, 9, 8), 1), At(0, 9, 8));
	CHECK_INT(ClvSnap(-8, 15), -15);

	ClvSpan nine{ At(0, 9), At(0, 10), false };
	auto r = ClvDragMove(nine, At(0, 9, 20), At(1, 11, 35), 30, 60);
	CHECK(r.start == At(1, 11, 30) && r.end == At(1, 12, 30) && !r.allDay);
	r = ClvDragMove(nine, At(0, 9, 20), At(1, 11, 35), 0, 60);   // not snapped
	CHECK(r.start == At(1, 11, 15) && r.end == At(1, 12, 15));
	r = ClvDragMove({ At(0, 0), At(1, 0), true }, At(0, 0), At(2, 14, 40), 30, 60);
	CHECK(r.start == At(2, 14, 30) && r.end == At(2, 15, 30) && !r.allDay);

	r = ClvDragMoveStrip({ At(0, 0), At(2, 0), true }, D0, D0 + 3);
	CHECK(r.start == At(3, 0) && r.end == At(5, 0) && r.allDay);
	r = ClvDragMoveStrip(nine, D0, D0 + 1);
	CHECK(r.start == At(1, 0) && r.end == At(2, 0) && r.allDay);
	r = ClvDragMoveStrip({ At(0, 22), At(1, 2), false }, D0, D0);
	CHECK(r.start == At(0, 0) && r.end == At(2, 0) && r.allDay);
	r = ClvDragMoveStrip({ At(0, 10), At(1, 12), false }, D0, D0 + 1);   // long and timed: by days
	CHECK(r.start == At(1, 10) && r.end == At(2, 12) && !r.allDay);

	r = ClvDragStart(nine, At(0, 9, 40), 15, 15);
	CHECK(r.start == At(0, 9, 45) && r.end == At(0, 10));
	r = ClvDragStart(nine, At(0, 9, 55), 15, 15);
	CHECK_INT(r.start, At(0, 9, 45));
	r = ClvDragStart(nine, At(-1, 23), 15, 15);
	CHECK_INT(r.start, At(-1, 23));
	r = ClvDragEnd(nine, At(0, 8), 15, 15);
	CHECK(r.start == At(0, 9) && r.end == At(0, 9, 15));
	r = ClvDragEnd(nine, At(1, 1), 15, 15);
	CHECK_INT(r.end, At(1, 1));

	ClvSpan days{ At(0, 0), At(3, 0), true };
	r = ClvDragStripStart(days, D0 + 1);
	CHECK(r.start == At(1, 0) && r.end == At(3, 0));
	r = ClvDragStripStart(days, D0 + 5);
	CHECK(r.start == At(2, 0) && r.end == At(3, 0));
	r = ClvDragStripEnd(days, D0 - 1);
	CHECK(r.start == At(0, 0) && r.end == At(1, 0));
	r = ClvDragStripEnd(days, D0 + 4);
	CHECK_INT(r.end, At(5, 0));
	ClvSpan longTimed{ At(0, 10), At(1, 12), false };
	r = ClvDragStripEnd(longTimed, D0 + 3);
	CHECK(r.start == At(0, 10) && r.end == At(3, 12));
	r = ClvDragStripStart(longTimed, D0 - 2);
	CHECK(r.start == At(-2, 10) && r.end == At(1, 12));

	r = ClvSlotRange(At(0, 10, 10), At(0, 9, 20), 30);
	CHECK(r.start == At(0, 9) && r.end == At(0, 10, 30) && !r.allDay);
	r = ClvSlotRange(At(0, 9, 20), At(0, 10, 10), 30);
	CHECK(r.start == At(0, 9) && r.end == At(0, 10, 30));
	r = ClvSlotRange(At(0, 10, 10), At(0, 10, 20), 30);
	CHECK(r.start == At(0, 10) && r.end == At(0, 10, 30));
	r = ClvDayRange(D0 + 2, D0);
	CHECK(r.start == At(0, 0) && r.end == At(3, 0) && r.allDay);

	CHECK_INT(ClvMoveSlot(At(0, 10, 10), 0, 1, 30), At(0, 10, 30));
	CHECK_INT(ClvMoveSlot(At(0, 23, 40), 0, 1, 30), At(0, 23, 30));
	CHECK_INT(ClvMoveSlot(At(0, 0, 10), 0, -1, 30), At(0, 0));
	CHECK_INT(ClvMoveSlot(At(0, 10), -1, 0, 30), At(-1, 10));
	CHECK_INT(ClvMoveSlot(At(0, 10), 2, -4, 60), At(2, 6));
}

TEST(Scrolling) {
	CHECK_INT(ClvClampScroll(-5, 1000, 300), 0);
	CHECK_INT(ClvClampScroll(900, 1000, 300), 700);
	CHECK_INT(ClvClampScroll(100, 200, 300), 0);
	CHECK_INT(ClvScrollToShow(100, 50, 80, 300, 1000), 50);
	CHECK_INT(ClvScrollToShow(100, 350, 420, 300, 1000), 120);
	CHECK_INT(ClvScrollToShow(100, 150, 200, 300, 1000), 100);
	CHECK_INT(ClvScrollToShow(100, 500, 900, 300, 1000), 500);   // taller than the view: its top
	CHECK_INT(ClvScrollToShow(0, 950, 1000, 300, 1000), 700);

	CHECK_INT(ClvZoom(22, 1, 10, 80), 28);
	CHECK_INT(ClvZoom(22, -1, 10, 80), 18);
	CHECK_INT(ClvZoom(22, 10, 10, 80), 80);
	CHECK_INT(ClvZoom(10, -1, 10, 80), 10);
	CHECK_INT(ClvZoom(3, 1, 1, 80), 4);   // always a step
	CHECK_INT(ClvZoom(22, 0, 10, 80), 22);
}

TEST(TabOrder) {
	auto s = LayoutStore();
	s.Add(Timed(7, At(1, 22), At(2, 1)));   // two pieces, once in the order
	auto l = Layout(s);
	auto order = ClvTabOrder(l, s);
	CHECK((order == std::vector<uint64_t>{ 5, 1, 2, 3, 7, 4, 6 }));

	CHECK_INT(ClvNextItem(order, 0, true), 5);
	CHECK_INT(ClvNextItem(order, 0, false), 6);
	CHECK_INT(ClvNextItem(order, 2, true), 3);
	CHECK_INT(ClvNextItem(order, 2, false), 1);
	CHECK_INT(ClvNextItem(order, 6, true), 0);
	CHECK_INT(ClvNextItem(order, 5, false), 0);
	CHECK_INT(ClvNextItem(order, 99, true), 5);
	CHECK_INT(ClvNextItem({}, 1, true), 0);
}

TEST(Undo) {
	ClvItemStore s;
	ClvHistory h;
	CHECK(!h.CanUndo() && !h.CanRedo());
	auto before = s.Save();
	s.Add(Timed(1, At(0, 9), At(0, 10)));
	h.Push(before, s.Save());
	before = s.Save();
	auto item = *s.Find(1);
	item.start = At(0, 11);
	item.end = At(0, 12);
	s.Set(item);
	h.Push(before, s.Save());

	CHECK(h.Undo(s));
	CHECK(s.Find(1) && s.Find(1)->start == At(0, 9));
	CHECK(h.Undo(s));
	CHECK(!s.Find(1));
	CHECK(!h.Undo(s));
	CHECK(h.Redo(s));
	CHECK(h.Redo(s));
	CHECK(s.Find(1)->start == At(0, 11));
	CHECK(!h.Redo(s));

	// a new change drops what was undone
	h.Undo(s);
	before = s.Save();
	s.Remove(1);
	h.Push(before, s.Save());
	CHECK(!h.CanRedo());
	CHECK(h.Undo(s));
	CHECK(s.Find(1) && s.Find(1)->start == At(0, 9));

	ClvHistory limited(2);
	for (int i = 0; i < 5; i++)
		limited.Push(s.Save(), s.Save());
	CHECK(limited.Undo(s));
	CHECK(limited.Undo(s));
	CHECK(!limited.Undo(s));
	limited.Clear();
	CHECK(!limited.CanUndo() && !limited.CanRedo());
}

int wmain() {
	wprintf(L"Calendar view model tests\n\n");

	Run_Dates();
	Run_MonthGrid();
	Run_Ranges();
	Run_Store();
	Run_Split();
	Run_Overlaps();
	Run_Strip();
	Run_Geometry();
	Run_DayLayout();
	Run_HitTest();
	Run_Dragging();
	Run_Scrolling();
	Run_TabOrder();
	Run_Undo();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
