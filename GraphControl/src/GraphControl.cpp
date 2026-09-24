// GraphControl.cpp : a Task Manager style performance graph, drawn with Direct2D.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/GraphControl.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <memory>

#pragma comment(lib, "comctl32.lib")

namespace GraphCtrl {

// What a cross-thread PostSample/PostSamples hands to the UI thread.
struct PostedSamples {
    SeriesId           Id = InvalidSeries;   // InvalidSeries => all series
    std::vector<float> Values;
};

constexpr UINT_PTR k_UpdateTimerId = 1;
constexpr UINT_PTR k_RangeTimerId  = 2;
constexpr UINT     k_RangeFrameMs  = 16;   // ~60 fps while the axis is easing

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
    KillTimer(k_RangeTimerId);
    m_Renderer.Shutdown();
}

void CGraphControl::OnPaint(HDC) {
    CPaintDC dc(m_hWnd);   // validates the update region; D2D presents on its own
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    UpdateValueText();
    m_Renderer.Render(m_hWnd, rc, m_Data, m_Range, m_Theme,
                      MakeRenderOptions(), m_Formatter);
}

void CGraphControl::OnSize(UINT, CSize size) {
    m_Renderer.Resize(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy));
}

void CGraphControl::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == k_RangeTimerId) {
        StepRangeAnimation();
        return;
    }
    if (nIDEvent != k_UpdateTimerId || m_Paused || !m_SampleSource) return;

    m_SampleScratch.assign(m_Data.SeriesCount(), MissingSample);
    m_SampleSource(m_SampleScratch);
    PushSamples(m_SampleScratch.data(), m_SampleScratch.size());
}

void CGraphControl::OnMouseMove(UINT, CPoint pt) {
    if (!(m_CtrlStyle & GCS_TOOLTIP)) return;

    if (!m_TrackingMouse) {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
    }

    if (m_HoverPinned) return;   // the crosshair is parked until released

    const int index = HitTestSample(pt);
    if (index < 0) {
        ClearHover();
        return;
    }

    const bool indexChanged = (index != m_HoverIndex);
    m_HoverIndex = index;
    m_HoverPt    = pt;

    if (indexChanged) {
        GRAPHHOVERNOTIFY nmh{};
        nmh.Hdr.hwndFrom = m_hWnd;
        nmh.Hdr.idFrom   = GetDlgCtrlID();
        nmh.Hdr.code     = GCN_HOVERSAMPLE;
        nmh.SampleIndex  = index;
        if (HWND parent = GetParent())
            ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));

        BuildHoverText(index);
    }

    // Repaint on every move: the readout box follows the cursor.
    RequestInvalidate();
}

void CGraphControl::OnMouseLeave() {
    m_TrackingMouse = false;
    if (!m_HoverPinned)
        ClearHover();
}

void CGraphControl::OnLButtonDown(UINT, CPoint pt) {
    SetFocus();          // so the arrow keys reach OnKeyDown
    if (!(m_CtrlStyle & GCS_TOOLTIP)) return;

    if (m_HoverPinned) {
        ClearHoverPin();
        return;
    }

    const int index = HitTestSample(pt);
    if (index < 0) return;

    m_HoverPt = pt;
    SetHoverSample(index, true);
}

void CGraphControl::OnKeyDown(UINT vkey, UINT, UINT) {
    if (!(m_CtrlStyle & GCS_TOOLTIP)) return;

    // The newest sample a visible series holds; nothing is drawn past it.
    size_t newest = 0;
    for (const auto& s : m_Data.AllSeries()) {
        if (s.Style().Visible)
            newest = std::max(newest, s.Count());
    }
    if (newest == 0) return;
    const int last = static_cast<int>(newest) - 1;

    switch (vkey) {
    case VK_ESCAPE:
        ClearHoverPin();
        ClearHover();
        break;
    case VK_LEFT:    // further back in time
        SetHoverSample(std::min(m_HoverIndex < 0 ? 0 : m_HoverIndex + 1, last), true);
        break;
    case VK_RIGHT:
        SetHoverSample(std::max(m_HoverIndex < 0 ? last : m_HoverIndex - 1, 0), true);
        break;
    case VK_HOME:    // oldest retained sample
        SetHoverSample(last, true);
        break;
    case VK_END:     // newest
        SetHoverSample(0, true);
        break;
    default:
        break;
    }
}

