#include "BreadcrumbModel.h"

#include <algorithm>

// ---- Segments ---------------------------------------------------------------------------------

BrcMetrics BrcMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	BrcMetrics m;
	m.height = s(height);
	m.navButton = s(navButton);
	m.recentButton = s(recentButton);
	m.navGap = s(navGap);
	m.border = std::max(1, dpi / 96 * border);
	m.icon = s(icon);
	m.iconPad = s(iconPad);
	m.textPad = s(textPad);
	m.chevron = s(chevron);
	m.minSegment = s(minSegment);
	m.minEmpty = s(minEmpty);
	m.boxButton = s(boxButton);
	return m;
}

void BrcMeasureSegments(std::vector<BrcSegment>& segments, const BrcMeasure& measure) {
	for (auto& s : segments)
		s.textWidth = measure(s.text);
}

int BrcTextPart(const BrcSegment& segment, const BrcMetrics& m) {
	return segment.textWidth + 2 * m.textPad;
}

int BrcChevronPart(const BrcSegment& segment, const BrcMetrics& m) {
	return segment.noChildren ? 0 : m.chevron;
}

// ---- Layout -------------------------------------------------------------------------------------

namespace {
	RECT Rect(int left, int top, int right, int bottom) {
		return RECT{ left, top, std::max(left, right), std::max(top, bottom) };
	}
}

BrcLayout BrcLayoutBar(const std::vector<BrcSegment>& segments, const BrcParts& parts, int width, const BrcMetrics& m) {
	BrcLayout l;
	const int n = (int)segments.size();
	l.text.resize(n);
	l.chevron.resize(n);

	int x = 0;
	if (parts.nav) {
		l.back = Rect(x, 0, x + m.navButton, m.height);
		x += m.navButton;
		l.forward = Rect(x, 0, x + m.navButton, m.height);
		x += m.navButton;
		l.recent = Rect(x, 0, x + m.recentButton, m.height);
		x += m.recentButton;
	}
	if (parts.up) {
		l.up = Rect(x, 0, x + m.navButton, m.height);
		x += m.navButton;
	}
	if (parts.nav || parts.up)
		x += m.navGap;

	l.box = Rect(x, 0, std::max(x, width), m.height);
	const int top = (int)l.box.top + m.border, bottom = (int)l.box.bottom - m.border;
	int left = (int)l.box.left + m.border;
	int right = std::max(left, (int)l.box.right - m.border);

	if (parts.icon) {
		const int y = top + (bottom - top - m.icon) / 2;
		l.icon = Rect(left + m.iconPad, y, left + m.iconPad + m.icon, y + m.icon);
		left += 2 * m.iconPad + m.icon;
	}

	l.actions.resize((size_t)std::max(0, parts.actions));
	for (int i = parts.actions - 1; i >= 0; i--) {
		l.actions[i] = Rect(right - m.boxButton, top, right, bottom);
		right -= m.boxButton;
	}
	if (parts.history) {
		l.history = Rect(right - m.boxButton, top, right, bottom);
		right -= m.boxButton;
	}
	right = std::max(right, left);
	l.edit = Rect(left, top, right, bottom);

	// the first segment shown with the segments starting at start, and the last one's text part
	int lastText = 0;
	auto fit = [&](int start) {
		const int avail = right - start - m.minEmpty;
		int used = 0, first = n;
		for (int i = n - 1; i >= 0; i--) {
			const int w = BrcTextPart(segments[i], m) + BrcChevronPart(segments[i], m);
			if (i < n - 1 && used + w > avail)
				break;
			used += w;
			first = i;
		}
		if (n > 0) {
			const auto& last = segments[n - 1];
			const int chev = BrcChevronPart(last, m);
			const int full = BrcTextPart(last, m);
			const int room = right - start - chev;
			lastText = std::max(0, std::min(full, std::max(avail - chev, std::min(m.minSegment, room))));
		}
		return first;
	};

	bool chevron = parts.rootChevron;
	int start = left + (chevron ? m.chevron : 0);
	int first = fit(start);
	if (!chevron && first > 0) {
		chevron = true;
		start = left + m.chevron;
		first = fit(start);
	}
	if (chevron)
		l.overflow = Rect(left, top, left + m.chevron, bottom);
	l.firstVisible = n == 0 ? 0 : first;

	x = start;
	for (int i = l.firstVisible; i < n; i++) {
		int w = BrcTextPart(segments[i], m);
		if (i == n - 1 && lastText < w) {
			w = lastText;
			l.truncated = true;
		}
		l.text[i] = Rect(x, top, x + w, bottom);
		x += w;
		if (const int c = BrcChevronPart(segments[i], m); c > 0) {
			l.chevron[i] = Rect(x, top, x + c, bottom);
			x += c;
		}
	}
	l.empty = Rect(std::min(x, right), top, right, bottom);
	return l;
}

