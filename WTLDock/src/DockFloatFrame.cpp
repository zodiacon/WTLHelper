#include "DockFloatFrame.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace WTLDock {

void CDockFloatFrame::SetTitle(const std::wstring& title) {
	if (!m_hWnd)
		return;
	wchar_t current[256]{};
	::GetWindowTextW(m_hWnd, current, _countof(current));
	if (title != current)
		::SetWindowTextW(m_hWnd, title.c_str());
}

void CDockFloatFrame::ApplyTheme() {
	if (!m_hWnd)
		return;
	const BOOL dark = m_Host.Theme().IsDark;
	::DwmSetWindowAttribute(m_hWnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE before 20H1 */, &dark, sizeof(dark));
	::DwmSetWindowAttribute(m_hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
	::SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	Invalidate(FALSE);
}

void CDockFloatFrame::Retire() {
	m_Retired = true;
	if (m_hWnd)
		ShowWindow(SW_HIDE);
}

//
// painting
//

LRESULT CDockFloatFrame::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CDockFloatFrame::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		RECT client;
		GetClientRect(&client);
		Draw((HDC)wp, client);
		return 0;
	}
	CPaintDC paintDC(m_hWnd);
	Draw(paintDC.m_hDC, paintDC.m_ps.rcPaint);
	return 0;
}

void CDockFloatFrame::Draw(HDC hdc, RECT clip) {
	CMemoryDC dc(hdc, clip);
	RECT client;
	GetClientRect(&client);
	dc.FillSolidRect(&client, m_Host.Theme().Workspace);
	if (auto root = m_Host.FloatRoot(m_Id); root && !m_Retired)
		m_Splitters.Paint(dc.m_hDC, *root, m_Host.Theme());
}

//
// window messages
//

LRESULT CDockFloatFrame::OnSize(UINT, WPARAM wp, LPARAM, BOOL&) {
	if (!m_Retired && wp != SIZE_MINIMIZED)
		m_Host.Sync();
	return 0;
}

LRESULT CDockFloatFrame::OnDpiChanged(UINT, WPARAM wp, LPARAM lp, BOOL&) {
	// the window has been dragged to a monitor with another DPI
	if (!m_Retired)
		m_Host.SetFloatDpi(m_Id, HIWORD(wp), reinterpret_cast<const RECT*>(lp));
	return 0;
}

LRESULT CDockFloatFrame::OnWindowPosChanged(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	// (windows that show, hide or stack above each other get these without moving: what the window has is then
	// no news, and the layout may know better)
	const auto pos = reinterpret_cast<const WINDOWPOS*>(lp);
	const bool moved = !(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE);
	if (!m_Retired && moved)
		m_Host.OnFrameMoved(this);
	handled = FALSE;		// DefWindowProc turns this into WM_SIZE / WM_MOVE
	return 0;
}

LRESULT CDockFloatFrame::OnGetMinMaxInfo(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	handled = FALSE;
	if (m_Retired)
		return 0;
	const SIZE client = m_Host.FloatMinClientSize(m_Id);
	if (client.cx <= 0 && client.cy <= 0)
		return 0;
	RECT rc{ 0, 0, client.cx, client.cy };
	::AdjustWindowRectExForDpi(&rc, (DWORD)GetWindowLongPtr(GWL_STYLE), FALSE, (DWORD)GetWindowLongPtr(GWL_EXSTYLE), m_Host.FloatDpi(m_Id));
	auto info = reinterpret_cast<MINMAXINFO*>(lp);
	info->ptMinTrackSize.x = std::max<LONG>(info->ptMinTrackSize.x, Width(rc));
	info->ptMinTrackSize.y = std::max<LONG>(info->ptMinTrackSize.y, Height(rc));
	handled = TRUE;
	return 0;
}

LRESULT CDockFloatFrame::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
	// closing the window closes its panes (which may refuse); the window follows when the last one is gone
	if (!m_Retired)
		m_Host.CloseFloatWindow(m_Id);
	return 0;
}

LRESULT CDockFloatFrame::OnNcLButtonDblClk(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	// like Visual Studio: a double click on the title bar docks the window instead of maximizing it
	if (wp == HTCAPTION && !m_Retired) {
		m_Host.DockFloatWindow(m_Id);
		return 0;
	}
	handled = FALSE;
	return 0;
}

//
// moving the window by its title bar: the drop guides show, and releasing over one docks the window's group
//

LRESULT CDockFloatFrame::OnNcLButtonDown(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	m_MoveCandidate = wp == HTCAPTION;
	handled = FALSE;
	return 0;
}

LRESULT CDockFloatFrame::OnEnterSizeMove(UINT, WPARAM, LPARAM, BOOL& handled) {
	handled = FALSE;
	if (m_MoveCandidate && !m_Retired)
		m_Moving = m_Host.BeginFrameMove(m_Id);
	m_MoveCandidate = false;
	return 0;
}

LRESULT CDockFloatFrame::OnMoving(UINT, WPARAM, LPARAM, BOOL& handled) {
	handled = FALSE;
	if (m_Moving) {
		POINT cursor;
		::GetCursorPos(&cursor);
		m_Host.UpdateDrag(cursor, (::GetKeyState(VK_CONTROL) & 0x8000) != 0);
	}
	return 0;
}

LRESULT CDockFloatFrame::OnExitSizeMove(UINT, WPARAM, LPARAM, BOOL& handled) {
	handled = FALSE;
	m_MoveCandidate = false;
	if (m_Moving) {
		m_Moving = false;
		// Escape cancels the move, and with it the drop; docking may take this window away, which is deferred
		m_Host.EndDrag((::GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0);
	}
	return 0;
}

//
// splitters
//

LRESULT CDockFloatFrame::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (auto root = m_Host.FloatRoot(m_Id); root && !m_Retired)
		m_Splitters.Begin(m_hWnd, *root, { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
	return 0;
}

LRESULT CDockFloatFrame::OnMouseMove(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Splitters.Dragging()) {
		if (auto root = m_Host.FloatRoot(m_Id))
			m_Splitters.Move(m_Host.Layout(), *root, { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
	}
	return 0;
}

LRESULT CDockFloatFrame::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Splitters.End(m_hWnd))
		ReleaseCapture();
	return 0;
}

LRESULT CDockFloatFrame::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Splitters.End(m_hWnd);
	return 0;
}

LRESULT CDockFloatFrame::OnSetCursor(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	handled = FALSE;
	if (LOWORD(lp) != HTCLIENT || m_Retired)
		return 0;
	auto root = m_Host.FloatRoot(m_Id);
	if (!root)
		return 0;
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (SplitterTracker::SetCursorFor(m_Splitters.AxisAt(*root, pt))) {
		handled = TRUE;
		return TRUE;
	}
	return 0;
}

}
