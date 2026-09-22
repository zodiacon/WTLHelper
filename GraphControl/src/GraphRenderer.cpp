#include "../include/GraphRenderer.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace GraphCtrl {

static D2D1_COLOR_F ColorrefToD2D(COLORREF cr, float alpha = 1.0f) {
    return D2D1::ColorF(
        GetRValue(cr) / 255.0f,
        GetGValue(cr) / 255.0f,
        GetBValue(cr) / 255.0f,
        alpha);
}

// Defaults for the band heights, used until the formats have been measured.
constexpr float k_DefaultTitleHeight = 18.0f;
constexpr float k_DefaultLabelHeight = 16.0f;
constexpr float k_DefaultValueHeight = 32.0f;
constexpr float k_Padding            = 8.0f;

// Default type sizes in DIPs.
constexpr float k_DefaultLabelSize = 11.0f;
constexpr float k_DefaultTitleSize = 12.5f;
constexpr float k_DefaultValueSize = 22.0f;

// Overlay metrics (legend rows, readout box, value overlay).
constexpr float k_SwatchSize   = 10.0f;
constexpr float k_OverlayPad   = 7.0f;
constexpr float k_LegendInset  = 10.0f;
constexpr float k_ReadoutMaxW  = 280.0f;

// Bars occupy this share of one sample slot, leaving a gap between columns.
constexpr float k_BarSlotFill = 0.8f;

// Gradient brushes are cached by color. A graph uses a handful; the bound is
// there so a host that recolors series at runtime cannot grow the cache forever.
constexpr size_t k_MaxFillBrushes = 16;

GraphTheme GraphTheme::Dark() {
    return GraphTheme{};
}

GraphTheme GraphTheme::Light() {
    GraphTheme t;
    t.Background     = RGB(255, 255, 255);
    t.PlotColor      = RGB(249, 249, 249);
    t.GridColor      = RGB(220, 220, 220);
    t.BorderColor    = RGB(160, 160, 160);
    t.TextColor      = RGB(70, 70, 70);
    t.CrosshairColor = RGB(120, 120, 120);
    t.PanelColor     = RGB(252, 252, 252);
    t.ValueColor     = RGB(40, 40, 40);
    return t;
}

bool GraphRenderer::GeometryKey::Matches(const GeometryKey& o) const {
    return Version == o.Version && VisibleMask == o.VisibleMask &&
           SeriesCount == o.SeriesCount && Capacity == o.Capacity &&
           Left == o.Left && Top == o.Top && Right == o.Right && Bottom == o.Bottom &&
           RangeMin == o.RangeMin && RangeMax == o.RangeMax &&
           Stacked == o.Stacked && Fill == o.Fill && Bars == o.Bars;
}

// ---- Shared device-independent resources -------------------------------------
//
// The D2D factory, the DWrite factory, the text formats and the stroke style are
// identical for every graph and carry no per-window state, so one set serves all
// of them. A grid of 32 tiles used to build 32 factories and 128 text formats.
//
// One set per thread rather than per process: a SINGLE_THREADED D2D factory must
// not be used from another thread, and DrawLabel sets the alignment on a text
// format immediately before drawing with it. Controls on separate UI threads
// therefore get separate sets. Ownership is reference counted against Init and
// Shutdown, so the set goes away with the last graph instead of at exit, when
// COM may already be torn down.

namespace {

struct SharedGraphics {
    ComPtr<ID2D1Factory>      D2dFactory;
    ComPtr<IDWriteFactory>    DwFactory;
    ComPtr<IDWriteTextFormat> LabelFormat;
    ComPtr<IDWriteTextFormat> TitleFormat;
    ComPtr<IDWriteTextFormat> OverlayFormat;
    ComPtr<IDWriteTextFormat> ValueFormat;
    ComPtr<ID2D1StrokeStyle>  DashStyle;
};

thread_local SharedGraphics* t_shared     = nullptr;
thread_local int             t_sharedRefs = 0;

HRESULT CreateFormat(IDWriteFactory* dw, const wchar_t* family, float size,
                     DWRITE_FONT_WEIGHT weight, DWRITE_PARAGRAPH_ALIGNMENT vertical,
                     IDWriteTextFormat** out) {
    HRESULT hr = dw->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", out);
    if (FAILED(hr)) return hr;
    (*out)->SetParagraphAlignment(vertical);
    (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return S_OK;
}

HRESULT BuildSharedGraphics(SharedGraphics& g) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.D2dFactory.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g.DwFactory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = CreateFormat(g.DwFactory.Get(), L"Segoe UI", k_DefaultLabelSize,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      g.LabelFormat.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = CreateFormat(g.DwFactory.Get(), L"Segoe UI", k_DefaultTitleSize,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      g.TitleFormat.GetAddressOf());
    if (FAILED(hr)) return hr;

    // The readout stacks one line per series, so it is top-aligned, not centered.
    hr = CreateFormat(g.DwFactory.Get(), L"Segoe UI", k_DefaultLabelSize,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                      g.OverlayFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    g.OverlayFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    hr = CreateFormat(g.DwFactory.Get(), L"Segoe UI", k_DefaultValueSize,
                      DWRITE_FONT_WEIGHT_LIGHT, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      g.ValueFormat.GetAddressOf());
    if (FAILED(hr)) return hr;

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    return g.D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, g.DashStyle.GetAddressOf());
}

SharedGraphics* AcquireShared(HRESULT& hr) {
    hr = S_OK;
    if (!t_shared) {
        auto holder = std::make_unique<SharedGraphics>();
        hr = BuildSharedGraphics(*holder);
        if (FAILED(hr)) return nullptr;
        t_shared     = holder.release();
        t_sharedRefs = 0;
    }
    t_sharedRefs++;
    return t_shared;
}

void ReleaseShared() {
    if (!t_shared) return;
    if (--t_sharedRefs <= 0) {
        delete t_shared;
        t_shared     = nullptr;
        t_sharedRefs = 0;
    }
}

} // namespace

GraphRenderer::~GraphRenderer() {
    Shutdown();
}

HRESULT GraphRenderer::Init() {
    if (m_SharedHeld) return S_OK;

    HRESULT hr = S_OK;
    SharedGraphics* g = AcquireShared(hr);
    if (!g) return FAILED(hr) ? hr : E_FAIL;

    m_SharedHeld    = true;
    m_D2dFactory    = g->D2dFactory;
    m_DwFactory     = g->DwFactory;
    m_LabelFormat   = g->LabelFormat;
    m_TitleFormat   = g->TitleFormat;
    m_OverlayFormat = g->OverlayFormat;
    m_ValueFormat   = g->ValueFormat;
    m_DashStyle     = g->DashStyle;

    RecomputeMetrics();
    return S_OK;
}

// Safe to call more than once: the control shuts the renderer down on WM_DESTROY
// and the destructor runs it again.
// One line of this format, with a little air around it.
float GraphRenderer::MeasuredHeight(IDWriteTextFormat* format, float fallback) const {
    if (!m_DwFactory || !format) return fallback;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_DwFactory->CreateTextLayout(L"Xg", 2, format, 1000.0f, 1000.0f,
                                             layout.GetAddressOf())))
        return fallback;

    DWRITE_TEXT_METRICS tm{};
    if (FAILED(layout->GetMetrics(&tm)) || tm.height <= 0.0f) return fallback;
    return std::ceil(tm.height) + 2.0f;
}

