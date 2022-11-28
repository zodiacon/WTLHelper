#pragma once

#define WM_WINDOW_MENU_BUILT (WM_APP+111)

class CCustomTabView : public CTabViewImpl<CCustomTabView> {
public:
	DECLARE_WND_CLASS(L"WTL_TabView")

	bool CreateTabControl();
	void UpdateLayout();
	void SetRedraw(bool redraw);
	void UpdateMenu();
	void BuildWindowMenu(HMENU hMenu, int nMenuItemsCount = 10, bool bEmptyMenuItem = true, bool bWindowsMenuItem = true, bool bActivePageMenuItem = true, bool bActiveAsDefaultMenuItem = false);

	bool m_Redraw{ true };
};

#define COMMAND_TABVIEW_HANDLER(tabs, msgMapId)	\
	if(uMsg == WM_COMMAND) {	\
		int page = tabs.GetActivePage();	\
		if(page >= 0) {		\
			auto map = (CMessageMap*)tabs.GetPageData(page);	\
			ATLASSERT(map);		\
			LRESULT result;		\
			if(map->ProcessWindowMessage(m_hWnd, uMsg, wParam, lParam, result, msgMapId))	\
				return TRUE;	\
		}		\
	}
