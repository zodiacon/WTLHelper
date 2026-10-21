#pragma once

// The log view's window-free parts: line storage and the queue of lines appended from other threads.
// Covered by LogViewTests.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

// a stored line; its text is in a chunk
struct LgLineRec {
	uint32_t chunk;      // the chunk's serial number
	uint32_t offset;     // in the chunk, characters
	uint32_t length;
	BYTE     level;
	BYTE     style;
	LPARAM   lParam;
};

// Lines appended away from the window's thread, waiting to be taken in. The text of all of them is in one
// buffer, so queuing a line is one append and no allocation of its own.
class LgBatch {
public:
	struct Line {
		size_t offset;
		size_t length;
		BYTE   level;
		BYTE   style;
		LPARAM lParam;
	};

	void   Add(const wchar_t* text, size_t length, BYTE level, BYTE style, LPARAM lParam);
	// splits at "\r\n", "\n" and "\r"; a final line break doesn't start another line
	void   AddText(const wchar_t* text, size_t length, BYTE level, BYTE style);
	void   Clear();
	bool   Empty() const { return m_Lines.empty(); }
	size_t Count() const { return m_Lines.size(); }
	std::wstring_view Text(const Line& line) const { return { m_Text.data() + line.offset, line.length }; }
	const std::vector<Line>& Lines() const { return m_Lines; }

private:
	std::vector<wchar_t> m_Text;
	std::vector<Line>    m_Lines;
};

// The control's own lines. Text goes into large chunks, so a million lines aren't a million allocations;
// a chunk is freed once the limit has dropped all of its lines. Indexes are 0-based and shift down when
// lines are dropped; numbers (as shown) don't.
class LgStore {
public:
	static constexpr size_t DefaultChunk = 256 * 1024;   // characters
	static constexpr size_t MaxLineLength = 0xFFFFFFFF;  // longer lines are cut

	explicit LgStore(size_t chunkChars = DefaultChunk);

	void   Append(const wchar_t* text, size_t length, BYTE level, BYTE style, LPARAM lParam);
	void   Append(const LgBatch& batch);
	void   Clear();                       // numbering starts again at 1

	size_t Count() const { return m_Lines.size(); }
	std::wstring_view Text(size_t line) const;
	const LgLineRec& Line(size_t line) const { return m_Lines[line]; }
	LgLineRec& Line(size_t line) { return m_Lines[line]; }
	ULONGLONG Number(size_t line) const { return m_Dropped + line + 1; }
	ULONGLONG FirstSerial() const { return m_Dropped; }   // line 0's serial (its number - 1)

	void   SetLimit(size_t maxLines);     // 0: none; drops lines right away if there are more
	size_t Limit() const { return m_Limit; }
	size_t TakeDropped();                 // lines dropped since the last call (the view moves up by as many)

	// the widest line in columns, tabs expanded; it doesn't shrink as lines are dropped
	size_t MaxColumns() const { return m_MaxColumns; }
	void   SetTabSize(int tabSize);       // measures every line again
	size_t ChunkCount() const { return m_Chunks.size(); }

private:
	struct Chunk {
		std::unique_ptr<wchar_t[]> data;
		size_t size;
		size_t used;
	};

	void   Trim();

	size_t                 m_ChunkChars;
	std::deque<Chunk>      m_Chunks;
	uint32_t               m_FirstChunk = 0;   // the serial number of m_Chunks.front()
	std::deque<LgLineRec>  m_Lines;
	size_t                 m_Limit = 0;
	ULONGLONG              m_Dropped = 0;      // since the last Clear
	size_t                 m_NewlyDropped = 0;
	size_t                 m_MaxColumns = 0;
	int                    m_TabSize = 4;
};

// Text to look for in lines: plain (ordinal, optionally ignoring case) or an ECMAScript regex.
class LgMatcher {
public:
	struct Span {
		size_t start;
		size_t length;
	};

