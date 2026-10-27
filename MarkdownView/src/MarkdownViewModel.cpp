// The markdown view's model: the block tree built from md4c's callbacks, inline content, GitHub's extras (alerts,
// footnotes, emoji, anchors, front matter, raw HTML's subset), and the table columns' widths.

#include "MarkdownViewModel.h"

#include "md4c/md4c.h"

#include <algorithm>

// ---- MdvInline ---------------------------------------------------------------------

void MdvInline::Append(std::wstring_view s, uint16_t style, int link, size_t source, size_t sourceLength) {
	if (s.empty())
		return;
	const size_t pos = text.size();
	text.append(s);

	if (!runs.empty() && runs.back().style == style && runs.back().link == link)
		runs.back().length += s.size();
	else
		runs.push_back({ pos, s.size(), style, link });

	const bool exact = sourceLength == s.size();
	if (!map.empty()) {
		auto& last = map.back();
		if (exact && last.length == last.sourceLength && last.source + last.sourceLength == source) {
			last.length += s.size();
			last.sourceLength += sourceLength;
			goto mapped;
		}
	}
	map.push_back({ pos, s.size(), source, sourceLength });
mapped:
	if (link >= 0 && link < (int)links.size())
		links[link].length = text.size() - links[link].start;
}

void MdvInline::Replace(size_t pos, size_t count, std::wstring_view s, uint16_t addStyle, int link) {
	pos = std::min(pos, text.size());
	count = std::min(count, text.size() - pos);
	const size_t probe = count > 0 || pos < text.size() ? pos : (pos > 0 ? pos - 1 : 0);
	const uint16_t style = (text.empty() ? 0 : StyleAt(probe)) | addStyle | (link >= 0 ? MdvLinked : 0);
	if (link < 0)
		link = text.empty() ? -1 : LinkAt(probe);

	// the source the replaced text came from
	size_t srcStart = SourceOf(pos), srcEnd = srcStart;
	if (count > 0 && srcStart != MDV_NOPOS) {
		const size_t lastChar = pos + count - 1;
		for (auto& seg : map)
			if (lastChar >= seg.text && lastChar < seg.text + seg.length) {
				srcEnd = seg.length == seg.sourceLength ? seg.source + (lastChar - seg.text) + 1 : seg.source + seg.sourceLength;
				break;
			}
	}
	if (srcStart == MDV_NOPOS)
		srcStart = srcEnd = 0;
	if (srcEnd < srcStart)
		srcEnd = srcStart;

	const size_t end = pos + count;
	const ptrdiff_t delta = (ptrdiff_t)s.size() - (ptrdiff_t)count;
	text.replace(pos, count, s);

	// runs: the parts before pos, the new run, the parts after end shifted
	{
		std::vector<MdvRun> before, after;
		for (auto& r : runs) {
			const size_t rEnd = r.start + r.length;
			if (r.start < pos)
				before.push_back({ r.start, std::min(rEnd, pos) - r.start, r.style, r.link });
			if (rEnd > end) {
				const size_t a = std::max(r.start, end);
				after.push_back({ a + delta, rEnd - a, r.style, r.link });
			}
		}
		runs.clear();
		auto add = [&](const MdvRun& r) {
			if (r.length == 0)
				return;
			if (!runs.empty() && runs.back().style == r.style && runs.back().link == r.link &&
				runs.back().start + runs.back().length == r.start)
				runs.back().length += r.length;
			else
				runs.push_back(r);
		};
		for (auto& r : before)
			add(r);
		add({ pos, s.size(), style, link });
		for (auto& r : after)
			add(r);
	}

	// the map, the same way
	{
		std::vector<MdvSegment> result;
		for (auto& seg : map) {
			const size_t segEnd = seg.text + seg.length;
			const bool exact = seg.length == seg.sourceLength;
			if (seg.text < pos) {
				const size_t n = std::min(segEnd, pos) - seg.text;
				result.push_back({ seg.text, n, seg.source, exact ? n : seg.sourceLength });
			}
		}
		if (!s.empty())
			result.push_back({ pos, s.size(), srcStart, srcEnd - srcStart });
		for (auto& seg : map) {
			const size_t segEnd = seg.text + seg.length;
			const bool exact = seg.length == seg.sourceLength;
			if (segEnd > end) {
				const size_t a = std::max(seg.text, end);
				const size_t n = segEnd - a;
				result.push_back({ a + delta, n, exact ? seg.source + (a - seg.text) : seg.source, exact ? n : seg.sourceLength });
			}
		}
		map = std::move(result);
	}

	// links and images
	auto adjustStart = [&](size_t p) { return p <= pos ? p : p >= end ? p + delta : pos; };
	auto adjustEnd = [&](size_t p) { return p <= pos ? p : p >= end ? p + delta : pos + s.size(); };
	for (size_t i = 0; i < links.size(); i++) {
		auto& l = links[i];
		size_t a = adjustStart(l.start), b = adjustEnd(l.start + l.length);
		if ((int)i == link) {
			a = std::min(a, pos);
			b = std::max(b, pos + s.size());
		}
		l.start = a;
		l.length = b > a ? b - a : 0;
	}
	for (size_t i = 0; i < images.size();) {
		auto& img = images[i];
		if (img.pos >= pos && img.pos < end) {
			images.erase(images.begin() + i);
			continue;
		}
		if (img.pos >= end)
			img.pos += delta;
		i++;
	}
}

void MdvInline::TrimEnd() {
	size_t n = text.size();
	while (n > 0 && (text[n - 1] == L' ' || text[n - 1] == L'\n' || text[n - 1] == L'\t'))
		n--;
	if (n < text.size())
		Replace(n, text.size() - n, L"");
}

MdvInline MdvInline::Slice(size_t from, size_t to) const {
	MdvInline out;
	to = std::min(to, text.size());
	from = std::min(from, to);
	out.text = text.substr(from, to - from);
	std::vector<int> linkMap(links.size(), -1);
	for (auto& r : runs) {
		const size_t a = std::max(r.start, from), b = std::min(r.start + r.length, to);
		if (a >= b)
			continue;
		int link = -1;
		if (r.link >= 0) {
			if (linkMap[r.link] < 0) {
				MdvLink l = links[r.link];
				const size_t la = std::max(l.start, from), lb = std::min(l.start + l.length, to);
				l.start = la - from;
				l.length = lb > la ? lb - la : 0;
				out.links.push_back(std::move(l));
				linkMap[r.link] = (int)out.links.size() - 1;
			}
			link = linkMap[r.link];
		}
		out.runs.push_back({ a - from, b - a, r.style, link });
	}
	for (auto& seg : map) {
		const size_t a = std::max(seg.text, from), b = std::min(seg.text + seg.length, to);
		if (a >= b)
			continue;
		const bool exact = seg.length == seg.sourceLength;
		out.map.push_back({ a - from, b - a, exact ? seg.source + (a - seg.text) : seg.source, exact ? b - a : seg.sourceLength });
	}
	for (auto& img : images)
		if (img.pos >= from && img.pos < to) {
			MdvImage copy = img;
			copy.pos -= from;
			out.images.push_back(std::move(copy));
		}
	return out;
}

