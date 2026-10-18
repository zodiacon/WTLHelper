#pragma once

#include "DarkMode/DarkModeSubclass.h"

#include <atldlgs.h>

enum class DarkModeKind {
	Light, Dark, System, Classic,
	Unknown = 0xff
};

//
// Mirrors DarkMode::ColorTone (DarkModeSubclass.h) so callers don't need to include the dmlib headers.
// Values must stay in sync for the static_cast in WTLHelper.cpp to remain valid.
//
enum class ColorTone {
	Black, Red, Green, Blue, Purple, Cyan, Olive
};

struct MenuItemData {
	int id, icon;
	HICON hIcon { nullptr };
};

struct SuspendResumeHook {
	SuspendResumeHook();
	~SuspendResumeHook();
};

struct WTLHelper final {
	inline static UINT ThemeChangedMessage = ::RegisterWindowMessage(L"ThemeChanged");
	static bool InitDarkMode();
	static bool InitDarkMode(DarkModeKind type);
	static DarkModeKind DarkModeType() noexcept;
	static bool IsDarkMode() noexcept;
	static bool IsClassicMode() noexcept;
	static bool SwitchToMode(DarkModeKind type, HWND hWnd);
	static bool SwitchToMode(HWND hWnd);
	static void SetColorTone(ColorTone tone, HWND hWnd = nullptr);
	static ColorTone GetColorTone() noexcept;
	// the icons are made small icons for dpi (0: the system DPI); a menu shown on a monitor with another DPI may be initialized again for it
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count, UINT dpi = 0);
	static bool InitMenu(CMenuHandle menu, MenuItemData const& item, UINT dpi = 0);
	static bool IsSystemInDarkMode();
	static int SuspendHook() noexcept;
	static int ResumeHook() noexcept;
	static bool SetDarkTone(DarkMode::ColorTone tone, HWND hWnd = nullptr);
	static DarkMode::ColorTone GetDarkTone() noexcept;
	static bool InvokeFontDialog(CFontDialog& dlg, HWND hParent = nullptr);
};

