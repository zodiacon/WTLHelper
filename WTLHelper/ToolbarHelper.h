#pragma once

struct ToolBarButtonInfo {
	UINT id;
	int image;
	BYTE style = BTNS_BUTTON;
	PCWSTR text = nullptr;
};


struct ToolbarHelper {
	static HWND CreateAndInitToolBar(HWND hWnd, const ToolBarButtonInfo* buttons, int count, int size = 24);
	static POINT GetDropdownMenuPoint(HWND hToolBar, UINT buttonId);
};
