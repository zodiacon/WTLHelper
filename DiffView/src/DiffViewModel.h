#pragma once

// The diff view's window-free parts: the two texts split into lines, the line diff (Myers in linear space between
// lines unique to both sides, with a cost limit for very different texts), the blocks of equal and changed lines,
// the pairing of changed lines and their changes within the line (by words, then characters), the rows of the
// side-by-side and unified layouts with unchanged runs folded to a few lines of context, merging a change to the
// other side and the unified diff text. Covered by DiffViewTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ---- Lines -------------------------------------------------------------------------------

// A text's lines, without their line breaks (\r\n, \n or \r), in one buffer.
class DfLines {
public:
	DfLines() = default;
	explicit DfLines(std::wstring_view text);

	size_t size() const {
		return m_Starts.size() - 1;
	}
	bool empty() const {
		return size() == 0;
	}
	std::wstring_view operator[](size_t index) const {
		return std::wstring_view(m_Text).substr(m_Starts[index], m_Starts[index + 1] - m_Starts[index]);
	}
	// false when the last line has no line break after it (an empty text: true)
	bool EndsWithNewline() const {
		return m_EndsWithNewline;
	}

	// the lines with eol after each (but the last, if the text didn't end with a line break)
	std::wstring Join(std::wstring_view eol = L"\r\n") const;

	// Replaces count lines from first with fromCount lines of another text from fromFirst. Taking the other text's
	// last line takes its missing line break too.
	void Replace(size_t first, size_t count, const DfLines& from, size_t fromFirst, size_t fromCount);

private:
	std::wstring m_Text;
	std::vector<size_t> m_Starts{ 0 };   // size() + 1
	bool m_EndsWithNewline = true;
};

// ---- Diff ----------------------------------------------------------------------------------

enum class DfWhitespace {
	Exact,    // whitespace counts
	Change,   // a run of whitespace equals any other, leading and trailing whitespace is ignored
	All,      // whitespace is ignored
};

struct DfOptions {
	DfWhitespace whitespace = DfWhitespace::Exact;
	bool ignoreCase = false;
	bool intraLine = true;       // the changes within paired lines
	int  costLimit = 0;          // the edit distance past which a range is split by a good guess; 0: by size
};

// A run of lines: equal on both sides (the same count), or changed (deleted from the left, inserted on the right;
// either count may be 0).
struct DfBlock {
	bool   changed = false;
	size_t left = 0;
	size_t leftCount = 0;
	size_t right = 0;
	size_t rightCount = 0;
};

struct DfSpan {          // characters of a line
	uint32_t start = 0;
	uint32_t length = 0;
	bool operator==(const DfSpan&) const = default;
};

// A changed line on the left and the line on the right it became, with the characters that changed on each side.
struct DfLinePair {
	size_t left = 0;
	size_t right = 0;
	std::vector<DfSpan> leftSpans;
	std::vector<DfSpan> rightSpans;
};

struct DfDiff {
	std::vector<DfBlock> blocks;     // in order, equal and changed alternating, covering both texts
	std::vector<DfLinePair> pairs;   // in order
	std::vector<int> leftPair;       // per left line: its index in pairs, or -1
	std::vector<int> rightPair;      // per right line
	size_t changes = 0;              // changed blocks
	size_t deleted = 0;              // left lines in changed blocks
	size_t inserted = 0;             // right lines in changed blocks

	bool Identical() const {
		return changes == 0;
	}
};

DfDiff DfCompute(const DfLines& left, const DfLines& right, const DfOptions& options = {});

// The index of the block holding a line of one side (blocks with no lines on that side are skipped).
size_t DfBlockOfLine(const DfDiff& diff, bool right, size_t line);

// ---- Lines and words ----------------------------------------------------------------------

// The line as compared: case folded and whitespace removed or collapsed as the options say.
std::wstring DfNormalize(std::wstring_view line, const DfOptions& options);

struct DfToken {         // a run of word characters or of whitespace, or any other single character
	uint32_t start = 0;
	uint32_t length = 0;
	bool     space = false;
};
std::vector<DfToken> DfTokenize(std::wstring_view line);

// How alike two lines are, 0 .. 1: twice the characters of their common words over both lengths.
double DfSimilarity(std::wstring_view a, std::wstring_view b, const DfOptions& options = {});

// The characters that differ between two lines: by words, then by characters inside a changed word that mostly
// stayed. Whitespace changes the options ignore aren't included; a run of changes split only by a space is one span.
void DfIntraLine(std::wstring_view a, std::wstring_view b, const DfOptions& options, std::vector<DfSpan>& spansA, std::vector<DfSpan>& spansB);

// ---- Tabs ----------------------------------------------------------------------------------

size_t DfColumns(std::wstring_view line, int tabSize);                        // the line's width in columns
void   DfExpandTabs(std::wstring_view line, int tabSize, std::wstring& out);
size_t DfColumnOf(std::wstring_view line, size_t index, int tabSize);       // the column where a character starts

// ---- Rows ----------------------------------------------------------------------------------

enum class DfLayout {
	SideBySide,   // a changed block's lines side by side (paired lines on one row), the shorter side padded
	Unified,      // a changed block's left lines, then its right lines
};

enum class DfRowKind : uint8_t {
	Equal,
	Changed,
	Fold,         // unchanged lines not shown
};

struct DfRow {
	DfRowKind kind = DfRowKind::Equal;
	bool     paired = false;     // Changed: its lines are a pair (side by side), or the line has a pair (unified)
	int      left = -1;          // the line on each side; -1: none (Fold: the first hidden lines)
	int      right = -1;
	uint32_t block = 0;
	uint32_t hidden = 0;         // Fold: how many lines
};

// The rows to show. With context >= 0, runs of unchanged lines keep that many lines next to a change and fold the
// rest (a fold must hide at least two lines); expanded (by block index, may be shorter or empty) shows a block's
// lines anyway.
std::vector<DfRow> DfBuildRows(const DfDiff& diff, DfLayout layout, int context = -1, const std::vector<uint8_t>& expanded = {});

// The first row of the next (or previous) changed block after (or before) the block of the given row; -1 if none.
int DfNextChange(const std::vector<DfRow>& rows, int row, bool forward);

// The row showing a line of one side, or the fold hiding it, or the row before which it would be; -1 if none.
int DfRowOfLine(const std::vector<DfRow>& rows, bool right, size_t line);

// ---- Merging and text ---------------------------------------------------------------------

// Copies a changed block's lines to the other side: toRight replaces the block's right lines with its left ones.
void DfMerge(DfLines& left, DfLines& right, const DfBlock& block, bool toRight);

// The diff in the unified format ("@@ -1,3 +1,4 @@" hunks), with context lines around each change.
std::wstring DfUnifiedText(const DfLines& left, const DfLines& right, const DfDiff& diff, int context = 3,
	std::wstring_view leftName = L"a", std::wstring_view rightName = L"b", std::wstring_view eol = L"\n");

// ---- The control's document ------------------------------------------------------------------

struct DfDocument {
	DfLines left;
	DfLines right;
	DfDiff  diff;
	std::vector<size_t> changeBlocks;   // the changed blocks' indexes, in order
	std::vector<DfRow> rows;
	std::vector<uint8_t> expanded;      // by block: folds the user opened
	size_t  maxColumns = 0;             // the widest line of either side, tabs expanded

	void Compute(const DfOptions& options, int tabSize);
	void UpdateColumns(int tabSize);
	// the index in changeBlocks of a block, or -1 if it isn't a change
	int ChangeOfBlock(size_t block) const;
};
