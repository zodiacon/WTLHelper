// WTLHelper.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "WTLHelper.h"

#include <detours/detours.h>

#include "DarkMode/DarkModeSubclass.h"
#include "CustomHeader2.h"

static DarkMode::DarkModeType g_DarkModeType { DarkMode::DarkModeType::unknown };
static HHOOK g_hHook;
static int g_SuspendCount;

static LRESULT OnHook(int code, WPARAM wp, LPARAM lp) {
	if (g_SuspendCount <= 0 && code >= HC_ACTION) {
		auto msg = (CWPRETSTRUCT*)lp;
		if (msg->message == WM_INITDIALOG) {
			DarkMode::setDarkWndNotifySafe(msg->hwnd);
		}
		else if (msg->message == WM_CREATE) {
			auto hwnd = msg->hwnd;
			auto lpcs = (LPCREATESTRUCT)msg->lParam;
			if (lpcs->style & WS_CHILD) {
				CString name;
				if (::GetClassName(hwnd, name.GetBufferSetLength(32), 32)) {
					if (name.CompareNoCase(WC_HEADER) == 0 || name.CompareNoCase("ATL:" WC_HEADER) == 0) {
						auto win = new CCustomHeader2;
						win->SubclassWindow(hwnd);
					}
					else if (name == DATETIMEPICK_CLASS) {
						::SetWindowTheme(hwnd, L" ", L" ");
						CDateTimePickerCtrl dtp(hwnd);
						dtp.SetMonthCalColor(MCSC_BACKGROUND, DarkMode::getBackgroundColor());
						dtp.SetMonthCalColor(MCSC_MONTHBK, DarkMode::getBackgroundColor());
						dtp.SetMonthCalColor(MCSC_TITLEBK, DarkMode::getBackgroundColor());
						dtp.SetMonthCalColor(MCSC_TEXT, DarkMode::getTextColor());
						dtp.SetMonthCalColor(MCSC_TITLETEXT, DarkMode::getTextColor());
					}
					else if (name == MONTHCAL_CLASS) {
						::SetWindowTheme(hwnd, L" ", L" ");
						CMonthCalendarCtrl ctl(hwnd);
						ctl.SetColor(MCSC_BACKGROUND, DarkMode::getBackgroundColor());
						ctl.SetColor(MCSC_MONTHBK, DarkMode::getBackgroundColor());
						ctl.SetColor(MCSC_TITLEBK, DarkMode::getBackgroundColor());
						ctl.SetColor(MCSC_TEXT, DarkMode::getTextColor());
						ctl.SetColor(MCSC_TITLETEXT, DarkMode::getTextColor());
					}
				}
				DarkMode::setDarkWndNotifySafe(hwnd);

			}
			else {
				// top-level window
				DarkMode::setWindowEraseBgSubclass(hwnd);
				DarkMode::setWindowMenuBarSubclass(hwnd);
				DarkMode::setDarkWndNotifySafe(hwnd);
			}
		}
	}
	return ::CallNextHookEx(nullptr, code, wp, lp);
}

static decltype(::GetSysColor)* OrgGetSysColor;
static decltype(::GetSysColorBrush)* OrgGetSysColorBrush;
static bool g_ThemeChanged = true;

HBRUSH WINAPI HookedGetSysColorBrush2(int index) {
	if (g_DarkModeType != DarkMode::DarkModeType::dark)
		return OrgGetSysColorBrush(index);

	switch (index) {
		case COLOR_WINDOW:
		case COLOR_BACKGROUND:
			return DarkMode::getBackgroundBrush();
		case COLOR_3DFACE:
			return DarkMode::getCtrlBackgroundBrush();
		case COLOR_WINDOWTEXT:
			static auto textBrush = ::CreateSolidBrush(DarkMode::getTextColor());
			if (g_ThemeChanged) {
				g_ThemeChanged = false;
				::DeleteObject(textBrush);
				textBrush = ::CreateSolidBrush(DarkMode::getTextColor());
			}
			return textBrush;
	}
	return OrgGetSysColorBrush(index);
}

COLORREF WINAPI HookedGetSysColor2(int index) {
	if (g_DarkModeType != DarkMode::DarkModeType::dark)
		return OrgGetSysColor(index);

	switch (index) {
		case COLOR_WINDOW:
		case COLOR_BACKGROUND:
			return DarkMode::getBackgroundColor();
		case COLOR_3DFACE:
			return DarkMode::getCtrlBackgroundColor();
		case COLOR_WINDOWTEXT:
			return DarkMode::getTextColor();
	}
	return OrgGetSysColor(index);
}

bool InitHooks() {
	OrgGetSysColor = (decltype(OrgGetSysColor))::GetProcAddress(::GetModuleHandle(L"user32"), "GetSysColor");
	ATLASSERT(OrgGetSysColor);
	OrgGetSysColorBrush = (decltype(OrgGetSysColorBrush))::GetProcAddress(::GetModuleHandle(L"user32"), "GetSysColorBrush");
	ATLASSERT(OrgGetSysColorBrush);

	if (NOERROR != DetourTransactionBegin())
		return false;

	DetourUpdateThread(::GetCurrentThread());
	DetourAttach((PVOID*)&OrgGetSysColor, HookedGetSysColor2);
	DetourAttach((PVOID*)&OrgGetSysColorBrush, HookedGetSysColorBrush2);
	auto error = DetourTransactionCommit();
	ATLASSERT(error == NOERROR);
	return error == NOERROR;
}

bool WTLHelper::InitDarkMode(DarkMode::DarkModeType type) {
	g_hHook = ::SetWindowsHookEx(WH_CALLWNDPROCRET, OnHook, nullptr, GetCurrentThreadId());
	g_DarkModeType = type;

	DarkMode::initDarkMode();
	DarkMode::setDarkModeConfigEx(static_cast<UINT>(type));
	DarkMode::setDefaultColors(true);
	DarkMode::setColorizeTitleBarConfig(false);

	return true;// InitHooks();
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
	if (hWnd) {
		DarkMode::setDarkTitleBarEx(hWnd, true);
		DarkMode::setChildCtrlsTheme(hWnd);
		g_ThemeChanged = true;

		CWindow(hWnd).SendMessageToDescendants(ThemeChangedMessage, 0, static_cast<LPARAM>(type));
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	return true;
}

bool WTLHelper::SwitchToMode(HWND hWnd) {
	return SwitchToMode(DarkMode::DarkModeType::system, hWnd);
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
