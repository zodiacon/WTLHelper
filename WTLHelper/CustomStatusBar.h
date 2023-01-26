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
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		//MESSAGE_HANDLER(WM_PAINT, OnPaint)
		CHAIN_MSG_MAP_ALT(COwnerDraw<CCustomStatusBar>, 0)
	END_MSG_MAP()

	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/) {
		auto theme = reinterpret_cast<Theme*>(lParam);
		SetBkColor(theme->StatusBar.BackColor);

		return 0;
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

		return TRUE;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		auto theme = ThemeHelper::GetCurrentTheme();
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, theme->StatusBar.BackColor);
		//rc.left = rc.right - 20;
		//rc.top = rc.bottom - 20;
		//CSizeGrip::DrawSizeGrip(dc, rc);
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM, LPARAM, BOOL& bHandled) {
		auto theme = ThemeHelper::GetCurrentTheme();
		CRect rc;
		GetClientRect(&rc);
		if (m_Text.size() == 1 && !m_Text[0].empty()) {
			CPaintDC dc(m_hWnd);
			dc.SelectFont(GetFont());
			dc.SetTextColor(theme->StatusBar.TextColor);
			dc.SetBkMode(TRANSPARENT);
			rc.left += 2;
			dc.DrawText(m_Text[0].c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			rc.left -= 2;
			dc.SelectStockPen(DC_PEN);
			dc.SetDCPenColor(theme->StatusBar.TextColor);
			dc.MoveTo(0, 0); dc.LineTo(rc.right, 0);
		}
		else {
			DefWindowProc();
		}
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
		auto hIcon = m_sb.GetIcon(dis->itemID);
		if (hIcon) {
			auto pt = CRect(dis->rcItem).CenterPoint();
			dc.DrawIconEx(dis->rcItem.left + 2, pt.y - 8, hIcon, 16, 16);
		}
		else {
			dc.SetTextColor(ThemeHelper::GetCurrentTheme()->StatusBar.TextColor);
			dc.SetBkMode(OPAQUE);
			dc.SetBkColor(ThemeHelper::GetCurrentTheme()->StatusBar.BackColor);
			int type;
			CString text;
			m_sb.GetText(dis->itemID, text, &type);
			dc.DrawText(L" " + text, -1, &dis->rcItem, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
		}
	}

	void MeasureItem(LPMEASUREITEMSTRUCT ) {
		SetMsgHandled(FALSE);
	}

	void OnFinalMessage(HWND) override {
		delete this;
	}

	void Init(HWND hWnd, HWND hParent) {
		SubclassWindow(hParent);
		m_sb.SubclassWindow(hWnd);
	}

	CCustomStatusBar m_sb;
};