void GraphRenderer::RecomputeMetrics() {
    m_TitleHeight = MeasuredHeight(m_TitleFormat.Get(), k_DefaultTitleHeight);
    m_LabelHeight = MeasuredHeight(m_LabelFormat.Get(), k_DefaultLabelHeight);
    m_ValueHeight = MeasuredHeight(m_ValueFormat.Get(), k_DefaultValueHeight);
}

HRESULT GraphRenderer::SetFont(const wchar_t* family, float labelSize,
                               float titleSize, float valueSize) {
    if (!m_DwFactory || !family || !*family) return E_INVALIDARG;
    if (labelSize <= 0.0f || titleSize <= 0.0f || valueSize <= 0.0f) return E_INVALIDARG;

    // Private formats: the shared ones back every other graph on this thread.
    ComPtr<IDWriteTextFormat> label, title, overlay, value;
    HRESULT hr = CreateFormat(m_DwFactory.Get(), family, labelSize,
                              DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, label.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = CreateFormat(m_DwFactory.Get(), family, titleSize,
                      DWRITE_FONT_WEIGHT_NORMAL,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, title.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = CreateFormat(m_DwFactory.Get(), family, labelSize,
                      DWRITE_FONT_WEIGHT_NORMAL,
                      DWRITE_PARAGRAPH_ALIGNMENT_NEAR, overlay.GetAddressOf());
    if (FAILED(hr)) return hr;
    overlay->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    hr = CreateFormat(m_DwFactory.Get(), family, valueSize,
                      DWRITE_FONT_WEIGHT_LIGHT,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, value.GetAddressOf());
    if (FAILED(hr)) return hr;

    m_LabelFormat   = label;
    m_TitleFormat   = title;
    m_OverlayFormat = overlay;
    m_ValueFormat   = value;
    m_PrivateFonts  = true;

    RecomputeMetrics();
    return S_OK;
}

void GraphRenderer::ResetFont() {
    if (!m_PrivateFonts || !t_shared) return;

    m_LabelFormat   = t_shared->LabelFormat;
    m_TitleFormat   = t_shared->TitleFormat;
    m_OverlayFormat = t_shared->OverlayFormat;
    m_ValueFormat   = t_shared->ValueFormat;
    m_PrivateFonts  = false;

    RecomputeMetrics();
}

void GraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_Geometry.clear();
    m_GeometryValid = false;

    m_DashStyle.Reset();
    m_ValueFormat.Reset();
    m_OverlayFormat.Reset();
    m_TitleFormat.Reset();
    m_LabelFormat.Reset();
    m_DwFactory.Reset();
    m_D2dFactory.Reset();

    m_PrivateFonts = false;
    if (m_SharedHeld) {
        ReleaseShared();
        m_SharedHeld = false;
    }
}

HRESULT GraphRenderer::EnsureDeviceResources(HWND hwnd, const RECT& clientRect) {
    if (!m_D2dFactory) return E_FAIL;

    const auto pixelSize = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(clientRect.right - clientRect.left, 1)),
        static_cast<UINT32>(std::max<LONG>(clientRect.bottom - clientRect.top, 1)));

    if (m_HwndTarget) {
        const auto current = m_HwndTarget->GetPixelSize();
        if (current.width != pixelSize.width || current.height != pixelSize.height)
            m_HwndTarget->Resize(pixelSize);
        return S_OK;
    }

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        m_Dpi, m_Dpi);

    HRESULT hr = m_D2dFactory->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(hwnd, pixelSize),
        m_HwndTarget.GetAddressOf());
    if (FAILED(hr)) return hr;

    m_RenderTarget = m_HwndTarget;
    return m_RenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::Black), m_Brush.GetAddressOf());
}

