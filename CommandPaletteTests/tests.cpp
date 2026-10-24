// Unit tests for the CommandPalette model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: matching, ranking, the recently used list, menu text and
// choosing a provider. Anything that needs an HWND is checked by hand with CommandPaletteDemo.

#include "CommandPaletteModel.h"

#include <chrono>
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

static int Score(std::wstring_view pattern, std::wstring_view label) {
	int score;
	return CmpPattern(pattern).Match(label, score) ? score : INT_MIN;
}

static bool Matches(std::wstring_view pattern, std::wstring_view label) {
	return Score(pattern, label) != INT_MIN;
}

// the matched characters of the label, others as '.'
static std::wstring Marks(std::wstring_view pattern, std::wstring_view label) {
	int score;
	std::vector<uint16_t> positions;
	if (!CmpPattern(pattern).Match(label, score, &positions))
		return L"(no match)";
	std::wstring s(label.size(), L'.');
	for (auto p : positions)
		s[p] = label[p];
	return s;
}

// the labels ranked for a pattern, joined with '|'
static std::wstring Ranked(std::wstring_view pattern, const std::vector<std::wstring>& labels, const CmpRecent* recent = nullptr) {
	CmpPattern p(pattern);
	std::vector<CmpResult> results;
	std::vector<size_t> lengths;
	for (size_t i = 0; i < labels.size(); i++) {
		lengths.push_back(labels[i].size());
		CmpResult r;
		r.index = i;
		r.recent = recent ? recent->Rank((UINT)i + 1) : -1;   // ids are 1-based indexes
		if (p.Match(labels[i], r.score))
			results.push_back(r);
	}
	CmpSort(results, p.Empty() ? std::vector<size_t>() : lengths);
	std::wstring s;
	for (auto& r : results) {
		if (!s.empty())
			s += L'|';
		s += labels[r.index];
	}
	return s;
}

// ---- Matching -----------------------------------------------------------------------------

TEST(Match_Basics) {
	CHECK(Score(L"", L"anything") == 0);
	CHECK(Score(L"   ", L"anything") == 0);
	CHECK(CmpPattern(L"  ").Empty());
	CHECK(Matches(L"opn", L"Open"));
	CHECK(!Matches(L"opx", L"Open"));
	CHECK(!Matches(L"po", L"Open"));          // in order
	CHECK(Matches(L"OPEN", L"open"));         // case doesn't matter
	CHECK(!Matches(L"x", L""));
	CHECK_STR(Marks(L"of", L"Open File"), L"O....F...");
	CHECK_STR(Marks(L"file", L"File: Open"), L"File......");
	CHECK_STR(Marks(L"gtl", L"GoToLine"), L"G.T.L...");   // camel case words
	CHECK_STR(Marks(L"v2", L"Version2"), L"V......2");
	CHECK_STR(Marks(L"2", L"Version2"), L".......2");
}

TEST(Match_Scoring) {
	// word starts beat letters inside words
	CHECK(Score(L"of", L"Open File") > Score(L"of", L"Profile"));
	// a run beats the same letters apart
	CHECK(Score(L"save", L"Save All") > Score(L"save", L"Select All Value Editors"));
	// earlier beats later
	CHECK(Score(L"save", L"Save All") > Score(L"save", L"File: Save All"));
	// a capital typed scores on a capital
	CHECK(Score(L"OF", L"Open File") > Score(L"of", L"Open File"));
	// the best placement is found, not the first: "fi" at File, not the 'f' of "of"
	CHECK_STR(Marks(L"fi", L"Copy of File"), L"........Fi..");
	// a word that starts later but is whole beats scattered letters
	CHECK_STR(Marks(L"line", L"Toggle Line Numbers"), L".......Line........");
}

TEST(Match_Words) {
	CHECK(Matches(L"file open", L"Open File"));    // any order
	CHECK(Matches(L"open file", L"File: Open File"));
	CHECK(!Matches(L"open xyz", L"Open File"));
	CHECK_STR(Marks(L"op fi", L"Open File"), L"Op...Fi..");
	CHECK(Score(L"go line", L"Go to Line") > Score(L"go line", L"Go to Definition Online"));
}

TEST(Match_Long) {
	std::wstring label(600, L'a');
	label[550] = L'z';
	CHECK(!Matches(L"z", label));   // past CmpMaxLength
	label[500] = L'z';
	CHECK(Matches(L"z", label));
}

TEST(Match_Speed) {
	std::vector<std::wstring> labels;
	wchar_t buf[128];
	for (int i = 0; i < 50000; i++) {
		swprintf_s(buf, L"Category %d: Some Command Name Number %d With Words", i % 37, i);
		labels.push_back(buf);
	}
	CmpPattern p(L"cmd nm wrd");
	auto start = std::chrono::steady_clock::now();
	int count = 0, score;
	std::vector<uint16_t> positions;
	for (auto& label : labels)
		count += p.Match(label, score, &positions) ? 1 : 0;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	CHECK(count == 50000);
	wprintf(L"        50,000 labels: %lld ms\n", ms);
}

