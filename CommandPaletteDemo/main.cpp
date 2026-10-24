#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <shellapi.h>

#include "CommandPalette.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_PALETTE = 101,
	IDC_LOG,
	IDC_POPUP,

	// the menu bar's commands (what the palette lists)
	CMD_FILE_NEW = 1001, CMD_FILE_OPEN, CMD_FILE_SAVE, CMD_FILE_SAVEAS, CMD_FILE_SAVEALL, CMD_FILE_CLOSE, CMD_FILE_PRINT,
	CMD_FILE_RECENT1, CMD_FILE_RECENT2, CMD_FILE_RECENT3, CMD_FILE_EXIT,
	CMD_EDIT_UNDO = 1101, CMD_EDIT_REDO, CMD_EDIT_CUT, CMD_EDIT_COPY, CMD_EDIT_PASTE, CMD_EDIT_FIND, CMD_EDIT_REPLACE,
	CMD_EDIT_GOTOLINE, CMD_EDIT_SELECTALL, CMD_EDIT_TOUPPER, CMD_EDIT_TOLOWER, CMD_EDIT_COMMENT,
	CMD_VIEW_TOOLBAR = 1201, CMD_VIEW_STATUSBAR, CMD_VIEW_LINENUMBERS, CMD_VIEW_WORDWRAP, CMD_VIEW_ZOOMIN, CMD_VIEW_ZOOMOUT,
	CMD_VIEW_ZOOMRESET, CMD_VIEW_FULLSCREEN, CMD_VIEW_DARKMODE,
	CMD_PAL_HIDEDISABLED = 1301, CMD_PAL_NORECENT, CMD_PAL_COLORS, CMD_PAL_CLEARRECENT, CMD_PAL_REFRESH, CMD_PAL_SHOW,
	CMD_HELP_DOCS = 1401, CMD_HELP_SHORTCUTS, CMD_HELP_ABOUT,

	// added with AddCommand only
	CMD_CMD_REFRESH = 2001, CMD_CMD_SEARCH, CMD_CMD_SETTINGS, CMD_CMD_FOLDER, CMD_CMD_SHIELD,
};

// ---- State --------------------------------------------------------------------

static CCommandPaletteCtrl g_palette;
static CCommandPalettePopup g_popup;
static HWND g_main, g_log;
static HIMAGELIST g_images;
static bool g_customColors;

static const CMDPCOLORS g_colors = {
	RGB(0xFA, 0xF6, 0xEE), RGB(0x3A, 0x30, 0x28), RGB(0x9A, 0x8C, 0x7C), RGB(0xC0, 0x50, 0x10), RGB(0xF0, 0xDC, 0xC0),
	RGB(0x3A, 0x30, 0x28), RGB(0xF2, 0xEA, 0xDC), RGB(0xFF, 0xFF, 0xFF), RGB(0x3A, 0x30, 0x28), RGB(0xD8, 0xC8, 0xB0),
	RGB(0xC0, 0x50, 0x10),
};

static int Scale(HWND hwnd, int dips) {
	return ::MulDiv(dips, ::GetDpiForWindow(hwnd), 96);
}

static void Log(LPCWSTR text) {
	int index = (int)::SendMessage(g_log, LB_ADDSTRING, 0, (LPARAM)text);
	::SendMessage(g_log, LB_SETTOPINDEX, index, 0);
}

// ---- The menu bar ---------------------------------------------------------------

