#include "CellGraphsModel.h"

#include <algorithm>
#include <cmath>

namespace GraphCtrl::Cells {

// ---- Scale ----------------------------------------------------------------------

ValueRange ScaleRange(const Series& series, size_t slots, float min, float max, bool autoScale, float minSpan) {
    if (!autoScale)
        return { min, max };
    float lo = min, hi = -std::numeric_limits<float>::infinity();
    size_t shown = std::min(slots, series.Count());
    for (size_t k = 0; k < shown; k++) {
        float v = series.AtFromEnd(k);
        if (IsMissing(v))
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    hi = std::max(hi, lo + std::max(minSpan, 0.0f));
    if (!(hi > lo))
        hi = lo + 1;
    return { lo, hi };
}

float ValueToY(float value, ValueRange range, const RectF& area) {
    float span = range.Max - range.Min;
    float t = span > 0 ? std::clamp((value - range.Min) / span, 0.0f, 1.0f) : 0.0f;
    return area.Bottom - t * area.Height();
}

// ---- Line sparkline ------------------------------------------------------------------

std::vector<std::vector<PointF>> SparkRuns(const Series& series, size_t slots, ValueRange range, const RectF& area) {
    std::vector<std::vector<PointF>> runs;
    if (slots == 0)
        return runs;
    float dx = slots > 1 ? area.Width() / (float)(slots - 1) : 0.0f;
    size_t shown = std::min(slots, series.Count());
    std::vector<PointF>* run = nullptr;
    // oldest shown first: left to right
    for (size_t k = shown; k-- > 0;) {
        float v = series.AtFromEnd(k);
        if (IsMissing(v)) {
            run = nullptr;
            continue;
        }
        if (!run) {
            runs.emplace_back();
            run = &runs.back();
        }
        run->push_back({ area.Right - (float)k * dx, ValueToY(v, range, area) });
    }
    return runs;
}

// ---- Column sparkline ----------------------------------------------------------------

std::vector<Bar> ColumnBars(const Series& series, size_t slots, ValueRange range, const RectF& area, float gap) {
    std::vector<Bar> bars;
    if (slots == 0)
        return bars;
    float w = area.Width() / (float)slots;
    float g = w >= 3 * gap ? gap : 0.0f;
    size_t shown = std::min(slots, series.Count());
    bars.reserve(shown);
    for (size_t k = 0; k < shown; k++) {
        float v = series.AtFromEnd(k);
        if (IsMissing(v))
            continue;
        float top = ValueToY(v, range, area);
        if (top >= area.Bottom)
            continue;
        float right = area.Right - (float)k * w;
        bars.push_back({ { right - w + g / 2, top, right - g / 2, area.Bottom }, k });
    }
    return bars;
}

// ---- Meter -------------------------------------------------------------------------

float MeterFraction(float value, float min, float max) {
    if (IsMissing(value) || !(max > min))
        return 0.0f;
    return std::clamp((value - min) / (max - min), 0.0f, 1.0f);
}

COLORREF ThresholdColor(float value, const Threshold* thresholds, size_t count, COLORREF fallback) {
    COLORREF color = fallback;
    if (IsMissing(value))
        return color;
    for (size_t i = 0; i < count; i++) {
        if (value >= thresholds[i].From)
            color = thresholds[i].Color;
    }
    return color;
}

// ---- Stacked bar ---------------------------------------------------------------------

std::vector<RectF> StackSegments(const float* values, size_t count, float total, const RectF& area) {
    std::vector<RectF> segments;
    segments.reserve(count);
    double sum = 0;
    for (size_t i = 0; i < count; i++) {
        if (!IsMissing(values[i]) && values[i] > 0)
            sum += values[i];
    }
    double whole = total > 0 ? total : sum;
    double acc = 0;
    for (size_t i = 0; i < count; i++) {
        float v = values[i];
        float left = whole > 0 ? area.Left + (float)(acc / whole) * area.Width() : area.Left;
        if (!IsMissing(v) && v > 0)
            acc += v;
        float right = whole > 0 ? area.Left + (float)(acc / whole) * area.Width() : area.Left;
        segments.push_back({ std::min(left, area.Right), area.Top, std::min(right, area.Right), area.Bottom });
    }
    return segments;
}

// ---- Pixels ----------------------------------------------------------------------------

uint32_t Blend(uint32_t pixel, COLORREF color, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    auto mix = [&](int shift, int c) {
        float x = (float)((pixel >> shift) & 0xFF);
        return (uint32_t)std::lround(x + ((float)c - x) * alpha) << shift;
    };
    return mix(16, GetRValue(color)) | mix(8, GetGValue(color)) | mix(0, GetBValue(color));
}

static float Overlap(float a0, float a1, float b0, float b1) {
    return std::max(0.0f, std::min(a1, b1) - std::max(a0, b0));
}

void FillRect(const Pixels& px, const RectF& rc, COLORREF color, float alpha) {
    int x0 = std::max(0, (int)std::floor(rc.Left)), x1 = std::min(px.Width, (int)std::ceil(rc.Right));
    int y0 = std::max(0, (int)std::floor(rc.Top)), y1 = std::min(px.Height, (int)std::ceil(rc.Bottom));
    for (int y = y0; y < y1; y++) {
        float cy = Overlap((float)y, (float)y + 1, rc.Top, rc.Bottom);
        for (int x = x0; x < x1; x++) {
            float c = cy * Overlap((float)x, (float)x + 1, rc.Left, rc.Right) * alpha;
            if (c > 0)
                px.At(x, y) = Blend(px.At(x, y), color, c);
        }
    }
}

void FillArea(const Pixels& px, const std::vector<PointF>& run, float baseY, COLORREF color, float alpha) {
    if (run.size() < 2)
        return;
    float first = run.front().X, last = run.back().X;
    int x0 = std::max(0, (int)std::floor(first)), x1 = std::min(px.Width, (int)std::ceil(last));
    size_t seg = 0;
    for (int x = x0; x < x1; x++) {
        float cover = Overlap((float)x, (float)x + 1, first, last);
        if (cover <= 0)
            continue;
        // the line's height at the column's center (within the run)
        float cx = std::clamp((float)x + 0.5f, first, last);
        while (seg + 2 < run.size() && run[seg + 1].X < cx)
            seg++;
        const auto& a = run[seg];
        const auto& b = run[seg + 1];
        float t = b.X > a.X ? (cx - a.X) / (b.X - a.X) : 0.0f;
        float top = a.Y + (b.Y - a.Y) * std::clamp(t, 0.0f, 1.0f);
        float lo = std::min(top, baseY), hi = std::max(top, baseY);
        int y0 = std::max(0, (int)std::floor(lo)), y1 = std::min(px.Height, (int)std::ceil(hi));
        for (int y = y0; y < y1; y++) {
            float c = cover * Overlap((float)y, (float)y + 1, lo, hi) * alpha;
            if (c > 0)
                px.At(x, y) = Blend(px.At(x, y), color, c);
        }
    }
}

static float SegmentDistance(float px, float py, const PointF& a, const PointF& b) {
    float dx = b.X - a.X, dy = b.Y - a.Y;
    float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? std::clamp(((px - a.X) * dx + (py - a.Y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(px - (a.X + t * dx), py - (a.Y + t * dy));
}

void DrawPolyline(const Pixels& px, const std::vector<PointF>& points, float width, COLORREF color, float alpha) {
    if (points.empty() || px.Width <= 0 || px.Height <= 0)
        return;
    if (points.size() == 1) {
        FillDot(px, points[0], width / 2, color, alpha);
        return;
    }
    // coverage per pixel, the most any segment gives it, then one blend: no darker joints
    std::vector<float> mask((size_t)px.Width * px.Height, 0.0f);
    float half = width / 2, reach = half + 1;
    int minX = px.Width, maxX = -1, minY = px.Height, maxY = -1;
    for (size_t i = 0; i + 1 < points.size(); i++) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        int x0 = std::max(0, (int)std::floor(std::min(a.X, b.X) - reach));
        int x1 = std::min(px.Width, (int)std::ceil(std::max(a.X, b.X) + reach));
        int y0 = std::max(0, (int)std::floor(std::min(a.Y, b.Y) - reach));
        int y1 = std::min(px.Height, (int)std::ceil(std::max(a.Y, b.Y) + reach));
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                float c = std::clamp(half + 0.5f - SegmentDistance(x + 0.5f, y + 0.5f, a, b), 0.0f, 1.0f);
                float& m = mask[(size_t)y * px.Width + x];
                m = std::max(m, c);
            }
        }
        minX = std::min(minX, x0); maxX = std::max(maxX, x1);
        minY = std::min(minY, y0); maxY = std::max(maxY, y1);
    }
    for (int y = minY; y < maxY; y++) {
        for (int x = minX; x < maxX; x++) {
            float c = mask[(size_t)y * px.Width + x];
            if (c > 0)
                px.At(x, y) = Blend(px.At(x, y), color, c * alpha);
        }
    }
}

void FillDot(const Pixels& px, PointF center, float radius, COLORREF color, float alpha) {
    float reach = radius + 1;
    int x0 = std::max(0, (int)std::floor(center.X - reach)), x1 = std::min(px.Width, (int)std::ceil(center.X + reach));
    int y0 = std::max(0, (int)std::floor(center.Y - reach)), y1 = std::min(px.Height, (int)std::ceil(center.Y + reach));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float c = std::clamp(radius + 0.5f - std::hypot(x + 0.5f - center.X, y + 0.5f - center.Y), 0.0f, 1.0f);
            if (c > 0)
                px.At(x, y) = Blend(px.At(x, y), color, c * alpha);
        }
    }
}

} // namespace GraphCtrl::Cells
