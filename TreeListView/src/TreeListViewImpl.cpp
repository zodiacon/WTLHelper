#define _ATL_NO_AUTOMATIC_NAMESPACE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlmisc.h>
#include <atlgdi.h>
#include <atltheme.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <algorithm>
#include <cwctype>

#include "../include/TreeListView.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

// ---- OnCreate ---------------------------------------------------------------

LRESULT CTreeListViewCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	CRect rc;
	GetClientRect(rc);

	m_List.Create(m_hWnd, rc, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_NOSORTHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		WS_EX_CLIENTEDGE, IDC_LIST);

	m_List.SetExtendedListViewStyle(
		LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);

	UINT dpi = ::GetDpiForWindow(m_hWnd);
	m_IndentWidth = ::GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	m_Theme.OpenThemeData(m_hWnd, L"TREEVIEW");
	m_ThemeBtn.OpenThemeData(m_hWnd, L"BUTTON");
	SetWindowSubclass(m_List.m_hWnd, ListSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
	return 0;
}

// ---- OnDestroy --------------------------------------------------------------

LRESULT CTreeListViewCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
	RemoveWindowSubclass(m_List.m_hWnd, ListSubclassProc, 0);
	m_Theme.CloseThemeData();
	m_ThemeBtn.CloseThemeData();
	DeleteTree(&m_Root);
	return 0;
}

// ---- OnSize -----------------------------------------------------------------

LRESULT CTreeListViewCtrl::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_List.IsWindow())
		m_List.MoveWindow(0, 0, LOWORD(lParam), HIWORD(lParam));
	return 0;
}

// ---- OnThemeChanged ---------------------------------------------------------

LRESULT CTreeListViewCtrl::OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Theme.CloseThemeData();
	m_ThemeBtn.CloseThemeData();
	m_Theme.OpenThemeData(m_hWnd, L"TREEVIEW");
	m_ThemeBtn.OpenThemeData(m_hWnd, L"BUTTON");
	if (m_List.IsWindow()) m_List.Invalidate();
	return 0;
}

// ---- OnDpiChanged -----------------------------------------------------------

LRESULT CTreeListViewCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	m_IndentWidth = ::GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	m_Theme.CloseThemeData();
	m_ThemeBtn.CloseThemeData();
	m_Theme.OpenThemeData(m_hWnd, L"TREEVIEW");
	m_ThemeBtn.OpenThemeData(m_hWnd, L"BUTTON");
	if (m_List.IsWindow()) m_List.Invalidate();
	return 0;
}

// ---- Column API -------------------------------------------------------------

int CTreeListViewCtrl::InsertColumn(int col, LPCWSTR title, int width, int fmt) {
	LVCOLUMNW lvc = {};
	lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
	lvc.pszText = const_cast<LPWSTR>(title);
	lvc.cx = width;
	lvc.fmt = fmt;
	int result = m_List.InsertColumn(col, &lvc);
	if (result >= 0)
		m_ColCount = m_List.GetHeader().GetItemCount();
	return result;
}

BOOL CTreeListViewCtrl::DeleteColumn(int col) {
	BOOL ok = m_List.DeleteColumn(col);
	if (ok)
		m_ColCount = m_List.GetHeader().GetItemCount();
	return ok;
}

int CTreeListViewCtrl::GetColumnWidth(int col) const {
	return m_List.GetColumnWidth(col);
}

BOOL CTreeListViewCtrl::SetColumnWidth(int col, int width) {
	return m_List.SetColumnWidth(col, width);
}

int CTreeListViewCtrl::GetColumnCount() const {
	return m_ColCount;
}

CHeaderCtrl CTreeListViewCtrl::GetHeader() const {
	return m_List.GetHeader();
}

// ---- Tree linkage helpers ---------------------------------------------------

void CTreeListViewCtrl::LinkAfter(TlvNode* parent, TlvNode* node, TlvNode* after) {
	node->parent = parent;
	++parent->childCount;

	if (after == TLV_LAST || after == nullptr) {
		node->prev = parent->lastChild;
		node->next = nullptr;
		if (parent->lastChild) parent->lastChild->next = node;
		else                    parent->firstChild = node;
		parent->lastChild = node;
	}
	else if (after == TLV_FIRST) {
		node->next = parent->firstChild;
		node->prev = nullptr;
		if (parent->firstChild) parent->firstChild->prev = node;
		else                     parent->lastChild = node;
		parent->firstChild = node;
	}
	else {
		node->prev = after;
		node->next = after->next;
		if (after->next) after->next->prev = node;
		else              parent->lastChild = node;
		after->next = node;
	}
}

void CTreeListViewCtrl::Unlink(TlvNode* node) {
	TlvNode* parent = node->parent;
	if (!parent) return;
	if (node->prev) node->prev->next = node->next;
	else             parent->firstChild = node->next;
	if (node->next) node->next->prev = node->prev;
	else             parent->lastChild = node->prev;
	node->parent = nullptr;
	--parent->childCount;
}

void CTreeListViewCtrl::DeleteTree(TlvNode* node) {
	for (TlvNode* child = node->firstChild; child; ) {
		TlvNode* next = child->next;
		DeleteTree(child);
		child = next;
	}
	if (node != &m_Root) {
		delete node;
	}
	else {
		node->firstChild = node->lastChild = nullptr;
		node->childCount = 0;
	}
}

// ---- Item API ---------------------------------------------------------------

HTLITEM CTreeListViewCtrl::InsertItem(HTLITEM hParent, HTLITEM hInsertAfter,
	LPCWSTR text, int image, LPARAM data) {
	TlvNode* parent = hParent ? hParent : &m_Root;
	TlvNode* node = new TlvNode;
	node->level = (parent == &m_Root) ? 0 : parent->level + 1;
	node->image = image;
	node->data = data;
	node->cols.resize(m_ColCount > 0 ? m_ColCount : 1);
	if (text == LPSTR_TEXTCALLBACKW)
		node->callbackCols |= 1ULL;
	else if (text)
		node->cols[0] = text;

	LinkAfter(parent, node, hInsertAfter);
	if (m_UpdateCount == 0) {
		RebuildVisibleList();
		m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
		m_List.Invalidate();
	}
	return node;
}

BOOL CTreeListViewCtrl::DeleteItem(HTLITEM hItem) {
	if (!hItem || hItem == &m_Root) return FALSE;
	Unlink(hItem);
	DeleteTree(hItem);
	if (m_UpdateCount == 0) {
		RebuildVisibleList();
		m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
		m_List.Invalidate();
	}
	return TRUE;
}

void CTreeListViewCtrl::DeleteAllItems() {
	DeleteTree(&m_Root);
	m_Visible.clear();
	if (m_UpdateCount == 0) {
		m_List.SetItemCountEx(0, LVSICF_NOINVALIDATEALL);
		m_List.Invalidate();
	}
}

void CTreeListViewCtrl::SetItemText(HTLITEM hItem, int col, LPCWSTR text) {
	ATLASSERT(hItem && hItem != &m_Root);
	if ((int)hItem->cols.size() <= col)
		hItem->cols.resize(col + 1);
	if (text == LPSTR_TEXTCALLBACKW) {
		if (col < 64) hItem->callbackCols |=  (1ULL << col);
		hItem->cols[col].clear();
	}
	else {
		if (col < 64) hItem->callbackCols &= ~(1ULL << col);
		hItem->cols[col] = text ? text : L"";
	}
	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.RedrawItems(idx, idx);
}

