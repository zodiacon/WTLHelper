#include "pch.h"
#include "TreeListViewCtrl.h"
#include "ThemeHelper.h"
#include "Theme.h"
#include "WTLHelperRes.h"

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
		it->second.push_back(SaveItem(id));
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
		ATLASSERT(n2 >= 0);
		int c = 1;
		for (auto const& si : child.SubItems)
			CListViewCtrl::SetItemText(n2, c++, si.c_str());
		//if (child.state & INDEXTOSTATEIMAGEMASK(2))
		//	DoCollapseItem(MapIndexToID(n));
		if(m_Collapsed.contains(id))
			DoExpandItem(MapIndexToID(n));
		m_HiddenItems.erase(*it);
	}
	return 1;
}

int CTreeListView::InsertChildItems(int index) {
	auto id = MapIndexToID(index);
	SuspendSetItemText();
	InsertChildItems(index, m_Collapsed[id]);
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
		auto id = MapIndexToID(n);
		auto it = m_Collapsed.find(id);
		if (it == m_Collapsed.end()) {
			CollapseItem(id);
		}
		else {
			// expand
			SetItemState(n, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
			InsertChildItems(n);
			m_Collapsed.erase(id);
			EnsureVisible(n, FALSE);
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
		children.push_back(SaveItem(MapIndexToID(i)));
		i++;
	}
	for (auto child : children)
		DeleteItem(MapIDToIndex(child));
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
		if ((int)lvi.SubItems.size() < GetHeader().GetItemCount() - 1)
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

HTLItem CTreeListView::SaveItem(HTLItem id) {
	ListViewItem item;
	item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT | LVIF_PARAM | LVIF_STATE;
	item.stateMask = LVIS_STATEIMAGEMASK;
	item.iItem = MapIDToIndex(id);
	item.Id = id;
	ATLVERIFY(GetItem(&item));
	auto columns = GetHeader().GetItemCount();
	for (int c = 1; c < columns; c++) {
		CString text;
		GetItemText(item.iItem, c, text);
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

bool CTreeListView::SetIcons(HICON hIconExpanded, HICON hIconCollapsed, bool dark) const {
	GetImageList(LVSIL_STATE).ReplaceIcon(dark ? 2 : 0, hIconExpanded);
	GetImageList(LVSIL_STATE).ReplaceIcon(dark ? 3 : 1, hIconCollapsed);
	return true;
}

LRESULT CTreeListView::DoCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_Light.Create(16, 16, ILC_COLOR32, 2, 0);
	m_Dark.Create(16, 16, ILC_COLOR32, 2, 0);
	UINT icons[] = { IDI_EXPANDED, IDI_COLLAPSED, IDI_EXPANDED2, IDI_COLLAPSED2 };
	m_Light.AddIcon(AtlLoadIconImage(IDI_EXPANDED, 0, 16, 16));
	m_Light.AddIcon(AtlLoadIconImage(IDI_COLLAPSED, 0, 16, 16));
	m_Dark.AddIcon(AtlLoadIconImage(IDI_EXPANDED2, 0, 16, 16));
	m_Dark.AddIcon(AtlLoadIconImage(IDI_COLLAPSED2, 0, 16, 16));

	SetImageList(ThemeHelper::IsDefault() ? m_Light : m_Dark, LVSIL_STATE);

	return DefWindowProc();
}

LRESULT CTreeListView::DoDeleteAllItems(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	m_Collapsed.clear();
	m_HiddenItems.clear();
	bHandled = FALSE;
	return 0;
}

LRESULT CTreeListView::OnLMouseButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	CPoint pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	UINT flags;
	int n = HitTest(pt, &flags);
	if (flags == LVHT_ONITEMSTATEICON && GetItemState(n, LVIS_STATEIMAGEMASK) > 0) {
		auto id = MapIndexToID(n);
		auto it = m_Collapsed.find(id);
		if (it == m_Collapsed.end()) {
			CollapseItem(id);
		}
		else {
			// expand
			SetItemState(n, INDEXTOSTATEIMAGEMASK(1), LVIS_STATEIMAGEMASK);
			InsertChildItems(n);
			m_Collapsed.erase(id);
			EnsureVisible(n, FALSE);
		}
	}
	return 0;
}

LRESULT CTreeListView::OnUpdateTheme(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/) {
	auto theme = reinterpret_cast<Theme*>(lParam);
	SetBkColor(theme->BackColor);
	SetTextColor(theme->TextColor);
	SetImageList(theme->IsDefault() ? m_Light : m_Dark, LVSIL_STATE);

	return 0;
}
