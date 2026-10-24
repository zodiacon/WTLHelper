#include "CommandPaletteModel.h"

#include <algorithm>
#include <climits>
#include <cwctype>

// ---- Matching -----------------------------------------------------------------------------

namespace {
	// the score's parts
	constexpr int MatchScore = 1;           // each character
	constexpr int WordStartBonus = 8;       // a character starting a word
	constexpr int RunBonus = 5;             // right after the previous one
	constexpr int CaseBonus = 2;            // a capital typed, on that capital
	constexpr int GapPenalty = 1;           // each character skipped between two
	constexpr int LeadPenaltyMax = 10;      // the characters before the first, a quarter each, at most this
	constexpr int None = INT_MIN / 4;

	void Lower(std::wstring& s) {
		if (!s.empty())
			::CharLowerBuffW(s.data(), (DWORD)s.size());
	}

	bool IsSeparator(wchar_t c) {
		return !(iswalnum(c) || (c >= 0xD800 && c <= 0xDFFF));
	}
}

bool CmpIsWordStart(std::wstring_view label, size_t index) {
	if (index == 0)
		return true;
	if (index >= label.size())
		return false;
	wchar_t prev = label[index - 1], c = label[index];
	if (IsSeparator(prev))
		return !IsSeparator(c);
	if (iswlower(prev) && iswupper(c))
		return true;
	return iswdigit(c) && !iswdigit(prev);
}

CmpPattern::CmpPattern(std::wstring_view text) {
	size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && iswspace(text[i]))
			i++;
		size_t start = i;
		while (i < text.size() && !iswspace(text[i]))
			i++;
		if (i > start) {
			Word word;
			word.text = text.substr(start, std::min<size_t>(i - start, 64));
			word.lower = word.text;
			Lower(word.lower);
			m_Words.push_back(std::move(word));
		}
	}
}

bool CmpPattern::Match(std::wstring_view label, int& score, std::vector<uint16_t>* positions) const {
	score = 0;
	if (positions)
		positions->clear();
	if (m_Words.empty())
		return true;
	label = label.substr(0, CmpMaxLength);
	m_Lower = label;
	Lower(m_Lower);
	const int m = (int)label.size();

	std::vector<uint16_t> found;
	for (auto& word : m_Words) {
		const int n = (int)word.lower.size();
		// quick: the characters in order at all?
		for (int i = 0, j = 0; i < n; i++, j++) {
			while (j < m && m_Lower[j] != word.lower[i])
				j++;
			if (j >= m)
				return false;
		}

		// best[i][j]: the best score with the word's first i+1 characters matched, the last at j. from: the column
		// of the character before.
		m_Score.assign((size_t)n * m, None);
		m_From.assign((size_t)n * m, -1);
		auto at = [&](std::vector<int>& v, int i, int j) -> int& { return v[(size_t)i * m + j]; };
		for (int i = 0; i < n; i++) {
			int run = None, runFrom = -1;   // the best of the row above at k < j, less the gap after it
			for (int j = 0; j < m; j++) {
				if (i > 0 && j > 0) {
					if (run > None)
						run -= GapPenalty;
					int prev = at(m_Score, i - 1, j - 1);
					if (prev > run) {
						run = prev;
						runFrom = j - 1;
					}
				}
				if (m_Lower[j] != word.lower[i])
					continue;
				int gain = MatchScore + (CmpIsWordStart(label, j) ? WordStartBonus : 0) +
					(iswupper(word.text[i]) && label[j] == word.text[i] ? CaseBonus : 0);
				if (i == 0) {
					at(m_Score, i, j) = gain - std::min(j / 4, LeadPenaltyMax);
					continue;
				}
				int best = None, from = -1;
				if (j > 0 && at(m_Score, i - 1, j - 1) > None) {
					best = at(m_Score, i - 1, j - 1) + RunBonus;
					from = j - 1;
				}
				if (run > best) {
					best = run;
					from = runFrom;
				}
				if (best > None) {
					at(m_Score, i, j) = best + gain;
					at(m_From, i, j) = from;
				}
			}
		}
		int bestJ = -1, best = None;
		for (int j = 0; j < m; j++) {
			if (at(m_Score, n - 1, j) > best) {
				best = at(m_Score, n - 1, j);
				bestJ = j;
			}
		}
		if (bestJ < 0)
			return false;
		score += best;
		if (positions) {
			for (int i = n - 1, j = bestJ; i >= 0 && j >= 0; i--) {
				found.push_back((uint16_t)j);
				j = at(m_From, i, j);
			}
		}
	}
	if (positions) {
		std::sort(found.begin(), found.end());
		found.erase(std::unique(found.begin(), found.end()), found.end());
		*positions = std::move(found);
	}
	return true;
}

