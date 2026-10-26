#include "TokenEditModel.h"

#include <algorithm>
#include <cwctype>

namespace {
	bool SameText(std::wstring_view a, std::wstring_view b, bool ignoreCase = true) {
		if (a.size() != b.size())
			return false;
		if (a.empty())
			return true;
		return ::CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), ignoreCase) == CSTR_EQUAL;
	}

	bool IsBlank(wchar_t c) {
		return c == L' ' || c == L'\t';
	}

	bool IsLineBreak(wchar_t c) {
		return c == L'\r' || c == L'\n';
	}

	bool IsSeparator(wchar_t c, std::wstring_view separators) {
		return separators.find(c) != std::wstring_view::npos;
	}
}

// ---- Tokens -------------------------------------------------------------------------------------

UINT TkeTokenList::NewId() {
	while (Find(m_NextId) >= 0 || m_NextId == 0)
		m_NextId++;
	return m_NextId++;
}

size_t TkeTokenList::Insert(size_t at, TkeToken token) {
	at = std::min(at, m_Tokens.size());
	if (token.id == 0 || Find(token.id) >= 0)
		token.id = NewId();
	else
		m_NextId = std::max(m_NextId, token.id + 1);
	m_Tokens.insert(m_Tokens.begin() + at, std::move(token));
	if (m_Anchor >= (int)at)
		m_Anchor++;
	return at;
}

void TkeTokenList::Remove(size_t index) {
	if (index >= m_Tokens.size())
		return;
	m_Tokens.erase(m_Tokens.begin() + index);
	if (m_Anchor == (int)index)
		m_Anchor = -1;
	else if (m_Anchor > (int)index)
		m_Anchor--;
}

void TkeTokenList::Clear() {
	m_Tokens.clear();
	m_Anchor = -1;
}

int TkeTokenList::Find(UINT id) const {
	for (size_t i = 0; i < m_Tokens.size(); i++)
		if (m_Tokens[i].id == id)
			return (int)i;
	return -1;
}

int TkeTokenList::FindText(std::wstring_view text, bool ignoreCase) const {
	for (size_t i = 0; i < m_Tokens.size(); i++)
		if (SameText(m_Tokens[i].text, text, ignoreCase))
			return (int)i;
	return -1;
}