static HMENU BuildMenu() {
	auto popup = [](std::initializer_list<std::pair<UINT, LPCWSTR>> items) {
		HMENU menu = ::CreatePopupMenu();
		for (auto& [id, text] : items) {
			if (id == 0)
				::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			else
				::AppendMenuW(menu, MF_STRING, id, text);
		}
		return menu;
	};
	HMENU recent = popup({ { CMD_FILE_RECENT1, L"&1 Report.docx" }, { CMD_FILE_RECENT2, L"&2 Budget.xlsx" }, { CMD_FILE_RECENT3, L"&3 Notes.txt" } });
	HMENU file = popup({ { CMD_FILE_NEW, L"&New\tCtrl+N" }, { CMD_FILE_OPEN, L"&Open...\tCtrl+O" }, { CMD_FILE_SAVE, L"&Save\tCtrl+S" },
		{ CMD_FILE_SAVEAS, L"Save &As...\tCtrl+Shift+S" }, { CMD_FILE_SAVEALL, L"Save A&ll" }, { CMD_FILE_CLOSE, L"&Close\tCtrl+W" }, { 0, nullptr },
		{ CMD_FILE_PRINT, L"&Print...\tCtrl+P" }, { 0, nullptr } });
	::AppendMenuW(file, MF_POPUP, (UINT_PTR)recent, L"Recent &Files");
	::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(file, MF_STRING, CMD_FILE_EXIT, L"E&xit\tAlt+F4");
	HMENU edit = popup({ { CMD_EDIT_UNDO, L"&Undo\tCtrl+Z" }, { CMD_EDIT_REDO, L"&Redo\tCtrl+Y" }, { 0, nullptr },
		{ CMD_EDIT_CUT, L"Cu&t\tCtrl+X" }, { CMD_EDIT_COPY, L"&Copy\tCtrl+C" }, { CMD_EDIT_PASTE, L"&Paste\tCtrl+V" }, { 0, nullptr },
		{ CMD_EDIT_FIND, L"&Find...\tCtrl+F" }, { CMD_EDIT_REPLACE, L"R&eplace...\tCtrl+H" }, { CMD_EDIT_GOTOLINE, L"&Go to Line...\tCtrl+G" },
		{ 0, nullptr }, { CMD_EDIT_SELECTALL, L"Select &All\tCtrl+A" } });
	HMENU advanced = popup({ { CMD_EDIT_TOUPPER, L"Make &Uppercase\tCtrl+Shift+U" }, { CMD_EDIT_TOLOWER, L"Make &Lowercase\tCtrl+U" },
		{ CMD_EDIT_COMMENT, L"Toggle Line &Comment\tCtrl+/" } });
	::AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(edit, MF_POPUP, (UINT_PTR)advanced, L"Ad&vanced");
	HMENU view = popup({ { CMD_VIEW_TOOLBAR, L"&Toolbar" }, { CMD_VIEW_STATUSBAR, L"&Status Bar" }, { CMD_VIEW_LINENUMBERS, L"&Line Numbers" },
		{ CMD_VIEW_WORDWRAP, L"&Word Wrap\tAlt+Z" }, { 0, nullptr }, { CMD_VIEW_ZOOMIN, L"Zoom &In\tCtrl++" }, { CMD_VIEW_ZOOMOUT, L"Zoom &Out\tCtrl+-" },
		{ CMD_VIEW_ZOOMRESET, L"&Reset Zoom\tCtrl+0" }, { 0, nullptr }, { CMD_VIEW_FULLSCREEN, L"&Full Screen\tF11" } });
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_VIEW_DARKMODE, L"&Dark Mode");
#endif
	HMENU palette = popup({ { CMD_PAL_HIDEDISABLED, L"&Hide Disabled Commands" }, { CMD_PAL_NORECENT, L"&No Recently Used First" },
		{ CMD_PAL_COLORS, L"&Custom Colors" }, { 0, nullptr }, { CMD_PAL_CLEARRECENT, L"C&lear Recently Used" },
		{ CMD_PAL_REFRESH, L"&Read the Menus Again" }, { 0, nullptr }, { CMD_PAL_SHOW, L"&Show Command Palette...\tCtrl+Shift+P" } });
	HMENU help = popup({ { CMD_HELP_DOCS, L"&Documentation\tF1" }, { CMD_HELP_SHORTCUTS, L"&Keyboard Shortcuts" }, { 0, nullptr },
		{ CMD_HELP_ABOUT, L"&About" } });
	HMENU bar = ::CreateMenu();
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)palette, L"&Palette");
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
	// some state to show: grayed and checked items
	::EnableMenuItem(bar, CMD_EDIT_REDO, MF_BYCOMMAND | MF_GRAYED);
	::EnableMenuItem(bar, CMD_EDIT_PASTE, MF_BYCOMMAND | MF_GRAYED);
	::CheckMenuItem(bar, CMD_VIEW_STATUSBAR, MF_BYCOMMAND | MF_CHECKED);
	::CheckMenuItem(bar, CMD_VIEW_LINENUMBERS, MF_BYCOMMAND | MF_CHECKED);
	return bar;
}

