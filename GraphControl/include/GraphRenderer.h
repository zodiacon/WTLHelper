#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <utility>
#include <vector>

#include "GraphData.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace GraphCtrl {

using Microsoft::WRL::ComPtr;

struct GraphTheme {
    COLORREF Background     = RGB(24, 24, 24);
    COLORREF PlotColor      = RGB(31, 31, 31);   // inside the graph box
    COLORREF GridColor      = RGB(56, 56, 56);
    COLORREF BorderColor    = RGB(92, 92, 92);
    COLORREF TextColor      = RGB(190, 190, 190);
    COLORREF CrosshairColor = RGB(150, 150, 150);
    COLORREF PanelColor     = RGB(44, 44, 44);   // legend and hover readout backing
    COLORREF ValueColor     = RGB(225, 225, 225);// current-value overlay

    static GraphTheme Dark();
    static GraphTheme Light();
};

using RefLineId = uint32_t;
constexpr RefLineId InvalidRefLine = UINT32_MAX;

// A horizontal marker at a fixed value: a threshold, a quota, a baseline.
struct ReferenceLine {
    RefLineId    Id     = InvalidRefLine;
    float        Value  = 0.0f;
    COLORREF     Color  = RGB(214, 94, 94);
    float        Width  = 1.0f;
    bool         Dashed = true;
    std::wstring Label;     // drawn right-aligned just above the line
};

using SegmentId = uint32_t;
constexpr SegmentId InvalidSegment = UINT32_MAX;

// One slice of a composition bar: a share of a whole, not a point in time.
struct CompositionSegment {
    SegmentId    Id    = InvalidSegment;
    std::wstring Name;
    float        Value = 0.0f;
    COLORREF     Color = RGB(70, 130, 180);
};

struct CompositionOptions {
    const wchar_t* Title      = nullptr;
    bool           ShowLabels = true;
    // Segments run left to right, or top to bottom when vertical -- either way
    // in the order they were added, so a host that wants the other direction
    // adds them the other way round.
    bool           Vertical   = false;
    float          Total      = 0.0f;   // 0 = the sum of the segments

    int            HoverIndex = -1;
    float          HoverX     = 0.0f;
    float          HoverY     = 0.0f;
    const wchar_t* HoverText  = nullptr;
};

// Everything the control tells the renderer about one frame.
struct RenderOptions {
    bool  DrawGrid     = true;
    bool  ScrollGrid   = false;   // vertical grid lines drift left with the data
    bool  Fill         = true;    // gradient area under each line
    bool  AxisLabels   = true;    // max value, "0", and the time-span caption
    bool  Legend       = false;   // swatch + name per series, inside the plot
    bool  Stacked      = false;   // series accumulate instead of overlapping
    bool  Bars         = false;   // columns per sample instead of a line
    bool  ValueOverlay = false;   // large current reading inside the plot
    int   GridCols     = 10;
    int   GridRows     = 5;
    float GridPhase    = 0.0f;    // 0..1, scroll offset within one grid cell

    const wchar_t* Title        = nullptr;   // above the box, left
    const wchar_t* TimeSpanText = nullptr;   // below the box, left
    const wchar_t* ValueText    = nullptr;   // the current-value overlay text

    const ReferenceLine* RefLines     = nullptr;
    size_t               RefLineCount = 0;

    // Hover crosshair. HoverIndex counts back from the newest sample; a negative
    // value means the pointer is not over the plot.
    int            HoverIndex = -1;
    float          HoverX     = 0.0f;   // cursor position in DIPs, for the readout
    float          HoverY     = 0.0f;
    const wchar_t* HoverText  = nullptr;
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

    // Swap in a private set of text formats. Sizes are in DIPs and the header,
    // footer and overlay bands are re-measured from them, so the layout follows
    // the font instead of assuming one. ResetFont goes back to the shared
    // defaults; a custom font is per control, the default set is shared.
    HRESULT SetFont(const wchar_t* family, float labelSize, float titleSize, float valueSize);
    void    ResetFont();

    // Called on WM_SIZE / WM_DPICHANGED; no-ops before the target exists.
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    float GetDpi() const { return m_Dpi; }

    // Called on WM_PAINT. clientRect is in pixels.
    void Render(HWND hwnd, const RECT& clientRect, const GraphData& data,
                const AxisRange& range, const GraphTheme& theme,
                const RenderOptions& opt, const ValueFormatFn& formatter);

