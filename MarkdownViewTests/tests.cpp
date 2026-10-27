// Unit tests for the markdown view model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: the block tree md4c's callbacks build, inline styles, links and
// images, the map back to the source, GitHub's extras (alerts, footnotes, emoji, anchors, front matter, raw HTML's
// subset), plain text and HTML export, source ranges, the tables' column widths, and the flow of boxes the view lays
// out (indents, margins, markers, bars, placement). Anything that needs an HWND or a render target is checked by
// hand with MarkdownViewDemo.

#include "MarkdownViewLayout.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ---- Tiny harness ------------------------------------------------------------

static int g_checks = 0;
static int g_failures = 0;
static const wchar_t* g_currentTest = L"";

static void Check(bool ok, const char* expr, int line) {
	g_checks++;
	if (!ok) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
	}
}

static void CheckInt(long long actual, long long expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: %lld, want: %lld\n", g_currentTest, line, expr, actual, expected);
	}
}

static std::wstring Visible(const std::wstring& s) {
	std::wstring out;
	for (wchar_t ch : s) {
		if (ch == L'\n')
			out += L"\\n";
		else if (ch == L'\t')
			out += L"\\t";
		else if (ch < 32 || ch > 126) {
			wchar_t buf[8];
			swprintf_s(buf, L"\\x%04X", ch);
			out += buf;
		}
		else
			out += ch;
	}
	return out;
}

static void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got:  \"%s\"\n        want: \"%s\"\n", g_currentTest, line, expr,
			Visible(actual).c_str(), Visible(expected).c_str());
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
#define CHECK_INT(a, b)  CheckInt((long long)(a), (long long)(b), #a " == " #b, __LINE__)
#define CHECK_STR(a, b)  CheckStr((a), (b), #a " == " #b, __LINE__)

#define TEST(name)															\
	static void name();														\
	static void Run_##name() {												\
		g_currentTest = L## #name;											\
		const int before = g_failures;										\
		name();																\
		wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name);	\
	}																		\
	static void name()

// ---- Helpers --------------------------------------------------------------------

static MdvDocument Parse(const std::wstring& text, unsigned flags = 0) {
	MdvDocument doc;
	MdvParse(text, flags, doc);
	return doc;
}

// the tree's shape: "doc(h1,p,ul(li(p)))"
static std::wstring Shape(const MdvDocument& doc, int b = 0) {
	auto& block = doc.blocks[b];
	std::wstring s;
	switch (block.kind) {
		case MdvKind::Document: s = L"doc"; break;
		case MdvKind::Paragraph: s = L"p"; break;
		case MdvKind::Heading: s = L"h" + std::to_wstring(block.level); break;
		case MdvKind::Quote: s = L"quote"; break;
		case MdvKind::Alert: s = L"alert"; break;
		case MdvKind::List: s = block.ordered ? L"ol" : L"ul"; break;
		case MdvKind::Item: s = L"li"; break;
		case MdvKind::Code: s = L"code"; break;
		case MdvKind::Rule: s = L"hr"; break;
		case MdvKind::Table: s = L"table"; break;
		case MdvKind::Row: s = L"tr"; break;
		case MdvKind::Cell: s = block.header ? L"th" : L"td"; break;
		case MdvKind::Details: s = L"details"; break;
		case MdvKind::Footnotes: s = L"footnotes"; break;
		case MdvKind::Footnote: s = L"fn"; break;
	}
	if (!block.children.empty()) {
		s += L'(';
		for (size_t i = 0; i < block.children.size(); i++) {
			if (i)
				s += L',';
			s += Shape(doc, block.children[i]);
		}
		s += L')';
	}
	return s;
}

// a block by its path of child indexes from the document
static const MdvBlock& At(const MdvDocument& doc, std::initializer_list<int> path) {
	int b = 0;
	for (int i : path)
		b = doc.blocks[b].children[i];
	return doc.blocks[b];
}

static int IndexAt(const MdvDocument& doc, std::initializer_list<int> path) {
	int b = 0;
	for (int i : path)
		b = doc.blocks[b].children[i];
	return b;
}

static const MdvBlock* AnchorBlock(const MdvDocument& doc, const wchar_t* name) {
	const int a = doc.FindAnchor(name);
	return a < 0 ? nullptr : &doc.blocks[doc.anchors[a].block];
}

// ---- Blocks ---------------------------------------------------------------------

TEST(Blocks) {
	auto doc = Parse(L"# Title\n\nSome *text* here.\n\n---\n\n```cpp\nint x;\nint y;\n```\n");
	CHECK_STR(Shape(doc), L"doc(h1,p,hr,code)");
	CHECK_STR(At(doc, { 0 }).content.text, L"Title");
	CHECK_STR(At(doc, { 1 }).content.text, L"Some text here.");
	auto& code = At(doc, { 3 });
	CHECK_STR(code.lang, L"cpp");
	CHECK(code.fenced);
	CHECK_STR(code.content.text, L"int x;\nint y;");
	CHECK(code.content.StyleAt(0) & MdvCode);

	auto indented = Parse(L"para\n\n    x\n    y\n");
	CHECK_STR(Shape(indented), L"doc(p,code)");
	CHECK(!At(indented, { 1 }).fenced);
	CHECK_STR(At(indented, { 1 }).content.text, L"x\ny");

	auto levels = Parse(L"## Two\n\nSetext\n===\n\n###### Six\n");
	CHECK_STR(Shape(levels), L"doc(h2,h1,h6)");

	CHECK_STR(Shape(Parse(L"")), L"doc");
}

TEST(InlineStyles) {
	auto doc = Parse(L"a **b** *c* ~~d~~ `e` f");
	auto& c = At(doc, { 0 }).content;
	CHECK_STR(c.text, L"a b c d e f");
	CHECK_INT(c.StyleAt(0), 0);
	CHECK_INT(c.StyleAt(2), MdvBold);
	CHECK_INT(c.StyleAt(4), MdvItalic);
	CHECK_INT(c.StyleAt(6), MdvStrike);
	CHECK_INT(c.StyleAt(8), MdvCode);
	CHECK_INT(c.StyleAt(10), 0);

	auto nested = Parse(L"***both*** _under_");
	auto& n = At(nested, { 0 }).content;
	CHECK_STR(n.text, L"both under");
	CHECK_INT(n.StyleAt(0), MdvBold | MdvItalic);
	CHECK_INT(n.StyleAt(5), MdvItalic);

	// whitespace runs collapse as in HTML
	CHECK_STR(At(Parse(L"a   b"), { 0 }).content.text, L"a b");

	auto math = Parse(L"Euler: $e^{i\\pi}$");
	auto& m = At(math, { 0 }).content;
	CHECK_STR(m.text, L"Euler: e^{i\\pi}");
	CHECK(m.StyleAt(7) & MdvMath);
}

