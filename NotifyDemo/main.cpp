// NotifyDemo: an info bar over a text area, and toasts in a corner of the window. The Info Bar menu adds messages of
// each kind (severities, buttons and links, progress, details, "don't show again", long text), the Toasts menu shows
// toasts (and where); the log at the bottom shows the notifications. F6 moves the keys between the bar and the text;
// Ctrl+Shift+T gives them to the newest toast (Escape gives them back).

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "Notify.h"

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
	IDC_BAR = 100, IDC_EDIT, IDC_LOG, IDC_TOASTS,
	CMD_EXIT = 1000, CMD_INFO, CMD_SUCCESS, CMD_WARNING, CMD_ERROR, CMD_UPDATE, CMD_PROGRESS, CMD_INDETERMINATE, CMD_DETAILS,
	CMD_TIP, CMD_LONG, CMD_REMOVEALL, CMD_ROWS1, CMD_ROWS3, CMD_ROWSALL, CMD_DARKMODE, CMD_LARGEFONT, CMD_NOANIMATION, CMD_CLEARLOG, CMD_LOGHEIGHT, CMD_ALWAYSANIMATE,
	CMD_T_INFO, CMD_T_SUCCESS, CMD_T_WARNING, CMD_T_ERROR, CMD_T_UPDATE, CMD_T_PROGRESS, CMD_T_BURST, CMD_T_CLOSEALL,
	CMD_T_TOPLEFT, CMD_T_TOPRIGHT, CMD_T_BOTTOMLEFT, CMD_T_BOTTOMRIGHT, CMD_T_MAX1, CMD_T_MAX3, CMD_T_MAXALL, CMD_T_STAY, CMD_HIGHCONTRAST,
	// the messages' buttons
	BTN_RESTART = 2000, BTN_LATER, BTN_CANCEL, BTN_RETRY, BTN_REPORT, BTN_OK,
	ID_PROGRESS = 50, ID_UPDATE, ID_TIP, ID_TPROGRESS,
	TIMER_PROGRESS = 1, TIMER_TPROGRESS,
};

static CInfoBarCtrl g_bar;
static CToastHost g_toasts;
static int g_toastProgress;
static HWND g_main, g_edit, g_log;
static HMENU g_menu;
static HFONT g_font, g_largeFont;
static int g_progress;
static int g_counter;

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

