// Unit tests for the RibbonBar model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: measuring, the steps groups shrink through, placing items,
// fitting a tab, hit testing, the tabs' order, key tips, radio groups, arrow keys and the state text. Anything that
// needs an HWND is checked by hand with RibbonBarDemo.

#include "RibbonBarModel.h"

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
static const RibMeasure Measure = [](std::wstring_view s) {
	return (int)s.size() * 6;
};

static const RibMetrics M;     // 96 DPI

static RibItem Button(UINT id, const wchar_t* text, RibKind kind = RibKind::Button, RibSize size = RibSize::Large) {
	RibItem item;
	item.id = id;
	item.text = text;
	item.kind = kind;
	item.size = size;
	item.image = item.smallImage = (int)id;
	return item;
}

// "File": New, Open, Save, Print, all large
static RibGroup FileGroup(UINT id = 1) {
	RibGroup g;
	g.id = id;
	g.text = L"File";
	g.items = { Button(10, L"New"), Button(11, L"Open"), Button(12, L"Save"), Button(13, L"Print") };
	RibMeasureGroup(g, M, Measure);
	return g;
}

static std::vector<int> Widths(const RibGroup& g) {
	std::vector<int> w;
	for (auto& s : g.steps)
		w.push_back(s.width);
	return w;
}

