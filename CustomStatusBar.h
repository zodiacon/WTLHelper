#pragma once

#include "ThemeHelper.h"
#include "SizeGrip.h"

class CCustomStatusBar : 
	public CWindowImpl<CCustomStatusBar, CStatusBarCtrl>,
	public COwnerDraw<CCustomStatusBar> {
public:
	BEGIN_MSG_MAP(CCustomStatusBar)
		MESSAGE_HANDLER(SB_SETTEXT, OnSetText)
		MESSAGE_HANDLER(SB_GETTEXT, OnGetText)
		MESSAGE_HANDLER(SB_GETTEXTLENGTH, OnGetTextLength)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		CHAIN_MSG_MAP_ALT(COwnerDraw<CCustomStatusBar>, 1)
	END_MSG_MAP()

	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init(HWND hWnd, LPCREATESTRUCT cs) {
		SubclassWindow(hWnd);
		m_Text.push_back(cs->lpszName);
	}

	LRESULT OnGetTextLength(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		if (wParam >= m_Text.size())
			return SBT_OWNERDRAW;

		return (WORD)m_Text[wParam].length() | SBT_OWNERDRAW;
	}

	LRESULT OnGetText(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		if (wParam >= m_Text.size()) {
			*(PWSTR)lParam = 0;
			return 0;
		}
		wcscpy_s((PWSTR)lParam, m_Text[wParam].length() + 1, m_Text[wParam].c_str());
		return 0;
	}

	LRESULT OnSetText(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		auto pane = LOBYTE(wParam);
		bool simple = pane == SB_SIMPLEID;
		if (simple) {
			wParam = pane = 0;
		}
		m_Text.resize(pane + 1);
		m_Text[pane] = (PCWSTR)lParam;
		DefWindowProc(uMsg, wParam | SBT_OWNERDRAW, lParam);
		if(simple)
			Invalidate();

		return TRUE;
	}

	void DrawItem(LPDRAWITEMSTRUCT dis) {
		if (dis->hwndItem != m_hWnd) {
			SetMsgHandled(FALSE);
			return;
		}
		CDCHandle dc(dis->hDC);
		dc.SetTextColor(ThemeHelper::GetCurrentTheme()->TextColor);
		dc.SetBkMode(TRANSPARENT);
		SetMsgHandled(FALSE);
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		auto theme = ThemeHelper::GetCurrentTheme();
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, theme->BackColor);
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM, LPARAM, BOOL& bHandled) {
		auto theme = ThemeHelper::GetCurrentTheme();
		CRect rc;
		GetClientRect(&rc);
		if (m_Text.size() == 1) {
			CPaintDC dc(m_hWnd);
			dc.SelectFont(GetFont());
			dc.SetTextColor(ThemeHelper::GetCurrentTheme()->TextColor);
			dc.SetBkMode(TRANSPARENT);
			::DrawStatusText(dc.m_hDC, &rc, m_Text[0].c_str(), 0);
		}
		else {
			DefWindowProc();
		}		
		rc.left = rc.right - 20;
		rc.top = rc.bottom - 20;
		CSizeGrip::DrawSizeGrip(*this, rc);

		return 0;
	}

	std::vector<std::wstring> m_Text;
};

class CCustomStatusBarParent :
	public CWindowImpl<CCustomStatusBarParent>,
	public COwnerDraw<CCustomStatusBarParent> {

	BEGIN_MSG_MAP(CCustomStatusBarParent)
		CHAIN_MSG_MAP(COwnerDraw<CCustomStatusBarParent>)
	END_MSG_MAP()

	void DrawItem(LPDRAWITEMSTRUCT dis) {
		if (dis->hwndItem != m_sb) {
			SetMsgHandled(FALSE);
			return;
		}
		CDCHandle dc(dis->hDC);
		dc.SetTextColor(ThemeHelper::GetCurrentTheme()->TextColor);
		dc.SetBkMode(TRANSPARENT);
		int type;
		CString text;
		m_sb.GetText(dis->itemID, text, &type);
		::DrawStatusText(dc.m_hDC, &dis->rcItem, text, 0);
	}

	void MeasureItem(LPMEASUREITEMSTRUCT ) {
		SetMsgHandled(FALSE);
	}

	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init(HWND hWnd, HWND hParent) {
		SubclassWindow(hParent);
		m_sb.Attach(hWnd);
	}

	CStatusBarCtrl m_sb;
};

