#pragma once

// The markdown view's state, kept out of the public header: the document, its flow, the style and colors, the
// Direct2D and DirectWrite resources with the text layouts made for the boxes, and the images.

#include "MarkdownView.h"
#include "MarkdownViewLayout.h"

#include <d2d1_3.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Microsoft::WRL::ComPtr;

ID2D1Factory* MdvD2DFactory();
IDWriteFactory* MdvDWriteFactory();

// a font family's ascent and descent, of its em
struct MdvFontMetrics {
	float ascent = 0.9f;
	float descent = 0.25f;
	float lineGap = 0;
};

// the shade behind a run of text: a code span, a key, <mark>
struct MdvBackRect {
	enum Kind : uint8_t { Code, Kbd, Mark } kind = Code;
	D2D1_RECT_F rect{};
};

// a code block's colored run: a built-in token's kind, or the app's highlighter's color
struct MdvSyntaxRun {
	UINT start = 0;
	UINT length = 0;
	int kind = -1;                      // MDVSYN_*, or -1 for color
	COLORREF color = 0;
};

// a laid out text: a box's, or a table cell's
struct MdvText {
	ComPtr<IDWriteTextLayout> layout;
	std::vector<ComPtr<IDWriteInlineObject>> images;
	std::vector<MdvBackRect> backs;     // from the layout's origin
	std::vector<MdvSyntaxRun> syntax;
	float firstLine = 0;                // the first line's height and baseline, for the list markers
	float firstBaseline = 0;
	bool effects = false;               // the links' brushes set (made again with the target or the colors)
};

struct MdvLayoutCache {
	MdvText text;
	std::vector<MdvText> cells;         // a table's, row by row
	std::vector<float> columns;         // a table's columns' content widths
	std::vector<float> rowTops;         // from the table's top
	std::vector<float> rowHeights;
	float width = -1;                   // the text width it was made for
	float height = 0;
	float contentWidth = 0;             // a code block's or a table's: wider than the box, it scrolls sideways
};

// an image, by its source: loaded on the worker thread, or given by the app
struct MdvImageEntry {
	enum State : uint8_t { Waiting, Loading, Ready, Failed } state = Loading;
	float width = 0, height = 0;        // CSS pixels
	UINT pixelWidth = 0, pixelHeight = 0;
	std::vector<BYTE> pixels;           // premultiplied BGRA
	std::string svg;                    // an SVG's text
	std::vector<MdvSvgText> svgTexts;   // its <text>s, which Direct2D doesn't draw
	ComPtr<ID2D1Bitmap> bitmap;         // made on the target when first drawn
	ComPtr<ID2D1SvgDocument> svgDocument;
	std::unordered_set<uint64_t> users; // the layouts showing it
};

struct MdvImageJob {
	std::wstring key;
	std::wstring path;                  // a file to read, or
	std::string bytes;                  // the image itself
};

struct MdvImageResult {
	std::wstring key;
	bool ok = false;
	bool svg = false;
	std::string svgText;
	std::vector<MdvSvgText> svgTexts;
	float width = 0, height = 0;
	UINT pixelWidth = 0, pixelHeight = 0;
	std::vector<BYTE> pixels;
};

// the worker thread's queue; shared with the thread, which outlives the window if it must
struct MdvLoader {
	std::mutex lock;
	std::condition_variable wake;
	std::deque<MdvImageJob> jobs;
	std::vector<MdvImageResult> done;
	HWND hwnd = nullptr;
	bool stop = false;
	bool running = false;
};

// a table's cells' padding (GitHub's 6px 13px at 16px, in ems) and borders, and the room under a table that
// scrolls sideways, for its bar
constexpr float MdvCellPadX = 0.8125f, MdvCellPadY = 0.375f, MdvTableBorder = 1, MdvTableBar = 10;

constexpr int MdvAlignMiddle = 1;        // a format's paragraphs centered vertically
constexpr int MdvAlignCenter = 2;        // and its lines horizontally

struct MdvState {
	MdvDocument doc;
	MdvFlow flow;
	MdvMetrics metrics;
	std::vector<int> blockBox;          // block -> its first box, -1 if not shown
	UINT parseFlags = 0;
	std::wstring basePath;

