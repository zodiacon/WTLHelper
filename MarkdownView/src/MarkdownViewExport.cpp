// The markdown view's export: a document, or a range of it, as plain text, as HTML (and the CF_HTML clipboard
// format), and the range's source text; and finding text in it.

#include "MarkdownViewModel.h"

#include <windows.h>
#include <algorithm>
#include <climits>

namespace {

// which leaves (and how much of the first and last) a range covers
struct Clip {
	const MdvDocument& doc;
	std::vector<int> order;            // block -> its leaf's order, -1 for containers
	std::vector<int> minLeaf, maxLeaf; // the leaves in a block's subtree
	int first = 0, last = INT_MAX;
	size_t firstPos = 0, lastPos = MDV_NOPOS;

	Clip(const MdvDocument& d, const MdvRange& range) : doc(d), order(d.blocks.size(), -1),
		minLeaf(d.blocks.size(), INT_MAX), maxLeaf(d.blocks.size(), -1) {
		const auto leaves = d.Leaves();
		for (size_t i = 0; i < leaves.size(); i++)
			order[leaves[i]] = (int)i;
		Spans(0);
		if (range.first.block < 0)
			return;
		MdvTextPos a = range.first, b = range.last;
		if (order[b.block] < order[a.block] || order[b.block] == order[a.block] && b.pos < a.pos)
			std::swap(a, b);
		first = order[a.block];
		firstPos = a.pos;
		last = order[b.block];
		lastPos = b.pos;
		// ending at the start of a leaf takes nothing of it
		if (lastPos == 0 && last > first) {
			last--;
			lastPos = MDV_NOPOS;
		}
	}

	void Spans(int b) {
		if (order[b] >= 0)
			minLeaf[b] = maxLeaf[b] = order[b];
		for (int c : doc.blocks[b].children) {
			Spans(c);
			minLeaf[b] = std::min(minLeaf[b], minLeaf[c]);
			maxLeaf[b] = std::max(maxLeaf[b], maxLeaf[c]);
		}
	}

	bool Covers(int b) const {
		return maxLeaf[b] >= first && minLeaf[b] <= last;
	}

	// the part of a leaf's content in the range
	void Part(int b, size_t& a, size_t& e) const {
		const size_t len = doc.blocks[b].content.text.size();
		a = order[b] == first ? std::min(firstPos, len) : 0;
		e = order[b] == last ? std::min(lastPos, len) : len;
		if (e < a)
			e = a;
	}

