#pragma once

#define WM_WINDOW_MENU_BUILT (WM_APP+111)

class CCustomTabView : public CTabViewImpl<CCustomTabView> {
public:
	DECLARE_WND_CLASS_EX(L"WTL_TabView", CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, COLOR_APPWORKSPACE)

	BEGIN_MSG_MAP(CCustomTabView)
		//MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		CHAIN_MSG_MAP(CTabViewImpl<CCustomTabView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CTabViewImpl<CCustomTabView>, 1)
	END_MSG_MAP()

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) const;
	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/);

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
			if(map) { \
				LRESULT result;		\
				if (map->ProcessWindowMessage(m_hWnd, uMsg, wParam, lParam, result, msgMapId))	\
					return TRUE;	\
			}	\
		}		\
	}

