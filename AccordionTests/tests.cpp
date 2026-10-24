// Unit tests for the Accordion model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the sections' state, the layout, hit testing, resizing,
// reordering and the saved state. Anything that needs an HWND is checked by hand with AccordionDemo.

#include "AccordionModel.h"

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

static AcdSection Fixed(UINT id, int height, bool expanded = true) {
	AcdSection s;
	s.id = id;
	s.mode = AcdHeight::Fixed;
	s.height = height;
	s.expanded = expanded;
	s.progress = expanded ? 1 : 0;
	return s;
}

static AcdSection Fill(UINT id, float weight = 1, bool expanded = true) {
	AcdSection s = Fixed(id, 0, expanded);
	s.mode = AcdHeight::Fill;
	s.weight = weight;
	return s;
}

static int Height(const RECT& rc) {
	return rc.bottom - rc.top;
}

static const AcdMetrics k_Metrics{ 20, 0, 96 };

// ---- Tests -------------------------------------------------------------------------

TEST(Expand_MultipleOpen) {
	std::vector<AcdSection> s{ Fixed(1, 50, false), Fixed(2, 50, false), Fixed(3, 50, false) };
	auto changed = AcdSetExpanded(s, 0, true, false);
	CHECK(changed.size() == 1 && changed[0] == 0);
	AcdSetExpanded(s, 2, true, false);
	CHECK(s[0].expanded && !s[1].expanded && s[2].expanded);
	// no change: nothing reported
	CHECK(AcdSetExpanded(s, 2, true, false).empty());
	CHECK(AcdSetExpanded(s, 9, true, false).empty());
}

TEST(Expand_Single) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 50, false), Fixed(3, 50) };
	s[2].collapsible = false;
	auto changed = AcdSetExpanded(s, 1, true, true);
	// the given one first, then the ones it closed; a section that can't close stays open
	CHECK(changed.size() == 2 && changed[0] == 1 && changed[1] == 0);
	CHECK(!s[0].expanded && s[1].expanded && s[2].expanded);
	// can't collapse a non-collapsible section
	CHECK(AcdSetExpanded(s, 2, false, true).empty() && s[2].expanded);
}

TEST(Animate_TowardTarget) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 50, false) };
	AcdSetExpanded(s, 0, false, false);
	AcdSetExpanded(s, 1, true, false);
	CHECK(AcdAnimate(s, 50, 100));
	CHECK(s[0].progress == 0.5 && s[1].progress == 0.5);
	CHECK(!AcdAnimate(s, 80, 100));   // overshooting stops at the end
	CHECK(s[0].progress == 0 && s[1].progress == 1);
	// no duration: straight there
	AcdSetExpanded(s, 0, true, false);
	CHECK(!AcdAnimate(s, 0, 0) && s[0].progress == 1);
	CHECK(AcdEase(0) == 0 && AcdEase(1) == 1 && AcdEase(0.5) == 0.5 && AcdEase(0.25) < 0.25);
}

TEST(Layout_Fixed) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 30, false), Fixed(3, 40) };
	auto layout = AcdLayoutSections(s, 200, 500, 0, k_Metrics);
	CHECK(layout.items[0].header.top == 0 && layout.items[0].header.bottom == 20);
	CHECK(layout.items[0].body.top == 20 && Height(layout.items[0].body) == 50);
	CHECK(layout.items[1].header.top == 70 && Height(layout.items[1].body) == 0);
	CHECK(layout.items[1].fullHeight == 30);
	CHECK(layout.items[2].header.top == 90 && Height(layout.items[2].body) == 40);
	CHECK(layout.contentHeight == 150 && layout.scrollMax == 0);
	CHECK(layout.items[2].body.right == 200);
}

TEST(Layout_DpiAndGap) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 50) };
	AcdMetrics m{ 30, 4, 144 };
	auto layout = AcdLayoutSections(s, 100, 500, 0, m);
	CHECK(Height(layout.items[0].body) == 75);
	CHECK(layout.items[1].header.top == 30 + 75 + 4);
	CHECK(layout.contentHeight == 2 * 30 + 2 * 75 + 4);
}

