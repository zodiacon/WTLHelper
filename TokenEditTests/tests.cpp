// Unit tests for the token edit model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the token list and its selection, splitting and joining text,
// the layout, the box's height and scrolling, hit testing, drop places, line navigation, suggestions and undo.
// Anything that needs an HWND is checked by hand with TokenEditDemo.

#include "TokenEditModel.h"

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

static void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: [%s], want: [%s]\n", g_currentTest, line, expr, actual.c_str(), expected.c_str());
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
#define CHECK_INT(a, b)  CheckInt((long long)(a), (long long)(b), #a " == " #b, __LINE__)
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

static bool Same(const RECT& rc, int left, int top, int right, int bottom) {
	return rc.left == left && rc.top == top && rc.right == right && rc.bottom == bottom;
}

static bool Empty(const RECT& rc) {
	return ::IsRectEmpty(&rc) != FALSE;
}

// 7 pixels a character
static int Measure(std::wstring_view text) {
	return (int)text.size() * 7;
}

static std::vector<TkeToken> Tokens(std::initializer_list<const wchar_t*> texts) {
	std::vector<TkeToken> tokens;
	tokens.reserve(texts.size());
	for (auto text : texts) {
		TkeToken t;
		t.text = text;
		tokens.push_back(std::move(t));
	}
	TkeMeasureTokens(tokens, Measure);
	return tokens;
}

static TkeTokenList List(std::initializer_list<const wchar_t*> texts) {
	TkeTokenList list;
	for (auto text : texts) {
		TkeToken t;
		t.text = text;
		list.Insert(list.Size(), std::move(t));
	}
	return list;
}

static std::wstring Texts(const TkeTokenList& list) {
	std::wstring s;
	for (auto& t : list.Tokens())
		s += t.text;
	return s;
}

static std::wstring Joined(const std::vector<std::wstring>& pieces) {
	std::wstring s;
	for (auto& p : pieces)
		s += L"[" + p + L"]";
	return s;
}

static std::wstring Selected(const TkeTokenList& list) {
	std::wstring s;
	for (auto i : list.Selection())
		s += std::to_wstring(i);
	return s;
}

// ---- Tests ----------------------------------------------------------------------

TEST(TokenList) {
	TkeTokenList list;
	TkeToken t;
	t.text = L"a";
	CHECK_INT(list.Insert(0, t), 0);
	CHECK_INT(list[0].id, 1);
	t.text = L"b";
	CHECK_INT(list.Insert(5, t), 1);       // clamped to the end
	CHECK_INT(list[1].id, 2);
	t.text = L"c";
	t.id = 2;                              // taken
	list.Insert(2, t);
	CHECK_INT(list[2].id, 3);
	t.text = L"d";
	t.id = 10;
	list.Insert(3, t);
	CHECK_INT(list[3].id, 10);
	t.id = 0;
	t.text = L"e";
	list.Insert(4, t);
	CHECK_INT(list[4].id, 11);

	CHECK_INT(list.Find(10), 3);
	CHECK_INT(list.Find(99), -1);
	CHECK_INT(list.FindText(L"C"), 2);
	CHECK_INT(list.FindText(L"C", false), -1);

	list.Remove(1);
	CHECK_STR(Texts(list), L"acde");
	list.Remove(9);                        // nothing
	CHECK_INT(list.Size(), 4);

	// the anchor follows inserts and removes before it
	list.Select(2, TkeSelect::Only);       // d
	CHECK_INT(list.Anchor(), 2);
	t.text = L"x";
	list.Insert(0, t);
	CHECK_INT(list.Anchor(), 3);
	list.Remove(0);
	CHECK_INT(list.Anchor(), 2);
	list.Remove(2);
	CHECK_INT(list.Anchor(), -1);
}