uint16_t MdvInline::StyleAt(size_t pos) const {
	auto it = std::upper_bound(runs.begin(), runs.end(), pos, [](size_t p, const MdvRun& r) { return p < r.start; });
	if (it == runs.begin())
		return 0;
	--it;
	return pos < it->start + it->length ? it->style : 0;
}

int MdvInline::LinkAt(size_t pos) const {
	auto it = std::upper_bound(runs.begin(), runs.end(), pos, [](size_t p, const MdvRun& r) { return p < r.start; });
	if (it == runs.begin())
		return -1;
	--it;
	return pos < it->start + it->length ? it->link : -1;
}

int MdvInline::ImageAt(size_t pos) const {
	for (size_t i = 0; i < images.size(); i++)
		if (images[i].pos == pos)
			return (int)i;
	return -1;
}

size_t MdvInline::SourceOf(size_t pos) const {
	if (map.empty())
		return MDV_NOPOS;
	if (pos >= text.size())
		return SourceEnd();
	auto it = std::upper_bound(map.begin(), map.end(), pos, [](size_t p, const MdvSegment& s) { return p < s.text; });
	if (it == map.begin())
		return map.front().source;
	--it;
	if (pos >= it->text + it->length)
		return it->source + it->sourceLength;
	return it->length == it->sourceLength ? it->source + (pos - it->text) : it->source;
}

size_t MdvInline::SourceEnd() const {
	if (map.empty())
		return MDV_NOPOS;
	size_t end = 0;
	for (auto& s : map)
		end = std::max(end, s.source + s.sourceLength);
	return end;
}

size_t MdvInline::TextOf(size_t source) const {
	for (auto& s : map) {
		if (source < s.source)
			return s.text;
		if (source < s.source + s.sourceLength)
			return s.length == s.sourceLength ? s.text + (source - s.source) : s.text;
	}
	return text.size();
}

// ---- Document queries -----------------------------------------------------------------

static void CollectLeaves(const MdvDocument& doc, int b, std::vector<int>& out) {
	auto& block = doc.blocks[b];
	if (block.HasContent() || block.kind == MdvKind::Rule)
		out.push_back(b);
	for (int c : block.children)
		CollectLeaves(doc, c, out);
}

std::vector<int> MdvDocument::Leaves() const {
	std::vector<int> out;
	if (!blocks.empty())
		CollectLeaves(*this, 0, out);
	return out;
}

std::vector<int> MdvDocument::TextLeaves() const {
	std::vector<int> out;
	for (int b : Leaves())
		if (blocks[b].HasContent())
			out.push_back(b);
	return out;
}

int MdvDocument::LeafAtSource(size_t offset) const {
	int last = -1;
	for (int b : Leaves()) {
		auto& block = blocks[b];
		if (block.sourceEnd != MDV_NOPOS && (offset < block.sourceEnd || offset <= block.sourceStart))
			return b;
		last = b;
	}
	return last;
}

int MdvDocument::FindAnchor(std::wstring_view name) const {
	if (!name.empty() && name[0] == L'#')
		name.remove_prefix(1);
	const std::wstring decoded = MdvPercentDecode(name);
	for (size_t i = 0; i < anchors.size(); i++)
		if (::CompareStringOrdinal(anchors[i].name.c_str(), (int)anchors[i].name.size(), decoded.c_str(), (int)decoded.size(), TRUE) == CSTR_EQUAL)
			return (int)i;
	return -1;
}

// ---- Slugs ---------------------------------------------------------------------------

std::wstring MdvSlug(std::wstring_view text) {
	if (text.empty())
		return {};
	std::wstring lower(text);
	::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), (int)text.size(), lower.data(), (int)lower.size(), nullptr, nullptr, 0);
	std::vector<WORD> type1(lower.size()), type3(lower.size());
	::GetStringTypeW(CT_CTYPE1, lower.data(), (int)lower.size(), type1.data());
	::GetStringTypeW(CT_CTYPE3, lower.data(), (int)lower.size(), type3.data());
	std::wstring slug;
	slug.reserve(lower.size());
	for (size_t i = 0; i < lower.size(); i++) {
		const wchar_t ch = lower[i];
		if (ch == L' ')
			slug += L'-';
		else if (ch == L'-' || ch == L'_')
			slug += ch;
		else if (ch >= 0xD800 && ch <= 0xDFFF)
			continue;                       // emoji and the like
		else if ((type1[i] & (C1_ALPHA | C1_DIGIT)) || (type3[i] & C3_NONSPACING))
			slug += ch;
	}
	return slug;
}

// ---- Tables ----------------------------------------------------------------------------

std::vector<float> MdvColumnWidths(const std::vector<MdvColumnMeasure>& columns, float available, float padding, float overflowCap) {
	std::vector<float> widths;
	widths.reserve(columns.size());
	float sumMin = 0, sumMax = 0;
	for (auto& c : columns) {
		sumMin += c.minWidth;
		sumMax += std::max(c.minWidth, c.maxWidth);
	}
	const float room = available - padding * columns.size();
	for (auto& c : columns) {
		const float mx = std::max(c.minWidth, c.maxWidth);
		if (sumMax <= room)
			widths.push_back(mx);
		else if (sumMin >= room || sumMax <= sumMin)
			widths.push_back(std::max(c.minWidth, std::min(mx, overflowCap)));
		else
			widths.push_back(c.minWidth + (mx - c.minWidth) * (room - sumMin) / (sumMax - sumMin));
	}
	return widths;
}

// ---- The builder -------------------------------------------------------------------------

namespace {

struct Builder {
	MdvDocument& doc;
	unsigned flags;

	struct Frame {
		int block;
		bool real;                  // pushed by md4c's enter_block (else by a <details>)
	};
	std::vector<Frame> stack;

	struct AlignEntry {
		size_t depth;
		MdvAlign align;
		std::wstring tag;
	};
	std::vector<AlignEntry> aligns;

	struct StyleEntry {
		uint16_t bits;
		int link;
		int span;                   // MD_SPANTYPE, or -1 for an HTML tag
		std::wstring tag;
	};
	std::vector<StyleEntry> styles;

	int target = -1;                // the block taking inline content
	bool implicitTarget = false;    // target is a tight list item's paragraph
	int image = -1;                 // the image whose alt text is being read
	int imageDepth = 0;
	bool inHead = false;
	size_t lastSource = 0;

	bool inHtmlBlock = false;
	MdvInline htmlBlock;            // the HTML block's text and its source
	MdvInline inlineHtml;           // inline HTML pieces waiting for the next event
	std::vector<std::wstring> pendingAnchors;   // <a name> before any content

	Builder(MdvDocument& d, unsigned f) : doc(d), flags(f) {}