void CGraphControl::OnContextMenu(CWindow, CPoint pt) {
    HWND parent = GetParent();
    if (!parent) return;

    // Keyboard-invoked menus arrive at (-1, -1); put those over the plot.
    CPoint screen = pt;
    if (screen.x == -1 && screen.y == -1) {
        CRect rc;
        GetClientRect(&rc);
        screen = rc.CenterPoint();
        ClientToScreen(&screen);
    }

    CPoint client = screen;
    ScreenToClient(&client);

    GRAPHCONTEXTNOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = GCN_CONTEXTMENU;
    nmh.Pt           = screen;
    nmh.SampleIndex  = HitTestSample(client);
    ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));
}

LRESULT CGraphControl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
    // Arrow keys step the crosshair rather than moving focus in a dialog.
    return DLGC_WANTARROWS;
}

LRESULT CGraphControl::OnPrintClient(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    CRect rc;
    GetClientRect(&rc);
    if (!dc || rc.IsRectEmpty()) {
        bHandled = FALSE;
        return 0;
    }

    UpdateValueText();
    m_Renderer.RenderToDC(dc, rc, m_Data, m_Range, m_Theme,
                          MakeRenderOptions(), m_Formatter);
    return 0;
}

// Takes ownership of the payload posted by PostSample/PostSamples.
LRESULT CGraphControl::OnPostedSamples(UINT, WPARAM, LPARAM lParam, BOOL&) {
    std::unique_ptr<PostedSamples> posted(reinterpret_cast<PostedSamples*>(lParam));
    if (!posted) return 0;

    if (posted->Id == InvalidSeries)
        PushSamples(posted->Values.data(), posted->Values.size());
    else if (!posted->Values.empty())
        PushSample(posted->Id, posted->Values.front());

    return 0;
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
    if (removed) {
        ClearHover();  // the cached readout still names the removed series
        RequestInvalidate();
    }
    return removed;
}

void CGraphControl::SetSeriesStyle(SeriesId id, const SeriesStyle& style) {
    if (Series* s = m_Data.GetSeries(id)) {
        s->SetStyle(style);
        m_Data.Touch();          // the geometry cache keys off this
        RequestInvalidate();
    }
}

void CGraphControl::SetSeriesVisible(SeriesId id, bool visible) {
    if (Series* s = m_Data.GetSeries(id)) {
        SeriesStyle style = s->Style();
        style.Visible = visible;
        s->SetStyle(style);
        m_Data.Touch();
        RequestInvalidate();
    }
}

// Note: on a multi-series graph, prefer PushSamples so every series advances
// together. Each push here also counts as one step of the shared time base.
void CGraphControl::PushSample(SeriesId id, float value) {
    FillMissedIntervals();
    m_Data.PushSample(id, value);
    OnSamplesPushed();
}

void CGraphControl::PushSamples(const float* values, size_t count) {
    FillMissedIntervals();
    m_Data.PushSamples(values, count);
    OnSamplesPushed();
}

void CGraphControl::PushSamples(const std::vector<float>& values) {
    PushSamples(values.data(), values.size());
}

bool CGraphControl::PostSample(SeriesId id, float value) {
    if (!m_hWnd) return false;

    auto posted = std::make_unique<PostedSamples>();
    posted->Id = id;
    posted->Values.assign(1, value);

    if (!::PostMessage(m_hWnd, WM_GRAPHPOSTSAMPLE, 0,
                       reinterpret_cast<LPARAM>(posted.get())))
        return false;                    // the payload is still ours

    posted.release();                    // the handler frees it
    return true;
}

