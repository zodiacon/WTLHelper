#pragma once

#include "ThemeHelper.h"
#include "Theme.h"
#include "WTLHelper.h"
#include <atlsplit.h>
#include <atltypes.h>
#include <algorithm>

//
// How the bar between the panes is drawn. Line (the default) is a hairline in the window's background, as Windows 11's
// apps draw it; Gutter a band a shade off the background; Grip three dots at the bar's middle; Classic the band
// with two edges this class drew before; None nothing until the mouse is over the bar.
//
enum class SplitterBarStyle {
	Line, Gutter, Grip, Classic, None
};

//
// The bar's colors. CLR_DEFAULT follows the mode (light, WTLHelper's dark mode, a ThemeHelper theme): the views'
// background (the panes are mostly list and tree views), and the highlight color for the hot and pressed bar.
// High contrast uses the system colors over these.
//
struct SplitterColors {
	COLORREF Back{ CLR_DEFAULT };       // the bar's background (Line, Grip, None)
	COLORREF Line{ CLR_DEFAULT };       // the line (Line), the edges (Classic)
	COLORREF Gutter{ CLR_DEFAULT };     // the band (Gutter, Classic)
	COLORREF Grip{ CLR_DEFAULT };       // the dots (Grip)
	COLORREF Hot{ CLR_DEFAULT };        // the mouse over the bar
	COLORREF Pressed{ CLR_DEFAULT };    // the bar being dragged, and the ghost bar
};

//
// The bar while it's dragged with the system's "show window contents while dragging" off: a translucent popup
// in the pressed color, click-through, instead of WTL's inverted halftone pattern.
//
class CSplitterGhostBar : public CWindowImpl<CSplitterGhostBar, CWindow,
	CWinTraits<WS_POPUP | WS_DISABLED, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW>> {
public:
	DECLARE_WND_CLASS_EX(L"WTLHelper_SplitterGhostBar", 0, -1)

	BEGIN_MSG_MAP(CSplitterGhostBar)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
	END_MSG_MAP()

	// rect: in screen coordinates
	void Show(HWND hOwner, RECT const& rect, COLORREF color, BYTE alpha = 0x90) {
		if (!this->IsWindow()) {
			RECT rc = rect;
			if (!this->Create(hOwner, rc))
				return;
		}
		::SetLayeredWindowAttributes(this->m_hWnd, 0, alpha, LWA_ALPHA);
		if (m_Color != color) {
			m_Color = color;
			this->Invalidate();
		}
		this->SetWindowPos(HWND_TOP, &rect, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	void Hide() {
		if (this->IsWindow())
			this->DestroyWindow();
	}

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		CPaintDC dc(this->m_hWnd);
		dc.FillSolidRect(&dc.m_ps.rcPaint, m_Color);
		return 0;
	}

	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) {
		return HTTRANSPARENT;
	}

	COLORREF m_Color{ CLR_INVALID };
};

template <bool t_bVertical>
class CCustomSplitterWindowT : public CSplitterWindowImpl<CCustomSplitterWindowT<t_bVertical>> {
	using Base = CSplitterWindowImpl<CCustomSplitterWindowT<t_bVertical>>;
public:
	DECLARE_WND_CLASS2(_T("WTL_SplitterWindow"), CSplitterWindowT<t_bVertical>)

	BEGIN_MSG_MAP(CCustomSplitterWindowT)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(WTLHelper::ThemeChangedMessage, OnColorsChanged)
		if (this->IsInteractive()) {
			MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
			MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
			MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
			MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		}
		CHAIN_MSG_MAP(Base)
	END_MSG_MAP()

	CCustomSplitterWindowT() : Base(t_bVertical) {
		this->m_dwExtendedStyle |= SPLIT_FIXEDBARSIZE;
	}

	void SetBarStyle(SplitterBarStyle style) {
		m_Style = style;
		InvalidateBar();
	}

	SplitterBarStyle GetBarStyle() const {
		return m_Style;
	}

	// the bar's thickness (what the mouse can grab) in DIPs; 0: 6, or 2 with WS_EX_CLIENTEDGE
	void SetBarWidth(int dips) {
		m_BarWidth = dips;
		if (this->IsWindow()) {
			GetSystemSettings(true);
			this->Invalidate();
		}
	}

	// the line's thickness in DIPs (Line), and the hot or pressed line's (Line, None)
	void SetLineWidth(int dips, int hotDips = 3) {
		m_LineWidth = dips;
		m_HotLineWidth = hotDips;
		InvalidateBar();
	}

