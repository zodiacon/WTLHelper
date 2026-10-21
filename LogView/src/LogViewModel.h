#pragma once

// The log view's window-free parts: line storage and the queue of lines appended from other threads.
// Covered by LogViewTests.

#include <windows.h>

#include <cstdint>
#include <deque>
#include <memory>
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

// columns the text takes with tabs expanded to the next multiple of tabSize
size_t LgColumns(std::wstring_view text, int tabSize);
// the text with tabs expanded to spaces
void   LgExpandTabs(std::wstring_view text, int tabSize, std::wstring& out);
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
