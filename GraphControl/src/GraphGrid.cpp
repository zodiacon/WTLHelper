// GraphGrid.cpp : tiles a set of CGraphControls over one shared time base.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/GraphGrid.h"

#include <algorithm>
#include <cmath>

namespace GraphCtrl {

constexpr UINT_PTR k_GridTimerId = 1;

// ---- Message handlers --------------------------------------------------------

int CGraphGrid::OnCreate(LPCREATESTRUCT) {
    // Tiles asked for before the grid had a window get their windows now.
    for (size_t i = 0; i < m_Graphs.size(); i++) {
        if (!m_Graphs[i]->m_hWnd)
            CreateChildWindow(i);
    }
    Layout();

    if (m_UpdateInterval && !m_Paused)
        SetTimer(k_GridTimerId, m_UpdateInterval);

    return 0;
}

void CGraphGrid::OnDestroy() {
    KillTimer(k_GridTimerId);

    // Destroy the tile windows before freeing the objects behind them. Windows
    // tears down child windows only after this handler returns, and a freed
    // CWindowImpl would still be on the receiving end of their messages.
    for (auto& g : m_Graphs) {
        if (g->m_hWnd)
            g->DestroyWindow();
    }
    m_Graphs.clear();
    m_SeriesIds.clear();
}

void CGraphGrid::OnPaint(HDC) {
    CPaintDC dc(m_hWnd);
    CRect rc;
    GetClientRect(&rc);

    // Only the gaps between tiles reach here: the grid is WS_CLIPCHILDREN.
    CBrush brush;
    brush.CreateSolidBrush(m_Theme.Background);
    dc.FillRect(&rc, brush);

    // a frame around each group of tiles, in the gaps around it
    if (m_Groups.size() >= m_Graphs.size() && m_TileRects.size() == m_Graphs.size()) {
        CPen pen;
        pen.CreatePen(PS_SOLID, 1, m_Theme.BorderColor);
        auto oldPen = dc.SelectPen(pen);
        auto oldBrush = dc.SelectStockBrush(NULL_BRUSH);
        const int inset = std::max(1, m_Spacing / 3);   // leaving a gap between two groups' frames
        for (size_t first = 0; first < m_TileRects.size(); ) {
            CRect group(m_TileRects[first]);
            size_t last = first;
            while (last + 1 < m_TileRects.size() && m_Groups[last + 1] == m_Groups[first]
                && m_TileRects[last + 1].top == m_TileRects[first].top)
                group.UnionRect(group, &m_TileRects[++last]);
            if (last > first && !group.IsRectEmpty()) {
                group.InflateRect(inset, inset);
                dc.RoundRect(group, CPoint(inset * 2, inset * 2));
            }
            first = last + 1;
        }
        dc.SelectBrush(oldBrush);
        dc.SelectPen(oldPen);
    }
}

void CGraphGrid::OnSize(UINT, CSize) {
    Layout();
}

void CGraphGrid::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != k_GridTimerId || m_Paused || !m_SampleSource) return;

    m_SampleScratch.assign(m_Graphs.size(), MissingSample);
    m_SampleSource(m_SampleScratch);
    PushSamples(m_SampleScratch.data(), m_SampleScratch.size());
}

BOOL CGraphGrid::OnEraseBkgnd(CDCHandle) {
    return TRUE;   // OnPaint fills the gaps
}