	// whether a block's start is in the range (its list marker goes with it)
	bool StartsIn(int b) const {
		if (minLeaf[b] > maxLeaf[b])
			return true;
		return minLeaf[b] > first || minLeaf[b] == first && firstPos == 0;
	}
};

// ---- Plain text ----

std::wstring InlineText(const MdvInline& content, size_t a, size_t e) {
	std::wstring out;
	for (size_t i = a; i < e; i++) {
		if (content.text[i] == MdvObjectChar) {
			const int img = content.ImageAt(i);
			if (img >= 0)
				out += content.images[img].alt;
			continue;
		}
		out += content.text[i];
	}
	return out;
}

std::wstring Indent(const std::wstring& text, size_t width) {
	std::wstring out;
	const std::wstring pad(width, L' ');
	for (size_t i = 0; i < text.size(); i++) {
		out += text[i];
		if (text[i] == L'\n' && i + 1 < text.size() && text[i + 1] != L'\n')
			out += pad;
	}
	return out;
}

std::wstring Plain(const Clip& clip, int b) {
	auto& doc = clip.doc;
	auto& block = doc.blocks[b];
	if (!clip.Covers(b))
		return {};
	auto join = [&](const wchar_t* sep) {
		std::wstring out;
		for (int c : block.children) {
			std::wstring s = Plain(clip, c);
			if (s.empty())
				continue;
			if (!out.empty())
				out += sep;
			out += s;
		}
		return out;
	};
	switch (block.kind) {
		case MdvKind::Paragraph:
		case MdvKind::Heading:
		case MdvKind::Code:
		case MdvKind::Cell: {
			size_t a, e;
			clip.Part(b, a, e);
			return InlineText(block.content, a, e);
		}
		case MdvKind::Rule:
			return L"---";
		case MdvKind::List: {
			std::wstring out;
			for (int c : block.children) {
				std::wstring s = Plain(clip, c);
				if (s.empty())
					continue;
				if (!out.empty())
					out += block.tight ? L"\n" : L"\n\n";
				out += s;
			}
			return out;
		}
		case MdvKind::Item: {
			std::wstring s = join(doc.blocks[block.parent].tight ? L"\n" : L"\n\n");
			if (!clip.StartsIn(b))
				return s;
			auto& list = doc.blocks[block.parent];
			std::wstring marker = list.ordered ? std::to_wstring(block.number) + list.mark + L' ' : L"- ";
			if (block.task != MdvTask::None)
				marker += block.task == MdvTask::Checked ? L"[x] " : L"[ ] ";
			return marker + Indent(s, marker.size());
		}
		case MdvKind::Row:
			return join(L"\t");
		case MdvKind::Table:
			return join(L"\n");
		case MdvKind::Details: {
			size_t a, e;
			clip.Part(b, a, e);
			std::wstring out = clip.order[b] >= clip.first && clip.order[b] <= clip.last ? InlineText(block.content, a, e) : L"";
			std::wstring body = join(L"\n\n");
			if (!out.empty() && !body.empty())
				out += L"\n\n";
			return out + body;
		}
		case MdvKind::Footnote: {
			std::wstring s = join(L"\n\n");
			if (!clip.StartsIn(b))
				return s;
			const std::wstring marker = std::to_wstring(block.footnote) + L". ";
			return marker + Indent(s, marker.size());
		}
		default:
			return join(L"\n\n");
	}
}

// ---- HTML ----

void Escape(std::wstring& out, std::wstring_view s, bool attribute = false) {
	for (wchar_t ch : s) {
		switch (ch) {
			case L'&': out += L"&amp;"; break;
			case L'<': out += L"&lt;"; break;
			case L'>': out += L"&gt;"; break;
			case L'"': out += attribute ? L"&quot;" : L"\""; break;
			default: out += ch; break;
		}
	}
}

void InlineHtml(std::wstring& out, const MdvInline& content, size_t a, size_t e) {
	static const struct { uint16_t bit; const wchar_t* tag; } tags[] = {
		{ MdvBold, L"strong" }, { MdvItalic, L"em" }, { MdvStrike, L"del" }, { MdvUnderline, L"u" }, { MdvCode, L"code" },
		{ MdvMath, L"code" }, { MdvKbd, L"kbd" }, { MdvSub, L"sub" }, { MdvSup, L"sup" }, { MdvMark, L"mark" },
	};
	for (auto& run : content.runs) {
		const size_t ra = std::max(run.start, a), re = std::min(run.start + run.length, e);
		if (ra >= re)
			continue;
		if (run.link >= 0) {
			auto& link = content.links[run.link];
			out += L"<a href=\"";
			Escape(out, link.url, true);
			out += L'"';
			if (!link.title.empty()) {
				out += L" title=\"";
				Escape(out, link.title, true);
				out += L'"';
			}
			out += L'>';
		}
		for (auto& t : tags)
			if (run.style & t.bit && !(t.bit == MdvMath && run.style & MdvCode))
				out += std::wstring(L"<") + t.tag + L">";
		for (size_t i = ra; i < re; i++) {
			const wchar_t ch = content.text[i];
			if (ch == MdvObjectChar) {
				const int img = content.ImageAt(i);
				if (img < 0)
					continue;
				auto& image = content.images[img];
				out += L"<img src=\"";
				Escape(out, image.src, true);
				out += L"\" alt=\"";
				Escape(out, image.alt, true);
				out += L'"';
				if (!image.title.empty()) {
					out += L" title=\"";
					Escape(out, image.title, true);
					out += L'"';
				}
				if (image.width)
					out += L" width=\"" + std::to_wstring(image.width) + L'"';
				if (image.height)
					out += L" height=\"" + std::to_wstring(image.height) + L'"';
				out += L'>';
			}
			else if (ch == L'\n' && !(run.style & MdvCode))
				out += L"<br>";
			else
				Escape(out, std::wstring_view(&ch, 1));
		}
		for (size_t i = std::size(tags); i-- > 0;)
			if (run.style & tags[i].bit && !(tags[i].bit == MdvMath && run.style & MdvCode))
				out += std::wstring(L"</") + tags[i].tag + L">";
		if (run.link >= 0)
			out += L"</a>";
	}
}

const wchar_t* AlignAttribute(MdvAlign align) {
	switch (align) {
		case MdvAlign::Left: return L"left";
		case MdvAlign::Center: return L"center";
		case MdvAlign::Right: return L"right";
		default: return nullptr;
	}
}

const wchar_t* AlertTitle(MdvAlert alert) {
	switch (alert) {
		case MdvAlert::Note: return L"Note";
		case MdvAlert::Tip: return L"Tip";
		case MdvAlert::Important: return L"Important";
		case MdvAlert::Warning: return L"Warning";
		case MdvAlert::Caution: return L"Caution";
		default: return L"";
	}
}

void Html(std::wstring& out, const Clip& clip, int b) {
	auto& block = clip.doc.blocks[b];
	if (!clip.Covers(b))
		return;
	auto children = [&] {
		for (int c : block.children)
			Html(out, clip, c);
	};
	size_t a = 0, e = 0;
	if (block.HasContent())
		clip.Part(b, a, e);
	switch (block.kind) {
		case MdvKind::Paragraph:
			out += L"<p";
			if (auto align = AlignAttribute(block.align))
				out += std::wstring(L" align=\"") + align + L'"';
			out += L'>';
			InlineHtml(out, block.content, a, e);
			out += L"</p>\n";
			break;
		case MdvKind::Heading: {
			const std::wstring tag = L"h" + std::to_wstring(block.level);
			out += L"<" + tag;
			if (!block.anchor.empty()) {
				out += L" id=\"";
				Escape(out, block.anchor, true);
				out += L'"';
			}
			if (auto align = AlignAttribute(block.align))
				out += std::wstring(L" align=\"") + align + L'"';
			out += L'>';
			InlineHtml(out, block.content, a, e);
			out += L"</" + tag + L">\n";
			break;
		}
		case MdvKind::Code:
			out += L"<pre><code";
			if (!block.lang.empty()) {
				out += L" class=\"language-";
				Escape(out, block.lang, true);
				out += L'"';
			}
			out += L'>';
			Escape(out, std::wstring_view(block.content.text).substr(a, e - a));
			out += L"</code></pre>\n";
			break;
		case MdvKind::Rule:
			out += L"<hr>\n";
			break;
		case MdvKind::Quote:
			out += L"<blockquote>\n";
			children();
			out += L"</blockquote>\n";
			break;
		case MdvKind::Alert:
			out += L"<blockquote>\n<p><strong>";
			out += AlertTitle(block.alert);
			out += L"</strong></p>\n";
			children();
			out += L"</blockquote>\n";
			break;
		case MdvKind::List:
			if (block.ordered) {
				out += L"<ol";
				if (block.start != 1)
					out += L" start=\"" + std::to_wstring(block.start) + L'"';
				out += L">\n";
			}
			else
				out += L"<ul>\n";
			children();
			out += block.ordered ? L"</ol>\n" : L"</ul>\n";
			break;
		case MdvKind::Item:
			out += L"<li>";
			if (block.task != MdvTask::None && clip.StartsIn(b))
				out += block.task == MdvTask::Checked ? L"<input type=\"checkbox\" disabled checked> " : L"<input type=\"checkbox\" disabled> ";
			children();
			out += L"</li>\n";
			break;
		case MdvKind::Table: {
			out += L"<table>\n";
			bool head = false, body = false;
			for (int c : block.children) {
				if (!clip.Covers(c))
					continue;
				auto& row = clip.doc.blocks[c];
				if (row.header && !head) {
					out += L"<thead>\n";
					head = true;
				}
				if (!row.header && !body) {
					if (head)
						out += L"</thead>\n";
					out += L"<tbody>\n";
					body = true;
				}
				Html(out, clip, c);
			}
			out += body ? L"</tbody>\n" : head ? L"</thead>\n" : L"";
			out += L"</table>\n";
			break;
		}
		case MdvKind::Row:
			out += L"<tr>";
			children();
			out += L"</tr>\n";
			break;
		case MdvKind::Cell: {
			const wchar_t* tag = block.header ? L"th" : L"td";
			out += std::wstring(L"<") + tag;
			if (auto align = AlignAttribute(block.align))
				out += std::wstring(L" style=\"text-align:") + align + L'"';
			out += L'>';
			InlineHtml(out, block.content, a, e);
			out += std::wstring(L"</") + tag + L">";
			break;
		}
		case MdvKind::Details:
			out += block.open ? L"<details open>\n<summary>" : L"<details>\n<summary>";
			InlineHtml(out, block.content, a, e);
			out += L"</summary>\n";
			children();
			out += L"</details>\n";
			break;
		case MdvKind::Footnotes:
			out += L"<section class=\"footnotes\">\n<ol>\n";
			children();
			out += L"</ol>\n</section>\n";
			break;
		case MdvKind::Footnote:
			out += L"<li id=\"";
			Escape(out, block.anchor, true);
			out += L"\">\n";
			children();
			out += L"</li>\n";
			break;
		default:
			children();
			break;
	}
}

}  // namespace