void GraphRenderer::DiscardDeviceResources() {
    m_FillBrushes.clear();
    m_Brush.Reset();
    m_RenderTarget.Reset();
    m_HwndTarget.Reset();
}

void GraphRenderer::Resize(UINT width, UINT height) {
    if (m_HwndTarget && width && height)
        m_HwndTarget->Resize(D2D1::SizeU(width, height));
}

void GraphRenderer::SetDpi(float dpi) {
    if (dpi <= 0.0f) return;
    m_Dpi = dpi;
    if (m_RenderTarget)
        m_RenderTarget->SetDpi(dpi, dpi);
}

// ---- Shared geometry ---------------------------------------------------------

D2D1_RECT_F GraphRenderer::PlotRect(const RECT& clientRect, const RenderOptions& opt) const {
    const float scale = 96.0f / m_Dpi;   // pixels -> DIPs
    const float w = (clientRect.right - clientRect.left) * scale;
    const float h = (clientRect.bottom - clientRect.top) * scale;

    const bool hasHeader = (opt.Title && *opt.Title) || opt.AxisLabels;
    const bool hasFooter = opt.AxisLabels;

    return D2D1::RectF(
        k_Padding,
        k_Padding + (hasHeader ? m_TitleHeight : 0.0f),
        w - k_Padding,
        h - k_Padding - (hasFooter ? m_LabelHeight : 0.0f));
}

float GraphRenderer::SampleSpacing(const D2D1_RECT_F& plot, size_t capacity) {
    const float w = plot.right - plot.left;
    return capacity > 1 ? w / (capacity - 1) : w;
}

// ---- Brushes -----------------------------------------------------------------

ID2D1LinearGradientBrush* GraphRenderer::FillBrush(COLORREF color, float opacity,
                                                   const D2D1_RECT_F& plot) {
    ComPtr<ID2D1LinearGradientBrush> brush;
    for (const auto& entry : m_FillBrushes) {
        if (entry.first == color) {
            brush = entry.second;
            break;
        }
    }

    if (!brush) {
        const D2D1_GRADIENT_STOP stops[] = {
            { 0.0f, ColorrefToD2D(color, 1.00f) },
            { 1.0f, ColorrefToD2D(color, 0.10f) },
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        if (FAILED(m_RenderTarget->CreateGradientStopCollection(
                stops, ARRAYSIZE(stops), collection.GetAddressOf())))
            return nullptr;

        if (FAILED(m_RenderTarget->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(), D2D1::Point2F()),
                collection.Get(), brush.GetAddressOf())))
            return nullptr;

        if (m_FillBrushes.size() >= k_MaxFillBrushes)
            m_FillBrushes.erase(m_FillBrushes.begin());   // oldest out
        m_FillBrushes.emplace_back(color, brush);
    }

    brush->SetStartPoint(D2D1::Point2F(plot.left, plot.top));
    brush->SetEndPoint(D2D1::Point2F(plot.left, plot.bottom));
    brush->SetOpacity(opacity);
    return brush.Get();
}

// ---- Grid --------------------------------------------------------------------

void GraphRenderer::DrawGrid(const D2D1_RECT_F& plot, const RenderOptions& opt,
                             const GraphTheme& theme) {
    m_Brush->SetColor(ColorrefToD2D(theme.GridColor));

    const float w = plot.right - plot.left;
    const float h = plot.bottom - plot.top;

    if (opt.GridCols > 0) {
        const float cell   = w / opt.GridCols;
        const float offset = opt.ScrollGrid ? opt.GridPhase * cell : 0.0f;
        for (int i = 0; i <= opt.GridCols; i++) {
            const float x = std::floor(plot.left + i * cell - offset) + 0.5f;
            if (x <= plot.left || x >= plot.right) continue;
            m_RenderTarget->DrawLine(D2D1::Point2F(x, plot.top),
                                     D2D1::Point2F(x, plot.bottom), m_Brush.Get(), 1.0f);
        }
    }

    if (opt.GridRows > 0) {
        const float cell = h / opt.GridRows;
        for (int j = 1; j < opt.GridRows; j++) {
            const float y = std::floor(plot.top + j * cell) + 0.5f;
            m_RenderTarget->DrawLine(D2D1::Point2F(plot.left, y),
                                     D2D1::Point2F(plot.right, y), m_Brush.Get(), 1.0f);
        }
    }
}

// ---- Geometry building -------------------------------------------------------