TEST(Selection) {
	auto list = List({ L"a", L"b", L"c", L"d", L"e" });
	list.Select(1, TkeSelect::Only);
	CHECK_STR(Selected(list), L"1");
	CHECK_INT(list.Anchor(), 1);
	list.Select(3, TkeSelect::Extend);
	CHECK_STR(Selected(list), L"123");
	CHECK_INT(list.Anchor(), 1);
	list.Select(0, TkeSelect::Extend);     // the other way, the rest dropped
	CHECK_STR(Selected(list), L"01");
	list.Select(3, TkeSelect::Toggle);
	CHECK_STR(Selected(list), L"013");
	CHECK_INT(list.Anchor(), 3);
	list.Select(0, TkeSelect::Toggle);
	CHECK_STR(Selected(list), L"13");
	list.Select(4, TkeSelect::ExtendAdd);  // from the anchor (0), added
	CHECK_STR(Selected(list), L"01234");
	CHECK_INT(list.SelectedCount(), 5);
	list.SelectAll(false);
	CHECK_INT(list.SelectedCount(), 0);
	list.SetAnchor(-1);
	list.Select(2, TkeSelect::Extend);     // no anchor: from itself
	CHECK_STR(Selected(list), L"2");
	list.Select(7, TkeSelect::Only);       // out of range: nothing
	CHECK_STR(Selected(list), L"2");

	// removing the selection keeps locked tokens
	list.SelectAll();
	list[2].locked = true;
	CHECK_INT(list.RemoveSelected(), 4);
	CHECK_STR(Texts(list), L"c");
}

TEST(Moving) {
	auto list = List({ L"a", L"b", L"c", L"d", L"e" });
	list.Select(1, TkeSelect::Only);
	list.Select(3, TkeSelect::Toggle);
	CHECK_INT(list.MoveSelected(0), 0);
	CHECK_STR(Texts(list), L"bdace");
	CHECK_INT(list.Anchor(), 1);           // d, followed
	CHECK_STR(Selected(list), L"01");

	list = List({ L"a", L"b", L"c", L"d", L"e" });
	CHECK_INT(list.Move({ 0 }, 5), 4);
	CHECK_STR(Texts(list), L"bcdea");
	list = List({ L"a", L"b", L"c", L"d", L"e" });
	CHECK_INT(list.Move({ 2, 1 }, 4), 2);  // before d
	CHECK_STR(Texts(list), L"adbce");
	list = List({ L"a", L"b", L"c", L"d", L"e" });
	CHECK_INT(list.Move({ 3 }, 1), 1);
	CHECK_STR(Texts(list), L"adbce");
	list = List({ L"a", L"b", L"c", L"d", L"e" });
	CHECK_INT(list.Move({ 1, 2 }, 2), 1);  // onto itself: nothing changes
	CHECK_STR(Texts(list), L"abcde");
	CHECK_INT(list.Move({}, 3), 3);
	CHECK_INT(list.Move({ 9 }, 3), 3);
	CHECK_STR(Texts(list), L"abcde");
}

TEST(Splitting) {
	CHECK_STR(Joined(TkeSplit(L"a, b;c", L",;")), L"[a][b][c]");
	CHECK_STR(Joined(TkeSplit(L"a,,  ,b", L",")), L"[a][b]");
	CHECK_STR(Joined(TkeSplit(L"  one two  ", L",")), L"[one two]");
	CHECK_STR(Joined(TkeSplit(L"\"Smith, John\" <j@x>, b", L",")), L"[\"Smith, John\" <j@x>][b]");
	CHECK_STR(Joined(TkeSplit(L"\"a, b\", c", L",")), L"[a, b][c]");
	CHECK_STR(Joined(TkeSplit(L"\"say \"\"hi\"\"\"", L",")), L"[say \"hi\"]");
	CHECK_STR(Joined(TkeSplit(L"\"a\" b", L",")), L"[\"a\" b]");
	CHECK_STR(Joined(TkeSplit(L"\"\", x", L",")), L"[x]");
	CHECK_STR(Joined(TkeSplit(L"\"abc, d", L",")), L"[\"abc, d]");       // never closed
	CHECK_STR(Joined(TkeSplit(L"a\r\nb\nc", L"")), L"[a][b][c]");        // line breaks always
	CHECK_STR(Joined(TkeSplit(L"\"a\nb\", c", L",")), L"[\"a][b\", c]");  // even in quotes (the stray quote opens again)

	std::wstring rest;
	CHECK_STR(Joined(TkeSplit(L"a, b, cha", L",", &rest)), L"[a][b]");
	CHECK_STR(rest, L" cha");
	CHECK_STR(Joined(TkeSplit(L"\"Smith, ", L",", &rest)), L"");
	CHECK_STR(rest, L"\"Smith, ");
	CHECK_STR(Joined(TkeSplit(L"a,", L",", &rest)), L"[a]");
	CHECK_STR(rest, L"");

	CHECK(!TkeHasSeparator(L"abc", L","));
	CHECK(TkeHasSeparator(L"ab,", L","));
	CHECK(!TkeHasSeparator(L"\"a,b", L","));
	CHECK(TkeHasSeparator(L"a\nb", L","));
	CHECK(TkeHasSeparator(L"\"a,b\";", L",;"));
}

