// Unit tests for the Gantt model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: dates, the working calendar, the time scale, the task tree, links
// and pushing successors, bars and hit testing, links' routes, dragging and undo. Anything that needs an HWND is
// checked by hand with GanttDemo.

#include "GanttModel.h"

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

// Monday 2026-10-26 at h:mm, plus days
static GntTime At(int days, int h, int m = 0) {
	return GntTimeOf(GntDayFromDate(2026, 10, 26) + days, h * 60 + m);
}

static const int Day = 540;   // a working day's minutes, 8:00 to 17:00

static bool Same(const RECT& rc, int left, int top, int right, int bottom) {
	return rc.left == left && rc.top == top && rc.right == right && rc.bottom == bottom;
}

static bool SamePoints(const std::vector<POINT>& got, std::vector<POINT> want) {
	if (got.size() != want.size())
		return false;
	for (size_t i = 0; i < got.size(); i++)
		if (got[i].x != want[i].x || got[i].y != want[i].y)
			return false;
	return true;
}

static GntTask Task(UINT id, GntTime start, int64_t duration, UINT parent = 0) {
	GntTask t;
	t.id = id;
	t.parent = parent;
	t.start = start;
	t.duration = duration;
	return t;
}

// ---- Tests ----------------------------------------------------------------------

TEST(Dates) {
	CHECK_INT(GntWeekday(0), 1);   // 1601-01-01, a Monday
	CHECK_INT(GntDayFromDate(1601, 1, 1), 0);
	CHECK_INT(GntDayFromDate(1970, 1, 1), 134774);
	CHECK_INT(GntWeekday(134774), 4);   // a Thursday
	const int64_t today = GntDayFromDate(2026, 10, 25);
	CHECK_INT(GntWeekday(today), 0);   // a Sunday
	auto d = GntDateOf(today);
	CHECK(d.year == 2026 && d.month == 10 && d.day == 25);
	// a leap day
	const int64_t leap = GntDayFromDate(2024, 2, 29);
	d = GntDateOf(leap);
	CHECK(d.year == 2024 && d.month == 2 && d.day == 29);
	d = GntDateOf(leap + 1);
	CHECK(d.month == 3 && d.day == 1);
	CHECK_INT(GntDayFromDate(2025, 3, 1) - GntDayFromDate(2025, 2, 28), 1);
	// times and SYSTEMTIME
	CHECK_INT(GntDayOf(At(0, 9, 30)), GntDayFromDate(2026, 10, 26));
	CHECK_INT(GntDayOf(-1), -1);
	auto st = GntToSystemTime(At(0, 9, 30));
	CHECK(st.wYear == 2026 && st.wMonth == 10 && st.wDay == 26 && st.wHour == 9 && st.wMinute == 30 && st.wDayOfWeek == 1);
	CHECK_INT(GntFromSystemTime(st), At(0, 9, 30));
}

