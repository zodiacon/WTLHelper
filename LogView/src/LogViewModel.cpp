#include "LogViewModel.h"

#include <algorithm>
#include <climits>
#include <cwctype>
#include <utility>

// ---- Helpers ---------------------------------------------------------------------

size_t LgColumns(std::wstring_view text, int tabSize) {
	size_t columns = 0;
	for (auto ch : text)
		columns = ch == L'\t' ? (columns / tabSize + 1) * tabSize : columns + 1;
	return columns;
}

void LgExpandTabs(std::wstring_view text, int tabSize, std::wstring& out) {
	out.clear();
	if (text.find(L'\t') == std::wstring_view::npos) {
		out.assign(text);
		return;
	}
	out.reserve(LgColumns(text, tabSize));
	for (auto ch : text) {
		if (ch == L'\t')
			out.append(tabSize - out.size() % tabSize, L' ');
		else
			out.push_back(ch);
	}
}

size_t LgColumnOf(std::wstring_view text, size_t index, int tabSize) {
	return LgColumns(text.substr(0, std::min(index, text.size())), tabSize);
}

size_t LgIndexAtColumn(std::wstring_view text, double column, int tabSize) {
	size_t col = 0;
	for (size_t i = 0; i < text.size(); i++) {
		size_t next = text[i] == L'\t' ? (col / tabSize + 1) * tabSize : col + 1;
		if (column < (col + next) / 2.0)
			return i;
		col = next;
	}
	return text.size();
}

// 0: space, 1: letters, digits and '_', 2: anything else
static int CharClass(wchar_t ch) {
	if (::iswspace(ch))
		return 0;
	return ::iswalnum(ch) || ch == L'_' ? 1 : 2;
}

size_t LgWordRight(std::wstring_view text, size_t index) {
	size_t n = text.size();
	if (index >= n)
		return n;
	int cls = CharClass(text[index]);
	while (index < n && cls != 0 && CharClass(text[index]) == cls)
		index++;
	while (index < n && CharClass(text[index]) == 0)
		index++;
	return index;
}

size_t LgWordLeft(std::wstring_view text, size_t index) {
	index = std::min(index, text.size());
	while (index > 0 && CharClass(text[index - 1]) == 0)
		index--;
	if (index == 0)
		return 0;
	int cls = CharClass(text[index - 1]);
	while (index > 0 && CharClass(text[index - 1]) == cls)
		index--;
	return index;
}

void LgWordAt(std::wstring_view text, size_t index, size_t& start, size_t& end) {
	size_t n = text.size();
	if (n == 0) {
		start = end = 0;
		return;
	}
	index = std::min(index, n - 1);
	int cls = CharClass(text[index]);
	start = end = index;
	while (start > 0 && CharClass(text[start - 1]) == cls)
		start--;
	while (end < n && CharClass(text[end]) == cls)
		end++;
}

// ---- LgMatcher ---------------------------------------------------------------------

bool LgMatcher::Set(std::wstring_view text, bool regex, bool matchCase, bool wholeWord) {
	if (text.empty()) {
		Clear();
		return true;
	}
	std::shared_ptr<std::wregex> compiled;
	if (regex) {
		try {
			auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
			if (!matchCase)
				flags |= std::regex_constants::icase;
			compiled = std::make_shared<std::wregex>(text.data(), text.size(), flags);
		}
		catch (const std::regex_error&) {
			return false;
		}
	}
	m_Text.assign(text);
	m_MatchCase = matchCase;
	m_WholeWord = wholeWord;
	m_Regex = std::move(compiled);
	return true;
}

void LgMatcher::Clear() {
	m_Text.clear();
	m_MatchCase = false;
	m_WholeWord = false;
	m_Regex.reset();
}

// FindStringOrdinal takes int lengths
static int FindOrdinal(std::wstring_view text, const std::wstring& what, bool matchCase) {
	int n = (int)std::min<size_t>(text.size(), INT_MAX);
	return ::FindStringOrdinal(FIND_FROMSTART, text.data(), n, what.c_str(), (int)what.size(), !matchCase);
}

// whole word: not preceded or followed by a letter, digit or '_'
static bool IsWholeWord(std::wstring_view text, size_t start, size_t length) {
	return (start == 0 || CharClass(text[start - 1]) != 1) && (start + length >= text.size() || CharClass(text[start + length]) != 1);
}

bool LgMatcher::Matches(std::wstring_view text) const {
	if (m_Text.empty())
		return true;
	if (m_WholeWord) {
		std::vector<Span> spans;
		FindAll(text, spans);
		return !spans.empty();
	}
	if (m_Regex)
		return std::regex_search(text.begin(), text.end(), *m_Regex);
	return FindOrdinal(text, m_Text, m_MatchCase) >= 0;
}

void LgMatcher::FindAll(std::wstring_view text, std::vector<Span>& spans) const {
	spans.clear();
	if (m_Text.empty())
		return;
	if (m_Regex) {
		using Iterator = std::regex_iterator<std::wstring_view::const_iterator>;
		for (Iterator it(text.begin(), text.end(), *m_Regex), end; it != end; ++it) {
			if (it->length(0) > 0 && (!m_WholeWord || IsWholeWord(text, it->position(0), it->length(0))))
				spans.push_back({ (size_t)it->position(0), (size_t)it->length(0) });
		}
		return;
	}
	size_t pos = 0;
	while (pos < text.size()) {
		int found = FindOrdinal(text.substr(pos), m_Text, m_MatchCase);
		if (found < 0)
			break;
		if (m_WholeWord && !IsWholeWord(text, pos + found, m_Text.size())) {
			pos += found + 1;   // a later occurrence may overlap this one
			continue;
		}
		spans.push_back({ pos + found, m_Text.size() });
		pos += found + m_Text.size();
	}
}