std::wstring MdvPlainText(const MdvDocument& doc, const MdvRange& range) {
	if (doc.blocks.empty())
		return {};
	Clip clip(doc, range);
	return Plain(clip, 0);
}

std::wstring MdvHtml(const MdvDocument& doc, const MdvRange& range) {
	std::wstring out;
	if (doc.blocks.empty())
		return out;
	Clip clip(doc, range);
	Html(out, clip, 0);
	return out;
}

std::string MdvCfHtml(std::wstring_view fragment) {
	std::string utf8;
	if (!fragment.empty()) {
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, fragment.data(), (int)fragment.size(), nullptr, 0, nullptr, nullptr);
		utf8.resize(n);
		::WideCharToMultiByte(CP_UTF8, 0, fragment.data(), (int)fragment.size(), utf8.data(), n, nullptr, nullptr);
	}
	const char header[] = "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n";
	const char prefix[] = "<html><body>\r\n<!--StartFragment-->";
	const char suffix[] = "<!--EndFragment-->\r\n</body></html>";
	char buf[160];
	const size_t headerLength = (size_t)snprintf(buf, sizeof(buf), header, (size_t)0, (size_t)0, (size_t)0, (size_t)0);
	const size_t startHtml = headerLength;
	const size_t startFragment = startHtml + strlen(prefix);
	const size_t endFragment = startFragment + utf8.size();
	const size_t endHtml = endFragment + strlen(suffix);
	snprintf(buf, sizeof(buf), header, startHtml, endHtml, startFragment, endFragment);
	return buf + std::string(prefix) + utf8 + suffix;
}

