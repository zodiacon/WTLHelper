// BreadcrumbDemo: a folder browser over the file system or the registry, with a breadcrumb bar above the folder's
// contents. The bar asks the demo for what's under a segment (BRCN_GETCHILDREN) and says where the user wants to go
// (BRCN_NAVIGATE); the demo goes there and sets the bar's path. The log at the bottom shows the notifications.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include "Breadcrumb.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

enum : UINT {
	IDC_CRUMB = 100, IDC_LIST, IDC_LOG,
	CMD_EXIT = 1000, CMD_FILES, CMD_REGISTRY, CMD_NAV, CMD_UP, CMD_ICON, CMD_ROOTCHEVRON, CMD_HISTORY, CMD_BUTTONS,
	CMD_DARKMODE, CMD_CUSTOMCOLORS, CMD_LARGEFONT, CMD_CLEARLOG,
	CMD_REFRESH = 2000, CMD_SEARCH,
};

static CBreadcrumbCtrl g_crumb;
static HWND g_main, g_list, g_log;
static HMENU g_menu;
static HIMAGELIST g_images;           // the system's small icons
static HFONT g_font, g_largeFont;
static bool g_registry;
static std::wstring g_path;           // the file system's ("" This PC) or the registry's ("Computer\HKEY_...")
static int g_folderIcon = -1, g_pcIcon = -1;

struct Entry {
	std::wstring text, name;
	int image = -1;
};
static std::vector<Entry> g_contents;   // the list view's

// ---- Helpers ------------------------------------------------------------------------------------

static void Log(const wchar_t* format, ...) {
	wchar_t text[1024];
	va_list args;
	va_start(args, format);
	::_vsnwprintf_s(text, _TRUNCATE, format, args);
	va_end(args);
	int i = (int)::SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)text);
	::SendMessageW(g_log, LB_SETCURSEL, i, 0);
}

static int Scale(int dips) {
	return ::MulDiv(dips, (int)::GetDpiForWindow(g_main), 96);
}

static std::wstring Join(const std::wstring& parent, const std::wstring& name) {
	if (parent.empty())
		return name;
	return parent.back() == L'\\' ? parent + name : parent + L'\\' + name;
}

static std::vector<std::wstring> Split(const std::wstring& path) {
	std::vector<std::wstring> parts;
	size_t i = 0;
	while (i < path.size()) {
		size_t j = path.find(L'\\', i);
		if (j == std::wstring::npos)
			j = path.size();
		if (j > i)
			parts.push_back(path.substr(i, j - i));
		i = j + 1;
	}
	return parts;
}

static void Sort(std::vector<Entry>& entries) {
	std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
		return ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
			a.text.c_str(), -1, b.text.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
	});
}

static int FileIcon(const wchar_t* path, bool generic = false) {
	SHFILEINFOW sfi{};
	UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON | (generic ? SHGFI_USEFILEATTRIBUTES : 0);
	::SHGetFileInfoW(path, FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), flags);
	return sfi.iIcon;
}

// ---- The file system ----------------------------------------------------------------------------

