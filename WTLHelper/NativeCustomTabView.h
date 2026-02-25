#pragma once

template <class T, class TBase = ATL::CWindow, class TWinTraits = ATL::CControlWinTraits>
class ATL_NO_VTABLE CNativeCustomTabViewImpl : public ATL::CWindowImpl< T, TBase, TWinTraits > {
public:
	DECLARE_WND_CLASS_EX2(nullptr, T, 0, COLOR_APPWORKSPACE)

	// Declarations and enums
	struct TABVIEWPAGE {
		HWND hWnd;
		LPTSTR lpstrTitle;
		LPVOID pData;
	};

	struct TCITEMEXTRA {
		TCITEMHEADER tciheader;
		TABVIEWPAGE tvpage;

		operator LPTCITEM() { return (LPTCITEM)this; }
	};

	enum {
		m_nTabID = 1313,
		m_cxMoveMark = 6,
		m_cyMoveMark = 3,
		m_nMenuItemsMax = (ID_WINDOW_TABLAST - ID_WINDOW_TABFIRST + 1)
	};

	enum { _nAutoScrollTimerID = 4321 };

	enum AutoScroll {
		_AUTOSCROLL_NONE = 0,
		_AUTOSCROLL_LEFT = -1,
		_AUTOSCROLL_RIGHT = 1
	};

	enum CloseBtn {
		_cxCloseBtn = 14,
		_cyCloseBtn = 13,
		_cxCloseBtnMargin = 4,
		_cxCloseBtnMarginSel = 1,

		_nCloseBtnID = ID_PANE_CLOSE
	};

	// Data members
	CTabCtrl m_tab;
	int m_cyTabHeight;

	int m_nActivePage;

	int m_nInsertItem;
	POINT m_ptStartDrag;

	CMenuHandle m_menu;

	int m_cchTabTextLength;

	int m_nMenuItemsCount;

	ATL::CWindow m_wndTitleBar;
	LPTSTR m_lpstrTitleBarBase;
	int m_cchTitleBarLength;

	CImageList m_ilDrag;

	AutoScroll m_AutoScroll;
	CUpDownCtrl m_ud;

	CTabViewCloseBtn m_btnClose;
	int m_nCloseItem;

	bool m_bDestroyPageOnRemove : 1;
	bool m_bDestroyImageList : 1;
	bool m_bActivePageMenuItem : 1;
	bool m_bActiveAsDefaultMenuItem : 1;
	bool m_bEmptyMenuItem : 1;
	bool m_bWindowsMenuItem : 1;
	bool m_bNoTabDrag : 1;
	bool m_bNoTabDragAutoScroll : 1;
	bool m_bTabCloseButton : 1;
	// internal
	bool m_bTabCapture : 1;
	bool m_bTabDrag : 1;
	bool m_bInternalFont : 1;

	// Constructor/destructor
	CNativeCustomTabViewImpl() :
		m_cyTabHeight(0),
		m_nActivePage(-1),
		m_nInsertItem(-1),
		m_cchTabTextLength(30),
		m_nMenuItemsCount(10),
		m_lpstrTitleBarBase(nullptr),
		m_cchTitleBarLength(100),
		m_AutoScroll(_AUTOSCROLL_NONE),
		m_nCloseItem(-1),
		m_bDestroyPageOnRemove(true),
		m_bDestroyImageList(true),
		m_bActivePageMenuItem(true),
		m_bActiveAsDefaultMenuItem(false),
		m_bEmptyMenuItem(false),
		m_bWindowsMenuItem(false),
		m_bNoTabDrag(false),
		m_bNoTabDragAutoScroll(false),
		m_bTabCloseButton(true),
		m_bTabCapture(false),
		m_bTabDrag(false),
		m_bInternalFont(false) {
		m_ptStartDrag.x = 0;
		m_ptStartDrag.y = 0;
	}

	~CNativeCustomTabViewImpl() {
		delete[] m_lpstrTitleBarBase;
	}

	// Message filter function - to be called from PreTranslateMessage of the main window
	BOOL PreTranslateMessage(MSG* pMsg) {
		if (this->IsWindow() == FALSE)
			return FALSE;

		BOOL bRet = FALSE;

		// Check for TabView built-in accelerators (Ctrl+Tab/Ctrl+Shift+Tab - next/previous page)
		int nCount = GetPageCount();
		if (nCount > 0) {
			bool bControl = (::GetKeyState(VK_CONTROL) < 0);
			if ((pMsg->message == WM_KEYDOWN) && (pMsg->wParam == VK_TAB) && bControl) {
				if (nCount > 1) {
					int nPage = m_nActivePage;
					bool bShift = (::GetKeyState(VK_SHIFT) < 0);
					if (bShift)
						nPage = (nPage > 0) ? (nPage - 1) : (nCount - 1);
					else
						nPage = ((nPage >= 0) && (nPage < (nCount - 1))) ? (nPage + 1) : 0;

					SetActivePage(nPage);
					T* pT = static_cast<T*>(this);
					pT->OnPageActivated(m_nActivePage);
				}

				bRet = TRUE;
			}
		}

		// If we are doing drag-drop, check for Escape key that cancels it
		if (bRet == FALSE) {
			if (m_bTabCapture && (pMsg->message == WM_KEYDOWN) && (pMsg->wParam == VK_ESCAPE)) {
				::ReleaseCapture();
				bRet = TRUE;
			}
		}

		// Pass the message to the active page
		if (bRet == FALSE) {
			if (m_nActivePage != -1)
				bRet = (BOOL)::SendMessage(GetPageHWND(m_nActivePage), WM_FORWARDMSG, 0, (LPARAM)pMsg);
		}

		return bRet;
	}