	void SetColors(SplitterColors const& colors) {
		m_Colors = colors;
		InvalidateBar();
	}

	SplitterColors const& GetColors() const {
		return m_Colors;
	}

	// the bar changing color under the mouse and while dragged
	void EnableHotTracking(bool enable = true) {
		m_HotTracking = enable;
		if (!enable)
			m_Hot = false;
		InvalidateBar();
	}

	// translucent: a ghost bar in the pressed color when the system doesn't drag with the windows' contents; else WTL's
	void SetGhostBarStyle(bool translucent) {
		m_TranslucentGhost = translucent;
		if (this->IsWindow() && !m_Dragging)
			GetSystemSettings(false);
	}

	// for testing the high contrast colors
	void ForceHighContrast(bool force) {
		m_ForceHighContrast = force;
		InvalidateBar();
	}

	// called by CSplitterImpl (create, WM_SETTINGCHANGE): the bar's size for the window's DPI
	void GetSystemSettings(bool update) {
		Base::GetSystemSettings(false);
		m_Dpi = ::GetDpiForWindow(this->m_hWnd);
		if (m_Dpi == 0)
			m_Dpi = 96;
		bool edge = (this->GetExStyle() & WS_EX_CLIENTEDGE) != 0;
		this->m_cxySplitBar = Scale(m_BarWidth > 0 ? m_BarWidth : (edge ? 2 : 6));
		int cxyEdge = 2 * ::GetSystemMetricsForDpi(t_bVertical ? SM_CXEDGE : SM_CYEDGE, m_Dpi);
		this->m_cxyBarEdge = edge ? cxyEdge : 0;
		this->m_cxyMin = edge ? 0 : cxyEdge;

		// with the ghost bar ours, CSplitterImpl drags as with the contents (it never draws its own ghost bar)
		// and the mouse moves are taken before it gets them
		m_GhostDrag = m_TranslucentGhost && !this->m_bFullDrag;
		if (m_GhostDrag)
			this->m_bFullDrag = true;

		if (update)
			this->UpdateSplitterLayout();
	}

	void DrawSplitterBar(CDCHandle dc) {
		CRect rect;
		if (!this->GetSplitterBarRect(&rect))
			return;

		auto p = GetPalette();
		bool active = m_HotTracking && (m_Dragging || m_Hot);
		COLORREF accent = m_Dragging ? p.Pressed : p.Hot;
		switch (m_Style) {
			case SplitterBarStyle::Line:
				dc.FillSolidRect(&rect, p.Back);
				DrawLine(dc, rect, active ? m_HotLineWidth : m_LineWidth, active ? accent : p.Line);
				break;

			case SplitterBarStyle::None:
				dc.FillSolidRect(&rect, p.Back);
				if (active)
					DrawLine(dc, rect, m_HotLineWidth, accent);
				break;

			case SplitterBarStyle::Gutter:
				dc.FillSolidRect(&rect, active ? (p.HighContrast ? accent : Blend(p.Gutter, accent, m_Dragging ? 50 : 30)) : p.Gutter);
				break;

			case SplitterBarStyle::Grip:
				dc.FillSolidRect(&rect, active && !p.HighContrast ? Blend(p.Back, accent, 15) : p.Back);
				DrawGrip(dc, rect, active ? accent : p.Grip);
				break;

			case SplitterBarStyle::Classic:
			{
				dc.FillSolidRect(&rect, active ? (p.HighContrast ? accent : Blend(p.Gutter, accent, 40)) : p.Gutter);
				// the two long edges
				CRect edge(rect);
				if constexpr (t_bVertical)
					edge.right = edge.left + 1;
				else
					edge.bottom = edge.top + 1;
				dc.FillSolidRect(&edge, p.Line);
				edge = rect;
				if constexpr (t_bVertical)
					edge.left = edge.right - 1;
				else
					edge.top = edge.bottom - 1;
				dc.FillSolidRect(&edge, p.Line);
				break;
			}
		}
	}

private:
	struct Palette {
		COLORREF Back, Line, Gutter, Grip, Hot, Pressed;
		bool HighContrast;
	};

	static COLORREF Blend(COLORREF from, COLORREF to, int percent) {
		auto mix = [&](int a, int b) { return (BYTE)(a + (b - a) * percent / 100); };
		return RGB(mix(GetRValue(from), GetRValue(to)), mix(GetGValue(from), GetGValue(to)), mix(GetBValue(from), GetBValue(to)));
	}

