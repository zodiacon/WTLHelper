#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <commdlg.h>

#include "LogView.h"

#include <atomic>
#include <iterator>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_LOG = 101,
	IDC_STATUS = 102,
	IDC_FILTER = 103,
	IDC_SEARCH = 104,

	ID_FILE_ADD = 201,
	ID_FILE_ADDMILLION,
	ID_FILE_STREAM,
	ID_FILE_LOAD,
	ID_FILE_CLEAR,
	ID_FILE_EXIT,
	ID_VIEW_FOLLOW,
	ID_VIEW_LINENUMBERS,
	ID_VIEW_DARK,
	ID_VIEW_TABS8,
	ID_VIEW_LIMIT,
	ID_VIEW_VIRTUAL,
	ID_LINE_COPY,
	ID_LINE_MARK,
	ID_FILTER_FOCUS,
	ID_FILTER_REGEX,
	ID_FILTER_MATCHCASE,
	ID_FILTER_EXCLUDE,
	ID_FILTER_WARNINGS,
	ID_FILTER_CLEAR,
	ID_RULES_WORKER3,
	ID_RULES_CACHEMISS,
	ID_RULES_NUMBERS,
	ID_RULES_CLEAR,
	ID_SEARCH_FOCUS,
	ID_SEARCH_NEXT,
	ID_SEARCH_PREV,
	ID_SEARCH_MATCHCASE,
	ID_SEARCH_REGEX,
	ID_SEARCH_WHOLEWORD,
	ID_FILTER_WHOLEWORD,
};

// ---- Globals ----------------------------------------------------------------

static CLogViewCtrl      g_log;
static HWND              g_status = nullptr;
static ULONGLONG         g_generated = 0;   // lines made so far, for their text
static std::thread       g_stream;
static std::atomic<bool> g_streamStop = false;
static size_t            g_menuLine = LGV_NOLINE;   // the context menu's line
static CString           g_event;
static HWND              g_filterEdit = nullptr;
static UINT              g_filterFlags = 0;
static HWND              g_searchEdit = nullptr;
static UINT              g_searchFlags = 0;
static bool              g_warningsOnly = false;

constexpr BYTE STYLE_MARKED = 1;
constexpr BYTE STYLE_HIGHLIGHT = 2;   // match-only rule: yellow behind the text
constexpr BYTE STYLE_CACHE = 3;       // whole-line rule
constexpr BYTE STYLE_NUMBER = 4;      // match-only rule: colored text
constexpr size_t VIRTUAL_LINES = 10'000'000;

static void SetEventText(LPCWSTR text) {
	g_event = text;
}

// ---- Sample lines -----------------------------------------------------------

static BYTE LevelFor(ULONGLONG n) {
	// mostly info and debug, some warnings, a few errors
	ULONGLONG h = n * 2654435761ULL;
	int r = (int)((h >> 8) % 100);
	if (r < 15) return LGVL_TRACE;
	if (r < 40) return LGVL_DEBUG;
	if (r < 86) return LGVL_INFO;
	if (r < 95) return LGVL_WARNING;
	if (r < 99) return LGVL_ERROR;
	return LGVL_CRITICAL;
}

static void MakeLine(ULONGLONG n, CString& text, BYTE& level) {
	static const wchar_t* names[] = { L"TRACE", L"DEBUG", L"INFO ", L"WARN ", L"ERROR", L"FATAL" };
	static const wchar_t* messages[] = {
		L"processed request",
		L"cache miss for key",
		L"connection opened\tremote=10.0.0.7",
		L"retrying after timeout",
		L"queue length is",
		L"configuration reloaded",
	};
	level = LevelFor(n);
	ULONGLONG ms = n * 37;
	text.Format(L"%02u:%02u:%02u.%03u [%s] worker %u: %s %llu", (UINT)(ms / 3600000 % 24), (UINT)(ms / 60000 % 60),
		(UINT)(ms / 1000 % 60), (UINT)(ms % 1000), names[level - 1], (UINT)(n % 8), messages[n % std::size(messages)], n);
	if (n % 97 == 0) {
		// a long line, for the horizontal scroll bar
		text += L" | payload:";
		for (int i = 0; i < 30; i++)
			text.AppendFormat(L" %08X", (UINT)(n * (i + 1) * 2246822519U));
	}
}

static void AddLines(int count) {
	std::vector<CString> texts(count);
	std::vector<LGVLINE> lines(count);
	for (int i = 0; i < count; i++) {
		MakeLine(g_generated++, texts[i], lines[i].level);
		lines[i].pszText = texts[i];
	}
	g_log.AppendLines(lines.data(), count);
}