	MdvBlock& Top() { return doc.blocks[stack.back().block]; }

	size_t SourceOf(const MD_CHAR* p, MD_SIZE size, size_t& length) {
		const wchar_t* base = doc.source.data();
		if (p >= base && p + size <= base + doc.source.size()) {
			length = size;
			lastSource = (size_t)(p - base) + size;
			return (size_t)(p - base);
		}
		length = 0;
		return lastSource;
	}

	MdvAlign CurrentAlign() const {
		return aligns.empty() ? MdvAlign::Default : aligns.back().align;
	}

	int NewBlock(MdvKind kind) {
		const int parent = stack.back().block;
		MdvBlock b;
		b.kind = kind;
		b.parent = parent;
		doc.blocks.push_back(std::move(b));
		const int index = (int)doc.blocks.size() - 1;
		doc.blocks[parent].children.push_back(index);
		if (kind == MdvKind::Paragraph || kind == MdvKind::Heading)
			doc.blocks[index].align = CurrentAlign();
		if (!pendingAnchors.empty() && (doc.blocks[index].HasContent() || kind == MdvKind::Rule)) {
			for (auto& name : pendingAnchors)
				doc.anchors.push_back({ name, index, 0 });
			pendingAnchors.clear();
		}
		return index;
	}

	int Push(MdvKind kind, bool real = true) {
		const int index = NewBlock(kind);
		stack.push_back({ index, real });
		return index;
	}

	void PopReal() {
		while (stack.size() > 1 && !stack.back().real)
			stack.pop_back();
		if (stack.size() > 1)
			stack.pop_back();
		while (!aligns.empty() && aligns.back().depth > stack.size())
			aligns.pop_back();
	}

	uint16_t CurrentStyle(int& link) const {
		uint16_t bits = 0;
		link = -1;
		for (auto& s : styles) {
			bits |= s.bits;
			if (s.link >= 0)
				link = s.link;
		}
		if (link >= 0)
			bits |= MdvLinked;
		return bits;
	}

	void AppendText(std::wstring_view s, uint16_t extra, size_t source, size_t sourceLength) {
		if (target < 0)
			return;
		auto& content = doc.blocks[target].content;
		if (imageDepth > 0) {
			if (image >= 0 && image < (int)content.images.size())
				content.images[image].alt.append(s);
			return;
		}
		int link;
		const uint16_t style = CurrentStyle(link) | extra;
		content.Append(s, style, link, source, sourceLength);
	}

	static std::wstring Attribute(const MD_ATTRIBUTE& attr) {
		std::wstring out;
		if (!attr.text)
			return out;
		for (int i = 0; attr.substr_offsets[i] < attr.size; i++) {
			const std::wstring_view part(attr.text + attr.substr_offsets[i], attr.substr_offsets[i + 1] - attr.substr_offsets[i]);
			switch (attr.substr_types[i]) {
				case MD_TEXT_ENTITY:
					if (!MdvDecodeEntity(part, out))
						out.append(part);
					break;
				case MD_TEXT_NULLCHAR:
					out += (wchar_t)0xFFFD;
					break;
				default:
					out.append(part);
					break;
			}
		}
		return out;
	}

	// ---- Raw HTML ----

	static bool StyleOfTag(const std::wstring& tag, uint16_t& bits) {
		static const struct { const wchar_t* tag; uint16_t bits; } tags[] = {
			{ L"b", MdvBold }, { L"strong", MdvBold }, { L"i", MdvItalic }, { L"em", MdvItalic }, { L"cite", MdvItalic },
			{ L"s", MdvStrike }, { L"del", MdvStrike }, { L"strike", MdvStrike }, { L"u", MdvUnderline }, { L"ins", MdvUnderline },
			{ L"code", MdvCode }, { L"tt", MdvCode }, { L"samp", MdvCode }, { L"kbd", MdvKbd }, { L"sub", MdvSub },
			{ L"sup", MdvSup }, { L"mark", MdvMark },
		};
		for (auto& t : tags)
			if (tag == t.tag) {
				bits = t.bits;
				return true;
			}
		return false;
	}

	static int ParsePixels(const std::wstring* value) {
		if (!value || value->empty() || value->find(L'%') != std::wstring::npos)
			return 0;
		return _wtoi(value->c_str());
	}

	void AddAnchor(const std::wstring& name) {
		if (name.empty())
			return;
		if (target >= 0)
			doc.anchors.push_back({ name, target, doc.blocks[target].content.text.size() });
		else
			pendingAnchors.push_back(name);
	}

	// an inline tag, in a paragraph's text or in an HTML block's; false if it isn't one
	bool InlineTag(const MdvHtmlToken& t, size_t source, size_t sourceLength) {
		uint16_t bits;
		if (t.type == MdvHtmlToken::Start) {
			if (t.name == L"br") {
				AppendText(L"\n", 0, source, 0);
				return true;
			}
			if (t.name == L"img") {
				if (target < 0)
					return true;
				auto& content = doc.blocks[target].content;
				MdvImage img;
				if (auto v = t.Attribute(L"src"))
					img.src = *v;
				if (auto v = t.Attribute(L"alt"))
					img.alt = *v;
				if (auto v = t.Attribute(L"title"))
					img.title = *v;
				img.width = ParsePixels(t.Attribute(L"width"));
				img.height = ParsePixels(t.Attribute(L"height"));
				img.pos = content.text.size();
				content.images.push_back(std::move(img));
				const wchar_t obj = MdvObjectChar;
				AppendText(std::wstring_view(&obj, 1), 0, source, sourceLength);
				return true;
			}
			if (t.name == L"a") {
				if (auto v = t.Attribute(L"name"))
					AddAnchor(*v);
				if (auto v = t.Attribute(L"id"))
					AddAnchor(*v);
				int link = -1;
				if (auto href = t.Attribute(L"href"); href && target >= 0) {
					auto& content = doc.blocks[target].content;
					MdvLink l;
					l.url = *href;
					if (auto title = t.Attribute(L"title"))
						l.title = *title;
					l.start = content.text.size();
					content.links.push_back(std::move(l));
					link = (int)content.links.size() - 1;
				}
				if (!t.selfClosing)
					styles.push_back({ 0, link, -1, L"a" });
				return true;
			}
			if (StyleOfTag(t.name, bits)) {
				if (!t.selfClosing)
					styles.push_back({ bits, -1, -1, t.name });
				return true;
			}
			if (t.name == L"span" || t.name == L"font" || t.name == L"small" || t.name == L"big" || t.name == L"abbr" ||
				t.name == L"picture" || t.name == L"source" || t.name == L"wbr" || t.name == L"input")
				return true;
			return false;
		}
		if (t.type == MdvHtmlToken::End) {
			if (t.name == L"a" || StyleOfTag(t.name, bits)) {
				for (size_t i = styles.size(); i-- > 0;)
					if (styles[i].span < 0 && styles[i].tag == t.name) {
						styles.erase(styles.begin() + i);
						break;
					}
				return true;
			}
			if (t.name == L"span" || t.name == L"font" || t.name == L"small" || t.name == L"big" || t.name == L"abbr" ||
				t.name == L"picture")
				return true;
			return false;
		}
		return t.type == MdvHtmlToken::Comment;
	}

