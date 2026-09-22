#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlgdi.h>
#include <atltypes.h>

#include "GraphData.h"
#include "GraphRenderer.h"

// Notification codes sent via WM_NOTIFY to the parent window.
#define GCN_RANGECHANGED   1   // lParam -> GRAPHRANGENOTIFY*, auto-scale moved the Y range
#define GCN_HOVERSAMPLE    2   // lParam -> GRAPHHOVERNOTIFY*, hovered sample changed
#define GCN_GETTOOLTIP     3   // lParam -> GRAPHTOOLTIPNOTIFY*; fill SzText to override
#define GCN_CONTEXTMENU    4   // lParam -> GRAPHCONTEXTNOTIFY*, right-click on the graph

namespace GraphCtrl {

struct GRAPHRANGENOTIFY {
    NMHDR Hdr;
    float Min;
    float Max;
};

// SampleIndex counts back from the newest sample (0 = newest); -1 means the
// pointer left the plot.
struct GRAPHHOVERNOTIFY {
    NMHDR Hdr;
    int   SampleIndex;
};

struct GRAPHTOOLTIPNOTIFY {
    NMHDR   Hdr;
    int     SampleIndex;
    wchar_t SzText[256];   // pre-filled with the default readout; host may override
};

// Pt is in screen coordinates, ready for TrackPopupMenu. SampleIndex is the
// sample under the pointer, or -1 outside the plot.
struct GRAPHCONTEXTNOTIFY {
    NMHDR Hdr;
    POINT Pt;
    int   SampleIndex;
};

// Register the window class. Call once at startup (or on DLL attach).
// Returns false if registration fails and the class does not already exist.
bool Register(HINSTANCE hInstance);

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_GRAPHCONTROL[] = L"GraphControl";

// Control styles, packed into the low-order style bits like LVS_*/ES_*.
// All of them can also be changed after creation with ModifyGraphStyle.
#define GCS_GRID        0x0001   // background grid
#define GCS_SCROLLGRID  0x0002   // grid drifts left with the data
#define GCS_FILL        0x0004   // gradient area fill under each line
#define GCS_AUTOSCALE   0x0008   // recompute the Y maximum from the data
#define GCS_AXISLABELS  0x0010   // draw the range captions and time span
#define GCS_LEGEND      0x0020   // swatch and name per series, inside the plot
#define GCS_STACKED     0x0040   // series accumulate instead of overlapping
#define GCS_TOOLTIP     0x0080   // hover crosshair and value readout
#define GCS_BARS        0x0100   // columns per sample instead of a line
#define GCS_VALUEOVERLAY 0x0200  // large current reading inside the plot

// Default look: Task Manager style, fixed 0..100 range.
#define GCS_DEFAULT  (GCS_GRID | GCS_FILL | GCS_AXISLABELS)

// Private: carries samples posted from another thread to the UI thread.
constexpr UINT WM_GRAPHPOSTSAMPLE = WM_APP + 0x100;

// Fills one value per series, in AddSeries order. Installed with
// SetSampleSource to let the control pull its own data on each timer tick.
using SampleSourceFn = std::function<void(std::vector<float>& values)>;

// ---- Main control ----------------------------------------------------------
// A self-contained WTL control: derive-and-embed like any other CWindowImpl
// class (e.g. CGraphControl m_wndCpu; m_wndCpu.Create(...);).
class CGraphControl : public CWindowImpl<CGraphControl> {
public:
    DECLARE_WND_CLASS_EX(WC_GRAPHCONTROL, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

    BEGIN_MSG_MAP(CGraphControl)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MSG_WM_MOUSELEAVE(OnMouseLeave)
        MSG_WM_LBUTTONDOWN(OnLButtonDown)
        MSG_WM_KEYDOWN(OnKeyDown)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrintClient)
        MESSAGE_HANDLER(WM_GRAPHPOSTSAMPLE, OnPostedSamples)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
    END_MSG_MAP()

    // Creation convenience overload; dwStyle is a normal window style, ctrlStyle
    // combines GCS_* flags.
    HWND Create(HWND hWndParent, int x, int y, int w, int h,
                DWORD dwStyle = WS_CHILD | WS_VISIBLE, DWORD ctrlStyle = GCS_DEFAULT,
                UINT nID = 0);

