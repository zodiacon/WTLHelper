#pragma once

#include "ColorPicker.h"

// ---- The eyedropper's magnifier --------------------------------------------------------
//
// A popup next to the mouse showing the pixels around it, zoomed, with the one under it framed and its hex
// value below. Layered and transparent to the mouse, so it never gets in the way of the pick; it's away from
// the pixels it shows, so it's never in them.

class CpMagnifier : public ATL::CWindowImpl<CpMagnifier, ATL::CWindow,
	ATL::CWinTraits<WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT>> {
public:
	DECLARE_WND_CLASS_EX(L"CpMagnifier", 0, -1)

	enum { Pixels = 11 };   // the square shown; odd, so the pixel under the mouse is in the middle

	~CpMagnifier();

	BEGIN_MSG_MAP(CpMagnifier)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
	END_MSG_MAP()

	// takes the pixels around pt (screen coords), places itself beside it and shows them
	void     Show(POINT pt, UINT dpi, const CPKCOLORS& colors, HFONT font);
	COLORREF GetColor() const;   // the pixel under the mouse

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&);

	HBITMAP   m_hBits = nullptr;   // a DIB section of Pixels x Pixels
	uint32_t* m_Bits = nullptr;
	UINT      m_Dpi = 96;
	CPKCOLORS m_Colors{};
	HFONT     m_hFont = nullptr;
};
