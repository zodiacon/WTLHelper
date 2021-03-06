#pragma once

#pragma once

#include "ThemeHelper.h"

class CCustomButtonParent :
	public CWindowImpl<CCustomButtonParent>,
	public CCustomDraw<CCustomButtonParent> {
public:
	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CCustomButtonParent)
		CHAIN_MSG_MAP(CCustomDraw<CCustomButtonParent>)
	END_MSG_MAP()

	DWORD OnPreErase(int, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_hWnd) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}
		return ThemeHelper::IsDefault() ? CDRF_DODEFAULT : CDRF_NOTIFYPOSTERASE;
	}

	DWORD OnPostErase(int, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_hWnd) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}
		CDCHandle dc(cd->hdc);
		auto theme = ThemeHelper::GetCurrentTheme();
		dc.FillSolidRect(&cd->rc, (cd->uItemState & (CDIS_DISABLED | CDIS_GRAYED)) ? ::GetSysColor(COLOR_GRAYTEXT) : theme->BackColor);
		dc.DrawEdge(&cd->rc, (cd->uItemState & CDIS_SELECTED) ? EDGE_SUNKEN : EDGE_BUMP, BF_RECT);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(theme->TextColor);

		return CDRF_DODEFAULT;
	}

	void Init(HWND hWnd) {
		m_hWnd = hWnd;
		ATLVERIFY(SubclassWindow(::GetParent(hWnd)));
	}

	HWND m_hWnd;
};