ATL::CString CTreeListViewCtrl::GetItemText(HTLITEM hItem, int col) const {
	ATLASSERT(hItem && hItem != &m_Root);
	if (col < (int)hItem->cols.size())
		return hItem->cols[col].c_str();
	return L"";
}

void CTreeListViewCtrl::SetItemData(HTLITEM hItem, LPARAM data) {
	ATLASSERT(hItem && hItem != &m_Root);
	hItem->data = data;
}

LPARAM CTreeListViewCtrl::GetItemData(HTLITEM hItem) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return hItem->data;
}

void CTreeListViewCtrl::SetItemImage(HTLITEM hItem, int image) {
	ATLASSERT(hItem && hItem != &m_Root);
	hItem->image = image;
	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.RedrawItems(idx, idx);
}

int CTreeListViewCtrl::GetItemImage(HTLITEM hItem) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return hItem->image;
}

bool CTreeListViewCtrl::IsTextCallback(HTLITEM hItem, int col) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return m_CallbackMode || (col < 64 && (hItem->callbackCols >> col) & 1);
}

void CTreeListViewCtrl::SetCallbackMode(bool enable) {
	if (m_CallbackMode == enable) return;
	m_CallbackMode = enable;
	if (m_List.IsWindow()) m_List.Invalidate();
}

bool CTreeListViewCtrl::GetCallbackMode() const {
	return m_CallbackMode;
}

// ---- Tree navigation --------------------------------------------------------

HTLITEM CTreeListViewCtrl::GetRootItem() const {
	return m_Root.firstChild;
}

HTLITEM CTreeListViewCtrl::GetChildItem(HTLITEM hItem) const {
	return hItem ? hItem->firstChild : m_Root.firstChild;
}

HTLITEM CTreeListViewCtrl::GetNextSiblingItem(HTLITEM hItem) const {
	return hItem ? hItem->next : nullptr;
}

HTLITEM CTreeListViewCtrl::GetPrevSiblingItem(HTLITEM hItem) const {
	return hItem ? hItem->prev : nullptr;
}

HTLITEM CTreeListViewCtrl::GetParentItem(HTLITEM hItem) const {
	if (!hItem || hItem->parent == &m_Root) return nullptr;
	return hItem->parent;
}

HTLITEM CTreeListViewCtrl::GetNextItem(HTLITEM hItem, UINT flag) const {
	switch (flag) {
	case TVGN_ROOT:          return m_Root.firstChild;
	case TVGN_NEXT:          return hItem ? hItem->next : nullptr;
	case TVGN_PREVIOUS:      return hItem ? hItem->prev : nullptr;
	case TVGN_PARENT:        return GetParentItem(hItem);
	case TVGN_CHILD:         return hItem ? hItem->firstChild : m_Root.firstChild;
	case TVGN_FIRSTVISIBLE:  return m_Visible.empty() ? nullptr : m_Visible.front();
	case TVGN_LASTVISIBLE:   return m_Visible.empty() ? nullptr : m_Visible.back();
	case TVGN_NEXTVISIBLE: {
		if (!hItem) return nullptr;
		int idx = VisibleIndex(hItem);
		return (idx >= 0 && idx + 1 < (int)m_Visible.size()) ? m_Visible[idx + 1] : nullptr;
	}
	case TVGN_PREVIOUSVISIBLE: {
		if (!hItem) return nullptr;
		int idx = VisibleIndex(hItem);
		return (idx > 0) ? m_Visible[idx - 1] : nullptr;
	}
	default: return nullptr;
	}
}

int CTreeListViewCtrl::GetItemLevel(HTLITEM hItem) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return hItem->level;
}

bool CTreeListViewCtrl::HasChildren(HTLITEM hItem) const {
	if (!hItem) return m_Root.childCount > 0;
	return hItem->childCount > 0 || hItem->hasChildren;
}

void CTreeListViewCtrl::SetHasChildren(HTLITEM hItem, bool has) {
	ATLASSERT(hItem && hItem != &m_Root);
	hItem->hasChildren = has;
	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.RedrawItems(idx, idx);
}

// ---- Expand / collapse ------------------------------------------------------

void CTreeListViewCtrl::Expand(HTLITEM hItem, bool expand) {
	ATLASSERT(hItem && hItem != &m_Root);
	if (hItem->expanded == expand) return;
	if (hItem->childCount == 0 && !hItem->hasChildren) return;

	NMTLVEXPAND nm = {};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = TLVN_ITEMEXPANDING;
	nm.hItem        = hItem;
	nm.bExpanded    = expand;
	if (FireNotify(&nm.hdr)) return;  // non-zero return vetoes the operation

	hItem->expanded = expand;
	RebuildVisibleList();
	m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();

	nm.hdr.code = TLVN_ITEMEXPANDED;
	FireNotify(&nm.hdr);
}

void CTreeListViewCtrl::Toggle(HTLITEM hItem) {
	ATLASSERT(hItem && hItem != &m_Root);
	Expand(hItem, !hItem->expanded);
}

bool CTreeListViewCtrl::IsExpanded(HTLITEM hItem) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return hItem->expanded;
}

void CTreeListViewCtrl::ExpandAll(HTLITEM hItem) {
	ExpandRecursive(hItem ? hItem : &m_Root, true);
	RebuildVisibleList();
	m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();
}

void CTreeListViewCtrl::CollapseAll(HTLITEM hItem) {
	ExpandRecursive(hItem ? hItem : &m_Root, false);
	RebuildVisibleList();
	m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();
}

void CTreeListViewCtrl::ExpandRecursive(TlvNode* node, bool expand) {
	if (node != &m_Root && node->childCount > 0)
		node->expanded = expand;
	for (TlvNode* child = node->firstChild; child; child = child->next)
		ExpandRecursive(child, expand);
}

// ---- Tree lines -------------------------------------------------------------

void CTreeListViewCtrl::SetTreeLines(bool enable) {
	if (m_TreeLines == enable) return;
	m_TreeLines = enable;
	if (m_List.IsWindow()) m_List.Invalidate();
}

bool CTreeListViewCtrl::GetTreeLines() const {
	return m_TreeLines;
}

// ---- View mode --------------------------------------------------------------

void CTreeListViewCtrl::SetViewMode(bool treeMode) {
	if (m_TreeMode == treeMode) return;
	m_TreeMode = treeMode;
	RebuildVisibleList();
	m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();
}

bool CTreeListViewCtrl::IsTreeMode() const {
	return m_TreeMode;
}

// ---- Selection --------------------------------------------------------------

HTLITEM CTreeListViewCtrl::GetSelectedItem() const {
	int idx = m_List.GetNextItem(-1, LVNI_SELECTED);
	if (idx < 0 || idx >= (int)m_Visible.size()) return nullptr;
	return m_Visible[idx];
}

