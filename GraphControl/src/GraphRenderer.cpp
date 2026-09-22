#include "../include/GraphRenderer.h"
#include <algorithm>
#include <cmath>

namespace GraphCtrl {

static D2D1_COLOR_F ColorrefToD2D(COLORREF cr, float alpha = 1.0f) {
    return D2D1::ColorF(
        GetRValue(cr) / 255.0f,
        GetGValue(cr) / 255.0f,
        GetBValue(cr) / 255.0f,
        alpha);
}

// Text band heights in DIPs; big enough for the formats created in Init().
constexpr float k_TitleHeight = 18.0f;
constexpr float k_LabelHeight = 16.0f;
constexpr float k_Padding     = 8.0f;

// Overlay metrics (legend rows, readout box).
constexpr float k_SwatchSize   = 10.0f;
constexpr float k_OverlayPad   = 7.0f;
constexpr float k_LegendRowH   = 16.0f;
constexpr float k_LegendInset  = 10.0f;
constexpr float k_ReadoutMaxW  = 280.0f;

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
    return t;
}

GraphRenderer::~GraphRenderer() {
    Shutdown();
}

HRESULT GraphRenderer::Init() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_D2dFactory.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_DwFactory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-US", m_LabelFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_LabelFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_LabelFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.5f, L"en-US", m_TitleFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_TitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_TitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // The readout stacks one line per series, so it is top-aligned, not centered.
    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-US", m_OverlayFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_OverlayFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_OverlayFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_OverlayFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    return m_D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, m_DashStyle.GetAddressOf());
}

void GraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_DashStyle.Reset();
    m_OverlayFormat.Reset();
    m_TitleFormat.Reset();
    m_LabelFormat.Reset();
    m_DwFactory.Reset();
    m_D2dFactory.Reset();
}

HRESULT GraphRenderer::EnsureDeviceResources(HWND hwnd, const RECT& clientRect) {
    if (!m_D2dFactory) return E_FAIL;

    const auto pixelSize = D2D1::SizeU(
        static_cast<UINT32>(std::max<LONG>(clientRect.right - clientRect.left, 1)),
        static_cast<UINT32>(std::max<LONG>(clientRect.bottom - clientRect.top, 1)));

    if (m_RenderTarget) {
        const auto current = m_RenderTarget->GetPixelSize();
        if (current.width != pixelSize.width || current.height != pixelSize.height)
            m_RenderTarget->Resize(pixelSize);
        return S_OK;
    }

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        m_Dpi, m_Dpi);

    HRESULT hr = m_D2dFactory->CreateHwndRenderTarget(
        props,
        D2D1::HwndRenderTargetProperties(hwnd, pixelSize),
        m_RenderTarget.GetAddressOf());
    if (FAILED(hr)) return hr;

    return m_RenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::Black), m_Brush.GetAddressOf());
}

void GraphRenderer::DiscardDeviceResources() {
    m_FillBrushes.clear();
    m_Brush.Reset();
    m_RenderTarget.Reset();
}

void GraphRenderer::Resize(UINT width, UINT height) {
    if (m_RenderTarget && width && height)
        m_RenderTarget->Resize(D2D1::SizeU(width, height));
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
        k_Padding + (hasHeader ? k_TitleHeight : 0.0f),
        w - k_Padding,
        h - k_Padding - (hasFooter ? k_LabelHeight : 0.0f));
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

// ---- Series ------------------------------------------------------------------