TEST(Breaks) {
	CHECK_STR(At(Parse(L"one\ntwo"), { 0 }).content.text, L"one two");
	CHECK_STR(At(Parse(L"one  \ntwo"), { 0 }).content.text, L"one\ntwo");
	CHECK_STR(At(Parse(L"one\\\ntwo"), { 0 }).content.text, L"one\ntwo");
	CHECK_STR(At(Parse(L"one\ntwo", MdvHardBreaks), { 0 }).content.text, L"one\ntwo");
	CHECK_STR(At(Parse(L"one\r\ntwo"), { 0 }).content.text, L"one two");
}

TEST(SourceMap) {
	auto doc = Parse(L"Hello **world**");
	auto& c = At(doc, { 0 }).content;
	CHECK_STR(c.text, L"Hello world");
	CHECK_INT(c.SourceOf(0), 0);
	CHECK_INT(c.SourceOf(6), 8);
	CHECK_INT(c.SourceOf(10), 12);
	CHECK_INT(c.SourceOf(11), 13);
	CHECK_INT(c.TextOf(8), 6);
	CHECK_INT(c.TextOf(6), 6);        // a '*' maps to the next character's place
	CHECK_INT(c.TextOf(100), 11);

	auto ent = Parse(L"a &amp; b");
	auto& e = At(ent, { 0 }).content;
	CHECK_STR(e.text, L"a & b");
	CHECK_INT(e.SourceOf(2), 2);
	CHECK_INT(e.SourceOf(3), 7);
	CHECK_INT(e.SourceOf(4), 8);
	CHECK_INT(e.TextOf(4), 2);         // inside the entity: the entity

	auto& b = At(doc, { 0 });
	CHECK_INT(b.sourceStart, 0);
	CHECK_INT(b.sourceEnd, 13);
}

TEST(InlineReplace) {
	MdvInline c;
	c.Append(L"Hello ", 0, -1, 0, 6);
	c.Append(L"world", MdvBold, -1, 6, 5);
	CHECK_INT(c.runs.size(), 2);
	CHECK_INT(c.map.size(), 1);        // contiguous exact pieces merge

	c.Replace(6, 5, L"there");
	CHECK_STR(c.text, L"Hello there");
	CHECK_INT(c.StyleAt(6), MdvBold);
	CHECK_INT(c.SourceOf(8), 8);

	c.Replace(0, 5, L"Hi");
	CHECK_STR(c.text, L"Hi there");
	CHECK_INT(c.SourceOf(1), 0);       // "Hi" maps to all of "Hello"
	CHECK_INT(c.SourceOf(3), 6);
	CHECK_INT(c.StyleAt(0), 0);
	CHECK_INT(c.StyleAt(3), MdvBold);

	MdvInline l;
	l.links.push_back({ L"u", L"", 0, 0, false });
	l.links[0].start = 4;
	l.Append(L"see ", 0, -1, 0, 4);
	l.Append(L"link", MdvLinked, 0, 4, 4);
	CHECK_INT(l.links[0].length, 4);
	l.Replace(0, 3, L"look at");
	CHECK_INT(l.links[0].start, 8);
	CHECK_INT(l.links[0].length, 4);
	CHECK_INT(l.LinkAt(8), 0);
	CHECK_INT(l.LinkAt(2), -1);

	MdvInline t;
	t.Append(L"end  \n", 0, -1, 0, 6);
	t.TrimEnd();
	CHECK_STR(t.text, L"end");
	CHECK_INT(t.runs.size(), 1);
	CHECK_INT(t.runs[0].length, 3);
}

// ---- Lists, quotes, alerts, tables --------------------------------------------------

TEST(Lists) {
	auto doc = Parse(L"- a\n- b\n  - c\n\n3. x\n4. y\n");
	CHECK_STR(Shape(doc), L"doc(ul(li(p),li(p,ul(li(p)))),ol(li(p),li(p)))");
	auto& ul = At(doc, { 0 });
	CHECK(ul.tight);
	CHECK(!ul.ordered);
	CHECK_STR(At(doc, { 0, 1, 0 }).content.text, L"b");
	CHECK_STR(At(doc, { 0, 1, 1, 0, 0 }).content.text, L"c");
	auto& ol = At(doc, { 1 });
	CHECK(ol.ordered);
	CHECK_INT(ol.start, 3);
	CHECK_INT(At(doc, { 1, 0 }).number, 3);
	CHECK_INT(At(doc, { 1, 1 }).number, 4);

	auto loose = Parse(L"- a\n\n- b\n");
	CHECK_STR(Shape(loose), L"doc(ul(li(p),li(p)))");
	CHECK(!At(loose, { 0 }).tight);

	// a tight item starting with a span, or with inline HTML
	auto spans = Parse(L"- **bold** item\n- <kbd>K</kbd> key\n");
	CHECK_STR(At(spans, { 0, 0, 0 }).content.text, L"bold item");
	CHECK_INT(At(spans, { 0, 0, 0 }).content.StyleAt(0), MdvBold);
	CHECK_STR(At(spans, { 0, 1, 0 }).content.text, L"K key");
	CHECK(At(spans, { 0, 1, 0 }).content.StyleAt(0) & MdvKbd);
}

TEST(Tasks) {
	auto doc = Parse(L"- [ ] todo\n- [x] done\n- plain\n");
	CHECK(At(doc, { 0, 0 }).task == MdvTask::Unchecked);
	CHECK(At(doc, { 0, 1 }).task == MdvTask::Checked);
	CHECK(At(doc, { 0, 2 }).task == MdvTask::None);
	CHECK_INT(At(doc, { 0, 0 }).taskOffset, 3);
	CHECK_INT(At(doc, { 0, 1 }).taskOffset, 14);
	CHECK_STR(At(doc, { 0, 0, 0 }).content.text, L"todo");
}

