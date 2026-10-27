#pragma once

// The markdown view's state, kept out of the public header: the document, its flow, the style and colors, and the
// Direct2D and DirectWrite resources with the text layouts made for the boxes.

#include "MarkdownView.h"
#include "MarkdownViewLayout.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
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

struct MdvLayoutCache {
	ComPtr<IDWriteTextLayout> layout;
	std::vector<ComPtr<IDWriteInlineObject>> images;
	std::vector<MdvBackRect> backs;     // from the layout's origin
	float width = -1;                   // the text width it was made for
	float height = 0;
	float firstLine = 0;                // the first line's height and baseline, for the list markers
	float firstBaseline = 0;
	bool effects = false;               // the links' brushes set (made again with the target or the colors)
};

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

	UINT dpi = 96;
	SIZE client{};                      // pixels
	float scrollY = 0;                  // DIPs
	int wheelDelta = 0;
	int darkScrollBars = -1;
	int lastTopHeading = -2;

	ComPtr<ID2D1HwndRenderTarget> target;
	std::unordered_map<COLORREF, ComPtr<ID2D1SolidColorBrush>> brushes;
	std::map<std::tuple<int, int, int, int>, ComPtr<IDWriteTextFormat>> formats;   // family, size, weight, line height
	std::unordered_map<uint64_t, MdvLayoutCache> layouts;    // by block and box kind
	std::wstring bodyFamily, codeFamily, iconFamily;
	MdvFontMetrics bodyFont, codeFont;

	static uint64_t Key(int block, MdvBoxKind kind) { return (uint64_t)block << 8 | (uint64_t)kind; }

	float Scale() const { return dpi / 96.0f; }
	ID2D1SolidColorBrush* Brush(COLORREF color);
	IDWriteTextFormat* Format(int family, float size, DWRITE_FONT_WEIGHT weight, float lineHeight);   // 0 body, 1 code, 2 icons
};
