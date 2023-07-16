#pragma once

struct Theme;

enum class DCOperation {
	None,
	SetTextColor = 1,
};
DEFINE_ENUM_FLAG_OPERATORS(DCOperation);

struct COwnerDrawnMenuBase;

struct ThemeHelper abstract final {
	static bool LoadFromFile(PCWSTR path, Theme& theme);
	static bool SaveToFile(Theme const& theme, PCWSTR path);
	static bool Init(HANDLE hThread = ::GetCurrentThread());
	static int Suspend();
	static bool IsSuspended();
	static int Resume();

	static const Theme* GetCurrentTheme();
	static bool IsDefault();
	static void SetCurrentTheme(const Theme& theme, HWND hWnd = nullptr);
	static void SetDefaultTheme(HWND hWnd);
	static void UpdateMenuColors(COwnerDrawnMenuBase& menu, bool dark);
	//static void SendMessageToDescendants(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};