TEST(QuotesAndAlerts) {
	CHECK_STR(Shape(Parse(L"> plain\n")), L"doc(quote(p))");

	auto note = Parse(L"> [!NOTE]\n> Be careful.\n");
	CHECK_STR(Shape(note), L"doc(alert(p))");
	CHECK(At(note, { 0 }).alert == MdvAlert::Note);
	CHECK_STR(At(note, { 0, 0 }).content.text, L"Be careful.");
	CHECK_INT(At(note, { 0, 0 }).content.SourceOf(0), 12);

	auto warning = Parse(L"> [!warning]\n>\n> Text\n");
	CHECK_STR(Shape(warning), L"doc(alert(p))");
	CHECK(At(warning, { 0 }).alert == MdvAlert::Warning);
	CHECK_STR(At(warning, { 0, 0 }).content.text, L"Text");

	// the marker must be alone on its line, and a known one
	CHECK_STR(Shape(Parse(L"> [!NOTE] inline text\n")), L"doc(quote(p))");
	CHECK_STR(Shape(Parse(L"> [!FOO]\n> x\n")), L"doc(quote(p))");
	CHECK_STR(Shape(Parse(L"> `[!NOTE]`\n> x\n")), L"doc(quote(p))");
}

TEST(Tables) {
	auto doc = Parse(L"| a | b | c |\n|:--|--:|:-:|\n| 1 | **2** | 3 |\n| 4 | 5 | 6 |\n");
	CHECK_STR(Shape(doc), L"doc(table(tr(th,th,th),tr(td,td,td),tr(td,td,td)))");
	auto& table = At(doc, { 0 });
	CHECK_INT(table.columns, 3);
	CHECK(At(doc, { 0, 0 }).header);
	CHECK(!At(doc, { 0, 1 }).header);
	CHECK(At(doc, { 0, 1, 0 }).align == MdvAlign::Left);
	CHECK(At(doc, { 0, 1, 1 }).align == MdvAlign::Right);
	CHECK(At(doc, { 0, 1, 2 }).align == MdvAlign::Center);
	CHECK_STR(At(doc, { 0, 1, 1 }).content.text, L"2");
	CHECK_INT(At(doc, { 0, 1, 1 }).content.StyleAt(0), MdvBold);

	CHECK_STR(Shape(Parse(L"| a |\n|---|\n| 1 |\n", MdvNoTables)), L"doc(p)");
}

// ---- Links and images -------------------------------------------------------------

TEST(LinksAndImages) {
	auto doc = Parse(L"[site](https://x.com \"T\") and ![alt *text*](pic.png)");
	auto& c = At(doc, { 0 }).content;
	CHECK_STR(c.text, L"site and \uFFFC");
	CHECK_INT(c.links.size(), 1);
	CHECK_STR(c.links[0].url, L"https://x.com");
	CHECK_STR(c.links[0].title, L"T");
	CHECK_INT(c.links[0].start, 0);
	CHECK_INT(c.links[0].length, 4);
	CHECK(c.StyleAt(0) & MdvLinked);
	CHECK_INT(c.LinkAt(0), 0);
	CHECK_INT(c.LinkAt(5), -1);
	CHECK_INT(c.images.size(), 1);
	CHECK_STR(c.images[0].src, L"pic.png");
	CHECK_STR(c.images[0].alt, L"alt text");
	CHECK_INT(c.images[0].pos, 9);
	CHECK_INT(c.ImageAt(9), 0);

	auto badge = Parse(L"[![build](ci.svg)](https://ci)");
	auto& b = At(badge, { 0 }).content;
	CHECK_STR(b.text, L"\uFFFC");
	CHECK_INT(b.LinkAt(0), 0);
	CHECK_STR(b.links[0].url, L"https://ci");
	CHECK_STR(b.images[0].src, L"ci.svg");
	CHECK_STR(b.images[0].alt, L"build");

	auto ref = Parse(L"[text][r]\n\n[r]: https://ref.org \"Ref\"\n");
	CHECK_STR(Shape(ref), L"doc(p)");
	CHECK_STR(At(ref, { 0 }).content.links[0].url, L"https://ref.org");
	CHECK_STR(At(ref, { 0 }).content.links[0].title, L"Ref");

	auto entityUrl = Parse(L"[x](a&amp;b)");
	CHECK_STR(At(entityUrl, { 0 }).content.links[0].url, L"a&b");
}

TEST(Autolinks) {
	auto doc = Parse(L"see https://a.org/x now, www.b.com and me@c.com or <https://d.net>");
	auto& c = At(doc, { 0 }).content;
	CHECK_INT(c.links.size(), 4);
	CHECK_STR(c.links[0].url, L"https://a.org/x");
	CHECK(c.links[0].autolink);
	CHECK_STR(c.links[1].url, L"http://www.b.com");
	CHECK_STR(c.links[2].url, L"mailto:me@c.com");
	CHECK_STR(c.links[3].url, L"https://d.net");
	CHECK_STR(std::wstring(c.text.substr(c.links[0].start, c.links[0].length)), L"https://a.org/x");
}

// ---- Entities, HTML tokens ------------------------------------------------------------

TEST(Entities) {
	std::wstring out;
	CHECK(MdvDecodeEntity(L"&amp;", out));
	CHECK(MdvDecodeEntity(L"&#65;", out));
	CHECK(MdvDecodeEntity(L"&#x1F600;", out));
	CHECK(MdvDecodeEntity(L"&#0;", out));
	CHECK_STR(out, L"&A\U0001F600\uFFFD");
	CHECK(!MdvDecodeEntity(L"&bogus;", out));
	CHECK(!MdvDecodeEntity(L"&#x;", out));
	CHECK(!MdvDecodeEntity(L"amp", out));
	CHECK_STR(MdvDecodeEntities(L"a&lt;b&gt; &nope; &copy; & done"), L"a<b> &nope; \u00A9 & done");

	CHECK_STR(At(Parse(L"&copy; &#x41; &nosuch;"), { 0 }).content.text, L"\u00A9 A &nosuch;");
	CHECK_STR(At(Parse(std::wstring(L"a") + L'\0' + L"b"), { 0 }).content.text, L"a\uFFFDb");
}

