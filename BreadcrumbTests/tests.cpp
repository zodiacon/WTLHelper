// Unit tests for the Breadcrumb model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: measuring, laying out the bar and folding segments, hit testing,
// the back/forward history, the recent locations, path text and suggestions. Anything that needs an HWND is checked
// by hand with BreadcrumbDemo.

#include "BreadcrumbModel.h"

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

// every character 6 pixels wide
static const BrcMeasure Measure = [](std::wstring_view s) {
	return (int)s.size() * 6;
};

static const BrcMetrics M;     // 96 DPI

static bool Same(const RECT& rc, int left, int top, int right, int bottom) {
	return rc.left == left && rc.top == top && rc.right == right && rc.bottom == bottom;
}

static bool Empty(const RECT& rc) {
	return ::IsRectEmpty(&rc) != FALSE;
}

// This PC > C: > Users > pavel: text parts 54, 24, 42, 42, each with a 16 pixel chevron
static std::vector<BrcSegment> Path() {
	std::vector<BrcSegment> s(4);
	s[0].text = L"This PC";
	s[1].text = s[1].name = L"C:";
	s[2].text = s[2].name = L"Users";
	s[3].text = s[3].name = L"pavel";
	BrcMeasureSegments(s, Measure);
	return s;
}

static BrcLocation Loc(const wchar_t* path) {
	return BrcLocation{ path, path, -1 };
}

// ---- Tests ----------------------------------------------------------------------

TEST(MeasureSegments) {
	auto s = Path();
	CHECK_INT(s[0].textWidth, 42);
	CHECK_INT(BrcTextPart(s[0], M), 54);
	CHECK_INT(BrcChevronPart(s[0], M), 16);
	s[0].noChildren = true;
	CHECK_INT(BrcChevronPart(s[0], M), 0);
}

TEST(LayoutParts) {
	const auto s = Path();
	BrcParts parts;
	auto l = BrcLayoutBar(s, parts, 600, M);
	CHECK(Same(l.back, 0, 0, 26, 26));
	CHECK(Same(l.forward, 26, 0, 52, 26));
	CHECK(Same(l.recent, 52, 0, 66, 26));
	CHECK(Same(l.up, 66, 0, 92, 26));
	CHECK(Same(l.box, 96, 0, 600, 26));
	CHECK(Same(l.icon, 101, 5, 117, 21));
	CHECK(Same(l.overflow, 121, 1, 137, 25));
	CHECK(Same(l.history, 575, 1, 599, 25));
	CHECK(Same(l.edit, 121, 1, 575, 25));
	CHECK(l.actions.empty());

	// no nav buttons, no icon: the box starts at 0, the chevron at its frame
	parts.nav = parts.up = parts.icon = false;
	l = BrcLayoutBar(s, parts, 600, M);
	CHECK(Empty(l.back) && Empty(l.forward) && Empty(l.recent) && Empty(l.up) && Empty(l.icon));
	CHECK(Same(l.box, 0, 0, 600, 26));
	CHECK(Same(l.overflow, 1, 1, 17, 25));

	// action buttons at the right, the history arrow before them
	parts = {};
	parts.actions = 2;
	l = BrcLayoutBar(s, parts, 600, M);
	CHECK_INT(l.actions.size(), 2);
	CHECK(Same(l.actions[1], 575, 1, 599, 25));
	CHECK(Same(l.actions[0], 551, 1, 575, 25));
	CHECK(Same(l.history, 527, 1, 551, 25));
	CHECK_INT(l.edit.right, 527);

	// too narrow for even the box: nothing negative
	l = BrcLayoutBar(s, BrcParts{}, 50, M);
	CHECK(l.box.right >= l.box.left);
	CHECK(l.edit.right >= l.edit.left);
	CHECK_INT(l.firstVisible, 3);
}