// Emits one figure per run of consecutive present samples, so a missing sample
// breaks the line and the fill instead of being drawn as a dive to the floor.
template <typename ValueFn, typename XFn, typename YFn, typename BaseFn>
static void EmitRuns(ID2D1GeometrySink* sink, size_t n, ValueFn value, XFn xAt, YFn yOf,
                     bool filled, BaseFn baseY) {
    size_t i = 0;
    while (i < n) {
        while (i < n && IsMissing(value(i))) i++;
        const size_t start = i;
        while (i < n && !IsMissing(value(i))) i++;
        const size_t end = i;                    // exclusive
        if (end - start < 2) continue;           // a lone sample has nothing to join

        if (filled) {
            sink->BeginFigure(D2D1::Point2F(xAt(start), baseY(start)), D2D1_FIGURE_BEGIN_FILLED);
            for (size_t j = start; j < end; j++)
                sink->AddLine(D2D1::Point2F(xAt(j), yOf(value(j))));
            for (size_t j = end; j-- > start; )
                sink->AddLine(D2D1::Point2F(xAt(j), baseY(j)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        else {
            sink->BeginFigure(D2D1::Point2F(xAt(start), yOf(value(start))), D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t j = start + 1; j < end; j++)
                sink->AddLine(D2D1::Point2F(xAt(j), yOf(value(j))));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        }
    }
}

void GraphRenderer::BuildOverlay(const GraphData& data, const D2D1_RECT_F& plot,
                                 const AxisRange& range, bool fill) {
    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;
    auto yOf = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };

    const auto& all = data.AllSeries();
    for (size_t si = 0; si < all.size(); si++) {
        const Series& s = all[si];
        if (!s.Style().Visible) continue;

        const size_t n = s.Count();
        if (n < 2) continue;

        const float dx = SampleSpacing(plot, s.Capacity());
        // Newest sample sits at the right edge, so a partly filled history grows
        // in from the right the way the Task Manager graphs do.
        auto xAt   = [&](size_t i) { return plot.right - (n - 1 - i) * dx; };
        auto value = [&](size_t i) { return s.At(i); };
        auto base  = [&](size_t)   { return plot.bottom; };

        if (fill && s.Style().FillOpacity > 0.0f) {
            ComPtr<ID2D1PathGeometry> geometry;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) &&
                SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
                EmitRuns(sink.Get(), n, value, xAt, yOf, true, base);
                sink->Close();
                m_Geometry[si].Fill = geometry;
            }
        }

        ComPtr<ID2D1PathGeometry> line;
        ComPtr<ID2D1GeometrySink> lineSink;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(line.GetAddressOf())) &&
            SUCCEEDED(line->Open(lineSink.GetAddressOf()))) {
            EmitRuns(lineSink.Get(), n, value, xAt, yOf, false, base);
            lineSink->Close();
            m_Geometry[si].Line = line;
        }
    }
}

// Stacked: each series sits on the running total of the ones before it. Only the
// slots every visible series holds are drawn, so a shorter series cannot shift
// the ones above it. A missing sample contributes nothing to the baseline and
// breaks that series' own band.
void GraphRenderer::BuildStacked(const GraphData& data, const D2D1_RECT_F& plot,
                                 const AxisRange& range, bool fill) {
    const size_t n = data.CommonCount();
    if (n < 2) return;

    size_t capacity = 0;
    for (const auto& s : data.AllSeries()) {
        if (s.Style().Visible)
            capacity = std::max(capacity, s.Capacity());
    }
    if (capacity < 2) return;

    const float dx   = SampleSpacing(plot, capacity);
    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;

    auto yOf = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };
    auto xAt = [&](size_t i) { return plot.right - (n - 1 - i) * dx; };   // i: oldest -> newest

    m_Baseline.assign(n, 0.0f);

    const auto& all = data.AllSeries();
    for (size_t si = 0; si < all.size(); si++) {
        const Series& s = all[si];
        if (!s.Style().Visible) continue;

        auto raw   = [&](size_t i) { return s.AtFromEnd(n - 1 - i); };
        auto value = [&](size_t i) {
            const float v = raw(i);
            return IsMissing(v) ? MissingSample : m_Baseline[i] + v;
        };
        auto base = [&](size_t i) { return yOf(m_Baseline[i]); };

        if (fill && s.Style().FillOpacity > 0.0f) {
            ComPtr<ID2D1PathGeometry> geometry;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) &&
                SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
                EmitRuns(sink.Get(), n, value, xAt, yOf, true, base);
                sink->Close();
                m_Geometry[si].Fill = geometry;
            }
        }

        ComPtr<ID2D1PathGeometry> line;
        ComPtr<ID2D1GeometrySink> lineSink;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(line.GetAddressOf())) &&
            SUCCEEDED(line->Open(lineSink.GetAddressOf()))) {
            EmitRuns(lineSink.Get(), n, value, xAt, yOf, false, base);
            lineSink->Close();
            m_Geometry[si].Line = line;
        }

        for (size_t i = 0; i < n; i++) {
            const float v = raw(i);
            if (!IsMissing(v)) m_Baseline[i] += v;
        }
    }
}

