#pragma once

// The color picker's window-free parts: HSV <-> RGB, hex text, the layout of the parts, mapping points to
// values and back, and drawing into 32-bit pixel buffers. Covered by ColorPickerTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

// The panel on the right: the new/old swatch above, the fields below in two columns: H S V A | R G B Hex.

enum CpField { CpH, CpS, CpV, CpA, CpR, CpG, CpB, CpHex, CpFieldCount };   // also the tab order

// Under the fields, the swatch groups: the palette's rows, a row of custom colors and a row of recent ones,
// all in CpSwatchColumns columns.

constexpr int CpSwatchColumns = 12;
constexpr int CpMaxPalette = 4 * CpSwatchColumns;
constexpr int CpMaxCustom = CpSwatchColumns;
constexpr int CpMaxRecent = CpSwatchColumns;

enum CpGroup { CpPalette, CpCustom, CpRecent, CpGroupCount };

struct CpPanelMetrics {    // pixels at the window's DPI
	int labelWidth = 28;
	int fieldWidth = 64;   // the frame, spin buttons included
	int fieldHeight = 22;
	int rowGap = 6;
	int colGap = 10;
	int swatchHeight = 56;
	int swatchGap = 12;    // between the swatch, the fields and the swatch groups
	int spinWidth = 14;
	int buttonGap = 6;     // between the swatch and the eyedropper button (fieldHeight wide)
	int cellGap = 3;       // between swatch cells
	int groupGap = 8;      // between swatch groups
};

struct CpPanelOptions {
	bool swatch = true;
	bool eyedropper = true;            // beside the swatch (not without it)
	bool fields = true;
	bool alpha = true;
	bool groups = false;               // the swatch groups
	int  paletteCount = 0;             // 0: no palette rows (the custom and recent ones stay)
};

struct CpPanel {
	RECT swatch{};                     // empty without it
	RECT eyedropper{};
	RECT label[CpFieldCount]{};        // empty for a field not shown
	RECT frame[CpFieldCount]{};
	RECT group[CpGroupCount]{};        // the cells' bounds; all empty with no palette
	int  cell = 0;                     // a cell's side
};

int      CpPanelWidth(const CpPanelMetrics& m);
CpPanel  CpComputePanel(int left, int top, const CpPanelMetrics& m, const CpPanelOptions& o);
int      CpGroupRows(CpGroup group, int paletteCount);
RECT     CpSwatchCell(const CpPanel& p, CpGroup group, int index, int cellGap);
// the cell under pt (-1 if none); count: the cells there are
int      CpSwatchAt(const CpPanel& p, CpGroup group, int count, int cellGap, POINT pt);
const COLORREF* CpDefaultPalette(int* pCount);

// Keyboard moves over the swatch groups as one grid of 12 columns (rows of the palette, then custom, then
// recent). counts: the cells of each group that take the focus. A cell that isn't one moves to the first.
// false if no group has cells.
enum CpNav { CpNavLeft, CpNavRight, CpNavUp, CpNavDown, CpNavHome, CpNavEnd, CpNavFirst, CpNavLast };
bool     CpSwatchMove(const int counts[CpGroupCount], int& group, int& index, CpNav nav);
// color to the front, once; at most max
void     CpAddRecent(std::vector<COLORREF>& recent, COLORREF color, size_t max = CpMaxRecent);

// the eyedropper's magnifier: below and right of the cursor, flipped to stay in the work area
POINT    CpPlaceMagnifier(POINT cursor, SIZE size, const RECT& work, int offset);
// a drop-down under the anchor, left-aligned; above it if it doesn't fit below and does above; in the work area
POINT    CpPlaceDropDown(const RECT& anchor, SIZE size, const RECT& work);
// centered over a window, in the work area
POINT    CpCenterOver(const RECT& owner, SIZE size, const RECT& work);
// from the panel's top to the bottom of its lowest part (0 if nothing is shown)
int      CpPanelHeight(const CpPanel& p, int top);
bool     CpHasSpin(CpField field);         // all but Hex
// the spin buttons inside a field's frame: the upper and lower halves
void     CpSpinRects(const RECT& frame, int spinWidth, RECT* pUp, RECT* pDown);
// the edit inside a field's frame, textHeight high and centered
RECT     CpEditRect(const RECT& frame, CpField field, int spinWidth, int textHeight, int padX);
// the swatch's halves: the new color above the original, or all of it with no original
void     CpSwatchRects(const RECT& swatch, bool hasOriginal, RECT* pNew, RECT* pOld);

// ---- Fields --------------------------------------------------------------------------
//
// H in degrees 0..360, S and V in percent, R G B and A 0..255, Hex "#RRGGBB" (also takes "#AARRGGBB", which
// sets the alpha). Numbers are whole; out of range ones are clamped.

std::wstring CpFieldText(CpField field, const CpHsv& hsv, BYTE alpha);
// false if the text isn't a value for the field (hsv and alpha untouched)
bool     CpApplyField(CpField field, std::wstring_view text, CpHsv& hsv, BYTE& alpha);
// up (delta > 0) or down from the field's shown value; false for Hex
bool     CpStepField(CpField field, int delta, CpHsv& hsv, BYTE& alpha);
bool     CpParseInt(std::wstring_view text, int* pValue);   // digits, an optional '-', spaces around

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
// the checkerboard under a color with alpha (also for swatches)
uint32_t CpChecker(int x, int y, int cell, COLORREF check1, COLORREF check2);
// all of px: the color opaque on the left half, with its alpha over the checkerboard on the right
void     CpRenderSwatch(const CpPixels& px, COLORREF color, BYTE alpha, int cell, COLORREF check1, COLORREF check2);

void     CpFillRect(const CpPixels& px, const RECT& rc, COLORREF color);   // clipped to px
void     CpFrameRect(const CpPixels& px, const RECT& rc, COLORREF color, int thickness = 1);   // inside rc
void     CpCopy(const CpPixels& dst, int x, int y, const CpPixels& src);   // clipped
// an antialiased ring centered on (cx, cy) (pixel centers are at +0.5), radius to the middle of its line
void     CpDrawRing(const CpPixels& px, double cx, double cy, double radius, double thickness, COLORREF color);
