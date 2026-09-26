#pragma once

#include "DockHost.h"

namespace WTLDock {

// The window switcher shown by Ctrl+Tab. It never takes the focus: the keys go to the host's PreTranslateMessage.
// Created and owned by CDockHost; not for direct use.
class CDockNavigatorWnd : public ATL::CWindowImpl<CDockNavigatorWnd, ATL::CWindow,
	ATL::CWinTraits<WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE>> {
public:
	DECLARE_WND_CLASS_EX(L"WTLDock_Navigator", CS_DROPSHADOW, 0)

	explicit CDockNavigatorWnd(CDockHost& host) : m_Host(host) {
	}

	// Shows the window centred over the owner, sized for the lists of the host's navigator.
	void Open(HWND owner, bool watchControlKey);
	void Close();
	// the selection or the lists changed
	void Refresh();
	// where a row is (client coordinates); empty if it is not visible
	RECT RowRect(DockNavigator::Column column, int row) const;
	const NavigatorLayout& Layout() const {
		return m_Layout;
	}

	BEGIN_MSG_MAP(CDockNavigatorWnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
	END_MSG_MAP()

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);

	bool RowAt(POINT pt, DockNavigator::Column& column, int& row) const;
	void ComputeLayout();
	void Draw(HDC hdc);
	static std::wstring Describe(const DockPane& pane);

	CDockHost& m_Host;
	NavigatorLayout m_Layout;
	int m_First[2]{};
	bool m_WatchControl{};
	POINT m_LastMouse{ -1, -1 };
};

}