TEST(Joining) {
	std::vector<std::wstring> texts = { L"a", L"b, c", L"say \"hi\"", L" x", L"semi;colon" };
	auto text = TkeJoin(texts, L",;", L"; ");
	CHECK_STR(text, L"a; \"b, c\"; \"say \"\"hi\"\"\"; \" x\"; \"semi;colon\"");
	CHECK(TkeSplit(text, L",;") == texts);
	CHECK_STR(TkeJoin({}, L",", L", "), L"");
	CHECK_STR(TkeJoin({ L"one" }, L",", L", "), L"one");
	// a separator the joiner doesn't use is still quoted
	CHECK_STR(TkeJoin({ L"a;b", L"c" }, L",;", L", "), L"\"a;b\", c");
}

TEST(TokenWidth) {
	TkeMetrics m;
	auto tokens = Tokens({ L"ab" });
	auto& t = tokens[0];
	CHECK_INT(TkeTokenWidth(t, true, true, m), 14 + 31);   // no image: text pad, the close button
	CHECK_INT(TkeTokenWidth(t, false, true, m), 14 + 16);
	t.image = 0;
	CHECK_INT(TkeTokenWidth(t, true, true, m), 14 + 47);
	CHECK_INT(TkeTokenWidth(t, true, false, m), 14 + 31);
	t.locked = true;
	CHECK_INT(TkeTokenWidth(t, true, true, m), 14 + 32);   // icon, text pad at the right

	TkeLayoutInput in;
	in.width = 200;
	t.locked = false;
	auto layout = TkeLayoutTokens(tokens, in, m);
	auto& box = layout.tokens[0];
	CHECK(Same(box.rc, 3, 3, 64, 25));
	CHECK(Same(box.icon, 7, 6, 23, 22));
	CHECK(Same(box.close, 45, 6, 61, 22));
	CHECK(Same(box.text, 27, 3, 41, 25));
	CHECK(!box.cut);

	t.locked = true;
	t.image = -1;
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Empty(layout.tokens[0].close));
	CHECK(Empty(layout.tokens[0].icon));
	CHECK(Same(layout.tokens[0].text, 11, 3, 25, 25));
}

TEST(Layout) {
	TkeMetrics m;
	auto tokens = Tokens({ L"alpha", L"beta", L"gamma" });    // 66, 59, 66 wide
	TkeLayoutInput in;
	in.width = 200;                        // the lines run [3, 197)
	auto layout = TkeLayoutTokens(tokens, in, m);
	CHECK_INT(layout.lines, 2);
	CHECK(Same(layout.tokens[0].rc, 3, 3, 69, 25));
	CHECK(Same(layout.tokens[1].rc, 73, 3, 132, 25));
	CHECK(Same(layout.tokens[2].rc, 3, 28, 69, 50));
	CHECK_INT(layout.tokens[2].line, 1);
	CHECK(Same(layout.edit, 73, 28, 197, 50));                // the rest of the line
	CHECK_INT(layout.editLine, 1);
	CHECK_INT(layout.editAt, 3);
	CHECK_INT(layout.height, 53);
	CHECK(Same(layout.tokens[0].close, 50, 6, 66, 22));
	CHECK(Same(layout.tokens[0].text, 11, 3, 46, 25));

	// among the tokens the edit is as wide as its text
	in.editAt = 1;
	in.editWidth = 20;
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.edit, 73, 3, 93, 25));
	CHECK(Same(layout.tokens[1].rc, 97, 3, 156, 25));
	CHECK(Same(layout.tokens[2].rc, 3, 28, 69, 50));
	CHECK_INT(layout.editAt, 1);
	in.editWidth = 0;                      // at least midEdit
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.edit, 73, 3, 81, 25));
	in.editAt = 9;                         // past the end: the end
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK_INT(layout.editAt, 3);
	in.editAt = -1;

	// the edit's least width doesn't fit after the last token: it starts the next line
	tokens = Tokens({ L"alpha", L"beta", L"gam" });          // gam: 52
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.tokens[2].rc, 136, 3, 188, 25));
	CHECK(Same(layout.edit, 3, 28, 197, 50));
	CHECK_INT(layout.lines, 2);
	in.editWidth = 120;                    // a long text typed
	tokens = Tokens({ L"alpha" });
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.edit, 73, 3, 197, 25));
	in.editWidth = 130;
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.edit, 3, 28, 197, 50));
	in.editWidth = 0;

	// no tokens: the edit fills the first line
	layout = TkeLayoutTokens({}, in, m);
	CHECK(Same(layout.edit, 3, 3, 197, 25));
	CHECK_INT(layout.lines, 1);
	CHECK_INT(layout.height, 28);
}

