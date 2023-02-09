#include "pch.h"
#include "TreeListViewCtrl.h"

HTLItem CTreeListView::AddItem(PCWSTR text, int image) {
	return MapIndexToID(InsertItem(GetItemCount(), text, image));
}

HTLItem CTreeListView::AddChildItem(HTLItem hItem, PCWSTR text, int image) {
	ListViewItem item;
	int index = MapIDToIndex(hItem);
	item.mask = LVIF_INDENT;
	item.iItem = index;
	item.iSubItem = 0;
	ATLVERIFY(GetItem(&item));

	item.mask |= LVIF_TEXT | LVIF_IMAGE;
	item.iIndent++;
	item.iImage = image;
	item.iItem = index + 1;
	item.pszText = (PWSTR)text;
	int n = InsertItem(&item);
	auto id = MapIndexToID(n);
	if (auto it = m_Collapsed.find(hItem); it != m_Collapsed.end()) {
		//
		// collapsed parent, add to data only
		//
		it->second.push_back(SaveItem(n));
		SetItemState(MapIDToIndex(hItem), INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK);
	}
	else {
		SetItemState(MapIDToIndex(hItem), INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
	}
	return id;
}

int CTreeListView::InsertChildItems(int index, std::vector<HTLItem>& children) {
	int n = index + 1;
	for (auto it = children.rbegin(); it != children.rend(); ++it) {
		auto& child = m_HiddenItems.at(*it);
		auto id = child.Id;
		child.iItem = n;
		child.iSubItem = 0;
		child.pszText = child.Text;
		child.mask = LVIF_INDENT | LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE | LVIF_STATE;
		child.stateMask = LVIS_STATEIMAGEMASK;
		int n2 = InsertItem(&child);
		int c = 1;
		for (auto const& si : child.SubItems)
			CListViewCtrl::SetItemText(n2, c++, si.c_str());
		if (child.Collapsed)
			DoCollapseItem(MapIndexToID(n));
		m_HiddenItems.erase(id);
	}
	return 1;
}

int CTreeListView::InsertChildItems(int index) {
	auto data = MapIndexToID(index);
	LVITEM item{};
	item.mask = LVIF_INDENT;
	SuspendSetItemText();
	InsertChildItems(index, m_Collapsed[data]);
	SuspendSetItemText(false);
	return 1;
}

LRESULT CTreeListView::OnClick(int, LPNMHDR hdr, BOOL&) {
	auto lv = (NMITEMACTIVATE*)hdr;
	if (lv->iSubItem != 0)
		return 0;

	UINT flags;
	int n = HitTest(lv->ptAction, &flags);
	if (n == lv->iItem && flags == LVHT_ONITEMSTATEICON) {
		auto data = MapIndexToID(n);
		auto it = m_Collapsed.find(data);
		if (it == m_Collapsed.end()) {
			CollapseItem(data);
		}
		else {
			// expand
			LVITEM item;
			item.mask = LVIF_INDENT;
			item.iItem = n;
			item.iSubItem = 0;
			GetItem(&item);
			SetItemState(n, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
			InsertChildItems(n);
			m_Collapsed.erase(MapIndexToID(n));
		}
	}
	return 0;
}

bool CTreeListView::CollapseItem(HTLItem hItem) {
	SetRedraw(FALSE);
	DoCollapseItem(hItem);
	SetItemState(MapIDToIndex(hItem), INDEXTOSTATEIMAGEMASK(2), LVIS_STATEIMAGEMASK);
	SetRedraw(TRUE);
	return true;
}

bool CTreeListView::ExpandItem(HTLItem hItem) {
	SetRedraw(FALSE);
	DoExpandItem(hItem);
	SetItemState(MapIDToIndex(hItem), INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
	SetRedraw(TRUE);
	return true;
}

void CTreeListView::DoExpandItem(HTLItem hItem) {
	LVITEM item{};
	item.mask = LVIF_INDENT;
	item.iItem = MapIDToIndex(hItem);
	GetItem(&item);
	SetItemState(item.iItem, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
	InsertChildItems(item.iItem);
	m_Collapsed.erase(hItem);
}

void CTreeListView::DoCollapseItem(HTLItem hItem) {
	int n = MapIDToIndex(hItem);
	ListViewItem item{};
	item.mask = LVIF_INDENT | LVIF_STATE;
	item.stateMask = LVIS_STATEIMAGEMASK;
	item.iItem = n;

	ATLVERIFY(GetItem(&item));
	std::vector<HTLItem> children;
	int i = n + 1;
	int target = item.iIndent;
	while (true) {
		item.iItem = i;
		if (!GetItem(&item) || item.iIndent <= target)
			break;
		if (item.state == INDEXTOSTATEIMAGEMASK(2)) {
			//
			// collapsed
			//
			DoExpandItem(MapIndexToID(i));
			item.Collapsed = true;
		}
		HTLItem id;
		children.push_back(id = SaveItem(i));
		DeleteItem(i);
	}
	m_Collapsed.try_emplace(hItem, std::move(children));
}

bool CTreeListView::SetItemText(HTLItem hItem, int subItem, PCWSTR text) {
	if (m_HiddenItems.contains(hItem))
		return DoSetItemText(hItem, subItem, text);
	return CListViewCtrl::SetItemText(MapIDToIndex(hItem), subItem, text);
}


LRESULT CTreeListView::OnSetItemText(UINT, WPARAM n, LPARAM lp, BOOL&) {
	auto item = reinterpret_cast<LVITEM*>(lp);
	DoSetItemText((int)n, item->iSubItem, item->pszText);
	return DefWindowProc();
}

bool CTreeListView::DoSetItemText(HTLItem hItem, int subitem, PCWSTR text) {
	auto& lvi = m_HiddenItems.at(hItem);
	if (subitem == 0)
		wcscpy_s(lvi.Text, text);
	else {
		if (lvi.SubItems.size() < GetHeader().GetItemCount() - 1)
			lvi.SubItems.resize(GetHeader().GetItemCount() - 1);
		lvi.SubItems[subitem - 1] = text;
	}
	return true;
}

LRESULT CTreeListView::OnSetItem(UINT m, WPARAM, LPARAM lp, BOOL& handled) {
	auto item = reinterpret_cast<LVITEM*>(lp);
	if ((item->mask & LVIF_TEXT) && m_HiddenItems.contains(MapIndexToID(item->iItem)))
		return DoSetItemText(MapIndexToID(item->iItem), item->iSubItem, item->pszText);
	return DefWindowProc();
}

void CTreeListView::SuspendSetItemText(bool suspend) {
	m_SuspendSetItem = suspend;
}

HTLItem CTreeListView::SaveItem(int index) {
	auto id = MapIndexToID(index);
	ListViewItem item{};
	item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT | LVIF_PARAM | LVIF_STATE;
	item.stateMask = LVIS_STATEIMAGEMASK;
	item.iItem = index;
	item.Id = id;
	GetItem(&item);
	auto columns = GetHeader().GetItemCount();
	for (int c = 1; c < columns; c++) {
		CString text;
		GetItemText(index, c, text);
		item.SubItems.push_back((PCWSTR)text);
	}
	m_HiddenItems.insert({ id, std::move(item) });
	return id;
}

bool CTreeListView::IsExpanded(HTLItem hItem) const {
	return !m_Collapsed.contains(hItem);
}

LRESULT CTreeListView::DoDeleteItem(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& handled) {
	if (m_Deleting)
		return FALSE;

	int index = (int)wParam;
	LVITEM item{};
	item.mask = LVIF_INDENT;
	item.iItem = index;
	GetItem(&item);
	int indent = item.iIndent;
	index++;
	m_Deleting = true;
	do {
		item.iItem = index;
		if (!GetItem(&item))
			break;
		if (item.iIndent <= index)
			break;
		DefWindowProc(uMsg, index, lParam);
	} while (true);
	m_Deleting = false;
	handled = FALSE;

	return TRUE;
}

bool CTreeListView::SetIcon(HICON hIcon, bool expanded) {
	return GetImageList(LVSIL_STATE).ReplaceIcon(expanded ? 0 : 1, hIcon) >= 0;
}

LRESULT CTreeListView::DoCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CImageList images;
	images.Create(16, 16, ILC_COLOR32, 2, 0);
	images.AddIcon(AtlLoadIconImage(IDI_EXCLAMATION));
	images.AddIcon(AtlLoadIconImage(IDI_HAND));
	SetImageList(images, LVSIL_STATE);

	return DefWindowProc();
}