static void Layout() {
	if (!g_bar.IsWindow())
		return;   // SetMenu in WM_CREATE sizes the window before the bar is made
	RECT rc;
	::GetClientRect(g_main, &rc);
	int bar = g_bar.GetIdealHeight(), log = Scale(140);
	g_bar.SetWindowPos(nullptr, 0, 0, rc.right, bar, SWP_NOZORDER | SWP_NOACTIVATE);
	// the bar wraps for its width: its height again
	bar = g_bar.GetIdealHeight();
	g_bar.SetWindowPos(nullptr, 0, 0, rc.right, bar, SWP_NOZORDER | SWP_NOACTIVATE);
	int bottom = std::max(bar, (int)rc.bottom - log);
	::SetWindowPos(g_edit, nullptr, 0, bar, rc.right, bottom - bar, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, 0, bottom, rc.right, rc.bottom - bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts() {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
	if (g_font)
		::DeleteObject(g_font);
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	::SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
	::SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);
	if (g_largeFont) {
		// View > Larger Font: a font of the app's, made again for the DPI
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_bar.SetFont(g_largeFont);
	}
}

// ---- The messages -----------------------------------------------------------------------------------

static void AddSimple(UINT severity) {
	static const wchar_t* const Titles[] = { L"Information", L"Done", L"Warning", L"Error" };
	static const wchar_t* const Texts[] = {
		L"The project was opened from a network share.",
		L"The build succeeded.",
		L"Some files couldn't be indexed.",
		L"The connection to the server was lost.",
	};
	wchar_t title[64];
	swprintf_s(title, L"%s %d", Titles[severity], ++g_counter);
	g_bar.AddMessage(severity, title, Texts[severity]);
}

static void AddUpdate() {
	NTFBUTTON buttons[] = { { BTN_RESTART, L"Restart now" }, { BTN_LATER, L"Later" } };
	NTFMESSAGE m;
	m.id = ID_UPDATE;
	m.severity = NTFSEV_INFO;
	m.pszTitle = L"Update ready";
	m.pszText = L"Version 2.4 has been downloaded. <a id=\"notes\">What's new</a> &amp; <a id=\"privacy\">privacy</a>.";
	m.pButtons = buttons;
	m.cButtons = _countof(buttons);
	g_bar.AddMessage(&m);
}

static void AddProgress(bool indeterminate) {
	NTFBUTTON cancel[] = { { BTN_CANCEL, L"Cancel" } };
	NTFMESSAGE m;
	m.id = ID_PROGRESS;
	m.pszTitle = indeterminate ? L"Connecting" : L"Copying";
	m.pszText = indeterminate ? L"Waiting for the server..." : L"128 files to D:\\Backup";
	m.pButtons = cancel;
	m.cButtons = 1;
	m.progress = indeterminate ? NTFP_INDETERMINATE : NTFP_VALUE;
	m.flags = NTFF_NOCLOSE;
	g_bar.AddMessage(&m);
	if (!indeterminate) {
		g_progress = 0;
		::SetTimer(g_main, TIMER_PROGRESS, 100, nullptr);
	}
}

static void OnProgressTimer() {
	if (!g_bar.HasMessage(ID_PROGRESS)) {
		::KillTimer(g_main, TIMER_PROGRESS);
		return;
	}
	g_progress += 2;
	if (g_progress < 100) {
		g_bar.SetProgress(ID_PROGRESS, NTFP_VALUE, g_progress);
		return;
	}
	::KillTimer(g_main, TIMER_PROGRESS);
	// done: the same message, now a success
	g_bar.AddMessage(NTFSEV_SUCCESS, L"Copied", L"128 files to D:\\Backup.", ID_PROGRESS);
}

static void AddDetails() {
	NTFBUTTON buttons[] = { { BTN_RETRY, L"Retry" }, { BTN_REPORT, L"Report" } };
	NTFMESSAGE m;
	m.severity = NTFSEV_ERROR;
	m.pszTitle = L"Save failed";
	m.pszText = L"The file is locked by another process.";
	m.pszDetails = L"System.IO.IOException: The process cannot access the file 'C:\\Projects\\report.docx' because it is "
		L"being used by another process.\n   at Document.Save(String path)\n   at Editor.OnSave()";
	m.pButtons = buttons;
	m.cButtons = _countof(buttons);
	g_bar.AddMessage(&m);
}

static void AddTip() {
	NTFBUTTON ok[] = { { BTN_OK, L"Got it" } };
	NTFMESSAGE m;
	m.id = ID_TIP;
	m.pszTitle = L"Tip";
	m.pszText = L"Press Ctrl+Shift+P to search every command.";
	m.pszCheck = L"Don't show again";
	m.pButtons = ok;
	m.cButtons = 1;
	g_bar.AddMessage(&m);
}

static void AddLong() {
	g_bar.AddMessage(NTFSEV_WARNING, L"Compatibility",
		L"This document was created in an older version and some of its features are shown differently: tables with "
		L"merged cells, linked images and custom fonts may not look as they did. <a id=\"convert\">Convert it</a> to "
		L"the current format to edit all of it, or keep it as it is to share it with people on the older version.");
}

// ---- Toasts -----------------------------------------------------------------------------------------------

static void ToastSimple(UINT severity) {
	static const wchar_t* const Titles[] = { L"Synced", L"Saved", L"Low disk space", L"Upload failed" };
	static const wchar_t* const Texts[] = {
		L"Your settings were synced with your other devices.",
		L"report.docx was saved to OneDrive.",
		L"Drive C: has less than 2 GB free. <a id=\"clean\">Free up space</a>",
		L"photos.zip couldn't be uploaded: the connection was reset.",
	};
	g_toasts.Show(severity, Titles[severity], Texts[severity], 5000);
}

static void ToastUpdate() {
	NTFBUTTON buttons[] = { { BTN_RESTART, L"Restart" }, { BTN_LATER, L"Later" } };
	NTFMESSAGE m;
	m.pszTitle = L"Update ready";
	m.pszText = L"Restart to finish installing version 2.4.";
	m.pButtons = buttons;
	m.cButtons = _countof(buttons);
	m.timeout = 8000;
	g_toasts.Show(&m);
}

static void ToastProgress() {
	NTFBUTTON cancel[] = { { BTN_CANCEL, L"Cancel" } };
	NTFMESSAGE m;
	m.id = ID_TPROGRESS;
	m.pszTitle = L"Exporting";
	m.pszText = L"video.mp4";
	m.pButtons = cancel;
	m.cButtons = 1;
	m.progress = NTFP_VALUE;
	m.flags = NTFF_NOCLOSE;
	g_toasts.Show(&m);
	g_toastProgress = 0;
	::SetTimer(g_main, TIMER_TPROGRESS, 100, nullptr);
}

static void OnToastProgressTimer() {
	if (!g_toasts.IsShown(ID_TPROGRESS)) {
		::KillTimer(g_main, TIMER_TPROGRESS);
		return;
	}
	g_toastProgress += 3;
	if (g_toastProgress < 100) {
		g_toasts.SetProgress(ID_TPROGRESS, NTFP_VALUE, g_toastProgress);
		return;
	}
	::KillTimer(g_main, TIMER_TPROGRESS);
	g_toasts.Show(NTFSEV_SUCCESS, L"Exported", L"video.mp4 is ready. <a id=\"open\">Open folder</a>", 5000, ID_TPROGRESS);
}

static void ToastStay() {
	NTFMESSAGE m;
	m.severity = NTFSEV_ERROR;
	m.pszTitle = L"Sync stopped";
	m.pszText = L"The server rejected the credentials.";
	m.pszDetails = L"HTTP 401 Unauthorized from sync.example.com at 10:42.";
	m.pszCheck = L"Don't show again";
	g_toasts.Show(&m);   // no timeout: it stays
}

// ---- The window ---------------------------------------------------------------------------------------

static HMENU BuildMenu() {
	HMENU file = ::CreatePopupMenu();
	::AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
	HMENU bar = ::CreatePopupMenu();
	::AppendMenuW(bar, MF_STRING, CMD_INFO, L"&Information");
	::AppendMenuW(bar, MF_STRING, CMD_SUCCESS, L"&Success");
	::AppendMenuW(bar, MF_STRING, CMD_WARNING, L"&Warning");
	::AppendMenuW(bar, MF_STRING, CMD_ERROR, L"&Error");
	::AppendMenuW(bar, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(bar, MF_STRING, CMD_UPDATE, L"&Update (buttons, links)");
	::AppendMenuW(bar, MF_STRING, CMD_PROGRESS, L"&Progress");
	::AppendMenuW(bar, MF_STRING, CMD_INDETERMINATE, L"I&ndeterminate Progress");
	::AppendMenuW(bar, MF_STRING, CMD_DETAILS, L"&Details");
	::AppendMenuW(bar, MF_STRING, CMD_TIP, L"&Tip (don't show again)");
	::AppendMenuW(bar, MF_STRING, CMD_LONG, L"&Long Text");
	::AppendMenuW(bar, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(bar, MF_STRING, CMD_REMOVEALL, L"&Remove All");
	HMENU rows = ::CreatePopupMenu();
	::AppendMenuW(rows, MF_STRING, CMD_ROWS1, L"&1");
	::AppendMenuW(rows, MF_STRING, CMD_ROWS3, L"&3");
	::AppendMenuW(rows, MF_STRING, CMD_ROWSALL, L"&All");
	::AppendMenuW(bar, MF_POPUP, (UINT_PTR)rows, L"&Maximum Rows");
	HMENU toasts = ::CreatePopupMenu();
	::AppendMenuW(toasts, MF_STRING, CMD_T_INFO, L"&Information");
	::AppendMenuW(toasts, MF_STRING, CMD_T_SUCCESS, L"&Success");
	::AppendMenuW(toasts, MF_STRING, CMD_T_WARNING, L"&Warning (a link)");
	::AppendMenuW(toasts, MF_STRING, CMD_T_ERROR, L"&Error");
	::AppendMenuW(toasts, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(toasts, MF_STRING, CMD_T_UPDATE, L"&Update (buttons)");
	::AppendMenuW(toasts, MF_STRING, CMD_T_PROGRESS, L"&Progress");
	::AppendMenuW(toasts, MF_STRING, CMD_T_STAY, L"S&taying (details, don't show again)");
	::AppendMenuW(toasts, MF_STRING, CMD_T_BURST, L"&Burst of Six");
	::AppendMenuW(toasts, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(toasts, MF_STRING, CMD_T_CLOSEALL, L"&Close All");
	HMENU corner = ::CreatePopupMenu();
	::AppendMenuW(corner, MF_STRING, CMD_T_TOPLEFT, L"Top &Left");
	::AppendMenuW(corner, MF_STRING, CMD_T_TOPRIGHT, L"Top &Right");
	::AppendMenuW(corner, MF_STRING, CMD_T_BOTTOMLEFT, L"Bottom L&eft");
	::AppendMenuW(corner, MF_STRING, CMD_T_BOTTOMRIGHT, L"Bottom Ri&ght");
	::AppendMenuW(toasts, MF_POPUP, (UINT_PTR)corner, L"C&orner");
	HMENU most = ::CreatePopupMenu();
	::AppendMenuW(most, MF_STRING, CMD_T_MAX1, L"&1");
	::AppendMenuW(most, MF_STRING, CMD_T_MAX3, L"&3");
	::AppendMenuW(most, MF_STRING, CMD_T_MAXALL, L"&As Many as Fit");
	::AppendMenuW(toasts, MF_POPUP, (UINT_PTR)most, L"&Most Shown");
	HMENU view = ::CreatePopupMenu();
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_DARKMODE, L"&Dark Mode");
#endif
	::AppendMenuW(view, MF_STRING, CMD_LARGEFONT, L"&Larger Font");
	::AppendMenuW(view, MF_STRING, CMD_HIGHCONTRAST, L"&High Contrast");
	::AppendMenuW(view, MF_STRING, CMD_NOANIMATION, L"&No Animation");
	::AppendMenuW(view, MF_STRING, CMD_ALWAYSANIMATE, L"&Always Animate (even with Windows' animations off)");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_CLEARLOG, L"Clear L&og");
	::AppendMenuW(view, MF_STRING, CMD_LOGHEIGHT, L"Log &Heights (the slides' frames)");
	HMENU menu = ::CreateMenu();
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)bar, L"&Info Bar");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)toasts, L"&Toasts");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"&View");
	return menu;
}

