#include "CommandPalette.h"

#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

// The popup: a top-level window owned by the owner's top-level window, holding a palette.

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWA_BORDER_COLOR 34
#endif

BOOL CCommandPalettePopup::Create(HWND owner, UINT id) {
	if (!::IsWindow(owner))
		return FALSE;
	m_Owner = owner;
	if (!CWindowImpl::Create(::GetAncestor(owner, GA_ROOT), rcDefault))
		return FALSE;
	m_Palette.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE, 0, id);
	m_Palette.SetOwner(owner);
	UpdateFrame();
	return TRUE;
}

bool CCommandPalettePopup::IsOpen() const {
	return IsWindow() && IsWindowVisible();
}

void CCommandPalettePopup::SetMaxRows(int rows) {
	m_MaxRows = std::max(rows, 1);
}

void CCommandPalettePopup::SetWidth(int dips) {
	m_Width = std::max(dips, 200);
}

RECT CCommandPalettePopup::TargetRect() const {
	RECT client{};
	::GetClientRect(m_Owner, &client);
	::MapWindowPoints(m_Owner, nullptr, (POINT*)&client, 2);
	UINT dpi = ::GetDpiForWindow(m_Owner);
	auto scale = [&](int dips) { return ::MulDiv(dips, dpi, 96); };
	int available = (int)(client.right - client.left) - scale(32);
	int width = std::max(std::min(scale(m_Width), available), scale(240));
	int rows = std::clamp(m_Palette.GetResultCount(), 1, m_MaxRows);
	int height = m_Palette.GetIdealHeight(rows) + 2 * m_Border;
	int x = client.left + ((int)(client.right - client.left) - width) / 2;
	return { x, client.top + scale(8), x + width, client.top + scale(8) + height };
}

BOOL CCommandPalettePopup::Show(LPCWSTR text) {
	if (!IsWindow())
		return FALSE;
	m_Palette.RefreshMenus(m_Owner);
	if (text)
		m_Palette.SetSearchText(text);
	::SendMessage(m_Palette.GetSearchBox(), EM_SETSEL, 0, -1);   // typing replaces it
	// placed first while still hidden: on another monitor the DPI changes, and with it the size
	auto rc = TargetRect();
	SetWindowPos(HWND_TOP, &rc, SWP_NOACTIVATE);
	rc = TargetRect();
	SetWindowPos(HWND_TOP, &rc, SWP_SHOWWINDOW);
	::SetActiveWindow(m_hWnd);
	m_Palette.SetFocus();
	return TRUE;
}

void CCommandPalettePopup::Close() {
	if (!IsOpen() || m_Closing)
		return;
	m_Closing = true;
	ShowWindow(SW_HIDE);   // the owner's window is activated again
	m_Closing = false;
	NMHDR hdr{ m_Palette.m_hWnd, (UINT_PTR)m_Palette.GetDlgCtrlID(), CMDPN_CLOSED };
	::SendMessage(m_Owner, WM_NOTIFY, hdr.idFrom, (LPARAM)&hdr);
}

void CCommandPalettePopup::UpdateFrame() {
	// Windows 11 rounds the corners and draws the border; else the popup draws a border itself
	auto c = m_Palette.GetColors();
	int corner = 2;   // DWMWCP_ROUND
	if (SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)))) {
		COLORREF border = c.crBoxBorder;
		::DwmSetWindowAttribute(m_hWnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
		m_Border = 0;
	}
	else {
		m_Border = 1;
	}
	// the palette inset by the border
	RECT rc;
	GetClientRect(&rc);
	BOOL handled = TRUE;
	OnSize(WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom), handled);
}

LRESULT CCommandPalettePopup::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Palette.IsWindow())
		m_Palette.SetWindowPos(nullptr, m_Border, m_Border, std::max(0, LOWORD(lParam) - 2 * m_Border), std::max(0, HIWORD(lParam) - 2 * m_Border), SWP_NOZORDER | SWP_NOACTIVATE);
	return 0;
}

LRESULT CCommandPalettePopup::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CCommandPalettePopup::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC dc(m_hWnd);
	auto c = m_Palette.GetColors();
	RECT rc;
	GetClientRect(&rc);
	WTL::CBrush back, border;
	back.CreateSolidBrush(c.crBack);
	dc.FillRect(&rc, back);
	if (m_Border) {
		border.CreateSolidBrush(c.crBoxBorder);
		dc.FrameRect(&rc, border);
	}
	return 0;
}

LRESULT CCommandPalettePopup::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Palette.SetFocus();
	return 0;
}

LRESULT CCommandPalettePopup::OnActivate(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	HWND root = ::GetAncestor(m_Owner, GA_ROOT);
	if (LOWORD(wParam) == WA_INACTIVE) {
		// the owner's caption looked active meanwhile: right again if the activation goes elsewhere
		if ((HWND)lParam != root)
			::SendMessage(root, WM_NCACTIVATE, FALSE, 0);
		Close();
	}
	else {
		::SendMessage(root, WM_NCACTIVATE, TRUE, 0);   // the app stays active to the eye, as with a menu
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCommandPalettePopup::OnNotify(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom != m_Palette.m_hWnd)
		return 0;
	// the owner hears it all; its answer to CMDPN_EXECUTE counts
	LRESULT result = ::SendMessage(m_Owner, WM_NOTIFY, wParam, lParam);
	switch (hdr->code) {
		case CMDPN_EXECUTE:
		case CMDPN_CANCEL:
			Close();
			break;
		case CMDPN_RESULTSCHANGED: {
			// as tall as the results need
			auto rc = TargetRect();
			SetWindowPos(nullptr, &rc, SWP_NOZORDER | SWP_NOACTIVATE);
			break;
		}
	}
	return result;
}

LRESULT CCommandPalettePopup::OnDpiChanged(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto rc = (RECT*)lParam;
	SetWindowPos(nullptr, rc, SWP_NOZORDER | SWP_NOACTIVATE);
	return 0;
}

LRESULT CCommandPalettePopup::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UpdateFrame();
	Invalidate();
	bHandled = FALSE;
	return 0;
}