TEST(HtmlTokens) {
	auto t = MdvTokenizeHtml(L"<img src=\"a.png\" width=100 alt='x &amp; y'/>text<!-- c --></p>");
	CHECK_INT(t.size(), 4);
	CHECK(t[0].type == MdvHtmlToken::Start);
	CHECK_STR(t[0].name, L"img");
	CHECK(t[0].selfClosing);
	CHECK_STR(*t[0].Attribute(L"src"), L"a.png");
	CHECK_STR(*t[0].Attribute(L"width"), L"100");
	CHECK_STR(*t[0].Attribute(L"alt"), L"x & y");
	CHECK(t[0].Attribute(L"height") == nullptr);
	CHECK(t[1].type == MdvHtmlToken::Text);
	CHECK_INT(t[1].pos, 44);
	CHECK_INT(t[1].length, 4);
	CHECK(t[2].type == MdvHtmlToken::Comment);
	CHECK(t[3].type == MdvHtmlToken::End);
	CHECK_STR(t[3].name, L"p");

	auto lt = MdvTokenizeHtml(L"a < b <3");
	CHECK_INT(lt.size(), 1);
	CHECK(lt[0].type == MdvHtmlToken::Text);

	auto script = MdvTokenizeHtml(L"<script>bad('<p>')</script>ok");
	CHECK_INT(script.size(), 3);
	CHECK_STR(script[1].name, L"script");
	CHECK(script[1].type == MdvHtmlToken::End);
	CHECK(script[2].type == MdvHtmlToken::Text);

	auto upper = MdvTokenizeHtml(L"<DIV Align=CENTER><br><details open>");
	CHECK_STR(upper[0].name, L"div");
	CHECK_STR(*upper[0].Attribute(L"align"), L"CENTER");
	CHECK(upper[1].selfClosing);          // void element
	CHECK(upper[2].Attribute(L"open") != nullptr);

	auto unclosed = MdvTokenizeHtml(L"<a href=\"x");
	CHECK_INT(unclosed.size(), 1);
	CHECK(unclosed[0].type == MdvHtmlToken::Text);
}

// ---- Raw HTML in documents ----------------------------------------------------------

TEST(HtmlBlocks) {
	auto doc = Parse(L"<p align=\"center\">\n  <img src=\"logo.png\" width=\"200\" alt=\"Logo\">\n</p>\n\n"
		L"<h1 align=\"center\">My Tool</h1>\n\nText\n");
	CHECK_STR(Shape(doc), L"doc(p,h1,p)");
	auto& logo = At(doc, { 0 });
	CHECK(logo.align == MdvAlign::Center);
	CHECK_STR(logo.content.text, L"\uFFFC");
	CHECK_STR(logo.content.images[0].src, L"logo.png");
	CHECK_STR(logo.content.images[0].alt, L"Logo");
	CHECK_INT(logo.content.images[0].width, 200);
	auto& title = At(doc, { 1 });
	CHECK_STR(title.content.text, L"My Tool");
	CHECK(title.align == MdvAlign::Center);
	CHECK_STR(title.anchor, L"my-tool");
	CHECK(At(doc, { 2 }).align == MdvAlign::Default);

	// badges: linked images in a centered paragraph
	auto badges = Parse(L"<p align=\"center\">\n<a href=\"https://ci\"><img src=\"b1.svg\"></a>\n<a href=\"https://lic\"><img src=\"b2.svg\"></a>\n</p>\n");
	auto& p = At(badges, { 0 });
	CHECK_STR(p.content.text, L"\uFFFC \uFFFC");
	CHECK_INT(p.content.LinkAt(0), 0);
	CHECK_INT(p.content.LinkAt(2), 1);
	CHECK_STR(p.content.links[1].url, L"https://lic");

	// a div around markdown aligns the blocks in it
	auto div = Parse(L"<div align=\"center\">\n\n**Bold** words\n\n</div>\n\nafter\n");
	CHECK_STR(Shape(div), L"doc(p,p)");
	CHECK(At(div, { 0 }).align == MdvAlign::Center);
	CHECK_STR(At(div, { 0 }).content.text, L"Bold words");
	CHECK(At(div, { 1 }).align == MdvAlign::Default);

	auto rule = Parse(L"<hr>\n\nx\n");
	CHECK_STR(Shape(rule), L"doc(hr,p)");

	// unknown tags are dropped, their text kept; comments go
	auto unknown = Parse(L"<table>\n<tr><td>cell one</td><td>cell two</td></tr>\n</table>\n\n<!-- hidden -->\n");
	CHECK_STR(Shape(unknown), L"doc(p,p)");
	CHECK_STR(At(unknown, { 0 }).content.text, L"cell one");

	CHECK_STR(At(Parse(L"<b>x</b>", MdvNoHtml), { 0 }).content.text, L"<b>x</b>");
}

TEST(Details) {
	auto doc = Parse(L"<details>\n<summary>More <b>info</b></summary>\n\nHidden *text*.\n\n</details>\n\nVisible\n");
	CHECK_STR(Shape(doc), L"doc(details(p),p)");
	auto& d = At(doc, { 0 });
	CHECK_STR(d.content.text, L"More info");
	CHECK_INT(d.content.StyleAt(5), MdvBold);
	CHECK(!d.open);
	CHECK_STR(At(doc, { 0, 0 }).content.text, L"Hidden text.");
	CHECK_STR(At(doc, { 1 }).content.text, L"Visible");

	CHECK(At(Parse(L"<details open>\n<summary>S</summary>\n\nx\n\n</details>\n"), { 0 }).open);

	// inside a list item: closed by the item's end even without </details>
	auto item = Parse(L"- <details>\n  <summary>S</summary>\n\n  inside\n- next\n");
	CHECK_STR(Shape(item), L"doc(ul(li(details(p)),li(p)))");
}

TEST(InlineHtml) {
	auto doc = Parse(L"Press <kbd>Ctrl</kbd>+<kbd>C</kbd> or H<sub>2</sub>O<br>next");
	auto& c = At(doc, { 0 }).content;
	CHECK_STR(c.text, L"Press Ctrl+C or H2O\nnext");
	CHECK(c.StyleAt(6) & MdvKbd);
	CHECK_INT(c.StyleAt(10), 0);
	CHECK(c.StyleAt(17) & MdvSub);
	CHECK_INT(c.StyleAt(18), 0);

	auto link = Parse(L"x <a href=\"https://y\">link</a> z");
	auto& l = At(link, { 0 }).content;
	CHECK_STR(l.text, L"x link z");
	CHECK_STR(l.links[0].url, L"https://y");
	CHECK_INT(l.LinkAt(2), 0);
	CHECK_INT(l.LinkAt(7), -1);

	auto img = Parse(L"Logo <img src=\"x.png\" height=\"20%\" width=\"32px\"> here");
	auto& i = At(img, { 0 }).content;
	CHECK_STR(i.text, L"Logo \uFFFC here");
	CHECK_INT(i.images[0].height, 0);
	CHECK_INT(i.images[0].width, 32);

	// an unclosed tag ends with its paragraph
	auto unclosed = Parse(L"<b>bold\n\nplain");
	CHECK_INT(At(unclosed, { 1 }).content.StyleAt(0), 0);
}

