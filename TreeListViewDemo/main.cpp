#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlmisc.h>
#include <atlstr.h>
#include <commctrl.h>

#include "../TreeListView/include/TreeListView.h"

#pragma comment(lib, "comctl32.lib")

CAppModule _Module;

// ---- Control and window IDs -------------------------------------------------

enum {
	IDC_TLV = 101,
	IDC_STATUS = 102,

	ID_FILE_POPULATE = 201,
	ID_FILE_CLEAR = 202,
	ID_ITEM_EXPAND = 203,
	ID_ITEM_COLLAPSE = 204,
	ID_ITEM_DELETE = 205,
	ID_VIEW_ALTROWS = 206,
	ID_VIEW_MULTISELECT = 207,
	ID_VIEW_TREELINES = 208,
	ID_VIEW_EDITLABELS = 209,
	ID_VIEW_CHECKBOXES = 210,
	ID_ITEM_EDITLABEL = 211,
	ID_VIEW_SORTCOLS  = 213,
	ID_FILE_EXIT      = 214,
	ID_VIEW_ICONS     = 215,
};

// ---- Icon indices for image list -----------------------------------------------
enum IconIndex {
	ICON_PROCESS = 0,
	ICON_FOLDER = 1,
	ICON_THREAD = 2,
	ICON_MODULE = 3,
	ICON_GENERIC = 4,
};

// ---- Globals ----------------------------------------------------------------

static CTreeListViewCtrl g_tlv;
static HWND              g_status = nullptr;
static HIMAGELIST        g_himl = nullptr;

// ---- Sort helpers -----------------------------------------------------------

struct SortCtx { CTreeListViewCtrl* tlv; int col; int dir; };

static int CALLBACK CompareItems(HTLITEM a, HTLITEM b, LPARAM lp) {
	auto* ctx = reinterpret_cast<SortCtx*>(lp);
	ATL::CString sa = ctx->tlv->GetItemText(a, ctx->col);
	ATL::CString sb = ctx->tlv->GetItemText(b, ctx->col);
	return ctx->dir * _wcsicmp(sa, sb);
}

// ---- Icon list initialization -----------------------------------------------

static void InitializeIcons() {
	if (g_himl) {
		return;  // Already initialized
	}

	// Create image list with 5 icons, 16x16 pixels
	g_himl = ImageList_Create(16, 16, ILC_COLOR32, 5, 0);
	if (!g_himl) {
		return;
	}

	// Create and add simple colored bitmaps as icons
	// Use AddMasked to convert colored bitmaps to icons
	HBITMAP hbm;
	HDC hdc = GetDC(nullptr);
	HDC hdcMem = CreateCompatibleDC(hdc);

	// Create 5 solid-colored bitmaps (16x16)
	COLORREF colors[] = { RGB(255, 0, 0), RGB(0, 200, 0), RGB(0, 0, 255),
	                       RGB(255, 255, 0), RGB(0, 255, 255) };

	for (int i = 0; i < 5; ++i) {
		hbm = CreateCompatibleBitmap(hdc, 16, 16);
		HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

		RECT rc = { 0, 0, 16, 16 };
		HBRUSH hbr = CreateSolidBrush(colors[i]);
		FillRect(hdcMem, &rc, hbr);
		DeleteObject(hbr);

		SelectObject(hdcMem, hbmOld);
		ImageList_AddMasked(g_himl, hbm, RGB(255, 0, 255));  // Magenta as transparent
		DeleteObject(hbm);
	}

	DeleteDC(hdcMem);
	ReleaseDC(nullptr, hdc);

	// Attach image list to the control
	g_tlv.SetImageList(g_himl, LVSIL_SMALL);
}

// ---- Demo data population ---------------------------------------------------

