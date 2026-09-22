#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

#include "GraphControl.h"

namespace GraphCtrl {

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_GRAPHGRID[] = L"GraphGrid";

// A tiled set of CGraphControls sharing one range, history and time base --
// the Task Manager "logical processors" view. Every tile holds exactly one
// series, and one PushSamples call advances all of them together.
//
// Notifications from the tiles (GCN_*) are forwarded to this window's parent
// unchanged, so hosts see them with the tile's own control id.
class CGraphGrid : public CWindowImpl<CGraphGrid> {
public:
    DECLARE_WND_CLASS_EX(WC_GRAPHGRID, CS_HREDRAW | CS_VREDRAW, -1)

    BEGIN_MSG_MAP(CGraphGrid)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_TIMER(OnTimer)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(WM_NOTIFY, OnChildNotify)
    END_MSG_MAP()

    HWND Create(HWND hWndParent, int x, int y, int w, int h,
                DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                DWORD childStyle = GCS_GRID | GCS_FILL,
                UINT nID = 0, UINT childIdBase = 0);

    // ---- Tiles ----
    void   SetGraphCount(size_t count);
    size_t GetGraphCount() const { return m_Graphs.size(); }
    CGraphControl* GetGraph(size_t index);

    void SetLayout(int columns, int rows);   // 0, 0 = auto (roughly square)
    void SetSpacing(int pixels);

    // ---- Applied to every tile ----
    void SetRange(float minValue, float maxValue);
    void SetAutoScale(bool enable, float headroom = 1.1f);
    void SetHistoryLength(size_t samples);
    void SetTheme(const GraphTheme& theme);
    void SetChildStyle(DWORD style);
    DWORD GetChildStyle() const { return m_ChildStyle; }
    void SetValueFormatter(ValueFormatFn formatter);
    void SetSeriesStyle(const SeriesStyle& style);
    void SetTimeSpanText(std::wstring text);
    void SetFont(const wchar_t* family, float labelSize = 11.0f,
                 float titleSize = 12.5f, float valueSize = 22.0f);
    void SetRangeAnimation(UINT milliseconds, int shrinkDelaySamples = 8);

    // Tile titles become "<prefix> <index>"; an empty prefix leaves just the index.
    void SetTitlePrefix(std::wstring prefix);

    // ---- Samples ----
    // One value per tile, in index order. Tiles past `count` get a missing
    // sample rather than a zero, so a short read leaves a gap instead of a dive.
    void PushSamples(const float* values, size_t count);
    void PushSamples(const std::vector<float>& values);
    void Clear();

    // ---- Live update ----
    void SetUpdateInterval(UINT milliseconds);
    UINT GetUpdateInterval() const { return m_UpdateInterval; }
    void SetSampleSource(SampleSourceFn source);   // fills one value per tile
    void Pause();
    void Resume();
    bool IsPaused() const { return m_Paused; }

private:
    int  OnCreate(LPCREATESTRUCT pcs);
    void OnDestroy();
    void OnPaint(HDC dc);
    void OnSize(UINT nType, CSize size);
    void OnTimer(UINT_PTR nIDEvent);
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    void CreateChildWindow(size_t index);
    void ConfigureChild(size_t index);
    void Layout();

private:
    std::vector<std::unique_ptr<CGraphControl>> m_Graphs;
    std::vector<SeriesId>                       m_SeriesIds;

    DWORD m_ChildStyle  = GCS_GRID | GCS_FILL;
    UINT  m_ChildIdBase = 0;
    int   m_Cols        = 0;    // 0 = auto
    int   m_Rows        = 0;
    int   m_Spacing     = 4;

    AxisRange     m_Range;
    GraphTheme    m_Theme = GraphTheme::Dark();
    SeriesStyle   m_SeriesStyle;
    ValueFormatFn m_Formatter;
    std::wstring  m_FontFamily;
    float         m_FontLabel = 11.0f;
    float         m_FontTitle = 12.5f;
    float         m_FontValue = 22.0f;
    UINT          m_RangeAnimMs = 250;
    int           m_ShrinkDelay = 8;
    std::wstring  m_TitlePrefix;
    std::wstring  m_TimeSpanText;
    size_t        m_History = k_DefaultCapacity;

    SampleSourceFn     m_SampleSource;
    std::vector<float> m_SampleScratch;
    UINT               m_UpdateInterval = 0;
    bool               m_Paused         = false;
};

} // namespace GraphCtrl