// Bars: one column per sample. Overlaid series sit side by side within a slot;
// stacked ones share a column and pile up.
void GraphRenderer::BuildBars(const GraphData& data, const D2D1_RECT_F& plot,
                              const AxisRange& range, bool stacked) {
    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;
    auto yOf = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };

    const auto& all = data.AllSeries();

    size_t visible = 0;
    for (const auto& s : all)
        if (s.Style().Visible) visible++;
    if (visible == 0) return;

    const size_t common = stacked ? data.CommonCount() : 0;
    if (stacked) {
        if (common == 0) return;
        m_Baseline.assign(common, 0.0f);
    }

    size_t slot = 0;
    for (size_t si = 0; si < all.size(); si++) {
        const Series& s = all[si];
        if (!s.Style().Visible) continue;

        const size_t n = stacked ? common : s.Count();
        if (n == 0) { slot++; continue; }

        const float dx = SampleSpacing(plot, s.Capacity());
        const float groupW = dx * k_BarSlotFill;
        // Side by side when overlaid, one shared column when stacked.
        const float barW = std::max(1.0f, stacked ? groupW : groupW / visible);

        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) ||
            FAILED(geometry->Open(sink.GetAddressOf()))) {
            slot++;
            continue;
        }

        for (size_t i = 0; i < n; i++) {
            const float v = stacked ? s.AtFromEnd(n - 1 - i) : s.At(i);
            if (IsMissing(v)) continue;

            const float center = plot.right - (n - 1 - i) * dx;
            const float left   = stacked ? center - barW / 2
                                         : center - groupW / 2 + slot * barW;
            const float right  = left + barW;

            const float bottomValue = stacked ? m_Baseline[i] : range.Min;
            const float yTop    = yOf(stacked ? m_Baseline[i] + v : v);
            const float yBottom = yOf(bottomValue);
            if (std::fabs(yBottom - yTop) < 0.5f) continue;

            sink->BeginFigure(D2D1::Point2F(left, yBottom), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(left, yTop));
            sink->AddLine(D2D1::Point2F(right, yTop));
            sink->AddLine(D2D1::Point2F(right, yBottom));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }

        sink->Close();
        m_Geometry[si].Fill = geometry;

        if (stacked) {
            for (size_t i = 0; i < n; i++) {
                const float v = s.AtFromEnd(n - 1 - i);
                if (!IsMissing(v)) m_Baseline[i] += v;
            }
        }
        slot++;
    }
}

void GraphRenderer::EnsureGeometry(const GraphData& data, const D2D1_RECT_F& plot,
                                   const AxisRange& range, const RenderOptions& opt) {
    GeometryKey key;
    key.Version     = data.Version();
    key.SeriesCount = data.SeriesCount();
    key.Capacity    = data.Capacity();
    key.Left        = plot.left;
    key.Top         = plot.top;
    key.Right       = plot.right;
    key.Bottom      = plot.bottom;
    key.RangeMin    = range.Min;
    key.RangeMax    = range.Max;
    key.Stacked     = opt.Stacked;
    key.Fill        = opt.Fill;
    key.Bars        = opt.Bars;

    size_t bit = 0;
    for (const auto& s : data.AllSeries()) {
        if (s.Style().Visible) key.VisibleMask |= (1ull << (bit & 63));
        bit++;
    }

    if (m_GeometryValid && m_Geometry.size() == data.SeriesCount() && key.Matches(m_GeometryKey))
        return;

    m_Geometry.assign(data.SeriesCount(), SeriesGeometry{});

    if (opt.Bars)         BuildBars(data, plot, range, opt.Stacked);
    else if (opt.Stacked) BuildStacked(data, plot, range, opt.Fill);
    else                  BuildOverlay(data, plot, range, opt.Fill);

    m_GeometryKey   = key;
    m_GeometryValid = true;
}

void GraphRenderer::DrawGeometry(const GraphData& data, const D2D1_RECT_F& plot,
                                 const RenderOptions& opt) {
    const auto& all = data.AllSeries();
    for (size_t si = 0; si < all.size() && si < m_Geometry.size(); si++) {
        const Series& s = all[si];
        if (!s.Style().Visible) continue;

        const SeriesStyle& style = s.Style();

        if (m_Geometry[si].Fill) {
            // Bars need enough opacity to read as solid columns.
            const float opacity = opt.Bars ? std::max(style.FillOpacity, 0.7f)
                                           : style.FillOpacity;
            if (auto* brush = FillBrush(style.FillColor, opacity, plot))
                m_RenderTarget->FillGeometry(m_Geometry[si].Fill.Get(), brush);

            if (opt.Bars) {
                m_Brush->SetColor(ColorrefToD2D(style.LineColor));
                m_RenderTarget->DrawGeometry(m_Geometry[si].Fill.Get(), m_Brush.Get(), 1.0f);
            }
        }

        if (m_Geometry[si].Line) {
            m_Brush->SetColor(ColorrefToD2D(style.LineColor));
            m_RenderTarget->DrawGeometry(m_Geometry[si].Line.Get(), m_Brush.Get(),
                                         style.LineWidth,
                                         style.Dashed ? m_DashStyle.Get() : nullptr);
        }
    }
}

// ---- Overlays ----------------------------------------------------------------

