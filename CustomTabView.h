#pragma once

class CCustomTabView : public CTabViewImpl<CCustomTabView> {
public:
	DECLARE_WND_CLASS_EX(_T("WTL_TabView"), 0, COLOR_APPWORKSPACE)

	bool CreateTabControl();
	void UpdateLayout();
	void SetRedraw(bool redraw);
	void UpdateMenu();
	void BuildWindowMenu(HMENU hMenu, int nMenuItemsCount = 10, bool bEmptyMenuItem = true, bool bWindowsMenuItem = true, bool bActivePageMenuItem = true, bool bActiveAsDefaultMenuItem = false);

	bool m_Redraw{ true };
};