TEST(Calendar) {
	GntCalendar c;
	CHECK(c.IsWorkingDay(GntDayOf(At(0, 0))));
	CHECK(!c.IsWorkingDay(GntDayOf(At(5, 0))));   // Saturday
	CHECK_INT(c.MinutesPerDay(), Day);

	CHECK_INT(c.NextWorkStart(At(0, 7)), At(0, 8));
	CHECK_INT(c.NextWorkStart(At(0, 9)), At(0, 9));
	CHECK_INT(c.NextWorkStart(At(0, 17)), At(1, 8));
	CHECK_INT(c.NextWorkStart(At(5, 10)), At(7, 8));    // Saturday: Monday
	CHECK_INT(c.PrevWorkEnd(At(0, 8)), At(-3, 17));     // Monday morning: Friday evening
	CHECK_INT(c.PrevWorkEnd(At(1, 12)), At(1, 12));
	CHECK_INT(c.PrevWorkEnd(At(5, 10)), At(4, 17));

	CHECK_INT(c.AddWork(At(0, 8), Day), At(0, 17));
	CHECK_INT(c.AddWork(At(0, 8), Day + 1), At(1, 8, 1));
	CHECK_INT(c.AddWork(At(4, 16), 120), At(7, 9));     // over the weekend
	CHECK_INT(c.AddWork(At(0, 8), 5 * Day), At(4, 17));
	CHECK_INT(c.AddWork(At(0, 8), 0), At(0, 8));
	CHECK_INT(c.AddWork(At(0, 9), -120), At(-3, 16));   // backwards over the weekend
	CHECK_INT(c.AddWork(At(1, 17), -Day), At(1, 8));

	CHECK_INT(c.WorkBetween(At(0, 8), At(7, 8)), 5 * Day);
	CHECK_INT(c.WorkBetween(At(4, 16), At(7, 9)), 120);
	CHECK_INT(c.WorkBetween(At(7, 9), At(4, 16)), -120);
	CHECK_INT(c.WorkBetween(At(5, 0), At(7, 0)), 0);

	// a holiday on Tuesday
	CHECK(c.AddHoliday(GntDayOf(At(1, 0))));
	CHECK(!c.AddHoliday(GntDayOf(At(1, 0))));
	CHECK(!c.IsWorkingDay(GntDayOf(At(1, 0))));
	CHECK_INT(c.AddWork(At(0, 8), 2 * Day), At(2, 17));
	CHECK_INT(c.WorkBetween(At(0, 8), At(7, 8)), 4 * Day);
	CHECK(c.RemoveHoliday(GntDayOf(At(1, 0))));
	CHECK(!c.RemoveHoliday(GntDayOf(At(1, 0))));

	// no working days: all time is work
	GntCalendar none;
	none.workDays = 0;
	CHECK_INT(none.AddWork(At(5, 10), 100), At(5, 11, 40));
	CHECK_INT(none.WorkBetween(At(0, 0), At(1, 0)), 1440);
}

TEST(Scale) {
	GntUnit lower, upper;
	GntPickUnits(10, 40, lower, upper);
	CHECK(lower == GntUnit::Day && upper == GntUnit::Week);
	GntPickUnits(1, 40, lower, upper);
	CHECK(lower == GntUnit::Hour && upper == GntUnit::Day);
	GntPickUnits(300, 40, lower, upper);
	CHECK(lower == GntUnit::Month && upper == GntUnit::Quarter);
	GntPickUnits(1e7, 40, lower, upper);
	CHECK(lower == GntUnit::Year && upper == GntUnit::Year);

	CHECK_INT(GntUnitStart(GntUnit::Hour, At(0, 9, 35)), At(0, 9));
	CHECK_INT(GntUnitStart(GntUnit::Day, At(0, 9, 35)), At(0, 0));
	CHECK_INT(GntUnitStart(GntUnit::Week, At(-1, 12)), At(-7, 0));   // Sunday: the Monday before
	CHECK_INT(GntUnitStart(GntUnit::Week, At(0, 12)), At(0, 0));
	CHECK_INT(GntUnitStart(GntUnit::Month, At(0, 12)), GntTimeOf(GntDayFromDate(2026, 10, 1)));
	CHECK_INT(GntUnitStart(GntUnit::Quarter, At(0, 12)), GntTimeOf(GntDayFromDate(2026, 10, 1)));
	CHECK_INT(GntUnitStart(GntUnit::Year, At(0, 12)), GntTimeOf(GntDayFromDate(2026, 1, 1)));
	CHECK_INT(GntUnitNext(GntUnit::Month, GntTimeOf(GntDayFromDate(2026, 12, 1))), GntTimeOf(GntDayFromDate(2027, 1, 1)));
	CHECK_INT(GntUnitNext(GntUnit::Quarter, GntTimeOf(GntDayFromDate(2026, 10, 1))), GntTimeOf(GntDayFromDate(2027, 1, 1)));

	auto cells = GntCells(GntUnit::Month, GntTimeOf(GntDayFromDate(2026, 1, 15)), GntTimeOf(GntDayFromDate(2026, 4, 1)));
	CHECK_INT(cells.size(), 3);
	CHECK_INT(cells[0].start, GntTimeOf(GntDayFromDate(2026, 1, 1)));
	CHECK_INT(cells[1].end - cells[1].start, 28 * GntDay);   // February

	GntScale s{ At(0, 0), 60 };
	CHECK(s.X(At(1, 0)) == 24.0);
	CHECK_INT(s.T(24), At(1, 0));
}

