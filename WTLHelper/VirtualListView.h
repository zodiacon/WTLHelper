#pragma once

#include "ColumnManager.h"
#include "ListViewhelper.h"
#include <memory>
#include <strsafe.h>

enum class ListViewRowCheck {
	None,
	Unchecked,
	Checked
};

struct ColumnsState {
	int Count{ 0 };
	int SortColumn{ -1 };
	bool SortAscending{ true };
	std::unique_ptr<int[]> Order;
	std::unique_ptr<LVCOLUMN[]> Columns;
	std::unique_ptr<std::wstring[]> Text;
	std::unique_ptr<int[]> Tags;
};

template<typename T>
struct CVirtualListView {
	BEGIN_MSG_MAP(CVirtualListView)
		NOTIFY_CODE_HANDLER(LVN_ODSTATECHANGED, OnStateChanged)
		NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemStateChanged)
		NOTIFY_CODE_HANDLER(LVN_ODFINDITEM, OnFindItem)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(NM_CLICK, OnClick)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
		ALT_MSG_MAP(1)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_ODSTATECHANGED, OnStateChanged)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemStateChanged)
		REFLECTED_NOTIFY_CODE_HANDLER(LVN_ODFINDITEM, OnFindItem)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_CLICK, OnClick)
		REFLECTED_NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
	END_MSG_MAP()

	struct SortInfo {
		int SortColumn = -1;
		UINT_PTR Id;
		HWND hWnd;
		bool SortAscending;
	private:
		friend struct CVirtualListView;
		int RealSortColumn = -1;
	};

	bool ClearSort(UINT_PTR id = 0) {
		auto si = FindById(id);
		if (si == nullptr)
			return false;

		auto header = CListViewCtrl(si->hWnd).GetHeader();
		HDITEM h;
		h.mask = HDI_FORMAT;
		header.GetItem(si->SortColumn, &h);
		h.fmt = (h.fmt & HDF_JUSTIFYMASK) | HDF_STRING;
		header.SetItem(si->SortColumn, &h);
		si->SortColumn = -1;
		return true;
	}

	bool ClearSort(HWND hWnd) {
		auto si = FindByHwnd(hWnd);
		if (si == nullptr)
			return false;

		auto header = CListViewCtrl(si->hWnd).GetHeader();
		HDITEM h;
		h.mask = HDI_FORMAT;
		header.GetItem(si->RealSortColumn, &h);
		h.fmt = (h.fmt & HDF_JUSTIFYMASK) | HDF_STRING;
		header.SetItem(si->RealSortColumn, &h);
		si->SortColumn = -1;
		return true;
	}

	LRESULT OnClick(int, LPNMHDR hdr, BOOL& handled) {
		auto lv = (NMITEMACTIVATE*)hdr;
		auto pT = static_cast<T*>(this);
		pT->OnListViewClick(hdr->hwndFrom, lv->iItem, lv->iSubItem, lv->ptAction);
		return 0;
	}

	LRESULT OnDoubleClick(int, LPNMHDR hdr, BOOL& handled) {
		WCHAR className[16];
		if (::GetClassName(hdr->hwndFrom, className, _countof(className)) && _wcsicmp(className, WC_LISTVIEW)) {
			handled = FALSE;
			return 0;
		}
		CListViewCtrl lv(hdr->hwndFrom);
		POINT pt;
		::GetCursorPos(&pt);
		POINT pt2(pt);
		lv.ScreenToClient(&pt);
		LVHITTESTINFO info{};
		info.pt = pt;
		lv.SubItemHitTest(&info);
		auto pT = static_cast<T*>(this);
		handled = pT->OnDoubleClickList(lv, info.iItem, info.iSubItem, pt2);
		return 0;
	}

	LRESULT OnRightClick(int, LPNMHDR hdr, BOOL& handled) {
		WCHAR className[16];
		if (!::GetClassName(hdr->hwndFrom, className, _countof(className))) {
			handled = FALSE;
			return 0;
		}
		POINT pt;
		::GetCursorPos(&pt);
		POINT pt2(pt);
		auto pT = static_cast<T*>(this);
		if (::wcscmp(className, WC_LISTVIEW) == 0) {
			CListViewCtrl lv(hdr->hwndFrom);
			lv.ScreenToClient(&pt);
			LVHITTESTINFO info{};
			info.pt = pt;
			lv.SubItemHitTest(&info);
			handled = pT->OnRightClickList(hdr->hwndFrom, info.iItem, info.iSubItem, pt2);
		}
		else if (::wcscmp(className, WC_HEADER) == 0) {
			CHeaderCtrl header(hdr->hwndFrom);
			header.ScreenToClient(&pt);
			HDHITTESTINFO hti;
			hti.pt = pt;
			int index = header.HitTest(&hti);
			if (index >= 0) {
				handled = pT->OnRightClickHeader(hdr->hwndFrom, index, pt2);
			}
		}
		else {
			handled = FALSE;
		}
		return 0;
	}

	bool OnRightClickHeader(HWND, int index, POINT const& pt) const {
		return false;
	}

	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const {
		return false;
	}

	bool OnDoubleClickList(HWND, int row, int col, POINT const& pt) const {
		return false;
	}

	void OnListViewClick(HWND, int row, int col, POINT const& pt) const {
	}

