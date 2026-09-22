// CompositionBar.cpp : one band split into segments that sum to a whole.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/CompositionBar.h"

#include <algorithm>

namespace GraphCtrl {

// ---- Message handlers --------------------------------------------------------

int CCompositionBar::OnCreate(LPCREATESTRUCT pcs) {
    m_CtrlStyle = pcs->style & 0xFFFF;

    if (FAILED(m_Renderer.Init())) return -1;

    const UINT dpi = ::GetDpiForWindow(m_hWnd);
    m_Renderer.SetDpi(dpi ? static_cast<float>(dpi) : 96.0f);
    return 0;
}

void CCompositionBar::OnDestroy() {
    m_Renderer.Shutdown();
}

void CCompositionBar::OnPaint(HDC) {
    CPaintDC dc(m_hWnd);
    CRect rc;
    GetClientRect(&rc);
    if (rc.IsRectEmpty()) return;

    m_Renderer.RenderComposition(m_hWnd, rc, m_Segments, m_Theme,
                                 MakeOptions(), m_Formatter);
}

void CCompositionBar::OnSize(UINT, CSize size) {
    m_Renderer.Resize(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy));
}

void CCompositionBar::OnMouseMove(UINT, CPoint pt) {
    if (!(m_CtrlStyle & CBS_TOOLTIP)) return;

    if (!m_TrackingMouse) {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
    }

    CRect rc;
    GetClientRect(&rc);
    const POINT p{ pt.x, pt.y };
    const int index = m_Renderer.HitTestSegment(rc, m_Segments, MakeOptions(), p);

    if (index < 0) {
        ClearHover();
        return;
    }

    if (index != m_HoverIndex) {
        m_HoverIndex = index;
        BuildHoverText(index);
    }
    m_HoverPt = pt;
    Invalidate(FALSE);
}

void CCompositionBar::OnMouseLeave() {
    m_TrackingMouse = false;
    ClearHover();
}

void CCompositionBar::OnLButtonDown(UINT, CPoint pt) {
    SetFocus();

    CRect rc;
    GetClientRect(&rc);
    const POINT p{ pt.x, pt.y };
    const int index = m_Renderer.HitTestSegment(rc, m_Segments, MakeOptions(), p);

    HWND parent = GetParent();
    if (!parent) return;

    SEGMENTNOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = CBN_SEGMENTCLICK;
    nmh.Index        = index;
    nmh.Id           = (index >= 0 && index < static_cast<int>(m_Segments.size()))
                       ? m_Segments[index].Id : InvalidSegment;
    ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));
}

BOOL CCompositionBar::OnEraseBkgnd(CDCHandle) {
    return TRUE;   // the renderer fills the background
}

LRESULT CCompositionBar::OnPrintClient(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    CRect rc;
    GetClientRect(&rc);
    if (!dc || rc.IsRectEmpty()) {
        bHandled = FALSE;
        return 0;
    }

    m_Renderer.RenderCompositionToDC(dc, rc, m_Segments, m_Theme,
                                     MakeOptions(), m_Formatter);
    return 0;
}

LRESULT CCompositionBar::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    const UINT dpi = ::GetDpiForWindow(m_hWnd);
    m_Renderer.SetDpi(dpi ? static_cast<float>(dpi) : 96.0f);
    Invalidate(FALSE);
    bHandled = FALSE;
    return 0;
}

// ---- Creation ----------------------------------------------------------------

HWND CCompositionBar::Create(HWND hWndParent, int x, int y, int w, int h,
                             DWORD dwStyle, DWORD ctrlStyle, UINT nID) {
    CRect rc(x, y, x + w, y + h);
    return CWindowImpl<CCompositionBar>::Create(hWndParent, rc, nullptr,
                                                dwStyle | ctrlStyle, 0, nID);
}

// ---- Segments ----------------------------------------------------------------

CompositionSegment* CCompositionBar::Find(SegmentId id) {
    auto it = std::find_if(m_Segments.begin(), m_Segments.end(),
                           [id](const CompositionSegment& s) { return s.Id == id; });
    return it == m_Segments.end() ? nullptr : &*it;
}

SegmentId CCompositionBar::AddSegment(std::wstring name, COLORREF color, float value) {
    CompositionSegment segment;
    segment.Id    = m_NextId++;
    segment.Name  = std::move(name);
    segment.Color = color;
    segment.Value = value;

    m_Segments.push_back(std::move(segment));
    Refresh();
    return m_Segments.back().Id;
}

bool CCompositionBar::RemoveSegment(SegmentId id) {
    auto it = std::find_if(m_Segments.begin(), m_Segments.end(),
                           [id](const CompositionSegment& s) { return s.Id == id; });
    if (it == m_Segments.end()) return false;

    m_Segments.erase(it);
    ClearHover();
    Refresh();
    return true;
}

void CCompositionBar::RemoveAllSegments() {
    m_Segments.clear();
    ClearHover();
    Refresh();
}

void CCompositionBar::SetSegmentValue(SegmentId id, float value) {
    if (CompositionSegment* s = Find(id)) {
        s->Value = value;
        Refresh();
    }
}