TEST(Tree) {
	GntProject p;
	CHECK(p.Add(Task(1, 0, 0)));
	CHECK(p.Add(Task(2, At(0, 8), 2 * Day, 1)));
	CHECK(p.Add(Task(3, At(2, 8), Day, 1)));
	CHECK(p.Add(Task(4, At(3, 17), 0)));   // a milestone
	CHECK(!p.Add(Task(4, 0, 0)));          // the id's taken
	CHECK(!p.Add(Task(9, 0, 0, 77)));      // no such parent
	CHECK(!p.Add(Task(9, 0, 0), 2));       // after a task of another parent

	auto& t = p.Tasks();
	CHECK_INT(t[0].depth, 0);
	CHECK_INT(t[1].depth, 1);
	CHECK(t[0].summary);
	CHECK(!t[1].summary);
	CHECK_INT(t[0].start, At(0, 8));
	CHECK_INT(t[0].finish, At(2, 17));
	CHECK_INT(t[0].duration, 3 * Day);
	CHECK(t[3].Milestone());
	CHECK_INT(t[3].finish, At(3, 17));

	// a summary's progress by its children's work
	p.Find(2)->progress = 50;
	p.Recompute();
	CHECK_INT(p.Find(1)->progress, 33);

	// after a sibling, with its children
	CHECK(p.Add(Task(5, At(0, 8), Day, 1), 2));
	CHECK_INT(p.Index(5), 2);
	CHECK(p.IsAncestor(1, 5));
	CHECK(!p.IsAncestor(5, 1));

	CHECK(p.Rows() == std::vector<int>({ 0, 1, 2, 3, 4 }));
	p.Find(1)->expanded = false;
	CHECK(p.Rows() == std::vector<int>({ 0, 4 }));

	CHECK(p.AddLink({ 2, 3 }));
	CHECK(p.Remove(1));
	CHECK_INT(p.Tasks().size(), 1);
	CHECK(p.Links().empty());
	CHECK(!p.Remove(1));
}

TEST(Links) {
	GntProject p;
	p.Add(Task(10, At(0, 8), Day));
	p.Add(Task(11, At(0, 8), Day));
	p.Add(Task(12, At(0, 8), Day));
	p.Add(Task(20, 0, 0));
	p.Add(Task(21, At(0, 8), Day, 20));
	CHECK(p.AddLink({ 10, 11 }));
	CHECK(!p.AddLink({ 11, 10 }));   // the other way
	CHECK(!p.AddLink({ 10, 11 }));
	CHECK(p.AddLink({ 11, 12 }));
	CHECK(!p.CanLink(12, 10));       // a cycle
	CHECK(!p.CanLink(10, 20));       // a summary
	CHECK(!p.CanLink(10, 10));
	CHECK(p.CanLink(10, 21));

	CHECK_INT(p.EarliestStart(11), At(1, 8));
	CHECK_INT(p.EarliestStart(10), INT64_MIN);
	auto moved = p.PushSuccessors(10);
	CHECK(moved == std::vector<UINT>({ 11, 12 }));
	CHECK_INT(p.Find(11)->start, At(1, 8));
	CHECK_INT(p.Find(12)->start, At(2, 8));
	CHECK(p.PushSuccessors(10).empty());   // nothing more to move

	CHECK(p.RemoveLink(11, 12));
	CHECK(!p.RemoveLink(11, 12));

	// lag and lead, and the other kinds
	GntProject q;
	q.Add(Task(1, At(0, 8), Day));
	q.Add(Task(2, At(0, 8), 2 * Day));
	q.AddLink({ 1, 2, GntLinkType::FinishStart, Day });
	CHECK_INT(q.EarliestStart(2), At(2, 8));
	q.RemoveLink(1, 2);
	q.AddLink({ 1, 2, GntLinkType::FinishStart, -270 });
	CHECK_INT(q.EarliestStart(2), At(0, 12, 30));
	q.RemoveLink(1, 2);
	q.AddLink({ 1, 2, GntLinkType::StartStart });
	CHECK_INT(q.EarliestStart(2), At(0, 8));
	q.RemoveLink(1, 2);
	q.AddLink({ 1, 2, GntLinkType::FinishFinish });
	CHECK_INT(q.EarliestStart(2), At(-3, 8));   // to finish with it: the Friday before
	CHECK(q.PushSuccessors(1).empty());        // never earlier

	// the whole project: a task that doesn't move (it may start with a milestone) still holds back the ones after it
	GntProject r;
	r.Add(Task(1, At(0, 8), 0));         // a milestone
	r.Add(Task(2, At(0, 8), 2 * Day));
	r.Add(Task(3, At(0, 8), Day));
	r.Add(Task(4, At(0, 8), Day));
	r.AddLink({ 1, 2 });
	r.AddLink({ 2, 3 });
	r.AddLink({ 2, 4 });
	r.AddLink({ 3, 4 });
	CHECK(r.PushSuccessors(1).empty());   // 2 needn't move: the push stops there
	auto scheduled = r.Schedule();
	CHECK(scheduled == std::vector<UINT>({ 3, 4 }));
	CHECK_INT(r.Find(2)->start, At(0, 8));
	CHECK_INT(r.Find(3)->start, At(2, 8));
	CHECK_INT(r.Find(4)->start, At(3, 8));   // after 3, the later of its two
	CHECK(r.Schedule().empty());
}