std::vector<int> BrcFoldedSegments(const BrcLayout& layout) {
	std::vector<int> folded;
	folded.reserve(layout.firstVisible);
	for (int i = layout.firstVisible - 1; i >= 0; i--)
		folded.push_back(i);
	return folded;
}

// ---- Hit testing --------------------------------------------------------------------------------

BrcHit BrcHitTest(const BrcLayout& l, POINT pt) {
	auto in = [pt](const RECT& rc) {
		return ::PtInRect(&rc, pt) != FALSE;
	};
	if (in(l.back))
		return { BrcPart::Back };
	if (in(l.forward))
		return { BrcPart::Forward };
	if (in(l.recent))
		return { BrcPart::Recent };
	if (in(l.up))
		return { BrcPart::Up };
	if (!in(l.box))
		return {};
	if (in(l.icon))
		return { BrcPart::Icon };
	if (in(l.overflow))
		return { BrcPart::Overflow };
	for (int i = 0; i < (int)l.text.size(); i++) {
		if (in(l.text[i]))
			return { BrcPart::Text, i };
		if (in(l.chevron[i]))
			return { BrcPart::Chevron, i };
	}
	if (in(l.history))
		return { BrcPart::History };
	for (int i = 0; i < (int)l.actions.size(); i++)
		if (in(l.actions[i]))
			return { BrcPart::Action, i };
	// the rest of the box (the empty room, the icon's padding, the frame) edits
	return { BrcPart::Empty };
}

// ---- History ------------------------------------------------------------------------------------

namespace {
	bool StartsWith(std::wstring_view s, std::wstring_view prefix) {
		return s.size() >= prefix.size() && BrcSamePath(s.substr(0, prefix.size()), prefix);
	}

	std::wstring_view Trim(std::wstring_view s) {
		while (!s.empty() && iswspace(s.front()))
			s.remove_prefix(1);
		while (!s.empty() && iswspace(s.back()))
			s.remove_suffix(1);
		return s;
	}

	bool IsSeparator(wchar_t ch, std::wstring_view separators) {
		return separators.find(ch) != std::wstring_view::npos;
	}
}

bool BrcSamePath(std::wstring_view a, std::wstring_view b) {
	if (a.empty() || b.empty())
		return a.empty() && b.empty();
	return ::CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
}

void BrcHistory::Navigate(const BrcLocation& location) {
	if (m_Current >= 0 && BrcSamePath(m_Entries[m_Current].path, location.path)) {
		m_Entries[m_Current] = location;
		return;
	}
	m_Entries.erase(m_Entries.begin() + (m_Current + 1), m_Entries.end());
	m_Entries.push_back(location);
	if (m_Limit > 0 && m_Entries.size() > m_Limit)
		m_Entries.erase(m_Entries.begin(), m_Entries.begin() + (m_Entries.size() - m_Limit));
	m_Current = (int)m_Entries.size() - 1;
}

bool BrcHistory::CanGo(int offset) const {
	const int i = m_Current + offset;
	return m_Current >= 0 && i >= 0 && i < (int)m_Entries.size();
}

bool BrcHistory::Go(int offset) {
	if (!CanGo(offset))
		return false;
	m_Current += offset;
	return true;
}

void BrcHistory::Replace(const BrcLocation& location) {
	if (m_Current < 0)
		Navigate(location);
	else
		m_Entries[m_Current] = location;
}

const BrcLocation* BrcHistory::At(int offset) const {
	return CanGo(offset) ? &m_Entries[m_Current + offset] : nullptr;
}

std::vector<int> BrcHistory::MenuOffsets(int count) const {
	std::vector<int> offsets;
	if (m_Current < 0 || count <= 0)
		return offsets;
	const int back = m_Current, ahead = (int)m_Entries.size() - 1 - m_Current;
	int f = std::min(ahead, (count - 1) / 2);
	const int b = std::min(back, count - 1 - f);
	f = std::min(ahead, count - 1 - b);
	offsets.reserve(f + b + 1);
	for (int i = f; i >= -b; i--)
		offsets.push_back(i);
	return offsets;
}

