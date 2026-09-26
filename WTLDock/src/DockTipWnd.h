#pragma once

#include "DockHost.h"

namespace WTLDock {

// The tooltip of the docking chrome (tabs, caption, caption buttons). Drawn in the theme, follows the DPI of what it
// describes, never takes the mouse or the focus. Created and owned by CDockHost; not for direct use.
class CDockTipWnd : public ATL::CWindowImpl<CDockTipWnd, ATL::CWindow,
	ATL::CWinTraits<WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT>> {
public:
	DECLARE_WND_CLASS_EX(L"WTLDock_Tip", CS_DROPSHADOW, 0)

	explicit CDockTipWnd(CDockHost& host) : m_Host(host) {
	}

	// Shows 'text' near 'target' (screen coordinates): below it, or above if there is no room. Sized for 'dpi'.
	void Show(HWND owner, const std::wstring& text, const RECT& target, int dpi);
	void Hide();
	bool IsShown() const {
		return m_Shown;
	}
	// where it is (screen coordinates)
	RECT Rect() const;

	BEGIN_MSG_MAP(CDockTipWnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
	END_MSG_MAP()

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) {
		return HTTRANSPARENT;
	}
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	void Draw(HDC hdc);

	CDockHost& m_Host;
	std::wstring m_Text;
	int m_Dpi{ 96 };
	bool m_Shown{};
};

}