static void PopulateTree() {
	// Clear old content including columns.
	g_tlv.DeleteAllItems();
	while (g_tlv.GetColumnCount() > 0)
		g_tlv.DeleteColumn(0);

	g_tlv.InsertColumn(0, L"Name", 200);
	g_tlv.InsertColumn(1, L"Type", 120);
	g_tlv.InsertColumn(2, L"Value", 160);
	g_tlv.InsertColumn(3, L"Address", 160);

	// Root: Process
	HTLITEM proc = g_tlv.InsertItem(TLV_ROOT, TLV_LAST, L"Process");
	g_tlv.SetItemImage(proc, ICON_PROCESS);
	g_tlv.SetItemText(proc, 1, L"EPROCESS*");
	g_tlv.SetItemText(proc, 2, L"PID: 1234");
	g_tlv.SetItemText(proc, 3, L"0xffff8001`23456780");

	// Process > Threads
	HTLITEM threads = g_tlv.InsertItem(proc, TLV_LAST, L"Threads");
	g_tlv.SetItemImage(threads, ICON_FOLDER);
	g_tlv.SetItemText(threads, 1, L"LIST_ENTRY");
	g_tlv.SetItemText(threads, 2, L"3 items");
	g_tlv.SetItemText(threads, 3, L"0xffff8001`11000000");

	struct { LPCWSTR state; LPCWSTR addr; LPCWSTR stkAddr; LPCWSTR stkSize; } threadTable[] = {
		{ L"Running", L"0xffff8001`dead0000", L"0xfffff780`00010000", L"64 KB" },
		{ L"Waiting", L"0xffff8001`dead1000", L"0xfffff780`00020000", L"32 KB" },
		{ L"Ready",   L"0xffff8001`dead2000", L"0xfffff780`00030000", L"64 KB" },
	};
	for (int i = 0; i < 3; ++i) {
		wchar_t name[32];
		swprintf_s(name, L"Thread[%d]", i);
		HTLITEM t = g_tlv.InsertItem(threads, TLV_LAST, name);
		g_tlv.SetItemImage(t, ICON_THREAD);
		g_tlv.SetItemText(t, 1, L"ETHREAD*");
		g_tlv.SetItemText(t, 2, threadTable[i].state);
		g_tlv.SetItemText(t, 3, threadTable[i].addr);

		HTLITEM stk = g_tlv.InsertItem(t, TLV_LAST, L"KernelStack");
		g_tlv.SetItemImage(stk, ICON_GENERIC);
		g_tlv.SetItemText(stk, 1, L"VOID*");
		g_tlv.SetItemText(stk, 2, threadTable[i].stkSize);
		g_tlv.SetItemText(stk, 3, threadTable[i].stkAddr);
	}

	// Process > Modules
	HTLITEM mods = g_tlv.InsertItem(proc, TLV_LAST, L"Modules");
	g_tlv.SetItemImage(mods, ICON_FOLDER);
	g_tlv.SetItemText(mods, 1, L"LDR_DATA_TABLE_ENTRY");
	g_tlv.SetItemText(mods, 2, L"4 items");
	g_tlv.SetItemText(mods, 3, L"0xffff8001`22000000");

	struct { LPCWSTR name, size, base; } modTable[] = {
		{ L"app.exe",      L"64 KB",  L"0x00400000`00000000" },
		{ L"ntdll.dll",    L"512 KB", L"0x7fff0000`00000000" },
		{ L"kernel32.dll", L"256 KB", L"0x7ffe8000`00000000" },
		{ L"user32.dll",   L"384 KB", L"0x7ffe0000`00000000" },
	};
	for (auto& m : modTable) {
		HTLITEM mi = g_tlv.InsertItem(mods, TLV_LAST, m.name);
		g_tlv.SetItemImage(mi, ICON_MODULE);
		g_tlv.SetItemText(mi, 1, L"LDR_MODULE*");
		g_tlv.SetItemText(mi, 2, m.size);
		g_tlv.SetItemText(mi, 3, m.base);
	}

	// Process > Handles
	HTLITEM handles = g_tlv.InsertItem(proc, TLV_LAST, L"HandleTable");
	g_tlv.SetItemImage(handles, ICON_FOLDER);
	g_tlv.SetItemText(handles, 1, L"HANDLE_TABLE*");
	g_tlv.SetItemText(handles, 2, L"128 entries");
	g_tlv.SetItemText(handles, 3, L"0xffff8001`aabbcc00");

	// Expand down to threads level by default.
	g_tlv.Expand(proc);
	g_tlv.Expand(threads);
}

// ---- Status bar update ------------------------------------------------------

static void UpdateStatus() {
	HTLITEM sel = g_tlv.GetSelectedItem();
	if (!sel) {
		SetWindowText(g_status, L"Ready  —  F5: Populate  |  Del: Delete item  |  View > Alternate Row Colors");
		return;
	}
	ATL::CString s;
	s.Format(L"  %s   [%s]   %s   %s",
		(LPCWSTR)g_tlv.GetItemText(sel, 0),
		(LPCWSTR)g_tlv.GetItemText(sel, 1),
		(LPCWSTR)g_tlv.GetItemText(sel, 2),
		(LPCWSTR)g_tlv.GetItemText(sel, 3));
	SetWindowText(g_status, s);
}

