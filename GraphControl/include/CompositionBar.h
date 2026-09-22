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

#include "GraphRenderer.h"

// Notification codes sent via WM_NOTIFY to the parent window.
#define CBN_SEGMENTCLICK   1   // lParam -> SEGMENTNOTIFY*, a segment was clicked
#define CBN_GETTOOLTIP     2   // lParam -> SEGMENTTOOLTIPNOTIFY*; fill SzText to override

namespace GraphCtrl {

struct SEGMENTNOTIFY {
    NMHDR     Hdr;
    SegmentId Id;
    int       Index;     // -1 when the click missed every segment
};

struct SEGMENTTOOLTIPNOTIFY {
    NMHDR     Hdr;
    SegmentId Id;
    int       Index;
    wchar_t   SzText[256];   // pre-filled with "name: value"; host may override
};

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_COMPOSITIONBAR[] = L"CompositionBar";

// Control styles, packed into the low-order style bits.
#define CBS_LABELS   0x0001   // name and value beside each segment that fits
#define CBS_TOOLTIP  0x0002   // hover highlight and readout
#define CBS_VERTICAL 0x0004   // stack top to bottom instead of left to right
#define CBS_DEFAULT  (CBS_LABELS | CBS_TOOLTIP)

// A single band split into segments that sum to a whole -- the Task Manager
// memory composition strip. Not a time series: use CGraphControl for those.
// Shares the graph theme, value formatters and Direct2D resources.
class CCompositionBar : public CWindowImpl<CCompositionBar> {
public:
    DECLARE_WND_CLASS_EX(WC_COMPOSITIONBAR, CS_HREDRAW | CS_VREDRAW, -1)

    BEGIN_MSG_MAP(CCompositionBar)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MSG_WM_MOUSELEAVE(OnMouseLeave)
        MSG_WM_LBUTTONDOWN(OnLButtonDown)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrintClient)
        MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
    END_MSG_MAP()

    HWND Create(HWND hWndParent, int x, int y, int w, int h,
                DWORD dwStyle = WS_CHILD | WS_VISIBLE, DWORD ctrlStyle = CBS_DEFAULT,
                UINT nID = 0);

    // ---- Segments ----
    SegmentId AddSegment(std::wstring name, COLORREF color, float value = 0.0f);
    bool      RemoveSegment(SegmentId id);
    void      RemoveAllSegments();

    void SetSegmentValue(SegmentId id, float value);
    void SetSegmentColor(SegmentId id, COLORREF color);
    // One value per segment, in AddSegment order.
    void SetValues(const float* values, size_t count);
    void SetValues(const std::vector<float>& values);

    size_t SegmentCount() const { return m_Segments.size(); }
    const std::vector<CompositionSegment>& Segments() const { return m_Segments; }

    // 0 (the default) means the segments are the whole; set it to show the
    // segments as a share of something larger, leaving the remainder empty.
    void  SetTotal(float total);
    float GetTotal() const { return m_Total; }

    // ---- Appearance ----
    void SetTitle(std::wstring title);
    void SetTheme(const GraphTheme& theme);
    const GraphTheme& GetTheme() const { return m_Theme; }
    void SetValueFormatter(ValueFormatFn formatter);
    bool SetFont(const wchar_t* family, float labelSize = 11.0f,
                 float titleSize = 12.5f, float valueSize = 22.0f);
    void ResetFont();

    DWORD GetBarStyle() const { return m_CtrlStyle; }
    void  SetBarStyle(DWORD style);

    // Segments run left to right, or top to bottom when vertical, in the order
    // they were added. Add them in reverse for a bar that fills from the bottom.
    void SetVertical(bool vertical);
    bool IsVertical() const { return (m_CtrlStyle & CBS_VERTICAL) != 0; }

    // ---- Hover ----
    int GetHoverSegment() const { return m_HoverIndex; }

    void Refresh();

private:
    int  OnCreate(LPCREATESTRUCT pcs);
    void OnDestroy();
    void OnPaint(HDC dc);
    void OnSize(UINT nType, CSize size);
    void OnMouseMove(UINT nFlags, CPoint pt);
    void OnMouseLeave();
    void OnLButtonDown(UINT nFlags, CPoint pt);
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    CompositionSegment* Find(SegmentId id);
    void BuildHoverText(int index);
    void ClearHover();
    CompositionOptions MakeOptions() const;

private:
    std::vector<CompositionSegment> m_Segments;
    SegmentId                       m_NextId = 0;

    GraphRenderer m_Renderer;
    GraphTheme    m_Theme = GraphTheme::Dark();
    ValueFormatFn m_Formatter;
    std::wstring  m_Title;
    float         m_Total     = 0.0f;
    DWORD         m_CtrlStyle = CBS_DEFAULT;

    int          m_HoverIndex    = -1;
    CPoint       m_HoverPt       = {};
    bool         m_TrackingMouse = false;
    std::wstring m_HoverText;
};

} // namespace GraphCtrl
