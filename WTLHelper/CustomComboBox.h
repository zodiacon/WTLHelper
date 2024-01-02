#pragma once

#include "ThemeHelper.h"

class CCustomComboBox : public CWindowImpl<CCustomComboBox, CComboBox> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomComboBox)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
		return 1;
	}
};

class CCustomComboLBox : public CWindowImpl<CCustomComboLBox> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomComboLBox)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
		return 1;
	}
};
