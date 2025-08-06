#pragma once

struct DarkModeHelper {
	static void Init();
	static void AllowDarkModeForApp(bool allow);
	static void RefreshTitleBarThemeColor(HWND hWnd);
};

