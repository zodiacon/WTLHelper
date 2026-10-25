// GanttDemo: a website relaunch planned this week on. The grid at the left shows the task tree, the chart its bars and
// links on a calendar; Ctrl+wheel zooms, Shift+wheel pans. The menus zoom to presets, change the calendar (Saturdays,
// a holiday), add and delete tasks, and switch dark mode; the log at the bottom shows the notifications.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "Gantt.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

enum : UINT {
	IDC_GANTT = 100, IDC_LOG,
	CMD_EXIT = 1000, CMD_ZOOMIN, CMD_ZOOMOUT, CMD_HOURS, CMD_DAYS, CMD_WEEKS, CMD_MONTHS, CMD_TODAY, CMD_EXPANDALL,
	CMD_COLLAPSEALL, CMD_ADDTASK, CMD_DELETE, CMD_SATURDAY, CMD_HOLIDAY, CMD_PROGRESSCOL, CMD_DARKMODE, CMD_LARGEFONT,
	CMD_CLEARLOG,
};

static CGanttCtrl g_gantt;
static HWND g_main, g_log;
static HMENU g_menu;
static HFONT g_font, g_largeFont;
static UINT g_nextId = 100;
static SYSTEMTIME g_monday;   // this week's

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

static bool Toggle(UINT id) {
	bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
	::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
	return on;
}

// a day of this week (0 Monday) at 8:00, as a SYSTEMTIME
static SYSTEMTIME Day(int offset) {
	FILETIME ft;
	::SystemTimeToFileTime(&g_monday, &ft);
	ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
	u.QuadPart += (ULONGLONG)offset * 24 * 60 * 60 * 10000000ULL;
	ft = { u.LowPart, u.HighPart };
	SYSTEMTIME st;
	::FileTimeToSystemTime(&ft, &st);
	return st;
}

// ---- The project ---------------------------------------------------------------------------------------

static void Build() {
	const LONGLONG d = g_gantt.GetMinutesPerDay();
	auto task = [&](UINT id, const wchar_t* name, LONGLONG days, UINT parent, int progress = 0, COLORREF color = CLR_INVALID) {
		GNTTASK t;
		t.id = id;
		t.parent = parent;
		t.pszName = name;
		t.start = Day(0);
		t.duration = days * d;
		t.progress = progress;
		t.color = color;
		g_gantt.AddTask(&t);
	};
	task(1, L"Planning", 0, 0);
	task(2, L"Kickoff", 0, 1);
	task(3, L"Requirements", 3, 1, 100);
	task(4, L"Design review", 2, 1, 60);
	task(10, L"Design", 0, 0);
	task(11, L"Wireframes", 4, 10, 20);
	task(12, L"Visual design", 5, 10);
	task(13, L"Design sign-off", 0, 10);
	task(20, L"Build", 0, 0);
	task(21, L"Front end", 10, 20);
	task(22, L"Back end", 12, 20);
	task(23, L"Content migration", 6, 20, 0, RGB(0xE0, 0x8A, 0x2E));
	task(30, L"Launch", 0, 0);
	task(31, L"Testing", 5, 30);
	task(32, L"Fixes", 3, 30);
	task(33, L"Go live", 0, 30);
	g_gantt.AddLink(2, 3);
	g_gantt.AddLink(3, 4);
	g_gantt.AddLink(4, 11);
	g_gantt.AddLink(11, 12);
	g_gantt.AddLink(12, 13);
	g_gantt.AddLink(13, 21);
	g_gantt.AddLink(13, 22);
	g_gantt.AddLink(21, 23, GNTL_STARTSTART, 2 * d);
	g_gantt.AddLink(21, 31);
	g_gantt.AddLink(22, 31);
	g_gantt.AddLink(31, 32);
	g_gantt.AddLink(32, 33);
	g_gantt.Reschedule();
}

// ---- The window ---------------------------------------------------------------------------------------

