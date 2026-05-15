#pragma once

#include "DarkMode/DarkModeSubclass.h"

//
// DateTimePicker (SysDateTimePick32) has no DarkMode_* visual style.
// Killing the theme with SetWindowTheme(hwnd, L" ", L" ") forces classic
// painting so the control body can be filled dark via WM_ERASEBKGND.
// The dropdown popup calendar is created as a separate SysMonthCal32
// window; the per-thread WH_CALLWNDPROCRET hook in WTLHelper.cpp catches
// its WM_CREATE and subclasses it with CCustomMonthCalendar.
//
class CCustomDateTimePicker : public CWindowImpl<CCustomDateTimePicker, CDateTimePickerCtrl> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init() {
		::SetWindowTheme(m_hWnd, L" ", L" ");
	}

	BEGIN_MSG_MAP(CCustomDateTimePicker)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, DarkMode::getBackgroundBrush());
		return 1;
	}

	LRESULT OnThemeChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
		::SetWindowTheme(m_hWnd, L" ", L" ");
		bHandled = FALSE;
		return 0;
	}
};
