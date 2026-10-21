#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "PropertyGrid.h"

#include <iterator>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_GRID = 101,
	IDC_STATUS = 102,

	ID_FILE_POPULATE = 201,
	ID_FILE_MANY,
	ID_FILE_CLEAR,
	ID_FILE_EXIT,
	ID_ITEM_RESET,
	ID_ITEM_DELETE,
	ID_ITEM_READONLY,
	ID_ITEM_HIDE,
	ID_ITEM_SHOWALL,
	ID_VIEW_CATEGORIZED,
	ID_VIEW_ALPHABETICAL,
	ID_VIEW_DESCRIPTION,
	ID_VIEW_DARK,
	ID_VIEW_EXPANDALL,
	ID_VIEW_COLLAPSEALL,
	ID_VIEW_READONLY,
	ID_ITEM_EDIT,
	ID_VIEW_GRIDTOOLBAR,
};

// ---- Globals ----------------------------------------------------------------

static CPropertyGridCtrl g_grid;
static HWND              g_status = nullptr;
static int               g_manyCount = 0;
static LONGLONG          g_counter = 5;     // the "Counter" callback item's value, kept here

// lParam of the callback items
enum { LIVE_TICKS = 1, LIVE_NOW, LIVE_COUNTER };

static void Status(LPCWSTR text) {
	::SendMessage(g_status, SB_SETTEXT, 0, (LPARAM)text);
}

// ---- Sample data ------------------------------------------------------------

static HPRGITEM Add(HPRGITEM parent, UINT kind, LPCWSTR name, LPCWSTR desc, PRGVALUE value, UINT flags = 0) {
	PRGITEMDESC d;
	d.kind = kind;
	d.pszName = name;
	d.pszDescription = desc;
	d.value = value;
	d.flags = flags;
	return g_grid.InsertItem(parent, &d);
}

