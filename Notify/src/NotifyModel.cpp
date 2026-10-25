#include "NotifyModel.h"

#include <algorithm>
#include <climits>

// ---- Text with links ----------------------------------------------------------------------------

namespace {
	// &lt; &gt; &amp; &quot; at s[i]: the character, i past it
	bool Entity(std::wstring_view s, size_t& i, std::wstring& out) {
		static const struct {
			std::wstring_view name;
			wchar_t ch;
		} Entities[] = { { L"&lt;", L'<' }, { L"&gt;", L'>' }, { L"&amp;", L'&' }, { L"&quot;", L'"' } };
		for (auto& e : Entities)
			if (s.substr(i, e.name.size()) == e.name) {
				out += e.ch;
				i += e.name.size();
				return true;
			}
		return false;
	}

	std::wstring Decode(std::wstring_view s) {
		std::wstring out;
		for (size_t i = 0; i < s.size();)
			if (s[i] != L'&' || !Entity(s, i, out))
				out += s[i++];
		return out;
	}

	bool SameNoCase(std::wstring_view a, std::wstring_view b) {
		return a.size() == b.size() && ::CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
	}

	size_t FindNoCase(std::wstring_view s, std::wstring_view what, size_t from) {
		for (size_t i = from; i + what.size() <= s.size(); i++)
			if (SameNoCase(s.substr(i, what.size()), what))
				return i;
		return std::wstring_view::npos;
	}

	// "<a>" or "<a id="x" ...>" (href too, when there's no id): true, and the id
	bool LinkTag(std::wstring_view tag, std::wstring& id) {
		if (tag.size() < 3 || tag.front() != L'<' || tag.back() != L'>' || (tag[1] != L'a' && tag[1] != L'A'))
			return false;
		if (tag.size() > 3 && !iswspace(tag[2]))
			return false;   // <abbr>
		std::wstring href;
		id.clear();
		size_t i = 2;
		const size_t end = tag.size() - 1;
		while (i < end) {
			while (i < end && iswspace(tag[i]))
				i++;
			size_t n = i;
			while (n < end && tag[n] != L'=' && !iswspace(tag[n]))
				n++;
			auto name = tag.substr(i, n - i);
			i = n;
			while (i < end && iswspace(tag[i]))
				i++;
			std::wstring value;
			if (i < end && tag[i] == L'=') {
				i++;
				while (i < end && iswspace(tag[i]))
					i++;
				if (i < end && (tag[i] == L'"' || tag[i] == L'\'')) {
					wchar_t quote = tag[i++];
					size_t close = tag.find(quote, i);
					if (close == std::wstring_view::npos || close > end)
						return false;
					value = Decode(tag.substr(i, close - i));
					i = close + 1;
				}
				else {
					size_t v = i;
					while (i < end && !iswspace(tag[i]))
						i++;
					value = Decode(tag.substr(v, i - v));
				}
			}
			if (SameNoCase(name, L"id"))
				id = value;
			else if (SameNoCase(name, L"href"))
				href = value;
			if (name.empty() && i < end)
				i++;   // a stray character
		}
		if (id.empty())
			id = href;
		return true;
	}
}

std::vector<NtfRun> NtfParseLinks(std::wstring_view s) {
	std::vector<NtfRun> runs;
	std::wstring plain;
	auto flush = [&] {
		if (!plain.empty()) {
			runs.push_back({ plain });
			plain.clear();
		}
	};
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] == L'&' && Entity(s, i, plain))
			continue;
		if (s[i] == L'<') {
			size_t close = s.find(L'>', i);
			std::wstring id;
			if (close != std::wstring_view::npos && LinkTag(s.substr(i, close - i + 1), id)) {
				size_t end = FindNoCase(s, L"</a>", close + 1);
				if (end != std::wstring_view::npos) {
					auto text = Decode(s.substr(close + 1, end - close - 1));
					if (!text.empty()) {
						flush();
						runs.push_back({ text, id.empty() ? text : id });
					}
					i = end + 4;
					continue;
				}
			}
		}
		plain += s[i++];
	}
	flush();
	return runs;
}