TEST(Layout_FillShares) {
	// 300 high, three headers of 20 and a fixed body of 60: 180 left, 1:3
	std::vector<AcdSection> s{ Fill(1, 1), Fixed(2, 60), Fill(3, 3) };
	auto layout = AcdLayoutSections(s, 100, 300, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 45 && Height(layout.items[2].body) == 135);
	CHECK(layout.contentHeight == 300 && layout.scrollMax == 0);
	// a closed fill section leaves it all to the other
	s[0].expanded = false;
	AcdSnap(s);
	layout = AcdLayoutSections(s, 100, 300, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 0 && Height(layout.items[2].body) == 180);
}

TEST(Layout_FillRounding) {
	// 100 left over three equal shares: 33.3 each, rounded without losing a pixel
	std::vector<AcdSection> s{ Fill(1), Fill(2), Fill(3) };
	auto layout = AcdLayoutSections(s, 100, 160, 0, k_Metrics);
	int sum = 0;
	for (auto& item : layout.items)
		sum += Height(item.body);
	CHECK(sum == 100);
	CHECK(layout.contentHeight == 160);
}

TEST(Layout_MinimumScrolls) {
	// no room for the fill section's minimum: it keeps it, and the content scrolls
	std::vector<AcdSection> s{ Fixed(1, 100), Fill(2) };
	s[1].minHeight = 80;
	auto layout = AcdLayoutSections(s, 100, 200, 0, k_Metrics);
	CHECK(Height(layout.items[1].body) == 80);
	CHECK(layout.contentHeight == 220 && layout.scrollMax == 20);
	CHECK(AcdClampScroll(100, layout) == 20 && AcdClampScroll(-5, layout) == 0);
	// scrolled: everything moves up
	auto scrolled = AcdLayoutSections(s, 100, 200, 20, k_Metrics);
	CHECK(scrolled.items[0].header.top == -20 && scrolled.contentHeight == 220);
	// a fixed body under its minimum gets the minimum
	s[0].minHeight = 150;
	layout = AcdLayoutSections(s, 100, 200, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 150);
}

TEST(Layout_Fit) {
	std::vector<AcdSection> s{ Fixed(1, 10) };
	s[0].mode = AcdHeight::Fit;
	s[0].fitHeight = 77;   // pixels: not scaled
	AcdMetrics m{ 20, 0, 192 };
	auto layout = AcdLayoutSections(s, 100, 500, 0, m);
	CHECK(Height(layout.items[0].body) == 77);
}

TEST(Layout_Animating) {
	// a fixed body half open shows half of itself (eased: 0.5 is half)
	std::vector<AcdSection> s{ Fixed(1, 100), Fill(2) };
	s[0].progress = 0.5;
	auto layout = AcdLayoutSections(s, 100, 300, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 50 && layout.items[0].fullHeight == 100);
	CHECK(Height(layout.items[1].body) == 300 - 40 - 50);
	// a lone fill section closing shrinks
	std::vector<AcdSection> lone{ Fill(1) };
	lone[0].expanded = false;
	lone[0].progress = 0.5;
	layout = AcdLayoutSections(lone, 100, 220, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 100);
	// two fill sections, one closing: the other grows into its space, together they keep it
	std::vector<AcdSection> two{ Fill(1), Fill(2) };
	two[0].expanded = false;
	two[0].progress = 0.5;
	layout = AcdLayoutSections(two, 100, 340, 0, k_Metrics);
	int a = Height(layout.items[0].body), b = Height(layout.items[1].body);
	CHECK(a + b == 300 && b > 150 && a > 0 && a < 150);
}

TEST(Layout_Empty) {
	std::vector<AcdSection> none;
	auto layout = AcdLayoutSections(none, 100, 100, 0, k_Metrics);
	CHECK(layout.items.empty() && layout.contentHeight == 0 && layout.scrollMax == 0);
}