	MDVSTYLE style{};
	bool customColors = false;
	MDVCOLORS colors{};
	int zoom = 100;
	MDVHIGHLIGHTPROC highlighter = nullptr;
	LPARAM highlighterParam = 0;

	UINT dpi = 96;
	SIZE client{};                      // pixels
	float scrollY = 0;                  // DIPs
	int wheelDelta = 0;
	int darkScrollBars = -1;
	std::unordered_map<int, float> sideways;   // block -> how far a code block or a table is scrolled

	// the mouse
	int hotBox = -1;                    // the code block under the mouse: its copy button shows
	bool hotCopy = false;
	int copiedBlock = -1;               // the copy button shows a check mark for a moment
	int dragBox = -1;                   // a sideways bar being dragged
	float dragGrab = 0;                 // where on the thumb
	bool tracking = false;

	// the selection (anchor: where it started; caret: where the mouse took it) and the finds
	std::vector<int> leaves;            // the text leaves in document order
	std::vector<int> leafOrder;         // block -> its place in leaves, -1
	MdvTextPos anchor, caret;           // block -1: no selection
	bool selecting = false;             // a press in the text, until the button goes up
	bool dragged = false;
	POINT pressPoint{};
	int pressLeaf = -1, pressLink = -1; // a press on a link: a click unless it becomes a drag
	int clicks = 0;                     // 2: words, 3: blocks
	DWORD lastClick = 0;
	POINT lastClickPoint{};
	POINT mouse{};                      // while selecting, for scrolling at the edges
	int hotLeaf = -1, hotLink = -1;     // the link under the mouse
	HWND tip = nullptr;
	std::wstring findText;              // the last Find's, for F3
	UINT findFlags = 0;
	std::unordered_map<int, std::vector<std::pair<size_t, size_t>>> highlights;   // leaf -> HighlightAll's matches

	bool Before(const MdvTextPos& a, const MdvTextPos& b) const;
	bool HasSelection() const { return anchor.block >= 0 && caret.block >= 0 && !(anchor == caret); }
	bool Selected(int leaf, size_t length, size_t& start, size_t& end) const;   // the part of a leaf selected

	ComPtr<ID2D1HwndRenderTarget> target;
	std::unordered_map<COLORREF, ComPtr<ID2D1SolidColorBrush>> brushes;
	std::map<std::tuple<int, int, int, int>, ComPtr<IDWriteTextFormat>> formats;   // family, size, weight, line height
	std::unordered_map<uint64_t, MdvLayoutCache> layouts;    // by block and box kind
	std::wstring bodyFamily, codeFamily, iconFamily;
	MdvFontMetrics bodyFont, codeFont;

	std::unordered_map<std::wstring, std::shared_ptr<MdvImageEntry>> images;
	std::shared_ptr<MdvLoader> loader;

	static uint64_t Key(int block, MdvBoxKind kind) { return (uint64_t)block << 8 | (uint64_t)kind; }

	float Scale() const { return dpi / 96.0f; }
	ID2D1SolidColorBrush* Brush(COLORREF color);
	// family: 0 body, 1 code, 2 icons; align: MdvAlign* bits
	IDWriteTextFormat* Format(int family, float size, DWRITE_FONT_WEIGHT weight, float lineHeight, int align = 0);
	void ResetEffects();
};

// images (MarkdownViewImages.cpp)
std::wstring MdvImageKey(const MdvState& s, const std::wstring& src);
std::shared_ptr<MdvImageEntry> MdvRequestImage(MdvState& s, HWND hwnd, const std::wstring& src);
void MdvQueueImage(MdvState& s, HWND hwnd, MdvImageJob job);
void MdvStopLoader(MdvState& s);
std::vector<uint64_t> MdvTakeLoadedImages(MdvState& s);   // the layouts to make again
IDWriteInlineObject* MdvCreateImageObject(std::shared_ptr<MdvImageEntry> entry, float width, float height, const std::wstring& alt);

// what the images' inline objects draw with while a text is painted
struct MdvPaintContext {
	ID2D1RenderTarget* target = nullptr;
	ID2D1Brush* frame = nullptr;
	ID2D1Brush* altText = nullptr;
	IDWriteTextFormat* altFormat = nullptr;
};
extern MdvPaintContext g_mdvPaint;
