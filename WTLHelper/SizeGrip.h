#pragma once

#include "ThemeHelper.h"
#include "Theme.h"
#include "WTLHelper.h"

//
// size grip (SBS_SIZEGRIP scroll bar) that follows the colors of the application:
// in dark mode it's drawn in dark mode colors (dark mode leaves a size grip light),
// with a custom ThemeHelper theme in the theme's status bar colors; otherwise the system draws it.
// Use CreateGrip to create one at the parent's bottom-right corner, or SubclassWindow an existing grip
// (the ThemeHelper hook subclasses grips with a CSizeGrip created with new and autoDelete).
// The parent is responsible for moving it when it resizes.
//
class CSizeGrip : public CWindowImpl<CSizeGrip, CScrollBar> {
public:
	// autoDelete: the object deletes itself when its window is destroyed (for an object created with new)
	explicit CSizeGrip(bool autoDelete = false) : m_AutoDelete(autoDelete) {}

	BEGIN_MSG_MAP(CSizeGrip)
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

	void OnFinalMessage(HWND) override {
		if (m_AutoDelete)
			delete this;
	}

	// draws in the ThemeHelper theme's status bar colors
	static void DrawSizeGrip(CWindow win, CRect& rc) {
		CClientDC dc(win);
		DrawSizeGrip(dc.m_hDC, rc);
		win.ValidateRect(nullptr);
	}

	static void DrawSizeGrip(CDCHandle dc, CRect& rc) {
		auto theme = ThemeHelper::GetCurrentTheme();
		ATLASSERT(theme);
		DrawSizeGrip(dc, rc, theme->StatusBar.BackColor, theme->StatusBar.TextColor, 96);
	}

	//
	// 2x2 dots, 4 pixels apart, on the three lower-right diagonals (at 96 DPI)
	//
	static void DrawSizeGrip(CDCHandle dc, CRect const& rc, COLORREF back, COLORREF dots, UINT dpi) {
		dc.FillSolidRect(&rc, back);
		int dot = ::MulDiv(2, dpi, 96), step = ::MulDiv(4, dpi, 96), margin = ::MulDiv(2, dpi, 96);
		CPoint origin(rc.right - margin - 3 * step + step - dot, rc.bottom - margin - 3 * step + step - dot);
		for (int y = 0; y < 3; y++) {
			for (int x = 0; x < 3; x++) {
				if (x + y < 2)
					continue;
				CRect rcDot(CPoint(origin.x + x * step, origin.y + y * step), CSize(dot, dot));
				dc.FillSolidRect(&rcDot, dots);
			}
		}
	}

private:
	enum class Look { System, DarkMode, Theme };

	Look GetLook() const {
		if ((GetStyle() & (SBS_SIZEBOX | SBS_SIZEGRIP)) == 0)
			return Look::System;
		if (WTLHelper::IsDarkMode())
			return Look::DarkMode;
		return ThemeHelper::IsDefault() ? Look::System : Look::Theme;
	}

	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		bHandled = GetLook() != Look::System;
		return 1;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		auto look = GetLook();
		if (look == Look::System) {
			bHandled = FALSE;
			return 0;
		}
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		if (look == Look::DarkMode)
			DrawSizeGrip(dc.m_hDC, rc, DarkMode::getDlgBackgroundColor(), DarkMode::getDisabledTextColor(), ::GetDpiForWindow(m_hWnd));
		else
			DrawSizeGrip(dc.m_hDC, rc);
		return 0;
	}

	bool m_AutoDelete;
};
