#pragma once

#include "DarkMode/DarkModeSubclass.h"

class CCustomDateTimePicker : public CWindowImpl<CCustomDateTimePicker, CDateTimePickerCtrl> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomDateTimePicker)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnColorEdit)
		//MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, DarkMode::getBackgroundBrush());

		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		CPaintDC dc(m_hWnd);
		dc.SelectBrush(DarkMode::getBackgroundBrush());

		return DefWindowProcW();
	}

	LRESULT OnColorEdit(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		return 0;
	}
};
