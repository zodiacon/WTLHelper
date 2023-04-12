#include "pch.h"
#include "ToolbarHelper.h"

HWND ToolbarHelper::CreateAndInitToolBar(HWND hWnd, const ToolBarButtonInfo* buttons, int count, int size) {
	CToolBarCtrl tb;
	auto hWndToolBar = tb.Create(hWnd, CWindow::rcDefault, nullptr, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST, 0, ATL_IDW_TOOLBAR);
	tb.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);

	CImageList tbImages;
	tbImages.Create(size, size, ILC_COLOR32, 4, 4);
	tb.SetImageList(tbImages);

	for (int i = 0; i < count; i++) {
		auto& b = buttons[i];
		if (b.id == 0)
			tb.AddSeparator(0);
		else {
			int image = b.image == 0 ? I_IMAGENONE : tbImages.AddIcon(AtlLoadIconImage(b.image, 0, size, size));
			tb.AddButton(b.id, b.style | (b.text ? BTNS_SHOWTEXT : 0), TBSTATE_ENABLED, image, b.text, 0);
		}
	}
	return hWndToolBar;
}

POINT ToolbarHelper::GetDropdownMenuPoint(HWND hToolBar, UINT buttonId) {
	CToolBarCtrl tb(hToolBar);
	CRect rect;
	tb.GetItemRect(tb.CommandToIndex(buttonId), &rect);
	CPoint pt(rect.left, rect.bottom);
	tb.MapWindowPoints(HWND_DESKTOP, &pt, 1);
	return pt;
}
