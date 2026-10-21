#pragma once

#include "DpiHelper.h"

const UINT EN_DELAYCHANGE = EN_CHANGE + 1;

class CQuickFindEdit : public CWindowImpl<CQuickFindEdit, CEdit> {
public:
	BEGIN_MSG_MAP(CQuickFindEdit)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_HOTKEY, OnHotKey)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
	END_MSG_MAP()

	~CQuickFindEdit() {
		if (m_hIcon)
			::DestroyIcon(m_hIcon);
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
			UINT dpi = DpiHelper::GetDpi(m_hWnd);
			rc.left += DpiHelper::Scale(2, dpi);
			if (m_hIcon) {
				int size = DpiHelper::SmallIconSize(dpi);
				dc.DrawIconEx(rc.left, (rc.Height() - size) / 2, m_hIcon, size, size);
				rc.left += size + DpiHelper::Scale(4, dpi);
			}
			dc.DrawText(m_Watermark, -1, &rc, DT_VCENTER | DT_SINGLELINE | DT_LEFT);
		}
		return 0;
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
		Invalidate();
	}

	void SetTextChangeDelay(UINT ms) {
		m_Delay = ms;
	}

	// drawn in the small icon size of the DPI of the edit box (stretched if it's another size)
	void SetWatermarkIcon(HICON hIcon) {
		m_hIcon = hIcon;
		Invalidate();
	}

	LRESULT OnGetDlgCode(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		return wParam == VK_TAB ? 0 : DLGC_WANTALLKEYS;
	}

	LRESULT OnHotKey(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == 1)
			SetFocus();
		return 0;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == 1) {
			KillTimer(1);
			GetParent().SendMessage(WM_COMMAND, MAKEWPARAM(GetWindowLongPtr(GWL_ID), EN_DELAYCHANGE), reinterpret_cast<LPARAM>(m_hWnd));
		}
		return 0;
	}

	LRESULT OnSetFocus(UINT msg, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		SetSelAll();
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnKillFocus(UINT msg, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		Invalidate();
	
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
