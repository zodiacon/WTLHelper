// Unit tests for the Notify model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the link markup, wrapping, a message's layout as a row or a toast,
// the info bar's stack, where toasts go, the toasts' timer and the easing. Anything that needs an HWND is checked by
// hand with NotifyDemo.

#include "NotifyModel.h"

#include <climits>
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

// every character 6 pixels wide, 7 in bold
static const NtfMeasure Measure = [](std::wstring_view s, bool bold) {
	return (int)s.size() * (bold ? 7 : 6);
};

static const NtfMetrics M;     // 96 DPI

static bool Same(const RECT& rc, int left, int top, int right, int bottom) {
	return rc.left == left && rc.top == top && rc.right == right && rc.bottom == bottom;
}

static bool Empty(const RECT& rc) {
	return ::IsRectEmpty(&rc) != FALSE;
}

static std::vector<NtfRun> Plain(const wchar_t* text) {
	return { NtfRun{ text } };
}

// "Update" "A new version is ready." [Restart] [Later] x
static NtfMessage Update() {
	NtfMessage m;
	m.title = L"Update";
	m.runs = Plain(L"A new version is ready.");
	m.buttons = { { 1, L"Restart" }, { 2, L"Later" } };
	return m;
}

// ---- Tests ----------------------------------------------------------------------

TEST(ParseLinks) {
	auto r = NtfParseLinks(L"Saved. <a id=\"undo\">Undo</a> it");
	CHECK_INT(r.size(), 3);
	CHECK_STR(r[0].text, L"Saved. ");
	CHECK(!r[0].IsLink());
	CHECK_STR(r[1].text, L"Undo");
	CHECK_STR(r[1].link, L"undo");
	CHECK_STR(r[2].text, L" it");

	r = NtfParseLinks(L"<a>Open</a>");
	CHECK_INT(r.size(), 1);
	CHECK_STR(r[0].link, L"Open");

	r = NtfParseLinks(L"see <A HREF='http://x'>site</A>");
	CHECK_INT(r.size(), 2);
	CHECK_STR(r[1].link, L"http://x");
	r = NtfParseLinks(L"<a href=\"h\" id=k>both</a>");
	CHECK_STR(r[0].link, L"k");

	r = NtfParseLinks(L"a &lt;b&gt; &amp; c &quot;");
	CHECK_INT(r.size(), 1);
	CHECK_STR(r[0].text, L"a <b> & c \"");
	CHECK_STR(NtfParseLinks(L"<a id=\"k\">R&amp;D</a>")[0].text, L"R&D");

	// not links: text
	CHECK_STR(NtfPlainText(NtfParseLinks(L"x < y")), L"x < y");
	CHECK_STR(NtfPlainText(NtfParseLinks(L"<a id=\"q\">never closed")), L"<a id=\"q\">never closed");
	CHECK_STR(NtfPlainText(NtfParseLinks(L"<abbr>t</abbr>")), L"<abbr>t</abbr>");
	CHECK_STR(NtfPlainText(NtfParseLinks(L"&nbsp;")), L"&nbsp;");
	r = NtfParseLinks(L"<a id=\"e\"></a>end");
	CHECK_INT(r.size(), 1);
	CHECK_STR(r[0].text, L"end");
	CHECK(NtfParseLinks(L"").empty());
}