void CTreeListViewCtrl::SelectItem(HTLITEM hItem) {
	ExpandToItem(hItem);
	int idx = VisibleIndex(hItem);
	if (idx < 0) return;
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
	m_List.SetItemState(idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_List.EnsureVisible(idx, FALSE);
}

void CTreeListViewCtrl::EnsureVisible(HTLITEM hItem) {
	ExpandToItem(hItem);
	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.EnsureVisible(idx, FALSE);
}

HTLITEM CTreeListViewCtrl::GetNextSelectedItem(HTLITEM hPrev) const {
	int start = hPrev ? VisibleIndex(hPrev) : -1;
	int idx = m_List.GetNextItem(start, LVNI_SELECTED);
	if (idx < 0 || idx >= (int)m_Visible.size()) return nullptr;
	return m_Visible[idx];
}

void CTreeListViewCtrl::SelectAll() {
	if (!m_MultiSelect) return;
	for (int i = 0; i < (int)m_Visible.size(); ++i)
		m_List.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
}

void CTreeListViewCtrl::DeselectAll() {
	m_List.SetItemState(-1, 0, LVIS_SELECTED);
}

// ---- Multi-select -----------------------------------------------------------

void CTreeListViewCtrl::SetMultiSelect(bool enable) {
	if (m_MultiSelect == enable) return;
	m_MultiSelect = enable;
	if (enable) {
		m_List.ModifyStyle(LVS_SINGLESEL, 0);
	}
	else {
		int first = m_List.GetNextItem(-1, LVNI_SELECTED);
		m_List.SetItemState(-1, 0, LVIS_SELECTED);
		if (first >= 0)
			m_List.SetItemState(first, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_List.ModifyStyle(0, LVS_SINGLESEL);
	}
}

bool CTreeListViewCtrl::IsMultiSelect() const {
	return m_MultiSelect;
}

// ---- Data query -------------------------------------------------------------

int CTreeListViewCtrl::GetItemCount() const {
	return CountNodes(&m_Root);
}

int CTreeListViewCtrl::CountNodes(const TlvNode* node) const {
	int n = 0;
	for (TlvNode* c = node->firstChild; c; c = c->next)
		n += 1 + CountNodes(c);
	return n;
}

int CTreeListViewCtrl::GetVisibleItemCount() const {
	return (int)m_Visible.size();
}

HTLITEM CTreeListViewCtrl::FindItem(HTLITEM hStart, int col, LPCWSTR text, UINT flags) const {
	if (!text || m_Visible.empty()) return nullptr;

	const bool matchCase = (flags & TLVFS_MATCHCASE) != 0;
	const bool contains  = (flags & TLVFS_CONTAINS) != 0;
	const int  textLen   = (int)wcslen(text);

	auto matches = [&](TlvNode* node) -> bool {
		const std::wstring& s = (col < (int)node->cols.size()) ? node->cols[col] : std::wstring{};
		if (contains) {
			auto eq = [matchCase](wchar_t a, wchar_t b) {
				return matchCase ? (a == b) : (::towlower(a) == ::towlower(b));
			};
			return std::search(s.begin(), s.end(), text, text + textLen, eq) != s.end();
		}
		else {
			if ((int)s.size() < textLen) return false;
			return matchCase ? (s.compare(0, textLen, text) == 0)
			                 : (_wcsnicmp(s.c_str(), text, textLen) == 0);
		}
	};

	// Search forward from the item after hStart, then wrap.
	int startIdx = 0;
	if (hStart) {
		int si = VisibleIndex(hStart);
		startIdx = (si >= 0) ? si + 1 : 0;
	}
	const int n = (int)m_Visible.size();
	for (int i = startIdx; i < n; ++i)
		if (matches(m_Visible[i])) return m_Visible[i];
	for (int i = 0; i < startIdx; ++i)
		if (matches(m_Visible[i])) return m_Visible[i];
	return nullptr;
}

bool CTreeListViewCtrl::GetItemRect(HTLITEM hItem, RECT* prc) const {
	int idx = VisibleIndex(hItem);
	if (idx < 0 || !prc) return false;
	return m_List.GetItemRect(idx, prc, LVIR_BOUNDS) != FALSE;
}

HTLITEM CTreeListViewCtrl::HitTest(POINT pt, int* pCol) const {
	// m_List fills the TLV client area, so coordinates are the same.
	LVHITTESTINFO hti = {};
	hti.pt = pt;
	int idx = (int)::SendMessage(m_List.m_hWnd,
		pCol ? LVM_SUBITEMHITTEST : LVM_HITTEST, 0, (LPARAM)&hti);
	if (pCol) *pCol = (idx >= 0) ? hti.iSubItem : -1;
	if (idx < 0 || idx >= (int)m_Visible.size()) return nullptr;
	return m_Visible[idx];
}

// ---- Sort -------------------------------------------------------------------

void CTreeListViewCtrl::SortChildren(HTLITEM hParent, PFNTLVCOMPARE pfn, LPARAM lParam, bool recurse) {
	SortNode(hParent ? hParent : &m_Root, pfn, lParam, recurse);
	RebuildVisibleList();
	m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
	m_List.Invalidate();
}

void CTreeListViewCtrl::SortNode(TlvNode* parent, PFNTLVCOMPARE pfn, LPARAM lParam, bool recurse) {
	if (parent->childCount >= 2) {
		std::vector<TlvNode*> children;
		children.reserve(parent->childCount);
		for (TlvNode* c = parent->firstChild; c; c = c->next)
			children.push_back(c);

		std::stable_sort(children.begin(), children.end(),
			[&](TlvNode* a, TlvNode* b) { return pfn(a, b, lParam) < 0; });

		parent->firstChild = children.front();
		parent->lastChild  = children.back();
		for (int i = 0; i < (int)children.size(); ++i) {
			children[i]->prev = (i > 0) ? children[i - 1] : nullptr;
			children[i]->next = (i + 1 < (int)children.size()) ? children[i + 1] : nullptr;
		}
	}

	if (recurse) {
		for (TlvNode* c = parent->firstChild; c; c = c->next)
			SortNode(c, pfn, lParam, true);
	}
}

// ---- Rendering --------------------------------------------------------------

COLORREF CTreeListViewCtrl::SetBkColor(COLORREF clr) {
	COLORREF old = m_List.GetBkColor();
	m_List.SetBkColor(clr);
	return old;
}

COLORREF CTreeListViewCtrl::GetBkColor() const {
	return m_List.GetBkColor();
}

COLORREF CTreeListViewCtrl::SetTextColor(COLORREF clr) {
	COLORREF old = m_List.GetTextColor();
	m_List.SetTextColor(clr);
	return old;
}

COLORREF CTreeListViewCtrl::GetTextColor() const {
	return m_List.GetTextColor();
}

HFONT CTreeListViewCtrl::SetFont(HFONT hFont, bool redraw) {
	HFONT hOld = (HFONT)m_List.SendMessage(WM_GETFONT);
	m_List.SetFont(hFont, redraw);
	return hOld;
}

HFONT CTreeListViewCtrl::GetFont() const {
	return m_List.GetFont();
}

void CTreeListViewCtrl::SetColumnSortArrow(int col, int dir) {
	CHeaderCtrl hdr = m_List.GetHeader();
	if (!hdr.IsWindow()) return;
	HDITEMW hdi = {};
	hdi.mask = HDI_FORMAT;
	if (!hdr.GetItem(col, &hdi)) return;
	hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
	if      (dir > 0) hdi.fmt |= HDF_SORTUP;
	else if (dir < 0) hdi.fmt |= HDF_SORTDOWN;
	hdr.SetItem(col, &hdi);
}

void CTreeListViewCtrl::SetAlternateRowColor(COLORREF clr) {
	m_AltRowColor = clr;
	if (m_List.IsWindow()) m_List.Invalidate();
}

COLORREF CTreeListViewCtrl::GetAlternateRowColor() const {
	return m_AltRowColor;
}

// ---- Image list -------------------------------------------------------------

CImageList CTreeListViewCtrl::SetImageList(HIMAGELIST himl, int type) {
	return m_List.SetImageList(himl, type);
}

CImageList CTreeListViewCtrl::GetImageList(int type) const {
	return m_List.GetImageList(type);
}

// ---- Extended LV styles -----------------------------------------------------

DWORD CTreeListViewCtrl::GetExtendedStyle() const {
	return m_List.GetExtendedListViewStyle();
}

void CTreeListViewCtrl::SetExtendedStyle(DWORD dwExStyle) {
	m_List.SetExtendedListViewStyle(dwExStyle);
}

CListViewCtrl& CTreeListViewCtrl::GetListCtrl() {
	return m_List;
}

// ---- Visible-list helpers ---------------------------------------------------

void CTreeListViewCtrl::RebuildVisibleList() {
	m_Visible.clear();
	for (TlvNode* child = m_Root.firstChild; child; child = child->next)
		AppendVisible(child);
}

void CTreeListViewCtrl::AppendVisible(TlvNode* node) {
	m_Visible.push_back(node);
	if (!m_TreeMode || node->expanded) {
		for (TlvNode* child = node->firstChild; child; child = child->next)
			AppendVisible(child);
	}
}

void CTreeListViewCtrl::ExpandToItem(TlvNode* node) {
	if (!node || node == &m_Root) return;
	bool changed = false;
	for (TlvNode* p = node->parent; p && p != &m_Root; p = p->parent) {
		if (!p->expanded) { p->expanded = true; changed = true; }
	}
	if (changed) {
		RebuildVisibleList();
		m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
		m_List.Invalidate();
	}
}

int CTreeListViewCtrl::VisibleIndex(HTLITEM hItem) const {
	for (int i = 0; i < (int)m_Visible.size(); ++i)
		if (m_Visible[i] == hItem) return i;
	return -1;
}

// ---- Indent -----------------------------------------------------------------

void CTreeListViewCtrl::SetIndent(int width) {
	m_IndentWidth = width;
	if (m_List.IsWindow()) m_List.Invalidate();
}

int CTreeListViewCtrl::GetIndent() const {
	return m_IndentWidth;
}

// ---- Batch update -----------------------------------------------------------

void CTreeListViewCtrl::BeginUpdate() {
	if (++m_UpdateCount == 1)
		m_List.SendMessage(WM_SETREDRAW, FALSE);
}

void CTreeListViewCtrl::EndUpdate() {
	if (m_UpdateCount > 0 && --m_UpdateCount == 0) {
		RebuildVisibleList();
		m_List.SetItemCountEx((int)m_Visible.size(), LVSICF_NOINVALIDATEALL);
		m_List.SendMessage(WM_SETREDRAW, TRUE);
		m_List.Invalidate();
		m_List.UpdateWindow();
	}
}

LRESULT CTreeListViewCtrl::FireNotify(NMHDR* pnm) {
	HWND parent = GetParent();
	if (::IsWindow(parent))
		return ::SendMessage(parent, WM_NOTIFY, (WPARAM)GetDlgCtrlID(), (LPARAM)pnm);
	return 0;
}

// ---- Custom draw: relay helper ----------------------------------------------

LRESULT CTreeListViewCtrl::RelayCustomDraw(NMLVCUSTOMDRAW* plvcd) {
	NMTLVCUSTOMDRAW tlvcd = {};
	tlvcd.nmcd = plvcd->nmcd;
	tlvcd.nmcd.hdr.hwndFrom = m_hWnd;
	tlvcd.nmcd.hdr.idFrom = GetDlgCtrlID();
	tlvcd.nmcd.hdr.code = NM_CUSTOMDRAW;
	tlvcd.clrText = plvcd->clrText;
	tlvcd.clrTextBk = plvcd->clrTextBk;
	tlvcd.iSubItem = plvcd->iSubItem;

	DWORD stage = plvcd->nmcd.dwDrawStage;
	if ((stage & CDDS_ITEM) && (int)plvcd->nmcd.dwItemSpec < (int)m_Visible.size())
		tlvcd.hItem = m_Visible[plvcd->nmcd.dwItemSpec];

	// Built-in alternate row color: pre-populate before relaying so the parent
	// can still override it in their own NM_CUSTOMDRAW handler.
	if (m_AltRowColor != CLR_NONE && (stage & CDDS_ITEM) &&
	    plvcd->nmcd.dwItemSpec % 2 == 1)
		tlvcd.clrTextBk = m_AltRowColor;

	LRESULT lr = ::SendMessage(GetParent(), WM_NOTIFY,
		(WPARAM)GetDlgCtrlID(), (LPARAM)&tlvcd);

	// Mirror color overrides back so ListView uses them for non-column-0 subitems.
	plvcd->clrText = tlvcd.clrText;
	plvcd->clrTextBk = tlvcd.clrTextBk;

	// Cache for DrawColumn0 to use (set during CDDS_ITEMPREPAINT).
	m_ClrText = tlvcd.clrText;
	m_ClrTextBk = tlvcd.clrTextBk;

	return lr;
}

// ---- Custom draw: column 0 renderer -----------------------------------------

void CTreeListViewCtrl::DrawColumn0(NMLVCUSTOMDRAW* pcd) {
	int idx = (int)pcd->nmcd.dwItemSpec;
	if (idx < 0 || idx >= (int)m_Visible.size()) return;
	TlvNode* node = m_Visible[idx];

	CDCHandle dc = pcd->nmcd.hdc;
	bool sel = m_List.GetItemState(idx, LVIS_SELECTED) != 0;
	bool focus = (pcd->nmcd.uItemState & CDIS_FOCUS) != 0;
	bool lvFocused = (::GetFocus() == m_List.m_hWnd);

	// GetSubItemRect with iSubItem=0 returns the full item rect on Vista+
	// comctl32. Use column 1's left edge as column 0's right boundary.
	CRect rcCol0;
	m_List.GetSubItemRect(idx, 0, LVIR_BOUNDS, &rcCol0);
	if (m_ColCount > 1) {
		CRect rcNext;
		m_List.GetSubItemRect(idx, 1, LVIR_BOUNDS, &rcNext);
		rcCol0.right = rcNext.left;
	}

	int savedDC = dc.SaveDC();
	dc.IntersectClipRect(&rcCol0);

	// ---- Background ---------------------------------------------------------
	// Use the ListView's own bg/text color as fallback so SetBkColor/SetTextColor
	// takes effect on column 0 too (ListView handles other columns natively).
	COLORREF lvBk  = m_List.GetBkColor();
	COLORREF lvTxt = m_List.GetTextColor();
	COLORREF fallbackBk  = (lvBk  != CLR_NONE) ? lvBk  : ::GetSysColor(COLOR_WINDOW);
	COLORREF fallbackTxt = (lvTxt != CLR_NONE) ? lvTxt : ::GetSysColor(COLOR_WINDOWTEXT);

	COLORREF bkClr, txtClr;
	if (sel) {
		bkClr = ::GetSysColor(lvFocused ? COLOR_HIGHLIGHT : COLOR_BTNFACE);
		txtClr = ::GetSysColor(lvFocused ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT);
	}
	else {
		bkClr = (m_ClrTextBk != CLR_DEFAULT) ? m_ClrTextBk : fallbackBk;
		txtClr = (m_ClrText   != CLR_DEFAULT) ? m_ClrText   : fallbackTxt;
	}
	dc.SetBkColor(bkClr);
	dc.SetTextColor(txtClr);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rcCol0, nullptr, 0, nullptr);

	// ---- Expand/collapse glyph (tree mode) and checkbox --------------------
	int textLeft = rcCol0.left + 2;
	const int rowH = rcCol0.Height();

	if (m_TreeMode) {
		const int iW = m_IndentWidth;
		int glyphLeft = rcCol0.left + node->level * iW;
		textLeft = glyphLeft + iW + 2;

		if (m_TreeLines) DrawTreeLines(dc, rcCol0, node);

		if (node->childCount > 0 || node->hasChildren) {
			const int gsz = m_IndentWidth * 9 / 16;  // ~56% of cell, scales with DPI
			int gx = glyphLeft + (iW - gsz) / 2;
			int gy = rcCol0.top + (rowH - gsz) / 2;
			CRect glyphR(gx, gy, gx + gsz, gy + gsz);

			if (!m_Theme.IsThemeNull()) {
				int state = node->expanded ? GLPS_OPENED : GLPS_CLOSED;
				m_Theme.DrawThemeBackground(dc, TVP_GLYPH, state, &glyphR, &rcCol0);
			}
			else {
				DrawGlyphManual(dc, glyphR, node->expanded);
			}
		}

		if (m_CheckBoxes) {
			const int cbSz = std::min(rowH - 4, iW);
			int cbX = glyphLeft + iW + (iW - cbSz) / 2;
			int cbY = rcCol0.top + (rowH - cbSz) / 2;
			CRect cbR(cbX, cbY, cbX + cbSz, cbY + cbSz);
			DrawCheckBox(dc, cbR, node->checked);
			textLeft = glyphLeft + 2 * iW + 2;
		}
	}
	else {
		if (m_CheckBoxes) {
			const int iW = m_IndentWidth;
			const int cbSz = std::min(rowH - 4, iW);
			int cbX = rcCol0.left + (iW - cbSz) / 2;
			int cbY = rcCol0.top + (rowH - cbSz) / 2;
			CRect cbR(cbX, cbY, cbX + cbSz, cbY + cbSz);
			DrawCheckBox(dc, cbR, node->checked);
			textLeft = rcCol0.left + iW + 2;
		}
	}

	// ---- Icon (from image list) -----------------------------------------------
	if (node->image >= 0) {
		auto himl = m_List.GetImageList(LVSIL_SMALL);
		if (himl) {
			int cx, cy;
			if (himl.GetIconSize(cx, cy)) {
				int iconY = rcCol0.top + (rowH - cy) / 2;
				himl.Draw(dc, node->image, textLeft, iconY, ILD_TRANSPARENT);
				textLeft += cx + 2;
			}
		}
	}

	// ---- Text ---------------------------------------------------------------
	CRect textR(textLeft, rcCol0.top, rcCol0.right, rcCol0.bottom);

	wchar_t  cbBuf[512];
	LPCWSTR  col0Text;
	int      col0Len;
	if (m_CallbackMode || (node->callbackCols & 1)) {
		cbBuf[0] = L'\0';
		NMTLVDISPINFO nm = {};
		nm.hdr.hwndFrom = m_hWnd;
		nm.hdr.idFrom   = GetDlgCtrlID();
		nm.hdr.code     = TLVN_GETDISPINFO;
		nm.hItem        = node;
		nm.iSubItem     = 0;
		nm.pszText      = cbBuf;
		nm.cchTextMax   = (int)std::size(cbBuf);
		FireNotify(&nm.hdr);
		col0Text = cbBuf;
		col0Len  = -1;  // let DrawText compute length
	}
	else {
		col0Text = node->cols.empty() ? L"" : node->cols[0].c_str();
		col0Len  = node->cols.empty() ? 0 : (int)node->cols[0].size();
	}

	if (col0Len != 0 && col0Text[0] != L'\0') {
		HFONT hFont = m_List.GetFont();
		HFONT hOldFont = hFont ? dc.SelectFont(hFont) : NULL;
		dc.SetBkMode(TRANSPARENT);
		dc.DrawText(const_cast<LPWSTR>(col0Text), col0Len,
			&textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
			DT_NOPREFIX | DT_END_ELLIPSIS);
		if (hOldFont) dc.SelectFont(hOldFont);
	}

	// ---- Focus rect ---------------------------------------------------------
	if (focus && lvFocused)
		dc.DrawFocusRect(&rcCol0);

	dc.RestoreDC(savedDC);
}

// ---- Custom draw: manual fallback glyph -------------------------------------

void CTreeListViewCtrl::DrawGlyphManual(CDCHandle dc, const RECT& r, bool expanded) {
	CPen pen;
	pen.CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_GRAYTEXT));
	HPEN   hOldP  = dc.SelectPen(pen);
	HBRUSH hOldBr = dc.SelectBrush((HBRUSH)::GetStockObject(WHITE_BRUSH));

	dc.Rectangle(r.left, r.top, r.right, r.bottom);

	dc.SelectBrush(hOldBr);

	int cx = (r.left + r.right) / 2;
	int cy = (r.top + r.bottom) / 2;

	// Horizontal bar (present for both + and -)
	dc.MoveTo(r.left + 2, cy);
	dc.LineTo(r.right - 2, cy);

	// Vertical bar (only when collapsed = plus sign)
	if (!expanded) {
		dc.MoveTo(cx, r.top + 2);
		dc.LineTo(cx, r.bottom - 2);
	}

	dc.SelectPen(hOldP);
}   // pen auto-deletes