static void Layout() {
	if (!g_gantt.IsWindow())
		return;   // SetMenu in WM_CREATE sizes the window before the chart is made
	RECT rc;
	::GetClientRect(g_main, &rc);
	const int log = Scale(110);
	const int bottom = std::max(0, (int)rc.bottom - log);
	g_gantt.SetWindowPos(nullptr, 0, 0, rc.right, bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, 0, bottom, rc.right, rc.bottom - bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts() {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
	if (g_font)
		::DeleteObject(g_font);
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	::SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);
	if (g_largeFont) {
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_gantt.SetFont(g_largeFont);
	}
}

static HMENU BuildMenu() {
	HMENU file = ::CreatePopupMenu();
	::AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
	HMENU view = ::CreatePopupMenu();
	::AppendMenuW(view, MF_STRING, CMD_ZOOMIN, L"Zoom &In");
	::AppendMenuW(view, MF_STRING, CMD_ZOOMOUT, L"Zoom &Out");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_HOURS, L"&Hours");
	::AppendMenuW(view, MF_STRING, CMD_DAYS, L"&Days");
	::AppendMenuW(view, MF_STRING, CMD_WEEKS, L"&Weeks");
	::AppendMenuW(view, MF_STRING, CMD_MONTHS, L"&Months");
	::AppendMenuW(view, MF_STRING, CMD_TODAY, L"Go to &Today");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_PROGRESSCOL, L"% &Done Column");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_DARKMODE, L"Dar&k Mode");
#endif
	::AppendMenuW(view, MF_STRING, CMD_LARGEFONT, L"&Larger Font");
	::AppendMenuW(view, MF_STRING, CMD_CLEARLOG, L"Clear L&og");
	HMENU tasks = ::CreatePopupMenu();
	::AppendMenuW(tasks, MF_STRING, CMD_EXPANDALL, L"&Expand All");
	::AppendMenuW(tasks, MF_STRING, CMD_COLLAPSEALL, L"&Collapse All");
	::AppendMenuW(tasks, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(tasks, MF_STRING, CMD_ADDTASK, L"&Add Task After the Selected One");
	::AppendMenuW(tasks, MF_STRING, CMD_DELETE, L"&Delete the Selected Task");
	::AppendMenuW(tasks, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(tasks, MF_STRING, CMD_SATURDAY, L"&Saturdays Are Working Days");
	::AppendMenuW(tasks, MF_STRING, CMD_HOLIDAY, L"&Holiday Next Friday");
	HMENU menu = ::CreateMenu();
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"&View");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)tasks, L"&Tasks");
	return menu;
}

static void ExpandAll(bool expand) {
	for (UINT id : { 1u, 10u, 20u, 30u })
		g_gantt.Expand(id, expand);
}