void GraphRenderer::DrawSeries(const Series& s, const D2D1_RECT_F& plot,
                               const AxisRange& range, bool fill) {
    const size_t n = s.Count();
    if (n < 2) return;

    const float dx   = SampleSpacing(plot, s.Capacity());
    const float span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;

    // Newest sample sits at the right edge, so a partly filled history grows in
    // from the right the way the Task Manager graphs do.
    auto xOf = [&](size_t i) { return plot.right - (n - 1 - i) * dx; };
    auto yOf = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };

    const SeriesStyle& style = s.Style();

    if (fill && style.FillOpacity > 0.0f) {
        ComPtr<ID2D1PathGeometry> geometry;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf()))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(xOf(0), plot.bottom), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 0; i < n; i++)
                    sink->AddLine(D2D1::Point2F(xOf(i), yOf(s.At(i))));
                sink->AddLine(D2D1::Point2F(xOf(n - 1), plot.bottom));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();

                if (auto* brush = FillBrush(style.FillColor, style.FillOpacity, plot))
                    m_RenderTarget->FillGeometry(geometry.Get(), brush);
            }
        }
    }

    ComPtr<ID2D1PathGeometry> line;
    if (FAILED(m_D2dFactory->CreatePathGeometry(line.GetAddressOf()))) return;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(line->Open(sink.GetAddressOf()))) return;

    sink->BeginFigure(D2D1::Point2F(xOf(0), yOf(s.At(0))), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < n; i++)
        sink->AddLine(D2D1::Point2F(xOf(i), yOf(s.At(i))));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    m_Brush->SetColor(ColorrefToD2D(style.LineColor));
    m_RenderTarget->DrawGeometry(line.Get(), m_Brush.Get(), style.LineWidth,
                                 style.Dashed ? m_DashStyle.Get() : nullptr);
}

// Stacked: each series sits on the running total of the ones before it. Only the
// slots every visible series holds are drawn, so a shorter series cannot shift
// the ones above it.
void GraphRenderer::DrawStacked(const GraphData& data, const D2D1_RECT_F& plot,
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

    auto xOfK = [&](size_t k) { return plot.right - k * dx; };   // k back from newest
    auto yOf  = [&](float v) {
        float t = (v - range.Min) / span;
        t = std::clamp(t, 0.0f, 1.0f);
        return plot.bottom - t * (plot.bottom - plot.top);
    };

    m_Baseline.assign(n, 0.0f);

    for (const auto& s : data.AllSeries()) {
        if (!s.Style().Visible) continue;
        const SeriesStyle& style = s.Style();

        if (fill && style.FillOpacity > 0.0f) {
            ComPtr<ID2D1PathGeometry> band;
            if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(band.GetAddressOf()))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(band->Open(sink.GetAddressOf()))) {
                    // Oldest to newest along the top, then back along the baseline.
                    sink->BeginFigure(D2D1::Point2F(xOfK(n - 1), yOf(m_Baseline[n - 1] + s.AtFromEnd(n - 1))),
                                      D2D1_FIGURE_BEGIN_FILLED);
                    for (size_t k = n; k-- > 0; )
                        sink->AddLine(D2D1::Point2F(xOfK(k), yOf(m_Baseline[k] + s.AtFromEnd(k))));
                    for (size_t k = 0; k < n; k++)
                        sink->AddLine(D2D1::Point2F(xOfK(k), yOf(m_Baseline[k])));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    if (auto* brush = FillBrush(style.FillColor, style.FillOpacity, plot))
                        m_RenderTarget->FillGeometry(band.Get(), brush);
                }
            }
        }

        ComPtr<ID2D1PathGeometry> line;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(line.GetAddressOf()))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(line->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(xOfK(n - 1), yOf(m_Baseline[n - 1] + s.AtFromEnd(n - 1))),
                                  D2D1_FIGURE_BEGIN_HOLLOW);
                for (size_t k = n - 1; k-- > 0; )
                    sink->AddLine(D2D1::Point2F(xOfK(k), yOf(m_Baseline[k] + s.AtFromEnd(k))));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();

                m_Brush->SetColor(ColorrefToD2D(style.LineColor));
                m_RenderTarget->DrawGeometry(line.Get(), m_Brush.Get(), style.LineWidth,
                                             style.Dashed ? m_DashStyle.Get() : nullptr);
            }
        }

        for (size_t k = 0; k < n; k++)
            m_Baseline[k] += s.AtFromEnd(k);
    }
}