static void Populate() {
	g_grid.BeginUpdate();
	g_grid.DeleteAllItems();

	Add(PRG_ROOT, PRGK_INTEGER, L"Id", L"A property outside any category: shown above the categories.", PrgMakeInt(1234), PRGF_READONLY);

	auto app = g_grid.AddCategory(L"Appearance", L"How the object looks.");
	PRGITEMDESC d;
	d.kind = PRGK_COLOR;
	d.pszName = L"Back Color";
	d.pszDescription = L"The background color. Bold when it differs from the default (white).";
	d.value = PrgMakeColor(RGB(0x33, 0x66, 0x99));
	d.defValue = PrgMakeColor(RGB(255, 255, 255));
	g_grid.InsertItem(app, &d);

	LOGFONTW lf{};
	wcscpy_s(lf.lfFaceName, L"Segoe UI");
	lf.lfHeight = -12;
	lf.lfWeight = FW_NORMAL;
	Add(app, PRGK_FONT, L"Font", L"The font of the text.", PrgMakeFont(lf));

	d = {};
	d.kind = PRGK_REAL;
	d.pszName = L"Opacity";
	d.pszDescription = L"0 is transparent, 1 is opaque. Shown with two digits after the point.";
	d.value = PrgMakeReal(0.85);
	d.defValue = PrgMakeReal(1);
	d.precision = 2;
	g_grid.InsertItem(app, &d);

	d = {};
	d.kind = PRGK_BOOL;
	d.pszName = L"Visible";
	d.pszDescription = L"Whether the object is shown.";
	d.value = PrgMakeBool(true);
	d.defValue = PrgMakeBool(true);
	g_grid.InsertItem(app, &d);

	static const PRGENUMITEM borders[] = { { L"None", 0 }, { L"Single", 1 }, { L"Double", 2 }, { L"Sunken", 3 } };
	static const PRGENUMITEM sizes[] = { { L"Small", 16 }, { L"Medium", 24 }, { L"Large", 32 } };
	d = {};
	d.kind = PRGK_ENUM;
	d.pszName = L"Border Style";
	d.pszDescription = L"The border drawn around the object.";
	d.value = PrgMakeInt(1);
	d.enumItems = borders;
	d.enumCount = (int)std::size(borders);
	g_grid.InsertItem(app, &d);

	d = {};
	d.kind = PRGK_ENUM;
	d.pszName = L"Icon Size";
	d.pszDescription = L"An editable enum (PRGF_EDITABLE): pick a size or type any number.";
	d.value = PrgMakeInt(24);
	d.flags = PRGF_EDITABLE;
	d.enumItems = sizes;
	d.enumCount = (int)std::size(sizes);
	g_grid.InsertItem(app, &d);

	auto behavior = g_grid.AddCategory(L"Behavior");
	Add(behavior, PRGK_STRING, L"Name", L"The object's name. Can't be empty (checked in PRGN_VALUECHANGING).", PrgMakeString(L"Sample Object"));
	Add(behavior, PRGK_STRING, L"Kind", L"Read-only: drawn gray.", PrgMakeString(L"Widget"), PRGF_READONLY);

	d = {};
	d.kind = PRGK_INTEGER;
	d.pszName = L"Max Count";
	d.pszDescription = L"An integer from 1 to 1000; the arrows (or Up and Down) step it.";
	d.value = PrgMakeInt(250);
	d.defValue = PrgMakeInt(100);
	d.minVal = 1;
	d.maxVal = 1000;
	g_grid.InsertItem(behavior, &d);

	Add(behavior, PRGK_INTEGER, L"Address", L"An integer shown in hex.", PrgMakeInt(0x7FF6'1234'0000), PRGF_HEX);

	static const PRGENUMITEM access[] = {
		{ L"None", 0 }, { L"Read", 1 }, { L"Write", 2 }, { L"Execute", 4 }, { L"Delete", 0x10000 },
	};
	d = {};
	d.kind = PRGK_FLAGS;
	d.pszName = L"Access";
	d.pszDescription = L"A combination of flags: expand it for a check box per flag, or open the drop-down. Bits without a name show as hex.";
	d.value = PrgMakeInt(1 | 4 | 0x100);
	d.enumItems = access;
	d.enumCount = (int)std::size(access);
	g_grid.InsertItem(behavior, &d);

	Add(behavior, PRGK_STRING, L"Password", L"Masked text.", PrgMakeString(L"secret"), PRGF_PASSWORD);
	Add(behavior, PRGK_STRING, L"Secret", L"Hidden: not shown at all (Item > Show All brings it back).", PrgMakeString(L"x"), PRGF_HIDDEN);

	auto files = g_grid.AddCategory(L"Files");
	d = {};
	d.kind = PRGK_PATH;
	d.pszName = L"Log File";
	d.pszDescription = L"A file path.";
	d.value = PrgMakeString(L"C:\\Logs\\app.log");
	d.pszFilter = L"Log files\0*.log\0All files\0*.*\0";
	g_grid.InsertItem(files, &d);
	Add(files, PRGK_PATH, L"Output Folder", L"A folder path.", PrgMakeString(L"C:\\Output"), PRGF_FOLDER);

	auto misc = g_grid.AddCategory(L"Misc", L"Everything else.");
	FILETIME now;
	::GetSystemTimeAsFileTime(&now);
	Add(misc, PRGK_DATETIME, L"Created", L"A date and time (stored in UTC, shown in local time).", PrgMakeDateTime(now));
	Add(misc, PRGK_DATETIME, L"Expires", L"A date only.", PrgMakeDateTime(now), PRGF_DATEONLY);

	auto pos = Add(misc, PRGK_CUSTOM, L"Position", L"A custom property with sub-properties.", PrgMakeString(L"10, 20"));
	Add(pos, PRGK_INTEGER, L"X", L"The horizontal coordinate.", PrgMakeInt(10));
	auto y = Add(pos, PRGK_INTEGER, L"Y", L"The vertical coordinate.", PrgMakeInt(20));
	Add(y, PRGK_STRING, L"Deeper", L"Sub-properties nest to any depth.", PrgMakeString(L"level 2"));
	Add(misc, PRGK_CUSTOM, L"Shape", L"PRGF_BUTTON: the demo shows a menu (PRGN_BUTTONCLICK); the text can be typed too.",
		PrgMakeString(L"Circle"), PRGF_BUTTON);
	auto size = Add(misc, PRGK_CUSTOM, L"Size", L"Starts collapsed (PRGF_COLLAPSED).", PrgMakeString(L"300 x 200"), PRGF_COLLAPSED);
	Add(size, PRGK_INTEGER, L"Width", nullptr, PrgMakeInt(300));
	Add(size, PRGK_INTEGER, L"Height", nullptr, PrgMakeInt(200));
	Add(misc, PRGK_STRING, L"A long value", L"Text too long for the column ends with an ellipsis.",
		PrgMakeString(L"The quick brown fox jumps over the lazy dog, again and again and again and again."));
	Add(misc, PRGK_STRING, L"A property with a name much too long for its column", L"Hover a cut name or value: the whole text shows.",
		PrgMakeString(L"short"));

	auto live = g_grid.AddCategory(L"Live", L"Values the demo gives in PRGN_GETDISPINFO (PRGF_CALLBACK), refreshed every second.");
	d = {};
	d.kind = PRGK_INTEGER;
	d.pszName = L"Tick Count";
	d.pszDescription = L"GetTickCount64, asked for each time it's drawn.";
	d.flags = PRGF_CALLBACK | PRGF_READONLY;
	d.lParam = LIVE_TICKS;
	g_grid.InsertItem(live, &d);
	d = {};
	d.kind = PRGK_DATETIME;
	d.pszName = L"Now";
	d.pszDescription = L"The current time.";
	d.flags = PRGF_CALLBACK | PRGF_READONLY;
	d.lParam = LIVE_NOW;
	g_grid.InsertItem(live, &d);
	d = {};
	d.kind = PRGK_INTEGER;
	d.pszName = L"Counter";
	d.pszDescription = L"Goes up every second. Kept by the demo: an edit is stored in PRGN_VALUECHANGED.";
	d.flags = PRGF_CALLBACK;
	d.lParam = LIVE_COUNTER;
	d.minVal = 0;
	d.maxVal = 1000000;
	g_grid.InsertItem(live, &d);

	g_grid.EndUpdate();
	g_manyCount = 0;
}