std::wstring NtfPlainText(const std::vector<NtfRun>& runs) {
	std::wstring text;
	for (auto& r : runs)
		text += r.text;
	return text;
}

NtfTextLayout NtfWrap(const std::vector<NtfRun>& runs, int width, const NtfMeasure& measure, int maxLines) {
	NtfTextLayout l;
	width = std::max(width, 1);
	int x = 0, line = 0;
	bool any = false;
	auto add = [&](int run, size_t start, size_t length, int w) {
		if (!l.pieces.empty()) {
			auto& p = l.pieces.back();
			if (p.run == run && p.line == line && p.start + p.length == start) {
				p.length += length;
				p.width += w;
				x += w;
				return;
			}
		}
		l.pieces.push_back({ run, start, length, x, line, w });
		x += w;
	};
	// false: past maxLines
	auto newLine = [&] {
		line++;
		x = 0;
		if (maxLines > 0 && line >= maxLines) {
			l.truncated = true;
			return false;
		}
		return true;
	};
	for (int r = 0; r < (int)runs.size(); r++) {
		const std::wstring_view t = runs[r].text;
		const bool bold = runs[r].bold;
		size_t i = 0;
		while (i < t.size()) {
			any = true;
			if (t[i] == L'\n') {
				i++;
				if (!newLine())
					goto done;
				continue;
			}
			// a word and the spaces after it
			size_t j = i;
			while (j < t.size() && t[j] != L' ' && t[j] != L'\n')
				j++;
			size_t k = j;
			while (k < t.size() && t[k] == L' ')
				k++;
			const int word = j > i ? measure(t.substr(i, j - i), bold) : 0;
			if (x > 0 && x + word > width && !newLine())
				goto done;
			if (word > width) {
				// wider than a line: broken where it must be
				size_t a = i;
				while (a < j) {
					size_t n = 1;
					while (a + n < j && measure(t.substr(a, n + 1), bold) <= width)
						n++;
					if (a + n < j) {
						int w = measure(t.substr(a, n), bold);
						add(r, a, n, w);
						l.width = std::max(l.width, x);
						a += n;
						if (!newLine())
							goto done;
					}
					else {
						const int before = x;
						add(r, a, k - a, measure(t.substr(a, k - a), bold));
						l.width = std::max(l.width, before + measure(t.substr(a, j - a), bold));
						a = j;
					}
				}
			}
			else {
				const int before = x;
				add(r, i, k - i, k > j ? measure(t.substr(i, k - i), bold) : word);
				l.width = std::max(l.width, before + word);
			}
			i = k;
		}
	}
done:
	l.lines = any ? std::min(line + 1, maxLines > 0 ? maxLines : INT_MAX) : 0;
	return l;
}

int NtfLinkAt(const NtfTextLayout& layout, const std::vector<NtfRun>& runs, int x, int y, int lineHeight) {
	if (y < 0 || x < 0 || lineHeight <= 0)
		return -1;
	const int line = y / lineHeight;
	for (auto& p : layout.pieces)
		if (p.line == line && x >= p.x && x < p.x + p.width)
			return p.run < (int)runs.size() && runs[p.run].IsLink() ? p.run : -1;
	return -1;
}

// ---- Messages ------------------------------------------------------------------------------------

NtfMetrics NtfMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	NtfMetrics m;
	m.pad = s(pad);
	m.icon = s(icon);
	m.gap = s(gap);
	m.lineHeight = s(lineHeight);
	m.buttonHeight = s(buttonHeight);
	m.buttonPad = s(buttonPad);
	m.buttonMin = s(buttonMin);
	m.buttonGap = s(buttonGap);
	m.close = s(close);
	m.progress = std::max(2, s(progress));
	m.check = s(check);
	m.minText = s(minText);
	m.maxDetailsLines = maxDetailsLines;
	m.more = s(more);
	m.toastWidth = s(toastWidth);
	m.toastMargin = s(toastMargin);
	m.toastGap = s(toastGap);
	return m;
}