// ---- Custom draw: tree lines ------------------------------------------------
//
// For each row, draws the connecting lines bottom-up:
//   Ancestor levels k < node->level : full-height vertical if that ancestor
//     has a next sibling (the line "passes through" this row on its way down).
//   Node's own level: elbow (no next sibling) or tee (has next sibling), plus
//     a short horizontal stub reaching into the glyph cell.

void CTreeListViewCtrl::DrawTreeLines(CDCHandle dc, const RECT& rc, TlvNode* node) {
	// A lone top-level item with no siblings needs no lines.
	if (node->level == 0 && !node->prev && !node->next) return;

	CPen pen;
	pen.CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_GRAYTEXT));
	HPEN hOld = dc.SelectPen(pen);

	const int midY = (rc.top + rc.bottom) / 2;
	const int iW   = m_IndentWidth;

	// Ancestor pass-through verticals
	for (int k = 0; k < node->level; ++k) {
		TlvNode* anc = node;
		while (anc->level > k) anc = anc->parent;
		if (anc->next) {
			int cx = rc.left + k * iW + iW / 2;
			dc.MoveTo(cx, rc.top);
			dc.LineTo(cx, rc.bottom);
		}
	}

	// Node's own level: elbow / tee + horizontal stub
	const int cx         = rc.left + node->level * iW + iW / 2;
	const int glyphRight = rc.left + (node->level + 1) * iW;

	// Vertical incoming (from prev sibling or parent)
	if (node->prev || node->parent != &m_Root) {
		dc.MoveTo(cx, rc.top);
		dc.LineTo(cx, midY);
	}
	// Vertical outgoing (to next sibling)
	if (node->next) {
		dc.MoveTo(cx, midY);
		dc.LineTo(cx, rc.bottom);
	}
	// Horizontal stub into glyph cell
	dc.MoveTo(cx, midY);
	dc.LineTo(glyphRight, midY);

	dc.SelectPen(hOld);
}   // pen auto-deletes