TEST(Anchors) {
	auto doc = Parse(L"<a name=\"here\"></a>\n\n## Title\n\n<a id=\"x\"></a>Some text\n");
	CHECK_STR(Shape(doc), L"doc(h2,p)");
	CHECK(AnchorBlock(doc, L"here") == &At(doc, { 0 }));
	CHECK(AnchorBlock(doc, L"#title") == &At(doc, { 0 }));
	CHECK(AnchorBlock(doc, L"#TITLE") == &At(doc, { 0 }));
	CHECK(AnchorBlock(doc, L"x") == &At(doc, { 1 }));
	CHECK(doc.FindAnchor(L"#none") < 0);
	CHECK_INT(doc.headings.size(), 1);

	auto dup = Parse(L"# A\n# A\n# A b\n# A\n");
	CHECK_STR(At(dup, { 0 }).anchor, L"a");
	CHECK_STR(At(dup, { 1 }).anchor, L"a-1");
	CHECK_STR(At(dup, { 2 }).anchor, L"a-b");
	CHECK_STR(At(dup, { 3 }).anchor, L"a-2");

	auto encoded = Parse(L"# Caf\u00E9 menu\n");
	CHECK(AnchorBlock(encoded, L"#caf%C3%A9-menu") == &At(encoded, { 0 }));
}

TEST(Slugs) {
	CHECK_STR(MdvSlug(L"Hello, World!"), L"hello-world");
	CHECK_STR(MdvSlug(L"API v2.0 (beta)"), L"api-v20-beta");
	CHECK_STR(MdvSlug(L"C++ & C#"), L"c--c");
	CHECK_STR(MdvSlug(L"snake_case-name"), L"snake_case-name");
	CHECK_STR(MdvSlug(L"Caf\u00E9"), L"caf\u00E9");
	CHECK_STR(MdvSlug(L"\U0001F680 Launch"), L"-launch");
	CHECK_STR(MdvSlug(L""), L"");
}

// ---- GitHub's extras ---------------------------------------------------------------------

TEST(Emoji) {
	auto doc = Parse(L":tada: done :nope: `:tada:` :+1:");
	CHECK_STR(At(doc, { 0 }).content.text, L"\U0001F389 done :nope: :tada: \U0001F44D");
	CHECK_STR(At(Parse(L":tada:", MdvNoEmoji), { 0 }).content.text, L":tada:");
	CHECK_STR(At(Parse(L"```\n:tada:\n```"), { 0 }).content.text, L":tada:");
	CHECK_STR(At(Parse(L"time 10:30:00 ok"), { 0 }).content.text, L"time 10:30:00 ok");
	auto& c = At(doc, { 0 }).content;
	CHECK_INT(c.SourceOf(0), 0);
	CHECK_INT(c.SourceOf(3), 7);       // " done": the emoji (two UTF-16 units) maps to all of ":tada:"
}

TEST(Footnotes) {
	// one-word definitions: md4c takes them for link definitions
	auto doc = Parse(L"Text[^a] and[^b].\n\n[^b]: Bee.\n\n[^a]: https://ay.org\n");
	CHECK_STR(Shape(doc), L"doc(p,footnotes(fn(p),fn(p)))");
	auto& p = At(doc, { 0 }).content;
	CHECK_STR(p.text, L"Text1 and2.");
	CHECK(p.StyleAt(4) & MdvSup);
	CHECK(p.StyleAt(4) & MdvFootnoteRef);
	CHECK_STR(p.links[p.LinkAt(4)].url, L"#fn-a");
	CHECK_STR(p.links[p.LinkAt(9)].url, L"#fn-b");
	CHECK_INT(At(doc, { 1, 0 }).footnote, 1);
	CHECK_STR(At(doc, { 1, 0 }).anchor, L"fn-a");
	CHECK_STR(At(doc, { 1, 0, 0 }).content.text, L"https://ay.org \u21A9");
	CHECK_INT(At(doc, { 1, 0, 0 }).content.LinkAt(0), 0);
	CHECK_STR(At(doc, { 1, 1, 0 }).content.text, L"Bee. \u21A9");
	auto& back = At(doc, { 1, 1, 0 }).content;
	CHECK_STR(back.links[back.LinkAt(5)].url, L"#fnref-b");

	CHECK(AnchorBlock(doc, L"fn-a") == &At(doc, { 1, 0, 0 }));
	const int ref = doc.FindAnchor(L"fnref-b");
	CHECK(ref >= 0 && doc.anchors[ref].block == IndexAt(doc, { 0 }) && doc.anchors[ref].pos == 9);

	// longer definitions are paragraphs
	auto words = Parse(L"x[^n] and again[^n]\n\n[^n]: Two *words*.\n");
	CHECK_STR(Shape(words), L"doc(p,footnotes(fn(p)))");
	CHECK_STR(At(words, { 0 }).content.text, L"x1 and again1");
	CHECK_STR(At(words, { 1, 0, 0 }).content.text, L"Two words. \u21A9");
	CHECK_INT(At(words, { 1, 0, 0 }).content.StyleAt(4), MdvItalic);

	// not in code; undefined references stay text; definitions in fenced code aren't any
	auto code = Parse(L"`[^z]` and [^q]\n\n```\n[^z]: code\n```\n");
	CHECK_STR(Shape(code), L"doc(p,code)");
	CHECK_STR(At(code, { 0 }).content.text, L"[^z] and [^q]");
}