void GraphRenderer::DrawReferenceLines(const D2D1_RECT_F& plot, const AxisRange& range,
                                       const RenderOptions& opt) {
    if (!opt.RefLines || opt.RefLineCount == 0) return;

    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;

    for (size_t i = 0; i < opt.RefLineCount; i++) {
        const ReferenceLine& rl = opt.RefLines[i];
        if (rl.Value < range.Min || rl.Value > range.Max) continue;   // off the axis

        const float t = (rl.Value - range.Min) / span;
        const float y = std::floor(plot.bottom - t * (plot.bottom - plot.top)) + 0.5f;

        m_Brush->SetColor(ColorrefToD2D(rl.Color));
        m_RenderTarget->DrawLine(D2D1::Point2F(plot.left, y), D2D1::Point2F(plot.right, y),
                                 m_Brush.Get(), rl.Width,
                                 rl.Dashed ? m_DashStyle.Get() : nullptr);

        if (!rl.Label.empty()) {
            const D2D1_RECT_F rc = D2D1::RectF(plot.left, y - m_LabelHeight - 1.0f,
                                               plot.right - 6.0f, y - 1.0f);
            DrawLabel(rl.Label.c_str(), rc, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
    }
}

void GraphRenderer::DrawValueOverlay(const D2D1_RECT_F& plot, const RenderOptions& opt,
                                     const GraphTheme& theme) {
    if (!opt.ValueText || !*opt.ValueText) return;
    if (plot.bottom - plot.top < m_ValueHeight + 8.0f) return;

    m_Brush->SetColor(ColorrefToD2D(theme.ValueColor));
    const D2D1_RECT_F rc = D2D1::RectF(plot.left + k_LegendInset, plot.top + 6.0f,
                                       plot.right - k_LegendInset, plot.top + 6.0f + m_ValueHeight);
    DrawLabel(opt.ValueText, rc, m_ValueFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void GraphRenderer::DrawLegend(const D2D1_RECT_F& plot, const GraphData& data,
                               const GraphTheme& theme) {
    m_LabelFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    int   rows   = 0;
    float widest = 0.0f;
    for (const auto& s : data.AllSeries()) {
        if (!s.Style().Visible || s.Name().empty()) continue;
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(m_DwFactory->CreateTextLayout(
                s.Name().c_str(), static_cast<UINT32>(s.Name().size()),
                m_LabelFormat.Get(), 200.0f, m_LabelHeight, layout.GetAddressOf()))) {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            widest = std::max(widest, tm.widthIncludingTrailingWhitespace);
        }
        rows++;
    }
    if (rows == 0) return;

    const float w = k_OverlayPad * 2 + k_SwatchSize + 6.0f + widest;
    const float h = k_OverlayPad * 2 + rows * m_LabelHeight;
    const float x = plot.left + k_LegendInset;
    const float y = plot.top + k_LegendInset;
    if (x + w > plot.right || y + h > plot.bottom) return;

    const auto panel = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 4.0f, 4.0f);
    m_Brush->SetColor(ColorrefToD2D(theme.PanelColor, 0.85f));
    m_RenderTarget->FillRoundedRectangle(panel, m_Brush.Get());
    m_Brush->SetColor(ColorrefToD2D(theme.BorderColor, 0.8f));
    m_RenderTarget->DrawRoundedRectangle(panel, m_Brush.Get(), 1.0f);

    float rowY = y + k_OverlayPad;
    for (const auto& s : data.AllSeries()) {
        if (!s.Style().Visible || s.Name().empty()) continue;

        const float swatchY = rowY + (m_LabelHeight - k_SwatchSize) / 2;
        const auto swatch = D2D1::RoundedRect(
            D2D1::RectF(x + k_OverlayPad, swatchY,
                        x + k_OverlayPad + k_SwatchSize, swatchY + k_SwatchSize), 2.0f, 2.0f);
        m_Brush->SetColor(ColorrefToD2D(s.Style().LineColor));
        m_RenderTarget->FillRoundedRectangle(swatch, m_Brush.Get());

        m_Brush->SetColor(ColorrefToD2D(theme.TextColor));
        DrawLabel(s.Name().c_str(),
                  D2D1::RectF(x + k_OverlayPad + k_SwatchSize + 6.0f, rowY, x + w, rowY + m_LabelHeight),
                  m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

        rowY += m_LabelHeight;
    }
}

void GraphRenderer::DrawReadout(const wchar_t* text, float x, float y,
                                const D2D1_RECT_F& plot, const GraphTheme& theme) {
    if (!text || !*text) return;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_DwFactory->CreateTextLayout(text, static_cast<UINT32>(wcslen(text)),
            m_OverlayFormat.Get(), k_ReadoutMaxW, 400.0f, layout.GetAddressOf())))
        return;

    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);

    const float w = tm.widthIncludingTrailingWhitespace + k_OverlayPad * 2;
    const float h = tm.height + k_OverlayPad * 2;

    // Prefer below-right of the cursor; flip or clamp when that would overflow.
    float bx = x + 14.0f;
    float by = y + 14.0f;
    if (bx + w > plot.right) bx = x - 14.0f - w;
    if (by + h > plot.bottom) by = plot.bottom - h - 2.0f;
    bx = std::max(bx, plot.left + 2.0f);
    by = std::max(by, plot.top + 2.0f);

    const auto box = D2D1::RoundedRect(D2D1::RectF(bx, by, bx + w, by + h), 4.0f, 4.0f);
    m_Brush->SetColor(ColorrefToD2D(theme.PanelColor, 0.95f));
    m_RenderTarget->FillRoundedRectangle(box, m_Brush.Get());
    m_Brush->SetColor(ColorrefToD2D(theme.BorderColor));
    m_RenderTarget->DrawRoundedRectangle(box, m_Brush.Get(), 1.0f);

    m_Brush->SetColor(ColorrefToD2D(theme.TextColor));
    m_RenderTarget->DrawTextLayout(D2D1::Point2F(bx + k_OverlayPad, by + k_OverlayPad),
                                   layout.Get(), m_Brush.Get());
}

