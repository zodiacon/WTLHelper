#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <vector>

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

struct Threshold {
    float    From;    // the color from this value up
    COLORREF Color;
};

float    MeterFraction(float value, float min, float max);   // 0..1; 0 for a missing value
// the color of the last threshold at or below the value (in ascending order), else fallback
COLORREF ThresholdColor(float value, const Threshold* thresholds, size_t count, COLORREF fallback);

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
// a dot (a run of one point)
void FillDot(const Pixels& px, PointF center, float radius, COLORREF color, float alpha = 1);

} // namespace GraphCtrl::Cells
