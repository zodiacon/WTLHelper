// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

using HTLItem = UINT;

class CTreeListView : public CWindowImpl<CTreeListView, CListViewCtrl> {
public:
	DECLARE_WND_SUPERCLASS(L"WYL_TreeListView", CListViewCtrl::GetWndClassName())

	HTLItem AddChildItem(HTLItem index, PCWSTR text, int image);
	HTLItem AddItem(PCWSTR text, int image);
	bool IsExpanded(HTLItem hItem) const;
	bool CollapseItem(HTLItem hItem);
	bool ExpandItem(HTLItem hItem);
	bool SetIcons(HICON hIconExpanded, HICON hIconCollapsed, bool dark) const;
	bool SetItemText(HTLItem hItem, int subItem, PCWSTR text);

protected:
	void DoCollapseItem(HTLItem hItem);
	void DoExpandItem(HTLItem hItem);

	BEGIN_MSG_MAP(CTreeListView)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLMouseButtonUp)
		MESSAGE_HANDLER(LVM_DELETEITEM, DoDeleteItem)
		MESSAGE_HANDLER(LVM_DELETEALLITEMS, DoDeleteAllItems)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, DoCreate)
		if (!m_SuspendSetItem) {
			MESSAGE_HANDLER(LVM_SETITEMTEXT, OnSetItemText)
			MESSAGE_HANDLER(LVM_SETITEM, OnSetItem)
		}
	END_MSG_MAP()

	struct ListViewItem : LVITEMW {
		WCHAR Text[64];
		UINT Id = -1;
		std::vector<std::wstring> SubItems;
		bool Collapsed{ false };

		ListViewItem() : LVITEM{} {
			pszText = Text;
			cchTextMax = _countof(Text);
		}
	};
	int InsertChildItems(int index);
	int InsertChildItems(int index, std::vector<HTLItem>& children);

	void SuspendSetItemText(bool suspend = true);
	bool DoSetItemText(HTLItem n, int subitem, PCWSTR text);
	HTLItem SaveItem(HTLItem index);

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInsertItem(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnSetItemText(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT DoDeleteItem(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT DoDeleteAllItems(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT DoCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLMouseButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	std::unordered_map<HTLItem, std::vector<HTLItem>> m_Collapsed;
	std::unordered_map<HTLItem, ListViewItem> m_HiddenItems;
	CImageList m_Light, m_Dark;
	bool m_SuspendSetItem{ false };
	bool m_Deleting{ false };
};