TEST(FrontMatter) {
	auto doc = Parse(L"---\ntitle: x\nlist: [a]\n---\n# Doc\n");
	CHECK_STR(doc.frontMatter, L"title: x\nlist: [a]");
	CHECK_STR(Shape(doc), L"doc(h1)");
	CHECK_INT(At(doc, { 0 }).content.SourceOf(0), 29);

	auto open = Parse(L"---\nno close\n");
	CHECK_STR(open.frontMatter, L"");
	CHECK_STR(Shape(open), L"doc(hr,p)");

	auto task = Parse(L"---\na: b\n---\n- [x] t\n");
	CHECK_INT(At(task, { 0, 0 }).taskOffset, 16);
}

// ---- Leaves and source ranges -----------------------------------------------------------

TEST(Leaves) {
	auto doc = Parse(L"# A\n\npara\n\n---\n\n- item\n");
	auto leaves = doc.Leaves();
	CHECK_INT(leaves.size(), 4);
	CHECK(leaves[0] == IndexAt(doc, { 0 }));
	CHECK(leaves[2] == IndexAt(doc, { 2 }));
	CHECK(leaves[3] == IndexAt(doc, { 3, 0, 0 }));
	CHECK_INT(doc.LeafAtSource(0), IndexAt(doc, { 0 }));
	CHECK_INT(doc.LeafAtSource(5), IndexAt(doc, { 1 }));
	CHECK_INT(doc.LeafAtSource(9), IndexAt(doc, { 2 }));      // between blocks: the next one
	CHECK_INT(doc.LeafAtSource(1000), IndexAt(doc, { 3, 0, 0 }));

	// a rule has no text: its range is between its neighbors'
	auto& hr = At(doc, { 2 });
	CHECK_INT(hr.sourceStart, 9);
	CHECK_INT(At(doc, { 3 }).sourceStart, 18);
	CHECK_INT(doc.Root().sourceEnd, doc.source.size());
}

// ---- Export ------------------------------------------------------------------------------

TEST(PlainText) {
	auto doc = Parse(L"# Title\n\nPara with **bold**.\n\n- one\n- two\n  - deep\n\n1. x\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
		L"- [x] done\n\n```\ncode\n```\n");
	CHECK_STR(MdvPlainText(doc), L"Title\n\nPara with bold.\n\n- one\n- two\n  - deep\n\n1. x\n\na\tb\n1\t2\n\n- [x] done\n\ncode");

	const int para = IndexAt(doc, { 1 });
	CHECK_STR(MdvPlainText(doc, { { para, 5 }, { para, 9 } }), L"with");
	CHECK_STR(MdvPlainText(doc, { { para, 9 }, { para, 5 } }), L"with");      // either way round
	const int one = IndexAt(doc, { 2, 0, 0 });
	const int head = IndexAt(doc, { 0 });
	CHECK_STR(MdvPlainText(doc, { { head, 2 }, { one, 3 } }), L"tle\n\nPara with bold.\n\n- one");
	CHECK_STR(MdvPlainText(doc, { { one, 1 }, { one, 3 } }), L"ne");           // no marker from inside the item
	// ending at the start of a leaf takes nothing of it
	CHECK_STR(MdvPlainText(doc, { { head, 0 }, { para, 0 } }), L"Title");

	auto img = Parse(L"An ![image](x.png) here");
	CHECK_STR(MdvPlainText(img), L"An image here");

	auto fn = Parse(L"x[^n]\n\n[^n]: Note text.\n");
	CHECK_STR(MdvPlainText(fn), L"x1\n\n1. Note text. \u21A9");
}

TEST(Html) {
	auto doc = Parse(L"# T\n\nA *b* [c](u \"t\") <x & y>\n\n```js\nx<1\n```\n\n> [!TIP]\n> tip\n");
	const std::wstring html = MdvHtml(doc);
	CHECK(html.find(L"<h1 id=\"t\">T</h1>") != std::wstring::npos);
	CHECK(html.find(L"<p>A <em>b</em> <a href=\"u\" title=\"t\">c</a> &lt;x &amp; y&gt;</p>") != std::wstring::npos);
	CHECK(html.find(L"<pre><code class=\"language-js\">x&lt;1</code></pre>") != std::wstring::npos);
	CHECK(html.find(L"<blockquote>\n<p><strong>Tip</strong></p>\n<p>tip</p>\n</blockquote>") != std::wstring::npos);

	auto table = Parse(L"| a | b |\n|---|:-:|\n| 1 | 2 |\n");
	CHECK_STR(MdvHtml(table), L"<table>\n<thead>\n<tr><th>a</th><th style=\"text-align:center\">b</th></tr>\n</thead>\n"
		L"<tbody>\n<tr><td>1</td><td style=\"text-align:center\">2</td></tr>\n</tbody>\n</table>\n");

	auto list = Parse(L"3. a\n4. [x] b\n");
	CHECK_STR(MdvHtml(list), L"<ol start=\"3\">\n<li><p>a</p>\n</li>\n<li><input type=\"checkbox\" disabled checked> <p>b</p>\n</li>\n</ol>\n");

	auto img = Parse(L"<img src=\"a&quot;.png\" width=\"20\">\n");
	CHECK(MdvHtml(img).find(L"<img src=\"a&quot;.png\" alt=\"\" width=\"20\">") != std::wstring::npos);

	// a range: part of a paragraph
	auto p = Parse(L"one **two** three");
	const int b = IndexAt(p, { 0 });
	CHECK_STR(MdvHtml(p, { { b, 4 }, { b, 7 } }), L"<p><strong>two</strong></p>\n");
}

TEST(CfHtml) {
	const std::string s = MdvCfHtml(L"<b>x\u00E9</b>");
	auto field = [&](const char* name) {
		const size_t p = s.find(name);
		return p == std::string::npos ? (size_t)-1 : (size_t)atoi(s.c_str() + p + strlen(name));
	};
	const size_t startHtml = field("StartHTML:"), endHtml = field("EndHTML:");
	const size_t startFragment = field("StartFragment:"), endFragment = field("EndFragment:");
	CHECK(s.compare(0, 12, "Version:0.9\r") == 0);
	CHECK(s.compare(startHtml, 6, "<html>") == 0);
	CHECK_INT(endHtml, s.size());
	CHECK(s.substr(startFragment, endFragment - startFragment) == "<b>x\xC3\xA9</b>");
}