static void AddMany() {
	auto start = ::GetTickCount64();
	g_grid.BeginUpdate();
	WCHAR name[64];
	swprintf_s(name, L"Generated %d", ++g_manyCount);
	auto cat = g_grid.AddCategory(name, L"500 generated items.");
	for (int i = 0; i < 500; i++) {
		swprintf_s(name, L"Item %d", i + 1);
		Add(cat, PRGK_INTEGER, name, L"A generated item.", PrgMakeInt(i * 7));
	}
	g_grid.EndUpdate();
	CString text;
	text.Format(L"Added 500 items in %llu ms; %d items in all", ::GetTickCount64() - start, g_grid.GetItemCount());
	Status(text);
}

static void ApplyDark(bool dark) {
	if (!dark) {
		g_grid.SetColors(nullptr);
		return;
	}
	PRGCOLORS c;
	c.crBack = RGB(0x25, 0x25, 0x26);
	c.crText = RGB(0xF1, 0xF1, 0xF1);
	c.crCategoryBack = RGB(0x33, 0x33, 0x37);
	c.crCategoryText = RGB(0xF1, 0xF1, 0xF1);
	c.crGridLine = RGB(0x3F, 0x3F, 0x46);
	c.crSelBack = RGB(0x33, 0x99, 0xFF);
	c.crSelText = RGB(0xFF, 0xFF, 0xFF);
	c.crSelBackInactive = RGB(0x3F, 0x3F, 0x46);
	c.crSelTextInactive = RGB(0xF1, 0xF1, 0xF1);
	c.crDisabledText = RGB(0x80, 0x80, 0x80);
	c.crDescBack = RGB(0x2D, 0x2D, 0x30);
	c.crDescText = RGB(0xF1, 0xF1, 0xF1);
	g_grid.SetColors(&c);
}

static void UpdateViewChecks(HMENU menu) {
	bool categorized = g_grid.GetView() == PRGV_CATEGORIZED;
	CheckMenuRadioItem(menu, ID_VIEW_CATEGORIZED, ID_VIEW_ALPHABETICAL,
		categorized ? ID_VIEW_CATEGORIZED : ID_VIEW_ALPHABETICAL, MF_BYCOMMAND);
	CheckMenuItem(menu, ID_VIEW_DESCRIPTION, g_grid.IsDescriptionVisible() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, ID_VIEW_READONLY, g_grid.IsReadOnly() ? MF_CHECKED : MF_UNCHECKED);
	CheckMenuItem(menu, ID_VIEW_GRIDTOOLBAR, g_grid.IsToolbarVisible() ? MF_CHECKED : MF_UNCHECKED);
}

