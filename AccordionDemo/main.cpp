#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "Accordion.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_ACCORDION = 101,

	ID_FILE_EXIT = 201,

	ID_VIEW_SINGLE = 301,
	ID_VIEW_ICONS,
	ID_VIEW_COLORS,
	ID_VIEW_DARKMODE,
	ID_VIEW_LOCKDETAILS,
	ID_VIEW_NOANIMATION,
	ID_VIEW_ALWAYSANIMATE,

	ID_SECTION_EXPANDALL = 401,
	ID_SECTION_COLLAPSEALL,
	ID_SECTION_ADD,
	ID_SECTION_REMOVE,
	ID_SECTION_MORE,       // more options: the fit section grows
	ID_SECTION_SCROLLEND,

	ID_MENU_TOGGLE = 501,   // the header context menu
	ID_MENU_ONLY,
	ID_MENU_SORT,           // the Files "..." button's menu
	ID_MENU_CLEAR,

	BTN_ADDFILE = 1,        // the Files header's buttons
	BTN_MORE,
};

// the sections' ids (the saved state finds them by these)
enum : UINT {
	SEC_FILES = 1,
	SEC_DETAILS,
	SEC_OPTIONS,
	SEC_LOG,
	SEC_PINNED,
	SEC_ADDED = 100,
};

// ---- State --------------------------------------------------------------------

static CAccordionCtrl g_acc;
static HFONT g_font;
static HIMAGELIST g_images;
static HWND g_options;          // the fit section's body
static int g_optionCount = 4;
static int g_added;
static bool g_icons = true, g_customColors, g_lockDetails;
static HWND g_files;
static HWND g_main;

static const ACDCOLORS g_colors = { RGB(0xF4, 0xF1, 0xEA), RGB(0x2F, 0x4F, 0x6F), RGB(0xFF, 0xFF, 0xFF),
	RGB(0xC8, 0xD8, 0xE8), RGB(0x1F, 0x3F, 0x5F) };

static int Scale(HWND hwnd, int dips) {
	return ::MulDiv(dips, ::GetDpiForWindow(hwnd), 96);
}

// ---- The options body: check boxes, as many as asked, its height what they need ----------