size_t TkeTokenList::Move(std::vector<size_t> indexes, size_t to) {
	std::sort(indexes.begin(), indexes.end());
	indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
	while (!indexes.empty() && indexes.back() >= m_Tokens.size())
		indexes.pop_back();
	to = std::min(to, m_Tokens.size());
	if (indexes.empty())
		return to;

	const UINT anchorId = m_Anchor >= 0 ? m_Tokens[m_Anchor].id : 0;
	std::vector<TkeToken> block;
	block.reserve(indexes.size());
	size_t before = 0;      // moved tokens before to
	for (auto i : indexes) {
		block.push_back(std::move(m_Tokens[i]));
		if (i < to)
			before++;
	}
	for (auto it = indexes.rbegin(); it != indexes.rend(); ++it)
		m_Tokens.erase(m_Tokens.begin() + *it);
	const size_t at = to - before;
	m_Tokens.insert(m_Tokens.begin() + at, std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
	if (anchorId)
		m_Anchor = Find(anchorId);
	return at;
}

size_t TkeTokenList::RemoveSelected() {
	const UINT anchorId = m_Anchor >= 0 ? m_Tokens[m_Anchor].id : 0;
	const size_t count = m_Tokens.size();
	std::erase_if(m_Tokens, [](auto& t) {
		return t.selected && !t.locked;
	});
	m_Anchor = anchorId ? Find(anchorId) : -1;
	return count - m_Tokens.size();
}

void TkeTokenList::Select(size_t index, TkeSelect how) {
	if (index >= m_Tokens.size())
		return;
	switch (how) {
		case TkeSelect::Only:
			SelectAll(false);
			m_Tokens[index].selected = true;
			m_Anchor = (int)index;
			break;

		case TkeSelect::Toggle:
			m_Tokens[index].selected = !m_Tokens[index].selected;
			m_Anchor = (int)index;
			break;

		case TkeSelect::Extend:
		case TkeSelect::ExtendAdd:
		{
			if (how == TkeSelect::Extend)
				SelectAll(false);
			if (m_Anchor < 0 || m_Anchor >= (int)m_Tokens.size())
				m_Anchor = (int)index;
			const size_t anchor = m_Anchor;     // minmax returns references: not to a temporary
			auto [first, last] = std::minmax(anchor, index);
			for (auto i = first; i <= last; i++)
				m_Tokens[i].selected = true;
			break;
		}
	}
}

void TkeTokenList::SelectAll(bool select) {
	for (auto& t : m_Tokens)
		t.selected = select;
}

std::vector<size_t> TkeTokenList::Selection() const {
	std::vector<size_t> selection;
	for (size_t i = 0; i < m_Tokens.size(); i++)
		if (m_Tokens[i].selected)
			selection.push_back(i);
	return selection;
}

size_t TkeTokenList::SelectedCount() const {
	return std::count_if(m_Tokens.begin(), m_Tokens.end(), [](auto& t) {
		return t.selected;
	});
}

TkeTokenList::State TkeTokenList::Save() const {
	return State{ m_Tokens, m_NextId, m_Anchor };
}

void TkeTokenList::Restore(const State& state) {
	m_Tokens = state.tokens;
	m_NextId = state.nextId;
	m_Anchor = state.anchor < (int)m_Tokens.size() ? state.anchor : -1;
}

// ---- Text ---------------------------------------------------------------------------------------

namespace {
	// a piece trimmed, and unquoted if it's all one quoted string
	std::wstring Piece(std::wstring_view raw) {
		while (!raw.empty() && IsBlank(raw.front()))
			raw.remove_prefix(1);
		while (!raw.empty() && IsBlank(raw.back()))
			raw.remove_suffix(1);
		if (raw.size() < 2 || raw.front() != L'"')
			return std::wstring(raw);

		std::wstring text;
		text.reserve(raw.size());
		for (size_t i = 1; i < raw.size(); i++) {
			if (raw[i] != L'"') {
				text += raw[i];
				continue;
			}
			if (i + 1 < raw.size() && raw[i + 1] == L'"') {
				text += L'"';
				i++;
				continue;
			}
			// the closing quote: the whole piece only if it's the last character
			return i + 1 == raw.size() ? text : std::wstring(raw);
		}
		return std::wstring(raw);   // never closed
	}
}

std::vector<std::wstring> TkeSplit(std::wstring_view text, std::wstring_view separators, std::wstring* rest) {
	std::vector<std::wstring> pieces;
	auto add = [&](std::wstring_view raw) {
		auto piece = Piece(raw);
		if (!piece.empty())
			pieces.push_back(std::move(piece));
	};

	bool quoted = false;
	size_t start = 0;
	for (size_t i = 0; i < text.size(); i++) {
		const wchar_t c = text[i];
		if (c == L'"')
			quoted = !quoted;
		else if (IsLineBreak(c) || (!quoted && IsSeparator(c, separators))) {
			add(text.substr(start, i - start));
			start = i + 1;
			quoted = false;
		}
	}
	if (rest)
		rest->assign(text.substr(start));
	else
		add(text.substr(start));
	return pieces;
}

bool TkeHasSeparator(std::wstring_view text, std::wstring_view separators) {
	bool quoted = false;
	for (auto c : text) {
		if (c == L'"')
			quoted = !quoted;
		else if (IsLineBreak(c) || (!quoted && IsSeparator(c, separators)))
			return true;
	}
	return false;
}

std::wstring TkeJoin(const std::vector<std::wstring>& texts, std::wstring_view separators, std::wstring_view joiner) {
	std::wstring result;
	for (auto& text : texts) {
		if (!result.empty())
			result += joiner;
		bool quote = !text.empty() && (IsBlank(text.front()) || IsBlank(text.back()));
		for (auto c : text)
			if (c == L'"' || IsLineBreak(c) || IsSeparator(c, separators))
				quote = true;
		if (!quote) {
			result += text;
			continue;
		}
		result += L'"';
		for (auto c : text) {
			result += c;
			if (c == L'"')
				result += L'"';
		}
		result += L'"';
	}
	return result;
}

// ---- Layout -------------------------------------------------------------------------------------

TkeMetrics TkeMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	TkeMetrics m;
	m.padding = s(padding);
	m.tokenHeight = s(tokenHeight);
	m.gap = s(gap);
	m.lineGap = s(lineGap);
	m.textPad = s(textPad);
	m.icon = s(icon);
	m.iconPad = s(iconPad);
	m.close = s(close);
	m.closePad = s(closePad);
	m.minEdit = s(minEdit);
	m.midEdit = s(midEdit);
	m.minToken = s(minToken);
	return m;
}

