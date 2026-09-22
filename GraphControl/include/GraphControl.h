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

namespace GraphCtrl {

struct GRAPHRANGENOTIFY {
    NMHDR Hdr;
    float Min;
    float Max;
};

// Register the window class. Call once at startup (or on DLL attach).
// Returns false if registration fails and the class does not already exist.
bool Register(HINSTANCE hInstance);

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_GRAPHCONTROL[] = L"GraphControl";

// Control styles, packed into the low-order style bits like LVS_*/ES_*.
#define GCS_GRID        0x0001   // background grid
#define GCS_SCROLLGRID  0x0002   // grid drifts left with the data
#define GCS_FILL        0x0004   // gradient area fill under each line
#define GCS_AUTOSCALE   0x0008   // recompute the Y maximum from the data
#define GCS_AXISLABELS  0x0010   // draw the range captions and time span

// Default look: Task Manager style, fixed 0..100 range.
#define GCS_DEFAULT  (GCS_GRID | GCS_FILL | GCS_AXISLABELS)

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

    void Clear();                       // drops samples, keeps the series
    void SetHistoryLength(size_t samples);
    size_t GetHistoryLength() const;

    GraphData&       GetData()       { return m_Data; }
    const GraphData& GetData() const { return m_Data; }

    // ---- Scale ----
    void      SetRange(float minValue, float maxValue);
    AxisRange GetRange() const { return m_Range; }
    void      SetAutoScale(bool enable, float headroom = 1.1f);
    void      SetValueFormatter(ValueFormatFn formatter);
    void      SetGridDivisions(int columns, int rows);

    // ---- Live update ----
    // interval == 0 stops the timer; the host can still push samples itself.
    void SetUpdateInterval(UINT milliseconds);
    UINT GetUpdateInterval() const { return m_UpdateInterval; }
    void SetSampleSource(SampleSourceFn source);
    void Pause();
    void Resume();
    bool IsPaused() const { return m_Paused; }

    // ---- Appearance ----
    void SetTitle(std::wstring title);
    void SetTimeSpanText(std::wstring text);   // bottom-left caption, e.g. "60 seconds"
    void SetTheme(const GraphTheme& theme);
    const GraphTheme& GetTheme() const { return m_Theme; }

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
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- Internal helpers ----
    void RequestInvalidate();
    void OnSamplesPushed();          // advances the grid phase, applies auto-scale
    void ApplyAutoScale();
    void NotifyRangeChanged();
    RenderOptions MakeRenderOptions() const;

private:
    GraphData     m_Data;
    GraphRenderer m_Renderer;
    AxisRange     m_Range;
    GraphTheme    m_Theme = GraphTheme::Dark();
    ValueFormatFn m_Formatter;

    std::wstring m_Title;
    std::wstring m_TimeSpanText;

    DWORD m_CtrlStyle = GCS_DEFAULT;
    int   m_GridCols  = 10;
    int   m_GridRows  = 5;

    SampleSourceFn     m_SampleSource;
    std::vector<float> m_SampleScratch;
    UINT               m_UpdateInterval = 0;
    bool               m_Paused         = false;
    uint64_t           m_TotalSamples   = 0;

    int  m_UpdateDepth       = 0;
    bool m_PendingInvalidate = false;
};

} // namespace GraphCtrl