bool CGraphControl::PostSamples(const float* values, size_t count) {
    if (!m_hWnd) return false;

    auto posted = std::make_unique<PostedSamples>();
    posted->Id = InvalidSeries;
    if (values && count)
        posted->Values.assign(values, values + count);

    if (!::PostMessage(m_hWnd, WM_GRAPHPOSTSAMPLE, 0,
                       reinterpret_cast<LPARAM>(posted.get())))
        return false;

    posted.release();
    return true;
}

void CGraphControl::Clear() {
    m_Data.Clear();
    m_TotalSamples = 0;
    ClearHover();
    RequestInvalidate();
}

void CGraphControl::SetHistoryLength(size_t samples) {
    m_Data.SetCapacity(samples);
    ClearHover();      // the hovered index may now point past the data
    RequestInvalidate();
}

size_t CGraphControl::GetHistoryLength() const {
    return m_Data.Capacity();
}

float CGraphControl::GetSampleAgeSeconds(int sampleIndex) const {
    if (sampleIndex <= 0) return 0.0f;

    const uint64_t newest = m_Data.NewestTimestamp();
    const uint64_t at     = m_Data.TimestampFromEnd(static_cast<size_t>(sampleIndex));
    if (newest && at && newest >= at)
        return static_cast<float>(newest - at) / 1000.0f;

    // Nothing stamped that far back yet: fall back to the nominal cadence.
    return m_ExpectedInterval ? sampleIndex * m_ExpectedInterval / 1000.0f : 0.0f;
}

float CGraphControl::GetHistorySeconds() const {
    const uint64_t newest = m_Data.NewestTimestamp();
    const uint64_t oldest = m_Data.OldestTimestamp();
    if (newest && oldest && newest > oldest)
        return static_cast<float>(newest - oldest) / 1000.0f;

    return m_ExpectedInterval ? m_Data.Capacity() * m_ExpectedInterval / 1000.0f : 0.0f;
}

// If whole update intervals went by without a push -- a throttled timer, a busy
// UI thread, a suspended machine -- insert a missing sample for each one. The
// plot is indexed by slot, so without this the axis silently compresses and the
// time-span caption becomes a lie. The fill is capped at the history length: a
// long stall simply blanks the graph, which is the truth.
void CGraphControl::FillMissedIntervals() {
    if (m_ExpectedInterval == 0) return;

    const uint64_t last = m_Data.NewestTimestamp();
    if (last == 0) return;

    const uint64_t now = GetTickCount64();
    if (now <= last) return;

    const uint64_t missed = (now - last) / m_ExpectedInterval;
    if (missed < 2) return;

    const size_t fill = static_cast<size_t>(
        std::min<uint64_t>(missed - 1, m_Data.Capacity()));

    for (size_t i = 0; i < fill; i++)
        m_Data.PushSamplesAt(nullptr, 0, last + (i + 1) * m_ExpectedInterval);

    m_TotalSamples += fill;   // the grid scrolls by the time that really passed
}

// ---- Scale -------------------------------------------------------------------

void CGraphControl::SetRange(float minValue, float maxValue) {
    if (maxValue <= minValue) maxValue = minValue + 1.0f;

    if (m_hWnd) KillTimer(k_RangeTimerId);
    m_Range.Min   = minValue;
    m_Range.Max   = maxValue;
    m_RangeTarget = maxValue;
    m_ShrinkVotes = 0;
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
        if (m_hWnd) KillTimer(k_RangeTimerId);
        m_ShrinkVotes = 0;
    }
    RequestInvalidate();
}

