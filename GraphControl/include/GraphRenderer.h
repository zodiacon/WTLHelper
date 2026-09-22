#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <utility>
#include <vector>

#include "GraphData.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace GraphCtrl {

using Microsoft::WRL::ComPtr;

struct GraphTheme {
    COLORREF Background  = RGB(24, 24, 24);
    COLORREF PlotColor   = RGB(31, 31, 31);   // inside the graph box
    COLORREF GridColor   = RGB(56, 56, 56);
    COLORREF BorderColor = RGB(92, 92, 92);
    COLORREF TextColor   = RGB(190, 190, 190);

    static GraphTheme Dark();
    static GraphTheme Light();
};

// Everything the control tells the renderer about one frame.
struct RenderOptions {
    bool  DrawGrid   = true;
    bool  ScrollGrid = false;   // vertical grid lines drift left with the data
    bool  Fill       = true;    // gradient area under each line
    bool  AxisLabels = true;    // max value, "0", and the time-span caption
    int   GridCols   = 10;
    int   GridRows   = 5;
    float GridPhase  = 0.0f;    // 0..1, scroll offset within one grid cell

    const wchar_t* Title        = nullptr;   // above the box, left
    const wchar_t* TimeSpanText = nullptr;   // below the box, left
};

// Direct2D drawing for CGraphControl. Device-independent resources live for the
// lifetime of the renderer; device-dependent ones are rebuilt after a lost device.
class GraphRenderer {
public:
    GraphRenderer() = default;
    ~GraphRenderer();

    GraphRenderer(const GraphRenderer&)            = delete;
    GraphRenderer& operator=(const GraphRenderer&) = delete;

    HRESULT Init();
    void    Shutdown();

    // Called on WM_SIZE / WM_DPICHANGED; no-ops before the target exists.
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);

    // Called on WM_PAINT. clientRect is in pixels.
    void Render(HWND hwnd, const RECT& clientRect, const GraphData& data,
                const AxisRange& range, const GraphTheme& theme,
                const RenderOptions& opt, const ValueFormatFn& formatter);

private:
    HRESULT EnsureDeviceResources(HWND hwnd, const RECT& clientRect);
    void    DiscardDeviceResources();

    // plot is the graph box in DIPs, borders included.
    void DrawGrid(const D2D1_RECT_F& plot, const RenderOptions& opt, const GraphTheme& theme);
    void DrawSeries(const Series& s, const D2D1_RECT_F& plot, const AxisRange& range, bool fill);
    void DrawChrome(const D2D1_RECT_F& plot, const D2D1_RECT_F& client,
                    const AxisRange& range, const GraphTheme& theme,
                    const RenderOptions& opt, const ValueFormatFn& formatter);
    void DrawLabel(const wchar_t* text, const D2D1_RECT_F& rc,
                   IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT align);

    ID2D1LinearGradientBrush* FillBrush(COLORREF color, float opacity, const D2D1_RECT_F& plot);

    // Device-independent.
    ComPtr<ID2D1Factory>      m_D2dFactory;
    ComPtr<IDWriteFactory>    m_DwFactory;
    ComPtr<IDWriteTextFormat> m_LabelFormat;   // 11pt, axis captions
    ComPtr<IDWriteTextFormat> m_TitleFormat;   // 12.5pt, graph title
    ComPtr<ID2D1StrokeStyle>  m_DashStyle;

    // Device-dependent.
    ComPtr<ID2D1HwndRenderTarget> m_RenderTarget;
    ComPtr<ID2D1SolidColorBrush>  m_Brush;
    std::vector<std::pair<COLORREF, ComPtr<ID2D1LinearGradientBrush>>> m_FillBrushes;

    float m_Dpi = 96.0f;
};

} // namespace GraphCtrl