static void ShowAll(HPRGITEM parent) {
	for (auto h = g_grid.GetChildItem(parent); h; h = g_grid.GetNextSiblingItem(h)) {
		g_grid.SetItemFlags(h, 0, PRGF_HIDDEN);
		ShowAll(h);
	}
}

// ---- Window procedure -------------------------------------------------------

static void OnCommand(HWND hwnd, UINT id) {
	auto sel = g_grid.GetSelectedItem();
	switch (id) {
		case ID_FILE_POPULATE: Populate(); break;
		case ID_FILE_MANY:     AddMany(); break;
		case ID_FILE_CLEAR:    g_grid.DeleteAllItems(); break;
		case ID_FILE_EXIT:     DestroyWindow(hwnd); break;

		case ID_ITEM_EDIT:
			if (sel)
				g_grid.BeginEdit(sel);
			break;
		case ID_VIEW_READONLY:
			g_grid.SetReadOnly(!g_grid.IsReadOnly());
			break;
		case ID_VIEW_GRIDTOOLBAR:
			g_grid.ShowToolbar(!g_grid.IsToolbarVisible());
			break;
		case ID_ITEM_RESET:
			if (sel && !g_grid.ResetValue(sel))
				Status(L"The item has no default value");
			break;
		case ID_ITEM_DELETE:
			if (sel)
				g_grid.DeleteItem(sel);
			break;
		case ID_ITEM_READONLY:
			if (sel)
				g_grid.SetItemFlags(sel, g_grid.GetItemFlags(sel) ^ PRGF_READONLY, PRGF_READONLY);
			break;
		case ID_ITEM_HIDE:
			if (sel)
				g_grid.SetItemFlags(sel, PRGF_HIDDEN, PRGF_HIDDEN);
			break;
		case ID_ITEM_SHOWALL:
			g_grid.BeginUpdate();
			ShowAll(PRG_ROOT);
			g_grid.EndUpdate();
			break;

		case ID_VIEW_CATEGORIZED:  g_grid.SetView(PRGV_CATEGORIZED); break;
		case ID_VIEW_ALPHABETICAL: g_grid.SetView(PRGV_ALPHABETICAL); break;
		case ID_VIEW_DESCRIPTION:  g_grid.ShowDescription(!g_grid.IsDescriptionVisible()); break;
		case ID_VIEW_EXPANDALL:    g_grid.ExpandAll(true); break;
		case ID_VIEW_COLLAPSEALL:  g_grid.ExpandAll(false); break;
		case ID_VIEW_DARK: {
			auto menu = GetMenu(hwnd);
			bool dark = (GetMenuState(menu, ID_VIEW_DARK, MF_BYCOMMAND) & MF_CHECKED) == 0;
			CheckMenuItem(menu, ID_VIEW_DARK, dark ? MF_CHECKED : MF_UNCHECKED);
			ApplyDark(dark);
			break;
		}
	}
	UpdateViewChecks(GetMenu(hwnd));
}