TEST(SourceRange) {
	auto doc = Parse(L"- one\n- two **b**\n");
	const int one = IndexAt(doc, { 0, 0, 0 }), two = IndexAt(doc, { 0, 1, 0 });
	size_t start, end;
	MdvSourceRange(doc, { { one, 0 }, { two, 5 } }, start, end);
	CHECK_STR(doc.source.substr(start, end - start), L"- one\n- two **b**");
	MdvSourceRange(doc, { { two, 4 }, { two, 5 } }, start, end);
	CHECK_INT(start, 14);
	CHECK_INT(end, 17);
	MdvSourceRange(doc, { { two, 0 }, { two, 3 } }, start, end);
	CHECK_STR(doc.source.substr(start, end - start), L"- two");
	MdvSourceRange(doc, {}, start, end);
	CHECK_INT(start, 0);
	CHECK_INT(end, doc.source.size());
}

// ---- Tables' widths and URLs ----------------------------------------------------------------

TEST(ColumnWidths) {
	const std::vector<MdvColumnMeasure> cols = { { 10, 50 }, { 20, 30 } };
	auto fit = MdvColumnWidths(cols, 200, 10);
	CHECK_INT(fit[0], 50);
	CHECK_INT(fit[1], 30);
	auto narrow = MdvColumnWidths(cols, 50, 10);
	CHECK_INT(narrow[0], 10);
	CHECK_INT(narrow[1], 20);
	auto between = MdvColumnWidths(cols, 70, 10);
	CHECK_INT(between[0] * 10, 260);
	CHECK_INT(between[1] * 10, 240);
	CHECK_INT(MdvColumnWidths({}, 100, 10).size(), 0);
	auto equal = MdvColumnWidths({ { 40, 40 } }, 20, 4);
	CHECK_INT(equal[0], 40);
}

TEST(Urls) {
	CHECK(MdvClassifyUrl(L"#x") == MdvUrlKind::Anchor);
	CHECK(MdvClassifyUrl(L"https://a") == MdvUrlKind::Web);
	CHECK(MdvClassifyUrl(L"HTTP://a") == MdvUrlKind::Web);
	CHECK(MdvClassifyUrl(L"mailto:a@b") == MdvUrlKind::Mail);
	CHECK(MdvClassifyUrl(L"docs/readme.md") == MdvUrlKind::File);
	CHECK(MdvClassifyUrl(L"C:\\x") == MdvUrlKind::File);
	CHECK(MdvClassifyUrl(L"file:///c:/x") == MdvUrlKind::File);
	CHECK(MdvClassifyUrl(L"javascript:alert(1)") == MdvUrlKind::Unsafe);
	CHECK(MdvClassifyUrl(L" java\tscript:x") == MdvUrlKind::Unsafe);
	CHECK(MdvClassifyUrl(L"data:text/html,x") == MdvUrlKind::Unsafe);
	CHECK(MdvClassifyUrl(L"page.html?a=b:c") == MdvUrlKind::File);
	CHECK_STR(MdvPercentDecode(L"a%20b%E2%9C%93"), L"a b\u2713");
	CHECK_STR(MdvPercentDecode(L"100%"), L"100%");
}

TEST(Robustness) {
	std::wstring quotes(400, L'>');
	auto deep = Parse(quotes + L" x");
	CHECK(deep.blocks.size() > 1);
	auto stars = Parse(std::wstring(20000, L'*') + L"a" + std::wstring(5000, L'_'));
	CHECK(stars.blocks.size() > 1);
	auto brackets = Parse(std::wstring(5000, L'[') + L"x" + std::wstring(5000, L']'));
	CHECK(brackets.blocks.size() > 1);
	auto html = Parse(L"<div>\n<details>\n<details>\n</div>\n</details></details></details>\n\nx\n");
	CHECK(html.blocks.size() > 1);
	MdvPlainText(deep);
	MdvHtml(stars);
}

TEST(FootnoteLines) {
	// definitions on consecutive lines are one paragraph to md4c
	auto doc = Parse(L"x[^a] y[^b]\n\n[^a]: First note.\n[^b]: Second *note*.\n");
	CHECK_STR(Shape(doc), L"doc(p,footnotes(fn(p),fn(p)))");
	CHECK_STR(At(doc, { 1, 0, 0 }).content.text, L"First note. \u21A9");
	CHECK_STR(At(doc, { 1, 1, 0 }).content.text, L"Second note. \u21A9");
	CHECK_INT(At(doc, { 1, 1, 0 }).content.StyleAt(7), MdvItalic);
	CHECK_INT(At(doc, { 1, 1, 0 }).content.SourceOf(0), 37);
	// a [^x]: inside a line isn't one
	auto inside = Parse(L"see [^a]: no\n");
	CHECK_STR(Shape(inside), L"doc(p)");
}

TEST(InlineSlice) {
	auto doc = Parse(L"one **two** [three](u) &amp; four");
	auto& c = At(doc, { 0 }).content;
	CHECK_STR(c.text, L"one two three & four");
	auto s = c.Slice(4, 15);
	CHECK_STR(s.text, L"two three &");
	CHECK_INT(s.StyleAt(0), MdvBold);
	CHECK_INT(s.LinkAt(4), 0);
	CHECK_STR(s.links[0].url, L"u");
	CHECK_INT(s.links[0].start, 4);
	CHECK_INT(s.links[0].length, 5);
	CHECK_INT(s.SourceOf(0), 6);
	CHECK_INT(s.SourceOf(10), 23);     // the entity maps whole
	CHECK_STR(c.Slice(10, 3).text, L"");
}

// ---- The flow ------------------------------------------------------------------------------

static MdvFlow Flow(const MdvDocument& doc, const MdvMetrics& m = {}) {
	MdvFlow flow;
	flow.Build(doc, m);
	return flow;
}