void TkeMeasureTokens(std::vector<TkeToken>& tokens, const TkeMeasure& measure) {
	for (auto& t : tokens)
		t.textWidth = measure(t.text);
}

namespace {
	bool HasIcon(const TkeToken& token, bool icons) {
		return icons && token.image >= 0;
	}

	bool HasClose(const TkeToken& token, bool closeButtons) {
		return closeButtons && !token.locked;
	}
}

int TkeTokenWidth(const TkeToken& token, bool closeButton, bool icon, const TkeMetrics& m) {
	int width = HasIcon(token, icon) ? m.iconPad + m.icon + m.iconPad : m.textPad;
	width += token.textWidth;
	width += HasClose(token, closeButton) ? m.iconPad + m.close + m.closePad : m.textPad;
	return width;
}

int TkeLineTop(int line, const TkeMetrics& m) {
	return m.padding + line * (m.tokenHeight + m.lineGap);
}

TkeLayout TkeLayoutTokens(const std::vector<TkeToken>& tokens, const TkeLayoutInput& in, const TkeMetrics& m) {
	TkeLayout layout;
	const int count = (int)tokens.size();
	const int avail = std::max(1, in.width - 2 * m.padding);
	const int right = m.padding + avail;
	layout.editAt = in.editAt < 0 || in.editAt > count ? count : in.editAt;
	layout.tokens.resize(count);

	int x = m.padding, line = 0;
	// where something w wide goes: on this line if it fits or starts it, else the next
	auto place = [&](int w) {
		if (x > m.padding && x + w > right) {
			line++;
			x = m.padding;
		}
		const int left = x;
		x += w + m.gap;
		return left;
	};

	for (int i = 0; i <= count; i++) {
		if (i == layout.editAt) {
			const bool last = i == count;
			const int w = std::min(avail, std::max(in.editWidth, last ? m.minEdit : m.midEdit));
			const int left = place(w);
			const int top = TkeLineTop(line, m);
			layout.edit = { left, top, last ? right : left + w, top + m.tokenHeight };
			layout.editLine = line;
		}
		if (i == count)
			break;

		auto& token = tokens[i];
		auto& box = layout.tokens[i];
		int w = TkeTokenWidth(token, in.closeButtons, in.icons, m);
		if (w > avail) {
			w = std::max(avail, std::min(w, m.minToken));
			box.cut = true;
		}
		const int left = place(w);
		const int top = TkeLineTop(line, m);
		box.line = line;
		box.rc = { left, top, left + w, top + m.tokenHeight };
		int textLeft = left + m.textPad, textRight = box.rc.right - m.textPad;
		if (HasIcon(token, in.icons)) {
			const int y = top + (m.tokenHeight - m.icon) / 2;
			box.icon = { left + m.iconPad, y, left + m.iconPad + m.icon, y + m.icon };
			textLeft = box.icon.right + m.iconPad;
		}
		if (HasClose(token, in.closeButtons)) {
			const int y = top + (m.tokenHeight - m.close) / 2;
			box.close = { box.rc.right - m.closePad - m.close, y, box.rc.right - m.closePad, y + m.close };
			textRight = box.close.left - m.iconPad;
		}
		box.text = { textLeft, top, std::max(textLeft, textRight), top + m.tokenHeight };
		if (box.text.right - box.text.left < token.textWidth)
			box.cut = true;
	}
	layout.lines = line + 1;
	layout.height = TkeBoxHeight(layout.lines, 1, 0, m);
	return layout;
}

