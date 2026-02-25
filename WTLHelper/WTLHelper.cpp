// WTLHelper.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "WTLHelper.h"
#include "DarkMode/DarkModeSubclass.h"

static LRESULT OnHook(int code, WPARAM wp, LPARAM lp) {
	if (code == HC_ACTION) {
		auto msg = (CWPRETSTRUCT*)lp;
		if (msg->message == WM_INITDIALOG) {
			DarkMode::setDarkWndNotifySafe(msg->hwnd);
		}

		else if (msg->message == WM_CREATE) {
			auto hwnd = msg->hwnd;
			auto lpcs = (LPCREATESTRUCT)msg->lParam;
			if (lpcs->style & WS_CHILD) {
				DarkMode::setWindowExStyle(hwnd, true, WS_EX_COMPOSITED);
				DarkMode::setWindowEraseBgSubclass(hwnd);
				DarkMode::setDarkWndNotifySafe(hwnd);
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

DarkMode::DarkModeType g_DarkModeType;

bool WTLHelper::InitDarkMode(DarkMode::DarkModeType type) {
	g_DarkModeType = type;
	DarkMode::initDarkMode();
	DarkMode::setDarkModeConfigEx(static_cast<UINT>(type));
	DarkMode::setDefaultColors(true);
	DarkMode::setColorizeTitleBarConfig(false);

	::SetWindowsHookEx(WH_CALLWNDPROCRET, OnHook, nullptr, GetCurrentThreadId());
	return true;
}

DarkMode::DarkModeType WTLHelper::DarkModeType() noexcept {
	return g_DarkModeType;
}

bool WTLHelper::IsDarkMode() noexcept {
	return g_DarkModeType == DarkMode::DarkModeType::dark;
}