TEST(LayoutSegments) {
	const auto s = Path();
	auto l = BrcLayoutBar(s, BrcParts{}, 600, M);
	CHECK_INT(l.firstVisible, 0);
	CHECK(!l.Overflowed());
	CHECK(!l.truncated);
	CHECK(Same(l.text[0], 137, 1, 191, 25));
	CHECK(Same(l.chevron[0], 191, 1, 207, 25));
	CHECK(Same(l.text[1], 207, 1, 231, 25));
	CHECK(Same(l.chevron[1], 231, 1, 247, 25));
	CHECK(Same(l.text[2], 247, 1, 289, 25));
	CHECK(Same(l.text[3], 305, 1, 347, 25));
	CHECK(Same(l.chevron[3], 347, 1, 363, 25));
	CHECK(Same(l.empty, 363, 1, 575, 25));

	// a segment without children has no chevron
	auto t = s;
	t[3].noChildren = true;
	l = BrcLayoutBar(t, BrcParts{}, 600, M);
	CHECK(Empty(l.chevron[3]));
	CHECK_INT(l.empty.left, 347);

	// no segments: all room to edit
	l = BrcLayoutBar({}, BrcParts{}, 600, M);
	CHECK_INT(l.firstVisible, 0);
	CHECK(Same(l.empty, 137, 1, 575, 25));
}

TEST(Fold) {
	const auto s = Path();
	// 114 pixels for the segments (and the room to edit): pavel (58) fits, Users after it doesn't
	auto l = BrcLayoutBar(s, BrcParts{}, 300, M);
	CHECK_INT(l.firstVisible, 3);
	CHECK(l.Overflowed());
	CHECK(Empty(l.text[0]) && Empty(l.text[2]) && Empty(l.chevron[2]));
	CHECK(Same(l.text[3], 137, 1, 179, 25));
	CHECK(!l.truncated);
	auto folded = BrcFoldedSegments(l);
	CHECK_INT(folded.size(), 3);
	CHECK(folded == std::vector<int>({ 2, 1, 0 }));

	// one pixel more for Users
	l = BrcLayoutBar(s, BrcParts{}, 302, M);
	CHECK_INT(l.firstVisible, 2);

	// even the last doesn't fit: its text cut to what's left, at most minSegment if that's more
	l = BrcLayoutBar(s, BrcParts{}, 200, M);
	CHECK_INT(l.firstVisible, 3);
	CHECK(l.truncated);
	CHECK(Same(l.text[3], 137, 1, 159, 25));
	CHECK(Same(l.chevron[3], 159, 1, 175, 25));
	CHECK(Empty(l.empty));

	// cut to minSegment rather than less, losing the room to edit
	l = BrcLayoutBar(s, BrcParts{}, 235, M);
	CHECK(l.truncated);
	CHECK_INT(l.text[3].right - l.text[3].left, 40);

	// without a root chevron, it appears only once segments fold
	BrcParts parts;
	parts.rootChevron = false;
	l = BrcLayoutBar(s, parts, 600, M);
	CHECK(Empty(l.overflow));
	CHECK_INT(l.text[0].left, 121);
	l = BrcLayoutBar(s, parts, 300, M);
	CHECK(l.Overflowed());
	CHECK(Same(l.overflow, 121, 1, 137, 25));
	CHECK_INT(l.text[3].left, 137);
	// fits only without the chevron: 235 pixels for the segments and the room to edit
	l = BrcLayoutBar(s, parts, 405, M);
	CHECK_INT(l.firstVisible, 0);
	CHECK(Empty(l.overflow));
}

TEST(HitTest) {
	const auto s = Path();
	BrcParts parts;
	parts.actions = 1;
	const auto l = BrcLayoutBar(s, parts, 624, M);
	auto hit = [&](int x, int y = 10) {
		return BrcHitTest(l, POINT{ x, y });
	};
	CHECK(hit(10).part == BrcPart::Back);
	CHECK(hit(30).part == BrcPart::Forward);
	CHECK(hit(60).part == BrcPart::Recent);
	CHECK(hit(80).part == BrcPart::Up);
	CHECK(hit(94).part == BrcPart::None);      // the gap
	CHECK(hit(110).part == BrcPart::Icon);
	CHECK(hit(98).part == BrcPart::Empty);     // the icon's padding
	CHECK(hit(125).part == BrcPart::Overflow);
	auto h = hit(150);
	CHECK(h.part == BrcPart::Text && h.index == 0);
	h = hit(200);
	CHECK(h.part == BrcPart::Chevron && h.index == 0);
	h = hit(310);
	CHECK(h.part == BrcPart::Text && h.index == 3);
	CHECK(hit(400).part == BrcPart::Empty);
	CHECK(hit(580).part == BrcPart::History);
	h = hit(610);
	CHECK(h.part == BrcPart::Action && h.index == 0);
	CHECK(hit(400, 0).part == BrcPart::Empty);    // the frame
	CHECK(hit(400, 30).part == BrcPart::None);
	CHECK(hit(-1).part == BrcPart::None);
}

