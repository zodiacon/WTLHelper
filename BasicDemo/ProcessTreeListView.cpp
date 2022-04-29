// View.cpp : implementation of the CProcessTreeListView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "ProcessTreeListView.h"

BOOL CProcessTreeListView::PreTranslateMessage(MSG* pMsg) {
	pMsg;
	return FALSE;
}

void CProcessTreeListView::OnFinalMessage(HWND /*hWnd*/) {
	delete this;
}

LRESULT CProcessTreeListView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_TreeList.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_TreeList.InsertColumn(0, L"Process Name", 220, LVCFMT_LEFT);
	m_TreeList.InsertColumn(1, L"PID", 100, LVCFMT_RIGHT);
	m_TreeList.InsertColumn(2, L"Threads", 100, LVCFMT_RIGHT);
	m_TreeList.InsertColumn(3, L"User name", 180, LVCFMT_LEFT);
	m_TreeList.InsertColumn(4, L"Image Path", 220, LVCFMT_LEFT);
	m_TreeList.InsertColumn(5, L"Command Line", 220, LVCFMT_LEFT);
	m_TreeList.InsertColumn(6, L"Working Set", 240, LVCFMT_RIGHT);

	Refresh();

	return 0;
}

void CProcessTreeListView::Refresh() {
	auto hSnapShot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe;
	pe.dwSize = sizeof(pe);

	::Process32First(hSnapShot, &pe);
	m_TreeList.SetRedraw(FALSE);
	HTREEITEM hParent = TVI_ROOT;
	int i = 0;
	do {
		auto hItem = m_TreeList.InsertItem(pe.szExeFile, hParent, TVI_LAST);
		ProcessInfo pi;
		pi.Name = pe.szExeFile;
		pi.Id = pe.th32ProcessID;
		pi.Threads = pe.cntThreads;
		if (++i % 4 == 0)
			hParent = hItem;
		else if (i % 6 == 0)
			hParent = TVI_ROOT;

		m_Processes.insert({ hItem, pi });
	} while (::Process32Next(hSnapShot, &pe));
	m_TreeList.SetRedraw(TRUE);

	::CloseHandle(hSnapShot);
}

LRESULT CProcessTreeListView::OnItemChanged(int, LPNMHDR, BOOL&) {
	return LRESULT();
}

LRESULT CProcessTreeListView::OnTreeGetDispInfo(int, LPNMHDR hdr, BOOL&) {
	auto di = (TLNMDISPINFO*)hdr;
	ATLASSERT(m_Processes.find(di->hItem) != m_Processes.end());

	auto& pi = m_Processes[di->hItem];
	CString text;
	switch (di->column) {
		case 1:
			text.Format(L"%u", pi.Id);
			break;

		case 2:
			text.Format(L"%u", pi.Threads);
			break;

		case 3:
			text = L"NT_AUTHORITY\\LocalSystem";	// dummy value
			break;
		default:
			text.Format(L"Some data at item 0x%p\n", di->hItem);
			break;
	}
	::StringCchCopy(di->text, di->cchText, text);
	return 0;
}

LRESULT CProcessTreeListView::OnTreeListGetDispInfo(int, LPNMHDR, BOOL&) {
	return LRESULT();
}

LRESULT CProcessTreeListView::OnHeaderItemClick(int, LPNMHDR hdr, BOOL&) {
	// 
	// update sorting column
	//
	auto hi = (NMHEADER*)hdr;
	int sortColumn = (int)hi->iButton;
	int col = hi->iItem;

	bool isTree = m_TreeList.IsTreeMode();

	if (col > 0) {
		//
		// switch to list mode (if not already there)
		//
		m_TreeList.SetTreeMode(false);

		if (m_SortColumn == -1) {
			m_SortColumn = sortColumn;
			m_SortAscending = true;
		}
		else {
			if (m_SortColumn == sortColumn)
				m_SortAscending = !m_SortAscending;
			else {
				m_SortColumn = sortColumn;
				m_SortAscending = true;
			}
		}
		if (isTree) {
			m_TreeList.DeleteAllItems();
			Refresh();
		}
		else {
			DoSort(true);
		}
		m_TreeList.SetSortColumn(col, m_SortAscending);
	}
	else {
		if (m_TreeList.IsTreeMode()) {
			// switch to list mode, sort by process name
			m_TreeList.SetTreeMode(false);
			m_SortColumn = 0;
			m_SortAscending = true;
			m_TreeList.SetSortColumn(col, m_SortAscending);
			BuildProcessList(true, true);
		}
		else {
			if (m_SortColumn == 0) {
				if (m_SortAscending) {
					m_SortAscending = false;
					m_TreeList.SetSortColumn(col, m_SortAscending);
					DoSort(true);
				}
				else {
					m_TreeList.ClearSortColumn();
					m_TreeList.SetTreeMode(true);
					BuildProcessTree();
				}
			}
			else {
				m_SortColumn = sortColumn;
				m_SortAscending = true;
				m_TreeList.SetSortColumn(col, m_SortAscending);
				DoSort(true);
			}
		}
	}
	return 0;
}
