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
	// sets the items' icons (as bitmaps on the current background); size: the icons' size in pixels
	// (for a per-monitor DPI aware app, the window's DPI scaled). The bitmaps are kept and shared by all menus
	// (for each icon, size and background), so menus made and initialized again and again don't leak them;
	// an item's hIcon (if not a resource icon) must therefore stay valid for the life of the process
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count, int size = 16);
	static bool InitMenu(CMenuHandle menu, MenuItemData const& item, int size = 16);
	static bool IsSystemInDarkMode();
	static int SuspendHook() noexcept;
	static int ResumeHook() noexcept;
	static bool SetDarkTone(DarkMode::ColorTone tone, HWND hWnd = nullptr);
	static DarkMode::ColorTone GetDarkTone() noexcept;
	static bool InvokeFontDialog(CFontDialog& dlg, HWND hParent = nullptr);
};

