#pragma once

// The color picker's window-free parts: HSV <-> RGB, hex text, the layout of the parts, mapping points to
// values and back, and drawing into 32-bit pixel buffers. Covered by ColorPickerTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

// ---- Colors ------------------------------------------------------------------------

// h 0..360 (360 is red again, kept so a marker at the bottom of the hue bar stays there), s and v 0..1
struct CpHsv {
	double h = 0;
	double s = 0;
	double v = 0;

	bool operator==(const CpHsv&) const = default;
};

COLORREF CpHsvToRgb(const CpHsv& hsv);
// prev supplies what the color leaves undefined: the hue of a gray, and the hue and saturation of black
CpHsv    CpRgbToHsv(COLORREF color, const CpHsv& prev = {});
// prev itself if it already gives the color (an app echoing the color back doesn't move the markers)
CpHsv    CpUpdateHsv(const CpHsv& prev, COLORREF color);
// h wrapped into 0..360, s and v clamped to 0..1; NaNs become 0
CpHsv    CpNormalize(const CpHsv& hsv);

// "#RRGGBB", or "#AARRGGBB" with alpha
std::wstring CpFormatHex(COLORREF color, BYTE alpha = 255, bool withAlpha = false);
// "#RGB", "#RRGGBB" or "#AARRGGBB", the '#' optional, spaces around ignored. Without alpha in the text,
// *pAlpha isn't touched and *pHasAlpha is false.
bool     CpParseHex(std::wstring_view text, COLORREF* pColor, BYTE* pAlpha = nullptr, bool* pHasAlpha = nullptr);

// ---- Layout --------------------------------------------------------------------------

struct CpMetrics {         // pixels at the window's DPI
	int pad = 8;           // around the parts (room for the markers that stick out)
	int barWidth = 20;
	int gap = 12;          // between the parts
};

struct CpLayout {          // the gradients; each has a 1px frame outside it
	RECT sv{};
	RECT hue{};
	RECT alpha{};          // empty without the alpha bar
};

// the SV area takes what the bars leave, the bars are on its right
CpLayout CpComputeLayout(int width, int height, const CpMetrics& m, bool alpha);

// a point anywhere (clamped into the area) to saturation (left to right) and value (bottom to top)
void     CpSvFromPoint(const RECT& rc, POINT pt, double* pS, double* pV);
POINT    CpPointFromSv(const RECT& rc, double s, double v);
// a bar from top (0) to bottom (1); y clamped
double   CpBarFromY(const RECT& rc, int y);
int      CpYFromBar(const RECT& rc, double t);
inline double CpHueFromBar(double t) { return t * 360; }
inline double CpBarFromHue(double h) { return h / 360; }
BYTE     CpAlphaFromBar(double t);          // opaque at the top
double   CpBarFromAlpha(BYTE alpha);

// ---- Pixels ------------------------------------------------------------------------
//
// Top-down 32-bit buffers, 0x00RRGGBB (as a DIB section stores BGRA), rows of width pixels.

struct CpPixels {
	uint32_t* bits = nullptr;
	int       width = 0;
	int       height = 0;

	uint32_t& At(int x, int y) const { return bits[(size_t)y * width + x]; }
};

inline uint32_t CpPixel(COLORREF c) { return (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c); }
inline COLORREF CpColor(uint32_t p) { return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF); }
// a over b, alpha 0..1
uint32_t CpBlend(uint32_t b, uint32_t a, double alpha);

// all of px: saturation across, value down, at the hue
void     CpRenderSv(const CpPixels& px, double hue);
// all of px: the hues from top (0) to bottom (360)
void     CpRenderHue(const CpPixels& px);
// all of px: the color from opaque at the top to transparent at the bottom, over a checkerboard of cell pixels
void     CpRenderAlpha(const CpPixels& px, COLORREF color, int cell, COLORREF check1, COLORREF check2);
// the checkerboard under a color with alpha (also for swatches); cells counted from (x0, y0)
uint32_t CpChecker(int x, int y, int cell, COLORREF check1, COLORREF check2);

void     CpFillRect(const CpPixels& px, const RECT& rc, COLORREF color);   // clipped to px
void     CpFrameRect(const CpPixels& px, const RECT& rc, COLORREF color, int thickness = 1);   // inside rc
void     CpCopy(const CpPixels& dst, int x, int y, const CpPixels& src);   // clipped
// an antialiased ring centered on (cx, cy) (pixel centers are at +0.5), radius to the middle of its line
void     CpDrawRing(const CpPixels& px, double cx, double cy, double radius, double thickness, COLORREF color);