static void LayoutOptions() {
	int y = Scale(g_options, 6);
	int row = Scale(g_options, 24);
	RECT rc;
	::GetClientRect(g_options, &rc);
	for (HWND child = ::GetWindow(g_options, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
		::SetWindowPos(child, nullptr, Scale(g_options, 10), y, rc.right - Scale(g_options, 20), row, SWP_NOZORDER | SWP_NOACTIVATE);
		y += row;
	}
}

static int OptionsHeight(HWND hwnd) {
	return Scale(hwnd, 12) + g_optionCount * Scale(hwnd, 24);
}

static void AddOption() {
	static const wchar_t* names[] = { L"Show hidden files", L"Show extensions", L"Group by type", L"Compact rows",
		L"Follow the selection", L"Preview pane", L"Sort folders first", L"Show sizes" };
	int n = 0;
	for (HWND child = ::GetWindow(g_options, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
		n++;
	HWND check = ::CreateWindowExW(0, WC_BUTTONW, names[n % std::size(names)], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		0, 0, 0, 0, g_options, nullptr, nullptr, nullptr);
	::SendMessage(check, WM_SETFONT, (WPARAM)g_font, FALSE);
	if (n % 2 == 0)
		::SendMessage(check, BM_SETCHECK, BST_CHECKED, 0);
}

static LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_SIZE:
			LayoutOptions();
			return 0;
		case WM_ERASEBKGND:
		{
			RECT rc;
			::GetClientRect(hwnd, &rc);
			::FillRect((HDC)wParam, &rc, ::GetSysColorBrush(COLOR_WINDOW));
			return 1;
		}
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN:
			::SetTextColor((HDC)wParam, ::GetSysColor(COLOR_WINDOWTEXT));
			::SetBkColor((HDC)wParam, ::GetSysColor(COLOR_WINDOW));
			return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- The sections ---------------------------------------------------------------------

static HWND MakeEdit(HWND parent, LPCWSTR text) {
	HWND edit = ::CreateWindowExW(0, WC_EDITW, text, WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
		0, 0, 0, 0, parent, nullptr, nullptr, nullptr);
	::SendMessage(edit, WM_SETFONT, (WPARAM)g_font, FALSE);
	return edit;
}

// what the accordion said last, in the title
static void ShowEvent(LPCWSTR text) {
	CString title;
	title.Format(L"Accordion Demo - %s", text);
	::SetWindowTextW(g_main, title);
}

static void UpdateFileCount() {
	CString info;
	info.Format(L"%d items", ListView_GetItemCount(g_files));
	g_acc.SetSectionInfoText(g_acc.FindSection(SEC_FILES), info);
}

static HWND MakeFileList(HWND parent) {
	CListViewCtrl list;
	list.Create(parent, CWindow::rcDefault, nullptr, WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER);
	list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	::SetWindowTheme(list, L"Explorer", nullptr);
	list.SetFont(g_font);
	list.InsertColumn(0, L"Name", LVCFMT_LEFT, Scale(parent, 220));
	for (int i = 0; i < 30; i++) {
		CString name;
		name.Format(L"Document %02d.txt", i + 1);
		list.InsertItem(i, name);
	}
	return list;
}

static void AddSections(HWND hwnd) {
	ACDSECTION s{};
	s.id = SEC_FILES;
	s.pszText = L"Files";
	s.pszInfo = L"30 items";
	s.iImage = 0;
	g_files = MakeFileList(g_acc);
	s.hwndBody = g_files;
	s.heightMode = ACDH_FILL;
	s.minHeight = 80;
	s.state = ACDSS_EXPANDED;
	auto files = g_acc.InsertSection(&s);
	ACDBUTTON buttons[] = { { BTN_ADDFILE, -1, L"+" }, { BTN_MORE, -1, L"..." } };
	g_acc.SetSectionButtons(files, buttons, (int)std::size(buttons));

	s = {};
	s.id = SEC_DETAILS;
	s.pszText = L"Details";
	s.iImage = 1;
	s.hwndBody = MakeEdit(g_acc, L"Document 07.txt\r\nModified: yesterday\r\nSize: 12 KB\r\nOwner: you");
	s.heightMode = ACDH_FIXED;
	s.height = 90;
	s.state = ACDSS_EXPANDED;
	g_acc.InsertSection(&s);

	s = {};
	s.id = SEC_OPTIONS;
	s.pszText = L"Options";
	s.pszInfo = L"fits its content";
	s.iImage = 2;
	g_options = ::CreateWindowExW(WS_EX_CONTROLPARENT, L"AccordionDemoOptions", nullptr, WS_CHILD | WS_CLIPCHILDREN,
		0, 0, 0, 0, g_acc, nullptr, nullptr, nullptr);
	for (int i = 0; i < g_optionCount; i++)
		AddOption();
	s.hwndBody = g_options;
	s.heightMode = ACDH_FIT;
	g_acc.InsertSection(&s);

	s = {};
	s.id = SEC_LOG;
	s.pszText = L"Log";
	s.pszInfo = L"fill, closed";
	s.iImage = 3;
	s.hwndBody = MakeEdit(g_acc, L"10:01 Started\r\n10:02 Loaded 30 files\r\n10:05 Saved Document 07.txt");
	s.heightMode = ACDH_FILL;
	s.minHeight = 60;
	g_acc.InsertSection(&s);

	s = {};
	s.id = SEC_PINNED;
	s.pszText = L"Pinned (always open)";
	s.iImage = -1;
	s.hwndBody = MakeEdit(g_acc, L"A section that can't be closed.");
	s.heightMode = ACDH_FIXED;
	s.height = 44;
	s.state = ACDSS_NOCOLLAPSE;
	g_acc.InsertSection(&s);
	(void)hwnd;
}

static void MakeImages(HWND hwnd) {
	int size = Scale(hwnd, 16);
	g_images = ::ImageList_Create(size, size, ILC_COLOR32, 4, 0);
	for (auto id : { IDI_APPLICATION, IDI_INFORMATION, IDI_SHIELD, IDI_WARNING }) {
		HICON icon = nullptr;
		::LoadIconWithScaleDown(nullptr, id, size, size, &icon);
		::ImageList_AddIcon(g_images, icon);
		::DestroyIcon(icon);
	}
}

static void UpdateFont(HWND hwnd) {
	if (g_font)
		::DeleteObject(g_font);
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
}

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_VIEW_SINGLE, (g_acc.GetStyle() & ACDS_SINGLEEXPAND) != 0);
	check(ID_VIEW_ICONS, g_icons);
	check(ID_VIEW_COLORS, g_customColors);
	check(ID_VIEW_LOCKDETAILS, g_lockDetails);
	check(ID_VIEW_NOANIMATION, (g_acc.GetStyle() & ACDS_NOANIMATION) != 0);
	check(ID_VIEW_ALWAYSANIMATE, (g_acc.GetStyle() & ACDS_ALWAYSANIMATE) != 0);
#ifdef DEMO_WTLHELPER
	check(ID_VIEW_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_FILE_EXIT:
			::DestroyWindow(hwnd);
			return;
		case ID_VIEW_SINGLE:
			if (g_acc.GetStyle() & ACDS_SINGLEEXPAND)
				g_acc.ModifyStyle(ACDS_SINGLEEXPAND, 0);
			else
				g_acc.ModifyStyle(0, ACDS_SINGLEEXPAND);
			break;
		case ID_VIEW_LOCKDETAILS:
			g_lockDetails = !g_lockDetails;
			break;
		case ID_VIEW_NOANIMATION:
			if (g_acc.GetStyle() & ACDS_NOANIMATION)
				g_acc.ModifyStyle(ACDS_NOANIMATION, 0);
			else
				g_acc.ModifyStyle(0, ACDS_NOANIMATION);
			break;
		case ID_VIEW_ALWAYSANIMATE:
			if (g_acc.GetStyle() & ACDS_ALWAYSANIMATE)
				g_acc.ModifyStyle(ACDS_ALWAYSANIMATE, 0);
			else
				g_acc.ModifyStyle(0, ACDS_ALWAYSANIMATE);
			break;
		case ID_VIEW_ICONS:
			g_icons = !g_icons;
			g_acc.SetImageList(g_icons ? g_images : nullptr);
			break;
		case ID_VIEW_COLORS:
			g_customColors = !g_customColors;
			g_acc.SetColors(g_customColors ? &g_colors : nullptr);
			break;
#ifdef DEMO_WTLHELPER
		case ID_VIEW_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			if (g_options)
				::InvalidateRect(g_options, nullptr, TRUE);
			break;
#endif
		case ID_SECTION_EXPANDALL:
			g_acc.ExpandAll(true);
			break;
		case ID_SECTION_COLLAPSEALL:
			g_acc.ExpandAll(false);
			break;
		case ID_SECTION_ADD:
		{
			CString title, text;
			g_added++;
			title.Format(L"Added %d", g_added);
			text.Format(L"Section %d, added at the end (fixed, 70 DIPs).", g_added);
			ACDSECTION s{};
			s.id = SEC_ADDED + g_added;
			s.pszText = title;
			s.iImage = g_added % 4;
			s.hwndBody = MakeEdit(g_acc, text);
			s.height = 70;
			s.state = ACDSS_EXPANDED;
			g_acc.EnsureVisible(g_acc.InsertSection(&s));
			break;
		}
		case ID_SECTION_REMOVE:
		{
			int n = g_acc.GetSectionCount();
			if (n > 0) {
				auto section = g_acc.GetSection(n - 1);
				HWND body = g_acc.GetSectionWindow(section);
				g_acc.DeleteSection(section);
				if (body && body != g_options)
					::DestroyWindow(body);
				else if (body == g_options)
					g_options = nullptr;
			}
			break;
		}
		case ID_SECTION_MORE:
			if (g_options) {
				g_optionCount++;
				AddOption();
				LayoutOptions();
				g_acc.UpdateFitHeight(g_acc.FindSection(SEC_OPTIONS));
			}
			break;
		case ID_SECTION_SCROLLEND:
			g_acc.EnsureVisible(g_acc.GetSection(g_acc.GetSectionCount() - 1));
			break;
		default:
			return;
	}
	UpdateChecks(::GetMenu(hwnd));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			g_main = hwnd;
			UpdateFont(hwnd);
			MakeImages(hwnd);
			g_acc.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, IDC_ACCORDION);
			g_acc.SetImageList(g_images);
			AddSections(hwnd);
			return 0;

		case WM_SIZE:
			g_acc.SetWindowPos(nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;

		case WM_DPICHANGED:
		{
			auto rc = (const RECT*)lParam;
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}

		case WM_NOTIFY:
		{
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom != IDC_ACCORDION) {
				// the bodies' notifications come through the accordion as if they were the window's children
				if (hdr->hwndFrom == g_files && hdr->code == NM_DBLCLK)
					ShowEvent(L"double click in Files (forwarded)");
				return 0;
			}
			switch (hdr->code) {
				case ACDN_GETFITHEIGHT:
				{
					auto nm = (NMACDFITHEIGHT*)lParam;
					if (nm->section.id == SEC_OPTIONS)
						nm->height = OptionsHeight(hwnd);
					return 0;
				}
				case ACDN_EXPANDING:
				{
					auto nm = (NMACDEXPAND*)lParam;
					if (g_lockDetails && nm->section.id == SEC_DETAILS) {
						ShowEvent(L"Details is locked (ACDN_EXPANDING vetoed)");
						return 1;
					}
					return 0;
				}
				case ACDN_EXPANDED:
				{
					auto nm = (NMACDEXPAND*)lParam;
					ACDSECTION info{};
					g_acc.GetSectionInfo(nm->section.hSection, &info);
					CString text;
					text.Format(L"%s %s", info.pszText, nm->expand ? L"opened" : L"closed");
					ShowEvent(text);
					return 0;
				}
				case ACDN_BUTTONCLICK:
				{
					auto nm = (NMACDBUTTON*)lParam;
					if (nm->idButton == BTN_ADDFILE) {
						CString name;
						name.Format(L"New document %d.txt", ListView_GetItemCount(g_files) + 1);
						LVITEMW item{ LVIF_TEXT };
						item.iItem = 0;
						item.pszText = name.GetBuffer();
						ListView_InsertItem(g_files, &item);
						UpdateFileCount();
						ShowEvent(L"+ clicked: a file added");
					}
					else {
						// a menu dropping down from the button
						POINT pt{ nm->rcButton.left, nm->rcButton.bottom };
						::ClientToScreen(hdr->hwndFrom, &pt);
						HMENU menu = ::CreatePopupMenu();
						::AppendMenuW(menu, MF_STRING, ID_MENU_SORT, L"&Sort by Name");
						::AppendMenuW(menu, MF_STRING, ID_MENU_CLEAR, L"&Clear");
						UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
						::DestroyMenu(menu);
						if (cmd == ID_MENU_CLEAR) {
							ListView_DeleteAllItems(g_files);
							UpdateFileCount();
						}
						ShowEvent(cmd == ID_MENU_SORT ? L"... menu: sort" : cmd == ID_MENU_CLEAR ? L"... menu: cleared" : L"... menu: nothing");
					}
					return 0;
				}
				case ACDN_CONTEXTMENU:
				{
					auto nm = (NMACDCONTEXTMENU*)lParam;
					if (!nm->section.hSection)
						return 0;
					auto section = nm->section.hSection;
					HMENU menu = ::CreatePopupMenu();
					::AppendMenuW(menu, MF_STRING, ID_MENU_TOGGLE, g_acc.IsExpanded(section) ? L"&Collapse" : L"&Expand");
					::AppendMenuW(menu, MF_STRING, ID_MENU_ONLY, L"&Only This One Open");
					UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, nm->ptScreen.x, nm->ptScreen.y, 0, hwnd, nullptr);
					::DestroyMenu(menu);
					if (cmd == ID_MENU_TOGGLE) {
						g_acc.Expand(section, !g_acc.IsExpanded(section));
					}
					else if (cmd == ID_MENU_ONLY) {
						for (int i = 0; i < g_acc.GetSectionCount(); i++)
							g_acc.Expand(g_acc.GetSection(i), g_acc.GetSection(i) == section);
					}
					ShowEvent(L"header context menu");
					return 1;
				}
			}
			return 0;
		}

		case WM_COMMAND:
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
	::InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = OptionsProc;
	wc.hInstance = hInstance;
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"AccordionDemoOptions";
	::RegisterClassExW(&wc);

	wc.lpfnWndProc = WndProc;
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"AccordionDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	::AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_VIEW_SINGLE, L"&One Section Open at a Time");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_ICONS, L"&Icons");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_COLORS, L"&Custom Colors");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_NOANIMATION, L"&No Animation");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_ALWAYSANIMATE, L"&Always Animate (Even with Windows' Animations Off)");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_LOCKDETAILS, L"&Lock Details (Veto Expanding)");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_VIEW_DARKMODE, L"&Dark Mode");
#endif

	HMENU hSection = ::CreatePopupMenu();
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_EXPANDALL, L"&Expand All");
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_COLLAPSEALL, L"&Collapse All");
	::AppendMenuW(hSection, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_ADD, L"&Add a Section");
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_REMOVE, L"&Remove the Last Section");
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_MORE, L"&More Options (the Fit Section Grows)");
	::AppendMenuW(hSection, MF_STRING, ID_SECTION_SCROLLEND, L"&Show the Last Section");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSection, L"&Section");

	HWND hwnd = ::CreateWindowExW(0, L"AccordionDemoWnd", L"Accordion Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 420, 640, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);
	::ShowWindow(hwnd, nCmdShow);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (!::IsDialogMessage(hwnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	_Module.Term();
	return 0;
}
