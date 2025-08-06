// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "TreeListViewCtrl.h"

class CProcessTreeListView : 
	public CFrameWindowImpl<CProcessTreeListView, CWindow, CControlWinTraits> {
	using BaseFrame = CFrameWindowImpl<CProcessTreeListView, CWindow, CControlWinTraits>;
public:

	BOOL PreTranslateMessage(MSG* pMsg);

	virtual void OnFinalMessage(HWND /*hWnd*/);

protected:
	BEGIN_MSG_MAP(CProcessTreeListView)
		NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDED, OnItemChanged)
		NOTIFY_CODE_HANDLER(TLN_GETDISPINFO, OnTreeListGetDispInfo)
		NOTIFY_CODE_HANDLER(TVN_GETDISPINFO, OnTreeGetDispInfo)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(BaseFrame)
		REFLECT_NOTIFICATIONS_EX()
	END_MSG_MAP()

private:
	struct ProcessInfo {
		DWORD Id;
		CString Name;
		DWORD Threads;
	};

	void Refresh();

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnItemChanged(int /*idCtrl*/, LPNMHDR /*nymph*/, BOOL& /*bHandled*/);
	LRESULT OnTreeGetDispInfo(int /*idCtrl*/, LPNMHDR /*nymph*/, BOOL& /*bHandled*/);
	LRESULT OnTreeListGetDispInfo(int /*idCtrl*/, LPNMHDR /*nymph*/, BOOL& /*bHandled*/);
	LRESULT OnHeaderItemClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);

	std::unordered_map<HTREEITEM, ProcessInfo> m_Processes;

	CTreeListView m_TreeList;
	int m_SortColumn{ -1 };
	int m_HighlightDiff{ 2000 };
	bool m_SortAscending{ true };
	bool m_TrackSelectedItem{ false };
	bool m_ScrollToNewProcesses{ false };

};
