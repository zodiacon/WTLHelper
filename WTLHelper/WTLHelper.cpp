// WTLHelper.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "WTLHelper.h"

#include <detours/detours.h>

#include "DarkMode/DarkModeSubclass.h"
#include "CustomHeader2.h"
#include "CustomDateTimePicker.h"
#include "CustomMonthCalendar.h"
#include "IconHelper.h"
#include "CustomCheckAclUI.h"

static DarkModeKind g_DarkModeType { DarkModeKind::Unknown };
static HHOOK g_hHook;
static int g_SuspendCount;

SuspendResumeHook::SuspendResumeHook() {
	WTLHelper::SuspendHook();
}

SuspendResumeHook::~SuspendResumeHook() {
	WTLHelper::ResumeHook();
}

static LRESULT CALLBACK OnHook(int code, WPARAM wp, LPARAM lp) {
	if (g_SuspendCount <= 0 && code >= HC_ACTION) {
		auto msg = (CWPRETSTRUCT*)lp;
		if (msg->message == WM_INITDIALOG) {
			DarkMode::setDarkWndNotifySafe(msg->hwnd);
		}
		else if (msg->message == WM_CREATE) {
			auto hwnd = msg->hwnd;
			auto lpcs = (LPCREATESTRUCT)msg->lParam;
			CString name;
			::GetClassName(hwnd, name.GetBufferSetLength(32), 32);

			if (name.CompareNoCase(DATETIMEPICK_CLASS) == 0) {
				::SetWindowTheme(hwnd, L"EXPLORER_DTP", L"");
				DarkMode::setDarkWndNotifySafe(hwnd);
			}
			else if (name.CompareNoCase(MONTHCAL_CLASS) == 0) {
				// SysMonthCal32 has no DarkMode_* visual style; CCustomMonthCalendar
				// strips visual styles and recolors via MCM_SETCOLOR instead.
				auto win = new CCustomMonthCalendar;
				win->SubclassWindow(hwnd);
				DarkMode::setDarkWndNotifySafe(hwnd);
				win->Init();
				return ::CallNextHookEx(nullptr, code, wp, lp);
			}
			if (name.CompareNoCase(L"CHECKLIST_ACLUI") == 0) {
				auto win = new CCustomCheckAclUI;
				win->SubclassWindow(hwnd);
				return ::CallNextHookEx(nullptr, code, wp, lp);
			}

			else if (lpcs->style & WS_CHILD) {
				if (name.CompareNoCase(WC_HEADER) == 0 || name.CompareNoCase("ATL:" WC_HEADER) == 0) {
					auto win = new CCustomHeader2;
					win->SubclassWindow(hwnd);
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

//
// GetSysColorBrush returns brushes that are not the caller's to delete, but a caller that got one can't tell
// that ours are different, and some do delete what they get: the shell's AutoComplete (which starts when the user
// types in a file dialog's File name box) deletes the COLOR_3DFACE brush. If that were the DarkMode library's own
// brush, the library would keep painting with a dead handle and every control created afterwards would come out
// light. So the hook hands out brushes of its own, checked on every call and recreated if they were deleted.
//
static HBRUSH SafeBrush(HBRUSH& brush, COLORREF color) {
	LOGBRUSH lb;
	if (brush && ::GetObjectType(brush) == OBJ_BRUSH && ::GetObject(brush, sizeof(lb), &lb) == sizeof(lb) && lb.lbStyle == BS_SOLID && lb.lbColor == color)
		return brush;
	// a brush that was deleted or is now of another color is not ours to delete (its handle may have been reused)
	return brush = ::CreateSolidBrush(color);
}

HBRUSH WINAPI HookedGetSysColorBrush2(int index) {
	if (g_DarkModeType != DarkModeKind::Dark)
		return OrgGetSysColorBrush(index);

	static HBRUSH windowBrush, faceBrush, textBrush;
	switch (index) {
		case COLOR_WINDOW:
		case COLOR_BACKGROUND:
			return SafeBrush(windowBrush, DarkMode::getBackgroundColor());
		case COLOR_3DFACE:
			return SafeBrush(faceBrush, DarkMode::getCtrlBackgroundColor());
		case COLOR_WINDOWTEXT:
			return SafeBrush(textBrush, DarkMode::getTextColor());
	}
	return OrgGetSysColorBrush(index);
}

COLORREF WINAPI HookedGetSysColor2(int index) {
	if (g_DarkModeType != DarkModeKind::Dark)
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

static decltype(::FillRect)* OrgFillRect;

//
// FillRect also accepts a system color index + 1 in place of a brush, which user32 resolves internally,
// bypassing the GetSysColorBrush hook. The font dialog paints its owner drawn font and style lists this way.
//
int WINAPI HookedFillRect(HDC hdc, RECT const* rc, HBRUSH brush) {
	if (g_DarkModeType == DarkModeKind::Dark && (ULONG_PTR)brush > 0 && (ULONG_PTR)brush <= COLOR_MENUBAR + 1)
		brush = HookedGetSysColorBrush2((int)(ULONG_PTR)brush - 1);
	return OrgFillRect(hdc, rc, brush);
}

bool InitHooks() {
	OrgGetSysColor = (decltype(OrgGetSysColor))::GetProcAddress(::GetModuleHandle(L"user32"), "GetSysColor");
	ATLASSERT(OrgGetSysColor);
	OrgGetSysColorBrush = (decltype(OrgGetSysColorBrush))::GetProcAddress(::GetModuleHandle(L"user32"), "GetSysColorBrush");
	ATLASSERT(OrgGetSysColorBrush);
	OrgFillRect = (decltype(OrgFillRect))::GetProcAddress(::GetModuleHandle(L"user32"), "FillRect");
	ATLASSERT(OrgFillRect);

	if (NOERROR != DetourTransactionBegin())
		return false;

	DetourUpdateThread(::GetCurrentThread());
	DetourAttach((PVOID*)&OrgGetSysColor, HookedGetSysColor2);
	DetourAttach((PVOID*)&OrgGetSysColorBrush, HookedGetSysColorBrush2);
	DetourAttach((PVOID*)&OrgFillRect, HookedFillRect);
	auto error = DetourTransactionCommit();
	ATLASSERT(error == NOERROR);
	return error == NOERROR;
}

bool WTLHelper::InitDarkMode(DarkModeKind type) {
	g_hHook = ::SetWindowsHookEx(WH_CALLWNDPROCRET, OnHook, nullptr, GetCurrentThreadId());
	g_DarkModeType = type;

	DarkMode::initDarkMode();
	DarkMode::setDarkModeConfigEx(static_cast<UINT>(type));
	DarkMode::setDefaultColors(true);
	DarkMode::setColorizeTitleBarConfig(false);

	return InitHooks();
}

bool WTLHelper::InitDarkMode() {
	return InitDarkMode(IsSystemInDarkMode() ? DarkModeKind::Dark : DarkModeKind::Classic);
}

DarkModeKind WTLHelper::DarkModeType() noexcept {
	return g_DarkModeType;
}

bool WTLHelper::IsDarkMode() noexcept {
	return g_DarkModeType == DarkModeKind::Dark;
}

bool WTLHelper::IsClassicMode() noexcept {
	return g_DarkModeType == DarkModeKind::Classic;
}

bool WTLHelper::SwitchToMode(DarkModeKind type, HWND hWnd) {
	if (type == DarkModeKind::System)
		type = IsSystemInDarkMode() ? DarkModeKind::Dark : DarkModeKind::Light;

	if (g_DarkModeType == type)
		return false;

	DarkMode::setDarkModeConfigEx(static_cast<UINT>(g_DarkModeType = type));
	DarkMode::setDefaultColors(true);
	if (hWnd) {
		DarkMode::setDarkTitleBarEx(hWnd, true);
		DarkMode::setChildCtrlsTheme(hWnd);

		CWindow(hWnd).SendMessageToDescendants(ThemeChangedMessage, 0, static_cast<LPARAM>(type));
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	return true;
}

bool WTLHelper::SwitchToMode(HWND hWnd) {
	return SwitchToMode(DarkModeKind::System, hWnd);
}

void WTLHelper::SetColorTone(ColorTone tone, HWND hWnd) {
	DarkMode::setColorTone(static_cast<int>(tone));
	if (hWnd) {
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
}

ColorTone WTLHelper::GetColorTone() noexcept {
	return static_cast<ColorTone>(DarkMode::getColorTone());
}

bool WTLHelper::InitMenu(CMenuHandle menu, MenuItemData const* items, int count, int size) {
	ATLASSERT(::IsMenu(menu));
	ATLASSERT(size > 0);

	CDC mdc;
	CClientDC dc(::GetDesktopWindow());
	mdc.CreateCompatibleDC(dc);
	CRect rc(0, 0, size, size);
	for (int i = 0; i < count; i++) {
		auto& cmd = items[i];
		// resource icons are kept (for each size), as menus are initialized again and again
		auto hIcon = cmd.hIcon ? cmd.hIcon : IconHelper::LoadCached(cmd.icon, size);
		ATLASSERT(hIcon);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(dc, size, size);
		auto hOld = mdc.SelectBitmap(bmp);
		mdc.FillRect(&rc, WTLHelper::DarkModeType() == DarkModeKind::Classic ? ::GetSysColorBrush(COLOR_MENU) : DarkMode::getCtrlBackgroundBrush());
		mdc.DrawIconEx(0, 0, hIcon, size, size);
		mdc.SelectBitmap(hOld);
		menu.SetMenuItemBitmaps(cmd.id, MF_BYCOMMAND, bmp, bmp);
		bmp.Detach();
	}
	return true;
}

bool WTLHelper::InitMenu(CMenuHandle menu, MenuItemData const& cmd, int size) {
	return InitMenu(menu, &cmd, 1, size);
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

bool WTLHelper::SetDarkTone(DarkMode::ColorTone tone, HWND hWnd) {
	DarkMode::setColorTone(static_cast<int>(tone));

	if (hWnd) {
		CWindow(hWnd).SendMessageToDescendants(ThemeChangedMessage, 0, static_cast<LPARAM>(g_DarkModeType));
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
	}
	return true;
}

DarkMode::ColorTone WTLHelper::GetDarkTone() noexcept {
	return static_cast<DarkMode::ColorTone>(DarkMode::getColorTone());
}

bool WTLHelper::InvokeFontDialog(CFontDialog& dlg, HWND hParent) {
	return dlg.DoModal() == IDOK;
}