TEST(LongTokens) {
	TkeMetrics m;
	auto tokens = Tokens({ L"a", L"twenty characters!!!", L"b" });   // 140 + 31
	TkeLayoutInput in;
	in.width = 100;                        // 94 a line
	auto layout = TkeLayoutTokens(tokens, in, m);
	CHECK(!layout.tokens[0].cut);
	CHECK(layout.tokens[1].cut);
	CHECK(Same(layout.tokens[1].rc, 3, 28, 97, 50));
	CHECK(Same(layout.tokens[1].close, 78, 31, 94, 47));
	CHECK(Same(layout.tokens[1].text, 11, 28, 74, 50));
	CHECK_INT(layout.tokens[2].line, 2);
	CHECK_INT(layout.lines, 4);            // b, then the edit (60 doesn't fit after b)

	in.width = 30;                         // narrower than minToken: overflows to it
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK(Same(layout.tokens[1].rc, 3, 28, 43, 50));
	CHECK(Same(layout.edit, 3, 78, 27, 100));                 // the edit at most the line
}

TEST(Height) {
	TkeMetrics m;
	CHECK_INT(TkeBoxHeight(1, 1, 0, m), 28);
	CHECK_INT(TkeBoxHeight(0, 0, 0, m), 28);
	CHECK_INT(TkeBoxHeight(1, 2, 0, m), 53);
	CHECK_INT(TkeBoxHeight(5, 1, 3, m), 78);
	CHECK_INT(TkeBoxHeight(2, 1, 3, m), 53);
	CHECK_INT(TkeLineTop(0, m), 3);
	CHECK_INT(TkeLineTop(2, m), 53);

	CHECK_INT(TkeScrollToShow(0, 50, 75, 40, 100), 35);
	CHECK_INT(TkeScrollToShow(35, 10, 30, 40, 100), 10);
	CHECK_INT(TkeScrollToShow(35, 40, 60, 40, 100), 35);      // shown already
	CHECK_INT(TkeScrollToShow(0, 10, 80, 40, 100), 10);       // taller than the view: its top
	CHECK_INT(TkeScrollToShow(0, 90, 120, 40, 100), 60);      // clamped
	CHECK_INT(TkeClampScroll(90, 40, 100), 60);
	CHECK_INT(TkeClampScroll(5, 50, 30), 0);
	CHECK_INT(TkeClampScroll(-5, 50, 100), 0);
}

TEST(HitTesting) {
	TkeMetrics m;
	auto tokens = Tokens({ L"alpha", L"beta", L"gamma" });
	TkeLayoutInput in;
	in.width = 200;
	auto layout = TkeLayoutTokens(tokens, in, m);

	auto hit = TkeHitTest(layout, { 55, 10 });
	CHECK(hit.part == TkePart::Close && hit.index == 0);
	hit = TkeHitTest(layout, { 20, 10 });
	CHECK(hit.part == TkePart::Token && hit.index == 0);
	hit = TkeHitTest(layout, { 50, 4 });   // above the close button, on the token
	CHECK(hit.part == TkePart::Token && hit.index == 0);
	hit = TkeHitTest(layout, { 100, 40 });
	CHECK(hit.part == TkePart::Edit);
	hit = TkeHitTest(layout, { 190, 10 });
	CHECK(hit.part == TkePart::None);
	hit = TkeHitTest(layout, { 71, 10 });  // the gap
	CHECK(hit.part == TkePart::None);
	hit = TkeHitTest(layout, { 10, 40 });
	CHECK(hit.part == TkePart::Token && hit.index == 2);
}

