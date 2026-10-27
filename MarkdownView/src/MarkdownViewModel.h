#pragma once

// The markdown view's model: a document parsed by md4c into a tree of blocks, each leaf with its inline content as
// one string (what a DirectWrite layout draws) plus style runs, links and images, and a map back to the source text,
// so positions can be kept as source offsets. GitHub's extras are added on top: alerts, footnotes, emoji shortcodes,
// heading anchors, YAML front matter, and a subset of raw HTML (align, img sizes, details, br, kbd, sub, sup ...).
// Window-free: covered by MarkdownViewTests.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

constexpr size_t MDV_NOPOS = (size_t)-1;

// parse flags (the control's MDVF_*)
constexpr unsigned MdvNoHtml = 0x0001;        // raw HTML is shown as text
constexpr unsigned MdvNoTables = 0x0002;
constexpr unsigned MdvHardBreaks = 0x0004;    // every line break is a hard one
constexpr unsigned MdvNoEmoji = 0x0008;       // :shortcodes: stay text

// ---- Inline content ----------------------------------------------------------------

enum MdvStyle : uint16_t {
	MdvBold = 0x0001,
	MdvItalic = 0x0002,
	MdvStrike = 0x0004,
	MdvUnderline = 0x0008,
	MdvCode = 0x0010,          // a code span, or a code block's text
	MdvLinked = 0x0020,        // inside a link (MdvRun::link)
	MdvSub = 0x0040,
	MdvSup = 0x0080,
	MdvKbd = 0x0100,
	MdvMath = 0x0200,          // $...$ shown as its source
	MdvFootnoteRef = 0x0400,   // a footnote's number (also MdvSup and MdvLinked)
	MdvMark = 0x0800,          // <mark>
};

struct MdvRun {                // the runs cover the text, in order, with no gaps
	size_t start = 0;
	size_t length = 0;
	uint16_t style = 0;
	int link = -1;             // index in MdvInline::links
};

struct MdvLink {
	std::wstring url;
	std::wstring title;
	size_t start = 0;          // the text it covers
	size_t length = 0;
	bool autolink = false;
};

struct MdvImage {
	std::wstring src;
	std::wstring alt;
	std::wstring title;
	size_t pos = 0;            // its U+FFFC in the text
	int width = 0;             // CSS pixels from <img width>; 0: unknown
	int height = 0;
};

// A piece of the text and where it came from. When text and source lengths are equal the piece maps character
// by character; otherwise (an entity, an emoji shortcode, a synthetic break) all of it maps to its whole source.
struct MdvSegment {
	size_t text = 0;
	size_t length = 0;
	size_t source = 0;
	size_t sourceLength = 0;
};

constexpr wchar_t MdvObjectChar = 0xFFFC;    // an image in the text

struct MdvInline {
	std::wstring text;
	std::vector<MdvRun> runs;
	std::vector<MdvLink> links;
	std::vector<MdvImage> images;
	std::vector<MdvSegment> map;

	void Append(std::wstring_view s, uint16_t style, int link, size_t source, size_t sourceLength);
	// replaces text[pos, pos + count) with s; the new text takes the style of the old one's first character plus
	// addStyle, the link given (or the old one's if -1) and maps to the old text's whole source
	void Replace(size_t pos, size_t count, std::wstring_view s, uint16_t addStyle = 0, int link = -1);
	void TrimEnd();                                // drops trailing spaces and line breaks
	MdvInline Slice(size_t from, size_t to) const; // a copy of text[from, to) with its runs, links, images and map

	uint16_t StyleAt(size_t pos) const;
	int LinkAt(size_t pos) const;
	int ImageAt(size_t pos) const;                 // the image whose U+FFFC is at pos, or -1
	size_t SourceOf(size_t pos) const;             // MDV_NOPOS with no map
	size_t SourceEnd() const;                      // past the last mapped source character
	size_t TextOf(size_t source) const;            // the text position at or after a source offset
};

// ---- Blocks ------------------------------------------------------------------------

enum class MdvKind : uint8_t {
	Document,
	Paragraph,
	Heading,
	Quote,
	Alert,          // GitHub's > [!NOTE]
	List,
	Item,
	Code,
	Rule,
	Table,
	Row,
	Cell,
	Details,        // <details>; its content is the summary
	Footnotes,      // the section at the document's end
	Footnote,       // a definition: children are its blocks
};

enum class MdvAlign : uint8_t { Default, Left, Center, Right };
enum class MdvAlert : uint8_t { None, Note, Tip, Important, Warning, Caution };
enum class MdvTask : uint8_t { None, Unchecked, Checked };

struct MdvBlock {
	MdvKind kind = MdvKind::Paragraph;
	int parent = -1;
	std::vector<int> children;

	MdvInline content;            // Paragraph, Heading, Code, Cell, Details (the summary)
	MdvAlign align = MdvAlign::Default;

	int level = 0;                // Heading: 1..6
	std::wstring anchor;          // Heading, Footnote: the name a #link goes to
	std::wstring lang;            // Code: the info string's first word
	bool fenced = false;          // Code

	bool ordered = false;         // List
	bool tight = false;           // List
	unsigned start = 1;           // List: an ordered list's first number
	wchar_t mark = L'-';          // List: '-', '+', '*', or '.' / ')' for an ordered one
	unsigned number = 0;          // Item: its number in an ordered list
	MdvTask task = MdvTask::None; // Item
	size_t taskOffset = MDV_NOPOS;// Item: the source offset of the character between [ and ]

