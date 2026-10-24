#include "CellGraphs.h"
#include "CellGraphsModel.h"

#include <algorithm>
#include <cmath>

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

// a BackgroundScope's cell before the list painted it
struct Background {
    const uint32_t* Before = nullptr;
    int  Width = 0;
    int  Height = 0;
    int  ProbeY = -1;
    bool Used = false;
};
thread_local Background t_Background;

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
    auto& bg = t_Background;
    if (bg.Before && !bg.Used && bg.Width == w && bg.Height == h) {
        CellBackground(bg.Before, c.Bits, w, h, c.Width, bg.ProbeY);
        bg.Used = true;
    }
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

// black or white, whichever reads on the color
COLORREF Contrast(COLORREF color) {
    return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000 ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

thread_local int t_HighContrast = -1;

// The colors of one cell, by its background (taken before anything is drawn): CLR_DEFAULT picked for it, a color
// too close to it moved apart, and in high contrast the system's text color for everything.
struct CellColors {
    COLORREF Background;
    bool     High;
    COLORREF Text = 0;   // high contrast

    CellColors(const Pixels& px, int w, int h) : Background(AverageColor(px, w, h)), High(IsHighContrast()) {
        if (High) {
            COLORREF texts[] = { ::GetSysColor(COLOR_WINDOWTEXT), ::GetSysColor(COLOR_HIGHLIGHTTEXT), ::GetSysColor(COLOR_BTNTEXT) };
            Text = Farthest(Background, texts, std::size(texts));
        }
    }

    COLORREF Pick(COLORREF color, bool fill = false) const {
        if (High)
            return Text;
        if (color == CLR_DEFAULT) {
            auto defaults = DefaultsFor(Background);
            return fill ? defaults.Fill : defaults.Line;
        }
        return Visible(color, Background);
    }

    // text over the background (not over a bar)
    COLORREF TextOver() const {
        return High ? Text : Contrast(Background);
    }
};

// a dashed line at a value, if it's in the range
void Reference(const Pixels& px, float value, ValueRange range, const RectF& area, UINT dpi) {
    if (IsMissing(value) || value < range.Min || value > range.Max)
        return;
    float y = ValueToY(value, range, area);
    DashedLine(px, y, area.Left, area.Right, std::max(1.0f, Dips(1, dpi)), Dips(3, dpi), Dips(2, dpi), 0.45f);
}

void Track(const Pixels& px, const RectF& rc, COLORREF color, const CellColors& colors) {
    if (color == CLR_DEFAULT || (colors.High && color != CLR_NONE))
        TintRect(px, rc, colors.High ? 0.3f : 0.12f);
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

BackgroundScope::BackgroundScope(const std::vector<uint32_t>& before, int w, int h, int probeY) {
    bool fits = w > 0 && h > 0 && before.size() == (size_t)w * h;
    t_Background = { fits ? before.data() : nullptr, w, h, probeY, false };
}

BackgroundScope::~BackgroundScope() {
    t_Background = {};
}

bool BackgroundScope::Used() const {
    return t_Background.Used;
}

bool IsHighContrast() {
    if (t_HighContrast >= 0)
        return t_HighContrast != 0;
    HIGHCONTRASTW hc{ sizeof(hc) };
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

void ForceHighContrast(int mode) {
    t_HighContrast = mode;
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
        CellColors colors(px, w, h);
        COLORREF line = colors.Pick(style.LineColor), fill = colors.Pick(style.FillColor, true);
        Reference(px, style.Reference, range, area, dpi);
        if (runs.empty())
            return;
        if (style.ThresholdCount) {
            auto stats = Stats(series, slots);
            COLORREF color = ThresholdColor(stats.Count ? stats.Last : MissingSample, style.Thresholds, style.ThresholdCount, CLR_DEFAULT);
            if (color != CLR_DEFAULT)
                line = fill = colors.Pick(color);
        }
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
        CellColors colors(px, w, h);
        Reference(px, style.Reference, range, area, dpi);
        for (auto& bar : bars)
            FillRect(px, bar.Rect, colors.Pick(ThresholdColor(series.AtFromEnd(bar.Slot), style.Thresholds, style.ThresholdCount, style.Color)));
    });
}

void DrawMeter(HDC hdc, const RECT& cell, float value, const MeterStyle& style, UINT dpi) {
    DrawMeter(hdc, cell, value, nullptr, style, dpi);
}

void DrawMeter(HDC hdc, const RECT& cell, float value, const wchar_t* text, const MeterStyle& style, UINT dpi) {
    Paint(hdc, cell, [&](const Pixels& px, int w, int h) {
        auto bar = BarRect(w, h, style.Padding, style.BarHeight, dpi);
        if (bar.Width() <= 0 || bar.Height() <= 0)
            return;
        // the colors by the background, before the track tints it
        CellColors colors(px, w, h);
        COLORREF color = colors.Pick(ThresholdColor(value, style.Thresholds, style.ThresholdCount, style.BarColor));
        Track(px, bar, style.TrackColor, colors);
        float fraction = MeterFraction(value, style.Min, style.Max);
        RectF filled{ bar.Left, bar.Top, bar.Left + fraction * bar.Width(), bar.Bottom };
        if (fraction > 0)
            FillRect(px, filled, color);
        if (!text || !*text)
            return;

        // GDI text into the canvas, in two parts: over the filled bar, and over the rest
        HDC dc = t_Canvas.Dc;
        int saved = ::SaveDC(dc);
        ::SelectObject(dc, ::GetCurrentObject(hdc, OBJ_FONT));
        ::SetBkMode(dc, TRANSPARENT);
        int inset = (int)std::lround(Dips(4, dpi));
        RECT rc{ (int)bar.Left + inset, 0, (int)bar.Right - inset, h };
        // ExtTextOut where the text fits (DrawText, twice per cell, was most of a meter's time); DrawText to end
        // it with an ellipsis where it doesn't
        int length = (int)wcslen(text);
        SIZE size{};
        ::GetTextExtentPoint32W(dc, text, length, &size);
        bool fits = size.cx <= rc.right - rc.left;
        UINT align = style.TextAlign & (DT_CENTER | DT_RIGHT);
        int x = align == DT_RIGHT ? rc.right - size.cx : align == DT_CENTER ? (rc.left + rc.right - size.cx) / 2 : rc.left;
        int y = (h - size.cy) / 2;
        auto draw = [&] {
            if (fits)
                ::ExtTextOutW(dc, x, y, 0, nullptr, text, length, nullptr);
            else
                ::DrawTextW(dc, text, length, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | align);
        };
        RECT fill{ (int)std::lround(filled.Left), (int)std::lround(filled.Top), (int)std::lround(filled.Right), (int)std::lround(filled.Bottom) };
        // over the bar, if the text reaches it
        if (fill.right > fill.left && (!fits || x < fill.right)) {
            ::SaveDC(dc);
            ::IntersectClipRect(dc, fill.left, fill.top, fill.right, fill.bottom);
            ::SetTextColor(dc, style.TextColor != CLR_DEFAULT && !colors.High ? style.TextColor : Contrast(color));
            draw();
            ::RestoreDC(dc, -1);
        }
        // over the track and the background, unless the bar has all of it
        if (!fits || x + size.cx > fill.right || y < fill.top || y + size.cy > fill.bottom) {
            ::IntersectClipRect(dc, 0, 0, w, h);
            ::ExcludeClipRect(dc, fill.left, fill.top, fill.right, fill.bottom);
            ::SetTextColor(dc, style.TextColor != CLR_DEFAULT && !colors.High ? style.TextColor : colors.TextOver());
            draw();
        }
        ::RestoreDC(dc, saved);
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
        CellColors cellColors(px, w, h);
        Track(px, bar, style.TrackColor, cellColors);
        auto segments = StackSegments(values, values ? count : 0, style.Total, bar);
        for (size_t i = 0; i < segments.size(); i++) {
            // high contrast has one color: every other segment in half of it
            if (segments[i].Width() > 0)
                FillRect(px, segments[i], cellColors.Pick(colors[i % colorCount]), cellColors.High && i % 2 ? 0.5f : 1.0f);
        }
    });
}

} // namespace GraphCtrl