static std::wstring DriveName(const std::wstring& drive, int* image) {
	SHFILEINFOW sfi{};
	::SHGetFileInfoW(drive.c_str(), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
	*image = sfi.iIcon;
	return sfi.szDisplayName[0] ? sfi.szDisplayName : drive;
}

static bool IsDrive(const std::wstring& s) {
	return s.size() >= 2 && s[1] == L':' && iswalpha(s[0]);
}

static std::vector<Entry> FileChildren(const std::wstring& path) {
	std::vector<Entry> entries;
	if (path.empty()) {
		// This PC: the drives
		wchar_t drives[512];
		DWORD n = ::GetLogicalDriveStringsW(_countof(drives), drives);
		for (const wchar_t* d = drives; d < drives + n && *d; d += wcslen(d) + 1) {
			Entry e;
			e.name = d;
			e.text = DriveName(d, &e.image);
			entries.push_back(std::move(e));
		}
		return entries;
	}
	WIN32_FIND_DATAW fd;
	HANDLE h = ::FindFirstFileExW(Join(path, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr,
		FIND_FIRST_EX_LARGE_FETCH);
	if (h == INVALID_HANDLE_VALUE)
		return entries;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
			continue;
		if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
			continue;
		entries.push_back({ fd.cFileName, fd.cFileName, g_folderIcon });
	} while (::FindNextFileW(h, &fd));
	::FindClose(h);
	Sort(entries);
	return entries;
}

static std::vector<Entry> FileSegments(const std::wstring& path) {
	std::vector<Entry> segs;
	segs.push_back({ L"This PC", L"", g_pcIcon });
	if (path.empty())
		return segs;
	std::wstring drive = path.substr(0, 3);
	Entry d;
	d.name = drive;
	d.text = DriveName(drive, &d.image);
	segs.push_back(std::move(d));
	for (auto& part : Split(path.substr(std::min(path.size(), (size_t)3))))
		segs.push_back({ part, part, g_folderIcon });
	return segs;
}

static bool FileExists(const std::wstring& path) {
	if (path.empty())
		return true;
	if (!IsDrive(path))
		return false;
	DWORD attrs = ::GetFileAttributesW(path.size() == 2 ? (path + L"\\").c_str() : path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ---- The registry ---------------------------------------------------------------------------------

static const struct {
	const wchar_t* name;
	HKEY key;
} Hives[] = {
	{ L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT },
	{ L"HKEY_CURRENT_USER", HKEY_CURRENT_USER },
	{ L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE },
	{ L"HKEY_USERS", HKEY_USERS },
	{ L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG },
};

// "Computer\HKEY_CURRENT_USER\Software": the key opened, nullptr for Computer itself; false if there's no such key
static bool OpenKey(const std::wstring& path, HKEY* key) {
	*key = nullptr;
	auto parts = Split(path);
	if (parts.empty() || _wcsicmp(parts[0].c_str(), L"Computer"))
		return false;
	if (parts.size() == 1)
		return true;
	for (auto& h : Hives)
		if (!_wcsicmp(parts[1].c_str(), h.name)) {
			std::wstring sub;
			for (size_t i = 2; i < parts.size(); i++)
				sub = sub.empty() ? parts[i] : sub + L'\\' + parts[i];
			return ::RegOpenKeyExW(h.key, sub.c_str(), 0, KEY_ENUMERATE_SUB_KEYS, key) == ERROR_SUCCESS;
		}
	return false;
}

static std::vector<Entry> RegistryChildren(const std::wstring& path) {
	std::vector<Entry> entries;
	HKEY key;
	if (!OpenKey(path, &key))
		return entries;
	if (!key) {
		for (auto& h : Hives)
			entries.push_back({ h.name, h.name, g_folderIcon });
		return entries;
	}
	wchar_t name[256];
	for (DWORD i = 0;; i++) {
		DWORD len = _countof(name);
		LSTATUS status = ::RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr);
		if (status == ERROR_NO_MORE_ITEMS)
			break;
		if (status == ERROR_SUCCESS)
			entries.push_back({ name, name, g_folderIcon });
	}
	::RegCloseKey(key);
	Sort(entries);
	return entries;
}

static std::vector<Entry> RegistrySegments(const std::wstring& path) {
	std::vector<Entry> segs;
	for (auto& part : Split(path))
		segs.push_back({ part, part, segs.empty() ? g_pcIcon : g_folderIcon });
	return segs;
}

// ---- Going places ----------------------------------------------------------------------------------

static std::vector<Entry> Children(const std::wstring& path) {
	return g_registry ? RegistryChildren(path) : FileChildren(path);
}

static bool GoTo(const std::wstring& path) {
	if (g_registry) {
		HKEY key;
		if (!OpenKey(path, &key))
			return false;
		if (key)
			::RegCloseKey(key);
	}
	else if (!FileExists(path))
		return false;
	g_path = path;

	auto segs = g_registry ? RegistrySegments(path) : FileSegments(path);
	std::vector<BRCITEM> items(segs.size());
	for (size_t i = 0; i < segs.size(); i++) {
		items[i].pszText = segs[i].text.c_str();
		items[i].pszName = segs[i].name.c_str();
		items[i].iImage = segs[i].image;
	}
	g_crumb.SetPath(items.data(), (int)items.size());

	g_contents = Children(path);
	::SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(g_list);
	for (int i = 0; i < (int)g_contents.size(); i++) {
		LVITEMW item{ LVIF_TEXT | LVIF_IMAGE };
		item.iItem = i;
		item.pszText = (LPWSTR)g_contents[i].text.c_str();
		item.iImage = g_contents[i].image;
		ListView_InsertItem(g_list, &item);
	}
	::SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);

	wchar_t text[64];
	g_crumb.GetItemText(g_crumb.GetItemCount() - 1, text, _countof(text));
	std::wstring title = std::wstring(text) + L" - Breadcrumb Demo";
	::SetWindowTextW(g_main, title.c_str());
	return true;
}

static void SetMode(bool registry) {
	g_registry = registry;
	::CheckMenuRadioItem(g_menu, CMD_FILES, CMD_REGISTRY, registry ? CMD_REGISTRY : CMD_FILES, MF_BYCOMMAND);
	if (registry)
		GoTo(L"Computer\\HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer");
	else {
		wchar_t* home = nullptr;
		::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home);
		GoTo(home ? home : L"C:\\");
		::CoTaskMemFree(home);
	}
}

