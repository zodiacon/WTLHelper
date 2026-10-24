// Unit tests for the DiffView model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the lines, the diff, the changes within lines, the rows of
// both layouts with folds, merging and the unified text. Anything that needs an HWND is checked by hand with
// DiffViewDemo.

#include "DiffViewModel.h"

#include <algorithm>
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

// The lines as one string per character of the script: '=' equal, '-' deleted, '+' inserted, in block order.
static std::wstring Script(const DfDiff& diff) {
	std::wstring s;
	for (auto& b : diff.blocks) {
		if (!b.changed)
			s.append(b.leftCount, L'=');
		else
			s.append(b.leftCount, L'-').append(b.rightCount, L'+');
	}
	return s;
}

// The blocks cover both texts in order, the equal ones really equal.
static bool Consistent(const DfDiff& diff, const DfLines& left, const DfLines& right, const DfOptions& options = {}) {
	size_t l = 0, r = 0;
	for (auto& b : diff.blocks) {
		if (b.left != l || b.right != r)
			return false;
		if (!b.changed) {
			if (b.leftCount != b.rightCount || b.leftCount == 0)
				return false;
			for (size_t i = 0; i < b.leftCount; i++)
				if (DfNormalize(left[b.left + i], options) != DfNormalize(right[b.right + i], options))
					return false;
		}
		else if (b.leftCount + b.rightCount == 0)
			return false;
		l += b.leftCount;
		r += b.rightCount;
	}
	return l == left.size() && r == right.size();
}

static size_t Common(const DfDiff& diff) {
	size_t n = 0;
	for (auto& b : diff.blocks)
		if (!b.changed)
			n += b.leftCount;
	return n;
}