	void FlushInlineHtml() {
		if (inlineHtml.text.empty())
			return;
		EnsureTarget();
		MdvInline html = std::move(inlineHtml);
		inlineHtml = {};
		for (auto& t : MdvTokenizeHtml(html.text)) {
			const size_t src = html.SourceOf(t.pos);
			const size_t srcEnd = html.SourceOf(t.pos + t.length);
			if (t.type == MdvHtmlToken::Text) {
				// a stray '<' that wasn't a tag: show it
				AppendText(MdvDecodeEntities(std::wstring_view(html.text).substr(t.pos, t.length)), 0, src, srcEnd - src);
				continue;
			}
			InlineTag(t, src, srcEnd - src);
		}
	}

	static bool IsBoundaryTag(const std::wstring& name) {
		static const wchar_t* tags[] = { L"table", L"thead", L"tbody", L"tfoot", L"tr", L"td", L"th", L"ul", L"ol", L"li",
			L"blockquote", L"section", L"article", L"header", L"footer", L"nav", L"figure", L"figcaption", L"dl", L"dt",
			L"dd", L"pre", L"main", L"aside", L"caption" };
		for (auto t : tags)
			if (name == t)
				return true;
		return false;
	}

	static MdvAlign AlignOf(const MdvHtmlToken& t, MdvAlign inherited) {
		if (t.name == L"center")
			return MdvAlign::Center;
		if (auto a = t.Attribute(L"align")) {
			if (_wcsicmp(a->c_str(), L"center") == 0)
				return MdvAlign::Center;
			if (_wcsicmp(a->c_str(), L"right") == 0)
				return MdvAlign::Right;
			if (_wcsicmp(a->c_str(), L"left") == 0)
				return MdvAlign::Left;
		}
		return inherited;
	}

	// removes a paragraph that has nothing to show (the last block made); its anchors go on to the next block
	void DropEmpty(int b) {
		auto& block = doc.blocks[b];
		if (block.kind != MdvKind::Paragraph || !block.content.text.empty() || (int)doc.blocks.size() - 1 != b)
			return;
		auto& siblings = doc.blocks[block.parent].children;
		if (siblings.empty() || siblings.back() != b)
			return;
		siblings.pop_back();
		for (size_t i = doc.anchors.size(); i-- > 0;)
			if (doc.anchors[i].block == b) {
				pendingAnchors.insert(pendingAnchors.begin(), doc.anchors[i].name);
				doc.anchors.erase(doc.anchors.begin() + i);
			}
		doc.blocks.pop_back();
	}

	// finishes a paragraph made from HTML
	void EndHtmlParagraph() {
		if (target < 0)
			return;
		const int b = target;
		doc.blocks[b].content.TrimEnd();
		target = -1;
		styles.clear();
		DropEmpty(b);
	}

	// md4c gives a tight list item's text without a paragraph: it gets one
	void EnsureTarget() {
		if (target < 0 && !inHtmlBlock && stack.size() > 1 && (Top().kind == MdvKind::Item || (!stack.back().real && Top().kind == MdvKind::Details))) {
			target = NewBlock(MdvKind::Paragraph);
			implicitTarget = true;
		}
	}

	void EndImplicit() {
		if (!implicitTarget)
			return;
		const int b = target;
		target = -1;
		styles.clear();
		imageDepth = 0;
		implicitTarget = false;
		DropEmpty(b);
	}

	void HtmlText(std::wstring_view raw, size_t source, size_t sourceLength) {
		std::wstring decoded = MdvDecodeEntities(raw);
		std::wstring text;
		text.reserve(decoded.size());
		bool space = target < 0 || doc.blocks[target].content.text.empty() ||
			doc.blocks[target].content.text.back() == L' ' || doc.blocks[target].content.text.back() == L'\n';
		for (wchar_t ch : decoded) {
			if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r') {
				if (!space)
					text += L' ';
				space = true;
			}
			else {
				text += ch;
				space = false;
			}
		}
		if (text.empty())
			return;
		if (target < 0)
			target = NewBlock(MdvKind::Paragraph);
		AppendText(text, 0, source, sourceLength);
	}

	void ProcessHtmlBlock() {
		MdvInline html = std::move(htmlBlock);
		htmlBlock = {};
		target = -1;
		styles.clear();
		int headingLevel = 0;
		for (auto& t : MdvTokenizeHtml(html.text)) {
			const size_t src = html.SourceOf(t.pos);
			const size_t srcEnd = std::max(src, html.SourceOf(t.pos + t.length));
			if (t.type == MdvHtmlToken::Comment)
				continue;
			if (t.type == MdvHtmlToken::Text) {
				HtmlText(std::wstring_view(html.text).substr(t.pos, t.length), src, srcEnd - src);
				continue;
			}
			const auto& name = t.name;
			if (t.type == MdvHtmlToken::Start) {
				if (auto id = t.Attribute(L"id"); id && name != L"a") {
					if (target >= 0 && (name == L"p" || name == L"div" || name.size() == 2 && name[0] == L'h'))
						EndHtmlParagraph();
					pendingAnchors.push_back(*id);
				}
				if (name == L"details") {
					EndHtmlParagraph();
					const int d = Push(MdvKind::Details, false);
					doc.blocks[d].open = t.Attribute(L"open") != nullptr;
					continue;
				}
				if (name == L"summary") {
					EndHtmlParagraph();
					if (!stack.back().real && Top().kind == MdvKind::Details)
						target = stack.back().block;
					continue;
				}
				if (name == L"p" || name == L"div" || name == L"center") {
					EndHtmlParagraph();
					aligns.push_back({ stack.size(), AlignOf(t, CurrentAlign()), name });
					continue;
				}
				if (name.size() == 2 && name[0] == L'h' && name[1] >= L'1' && name[1] <= L'6') {
					EndHtmlParagraph();
					target = NewBlock(MdvKind::Heading);
					doc.blocks[target].level = name[1] - L'0';
					doc.blocks[target].align = AlignOf(t, CurrentAlign());
					headingLevel = doc.blocks[target].level;
					continue;
				}
				if (name == L"hr") {
					EndHtmlParagraph();
					auto& rule = doc.blocks[NewBlock(MdvKind::Rule)];
					rule.sourceStart = src;
					rule.sourceEnd = srcEnd;
					continue;
				}
				if (IsBoundaryTag(name)) {
					EndHtmlParagraph();
					continue;
				}
				uint16_t bits;
				if (target < 0 && (name == L"img" || name == L"a" || StyleOfTag(name, bits)))
					target = NewBlock(MdvKind::Paragraph);
				InlineTag(t, src, srcEnd - src);
				continue;
			}
			// end tags
			if (name == L"details") {
				EndHtmlParagraph();
				for (size_t i = stack.size(); i-- > 1 && !stack[i].real;)
					if (doc.blocks[stack[i].block].kind == MdvKind::Details) {
						stack.resize(i);
						break;
					}
				continue;
			}
			if (name == L"summary") {
				if (target >= 0 && doc.blocks[target].kind == MdvKind::Details) {
					doc.blocks[target].content.TrimEnd();
					target = -1;
					styles.clear();
				}
				continue;
			}
			if (name == L"p" || name == L"div" || name == L"center") {
				EndHtmlParagraph();
				for (size_t i = aligns.size(); i-- > 0 && aligns[i].depth == stack.size();)
					if (aligns[i].tag == name) {
						aligns.erase(aligns.begin() + i);
						break;
					}
				continue;
			}
			if (name.size() == 2 && name[0] == L'h' && name[1] - L'0' == headingLevel) {
				EndHtmlParagraph();
				headingLevel = 0;
				continue;
			}
			if (IsBoundaryTag(name)) {
				EndHtmlParagraph();
				continue;
			}
			InlineTag(t, src, srcEnd - src);
		}
		if (target >= 0 && doc.blocks[target].kind == MdvKind::Details) {
			doc.blocks[target].content.TrimEnd();
			target = -1;
		}
		EndHtmlParagraph();
	}