LRESULT CGraphGrid::OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    auto* nm = reinterpret_cast<NMHDR*>(lParam);

    // Mirror the hovered slot onto the other tiles. m_SyncingHover stops the
    // notifications that mirroring itself raises from bouncing back.
    if (m_SharedCrosshair && !m_SyncingHover && nm && nm->code == GCN_HOVERSAMPLE) {
        auto* hn = reinterpret_cast<GRAPHHOVERNOTIFY*>(lParam);

        m_SyncingHover = true;
        for (auto& g : m_Graphs) {
            if (!g->m_hWnd || g->m_hWnd == nm->hwndFrom) continue;
            // Crosshair and dot on the siblings, but only one readout box --
            // the one on the tile actually under the pointer.
            if (hn->SampleIndex >= 0) g->SetHoverSample(hn->SampleIndex, false, false);
            else                      g->ClearHoverSample();
        }
        m_SyncingHover = false;
    }

    // Pass the tiles' notifications up unchanged; hosts identify them by idFrom.
    HWND parent = GetParent();
    if (!parent) {
        bHandled = FALSE;
        return 0;
    }
    bHandled = TRUE;
    return ::SendMessage(parent, uMsg, wParam, lParam);
}

void CGraphGrid::SetSharedCrosshair(bool enable) {
    m_SharedCrosshair = enable;
    if (enable) return;

    m_SyncingHover = true;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->ClearHoverSample();
    m_SyncingHover = false;
}

// ---- Creation ----------------------------------------------------------------

HWND CGraphGrid::Create(HWND hWndParent, int x, int y, int w, int h,
                        DWORD dwStyle, DWORD childStyle, UINT nID, UINT childIdBase) {
    m_ChildStyle  = childStyle;
    m_ChildIdBase = childIdBase;

    CRect rc(x, y, x + w, y + h);
    return CWindowImpl<CGraphGrid>::Create(hWndParent, rc, nullptr, dwStyle, 0, nID);
}

void CGraphGrid::CreateChildWindow(size_t index) {
    if (!m_hWnd || index >= m_Graphs.size()) return;

    m_Graphs[index]->Create(m_hWnd, 0, 0, 10, 10, WS_CHILD | WS_VISIBLE,
                            m_ChildStyle,
                            static_cast<UINT>(m_ChildIdBase + index));
    ConfigureChild(index);
}

// Pushes the grid-wide settings into one tile and gives it its single series.
void CGraphGrid::ConfigureChild(size_t index) {
    CGraphControl& g = *m_Graphs[index];
    if (!g.m_hWnd) return;

    std::wstring title = m_TitlePrefix;
    if (!title.empty()) title += L' ';
    title += std::to_wstring(index);

    g.BeginUpdate();
    if (!m_FontFamily.empty())
        g.SetFont(m_FontFamily.c_str(), m_FontLabel, m_FontTitle, m_FontValue);
    g.SetRangeAnimation(m_RangeAnimMs, m_ShrinkDelay);
    g.SetExpectedInterval(m_UpdateInterval);
    g.SetHistoryLength(m_History);
    g.SetTheme(m_Theme);
    g.SetValueFormatter(m_Formatter);
    g.SetTitle(title);
    g.SetTimeSpanText(m_TimeSpanText);

    if (m_Range.AutoScale) g.SetAutoScale(true, m_Range.Headroom);
    else                   g.SetRange(m_Range.Min, m_Range.Max);

    if (index >= m_SeriesIds.size() || m_SeriesIds[index] == InvalidSeries) {
        const SeriesId id = g.AddSeries(title, StyleOf(index));
        if (index >= m_SeriesIds.size())
            m_SeriesIds.resize(index + 1, InvalidSeries);
        m_SeriesIds[index] = id;
    }
    else {
        g.SetSeriesStyle(m_SeriesIds[index], StyleOf(index));
    }
    g.EndUpdate();
}

// ---- Tiles -------------------------------------------------------------------

void CGraphGrid::SetGraphCount(size_t count) {
    if (count == m_Graphs.size()) return;

    while (m_Graphs.size() > count) {
        if (m_Graphs.back()->m_hWnd)
            m_Graphs.back()->DestroyWindow();
        m_Graphs.pop_back();
    }
    m_SeriesIds.resize(m_Graphs.size(), InvalidSeries);

    while (m_Graphs.size() < count) {
        m_Graphs.push_back(std::make_unique<CGraphControl>());
        m_SeriesIds.push_back(InvalidSeries);
        if (m_hWnd)
            CreateChildWindow(m_Graphs.size() - 1);
    }

    Layout();
}