static LRESULT OnNotify(HWND hwnd, NMHDR* hdr) {
	if (hdr->idFrom != IDC_GRID)
		return 0;
	CString text;
	switch (hdr->code) {
		case PRGN_SELCHANGED: {
			auto nm = (NMPRGSELCHANGE*)hdr;
			if (nm->hItemNew)
				text.Format(L"Selected: %s = %s", (PCWSTR)g_grid.GetItemName(nm->hItemNew), (PCWSTR)g_grid.GetValueText(nm->hItemNew));
			else
				text = L"No selection";
			Status(text);
			break;
		}
		case PRGN_ITEMEXPANDED: {
			auto nm = (NMPRGEXPAND*)hdr;
			text.Format(L"%s %s", nm->expanded ? L"Expanded" : L"Collapsed", (PCWSTR)g_grid.GetItemName(nm->hItem));
			Status(text);
			break;
		}
		case PRGN_VALUECHANGING: {
			auto nm = (NMPRGVALUE*)hdr;
			if (g_grid.GetItemName(nm->hItem) == L"Name" && nm->newValue.type == PRGVT_STRING
				&& wcsspn(nm->newValue.pszVal, L" ") == wcslen(nm->newValue.pszVal)) {
				wcscpy_s(nm->szError, L"The name can't be empty.");
				return TRUE;
			}
			break;
		}
		case PRGN_BUTTONCLICK: {
			auto nm = (NMPRGITEM*)hdr;
			if (g_grid.GetItemName(nm->hItem) != L"Shape")
				return FALSE;   // the grid's own dialog
			static const wchar_t* const shapes[] = { L"Circle", L"Square", L"Triangle" };
			HMENU menu = CreatePopupMenu();
			for (int i = 0; i < (int)std::size(shapes); i++)
				AppendMenuW(menu, MF_STRING, i + 1, shapes[i]);
			int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, nm->pt.x, nm->pt.y, 0, hwnd, nullptr);
			DestroyMenu(menu);
			if (cmd > 0) {
				auto value = PrgMakeString(shapes[cmd - 1]);
				g_grid.SetValue(nm->hItem, &value);
				text.Format(L"Shape picked: %s", shapes[cmd - 1]);
				Status(text);
			}
			return TRUE;
		}
		case PRGN_VIEWCHANGED:
			Status(g_grid.GetView() == PRGV_CATEGORIZED ? L"View: categorized" : L"View: alphabetical");
			UpdateViewChecks(GetMenu(hwnd));
			break;
		case PRGN_FILTERCHANGED:
			text.Format(L"Filter: \"%s\"", (PCWSTR)g_grid.GetFilter());
			Status(text);
			break;
		case PRGN_GETDISPINFO: {
			auto nm = (NMPRGDISPINFO*)hdr;
			switch (nm->lParam) {
				case LIVE_TICKS:
					nm->value = PrgMakeInt((LONGLONG)::GetTickCount64());
					break;
				case LIVE_NOW: {
					FILETIME now;
					::GetSystemTimeAsFileTime(&now);
					nm->value = PrgMakeDateTime(now);
					break;
				}
				case LIVE_COUNTER:
					nm->value = PrgMakeInt(g_counter);
					break;
			}
			break;
		}
		case PRGN_VALUECHANGED: {
			auto nm = (NMPRGVALUE*)hdr;
			if (nm->lParam == LIVE_COUNTER)
				g_counter = nm->newValue.llVal;   // else the next PRGN_GETDISPINFO brings the old one back
			text.Format(L"Changed: %s = %s", (PCWSTR)g_grid.GetItemName(nm->hItem), (PCWSTR)g_grid.GetValueText(nm->hItem));
			Status(text);
			break;
		}
		case PRGN_CONTEXTMENU: {
			auto nm = (NMPRGITEM*)hdr;
			HMENU menu = CreatePopupMenu();
			UINT state = nm->hItem ? MF_STRING : MF_STRING | MF_GRAYED;
			AppendMenuW(menu, state, ID_ITEM_EDIT, L"&Edit");
			AppendMenuW(menu, state | (g_grid.IsModified(nm->hItem) ? 0 : MF_GRAYED), ID_ITEM_RESET, L"&Reset");
			AppendMenuW(menu, state, ID_ITEM_READONLY, L"Toggle Read-&Only");
			AppendMenuW(menu, state, ID_ITEM_HIDE, L"&Hide");
			AppendMenuW(menu, state, ID_ITEM_DELETE, L"&Delete");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, ID_VIEW_EXPANDALL, L"&Expand All");
			AppendMenuW(menu, MF_STRING, ID_VIEW_COLLAPSEALL, L"&Collapse All");
			TrackPopupMenu(menu, TPM_RIGHTBUTTON, nm->pt.x, nm->pt.y, 0, hwnd, nullptr);
			DestroyMenu(menu);
			break;
		}
	}
	return 0;
}

