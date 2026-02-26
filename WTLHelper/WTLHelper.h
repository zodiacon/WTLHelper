#pragma once

namespace DarkMode {
	enum class DarkModeType : unsigned char;
}

struct MenuItemData {
	UINT id, icon;
	HICON hIcon { nullptr };
};

struct WTLHelper final {
	static bool InitDarkMode(DarkMode::DarkModeType type);
	static DarkMode::DarkModeType DarkModeType() noexcept;
	static bool IsDarkMode() noexcept;
	static bool SwitchToMode(DarkMode::DarkModeType type, HWND hWnd);
	static bool InitMenu(CMenuHandle menu, MenuItemData const* items, int count);
	static bool IsSystemInDarkMode();
};

