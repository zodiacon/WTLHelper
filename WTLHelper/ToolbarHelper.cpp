#include "pch.h"
#include "ToolbarHelper.h"
#include "IconHelper.h"
#include "DpiHelper.h"

HWND ToolbarHelper::CreateAndInitToolBar(HWND hWnd, const ToolBarButtonInfo* buttons, int count, int size) {
	CToolBarCtrl tb;
	auto hWndToolBar = tb.Create(hWnd, CWindow::rcDefault, nullptr, ATL_SIMPLE_TOOLBAR_PANE_STYLE | TBSTYLE_LIST, 0, ATL_IDW_TOOLBAR);
	tb.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);

	auto tbImages = CreateImageList(buttons, count, DpiHelper::Scale(size, hWnd));
	tb.SetImageList(tbImages);

	int image = 0;
	for (int i = 0; i < count; i++) {
		auto& b = buttons[i];
		if (b.id == 0)
			tb.AddSeparator(0);
		else
			tb.AddButton(b.id, b.style | (b.text ? BTNS_SHOWTEXT : 0), TBSTATE_ENABLED, b.image == 0 ? I_IMAGENONE : image++, b.text, 0);
	}
	return hWndToolBar;
}

HIMAGELIST ToolbarHelper::CreateImageList(const ToolBarButtonInfo* buttons, int count, int size) {
	CImageList images;
	images.Create(size, size, ILC_COLOR32, 4, 4);
	// one image for each button with an image, in order
	for (int i = 0; i < count; i++) {
		auto& b = buttons[i];
		if (b.id != 0 && b.image != 0) {
			CIcon icon(IconHelper::Load(b.image, size));
			ATLASSERT(icon);	// the buttons refer to the images by index
			images.AddIcon(icon);
		}
	}
	return images.Detach();
}

void ToolbarHelper::UpdateToolBarImages(HWND hToolBar, const ToolBarButtonInfo* buttons, int count, int size) {
	CToolBarCtrl tb(hToolBar);
	size = DpiHelper::Scale(size, hToolBar);
	CImageList old = tb.SetImageList(CreateImageList(buttons, count, size));
	if (old)
		old.Destroy();
	tb.AutoSize();
	tb.Invalidate();
}

POINT ToolbarHelper::GetDropdownMenuPoint(HWND hToolBar, UINT buttonId) {
	CToolBarCtrl tb(hToolBar);
	CRect rect;
	tb.GetItemRect(tb.CommandToIndex(buttonId), &rect);
	CPoint pt(rect.left, rect.bottom);
	tb.MapWindowPoints(HWND_DESKTOP, &pt, 1);
	return pt;
}
