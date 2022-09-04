#pragma once

#include "ThemeHelper.h"
#include "Theme.h"

const UINT EN_DELAYCHANGE = EN_CHANGE + 1;

class CQuickFindEdit : public CWindowImpl<CQuickFindEdit, CEdit> {
public:
	BEGIN_MSG_MAP(CQuickFindEdit)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_HOTKEY, OnHotKey)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_SETFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnDialogColor)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnDialogColor)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	~CQuickFindEdit() {
		if (m_hIcon)
			::DestroyIcon(m_hIcon);
	}

	bool SetHotKey(UINT modifiers, UINT virtKey) {
		if (m_HotKeyId)
			::UnregisterHotKey(m_hWnd, 1);
		auto ok = ::RegisterHotKey(m_hWnd, 1, modifiers, virtKey);
		if (ok) {
			m_HotKeyId = 1;
		}
		return ok;
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

	LRESULT OnHotKey(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == 1)
			SetFocus();
		return 0;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		CDCHandle dc((HDC)wParam);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
		return 1;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == 1) {
			KillTimer(1);
			GetParent().SendMessage(WM_COMMAND, MAKEWPARAM(GetWindowLongPtr(GWL_ID), EN_DELAYCHANGE), reinterpret_cast<LPARAM>(m_hWnd));
		}
		return 0;
	}

	LRESULT OnDialogColor(UINT, WPARAM, LPARAM, BOOL&) {
		return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (GetWindowTextLength() == 0 && !m_Watermark.IsEmpty() && ::GetFocus() != m_hWnd) {
			CPaintDC dc(m_hWnd);
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
		else {
			DefWindowProc();
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
	UINT m_HotKeyId{ 0 };
};
