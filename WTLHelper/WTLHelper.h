#pragma once

enum class DarkModeKind {
	Light, Dark, System, Classic, 
	Unknown = 0xff
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
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count );
	static bool InitMenu(CMenuHandle menu, MenuItemData const& item);
	static bool IsSystemInDarkMode();
	static int SuspendHook() noexcept;
	static int ResumeHook() noexcept;
};