TEST(Wrap) {
	// "aaa " 24, "bbb " to 48, "ccc" doesn't fit 50
	auto l = NtfWrap(Plain(L"aaa bbb ccc"), 50, Measure);
	CHECK_INT(l.lines, 2);
	CHECK_INT(l.pieces.size(), 2);
	CHECK_INT(l.pieces[0].start, 0);
	CHECK_INT(l.pieces[0].length, 8);
	CHECK_INT(l.pieces[0].width, 48);
	CHECK_INT(l.pieces[1].line, 1);
	CHECK_INT(l.pieces[1].x, 0);
	CHECK_INT(l.pieces[1].start, 8);
	CHECK_INT(l.width, 42);   // trailing spaces left out
	CHECK(!l.truncated);

	l = NtfWrap(Plain(L"ab\ncd"), 500, Measure);
	CHECK_INT(l.lines, 2);
	CHECK_INT(l.pieces[1].line, 1);
	CHECK_INT(l.pieces[1].start, 3);

	// a word too wide: four characters a line
	l = NtfWrap(Plain(L"abcdefghij"), 25, Measure);
	CHECK_INT(l.lines, 3);
	CHECK_INT(l.pieces.size(), 3);
	CHECK_INT(l.pieces[0].length, 4);
	CHECK_INT(l.pieces[2].length, 2);
	CHECK_INT(l.pieces[2].line, 2);
	l = NtfWrap(Plain(L"abcdefghij"), 25, Measure, 2);
	CHECK_INT(l.lines, 2);
	CHECK(l.truncated);
	CHECK_INT(l.pieces.size(), 2);

	// runs on one line; bold measured as bold
	std::vector<NtfRun> runs = { { L"Click " }, { L"here", L"go" }, { L" now" } };
	l = NtfWrap(runs, 1000, Measure);
	CHECK_INT(l.lines, 1);
	CHECK_INT(l.pieces.size(), 3);
	CHECK_INT(l.pieces[1].x, 36);
	CHECK_INT(l.pieces[1].width, 24);
	CHECK_INT(l.pieces[2].x, 60);
	CHECK_INT(NtfWrap({ { L"Hi", L"", true } }, 100, Measure).pieces[0].width, 14);
	CHECK_INT(NtfWrap({}, 100, Measure).lines, 0);

	CHECK_INT(NtfLinkAt(l, runs, 40, 5, 18), 1);
	CHECK_INT(NtfLinkAt(l, runs, 10, 5, 18), -1);
	CHECK_INT(NtfLinkAt(l, runs, 40, 20, 18), -1);
	CHECK_INT(NtfLinkAt(l, runs, -1, 5, 18), -1);
}

TEST(BarRow) {
	auto m = Update();
	// wide: the buttons beside the text
	auto l = NtfLayoutMessage(m, NtfStyle::Bar, 800, M, Measure, L"Show details");
	CHECK(Same(l.icon, 8, 9, 24, 25));
	CHECK(Same(l.close, 768, 5, 792, 29));
	CHECK(l.buttonsInline);
	CHECK(Same(l.text, 32, 8, 606, 26));
	CHECK_INT(l.buttons.size(), 2);
	CHECK(Same(l.buttons[0], 614, 5, 686, 29));
	CHECK(Same(l.buttons[1], 692, 5, 764, 29));
	CHECK_INT(l.height, 37);
	CHECK_INT(l.textRuns.size(), 3);   // the title, a space, the text
	CHECK(l.textRuns[0].bold);

	// narrow: under the text
	l = NtfLayoutMessage(m, NtfStyle::Bar, 400, M, Measure, L"Show details");
	CHECK(!l.buttonsInline);
	CHECK(Same(l.text, 32, 8, 364, 26));
	CHECK(Same(l.buttons[0], 32, 34, 104, 58));
	CHECK(Same(l.buttons[1], 110, 34, 182, 58));
	CHECK_INT(l.height, 66);

	// no close button: the text gets its room
	m.closable = false;
	l = NtfLayoutMessage(m, NtfStyle::Bar, 800, M, Measure, L"");
	CHECK(Empty(l.close));
	CHECK_INT(l.text.right, 792 - 158);
}

TEST(Toast) {
	auto l = NtfLayoutMessage(Update(), NtfStyle::Toast, 360, M, Measure, L"");
	CHECK(Same(l.close, 328, 5, 352, 29));
	CHECK(!l.buttonsInline);
	CHECK_INT(l.textLayout.lines, 2);   // the title on its own line
	CHECK(Same(l.text, 32, 8, 324, 44));
	CHECK(Same(l.buttons[0], 202, 52, 274, 76));
	CHECK(Same(l.buttons[1], 280, 52, 352, 76));
	CHECK_INT(l.height, 84);
}

TEST(Extras) {
	NtfMessage m;
	m.closable = false;
	m.runs = Plain(L"Error");
	m.details = L"line1\nline2";
	// collapsed: only the toggle, a link at the text's end
	auto l = NtfLayoutMessage(m, NtfStyle::Bar, 800, M, Measure, L"Show details");
	CHECK_STR(l.textRuns.back().link, NtfDetailsLink);
	CHECK_STR(l.textRuns.back().text, L"Show details");
	CHECK(Empty(l.details));
	CHECK_INT(l.height, 34);
	m.expanded = true;
	l = NtfLayoutMessage(m, NtfStyle::Bar, 800, M, Measure, L"Hide details");
	CHECK(Same(l.details, 32, 30, 792, 66));
	CHECK_INT(l.detailsLayout.lines, 2);
	CHECK_INT(l.height, 74);

	NtfMessage p;
	p.closable = false;
	p.runs = Plain(L"Copying");
	p.progressKind = NtfProgress::Value;
	l = NtfLayoutMessage(p, NtfStyle::Bar, 800, M, Measure, L"");
	CHECK(Same(l.progress, 32, 34, 792, 38));
	CHECK_INT(l.height, 46);

	NtfMessage c;
	c.closable = false;
	c.runs = Plain(L"Tip");
	c.checkText = L"Don't show again";
	l = NtfLayoutMessage(c, NtfStyle::Bar, 800, M, Measure, L"");
	CHECK(Same(l.checkBox, 32, 32, 46, 46));
	CHECK(Same(l.check, 32, 30, 146, 48));
	CHECK_INT(l.height, 56);
}

