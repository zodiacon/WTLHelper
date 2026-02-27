// WTLHelper.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "WTLHelper.h"
#include "DarkMode/DarkModeSubclass.h"
#include "CustomHeader2.h"

static DarkMode::DarkModeType g_DarkModeType;
static HHOOK g_hHook;
static int g_SuspendCount;

static LRESULT OnHook(int code, WPARAM wp, LPARAM lp) {
	if (g_SuspendCount <= 0 && code == HC_ACTION) {
		auto msg = (CWPRETSTRUCT*)lp;
		if (msg->message == WM_INITDIALOG) {
			DarkMode::setDarkWndNotifySafe(msg->hwnd);
			::SetWindowLongPtr(msg->hwnd, GWL_STYLE, ::GetWindowLongPtr(msg->hwnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
		}

		else if (msg->message == WM_CREATE) {
			auto hwnd = msg->hwnd;
			auto lpcs = (LPCREATESTRUCT)msg->lParam;
			if (lpcs->style & WS_CHILD) {
				DarkMode::setDarkWndNotifySafe(hwnd);
				CString name;
				if (::GetClassName(hwnd, name.GetBufferSetLength(32), 32)) {
					if (name.CompareNoCase(WC_HEADER) == 0 || name.CompareNoCase("ATL:" WC_HEADER) == 0) {
						//::SetWindowTheme(hwnd, nullptr, nullptr);
						auto win = new CCustomHeader2;
						win->SubclassWindow(hwnd);
					}
				}
			}
			else {
				// top-level window
				DarkMode::setDarkWndNotifySafe(hwnd);
				DarkMode::setWindowEraseBgSubclass(hwnd);
				DarkMode::setWindowMenuBarSubclass(hwnd);
				//DarkMode::setWindowExStyle(msg->hwnd, true, WS_EX_COMPOSITED);
				::SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);
			}
		}
	}
	return ::CallNextHookEx(nullptr, code, wp, lp);
}

bool WTLHelper::InitDarkMode(DarkMode::DarkModeType type) {
	g_DarkModeType = type;
	DarkMode::initDarkMode();
	DarkMode::setDarkModeConfigEx(static_cast<UINT>(type));
	DarkMode::setDefaultColors(true);
	DarkMode::setColorizeTitleBarConfig(false);

	g_hHook = ::SetWindowsHookEx(WH_CALLWNDPROCRET, OnHook, nullptr, GetCurrentThreadId());
	return true;
}

bool WTLHelper::InitDarkMode() {
	return InitDarkMode(IsSystemInDarkMode() ? DarkMode::DarkModeType::dark : DarkMode::DarkModeType::classic);
}

DarkMode::DarkModeType WTLHelper::DarkModeType() noexcept {
	return g_DarkModeType;
}

bool WTLHelper::IsDarkMode() noexcept {
	return g_DarkModeType == DarkMode::DarkModeType::dark;
}

bool WTLHelper::IsClassicMode() noexcept {
	return g_DarkModeType == DarkMode::DarkModeType::classic;
}

bool WTLHelper::SwitchToMode(DarkMode::DarkModeType type, HWND hWnd) {
	if (type == DarkMode::DarkModeType::system)
		type = IsSystemInDarkMode() ? DarkMode::DarkModeType::dark : DarkMode::DarkModeType::light;

	if (g_DarkModeType == type)
		return false;

	DarkMode::setDarkModeConfigEx(static_cast<UINT>(g_DarkModeType = type));
	DarkMode::setDefaultColors(true);
	DarkMode::setDarkTitleBarEx(hWnd, true);
	DarkMode::setChildCtrlsTheme(hWnd);
	::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);

	return true;
}

bool WTLHelper::InitMenu(CMenuHandle menu, MenuItemData const* items, int count) {
	ATLASSERT(::IsMenu(menu));

	CDC mdc;
	CClientDC dc(::GetDesktopWindow());
	mdc.CreateCompatibleDC(dc);
	CRect rc(0, 0, 16, 16);
	for (int i = 0; i < count; i++) {
		auto& cmd = items[i];
		auto hIcon = cmd.hIcon ? cmd.hIcon : AtlLoadIconImage(cmd.icon, 0, 16, 16);
		ATLASSERT(hIcon);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(dc, 16, 16);
		mdc.SelectBitmap(bmp);
		mdc.FillRect(&rc, WTLHelper::DarkModeType() == DarkMode::DarkModeType::classic ? ::GetSysColorBrush(COLOR_MENU) : DarkMode::getCtrlBackgroundBrush());
		mdc.DrawIconEx(0, 0, hIcon, 16, 16);
		menu.SetMenuItemBitmaps(cmd.id, MF_BYCOMMAND, bmp, bmp);
		bmp.Detach();
	}
	return true;
}

bool WTLHelper::InitMenu(CMenuHandle menu, MenuItemData const& cmd) {
	auto hIcon = cmd.hIcon ? cmd.hIcon : AtlLoadIconImage(cmd.icon, 0, 16, 16);
	ATLASSERT(hIcon);
	CBitmap bmp;
	CDC mdc;
	CClientDC dc(::GetDesktopWindow());
	mdc.CreateCompatibleDC(dc);
	CRect rc(0, 0, 16, 16);
	bmp.CreateCompatibleBitmap(dc, 16, 16);
	mdc.SelectBitmap(bmp);
	mdc.FillRect(&rc, WTLHelper::DarkModeType() == DarkMode::DarkModeType::classic ? ::GetSysColorBrush(COLOR_MENU) : DarkMode::getCtrlBackgroundBrush());
	mdc.DrawIconEx(0, 0, hIcon, 16, 16);
	menu.SetMenuItemBitmaps(cmd.id, MF_BYCOMMAND, bmp, bmp);
	bmp.Detach();
	return true;
}

bool WTLHelper::IsSystemInDarkMode() {
	CRegKey key;
	if (ERROR_SUCCESS != key.Open(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", KEY_QUERY_VALUE))
		return false;

	DWORD value;
	return key.QueryDWORDValue(L"AppsUseLightTheme", value) == ERROR_SUCCESS && value == 0;
}

int WTLHelper::SuspendHook() noexcept {
	return ++g_SuspendCount;
}

int WTLHelper::ResumeHook() noexcept {
	return --g_SuspendCount;
}