void CGraphControl::SetRangeAnimation(UINT milliseconds, int shrinkDelaySamples) {
    m_RangeAnimMs = milliseconds;
    m_ShrinkDelay = shrinkDelaySamples < 0 ? 0 : shrinkDelaySamples;
    if (m_RangeAnimMs == 0 && m_hWnd) {
        KillTimer(k_RangeTimerId);
        if (m_RangeTarget > 0.0f) m_Range.Max = m_RangeTarget;
    }
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

// ---- Reference lines ---------------------------------------------------------

RefLineId CGraphControl::AddReferenceLine(float value, COLORREF color,
                                          std::wstring label, bool dashed) {
    ReferenceLine line;
    line.Id     = m_NextRefLineId++;
    line.Value  = value;
    line.Color  = color;
    line.Dashed = dashed;
    line.Label  = std::move(label);

    m_RefLines.push_back(std::move(line));
    RequestInvalidate();
    return m_RefLines.back().Id;
}

bool CGraphControl::RemoveReferenceLine(RefLineId id) {
    auto it = std::find_if(m_RefLines.begin(), m_RefLines.end(),
                           [id](const ReferenceLine& r) { return r.Id == id; });
    if (it == m_RefLines.end()) return false;

    m_RefLines.erase(it);
    RequestInvalidate();
    return true;
}

void CGraphControl::ClearReferenceLines() {
    if (m_RefLines.empty()) return;
    m_RefLines.clear();
    RequestInvalidate();
}

// ---- Current-value overlay ---------------------------------------------------

void CGraphControl::SetValueOverlaySeries(SeriesId id) {
    m_ValueSeries = id;
    RequestInvalidate();
}

void CGraphControl::UpdateValueText() {
    m_ValueText.clear();
    if (!(m_CtrlStyle & GCS_VALUEOVERLAY)) return;

    float value = 0.0f;
    bool  have  = false;

    if (m_CtrlStyle & GCS_STACKED) {
        // The headline number for a stacked graph is the total.
        for (const auto& s : m_Data.AllSeries()) {
            if (!s.Style().Visible) continue;
            const float v = s.Last();
            if (IsMissing(v)) continue;
            value += v;
            have = true;
        }
    }
    else if (m_ValueSeries != InvalidSeries) {
        const Series* s = m_Data.GetSeries(m_ValueSeries);
        if (s && s->Style().Visible && !IsMissing(s->Last())) {
            value = s->Last();
            have  = true;
        }
    }
    else {
        for (const auto& s : m_Data.AllSeries()) {
            if (!s.Style().Visible) continue;
            const float v = s.Last();
            if (!IsMissing(v)) {
                value = v;
                have  = true;
            }
            break;   // first visible series only
        }
    }

    if (!have) {
        m_ValueText = L"--";
        return;
    }

    wchar_t buffer[64] = {};
    if (m_Formatter) m_Formatter(value, buffer, _countof(buffer));
    else             Format::Number(value, buffer, _countof(buffer));
    m_ValueText = buffer;
}

// ---- Control styles ----------------------------------------------------------

void CGraphControl::SetGraphStyle(DWORD style) {
    m_CtrlStyle = style & 0xFFFF;
    m_Range.AutoScale = (m_CtrlStyle & GCS_AUTOSCALE) != 0;

    if (!(m_CtrlStyle & GCS_TOOLTIP))
        ClearHover();
    if (m_Range.AutoScale)
        ApplyAutoScale();

    RequestInvalidate();
}

void CGraphControl::ModifyGraphStyle(DWORD remove, DWORD add) {
    SetGraphStyle((m_CtrlStyle & ~remove) | add);
}

// ---- Live update -------------------------------------------------------------

void CGraphControl::SetUpdateInterval(UINT milliseconds) {
    m_UpdateInterval = milliseconds;
    if (milliseconds) m_ExpectedInterval = milliseconds;
    if (!m_hWnd) return;

    KillTimer(k_UpdateTimerId);
    if (m_UpdateInterval && !m_Paused)
        SetTimer(k_UpdateTimerId, m_UpdateInterval);
}

void CGraphControl::SetExpectedInterval(UINT milliseconds) {
    m_ExpectedInterval = milliseconds;
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

bool CGraphControl::SetFont(const wchar_t* family, float labelSize,
                            float titleSize, float valueSize) {
    const bool ok = SUCCEEDED(m_Renderer.SetFont(family, labelSize, titleSize, valueSize));
    if (ok) {
        ClearHover();    // the plot box moved, so the hit test answer changed
        RequestInvalidate();
    }
    return ok;
}

void CGraphControl::ResetFont() {
    m_Renderer.ResetFont();
    ClearHover();
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

void CGraphControl::SetHoverSample(int sampleIndex, bool pin, bool showReadout) {
    if (sampleIndex < 0) {
        ClearHoverPin();
        ClearHover();
        return;
    }

    const bool changed = (sampleIndex != m_HoverIndex);
    m_HoverIndex  = sampleIndex;
    m_HoverPinned = pin;

    if (changed) {
        GRAPHHOVERNOTIFY nmh{};
        nmh.Hdr.hwndFrom = m_hWnd;
        nmh.Hdr.idFrom   = GetDlgCtrlID();
        nmh.Hdr.code     = GCN_HOVERSAMPLE;
        nmh.SampleIndex  = sampleIndex;
        if (HWND parent = GetParent())
            ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));

        if (showReadout) BuildHoverText(sampleIndex);
        else             m_HoverText.clear();
    }
    RequestInvalidate();
}

void CGraphControl::ClearHoverSample() {
    ClearHover();
}

void CGraphControl::ClearHoverPin() {
    if (!m_HoverPinned) return;
    m_HoverPinned = false;
    RequestInvalidate();
}

bool CGraphControl::SaveImage(const wchar_t* path) {
    if (!m_hWnd) return false;
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return false;

    UpdateValueText();
    return m_Renderer.SaveImage(path, rc, m_Data, m_Range, m_Theme,
                                MakeRenderOptions(), m_Formatter);
}

bool CGraphControl::CopyImageToClipboard() {
    if (!m_hWnd) return false;
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return false;

    UpdateValueText();
    return m_Renderer.CopyToClipboard(m_hWnd, rc, m_Data, m_Range, m_Theme,
                                      MakeRenderOptions(), m_Formatter);
}

void CGraphControl::ClearHover() {
    m_HoverPinned = false;
    if (m_HoverIndex < 0) return;

    m_HoverIndex = -1;
    m_HoverText.clear();

    GRAPHHOVERNOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = GCN_HOVERSAMPLE;
    nmh.SampleIndex  = -1;
    if (HWND parent = GetParent())
        ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));

    RequestInvalidate();
}