	// ---- md4c's callbacks ----

	int EnterBlock(MD_BLOCKTYPE type, void* detail) {
		FlushInlineHtml();
		EndImplicit();
		switch (type) {
			case MD_BLOCK_DOC:
				stack.push_back({ 0, true });
				break;
			case MD_BLOCK_QUOTE:
				Push(MdvKind::Quote);
				break;
			case MD_BLOCK_UL: {
				auto d = (MD_BLOCK_UL_DETAIL*)detail;
				auto& b = doc.blocks[Push(MdvKind::List)];
				b.tight = d->is_tight != 0;
				b.mark = d->mark;
				break;
			}
			case MD_BLOCK_OL: {
				auto d = (MD_BLOCK_OL_DETAIL*)detail;
				auto& b = doc.blocks[Push(MdvKind::List)];
				b.ordered = true;
				b.tight = d->is_tight != 0;
				b.start = d->start;
				b.mark = d->mark_delimiter;
				break;
			}
			case MD_BLOCK_LI: {
				auto d = (MD_BLOCK_LI_DETAIL*)detail;
				auto& list = Top();
				const unsigned number = list.start + (unsigned)list.children.size();
				auto& b = doc.blocks[Push(MdvKind::Item)];
				b.number = number;
				if (d->is_task) {
					b.task = d->task_mark == L' ' ? MdvTask::Unchecked : MdvTask::Checked;
					b.taskOffset = d->task_mark_offset;
				}
				break;
			}
			case MD_BLOCK_HR:
				Push(MdvKind::Rule);
				break;
			case MD_BLOCK_H:
				target = Push(MdvKind::Heading);
				doc.blocks[target].level = ((MD_BLOCK_H_DETAIL*)detail)->level;
				break;
			case MD_BLOCK_CODE: {
				auto d = (MD_BLOCK_CODE_DETAIL*)detail;
				target = Push(MdvKind::Code);
				auto& b = doc.blocks[target];
				b.lang = Attribute(d->lang);
				b.fenced = d->fence_char != 0;
				break;
			}
			case MD_BLOCK_HTML:
				inHtmlBlock = true;
				htmlBlock = {};
				break;
			case MD_BLOCK_P:
				target = Push(MdvKind::Paragraph);
				break;
			case MD_BLOCK_TABLE: {
				auto& b = doc.blocks[Push(MdvKind::Table)];
				b.columns = ((MD_BLOCK_TABLE_DETAIL*)detail)->col_count;
				break;
			}
			case MD_BLOCK_THEAD:
				inHead = true;
				break;
			case MD_BLOCK_TBODY:
				inHead = false;
				break;
			case MD_BLOCK_TR:
				doc.blocks[Push(MdvKind::Row)].header = inHead;
				break;
			case MD_BLOCK_TH:
			case MD_BLOCK_TD: {
				target = Push(MdvKind::Cell);
				auto& b = doc.blocks[target];
				b.header = inHead;
				switch (((MD_BLOCK_TD_DETAIL*)detail)->align) {
					case MD_ALIGN_LEFT: b.align = MdvAlign::Left; break;
					case MD_ALIGN_CENTER: b.align = MdvAlign::Center; break;
					case MD_ALIGN_RIGHT: b.align = MdvAlign::Right; break;
					default: b.align = MdvAlign::Default; break;
				}
				break;
			}
		}
		return 0;
	}