int TkeBoxHeight(int lines, int minLines, int maxLines, const TkeMetrics& m) {
	int n = std::max({ lines, minLines, 1 });
	if (maxLines > 0)
		n = std::min(n, maxLines);
	return 2 * m.padding + n * m.tokenHeight + (n - 1) * m.lineGap;
}

int TkeClampScroll(int scroll, int viewHeight, int contentHeight) {
	return std::max(0, std::min(scroll, contentHeight - viewHeight));
}

int TkeScrollToShow(int scroll, int top, int bottom, int viewHeight, int contentHeight) {
	if (bottom > scroll + viewHeight)
		scroll = bottom - viewHeight;
	if (top < scroll)
		scroll = top;
	return TkeClampScroll(scroll, viewHeight, contentHeight);
}

// ---- Hit testing --------------------------------------------------------------------------------

TkeHit TkeHitTest(const TkeLayout& layout, POINT pt) {
	for (int i = 0; i < (int)layout.tokens.size(); i++) {
		auto& box = layout.tokens[i];
		if (::PtInRect(&box.close, pt))
			return { TkePart::Close, i };
		if (::PtInRect(&box.rc, pt))
			return { TkePart::Token, i };
	}
	if (::PtInRect(&layout.edit, pt))
		return { TkePart::Edit, -1 };
	return {};
}

namespace {
	// the line nearest y, by the tokens' and the edit's rows
	int LineAt(const TkeLayout& layout, int y) {
		int best = layout.editLine, bestDistance = INT_MAX;
		auto consider = [&](const RECT& rc, int line) {
			const int distance = y < rc.top ? rc.top - y : y >= rc.bottom ? y - rc.bottom + 1 : 0;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = line;
			}
		};
		consider(layout.edit, layout.editLine);
		for (auto& box : layout.tokens)
			consider(box.rc, box.line);
		return best;
	}

	int Middle(const RECT& rc) {
		return (rc.left + rc.right) / 2;
	}
}

int TkeDropIndex(const TkeLayout& layout, POINT pt) {
	const int line = LineAt(layout, pt.y);
	int after = -1;     // after the line's last token
	for (int i = 0; i < (int)layout.tokens.size(); i++) {
		auto& box = layout.tokens[i];
		if (box.line != line)
			continue;
		if (Middle(box.rc) > pt.x)
			return i;
		after = i + 1;
	}
	return after >= 0 ? after : layout.editAt;
}

RECT TkeDropMark(const TkeLayout& layout, int index, const TkeMetrics& m) {
	const int count = (int)layout.tokens.size();
	const int half = std::max(1, m.gap / 4);
	int x;
	RECT row;
	if (count == 0) {
		row = layout.edit;
		x = row.left;
	}
	else if (index < count) {
		row = layout.tokens[index].rc;
		x = row.left - m.gap / 2;
		if (index > 0 && layout.tokens[index - 1].line == layout.tokens[index].line)
			x = (layout.tokens[index - 1].rc.right + row.left) / 2;
	}
	else {
		row = layout.tokens[count - 1].rc;
		x = row.right + m.gap / 2;
	}
	return { x - half, row.top, x + half, row.bottom };
}

int TkeLineNeighbor(const TkeLayout& layout, int index, int dir) {
	const int count = (int)layout.tokens.size();
	if (index < 0 || index > count || dir == 0)
		return -1;
	const RECT& from = index < count ? layout.tokens[index].rc : layout.edit;
	const int line = (index < count ? layout.tokens[index].line : layout.editLine) + (dir < 0 ? -1 : 1);
	const int x = Middle(from);

	int best = -1, bestDistance = INT_MAX;
	auto consider = [&](const RECT& rc, int itemLine, int item) {
		if (itemLine != line)
			return;
		const int distance = abs(Middle(rc) - x);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = item;
		}
	};
	for (int i = 0; i < count; i++)
		consider(layout.tokens[i].rc, layout.tokens[i].line, i);
	consider(layout.edit, layout.editLine, count);
	return best;
}

