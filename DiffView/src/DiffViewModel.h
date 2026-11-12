#pragma once

// The diff view's window-free parts: the two texts split into lines, the line diff (Myers in linear space between
// lines unique to both sides, with a cost limit for very different texts), the blocks of equal and changed lines,
// the pairing of changed lines and their changes within the line (by words, then characters), the rows of the
// side-by-side and unified layouts with unchanged runs folded to a few lines of context, merging a change to the
// other side and the unified diff text. Covered by DiffViewTests.

#include <windows.h>
#include <commctrl.h>   // CLR_NONE, CLR_DEFAULT

#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <string_view>
#include <unordered_map>
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
	void SetEndsWithNewline(bool ends) {
		m_EndsWithNewline = ends || empty();
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
	// set (from another thread) to stop early; the diff is then still consistent but not a good one
	const std::atomic<bool>* cancel = nullptr;
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

using DfRanges = std::vector<std::pair<size_t, size_t>>;   // [first, last) runs of lines

// ---- Patches ---------------------------------------------------------------------------------

// What a unified diff of one file gives: the hunks' lines, and the lines between them (which it doesn't have).
struct DfPatchData {
	std::vector<DfBlock> blocks;       // the diff, as the patch says it: equal blocks (the context, with the gaps) and changes
	DfRanges unknown;                  // [first, last) of the lines the patch doesn't have, before and between its hunks
	std::wstring text;                 // the patch as given
};

// Reads the hunks of the first file in a unified diff (the headers before the hunks, "diff --git", "---", "+++"
// and the rest, are skipped; the next "diff " line ends the file). The left and right texts are the patch's lines at
// the line numbers of the hunks' headers, the lines before and between hunks empty (they're in data.unknown). The
// lines after the last hunk aren't known: the texts end with it. False and the reason (and the patch's line) in
// error if it isn't a unified diff or its hunks don't add up; the outputs are then not touched.
bool DfParsePatch(std::wstring_view patch, DfLines& left, DfLines& right, DfPatchData& data, std::wstring* error = nullptr);

// The diff for blocks that are given: the pairing and the changes within lines are done as DfCompute does.
DfDiff DfDiffFromBlocks(const std::vector<DfBlock>& blocks, const DfLines& left, const DfLines& right, const DfOptions& options = {});

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

// ---- Styled text (syntax highlighting) -------------------------------------------------------

constexpr uint32_t DfNoStyle = UINT32_MAX;   // plain text: no span, or a style the palette doesn't have

struct DfStyleSpan {     // characters of a line (indexes into the line, tabs not expanded) and the app's style for them
	uint32_t start = 0;
	uint32_t length = 0;
	uint32_t style = 0;
	bool operator==(const DfStyleSpan&) const = default;
};

// Sorts the spans by start and makes them fit the line: spans past its end are cut or dropped, empty ones dropped,
// and where spans overlap the one that starts first (the first given, on a tie) keeps the characters.
void DfNormalizeStyleSpans(size_t lineLength, std::vector<DfStyleSpan>& spans);

struct DfStyleSegment {  // columns of the line with its tabs expanded, [first, last)
	size_t   first = 0;
	size_t   last = 0;
	uint32_t style = DfNoStyle;
	bool operator==(const DfStyleSegment&) const = default;
};

// The line's whole width as consecutive segments: the (normalized) spans' columns, and plain ones between and after
// them. Neighbors of one style are joined.
std::vector<DfStyleSegment> DfStyleSegments(std::wstring_view line, const std::vector<DfStyleSpan>& spans, int tabSize);

struct DfStyle {         // one entry of the palette
	COLORREF text = CLR_DEFAULT;   // CLR_DEFAULT: the text color
	COLORREF back = CLR_NONE;      // CLR_NONE: none
	UINT     flags = 0;            // 1 bold, 2 italic, 4 underline
};

// The palette and the spans the app gave for the lines asked about, by side and line. With no palette nothing is asked.
struct DfStyleStore {
	std::vector<DfStyle> palette;
	std::unordered_map<size_t, std::vector<DfStyleSpan>> cache[2];
	static constexpr size_t MaxCached = 20000;   // lines per side; the cache is emptied when it gets past this
};

// ---- Rows ----------------------------------------------------------------------------------

enum class DfLayout {
	SideBySide,   // a changed block's lines side by side (paired lines on one row), the shorter side padded
	Unified,      // a changed block's left lines, then its right lines
};

enum class DfRowKind : uint8_t {
	Equal,
	Changed,
	Fold,         // unchanged lines not shown
	Annotation,   // room the app fills (a comment); no line of either side
};

// Room the app asked for under a line: a few rows, as many as its height needs (the control's row height is fixed,
// which keeps scrolling, paging and the overview in whole rows).
struct DfAnnotationSlot {
	uint32_t id = 0;
	bool     right = false;      // the side whose line it follows
	size_t   line = 0;
	uint32_t rows = 1;           // >= 1
	bool     full = false;       // side by side: across both panes (else in its side's pane, the other one hatched)
};

struct DfRow {
	DfRowKind kind = DfRowKind::Equal;
	bool     paired = false;     // Changed: its lines are a pair (side by side), or the line has a pair (unified)
	int      left = -1;          // the line on each side; -1: none (Fold: the first hidden lines)
	int      right = -1;
	uint32_t block = 0;
	uint32_t hidden = 0;         // Fold: how many lines
	uint32_t annotation = 0;     // Annotation: the app's id
	uint32_t slot = 0;           // Annotation: which of its rows this is, 0 for the first
	uint32_t slots = 0;          // Annotation: how many rows it has
	uint8_t  pane = 0;           // Annotation, side by side: 0 the left pane, 1 the right one, 2 both
	bool     gap = false;        // Fold: lines the patch doesn't have; no way to show them
};

// The rows to show. With context >= 0, runs of unchanged lines keep that many lines next to a change and fold the
// rest (a fold must hide at least two lines); expanded (by block index, may be shorter or empty) shows a block's
// lines anyway. An annotated unchanged line stays out of the folds, with up to DfAnnotationContext lines around it,
// and the annotation's rows come right after the row showing its line (annotations on one row: in the order given,
// the left side's first). One whose line isn't shown (there is no such line) gets no rows. Lines the text doesn't
// have (unknown: [first, last) ranges in line numbers, inside equal blocks; the same lines on both sides) are never
// shown: each run is a fold with gap set, which no expanding opens.
constexpr int DfAnnotationContext = 2;
std::vector<DfRow> DfBuildRows(const DfDiff& diff, DfLayout layout, int context = -1, const std::vector<uint8_t>& expanded = {},
	const std::vector<DfAnnotationSlot>& annotations = {}, const DfRanges& unknown = {});

// The first row of the next (or previous) changed block after (or before) the block of the given row; -1 if none.
int DfNextChange(const std::vector<DfRow>& rows, int row, bool forward);

// The row showing a line of one side, or the fold hiding it, or the row before which it would be; -1 if none.
int DfRowOfLine(const std::vector<DfRow>& rows, bool right, size_t line);

// ---- Merging and text ---------------------------------------------------------------------

// Copies a changed block's lines to the other side: toRight replaces the block's right lines with its left ones.
void DfMerge(DfLines& left, DfLines& right, const DfBlock& block, bool toRight);

// What a merge replaced, to undo it.
struct DfMergeUndo {
	bool    right = false;       // the side that changed
	size_t  first = 0;
	size_t  count = 0;           // the lines there now
	DfLines removed;             // the lines that were there
	bool    endsWithNewline = true;   // the side's, before
};
DfMergeUndo DfMergeWithUndo(DfLines& left, DfLines& right, const DfBlock& block, bool toRight);
void DfUndoMerge(DfLines& left, DfLines& right, const DfMergeUndo& undo);

struct DfUndoStack {
	std::vector<DfMergeUndo> items;
};

// The diff in the unified format ("@@ -1,3 +1,4 @@" hunks), with context lines around each change.
std::wstring DfUnifiedText(const DfLines& left, const DfLines& right, const DfDiff& diff, int context = 3,
	std::wstring_view leftName = L"a", std::wstring_view rightName = L"b", std::wstring_view eol = L"\n");

// ---- Words and columns ----------------------------------------------------------------------

size_t DfWordRight(std::wstring_view line, size_t index);   // the start of the next word (or the end)
size_t DfWordLeft(std::wstring_view line, size_t index);    // the start of this word or the one before
void   DfWordAt(std::wstring_view line, size_t index, size_t& start, size_t& end);   // the run around index
// the character boundary nearest to a column (fractional: a click inside a character)
size_t DfIndexAtColumn(std::wstring_view line, double column, int tabSize);

// ---- The control's document ------------------------------------------------------------------

struct DfPos {                          // a place between characters of a line of one side
	bool   right = false;
	size_t line = 0;
	size_t column = 0;                  // a character index, 0..length
	bool operator==(const DfPos&) const = default;
};

// The lines a comment started at a line covers (the "+" in the gutter): the selection's lines if the line is among
// them and the selection is on the line's side (a selection that ends at the start of a line doesn't include it),
// else just the line. first <= last.
void DfCommentRange(bool hasSelection, DfPos start, DfPos end, DfPos clicked, size_t& first, size_t& last);

struct DfChangeRows {                   // where a changed block's rows are
	size_t first = 0;
	size_t count = 0;
	bool   left = false;                // it has lines on the left
	bool   right = false;
};

struct DfDocument {
	DfLines left;
	DfLines right;
	DfDiff  diff;
	std::vector<size_t> changeBlocks;   // the changed blocks' indexes, in order
	DfLayout layout = DfLayout::SideBySide;
	std::vector<DfRow> rows;
	std::vector<DfChangeRows> changeRows;   // by change, as changeBlocks
	std::vector<int> leftRows, rightRows;   // by line: the row showing it, or the fold hiding it
	std::vector<uint8_t> expanded;      // by block: folds the user opened
	std::shared_ptr<DfPatchData> patch;          // the texts are a patch's: the diff is its blocks
	std::vector<DfAnnotationSlot> annotations;   // set before BuildRows
	std::unordered_map<uint32_t, size_t> annotationRows;   // by id: the first row of those that have rows
	size_t  maxColumns = 0;             // the widest line of either side, tabs expanded

	void Compute(const DfOptions& options, int tabSize);
	void UpdateColumns(int tabSize);
	void BuildRows(DfLayout layout, int context);
	// the index in changeBlocks of a block, or -1 if it isn't a change
	int ChangeOfBlock(size_t block) const;

	const DfLines& Lines(bool rightSide) const {
		return rightSide ? right : left;
	}
	int RowOf(bool rightSide, size_t line) const;   // -1 if there's no such line
	int RowOf(const DfPos& pos) const {
		return RowOf(pos.right, pos.line);
	}
	// The line a row shows on a side (side by side), or the row's line (unified: its left line if it has one,
	// else its right); false for a fold, a filler or no such row.
	bool LineOfRow(size_t row, bool rightSide, DfPos& pos) const;
	// From a row, delta rows (negative: up) that show a line (of the side, side by side), stopping at the last
	// one that does; -1 if no row does.
	int  StepRow(int row, int delta, bool rightSide) const;
	// a position moved into the text (an existing line of its side, a column within it)
	DfPos Clamp(DfPos pos) const;
	// Orders two positions: side by side by line (they're on one side), unified by row.
	bool Less(const DfPos& a, const DfPos& b) const;
	// The text from one position to the other (in either order), lines joined with eol. Side by side: the lines
	// of a's side; unified: the rows, a fold's lines included.
	std::wstring Text(DfPos a, DfPos b, std::wstring_view eol = L"\r\n") const;
	// the whole of a side (side by side) or of every row (unified)
	void All(bool rightSide, DfPos& start, DfPos& end) const;
};

// ---- Finding ----------------------------------------------------------------------------------
//
// A find goes through the texts in the order the unified layout shows them: an unchanged line once, then a change's
// left lines and then its right ones. An unchanged line whose two sides read the same is searched once, on the side
// the caller prefers (the caret's, side by side; the left one, unified, which is what that layout shows); one whose
// sides differ (ignoring whitespace or case made them equal) is searched on both, its left side first.

struct DfFind {
	std::wstring what;
	bool matchCase = false;
	bool wholeWord = false;             // not inside a run of word characters on either end
};

// The matches in a line, left to right, not overlapping.
void DfFindInLine(std::wstring_view line, const DfFind& find, std::vector<DfSpan>& matches);
// The match after from (forward: the first starting at from or later) or before it (backward: the last starting before
// from), going round past the end (or the start); false if there's none at all.
bool DfFindNext(const DfLines& left, const DfLines& right, const DfDiff& diff, const DfFind& find, DfPos from, bool forward,
	bool preferRight, DfPos& start, DfPos& end);
// How many matches there are, counted up to max (an unchanged line that reads the same on both sides once).
size_t DfCountMatches(const DfLines& left, const DfLines& right, const DfDiff& diff, const DfFind& find, size_t max = SIZE_MAX);