	int LeaveBlock(MD_BLOCKTYPE type) {
		FlushInlineHtml();
		EndImplicit();
		switch (type) {
			case MD_BLOCK_DOC:
				return 0;
			case MD_BLOCK_HTML:
				inHtmlBlock = false;
				ProcessHtmlBlock();
				return 0;
			case MD_BLOCK_THEAD:
			case MD_BLOCK_TBODY:
				return 0;
			case MD_BLOCK_CODE:
				if (target >= 0) {
					auto& content = doc.blocks[target].content;
					if (!content.text.empty() && content.text.back() == L'\n')
						content.Replace(content.text.size() - 1, 1, L"");
				}
				break;
			default:
				break;
		}
		const int block = stack.back().block;
		if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE || type == MD_BLOCK_TH || type == MD_BLOCK_TD) {
			target = -1;
			styles.clear();
			imageDepth = 0;
		}
		PopReal();
		if (type == MD_BLOCK_P)
			DropEmpty(block);           // only inline HTML (an <a name>)
		if (type == MD_BLOCK_QUOTE)
			CheckAlert(block);
		return 0;
	}

	int EnterSpan(MD_SPANTYPE type, void* detail) {
		FlushInlineHtml();
		EnsureTarget();
		if (imageDepth > 0) {
			if (type == MD_SPAN_IMG)
				imageDepth++;
			return 0;
		}
		switch (type) {
			case MD_SPAN_EM: styles.push_back({ MdvItalic, -1, type, {} }); break;
			case MD_SPAN_STRONG: styles.push_back({ MdvBold, -1, type, {} }); break;
			case MD_SPAN_DEL: styles.push_back({ MdvStrike, -1, type, {} }); break;
			case MD_SPAN_U: styles.push_back({ MdvUnderline, -1, type, {} }); break;
			case MD_SPAN_CODE: styles.push_back({ MdvCode, -1, type, {} }); break;
			case MD_SPAN_LATEXMATH:
			case MD_SPAN_LATEXMATH_DISPLAY: styles.push_back({ MdvMath, -1, type, {} }); break;
			case MD_SPAN_A: {
				int link = -1;
				if (target >= 0) {
					auto d = (MD_SPAN_A_DETAIL*)detail;
					auto& content = doc.blocks[target].content;
					MdvLink l;
					l.url = Attribute(d->href);
					l.title = Attribute(d->title);
					l.autolink = d->is_autolink != 0;
					l.start = content.text.size();
					content.links.push_back(std::move(l));
					link = (int)content.links.size() - 1;
				}
				styles.push_back({ 0, link, type, {} });
				break;
			}
			case MD_SPAN_IMG: {
				if (target < 0)
					break;
				auto d = (MD_SPAN_IMG_DETAIL*)detail;
				auto& content = doc.blocks[target].content;
				MdvImage img;
				img.src = Attribute(d->src);
				img.title = Attribute(d->title);
				img.pos = content.text.size();
				content.images.push_back(std::move(img));
				const wchar_t obj = MdvObjectChar;
				AppendText(std::wstring_view(&obj, 1), 0, lastSource, 0);
				image = (int)content.images.size() - 1;
				imageDepth = 1;
				break;
			}
			default:
				break;
		}
		return 0;
	}

	int LeaveSpan(MD_SPANTYPE type) {
		FlushInlineHtml();
		if (imageDepth > 0) {
			if (type == MD_SPAN_IMG && --imageDepth == 0)
				image = -1;
			return 0;
		}
		for (size_t i = styles.size(); i-- > 0;)
			if (styles[i].span == (int)type) {
				styles.erase(styles.begin() + i);
				break;
			}
		return 0;
	}

	int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
		size_t length;
		const size_t source = SourceOf(text, size, length);
		const std::wstring_view s(text, size);
		if (type == MD_TEXT_HTML) {
			if (inHtmlBlock)
				htmlBlock.Append(s, 0, -1, source, length);
			else
				inlineHtml.Append(s, 0, -1, source, length);
			return 0;
		}
		FlushInlineHtml();
		EnsureTarget();
		const bool code = target >= 0 && doc.blocks[target].kind == MdvKind::Code;
		switch (type) {
			case MD_TEXT_NULLCHAR: {
				const wchar_t ch = 0xFFFD;
				AppendText(std::wstring_view(&ch, 1), 0, source, length);
				break;
			}
			case MD_TEXT_BR:
				AppendText(imageDepth > 0 ? L" " : L"\n", 0, source, length);
				break;
			case MD_TEXT_SOFTBR:
				AppendText(L" ", 0, source, length);
				break;
			case MD_TEXT_ENTITY: {
				std::wstring decoded;
				if (!MdvDecodeEntity(s, decoded))
					decoded = s;
				AppendText(decoded, 0, source, length);
				break;
			}
			case MD_TEXT_CODE:
				AppendText(s, MdvCode, source, length);
				break;
			case MD_TEXT_LATEXMATH:
				AppendText(s, MdvMath, source, length);
				break;
			default:
				AppendText(s, code ? MdvCode : 0, source, length);
				break;
		}
		return 0;
	}

	// ---- After the parse ----

	void CheckAlert(int quote) {
		auto& q = doc.blocks[quote];
		if (q.children.empty())
			return;
		const int first = q.children[0];
		auto& p = doc.blocks[first];
		if (p.kind != MdvKind::Paragraph)
			return;
		auto& text = p.content.text;
		if (text.size() < 4 || text[0] != L'[' || text[1] != L'!')
			return;
		const size_t close = text.find(L']');
		if (close == std::wstring::npos || (p.content.StyleAt(0) & (MdvCode | MdvLinked)))
			return;
		static const struct { const wchar_t* name; MdvAlert alert; } names[] = {
			{ L"NOTE", MdvAlert::Note }, { L"TIP", MdvAlert::Tip }, { L"IMPORTANT", MdvAlert::Important },
			{ L"WARNING", MdvAlert::Warning }, { L"CAUTION", MdvAlert::Caution },
		};
		const std::wstring name = text.substr(2, close - 2);
		MdvAlert alert = MdvAlert::None;
		for (auto& n : names)
			if (_wcsicmp(name.c_str(), n.name) == 0)
				alert = n.alert;
		if (alert == MdvAlert::None)
			return;
		// the marker must be alone on its line
		size_t src = p.content.SourceOf(close);
		if (src == MDV_NOPOS)
			return;
		src++;
		auto& source = doc.source;
		while (src < source.size() && (source[src] == L' ' || source[src] == L'\t'))
			src++;
		if (src < source.size() && source[src] != L'\n' && source[src] != L'\r')
			return;
		size_t count = close + 1;
		while (count < text.size() && (text[count] == L' ' || text[count] == L'\n'))
			count++;
		p.content.Replace(0, count, L"");
		if (p.content.text.empty())
			q.children.erase(q.children.begin());
		q.kind = MdvKind::Alert;
		q.alert = alert;
	}

	void ReplaceEmoji(MdvInline& content) {
		auto& text = content.text;
		for (size_t i = 0; i < text.size(); i++) {
			if (text[i] != L':')
				continue;
			size_t j = i + 1;
			while (j < text.size() && j - i <= 40 && ((text[j] < 128 && iswalnum(text[j])) || text[j] == L'_' || text[j] == L'+' || text[j] == L'-'))
				j++;
			if (j >= text.size() || text[j] != L':' || j == i + 1)
				continue;
			if (content.StyleAt(i) & (MdvCode | MdvMath) || content.LinkAt(i) >= 0 && content.links[content.LinkAt(i)].autolink)
				continue;
			const wchar_t* emoji = MdvEmoji(std::wstring_view(text).substr(i + 1, j - i - 1));
			if (!emoji)
				continue;
			content.Replace(i, j - i + 1, emoji);
			i += wcslen(emoji) - 1;
		}
	}

	// footnote definitions in the source, outside fenced code: [^label]: on a line's start. md4c takes a one-word
	// one ("[^1]: https://x") as a link reference definition and shows nothing of it, so those are found here.
	struct SourceDef {
		std::wstring label;
		size_t text;                // the definition's text in the source
		size_t length;
	};

	std::vector<SourceDef> ScanDefinitions() const {
		std::vector<SourceDef> defs;
		auto& s = doc.source;
		wchar_t fence = 0;
		size_t fenceLength = 0;
		for (size_t line = 0; line < s.size();) {
			size_t end = s.find(L'\n', line);
			if (end == std::wstring::npos)
				end = s.size();
			size_t p = line, indent = 0;
			while (p < end && s[p] == L' ' && indent < 4)
				p++, indent++;
			if (indent < 4 && p < end && (s[p] == L'`' || s[p] == L'~')) {
				size_t n = 0;
				while (p + n < end && s[p + n] == s[p])
					n++;
				if (n >= 3) {
					if (!fence) {
						fence = s[p];
						fenceLength = n;
					}
					else if (s[p] == fence && n >= fenceLength)
						fence = 0;
				}
			}
			else if (!fence && indent < 4 && end - p > 4 && s[p] == L'[' && s[p + 1] == L'^') {
				size_t close = p + 2;
				while (close < end && s[close] != L']' && s[close] != L' ' && s[close] != L'[')
					close++;
				if (close > p + 2 && close + 1 < end && s[close] == L']' && s[close + 1] == L':') {
					size_t t = close + 2, e = end;
					while (t < e && (s[t] == L' ' || s[t] == L'\t'))
						t++;
					while (e > t && (s[e - 1] == L'\r' || s[e - 1] == L' ' || s[e - 1] == L'\t'))
						e--;
					defs.push_back({ s.substr(p + 2, close - p - 2), t, e - t });
				}
			}
			line = end + 1;
		}
		return defs;
	}

	static bool SameLabel(std::wstring_view a, std::wstring_view b) {
		return ::CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
	}

	void Footnotes() {
		struct Def {
			std::wstring label;
			int block;              // the Footnote block
			int number = 0;
		};
		std::vector<Def> defs;
		auto addFootnote = [&](std::wstring label, int para) {
			MdvBlock fn;
			fn.kind = MdvKind::Footnote;
			fn.anchor = L"fn-" + label;
			fn.children.push_back(para);
			doc.blocks.push_back(std::move(fn));
			const int index = (int)doc.blocks.size() - 1;
			doc.blocks[para].parent = index;
			defs.push_back({ std::move(label), index });
		};
		// definitions on the lines right after another's are in its paragraph: each [^label]: at a line's start
		// starts a paragraph of its own
		for (int b : doc.Leaves()) {
			if (doc.blocks[b].kind != MdvKind::Paragraph || doc.blocks[b].parent < 0)
				continue;
			int current = b;
			for (;;) {
				auto& content = doc.blocks[current].content;
				size_t at = MDV_NOPOS;
				for (size_t p = 1; p + 4 < content.text.size(); p++) {
					if (content.text[p] != L'[' || content.text[p + 1] != L'^' || content.text[p - 1] != L' ' || (content.StyleAt(p) & MdvCode))
						continue;
					size_t close = p + 2;
					while (close < content.text.size() && content.text[close] != L']' && content.text[close] != L' ' && content.text[close] != L'[')
						close++;
					if (close == p + 2 || close + 1 >= content.text.size() || content.text[close] != L']' || content.text[close + 1] != L':')
						continue;
					// at a line's start in the source (after its indentation)
					size_t src = content.SourceOf(p);
					if (src == MDV_NOPOS || src == 0)
						continue;
					while (src > 0 && (doc.source[src - 1] == L' ' || doc.source[src - 1] == L'\t'))
						src--;
					if (src > 0 && doc.source[src - 1] != L'\n')
						continue;
					at = p;
					break;
				}
				if (at == MDV_NOPOS)
					break;
				MdvBlock next;
				next.kind = MdvKind::Paragraph;
				next.parent = doc.blocks[current].parent;
				next.content = content.Slice(at, content.text.size());
				content.Replace(at, content.text.size() - at, L"");
				content.TrimEnd();
				doc.blocks.push_back(std::move(next));
				const int index = (int)doc.blocks.size() - 1;
				auto& siblings = doc.blocks[doc.blocks[index].parent].children;
				siblings.insert(std::find(siblings.begin(), siblings.end(), current) + 1, index);
				current = index;
			}
		}
		// definitions md4c made paragraphs of: paragraphs starting with [^label]:
		for (int b : doc.Leaves()) {
			auto& p = doc.blocks[b];
			if (p.kind != MdvKind::Paragraph || p.parent < 0)
				continue;
			auto& text = p.content.text;
			if (text.size() < 5 || text[0] != L'[' || text[1] != L'^' || (p.content.StyleAt(0) & MdvCode))
				continue;
			size_t close = 2;
			while (close < text.size() && text[close] != L']' && text[close] != L' ' && text[close] != L'[')
				close++;
			if (close == 2 || close + 1 >= text.size() || text[close] != L']' || text[close + 1] != L':')
				continue;
			std::wstring label = text.substr(2, close - 2);
			size_t count = close + 2;
			while (count < text.size() && text[count] == L' ')
				count++;
			p.content.Replace(0, count, L"");
			auto& siblings = doc.blocks[p.parent].children;
			siblings.erase(std::find(siblings.begin(), siblings.end(), b));
			addFootnote(std::move(label), b);
		}
		// the ones md4c took for link definitions: a paragraph of their text
		for (auto& sd : ScanDefinitions()) {
			if (std::any_of(defs.begin(), defs.end(), [&](const Def& d) { return SameLabel(d.label, sd.label); }))
				continue;
			MdvBlock para;
			para.kind = MdvKind::Paragraph;
			const std::wstring_view text = std::wstring_view(doc.source).substr(sd.text, sd.length);
			if (text.empty())
				continue;
			int link = -1;
			if (MdvClassifyUrl(text) == MdvUrlKind::Web || text.substr(0, 4) == L"www.") {
				MdvLink l;
				l.url = text.substr(0, 4) == L"www." ? L"http://" + std::wstring(text) : std::wstring(text);
				l.autolink = true;
				para.content.links.push_back(std::move(l));
				link = 0;
			}
			para.content.Append(text, link >= 0 ? MdvLinked : 0, link, sd.text, sd.length);
			doc.blocks.push_back(std::move(para));
			addFootnote(sd.label, (int)doc.blocks.size() - 1);
		}
		if (defs.empty())
			return;
		auto find = [&](std::wstring_view label) -> Def* {
			for (auto& d : defs)
				if (SameLabel(d.label, label))
					return &d;
			return nullptr;
		};
		// references: [^label] as text, or (when md4c took the definition for a link's) a link whose text is ^label
		int next = 0;
		for (int b : doc.Leaves()) {
			auto& block = doc.blocks[b];
			if (block.kind == MdvKind::Code || block.kind == MdvKind::Rule)
				continue;
			auto& content = block.content;
			for (size_t i = 0; i + 1 < content.text.size(); i++) {
				Def* def = nullptr;
				size_t count = 0;
				int link = -1;
				if (content.StyleAt(i) & (MdvCode | MdvMath | MdvFootnoteRef))
					continue;
				const int at = content.LinkAt(i);
				if (at >= 0 && content.links[at].start == i && content.text[i] == L'^' && !content.links[at].autolink) {
					auto& l = content.links[at];
					def = find(std::wstring_view(content.text).substr(i + 1, l.length - 1));
					count = l.length;
					link = at;
				}
				else if (at < 0 && content.text[i] == L'[' && content.text[i + 1] == L'^') {
					const size_t close = content.text.find(L']', i + 2);
					if (close != std::wstring::npos && close > i + 2) {
						def = find(std::wstring_view(content.text).substr(i + 2, close - i - 2));
						count = close - i + 1;
					}
				}
				if (!def)
					continue;
				const bool first = def->number == 0;
				if (first)
					def->number = ++next;
				if (link < 0) {
					content.links.push_back({});
					link = (int)content.links.size() - 1;
					content.links[link].start = i;
				}
				auto& l = content.links[link];
				l.url = L"#fn-" + def->label;
				l.title.clear();
				const std::wstring number = std::to_wstring(def->number);
				content.Replace(i, count, number, MdvSup | MdvFootnoteRef, link);
				if (first)
					doc.anchors.push_back({ L"fnref-" + def->label, b, i });
				i += number.size() - 1;
			}
		}
		// the section, numbered by first reference; the ones never referenced after them
		for (auto& d : defs)
			if (d.number == 0)
				d.number = ++next;
		std::stable_sort(defs.begin(), defs.end(), [](const Def& a, const Def& b) { return a.number < b.number; });
		MdvBlock section;
		section.kind = MdvKind::Footnotes;
		section.parent = 0;
		doc.blocks.push_back(std::move(section));
		const int sectionIndex = (int)doc.blocks.size() - 1;
		doc.blocks[0].children.push_back(sectionIndex);
		for (auto& d : defs) {
			auto& fn = doc.blocks[d.block];
			fn.parent = sectionIndex;
			fn.footnote = d.number;
			doc.blocks[sectionIndex].children.push_back(d.block);
			const int para = fn.children.front();
			doc.anchors.push_back({ fn.anchor, para, 0 });
			// the way back to the reference
			auto& content = doc.blocks[para].content;
			MdvLink back;
			back.url = L"#fnref-" + d.label;
			back.start = content.text.size() + 1;
			content.links.push_back(std::move(back));
			const size_t end = content.SourceEnd() == MDV_NOPOS ? 0 : content.SourceEnd();
			content.Append(L" ", 0, -1, end, 0);
			const wchar_t arrow = 0x21A9;
			content.Append(std::wstring_view(&arrow, 1), MdvLinked, (int)content.links.size() - 1, end, 0);
		}
	}

	void Headings() {
		std::vector<std::pair<std::wstring, int>> used;
		for (int b : doc.Leaves()) {
			auto& h = doc.blocks[b];
			if (h.kind != MdvKind::Heading)
				continue;
			doc.headings.push_back(b);
			std::wstring slug = MdvSlug(h.content.text);
			auto it = std::find_if(used.begin(), used.end(), [&](auto& u) { return u.first == slug; });
			if (it == used.end())
				used.push_back({ slug, 0 });
			else
				slug += L"-" + std::to_wstring(++it->second);
			h.anchor = slug;
			doc.anchors.push_back({ slug, b, 0 });
		}
	}

	void SourceRanges(int b, size_t& last) {
		auto& block = doc.blocks[b];
		size_t start = block.sourceStart, end = block.sourceEnd;
		if (block.HasContent()) {
			for (auto& s : block.content.map)
				if (s.sourceLength > 0) {
					start = std::min(start == MDV_NOPOS ? s.source : start, s.source);
					end = std::max(end == MDV_NOPOS ? 0 : end, s.source + s.sourceLength);
				}
		}
		for (int c : block.children) {
			SourceRanges(c, last);
			auto& child = doc.blocks[c];
			start = std::min(start, child.sourceStart);
			end = end == MDV_NOPOS ? child.sourceEnd : std::max(end, child.sourceEnd);
		}
		if (start == MDV_NOPOS)
			start = last;
		if (end == MDV_NOPOS || end < start)
			end = start;
		block.sourceStart = start;
		block.sourceEnd = end;
		last = std::max(last, end);
	}

	void Finish() {
		if (!pendingAnchors.empty()) {
			for (auto& name : pendingAnchors)
				doc.anchors.push_back({ name, 0, 0 });
			pendingAnchors.clear();
		}
		if (!(flags & MdvNoEmoji))
			for (int b : doc.Leaves())
				if (doc.blocks[b].kind != MdvKind::Code)
					ReplaceEmoji(doc.blocks[b].content);
		Footnotes();
		Headings();
		size_t last = 0;
		SourceRanges(0, last);
		doc.blocks[0].sourceStart = 0;
		doc.blocks[0].sourceEnd = doc.source.size();
	}
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* user) { return ((Builder*)user)->EnterBlock(type, detail); }
int LeaveBlock(MD_BLOCKTYPE type, void*, void* user) { return ((Builder*)user)->LeaveBlock(type); }
int EnterSpan(MD_SPANTYPE type, void* detail, void* user) { return ((Builder*)user)->EnterSpan(type, detail); }
int LeaveSpan(MD_SPANTYPE type, void*, void* user) { return ((Builder*)user)->LeaveSpan(type); }
int TextCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* user) { return ((Builder*)user)->Text(type, text, size); }