TEST(FlowBoxes) {
	auto doc = Parse(L"# T\n\npara\n\n- a\n- b\n  1. c\n\n> q\n\n```\nc\n```\n\n---\n\n| a |\n|---|\n| 1 |\n");
	auto flow = Flow(doc);
	CHECK_INT(flow.boxes.size(), 9);
	CHECK(flow.boxes[8].kind == MdvBoxKind::Table);
	auto& h = flow.boxes[0];
	CHECK(h.kind == MdvBoxKind::Text);
	CHECK_INT(std::lround(h.scale * 100), 200);
	CHECK(h.padBottom > 0);                       // the line under h1
	CHECK_INT(flow.boxes[1].indent, 0);
	CHECK_INT(flow.boxes[1].marginBottom, 16);
	auto& a = flow.boxes[2];
	CHECK_INT(a.indent, 32);
	CHECK(a.marker == MdvMarker::Disc);
	CHECK_INT(a.marginBottom, 0);                 // tight
	auto& b = flow.boxes[3];
	CHECK_INT(b.marginTop, 4);
	auto& c = flow.boxes[4];
	CHECK_INT(c.indent, 64);
	CHECK(c.marker == MdvMarker::Number);
	CHECK_INT(c.markerBlock, IndexAt(doc, { 2, 1, 1, 0 }));
	CHECK_INT(c.marginBottom, 16);                // the outer list's space, on its last box
	CHECK_INT(flow.boxes[5].indent, 20);          // in the quote
	CHECK(flow.boxes[5].muted);
	CHECK_INT(flow.spans.size(), 1);
	CHECK_INT(flow.spans[0].first, 5);
	CHECK_INT(flow.spans[0].last, 5);
	CHECK(flow.boxes[6].kind == MdvBoxKind::Code);
	CHECK_INT(flow.boxes[6].padLeft, 16);
	CHECK(flow.boxes[7].kind == MdvBoxKind::Rule);
	CHECK(flow.boxes[7].measured);

	auto tasks = Flow(Parse(L"- [x] done\n- [ ] todo\n-\n"));
	CHECK(tasks.boxes[0].marker == MdvMarker::Checked);
	CHECK(tasks.boxes[1].marker == MdvMarker::Unchecked);
	CHECK_INT(tasks.boxes.size(), 3);             // an empty item still has a line for its marker

	auto nested = Flow(Parse(L"- a\n  - b\n    - c\n"));
	CHECK(nested.boxes[1].marker == MdvMarker::Circle);
	CHECK(nested.boxes[2].marker == MdvMarker::Square);
}

TEST(FlowContainers) {
	auto alert = Parse(L"> [!TIP]\n> one\n>\n> two\n\nafter\n");
	auto flow = Flow(alert);
	CHECK_INT(flow.boxes.size(), 4);
	CHECK(flow.boxes[0].kind == MdvBoxKind::AlertTitle);
	CHECK(!flow.boxes[1].muted);                  // an alert's text isn't muted
	CHECK_INT(flow.spans[0].first, 0);
	CHECK_INT(flow.spans[0].last, 2);

	auto details = Parse(L"<details>\n<summary>S</summary>\n\ninside\n\n</details>\n\nafter\n");
	auto closed = Flow(details);
	CHECK_INT(closed.boxes.size(), 2);
	CHECK(closed.boxes[0].kind == MdvBoxKind::Summary);
	CHECK(closed.boxes[0].padLeft > 0);
	details.blocks[IndexAt(details, { 0 })].open = true;
	auto open = Flow(details);
	CHECK_INT(open.boxes.size(), 3);
	CHECK_INT(open.boxes[1].block, IndexAt(details, { 0, 0 }));

	auto notes = Flow(Parse(L"x[^1]\n\n[^1]: note\n"));
	CHECK_INT(notes.boxes.size(), 3);
	CHECK(notes.boxes[1].kind == MdvBoxKind::Rule);
	CHECK(notes.boxes[2].marker == MdvMarker::Number);
	CHECK_INT(std::lround(notes.boxes[2].scale * 100), 85);
}

TEST(FlowPlace) {
	auto doc = Parse(L"# T\n\none\n\ntwo\n");
	MdvMetrics m;
	auto flow = Flow(doc, m);
	for (auto& box : flow.boxes)
		box.height = 20;
	flow.Place();
	CHECK_INT(flow.boxes[0].top, 16);             // the padding
	const float hBottom = flow.boxes[0].Bottom();
	CHECK_INT(flow.boxes[1].top, hBottom + 16);   // max(heading's 16, paragraph's 0)
	CHECK_INT(flow.boxes[2].top, flow.boxes[1].Bottom() + 16);
	CHECK_INT(flow.height, flow.boxes[2].Bottom() + 16);        // the padding
	CHECK_INT(flow.BoxAt(0), 0);
	CHECK_INT(flow.BoxAt(20), 0);
	CHECK_INT(flow.BoxAt(hBottom + 1), 1);        // in the gap: the next one
	CHECK_INT(flow.BoxAt(flow.boxes[2].top + 5), 2);
	CHECK_INT(flow.BoxAt(10000), 3);
	CHECK_INT(flow.BoxOfBlock(IndexAt(doc, { 1 })), 1);

	// placing from a box keeps the ones above
	flow.boxes[1].height = 50;
	flow.Place(1);
	CHECK_INT(flow.boxes[0].top, 16);
	CHECK_INT(flow.boxes[2].top, flow.boxes[1].Bottom() + 16);
}

TEST(FlowEstimate) {
	auto doc = Parse(L"short\n\n" + std::wstring(400, L'x') + L"\n\n```\na\nb\nc\n```\n");
	MdvMetrics m;
	auto flow = Flow(doc, m);
	flow.Estimate(doc, m, 400);
	CHECK_INT(flow.boxes[0].height, 24);          // a line of 1.5em
	// 400 characters at 8 DIPs in 400: 8 lines
	CHECK_INT(flow.boxes[1].height, 8 * 24);
	CHECK_INT(std::lround(flow.boxes[2].height * 100), std::lround(3 * 1.45f * 16 * 0.875f * 100));
}

int wmain() {
	Run_Blocks();
	Run_InlineStyles();
	Run_Breaks();
	Run_SourceMap();
	Run_InlineReplace();
	Run_Lists();
	Run_Tasks();
	Run_QuotesAndAlerts();
	Run_Tables();
	Run_LinksAndImages();
	Run_Autolinks();
	Run_Entities();
	Run_HtmlTokens();
	Run_HtmlBlocks();
	Run_Details();
	Run_InlineHtml();
	Run_Anchors();
	Run_Slugs();
	Run_Emoji();
	Run_Footnotes();
	Run_FrontMatter();
	Run_Leaves();
	Run_PlainText();
	Run_Html();
	Run_CfHtml();
	Run_SourceRange();
	Run_ColumnWidths();
	Run_Urls();
	Run_Robustness();
	Run_FootnoteLines();
	Run_InlineSlice();
	Run_FlowBoxes();
	Run_FlowContainers();
	Run_FlowPlace();
	Run_FlowEstimate();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
