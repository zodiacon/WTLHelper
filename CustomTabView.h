#pragma once

#define WM_WINDOW_MENU_BUILT (WM_APP+111)

class CCustomTabView : public CTabViewImpl<CCustomTabView> {
public:
	DECLARE_WND_CLASS_EX(_T("WTL_TabView"), 0, COLOR_APPWORKSPACE)

	//BEGIN_MSG_MAP(CCustomTabView)
	//	CHAIN_MSG_MAP(CTabViewImpl<CCustomTabView>)
	//	FORWARD_NOTIFICATIONS()
	//ALT_MSG_MAP(1)
	//	CHAIN_MSG_MAP_ALT(CTabViewImpl<CCustomTabView>, 1)
	//END_MSG_MAP()

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