int LgPickMatch(const std::vector<LgMatcher::Span>& spans, bool forward, size_t lo, size_t hi) {
	int pick = -1;
	for (int i = 0; i < (int)spans.size(); i++) {
		if (spans[i].start < lo || spans[i].start >= hi)
			continue;
		pick = i;
		if (forward)
			break;
	}
	return pick;
}

// ---- LgRowMap ----------------------------------------------------------------------

size_t LgRowMap::RowOf(ULONGLONG serial) const {
	return std::lower_bound(m_Serials.begin(), m_Serials.end(), serial) - m_Serials.begin();
}

bool LgRowMap::Contains(ULONGLONG serial) const {
	return std::binary_search(m_Serials.begin(), m_Serials.end(), serial);
}

size_t LgRowMap::DropBefore(ULONGLONG serial) {
	auto end = std::lower_bound(m_Serials.begin(), m_Serials.end(), serial);
	size_t count = end - m_Serials.begin();
	m_Serials.erase(m_Serials.begin(), end);
	return count;
}

void LgRowMap::DropFrom(ULONGLONG serial) {
	m_Serials.erase(std::lower_bound(m_Serials.begin(), m_Serials.end(), serial), m_Serials.end());
}

// ---- LgBatch -----------------------------------------------------------------------

void LgBatch::Add(const wchar_t* text, size_t length, BYTE level, BYTE style, LPARAM lParam) {
	m_Lines.push_back({ m_Text.size(), length, level, style, lParam });
	m_Text.insert(m_Text.end(), text, text + length);
}

void LgBatch::AddText(const wchar_t* text, size_t length, BYTE level, BYTE style) {
	LgSplitLines({ text, length }, [&](size_t start, size_t count) {
		Add(text + start, count, level, style, 0);
	});
}

void LgBatch::Clear() {
	m_Text.clear();
	m_Lines.clear();
}

// ---- LgStore -----------------------------------------------------------------------

LgStore::LgStore(size_t chunkChars) : m_ChunkChars(std::max<size_t>(chunkChars, 16)) {
}

void LgStore::Append(const wchar_t* text, size_t length, BYTE level, BYTE style, LPARAM lParam) {
	length = std::min(length, MaxLineLength);
	if (m_Chunks.empty() || m_Chunks.back().size - m_Chunks.back().used < length) {
		size_t size = std::max(m_ChunkChars, length);
		m_Chunks.push_back({ std::make_unique_for_overwrite<wchar_t[]>(size), size, 0 });
	}
	auto& chunk = m_Chunks.back();
	std::copy_n(text, length, chunk.data.get() + chunk.used);
	m_Lines.push_back({ m_FirstChunk + (uint32_t)(m_Chunks.size() - 1), (uint32_t)chunk.used, (uint32_t)length, level, style, lParam });
	chunk.used += length;
	m_MaxColumns = std::max(m_MaxColumns, LgColumns({ text, length }, m_TabSize));
	if (m_Limit && m_Lines.size() > m_Limit)
		Trim();
}

void LgStore::Append(const LgBatch& batch) {
	for (auto& line : batch.Lines()) {
		auto text = batch.Text(line);
		Append(text.data(), text.size(), line.level, line.style, line.lParam);
	}
}

void LgStore::Clear() {
	m_FirstChunk += (uint32_t)m_Chunks.size();
	m_Chunks.clear();
	m_Lines.clear();
	m_Dropped = 0;
	m_NewlyDropped = 0;
	m_MaxColumns = 0;
}

std::wstring_view LgStore::Text(size_t line) const {
	auto& rec = m_Lines[line];
	return { m_Chunks[rec.chunk - m_FirstChunk].data.get() + rec.offset, rec.length };
}

void LgStore::SetLimit(size_t maxLines) {
	m_Limit = maxLines;
	if (m_Limit && m_Lines.size() > m_Limit)
		Trim();
}

void LgStore::Trim() {
	size_t count = m_Lines.size() - m_Limit;
	m_Lines.erase(m_Lines.begin(), m_Lines.begin() + count);
	m_Dropped += count;
	m_NewlyDropped += count;
	// the lines left are in the front line's chunk and those after it
	uint32_t keep = m_Lines.empty() ? m_FirstChunk + (uint32_t)m_Chunks.size() - 1 : m_Lines.front().chunk;
	while (m_FirstChunk < keep) {
		m_Chunks.pop_front();
		m_FirstChunk++;
	}
}

size_t LgStore::TakeDropped() {
	return std::exchange(m_NewlyDropped, 0);
}

void LgStore::SetTabSize(int tabSize) {
	m_TabSize = tabSize;
	m_MaxColumns = 0;
	for (size_t i = 0; i < m_Lines.size(); i++)
		m_MaxColumns = std::max(m_MaxColumns, LgColumns(Text(i), m_TabSize));
}
