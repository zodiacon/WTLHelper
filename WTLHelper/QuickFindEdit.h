#pragma once

const UINT EN_DELAYCHANGE = EN_CHANGE + 1;
class CQuickFindEdit : public CWindowImpl<CQuickFindEdit, CEdit> {
public:
	BEGIN_MSG_MAP(CQuickFindEdit)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_SETFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	~CQuickFindEdit() {
		if (m_hIcon)
			::DestroyIcon(m_hIcon);
	}

	void SetWatermark(PCWSTR watermark) {
		m_Watermark = watermark;
	}

	void SetTextChangeDelay(UINT ms) {
		m_Delay = ms;
	}

	void SetWatermarkIcon(HICON hIcon) {
		m_hIcon = hIcon;
	}

	LRESULT OnGetDlgCode(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		return wParam == VK_TAB ? 0 : DLGC_WANTALLKEYS;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == 1) {
			KillTimer(1);
			GetParent().SendMessage(WM_COMMAND, MAKEWPARAM(GetWindowLongPtr(GWL_ID), EN_DELAYCHANGE), reinterpret_cast<LPARAM>(m_hWnd));
		}
		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		DefWindowProc();
		if (GetWindowTextLength() == 0 && !m_Watermark.IsEmpty() && ::GetFocus() != m_hWnd) {
			CClientDC dc(m_hWnd);
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(m_WatermarkColor);
			dc.SelectFont(GetFont());
			CRect rc;
			GetClientRect(&rc);
			rc.left += 4;
			if (m_hIcon) {
				dc.DrawIconEx(rc.left, rc.Height() / 2 - 8, m_hIcon, 16, 16);
				rc.left += 20;
			}
			dc.DrawText(m_Watermark, -1, &rc, DT_VCENTER | DT_SINGLELINE);
		}
		return 0;
	}

	LRESULT OnKillFocus(UINT msg, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (!m_Watermark.IsEmpty())
			Invalidate();
		if (msg == WM_SETFOCUS)
			SetSelAll();
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == VK_ESCAPE)
			SetWindowText(L"");
		else
			bHandled = FALSE;
		SetTimer(1, m_Delay);
		return 0;
	}

private:
	CString m_Watermark;
	COLORREF m_WatermarkColor{ RGB(128, 128, 128) };
	UINT m_Delay{ 250 };
	HICON m_hIcon{ nullptr };
};