	MdvAlert alert = MdvAlert::None;
	bool open = false;            // Details: <details open>

	unsigned columns = 0;         // Table
	bool header = false;          // Row, Cell: in the table's head
	int footnote = 0;             // Footnote: its number (1-based, by first reference; 0: never referenced)

	size_t sourceStart = MDV_NOPOS; // the source it was parsed from, roughly (md4c has no block offsets)
	size_t sourceEnd = MDV_NOPOS;

	bool HasContent() const {
		return kind == MdvKind::Paragraph || kind == MdvKind::Heading || kind == MdvKind::Code ||
			kind == MdvKind::Cell || kind == MdvKind::Details;
	}
};

struct MdvAnchor {
	std::wstring name;            // a heading's slug, "fn-label", "fnref-label"
	int block = 0;
	size_t pos = 0;               // in the block's content
};

struct MdvDocument {
	std::wstring source;
	std::wstring frontMatter;     // the YAML between the --- lines, not shown
	std::vector<MdvBlock> blocks; // [0] is the document
	std::vector<int> headings;    // in order
	std::vector<MdvAnchor> anchors;

	const MdvBlock& Root() const { return blocks[0]; }
	int FindAnchor(std::wstring_view name) const;   // a #fragment (leading '#' and %XX allowed), or -1 (index in anchors)
	std::vector<int> Leaves() const;                // the blocks with content (and rules), in document order
	int LeafAtSource(size_t offset) const;          // the leaf holding or after a source offset, or -1
};

bool MdvParse(std::wstring source, unsigned flags, MdvDocument& doc);

// ---- Text helpers --------------------------------------------------------------------

std::wstring MdvSlug(std::wstring_view text);       // GitHub's: lowercase, punctuation dropped, spaces to '-'
bool MdvDecodeEntity(std::wstring_view entity, std::wstring& out);   // "&amp;", "&#123;", "&#x1F600;"
std::wstring MdvDecodeEntities(std::wstring_view text);
const wchar_t* MdvEmoji(std::wstring_view name);    // "smile" -> the emoji, or null
std::wstring MdvPercentDecode(std::wstring_view s); // %XX as UTF-8

enum class MdvUrlKind : uint8_t { Anchor, Web, Mail, File, Unsafe };
MdvUrlKind MdvClassifyUrl(std::wstring_view url);  // javascript:, vbscript:, data: are Unsafe for links

// images
bool MdvDecodeDataUrl(std::wstring_view url, std::string& bytes, std::wstring& mime);   // data:[mime][;base64],...
std::wstring MdvResolvePath(std::wstring_view base, std::wstring_view url);  // a local file's path, or empty
bool MdvSvgSize(std::string_view svg, float& width, float& height);        // the root's size in CSS pixels
bool MdvIsSvg(std::string_view bytes);

// An SVG's <text> elements, which Direct2D's SVG support doesn't draw: drawn over it with DirectWrite. Attributes
// come from the <g> elements around them; transforms are only scale() and translate().
struct MdvSvgText {
	std::wstring text;
	std::wstring family;          // the first of font-family's
	float x = 0, y = 0;           // the baseline's start (or middle, or end), in the SVG's user units
	float size = 16;
	bool bold = false;
	int anchor = 0;               // 0 start, 1 middle, 2 end
	uint32_t color = 0;           // a COLORREF
	float opacity = 1;
};
std::vector<MdvSvgText> MdvSvgTexts(std::string_view svg);

// ---- Raw HTML ------------------------------------------------------------------------

struct MdvHtmlToken {
	enum Type : uint8_t { Text, Start, End, Comment } type = Text;
	std::wstring name;            // tags: lowercase
	std::vector<std::pair<std::wstring, std::wstring>> attributes;   // names lowercase, values decoded
	bool selfClosing = false;
	size_t pos = 0;               // in the HTML given
	size_t length = 0;
	const std::wstring* Attribute(std::wstring_view key) const;
};

std::vector<MdvHtmlToken> MdvTokenizeHtml(std::wstring_view html);

// ---- Selection ranges and export ----------------------------------------------------

struct MdvTextPos {               // a place in a leaf's content
	int block = -1;
	size_t pos = 0;
	bool operator==(const MdvTextPos&) const = default;
};

struct MdvRange {                 // [first, last) in document order; first.block -1: the whole document
	MdvTextPos first;
	MdvTextPos last;
};

std::wstring MdvPlainText(const MdvDocument& doc, const MdvRange& range = {});
std::wstring MdvHtml(const MdvDocument& doc, const MdvRange& range = {});
std::string MdvCfHtml(std::wstring_view fragment);    // the CF_HTML clipboard format (UTF-8, with its header)
void MdvSourceRange(const MdvDocument& doc, const MdvRange& range, size_t& start, size_t& end);   // whole lines at the ends

// ---- Tables --------------------------------------------------------------------------

struct MdvColumnMeasure {
	float minWidth = 0;           // the widest word in the column's cells
	float maxWidth = 0;           // the widest cell unwrapped
};

// The columns' content widths in an available width (padding is added to every cell): their unwrapped widths
// when those fit, else the room past the minimums shared in proportion to (max - min), as HTML's auto table layout.
// When even the minimums don't fit, the table scrolls: each column is then as wide as its text unwrapped up to
// overflowCap (0: its minimum), as GitHub's tables are.
std::vector<float> MdvColumnWidths(const std::vector<MdvColumnMeasure>& columns, float available, float padding, float overflowCap = 0);