void CCompositionBar::SetSegmentColor(SegmentId id, COLORREF color) {
    if (CompositionSegment* s = Find(id)) {
        s->Color = color;
        Refresh();
    }
}

void CCompositionBar::SetValues(const float* values, size_t count) {
    for (size_t i = 0; i < m_Segments.size(); i++)
        m_Segments[i].Value = (values && i < count) ? values[i] : 0.0f;
    Refresh();
}

void CCompositionBar::SetValues(const std::vector<float>& values) {
    SetValues(values.data(), values.size());
}

void CCompositionBar::SetTotal(float total) {
    m_Total = total < 0.0f ? 0.0f : total;
    Refresh();
}

// ---- Appearance --------------------------------------------------------------

void CCompositionBar::SetTitle(std::wstring title) {
    m_Title = std::move(title);
    Refresh();
}

void CCompositionBar::SetTheme(const GraphTheme& theme) {
    m_Theme = theme;
    Refresh();
}

void CCompositionBar::SetValueFormatter(ValueFormatFn formatter) {
    m_Formatter = std::move(formatter);
    Refresh();
}

bool CCompositionBar::SetFont(const wchar_t* family, float labelSize,
                              float titleSize, float valueSize) {
    const bool ok = SUCCEEDED(m_Renderer.SetFont(family, labelSize, titleSize, valueSize));
    if (ok) Refresh();
    return ok;
}

void CCompositionBar::ResetFont() {
    m_Renderer.ResetFont();
    Refresh();
}

void CCompositionBar::SetBarStyle(DWORD style) {
    const DWORD previous = m_CtrlStyle;
    m_CtrlStyle = style & 0xFFFF;

    // A changed orientation moves every segment, so the hovered one is stale.
    if (!(m_CtrlStyle & CBS_TOOLTIP) ||
        ((previous ^ m_CtrlStyle) & CBS_VERTICAL))
        ClearHover();

    Refresh();
}

void CCompositionBar::SetVertical(bool vertical) {
    const DWORD style = vertical ? (m_CtrlStyle | CBS_VERTICAL)
                                 : (m_CtrlStyle & ~CBS_VERTICAL);
    if (style == m_CtrlStyle) return;
    SetBarStyle(style);
}

void CCompositionBar::Refresh() {
    if (m_hWnd) Invalidate(FALSE);
}

// ---- Hover -------------------------------------------------------------------

void CCompositionBar::ClearHover() {
    if (m_HoverIndex < 0) return;
    m_HoverIndex = -1;
    m_HoverText.clear();
    Refresh();
}

void CCompositionBar::BuildHoverText(int index) {
    m_HoverText.clear();
    if (index < 0 || index >= static_cast<int>(m_Segments.size())) return;

    const CompositionSegment& segment = m_Segments[index];

    wchar_t value[64] = {};
    if (m_Formatter) m_Formatter(segment.Value, value, _countof(value));
    else             Format::Number(segment.Value, value, _countof(value));

    m_HoverText = segment.Name;
    if (!m_HoverText.empty()) m_HoverText += L": ";
    m_HoverText += value;

    // Share of the whole, which is the question a composition bar invites.
    float total = m_Total;
    if (total <= 0.0f) {
        total = 0.0f;
        for (const auto& s : m_Segments) total += std::max(0.0f, s.Value);
    }
    if (total > 0.0f) {
        wchar_t share[32] = {};
        swprintf_s(share, L"\n%.1f%% of total", segment.Value * 100.0f / total);
        m_HoverText += share;
    }

    // Let the host replace the text entirely.
    HWND parent = GetParent();
    if (!parent) return;

    SEGMENTTOOLTIPNOTIFY nmh{};
    nmh.Hdr.hwndFrom = m_hWnd;
    nmh.Hdr.idFrom   = GetDlgCtrlID();
    nmh.Hdr.code     = CBN_GETTOOLTIP;
    nmh.Id           = segment.Id;
    nmh.Index        = index;
    wcsncpy_s(nmh.SzText, m_HoverText.c_str(), _TRUNCATE);

    ::SendMessage(parent, WM_NOTIFY, nmh.Hdr.idFrom, reinterpret_cast<LPARAM>(&nmh));
    m_HoverText = nmh.SzText;
}

CompositionOptions CCompositionBar::MakeOptions() const {
    CompositionOptions opt;
    opt.Title      = m_Title.empty() ? nullptr : m_Title.c_str();
    opt.ShowLabels = (m_CtrlStyle & CBS_LABELS) != 0;
    opt.Vertical   = (m_CtrlStyle & CBS_VERTICAL) != 0;
    opt.Total      = m_Total;

    if ((m_CtrlStyle & CBS_TOOLTIP) && m_HoverIndex >= 0) {
        const float dip = 96.0f / m_Renderer.GetDpi();
        opt.HoverIndex  = m_HoverIndex;
        opt.HoverX      = m_HoverPt.x * dip;
        opt.HoverY      = m_HoverPt.y * dip;
        opt.HoverText   = m_HoverText.empty() ? nullptr : m_HoverText.c_str();
    }
    return opt;
}

} // namespace GraphCtrl