// ---- Ranking ---------------------------------------------------------------------------------

TEST(Rank) {
	std::vector<std::wstring> labels{ L"File: Open", L"File: Save", L"Edit: Copy", L"View: Open Recent", L"Window: Close" };
	CHECK_STR(Ranked(L"open", labels), L"File: Open|View: Open Recent");
	CHECK_STR(Ranked(L"", labels), L"File: Open|File: Save|Edit: Copy|View: Open Recent|Window: Close");
	CHECK_STR(Ranked(L"zzz", labels), L"");

	// the recently used first with an empty pattern, latest first; with a pattern, between equal scores
	CmpRecent recent;
	recent.Use(5);
	recent.Use(3);
	CHECK_STR(Ranked(L"", labels, &recent), L"Edit: Copy|Window: Close|File: Open|File: Save|View: Open Recent");
	std::vector<std::wstring> same{ L"Open A", L"Open B" };
	CmpRecent second;
	second.Use(2);
	CHECK_STR(Ranked(L"open", same, &second), L"Open B|Open A");
	CHECK_STR(Ranked(L"open", same), L"Open A|Open B");
}

// ---- Recently used --------------------------------------------------------------------------

TEST(Recent) {
	CmpRecent r(3);
	r.Use(1);
	r.Use(2);
	r.Use(3);
	CHECK(r.Rank(3) == 0 && r.Rank(1) == 2);
	r.Use(1);   // to the front
	CHECK(r.Rank(1) == 0 && r.Rank(3) == 1 && r.Count() == 3);
	r.Use(4);   // past the limit: the oldest goes
	CHECK(r.Rank(2) == -1 && r.Count() == 3);
	CHECK_STR(r.Save(), L"4;1;3");
	r.Remove(1);
	CHECK_STR(r.Save(), L"4;3");

	CmpRecent loaded(3);
	CHECK(loaded.Load(L"7;8;7;9;10"));   // duplicates once, the limit kept
	CHECK_STR(loaded.Save(), L"7;8;9");
	CHECK(loaded.Load(L""));
	CHECK(loaded.Count() == 0);
	CHECK(!loaded.Load(L"1;x;2"));
	CHECK(loaded.Count() == 0);
	CHECK(!loaded.Load(L"1;;2"));
	CHECK(!loaded.Load(L"99999999999"));
	CHECK(loaded.Load(L"5;"));   // a trailing separator is fine
	CHECK_STR(loaded.Save(), L"5");
	loaded.SetLimit(0);
	CHECK(loaded.Count() == 0);
}

// ---- Menus ------------------------------------------------------------------------------------

TEST(MenuText) {
	std::wstring name, shortcut;
	CmpParseMenuText(L"&Open...\tCtrl+O", name, shortcut);
	CHECK_STR(name, L"Open...");
	CHECK_STR(shortcut, L"Ctrl+O");
	CmpParseMenuText(L"Save && E&xit", name, shortcut);
	CHECK_STR(name, L"Save & Exit");
	CHECK(shortcut.empty());
	CmpParseMenuText(L"  Spaced  \t  F5 ", name, shortcut);
	CHECK_STR(name, L"Spaced");
	CHECK_STR(shortcut, L"F5");
	CmpParseMenuText(L"", name, shortcut);
	CHECK(name.empty() && shortcut.empty());
	CHECK_STR(CmpJoinPath({ L"File", L"Recent" }), L"File > Recent");
	CHECK_STR(CmpJoinPath({}), L"");
}

// ---- Providers ----------------------------------------------------------------------------------

TEST(Providers) {
	std::vector<std::wstring> prefixes{ L"", L"@", L":", L"@@" };
	std::wstring_view rest;
	CHECK(CmpPickProvider(prefixes, L"open", rest) == 0 && rest == L"open");
	CHECK(CmpPickProvider(prefixes, L"@ main", rest) == 1 && rest == L"main");
	CHECK(CmpPickProvider(prefixes, L"@@x", rest) == 3 && rest == L"x");   // the longest
	CHECK(CmpPickProvider(prefixes, L":120", rest) == 2 && rest == L"120");
	CHECK(CmpPickProvider(prefixes, L"", rest) == 0 && rest.empty());
	std::vector<std::wstring> noDefault{ L"@" };
	CHECK(CmpPickProvider(noDefault, L"abc", rest) == -1 && rest == L"abc");
	CHECK(CmpPickProvider({}, L"abc", rest) == -1);
}

// ---- Entry point -------------------------------------------------------------------------

int wmain() {
	wprintf(L"CommandPalette model tests\n\n");

	Run_Match_Basics();
	Run_Match_Scoring();
	Run_Match_Words();
	Run_Match_Long();
	Run_Match_Speed();

	Run_Rank();
	Run_Recent();
	Run_MenuText();
	Run_Providers();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
