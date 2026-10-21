#include "LogViewModel.h"

#include <algorithm>
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