// ---- The stream: lines from another thread ------------------------------------

static void StopStream() {
	if (!g_stream.joinable())
		return;
	g_streamStop = true;
	g_stream.join();
}

static void StartStream() {
	g_streamStop = false;
	// its own numbering, so the UI thread's counter isn't shared
	g_stream = std::thread([] {
		ULONGLONG n = 1'000'000'000;
		CString text;
		while (!g_streamStop) {
			for (int i = 0; i < 20; i++) {
				BYTE level;
				MakeLine(n++, text, level);
				g_log.AppendLine(text, level);
			}
			::Sleep(10);
		}
	});
}

// ---- Loading a file -----------------------------------------------------------

static void LoadFile(HWND hwnd) {
	wchar_t path[MAX_PATH] = L"";
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = L"Logs and text\0*.log;*.txt\0All files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (!GetOpenFileNameW(&ofn))
		return;

	HANDLE hFile = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return;
	LARGE_INTEGER size{};
	::GetFileSizeEx(hFile, &size);
	std::vector<char> bytes((size_t)std::min<LONGLONG>(size.QuadPart, 512LL << 20));
	DWORD read = 0;
	::ReadFile(hFile, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
	::CloseHandle(hFile);
	bytes.resize(read);

	std::wstring text;
	if (bytes.size() >= 2 && (BYTE)bytes[0] == 0xFF && (BYTE)bytes[1] == 0xFE) {
		text.assign((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
	}
	else {
		size_t skip = bytes.size() >= 3 && (BYTE)bytes[0] == 0xEF && (BYTE)bytes[1] == 0xBB && (BYTE)bytes[2] == 0xBF ? 3 : 0;
		int n = ::MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), nullptr, 0);
		text.resize(n);
		::MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, (int)(bytes.size() - skip), text.data(), n);
	}
	StopStream();
	g_log.Clear();
	auto start = ::GetTickCount64();
	g_log.AppendText(text.c_str(), (int)text.size());
	CString msg;
	msg.Format(L"Loaded %zu lines in %llu ms", g_log.GetLineCount(), ::GetTickCount64() - start);
	SetEventText(msg);
}

// ---- Colors -------------------------------------------------------------------

static bool g_dark = false;

static void ApplyColors() {
	if (!g_dark) {
		g_log.SetColors(nullptr);
		return;
	}
	LGVCOLORS c;
	c.crBack = RGB(30, 30, 30);
	c.crText = RGB(220, 220, 220);
	c.crGutterBack = RGB(37, 37, 38);
	c.crGutterText = RGB(110, 118, 129);
	c.crSelBack = RGB(38, 79, 120);
	c.crSelText = RGB(255, 255, 255);
	c.crSelBackInactive = RGB(58, 61, 65);
	c.crSelTextInactive = RGB(220, 220, 220);
	c.crMatchBack = RGB(0x61, 0x51, 0x1E);
	c.crMatchText = RGB(240, 240, 240);
	g_log.SetColors(&c);
}

// ---- Filter -------------------------------------------------------------------

static void ApplyFilter() {
	wchar_t text[256] = L"";
	::GetWindowTextW(g_filterEdit, text, (int)std::size(text));
	LGVFILTER f;
	f.pszText = text;
	f.flags = g_filterFlags;
	if (g_warningsOnly)
		f.levelMask = (1u << LGVL_WARNING) | (1u << LGVL_ERROR) | (1u << LGVL_CRITICAL);
	if (!g_log.SetFilter(&f))
		SetEventText(L"Bad regex");
}

static void ApplySearch() {
	wchar_t text[256] = L"";
	::GetWindowTextW(g_searchEdit, text, (int)std::size(text));
	LGVSEARCH search;
	search.pszText = text;
	search.flags = g_searchFlags;
	if (!g_log.SetSearch(&search))
		SetEventText(L"Bad search regex");
}

static void ToggleRule(LPCWSTR text, UINT flags, BYTE style) {
	// a rule with the same text goes, else it's added
	static std::vector<std::pair<CString, int>> added;
	for (size_t i = 0; i < added.size(); i++) {
		if (added[i].first == text) {
			g_log.RemoveRule(added[i].second);
			for (auto& a : added)
				if (a.second > added[i].second)
					a.second--;
			added.erase(added.begin() + i);
			return;
		}
	}
	LGVRULE r;
	r.pszText = text;
	r.flags = flags;
	r.style = style;
	added.push_back({ text, g_log.AddRule(&r) });
}

