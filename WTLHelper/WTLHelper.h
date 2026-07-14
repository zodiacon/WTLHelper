#pragma once

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
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count );
	static bool InitMenu(CMenuHandle menu, MenuItemData const& item);
	static bool IsSystemInDarkMode();
	static int SuspendHook() noexcept;
	static int ResumeHook() noexcept;
	static bool InvokeFontDialog(CFontDialog& dlg);
};

