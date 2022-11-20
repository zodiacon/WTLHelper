#pragma once

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

		return CDRF_DODEFAULT | TBCDRF_USECDCOLORS;
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