void MdvSourceRange(const MdvDocument& doc, const MdvRange& range, size_t& start, size_t& end) {
	const auto& source = doc.source;
	if (range.first.block < 0) {
		start = 0;
		end = source.size();
		return;
	}
	auto at = [&](const MdvTextPos& p) {
		auto& block = doc.blocks[p.block];
		if (!block.HasContent() || block.content.map.empty())
			return p.pos == 0 ? block.sourceStart : block.sourceEnd;
		return block.content.SourceOf(p.pos);
	};
	MdvTextPos a = range.first, b = range.last;
	start = at(a);
	end = at(b);
	if (end < start) {
		std::swap(start, end);
		std::swap(a, b);
	}
	start = std::min(start, source.size());
	end = std::min(end, source.size());
	// a leaf taken from its start brings its line's markers ("- ", "# ", "> "); one taken to its end, the rest of its line
	if (a.pos == 0)
		while (start > 0 && source[start - 1] != L'\n')
			start--;
	auto& last = doc.blocks[b.block];
	if (!last.HasContent() || b.pos >= last.content.text.size())
		while (end < source.size() && source[end] != L'\n' && source[end] != L'\r')
			end++;
}

// ---- Finding ---------------------------------------------------------------------------

namespace {

bool IsWordChar(wchar_t ch) {
	return iswalnum(ch) || ch == L'_';
}

std::wstring Fold(std::wstring_view s) {
	std::wstring out(s);
	if (!s.empty())
		::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(), (int)s.size(), out.data(), (int)out.size(), nullptr, nullptr, 0);
	return out;
}

}  // namespace

