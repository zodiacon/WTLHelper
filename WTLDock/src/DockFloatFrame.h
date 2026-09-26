#pragma once

#include "DockHost.h"
#include "SplitterTracker.h"

namespace WTLDock {

inline constexpr DWORD CDockFloatFrame_Style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
inline constexpr DWORD CDockFloatFrame_ExStyle = WS_EX_TOOLWINDOW;

// The window of a floating layout tree: an owned tool window with a native title bar whose client area holds the
// group windows of the tree. Created and owned by CDockHost; not for direct use.
class CDockFloatFrame : public ATL::CWindowImpl<CDockFloatFrame, ATL::CWindow, ATL::CWinTraits<CDockFloatFrame_Style, CDockFloatFrame_ExStyle>> {
public:
	static constexpr DWORD Style = CDockFloatFrame_Style;
	static constexpr DWORD ExStyle = CDockFloatFrame_ExStyle;

	DECLARE_WND_CLASS_EX(L"WTLDock_Float", CS_DBLCLKS, 0)

	CDockFloatFrame(CDockHost& host, int id) : m_Host(host), m_Id(id) {
	}

	int Id() const {
		return m_Id;
	}
	void SetTitle(const std::wstring& title);
	// follows the theme (dark title bar)
	void ApplyTheme();
	// the frame is on its way out: it stops talking to the layout
	void Retire();

	void OnFinalMessage(HWND) override {
		m_Host.ForgetFrame(this);
		delete this;
	}

	BEGIN_MSG_MAP(CDockFloatFrame)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_WINDOWPOSCHANGED, OnWindowPosChanged)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK, OnNcLButtonDblClk)
		MESSAGE_HANDLER(WM_NCLBUTTONDOWN, OnNcLButtonDown)
		MESSAGE_HANDLER(WM_ENTERSIZEMOVE, OnEnterSizeMove)
		MESSAGE_HANDLER(WM_MOVING, OnMoving)
		MESSAGE_HANDLER(WM_EXITSIZEMOVE, OnExitSizeMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
	END_MSG_MAP()

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnWindowPosChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetMinMaxInfo(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNcLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNcLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEnterSizeMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMoving(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnExitSizeMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);

	void Draw(HDC hdc, RECT clip);

	CDockHost& m_Host;
	int m_Id;
	bool m_Retired{};
	bool m_MoveCandidate{};		// the title bar was pressed: the coming move loop is a move, not a resize
	bool m_Moving{};			// moving the window by its title bar with the drop guides on
	SplitterTracker m_Splitters;
};

}