static bool Toggle(UINT id) {
	bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
	::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
	return on;
}

static void OnCommand(UINT id) {
	switch (id) {
		case CMD_EXIT: ::DestroyWindow(g_main); break;
		case CMD_INFO: case CMD_SUCCESS: case CMD_WARNING: case CMD_ERROR: AddSimple(id - CMD_INFO); break;
		case CMD_UPDATE: AddUpdate(); break;
		case CMD_PROGRESS: AddProgress(false); break;
		case CMD_INDETERMINATE: AddProgress(true); break;
		case CMD_DETAILS: AddDetails(); break;
		case CMD_TIP: AddTip(); break;
		case CMD_LONG: AddLong(); break;
		case CMD_REMOVEALL: g_bar.RemoveAll(); break;
		case CMD_ROWS1: case CMD_ROWS3: case CMD_ROWSALL:
			g_bar.SetMaxRows(id == CMD_ROWS1 ? 1 : id == CMD_ROWS3 ? 3 : 0);
			::CheckMenuRadioItem(g_menu, CMD_ROWS1, CMD_ROWSALL, id, MF_BYCOMMAND);
			break;
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
				g_bar.SetFont(g_largeFont);
			}
			else {
				g_bar.SetFont(nullptr);   // the default again
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		case CMD_NOANIMATION:
			if (Toggle(id))
				g_bar.ModifyStyle(0, NTFS_NOANIMATION);
			else
				g_bar.ModifyStyle(NTFS_NOANIMATION, 0);
			break;
		case CMD_CLEARLOG: ::SendMessageW(g_log, LB_RESETCONTENT, 0, 0); break;
		case CMD_LOGHEIGHT: Toggle(id); break;
		case CMD_HIGHCONTRAST: {
			// as Windows' high contrast would show them (the rest of the window stays as it is)
			int on = Toggle(id) ? 1 : -1;
			g_bar.ForceHighContrast(on);
			g_toasts.ForceHighContrast(on);
			break;
		}
		case CMD_ALWAYSANIMATE:
			if (Toggle(id)) {
				g_bar.ModifyStyle(0, NTFS_ALWAYSANIMATE);
				g_toasts.SetAnimation(1);
			}
			else {
				g_bar.ModifyStyle(NTFS_ALWAYSANIMATE, 0);
				g_toasts.SetAnimation(-1);
			}
			break;
		case CMD_T_INFO: case CMD_T_SUCCESS: case CMD_T_WARNING: case CMD_T_ERROR: ToastSimple(id - CMD_T_INFO); break;
		case CMD_T_UPDATE: ToastUpdate(); break;
		case CMD_T_PROGRESS: ToastProgress(); break;
		case CMD_T_STAY: ToastStay(); break;
		case CMD_T_BURST:
			for (UINT k = 0; k < 6; k++)
				ToastSimple(k % 4);
			break;
		case CMD_T_CLOSEALL: g_toasts.CloseAll(); break;
		case CMD_T_TOPLEFT: case CMD_T_TOPRIGHT: case CMD_T_BOTTOMLEFT: case CMD_T_BOTTOMRIGHT:
			g_toasts.SetCorner(id - CMD_T_TOPLEFT);
			::CheckMenuRadioItem(g_menu, CMD_T_TOPLEFT, CMD_T_BOTTOMRIGHT, id, MF_BYCOMMAND);
			break;
		case CMD_T_MAX1: case CMD_T_MAX3: case CMD_T_MAXALL:
			g_toasts.SetMaxShown(id == CMD_T_MAX1 ? 1 : id == CMD_T_MAX3 ? 3 : 0);
			::CheckMenuRadioItem(g_menu, CMD_T_MAX1, CMD_T_MAXALL, id, MF_BYCOMMAND);
			break;
	}
}

