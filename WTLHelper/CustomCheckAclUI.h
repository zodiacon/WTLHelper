#pragma once

#include "DarkMode/DarkModeSubclass.h"

//
// CHECK_ACLUI is the permissions checklist control hosted by the ACLUI
// security property sheet (EditSecurity). Its window class background is
// a system color index, which bypasses the GetSysColorBrush hook, so it
// stays light in dark mode. Its checkbox and label children also ask it
// (not the dialog) for their colors via WM_CTLCOLOR*.
//
class CCustomCheckAclUI : public CWindowImpl<CCustomCheckAclUI> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomCheckAclUI)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColor)
		MESSAGE_HANDLER(WM_CTLCOLORBTN, OnCtlColor)
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

	LRESULT OnCtlColor(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (!DarkMode::isEnabled()) {
			bHandled = FALSE;
			return 0;
		}
		CDCHandle dc((HDC)wParam);
		dc.SetTextColor(DarkMode::getTextColor());
		dc.SetBkColor(DarkMode::getBackgroundColor());
		return (LRESULT)DarkMode::getBackgroundBrush();
	}
};
