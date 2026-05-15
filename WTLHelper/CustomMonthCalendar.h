#pragma once

#include "DarkMode/DarkModeSubclass.h"

//
// MonthCalendar (SysMonthCal32) does not have a DarkMode_* visual style.
// Killing the theme with SetWindowTheme(hwnd, L" ", L" ") forces the control
// back to classic GDI painting, which honors MCM_SETCOLOR for every slot.
//
class CCustomMonthCalendar : public CWindowImpl<CCustomMonthCalendar, CMonthCalendarCtrl> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init() {
		::SetWindowTheme(m_hWnd, L" ", L" ");
		ApplyColors();
	}

	BEGIN_MSG_MAP(CCustomMonthCalendar)
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
		ApplyColors();
		bHandled = FALSE;
		return 0;
	}

private:
	void ApplyColors() {
		auto back = DarkMode::getBackgroundColor();
		auto text = DarkMode::getTextColor();
		auto titleBack = DarkMode::getCtrlBackgroundColor();
		auto dimmed = DarkMode::getDisabledTextColor();
		SetColor(MCSC_BACKGROUND, back);
		SetColor(MCSC_MONTHBK, back);
		SetColor(MCSC_TEXT, text);
		SetColor(MCSC_TITLEBK, titleBack);
		SetColor(MCSC_TITLETEXT, text);
		SetColor(MCSC_TRAILINGTEXT, dimmed);
	}
};