static HIMAGELIST BuildImages(HWND hwnd) {
	int size = ::GetSystemMetricsForDpi(SM_CXSMICON, ::GetDpiForWindow(hwnd));
	HIMAGELIST images = ::ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 5, 0);
	for (auto id : { SIID_FIND, SIID_FIND, SIID_SETTINGS, SIID_FOLDER, SIID_SHIELD }) {
		SHSTOCKICONINFO info{ sizeof(info) };
		if (SUCCEEDED(::SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_SMALLICON, &info))) {
			::ImageList_AddIcon(images, info.hIcon);
			::DestroyIcon(info.hIcon);
		}
	}
	return images;
}

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(CMD_PAL_HIDEDISABLED, (g_palette.GetStyle() & CMDPS_HIDEDISABLED) != 0);
	check(CMD_PAL_NORECENT, (g_palette.GetStyle() & CMDPS_NORECENT) != 0);
	check(CMD_PAL_COLORS, g_customColors);
#ifdef DEMO_WTLHELPER
	check(CMD_VIEW_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void ToggleStyle(DWORD style) {
	if (g_palette.GetStyle() & style)
		g_palette.ModifyStyle(style, 0);
	else
		g_palette.ModifyStyle(0, style);
	g_palette.RefreshMenus();   // filters again with the style
}

static void OnCommand(HWND hwnd, UINT id) {
	HMENU menu = ::GetMenu(hwnd);
	switch (id) {
		case CMD_FILE_EXIT:
			::DestroyWindow(hwnd);
			return;
		case CMD_PAL_HIDEDISABLED: ToggleStyle(CMDPS_HIDEDISABLED); break;
		case CMD_PAL_NORECENT: ToggleStyle(CMDPS_NORECENT); break;
		case CMD_PAL_COLORS:
			g_customColors = !g_customColors;
			g_palette.SetColors(g_customColors ? &g_colors : nullptr);
			break;
		case CMD_PAL_CLEARRECENT: g_palette.ClearRecent(); break;
		case CMD_PAL_REFRESH: g_palette.RefreshMenus(hwnd); break;
		case CMD_PAL_SHOW:
			g_popup.Show();
			return;
		case CMD_VIEW_TOOLBAR:
		case CMD_VIEW_STATUSBAR:
		case CMD_VIEW_LINENUMBERS:
		case CMD_VIEW_WORDWRAP: {
			// checkable: the palette shows it when the menus are read again
			bool on = (::GetMenuState(menu, id, MF_BYCOMMAND) & MF_CHECKED) != 0;
			::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_UNCHECKED : MF_CHECKED));
			g_palette.RefreshMenus();
			break;
		}
#ifdef DEMO_WTLHELPER
		case CMD_VIEW_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
	}
	// the log: what ran (its menu text without the & marks and the shortcut, or just the id)
	wchar_t text[128] = L"";
	CString name;
	if (::GetMenuStringW(menu, id, text, (int)std::size(text), MF_BYCOMMAND)) {
		name = text;
		int tab = name.Find(L'\t');
		if (tab >= 0)
			name = name.Left(tab);
		name.Replace(L"&&", L"\x1");
		name.Remove(L'&');
		name.Replace(L"\x1", L"&");
	}
	CString line;
	if (name.IsEmpty())
		line.Format(L"WM_COMMAND %u", id);
	else
		line.Format(L"WM_COMMAND %u: %s", id, (LPCWSTR)name);
	Log(line);
	UpdateChecks(menu);
}