	// Attributes
	int GetPageCount() const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		return m_tab.GetItemCount();
	}

	int GetActivePage() const {
		return m_nActivePage;
	}

	void SetActivePage(int nPage) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		T* pT = static_cast<T*>(this);

		this->SetRedraw(FALSE);

		if (m_nActivePage != -1)
			::ShowWindow(GetPageHWND(m_nActivePage), SW_HIDE);
		m_nActivePage = nPage;
		m_tab.SetCurSel(m_nActivePage);
		::ShowWindow(GetPageHWND(m_nActivePage), SW_SHOW);

		pT->UpdateLayout();

		this->SetRedraw(TRUE);
		this->RedrawWindow(nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

		HWND hWndFocus = ::GetFocus();
		ATL::CWindow wndTop = this->GetTopLevelWindow();
		if ((hWndFocus == wndTop.m_hWnd) || ((wndTop.IsChild(hWndFocus) != FALSE) && (hWndFocus != m_tab.m_hWnd)))
			::SetFocus(GetPageHWND(m_nActivePage));

		pT->UpdateTitleBar();
		pT->UpdateMenu();
	}

	HIMAGELIST GetImageList() const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		return m_tab.GetImageList();
	}

	HIMAGELIST SetImageList(HIMAGELIST hImageList) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		return m_tab.SetImageList(hImageList);
	}

	void SetWindowMenu(HMENU hMenu) {
		ATLASSERT(::IsWindow(this->m_hWnd));

		m_menu = hMenu;

		T* pT = static_cast<T*>(this);
		pT->UpdateMenu();
	}

	void SetTitleBarWindow(HWND hWnd) {
		ATLASSERT(::IsWindow(this->m_hWnd));

		delete[] m_lpstrTitleBarBase;
		m_lpstrTitleBarBase = nullptr;

		m_wndTitleBar = hWnd;
		if (hWnd == nullptr)
			return;

		int cchLen = m_wndTitleBar.GetWindowTextLength() + 1;
		ATLTRY(m_lpstrTitleBarBase = new TCHAR[cchLen]);
		if (m_lpstrTitleBarBase != nullptr) {
			m_wndTitleBar.GetWindowText(m_lpstrTitleBarBase, cchLen);
			T* pT = static_cast<T*>(this);
			pT->UpdateTitleBar();
		}
	}

	// Page attributes
	HWND GetPageHWND(int nPage) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_PARAM;
		m_tab.GetItem(nPage, tcix);

		return tcix.tvpage.hWnd;
	}

	LPCTSTR GetPageTitle(int nPage) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_PARAM;
		if (m_tab.GetItem(nPage, tcix) == FALSE)
			return nullptr;

		return tcix.tvpage.lpstrTitle;
	}

	bool SetPageTitle(int nPage, LPCTSTR lpstrTitle) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		T* pT = static_cast<T*>(this);

		int cchBuff = lstrlen(lpstrTitle) + 1;
		LPTSTR lpstrBuff = nullptr;
		ATLTRY(lpstrBuff = new TCHAR[cchBuff]);
		if (lpstrBuff == nullptr)
			return false;

		ATL::Checked::tcscpy_s(lpstrBuff, cchBuff, lpstrTitle);
		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_PARAM;
		if (m_tab.GetItem(nPage, tcix) == FALSE)
			return false;

		ATL::CTempBuffer<TCHAR, _WTL_STACK_ALLOC_THRESHOLD> buff;
		LPTSTR lpstrTabText = buff.Allocate(m_cchTabTextLength + 1);
		if (lpstrTabText == nullptr)
			return false;

		delete[] tcix.tvpage.lpstrTitle;

		pT->ShortenTitle(lpstrTitle, lpstrTabText, m_cchTabTextLength + 1);

		tcix.tciheader.mask = TCIF_TEXT | TCIF_PARAM;
		tcix.tciheader.pszText = lpstrTabText;
		tcix.tvpage.lpstrTitle = lpstrBuff;
		if (m_tab.SetItem(nPage, tcix) == FALSE)
			return false;

		pT->UpdateTitleBar();
		pT->UpdateMenu();

		return true;
	}

	LPVOID GetPageData(int nPage) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_PARAM;
		m_tab.GetItem(nPage, tcix);

		return tcix.tvpage.pData;
	}

	LPVOID SetPageData(int nPage, LPVOID pData) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_PARAM;
		m_tab.GetItem(nPage, tcix);
		LPVOID pDataOld = tcix.tvpage.pData;

		tcix.tvpage.pData = pData;
		m_tab.SetItem(nPage, tcix);

		return pDataOld;
	}

	int GetPageImage(int nPage) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_IMAGE;
		m_tab.GetItem(nPage, tcix);

		return tcix.tciheader.iImage;
	}

	int SetPageImage(int nPage, int nImage) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_IMAGE;
		m_tab.GetItem(nPage, tcix);
		int nImageOld = tcix.tciheader.iImage;

		tcix.tciheader.iImage = nImage;
		m_tab.SetItem(nPage, tcix);

		return nImageOld;
	}

	// Operations
	bool AddPage(HWND hWndView, LPCTSTR lpstrTitle, int nImage = -1, LPVOID pData = nullptr) {
		return InsertPage(GetPageCount(), hWndView, lpstrTitle, nImage, pData);
	}

	bool InsertPage(int nPage, HWND hWndView, LPCTSTR lpstrTitle, int nImage = -1, LPVOID pData = nullptr) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT((nPage == GetPageCount()) || IsValidPageIndex(nPage));

		T* pT = static_cast<T*>(this);

		int cchBuff = lstrlen(lpstrTitle) + 1;
		LPTSTR lpstrBuff = nullptr;
		ATLTRY(lpstrBuff = new TCHAR[cchBuff]);
		if (lpstrBuff == nullptr)
			return false;

		ATL::Checked::tcscpy_s(lpstrBuff, cchBuff, lpstrTitle);

		ATL::CTempBuffer<TCHAR, _WTL_STACK_ALLOC_THRESHOLD> buff;
		LPTSTR lpstrTabText = buff.Allocate(m_cchTabTextLength + 1);
		if (lpstrTabText == nullptr)
			return false;

		pT->ShortenTitle(lpstrTitle, lpstrTabText, m_cchTabTextLength + 1);

		this->SetRedraw(FALSE);

		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_PARAM;
		tcix.tciheader.pszText = lpstrTabText;
		tcix.tciheader.iImage = nImage;
		tcix.tvpage.hWnd = hWndView;
		tcix.tvpage.lpstrTitle = lpstrBuff;
		tcix.tvpage.pData = pData;
		int nItem = m_tab.InsertItem(nPage, tcix);
		if (nItem == -1) {
			delete[] lpstrBuff;
			this->SetRedraw(TRUE);
			return false;
		}

		// adjust active page index, if inserted before it
		if (nPage <= m_nActivePage)
			m_nActivePage++;

		SetActivePage(nItem);
		pT->OnPageActivated(m_nActivePage);

		if (GetPageCount() == 1)
			pT->ShowTabControl(true);

		pT->UpdateLayout();

		this->SetRedraw(TRUE);
		this->RedrawWindow(nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

		return true;
	}

	void RemovePage(int nPage) {
		ATLASSERT(::IsWindow(this->m_hWnd));
		ATLASSERT(IsValidPageIndex(nPage));

		T* pT = static_cast<T*>(this);

		this->SetRedraw(FALSE);

		if (GetPageCount() == 1)
			pT->ShowTabControl(false);

		if (m_bDestroyPageOnRemove)
			::DestroyWindow(GetPageHWND(nPage));
		else
			::ShowWindow(GetPageHWND(nPage), SW_HIDE);
		LPTSTR lpstrTitle = (LPTSTR)GetPageTitle(nPage);
		delete[] lpstrTitle;

		ATLVERIFY(m_tab.DeleteItem(nPage) != FALSE);

		if (m_nActivePage == nPage) {
			m_nActivePage = -1;

			if (nPage > 0) {
				SetActivePage(nPage - 1);
			}
			else if (GetPageCount() > 0) {
				SetActivePage(nPage);
			}
			else {
				this->SetRedraw(TRUE);
				this->Invalidate();
				this->UpdateWindow();
				pT->UpdateTitleBar();
				pT->UpdateMenu();
			}
		}
		else {
			nPage = (nPage < m_nActivePage) ? (m_nActivePage - 1) : m_nActivePage;
			m_nActivePage = -1;
			SetActivePage(nPage);
		}

		pT->OnPageActivated(m_nActivePage);
	}

	void RemoveAllPages() {
		ATLASSERT(::IsWindow(this->m_hWnd));

		if (GetPageCount() == 0)
			return;

		T* pT = static_cast<T*>(this);

		this->SetRedraw(FALSE);

		pT->ShowTabControl(false);

		for (int i = 0; i < GetPageCount(); i++) {
			if (m_bDestroyPageOnRemove)
				::DestroyWindow(GetPageHWND(i));
			else
				::ShowWindow(GetPageHWND(i), SW_HIDE);
			LPTSTR lpstrTitle = (LPTSTR)GetPageTitle(i);
			delete[] lpstrTitle;
		}
		m_tab.DeleteAllItems();

		m_nActivePage = -1;
		pT->OnPageActivated(m_nActivePage);

		this->SetRedraw(TRUE);
		this->Invalidate();
		this->UpdateWindow();

		pT->UpdateTitleBar();
		pT->UpdateMenu();
	}

	int PageIndexFromHwnd(HWND hWnd) const {
		int nIndex = -1;

		for (int i = 0; i < GetPageCount(); i++) {
			if (GetPageHWND(i) == hWnd) {
				nIndex = i;
				break;
			}
		}

		return nIndex;
	}

	void BuildWindowMenu(HMENU hMenu, int nMenuItemsCount = 10, bool bEmptyMenuItem = true, bool bWindowsMenuItem = true, bool bActivePageMenuItem = true, bool bActiveAsDefaultMenuItem = false) {
		ATLASSERT(::IsWindow(this->m_hWnd));

		CMenuHandle menu = hMenu;
		T* pT = static_cast<T*>(this);
		(void)pT;   // avoid level 4 warning
		int nFirstPos = 0;

		// Find first menu item in our range
		for (nFirstPos = 0; nFirstPos < menu.GetMenuItemCount(); nFirstPos++) {
			UINT nID = menu.GetMenuItemID(nFirstPos);
			if (((nID >= ID_WINDOW_TABFIRST) && (nID <= ID_WINDOW_TABLAST)) || (nID == ID_WINDOW_SHOWTABLIST))
				break;
		}

		// Remove all menu items for tab pages
		BOOL bRet = TRUE;
		while (bRet != FALSE)
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

		// Add menu items for all pages
		if (nPageCount > 0) {
			// Append menu items for all pages
			const int cchPrefix = 3;   // 2 digits + space
			nMenuItemsCount = __min(__min(nPageCount, nMenuItemsCount), (int)m_nMenuItemsMax);
			ATLASSERT(nMenuItemsCount < 100);   // 2 digits only
			if (nMenuItemsCount >= 100)
				nMenuItemsCount = 99;

			for (int i = 0; i < nMenuItemsCount; i++) {
				LPCTSTR lpstrTitle = GetPageTitle(i);
				int nLen = lstrlen(lpstrTitle);
				ATL::CTempBuffer<TCHAR, _WTL_STACK_ALLOC_THRESHOLD> buff;
				LPTSTR lpstrText = buff.Allocate(cchPrefix + nLen + 1);
				ATLASSERT(lpstrText != nullptr);
				if (lpstrText != nullptr) {
					LPCTSTR lpstrFormat = (i < 9) ? _T("&%i %s") : _T("%i %s");
					_stprintf_s(lpstrText, cchPrefix + nLen + 1, lpstrFormat, i + 1, lpstrTitle);
					menu.AppendMenu(MF_STRING, ID_WINDOW_TABFIRST + i, lpstrText);
				}
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
				menu.AppendMenu(MF_BYPOSITION | MF_STRING, ID_WINDOW_TABFIRST, pT->GetEmptyListText());
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

		// Add "Windows..." menu item
		if (bWindowsMenuItem)
			menu.AppendMenu(MF_BYPOSITION | MF_STRING, ID_WINDOW_SHOWTABLIST, pT->GetWindowsMenuItemText());
	}

	BOOL SubclassWindow(HWND hWnd) {
		BOOL bRet = ATL::CWindowImpl< T, TBase, TWinTraits >::SubclassWindow(hWnd);
		if (bRet != FALSE) {
			T* pT = static_cast<T*>(this);
			pT->CreateTabControl();
			pT->UpdateLayout();
		}

		return bRet;
	}

	// Message map and handlers
	BEGIN_MSG_MAP(CNativeCustomTabViewImpl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTabContextMenu)
		NOTIFY_HANDLER(m_nTabID, TCN_SELCHANGE, OnTabChanged)
		NOTIFY_ID_HANDLER(m_nTabID, OnTabNotification)
		NOTIFY_CODE_HANDLER(TTN_GETDISPINFO, OnTabGetDispInfo)
		FORWARD_NOTIFICATIONS()
		ALT_MSG_MAP(1)   // tab control
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnTabLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnTabLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnTabCaptureChanged)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnTabMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnTabMouseLeave)
		NOTIFY_HANDLER(T::_nCloseBtnID, TBVN_CLOSEBTNMOUSELEAVE, OnTabCloseBtnMouseLeave)
		COMMAND_HANDLER(T::_nCloseBtnID, BN_CLICKED, OnTabCloseBtnClicked)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		T* pT = static_cast<T*>(this);
		pT->CreateTabControl();

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		RemoveAllPages();

		if (m_bDestroyImageList) {
			CImageList il = m_tab.SetImageList(nullptr);
			if (il.m_hImageList != nullptr)
				il.Destroy();
		}

		if (m_bInternalFont) {
			HFONT hFont = m_tab.GetFont();
			m_tab.SetFont(nullptr, FALSE);
			::DeleteObject(hFont);
			m_bInternalFont = false;
		}

		m_ud.m_hWnd = nullptr;

		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		T* pT = static_cast<T*>(this);
		pT->UpdateLayout();
		return 0;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		if (m_nActivePage != -1)
			::SetFocus(GetPageHWND(m_nActivePage));
		return 0;
	}

	LRESULT OnGetFont(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		return m_tab.SendMessage(WM_GETFONT);
	}

	LRESULT OnSetFont(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
		if (m_bInternalFont) {
			HFONT hFont = m_tab.GetFont();
			m_tab.SetFont(nullptr, FALSE);
			::DeleteObject(hFont);
			m_bInternalFont = false;
		}

		m_tab.SendMessage(WM_SETFONT, wParam, lParam);

		T* pT = static_cast<T*>(this);
		m_cyTabHeight = pT->CalcTabHeight();

		if ((BOOL)lParam != FALSE)
			pT->UpdateLayout();

		return 0;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (wParam == _nAutoScrollTimerID) {
			T* pT = static_cast<T*>(this);
			pT->DoAutoScroll();
		}
		else {
			bHandled = FALSE;
		}

		return 0;
	}

	LRESULT OnTabContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		int nPage = m_nActivePage;
		bool bAction = false;
		if ((HWND)wParam == m_tab.m_hWnd) {
			if ((pt.x == -1) && (pt.y == -1))   // keyboard
			{
				RECT rect = {};
				m_tab.GetItemRect(m_nActivePage, &rect);
				pt.x = rect.left;
				pt.y = rect.bottom;
				m_tab.ClientToScreen(&pt);
				bAction = true;
			}
			else if (::WindowFromPoint(pt) == m_tab.m_hWnd) {
				TCHITTESTINFO hti = {};
				hti.pt = pt;
				this->ScreenToClient(&hti.pt);
				nPage = m_tab.HitTest(&hti);

				bAction = true;
			}
		}

		if (bAction) {
			T* pT = static_cast<T*>(this);
			pT->OnContextMenu(nPage, pt);
		}
		else {
			bHandled = FALSE;
		}

		return 0;
	}

	LRESULT OnTabChanged(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
		if (m_bTabCloseButton && (m_btnClose.m_hWnd != nullptr)) {
			T* pT = static_cast<T*>(this);
			RECT rcClose = {};
			pT->CalcCloseButtonRect(m_nCloseItem, rcClose);
			m_btnClose.SetWindowPos(nullptr, &rcClose, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		SetActivePage(m_tab.GetCurSel());
		T* pT = static_cast<T*>(this);
		pT->OnPageActivated(m_nActivePage);

		return 0;
	}

	LRESULT OnTabNotification(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
		// nothing to do - this just blocks all tab control
		// notifications from being propagated further
		return 0;
	}

	LRESULT OnTabGetDispInfo(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled) {
		LPNMTTDISPINFO pTTDI = (LPNMTTDISPINFO)pnmh;
		if (pTTDI->hdr.hwndFrom == m_tab.GetTooltips()) {
			T* pT = static_cast<T*>(this);
			pT->UpdateTooltipText(pTTDI);
		}
		else {
			bHandled = FALSE;
		}

		return 0;
	}

	// Tab control message handlers
	LRESULT OnTabLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
		if (!m_bNoTabDrag && (m_tab.GetItemCount() > 1)) {
			m_bTabCapture = true;
			m_tab.SetCapture();

			m_ptStartDrag.x = GET_X_LPARAM(lParam);
			m_ptStartDrag.y = GET_Y_LPARAM(lParam);
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnTabLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
		if (m_bTabCapture) {
			if (m_bTabDrag) {
				T* pT = static_cast<T*>(this);
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				int nItem = pT->DragHitTest(pt);
				if (nItem != -1)
					MovePage(m_nActivePage, nItem);
			}

			::ReleaseCapture();
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnTabCaptureChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
		if (m_bTabCapture) {
			m_bTabCapture = false;

			if (m_bTabDrag) {
				m_bTabDrag = false;

				T* pT = static_cast<T*>(this);
				if (!m_bNoTabDragAutoScroll)
					pT->StartStopAutoScroll(-1);

				pT->DrawMoveMark(-1);

				m_ilDrag.DragLeave(GetDesktopWindow());
				m_ilDrag.EndDrag();

				m_ilDrag.Destroy();
				m_ilDrag.m_hImageList = nullptr;
			}
		}

		bHandled = FALSE;
		return 0;
	}

	LRESULT OnTabMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
		bHandled = FALSE;

		if (m_bTabCapture) {
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

			if (!m_bTabDrag) {
				if ((abs(m_ptStartDrag.x - GET_X_LPARAM(lParam)) >= ::GetSystemMetrics(SM_CXDRAG)) ||
					(abs(m_ptStartDrag.y - GET_Y_LPARAM(lParam)) >= ::GetSystemMetrics(SM_CYDRAG))) {
					T* pT = static_cast<T*>(this);
					pT->GenerateDragImage(m_nActivePage);

					int cxCursor = ::GetSystemMetrics(SM_CXCURSOR);
					int cyCursor = ::GetSystemMetrics(SM_CYCURSOR);
					m_ilDrag.BeginDrag(0, -(cxCursor / 2), -(cyCursor / 2));
					POINT ptEnter = m_ptStartDrag;
					m_tab.ClientToScreen(&ptEnter);
					m_ilDrag.DragEnter(GetDesktopWindow(), ptEnter);

					m_bTabDrag = true;
				}
			}

			if (m_bTabDrag) {
				T* pT = static_cast<T*>(this);
				int nItem = pT->DragHitTest(pt);

				pT->SetMoveCursor(nItem != -1);

				if (m_nInsertItem != nItem)
					pT->DrawMoveMark(nItem);

				if (!m_bNoTabDragAutoScroll)
					pT->StartStopAutoScroll(pt.x);

				m_ilDrag.DragShowNolock((nItem != -1) ? TRUE : FALSE);
				m_tab.ClientToScreen(&pt);
				m_ilDrag.DragMove(pt);

				bHandled = TRUE;
			}
		}
		else if (m_bTabCloseButton) {
			TCHITTESTINFO thti = {};
			thti.pt.x = GET_X_LPARAM(lParam);
			thti.pt.y = GET_Y_LPARAM(lParam);

			int nItem = m_tab.HitTest(&thti);
			if (nItem >= 0) {
				ATLTRACE(_T("+++++ item = %i\n"), nItem);

				T* pT = static_cast<T*>(this);
				if (m_btnClose.m_hWnd == nullptr) {
					pT->CreateCloseButton(nItem);
					m_nCloseItem = nItem;
				}
				else if (m_nCloseItem != nItem) {
					RECT rcClose = {};
					pT->CalcCloseButtonRect(nItem, rcClose);
					m_btnClose.SetWindowPos(nullptr, &rcClose, SWP_NOZORDER | SWP_NOACTIVATE);
					m_nCloseItem = nItem;
				}

				TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_tab.m_hWnd };
				::TrackMouseEvent(&tme);
			}
		}

		return 0;
	}

	LRESULT OnTabMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
		bHandled = FALSE;

		if (m_btnClose.m_hWnd != nullptr) {
			POINT pt = {};
			::GetCursorPos(&pt);
			RECT rect = {};
			m_btnClose.GetWindowRect(&rect);
			if (::PtInRect(&rect, pt) == FALSE) {
				m_nCloseItem = -1;
				T* pT = static_cast<T*>(this);
				pT->DestroyCloseButton();
			}
			else {
				bHandled = TRUE;
			}
		}

		return 0;
	}

	LRESULT OnTabCloseBtnMouseLeave(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/) {
		TCHITTESTINFO thti = {};
		::GetCursorPos(&thti.pt);
		m_tab.ScreenToClient(&thti.pt);
		int nItem = m_tab.HitTest(&thti);
		if (nItem == -1)
			m_tab.SendMessage(WM_MOUSELEAVE);

		return 0;
	}

	LRESULT OnTabCloseBtnClicked(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
		T* pT = static_cast<T*>(this);
		pT->OnTabCloseBtn(m_nCloseItem);

		return 0;
	}

	// Implementation helpers
	bool IsValidPageIndex(int nPage) const {
		return ((nPage >= 0) && (nPage < GetPageCount()));
	}

	bool MovePage(int nMovePage, int nInsertBeforePage) {
		ATLASSERT(IsValidPageIndex(nMovePage));
		ATLASSERT(IsValidPageIndex(nInsertBeforePage));

		if (!IsValidPageIndex(nMovePage) || !IsValidPageIndex(nInsertBeforePage))
			return false;

		if (nMovePage == nInsertBeforePage)
			return true;   // nothing to do

		ATL::CTempBuffer<TCHAR, _WTL_STACK_ALLOC_THRESHOLD> buff;
		LPTSTR lpstrTabText = buff.Allocate(m_cchTabTextLength + 1);
		if (lpstrTabText == nullptr)
			return false;
		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_PARAM;
		tcix.tciheader.pszText = lpstrTabText;
		tcix.tciheader.cchTextMax = m_cchTabTextLength + 1;
		BOOL bRet = m_tab.GetItem(nMovePage, tcix);
		ATLASSERT(bRet != FALSE);
		if (bRet == FALSE)
			return false;

		int nInsertItem = (nInsertBeforePage > nMovePage) ? nInsertBeforePage + 1 : nInsertBeforePage;
		int nNewItem = m_tab.InsertItem(nInsertItem, tcix);
		ATLASSERT(nNewItem == nInsertItem);
		if (nNewItem != nInsertItem) {
			ATLVERIFY(m_tab.DeleteItem(nNewItem));
			return false;
		}

		if (nMovePage > nInsertBeforePage)
			ATLVERIFY(m_tab.DeleteItem(nMovePage + 1) != FALSE);
		else if (nMovePage < nInsertBeforePage)
			ATLVERIFY(m_tab.DeleteItem(nMovePage) != FALSE);

		SetActivePage(nInsertBeforePage);
		T* pT = static_cast<T*>(this);
		pT->OnPageActivated(m_nActivePage);

		return true;
	}

	// Implementation overrideables
	bool CreateTabControl() {
		m_tab.Create(this->m_hWnd, this->rcDefault, nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | TCS_TOOLTIPS, 0, m_nTabID);
		ATLASSERT(m_tab.m_hWnd != nullptr);
		if (m_tab.m_hWnd == nullptr)
			return false;

		m_tab.SetFont(AtlCreateControlFont());
		m_bInternalFont = true;

		m_tab.SetItemExtra(sizeof(TABVIEWPAGE));

		T* pT = static_cast<T*>(this);
		m_cyTabHeight = pT->CalcTabHeight();

		return true;
	}

	int CalcTabHeight() {
		int nCount = m_tab.GetItemCount();
		TCHAR szText[] = _T("NS");
		TCITEMEXTRA tcix = {};
		tcix.tciheader.mask = TCIF_TEXT;
		tcix.tciheader.pszText = szText;
		int nIndex = m_tab.InsertItem(nCount, tcix);

		RECT rect = { 0, 0, 1000, 1000 };
		m_tab.AdjustRect(FALSE, &rect);

		RECT rcWnd = { 0, 0, 1000, rect.top };
		::AdjustWindowRectEx(&rcWnd, m_tab.GetStyle(), FALSE, m_tab.GetExStyle());

		int nHeight = rcWnd.bottom - rcWnd.top;

		m_tab.DeleteItem(nIndex);

		return nHeight;
	}

	void ShowTabControl(bool bShow) {
		m_tab.ShowWindow(bShow ? SW_SHOWNOACTIVATE : SW_HIDE);
		T* pT = static_cast<T*>(this);
		pT->UpdateLayout();
	}

	void UpdateLayout() {
		RECT rect = {};
		this->GetClientRect(&rect);

		int cyOffset = 0;
		if (m_tab.IsWindow() && ((m_tab.GetStyle() & WS_VISIBLE) != 0)) {
			m_tab.SetWindowPos(nullptr, 0, 0, rect.right - rect.left, m_cyTabHeight, SWP_NOZORDER);
			cyOffset = m_cyTabHeight;
		}

		if (m_nActivePage != -1)
			::SetWindowPos(GetPageHWND(m_nActivePage), nullptr, 0, cyOffset, rect.right - rect.left, rect.bottom - rect.top - cyOffset, SWP_NOZORDER);
	}

	void UpdateMenu() {
		if (m_menu.m_hMenu != nullptr)
			BuildWindowMenu(m_menu, m_nMenuItemsCount, m_bEmptyMenuItem, m_bWindowsMenuItem, m_bActivePageMenuItem, m_bActiveAsDefaultMenuItem);
	}

	void UpdateTitleBar() {
		if (!m_wndTitleBar.IsWindow() || (m_lpstrTitleBarBase == nullptr))
			return;   // nothing to do

		if (m_nActivePage != -1) {
			T* pT = static_cast<T*>(this);
			LPCTSTR lpstrTitle = pT->GetPageTitle(m_nActivePage);
			LPCTSTR lpstrDivider = pT->GetTitleDividerText();
			int cchBuffer = m_cchTitleBarLength + lstrlen(lpstrDivider) + lstrlen(m_lpstrTitleBarBase) + 1;
			ATL::CTempBuffer<TCHAR, _WTL_STACK_ALLOC_THRESHOLD> buff;
			LPTSTR lpstrPageTitle = buff.Allocate(cchBuffer);
			ATLASSERT(lpstrPageTitle != nullptr);
			if (lpstrPageTitle != nullptr) {
				pT->ShortenTitle(lpstrTitle, lpstrPageTitle, m_cchTitleBarLength + 1);
				ATL::Checked::tcscat_s(lpstrPageTitle, cchBuffer, lpstrDivider);
				ATL::Checked::tcscat_s(lpstrPageTitle, cchBuffer, m_lpstrTitleBarBase);
			}
			else {
				lpstrPageTitle = m_lpstrTitleBarBase;
			}

			m_wndTitleBar.SetWindowText(lpstrPageTitle);
		}
		else {
			m_wndTitleBar.SetWindowText(m_lpstrTitleBarBase);
		}
	}

	void DrawMoveMark(int nItem) {
		T* pT = static_cast<T*>(this);

		if (m_nInsertItem != -1) {
			RECT rect = {};
			pT->GetMoveMarkRect(rect);
			m_tab.InvalidateRect(&rect);
		}

		m_nInsertItem = nItem;

		if (m_nInsertItem != -1) {
			CClientDC dc(m_tab.m_hWnd);

			RECT rect = {};
			pT->GetMoveMarkRect(rect);

			CPen pen;
			pen.CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_WINDOWTEXT));
			CBrush brush;
			brush.CreateSolidBrush(::GetSysColor(COLOR_WINDOWTEXT));

			HPEN hPenOld = dc.SelectPen(pen);
			HBRUSH hBrushOld = dc.SelectBrush(brush);

			int x = rect.left;
			int y = rect.top;
			POINT ptsTop[3] = { { x, y }, { x + m_cxMoveMark, y }, { x + (m_cxMoveMark / 2), y + m_cyMoveMark } };
			dc.Polygon(ptsTop, 3);

			y = rect.bottom - 1;
			POINT ptsBottom[3] = { { x, y }, { x + m_cxMoveMark, y }, { x + (m_cxMoveMark / 2), y - m_cyMoveMark } };
			dc.Polygon(ptsBottom, 3);

			dc.SelectPen(hPenOld);
			dc.SelectBrush(hBrushOld);
		}
	}

	void GetMoveMarkRect(RECT& rect) const {
		m_tab.GetClientRect(&rect);

		RECT rcItem = {};
		m_tab.GetItemRect(m_nInsertItem, &rcItem);

		if (m_nInsertItem <= m_nActivePage) {
			rect.left = rcItem.left - m_cxMoveMark / 2 - 1;
			rect.right = rcItem.left + m_cxMoveMark / 2;
		}
		else {
			rect.left = rcItem.right - m_cxMoveMark / 2 - 1;
			rect.right = rcItem.right + m_cxMoveMark / 2;
		}
	}

	void SetMoveCursor(bool bCanMove) {
		::SetCursor(::LoadCursor(nullptr, bCanMove ? IDC_ARROW : IDC_NO));
	}

	void GenerateDragImage(int nItem) {
		ATLASSERT(IsValidPageIndex(nItem));

		RECT rcItem = {};
		m_tab.GetItemRect(nItem, &rcItem);
		::InflateRect(&rcItem, 2, 2);   // make bigger to cover selected item

		ATLASSERT(m_ilDrag.m_hImageList == nullptr);
		m_ilDrag.Create(rcItem.right - rcItem.left, rcItem.bottom - rcItem.top, ILC_COLORDDB | ILC_MASK, 1, 1);

		CClientDC dc(this->m_hWnd);
		CDC dcMem;
		dcMem.CreateCompatibleDC(dc);
		ATLASSERT(dcMem.m_hDC != nullptr);
		dcMem.SetViewportOrg(-rcItem.left, -rcItem.top);

		CBitmap bmp;
		bmp.CreateCompatibleBitmap(dc, rcItem.right - rcItem.left, rcItem.bottom - rcItem.top);
		ATLASSERT(bmp.m_hBitmap != nullptr);

		HBITMAP hBmpOld = dcMem.SelectBitmap(bmp);
		m_tab.SendMessage(WM_PRINTCLIENT, (WPARAM)dcMem.m_hDC);
		dcMem.SelectBitmap(hBmpOld);

		ATLVERIFY(m_ilDrag.Add(bmp.m_hBitmap, RGB(255, 0, 255)) != -1);
	}

	void ShortenTitle(LPCTSTR lpstrTitle, LPTSTR lpstrShortTitle, int cchShortTitle) {
		if (lstrlen(lpstrTitle) >= cchShortTitle) {
			LPCTSTR lpstrEllipsis = _T("...");
			int cchEllipsis = lstrlen(lpstrEllipsis);
			ATL::Checked::tcsncpy_s(lpstrShortTitle, cchShortTitle, lpstrTitle, cchShortTitle - cchEllipsis - 1);
			ATL::Checked::tcscat_s(lpstrShortTitle, cchShortTitle, lpstrEllipsis);
		}
		else {
			ATL::Checked::tcscpy_s(lpstrShortTitle, cchShortTitle, lpstrTitle);
		}
	}

	void UpdateTooltipText(LPNMTTDISPINFO pTTDI) {
		ATLASSERT(pTTDI != nullptr);
		pTTDI->lpszText = (LPTSTR)GetPageTitle((int)pTTDI->hdr.idFrom);
	}

	int DragHitTest(POINT pt) const {
		RECT rect = {};
		this->GetClientRect(&rect);
		if (::PtInRect(&rect, pt) == FALSE)
			return -1;

		m_tab.GetClientRect(&rect);
		TCHITTESTINFO hti = {};
		hti.pt.x = pt.x;
		hti.pt.y = rect.bottom / 2;   // use middle to ignore
		int nItem = m_tab.HitTest(&hti);
		if (nItem == -1) {
			int nLast = m_tab.GetItemCount() - 1;
			RECT rcItem = {};
			m_tab.GetItemRect(nLast, &rcItem);
			if (pt.x >= rcItem.right)
				nItem = nLast;
		}

		return nItem;
	}

	void StartStopAutoScroll(int x) {
		AutoScroll scroll = _AUTOSCROLL_NONE;
		if (x != -1) {
			RECT rect = {};
			m_tab.GetClientRect(&rect);
			int dx = ::GetSystemMetrics(SM_CXVSCROLL);
			if ((x >= 0) && (x < dx)) {
				RECT rcItem = {};
				m_tab.GetItemRect(0, &rcItem);
				if (rcItem.left < rect.left)
					scroll = _AUTOSCROLL_LEFT;
			}
			else if ((x >= (rect.right - dx)) && (x < rect.right)) {
				RECT rcItem = {};
				m_tab.GetItemRect(m_tab.GetItemCount() - 1, &rcItem);
				if (rcItem.right > rect.right)
					scroll = _AUTOSCROLL_RIGHT;
			}
		}

		if (scroll != _AUTOSCROLL_NONE) {
			if (m_ud.m_hWnd == nullptr)
				m_ud = m_tab.GetWindow(GW_CHILD);

			if (m_AutoScroll != scroll) {
				m_AutoScroll = scroll;
				this->SetTimer(_nAutoScrollTimerID, 300);
			}
		}
		else {
			this->KillTimer(_nAutoScrollTimerID);
			m_AutoScroll = _AUTOSCROLL_NONE;
		}
	}

	void DoAutoScroll() {
		ATLASSERT(m_AutoScroll != _AUTOSCROLL_NONE);

		int nMin = -1, nMax = -1;
		m_ud.GetRange(nMin, nMax);
		int nPos = m_ud.GetPos();

		int nNewPos = -1;
		if ((m_AutoScroll == _AUTOSCROLL_LEFT) && (nPos > nMin))
			nNewPos = nPos - 1;
		else if ((m_AutoScroll == _AUTOSCROLL_RIGHT) && (nPos < nMax))
			nNewPos = nPos + 1;
		if (nNewPos != -1) {
			m_tab.SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, nNewPos));
			m_tab.SendMessage(WM_HSCROLL, MAKEWPARAM(SB_ENDSCROLL, 0));

			POINT pt = {};
			::GetCursorPos(&pt);
			m_tab.ScreenToClient(&pt);
			m_tab.SendMessage(WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
		}
	}

	// Text for menu items and title bar - override to provide different strings
	static LPCTSTR GetEmptyListText() {
		return _T("(Empty)");
	}

	static LPCTSTR GetWindowsMenuItemText() {
		return _T("&Windows...");
	}

	static LPCTSTR GetTitleDividerText() {
		return _T(" - ");
	}

	// Notifications - override to provide different behavior
	void OnPageActivated(int nPage) {
		NMHDR nmhdr = {};
		nmhdr.hwndFrom = this->m_hWnd;
		nmhdr.idFrom = nPage;
		nmhdr.code = TBVN_PAGEACTIVATED;
		this->GetParent().SendMessage(WM_NOTIFY, this->GetDlgCtrlID(), (LPARAM)&nmhdr);
	}

	void OnContextMenu(int nPage, POINT pt) {
		TBVCONTEXTMENUINFO cmi = {};
		cmi.hdr.hwndFrom = this->m_hWnd;
		cmi.hdr.idFrom = nPage;
		cmi.hdr.code = TBVN_CONTEXTMENU;
		cmi.pt = pt;
		this->GetParent().SendMessage(WM_NOTIFY, this->GetDlgCtrlID(), (LPARAM)&cmi);
	}

	void OnTabCloseBtn(int nPage) {
		NMHDR nmhdr = {};
		nmhdr.hwndFrom = this->m_hWnd;
		nmhdr.idFrom = nPage;
		nmhdr.code = TBVN_TABCLOSEBTN;
		LRESULT lRet = this->GetParent().SendMessage(WM_NOTIFY, this->GetDlgCtrlID(), (LPARAM)&nmhdr);
		if (lRet == 0)   // default - close page
		{
			T* pT = static_cast<T*>(this);
			pT->RemovePage(m_nCloseItem);
			m_nCloseItem = -1;
			pT->DestroyCloseButton();
		}
		else {
			m_tab.SendMessage(WM_MOUSELEAVE);
		}
	}

	// Close button overrideables
	void CreateCloseButton(int nItem) {
		ATLASSERT(m_btnClose.m_hWnd == nullptr);

		m_btnClose.m_bPressed = false;

		T* pT = static_cast<T*>(this);
		RECT rcClose = {};
		pT->CalcCloseButtonRect(nItem, rcClose);
		m_btnClose.Create(m_tab.m_hWnd, rcClose, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, T::_nCloseBtnID);
		ATLASSERT(m_btnClose.IsWindow());

		if (m_btnClose.m_hWnd != nullptr) {
			// create a tool tip
			ATLASSERT(m_btnClose.m_tip.m_hWnd == nullptr);
			m_btnClose.m_tip.Create(m_btnClose.m_hWnd);
			ATLASSERT(m_btnClose.m_tip.IsWindow());

			if (m_btnClose.m_tip.IsWindow()) {
				m_btnClose.m_tip.Activate(TRUE);

				RECT rect = {};
				m_btnClose.GetClientRect(&rect);
				m_btnClose.m_tip.AddTool(m_btnClose.m_hWnd, LPSTR_TEXTCALLBACK, &rect, T::_nCloseBtnID);
			}
		}
	}

	void DestroyCloseButton() {
		ATLASSERT(m_btnClose.m_hWnd != nullptr);

		if (m_btnClose.m_hWnd != nullptr) {
			if (m_btnClose.m_tip.IsWindow()) {
				m_btnClose.m_tip.DestroyWindow();
				m_btnClose.m_tip.m_hWnd = nullptr;
			}

			m_btnClose.DestroyWindow();
		}
	}

	void CalcCloseButtonRect(int nItem, RECT& rcClose) {
		RECT rcItem = {};
		m_tab.GetItemRect(nItem, &rcItem);

		int cy = (rcItem.bottom - rcItem.top - _cyCloseBtn) / 2;
		int cx = (nItem == m_tab.GetCurSel()) ? _cxCloseBtnMarginSel : _cxCloseBtnMargin;
		::SetRect(&rcClose, rcItem.right - cx - _cxCloseBtn, rcItem.top + cy,
			rcItem.right - cx, rcItem.top + cy + _cyCloseBtn);
	}
};

class CNativeCustomTabView : public CNativeCustomTabViewImpl<CNativeCustomTabView> {
public:
	DECLARE_WND_CLASS_EX(_T("WTL_TabView"), 0, COLOR_APPWORKSPACE)
};