// the root chevron's list
static void AddRootChildren() {
	if (g_registry) {
		g_crumb.AddChild(L"Computer", g_pcIcon, 0, L"Computer");
		g_crumb.AddChild(L"HKCU\\Software", g_folderIcon, 0, L"Computer\\HKEY_CURRENT_USER\\Software");
		g_crumb.AddChild(L"HKLM\\SOFTWARE", g_folderIcon, 0, L"Computer\\HKEY_LOCAL_MACHINE\\SOFTWARE");
		return;
	}
	g_crumb.AddChild(L"This PC", g_pcIcon, 0, L"");
	for (auto& id : { FOLDERID_Desktop, FOLDERID_Documents, FOLDERID_Downloads, FOLDERID_Pictures }) {
		wchar_t* path = nullptr;
		if (SUCCEEDED(::SHGetKnownFolderPath(id, 0, nullptr, &path))) {
			std::wstring p = path;
			g_crumb.AddChild(p.substr(p.find_last_of(L'\\') + 1).c_str(), FileIcon(path), 0, path);
		}
		::CoTaskMemFree(path);
	}
}

// ---- The window ---------------------------------------------------------------------------------------

static void Layout() {
	if (!g_crumb.IsWindow())
		return;   // SetMenu in WM_CREATE sizes the window before the bar is made
	RECT rc;
	::GetClientRect(g_main, &rc);
	int margin = Scale(6), bar = g_crumb.GetIdealHeight(), log = Scale(130);
	g_crumb.SetWindowPos(nullptr, margin, margin, std::max(0, (int)rc.right - 2 * margin), bar, SWP_NOZORDER | SWP_NOACTIVATE);
	int top = bar + 2 * margin;
	int bottom = std::max(top, (int)rc.bottom - log);
	::SetWindowPos(g_list, nullptr, 0, top, rc.right, bottom - top, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, 0, bottom, rc.right, rc.bottom - bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts() {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
	if (g_font)
		::DeleteObject(g_font);
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	::SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);
	::SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);
	if (g_largeFont) {
		// View > Larger Font: a font of the app's, made again for the DPI
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_crumb.SetFont(g_largeFont);
	}
}

static void AddButtons() {
	BRCBUTTON b;
	b.id = CMD_SEARCH;
	b.glyph = BRCGLYPH_SEARCH;
	b.pszTip = L"Search";
	g_crumb.AddButton(&b);
	b.id = CMD_REFRESH;
	b.glyph = BRCGLYPH_REFRESH;
	b.pszTip = L"Refresh";
	g_crumb.AddButton(&b);
}

static void ToggleStyle(UINT cmd, DWORD style) {
	bool on = !(::GetMenuState(g_menu, cmd, MF_BYCOMMAND) & MF_CHECKED);
	::CheckMenuItem(g_menu, cmd, on ? MF_CHECKED : MF_UNCHECKED);
	// the menu item says the part is shown; the style hides it
	if (on)
		g_crumb.ModifyStyle(style, 0);
	else
		g_crumb.ModifyStyle(0, style);
}