TEST(History) {
	BrcHistory h;
	CHECK(h.Current() == nullptr);
	CHECK(!h.CanGo(-1) && !h.CanGo(0));
	CHECK(h.MenuOffsets(5).empty());

	for (auto p : { L"A", L"B", L"C", L"D", L"E" })
		h.Navigate(Loc(p));
	CHECK_STR(h.Current()->path, L"E");
	CHECK(!h.CanGo(1));
	CHECK(h.Go(-1));
	CHECK(h.Go(-1));
	CHECK_STR(h.Current()->path, L"C");
	CHECK_STR(h.At(2)->path, L"E");
	CHECK_STR(h.At(-2)->path, L"A");
	CHECK(h.At(-3) == nullptr);
	CHECK(!h.Go(3));
	CHECK_STR(h.Current()->path, L"C");

	CHECK(h.MenuOffsets(3) == std::vector<int>({ 1, 0, -1 }));
	CHECK(h.MenuOffsets(10) == std::vector<int>({ 2, 1, 0, -1, -2 }));
	CHECK(h.MenuOffsets(1) == std::vector<int>({ 0 }));
	h.Go(2);
	CHECK(h.MenuOffsets(3) == std::vector<int>({ 0, -1, -2 }));
	h.Go(-4);
	CHECK(h.MenuOffsets(3) == std::vector<int>({ 2, 1, 0 }));
	CHECK(h.MenuOffsets(4) == std::vector<int>({ 3, 2, 1, 0 }));

	// the current one again (ignoring case) only updates it
	h.Go(2);
	BrcLocation c = Loc(L"c");
	c.text = L"Sea";
	h.Navigate(c);
	CHECK_INT(h.Size(), 5);
	CHECK_STR(h.Current()->text, L"Sea");
	CHECK(h.CanGo(2));

	// a new one drops the ones ahead
	h.Navigate(Loc(L"X"));
	CHECK_INT(h.Size(), 4);
	CHECK(!h.CanGo(1));
	CHECK_STR(h.At(-1)->path, L"c");

	// the limit drops the oldest
	BrcHistory l(3);
	for (auto p : { L"1", L"2", L"3", L"4", L"5" })
		l.Navigate(Loc(p));
	CHECK_INT(l.Size(), 3);
	CHECK_STR(l.Current()->path, L"5");
	CHECK_STR(l.At(-2)->path, L"3");
	CHECK(!l.CanGo(-3));

	l.Clear();
	CHECK(l.Current() == nullptr);
	CHECK_INT(l.Size(), 0);
}

TEST(Recent) {
	BrcRecent r(3);
	r.Add(L"C:\\Windows");
	r.Add(L"  D:\\Data ");
	r.Add(L"");
	CHECK_INT(r.Paths().size(), 2);
	CHECK_STR(r.Paths()[0], L"D:\\Data");
	r.Add(L"c:\\windows");     // again: to the front, as now typed
	CHECK_INT(r.Paths().size(), 2);
	CHECK_STR(r.Paths()[0], L"c:\\windows");
	r.Add(L"E:\\");
	r.Add(L"F:\\");
	CHECK_INT(r.Paths().size(), 3);
	CHECK_STR(r.Paths()[2], L"c:\\windows");
	CHECK(r.Remove(L"E:\\"));
	CHECK(!r.Remove(L"E:\\"));
	CHECK_STR(r.Save(), L"F:\\\nc:\\windows");

	BrcRecent s(3);
	s.Load(L"A\r\n\nB\na\nC\nD\n");
	CHECK_INT(s.Paths().size(), 3);
	CHECK_STR(s.Save(), L"A\nB\nC");
	s.Load(L"");
	CHECK(s.Paths().empty());
}