int CGraphControl::HitTestSample(CPoint pt) const {
    const size_t capacity = m_Data.Capacity();
    if (capacity < 2) return -1;

    // The newest sample a visible series holds; nothing is drawn past it.
    size_t newest = 0;
    for (const auto& s : m_Data.AllSeries()) {
        if (s.Style().Visible)
            newest = std::max(newest, s.Count());
    }
    if (newest == 0) return -1;

    CRect rc;
    const_cast<CGraphControl*>(this)->GetClientRect(&rc);

    const RenderOptions opt  = MakeRenderOptions();
    const D2D1_RECT_F   plot = m_Renderer.PlotRect(rc, opt);
    const float         dip  = 96.0f / m_Renderer.GetDpi();
    const float         x    = pt.x * dip;
    const float         y    = pt.y * dip;

    if (x < plot.left || x > plot.right || y < plot.top || y > plot.bottom) return -1;

    const float dx = GraphRenderer::SampleSpacing(plot, capacity);
    if (dx <= 0.0f) return -1;

    const int k = static_cast<int>(std::lround((plot.right - x) / dx));
    if (k < 0 || static_cast<size_t>(k) >= newest) return -1;
    return k;
}

void CGraphControl::BuildHoverText(int sampleIndex) {
    const size_t k = static_cast<size_t>(sampleIndex);

    m_HoverText.clear();
    for (const auto& s : m_Data.AllSeries()) {
        if (!s.Style().Visible || k >= s.Count()) continue;

        const float sample = s.AtFromEnd(k);

        wchar_t value[64] = {};
        if (IsMissing(sample))     wcscpy_s(value, L"(no data)");
        else if (m_Formatter)      m_Formatter(sample, value, _countof(value));
        else                       Format::Number(sample, value, _countof(value));

        if (!m_HoverText.empty()) m_HoverText += L'\n';
        if (!s.Name().empty()) {
            m_HoverText += s.Name();
            m_HoverText += L": ";
        }
        m_HoverText += value;
    }

    // Let the host replace the text entirely.
    GRAPHTOOLTIPNOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = GCN_GETTOOLTIP;
    nmh.SampleIndex  = sampleIndex;
    wcsncpy_s(nmh.SzText, m_HoverText.c_str(), _TRUNCATE);

    if (HWND parent = GetParent()) {
        ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));
        m_HoverText = nmh.SzText;
    }
}

