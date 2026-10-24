#pragma once

// The command palette's window-free parts: fuzzy matching (a pattern's words found in order in a label, scored by
// where they fall: word starts, runs, case, gaps), ranking a list by it with the recently used first, the recently
// used list and its saved text, splitting menu item text, and choosing a provider by the text's prefix. Covered by
// CommandPaletteTests.

#include <windows.h>

#include <algorithm>
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

// ---- Commands --------------------------------------------------------------------------------

struct CmpItem {
	UINT   id = 0;
	std::wstring name;
	std::wstring category;
	std::wstring shortcut;
	std::wstring label;            // what's shown and matched: "Category: Name", or the name
	int    image = -1;
	UINT   state = 0;              // CMDPIS_*
	LPARAM lParam = 0;
	int    source = -1;            // the menu it was read from (an index), -1: added by itself

	void UpdateLabel() {
		label = category.empty() ? name : category + L": " + name;
	}
	size_t NameStart() const {     // where the name starts in the label
		return label.size() - name.size();
	}
};

// An item provider: the app's list picked by a prefix, its items asked for as they're needed.
struct CmpProvider {
	int    id = 0;
	std::wstring prefix;
	std::wstring name;
	std::wstring placeholder;
	UINT   flags = 0;              // CMDPPF_*
	LPARAM lParam = 0;
	size_t count = 0;
	void (CALLBACK* getItem)(void*, struct NMCMDPGETITEM*) = nullptr;   // the app's callback, else the notification
	void*  context = nullptr;
	std::vector<CmpItem> items;    // count of them, filled in as they're asked for
	std::vector<uint8_t> fetched;
};

// ---- Ranking ---------------------------------------------------------------------------------

struct CmpResult {
	size_t index = 0;              // in the list ranked
	int    score = 0;
	int    recent = -1;            // its place among the recently used (0: the last used), -1 if not
	uint32_t length = 0;           // its label's, for the order
	std::vector<uint16_t> positions;
};

// Orders results: by score, then the recently used first (the latest first), then shorter labels (byLength), then by
// index. With an empty pattern scores are equal: without byLength the recently used come first, then the list in its
// order.
void CmpSort(std::vector<CmpResult>& results, bool byLength);
bool CmpLess(const CmpResult& a, const CmpResult& b, bool byLength);   // CmpSort's order

// A search through a list in steps, for lists too long to go through at once: each step matches the next items, the
// results so far can be shown in between.
class CmpSearch {
public:
	// filter: match the pattern; else every item is a result, in the list's order
	void Start(std::wstring_view pattern, size_t count, bool filter = true);
	void Stop();
	bool Running() const {
		return m_Next < m_Count;
	}
	size_t Done() const {          // the items gone through
		return m_Next;
	}
	size_t Count() const {
		return m_Count;
	}
	bool Filtering() const {
		return m_Filter && !m_Pattern.Empty();
	}
	// Goes through up to budget more items: label(i) is item i's label (a std::wstring_view; empty: skip it), recent(i)
	// its rank among the recently used (-1: none). True when the list is done.
	template<typename Label, typename Recent>
	bool Step(size_t budget, Label&& label, Recent&& recent) {
		size_t end = m_Next + std::min(budget, m_Count - m_Next);
		for (; m_Next < end; m_Next++) {
			std::wstring_view text = label(m_Next);
			if (text.empty())
				continue;
			CmpResult r;
			r.index = m_Next;
			r.length = (uint32_t)text.size();
			if (m_Filter && !m_Pattern.Match(text, r.score, &r.positions))
				continue;
			r.recent = recent(m_Next);
			m_AnyRecent = m_AnyRecent || r.recent >= 0;
			m_Results.push_back(std::move(r));
		}
		return !Running();
	}
	// The results so far, ordered (a list not filtered keeps its order). Each time only the new ones are sorted, then
	// merged with the others: a long search sorting after every step costs no more than once at the end.
	void Sort();
	std::vector<CmpResult>& Results() {
		return m_Results;
	}

private:
	CmpPattern m_Pattern;
	size_t m_Count = 0;
	size_t m_Next = 0;
	bool   m_Filter = true;
	bool   m_AnyRecent = false;
	size_t m_Sorted = 0;           // the results in order, the rest come after
	std::vector<CmpResult> m_Results;
};

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