	static bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	bool IsHighContrast() const {
		if (m_ForceHighContrast)
			return true;
		HIGHCONTRAST hc{ sizeof(hc) };
		return ::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
	}

	Palette GetPalette() const {
		if (IsHighContrast()) {
			auto back = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), hot = ::GetSysColor(COLOR_HIGHLIGHT);
			return { back, text, back, text, hot, hot, true };
		}

		// the dark mode gives list and tree views a background of their own (COLOR_WINDOW is darker);
		// ThemeHelper's themes answer GetSysColor
		bool darkMode = WTLHelper::IsDarkMode();
		auto back = darkMode ? DarkMode::getViewBackgroundColor() : ::GetSysColor(COLOR_WINDOW);
		auto text = darkMode ? DarkMode::getViewTextColor() : ::GetSysColor(COLOR_WINDOWTEXT);
		auto hot = ::GetSysColor(COLOR_HIGHLIGHT);
		Palette p{ back, Blend(back, text, 20), Blend(back, text, 8), Blend(back, text, 45), hot, Blend(hot, text, 25), false };
		if (m_Style == SplitterBarStyle::Classic) {
			// neither is mapped by the dark mode, which kept the bar light gray there
			bool dark = IsDark(back);
			p.Gutter = dark ? Blend(back, text, 25) : ::GetSysColor(COLOR_APPWORKSPACE);
			p.Line = dark ? Blend(back, text, 45) : ::GetSysColor(COLOR_GRAYTEXT);
		}

