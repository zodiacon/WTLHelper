#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <vector>

#include "CellGraphs.h"
#include "GraphData.h"

// The window-free parts of the cell graphs: where a series' samples go in a cell, the bars of a column graph,
// a meter's fill and color, a stacked bar's segments, and a small rasterizer (anti-aliased polylines, area
// fills and rectangles) drawing into 32-bit pixels. Covered by GraphControlTests.

namespace GraphCtrl::Cells {

struct PointF {
    float X;
    float Y;
};

struct RectF {
    float Left, Top, Right, Bottom;
    float Width() const  { return Right - Left; }
    float Height() const { return Bottom - Top; }
};

// ---- Scale ----------------------------------------------------------------------

struct ValueRange {
    float Min;
    float Max;
};

// The range a cell shows: fixed, or with autoScale the data's, from min (kept below the lowest sample) up to
// the highest sample, or min + minSpan if that's higher (so a flat line near zero isn't stretched to the top).
// Only the newest `slots` samples count. Missing samples are ignored.
ValueRange ScaleRange(const Series& series, size_t slots, float min, float max, bool autoScale, float minSpan);

// y of a value in the area: Max at the top, Min at the bottom, clamped
float ValueToY(float value, ValueRange range, const RectF& area);

// the newest `slots` samples (missing ones skipped): the newest, lowest, highest and average; Count 0: none
struct CellStats {
    float  Last    = 0;
    float  Min     = 0;
    float  Max     = 0;
    float  Average = 0;
    size_t Count   = 0;
};
CellStats Stats(const Series& series, size_t slots);

// ---- Line sparkline ------------------------------------------------------------------
//
// slots samples across the area, the newest at the right edge: a series still filling up grows from the right.
// A missing sample breaks the line, so the result is runs of points; a run of one point is a lone sample.

std::vector<std::vector<PointF>> SparkRuns(const Series& series, size_t slots, ValueRange range, const RectF& area);

// ---- Column sparkline ----------------------------------------------------------------

struct Bar {
    RectF    Rect;
    size_t   Slot;   // 0 = the newest
};

// one bar per slot (none for a missing sample or one at the bottom); a gap of `gap` between bars when the slots
// are at least three times as wide
std::vector<Bar> ColumnBars(const Series& series, size_t slots, ValueRange range, const RectF& area, float gap);

// ---- Meter -------------------------------------------------------------------------

float    MeterFraction(float value, float min, float max);   // 0..1; 0 for a missing value
// the color of the last threshold at or below the value (in ascending order), else fallback
COLORREF ThresholdColor(float value, const CellThreshold* thresholds, size_t count, COLORREF fallback);

// ---- Stacked bar ---------------------------------------------------------------------
//
// Values side by side from the left, each as wide as its share of `total` (or of their sum if total <= 0);
// negative and missing values take no room. The last segment ends exactly where the sum does.

std::vector<RectF> StackSegments(const float* values, size_t count, float total, const RectF& area);

// ---- Pixels ----------------------------------------------------------------------------
//
// Top-down 32-bit buffers, 0x00RRGGBB (as a DIB section stores BGRA). Pixel centers are at +0.5.

struct Pixels {
    uint32_t* Bits = nullptr;
    int       Width = 0;
    int       Height = 0;

    uint32_t& At(int x, int y) const { return Bits[(size_t)y * Width + x]; }
};

inline uint32_t ToPixel(COLORREF c)  { return (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c); }
inline COLORREF ToColor(uint32_t p)  { return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF); }
// color over the pixel, alpha 0..1
uint32_t Blend(uint32_t pixel, COLORREF color, float alpha);

// a rectangle with fractional edges: edge pixels get their coverage
void FillRect(const Pixels& px, const RectF& rc, COLORREF color, float alpha = 1);
// the area between a run and baseY, column by column; the top pixel gets its coverage
void FillArea(const Pixels& px, const std::vector<PointF>& run, float baseY, COLORREF color, float alpha = 1);
// an anti-aliased line through the points, width wide; joints blend once (coverage is the max over the segments)
void DrawPolyline(const Pixels& px, const std::vector<PointF>& points, float width, COLORREF color, float alpha = 1);
// the pixels under the rectangle shifted toward black on a light background, white on a dark one (each pixel by
// its own lightness, so it reads the same over a selection highlight): a track that suits any background
void TintRect(const Pixels& px, const RectF& rc, float strength);
// A list cell's background without the text the list drew in it, written over `after` (w x h, top-down). `before`
// is the cell before the list drew it: a themed list (Explorer, dark mode) has drawn the row's background by then
// (its selection and hot highlight too) and draws only the text. A list that fills each cell itself (a classic
// selection highlight, a background color set for the cell) changes the pixel at (1, h / 2), which text doesn't
// reach: then the cell becomes that fill, but for the border pixels it left alone (a grid line).
void CellBackground(const uint32_t* before, uint32_t* after, int w, int h);

// the average color of the w x h pixels at the top left (of a sample of them)
COLORREF AverageColor(const Pixels& px, int w, int h);

// The colors CLR_DEFAULT stands for, by the background: a darker blue on a light one, a lighter blue on a dark one,
// and white or black on one close to those (a blue selection highlight).
struct DefaultColors {
    COLORREF Line;
    COLORREF Fill;
};
DefaultColors DefaultsFor(COLORREF background);

// a horizontal dashed line from x0 to x1 centered on y, `dash` on and `gap` off, tinting what's under it (like
// TintRect), so it reads on any background
void DashedLine(const Pixels& px, float y, float x0, float x1, float width, float dash, float gap, float strength);

// a dot (a run of one point)
void FillDot(const Pixels& px, PointF center, float radius, COLORREF color, float alpha = 1);

// ---- Cell pixels on a DC (CellGraphs.cpp) ------------------------------------------------

// a cell's pixels, w x h top-down
bool ReadCell(HDC hdc, const RECT& cell, std::vector<uint32_t>& pixels);
void WriteCell(HDC hdc, const RECT& cell, const std::vector<uint32_t>& pixels);

} // namespace GraphCtrl::Cells
