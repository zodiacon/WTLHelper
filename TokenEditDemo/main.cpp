// TokenEditDemo: a message's recipients and tags. "To" takes addresses: a name from the address book becomes
// "Name <address>" with its icon, an address is kept, anything else is marked invalid. "Tags" makes each tag lower case
// in a color of its own, with no duplicates (and refuses "spam" if Edit says so). Type, then a comma, a semicolon,
// Enter or Tab to make a token; paste lines or a list; click tokens to select them (Ctrl, Shift), Delete or Backspace
// removes them, as does their close button. The boxes grow to four lines, then scroll. The log at the bottom shows the
// notifications.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "TokenEdit.h"

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
	IDC_TO = 100, IDC_TAGS, IDC_LOG, IDC_TOLABEL, IDC_TAGSLABEL, IDC_SUBJECT,
	CMD_EXIT = 1000, CMD_DARKMODE, CMD_LARGEFONT, CMD_CLEARLOG, CMD_READONLY, CMD_NOCLOSE, CMD_REFUSESPAM, CMD_MAXTAGS,
	CMD_ADDLOCKED, CMD_SETTEXT, CMD_REMOVEALL, CMD_SHOWTEXT, CMD_INVALIDATE,
};

static CTokenEditCtrl g_to, g_tags;
static HWND g_main, g_log, g_toLabel, g_tagsLabel, g_subject;
static HMENU g_menu;
static HFONT g_font, g_largeFont;
static HIMAGELIST g_images;

struct Contact {
	const wchar_t* name;
	const wchar_t* address;
	int image;
};

static const Contact Contacts[] = {
	{ L"Ada Lovelace", L"ada@example.com", 0 },
	{ L"Alan Turing", L"alan@example.com", 1 },
	{ L"Grace Hopper", L"grace@example.com", 2 },
	{ L"Edsger Dijkstra", L"edsger@example.com", 0 },
	{ L"Barbara Liskov", L"barbara@example.com", 1 },
	{ L"Donald Knuth", L"don@example.com", 2 },
	{ L"Margaret Hamilton", L"margaret@example.com", 0 },
	{ L"Dennis Ritchie", L"dmr@example.com", 1 },
	{ L"Ken Thompson", L"ken@example.com", 2 },
	{ L"Frances Allen", L"fran@example.com", 0 },
};

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

static bool IsChecked(UINT id) {
	return (::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED) != 0;
}

static const wchar_t* Name(HWND hwnd) {
	return hwnd == g_to.m_hWnd ? L"to" : L"tags";
}

// people: colored circles, for the DPI
static void MakeImages() {
	const int size = Scale(16);
	if (g_images)
		::ImageList_Destroy(g_images);
	g_images = ::ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 3, 0);
	const COLORREF colors[] = { RGB(0x2E, 0x7D, 0xD1), RGB(0x2E, 0xA0, 0x5A), RGB(0xC8, 0x5A, 0x20) };
	HDC screen = ::GetDC(nullptr);
	for (auto color : colors) {
		HDC dc = ::CreateCompatibleDC(screen);
		HBITMAP bmp = ::CreateCompatibleBitmap(screen, size, size);
		auto old = ::SelectObject(dc, bmp);
		RECT rc{ 0, 0, size, size };
		HBRUSH magenta = ::CreateSolidBrush(RGB(255, 0, 255));
		::FillRect(dc, &rc, magenta);
		::DeleteObject(magenta);
		HBRUSH brush = ::CreateSolidBrush(color);
		auto oldBrush = ::SelectObject(dc, brush);
		auto oldPen = ::SelectObject(dc, ::GetStockObject(NULL_PEN));
		::Ellipse(dc, 1, 1, size, size);
		// a head and shoulders in white
		HBRUSH white = (HBRUSH)::GetStockObject(WHITE_BRUSH);
		::SelectObject(dc, white);
		const int c = size / 2, h = size / 6;
		::Ellipse(dc, c - h, size / 4, c + h + 1, size / 4 + 2 * h + 1);
		::Pie(dc, c - 2 * h, size * 5 / 8, c + 2 * h + 1, size + 2 * h, c + 2 * h, size * 7 / 8, c - 2 * h, size * 7 / 8);
		::SelectObject(dc, oldPen);
		::SelectObject(dc, oldBrush);
		::DeleteObject(brush);
		::SelectObject(dc, old);
		::DeleteDC(dc);
		::ImageList_AddMasked(g_images, bmp, RGB(255, 0, 255));
		::DeleteObject(bmp);
	}
	::ReleaseDC(nullptr, screen);
	if (g_to.IsWindow())
		g_to.SetImageList(g_images);
}

