#pragma once

class CCustomToolBar : public CWindowImpl<CCustomToolBar, CToolBarCtrl> {
public:
	void OnFinalMessage(HWND) override {
	}

	BEGIN_MSG_MAP(CCustomRebar)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
//		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& bHandled) {
		CDCHandle dc((HDC)wp);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, ::GetSysColorBrush(COLOR_WINDOW));
		return 1;
	}

};

class CCustomToolBarParent :
	public CWindowImpl<CCustomToolBarParent>,
	public CCustomDraw<CCustomToolBarParent> {

	BEGIN_MSG_MAP(CCustomToolBarParent)
		CHAIN_MSG_MAP(CCustomDraw<CCustomToolBarParent>)
	END_MSG_MAP()

	DWORD OnPrePaint(int, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_ToolBar) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}
		return CDRF_NOTIFYITEMDRAW;
	}

	DWORD OnItemPrePaint(int, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_ToolBar) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}
		auto tb = (NMTBCUSTOMDRAW*)cd;
		tb->clrText = ThemeHelper::GetCurrentTheme()->TextColor;
		tb->clrBtnHighlight = ::GetSysColor(COLOR_BTNHIGHLIGHT);
	
		return TBCDRF_USECDCOLORS;
	}

	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init(HWND tb) {
		SubclassWindow(::GetParent(tb));
		m_ToolBar.Attach(tb);
	}

	CToolBarCtrl m_ToolBar;
};