    // Draw the same frame into an offscreen bitmap and hand it out: as a PNG
    // file, or as a device-independent bitmap on the clipboard. Both go through
    // the drawing code the window uses, so what you get is what you saw.
    bool SaveImage(const wchar_t* path, const RECT& clientRect, const GraphData& data,
                   const AxisRange& range, const GraphTheme& theme,
                   const RenderOptions& opt, const ValueFormatFn& formatter);
    bool CopyToClipboard(HWND owner, const RECT& clientRect, const GraphData& data,
                         const AxisRange& range, const GraphTheme& theme,
                         const RenderOptions& opt, const ValueFormatFn& formatter);

    // Draw into a DC the system handed us: WM_PRINTCLIENT, so the control shows
    // up in PrintWindow captures and host print paths instead of coming out blank.
    bool RenderToDC(HDC dc, const RECT& clientRect, const GraphData& data,
                    const AxisRange& range, const GraphTheme& theme,
                    const RenderOptions& opt, const ValueFormatFn& formatter);

    // ---- Composition bar ----
    void RenderComposition(HWND hwnd, const RECT& clientRect,
                           const std::vector<CompositionSegment>& segments,
                           const GraphTheme& theme, const CompositionOptions& opt,
                           const ValueFormatFn& formatter);
    bool RenderCompositionToDC(HDC dc, const RECT& clientRect,
                               const std::vector<CompositionSegment>& segments,
                               const GraphTheme& theme, const CompositionOptions& opt,
                               const ValueFormatFn& formatter);

    D2D1_RECT_F CompositionBarRect(const RECT& clientRect,
                                   const CompositionOptions& opt) const;
    int HitTestSegment(const RECT& clientRect,
                       const std::vector<CompositionSegment>& segments,
                       const CompositionOptions& opt, POINT pt) const;

    // The plot box for a client rect, in DIPs. The control uses it to turn a
    // cursor position into a sample index, so both agree on the mapping.
    D2D1_RECT_F PlotRect(const RECT& clientRect, const RenderOptions& opt) const;

    // Horizontal spacing between samples inside plot, in DIPs.
    static float SampleSpacing(const D2D1_RECT_F& plot, size_t capacity);

private:
    // Chrome sizes for one frame. Small plots -- a tile in a grid -- give up
    // padding and the footer band rather than shrinking the type, so the text
    // stays legible and the plot gets the space back.
    struct ChromeMetrics {
        float Padding     = 8.0f;
        float TitleHeight = 18.0f;
        float LabelHeight = 16.0f;
        bool  ShowHeader  = true;
        bool  ShowFooter  = true;
    };

    ChromeMetrics MetricsFor(const RECT& clientRect, const RenderOptions& opt) const;

    // Path geometry comes from the D2D factory, not the render target, so the
    // cache is device-independent and survives a lost device.
    struct SeriesGeometry {
        ComPtr<ID2D1PathGeometry> Fill;
        ComPtr<ID2D1PathGeometry> Line;
    };

    // Everything the built geometry depends on. Anything else (colors, legend,
    // crosshair, grid) can change without a rebuild.
    struct GeometryKey {
        uint64_t Version     = 0;
        uint64_t VisibleMask = 0;
        size_t   SeriesCount = 0;
        size_t   Capacity    = 0;
        float    Left = 0, Top = 0, Right = 0, Bottom = 0;
        float    RangeMin = 0, RangeMax = 0;
        bool     Stacked = false, Fill = false, Bars = false;

        bool Matches(const GeometryKey& o) const;
    };

    HRESULT EnsureDeviceResources(HWND hwnd, const RECT& clientRect);
    void    DiscardDeviceResources();

    // One frame, from BeginDraw to EndDraw, against whatever m_RenderTarget is.
    HRESULT DrawFrame(const RECT& clientRect, const GraphData& data,
                      const AxisRange& range, const GraphTheme& theme,
                      const RenderOptions& opt, const ValueFormatFn& formatter);

    // Draws one frame against a target that is not the window: the brushes
    // belong to a target, so this swaps in a fresh set and restores after.
    HRESULT DrawFrameOn(ID2D1RenderTarget* target, const RECT& clientRect,
                        const GraphData& data, const AxisRange& range,
                        const GraphTheme& theme, const RenderOptions& opt,
                        const ValueFormatFn& formatter);

    HRESULT DrawComposition(const RECT& clientRect,
                            const std::vector<CompositionSegment>& segments,
                            const GraphTheme& theme, const CompositionOptions& opt,
                            const ValueFormatFn& formatter);

    // Swaps in a WIC-backed target, draws one frame, swaps back.
    HRESULT RenderOffscreen(IWICImagingFactory* wic, const RECT& clientRect,
                            const GraphData& data, const AxisRange& range,
                            const GraphTheme& theme, const RenderOptions& opt,
                            const ValueFormatFn& formatter, IWICBitmap** out);