// ---- Commands ---------------------------------------------------------------

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_FILE_STREAM, g_stream.joinable());
	check(ID_VIEW_FOLLOW, g_log.IsFollowingTail());
	check(ID_VIEW_LINENUMBERS, g_log.AreLineNumbersShown());
	check(ID_VIEW_DARK, g_dark);
	check(ID_VIEW_TABS8, g_log.GetTabSize() == 8);
	check(ID_VIEW_LIMIT, g_log.GetLineLimit() != 0);
	check(ID_VIEW_VIRTUAL, g_log.IsVirtual());
	check(ID_FILTER_REGEX, g_filterFlags & LGVFF_REGEX);
	check(ID_FILTER_MATCHCASE, g_filterFlags & LGVFF_MATCHCASE);
	check(ID_FILTER_EXCLUDE, g_filterFlags & LGVFF_EXCLUDE);
	check(ID_FILTER_WARNINGS, g_warningsOnly);
	check(ID_FILTER_WHOLEWORD, g_filterFlags & LGVFF_WHOLEWORD);
	check(ID_SEARCH_MATCHCASE, g_searchFlags & LGVFF_MATCHCASE);
	check(ID_SEARCH_REGEX, g_searchFlags & LGVFF_REGEX);
	check(ID_SEARCH_WHOLEWORD, g_searchFlags & LGVFF_WHOLEWORD);
}

static void UpdateStatus() {
	CString text;
	auto caret = g_log.GetCaret();
	text.Format(L"%zu lines%s%s | top %zu | Ln %zu, Col %zu", g_log.GetLineCount(), g_log.IsVirtual() ? L" (virtual)" : L"",
		g_log.IsFollowingTail() ? L" | following" : L"", g_log.GetTopLine(), caret.line + 1, caret.column + 1);
	if (g_log.IsFiltered())
		text.AppendFormat(L" | %zu shown%s", g_log.GetShownLineCount(), g_log.IsFilterPending() ? L" (filtering...)" : L"");
	if (g_log.HasSelection()) {
		LGVPOS start, end;
		g_log.GetSelection(&start, &end);
		// a selection ending at the start of a line doesn't take any of it
		size_t lines = end.line - start.line + (end.column > 0 || end.line == start.line ? 1 : 0);
		text.AppendFormat(L" | selected %zu line(s)", lines);
	}
	text.AppendFormat(L" | %s", (PCWSTR)g_event);
	::SendMessage(g_status, SB_SETTEXT, 0, (LPARAM)(PCWSTR)text);
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_FILE_ADD:
			AddLines(1000);
			break;
		case ID_FILE_ADDMILLION: {
			auto start = ::GetTickCount64();
			g_log.BeginUpdate();
			for (int i = 0; i < 1000; i++)
				AddLines(1000);
			g_log.EndUpdate();
			CString msg;
			msg.Format(L"1,000,000 lines in %llu ms", ::GetTickCount64() - start);
			SetEventText(msg);
			break;
		}
		case ID_FILE_STREAM:
			if (g_stream.joinable())
				StopStream();
			else if (!g_log.IsVirtual())
				StartStream();
			break;
		case ID_FILE_LOAD:
			LoadFile(hwnd);
			break;
		case ID_FILE_CLEAR:
			g_log.Clear();
			break;
		case ID_FILE_EXIT:
			PostMessage(hwnd, WM_CLOSE, 0, 0);
			break;
		case ID_VIEW_FOLLOW:
			g_log.SetFollowTail(!g_log.IsFollowingTail());
			break;
		case ID_VIEW_LINENUMBERS:
			g_log.ShowLineNumbers(!g_log.AreLineNumbersShown());
			break;
		case ID_VIEW_DARK:
			g_dark = !g_dark;
			ApplyColors();
			break;
		case ID_VIEW_TABS8:
			g_log.SetTabSize(g_log.GetTabSize() == 8 ? 4 : 8);
			break;
		case ID_VIEW_LIMIT:
			g_log.SetLineLimit(g_log.GetLineLimit() ? 0 : 100'000);
			break;
		case ID_VIEW_VIRTUAL:
			if (g_log.IsVirtual()) {
				g_log.Clear();
			}
			else {
				StopStream();
				g_log.SetVirtualLineCount(VIRTUAL_LINES);
			}
			break;
		case ID_FILTER_FOCUS:
			::SetFocus(g_filterEdit);
			::SendMessage(g_filterEdit, EM_SETSEL, 0, -1);
			break;
		case ID_FILTER_REGEX:
			g_filterFlags ^= LGVFF_REGEX;
			ApplyFilter();
			break;
		case ID_FILTER_MATCHCASE:
			g_filterFlags ^= LGVFF_MATCHCASE;
			ApplyFilter();
			break;
		case ID_FILTER_EXCLUDE:
			g_filterFlags ^= LGVFF_EXCLUDE;
			ApplyFilter();
			break;
		case ID_FILTER_WHOLEWORD:
			g_filterFlags ^= LGVFF_WHOLEWORD;
			ApplyFilter();
			break;
		case ID_SEARCH_FOCUS:
			::SetFocus(g_searchEdit);
			::SendMessage(g_searchEdit, EM_SETSEL, 0, -1);
			break;
		case ID_SEARCH_NEXT:
		case ID_SEARCH_PREV:
			if (!g_log.FindNext(id == ID_SEARCH_NEXT))
				SetEventText(L"Nothing to search for");
			break;
		case ID_SEARCH_MATCHCASE:
			g_searchFlags ^= LGVFF_MATCHCASE;
			ApplySearch();
			break;
		case ID_SEARCH_REGEX:
			g_searchFlags ^= LGVFF_REGEX;
			ApplySearch();
			break;
		case ID_SEARCH_WHOLEWORD:
			g_searchFlags ^= LGVFF_WHOLEWORD;
			ApplySearch();
			break;
		case ID_FILTER_WARNINGS:
			g_warningsOnly = !g_warningsOnly;
			ApplyFilter();
			break;
		case ID_FILTER_CLEAR:
			g_warningsOnly = false;
			::SetWindowTextW(g_filterEdit, L"");   // EN_CHANGE applies it
			ApplyFilter();
			break;
		case ID_RULES_WORKER3:
			ToggleRule(L"worker 3", LGVRF_MATCHONLY, STYLE_HIGHLIGHT);
			break;
		case ID_RULES_CACHEMISS:
			ToggleRule(L"cache miss", 0, STYLE_CACHE);
			break;
		case ID_RULES_NUMBERS:
			ToggleRule(L"\\b\\d+$", LGVFF_REGEX | LGVRF_MATCHONLY, STYLE_NUMBER);
			break;
		case ID_RULES_CLEAR:
			g_log.ClearRules();
			break;
		case ID_LINE_COPY:
			g_log.Copy();
			break;
		case ID_LINE_MARK:
			if (g_menuLine != LGV_NOLINE) {
				LGVLINEINFO info;
				g_log.GetLineInfo(g_menuLine, &info);
				g_log.SetLineStyle(g_menuLine, info.level, info.style == STYLE_MARKED ? 0 : STYLE_MARKED);
			}
			break;
	}
	UpdateChecks(GetMenu(hwnd));
	UpdateStatus();
}

