#pragma once

#include "WTLHelper.h"

//
// size grip (SBS_SIZEGRIP scroll bar) that follows dark mode.
// Dark mode leaves a size grip light, so in dark mode it is painted in dark mode colors;
// otherwise the system draws it.
// Use CreateGrip to create one at the parent's bottom-right corner, or SubclassWindow an existing grip.
// The parent is responsible for moving it when it resizes.
//
class CDarkModeSizeGrip : public CWindowImpl<CDarkModeSizeGrip, CScrollBar> {
public:
	BEGIN_MSG_MAP(CDarkModeSizeGrip)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	HWND CreateGrip(HWND hParent) {
		CRect client;
		::GetClientRect(hParent, &client);
		auto cx = ::GetSystemMetrics(SM_CXVSCROLL), cy = ::GetSystemMetrics(SM_CYHSCROLL);
		CRect rc(client.right - cx, client.bottom - cy, client.right, client.bottom);
		CScrollBar grip;
		grip.Create(hParent, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_SIZEGRIP | SBS_SIZEBOXBOTTOMRIGHTALIGN);
		if (!grip || !SubclassWindow(grip))
			return nullptr;
		SetWindowPos(HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		return m_hWnd;
	}

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		bHandled = WTLHelper::IsDarkMode();
		return 1;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		if (!WTLHelper::IsDarkMode()) {
			bHandled = FALSE;
			return 0;
		}
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, DarkMode::getDlgBackgroundColor());

		//
		// 2x2 dots, 4 pixels apart, on the three lower-right diagonals (at 96 DPI)
		//
		auto dpi = ::GetDpiForWindow(m_hWnd);
		int dot = ::MulDiv(2, dpi, 96), step = ::MulDiv(4, dpi, 96), margin = ::MulDiv(2, dpi, 96);
		CPoint origin(rc.right - margin - 3 * step + step - dot, rc.bottom - margin - 3 * step + step - dot);
		auto color = DarkMode::getDisabledTextColor();
		for (int y = 0; y < 3; y++) {
			for (int x = 0; x < 3; x++) {
				if (x + y < 2)
					continue;
				CRect rcDot(CPoint(origin.x + x * step, origin.y + y * step), CSize(dot, dot));
				dc.FillSolidRect(&rcDot, color);
			}
		}
		return 0;
	}
};