TEST(Dropping) {
	TkeMetrics m;
	auto tokens = Tokens({ L"alpha", L"beta", L"gamma" });
	TkeLayoutInput in;
	in.width = 200;
	auto layout = TkeLayoutTokens(tokens, in, m);

	CHECK_INT(TkeDropIndex(layout, { 10, 10 }), 0);
	CHECK_INT(TkeDropIndex(layout, { 80, 10 }), 1);
	CHECK_INT(TkeDropIndex(layout, { 190, 10 }), 2);           // after the line's last
	CHECK_INT(TkeDropIndex(layout, { 10, 40 }), 2);
	CHECK_INT(TkeDropIndex(layout, { 50, 40 }), 3);
	CHECK_INT(TkeDropIndex(layout, { 10, 500 }), 2);           // below: the last line
	CHECK_INT(TkeDropIndex(layout, { 80, -20 }), 1);           // above: the first

	CHECK(Same(TkeDropMark(layout, 0, m), 0, 3, 2, 25));
	CHECK(Same(TkeDropMark(layout, 1, m), 70, 3, 72, 25));     // between alpha and beta
	CHECK(Same(TkeDropMark(layout, 2, m), 0, 28, 2, 50));      // gamma starts its line
	CHECK(Same(TkeDropMark(layout, 3, m), 70, 28, 72, 50));    // after the last

	// an edit-only line
	tokens = Tokens({ L"alpha", L"beta", L"gam" });
	layout = TkeLayoutTokens(tokens, in, m);
	CHECK_INT(TkeDropIndex(layout, { 50, 40 }), 3);
	layout = TkeLayoutTokens({}, in, m);
	CHECK_INT(TkeDropIndex(layout, { 50, 10 }), 0);
	CHECK(Same(TkeDropMark(layout, 0, m), 2, 3, 4, 25));
}

TEST(LineNavigation) {
	TkeMetrics m;
	auto tokens = Tokens({ L"alpha", L"beta", L"gamma" });
	TkeLayoutInput in;
	in.width = 200;
	auto layout = TkeLayoutTokens(tokens, in, m);

	CHECK_INT(TkeLineNeighbor(layout, 0, 1), 2);              // gamma under alpha
	CHECK_INT(TkeLineNeighbor(layout, 1, 1), 3);              // the edit nearer beta
	CHECK_INT(TkeLineNeighbor(layout, 2, -1), 0);
	CHECK_INT(TkeLineNeighbor(layout, 3, -1), 1);             // the edit up
	CHECK_INT(TkeLineNeighbor(layout, 0, -1), -1);
	CHECK_INT(TkeLineNeighbor(layout, 2, 1), -1);
	CHECK_INT(TkeLineNeighbor(layout, 7, 1), -1);
}

TEST(Matching) {
	auto m = TkeMatchText(L"jo", L"John Smith");
	CHECK(m.kind == TkeMatchKind::Prefix && m.start == 0 && m.length == 2);
	m = TkeMatchText(L"JO", L"john");
	CHECK(m.kind == TkeMatchKind::Prefix);
	m = TkeMatchText(L" jo ", L"John");
	CHECK(m.kind == TkeMatchKind::Prefix && m.length == 2);
	m = TkeMatchText(L"sm", L"John Smith");
	CHECK(m.kind == TkeMatchKind::Word && m.start == 5);
	m = TkeMatchText(L"j", L"Smith, John <j@x>");
	CHECK(m.kind == TkeMatchKind::Word && m.start == 7);
	m = TkeMatchText(L"x", L"j@x");
	CHECK(m.kind == TkeMatchKind::Word && m.start == 2);
	m = TkeMatchText(L"Edit", L"TokenEdit");
	CHECK(m.kind == TkeMatchKind::Word && m.start == 5);
	m = TkeMatchText(L"<j", L"Smith <j@x>");
	CHECK(m.kind == TkeMatchKind::Word && m.start == 6);
	m = TkeMatchText(L"ith", L"John Smith");
	CHECK(m.kind == TkeMatchKind::None);
	m = TkeMatchText(L"ith", L"John Smith", true);
	CHECK(m.kind == TkeMatchKind::Inside && m.start == 7 && m.length == 3);
	CHECK(TkeMatchText(L"", L"abc").kind == TkeMatchKind::None);
	CHECK(TkeMatchText(L"abcd", L"abc").kind == TkeMatchKind::None);
	CHECK(TkeMatchText(L"abc", L"abc").kind == TkeMatchKind::Prefix);
}

