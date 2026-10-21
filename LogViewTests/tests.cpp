// Unit tests for the LogView model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: line storage, chunks, the line limit, the queue of
// lines from other threads and the text helpers. Anything that needs an HWND is checked by hand with
// LogViewDemo.

#include "LogViewModel.h"

#include <cstdio>
#include <string>

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

static void Add(LgStore& s, const std::wstring& text, BYTE level = 0, BYTE style = 0, LPARAM lParam = 0) {
	s.Append(text.c_str(), text.size(), level, style, lParam);
}

static std::wstring Text(const LgStore& s, size_t line) {
	return std::wstring(s.Text(line));
}

// ---- Tests -------------------------------------------------------------------------

TEST(Store_AppendKeepsTextAndAttributes) {
	LgStore s;
	CHECK(s.Count() == 0);
	Add(s, L"first", 3, 7, 42);
	Add(s, L"");
	Add(s, L"third line");
	CHECK(s.Count() == 3);
	CHECK_STR(Text(s, 0), L"first");
	CHECK_STR(Text(s, 1), L"");
	CHECK_STR(Text(s, 2), L"third line");
	CHECK(s.Line(0).level == 3);
	CHECK(s.Line(0).style == 7);
	CHECK(s.Line(0).lParam == 42);
	CHECK(s.Line(2).level == 0);
	CHECK(s.Number(0) == 1);
	CHECK(s.Number(2) == 3);
	CHECK(s.ChunkCount() == 1);
}

TEST(Store_EmbeddedNulsAreText) {
	LgStore s;
	const wchar_t text[] = { L'a', 0, L'b' };
	s.Append(text, 3, 0, 0, 0);
	CHECK(s.Text(0).size() == 3);
	CHECK(s.Text(0)[2] == L'b');
}

TEST(Store_LinesDontSpanChunks) {
	LgStore s(16);
	Add(s, L"0123456789");      // 10 of 16
	Add(s, L"abcdefgh");        // doesn't fit: a new chunk
	Add(s, L"xyz");             // fits after it
	CHECK(s.ChunkCount() == 2);
	CHECK_STR(Text(s, 0), L"0123456789");
	CHECK_STR(Text(s, 1), L"abcdefgh");
	CHECK_STR(Text(s, 2), L"xyz");
	CHECK(s.Line(1).chunk == s.Line(2).chunk);
}

TEST(Store_LongLineGetsAChunkOfItsOwn) {
	LgStore s(16);
	Add(s, L"short");
	std::wstring longLine(100, L'L');
	Add(s, longLine);
	Add(s, L"after");
	CHECK(s.ChunkCount() == 3);
	CHECK_STR(Text(s, 1), longLine);
	CHECK_STR(Text(s, 2), L"after");
	CHECK_STR(Text(s, 0), L"short");
}

TEST(Store_ManyLines) {
	LgStore s(1024);
	for (int i = 0; i < 100000; i++)
		Add(s, std::to_wstring(i));
	CHECK(s.Count() == 100000);
	CHECK_STR(Text(s, 0), L"0");
	CHECK_STR(Text(s, 54321), L"54321");
	CHECK_STR(Text(s, 99999), L"99999");
	CHECK(s.ChunkCount() > 100);
}

TEST(Store_LimitDropsTheOldest) {
	LgStore s;
	s.SetLimit(3);
	for (int i = 1; i <= 5; i++)
		Add(s, L"line " + std::to_wstring(i));
	CHECK(s.Count() == 3);
	CHECK_STR(Text(s, 0), L"line 3");
	CHECK_STR(Text(s, 2), L"line 5");
	// numbers keep counting
	CHECK(s.Number(0) == 3);
	CHECK(s.Number(2) == 5);
	CHECK(s.TakeDropped() == 2);
	CHECK(s.TakeDropped() == 0);
}

TEST(Store_LimitFreesChunks) {
	LgStore s(16);
	s.SetLimit(4);
	for (int i = 0; i < 1000; i++)
		Add(s, L"0123456789");   // one line per chunk
	CHECK(s.Count() == 4);
	CHECK(s.ChunkCount() <= 5);
	CHECK_STR(Text(s, 3), L"0123456789");
	CHECK(s.Number(3) == 1000);
}

TEST(Store_LimitSetLaterTrimsNow) {
	LgStore s;
	for (int i = 0; i < 10; i++)
		Add(s, std::to_wstring(i));
	s.SetLimit(4);
	CHECK(s.Count() == 4);
	CHECK_STR(Text(s, 0), L"6");
	CHECK(s.TakeDropped() == 6);
	s.SetLimit(0);
	for (int i = 0; i < 10; i++)
		Add(s, L"more");
	CHECK(s.Count() == 14);
}