    // ---- Series and samples ----
    SeriesId AddSeries(std::wstring name, const SeriesStyle& style = {});
    bool     RemoveSeries(SeriesId id);
    void     SetSeriesStyle(SeriesId id, const SeriesStyle& style);
    void     SetSeriesVisible(SeriesId id, bool visible);

    void PushSample(SeriesId id, float value);
    void PushSamples(const float* values, size_t count);
    void PushSamples(const std::vector<float>& values);

    // Safe from any thread: the values are copied and handed to the UI thread,
    // which does the actual push. Returns false if the post could not be made
    // (no window yet, or the queue refused it). The control must already exist.
    bool PostSample(SeriesId id, float value);
    bool PostSamples(const float* values, size_t count);

    void Clear();                       // drops samples, keeps the series
    void SetHistoryLength(size_t samples);
    size_t GetHistoryLength() const;

    // How far back a sample actually is, from the recorded times rather than
    // from counting slots -- so a throttled timer or a suspend does not turn
    // into a graph that quietly claims less time passed than did. Falls back to
    // the nominal cadence before anything has been stamped.
    float GetSampleAgeSeconds(int sampleIndex) const;

    // Seconds the whole plot spans, by the same reckoning.
    float GetHistorySeconds() const;

    GraphData&       GetData()       { return m_Data; }
    const GraphData& GetData() const { return m_Data; }

    // ---- Scale ----
    void      SetRange(float minValue, float maxValue);
    AxisRange GetRange() const { return m_Range; }
    void      SetAutoScale(bool enable, float headroom = 1.1f);

    // Auto-scale eases the axis to its new maximum instead of snapping, and
    // waits for several consecutive samples before shrinking, so one spike
    // leaving the window does not make the whole plot jump. Growth is always
    // immediate -- data must never be clipped. Pass 0 for an instant change.
    void SetRangeAnimation(UINT milliseconds, int shrinkDelaySamples = 8);
    void      SetValueFormatter(ValueFormatFn formatter);
    void      SetGridDivisions(int columns, int rows);

    // ---- Reference lines ----
    // Horizontal markers at fixed values: thresholds, quotas, baselines.
    RefLineId AddReferenceLine(float value, COLORREF color = RGB(214, 94, 94),
                               std::wstring label = {}, bool dashed = true);
    bool      RemoveReferenceLine(RefLineId id);
    void      ClearReferenceLines();

    // ---- Current-value overlay ----
    // Which series the GCS_VALUEOVERLAY reading comes from. InvalidSeries (the
    // default) means the first visible series, or the sum when stacked.
    void SetValueOverlaySeries(SeriesId id);

    // ---- Control styles ----
    DWORD GetGraphStyle() const { return m_CtrlStyle; }
    void  SetGraphStyle(DWORD style);
    void  ModifyGraphStyle(DWORD remove, DWORD add);

    // ---- Live update ----
    // interval == 0 stops the timer; the host can still push samples itself.
    void SetUpdateInterval(UINT milliseconds);
    UINT GetUpdateInterval() const { return m_UpdateInterval; }
    void SetSampleSource(SampleSourceFn source);

    // The cadence samples are expected to arrive at. SetUpdateInterval sets it
    // too; a host driving its own timer sets it directly so the control can
    // still tell when intervals have been missed.
    void SetExpectedInterval(UINT milliseconds);
    UINT GetExpectedInterval() const { return m_ExpectedInterval; }
    void Pause();
    void Resume();
    bool IsPaused() const { return m_Paused; }

    // ---- Hover ----
    // Samples back from the newest (0 = newest); -1 when nothing is hovered.
    int GetHoverSample() const { return m_HoverIndex; }

    // A pinned crosshair stays put when the pointer moves or leaves, so a
    // reading can be studied without holding the mouse still. Clicking the
    // plot, or Escape, releases it.
    bool IsHoverPinned() const { return m_HoverPinned; }
    // showReadout == false marks the sample without the value box: what a
    // grid wants on the tiles the pointer is not actually over.
    void SetHoverSample(int sampleIndex, bool pin = true, bool showReadout = true);
    void ClearHoverSample();
    void ClearHoverPin();