static LRESULT OnNotify(NMHDR* hdr) {
	// the bar's, or a toast's (idFrom: the host's)
	const bool toast = hdr->idFrom == IDC_TOASTS && hdr->hwndFrom != g_bar.m_hWnd;
	if (hdr->hwndFrom != g_bar.m_hWnd && !toast)
		return 0;
	const wchar_t* from = toast ? L"toast " : L"";
	switch (hdr->code) {
		case NTFN_BUTTONCLICK: {
			auto nm = (NMNTFBUTTON*)hdr;
			Log(L"%sNTFN_BUTTONCLICK message %u button %u checked %d", from, nm->id, nm->button, nm->checked);
			if (nm->button == BTN_RETRY) {
				Log(L"  (kept: retrying)");
				return 1;   // the message stays
			}
			return 0;
		}
		case NTFN_LINKCLICK: {
			auto nm = (NMNTFLINK*)hdr;
			Log(L"%sNTFN_LINKCLICK message %u link \"%s\"", from, nm->id, nm->pszLink);
			return 0;
		}
		case NTFN_CLOSED: {
			auto nm = (NMNTFCLOSED*)hdr;
			static const wchar_t* const Reasons[] = { L"close button", L"button", L"timeout" };
			Log(L"%sNTFN_CLOSED message %u (%s) checked %d", from, nm->id, Reasons[std::min(nm->reason, 2u)], nm->checked);
			return 0;
		}
		case NTFN_HEIGHTCHANGED:
			if (::GetMenuState(g_menu, CMD_LOGHEIGHT, MF_BYCOMMAND) & MF_CHECKED)
				Log(L"NTFN_HEIGHTCHANGED %d", g_bar.GetIdealHeight());
			Layout();
			return 0;
	}
	return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_menu = BuildMenu();
			::SetMenu(hwnd, g_menu);
			::CheckMenuRadioItem(g_menu, CMD_ROWS1, CMD_ROWSALL, CMD_ROWS3, MF_BYCOMMAND);
			// the log first: the test script finds the first list box
			g_log = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_BORDER,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			g_edit = ::CreateWindowExW(0, WC_EDITW, L"The document's text.", WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL,
				0, 0, 0, 0, hwnd, (HMENU)IDC_EDIT, nullptr, nullptr);
			g_bar.Create(hwnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, IDC_BAR);
			g_toasts.Attach(hwnd, IDC_TOASTS);
			::CheckMenuRadioItem(g_menu, CMD_T_TOPLEFT, CMD_T_BOTTOMRIGHT, CMD_T_BOTTOMRIGHT, MF_BYCOMMAND);
			::CheckMenuRadioItem(g_menu, CMD_T_MAX1, CMD_T_MAXALL, CMD_T_MAXALL, MF_BYCOMMAND);
			UpdateFonts();
			auto cs = (CREATESTRUCTW*)lParam;
			auto args = (const wchar_t*)cs->lpCreateParams;
			if (!args || !wcsstr(args, L"/empty")) {
				AddUpdate();
				AddSimple(NTFSEV_WARNING);
			}
			Layout();
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
		case WM_TIMER:
			if (wParam == TIMER_PROGRESS)
				OnProgressTimer();
			else if (wParam == TIMER_TPROGRESS)
				OnToastProgressTimer();
			return 0;
		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			UpdateFonts();
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			return 0;
		}
		case WM_DESTROY:
			g_toasts.Detach();   // the host isn't a window: before this one goes
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