// ---- Main window ------------------------------------------------------------------

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	int width = std::min((int)rc.right / 2, Scale(hwnd, 460));
	int pad = Scale(hwnd, 8);
	g_palette.SetWindowPos(nullptr, pad, pad, width, std::min((int)rc.bottom - 2 * pad, g_palette.GetIdealHeight(12)), SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, width + 2 * pad, pad, rc.right - width - 3 * pad, rc.bottom - 2 * pad, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_palette.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER, 0, IDC_PALETTE);
			g_images = BuildImages(hwnd);
			g_popup.Create(hwnd, IDC_POPUP);
			// the same commands in both: the embedded palette and the popup's
			for (auto palette : { &g_palette, &g_popup.Palette() }) {
				palette->SetImageList(g_images);
				palette->AddFromMenu(::GetMenu(hwnd));
				palette->AddCommand(CMD_CMD_REFRESH, L"Refresh Everything", L"Developer", L"F5", 0);
				palette->AddCommand(CMD_CMD_SEARCH, L"Search Files", L"Go", L"Ctrl+Shift+F", 1);
				palette->AddCommand(CMD_CMD_SETTINGS, L"Open Settings", L"Preferences", L"Ctrl+,", 2);
				palette->AddCommand(CMD_CMD_FOLDER, L"Reveal in File Explorer", L"File", nullptr, 3);
				palette->AddCommand(CMD_CMD_SHIELD, L"Run as Administrator", nullptr, nullptr, 4);
			}
			g_log = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			::SendMessage(g_log, WM_SETFONT, g_palette.SendMessage(WM_GETFONT), FALSE);
			Layout(hwnd);
			return 0;
		}

		case WM_SIZE:
			Layout(hwnd);
			return 0;

		case WM_SETFOCUS:
			g_palette.SetFocus();
			return 0;

		case WM_COMMAND:
			if (lParam == 0)
				OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_INITMENUPOPUP:
			// as an app would before showing a menu (and the palettes, when they read it): Paste needs text to paste
			if ((HMENU)wParam == ::GetSubMenu(::GetMenu(hwnd), 1))
				::EnableMenuItem((HMENU)wParam, CMD_EDIT_PASTE, MF_BYCOMMAND | (::IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
			return 0;

		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom != IDC_PALETTE && hdr->idFrom != IDC_POPUP)
				break;
			LPCWSTR from = hdr->idFrom == IDC_POPUP ? L"popup" : L"embedded";
			CString line;
			switch (hdr->code) {
				case CMDPN_CANCEL:
					if (hdr->idFrom == IDC_PALETTE) {
						g_palette.SetSearchText(L"");
						Log(L"embedded: CMDPN_CANCEL (Escape), the search cleared");
					}
					else {
						Log(L"popup: CMDPN_CANCEL (Escape)");
					}
					break;
				case CMDPN_EXECUTE:
					line.Format(L"%s: CMDPN_EXECUTE %u", from, ((NMCMDPITEM*)lParam)->id);
					Log(line);
					break;
				case CMDPN_CLOSED:
					Log(L"popup: CMDPN_CLOSED");
					break;
			}
			return 0;
		}

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	::InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"CommandPaletteDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"CommandPaletteDemoWnd", L"CommandPalette Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640, nullptr, BuildMenu(), hInstance, nullptr);
	UpdateChecks(::GetMenu(hwnd));
	::ShowWindow(hwnd, nCmdShow);

	ACCEL accels[] = { { FVIRTKEY | FCONTROL | FSHIFT, 'P', CMD_PAL_SHOW } };
	HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)std::size(accels));
	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (::TranslateAccelerator(hwnd, hAccel, &msg))
			continue;
		if (!::IsDialogMessage(hwnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	if (g_images)
		::ImageList_Destroy(g_images);
	_Module.Term();
	return 0;
}