// The longest common subsequence's length, by dynamic programming, to check the diff is minimal.
static size_t Lcs(const DfLines& a, const DfLines& b) {
	std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
	for (size_t i = 1; i <= a.size(); i++) {
		for (size_t j = 1; j <= b.size(); j++)
			cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : std::max(prev[j], cur[j - 1]);
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

static std::wstring Spans(std::wstring_view line, const std::vector<DfSpan>& spans) {
	std::wstring s;
	for (auto& sp : spans) {
		if (!s.empty())
			s += L'|';
		s += line.substr(sp.start, sp.length);
	}
	return s;
}

// The rows as text: per row "l:r" (line numbers or '.'), F<n> for a fold, * for a pair.
static std::wstring Rows(const std::vector<DfRow>& rows) {
	std::wstring s;
	for (auto& row : rows) {
		if (!s.empty())
			s += L' ';
		if (row.kind == DfRowKind::Fold) {
			s += L"F" + std::to_wstring(row.hidden);
			continue;
		}
		s += row.left < 0 ? L"." : std::to_wstring(row.left);
		s += row.kind == DfRowKind::Changed ? (row.paired ? L"*" : L"~") : L":";
		s += row.right < 0 ? L"." : std::to_wstring(row.right);
	}
	return s;
}

// a text of the letters as lines: "abc" -> "a\nb\nc\n"
static DfLines Letters(std::wstring_view letters) {
	std::wstring text;
	for (auto c : letters)
		text.append(1, c).append(L"\n");
	return DfLines(text);
}

static unsigned g_seed = 12345;
static unsigned Random() {
	g_seed = g_seed * 1103515245 + 12345;
	return (g_seed >> 16) & 0x7fff;
}

// ---- Lines ------------------------------------------------------------------------

TEST(Lines_Split) {
	DfLines empty(L"");
	CHECK(empty.size() == 0);
	CHECK(empty.EndsWithNewline());

	DfLines lines(L"one\r\ntwo\nthree\rfour");
	CHECK(lines.size() == 4);
	CHECK(lines[0] == L"one");
	CHECK(lines[1] == L"two");
	CHECK(lines[2] == L"three");
	CHECK(lines[3] == L"four");
	CHECK(!lines.EndsWithNewline());
	CHECK_STR(lines.Join(L"\n"), L"one\ntwo\nthree\nfour");

	DfLines blank(L"\n\nx\n");
	CHECK(blank.size() == 3);
	CHECK(blank[0].empty() && blank[1].empty() && blank[2] == L"x");
	CHECK(blank.EndsWithNewline());
	CHECK_STR(blank.Join(L"\r\n"), L"\r\n\r\nx\r\n");

	DfLines crlfEnd(L"a\r\n");
	CHECK(crlfEnd.size() == 1 && crlfEnd.EndsWithNewline());
}

TEST(Lines_Replace) {
	DfLines a(L"1\n2\n3\n4\n"), b(L"x\ny\nz");
	auto c = a;
	c.Replace(1, 2, b, 0, 2);
	CHECK_STR(c.Join(L"\n"), L"1\nx\ny\n4\n");

	// taking the other text's last line takes its missing line break
	c = a;
	c.Replace(3, 1, b, 2, 1);
	CHECK_STR(c.Join(L"\n"), L"1\n2\n3\nz");

	// but not in the middle
	c = a;
	c.Replace(0, 1, b, 2, 1);
	CHECK_STR(c.Join(L"\n"), L"z\n2\n3\n4\n");

	// removing the last line that had none leaves one that has
	c = b;
	c.Replace(2, 1, a, 0, 0);
	CHECK_STR(c.Join(L"\n"), L"x\ny\n");

	// inserting, deleting everything
	c = a;
	c.Replace(2, 0, b, 0, 1);
	CHECK_STR(c.Join(L"\n"), L"1\n2\nx\n3\n4\n");
	c.Replace(0, 5, b, 0, 0);
	CHECK(c.empty() && c.EndsWithNewline());
}

// ---- Diff ------------------------------------------------------------------------------

TEST(Diff_Basic) {
	auto same = DfCompute(Letters(L"abc"), Letters(L"abc"));
	CHECK(same.Identical());
	CHECK_STR(Script(same), L"===");

	auto empty = DfCompute(DfLines(), DfLines());
	CHECK(empty.blocks.empty() && empty.Identical());

	auto added = DfCompute(DfLines(), Letters(L"ab"));
	CHECK_STR(Script(added), L"++");
	CHECK(added.changes == 1 && added.inserted == 2 && added.deleted == 0);

	auto removed = DfCompute(Letters(L"ab"), DfLines());
	CHECK_STR(Script(removed), L"--");

	auto mid = DfCompute(Letters(L"abcd"), Letters(L"abxd"));
	CHECK_STR(Script(mid), L"==-+=");
	CHECK(mid.changes == 1 && mid.deleted == 1 && mid.inserted == 1);
	CHECK(mid.blocks.size() == 3);
	CHECK(mid.blocks[1].left == 2 && mid.blocks[1].right == 2);

	// the classic example from Myers' paper: ABCABBA -> CBABAC, 5 edits
	DfLines a = Letters(L"ABCABBA"), b = Letters(L"CBABAC");
	auto paper = DfCompute(a, b);
	CHECK(Consistent(paper, a, b));
	CHECK(Common(paper) == 4);
	CHECK(paper.deleted + paper.inserted == 5);
}

TEST(Diff_Minimal_Random) {
	// small random texts over a few letters: with no line once on each side (no anchors, which needn't be
	// minimal), the common lines are the LCS
	bool ok = true, consistent = true;
	int tested = 0;
	for (int round = 0; round < 500; round++) {
		std::wstring sa, sb;
		size_t la = Random() % 30, lb = Random() % 30;
		for (size_t i = 0; i < la; i++)
			sa += wchar_t(L'a' + Random() % 4);
		for (size_t i = 0; i < lb; i++)
			sb += wchar_t(L'a' + Random() % 4);
		auto a = Letters(sa), b = Letters(sb);
		auto diff = DfCompute(a, b);
		consistent = consistent && Consistent(diff, a, b);
		bool anchor = false;
		for (wchar_t c = L'a'; c <= L'd'; c++)
			anchor = anchor || (std::count(sa.begin(), sa.end(), c) == 1 && std::count(sb.begin(), sb.end(), c) == 1);
		if (!anchor) {
			tested++;
			ok = ok && Common(diff) == Lcs(a, b);
		}
	}
	CHECK(consistent);
	CHECK(ok);
	CHECK(tested > 300);
}

TEST(Diff_Edits_Random) {
	// a long text with unique lines (anchors) edited here and there: consistent, and no more changes than made
	bool ok = true, consistent = true;
	for (int round = 0; round < 20; round++) {
		std::wstring text;
		for (int i = 0; i < 2000; i++)
			text += L"line " + std::to_wstring(i % 700) + L"\n";   // repeats too
		DfLines a(text);
		auto b = a;
		size_t edits = 0;
		for (int e = 0; e < 30; e++) {
			size_t at = Random() % (b.size() + 1);
			switch (Random() % 3) {
				case 0:
					b.Replace(at, 0, DfLines(L"new " + std::to_wstring(Random()) + L"\n"), 0, 1);
					edits++;
					break;
				case 1:
					if (at < b.size()) {
						b.Replace(at, 1, DfLines(), 0, 0);
						edits++;
					}
					break;
				default:
					if (at < b.size()) {
						b.Replace(at, 1, DfLines(L"changed " + std::to_wstring(Random()) + L"\n"), 0, 1);
						edits += 2;
					}
					break;
			}
		}
		auto diff = DfCompute(a, b);
		consistent = consistent && Consistent(diff, a, b);
		ok = ok && diff.deleted + diff.inserted <= edits;
	}
	CHECK(consistent);
	CHECK(ok);
}

TEST(Diff_Anchors) {
	// a block moved: the unique lines keep the larger part in place
	DfLines a(L"a\nb\nc\nd\ne\nf\ng\n"), b(L"e\nf\na\nb\nc\nd\ng\n");
	auto diff = DfCompute(a, b);
	CHECK(Consistent(diff, a, b));
	CHECK_STR(Script(diff), L"++====--=");
}

TEST(Diff_CostLimit) {
	// two unrelated texts with the cost limit very low: still a valid script
	std::wstring ta, tb;
	for (int i = 0; i < 3000; i++) {
		ta += std::to_wstring(Random() % 50) + L"\n";
		tb += std::to_wstring(Random() % 50) + L"\n";
	}
	DfLines a(ta), b(tb);
	DfOptions options;
	options.costLimit = 8;
	auto limited = DfCompute(a, b, options);
	CHECK(Consistent(limited, a, b));
	auto full = DfCompute(a, b);
	CHECK(Consistent(full, a, b));
	CHECK(Common(limited) <= Common(full));
	CHECK(Common(limited) > 0);
}

TEST(Diff_Large) {
	// 200k lines, a few hundred changes: fast enough
	std::wstring text;
	for (int i = 0; i < 200000; i++)
		text += L"line number " + std::to_wstring(i) + L" of the text\n";
	DfLines a(text);
	auto b = a;
	for (int e = 0; e < 300; e++) {
		size_t at = Random() * 7 % b.size();
		b.Replace(at, 1, DfLines(L"edited\n"), 0, 1);
	}
	auto start = std::chrono::steady_clock::now();
	auto diff = DfCompute(a, b);
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	CHECK(Consistent(diff, a, b));
	CHECK(diff.deleted <= 300 && diff.inserted == diff.deleted);
	wprintf(L"        200k lines, %zu changes: %lld ms\n", diff.changes, ms);

	// two unrelated texts, 20k lines each
	std::wstring ta, tb;
	for (int i = 0; i < 20000; i++) {
		ta += std::to_wstring(Random() % 1000) + L"\n";
		tb += std::to_wstring(Random() % 1000) + L"\n";
	}
	DfLines ua(ta), ub(tb);
	start = std::chrono::steady_clock::now();
	auto unrelated = DfCompute(ua, ub);
	ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	CHECK(Consistent(unrelated, ua, ub));
	wprintf(L"        20k unrelated lines: %lld ms\n", ms);
}

TEST(Diff_Whitespace_Case) {
	DfLines a(L"int x = 1;\n  if (a)  b();\nEND\n"), b(L"int x  =  1;\n\tif (a) b();   \nend\n");
	CHECK_STR(Script(DfCompute(a, b)), L"---+++");

	DfOptions change;
	change.whitespace = DfWhitespace::Change;
	CHECK_STR(Script(DfCompute(a, b, change)), L"==-+");

	DfOptions all;
	all.whitespace = DfWhitespace::All;
	all.ignoreCase = true;
	auto diff = DfCompute(a, b, all);
	CHECK(diff.Identical());
	CHECK(Consistent(diff, a, b, all));

	// Change keeps the presence of a space inside the line
	CHECK(DfNormalize(L"  a+b ", change) == L"a+b");
	CHECK(DfNormalize(L"a + \t b", change) == L"a + b");
	CHECK(DfNormalize(L" a + b ", all) == L"a+b");
	DfOptions caseOnly;
	caseOnly.ignoreCase = true;
	CHECK(DfNormalize(L"MiXeD", caseOnly) == L"mixed");
}

TEST(Diff_Slide) {
	// a function added between two others: the inserted block ends on the blank line after it
	DfLines a(L"{\n  1\n}\n\n{\n  3\n}\n");
	DfLines b(L"{\n  1\n}\n\n{\n  2\n}\n\n{\n  3\n}\n");
	auto diff = DfCompute(a, b);
	CHECK(Consistent(diff, a, b));
	CHECK(diff.blocks.size() == 3);
	CHECK(diff.blocks[1].changed && diff.blocks[1].right == 4 && diff.blocks[1].rightCount == 4);

	// with no blank line, as far down as it goes
	auto down = DfCompute(Letters(L"xay"), Letters(L"xaay"));
	CHECK(down.blocks.size() == 3);
	CHECK(down.blocks[1].right == 2);
}

TEST(Diff_BlockOfLine) {
	auto diff = DfCompute(Letters(L"abcd"), Letters(L"abxyd"));   // ==, -+ +, =
	CHECK(DfBlockOfLine(diff, false, 0) == 0);
	CHECK(DfBlockOfLine(diff, false, 2) == 1);
	CHECK(DfBlockOfLine(diff, false, 3) == 2);
	CHECK(DfBlockOfLine(diff, true, 3) == 1);
	CHECK(DfBlockOfLine(diff, true, 4) == 2);
	CHECK(DfBlockOfLine(diff, true, 5) == 3);
}

// ---- Within lines ---------------------------------------------------------------------

TEST(Tokens) {
	auto t = DfTokenize(L"foo(bar_1,  x)");
	CHECK(t.size() == 7);
	CHECK(t[0].start == 0 && t[0].length == 3 && !t[0].space);
	CHECK(t[1].length == 1);   // (
	CHECK(t[2].start == 4 && t[2].length == 5);   // bar_1
	CHECK(t[4].space && t[4].length == 2);
	CHECK(DfTokenize(L"").empty());
}

TEST(Similarity) {
	CHECK(DfSimilarity(L"", L"") == 1.0);
	CHECK(DfSimilarity(L"abc", L"") == 0.0);
	CHECK(DfSimilarity(L"int x = 1;", L"int x = 1;") == 1.0);
	CHECK(DfSimilarity(L"int x = 1;", L"int x = 2;") > 0.7);
	CHECK(DfSimilarity(L"int x = 1;", L"return nothing") < 0.2);
	DfOptions caseOnly;
	caseOnly.ignoreCase = true;
	CHECK(DfSimilarity(L"ABC def", L"abc DEF", caseOnly) == 1.0);
}

TEST(IntraLine_Words) {
	std::vector<DfSpan> sa, sb;
	std::wstring a = L"int count = total + 1;", b = L"int count = total - offset;";
	DfIntraLine(a, b, {}, sa, sb);
	CHECK_STR(Spans(a, sa), L"+ 1");
	CHECK_STR(Spans(b, sb), L"- offset");

	// a word that mostly stayed: its characters
	a = L"return counter;", b = L"return counters;";
	DfIntraLine(a, b, {}, sa, sb);
	CHECK(sa.empty());
	CHECK_STR(Spans(b, sb), L"s");

	// a word replaced by another
	a = L"call alpha();", b = L"call omega();";
	DfIntraLine(a, b, {}, sa, sb);
	CHECK_STR(Spans(a, sa), L"alpha");
	CHECK_STR(Spans(b, sb), L"omega");

	// changes split only by a space are one span
	a = L"a b c d", b = L"a x y d";
	DfIntraLine(a, b, {}, sa, sb);
	CHECK_STR(Spans(a, sa), L"b c");
	CHECK_STR(Spans(b, sb), L"x y");
}

TEST(IntraLine_Whitespace) {
	std::vector<DfSpan> sa, sb;
	std::wstring a = L"if (a)  b();", b = L"if (a) b(); ";
	DfIntraLine(a, b, {}, sa, sb);
	CHECK(sa.size() == 1 && sb.size() == 2);

	DfOptions change;
	change.whitespace = DfWhitespace::Change;
	DfIntraLine(a, b, change, sa, sb);
	CHECK(sa.empty() && sb.empty());

	a = L"a+b", b = L"a + b";
	DfIntraLine(a, b, change, sa, sb);
	CHECK(sa.empty() && sb.size() == 2);   // the spaces appeared

	DfOptions all;
	all.whitespace = DfWhitespace::All;
	DfIntraLine(a, b, all, sa, sb);
	CHECK(sa.empty() && sb.empty());
}

TEST(Pairs) {
	DfLines a(L"keep\nint value = 1;\nremoved entirely\nfoo(a, b);\nkeep2\n");
	DfLines b(L"keep\nint value = 2;\nfoo(a, b, c);\nadded line here\nkeep2\n");
	auto diff = DfCompute(a, b);
	CHECK_STR(Script(diff), L"=---+++=");
	CHECK(diff.pairs.size() == 2);
	CHECK(diff.pairs[0].left == 1 && diff.pairs[0].right == 1);
	CHECK(diff.pairs[1].left == 3 && diff.pairs[1].right == 2);
	CHECK(diff.leftPair[2] == -1 && diff.rightPair[3] == -1);
	CHECK(diff.leftPair[3] == 1 && diff.rightPair[2] == 1);
	CHECK_STR(Spans(a[1], diff.pairs[0].leftSpans), L"1");
	CHECK_STR(Spans(b[2], diff.pairs[1].rightSpans), L", c");

	DfOptions plain;
	plain.intraLine = false;
	auto noSpans = DfCompute(a, b, plain);
	CHECK(noSpans.pairs.size() == 2 && noSpans.pairs[0].leftSpans.empty());
}

// ---- Rows ----------------------------------------------------------------------------------

TEST(Rows_SideBySide) {
	DfLines a(L"keep\nint value = 1;\nremoved entirely\nfoo(a, b);\nkeep2\n");
	DfLines b(L"keep\nint value = 2;\nfoo(a, b, c);\nadded line here\nkeep2\n");
	auto diff = DfCompute(a, b);
	auto rows = DfBuildRows(diff, DfLayout::SideBySide);
	CHECK_STR(Rows(rows), L"0:0 1*1 2~. 3*2 .~3 4:4");

	auto unified = DfBuildRows(diff, DfLayout::Unified);
	CHECK_STR(Rows(unified), L"0:0 1*. 2~. 3*. .*1 .*2 .~3 4:4");

	// unpaired lines on both sides share rows
	auto zip = DfBuildRows(DfCompute(Letters(L"xaby"), Letters(L"xcdey")), DfLayout::SideBySide);
	CHECK_STR(Rows(zip), L"0:0 1~1 2~2 .~3 3:4");
}

TEST(Rows_Folds) {
	auto a = Letters(L"abcdefghijklmnop");
	auto b = Letters(L"abcdefgXijklmnop");   // h -> X at 7
	auto diff = DfCompute(a, b);
	CHECK_STR(Rows(DfBuildRows(diff, DfLayout::SideBySide, 2)), L"F5 5:5 6:6 7~7 8:8 9:9 F6");
	CHECK_STR(Rows(DfBuildRows(diff, DfLayout::SideBySide, 0)), L"F7 7~7 F8");

	// a fold must hide two lines
	CHECK_STR(Rows(DfBuildRows(DfCompute(Letters(L"abc"), Letters(L"abX")), DfLayout::SideBySide, 1)), L"0:0 1:1 2~2");
	CHECK_STR(Rows(DfBuildRows(DfCompute(Letters(L"abcd"), Letters(L"abcX")), DfLayout::SideBySide, 1)), L"F2 2:2 3~3");
	CHECK_STR(Rows(DfBuildRows(DfCompute(Letters(L"abcde"), Letters(L"abcdX")), DfLayout::SideBySide, 1)), L"F3 3:3 4~4");

	// expanded blocks show their lines
	std::vector<uint8_t> expanded{ 1 };
	CHECK_STR(Rows(DfBuildRows(diff, DfLayout::SideBySide, 2, expanded)), L"0:0 1:1 2:2 3:3 4:4 5:5 6:6 7~7 8:8 9:9 F6");

	// a fold's first lines
	auto rows = DfBuildRows(diff, DfLayout::SideBySide, 2);
	CHECK(rows.back().left == 10 && rows.back().right == 10);

	// identical texts: one fold
	CHECK_STR(Rows(DfBuildRows(DfCompute(a, a), DfLayout::Unified, 3)), L"F16");

	// between two changes: context after the first and before the second
	auto two = DfCompute(Letters(L"aXcdefghYj"), Letters(L"a1cdefgh2j"));
	CHECK_STR(Rows(DfBuildRows(two, DfLayout::SideBySide, 1)), L"0:0 1~1 2:2 F4 7:7 8~8 9:9");
}

TEST(Rows_Navigation) {
	auto diff = DfCompute(Letters(L"aXcdefghYj"), Letters(L"a1cdefgh2Zj"));
	auto rows = DfBuildRows(diff, DfLayout::SideBySide);
	// 0:0 1~1 2:2 ... 7:7 8~8 .~9 9:10
	CHECK(DfNextChange(rows, -1, true) == 1);
	CHECK(DfNextChange(rows, 0, true) == 1);
	CHECK(DfNextChange(rows, 1, true) == 8);
	CHECK(DfNextChange(rows, 8, true) == -1);
	CHECK(DfNextChange(rows, 9, true) == -1);
	CHECK(DfNextChange(rows, 9, false) == 1);   // from inside the second change
	CHECK(DfNextChange(rows, 10, false) == 8);
	CHECK(DfNextChange(rows, int(rows.size()), false) == 8);
	CHECK(DfNextChange(rows, 1, false) == -1);

	CHECK(DfRowOfLine(rows, false, 9) == 10);
	CHECK(DfRowOfLine(rows, true, 9) == 9);
	CHECK(DfRowOfLine(rows, true, 10) == 10);
	CHECK(DfRowOfLine(rows, true, 11) == -1);

	auto folded = DfBuildRows(diff, DfLayout::SideBySide, 1);   // 0:0 1~1 2:2 F4 7:7 ...
	CHECK(DfRowOfLine(folded, false, 4) == 3);
	CHECK(DfRowOfLine(folded, false, 7) == 4);
}

// ---- Merging and text ---------------------------------------------------------------------

TEST(Merge) {
	auto left = Letters(L"abcdef"), right = Letters(L"abXYef");
	auto diff = DfCompute(left, right);
	CHECK(diff.blocks.size() == 3);
	auto l = left, r = right;
	DfMerge(l, r, diff.blocks[1], true);
	CHECK_STR(r.Join(L""), L"abcdef");
	CHECK(DfCompute(l, r).Identical());

	l = left, r = right;
	DfMerge(l, r, diff.blocks[1], false);
	CHECK_STR(l.Join(L""), L"abXYef");

	// a deletion merged to the right removes the lines there
	auto ins = DfCompute(Letters(L"ab"), Letters(L"aZb"));
	l = Letters(L"ab"), r = Letters(L"aZb");
	DfMerge(l, r, ins.blocks[1], true);
	CHECK_STR(r.Join(L""), L"ab");
}

TEST(UnifiedText) {
	auto a = Letters(L"abcdefghij"), b = Letters(L"abcDefghiJk");
	auto diff = DfCompute(a, b);
	CHECK_STR(DfUnifiedText(a, b, diff, 1),
		L"--- a\n+++ b\n"
		L"@@ -3,3 +3,3 @@\n c\n-d\n+D\n e\n"
		L"@@ -9,2 +9,3 @@\n i\n-j\n+J\n+k\n");
	// close changes share a hunk
	CHECK_STR(DfUnifiedText(a, b, diff, 3, L"old", L"new"),
		L"--- old\n+++ new\n"
		L"@@ -1,10 +1,11 @@\n a\n b\n c\n-d\n+D\n e\n f\n g\n h\n i\n-j\n+J\n+k\n");
	CHECK(DfUnifiedText(a, a, DfCompute(a, a)).empty());

	// counts of 0 and 1, a missing line break
	DfLines x(L"one"), y(L"one\ntwo");
	CHECK_STR(DfUnifiedText(x, y, DfCompute(x, y), 0),
		L"--- a\n+++ b\n@@ -1 +1,2 @@\n-one\n\\ No newline at end of file\n+one\n+two\n\\ No newline at end of file\n");
	DfLines e;
	CHECK_STR(DfUnifiedText(e, Letters(L"q"), DfCompute(e, Letters(L"q"))), L"--- a\n+++ b\n@@ -0,0 +1 @@\n+q\n");
}

// ---- Entry point -------------------------------------------------------------------------

int wmain() {
	wprintf(L"DiffView model tests\n\n");

	Run_Lines_Split();
	Run_Lines_Replace();

	Run_Diff_Basic();
	Run_Diff_Minimal_Random();
	Run_Diff_Edits_Random();
	Run_Diff_Anchors();
	Run_Diff_CostLimit();
	Run_Diff_Large();
	Run_Diff_Whitespace_Case();
	Run_Diff_Slide();
	Run_Diff_BlockOfLine();

	Run_Tokens();
	Run_Similarity();
	Run_IntraLine_Words();
	Run_IntraLine_Whitespace();
	Run_Pairs();

	Run_Rows_SideBySide();
	Run_Rows_Folds();
	Run_Rows_Navigation();

	Run_Merge();
	Run_UnifiedText();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