// ---- Overlays ----------------------------------------------------------------

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
                m_LabelFormat.Get(), 200.0f, k_LegendRowH, layout.GetAddressOf()))) {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            widest = std::max(widest, tm.widthIncludingTrailingWhitespace);
        }
        rows++;
    }
    if (rows == 0) return;

    const float w = k_OverlayPad * 2 + k_SwatchSize + 6.0f + widest;
    const float h = k_OverlayPad * 2 + rows * k_LegendRowH;
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

        const float swatchY = rowY + (k_LegendRowH - k_SwatchSize) / 2;
        const auto swatch = D2D1::RoundedRect(
            D2D1::RectF(x + k_OverlayPad, swatchY,
                        x + k_OverlayPad + k_SwatchSize, swatchY + k_SwatchSize), 2.0f, 2.0f);
        m_Brush->SetColor(ColorrefToD2D(s.Style().LineColor));
        m_RenderTarget->FillRoundedRectangle(swatch, m_Brush.Get());

        m_Brush->SetColor(ColorrefToD2D(theme.TextColor));
        DrawLabel(s.Name().c_str(),
                  D2D1::RectF(x + k_OverlayPad + k_SwatchSize + 6.0f, rowY, x + w, rowY + k_LegendRowH),
                  m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

        rowY += k_LegendRowH;
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
        const float top   = opt.Stacked ? baseline + value : value;
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
    const D2D1_RECT_F header = D2D1::RectF(client.left + k_Padding, plot.top - k_TitleHeight,
                                           client.right - k_Padding, plot.top);
    DrawLabel(opt.Title, header, m_TitleFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

    if (!opt.AxisLabels) return;

    wchar_t buffer[64] = {};
    if (formatter) formatter(range.Max, buffer, _countof(buffer));
    else           Format::Number(range.Max, buffer, _countof(buffer));
    DrawLabel(buffer, header, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    // Footer band: time span on the left, axis minimum on the right.
    const D2D1_RECT_F footer = D2D1::RectF(client.left + k_Padding, plot.bottom,
                                           client.right - k_Padding, plot.bottom + k_LabelHeight);
    DrawLabel(opt.TimeSpanText, footer, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_LEADING);

    buffer[0] = 0;
    if (formatter) formatter(range.Min, buffer, _countof(buffer));
    else           Format::Number(range.Min, buffer, _countof(buffer));
    DrawLabel(buffer, footer, m_LabelFormat.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
}

// ---- Frame -------------------------------------------------------------------

void GraphRenderer::Render(HWND hwnd, const RECT& clientRect, const GraphData& data,
                           const AxisRange& range, const GraphTheme& theme,
                           const RenderOptions& opt, const ValueFormatFn& formatter) {
    if (FAILED(EnsureDeviceResources(hwnd, clientRect))) return;

    const D2D1_SIZE_F size = m_RenderTarget->GetSize();   // DIPs
    if (size.width <= 0.0f || size.height <= 0.0f) return;

    const D2D1_RECT_F client = D2D1::RectF(0.0f, 0.0f, size.width, size.height);
    const D2D1_RECT_F plot   = PlotRect(clientRect, opt);

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(ColorrefToD2D(theme.Background));

    if (plot.right - plot.left >= 4.0f && plot.bottom - plot.top >= 4.0f) {
        m_Brush->SetColor(ColorrefToD2D(theme.PlotColor));
        m_RenderTarget->FillRectangle(plot, m_Brush.Get());

        if (opt.DrawGrid)
            DrawGrid(plot, opt, theme);

        m_RenderTarget->PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_ALIASED);
        if (opt.Stacked) {
            DrawStacked(data, plot, range, opt.Fill);
        }
        else {
            for (const auto& s : data.AllSeries()) {
                if (s.Style().Visible)
                    DrawSeries(s, plot, range, opt.Fill);
            }
        }
        m_RenderTarget->PopAxisAlignedClip();

        m_Brush->SetColor(ColorrefToD2D(theme.BorderColor));
        const D2D1_RECT_F border = D2D1::RectF(
            std::floor(plot.left) + 0.5f, std::floor(plot.top) + 0.5f,
            std::floor(plot.right) - 0.5f, std::floor(plot.bottom) - 0.5f);
        m_RenderTarget->DrawRectangle(border, m_Brush.Get(), 1.0f);

        if (opt.Legend)
            DrawLegend(plot, data, theme);

        DrawCrosshair(plot, data, range, opt, theme);
        DrawChrome(plot, client, range, theme, opt, formatter);
    }

    if (m_RenderTarget->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

} // namespace GraphCtrl