void GraphRenderer::DrawCrosshair(const D2D1_RECT_F& plot, const GraphData& data,
                                  const AxisRange& range, const RenderOptions& opt,
                                  const GraphTheme& theme) {
    if (opt.HoverIndex < 0) return;

    size_t capacity = 0;
    for (const auto& s : data.AllSeries()) {
        if (s.Style().Visible)
            capacity = std::max(capacity, s.Capacity());
    }
    if (capacity < 2) return;

    const size_t k  = static_cast<size_t>(opt.HoverIndex);
    const float  dx = SampleSpacing(plot, capacity);
    const float  x  = plot.right - k * dx;
    if (x < plot.left || x > plot.right) return;

    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;
    auto yOf = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };

    m_Brush->SetColor(ColorrefToD2D(theme.CrosshairColor, 0.85f));
    m_RenderTarget->DrawLine(D2D1::Point2F(x, plot.top), D2D1::Point2F(x, plot.bottom),
                             m_Brush.Get(), 1.0f, m_DashStyle.Get());

    float baseline = 0.0f;
    for (const auto& s : data.AllSeries()) {
        if (!s.Style().Visible || k >= s.Count()) continue;

        const float value = s.AtFromEnd(k);
        if (IsMissing(value)) continue;          // no dot where there is no reading

        const float top = opt.Stacked ? baseline + value : value;
        if (opt.Stacked) baseline = top;

        const auto dot = D2D1::Ellipse(D2D1::Point2F(x, yOf(top)), 3.5f, 3.5f);
        m_Brush->SetColor(ColorrefToD2D(s.Style().LineColor));
        m_RenderTarget->FillEllipse(dot, m_Brush.Get());
        m_Brush->SetColor(ColorrefToD2D(theme.PanelColor));
        m_RenderTarget->DrawEllipse(dot, m_Brush.Get(), 1.0f);
    }

    DrawReadout(opt.HoverText, opt.HoverX, opt.HoverY, plot, theme);
}

// ---- Chrome ------------------------------------------------------------------

void GraphRenderer::DrawLabel(const wchar_t* text, const D2D1_RECT_F& rc,
                              IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT align) {
    if (!text || !*text) return;
    format->SetTextAlignment(align);
    m_RenderTarget->DrawTextW(text, static_cast<UINT32>(wcslen(text)), format, rc,
                              m_Brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void GraphRenderer::DrawChrome(const D2D1_RECT_F& plot, const D2D1_RECT_F& client,
                               const AxisRange& range, const GraphTheme& theme,
                               const RenderOptions& opt, const ValueFormatFn& formatter) {
    m_Brush->SetColor(ColorrefToD2D(theme.TextColor));

    // Header band: title on the left, axis maximum on the right.
    const D2D1_RECT_F header = D2D1::RectF(client.left + k_Padding, plot.top - m_TitleHeight,
                                           client.right - k_Padding, plot.top);
    DrawLabel(opt.Title, header, m_TitleFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

    if (!opt.AxisLabels) return;

    wchar_t buffer[64] = {};
    if (formatter) formatter(range.Max, buffer, _countof(buffer));
    else           Format::Number(range.Max, buffer, _countof(buffer));
    DrawLabel(buffer, header, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    // Footer band: time span on the left, axis minimum on the right.
    const D2D1_RECT_F footer = D2D1::RectF(client.left + k_Padding, plot.bottom,
                                           client.right - k_Padding, plot.bottom + m_LabelHeight);
    DrawLabel(opt.TimeSpanText, footer, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

    buffer[0] = 0;
    if (formatter) formatter(range.Min, buffer, _countof(buffer));
    else           Format::Number(range.Min, buffer, _countof(buffer));
    DrawLabel(buffer, footer, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ---- Frame -------------------------------------------------------------------

HRESULT GraphRenderer::DrawFrame(const RECT& clientRect, const GraphData& data,
                                 const AxisRange& range, const GraphTheme& theme,
                                 const RenderOptions& opt, const ValueFormatFn& formatter) {
    const D2D1_SIZE_F size = m_RenderTarget->GetSize();   // DIPs
    if (size.width <= 0.0f || size.height <= 0.0f) return S_OK;

    const D2D1_RECT_F client = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
    const D2D1_RECT_F plot   = PlotRect(clientRect, opt);

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(ColorrefToD2D(theme.Background));

    if (plot.right - plot.left >= 4.0f && plot.bottom - plot.top >= 4.0f) {
        m_Brush->SetColor(ColorrefToD2D(theme.PlotColor));
        m_RenderTarget->FillRectangle(plot, m_Brush.Get());

        if (opt.DrawGrid)
            DrawGrid(plot, opt, theme);

        EnsureGeometry(data, plot, range, opt);

        m_RenderTarget->PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_ALIASED);
        DrawGeometry(data, plot, opt);
        DrawReferenceLines(plot, range, opt);
        m_RenderTarget->PopAxisAlignedClip();

        m_Brush->SetColor(ColorrefToD2D(theme.BorderColor));
        const D2D1_RECT_F border = D2D1::RectF(
            std::floor(plot.left) + 0.5f, std::floor(plot.top) + 0.5f,
            std::floor(plot.right) - 0.5f, std::floor(plot.bottom) - 0.5f);
        m_RenderTarget->DrawRectangle(border, m_Brush.Get(), 1.0f);

        if (opt.ValueOverlay)
            DrawValueOverlay(plot, opt, theme);
        if (opt.Legend)
            DrawLegend(plot, data, theme);

        DrawCrosshair(plot, data, range, opt, theme);
        DrawChrome(plot, client, range, theme, opt, formatter);
    }

    return m_RenderTarget->EndDraw();
}

void GraphRenderer::Render(HWND hwnd, const RECT& clientRect, const GraphData& data,
                           const AxisRange& range, const GraphTheme& theme,
                           const RenderOptions& opt, const ValueFormatFn& formatter) {
    if (FAILED(EnsureDeviceResources(hwnd, clientRect))) return;

    if (DrawFrame(clientRect, data, range, theme, opt, formatter) == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

// ---- Image export ------------------------------------------------------------

namespace {

// WIC needs COM. Hosts may or may not have initialised it, and a host that chose
// MTA must not be overridden, so a changed-mode failure is simply left alone.
struct ComScope {
    bool Owned = false;
    ComScope() {
        Owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    }
    ~ComScope() {
        if (Owned) CoUninitialize();
    }
    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;
};

bool WritePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const wchar_t* path) {
    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())))
        return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf())))
        return false;
    if (FAILED(frame->Initialize(options.Get()))) return false;

    UINT w = 0, h = 0;
    bitmap->GetSize(&w, &h);
    if (FAILED(frame->SetSize(w, h))) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormatDontCare;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (FAILED(frame->WriteSource(bitmap, nullptr))) return false;

    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

// CF_DIB, bottom-up 32bpp: the form the widest range of consumers accepts.
bool PutDibOnClipboard(HWND owner, IWICBitmap* bitmap) {
    UINT w = 0, h = 0;
    bitmap->GetSize(&w, &h);
    if (!w || !h) return false;

    WICRect rect{ 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, lock.GetAddressOf()))) return false;

    UINT stride = 0, bytes = 0;
    BYTE* pixels = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bytes, &pixels)))
        return false;

    const SIZE_T rowBytes = static_cast<SIZE_T>(w) * 4;
    const SIZE_T total    = sizeof(BITMAPINFOHEADER) + rowBytes * h;

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!handle) return false;

    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(handle));
    if (!header) {
        GlobalFree(handle);
        return false;
    }

    *header = {};
    header->biSize        = sizeof(BITMAPINFOHEADER);
    header->biWidth       = static_cast<LONG>(w);
    header->biHeight      = static_cast<LONG>(h);   // positive: bottom-up
    header->biPlanes      = 1;
    header->biBitCount    = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage   = static_cast<DWORD>(rowBytes * h);

    auto* dest = reinterpret_cast<BYTE*>(header + 1);
    for (UINT row = 0; row < h; row++)
        memcpy(dest + rowBytes * row, pixels + static_cast<SIZE_T>(stride) * (h - 1 - row), rowBytes);

    GlobalUnlock(handle);

    if (!OpenClipboard(owner)) {
        GlobalFree(handle);
        return false;
    }
    EmptyClipboard();
    const bool ok = ::SetClipboardData(CF_DIB, handle) != nullptr;
    CloseClipboard();

    if (!ok) GlobalFree(handle);   // the clipboard did not take ownership
    return ok;
}

} // namespace