// the keys wherever they are: F6 between the bar and the text, Ctrl+Shift+T to the toasts
struct KeyFilter : CMessageFilter {
	BOOL PreTranslateMessage(MSG* pMsg) override {
		if (pMsg->message != WM_KEYDOWN)
			return FALSE;
		if (pMsg->wParam == VK_F6) {
			::SetFocus(::GetFocus() == g_bar.m_hWnd ? g_edit : g_bar.m_hWnd);
			return TRUE;
		}
		if (pMsg->wParam == 'T' && ::GetKeyState(VK_CONTROL) < 0 && ::GetKeyState(VK_SHIFT) < 0) {
			if (!g_toasts.Focus())
				Log(L"no toast to focus");
			return TRUE;
		}
		return FALSE;
	}
};

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
	wc.lpszClassName = L"NotifyDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"NotifyDemoWnd", L"Notify Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, nullptr, nullptr, hInstance, cmdLine);
	::ShowWindow(hwnd, nCmdShow);

	CMessageLoop loop;
	KeyFilter keys;
	_Module.AddMessageLoop(&loop);
	loop.AddMessageFilter(&keys);
	loop.Run();
	loop.RemoveMessageFilter(&keys);
	_Module.RemoveMessageLoop();

	if (g_font)
		::DeleteObject(g_font);
	if (g_largeFont)
		::DeleteObject(g_largeFont);
	_Module.Term();
	return 0;
}