static void OnCommand(UINT id) {
	switch (id) {
		case CMD_EXIT: ::DestroyWindow(g_main); break;
		case CMD_ZOOMIN: g_gantt.SetZoom(g_gantt.GetZoom() / 1.5); break;
		case CMD_ZOOMOUT: g_gantt.SetZoom(g_gantt.GetZoom() * 1.5); break;
		case CMD_HOURS: g_gantt.SetZoom(1.5); break;
		case CMD_DAYS: g_gantt.SetZoom(45); break;
		case CMD_WEEKS: g_gantt.SetZoom(120); break;
		case CMD_MONTHS: g_gantt.SetZoom(480); break;
		case CMD_TODAY: {
			SYSTEMTIME st;
			::GetLocalTime(&st);
			st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
			g_gantt.ScrollToTime(Day(-1));
			break;
		}
		case CMD_EXPANDALL: ExpandAll(true); break;
		case CMD_COLLAPSEALL: ExpandAll(false); break;
		case CMD_ADDTASK: {
			UINT after = g_gantt.GetSelected();
			GNTTASK sel;
			GNTTASK t;
			t.id = g_nextId++;
			wchar_t name[40];
			swprintf_s(name, L"New task %u", t.id);
			t.pszName = name;
			t.duration = 2LL * g_gantt.GetMinutesPerDay();
			t.start = Day(0);
			if (after && g_gantt.GetTask(after, &sel)) {
				t.parent = sel.parent;
				t.start = sel.finish;
			}
			if (g_gantt.AddTask(&t, after ? after : 0)) {
				g_gantt.Select(t.id);
				g_gantt.EnsureVisible(t.id);
				Log(L"added task %u", t.id);
			}
			break;
		}
		case CMD_DELETE:
			if (UINT sel = g_gantt.GetSelected(); sel && g_gantt.RemoveTask(sel))
				Log(L"removed task %u", sel);
			break;
		case CMD_SATURDAY:
			g_gantt.SetWorkDays(Toggle(id) ? 0x7E : 0x3E);
			g_gantt.Reschedule();
			break;
		case CMD_HOLIDAY:
			if (Toggle(id))
				g_gantt.AddHoliday(Day(11));
			else
				g_gantt.RemoveHoliday(Day(11));
			g_gantt.Reschedule();
			break;
		case CMD_PROGRESSCOL: {
			const bool on = Toggle(id);
			UINT cols[] = { GNTCOL_NAME, GNTCOL_START, GNTCOL_FINISH, GNTCOL_DURATION, GNTCOL_PROGRESS };
			int widths[] = { 170, 80, 80, 60, 56 };
			g_gantt.SetColumns(cols, widths, on ? 5 : 4);
			g_gantt.SetGridWidth(on ? 446 : 390);
			break;
		}
#ifdef DEMO_WTLHELPER
		case CMD_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, g_main);
			::CheckMenuItem(g_menu, id, WTLHelper::IsDarkMode() ? MF_CHECKED : MF_UNCHECKED);
			break;
#endif
		case CMD_LARGEFONT:
			if (Toggle(id)) {
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
				ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
				g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
				g_gantt.SetFont(g_largeFont);
			}
			else {
				g_gantt.SetFont(nullptr);   // the default again
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		case CMD_CLEARLOG: ::SendMessageW(g_log, LB_RESETCONTENT, 0, 0); break;
	}
}

static LRESULT OnNotify(NMHDR* hdr) {
	if (hdr->hwndFrom != g_gantt.m_hWnd)
		return 0;
	auto nm = (NMGANTTTASK*)hdr;
	switch (hdr->code) {
		case GNTN_SELCHANGED: Log(L"GNTN_SELCHANGED %u", nm->id); break;
		case GNTN_DBLCLK: Log(L"GNTN_DBLCLK %u", nm->id); break;
		case GNTN_EXPANDED: Log(L"GNTN_EXPANDED %u", nm->id); break;
	}
	return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_menu = BuildMenu();
			::SetMenu(hwnd, g_menu);
			// the log first: the test script finds the first list box
			g_log = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_BORDER,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			g_gantt.Create(hwnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, IDC_GANTT);
			g_gantt.SetGridWidth(390);
			UpdateFonts();
			// this week's Monday
			::GetLocalTime(&g_monday);
			FILETIME ft;
			g_monday.wHour = 8;
			g_monday.wMinute = g_monday.wSecond = g_monday.wMilliseconds = 0;
			::SystemTimeToFileTime(&g_monday, &ft);
			ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
			u.QuadPart -= (ULONGLONG)((g_monday.wDayOfWeek + 6) % 7) * 24 * 60 * 60 * 10000000ULL;
			ft = { u.LowPart, u.HighPart };
			::FileTimeToSystemTime(&ft, &g_monday);
			Build();
			Layout();
			g_gantt.ScrollToTime(Day(-1));
			return 0;
		}
		case WM_SIZE:
			Layout();
			return 0;
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
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"GanttDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"GanttDemoWnd", L"Gantt Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640, nullptr, nullptr, hInstance, cmdLine);
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
	return 0;
}
