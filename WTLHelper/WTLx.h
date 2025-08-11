#pragma once

enum class WindowStyles : uint32_t {
	None = 0,
	Child = WS_CHILD,
	Visible = WS_VISIBLE,
	Overlapped = WS_OVERLAPPED,
	MinimizeBox = WS_MINIMIZEBOX,
	Border = WS_BORDER,
	Caption = WS_CAPTION,
	SystemMenu = WS_SYSMENU,
};


enum class WindowStylesEx : uint32_t {
	None = 0,
};


class CWindowX : public CWindow {
public:
	using CWindow::CWindow;

	HWND Create(
		_In_opt_z_ LPCTSTR lpstrWndClass,
		_In_opt_ HWND hWndParent,
		_In_ _U_RECT rect = nullptr,
		_In_opt_z_ LPCTSTR szWindowName = nullptr,
		_In_ WindowStyles dwStyle = WindowStyles::None,
		_In_ WindowStylesEx dwExStyle = WindowStylesEx::None,
		_In_ _U_MENUorID MenuOrID = 0U,
		_In_opt_ LPVOID lpCreateParam = nullptr) noexcept {
		return CWindow::Create(lpstrWndClass, hWndParent, rect, szWindowName, static_cast<DWORD>(dwStyle),
			static_cast<DWORD>(dwExStyle), MenuOrID, lpCreateParam);
	}

};