    // ---- Image export ----
    // Both redraw the current frame offscreen, so what you get is what you see.
    bool SaveImage(const wchar_t* path);      // PNG
    bool CopyImageToClipboard();              // CF_DIB

    // ---- Appearance ----
    void SetTitle(std::wstring title);
    void SetTimeSpanText(std::wstring text);   // bottom-left caption, e.g. "60 seconds"
    void SetTheme(const GraphTheme& theme);
    const GraphTheme& GetTheme() const { return m_Theme; }

    // Type for the captions, title and current-value overlay. Sizes are in DIPs
    // and the header, footer and overlay bands re-measure to match, so a bigger
    // font gets more room rather than being clipped. A control with a custom
    // font keeps its own text formats; the defaults are shared between graphs.
    bool SetFont(const wchar_t* family, float labelSize = 11.0f,
                 float titleSize = 12.5f, float valueSize = 22.0f);
    void ResetFont();

    // ---- Painting ----
    void Refresh();          // immediate repaint without changing state
    void BeginUpdate();      // suppress repaints during bulk changes
    void EndUpdate();

private:
    // ---- Message handlers ----
    int  OnCreate(LPCREATESTRUCT pcs);
    void OnDestroy();
    void OnPaint(HDC dc);
    void OnSize(UINT nType, CSize size);
    void OnTimer(UINT_PTR nIDEvent);
    void OnMouseMove(UINT nFlags, CPoint pt);
    void OnMouseLeave();
    void OnLButtonDown(UINT nFlags, CPoint pt);
    void OnKeyDown(UINT vkey, UINT repeats, UINT flags);
    void OnContextMenu(CWindow wnd, CPoint pt);
    LRESULT OnGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPostedSamples(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- Internal helpers ----
    void RequestInvalidate();
    void FillMissedIntervals();      // keeps the time axis honest after a stall
    void OnSamplesPushed();          // advances the grid phase, applies auto-scale
    void ApplyAutoScale();
    void StartRangeAnimation(float target);
    void StepRangeAnimation();
    void NotifyRangeChanged();
    void UpdateValueText();
    void ClearHover();
    int  HitTestSample(CPoint pt) const;   // client pixels -> samples back from newest
    void BuildHoverText(int sampleIndex);
    RenderOptions MakeRenderOptions() const;

private:
    GraphData     m_Data;
    GraphRenderer m_Renderer;
    AxisRange     m_Range;
    GraphTheme    m_Theme = GraphTheme::Dark();
    ValueFormatFn m_Formatter;

    std::wstring m_Title;
    std::wstring m_TimeSpanText;
    std::wstring m_ValueText;

    std::vector<ReferenceLine> m_RefLines;
    RefLineId                  m_NextRefLineId = 0;
    SeriesId                   m_ValueSeries   = InvalidSeries;

    DWORD m_CtrlStyle = GCS_DEFAULT;
    int   m_GridCols  = 10;
    int   m_GridRows  = 5;

    // Auto-scale easing.
    float    m_RangeTarget     = 0.0f;
    float    m_RangeFrom       = 0.0f;
    uint64_t m_RangeAnimStart  = 0;
    UINT     m_RangeAnimMs     = 250;
    int      m_ShrinkDelay     = 8;
    int      m_ShrinkVotes     = 0;

    SampleSourceFn     m_SampleSource;
    std::vector<float> m_SampleScratch;
    UINT               m_UpdateInterval   = 0;
    UINT               m_ExpectedInterval = 0;
    bool               m_Paused         = false;
    uint64_t           m_TotalSamples   = 0;

    int          m_HoverIndex    = -1;
    CPoint       m_HoverPt       = {};
    bool         m_TrackingMouse = false;
    bool         m_HoverPinned   = false;
    std::wstring m_HoverText;

    int  m_UpdateDepth       = 0;
    bool m_PendingInvalidate = false;
};

} // namespace GraphCtrl
