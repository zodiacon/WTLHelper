// GraphControl.cpp : a Task Manager style performance graph, drawn with Direct2D.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/GraphControl.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

namespace GraphCtrl {

constexpr UINT_PTR k_UpdateTimerId = 1;

// ---- CGraphControl : message handlers ---------------------------------------

int CGraphControl::OnCreate(LPCREATESTRUCT pcs) {
    m_CtrlStyle = pcs->style & 0xFFFF;
    m_Range.AutoScale = (m_CtrlStyle & GCS_AUTOSCALE) != 0;

    if (FAILED(m_Renderer.Init())) return -1;

    const UINT dpi = ::GetDpiForWindow(m_hWnd);
    m_Renderer.SetDpi(dpi ? static_cast<float>(dpi) : 96.0f);

    if (m_UpdateInterval && !m_Paused)
        SetTimer(k_UpdateTimerId, m_UpdateInterval);

    return 0;
}

void CGraphControl::OnDestroy() {
    KillTimer(k_UpdateTimerId);
    m_Renderer.Shutdown();
}

void CGraphControl::OnPaint(HDC) {
    CPaintDC dc(m_hWnd);   // validates the update region; D2D presents on its own
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    m_Renderer.Render(m_hWnd, rc, m_Data, m_Range, m_Theme,
                      MakeRenderOptions(), m_Formatter);
}

void CGraphControl::OnSize(UINT, CSize size) {
    m_Renderer.Resize(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy));
}

void CGraphControl::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != k_UpdateTimerId || m_Paused || !m_SampleSource) return;

    m_SampleScratch.assign(m_Data.SeriesCount(), 0.0f);
    m_SampleSource(m_SampleScratch);
    m_Data.PushSamples(m_SampleScratch.data(), m_SampleScratch.size());
    OnSamplesPushed();
}

BOOL CGraphControl::OnEraseBkgnd(CDCHandle) {
    return TRUE;   // prevent flicker; the renderer fills the background
}

LRESULT CGraphControl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    const UINT dpi = ::GetDpiForWindow(m_hWnd);
    m_Renderer.SetDpi(dpi ? static_cast<float>(dpi) : 96.0f);
    RequestInvalidate();
    bHandled = FALSE;   // let the default handler resize the window
    return 0;
}

// ---- Creation ----------------------------------------------------------------

HWND CGraphControl::Create(HWND hWndParent, int x, int y, int w, int h,
                           DWORD dwStyle, DWORD ctrlStyle, UINT nID) {
    CRect rc(x, y, x + w, y + h);
    return CWindowImpl<CGraphControl>::Create(hWndParent, rc, nullptr,
                                              dwStyle | ctrlStyle, 0, nID);
}

// ---- Series and samples ------------------------------------------------------

SeriesId CGraphControl::AddSeries(std::wstring name, const SeriesStyle& style) {
    const SeriesId id = m_Data.AddSeries(std::move(name), style);
    RequestInvalidate();
    return id;
}

bool CGraphControl::RemoveSeries(SeriesId id) {
    const bool removed = m_Data.RemoveSeries(id);
    if (removed) RequestInvalidate();
    return removed;
}

void CGraphControl::SetSeriesStyle(SeriesId id, const SeriesStyle& style) {
    if (Series* s = m_Data.GetSeries(id)) {
        s->SetStyle(style);
        RequestInvalidate();
    }
}

void CGraphControl::SetSeriesVisible(SeriesId id, bool visible) {
    if (Series* s = m_Data.GetSeries(id)) {
        SeriesStyle style = s->Style();
        style.Visible = visible;
        s->SetStyle(style);
        RequestInvalidate();
    }
}

// Note: on a multi-series graph, prefer PushSamples so every series advances
// together. Each push here also counts as one step of the shared time base.
void CGraphControl::PushSample(SeriesId id, float value) {
    m_Data.PushSample(id, value);
    OnSamplesPushed();
}

void CGraphControl::PushSamples(const float* values, size_t count) {
    m_Data.PushSamples(values, count);
    OnSamplesPushed();
}

void CGraphControl::PushSamples(const std::vector<float>& values) {
    PushSamples(values.data(), values.size());
}

void CGraphControl::Clear() {
    m_Data.Clear();
    m_TotalSamples = 0;
    RequestInvalidate();
}

void CGraphControl::SetHistoryLength(size_t samples) {
    m_Data.SetCapacity(samples);
    RequestInvalidate();
}

size_t CGraphControl::GetHistoryLength() const {
    return m_Data.Capacity();
}

// ---- Scale -------------------------------------------------------------------

void CGraphControl::SetRange(float minValue, float maxValue) {
    if (maxValue <= minValue) maxValue = minValue + 1.0f;
    m_Range.Min = minValue;
    m_Range.Max = maxValue;
    RequestInvalidate();
}

void CGraphControl::SetAutoScale(bool enable, float headroom) {
    m_Range.AutoScale = enable;
    m_Range.Headroom  = headroom > 0.0f ? headroom : 1.0f;
    if (enable) {
        m_CtrlStyle |= GCS_AUTOSCALE;
        ApplyAutoScale();
    }
    else {
        m_CtrlStyle &= ~GCS_AUTOSCALE;
    }
    RequestInvalidate();
}