void CGraphControl::ApplyAutoScale() {
    if (!m_Range.AutoScale) return;

    // A stacked graph has to fit the sum of the series, not the tallest one.
    const float observed = (m_CtrlStyle & GCS_STACKED) ? m_Data.MaxStackedValue(0.0f)
                                                       : m_Data.MaxValue(0.0f);
    float peak = observed * m_Range.Headroom;
    if (m_Range.RoundNice) peak = NiceCeil(peak);
    if (peak <= m_Range.Min) peak = m_Range.Min + 1.0f;

    if (m_RangeTarget <= 0.0f) m_RangeTarget = m_Range.Max;
    if (peak == m_RangeTarget) {
        m_ShrinkVotes = 0;
        return;
    }

    if (peak > m_RangeTarget) {
        // Never clip: grow at once.
        m_ShrinkVotes = 0;
        StartRangeAnimation(peak);
        return;
    }

    // Shrinking waits for the smaller peak to hold, so a spike dropping out of
    // the window does not yank the axis down on the very next sample.
    if (++m_ShrinkVotes >= m_ShrinkDelay) {
        m_ShrinkVotes = 0;
        StartRangeAnimation(peak);
    }
}

void CGraphControl::StartRangeAnimation(float target) {
    m_RangeTarget = target;
    NotifyRangeChanged();    // the decision is the event, not each eased frame

    if (m_RangeAnimMs == 0 || !m_hWnd) {
        m_Range.Max = target;
        RequestInvalidate();
        return;
    }

    m_RangeFrom      = m_Range.Max;
    m_RangeAnimStart = GetTickCount64();
    SetTimer(k_RangeTimerId, k_RangeFrameMs);
}

void CGraphControl::StepRangeAnimation() {
    if (m_RangeAnimMs == 0) {
        KillTimer(k_RangeTimerId);
        return;
    }

    const uint64_t elapsed = GetTickCount64() - m_RangeAnimStart;
    float t = static_cast<float>(elapsed) / m_RangeAnimMs;

    if (t >= 1.0f) {
        m_Range.Max = m_RangeTarget;
        KillTimer(k_RangeTimerId);
    }
    else {
        // Ease out cubic: quick off the mark, gentle into place.
        const float inv    = 1.0f - t;
        const float eased  = 1.0f - inv * inv * inv;
        m_Range.Max = m_RangeFrom + (m_RangeTarget - m_RangeFrom) * eased;
    }
    RequestInvalidate();
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
    opt.Legend     = (m_CtrlStyle & GCS_LEGEND) != 0;
    opt.Stacked    = (m_CtrlStyle & GCS_STACKED) != 0;
    opt.Bars       = (m_CtrlStyle & GCS_BARS) != 0;
    opt.ValueOverlay = (m_CtrlStyle & GCS_VALUEOVERLAY) != 0;
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
    opt.ValueText    = m_ValueText.empty() ? nullptr : m_ValueText.c_str();
    opt.RefLines     = m_RefLines.empty() ? nullptr : m_RefLines.data();
    opt.RefLineCount = m_RefLines.size();

    if ((m_CtrlStyle & GCS_TOOLTIP) && m_HoverIndex >= 0) {
        const float dip = 96.0f / m_Renderer.GetDpi();
        opt.HoverIndex  = m_HoverIndex;
        opt.HoverX      = m_HoverPt.x * dip;
        opt.HoverY      = m_HoverPt.y * dip;
        opt.HoverText   = m_HoverText.empty() ? nullptr : m_HoverText.c_str();
    }
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