static void LayoutChildren(HWND hwnd) {
	RECT rc;
	GetClientRect(hwnd, &rc);
	::SendMessage(g_status, WM_SIZE, 0, 0);
	RECT rcStatus;
	::GetWindowRect(g_status, &rcStatus);
	g_grid.SetWindowPos(nullptr, 0, 0, rc.right, rc.bottom - (rcStatus.bottom - rcStatus.top), SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			g_status = CreateWindowExW(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
				0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, _Module.GetModuleInstance(), nullptr);
			g_grid.ShowToolbar(true);
			g_grid.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
				WS_EX_CLIENTEDGE, IDC_GRID);
			Populate();
			g_grid.SetFocus();
			SetTimer(hwnd, 1, 1000, nullptr);
			return 0;

		case WM_TIMER:
			g_counter++;
			g_grid.RefreshValues();
			return 0;

		case WM_SIZE:
			LayoutChildren(hwnd);
			return 0;

		case WM_SETFOCUS:
			g_grid.SetFocus();
			return 0;

		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}

		case WM_NOTIFY:
			return OnNotify(hwnd, (NMHDR*)lParam);

		case WM_COMMAND:
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
	InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"PropertyGridDemoWnd";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	HMENU hFile = CreatePopupMenu();
	AppendMenuW(hFile, MF_STRING, ID_FILE_POPULATE, L"&Populate\tF5");
	AppendMenuW(hFile, MF_STRING, ID_FILE_MANY, L"Add &500 Items\tCtrl+M");
	AppendMenuW(hFile, MF_STRING, ID_FILE_CLEAR, L"&Clear");
	AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hItem = CreatePopupMenu();
	AppendMenuW(hItem, MF_STRING, ID_ITEM_EDIT, L"&Edit\tF2");
	AppendMenuW(hItem, MF_STRING, ID_ITEM_RESET, L"&Reset Value\tCtrl+R");
	AppendMenuW(hItem, MF_STRING, ID_ITEM_READONLY, L"Toggle Read-&Only");
	AppendMenuW(hItem, MF_STRING, ID_ITEM_HIDE, L"&Hide");
	AppendMenuW(hItem, MF_STRING, ID_ITEM_SHOWALL, L"&Show All");
	AppendMenuW(hItem, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hItem, MF_STRING, ID_ITEM_DELETE, L"&Delete\tDel");

	HMENU hView = CreatePopupMenu();
	AppendMenuW(hView, MF_STRING, ID_VIEW_CATEGORIZED, L"&Categorized");
	AppendMenuW(hView, MF_STRING, ID_VIEW_ALPHABETICAL, L"&Alphabetical");
	AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hView, MF_STRING, ID_VIEW_GRIDTOOLBAR, L"&Toolbar");
	AppendMenuW(hView, MF_STRING, ID_VIEW_DESCRIPTION, L"&Description Pane");
	AppendMenuW(hView, MF_STRING, ID_VIEW_DARK, L"Dar&k Colors");
	AppendMenuW(hView, MF_STRING, ID_VIEW_READONLY, L"&Read-Only Grid");
	AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hView, MF_STRING, ID_VIEW_EXPANDALL, L"&Expand All");
	AppendMenuW(hView, MF_STRING, ID_VIEW_COLLAPSEALL, L"C&ollapse All");

	HMENU hMenu = CreateMenu();
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hItem, L"&Item");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

	HWND hwnd = CreateWindowExW(0, L"PropertyGridDemoWnd", L"Property Grid Demo", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 520, 640, nullptr, hMenu, hInstance, nullptr);
	UpdateViewChecks(hMenu);

	ACCEL accels[] = {
		{ FVIRTKEY, VK_F5, ID_FILE_POPULATE },
		{ FVIRTKEY | FCONTROL, 'M', ID_FILE_MANY },
		{ FVIRTKEY | FCONTROL, 'R', ID_ITEM_RESET },
		{ FVIRTKEY, VK_DELETE, ID_ITEM_DELETE },
	};
	HACCEL hAccel = CreateAcceleratorTable(accels, (int)std::size(accels));

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		// no accelerators in the grid's editors and search box: Delete and Ctrl+R belong to them then
		if (::IsChild(g_grid, ::GetFocus()) || !TranslateAccelerator(hwnd, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return (int)msg.wParam;
}
