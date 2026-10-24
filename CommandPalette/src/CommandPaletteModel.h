#pragma once

// The command palette's window-free parts: fuzzy matching (a pattern's words found in order in a label, scored by
// where they fall: word starts, runs, case, gaps), ranking a list by it with the recently used first, the recently
// used list and its saved text, splitting menu item text, and choosing a provider by the text's prefix. Covered by
// CommandPaletteTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ---- Matching -----------------------------------------------------------------------------

// A pattern: its words (split at spaces) must each be found in a label, their characters in order but not
// necessarily together; the words in any order. Case doesn't matter, though a capital typed scores more on a
// capital. Labels past CmpMaxLength characters are matched on their first CmpMaxLength.
constexpr size_t CmpMaxLength = 512;

class CmpPattern {
public:
	CmpPattern() = default;
	explicit CmpPattern(std::wstring_view text);

	bool Empty() const {
		return m_Words.empty();
	}
	// true if every word matches; score: higher is better; positions: the matched characters, in order
	bool Match(std::wstring_view label, int& score, std::vector<uint16_t>* positions = nullptr) const;

private:
	struct Word {
		std::wstring text;     // as typed
		std::wstring lower;
	};
	std::vector<Word> m_Words;

	// scratch for the matrices, reused across labels
	mutable std::vector<int> m_Score, m_From;
	mutable std::wstring m_Lower;
};

// a character that starts a word in a label: the first, one after a separator, a capital after a small letter, a
// digit after a non-digit
bool CmpIsWordStart(std::wstring_view label, size_t index);

// ---- Ranking ---------------------------------------------------------------------------------

struct CmpResult {
	size_t index = 0;              // in the list ranked
	int    score = 0;
	int    recent = -1;            // its place among the recently used (0: the last used), -1 if not
	std::vector<uint16_t> positions;
};

// Orders results: by score, then the recently used first (the latest first), then shorter labels (by lengths, the
// labels' lengths by index; empty: not), then by index. With an empty pattern scores are equal: given no lengths, the
// recently used come first, then the list in its order.
void CmpSort(std::vector<CmpResult>& results, const std::vector<size_t>& lengths);

// ---- Recently used --------------------------------------------------------------------------

class CmpRecent {
public:
	explicit CmpRecent(size_t limit = 50) : m_Limit(limit) {}

	void Use(UINT id);                        // to the front
	int  Rank(UINT id) const;                 // 0 for the last used, -1 if not in the list
	void Remove(UINT id);
	void Clear();
	size_t Count() const {
		return m_Ids.size();
	}
	const std::vector<UINT>& Ids() const {
		return m_Ids;
	}
	void SetLimit(size_t limit);

	std::wstring Save() const;                // "12;7;301", the latest first
	bool Load(std::wstring_view text);        // false (and empty) if it isn't such a list

private:
	std::vector<UINT> m_Ids;
	size_t m_Limit;
};

// ---- Menus ------------------------------------------------------------------------------------

// Splits a menu item's text: the name without its mnemonic marks ("&&" stays one '&'), and what follows a tab (the
// shortcut), trimmed.
void CmpParseMenuText(std::wstring_view text, std::wstring& name, std::wstring& shortcut);

// a submenu's path for the category: "File > Recent"
std::wstring CmpJoinPath(const std::vector<std::wstring>& path);

// ---- Providers ----------------------------------------------------------------------------------

// The provider whose prefix the text starts with, the longest that does (an empty prefix matches everything); -1 if
// none. rest: the text after the prefix, leading spaces skipped.
int CmpPickProvider(const std::vector<std::wstring>& prefixes, std::wstring_view text, std::wstring_view& rest);