// ---- The window ---------------------------------------------------------------------------------------

static void Layout() {
	if (!g_to.IsWindow() || !g_tags.IsWindow())
		return;   // SetMenu in WM_CREATE sizes the window before the boxes are made
	RECT rc;
	::GetClientRect(g_main, &rc);
	const int pad = Scale(10), label = Scale(60), gap = Scale(8);
	const int x = pad + label, width = std::max(0, (int)rc.right - x - pad);
	int y = pad;
	auto row = [&](HWND labelWnd, HWND box, int height) {
		::SetWindowPos(labelWnd, nullptr, pad, y + Scale(5), label - Scale(6), Scale(20), SWP_NOZORDER | SWP_NOACTIVATE);
		::SetWindowPos(box, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
		y += height + gap;
	};
	// the width first: the ideal height is for the lines at the box's width
	g_to.SetWindowPos(nullptr, 0, 0, width, g_to.GetIdealHeight(), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	row(g_toLabel, g_to, g_to.GetIdealHeight());
	g_tags.SetWindowPos(nullptr, 0, 0, width, g_tags.GetIdealHeight(), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	row(g_tagsLabel, g_tags, g_tags.GetIdealHeight());
	::SetWindowPos(g_subject, nullptr, x, y, width, Scale(24), SWP_NOZORDER | SWP_NOACTIVATE);
	y += Scale(24) + gap;
	::SetWindowPos(g_log, nullptr, 0, y, rc.right, std::max(0, (int)rc.bottom - y), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts() {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
	if (g_font)
		::DeleteObject(g_font);
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	for (HWND hwnd : { g_log, g_toLabel, g_tagsLabel, g_subject })
		::SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);
	if (g_largeFont) {
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_to.SetFont(g_largeFont);
		g_tags.SetFont(g_largeFont);
	}
}

static HMENU BuildMenu() {
	HMENU file = ::CreatePopupMenu();
	::AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
	HMENU edit = ::CreatePopupMenu();
	::AppendMenuW(edit, MF_STRING, CMD_READONLY, L"Read &Only");
	::AppendMenuW(edit, MF_STRING, CMD_NOCLOSE, L"No &Close Buttons");
	::AppendMenuW(edit, MF_STRING, CMD_REFUSESPAM, L"Refuse the Tag \"&spam\"");
	::AppendMenuW(edit, MF_STRING, CMD_MAXTAGS, L"At Most &5 Tags");
	HMENU tokens = ::CreatePopupMenu();
	::AppendMenuW(tokens, MF_STRING, CMD_ADDLOCKED, L"Add a &Locked Recipient");
	::AppendMenuW(tokens, MF_STRING, CMD_SETTEXT, L"&Set Recipients from Text");
	::AppendMenuW(tokens, MF_STRING, CMD_INVALIDATE, L"Mark the Selected &Invalid");
	::AppendMenuW(tokens, MF_STRING, CMD_SHOWTEXT, L"Log the &Text");
	::AppendMenuW(tokens, MF_STRING, CMD_REMOVEALL, L"&Remove All");
	HMENU view = ::CreatePopupMenu();
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_DARKMODE, L"Dar&k Mode");
#endif
	::AppendMenuW(view, MF_STRING, CMD_LARGEFONT, L"&Larger Font");
	::AppendMenuW(view, MF_STRING, CMD_CLEARLOG, L"Clear L&og");
	HMENU menu = ::CreateMenu();
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)edit, L"&Edit");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)tokens, L"&Tokens");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"&View");
	return menu;
}

static void SetStyle(DWORD style, bool on) {
	for (auto box : { &g_to, &g_tags }) {
		if (on)
			box->ModifyStyle(0, style);
		else
			box->ModifyStyle(style, 0);
	}
}

