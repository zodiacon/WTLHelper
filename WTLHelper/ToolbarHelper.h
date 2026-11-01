#pragma once

#include "IconHelper.h"

// a button: its command (0: a separator), its icon (the app's by ID, or a stock icon such as IDI_WARNING; none: 0),
// style and text (shown beside the icon: TBSTYLE_LIST)
struct ToolBarButtonInfo {
	UINT id;
	IconId image;
	BYTE style = BTNS_BUTTON;
	PCWSTR text = nullptr;
};


//
// size: of the button images at 96 DPI, scaled to the DPI of the window
//
struct ToolbarHelper {
	static HWND CreateAndInitToolBar(HWND hWnd, const ToolBarButtonInfo* buttons, int count, int size = 24);
	// images for the current DPI of the tool bar (after its DPI changed), from the buttons it was created with
	static void UpdateToolBarImages(HWND hToolBar, const ToolBarButtonInfo* buttons, int count, int size = 24);
	static POINT GetDropdownMenuPoint(HWND hToolBar, UINT buttonId);

private:
	static HIMAGELIST CreateImageList(const ToolBarButtonInfo* buttons, int count, int size);
};