static bool Same(const RECT& a, const RECT& b) {
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

// ---- Measuring ------------------------------------------------------------------------------

TEST(SplitLabel) {
	std::wstring a, b;
	RibSplitLabel(L"Paste", Measure, 0, a, b);
	CHECK(a == L"Paste" && b.empty());
	RibSplitLabel(L"Format Painter", Measure, 0, a, b);
	CHECK(a == L"Format" && b == L"Painter");
	RibSplitLabel(L"Insert Table Of Contents", Measure, 0, a, b);
	CHECK(a == L"Insert Table" && b == L"Of Contents");
	RibSplitLabel(L"Insert Table Of Contents", Measure, 40, a, b);   // room for something after line 2
	CHECK(a == L"Insert Table Of" && b == L"Contents");
	RibSplitLabel(L"Two  spaces", Measure, 0, a, b);
	CHECK(a == L"Two" && b == L"spaces");
	RibSplitLabel(L"", Measure, 0, a, b);
	CHECK(a.empty() && b.empty());
}

TEST(MeasureItems) {
	auto paste = Button(1, L"Paste");
	RibMeasureItem(paste, M, Measure);
	CHECK_INT(paste.width[(int)RibSize::Large], 38);    // the icon is wider than the label
	CHECK_INT(paste.width[(int)RibSize::Small], 56);
	CHECK_INT(paste.width[(int)RibSize::Icon], 22);

	auto painter = Button(2, L"Format Painter");
	RibMeasureItem(painter, M, Measure);
	CHECK_INT(painter.width[(int)RibSize::Large], 48);  // "Painter" is the wider line
	CHECK(painter.line1 == L"Format" && painter.line2 == L"Painter");
	CHECK_INT(painter.width[(int)RibSize::Small], 110);

	auto drop = Button(3, L"Paste", RibKind::DropDown);
	RibMeasureItem(drop, M, Measure);
	CHECK_INT(drop.width[(int)RibSize::Large], 38);     // the arrow alone on the second line
	CHECK(drop.line2.empty());
	CHECK_INT(drop.width[(int)RibSize::Small], 67);
	CHECK_INT(drop.width[(int)RibSize::Icon], 33);

	RibItem window;
	window.kind = RibKind::Window;
	window.text = L"Font";
	window.windowWidth = 100;
	RibMeasureItem(window, M, Measure);
	CHECK_INT(window.width[(int)RibSize::Small], 128);  // label, gap, window
	CHECK_INT(window.width[(int)RibSize::Icon], 100);
	CHECK(window.Largest() == RibSize::Small);

	auto plain = Button(4, L"Text");
	plain.image = plain.smallImage = -1;
	CHECK(plain.Smallest() == RibSize::Small);          // no image, no icon size
	RibMeasureItem(plain, M, Measure);
	CHECK_INT(plain.width[(int)RibSize::Small], 30);
	plain.minSize = RibSize::Large;
	CHECK(plain.Smallest() == RibSize::Large);
	plain.size = RibSize::Small;
	CHECK(plain.Smallest() == RibSize::Small);          // never below its largest

	RibItem sep;
	sep.kind = RibKind::Separator;
	RibMeasureItem(sep, M, Measure);
	CHECK_INT(sep.width[0], 9);
	CHECK_INT(sep.Rows(RibSize::Small), 3);
}

// ---- Steps ---------------------------------------------------------------------------------------

TEST(Steps) {
	auto g = FileGroup();
	// all large; the last three small; (New small too isn't narrower); three icons; all icons; collapsed
	CHECK(Widths(g) == std::vector<int>({ 166, 104, 76, 54, 46 }));
	CHECK(g.steps[1].sizes == std::vector<RibSize>({ RibSize::Large, RibSize::Small, RibSize::Small, RibSize::Small }));
	CHECK(g.steps[2].sizes == std::vector<RibSize>({ RibSize::Small, RibSize::Icon, RibSize::Icon, RibSize::Icon }));
	CHECK(g.steps.back().collapsed);
	CHECK(!g.steps[3].collapsed);
	CHECK(g.Visible());

	g.flags = RibGroupNoCollapse;
	RibGroupSteps(g, M);
	CHECK(Widths(g) == std::vector<int>({ 166, 104, 76, 54 }));

	g.flags = 0;
	for (auto& item : g.items)
		item.minSize = RibSize::Small;
	RibGroupSteps(g, M);
	CHECK(Widths(g) == std::vector<int>({ 166, 104, 46 }));

	g = FileGroup();
	g.items[3].state = RibStateHidden;
	RibGroupSteps(g, M);
	CHECK_INT(g.steps[0].width, 3 * 38 + 2 * 2 + 8);
	for (auto& item : g.items)
		item.state = RibStateHidden;
	RibGroupSteps(g, M);
	CHECK(g.steps.empty() && !g.Visible());
}

TEST(PlaceItems) {
	auto g = FileGroup();
	int width = 0;
	auto p = RibPlaceItems(g, g.steps[0].sizes, M, &width);
	CHECK_INT(width, 166);
	CHECK(Same(p[0].rc, { 4, 4, 42, 70 }));
	CHECK(Same(p[1].rc, { 44, 4, 82, 70 }));

	p = RibPlaceItems(g, g.steps[1].sizes, M, &width);
	CHECK(Same(p[0].rc, { 4, 4, 42, 70 }));
	CHECK(Same(p[1].rc, { 44, 4, 94, 26 }));      // a column of three rows
	CHECK(Same(p[2].rc, { 44, 26, 94, 48 }));
	CHECK(Same(p[3].rc, { 44, 48, 100, 70 }));    // its own width
	CHECK(p[3].size == RibSize::Small);

	// a window two rows high doesn't fit under two small items: a new column
	RibGroup w;
	w.items = { Button(1, L"A", RibKind::Button, RibSize::Small), Button(2, L"B", RibKind::Button, RibSize::Small) };
	RibItem combo;
	combo.kind = RibKind::Window;
	combo.windowWidth = 60;
	combo.rows = 2;
	w.items.push_back(combo);
	RibItem sep;
	sep.kind = RibKind::Separator;
	w.items.push_back(sep);
	w.items.push_back(Button(3, L"C", RibKind::Button, RibSize::Small));
	RibMeasureGroup(w, M, Measure);
	p = RibPlaceItems(w, w.steps[0].sizes, M, &width);
	CHECK(Same(p[0].rc, { 4, 4, 36, 26 }));
	CHECK(Same(p[1].rc, { 4, 26, 36, 48 }));
	CHECK(Same(p[2].rc, { 38, 4, 98, 48 }));
	CHECK(Same(p[3].rc, { 100, 4, 109, 70 }));    // a separator takes a column
	CHECK(Same(p[4].rc, { 111, 4, 143, 26 }));
	CHECK_INT(width, 143 + 4);

	// a label wider than the items: they're centered
	RibGroup wide;
	wide.text = L"Very long group label";      // 126
	wide.items = { Button(1, L"B", RibKind::Button, RibSize::Small) };
	RibMeasureGroup(wide, M, Measure);
	p = RibPlaceItems(wide, wide.steps[0].sizes, M, &width);
	CHECK_INT(width, 134);
	CHECK_INT(p[0].rc.left, 4 + (126 - 32) / 2);
}

// ---- Fitting a tab ------------------------------------------------------------------------------------

TEST(FitTab) {
	std::vector<RibGroup> groups{ FileGroup(1), FileGroup(2) };
	int total = 0;
	CHECK(RibFitTab(groups, 333, M, &total) == std::vector<int>({ 0, 0 }));
	CHECK_INT(total, 333);
	CHECK(RibFitTab(groups, 332, M, &total) == std::vector<int>({ 0, 1 }));    // the right one first
	CHECK_INT(total, 271);
	CHECK(RibFitTab(groups, 250, M, &total) == std::vector<int>({ 1, 1 }));    // then round the left
	CHECK(RibFitTab(groups, 200, M, &total) == std::vector<int>({ 1, 2 }));
	CHECK_INT(total, 181);
	CHECK(RibFitTab(groups, 10, M, &total) == std::vector<int>({ 4, 4 }));     // doesn't fit at all
	CHECK_INT(total, 93);

	// a lower priority shrinks all the way first
	groups[1].priority = 1;
	CHECK(RibFitTab(groups, 200, M, &total) == std::vector<int>({ 4, 1 }));
	CHECK_INT(total, 151);

	// a group with fewer steps doesn't collapse while the other can still shrink
	groups[1].priority = 0;
	groups[0].flags = 0;
	for (auto& item : groups[0].items)
		item.size = RibSize::Icon;
	RibGroupSteps(groups[0], M);
	CHECK(Widths(groups[0]) == std::vector<int>({ 54, 46 }));    // all icons, collapsed
	CHECK(RibFitTab(groups, 120, M, &total) == std::vector<int>({ 0, 3 }));
	CHECK_INT(total, 54 + 1 + 54);
	CHECK(RibFitTab(groups, 105, M, &total) == std::vector<int>({ 0, 4 }));     // the right one collapses first
	CHECK_INT(total, 54 + 1 + 46);
	CHECK(RibFitTab(groups, 95, M, &total) == std::vector<int>({ 1, 4 }));
	CHECK_INT(total, 93);
	groups[0] = FileGroup(1);
	groups[1].priority = 1;

	groups[0].hidden = true;
	CHECK(RibFitTab(groups, 200, M, &total) == std::vector<int>({ -1, 0 }));
	CHECK_INT(total, 166);

	groups[0].hidden = false;
	groups[1].priority = 0;
	auto places = RibLayoutTab(groups, 250, M, &total);
	CHECK_INT(places[0].x, 0);
	CHECK_INT(places[1].x, 105);
	CHECK_INT(total, 209);
	CHECK(Same(places[1].label, { 4, 70, 100, 88 }));
	CHECK(places[1].items.size() == 4);

	places = RibLayoutTab(groups, 10, M);
	CHECK(places[0].collapsed && places[1].collapsed);
	CHECK(Same(RibCollapsedRect(places[0].width, M), { 4, 4, 42, 88 }));
}

// ---- Hit testing ----------------------------------------------------------------------------------------

TEST(HitTest) {
	std::vector<RibGroup> groups{ FileGroup(1) };
	RibGroup clip;
	clip.text = L"Clipboard";
	clip.flags = RibGroupLauncher;
	clip.items = { Button(20, L"Paste", RibKind::Split), Button(21, L"Cut", RibKind::Split, RibSize::Small) };
	RibMeasureGroup(clip, M, Measure);
	groups.push_back(clip);
	auto places = RibLayoutTab(groups, 1000, M);

	auto hit = RibHitTest(groups, places, { 20, 20 }, M);
	CHECK(hit.part == RibPart::Item && hit.group == 0 && hit.item == 0);
	hit = RibHitTest(groups, places, { 43, 20 }, M);     // between the columns
	CHECK(hit.part == RibPart::None && hit.group == 0);
	hit = RibHitTest(groups, places, { 500, 20 }, M);
	CHECK(hit.part == RibPart::None && hit.group == -1);

	int x = places[1].x;
	auto& paste = places[1].items[0].rc;     // large split: image above, the arrow part below
	hit = RibHitTest(groups, places, { x + paste.left + 5, paste.top + 10 }, M);
	CHECK(hit.part == RibPart::Item && hit.group == 1 && hit.item == 0);
	hit = RibHitTest(groups, places, { x + paste.left + 5, paste.bottom - 5 }, M);
	CHECK(hit.part == RibPart::Arrow && hit.item == 0);
	auto& cut = places[1].items[1].rc;       // small: the arrow at the right
	hit = RibHitTest(groups, places, { x + cut.right - 3, cut.top + 5 }, M);
	CHECK(hit.part == RibPart::Arrow && hit.item == 1);
	hit = RibHitTest(groups, places, { x + cut.left + 3, cut.top + 5 }, M);
	CHECK(hit.part == RibPart::Item && hit.item == 1);
	auto& launcher = places[1].launcher;
	CHECK(launcher.right == places[1].width - 4 && launcher.right - launcher.left == 12);
	hit = RibHitTest(groups, places, { x + launcher.left + 2, launcher.top + 2 }, M);
	CHECK(hit.part == RibPart::Launcher && hit.group == 1);

	RECT main, arrow;
	RibSplitRects({ 0, 0, 40, 66 }, RibSize::Large, M, main, arrow);
	CHECK(Same(main, { 0, 0, 40, 38 }) && Same(arrow, { 0, 38, 40, 66 }));
	RibSplitRects({ 0, 0, 60, 22 }, RibSize::Small, M, main, arrow);
	CHECK(Same(main, { 0, 0, 48, 22 }) && Same(arrow, { 48, 0, 60, 22 }));

	places = RibLayoutTab(groups, 10, M);    // collapsed
	hit = RibHitTest(groups, places, { 10, 10 }, M);
	CHECK(hit.part == RibPart::Group && hit.group == 0);
}

// ---- Tabs -----------------------------------------------------------------------------------------------

TEST(VisibleTabs) {
	std::vector<RibTab> tabs(6);
	const wchar_t* names[] = { L"Home", L"Insert", L"Format", L"Table", L"View", L"Layout" };
	int contexts[] = { 0, 0, 1, 2, 0, 2 };
	for (int i = 0; i < 6; i++) {
		tabs[i].text = names[i];
		tabs[i].context = contexts[i];
	}
	std::vector<RibContext> sets(2);
	sets[0].id = 2;
	sets[1].id = 1;
	CHECK(RibVisibleTabs(tabs, sets) == std::vector<int>({ 0, 1, 4 }));
	sets[0].shown = sets[1].shown = true;
	CHECK(RibVisibleTabs(tabs, sets) == std::vector<int>({ 0, 1, 4, 3, 5, 2 }));   // by the contexts' order
	tabs[1].hidden = true;
	sets[0].shown = false;
	CHECK(RibVisibleTabs(tabs, sets) == std::vector<int>({ 0, 4, 2 }));
}

// ---- Key tips ---------------------------------------------------------------------------------------------

static bool PrefixFree(const std::vector<std::wstring>& tips) {
	for (size_t i = 0; i < tips.size(); i++) {
		if (tips[i].empty())
			return false;
		for (size_t j = 0; j < tips.size(); j++)
			if (i != j && tips[j].starts_with(tips[i]))
				return false;
	}
	return true;
}

TEST(KeyTips) {
	auto tips = RibAssignKeyTips({ L"Home", L"Insert", L"Help", L"Design", L"Draw", L"Developer" }, {});
	CHECK(tips == std::vector<std::wstring>({ L"H", L"I", L"E", L"D", L"R", L"V" }));

	tips = RibAssignKeyTips({ L"File", L"Format", L"Page Layout" }, { L"f" });
	CHECK(tips == std::vector<std::wstring>({ L"F", L"O", L"P" }));     // the app's kept, in capitals

	// word starts first: "Page Layout" gets L before anyone takes the other letters
	tips = RibAssignKeyTips({ L"Pan", L"Page Layout", L"Lap" }, {});
	CHECK(tips == std::vector<std::wstring>({ L"P", L"L", L"A" }));

	// out of letters: digits, then two characters, none the start of another
	std::vector<std::wstring> same(11, L"A");
	tips = RibAssignKeyTips(same, {});
	CHECK_STR(tips[0], L"A");
	CHECK_STR(tips[1], L"1");
	CHECK_STR(tips[9], L"9");
	CHECK_STR(tips[10], L"BA");
	CHECK(PrefixFree(tips));

	// a fixed two-character tip keeps its first letter from anyone else
	tips = RibAssignKeyTips({ L"Bold", L"Bullets" }, { L"BO" });
	CHECK(tips[0] == L"BO" && tips[1] == L"U");
	CHECK(PrefixFree(tips));

	CHECK_STR(RibQatKeyTip(0), L"1");
	CHECK_STR(RibQatKeyTip(8), L"9");
	CHECK_STR(RibQatKeyTip(9), L"09");
	CHECK_STR(RibQatKeyTip(17), L"01");
	CHECK_STR(RibQatKeyTip(18), L"0A");
	CHECK_STR(RibQatKeyTip(100), L"");

	std::vector<std::wstring> level{ L"H", L"FA", L"FB" };
	int count = 0;
	CHECK(RibMatchKeyTip(level, L"F", &count) == -1 && count == 2);
	CHECK(RibMatchKeyTip(level, L"fa", &count) == 1 && count == 1);
	CHECK(RibMatchKeyTip(level, L"h", &count) == 0);
	CHECK(RibMatchKeyTip(level, L"X", &count) == -1 && count == 0);
}

// ---- Items -------------------------------------------------------------------------------------------------

TEST(Radio) {
	std::vector<RibGroup> groups(2);
	for (int i = 0; i < 3; i++) {
		auto item = Button(100 + i, L"Align", RibKind::Radio, RibSize::Small);
		item.radio = 1;
		groups[0].items.push_back(item);
	}
	groups[0].items[0].state = RibStateChecked;
	auto other = Button(200, L"Other", RibKind::Radio);
	other.radio = 2;
	other.state = RibStateChecked;
	groups[1].items.push_back(other);
	auto toggle = Button(201, L"Bold", RibKind::Toggle);
	groups[1].items.push_back(toggle);

	CHECK(RibCheckRadio(groups, 0, 2));
	CHECK(!(groups[0].items[0].state & RibStateChecked));
	CHECK(groups[0].items[2].state & RibStateChecked);
	CHECK(groups[1].items[0].state & RibStateChecked);   // another radio group
	CHECK(!RibCheckRadio(groups, 0, 2));                  // already
	CHECK(!RibCheckRadio(groups, 1, 1));                  // not a radio item
	CHECK(!RibCheckRadio(groups, 5, 0));
}

TEST(Nearest) {
	// New (large), then a column of Open, Save, Print
	std::vector<RECT> rects{ { 4, 4, 42, 70 }, { 44, 4, 94, 26 }, { 44, 26, 94, 48 }, { 44, 48, 100, 70 }, {} };
	CHECK(RibNearest(rects, 0, VK_RIGHT) == 1);
	CHECK(RibNearest(rects, 3, VK_LEFT) == 0);
	CHECK(RibNearest(rects, 2, VK_UP) == 1);
	CHECK(RibNearest(rects, 2, VK_DOWN) == 3);
	CHECK(RibNearest(rects, 1, VK_UP) == -1);
	CHECK(RibNearest(rects, 2, VK_RIGHT) == -1);
	CHECK(RibNearest(rects, 0, VK_LEFT) == -1);
	CHECK(RibNearest(rects, 4, VK_LEFT) == -1);

	// lined up wins over closer but off the line
	std::vector<RECT> row{ { 0, 0, 20, 20 }, { 25, 40, 45, 60 }, { 100, 0, 120, 20 } };
	CHECK(RibNearest(row, 0, VK_RIGHT) == 2);
	CHECK(RibNearest(row, 1, VK_RIGHT) == 2);
}

// ---- State --------------------------------------------------------------------------------------------------

TEST(State) {
	RibState s;
	s.tab = 3;
	s.minimized = true;
	s.qat = { 101, 102 };
	CHECK_STR(RibSaveState(s), L"tab=3;min=1;qat=101,102");
	RibState t;
	CHECK(RibLoadState(RibSaveState(s), t));
	CHECK(t.tab == 3 && t.minimized && t.qat == std::vector<UINT>({ 101, 102 }));
	CHECK_STR(RibSaveState({}), L"tab=0;min=0;qat=");
	CHECK(RibLoadState(L"tab=0;min=0;qat=", t) && t.qat.empty() && !t.minimized);
	CHECK(RibLoadState(L"tab=5", t) && t.tab == 5 && t.qat.empty());
	CHECK(RibLoadState(L"foo=bar;tab=2;", t) && t.tab == 2);     // unknown keys skipped
	CHECK(!RibLoadState(L"garbage", t));
	CHECK(t.tab == 0);                                            // left empty
	CHECK(!RibLoadState(L"tab=x", t));
	CHECK(!RibLoadState(L"min=2", t));
	CHECK(!RibLoadState(L"qat=1,,2", t));
	CHECK(!RibLoadState(L"tab=99999999999", t));
	CHECK(!RibLoadState(L"", t));
}

TEST(Scaled) {
	auto m = M.Scaled(144);
	CHECK_INT(m.rowHeight, 33);
	CHECK_INT(m.largeIcon, 48);
	CHECK_INT(m.BodyHeight(), 2 * 6 + 99 + 27);
	CHECK_INT(M.BodyHeight(), 92);
}

// ---- Entry point -------------------------------------------------------------------------

int wmain() {
	wprintf(L"RibbonBar model tests\n\n");

	Run_SplitLabel();
	Run_MeasureItems();
	Run_Steps();
	Run_PlaceItems();
	Run_FitTab();
	Run_HitTest();
	Run_VisibleTabs();
	Run_KeyTips();
	Run_Radio();
	Run_Nearest();
	Run_State();
	Run_Scaled();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