    // plot is the graph box in DIPs, borders included.
    void DrawGrid(const D2D1_RECT_F& plot, const RenderOptions& opt, const GraphTheme& theme);

    void EnsureGeometry(const GraphData& data, const D2D1_RECT_F& plot,
                        const AxisRange& range, const RenderOptions& opt);
    void BuildOverlay(const GraphData& data, const D2D1_RECT_F& plot,
                      const AxisRange& range, bool fill);
    void BuildStacked(const GraphData& data, const D2D1_RECT_F& plot,
                      const AxisRange& range, bool fill);
    void BuildBars(const GraphData& data, const D2D1_RECT_F& plot,
                   const AxisRange& range, bool stacked);
    void DrawGeometry(const GraphData& data, const D2D1_RECT_F& plot, const RenderOptions& opt);

    void DrawReferenceLines(const D2D1_RECT_F& plot, const AxisRange& range,
                            const RenderOptions& opt, const ChromeMetrics& metrics);
    void DrawValueOverlay(const D2D1_RECT_F& plot, const RenderOptions& opt,
                          const GraphTheme& theme, const ChromeMetrics& metrics);
    void DrawChrome(const D2D1_RECT_F& plot, const D2D1_RECT_F& client,
                    const AxisRange& range, const GraphTheme& theme,
                    const RenderOptions& opt, const ValueFormatFn& formatter,
                    const ChromeMetrics& metrics);
    void DrawLegend(const D2D1_RECT_F& plot, const GraphData& data, const GraphTheme& theme,
                    const ChromeMetrics& metrics);

    // Where the current-value overlay sits, so other chrome can avoid it.
    D2D1_RECT_F ValueOverlayRect(const D2D1_RECT_F& plot, const ChromeMetrics& metrics) const;
    void DrawCrosshair(const D2D1_RECT_F& plot, const GraphData& data,
                       const AxisRange& range, const RenderOptions& opt,
                       const GraphTheme& theme);
    void DrawReadout(const wchar_t* text, float x, float y,
                     const D2D1_RECT_F& plot, const GraphTheme& theme);
    void DrawLabel(const wchar_t* text, const D2D1_RECT_F& rc,
                   IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT align);

    ID2D1LinearGradientBrush* FillBrush(COLORREF color, float opacity, const D2D1_RECT_F& plot);

    // Height one line of a format actually occupies, plus breathing room.
    float MeasuredHeight(IDWriteTextFormat* format, float fallback) const;
    void  RecomputeMetrics();

    // Device-independent, and shared with every other renderer on this thread
    // (see AcquireShared in the .cpp). These are references, not ownership.
    bool                      m_SharedHeld   = false;
    bool                      m_PrivateFonts = false;
    ComPtr<ID2D1Factory>      m_D2dFactory;
    ComPtr<IDWriteFactory>    m_DwFactory;
    ComPtr<IDWriteTextFormat> m_LabelFormat;   // 11pt, axis captions and overlays
    ComPtr<IDWriteTextFormat> m_TitleFormat;   // 12.5pt, graph title
    ComPtr<IDWriteTextFormat> m_OverlayFormat; // 11pt, top-aligned, multi-line readout
    ComPtr<IDWriteTextFormat> m_ValueFormat;   // 22pt, current-value overlay
    ComPtr<ID2D1StrokeStyle>  m_DashStyle;

    // Device-dependent. m_RenderTarget is what the drawing code talks to; it is
    // normally m_HwndTarget, and points at an offscreen target during an export.
    ComPtr<ID2D1HwndRenderTarget> m_HwndTarget;
    ComPtr<ID2D1DCRenderTarget>   m_DcTarget;     // for WM_PRINTCLIENT
    ComPtr<ID2D1RenderTarget>     m_RenderTarget;
    ComPtr<ID2D1SolidColorBrush>  m_Brush;
    std::vector<std::pair<COLORREF, ComPtr<ID2D1LinearGradientBrush>>> m_FillBrushes;

    // Geometry cache, one entry per series (empty for hidden ones).
    std::vector<SeriesGeometry> m_Geometry;
    GeometryKey                 m_GeometryKey;
    bool                        m_GeometryValid = false;

    // Band heights in DIPs, measured from the formats in use.
    float m_TitleHeight = 18.0f;
    float m_LabelHeight = 16.0f;
    float m_ValueHeight = 32.0f;

    // Scratch for stacked building, kept between frames to avoid reallocating.
    std::vector<float> m_Baseline;

    float m_Dpi = 96.0f;
};

} // namespace GraphCtrl