		auto pick = [](COLORREF& color, COLORREF custom) {
			if (custom != CLR_DEFAULT)
				color = custom;
		};
		pick(p.Back, m_Colors.Back);
		pick(p.Line, m_Colors.Line);
		pick(p.Gutter, m_Colors.Gutter);
		pick(p.Grip, m_Colors.Grip);
		pick(p.Hot, m_Colors.Hot);
		pick(p.Pressed, m_Colors.Pressed);
		return p;
	}

	int Scale(int dips) const {
		return ::MulDiv(dips, m_Dpi, 96);
	}

	// a line along the bar, centered across it
	void DrawLine(CDCHandle dc, CRect const& rect, int dips, COLORREF color) const {
		int bar = t_bVertical ? rect.Width() : rect.Height();
		int width = std::clamp(Scale(dips), 1, (std::max)(bar, 1));
		CRect line(rect);
		if constexpr (t_bVertical) {
			line.left += (bar - width) / 2;
			line.right = line.left + width;
		}
		else {
			line.top += (bar - width) / 2;
			line.bottom = line.top + width;
		}
		dc.FillSolidRect(&line, color);
	}

	// three dots at the middle of the bar
	void DrawGrip(CDCHandle dc, CRect const& rect, COLORREF color) const {
		int bar = t_bVertical ? rect.Width() : rect.Height();
		int dot = std::clamp(Scale(2), 1, (std::max)(bar, 1));
		int step = dot + Scale(3);
		int across = (t_bVertical ? rect.left : rect.top) + (bar - dot) / 2;
		int along = (t_bVertical ? (rect.top + rect.bottom) : (rect.left + rect.right)) / 2 - step - dot / 2;

		CBrush brush;
		brush.CreateSolidBrush(color);
		auto oldBrush = dc.SelectBrush(brush);
		auto oldPen = dc.SelectStockPen(NULL_PEN);
		for (int i = 0; i < 3; i++, along += step) {
			CRect rc = t_bVertical ? CRect(across, along, across + dot, along + dot) : CRect(along, across, along + dot, across + dot);
			if (dot < 4)
				dc.FillRect(&rc, brush);
			else
				dc.Ellipse(rc.left, rc.top, rc.right + 1, rc.bottom + 1);   // a null pen leaves out the right and bottom edges
		}
		dc.SelectPen(oldPen);
		dc.SelectBrush(oldBrush);
	}

	void InvalidateBar() {
		CRect rect;
		if (this->IsWindow() && this->GetSplitterBarRect(&rect))
			this->InvalidateRect(&rect, FALSE);
	}

	void SetHot(bool hot) {
		if (!m_HotTracking || m_Hot == hot)
			return;
		m_Hot = hot;
		if (hot) {
			TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, this->m_hWnd };
			::TrackMouseEvent(&tme);
		}
		InvalidateBar();
	}

	void UpdateHot() {
		POINT pt;
		::GetCursorPos(&pt);
		bool over = ::WindowFromPoint(pt) == this->m_hWnd;
		this->ScreenToClient(&pt);
		SetHot(over && this->IsOverSplitterBar(pt.x, pt.y));
	}

	void ShowGhost() {
		CRect rect;
		if (!this->GetSplitterBarRect(&rect))
			return;
		this->ClientToScreen(&rect);
		m_Ghost.Show(::GetAncestor(this->m_hWnd, GA_ROOT), rect, GetPalette().Pressed);
	}

	// the mouse pressed on the bar, or the keyboard's MoveSplitterBar
	void BeginDrag() {
		if (m_Dragging)
			return;
		m_Dragging = true;
		if (m_GhostDrag)
			ShowGhost();
		InvalidateBar();
	}

	// handlers of CSplitterImpl's that leave a message to DefWindowProc are called here rather than chained to,
	// which would run them a second time
	template <typename F>
	LRESULT CallBase(F handler, UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
		handled = TRUE;
		auto result = (this->*handler)(msg, wp, lp, handled);
		if (!handled) {
			result = this->DefWindowProc(msg, wp, lp);
			handled = TRUE;
		}
		return result;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
		m_Ghost.Hide();
		handled = FALSE;
		return 0;
	}

	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
		auto oldDpi = m_Dpi;
		GetSystemSettings(false);
		// a proportional position follows the size; a fixed one is pixels
		if (!this->IsProportional() && this->m_xySplitterPos >= 0 && oldDpi != m_Dpi)
			this->SetSplitterPos(::MulDiv(this->m_xySplitterPos, m_Dpi, oldDpi), false);
		this->UpdateSplitterLayout();
		this->Invalidate();
		return 0;
	}

	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& handled) {
		InvalidateBar();
		handled = FALSE;
		return 0;
	}

	LRESULT OnMouseMove(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
		int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
		if (::GetCapture() != this->m_hWnd) {
			SetHot(this->IsOverSplitterBar(x, y));
			return CallBase(&Base::OnMouseMove, msg, wp, lp, handled);
		}

		BeginDrag();
		if (!m_GhostDrag)
			return CallBase(&Base::OnMouseMove, msg, wp, lp, handled);

		// as CSplitterImpl's full drag, but the panes stay until the drop
		int pos = (t_bVertical ? x - this->m_rcSplitter.left : y - this->m_rcSplitter.top) - this->m_cxyDragOffset;
		if (pos == -1)   // -1 is the default position
			pos = -2;
		if (pos != this->m_xySplitterPos) {
			this->SetSplitterPos(pos, false);
			ShowGhost();
		}
		return 0;
	}

	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
		if (!m_Dragging)
			SetHot(false);
		return 0;
	}

	LRESULT OnLButtonDown(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
		auto result = CallBase(&Base::OnLButtonDown, msg, wp, lp, handled);
		if (::GetCapture() == this->m_hWnd)
			BeginDrag();
		return result;
	}

	LRESULT OnCaptureChanged(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
		bool ghost = m_Dragging && m_GhostDrag;
		m_Ghost.Hide();
		auto result = CallBase(&Base::OnCaptureChanged, msg, wp, lp, handled);
		if (ghost) {
			// dropped (or put back by Escape): the panes go where the ghost bar was
			this->SetSplitterPos(this->m_xySplitterPos, false);
			this->UpdateSplitterLayout();
			this->UpdateWindow();
		}
		if (m_Dragging) {
			m_Dragging = false;
			InvalidateBar();
			m_Hot = false;
			UpdateHot();
		}
		return result;
	}

	CSplitterGhostBar m_Ghost;
	SplitterColors m_Colors;
	SplitterBarStyle m_Style{ SplitterBarStyle::Line };
	UINT m_Dpi{ 96 };
	int m_BarWidth{ 0 };
	int m_LineWidth{ 1 }, m_HotLineWidth{ 3 };
	bool m_HotTracking{ true };
	bool m_Hot{ false }, m_Dragging{ false };
	bool m_TranslucentGhost{ true }, m_GhostDrag{ false };
	bool m_ForceHighContrast{ false };
};

using CCustomSplitterWindow = CCustomSplitterWindowT<true>;
using CCustomHorSplitterWindow = CCustomSplitterWindowT<false>;