TEST(JoinPath) {
	auto s = Path();
	CHECK_STR(BrcJoinPath(s, L"\\"), L"C:\\Users\\pavel");
	CHECK_STR(BrcJoinPath(s, L"\\", 2), L"C:");
	CHECK_STR(BrcJoinPath(s, L"\\", 1), L"");
	s[1].name = L"C:\\";
	CHECK_STR(BrcJoinPath(s, L"\\"), L"C:\\Users\\pavel");
	CHECK_STR(BrcJoinPath(s, L"/\\"), L"C:\\Users/pavel");
	CHECK_STR(BrcJoinPath({}, L"\\"), L"");
}

TEST(SplitPath) {
	const std::wstring_view seps = L"\\/";
	CHECK(BrcSplitPath(L"  C:\\Users\\\\pavel\\ ", seps) == std::vector<std::wstring>({ L"C:", L"Users", L"pavel" }));
	CHECK(BrcSplitPath(L"\\\\server\\share", seps) == std::vector<std::wstring>({ L"\\\\server", L"share" }));
	CHECK(BrcSplitPath(L"/", seps) == std::vector<std::wstring>({ L"/" }));
	CHECK(BrcSplitPath(L"a / b", seps) == std::vector<std::wstring>({ L"a", L"b" }));
	CHECK(BrcSplitPath(L"   ", seps).empty());

	std::wstring parent, leaf;
	BrcSplitLast(L"C:\\Us", seps, parent, leaf);
	CHECK_STR(parent, L"C:\\");
	CHECK_STR(leaf, L"Us");
	BrcSplitLast(L"C:", seps, parent, leaf);
	CHECK_STR(parent, L"");
	CHECK_STR(leaf, L"C:");
	BrcSplitLast(L"C:\\Users/", seps, parent, leaf);
	CHECK_STR(parent, L"C:\\Users/");
	CHECK_STR(leaf, L"");
}

TEST(Suggest) {
	const std::vector<std::wstring> candidates = { L"C:\\Users", L"C:\\Utils", L"C:\\Windows" };
	const std::vector<std::wstring> recent = { L"c:\\users", L"D:\\x" };
	CHECK(BrcSuggest(L"C:\\U", candidates, recent) == std::vector<std::wstring>({ L"c:\\users", L"C:\\Utils" }));
	CHECK(BrcSuggest(L"C:\\U", candidates, recent, 1) == std::vector<std::wstring>({ L"c:\\users" }));
	CHECK(BrcSuggest(L"", candidates, recent) == recent);
	CHECK(BrcSuggest(L"Z", candidates, recent).empty());
	CHECK(BrcSuggest(L"C:\\Windows", candidates, {}) == std::vector<std::wstring>({ L"C:\\Windows" }));

	CHECK_STR(BrcCommonPrefix(L"C:\\U", { L"C:\\Users", L"C:\\User2" }), L"C:\\User");
	CHECK_STR(BrcCommonPrefix(L"c:\\u", { L"C:\\Users", L"c:\\USERS" }), L"C:\\Users");
	CHECK_STR(BrcCommonPrefix(L"C:\\U", { L"C:\\Utils", L"c:\\users" }), L"C:\\U");
	CHECK_STR(BrcCommonPrefix(L"C:\\U", {}), L"C:\\U");
	CHECK_STR(BrcCommonPrefix(L"C:\\Us", { L"C:\\Users" }), L"C:\\Users");
}

TEST(Scaled) {
	const auto m = M.Scaled(144);
	CHECK_INT(m.height, 39);
	CHECK_INT(m.chevron, 24);
	CHECK_INT(m.border, 1);
	CHECK_INT(M.Scaled(192).border, 2);
	CHECK_INT(M.Scaled(96).boxButton, M.boxButton);
}

int wmain() {
	wprintf(L"Breadcrumb model tests\n\n");

	Run_MeasureSegments();
	Run_LayoutParts();
	Run_LayoutSegments();
	Run_Fold();
	Run_HitTest();
	Run_History();
	Run_Recent();
	Run_JoinPath();
	Run_SplitPath();
	Run_Suggest();
	Run_Scaled();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
