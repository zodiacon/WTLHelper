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

// Text band heights in DIPs; big enough for the two formats created in Init().
constexpr float k_TitleHeight = 18.0f;
constexpr float k_LabelHeight = 16.0f;
constexpr float k_Padding     = 8.0f;

GraphTheme GraphTheme::Dark() {
    return GraphTheme{};
}

GraphTheme GraphTheme::Light() {
    GraphTheme t;
    t.Background  = RGB(255, 255, 255);
    t.PlotColor   = RGB(249, 249, 249);
    t.GridColor   = RGB(220, 220, 220);
    t.BorderColor = RGB(160, 160, 160);
    t.TextColor   = RGB(70, 70, 70);
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

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    return m_D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, m_DashStyle.GetAddressOf());
}

void GraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_DashStyle.Reset();
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

void GraphRenderer::DrawSeries(const Series& s, const D2D1_RECT_F& plot,
                               const AxisRange& range, bool fill) {
    const size_t n = s.Count();
    if (n < 2) return;

    const size_t cap  = s.Capacity();
    const float  w    = plot.right - plot.left;
    const float  dx   = (cap > 1) ? w / (cap - 1) : w;
    const float  span = (range.Max - range.Min) != 0.0f ? (range.Max - range.Min) : 1.0f;

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

void GraphRenderer::Render(HWND hwnd, const RECT& clientRect, const GraphData& data,
                           const AxisRange& range, const GraphTheme& theme,
                           const RenderOptions& opt, const ValueFormatFn& formatter) {
    if (FAILED(EnsureDeviceResources(hwnd, clientRect))) return;

    const D2D1_SIZE_F size = m_RenderTarget->GetSize();   // DIPs
    if (size.width <= 0.0f || size.height <= 0.0f) return;

    const D2D1_RECT_F client = D2D1::RectF(0.0f, 0.0f, size.width, size.height);

    const bool hasHeader = (opt.Title && *opt.Title) || opt.AxisLabels;
    const bool hasFooter = opt.AxisLabels;

    const D2D1_RECT_F plot = D2D1::RectF(
        client.left + k_Padding,
        client.top + k_Padding + (hasHeader ? k_TitleHeight : 0.0f),
        client.right - k_Padding,
        client.bottom - k_Padding - (hasFooter ? k_LabelHeight : 0.0f));

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(ColorrefToD2D(theme.Background));

    if (plot.right - plot.left >= 4.0f && plot.bottom - plot.top >= 4.0f) {
        m_Brush->SetColor(ColorrefToD2D(theme.PlotColor));
        m_RenderTarget->FillRectangle(plot, m_Brush.Get());

        if (opt.DrawGrid)
            DrawGrid(plot, opt, theme);

        m_RenderTarget->PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_ALIASED);
        for (const auto& s : data.AllSeries()) {
            if (s.Style().Visible)
                DrawSeries(s, plot, range, opt.Fill);
        }
        m_RenderTarget->PopAxisAlignedClip();

        m_Brush->SetColor(ColorrefToD2D(theme.BorderColor));
        const D2D1_RECT_F border = D2D1::RectF(
            std::floor(plot.left) + 0.5f, std::floor(plot.top) + 0.5f,
            std::floor(plot.right) - 0.5f, std::floor(plot.bottom) - 0.5f);
        m_RenderTarget->DrawRectangle(border, m_Brush.Get(), 1.0f);

        DrawChrome(plot, client, range, theme, opt, formatter);
    }

    if (m_RenderTarget->EndDraw() == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

} // namespace GraphCtrl