	bool   Set(std::wstring_view text, bool regex, bool matchCase);   // false: a bad regex (nothing changes)
	void   Clear();
	bool   Empty() const { return m_Text.empty(); }
	bool   Matches(std::wstring_view text) const;                   // an empty matcher matches everything
	void   FindAll(std::wstring_view text, std::vector<Span>& spans) const;   // in order, not overlapping, not empty
	const std::wstring& Text() const { return m_Text; }
	bool   IsRegex() const { return m_Regex != nullptr; }
	bool   MatchCase() const { return m_MatchCase; }

private:
	std::wstring                 m_Text;
	bool                         m_MatchCase = false;
	std::shared_ptr<std::wregex> m_Regex;   // shared: matchers are copied, the compiled regex isn't changed
};

// Which lines a filter shows: those matching the text (or not matching it, with exclude) whose level is in the mask.
struct LgFilter {
	LgMatcher matcher;
	bool      exclude = false;
	DWORD     levelMask = 0xFFFFFFFF;

	bool Active() const { return !matcher.Empty() || levelMask != 0xFFFFFFFF; }
	bool Passes(std::wstring_view text, BYTE level) const {
		if (!(levelMask & (1u << (level & 31))))
			return false;
		return matcher.Empty() || matcher.Matches(text) != exclude;
	}
};

// A rule: lines (or with matchOnly the text) matching it get a style.
struct LgRule {
	LgMatcher matcher;
	BYTE      style = 0;
	bool      matchOnly = false;
};

// The lines a filter shows, in order, by serial number (a line's number - 1: it doesn't change when older
// lines are dropped). Row = index here.
class LgRowMap {
public:
	void     Clear() { m_Serials.clear(); }
	void     Add(ULONGLONG serial) { m_Serials.push_back(serial); }   // larger than any before
	size_t   Count() const { return m_Serials.size(); }
	ULONGLONG Serial(size_t row) const { return m_Serials[row]; }
	size_t   RowOf(ULONGLONG serial) const;          // the first row at or after serial (Count() if none)
	bool     Contains(ULONGLONG serial) const;
	size_t   DropBefore(ULONGLONG serial);           // returns the rows dropped
	void     DropFrom(ULONGLONG serial);

private:
	std::deque<ULONGLONG> m_Serials;
};

// columns the text takes with tabs expanded to the next multiple of tabSize
size_t LgColumns(std::wstring_view text, int tabSize);
// the text with tabs expanded to spaces
void   LgExpandTabs(std::wstring_view text, int tabSize, std::wstring& out);
// the columns before the character at index (tabs expanded)
size_t LgColumnOf(std::wstring_view text, size_t index, int tabSize);
// the character boundary nearest to a column (fractional: a point inside a character); a point inside a tab goes
// to the nearer side of the tab
size_t LgIndexAtColumn(std::wstring_view text, double column, int tabSize);

// words, for Ctrl+arrows and double clicks: runs of letters, digits and '_', runs of other non-space characters
size_t LgWordRight(std::wstring_view text, size_t index);   // the start of the next word (or the end)
size_t LgWordLeft(std::wstring_view text, size_t index);    // the start of this word or the one before
void   LgWordAt(std::wstring_view text, size_t index, size_t& start, size_t& end);   // the run around index

// the text from (startLine, startCol) to (endLine, endCol), lines joined with "\r\n"; getLine(line) returns
// a line's text. Columns past a line's end are taken as its end.
template<typename GetLine>
void   LgJoinRange(size_t startLine, size_t startCol, size_t endLine, size_t endCol, GetLine&& getLine, std::wstring& out) {
	out.clear();
	for (size_t line = startLine; line <= endLine; line++) {
		std::wstring_view text = getLine(line);
		size_t from = line == startLine ? std::min(startCol, text.size()) : 0;
		size_t to = line == endLine ? std::min(endCol, text.size()) : text.size();
		if (to > from)
			out.append(text.substr(from, to - from));
		if (line < endLine)
			out.append(L"\r\n");
	}
}

// calls fn(start, length) for each line of text, as LgBatch::AddText splits it
template<typename Fn>
void   LgSplitLines(std::wstring_view text, Fn&& fn) {
	size_t start = 0;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == L'\n' || text[i] == L'\r') {
			fn(start, i - start);
			if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
				i++;
			start = i + 1;
		}
	}
	if (start < text.size())
		fn(start, text.size() - start);
}