static HMENU BuildMenu() {
	HMENU file = ::CreatePopupMenu();
	::AppendMenuW(file, MF_STRING, CMD_FILES, L"&File System");
	::AppendMenuW(file, MF_STRING, CMD_REGISTRY, L"&Registry");
	::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
	HMENU view = ::CreatePopupMenu();
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_NAV, L"&Back and Forward");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_UP, L"&Up");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_ICON, L"Location &Icon");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_ROOTCHEVRON, L"&Root Chevron");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_HISTORY, L"&History Arrow");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_BUTTONS, L"&Action Buttons");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_DARKMODE, L"&Dark Mode");
#endif
	::AppendMenuW(view, MF_STRING, CMD_CUSTOMCOLORS, L"&Custom Colors");
	::AppendMenuW(view, MF_STRING, CMD_LARGEFONT, L"&Larger Font");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_CLEARLOG, L"Clear L&og");
	HMENU menu = ::CreateMenu();
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"&View");
	return menu;
}

static void OnCommand(UINT id) {
	switch (id) {
		case CMD_EXIT: ::DestroyWindow(g_main); break;
		case CMD_FILES: SetMode(false); break;
		case CMD_REGISTRY: SetMode(true); break;
		case CMD_NAV: ToggleStyle(id, BRCS_NONAV); break;
		case CMD_UP: ToggleStyle(id, BRCS_NOUP); break;
		case CMD_ICON: ToggleStyle(id, BRCS_NOICON); break;
		case CMD_ROOTCHEVRON: ToggleStyle(id, BRCS_NOROOTCHEVRON); break;
		case CMD_HISTORY: ToggleStyle(id, BRCS_NOHISTORY); break;
		case CMD_BUTTONS: {
			bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
			::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
			if (on)
				AddButtons();
			else {
				g_crumb.RemoveButton(CMD_SEARCH);
				g_crumb.RemoveButton(CMD_REFRESH);
			}
			break;
		}
#ifdef DEMO_WTLHELPER
		case CMD_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, g_main);
			::CheckMenuItem(g_menu, id, WTLHelper::IsDarkMode() ? MF_CHECKED : MF_UNCHECKED);
			::InvalidateRect(g_main, nullptr, TRUE);
			break;
#endif
		case CMD_CUSTOMCOLORS: {
			bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
			::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
			BRCCOLORS c;
			c.crBack = RGB(0x1E, 0x3A, 0x3F);
			c.crBox = RGB(0x24, 0x47, 0x4D);
			c.crBorder = RGB(0x3D, 0x6B, 0x72);
			c.crHotBorder = RGB(0x6F, 0xB8, 0xC2);
			c.crText = RGB(0xE8, 0xF4, 0xF5);
			c.crDimText = RGB(0x7F, 0x9E, 0xA2);
			c.crHotBack = RGB(0x2F, 0x5C, 0x63);
			c.crPressedBack = RGB(0x3A, 0x72, 0x7A);
			c.crListBack = RGB(0x24, 0x47, 0x4D);
			c.crListHotBack = RGB(0x2E, 0x6E, 0x78);
			g_crumb.SetColors(on ? &c : nullptr);
			::InvalidateRect(g_main, nullptr, TRUE);
			break;
		}
		case CMD_LARGEFONT: {
			bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
			::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
			if (on) {
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
				ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
				g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
				g_crumb.SetFont(g_largeFont);
			}
			else {
				g_crumb.SetFont(nullptr);   // the default again
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		}
		case CMD_CLEARLOG: ::SendMessageW(g_log, LB_RESETCONTENT, 0, 0); break;
		case CMD_REFRESH:
			Log(L"WM_COMMAND Refresh");
			GoTo(g_path);
			break;
		case CMD_SEARCH: Log(L"WM_COMMAND Search"); break;
	}
}