CGraphControl* CGraphGrid::GetGraph(size_t index) {
    return index < m_Graphs.size() ? m_Graphs[index].get() : nullptr;
}

void CGraphGrid::SetLayout(int columns, int rows) {
    m_Cols = std::max(columns, 0);
    m_Rows = std::max(rows, 0);
    Layout();
}

void CGraphGrid::SetTileGroups(std::vector<uint32_t> groups, int groupSpacing) {
    m_Groups = std::move(groups);
    m_GroupSpacing = std::max(groupSpacing, 0);
    Layout();
}

void CGraphGrid::SetSpacing(int pixels) {
    m_Spacing = std::max(pixels, 0);
    Layout();
}

void CGraphGrid::Layout() {
    if (!m_hWnd || m_Graphs.empty()) return;

    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    GridLayoutInput in;
    in.width = rc.Width();
    in.height = rc.Height();
    in.count = static_cast<int>(m_Graphs.size());
    in.columns = m_Cols;
    in.rows = m_Rows;
    in.spacing = m_Spacing;
    if (m_Groups.size() >= m_Graphs.size()) {
        in.groups = m_Groups;
        in.groupSpacing = m_GroupSpacing;
    }
    const auto rects = LayoutGridTiles(in);
    if (!in.groups.empty()) {
        m_TileRects = rects;
        Invalidate();   // the groups' frames, in the gaps
    }
    else {
        m_TileRects.clear();
    }

    for (size_t i = 0; i < m_Graphs.size(); i++) {
        HWND child = m_Graphs[i]->m_hWnd;
        if (!child) continue;

        // Tiles that do not fit the requested layout are hidden rather than
        // squeezed into a cell someone else owns.
        const RECT& r = rects[i];
        if (r.right <= r.left || r.bottom <= r.top) {
            ::ShowWindow(child, SW_HIDE);
            continue;
        }
        ::SetWindowPos(child, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
        ::ShowWindow(child, SW_SHOW);
    }
}

// ---- Applied to every tile ---------------------------------------------------

void CGraphGrid::SetRange(float minValue, float maxValue) {
    m_Range.AutoScale = false;
    m_Range.Min = minValue;
    m_Range.Max = maxValue;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetRange(minValue, maxValue);
}

void CGraphGrid::SetAutoScale(bool enable, float headroom) {
    m_Range.AutoScale = enable;
    m_Range.Headroom  = headroom;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetAutoScale(enable, headroom);
}

void CGraphGrid::SetHistoryLength(size_t samples) {
    m_History = std::max(samples, k_MinCapacity);
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetHistoryLength(m_History);
}

void CGraphGrid::SetTheme(const GraphTheme& theme) {
    m_Theme = theme;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetTheme(theme);
    if (m_hWnd) Invalidate(FALSE);
}

void CGraphGrid::SetChildStyle(DWORD style) {
    m_ChildStyle = style;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetGraphStyle(style);
}

void CGraphGrid::SetValueFormatter(ValueFormatFn formatter) {
    m_Formatter = std::move(formatter);
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetValueFormatter(m_Formatter);
}

void CGraphGrid::SetSeriesStyle(const SeriesStyle& style) {
    m_SeriesStyle = style;
    for (size_t i = 0; i < m_Graphs.size(); i++) {
        if (m_Graphs[i]->m_hWnd && i < m_SeriesIds.size() && m_SeriesIds[i] != InvalidSeries)
            m_Graphs[i]->SetSeriesStyle(m_SeriesIds[i], StyleOf(i));
    }
}

const SeriesStyle& CGraphGrid::StyleOf(size_t index) const {
    return index < m_TileStyles.size() && m_TileStyles[index] ? *m_TileStyles[index] : m_SeriesStyle;
}

void CGraphGrid::SetTileSeriesStyle(size_t index, const SeriesStyle& style) {
    if (index >= m_TileStyles.size())
        m_TileStyles.resize(index + 1);
    m_TileStyles[index] = style;
    if (index < m_Graphs.size() && m_Graphs[index]->m_hWnd && index < m_SeriesIds.size() && m_SeriesIds[index] != InvalidSeries)
        m_Graphs[index]->SetSeriesStyle(m_SeriesIds[index], style);
}

void CGraphGrid::ClearTileSeriesStyle(size_t index) {
    if (index >= m_TileStyles.size() || !m_TileStyles[index])
        return;
    m_TileStyles[index].reset();
    if (index < m_Graphs.size() && m_Graphs[index]->m_hWnd && index < m_SeriesIds.size() && m_SeriesIds[index] != InvalidSeries)
        m_Graphs[index]->SetSeriesStyle(m_SeriesIds[index], m_SeriesStyle);
}

void CGraphGrid::SetFont(const wchar_t* family, float labelSize,
                         float titleSize, float valueSize) {
    m_FontFamily = family ? family : L"";
    m_FontLabel  = labelSize;
    m_FontTitle  = titleSize;
    m_FontValue  = valueSize;

    for (auto& g : m_Graphs) {
        if (!g->m_hWnd) continue;
        if (m_FontFamily.empty()) g->ResetFont();
        else                      g->SetFont(m_FontFamily.c_str(), labelSize, titleSize, valueSize);
    }
}

void CGraphGrid::SetRangeAnimation(UINT milliseconds, int shrinkDelaySamples) {
    m_RangeAnimMs = milliseconds;
    m_ShrinkDelay = shrinkDelaySamples;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetRangeAnimation(milliseconds, shrinkDelaySamples);
}

void CGraphGrid::SetTimeSpanText(std::wstring text) {
    m_TimeSpanText = std::move(text);
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetTimeSpanText(m_TimeSpanText);
}

void CGraphGrid::SetTitlePrefix(std::wstring prefix) {
    m_TitlePrefix = std::move(prefix);
    for (size_t i = 0; i < m_Graphs.size(); i++) {
        if (!m_Graphs[i]->m_hWnd) continue;
        std::wstring title = m_TitlePrefix;
        if (!title.empty()) title += L' ';
        title += std::to_wstring(i);
        m_Graphs[i]->SetTitle(title);
    }
}

// ---- Samples -----------------------------------------------------------------

void CGraphGrid::PushSamples(const float* values, size_t count) {
    for (size_t i = 0; i < m_Graphs.size(); i++) {
        if (!m_Graphs[i]->m_hWnd || i >= m_SeriesIds.size()) continue;
        if (m_SeriesIds[i] == InvalidSeries) continue;

        const float v = (values && i < count) ? values[i] : MissingSample;
        m_Graphs[i]->PushSample(m_SeriesIds[i], v);
    }
}

void CGraphGrid::PushSamples(const std::vector<float>& values) {
    PushSamples(values.data(), values.size());
}

void CGraphGrid::Clear() {
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->Clear();
}

// ---- Live update -------------------------------------------------------------

void CGraphGrid::SetUpdateInterval(UINT milliseconds) {
    m_UpdateInterval = milliseconds;
    for (auto& g : m_Graphs)
        if (g->m_hWnd) g->SetExpectedInterval(milliseconds);
    if (!m_hWnd) return;

    KillTimer(k_GridTimerId);
    if (m_UpdateInterval && !m_Paused)
        SetTimer(k_GridTimerId, m_UpdateInterval);
}

void CGraphGrid::SetSampleSource(SampleSourceFn source) {
    m_SampleSource = std::move(source);
}

void CGraphGrid::Pause() {
    if (m_Paused) return;
    m_Paused = true;
    if (m_hWnd) KillTimer(k_GridTimerId);
}

void CGraphGrid::Resume() {
    if (!m_Paused) return;
    m_Paused = false;
    if (m_hWnd && m_UpdateInterval)
        SetTimer(k_GridTimerId, m_UpdateInterval);
}

} // namespace GraphCtrl