TEST(Bars) {
	GntMetrics m;
	GntScale s{ At(0, 0), 60 };   // 24 pixels a day
	GntTask t = Task(1, At(0, 8), 2 * Day);
	t.finish = At(1, 17);
	t.progress = 50;
	auto bar = GntLayoutBar(t, 2, s, m);
	CHECK(Same(bar.rc, 8, 53, 41, 67));
	CHECK_INT(bar.done.right, 25);
	CHECK(!bar.milestone && !bar.summary);

	GntTask ms = Task(2, At(0, 12), 0);
	ms.finish = ms.start;
	auto mb = GntLayoutBar(ms, 0, s, m);
	CHECK(mb.milestone);
	CHECK(Same(mb.rc, 5, 5, 19, 19));

	GntTask sum = t;
	sum.summary = true;
	auto sb = GntLayoutBar(sum, 1, s, m);
	CHECK(Same(sb.rc, 8, 32, 41, 40));

	CHECK(Same(GntLinkHandle(bar, m), 44, 56, 52, 64));
	CHECK(GntHitBar(bar, { 48, 60 }, m) == GntPart::Link);
	CHECK(GntHitBar(bar, { 41, 55 }, m) == GntPart::Finish);
	CHECK(GntHitBar(bar, { 8, 55 }, m) == GntPart::Start);
	CHECK(GntHitBar(bar, { 25, 65 }, m) == GntPart::Progress);
	CHECK(GntHitBar(bar, { 25, 55 }, m) == GntPart::Bar);
	CHECK(GntHitBar(bar, { 20, 70 }, m) == GntPart::None);
	CHECK(GntHitBar(mb, { 12, 12 }, m) == GntPart::Bar);
	CHECK(GntHitBar(sb, { 9, 35 }, m) == GntPart::Bar);
	CHECK(GntHitBar(sb, { 48, 36 }, m) == GntPart::None);   // no link handle on a summary
}

TEST(Routes) {
	GntMetrics m;
	GntBar a;
	a.rc = { 8, 53, 41, 67 };    // row 2
	GntBar b;
	b.rc = { 60, 77, 90, 91 };   // row 3, after it
	GntBar c;
	c.rc = { 20, 77, 50, 91 };   // row 3, overlapping it
	CHECK(SamePoints(GntRouteLink(a, b, GntLinkType::FinishStart, m), { { 41, 60 }, { 49, 60 }, { 49, 84 }, { 60, 84 } }));
	CHECK(SamePoints(GntRouteLink(a, c, GntLinkType::FinishStart, m),
		{ { 41, 60 }, { 49, 60 }, { 49, 72 }, { 12, 72 }, { 12, 84 }, { 20, 84 } }));
	CHECK(SamePoints(GntRouteLink(a, c, GntLinkType::StartStart, m), { { 8, 60 }, { 0, 60 }, { 0, 84 }, { 20, 84 } }));
	CHECK(SamePoints(GntRouteLink(a, c, GntLinkType::FinishFinish, m), { { 41, 60 }, { 58, 60 }, { 58, 84 }, { 50, 84 } }));
	// upwards: the turn is under the target's row
	CHECK(SamePoints(GntRouteLink(c, a, GntLinkType::FinishStart, m),
		{ { 50, 84 }, { 58, 84 }, { 58, 72 }, { 0, 72 }, { 0, 60 }, { 8, 60 } }));
}

