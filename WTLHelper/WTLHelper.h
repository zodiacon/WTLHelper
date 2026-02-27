#pragma once

namespace DarkMode {
	enum class DarkModeType : unsigned char;
}

struct MenuItemData {
	int id, icon;
	HICON hIcon { nullptr };
};

struct WTLHelper final {
	static bool InitDarkMode();
	static bool InitDarkMode(DarkMode::DarkModeType type);
	static DarkMode::DarkModeType DarkModeType() noexcept;
	static bool IsDarkMode() noexcept;
	static bool IsClassicMode() noexcept;
	static bool SwitchToMode(DarkMode::DarkModeType type, HWND hWnd);
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count );
	static bool InitMenu(CMenuHandle menu, MenuItemData const& item);
	static bool IsSystemInDarkMode();
	static int SuspendHook() noexcept;
	static int ResumeHook() noexcept;
};

