#pragma once

class CCustomTabControl : public CWindowImpl<CCustomTabControl, CTabCtrl> {
public:
	BEGIN_MSG_MAP(CCustomTabContro)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (ThemeHelper::IsDefault()) {
			bHandled = FALSE;
			return 0;
		}

		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
		return 1;
	}

};

struct CCustomTabControlParent :
	CWindowImpl<CCustomTabControlParent>,
	COwnerDraw<CCustomTabControlParent> {
	BEGIN_MSG_MAP(CCustomTabControlParent)
		CHAIN_MSG_MAP(COwnerDraw<CCustomTabControlParent>)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override {
		delete this;
	}

	void MeasureItem(LPMEASUREITEMSTRUCT mis) {
		if (mis->CtlType != ODT_TAB) {
			SetMsgHandled(FALSE);
			return;
		}
		// not called with a dialog box
		mis->itemWidth = 120;
	}

	void DrawItem(LPDRAWITEMSTRUCT dis) {
		if (dis->hwndItem != m_Tab) {
			SetMsgHandled(FALSE);
			return;
		}
		CDCHandle dc(dis->hDC);
		auto& rc(dis->rcItem);
		auto theme = ThemeHelper::GetCurrentTheme();
		TCITEM tci;
		WCHAR text[32];
		tci.mask = TCIF_STATE | TCIF_TEXT | TCIF_IMAGE;
		tci.dwStateMask = TCIS_HIGHLIGHTED;
		tci.pszText = text;
		tci.cchTextMax = _countof(text);
		ATLVERIFY(m_Tab.GetItem(dis->itemID, &tci));

		dc.SelectFont(m_Tab.GetFont());
		dc.FillSolidRect(&rc, theme->BackColor);
		dc.SetTextColor(theme->TextColor);
		dc.SetBkMode(TRANSPARENT);
		int space = 0;
		if (tci.iImage >= 0) {
			CSize size;
			::GetTextExtentPoint32(dc.m_hDC, tci.pszText, (int)wcslen(tci.pszText), &size);
			space = 12;
			CRect irc(rc);
			irc.top += 4;
			irc.bottom = irc.top + 16;
			irc.left += (irc.Width() - size.cx) / 2 - space;
			irc.right = irc.left + 16;

			m_Tab.GetImageList().DrawEx(tci.iImage, dc.m_hDC, irc, CLR_NONE, CLR_NONE, ILD_NORMAL);
			rc.left += space * 2;
			rc.top += 2;
		}
		dc.DrawText(tci.pszText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}

	void Init(HWND hWnd) {
		m_Tab.SubclassWindow(hWnd);
		m_Tab.ModifyStyle(0, TCS_OWNERDRAWFIXED);
	}

	CCustomTabControl m_Tab;
};

