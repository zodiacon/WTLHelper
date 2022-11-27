#pragma once

template<typename T>
struct CTreeViewHelper {
	template<typename TData, typename TIcon>
	HTREEITEM InsertTreeItem(CTreeViewCtrl& tree, PCWSTR text, TIcon image, TIcon selectedImage, TData const& data, HTREEITEM hParent = TVI_ROOT, HTREEITEM hAfter = TVI_LAST) {
		auto hItem = tree.InsertItem(text, static_cast<int>(image), static_cast<int>(selectedImage), hParent, hAfter);
		if(hItem)
			tree.SetItemData(hItem, static_cast<DWORD_PTR>(data));
		return hItem;
	}

	template<typename TData, typename TIcon>
	HTREEITEM InsertTreeItem(CTreeViewCtrl& tree, PCWSTR text, TIcon image, TData const& data, HTREEITEM hParent = TVI_ROOT, HTREEITEM hAfter = TVI_LAST) {
		return InsertTreeItem(tree, text, static_cast<int>(image), static_cast<int>(image), data, hParent, hAfter);
	}

	template<typename TData>
	static TData GetItemData(CTreeViewCtrl const& tree, HTREEITEM hItem) {
		return static_cast<TData>(tree.GetItemData(hItem));
	}

	template<typename TData>
	static void SetItemData(CTreeViewCtrl& tree, HTREEITEM hItem, TData const& data) {
		tree.SetItemData(hItem, static_cast<DWORD_PTR>(data));
	}

	static HTREEITEM FindItem(CTreeViewCtrl& tree, HTREEITEM hParent, PCWSTR path) {
		int start = 0;
		CString spath(path);
		if (spath[0] == L'\\') {
			// skip first
			spath = spath.Mid(spath.Find(L'\\', 1));
		}
		HTREEITEM hItem = nullptr;
		while (hParent) {
			auto name = spath.Tokenize(L"\\", start);
			if (name.IsEmpty())
				break;
			tree.Expand(hParent, TVE_EXPAND);
			hItem = FindChild(tree, hParent, name);
			if (!hItem)
				break;
			hParent = hItem;
		}
		return hItem;
	}

	static HTREEITEM FindChild(CTreeViewCtrl& tree, HTREEITEM item, PCWSTR name) {
		item = tree.GetChildItem(item);
		while (item) {
			CString text;
			tree.GetItemText(item, text);
			if (text.CompareNoCase(name) == 0)
				return item;
			item = tree.GetNextSiblingItem(item);
		}
		return nullptr;
	}

	template<typename TData>
	static HTREEITEM FindChildByData(CTreeViewCtrl const& tree, HTREEITEM item, TData const& data) {
		item = tree.GetChildItem(item);
		while (item) {
			if (GetItemData<TData>(tree, item) == data)
				return item;

			auto item2 = FindChildByData(tree, item, data);
			if (item2)
				return item2;
			item = tree.GetNextSiblingItem(item);
		}
		return nullptr;
	}

	template<typename TData>
	HTREEITEM FindItemByData(CTreeViewCtrl const& tree, HTREEITEM hParent, TData const& data) const {
		int start = 0;
		HTREEITEM hItem = nullptr;
		while (hParent) {
			hItem = FindChildByData(tree, hParent, data);
			if (hItem)
				break;
			hParent = tree.GetNextSiblingItem(hParent);
		}
		return hItem;
	}

	static CString GetFullItemPath(CTreeViewCtrl const& tree, HTREEITEM hItem) {
		CString path;
		while (hItem) {
			CString name;
			tree.GetItemText(hItem, name);
			path = name + (path.IsEmpty() ? CString() : (L"\\" + path));
			hItem = tree.GetParentItem(hItem);
		}
		return path.TrimRight(L'\\');
	}

protected:
	BEGIN_MSG_MAP(CTreeViewHelper)
		NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnSelChanged)
		NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
	ALT_MSG_MAP(1)
		REFLECTED_NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnSelChanged)
		REFLECTED_NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
	END_MSG_MAP()

	LRESULT OnItemExpanding(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto pT = static_cast<T*>(this);
		auto tv = (NMTREEVIEW*)hdr;
		return pT->OnTreeItemExpanding(hdr->hwndFrom, tv->itemNew.hItem, tv->itemNew.state, tv->action);
	}

	LRESULT OnSelChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto pT = static_cast<T*>(this);
		auto tv = (NMTREEVIEW*)hdr;
		pT->OnTreeSelChanged(hdr->hwndFrom, tv->itemOld.hItem, tv->itemNew.hItem);
		return 0;
	}

	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		CPoint pt;
		::GetCursorPos(&pt);
		CPoint pt2(pt);
		CTreeViewCtrl tv(hdr->hwndFrom);
		tv.ScreenToClient(&pt);
		auto hItem = tv.HitTest(pt, nullptr);
		if (hItem)
			return static_cast<T*>(this)->OnTreeRightClick(tv, hItem, pt2);
		return 0;
	}

	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		CPoint pt;
		::GetCursorPos(&pt);
		CTreeViewCtrl tv(hdr->hwndFrom);
		tv.ScreenToClient(&pt);
		auto hItem = tv.HitTest(pt, nullptr);
		if (hItem)
			return static_cast<T*>(this)->OnTreeDoubleClick(tv, hItem);
		return 0;
	}

private:
	// overridables

	void OnTreeSelChanged(HWND tree, HTREEITEM hOld, HTREEITEM hNew) {}
	bool OnTreeItemExpanding(HWND tree, HTREEITEM hItem, DWORD state, DWORD action) {
		return false;
	}

	bool OnTreeRightClick(HWND tree, HTREEITEM hItem, POINT const& pt) {
		return false;
	}

	bool OnTreeDoubleClick(HWND tree, HTREEITEM hItem) {
		return false;
	}

};