protected:
	ColumnManager* GetExistingColumnManager(HWND hListView) const {
		auto it = std::find_if(m_Columns.begin(), m_Columns.end(), [=](auto& cm) {
			return cm->GetListView() == hListView;
			});
		if (it != m_Columns.end())
			return (*it).get();
		return nullptr;
	}

	ColumnManager* GetColumnManager(HWND hListView) const {
		auto mgr = GetExistingColumnManager(hListView);
		if (mgr)
			return mgr;
		auto cm = std::make_unique<ColumnManager>(hListView);
		auto pcm = cm.get();
		m_Columns.push_back(std::move(cm));
		return pcm;
	}

	int GetRealColumn(HWND hListView, int column) const {
		auto cm = GetExistingColumnManager(hListView);
		return cm ? cm->GetRealColumn(column) : column;
	}

	LRESULT OnStateChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto lv = (NMLVODSTATECHANGE*)hdr;
		auto p = static_cast<T*>(this);
		p->OnStateChanged(hdr->hwndFrom, lv->iFrom, lv->iTo, lv->uOldState, lv->uNewState);
		return 0;
	}

	LRESULT OnItemStateChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto lv = (NMLISTVIEW*)hdr;
		auto p = static_cast<T*>(this);
		p->OnStateChanged(hdr->hwndFrom, lv->iItem, lv->iItem, lv->uOldState, lv->uNewState);
		return 0;
	}

	LRESULT OnGetDispInfo(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto lv = (NMLVDISPINFO*)hdr;
		auto& item = lv->item;
		auto col = GetRealColumn(hdr->hwndFrom, item.iSubItem);
		auto p = static_cast<T*>(this);
		if (item.mask & LVIF_TEXT) {
			if (auto text = p->GetExistingColumnText(hdr->hwndFrom, item.iItem, col); text)
				item.pszText = (PWSTR)text;
			else
				::StringCchCopy(item.pszText, item.cchTextMax, p->GetColumnText(hdr->hwndFrom, item.iItem, col));
		}
		if (item.mask & LVIF_IMAGE) {
			item.iImage = p->GetRowImage(hdr->hwndFrom, item.iItem, col);
		}
		if (item.mask & LVIF_PARAM) {
			item.lParam = p->GetRowParam(hdr->hwndFrom, item.iItem);
		}
		if (item.mask & LVIF_INDENT)
			item.iIndent = p->GetRowIndent(hdr->hwndFrom, item.iItem);
		if ((ListView_GetExtendedListViewStyle(hdr->hwndFrom) & LVS_EX_CHECKBOXES) && item.iSubItem == 0 && (item.mask & LVIF_STATE)) {
			item.state = INDEXTOSTATEIMAGEMASK((int)p->IsRowChecked(hdr->hwndFrom, item.iItem));
			item.stateMask = LVIS_STATEIMAGEMASK;

			if (item.iItem == m_Selected) {
				item.state |= LVIS_SELECTED;
				item.stateMask |= LVIS_SELECTED;
			}
		}
		else if (item.mask & LVIF_STATE)
			item.state = p->GetListViewItemState(hdr->hwndFrom, item.iItem);
		return 0;
	}

	PCWSTR GetExistingColumnText(HWND hWnd, int row, int column) const {
		return nullptr;
	}

	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const {
	}

	LRESULT OnFindItem(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		auto fi = (NMLVFINDITEM*)hdr;
		auto text = fi->lvfi.psz;
		auto len = ::wcslen(text);
		auto list = fi->hdr.hwndFrom;

		int selected = fi->iStart;
		int start = selected;
		int count = ListView_GetItemCount(list);
		WCHAR name[128]{};
		if (len >= _countof(name))
			len = _countof(name) - 1;
		int end = (fi->lvfi.flags & LVFI_WRAP) ? (count + start) : count;
		bool partial = fi->lvfi.flags & (LVFI_PARTIAL | LVFI_SUBSTRING);
		for (int i = start; i < end; i++) {
			ListView_GetItemText(list, i % count, 0, name, _countof(name));
			if (partial) {
				if (::_wcsnicmp(name, text, len) == 0)
					return i % count;
			}
			else {
				if (::_wcsicmp(name, text) == 0)
					return i % count;
			}
		}
		return -1;
	}

	void Sort(SortInfo const* si) {
		if (si == nullptr)
			return;
		auto p = static_cast<T*>(this);
		p->PreSort(si->hWnd);
		p->DoSort(si);
		p->PostSort(si->hWnd);
	}

	void IsSorting(bool sorting) {
		m_IsSorting = sorting;
	}

	LRESULT OnColumnClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/) {
		IsSorting(true);
		auto lv = (NMLISTVIEW*)hdr;
		auto col = GetRealColumn(hdr->hwndFrom, lv->iSubItem);

		auto p = static_cast<T*>(this);
		if (!p->IsSortable(hdr->hwndFrom, col))
			return 0;

		auto si = FindByHwnd(hdr->hwndFrom);
		if (si == nullptr) {
			SortInfo s;
			s.hWnd = hdr->hwndFrom;
			s.Id = hdr->idFrom;
			m_Controls.push_back(s);
			si = m_Controls.data() + m_Controls.size() - 1;
		}

		auto oldSortColumn = si->SortColumn;
		if (col == si->SortColumn)
			si->SortAscending = !si->SortAscending;
		else {
			si->SortColumn = col;
			si->SortAscending = true;
		}

		CListViewCtrl list(hdr->hwndFrom);
		auto header = list.GetHeader();

		HDITEM h;
		if (si->RealSortColumn >= 0) {
			h.mask = HDI_FORMAT;
			header.GetItem(si->RealSortColumn, &h);
			h.fmt = (h.fmt & HDF_JUSTIFYMASK) | HDF_STRING;
			header.SetItem(si->RealSortColumn, &h);
		}
		si->RealSortColumn = lv->iSubItem;

		h.mask = HDI_FORMAT;
		header.GetItem(lv->iSubItem, &h);
		h.fmt = (h.fmt & HDF_JUSTIFYMASK) | HDF_STRING | (si->SortAscending ? HDF_SORTUP : HDF_SORTDOWN);
		header.SetItem(lv->iSubItem, &h);

		//if (si->RealSortColumn >= 0) {
		//	h.mask = HDI_FORMAT;
		//	header.GetItem(oldSortColumn, &h);
		//	h.fmt = (h.fmt & HDF_JUSTIFYMASK) | HDF_STRING;
		//	header.SetItem(oldSortColumn, &h);
		//}

		Sort(si);
		IsSorting(false);
		list.RedrawItems(list.GetTopIndex(), list.GetTopIndex() + list.GetCountPerPage());

		return 0;
	}

	void Sort(CListViewCtrl list, bool redraw = false) {
		auto si = GetSortInfo(list);
		if (si) {
			static_cast<T*>(this)->DoSort(si);
			redraw = true;
		}
		if (redraw)
			list.RedrawItems(list.GetTopIndex(), list.GetTopIndex() + list.GetCountPerPage());
	}

	bool IsSorting() const {
		return m_IsSorting;
	}

	bool IsSortable(HWND, int) const {
		return true;
	}

	bool LoadState(HWND h, ColumnsState const& state) {
		if (state.Count == 0)
			return false;

		CListViewCtrl lv(h);
		while (lv.DeleteColumn(0))
			;
		auto header = lv.GetHeader();
		auto empty = header.GetItemCount() == 0;
		HDITEM hdi;
		hdi.mask = HDI_LPARAM;
		for (int i = 0; i < state.Count; i++) {
			if (state.Text) {
				state.Columns[i].pszText = state.Text[i].data();
			}
			if(empty)
				lv.InsertColumn(i, state.Columns.get() + i);
			else
				lv.SetColumn(i, state.Columns.get() + i);
			hdi.lParam = state.Tags[i];
			header.SetItem(i, &hdi);
		}
		if (state.SortColumn < 0)
			ClearSort(h);
		else {
			auto si = GetSortInfo(h);
			si->SortAscending = state.SortAscending;
			si->SortColumn = state.SortColumn;
		}
		GetColumnManager(h)->AddFromControl();
		lv.SetColumnOrderArray(state.Count, state.Order.get());
		return true;
	}

	ColumnsState SaveState(HWND h, bool names = true) {
		CListViewCtrl lv(h);
		ColumnsState state;
		auto si = GetSortInfo(h);
		auto header = lv.GetHeader();
		auto count = header.GetItemCount();
		state.Order = std::make_unique<int[]>(count);
		state.Columns = std::make_unique<LVCOLUMN[]>(count);
		state.Tags = std::make_unique<int[]>(count);
		if (names)
			state.Text = std::make_unique<std::wstring[]>(count);
		if (si) {
			state.SortColumn = si->SortColumn;
			state.SortAscending = si->SortAscending;
		}
		lv.GetColumnOrderArray(count, state.Order.get());
		LVCOLUMN lvc;
		lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_MINWIDTH | (names ? LVCF_TEXT : 0);
		state.Count = count;
		WCHAR text[128];
		HDITEM hdi;
		hdi.mask = HDI_LPARAM;
		for (int i = 0; i < count; i++) {
			state.Columns[i].mask = lvc.mask;
			if (names) {
				state.Columns[i].cchTextMax = _countof(text);
				state.Columns[i].pszText = text;
			}
			lv.GetColumn(i, &state.Columns[i]);
			if (names)
				state.Text[i] = state.Columns[i].pszText;
			header.GetItem(i, &hdi);
			state.Tags[i] = (int)hdi.lParam;
		}
		return state;
	}

	int m_SaveSelected{ -1 };
	CString m_SaveSelectedText;

	void PreSort(HWND h) {
		int start = 0;
		int count = static_cast<T*>(this)->GetSaveColumnRange(h, start);
		CListViewCtrl lv(h);
		m_SaveSelected = (lv.GetStyle() & LVS_SINGLESEL) ? lv.GetSelectedIndex() : lv.GetSelectionMark();
		if (m_SaveSelected >= 0 && count >= 0)
			m_SaveSelectedText = ListViewHelper::GetRowColumnsAsString(lv, m_SaveSelected, start, count);
	}

	void PostSort(HWND h) {
		if (m_SaveSelected >= 0) {
			int start = 0;
			int count = static_cast<T*>(this)->GetSaveColumnRange(h, start);
			if (count >= 0) {
				CListViewCtrl lv(h);
				int index = ListViewHelper::FindRow(lv, start, count, m_SaveSelectedText);
				ATLASSERT(index >= 0);
				if ((lv.GetStyle() & LVS_SINGLESEL) == 0)
					lv.SelectAllItems(false);
				lv.SelectItem(index);
			}
		}
	}

	int GetSortColumn(HWND hWnd, UINT_PTR id = 0) const {
		auto si = FindById(id);
		return si ? GetRealColumn(hWnd, si->SortColumn) : -1;
	}
	int IsSortAscending(UINT_PTR id) const {
		auto si = FindById(id);
		return si ? si->SortAscending : false;
	}

	SortInfo* GetSortInfo(HWND h = nullptr) {
		if (h == nullptr && m_Controls.empty())
			return nullptr;
		return h == nullptr ? &m_Controls[0] : FindByHwnd(h);
	}

	void DoSort(const SortInfo*) {}
	CString GetColumnText(HWND hWnd, int row, int column) const {
		return L"";
	}

	LPARAM GetRowParam(HWND, int row) const {
		return 0;
	}

	int GetRowImage(HWND hWnd, int row, int col) const {
		return -1;
	}

	int GetRowIndent(HWND, int row) const {
		return 0;
	}

	DWORD GetListViewItemState(HWND, int row) const {
		return 0;
	}

	ListViewRowCheck IsRowChecked(HWND, int row) const {
		return ListViewRowCheck::None;
	}

	int GetSaveColumnRange(HWND, int&) const {
		return 0;
	}

private:
	SortInfo* FindById(UINT_PTR id) const {
		if (id == 0)
			return m_Controls.empty() ? nullptr : &m_Controls[0];
		for (auto& info : m_Controls)
			if (info.Id == id)
				return &info;
		return nullptr;
	}

	SortInfo* FindByHwnd(HWND h) const {
		if (h == nullptr)
			return m_Controls.empty() ? nullptr : &m_Controls[0];
		for (auto& info : m_Controls)
			if (info.hWnd == h)
				return &info;
		return nullptr;
	}

	mutable std::vector<SortInfo> m_Controls;
	mutable std::vector<std::unique_ptr<ColumnManager>> m_Columns;
	int m_Selected = -1;
	bool m_IsSorting{ false };
};