NtfRowLayout NtfLayoutMessage(const NtfMessage& m, NtfStyle style, int width, const NtfMetrics& mt, const NtfMeasure& measure,
	std::wstring_view toggle) {
	NtfRowLayout l;
	const bool toast = style == NtfStyle::Toast;
	const int top = mt.pad;
	int left = mt.pad, right = width - mt.pad;
	const int full = right;   // under the first lines, nothing's at the right
	const int firstLine = top + mt.lineHeight / 2;
	auto centered = [&](int size) {
		return std::max(0, firstLine - size / 2);
	};

	l.icon = { left, centered(mt.icon), left + mt.icon, centered(mt.icon) + mt.icon };
	left += mt.icon + mt.gap;
	if (m.closable) {
		const int y = centered(mt.close);
		l.close = { right - mt.close, y, right, y + mt.close };
		right -= mt.close + mt.gap / 2;
	}

	// the title, the text, the details toggle
	if (!m.title.empty()) {
		l.textRuns.push_back({ m.title, L"", true });
		if (!m.runs.empty())
			l.textRuns.push_back({ toast ? L"\n" : L" " });
	}
	l.textRuns.insert(l.textRuns.end(), m.runs.begin(), m.runs.end());
	if (!m.details.empty()) {
		l.textRuns.push_back({ L"  " });
		l.textRuns.push_back({ std::wstring(toggle), NtfDetailsLink });
	}

	std::vector<int> widths;
	widths.reserve(m.buttons.size());
	int total = 0;
	for (auto& b : m.buttons) {
		widths.push_back(std::max(mt.buttonMin, measure(b.text, false) + 2 * mt.buttonPad));
		total += widths.back() + (widths.size() > 1 ? mt.buttonGap : 0);
	}
	int textWidth = std::max(1, right - left);
	if (!toast && !m.buttons.empty() && textWidth - total - mt.gap >= mt.minText) {
		l.buttonsInline = true;
		textWidth -= total + mt.gap;
	}
	l.textLayout = NtfWrap(l.textRuns, textWidth, measure);
	l.text = { left, top, left + textWidth, top + l.textLayout.lines * mt.lineHeight };
	int y = (int)l.text.bottom;
	int bottom = std::max({ y, (int)l.icon.bottom, (int)l.close.bottom });
	l.buttons.reserve(m.buttons.size());
	if (l.buttonsInline) {
		int x = (int)l.text.right + mt.gap;
		const int by = centered(mt.buttonHeight);
		for (int w : widths) {
			l.buttons.push_back({ x, by, x + w, by + mt.buttonHeight });
			x += w + mt.buttonGap;
		}
		bottom = std::max(bottom, by + mt.buttonHeight);
	}

	if (m.expanded && !m.details.empty()) {
		y += mt.gap / 2;
		l.detailsRuns.push_back({ m.details });
		l.detailsLayout = NtfWrap(l.detailsRuns, std::max(1, full - left), measure, mt.maxDetailsLines);
		l.details = { left, y, full, y + l.detailsLayout.lines * mt.lineHeight };
		y = (int)l.details.bottom;
	}
	if (m.progressKind != NtfProgress::None) {
		y += mt.gap;
		l.progress = { left, y, full, y + mt.progress };
		y += mt.progress;
	}
	if (!m.checkText.empty()) {
		y += mt.gap / 2;
		const int cy = y + (mt.lineHeight - mt.check) / 2;
		l.checkBox = { left, cy, left + mt.check, cy + mt.check };
		l.check = { left, y, left + mt.check + mt.gap / 2 + measure(m.checkText, false), y + mt.lineHeight };
		y += mt.lineHeight;
	}
	if (!m.buttons.empty() && !l.buttonsInline) {
		y += mt.gap;
		// an info bar's under the text, a toast's at its right
		int x = toast ? full - total : left;
		for (int w : widths) {
			l.buttons.push_back({ x, y, x + w, y + mt.buttonHeight });
			x += w + mt.buttonGap;
		}
		y += mt.buttonHeight;
	}
	l.height = std::max(bottom, y) + mt.pad;
	return l;
}

