#pragma once

#include "ThemeHelper.h"

class CCustomRebar : public CWindowImpl<CCustomRebar, CReBarCtrl> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomRebar)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnColorEdit)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
	END_MSG_MAP()
	
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& bHandled) {
		CDCHandle dc((HDC)wp);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, ::GetSysColorBrush(COLOR_WINDOW));
		return 1;
	}

	LRESULT OnColorEdit(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/) {
		auto theme = ThemeHelper::GetCurrentTheme();

		CDCHandle dc((HDC)wp);
		dc.SetBkMode(OPAQUE);
		dc.SetTextColor(theme->TextColor);
		dc.SetBkColor(theme->BackColor);
		return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
	}

	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/) {
		REBARBANDINFO info = { sizeof(info) };
		info.fMask = RBBIM_COLORS;
		info.clrBack = ::GetSysColor(COLOR_WINDOW);
		info.clrFore = ::GetSysColor(COLOR_WINDOWTEXT);

		int count = GetBandCount();
		for(int i = 0; i < count; i++)
			SetBandInfo(i, &info);
		return 0;
	}
};