// ---- NM_CUSTOMDRAW from child ListView --------------------------------------

LRESULT CTreeListViewCtrl::OnCustomDraw(int, LPNMHDR pnmh, BOOL&) {
	auto* plvcd = reinterpret_cast<NMLVCUSTOMDRAW*>(pnmh);
	DWORD stage = plvcd->nmcd.dwDrawStage;

	if (stage == CDDS_PREPAINT)
		return CDRF_NOTIFYITEMDRAW;

	if (stage == CDDS_ITEMPREPAINT) {
		m_ClrText = m_ClrTextBk = CLR_DEFAULT;
		RelayCustomDraw(plvcd);
		// CDRF_NOTIFYPOSTPAINT ensures CDDS_ITEMPOSTPAINT fires, where we draw
		// column 0 reliably (CDDS_SUBITEM for col 0 is not sent on all comctl32 versions).
		return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
	}

	if (stage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
		return RelayCustomDraw(plvcd);

	if (stage == CDDS_ITEMPOSTPAINT) {
		DrawColumn0(plvcd);
		return CDRF_DODEFAULT;
	}

	return CDRF_DODEFAULT;
}

// ---- NM_CLICK: hit-test the glyph area -------------------------------------

LRESULT CTreeListViewCtrl::OnClick(int, LPNMHDR pnmh, BOOL&) {
	if (!m_TreeMode) return 0;

	auto* pnia = reinterpret_cast<NMITEMACTIVATE*>(pnmh);
	if (pnia->iItem < 0 || pnia->iItem >= (int)m_Visible.size()) return 0;

	TlvNode* node = m_Visible[pnia->iItem];
	if (!node->childCount && !node->hasChildren) return 0;

	// Get column 0 bounds in ListView client coords.
	CRect rcCol0;
	m_List.GetSubItemRect(pnia->iItem, 0, LVIR_BOUNDS, &rcCol0);

	int glyphLeft = rcCol0.left + node->level * m_IndentWidth;
	int glyphRight = glyphLeft + m_IndentWidth;

	if (pnia->ptAction.x >= glyphLeft && pnia->ptAction.x < glyphRight)
		Toggle(node);

	return 0;
}

// ---- ListView subclass: TreeView-style selection behaviour ------------------
//
// WM_LBUTTONDOWN is intercepted before the ListView's own selection logic runs.
// Clicks on empty space and on the already-selected item are eaten; the glyph
// area of an already-selected item still toggles expansion.
// A notification-level approach (LVN_ITEMCHANGING) cannot work here because
// the ListView sends the losing-selection notification for the OLD item before
// the gaining-selection notification for the NEW item, so blocking the losing
// notification also prevents the new item from ever being selected.

LRESULT CALLBACK CTreeListViewCtrl::ListSubclassProc(
	HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR data) {
	auto* self = reinterpret_cast<CTreeListViewCtrl*>(data);

	if (msg == WM_LBUTTONDBLCLK) {
		CPoint pt((DWORD)lParam);
		LVHITTESTINFO hti = {};
		hti.pt = pt;
		int hit = self->m_List.HitTest(&hti);

		if (self->m_TreeMode && hit >= 0 && hit < (int)self->m_Visible.size()) {
			TlvNode* node = self->m_Visible[hit];
			if (node->childCount > 0 || node->hasChildren) {
				self->Toggle(node);
			}
			else {
				POINT ptScreen = pt;
				::ClientToScreen(hwnd, &ptScreen);
				NMTLVACTIVATE nm = {};
				nm.hdr.hwndFrom = self->m_hWnd;
				nm.hdr.idFrom   = self->GetDlgCtrlID();
				nm.hdr.code     = TLVN_ACTIVATE;
				nm.hItem        = node;
				nm.ptAction     = ptScreen;
				self->FireNotify(&nm.hdr);
			}
		}
		// Always pass through: ListView generates NM_DBLCLK which OnDblClk relays.
		return ::DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	if (msg == WM_RBUTTONDOWN) {
		CPoint pt((DWORD)lParam);
		LVHITTESTINFO hti = {};
		hti.pt = pt;
		int hit = self->m_List.HitTest(&hti);
		if (hit >= 0 && hit < (int)self->m_Visible.size()) {
			UINT state = self->m_List.GetItemState(hit, LVIS_SELECTED);
			if (!(state & LVIS_SELECTED))
				self->SelectItem(self->m_Visible[hit]);
		}
		return ::DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	if (msg == WM_LBUTTONDOWN) {
		CPoint pt((DWORD)lParam);
		LVHITTESTINFO hti = {};
		hti.pt = pt;
		int hit = self->m_List.HitTest(&hti);

		// Checkbox click: toggle state; handled before selection logic.
		if (hit >= 0 && hit < (int)self->m_Visible.size() && self->m_CheckBoxes) {
			TlvNode* node = self->m_Visible[hit];
			CRect rcItem;
			self->m_List.GetSubItemRect(hit, 0, LVIR_BOUNDS, &rcItem);
			int cbLeft = self->m_TreeMode
				? (rcItem.left + (node->level + 1) * self->m_IndentWidth)
				: rcItem.left;
			if (pt.x >= cbLeft && pt.x < cbLeft + self->m_IndentWidth) {
				self->SetCheckState(node, !node->checked);
				::SetFocus(hwnd);
				return 0;
			}
		}

		// Tree-mode selection logic.
		if (!self->m_TreeMode)
			return ::DefSubclassProc(hwnd, msg, wParam, lParam);

		if (hit < 0) {
			// Empty space — focus without changing selection.
			::SetFocus(hwnd);
			return 0;
		}

		// In multi-select mode, Ctrl/Shift modifier clicks pass through.
		if (self->m_MultiSelect && (wParam & (MK_CONTROL | MK_SHIFT)))
			return ::DefSubclassProc(hwnd, msg, wParam, lParam);

		UINT state = self->m_List.GetItemState(hit, LVIS_SELECTED);
		if (!(state & LVIS_SELECTED))
			return ::DefSubclassProc(hwnd, msg, wParam, lParam);

		// Already-selected item: check for glyph click, then eat.
		if (hit < (int)self->m_Visible.size()) {
			TlvNode* node = self->m_Visible[hit];
			if (node->childCount || node->hasChildren) {
				CRect rc;
				self->m_List.GetSubItemRect(hit, 0, LVIR_BOUNDS, &rc);
				int left = rc.left + node->level * self->m_IndentWidth;
				int right = left + self->m_IndentWidth;
				if (pt.x >= left && pt.x < right)
					self->Toggle(node);
			}
		}
		::SetFocus(hwnd);
		return 0;
	}
	return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---- Edit-control subclass: enforce textLeft during label edit --------------
//
// The ListView repositions its edit control after LVN_BEGINLABELEDITW returns,
// overriding any SetWindowPos we called during the notification.  By clamping
// the left edge inside WM_WINDOWPOSCHANGING the wrong position is never applied
// and there is no visible flash.

LRESULT CALLBACK CTreeListViewCtrl::EditSubclassProc(
	HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR, DWORD_PTR data) {
	auto* self = reinterpret_cast<CTreeListViewCtrl*>(data);

	if (msg == WM_WINDOWPOSCHANGING) {
		auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);
		if (!(wp->flags & SWP_NOMOVE)) {
			wp->x  = self->m_EditTextLeft;
			wp->cx = self->m_EditRight - self->m_EditTextLeft;
		}
	}
	else if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, EditSubclassProc, 0);
	}

	return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---- LVN_ITEMCHANGED --------------------------------------------------------

LRESULT CTreeListViewCtrl::OnItemChanged(int, LPNMHDR pnmh, BOOL&) {
	if (m_InSelChange) return 0;
	auto* pnmlv = reinterpret_cast<NMLISTVIEW*>(pnmh);
	if (!(pnmlv->uChanged & LVIF_STATE)) return 0;
	if (!((pnmlv->uOldState ^ pnmlv->uNewState) & LVIS_SELECTED)) return 0;
	if (pnmlv->iItem < 0 || pnmlv->iItem >= (int)m_Visible.size()) return 0;

	m_InSelChange = true;
	NMTLVITEMCHANGE nm = {};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom = GetDlgCtrlID();
	nm.hdr.code = TLVN_SELCHANGED;
	nm.hItemOld = (pnmlv->uOldState & LVIS_SELECTED) ? m_Visible[pnmlv->iItem] : nullptr;
	nm.hItemNew = (pnmlv->uNewState & LVIS_SELECTED) ? m_Visible[pnmlv->iItem] : nullptr;
	FireNotify(&nm.hdr);
	m_InSelChange = false;
	return 0;
}

// ---- LVN_COLUMNCLICK --------------------------------------------------------

LRESULT CTreeListViewCtrl::OnColumnClick(int, LPNMHDR pnmh, BOOL&) {
	auto* pnmlv = reinterpret_cast<NMLISTVIEW*>(pnmh);
	int col = pnmlv->iSubItem;

	if (col >= 0 && col < 64 && (m_SortableCols >> col) & 1) {
		int newDir = (m_SortCol == col) ? -m_SortDir : 1;
		SetSortColumn(col, newDir);

		NMTLVSORT nm = {};
		nm.hdr.hwndFrom = m_hWnd;
		nm.hdr.idFrom   = GetDlgCtrlID();
		nm.hdr.code     = TLVN_SORT;
		nm.iSubItem     = col;
		nm.iDirection   = newDir;
		FireNotify(&nm.hdr);
	}
	else {
		NMLISTVIEW nm = *pnmlv;
		nm.hdr.hwndFrom = m_hWnd;
		nm.hdr.idFrom   = GetDlgCtrlID();
		nm.hdr.code     = LVN_COLUMNCLICK;
		FireNotify(&nm.hdr);
	}
	return 0;
}

// ---- Sortable-column API ----------------------------------------------------

bool CTreeListViewCtrl::SetColumnSortable(int col, bool sortable) {
	ATLASSERT(col >= 0 && col < 64);

	if (col < 0 || col >= 64) 
		return false;
	if (sortable) 
		m_SortableCols |=  (1ULL << col);
	else          
		m_SortableCols &= ~(1ULL << col);

	CHeaderCtrl hdr = m_List.GetHeader();
	if (hdr.IsWindow()) {
		if (m_SortableCols)
			hdr.ModifyStyle(0, HDS_BUTTONS);
		else
			hdr.ModifyStyle(HDS_BUTTONS, 0);
		hdr.Invalidate();
	}
	return true;
}

bool CTreeListViewCtrl::GetColumnSortable(int col) const {
	return col >= 0 && col < 64 && (m_SortableCols >> col) & 1;
}

int CTreeListViewCtrl::GetSortColumn() const    { return m_SortCol; }
int CTreeListViewCtrl::GetSortDirection() const  { return m_SortDir; }

void CTreeListViewCtrl::SetSortColumn(int col, int dir) {
	if (m_SortCol >= 0 && m_SortCol != col)
		SetColumnSortArrow(m_SortCol, 0);
	m_SortCol = col;
	m_SortDir = (col >= 0) ? dir : 0;
	if (col >= 0)
		SetColumnSortArrow(col, dir);
}

// ---- LVN_GETDISPINFO --------------------------------------------------------

LRESULT CTreeListViewCtrl::OnGetDispInfo(int, LPNMHDR pnmh, BOOL&) {
	auto* pdi = reinterpret_cast<NMLVDISPINFOW*>(pnmh);
	int   idx = pdi->item.iItem;
	if (idx < 0 || idx >= (int)m_Visible.size()) return 0;
	TlvNode* node = m_Visible[idx];

	if (pdi->item.mask & LVIF_TEXT) {
		int col = pdi->item.iSubItem;
		if (col == 0) {
			pdi->item.pszText[0] = L'\0';   // drawn manually in CDDS_ITEMPOSTPAINT
		}
		else if (m_CallbackMode || (col < 64 && (node->callbackCols >> col) & 1)) {
			NMTLVDISPINFO nm = {};
			nm.hdr.hwndFrom = m_hWnd;
			nm.hdr.idFrom   = GetDlgCtrlID();
			nm.hdr.code     = TLVN_GETDISPINFO;
			nm.hItem        = node;
			nm.iSubItem     = col;
			nm.pszText      = pdi->item.pszText;
			nm.cchTextMax   = pdi->item.cchTextMax;
			FireNotify(&nm.hdr);
		}
		else if (col < (int)node->cols.size())
			wcsncpy_s(pdi->item.pszText, pdi->item.cchTextMax,
				node->cols[col].c_str(), _TRUNCATE);
		else
			pdi->item.pszText[0] = L'\0';
	}
	if (pdi->item.mask & LVIF_IMAGE)
		pdi->item.iImage = node->image;

	return 0;
}

// ---- LVN_KEYDOWN: left/right arrow navigation --------------------------------

LRESULT CTreeListViewCtrl::OnKeyDown(int, LPNMHDR pnmh, BOOL&) {
	auto* pnkd = reinterpret_cast<NMLVKEYDOWN*>(pnmh);
	HTLITEM sel = GetSelectedItem();

	if (pnkd->wVKey == VK_SPACE && m_CheckBoxes && sel) {
		SetCheckState(sel, !sel->checked);
		return 0;
	}

	if (!m_TreeMode) return 0;
	if (!sel) return 0;

	switch (pnkd->wVKey) {
		case VK_RIGHT:
			if ((sel->childCount > 0 || sel->hasChildren) && !sel->expanded)
				Expand(sel, true);
			else if (sel->firstChild)
				SelectItem(sel->firstChild);
			break;
		case VK_LEFT:
			if (sel->childCount > 0 && sel->expanded)
				Expand(sel, false);
			else if (sel->parent && sel->parent != &m_Root)
				SelectItem(sel->parent);
			break;
		case VK_HOME:
			if (!m_Visible.empty())
				SelectItem(m_Visible.front());
			break;
		case VK_END:
			if (!m_Visible.empty())
				SelectItem(m_Visible.back());
			break;
		case VK_MULTIPLY:
			ExpandAll(sel);
			EnsureVisible(sel);
			break;
		case VK_RETURN: {
			NMTLVACTIVATE nm = {};
			nm.hdr.hwndFrom = m_hWnd;
			nm.hdr.idFrom   = GetDlgCtrlID();
			nm.hdr.code     = TLVN_ACTIVATE;
			nm.hItem        = sel;
			nm.ptAction     = { -1, -1 };
			FireNotify(&nm.hdr);
			break;
		}
		case 'A':
			if (m_MultiSelect && (::GetKeyState(VK_CONTROL) & 0x8000))
				SelectAll();
			break;
	}
	return 0;
}

// ---- NM_RCLICK relay --------------------------------------------------------

LRESULT CTreeListViewCtrl::OnRClick(int, LPNMHDR pnmh, BOOL&) {
	auto* pnia = reinterpret_cast<NMITEMACTIVATE*>(pnmh);
	NMITEMACTIVATE nm = *pnia;
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = NM_RCLICK;
	FireNotify(&nm.hdr);
	return 0;
}

// ---- NM_DBLCLK relay --------------------------------------------------------

LRESULT CTreeListViewCtrl::OnDblClk(int, LPNMHDR pnmh, BOOL&) {
	auto* pnia = reinterpret_cast<NMITEMACTIVATE*>(pnmh);
	NMITEMACTIVATE nm = *pnia;
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = NM_DBLCLK;
	FireNotify(&nm.hdr);
	return 0;
}

// ---- NM_SETFOCUS / NM_KILLFOCUS relay ---------------------------------------

LRESULT CTreeListViewCtrl::OnFocusChange(int, LPNMHDR pnmh, BOOL&) {
	NMHDR nm = *pnmh;
	nm.hwndFrom = m_hWnd;
	nm.idFrom   = GetDlgCtrlID();
	FireNotify(&nm);
	m_List.Invalidate();  // repaint: focused vs unfocused selection color
	return 0;
}

// ---- LVN_BEGINLABELEDITW ----------------------------------------------------

LRESULT CTreeListViewCtrl::OnBeginLabelEdit(int, LPNMHDR pnmh, BOOL&) {
	auto* pdi = reinterpret_cast<NMLVDISPINFOW*>(pnmh);
	int idx = pdi->item.iItem;
	if (idx < 0 || idx >= (int)m_Visible.size()) return TRUE;
	TlvNode* node = m_Visible[idx];

	NMTLVLABELEDIT nm = {};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = TLVN_BEGINLABELEDIT;
	nm.hItem        = node;
	nm.pszText      = nullptr;
	if (FireNotify(&nm.hdr)) 
		return TRUE;  // vetoed by parent

	// Compute where text starts in column 0 (matching DrawColumn0).
	// iSubItem=0 with LVIR_BOUNDS returns full item rect; fix right edge via col 1.
	CRect rcCol0;
	m_List.GetSubItemRect(idx, 0, LVIR_BOUNDS, &rcCol0);
	if (m_ColCount > 1) {
		CRect rcNext;
		m_List.GetSubItemRect(idx, 1, LVIR_BOUNDS, &rcNext);
		rcCol0.right = rcNext.left;
	}

	m_EditTextLeft = m_TreeMode
		? rcCol0.left + (node->level + 1) * m_IndentWidth + (m_CheckBoxes ? m_IndentWidth : 0)
		: rcCol0.left                                      + (m_CheckBoxes ? m_IndentWidth : 0);
	m_EditRight = rcCol0.right;

	// The ListView creates the edit control flush with the left of column 0
	// (because GetDispInfo returns empty text for col 0) and then repositions
	// it again after this notification returns.  Subclass the edit control so
	// WM_WINDOWPOSCHANGING can clamp the left edge before it is ever painted
	// in the wrong place.
	HWND hEdit = m_List.GetEditControl();
	if (hEdit) {
		if (m_CallbackMode || (node->callbackCols & 1)) {
			wchar_t cbBuf[512] = {};
			NMTLVDISPINFO di = {};
			di.hdr.hwndFrom = m_hWnd;
			di.hdr.idFrom   = GetDlgCtrlID();
			di.hdr.code     = TLVN_GETDISPINFO;
			di.hItem        = node;
			di.iSubItem     = 0;
			di.pszText      = cbBuf;
			di.cchTextMax   = (int)std::size(cbBuf);
			FireNotify(&di.hdr);
			::SetWindowTextW(hEdit, cbBuf);
		}
		else if (!node->cols.empty()) {
			::SetWindowTextW(hEdit, node->cols[0].c_str());
		}
		SetWindowSubclass(hEdit, EditSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
	}

	return FALSE;
}

// ---- LVN_ENDLABELEDITW ------------------------------------------------------

LRESULT CTreeListViewCtrl::OnEndLabelEdit(int, LPNMHDR pnmh, BOOL&) {
	auto* pdi = reinterpret_cast<NMLVDISPINFOW*>(pnmh);
	if (!pdi->item.pszText) return FALSE;  // user cancelled

	int idx = pdi->item.iItem;
	if (idx < 0 || idx >= (int)m_Visible.size()) return FALSE;
	TlvNode* node = m_Visible[idx];

	NMTLVLABELEDIT nm = {};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = TLVN_ENDLABELEDIT;
	nm.hItem        = node;
	nm.pszText      = pdi->item.pszText;
	if (!FireNotify(&nm.hdr)) return FALSE;  // parent rejected

	SetItemText(node, 0, pdi->item.pszText);
	return TRUE;
}

// ---- Editing API ------------------------------------------------------------

void CTreeListViewCtrl::SetEditLabels(bool enable) {
	if (m_EditLabels == enable) return;
	m_EditLabels = enable;
	if (enable) m_List.ModifyStyle(0, LVS_EDITLABELS);
	else        m_List.ModifyStyle(LVS_EDITLABELS, 0);
}

bool CTreeListViewCtrl::GetEditLabels() const {
	return m_EditLabels;
}

void CTreeListViewCtrl::EditLabel(HTLITEM hItem) {
	ExpandToItem(hItem);
	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.EditLabel(idx);
}

// ---- Checkbox API -----------------------------------------------------------

void CTreeListViewCtrl::SetCheckBoxes(bool enable) {
	if (m_CheckBoxes == enable) return;
	m_CheckBoxes = enable;
	if (m_List.IsWindow()) m_List.Invalidate();
}

bool CTreeListViewCtrl::GetCheckBoxes() const {
	return m_CheckBoxes;
}

bool CTreeListViewCtrl::GetCheckState(HTLITEM hItem) const {
	ATLASSERT(hItem && hItem != &m_Root);
	return hItem->checked;
}

void CTreeListViewCtrl::SetCheckState(HTLITEM hItem, bool checked) {
	ATLASSERT(hItem && hItem != &m_Root);
	if (hItem->checked == checked) return;
	hItem->checked = checked;

	NMTLVITEMCHECK nm = {};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom   = GetDlgCtrlID();
	nm.hdr.code     = TLVN_ITEMCHECK;
	nm.hItem        = hItem;
	nm.bChecked     = checked;
	FireNotify(&nm.hdr);

	int idx = VisibleIndex(hItem);
	if (idx >= 0) m_List.RedrawItems(idx, idx);
}

// ---- DrawCheckBox -----------------------------------------------------------

void CTreeListViewCtrl::DrawCheckBox(CDCHandle dc, const RECT& r, bool checked) {
	if (!m_ThemeBtn.IsThemeNull()) {
		int state = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
		m_ThemeBtn.DrawThemeBackground(dc, BP_CHECKBOX, state, &r, nullptr);
	}
	else {
		UINT flags = DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0);
		CRect rc = r;
		dc.DrawFrameControl(&rc, DFC_BUTTON, flags);
	}
}