static LRESULT OnNotify(HWND hwnd, NMHDR* hdr) {
	if (hdr->idFrom != IDC_LOG)
		return 0;
	CString text;
	switch (hdr->code) {
		case LGVN_GETDISPINFO: {
			auto nm = (NMLGVDISPINFO*)hdr;
			static CString line;   // has to last until the notification returns
			MakeLine(nm->line, line, nm->level);
			nm->pszText = line;
			nm->cchText = line.GetLength();
			break;
		}
		case LGVN_SELCHANGED:
			UpdateStatus();
			break;
		case LGVN_SEARCHDONE: {
			auto nm = (NMLGVSEARCH*)hdr;
			if (nm->found) {
				LGVLINEINFO info;
				g_log.GetLineInfo(nm->line, &info);
				text.Format(L"Found on line %llu%s", info.number, nm->wrapped ? L" (wrapped)" : L"");
				SetEventText(text);
			}
			else {
				SetEventText(L"Not found");
				::MessageBeep(MB_ICONASTERISK);
			}
			UpdateStatus();
			break;
		}
		case LGVN_FILTERDONE:
			SetEventText(L"Filter done");
			UpdateStatus();
			break;
		case LGVN_FOLLOWCHANGED:
			SetEventText(g_log.IsFollowingTail() ? L"Following the tail" : L"Stopped following");
			UpdateChecks(GetMenu(hwnd));
			UpdateStatus();
			break;
		case LGVN_ACTIVATE: {
			auto nm = (NMLGVLINE*)hdr;
			LGVLINEINFO info;
			g_log.GetLineInfo(nm->line, &info);
			text.Format(L"Activated line %llu (level %u)", info.number, info.level);
			SetEventText(text);
			UpdateStatus();
			break;
		}
		case LGVN_CONTEXTMENU: {
			auto nm = (NMLGVLINE*)hdr;
			g_menuLine = nm->line;
			HMENU menu = CreatePopupMenu();
			UINT state = nm->line != LGV_NOLINE ? MF_STRING : MF_STRING | MF_GRAYED;
			AppendMenuW(menu, state, ID_LINE_COPY, g_log.HasSelection() ? L"&Copy\tCtrl+C" : L"&Copy Line\tCtrl+C");
			AppendMenuW(menu, state | (g_log.IsVirtual() ? MF_GRAYED : 0), ID_LINE_MARK, L"Toggle &Mark");
			AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenuW(menu, MF_STRING, ID_FILE_CLEAR, L"C&lear");
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
	int filterHeight = ::MulDiv(24, ::GetDpiForWindow(hwnd), 96);
	::SetWindowPos(g_filterEdit, nullptr, 0, 0, rc.right / 2, filterHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_searchEdit, nullptr, rc.right / 2, 0, rc.right - rc.right / 2, filterHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	g_log.SetWindowPos(nullptr, 0, filterHeight, rc.right, rc.bottom - (rcStatus.bottom - rcStatus.top) - filterHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_status = CreateWindowExW(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
				0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, _Module.GetModuleInstance(), nullptr);
			LGVSTYLE marked;
			marked.crBack = RGB(0xFF, 0xF2, 0x9E);
			marked.crText = RGB(0, 0, 0);
			marked.crMarker = RGB(0x30, 0x80, 0xE0);
			g_log.SetStyle(STYLE_MARKED, &marked);
			LGVSTYLE highlight;
			highlight.crBack = RGB(0xFF, 0xE0, 0x40);
			highlight.crText = RGB(0, 0, 0);
			g_log.SetStyle(STYLE_HIGHLIGHT, &highlight);
			LGVSTYLE cache;
			cache.crText = RGB(0x20, 0x70, 0xD0);
			cache.flags = LGVSF_ITALIC;
			g_log.SetStyle(STYLE_CACHE, &cache);
			LGVSTYLE number;
			number.crText = RGB(0x10, 0xA0, 0x60);
			g_log.SetStyle(STYLE_NUMBER, &number);
			g_filterEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				0, 0, 0, 0, hwnd, (HMENU)IDC_FILTER, _Module.GetModuleInstance(), nullptr);
			::SendMessage(g_filterEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter (Ctrl+F)");
			g_searchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				0, 0, 0, 0, hwnd, (HMENU)IDC_SEARCH, _Module.GetModuleInstance(), nullptr);
			::SendMessage(g_searchEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search (Ctrl+E, Enter/F3, Shift+F3)");
			{
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
				static HFONT font = ::CreateFontIndirectW(&ncm.lfMessageFont);
				::SendMessage(g_filterEdit, WM_SETFONT, (WPARAM)font, FALSE);
				::SendMessage(g_searchEdit, WM_SETFONT, (WPARAM)font, FALSE);
			}
			// appended before the window exists: taken in when it's created
			g_log.AppendLine(L"LogView demo. File > Start Stream appends from another thread; View > Virtual Mode shows 10 million lines.");
			g_log.AppendLine(L"A tab\there, and\ttwo more\tstops.", LGVL_INFO);
			g_log.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
				WS_EX_CLIENTEDGE, IDC_LOG);
			AddLines(200);
			g_log.SetFocus();
			SetTimer(hwnd, 1, 250, nullptr);
			return 0;
		}

		case WM_TIMER:
			UpdateStatus();
			return 0;

		case WM_SIZE:
			LayoutChildren(hwnd);
			return 0;

		case WM_SETFOCUS:
			g_log.SetFocus();
			return 0;

		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}

		case WM_NOTIFY:
			return OnNotify(hwnd, (NMHDR*)lParam);

		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_FILTER) {
				if (HIWORD(wParam) == EN_CHANGE)
					ApplyFilter();
				return 0;
			}
			if (LOWORD(wParam) == IDC_SEARCH) {
				if (HIWORD(wParam) == EN_CHANGE)
					ApplySearch();
				return 0;
			}
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_CLOSE:
			StopStream();   // before the view goes
			break;

		case WM_DESTROY:
			StopStream();
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
	wc.lpszClassName = L"LogViewDemoWnd";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	RegisterClassExW(&wc);

	HMENU hFile = CreatePopupMenu();
	AppendMenuW(hFile, MF_STRING, ID_FILE_ADD, L"&Add 1,000 Lines\tF5");
	AppendMenuW(hFile, MF_STRING, ID_FILE_ADDMILLION, L"Add 1,000,000 &Lines\tCtrl+M");
	AppendMenuW(hFile, MF_STRING, ID_FILE_STREAM, L"&Stream from a Thread\tCtrl+S");
	AppendMenuW(hFile, MF_STRING, ID_FILE_LOAD, L"&Open File...\tCtrl+O");
	AppendMenuW(hFile, MF_STRING, ID_FILE_CLEAR, L"&Clear");
	AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hView = CreatePopupMenu();
	AppendMenuW(hView, MF_STRING, ID_VIEW_FOLLOW, L"&Follow Tail\tCtrl+T");
	AppendMenuW(hView, MF_STRING, ID_VIEW_LINENUMBERS, L"&Line Numbers");
	AppendMenuW(hView, MF_STRING, ID_VIEW_DARK, L"Dar&k Colors");
	AppendMenuW(hView, MF_STRING, ID_VIEW_TABS8, L"&Tab Size 8");
	AppendMenuW(hView, MF_STRING, ID_VIEW_LIMIT, L"Line Li&mit 100,000");
	AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hView, MF_STRING, ID_VIEW_VIRTUAL, L"&Virtual Mode (10M lines)");

	HMENU hFilter = CreatePopupMenu();
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_FOCUS, L"&Edit Filter\tCtrl+F");
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_REGEX, L"&Regex");
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_MATCHCASE, L"Match &Case");
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_EXCLUDE, L"E&xclude Matches");
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_WHOLEWORD, L"W&hole Word");
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_WARNINGS, L"&Warnings and Errors Only");
	AppendMenuW(hFilter, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hFilter, MF_STRING, ID_FILTER_CLEAR, L"C&lear Filter");

	HMENU hRules = CreatePopupMenu();
	AppendMenuW(hRules, MF_STRING, ID_RULES_WORKER3, L"Highlight \"worker 3\" (match only)");
	AppendMenuW(hRules, MF_STRING, ID_RULES_CACHEMISS, L"Style Lines with \"cache miss\"");
	AppendMenuW(hRules, MF_STRING, ID_RULES_NUMBERS, L"Color the Last Number (regex, match only)");
	AppendMenuW(hRules, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hRules, MF_STRING, ID_RULES_CLEAR, L"&Clear Rules");

	HMENU hSearch = CreatePopupMenu();
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_FOCUS, L"&Edit Search\tCtrl+E");
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_NEXT, L"Find &Next\tF3");
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_PREV, L"Find &Previous\tShift+F3");
	AppendMenuW(hSearch, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_MATCHCASE, L"Match &Case");
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_REGEX, L"&Regex");
	AppendMenuW(hSearch, MF_STRING, ID_SEARCH_WHOLEWORD, L"&Whole Word");

	HMENU hMenu = CreateMenu();
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFilter, L"F&ilter");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSearch, L"&Search");
	AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hRules, L"&Rules");

	HWND hwnd = CreateWindowExW(0, L"LogViewDemoWnd", L"Log View Demo", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);

	ACCEL accels[] = {
		{ FVIRTKEY, VK_F5, ID_FILE_ADD },
		{ FVIRTKEY | FCONTROL, 'M', ID_FILE_ADDMILLION },
		{ FVIRTKEY | FCONTROL, 'S', ID_FILE_STREAM },
		{ FVIRTKEY | FCONTROL, 'O', ID_FILE_LOAD },
		{ FVIRTKEY | FCONTROL, 'T', ID_VIEW_FOLLOW },
		{ FVIRTKEY | FCONTROL, 'F', ID_FILTER_FOCUS },
		{ FVIRTKEY | FCONTROL, 'E', ID_SEARCH_FOCUS },
		{ FVIRTKEY, VK_F3, ID_SEARCH_NEXT },
		{ FVIRTKEY | FSHIFT, VK_F3, ID_SEARCH_PREV },
	};
	HACCEL hAccel = CreateAcceleratorTable(accels, (int)std::size(accels));

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		// Enter in the search box finds the next match
		if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == g_searchEdit) {
			OnCommand(hwnd, ::GetKeyState(VK_SHIFT) < 0 ? ID_SEARCH_PREV : ID_SEARCH_NEXT);
			continue;
		}
		if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return (int)msg.wParam;
}