static LRESULT OnNotify(NMHDR* hdr) {
	if (hdr->hwndFrom == g_crumb.m_hWnd) {
		switch (hdr->code) {
			case BRCN_NAVIGATE: {
				auto nm = (NMBRCNAVIGATE*)hdr;
				Log(L"BRCN_NAVIGATE %s %d \"%s\" -> \"%s\"", nm->source == BRCNS_CHILD ? L"child of" : L"segment", nm->index, nm->pszText, nm->pszPath);
				if (!GoTo(nm->pszPath))
					Log(L"  not found");
				return 0;
			}
			case BRCN_GETCHILDREN: {
				auto nm = (NMBRCCHILDREN*)hdr;
				int count = 0;
				if (nm->index < 0) {
					AddRootChildren();
					count = -1;
				}
				else
					for (auto& e : Children(nm->pszPath)) {
						g_crumb.AddChild(e.text.c_str(), e.image, 0, e.name.c_str());
						count++;
					}
				Log(L"BRCN_GETCHILDREN %d \"%s\": %d", nm->index, nm->pszPath, count);
				return 0;
			}
			case BRCN_BUTTONCLICK:
				Log(L"BRCN_BUTTONCLICK %u", ((NMBRCBUTTON*)hdr)->id);
				return 0;
			case BRCN_HEIGHTCHANGED:
				Log(L"BRCN_HEIGHTCHANGED %d", g_crumb.GetIdealHeight());
				Layout();
				return 0;
		}
	}
	if (hdr->hwndFrom == g_list && hdr->code == LVN_ITEMACTIVATE) {
		int i = ((NMITEMACTIVATE*)hdr)->iItem;
		if (i >= 0 && i < (int)g_contents.size())
			GoTo(Join(g_path, g_contents[i].name));
	}
	return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_menu = BuildMenu();
			::SetMenu(hwnd, g_menu);
			::CheckMenuRadioItem(g_menu, CMD_FILES, CMD_REGISTRY, CMD_FILES, MF_BYCOMMAND);
			// the log first: the test script finds the first list box
			g_log = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_BORDER,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			g_list = ::CreateWindowExW(0, WC_LISTVIEWW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_LIST | LVS_SHAREIMAGELISTS | LVS_SINGLESEL,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LIST, nullptr, nullptr);
			ListView_SetExtendedListViewStyle(g_list, LVS_EX_DOUBLEBUFFER);

			SHFILEINFOW sfi{};
			g_images = (HIMAGELIST)::SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
			g_folderIcon = FileIcon(L"folder", true);
			PIDLIST_ABSOLUTE pc = nullptr;
			if (SUCCEEDED(::SHGetSpecialFolderLocation(nullptr, CSIDL_DRIVES, &pc))) {
				::SHGetFileInfoW((LPCWSTR)pc, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
				g_pcIcon = sfi.iIcon;
				::CoTaskMemFree(pc);
			}
			ListView_SetImageList(g_list, g_images, LVSIL_SMALL);

			g_crumb.Create(hwnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, IDC_CRUMB);
			g_crumb.SetImageList(g_images);
			AddButtons();
			UpdateFonts();
			Layout();
			auto cs = (CREATESTRUCTW*)lParam;
			bool registry = cs->lpCreateParams && wcsstr((const wchar_t*)cs->lpCreateParams, L"/reg");
			SetMode(registry);
			return 0;
		}
		case WM_SIZE:
			Layout();
			return 0;
		case WM_ERASEBKGND: {
			// the bar's background around it
			RECT rc;
			::GetClientRect(hwnd, &rc);
			HBRUSH brush = ::CreateSolidBrush(g_crumb.GetColors().crBack);
			::FillRect((HDC)wParam, &rc, brush);
			::DeleteObject(brush);
			return 1;
		}
		case WM_COMMAND:
			OnCommand(LOWORD(wParam));
			return 0;
		case WM_NOTIFY:
			return OnNotify((NMHDR*)lParam);
		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			UpdateFonts();
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			return 0;
		}
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES };
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
	wc.lpszClassName = L"BreadcrumbDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"BreadcrumbDemoWnd", L"Breadcrumb Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, nullptr, nullptr, hInstance, cmdLine);
	::ShowWindow(hwnd, nCmdShow);

	CMessageLoop loop;
	_Module.AddMessageLoop(&loop);
	loop.Run();
	_Module.RemoveMessageLoop();

	if (g_font)
		::DeleteObject(g_font);
	if (g_largeFont)
		::DeleteObject(g_largeFont);
	_Module.Term();
	::CoUninitialize();
	return 0;
}