// "---" alone on the first line, up to a "---" or "..." line: the length of it all, or 0
size_t FrontMatter(const std::wstring& s, std::wstring& yaml) {
	auto lineEnd = [&](size_t p) { size_t e = s.find(L'\n', p); return e == std::wstring::npos ? s.size() : e; };
	auto trimmed = [&](size_t a, size_t b) {
		while (b > a && (s[b - 1] == L'\r' || s[b - 1] == L' ' || s[b - 1] == L'\t'))
			b--;
		return std::wstring_view(s).substr(a, b - a);
	};
	size_t e = lineEnd(0);
	if (trimmed(0, e) != L"---" || e >= s.size())
		return 0;
	const size_t body = e + 1;
	for (size_t p = body; p < s.size();) {
		e = lineEnd(p);
		auto line = trimmed(p, e);
		if (line == L"---" || line == L"...") {
			yaml = s.substr(body, p - body);
			while (!yaml.empty() && (yaml.back() == L'\n' || yaml.back() == L'\r'))
				yaml.pop_back();
			return e < s.size() ? e + 1 : e;
		}
		p = e + 1;
	}
	return 0;
}

}  // namespace

bool MdvParse(std::wstring source, unsigned flags, MdvDocument& doc) {
	doc = {};
	doc.source = std::move(source);
	doc.blocks.push_back({});
	doc.blocks[0].kind = MdvKind::Document;

	const size_t skip = FrontMatter(doc.source, doc.frontMatter);

	MD_PARSER parser{};
	parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_COLLAPSEWHITESPACE;
	if (flags & MdvNoHtml)
		parser.flags |= MD_FLAG_NOHTML;
	if (flags & MdvNoTables)
		parser.flags &= ~MD_FLAG_TABLES;
	if (flags & MdvHardBreaks)
		parser.flags |= MD_FLAG_HARD_SOFT_BREAKS;
	parser.enter_block = EnterBlock;
	parser.leave_block = LeaveBlock;
	parser.enter_span = EnterSpan;
	parser.leave_span = LeaveSpan;
	parser.text = TextCallback;

	Builder builder(doc, flags);
	builder.lastSource = skip;
	const int result = md_parse(doc.source.data() + skip, (MD_SIZE)(doc.source.size() - skip), &parser, &builder);
	// md4c's task offsets are in the text it was given
	if (skip)
		for (auto& b : doc.blocks)
			if (b.taskOffset != MDV_NOPOS)
				b.taskOffset += skip;
	builder.Finish();
	return result == 0;
}