// ---- Main window procedure --------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

		case WM_CREATE:
		{
			RECT rc; GetClientRect(hwnd, &rc);
			const int sbH = 22;
			CRect rc2(0, 0, rc.right, rc.bottom - sbH);
			g_tlv.Create(hwnd, rc2,
				nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0,
				(UINT)IDC_TLV);

			g_status = CreateWindowExW(0, STATUSCLASSNAME, nullptr,
				WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
				0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_STATUS, nullptr, nullptr);

			InitializeIcons();
			UpdateStatus();
			return 0;
		}

		case WM_SIZE:
		{
			const int sbH = 22;
			const int w = LOWORD(lParam), h = HIWORD(lParam);
			if (g_tlv.IsWindow())
				g_tlv.MoveWindow(0, 0, w, h - sbH);
			if (g_status)
				SendMessage(g_status, WM_SIZE, 0, 0);
			return 0;
		}

		case WM_NOTIFY:
		{
			auto* nm = reinterpret_cast<NMHDR*>(lParam);
			if (nm->idFrom != IDC_TLV) return 0;

			switch (nm->code) {

				case TLVN_SELCHANGED:
					UpdateStatus();
					break;

				case NM_RCLICK:
				{
					auto* pnia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
					HTLITEM h = (pnia->iItem >= 0) ? g_tlv.HitTest(pnia->ptAction) : nullptr;
					ATL::CString s;
					s.Format(L"  Right-clicked: %s", h ? (LPCWSTR)g_tlv.GetItemText(h, 0) : L"(none)");
					SetWindowText(g_status, s);
					break;
				}

				case NM_DBLCLK:
				{
					auto* pnia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
					HTLITEM h = (pnia->iItem >= 0) ? g_tlv.HitTest(pnia->ptAction) : nullptr;
					ATL::CString s;
					s.Format(L"  Double-clicked: %s", h ? (LPCWSTR)g_tlv.GetItemText(h, 0) : L"(none)");
					SetWindowText(g_status, s);
					break;
				}

				case TLVN_ITEMCHECK:
				{
					auto* p = reinterpret_cast<NMTLVITEMCHECK*>(lParam);
					ATL::CString s;
					s.Format(L"  %s: %s", (LPCWSTR)g_tlv.GetItemText(p->hItem, 0),
						p->bChecked ? L"checked" : L"unchecked");
					SetWindowText(g_status, s);
					break;
				}

				case TLVN_SORT:
				{
					auto* p = reinterpret_cast<NMTLVSORT*>(lParam);
					SortCtx ctx = { &g_tlv, p->iSubItem, p->iDirection };
					g_tlv.SortChildren(TLV_ROOT, CompareItems, reinterpret_cast<LPARAM>(&ctx), true);
					ATL::CString s;
					s.Format(L"  Sorted by column %d (%s)", p->iSubItem,
						p->iDirection > 0 ? L"ascending" : L"descending");
					SetWindowText(g_status, s);
					break;
				}

				case TLVN_BEGINLABELEDIT:
					return 0;  // 0 = allow edit

				case TLVN_ENDLABELEDIT:
					return 1;  // non-zero = accept text

				case LVN_COLUMNCLICK:
				{
					auto* lv = reinterpret_cast<NMLISTVIEW*>(lParam);
					ATL::CString s;
					s.Format(L"  Column %d clicked", lv->iSubItem);
					SetWindowText(g_status, s);
					break;
				}

			}
			return 0;
		}

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case ID_FILE_EXIT:
					PostMessage(hwnd, WM_CLOSE, 0, 0);
					return 0;

				case ID_FILE_POPULATE:
					PopulateTree();
					UpdateStatus();
					break;

				case ID_FILE_CLEAR:
					g_tlv.DeleteAllItems();
					UpdateStatus();
					break;

				case ID_ITEM_EXPAND:
				{
					HTLITEM sel = g_tlv.GetSelectedItem();
					if (sel) g_tlv.Expand(sel, true);
					break;
				}
				case ID_ITEM_COLLAPSE:
				{
					HTLITEM sel = g_tlv.GetSelectedItem();
					if (sel) g_tlv.Expand(sel, false);
					break;
				}
				case ID_ITEM_DELETE:
				{
					HTLITEM sel = g_tlv.GetSelectedItem();
					if (sel) {
						g_tlv.DeleteItem(sel);
						UpdateStatus();
					}
					break;
				}
				case ID_VIEW_ALTROWS:
				{
					bool on = g_tlv.GetAlternateRowColor() == CLR_NONE;
					g_tlv.SetAlternateRowColor(on ? RGB(235, 243, 255) : CLR_NONE);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_ALTROWS, on ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_VIEW_MULTISELECT:
				{
					bool ms = g_tlv.IsMultiSelect();
					g_tlv.SetMultiSelect(!ms);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_MULTISELECT,
						ms ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_VIEW_TREELINES:
				{
					bool tl = !g_tlv.GetTreeLines();
					g_tlv.SetTreeLines(tl);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_TREELINES,
						tl ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_VIEW_EDITLABELS:
				{
					bool el = !g_tlv.GetEditLabels();
					g_tlv.SetEditLabels(el);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_EDITLABELS,
						el ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_VIEW_CHECKBOXES:
				{
					bool cb = !g_tlv.GetCheckBoxes();
					g_tlv.SetCheckBoxes(cb);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_CHECKBOXES,
						cb ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_ITEM_EDITLABEL:
				{
					HTLITEM sel = g_tlv.GetSelectedItem();
					if (sel) g_tlv.EditLabel(sel);
					break;
				}

				case ID_VIEW_SORTCOLS:
				{
					bool on = !g_tlv.GetColumnSortable(0);
					for (int i = 0; i < 4; ++i)
						g_tlv.SetColumnSortable(i, on);
					if (!on) g_tlv.SetSortColumn(-1, 0);
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_SORTCOLS,
						on ? MF_CHECKED : MF_UNCHECKED);
					break;
				}

				case ID_VIEW_ICONS:
				{
					bool on = g_tlv.GetImageList(LVSIL_SMALL) == nullptr;
					if (on) {
						g_tlv.SetImageList(g_himl, LVSIL_SMALL);
					} else {
						g_tlv.SetImageList(nullptr, LVSIL_SMALL);
					}
					CheckMenuItem(GetMenu(hwnd), ID_VIEW_ICONS,
						on ? MF_CHECKED : MF_UNCHECKED);
					break;
				}
			}
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES };
	InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"TlvDemoWnd";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	HMENU hFile = CreatePopupMenu();
	AppendMenuW(hFile, MF_STRING, ID_FILE_POPULATE, L"&Populate\tF5");
	AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFile, MF_STRING, ID_FILE_CLEAR, L"Clear &All");
	AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hItem = CreatePopupMenu();
	AppendMenuW(hItem, MF_STRING, ID_ITEM_EXPAND, L"&Expand selected");
	AppendMenuW(hItem, MF_STRING, ID_ITEM_COLLAPSE, L"&Collapse selected");
	AppendMenuW(hItem, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hItem, MF_STRING, ID_ITEM_DELETE, L"&Delete selected\tDel");
	AppendMenuW(hItem, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hItem, MF_STRING, ID_ITEM_EDITLABEL, L"Edit &Label\tF2");

	HMENU hView = CreatePopupMenu();
	AppendMenuW(hView, MF_STRING, ID_VIEW_ALTROWS, L"&Alternate Row Colors");
	AppendMenuW(hView, MF_STRING, ID_VIEW_TREELINES, L"Tree &Lines");
	AppendMenuW(hView, MF_STRING, ID_VIEW_EDITLABELS, L"&Edit Labels");
	AppendMenuW(hView, MF_STRING, ID_VIEW_CHECKBOXES, L"&Check Boxes");
	AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hView, MF_STRING, ID_VIEW_ICONS, L"Show &Icons");
	AppendMenuW(hView, MF_STRING, ID_VIEW_MULTISELECT, L"&Multi-Select");
	AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hView, MF_STRING, ID_VIEW_SORTCOLS,   L"Sortable &Columns");

	HMENU hMenu = CreateMenu();
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hItem, L"&Item");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

	HWND hwnd = CreateWindowExW(0, L"TlvDemoWnd", L"TreeListView Demo",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 860, 580,
		nullptr, hMenu, hInstance, nullptr);

	ACCEL accels[] = {
		{ FVIRTKEY, VK_F5,     ID_FILE_POPULATE },
		{ FVIRTKEY, VK_DELETE, ID_ITEM_DELETE   },
		{ FVIRTKEY, VK_F2,     ID_ITEM_EDITLABEL },
	};
	HACCEL hAccel = CreateAcceleratorTable(accels, (int)std::size(accels));

	// Sync initial checkmarks with control defaults.
	CheckMenuItem(hMenu, ID_VIEW_TREELINES,
		g_tlv.GetTreeLines() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(hMenu, ID_VIEW_ICONS,
		g_tlv.GetImageList(LVSIL_SMALL) != nullptr ? MF_CHECKED : MF_UNCHECKED);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return (int)msg.wParam;
}