// ---- Suggestions --------------------------------------------------------------------------------

namespace {
	bool IsWordStart(std::wstring_view text, size_t i) {
		if (i == 0)
			return true;
		const wchar_t c = text[i], before = text[i - 1];
		if (std::iswspace(c))
			return false;
		if (!std::iswalnum(before))
			return true;
		return std::iswlower(before) && std::iswupper(c);
	}
}

TkeMatch TkeMatchText(std::wstring_view typed, std::wstring_view candidate, bool inside) {
	while (!typed.empty() && IsBlank(typed.front()))
		typed.remove_prefix(1);
	while (!typed.empty() && IsBlank(typed.back()))
		typed.remove_suffix(1);
	if (typed.empty() || typed.size() > candidate.size())
		return {};

	const int length = (int)typed.size();
	auto at = [&](size_t i) {
		return SameText(candidate.substr(i, typed.size()), typed);
	};
	const size_t last = candidate.size() - typed.size();
	if (at(0))
		return { TkeMatchKind::Prefix, 0, length };
	for (size_t i = 1; i <= last; i++)
		if (IsWordStart(candidate, i) && at(i))
			return { TkeMatchKind::Word, (int)i, length };
	if (inside) {
		for (size_t i = 1; i <= last; i++)
			if (at(i))
				return { TkeMatchKind::Inside, (int)i, length };
	}
	return {};
}

std::vector<TkeSuggestion> TkeFilterSuggestions(std::wstring_view typed, const std::vector<std::wstring>& candidates,
	const std::vector<std::wstring>& exclude, bool inside, size_t count) {
	std::vector<TkeSuggestion> kinds[3];
	for (int i = 0; i < (int)candidates.size(); i++) {
		auto match = TkeMatchText(typed, candidates[i], inside);
		if (match.kind == TkeMatchKind::None)
			continue;
		if (std::any_of(exclude.begin(), exclude.end(), [&](auto& text) {
			return SameText(text, candidates[i]);
			}))
			continue;
		kinds[(int)match.kind - 1].push_back({ i, match });
	}
	std::vector<TkeSuggestion> result;
	result.reserve(std::min(count, kinds[0].size() + kinds[1].size() + kinds[2].size()));
	for (auto& kind : kinds)
		for (auto& s : kind) {
			if (result.size() == count)
				return result;
			result.push_back(s);
		}
	return result;
}

// ---- Undo ---------------------------------------------------------------------------------------

void TkeHistory::Push(TkeTokenList::State before, TkeTokenList::State after) {
	m_Changes.resize(m_Done);
	m_Changes.push_back({ std::move(before), std::move(after) });
	if (m_Changes.size() > m_Limit)
		m_Changes.erase(m_Changes.begin());
	m_Done = m_Changes.size();
}

bool TkeHistory::Undo(TkeTokenList& tokens) {
	if (!CanUndo())
		return false;
	tokens.Restore(m_Changes[--m_Done].before);
	return true;
}

bool TkeHistory::Redo(TkeTokenList& tokens) {
	if (!CanRedo())
		return false;
	tokens.Restore(m_Changes[m_Done++].after);
	return true;
}

// ---- Drawing ------------------------------------------------------------------------------------

std::vector<POINT> TkeWave(int left, int right, int y, int amplitude, int period) {
	std::vector<POINT> points;
	if (right <= left || period <= 0)
		return points;
	points.reserve((right - left) / period + 2);
	int k = 0;
	for (int x = left; x < right; x += period, k++)
		points.push_back({ x, y + (k % 2 ? amplitude : -amplitude) });
	points.push_back({ right, y + (k % 2 ? amplitude : -amplitude) });
	return points;
}