void BrcHistory::Clear() {
	m_Entries.clear();
	m_Current = -1;
}

void BrcRecent::Add(std::wstring_view path) {
	path = Trim(path);
	if (path.empty())
		return;
	Remove(path);
	m_Paths.insert(m_Paths.begin(), std::wstring(path));
	if (m_Limit > 0 && m_Paths.size() > m_Limit)
		m_Paths.resize(m_Limit);
}

bool BrcRecent::Remove(std::wstring_view path) {
	auto it = std::find_if(m_Paths.begin(), m_Paths.end(), [path](auto& p) {
		return BrcSamePath(p, path);
	});
	if (it == m_Paths.end())
		return false;
	m_Paths.erase(it);
	return true;
}

std::wstring BrcRecent::Save() const {
	std::wstring text;
	for (auto& p : m_Paths) {
		if (!text.empty())
			text += L'\n';
		text += p;
	}
	return text;
}

void BrcRecent::Load(std::wstring_view text) {
	m_Paths.clear();
	while (!text.empty() && (m_Limit == 0 || m_Paths.size() < m_Limit)) {
		const auto end = text.find(L'\n');
		const auto line = Trim(text.substr(0, end));
		text = end == std::wstring_view::npos ? std::wstring_view() : text.substr(end + 1);
		if (line.empty())
			continue;
		if (std::none_of(m_Paths.begin(), m_Paths.end(), [line](auto& p) { return BrcSamePath(p, line); }))
			m_Paths.emplace_back(line);
	}
}

// ---- Path text --------------------------------------------------------------------------------

std::wstring BrcJoinPath(const std::vector<BrcSegment>& segments, std::wstring_view separators, size_t count) {
	const wchar_t sep = separators.empty() ? L'\\' : separators[0];
	std::wstring path;
	count = std::min(count, segments.size());
	for (size_t i = 0; i < count; i++) {
		const auto& name = segments[i].name;
		if (name.empty())
			continue;
		if (!path.empty() && !IsSeparator(path.back(), separators) && !IsSeparator(name.front(), separators))
			path += sep;
		path += name;
	}
	return path;
}

std::vector<std::wstring> BrcSplitPath(std::wstring_view text, std::wstring_view separators) {
	std::vector<std::wstring> parts;
	text = Trim(text);
	size_t i = 0;
	while (i < text.size() && IsSeparator(text[i], separators))
		i++;
	std::wstring prefix(text.substr(0, i));
	while (i < text.size()) {
		size_t j = i;
		while (j < text.size() && !IsSeparator(text[j], separators))
			j++;
		if (const auto part = Trim(text.substr(i, j - i)); !part.empty()) {
			if (parts.empty())
				parts.push_back(prefix + std::wstring(part));
			else
				parts.emplace_back(part);
		}
		i = j + 1;
	}
	if (parts.empty() && !prefix.empty())
		parts.push_back(prefix);
	return parts;
}

void BrcSplitLast(std::wstring_view text, std::wstring_view separators, std::wstring& parent, std::wstring& leaf) {
	const auto i = text.find_last_of(separators);
	if (i == std::wstring_view::npos) {
		parent.clear();
		leaf = text;
	}
	else {
		parent = text.substr(0, i + 1);
		leaf = text.substr(i + 1);
	}
}

std::vector<std::wstring> BrcSuggest(std::wstring_view typed, const std::vector<std::wstring>& candidates,
	const std::vector<std::wstring>& recent, size_t count) {
	std::vector<std::wstring> result;
	auto add = [&](const std::wstring& s) {
		if (result.size() < count && StartsWith(s, typed)
			&& std::none_of(result.begin(), result.end(), [&](auto& r) { return BrcSamePath(r, s); }))
			result.push_back(s);
	};
	for (auto& s : recent)
		add(s);
	if (!typed.empty())
		for (auto& s : candidates)
			add(s);
	return result;
}

std::wstring BrcCommonPrefix(std::wstring_view typed, const std::vector<std::wstring>& suggestions) {
	if (suggestions.empty() || !StartsWith(suggestions[0], typed))
		return std::wstring(typed);
	const auto& first = suggestions[0];
	size_t len = first.size();
	for (size_t k = 1; k < suggestions.size(); k++) {
		const auto& s = suggestions[k];
		size_t i = 0;
		while (i < len && i < s.size() && BrcSamePath(std::wstring_view(first).substr(i, 1), std::wstring_view(s).substr(i, 1)))
			i++;
		len = i;
	}
	return first.substr(0, std::max(len, typed.size()));
}