static void OnCommand(UINT id) {
	switch (id) {
		case CMD_EXIT: ::DestroyWindow(g_main); break;
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
				g_to.SetFont(g_largeFont);
				g_tags.SetFont(g_largeFont);
			}
			else {
				g_to.SetFont(nullptr);   // the default again
				g_tags.SetFont(nullptr);
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		case CMD_CLEARLOG: ::SendMessageW(g_log, LB_RESETCONTENT, 0, 0); break;
		case CMD_READONLY: SetStyle(TKES_READONLY, Toggle(id)); break;
		case CMD_NOCLOSE: SetStyle(TKES_NOCLOSE, Toggle(id)); break;
		case CMD_REFUSESPAM: Toggle(id); break;
		case CMD_MAXTAGS: g_tags.SetMaxTokens(Toggle(id) ? 5 : 0); break;
		case CMD_ADDLOCKED: {
			TKETOKEN t;
			t.pszText = L"Me <me@example.com>";
			t.pszTip = L"You're always on it";
			t.iImage = 1;
			t.flags = TKEF_LOCKED;
			Log(L"InsertToken: id %u", g_to.InsertToken(&t, 0));
			break;
		}
		case CMD_SETTEXT:
			g_to.SetText(L"Ada Lovelace <ada@example.com>; \"Turing, Alan\" <alan@example.com>; grace@example.com; nobody");
			break;
		case CMD_INVALIDATE: {
			UINT ids[64];
			int n = std::min(g_to.GetSelection(ids, 64), 64);
			for (int i = 0; i < n; i++) {
				TKETOKEN t;
				t.mask = TKETF_FLAGS;
				t.id = ids[i];
				t.flags = TKEF_INVALID | TKEF_SELECTED;
				g_to.SetToken(&t);
			}
			break;
		}
		case CMD_SHOWTEXT:
			for (auto box : { &g_to, &g_tags }) {
				std::wstring text(box->GetText(nullptr, 0), L'\0');
				box->GetText(text.data(), (int)text.size());
				Log(L"%s: %s", Name(*box), text.c_str());
			}
			break;
		case CMD_REMOVEALL:
			g_to.RemoveAll();
			g_tags.RemoveAll();
			break;
	}
}

// the recipients: names from the address book get their address and icon, addresses are kept, the rest is invalid
static LRESULT ToAdding(NMTKETOKEN* nm) {
	static std::wstring rewritten;
	std::wstring text = nm->pszText;
	for (auto& c : Contacts) {
		if (::CompareStringOrdinal(text.c_str(), -1, c.name, -1, TRUE) == CSTR_EQUAL ||
			::CompareStringOrdinal(text.c_str(), -1, c.address, -1, TRUE) == CSTR_EQUAL) {
			rewritten = std::wstring(c.name) + L" <" + c.address + L">";
			nm->pszText = rewritten.c_str();
			nm->iImage = c.image;
			nm->pszTip = c.address;
			return 0;
		}
	}
	const auto at = text.find(L'@');
	if (at == std::wstring::npos || at == 0 || text.find(L'.', at) == std::wstring::npos) {
		nm->flags |= TKEF_INVALID;
		nm->pszTip = L"Not an address";
		Log(L"  marked invalid");
	}
	return 0;
}

// the tags: lower case, a color each
static LRESULT TagsAdding(NMTKETOKEN* nm) {
	static std::wstring rewritten;
	rewritten = nm->pszText;
	std::transform(rewritten.begin(), rewritten.end(), rewritten.begin(), towlower);
	if (IsChecked(CMD_REFUSESPAM) && rewritten == L"spam") {
		Log(L"  refused: spam");
		return 1;
	}
	nm->pszText = rewritten.c_str();
	static const COLORREF colors[] = { RGB(0xDB, 0xEA, 0xFE), RGB(0xDC, 0xFC, 0xE7), RGB(0xFE, 0xF3, 0xC7), RGB(0xFC, 0xE7, 0xF3),
		RGB(0xED, 0xE9, 0xFE), RGB(0xFF, 0xE4, 0xE6) };
	size_t hash = std::hash<std::wstring>{}(rewritten);
	nm->crBack = colors[hash % std::size(colors)];
	return 0;
}

