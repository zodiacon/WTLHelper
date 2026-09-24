#pragma once

#include "DarkMode/DarkModeSubclass.h"

//
// MonthCalendar (SysMonthCal32) does not have a DarkMode_* visual style.
// Verified on Windows 11 build 28120: the MONTHCAL theme class resolves to the
// same light colors under every app name (Explorer, DarkMode_Explorer,
// DarkMode_DarkTheme, DarkMode_CFD, DarkMode_ItemsView), so no SetWindowTheme
// name can darken it.
//
// The only way to recolor it is to kill visual styles with
// SetWindowTheme(hwnd, L"", L""), which forces the control back to classic GDI
// painting; only then are MCM_SETCOLOR slots honored. Order matters: a themed
// control ignores MCM_SETCOLOR entirely.
//
// Side effect: the prev/next arrows revert to classic 3D buttons.
//
class CCustomMonthCalendar : public CWindowImpl<CCustomMonthCalendar, CMonthCalendarCtrl> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init() {
		ApplyTheme();
	}

	BEGIN_MSG_MAP(CCustomMonthCalendar)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (!DarkMode::isEnabled()) {
			bHandled = FALSE;
			return 0;
		}
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, DarkMode::getBackgroundBrush());
		return 1;
	}

	LRESULT OnThemeChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
		// SetWindowTheme itself sends WM_THEMECHANGED, so guard against recursion.
		if (!m_Applying)
			ApplyTheme();
		bHandled = FALSE;
		return 0;
	}

private:
	void ApplyTheme() {
		m_Applying = true;
		if (DarkMode::isEnabled()) {
			::SetWindowTheme(m_hWnd, L"", L"");	// strip visual styles first
			ApplyDarkColors();
		}
		else {
			// Restore the themed look; a themed control ignores the stale
			// MCM_SETCOLOR values, so they need no resetting.
			::SetWindowTheme(m_hWnd, nullptr, nullptr);
		}
		m_Applying = false;
	}

	void ApplyDarkColors() {
		auto const back = DarkMode::getBackgroundColor();
		auto const text = DarkMode::getTextColor();
		SetColor(MCSC_BACKGROUND, back);
		SetColor(MCSC_MONTHBK, back);
		SetColor(MCSC_TEXT, text);
		SetColor(MCSC_TITLEBK, DarkMode::getCtrlBackgroundColor());
		SetColor(MCSC_TITLETEXT, text);
		SetColor(MCSC_TRAILINGTEXT, DarkMode::getDisabledTextColor());
	}

	bool m_Applying{ false };
};