// ---- Ranking ---------------------------------------------------------------------------------

void CmpSort(std::vector<CmpResult>& results, const std::vector<size_t>& lengths) {
	auto length = [&](size_t index) { return index < lengths.size() ? lengths[index] : 0; };
	std::sort(results.begin(), results.end(), [&](const CmpResult& a, const CmpResult& b) {
		if (a.score != b.score)
			return a.score > b.score;
		if (a.recent != b.recent) {
			if (a.recent < 0 || b.recent < 0)
				return a.recent >= 0;
			return a.recent < b.recent;
		}
		if (length(a.index) != length(b.index))
			return length(a.index) < length(b.index);
		return a.index < b.index;
	});
}

// ---- Recently used --------------------------------------------------------------------------

void CmpRecent::Use(UINT id) {
	Remove(id);
	m_Ids.insert(m_Ids.begin(), id);
	if (m_Ids.size() > m_Limit)
		m_Ids.resize(m_Limit);
}

int CmpRecent::Rank(UINT id) const {
	auto it = std::find(m_Ids.begin(), m_Ids.end(), id);
	return it == m_Ids.end() ? -1 : (int)(it - m_Ids.begin());
}

void CmpRecent::Remove(UINT id) {
	m_Ids.erase(std::remove(m_Ids.begin(), m_Ids.end(), id), m_Ids.end());
}

void CmpRecent::Clear() {
	m_Ids.clear();
}

void CmpRecent::SetLimit(size_t limit) {
	m_Limit = limit;
	if (m_Ids.size() > m_Limit)
		m_Ids.resize(m_Limit);
}

std::wstring CmpRecent::Save() const {
	std::wstring text;
	for (size_t i = 0; i < m_Ids.size(); i++) {
		if (i)
			text += L';';
		text += std::to_wstring(m_Ids[i]);
	}
	return text;
}

bool CmpRecent::Load(std::wstring_view text) {
	m_Ids.clear();
	size_t i = 0;
	while (i < text.size()) {
		unsigned long long value = 0;
		size_t start = i;
		while (i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
			value = value * 10 + (text[i] - L'0');
			if (value > UINT_MAX) {
				m_Ids.clear();
				return false;
			}
			i++;
		}
		if (i == start || (i < text.size() && text[i] != L';')) {
			m_Ids.clear();
			return false;
		}
		if (i < text.size())
			i++;   // the ';'
		UINT id = (UINT)value;
		if (std::find(m_Ids.begin(), m_Ids.end(), id) == m_Ids.end() && m_Ids.size() < m_Limit)
			m_Ids.push_back(id);
	}
	return true;
}

// ---- Menus ------------------------------------------------------------------------------------

void CmpParseMenuText(std::wstring_view text, std::wstring& name, std::wstring& shortcut) {
	name.clear();
	shortcut.clear();
	size_t tab = text.find(L'\t');
	auto label = text.substr(0, tab);
	for (size_t i = 0; i < label.size(); i++) {
		if (label[i] == L'&') {
			if (i + 1 < label.size() && label[i + 1] == L'&')
				name += L'&', i++;
			continue;
		}
		name += label[i];
	}
	if (tab != std::wstring_view::npos)
		shortcut = text.substr(tab + 1);
	auto trim = [](std::wstring& s) {
		size_t first = s.find_first_not_of(L" \t");
		if (first == std::wstring::npos) {
			s.clear();
			return;
		}
		s = s.substr(first, s.find_last_not_of(L" \t") - first + 1);
	};
	trim(name);
	trim(shortcut);
}

std::wstring CmpJoinPath(const std::vector<std::wstring>& path) {
	std::wstring text;
	for (auto& part : path) {
		if (!text.empty())
			text += L" > ";
		text += part;
	}
	return text;
}

// ---- Providers ----------------------------------------------------------------------------------

int CmpPickProvider(const std::vector<std::wstring>& prefixes, std::wstring_view text, std::wstring_view& rest) {
	int best = -1;
	size_t length = 0;
	for (size_t i = 0; i < prefixes.size(); i++) {
		auto& prefix = prefixes[i];
		if (text.substr(0, prefix.size()) == prefix && (best < 0 || prefix.size() > length)) {
			best = (int)i;
			length = prefix.size();
		}
	}
	rest = text.substr(best < 0 ? 0 : length);
	while (best >= 0 && !rest.empty() && iswspace(rest.front()))
		rest.remove_prefix(1);
	return best;
}