HRESULT GraphRenderer::RenderOffscreen(IWICImagingFactory* wic, const RECT& clientRect,
                                       const GraphData& data, const AxisRange& range,
                                       const GraphTheme& theme, const RenderOptions& opt,
                                       const ValueFormatFn& formatter, IWICBitmap** out) {
    if (!m_D2dFactory || !wic || !out) return E_FAIL;
    *out = nullptr;

    const UINT w = static_cast<UINT>(std::max<LONG>(clientRect.right - clientRect.left, 1));
    const UINT h = static_cast<UINT>(std::max<LONG>(clientRect.bottom - clientRect.top, 1));

    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapCacheOnLoad, bitmap.GetAddressOf());
    if (FAILED(hr)) return hr;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        m_Dpi, m_Dpi);

    ComPtr<ID2D1RenderTarget> target;
    hr = m_D2dFactory->CreateWicBitmapRenderTarget(bitmap.Get(), props, target.GetAddressOf());
    if (FAILED(hr)) return hr;

    // Brushes belong to a target, so the offscreen pass gets its own set. The
    // geometry cache comes from the factory and is reused as is.
    auto savedTarget = m_RenderTarget;
    auto savedBrush  = m_Brush;
    auto savedFills  = std::move(m_FillBrushes);
    m_FillBrushes.clear();

    m_RenderTarget = target;
    hr = m_RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),
                                               m_Brush.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr))
        hr = DrawFrame(clientRect, data, range, theme, opt, formatter);

    m_FillBrushes  = std::move(savedFills);
    m_Brush        = savedBrush;
    m_RenderTarget = savedTarget;

    if (FAILED(hr)) return hr;

    *out = bitmap.Detach();
    return S_OK;
}

bool GraphRenderer::SaveImage(const wchar_t* path, const RECT& clientRect,
                              const GraphData& data, const AxisRange& range,
                              const GraphTheme& theme, const RenderOptions& opt,
                              const ValueFormatFn& formatter) {
    if (!path || !*path) return false;

    ComScope com;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(RenderOffscreen(wic.Get(), clientRect, data, range, theme, opt, formatter,
                               bitmap.GetAddressOf())))
        return false;

    return WritePng(wic.Get(), bitmap.Get(), path);
}

bool GraphRenderer::CopyToClipboard(HWND owner, const RECT& clientRect,
                                    const GraphData& data, const AxisRange& range,
                                    const GraphTheme& theme, const RenderOptions& opt,
                                    const ValueFormatFn& formatter) {
    ComScope com;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(RenderOffscreen(wic.Get(), clientRect, data, range, theme, opt, formatter,
                               bitmap.GetAddressOf())))
        return false;

    return PutDibOnClipboard(owner, bitmap.Get());
}

} // namespace GraphCtrl