TEST(Dragging) {
	GntCalendar cal;
	GntScale s{ At(0, 0), 60 };
	CHECK_INT(GntDragStart(At(0, 8), 30, s, GntUnit::Day, cal), At(2, 8));    // Tuesday 14:00: past noon
	CHECK_INT(GntDragStart(At(0, 8), 20, s, GntUnit::Day, cal), At(1, 8));    // Tuesday 4:00
	CHECK_INT(GntDragStart(At(0, 8), -10, s, GntUnit::Day, cal), At(0, 8));
	CHECK_INT(GntDragStart(At(0, 8), 120, s, GntUnit::Day, cal), At(7, 8));   // Saturday: Monday
	CHECK_INT(GntDragStart(At(0, 8), 1, s, GntUnit::Hour, cal), At(0, 9));

	CHECK_INT(GntDragDuration(At(0, 8), At(0, 17), 24, s, GntUnit::Day, cal), 2 * Day);
	CHECK_INT(GntDragDuration(At(0, 8), At(0, 17), -24, s, GntUnit::Day, cal), Day);   // at least a day
	CHECK_INT(GntDragDuration(At(0, 8), At(0, 17), -5, s, GntUnit::Hour, cal), 240);

	GntBar bar;
	bar.rc = { 8, 0, 41, 10 };
	CHECK_INT(GntDragProgress(bar, 8), 0);
	CHECK_INT(GntDragProgress(bar, 41), 100);
	CHECK_INT(GntDragProgress(bar, 25), 52);
	CHECK_INT(GntDragProgress(bar, 100), 100);
	CHECK_INT(GntDragProgress(bar, -5), 0);
}

TEST(Undo) {
	GntProject p;
	p.Add(Task(1, At(0, 8), Day));
	GntHistory h;
	CHECK(!h.CanUndo());
	auto before = p.Save();
	p.Find(1)->start = At(1, 8);
	p.Recompute();
	h.Push(before, p.Save());
	CHECK(h.CanUndo() && !h.CanRedo());
	CHECK(h.Undo(p));
	CHECK_INT(p.Find(1)->start, At(0, 8));
	CHECK_INT(p.Find(1)->finish, At(0, 17));
	CHECK(h.CanRedo());
	CHECK(h.Redo(p));
	CHECK_INT(p.Find(1)->start, At(1, 8));
	CHECK(!h.Redo(p));

	// a new change drops what was undone
	h.Undo(p);
	before = p.Save();
	p.AddLink({ 1, 1 });   // refused: nothing changes, but it's pushed anyway
	h.Push(before, p.Save());
	CHECK(!h.CanRedo());

	GntHistory limited(2);
	for (int i = 0; i < 5; i++)
		limited.Push(p.Save(), p.Save());
	CHECK(limited.Undo(p));
	CHECK(limited.Undo(p));
	CHECK(!limited.Undo(p));
}

TEST(Scaled) {
	auto m = GntMetrics{}.Scaled(144);
	CHECK_INT(m.rowHeight, 36);
	CHECK_INT(m.milestone, 21);
	CHECK_INT(GntMetrics{}.Scaled(96).linkGap, 8);
}

int wmain() {
	wprintf(L"Gantt model tests\n\n");

	Run_Dates();
	Run_Calendar();
	Run_Scale();
	Run_Tree();
	Run_Links();
	Run_Bars();
	Run_Routes();
	Run_Dragging();
	Run_Undo();
	Run_Scaled();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