TEST(HitTest_Parts) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 50) };
	auto layout = AcdLayoutSections(s, 100, 500, 0, k_Metrics);
	auto hit = AcdHitTest(layout, s, { 10, 5 }, 3);
	CHECK(hit.part == AcdPart::Header && hit.index == 0);
	hit = AcdHitTest(layout, s, { 10, 40 }, 3);
	CHECK(hit.part == AcdPart::Body && hit.index == 0);
	// the first body's bottom edge (y 70), from a little inside it to a little into the next header
	hit = AcdHitTest(layout, s, { 10, 68 }, 3);
	CHECK(hit.part == AcdPart::ResizeEdge && hit.index == 0);
	hit = AcdHitTest(layout, s, { 10, 72 }, 3);
	CHECK(hit.part == AcdPart::ResizeEdge && hit.index == 0);
	hit = AcdHitTest(layout, s, { 10, 80 }, 3);
	CHECK(hit.part == AcdPart::Header && hit.index == 1);
	hit = AcdHitTest(layout, s, { 10, 400 }, 3);
	CHECK(hit.part == AcdPart::None);
	// a closed section has no edge
	s[0].expanded = false;
	AcdSnap(s);
	layout = AcdLayoutSections(s, 100, 500, 0, k_Metrics);
	hit = AcdHitTest(layout, s, { 10, 20 }, 3);
	CHECK(hit.part == AcdPart::Header && hit.index == 1);
}

TEST(Resize_Fixed) {
	std::vector<AcdSection> s{ Fixed(1, 50), Fixed(2, 50) };
	s[0].minHeight = 30;
	AcdMetrics m{ 20, 0, 192 };
	auto layout = AcdLayoutSections(s, 100, 500, 0, m);
	AcdResize(s, 0, 160, layout, m);
	CHECK(s[0].height == 80);   // DIPs
	AcdResize(s, 0, 10, layout, m);
	CHECK(s[0].height == 30);   // not under the minimum
	// a fit section becomes fixed
	s[1].mode = AcdHeight::Fit;
	s[1].fitHeight = 40;
	layout = AcdLayoutSections(s, 100, 500, 0, m);
	AcdResize(s, 1, 100, layout, m);
	CHECK(s[1].mode == AcdHeight::Fixed && s[1].height == 50);
}

TEST(Resize_Fill) {
	// a lone fill section can't be resized (it fills); two trade height
	std::vector<AcdSection> s{ Fixed(1, 50), Fill(2) };
	CHECK(AcdCanResize(s, 0) && !AcdCanResize(s, 1));
	std::vector<AcdSection> two{ Fill(1), Fixed(2, 40), Fill(3, 3) };
	CHECK(AcdCanResize(two, 0) && !AcdCanResize(two, 2));
	// 340 high: 60 of headers, 40 fixed, 240 between them 60:180
	auto layout = AcdLayoutSections(two, 100, 340, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 60 && Height(layout.items[2].body) == 180);
	AcdResize(two, 0, 120, layout, k_Metrics);
	layout = AcdLayoutSections(two, 100, 340, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 120 && Height(layout.items[2].body) == 120);
	// kept to the other's minimum
	two[2].minHeight = 100;
	AcdResize(two, 0, 230, layout, k_Metrics);
	layout = AcdLayoutSections(two, 100, 340, 0, k_Metrics);
	CHECK(Height(layout.items[0].body) == 140 && Height(layout.items[2].body) == 100);
	// a closed or animating section can't be resized
	two[0].progress = 0.5;
	CHECK(!AcdCanResize(two, 0));
}

