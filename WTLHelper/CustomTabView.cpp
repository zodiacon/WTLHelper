#include "pch.h"
#include "CustomTabView.h"
#include "ThemeHelper.h"
#include "Theme.h"

LRESULT CCustomTabView::OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL& handled) const {
	if (GetPageCount() > 0) {
		handled = FALSE;
		return 0;
	}
	CDCHandle dc((HDC)wp);
	CRect rc;
	GetClientRect(&rc);
	dc.FillRect(&rc, ThemeHelper::GetCurrentTheme()->GetSysBrush(COLOR_WINDOW));

	return 1;
}

LRESULT CCustomTabView::OnUpdateTheme(UINT /*uMsg*/, WPARAM wp, LPARAM lParam, BOOL& /*bHandled*/) {
	Invalidate();
	return 0;
}

bool CCustomTabView::CreateTabControl() {
	m_tab.Create(this->m_hWnd, this->rcDefault, nullptr, 
		WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | TCS_FOCUSNEVER | TCS_HOTTRACK | TCS_OWNERDRAWFIXED | TCS_TOOLTIPS,
		0, m_nTabID);
	ATLASSERT(m_tab.m_hWnd != NULL);
	if (m_tab.m_hWnd == NULL)
		return false;

	m_tab.SetFont(AtlCreateControlFont());
	m_bInternalFont = true;

	m_tab.SetItemExtra(sizeof(TABVIEWPAGE));

	m_cyTabHeight = 0;

	return true;
}

void CCustomTabView::UpdateLayout() {
	if (!m_Redraw)
		return;

	if (GetPageCount() == 0)
		return;

	if (m_cyTabHeight == 0) {
		CRect item;
		m_tab.GetItemRect(0, &item);
		m_cyTabHeight = item.Height();
	}

	RECT rect{};
	this->GetClientRect(&rect);

	int cyOffset = 0;
	if (m_tab.IsWindow() && (m_tab.GetStyle() & WS_VISIBLE)) {
		int rows = 1;
		m_tab.SetWindowPos(NULL, 0, 0, rect.right - rect.left, m_cyTabHeight * rows + 4, SWP_NOZORDER);
		cyOffset = m_cyTabHeight * rows + 4;
	}

	if (m_nActivePage != -1)
		::SetWindowPos(GetPageHWND(m_nActivePage), NULL, 0, cyOffset, rect.right - rect.left, rect.bottom - rect.top - cyOffset, SWP_NOZORDER);
}

void CCustomTabView::SetRedraw(bool redraw) {
	m_tab.SetRedraw(redraw);
	m_Redraw = redraw;
}

void CCustomTabView::UpdateMenu() {
	if (m_menu.m_hMenu)
		BuildWindowMenu(m_menu, m_nMenuItemsCount, m_bEmptyMenuItem, m_bWindowsMenuItem, m_bActivePageMenuItem, m_bActiveAsDefaultMenuItem);
}

void CCustomTabView::BuildWindowMenu(HMENU hMenu, int nMenuItemsCount, bool bEmptyMenuItem, bool bWindowsMenuItem, bool bActivePageMenuItem, bool bActiveAsDefaultMenuItem) {
	ATLASSERT(::IsWindow(this->m_hWnd));

	CMenuHandle menu = hMenu;
	int nFirstPos = 2;

	// Find first menu item in our range
	for (nFirstPos = 0; nFirstPos < menu.GetMenuItemCount(); nFirstPos++) {
		UINT nID = menu.GetMenuItemID(nFirstPos);
		if (((nID >= ID_WINDOW_TABFIRST) && (nID <= ID_WINDOW_TABLAST)) || (nID == ID_WINDOW_SHOWTABLIST))
			break;
	}

	BOOL bRet = TRUE;
	while (bRet)
		bRet = menu.DeleteMenu(nFirstPos, MF_BYPOSITION);

	// Add separator if it's not already there
	int nPageCount = GetPageCount();
	if ((bWindowsMenuItem || (nPageCount > 0)) && (nFirstPos > 0)) {
		CMenuItemInfo mii;
		mii.fMask = MIIM_TYPE;
		menu.GetMenuItemInfo(nFirstPos - 1, TRUE, &mii);
		if ((mii.fType & MFT_SEPARATOR) == 0) {
			menu.AppendMenu(MF_SEPARATOR);
			nFirstPos++;
		}
	}

	if (nPageCount > 0) {
		// Append menu items for all pages
		nMenuItemsCount = __min(__min(nPageCount, nMenuItemsCount), (int)m_nMenuItemsMax);
		ATLASSERT(nMenuItemsCount < 100);   // 2 digits only
		if (nMenuItemsCount >= 100)
			nMenuItemsCount = 99;

		for (int i = 0; i < nMenuItemsCount; i++) {
			auto title = GetPageTitle(i);
			menu.AppendMenu(MF_STRING, ID_WINDOW_TABFIRST + i, L" " + CString(title));
		}

		// Mark active page
		if (bActivePageMenuItem && (m_nActivePage != -1)) {
			if (bActiveAsDefaultMenuItem) {
				menu.SetMenuDefaultItem((UINT)-1, TRUE);
				menu.SetMenuDefaultItem(nFirstPos + m_nActivePage, TRUE);
			}
			else {
				menu.CheckMenuRadioItem(nFirstPos, nFirstPos + nMenuItemsCount, nFirstPos + m_nActivePage, MF_BYPOSITION);
			}
		}
	}
	else {
		if (bEmptyMenuItem) {
			menu.AppendMenu(MF_BYPOSITION | MF_STRING, ID_WINDOW_TABFIRST, GetEmptyListText());
			menu.EnableMenuItem(ID_WINDOW_TABFIRST, MF_GRAYED);
		}

		// Remove separator if nothing else is there
		if (!bEmptyMenuItem && !bWindowsMenuItem && (nFirstPos > 0)) {
			CMenuItemInfo mii;
			mii.fMask = MIIM_TYPE;
			menu.GetMenuItemInfo(nFirstPos - 1, TRUE, &mii);
			if ((mii.fType & MFT_SEPARATOR) != 0)
				menu.DeleteMenu(nFirstPos - 1, MF_BYPOSITION);
		}
	}

	if (bWindowsMenuItem)
		menu.AppendMenu(MF_BYPOSITION | MF_STRING, ID_WINDOW_SHOWTABLIST, L"Tabs...");
	GetParent().SendMessage(WM_WINDOW_MENU_BUILT, (WPARAM)menu.m_hMenu);
}