// ---- The info bar's stack ------------------------------------------------------------------------

NtfStackLayout NtfLayoutStack(const std::vector<int>& heights, int maxRows, int width, int moreHeight, int separator) {
	NtfStackLayout l;
	const int n = (int)heights.size();
	const int shown = maxRows > 0 ? std::min(n, maxRows) : n;
	int y = 0;
	l.tops.reserve(shown);
	for (int i = 0; i < shown; i++) {
		if (i > 0)
			y += separator;
		l.tops.push_back(y);
		y += heights[i];
	}
	l.hidden = n - shown;
	if (l.hidden > 0) {
		if (shown > 0)
			y += separator;
		l.more = { 0, y, width, y + moreHeight };
		y += moreHeight;
	}
	l.height = y;
	return l;
}

// ---- Toasts ----------------------------------------------------------------------------------------

std::vector<RECT> NtfPlaceToasts(const RECT& area, NtfCorner corner, const std::vector<int>& heights, int width, int margin,
	int gap, int maxShown) {
	std::vector<RECT> rects(heights.size());
	const int w = std::max(0, std::min(width, (int)(area.right - area.left) - 2 * margin));
	const bool right = corner == NtfCorner::TopRight || corner == NtfCorner::BottomRight;
	const bool bottom = corner == NtfCorner::BottomLeft || corner == NtfCorner::BottomRight;
	const int x = right ? (int)area.right - margin - w : (int)area.left + margin;
	const int limit = maxShown > 0 ? maxShown : INT_MAX;
	int y = bottom ? (int)area.bottom - margin : (int)area.top + margin;
	for (int i = 0; i < (int)heights.size(); i++) {
		const int h = heights[i];
		if (bottom) {
			const int top = y - h;
			if (i > 0 && (i >= limit || top < area.top + margin))
				break;   // it waits, and the older ones after it
			rects[i] = { x, top, x + w, y };
			y = top - gap;
		}
		else {
			const int end = y + h;
			if (i > 0 && (i >= limit || end > area.bottom - margin))
				break;
			rects[i] = { x, y, x + w, end };
			y = end + gap;
		}
	}
	return rects;
}

void NtfTimer::Start(ULONGLONG now, UINT duration) {
	m_Duration = duration;
	m_Used = 0;
	m_RunStart = now;
	m_Paused = false;
}

void NtfTimer::Pause(ULONGLONG now) {
	if (!m_Paused) {
		m_Used += now - m_RunStart;
		m_Paused = true;
	}
}

void NtfTimer::Resume(ULONGLONG now) {
	if (m_Paused) {
		m_RunStart = now;
		m_Paused = false;
	}
}

UINT NtfTimer::Remaining(ULONGLONG now) const {
	if (m_Duration == 0)
		return UINT_MAX;
	const ULONGLONG used = m_Used + (m_Paused || now < m_RunStart ? 0 : now - m_RunStart);
	return used >= m_Duration ? 0 : (UINT)(m_Duration - used);
}

bool NtfTimer::Expired(ULONGLONG now) const {
	return m_Duration != 0 && Remaining(now) == 0;
}

// ---- Animation -------------------------------------------------------------------------------------

double NtfAnimate(ULONGLONG start, UINT duration, ULONGLONG now) {
	if (duration == 0 || now >= start + duration)
		return 1.0;
	if (now <= start)
		return 0.0;
	const double t = 1.0 - (double)(now - start) / duration;
	return 1.0 - t * t * t;
}