static LRESULT OnNotify(NMHDR* hdr) {
	if (hdr->hwndFrom != g_to.m_hWnd && hdr->hwndFrom != g_tags.m_hWnd)
		return 0;
	auto nm = (NMTKETOKEN*)hdr;
	static const wchar_t* const Sources[] = { L"typed", L"focus", L"pasted", L"commit" };
	const wchar_t* box = Name(hdr->hwndFrom);
	switch (hdr->code) {
		case TKEN_TOKENADDING:
			Log(L"%s: TKEN_TOKENADDING \"%s\" at %d (%s)", box, nm->pszText, nm->index, Sources[std::min(nm->source, 3u)]);
			return hdr->hwndFrom == g_to.m_hWnd ? ToAdding(nm) : TagsAdding(nm);
		case TKEN_TOKENADDED: Log(L"%s: TKEN_TOKENADDED %u \"%s\"", box, nm->id, nm->pszText); break;
		case TKEN_TOKENREMOVING: Log(L"%s: TKEN_TOKENREMOVING %u \"%s\"", box, nm->id, nm->pszText); break;
		case TKEN_TOKENREMOVED: Log(L"%s: TKEN_TOKENREMOVED %u \"%s\"", box, nm->id, nm->pszText); break;
		case TKEN_CHANGED: Log(L"%s: TKEN_CHANGED (%d tokens)", box, ((CTokenEditCtrl*)(hdr->hwndFrom == g_to.m_hWnd ? &g_to : &g_tags))->GetTokenCount()); break;
		case TKEN_SELCHANGED: {
			auto& ctl = hdr->hwndFrom == g_to.m_hWnd ? g_to : g_tags;
			Log(L"%s: TKEN_SELCHANGED (%d selected)", box, ctl.GetSelectedCount());
			break;
		}
		case TKEN_HEIGHTCHANGED:
			Log(L"%s: TKEN_HEIGHTCHANGED %d", box, (hdr->hwndFrom == g_to.m_hWnd ? g_to : g_tags).GetIdealHeight());
			Layout();
			break;
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
			g_toLabel = ::CreateWindowExW(0, WC_STATICW, L"&To:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_TOLABEL, nullptr, nullptr);
			g_to.Create(hwnd, nullptr, L"Recipients", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, IDC_TO);
			g_tagsLabel = ::CreateWindowExW(0, WC_STATICW, L"T&ags:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, (HMENU)IDC_TAGSLABEL, nullptr, nullptr);
			g_tags.Create(hwnd, nullptr, L"Tags", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TKES_NODUPLICATES, 0, IDC_TAGS);
			g_subject = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"Quarterly review", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				0, 0, 0, 0, hwnd, (HMENU)IDC_SUBJECT, nullptr, nullptr);
			g_to.SetCueBanner(L"Names or addresses");
			g_tags.SetCueBanner(L"Add tags");
			g_tags.SetSeparators(L",; ");
			MakeImages();
			UpdateFonts();
			g_to.AddToken(L"Ada Lovelace <ada@example.com>", 0);
			g_to.AddToken(L"Alan Turing <alan@example.com>", 1);
			for (auto tag : { L"review", L"finance", L"q3" }) {
				TKETOKEN t;
				t.pszText = tag;
				t.crBack = RGB(0xDB, 0xEA, 0xFE);
				g_tags.InsertToken(&t);
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
		case WM_CTLCOLORSTATIC:
			// the labels on the window's color (GetSysColor follows WTLHelper's dark mode)
			::SetTextColor((HDC)wParam, ::GetSysColor(COLOR_WINDOWTEXT));
			::SetBkColor((HDC)wParam, ::GetSysColor(COLOR_WINDOW));
			return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			UpdateFonts();
			MakeImages();
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			return 0;
		}
		case WM_ACTIVATE: {
			// the focus kept while another window is active
			static HWND focus = nullptr;
			if (LOWORD(wParam) == WA_INACTIVE)
				focus = ::GetFocus();
			else
				::SetFocus(focus && ::IsChild(hwnd, focus) ? focus : g_to.m_hWnd);
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
	wc.lpszClassName = L"TokenEditDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(WS_EX_CONTROLPARENT, L"TokenEditDemoWnd", L"Token Edit Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, nullptr, nullptr, hInstance, cmdLine);
	::ShowWindow(hwnd, nCmdShow);

	// IsDialogMessage: Tab between the boxes
	MSG msg;
	while (::GetMessageW(&msg, nullptr, 0, 0)) {
		if (::IsDialogMessageW(hwnd, &msg))
			continue;
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}

	if (g_font)
		::DeleteObject(g_font);
	if (g_largeFont)
		::DeleteObject(g_largeFont);
	if (g_images)
		::ImageList_Destroy(g_images);
	_Module.Term();
	return 0;
}