TEST(Reorder) {
	std::vector<AcdSection> s{ Fixed(1, 0), Fixed(2, 0), Fixed(3, 0), Fixed(4, 0) };
	auto layout = AcdLayoutSections(s, 100, 500, 0, k_Metrics);   // headers at 0, 20, 40, 60
	CHECK(AcdDropIndex(layout, 5) == 0 && AcdDropIndex(layout, 15) == 1 && AcdDropIndex(layout, 75) == 4);
	CHECK(AcdMove(s, 0, 3) == 2);   // before the fourth: third
	CHECK(s[0].id == 2 && s[1].id == 3 && s[2].id == 1 && s[3].id == 4);
	CHECK(AcdMove(s, 3, 0) == 0);
	CHECK(s[0].id == 4 && s[1].id == 2);
	CHECK(AcdMove(s, 1, 1) == 1 && AcdMove(s, 1, 2) == 1);   // where it is already
	CHECK(AcdMove(s, 0, 99) == 3 && s[3].id == 4);           // past the end: last
}

TEST(State_RoundTrip) {
	std::vector<AcdSection> s{ Fixed(10, 50), Fill(20, 2.5f, false), Fixed(30, 70, false) };
	auto text = AcdSaveState(s);
	CHECK_STR(text, L"1;10,1,50;20,0,2.5;30,0,70");
	// another order, other sizes and states: loading brings them back
	std::vector<AcdSection> t{ Fixed(30, 10), Fixed(10, 10, false), Fill(20, 1) };
	CHECK(AcdLoadState(t, text));
	CHECK(t[0].id == 10 && t[1].id == 20 && t[2].id == 30);
	CHECK(t[0].expanded && t[0].height == 50 && t[0].progress == 1);
	CHECK(!t[1].expanded && t[1].weight == 2.5f && t[1].progress == 0);
	CHECK(!t[2].expanded && t[2].height == 70);
}

TEST(State_UnknownAndMissing) {
	// an unknown id is skipped; a section the text doesn't name goes after, in its order
	std::vector<AcdSection> s{ Fixed(1, 10), Fixed(2, 10), Fixed(3, 10) };
	CHECK(AcdLoadState(s, L"1;3,0,40;99,1,5;1,1,20"));
	CHECK(s[0].id == 3 && s[1].id == 1 && s[2].id == 2);
	CHECK(!s[0].expanded && s[0].height == 40 && s[2].height == 10);
	// a section that can't close stays open
	s[0].collapsible = false;
	CHECK(AcdLoadState(s, L"1;3,0,40") && s[0].expanded);
	// the mode isn't changed: a fill section's weight from a fixed height is taken as a weight
	std::vector<AcdSection> f{ Fill(5, 1) };
	CHECK(AcdLoadState(f, L"1;5,1,3") && f[0].mode == AcdHeight::Fill && f[0].weight == 3);
}

TEST(State_Invalid) {
	std::vector<AcdSection> s{ Fixed(1, 10) };
	CHECK(!AcdLoadState(s, L""));
	CHECK(!AcdLoadState(s, L"2;1,1,10"));
	CHECK(!AcdLoadState(s, L"1;1,1"));
	CHECK(!AcdLoadState(s, L"1;1,2,10"));
	CHECK(!AcdLoadState(s, L"1;1,1,10x"));
	CHECK(!AcdLoadState(s, L"1;1,1,-4"));
	CHECK(s[0].height == 10);   // nothing changed
	CHECK(AcdLoadState(s, L"1") && s[0].height == 10);
	CHECK(AcdLoadState(s, L"1;"));
}

int wmain() {
	wprintf(L"Accordion model tests\n\n");

	Run_Expand_MultipleOpen();
	Run_Expand_Single();
	Run_Animate_TowardTarget();

	Run_Layout_Fixed();
	Run_Layout_DpiAndGap();
	Run_Layout_FillShares();
	Run_Layout_FillRounding();
	Run_Layout_MinimumScrolls();
	Run_Layout_Fit();
	Run_Layout_Animating();
	Run_Layout_Empty();

	Run_HitTest_Parts();
	Run_Resize_Fixed();
	Run_Resize_Fill();
	Run_Reorder();

	Run_State_RoundTrip();
	Run_State_UnknownAndMissing();
	Run_State_Invalid();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