TEST(Stack) {
	auto s = NtfLayoutStack({ 30, 40, 50 }, 2, 300, 22);
	CHECK(s.tops == std::vector<int>({ 0, 31 }));
	CHECK_INT(s.hidden, 1);
	CHECK(Same(s.more, 0, 72, 300, 94));
	CHECK_INT(s.height, 94);
	s = NtfLayoutStack({ 30, 40, 50 }, 0, 300, 22);
	CHECK(s.tops == std::vector<int>({ 0, 31, 72 }));
	CHECK_INT(s.hidden, 0);
	CHECK(Empty(s.more));
	CHECK_INT(s.height, 122);
	CHECK_INT(NtfLayoutStack({}, 3, 300, 22).height, 0);
}

TEST(PlaceToasts) {
	const RECT area{ 0, 0, 1000, 500 };
	auto r = NtfPlaceToasts(area, NtfCorner::BottomRight, { 100, 150, 200, 100 }, 300, 12, 8);
	CHECK(Same(r[0], 688, 388, 988, 488));
	CHECK(Same(r[1], 688, 230, 988, 380));
	CHECK(Same(r[2], 688, 22, 988, 222));
	CHECK(Empty(r[3]));   // waits

	r = NtfPlaceToasts(area, NtfCorner::TopLeft, { 100, 150, 200, 100 }, 300, 12, 8);
	CHECK(Same(r[0], 12, 12, 312, 112));
	CHECK(Same(r[1], 12, 120, 312, 270));
	CHECK(Same(r[2], 12, 278, 312, 478));
	CHECK(Empty(r[3]));

	r = NtfPlaceToasts(area, NtfCorner::BottomLeft, { 100, 150, 200 }, 300, 12, 8, 2);
	CHECK_INT(r[0].left, 12);
	CHECK(!Empty(r[1]));
	CHECK(Empty(r[2]));   // past maxShown

	// the newest is always placed; a narrow area narrows them
	r = NtfPlaceToasts(area, NtfCorner::TopRight, { 1000 }, 300, 12, 8);
	CHECK(Same(r[0], 688, 12, 988, 1012));
	r = NtfPlaceToasts({ 0, 0, 200, 500 }, NtfCorner::TopRight, { 50 }, 300, 12, 8);
	CHECK(Same(r[0], 12, 12, 188, 62));
}

TEST(Timer) {
	NtfTimer t;
	t.Start(1000, 5000);
	CHECK_INT(t.Remaining(3000), 3000);
	t.Pause(3000);
	CHECK(t.Paused());
	CHECK_INT(t.Remaining(10000), 3000);
	CHECK(!t.Expired(10000));
	t.Resume(10000);
	CHECK_INT(t.Remaining(11000), 2000);
	CHECK(t.Expired(13000));
	CHECK_INT(t.Remaining(20000), 0);
	t.Start(0, 0);
	CHECK_INT(t.Remaining(100000), UINT_MAX);
	CHECK(!t.Expired(100000));
}

TEST(Animate) {
	CHECK(NtfAnimate(1000, 200, 1000) == 0.0);
	CHECK(NtfAnimate(1000, 200, 900) == 0.0);
	CHECK(NtfAnimate(1000, 200, 1200) == 1.0);
	CHECK(NtfAnimate(1000, 200, 1100) == 0.875);
	CHECK(NtfAnimate(1000, 0, 1000) == 1.0);
}

TEST(Scaled) {
	auto m = M.Scaled(144);
	CHECK_INT(m.pad, 12);
	CHECK_INT(m.lineHeight, 27);
	CHECK_INT(m.progress, 6);
	CHECK_INT(m.maxDetailsLines, 12);
	CHECK_INT(M.Scaled(96).toastWidth, M.toastWidth);
}

int wmain() {
	wprintf(L"Notify model tests\n\n");

	Run_ParseLinks();
	Run_Wrap();
	Run_BarRow();
	Run_Toast();
	Run_Extras();
	Run_Stack();
	Run_PlaceToasts();
	Run_Timer();
	Run_Animate();
	Run_Scaled();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