TEST(Suggestions) {
	std::vector<std::wstring> candidates = { L"Smith, John", L"John Smith", L"Johanna", L"Bob Johnson", L"Alice", L"Ajohx" };
	auto s = TkeFilterSuggestions(L"joh", candidates, { L"johanna" });
	CHECK_INT(s.size(), 3);
	CHECK_INT(s[0].index, 1);              // the prefix first
	CHECK_INT(s[1].index, 0);
	CHECK_INT(s[1].match.start, 7);
	CHECK_INT(s[2].index, 3);
	s = TkeFilterSuggestions(L"joh", candidates, { L"johanna" }, true);
	CHECK_INT(s.size(), 4);
	CHECK_INT(s[3].index, 5);
	CHECK(s[3].match.kind == TkeMatchKind::Inside);
	s = TkeFilterSuggestions(L"joh", candidates, {}, false, 2);
	CHECK_INT(s.size(), 2);
	CHECK_INT(s[0].index, 1);
	CHECK_INT(s[1].index, 2);
	CHECK(TkeFilterSuggestions(L"", candidates, {}).empty());
	CHECK(TkeFilterSuggestions(L"zz", candidates, {}).empty());
}

TEST(Undo) {
	auto list = List({ L"a" });
	TkeHistory history;
	CHECK(!history.CanUndo());
	CHECK(!history.Undo(list));

	auto before = list.Save();
	TkeToken t;
	t.text = L"b";
	list.Insert(1, t);
	history.Push(before, list.Save());
	before = list.Save();
	list.Select(0, TkeSelect::Only);
	list.RemoveSelected();
	history.Push(before, list.Save());
	CHECK_STR(Texts(list), L"b");

	CHECK(history.Undo(list));
	CHECK_STR(Texts(list), L"ab");
	CHECK(history.Undo(list));
	CHECK_STR(Texts(list), L"a");
	CHECK(!history.CanUndo());
	CHECK(history.Redo(list));
	CHECK_STR(Texts(list), L"ab");
	CHECK(history.CanRedo());

	// a new change drops what could be redone; ids aren't reused
	before = list.Save();
	t.text = L"c";
	list.Insert(2, t);
	history.Push(before, list.Save());
	CHECK(!history.CanRedo());
	CHECK_INT(list[2].id, 3);
	history.Undo(list);
	t.text = L"d";
	list.Insert(2, t);
	CHECK_INT(list[2].id, 3);              // the undone one's id is free again

	// the limit drops the oldest
	TkeHistory limited(2);
	list = List({});
	for (auto text : { L"x", L"y", L"z" }) {
		before = list.Save();
		t.text = text;
		t.id = 0;
		list.Insert(list.Size(), t);
		limited.Push(before, list.Save());
	}
	CHECK(limited.Undo(list));
	CHECK(limited.Undo(list));
	CHECK(!limited.Undo(list));
	CHECK_STR(Texts(list), L"x");
}

TEST(Drawing) {
	auto wave = TkeWave(0, 10, 5, 2, 4);
	CHECK_INT(wave.size(), 4);
	CHECK(wave[0].x == 0 && wave[0].y == 3);
	CHECK(wave[1].x == 4 && wave[1].y == 7);
	CHECK(wave[2].x == 8 && wave[2].y == 3);
	CHECK(wave[3].x == 10 && wave[3].y == 7);
	CHECK(TkeWave(5, 5, 0, 1, 2).empty());
}

TEST(Scaled) {
	auto m = TkeMetrics().Scaled(144);
	CHECK_INT(m.tokenHeight, 33);
	CHECK_INT(m.gap, 6);
	CHECK_INT(m.minEdit, 90);
	CHECK_INT(m.close, 24);
	m = TkeMetrics().Scaled(96);
	CHECK_INT(m.padding, 3);
	CHECK_INT(m.textPad, 8);
}

int wmain() {
	wprintf(L"TokenEdit model tests\n\n");

	Run_TokenList();
	Run_Selection();
	Run_Moving();
	Run_Splitting();
	Run_Joining();
	Run_TokenWidth();
	Run_Layout();
	Run_LongTokens();
	Run_Height();
	Run_HitTesting();
	Run_Dropping();
	Run_LineNavigation();
	Run_Matching();
	Run_Suggestions();
	Run_Undo();
	Run_Drawing();
	Run_Scaled();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
