#include "CellGraphs.h"
#include "CellGraphsModel.h"

#include <algorithm>

namespace GraphCtrl {

using namespace Cells;

namespace {

// A DIB section a cell is drawn in: the cell's pixels are copied in, drawn over and copied back. One per thread,
// grown to the largest cell drawn, so painting a list doesn't make a bitmap for every cell.
struct Canvas {
    HDC       Dc     = nullptr;
    HBITMAP   Bitmap = nullptr;
    HGDIOBJ   Old    = nullptr;
    uint32_t* Bits   = nullptr;
    int       Width  = 0;
    int       Height = 0;

    ~Canvas() {
        if (Dc) {
            ::SelectObject(Dc, Old);
            ::DeleteDC(Dc);
        }
        if (Bitmap)
            ::DeleteObject(Bitmap);
    }

    bool Ensure(int width, int height) {
        if (Dc && width <= Width && height <= Height)
            return true;
        int w = std::max(width, Width), h = std::max(height, Height);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;   // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bitmap = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap)
            return false;
        if (!Dc) {
            Dc = ::CreateCompatibleDC(nullptr);
            if (!Dc) {
                ::DeleteObject(bitmap);
                return false;
            }
            Old = ::SelectObject(Dc, bitmap);
        }
        else {
            ::SelectObject(Dc, bitmap);
            ::DeleteObject(Bitmap);
        }
        Bitmap = bitmap;
        Bits = (uint32_t*)bits;
        Width = w;
        Height = h;
        return true;
    }
};

thread_local Canvas t_Canvas;

// draw(pixels, width, height) over the cell's pixels; the buffer may be larger than the cell (the extra isn't
// copied back)
template<typename Draw>
void Paint(HDC hdc, const RECT& cell, Draw&& draw) {
    int w = cell.right - cell.left, h = cell.bottom - cell.top;
    if (w <= 0 || h <= 0 || !t_Canvas.Ensure(w, h))
        return;
    auto& c = t_Canvas;
    if (!::BitBlt(c.Dc, 0, 0, w, h, hdc, cell.left, cell.top, SRCCOPY))
        return;
    ::GdiFlush();   // the copy is in the bits before they're written
    Pixels px{ c.Bits, c.Width, c.Height };
    draw(px, w, h);
    ::BitBlt(hdc, cell.left, cell.top, w, h, c.Dc, 0, 0, SRCCOPY);
}

float Dips(float value, UINT dpi) {
    return value * (float)dpi / 96.0f;
}

// the bar of a meter or a stacked bar: inside the padding, BarHeight high and centered (or all of it)
RectF BarRect(int w, int h, int padding, int barHeight, UINT dpi) {
    float pad = Dips((float)padding, dpi);
    RectF rc{ pad, pad, (float)w - pad, (float)h - pad };
    if (barHeight > 0) {
        float bh = std::min(rc.Height(), Dips((float)barHeight, dpi));
        float top = std::round((h - bh) / 2);
        rc.Top = top;
        rc.Bottom = top + bh;
    }
    return rc;
}

// a style's color, or for CLR_DEFAULT the default for the cell's background
COLORREF Resolve(COLORREF color, const Pixels& px, int w, int h, bool fill = false) {
    if (color != CLR_DEFAULT)
        return color;
    auto defaults = DefaultsFor(AverageColor(px, w, h));
    return fill ? defaults.Fill : defaults.Line;
}

void Track(const Pixels& px, const RectF& rc, COLORREF color) {
    if (color == CLR_DEFAULT)
        TintRect(px, rc, 0.12f);
    else if (color != CLR_NONE)
        FillRect(px, rc, color);
}

} // namespace

namespace Cells {

bool ReadCell(HDC hdc, const RECT& cell, std::vector<uint32_t>& pixels) {
    int w = cell.right - cell.left, h = cell.bottom - cell.top;
    if (w <= 0 || h <= 0 || !t_Canvas.Ensure(w, h))
        return false;
    auto& c = t_Canvas;
    if (!::BitBlt(c.Dc, 0, 0, w, h, hdc, cell.left, cell.top, SRCCOPY))
        return false;
    ::GdiFlush();
    pixels.resize((size_t)w * h);
    for (int y = 0; y < h; y++)
        std::copy_n(c.Bits + (size_t)y * c.Width, w, pixels.data() + (size_t)y * w);
    return true;
}

void WriteCell(HDC hdc, const RECT& cell, const std::vector<uint32_t>& pixels) {
    int w = cell.right - cell.left, h = cell.bottom - cell.top;
    if (w <= 0 || h <= 0 || pixels.size() != (size_t)w * h || !t_Canvas.Ensure(w, h))
        return;
    auto& c = t_Canvas;
    ::GdiFlush();
    for (int y = 0; y < h; y++)
        std::copy_n(pixels.data() + (size_t)y * w, w, c.Bits + (size_t)y * c.Width);
    ::BitBlt(hdc, cell.left, cell.top, w, h, c.Dc, 0, 0, SRCCOPY);
}

} // namespace Cells

void DrawSparkline(HDC hdc, const RECT& cell, const Series& series, const SparklineStyle& style, UINT dpi) {
    Paint(hdc, cell, [&](const Pixels& px, int w, int h) {
        float width = std::max(1.0f, Dips(style.LineWidth, dpi));
        float inset = Dips((float)style.Padding, dpi) + width / 2;
        RectF area{ inset, inset, (float)w - inset, (float)h - inset };
        if (area.Width() <= 0 || area.Height() <= 0)
            return;
        size_t slots = style.Slots ? style.Slots : series.Capacity();
        auto range = ScaleRange(series, slots, style.Min, style.Max, style.AutoScale, style.MinSpan);
        auto runs = SparkRuns(series, slots, range, area);
        if (runs.empty())
            return;
        COLORREF line = Resolve(style.LineColor, px, w, h), fill = Resolve(style.FillColor, px, w, h, true);
        if (style.FillOpacity > 0) {
            for (auto& run : runs)
                FillArea(px, run, area.Bottom, fill, style.FillOpacity);
        }
        for (auto& run : runs)
            DrawPolyline(px, run, width, line);
    });
}

void DrawColumns(HDC hdc, const RECT& cell, const Series& series, const ColumnStyle& style, UINT dpi) {
    Paint(hdc, cell, [&](const Pixels& px, int w, int h) {
        float pad = Dips((float)style.Padding, dpi);
        RectF area{ pad, pad, (float)w - pad, (float)h - pad };
        if (area.Width() <= 0 || area.Height() <= 0)
            return;
        size_t slots = style.Slots ? style.Slots : series.Capacity();
        auto range = ScaleRange(series, slots, style.Min, style.Max, style.AutoScale, style.MinSpan);
        auto bars = ColumnBars(series, slots, range, area, Dips(style.Gap, dpi));
        if (bars.empty())
            return;
        COLORREF standard = Resolve(CLR_DEFAULT, px, w, h);
        for (auto& bar : bars) {
            COLORREF color = ThresholdColor(series.AtFromEnd(bar.Slot), style.Thresholds, style.ThresholdCount, style.Color);
            if (color == CLR_DEFAULT)
                color = standard;
            FillRect(px, bar.Rect, color);
        }
    });
}

void DrawMeter(HDC hdc, const RECT& cell, float value, const MeterStyle& style, UINT dpi) {
    Paint(hdc, cell, [&](const Pixels& px, int w, int h) {
        auto bar = BarRect(w, h, style.Padding, style.BarHeight, dpi);
        if (bar.Width() <= 0 || bar.Height() <= 0)
            return;
        // the color by the background before the track tints it
        COLORREF color = Resolve(ThresholdColor(value, style.Thresholds, style.ThresholdCount, style.BarColor), px, w, h);
        Track(px, bar, style.TrackColor);
        float fraction = MeterFraction(value, style.Min, style.Max);
        if (fraction > 0) {
            FillRect(px, { bar.Left, bar.Top, bar.Left + fraction * bar.Width(), bar.Bottom }, color);
        }
    });
}

void DrawStacked(HDC hdc, const RECT& cell, const float* values, size_t count, const StackedStyle& style, UINT dpi) {
    static const COLORREF defaults[] = {
        RGB(17, 125, 187), RGB(240, 160, 0), RGB(80, 170, 80), RGB(200, 60, 60), RGB(140, 90, 190), RGB(0, 160, 170),
    };
    const COLORREF* colors = style.Colors && style.ColorCount ? style.Colors : defaults;
    size_t colorCount = style.Colors && style.ColorCount ? style.ColorCount : std::size(defaults);
    Paint(hdc, cell, [&](const Pixels& px, int w, int h) {
        auto bar = BarRect(w, h, style.Padding, style.BarHeight, dpi);
        if (bar.Width() <= 0 || bar.Height() <= 0)
            return;
        Track(px, bar, style.TrackColor);
        auto segments = StackSegments(values, values ? count : 0, style.Total, bar);
        for (size_t i = 0; i < segments.size(); i++) {
            if (segments[i].Width() > 0)
                FillRect(px, segments[i], colors[i % colorCount]);
        }
    });
}

} // namespace GraphCtrl
