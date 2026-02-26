#pragma once

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
