#pragma once

class CCustomToolBarParent :
	public CWindowImpl<CCustomToolBarParent>,
	public CCustomDraw<CCustomToolBarParent> {

	DECLARE_EMPTY_MSG_MAP()

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
		::SetWindowTheme(tb, L" ", nullptr);
	}

	CToolBarCtrl m_ToolBar;
};