TEST(Store_ClearStartsNumberingAgain) {
	LgStore s(16);
	s.SetLimit(2);
	for (int i = 0; i < 5; i++)
		Add(s, L"x");
	s.Clear();
	CHECK(s.Count() == 0);
	CHECK(s.ChunkCount() == 0);
	CHECK(s.MaxColumns() == 0);
	CHECK(s.TakeDropped() == 0);
	Add(s, L"again");
	CHECK(s.Number(0) == 1);
	CHECK_STR(Text(s, 0), L"again");
	CHECK(s.Limit() == 2);
}

TEST(Store_MaxColumns) {
	LgStore s;
	Add(s, L"abc");
	CHECK(s.MaxColumns() == 3);
	Add(s, L"\tx");             // tab to 4, then x
	CHECK(s.MaxColumns() == 5);
	Add(s, L"a");
	CHECK(s.MaxColumns() == 5);
	s.SetTabSize(8);
	CHECK(s.MaxColumns() == 9);
	s.SetTabSize(2);
	CHECK(s.MaxColumns() == 3);
}

TEST(Columns_And_ExpandTabs) {
	CHECK(LgColumns(L"", 4) == 0);
	CHECK(LgColumns(L"abc", 4) == 3);
	CHECK(LgColumns(L"\t", 4) == 4);
	CHECK(LgColumns(L"ab\tc", 4) == 5);
	CHECK(LgColumns(L"abcd\t", 4) == 8);
	CHECK(LgColumns(L"a\t\tb", 3) == 7);

	std::wstring out;
	LgExpandTabs(L"ab\tc", 4, out);
	CHECK_STR(out, L"ab  c");
	LgExpandTabs(L"\t", 2, out);
	CHECK_STR(out, L"  ");
	LgExpandTabs(L"no tabs", 4, out);
	CHECK_STR(out, L"no tabs");
	LgExpandTabs(L"abcd\tx", 4, out);
	CHECK_STR(out, L"abcd    x");
	CHECK(out.size() == LgColumns(L"abcd\tx", 4));
}

TEST(SplitLines) {
	auto split = [](std::wstring_view text) {
		std::wstring out;
		LgSplitLines(text, [&](size_t start, size_t length) {
			out += L'[';
			out += text.substr(start, length);
			out += L']';
		});
		return out;
	};
	CHECK_STR(split(L""), L"");
	CHECK_STR(split(L"one"), L"[one]");
	CHECK_STR(split(L"one\n"), L"[one]");
	CHECK_STR(split(L"one\ntwo"), L"[one][two]");
	CHECK_STR(split(L"a\r\nb\rc\nd"), L"[a][b][c][d]");
	CHECK_STR(split(L"\n"), L"[]");
	CHECK_STR(split(L"\n\n"), L"[][]");
	CHECK_STR(split(L"a\r\n\r\nb"), L"[a][][b]");
	CHECK_STR(split(L"a\n\rb"), L"[a][][b]");   // "\n\r" is two breaks
	CHECK_STR(split(L"end\r"), L"[end]");
}

TEST(Batch_AddAndAppend) {
	LgBatch b;
	CHECK(b.Empty());
	b.Add(L"one", 3, 1, 2, 3);
	b.AddText(L"two\r\nthree\n", 11, 4, 5);
	CHECK(b.Count() == 3);
	CHECK(b.Text(b.Lines()[1]) == L"two");
	CHECK(b.Lines()[2].level == 4);
	CHECK(b.Lines()[2].lParam == 0);

	LgStore s;
	Add(s, L"zero");
	s.Append(b);
	CHECK(s.Count() == 4);
	CHECK_STR(Text(s, 1), L"one");
	CHECK(s.Line(1).lParam == 3);
	CHECK_STR(Text(s, 3), L"three");
	CHECK(s.Line(3).style == 5);

	b.Clear();
	CHECK(b.Empty());
	CHECK(b.Count() == 0);
}

int wmain() {
	Run_Store_AppendKeepsTextAndAttributes();
	Run_Store_EmbeddedNulsAreText();
	Run_Store_LinesDontSpanChunks();
	Run_Store_LongLineGetsAChunkOfItsOwn();
	Run_Store_ManyLines();
	Run_Store_LimitDropsTheOldest();
	Run_Store_LimitFreesChunks();
	Run_Store_LimitSetLaterTrimsNow();
	Run_Store_ClearStartsNumberingAgain();
	Run_Store_MaxColumns();
	Run_Columns_And_ExpandTabs();
	Run_SplitLines();
	Run_Batch_AddAndAppend();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