std::vector<MdvMatch> MdvFindAll(const MdvDocument& doc, std::wstring_view text, unsigned flags, size_t limit) {
	std::vector<MdvMatch> out;
	if (text.empty() || doc.blocks.empty())
		return out;
	const bool caseless = !(flags & MdvMatchCase);
	const std::wstring needle = caseless ? Fold(text) : std::wstring(text);
	for (int b : doc.TextLeaves()) {
		const std::wstring& raw = doc.blocks[b].content.text;
		if (raw.size() < needle.size())
			continue;
		const std::wstring folded = caseless ? Fold(raw) : std::wstring();
		const std::wstring& hay = caseless ? folded : raw;
		for (size_t at = hay.find(needle); at != std::wstring::npos; at = hay.find(needle, at + 1)) {
			if (flags & MdvWholeWord) {
				const bool before = at > 0 && IsWordChar(raw[at - 1]);
				const bool after = at + needle.size() < raw.size() && IsWordChar(raw[at + needle.size()]);
				if (before || after)
					continue;
			}
			out.push_back({ b, at, needle.size() });
			if (out.size() >= limit)
				return out;
		}
	}
	return out;
}

bool MdvFindNext(const MdvDocument& doc, std::wstring_view text, unsigned flags, const MdvTextPos& from, bool backwards, MdvMatch& match) {
	const auto all = MdvFindAll(doc, text, flags);
	if (all.empty())
		return false;
	// the leaves' order
	std::vector<int> order(doc.blocks.size(), -1);
	const auto leaves = doc.TextLeaves();
	for (size_t i = 0; i < leaves.size(); i++)
		order[leaves[i]] = (int)i;
	const int o = from.block >= 0 && from.block < (int)order.size() ? order[from.block] : -1;
	auto before = [&](const MdvMatch& m) {        // the match starts before from
		const int mo = order[m.block];
		return mo < o || (mo == o && m.start < from.pos);
	};
	if (!backwards) {
		for (auto& m : all)
			if (!before(m)) {
				match = m;
				return true;
			}
		match = all.front();
		return true;
	}
	for (size_t i = all.size(); i-- > 0;)
		if (before(all[i])) {
			match = all[i];
			return true;
		}
	match = all.back();
	return true;
}

void MdvWordAt(std::wstring_view text, size_t pos, size_t& start, size_t& end) {
	start = end = std::min(pos, text.size());
	if (text.empty())
		return;
	if (pos >= text.size())
		pos = text.size() - 1;
	auto kind = [](wchar_t ch) { return IsWordChar(ch) ? 0 : (ch == L' ' || ch == L'\t') ? 1 : ch == L'\n' ? 3 : 2; };
	const int k = kind(text[pos]);
	if (k == 3) {
		start = pos;
		end = pos + 1;
		return;
	}
	start = pos;
	while (start > 0 && kind(text[start - 1]) == k)
		start--;
	end = pos + 1;
	while (end < text.size() && kind(text[end]) == k)
		end++;
}