void CGraphControl::SetValueFormatter(ValueFormatFn formatter) {
    m_Formatter = std::move(formatter);
    RequestInvalidate();
}

void CGraphControl::SetGridDivisions(int columns, int rows) {
    m_GridCols = std::max(columns, 0);
    m_GridRows = std::max(rows, 0);
    RequestInvalidate();
}

// ---- Live update -------------------------------------------------------------

void CGraphControl::SetUpdateInterval(UINT milliseconds) {
    m_UpdateInterval = milliseconds;
    if (!m_hWnd) return;

    KillTimer(k_UpdateTimerId);
    if (m_UpdateInterval && !m_Paused)
        SetTimer(k_UpdateTimerId, m_UpdateInterval);
}

void CGraphControl::SetSampleSource(SampleSourceFn source) {
    m_SampleSource = std::move(source);
}

void CGraphControl::Pause() {
    if (m_Paused) return;
    m_Paused = true;
    if (m_hWnd) KillTimer(k_UpdateTimerId);
}

void CGraphControl::Resume() {
    if (!m_Paused) return;
    m_Paused = false;
    if (m_hWnd && m_UpdateInterval)
        SetTimer(k_UpdateTimerId, m_UpdateInterval);
}

// ---- Appearance --------------------------------------------------------------

void CGraphControl::SetTitle(std::wstring title) {
    m_Title = std::move(title);
    RequestInvalidate();
}

void CGraphControl::SetTimeSpanText(std::wstring text) {
    m_TimeSpanText = std::move(text);
    RequestInvalidate();
}

void CGraphControl::SetTheme(const GraphTheme& theme) {
    m_Theme = theme;
    RequestInvalidate();
}

// ---- Painting ----------------------------------------------------------------

void CGraphControl::Refresh() {
    if (!m_hWnd) return;
    Invalidate(FALSE);
    UpdateWindow();
}

void CGraphControl::BeginUpdate() {
    m_UpdateDepth++;
}

void CGraphControl::EndUpdate() {
    if (m_UpdateDepth > 0 && --m_UpdateDepth == 0 && m_PendingInvalidate) {
        m_PendingInvalidate = false;
        if (m_hWnd) Invalidate(FALSE);
    }
}

// ---- Internal helpers --------------------------------------------------------

void CGraphControl::RequestInvalidate() {
    if (m_UpdateDepth > 0) {
        m_PendingInvalidate = true;
        return;
    }
    if (m_hWnd) Invalidate(FALSE);
}

void CGraphControl::OnSamplesPushed() {
    m_TotalSamples++;
    ApplyAutoScale();
    RequestInvalidate();
}

void CGraphControl::ApplyAutoScale() {
    if (!m_Range.AutoScale) return;

    float peak = m_Data.MaxValue(0.0f) * m_Range.Headroom;
    if (m_Range.RoundNice) peak = NiceCeil(peak);
    if (peak <= m_Range.Min) peak = m_Range.Min + 1.0f;

    if (peak != m_Range.Max) {
        m_Range.Max = peak;
        NotifyRangeChanged();
    }
}

void CGraphControl::NotifyRangeChanged() {
    HWND parent = GetParent();
    if (!parent) return;

    GRAPHRANGENOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = GCN_RANGECHANGED;
    nmh.Min          = m_Range.Min;
    nmh.Max          = m_Range.Max;
    ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));
}

RenderOptions CGraphControl::MakeRenderOptions() const {
    RenderOptions opt;
    opt.DrawGrid   = (m_CtrlStyle & GCS_GRID) != 0;
    opt.ScrollGrid = (m_CtrlStyle & GCS_SCROLLGRID) != 0;
    opt.Fill       = (m_CtrlStyle & GCS_FILL) != 0;
    opt.AxisLabels = (m_CtrlStyle & GCS_AXISLABELS) != 0;
    opt.GridCols   = m_GridCols;
    opt.GridRows   = m_GridRows;

    // One grid cell spans capacity/columns samples; scroll by whole samples so
    // the grid drifts left at exactly the speed of the data.
    if (opt.ScrollGrid && m_GridCols > 0) {
        const float perCell = static_cast<float>(m_Data.Capacity()) / m_GridCols;
        if (perCell >= 1.0f)
            opt.GridPhase = std::fmod(static_cast<float>(m_TotalSamples), perCell) / perCell;
    }

    opt.Title        = m_Title.empty() ? nullptr : m_Title.c_str();
    opt.TimeSpanText = m_TimeSpanText.empty() ? nullptr : m_TimeSpanText.c_str();
    return opt;
}

// ---- Class registration ------------------------------------------------------

bool Register(HINSTANCE /*hInstance*/) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    // Pre-register the window class; CWindowImpl::Create() also does this lazily.
    ATOM a = CGraphControl::GetWndClassInfo().Register(nullptr);
    return a != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace GraphCtrl
