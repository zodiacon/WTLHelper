#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "MarkdownView.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_VIEW = 101,
	IDC_TOC,

	ID_DEMO_OPEN = 201,
	ID_DEMO_SAMPLE,
	ID_DEMO_LARGE,
	ID_DEMO_RELOAD,
	ID_DEMO_EXIT,

	ID_DEMO_ZOOMIN = 301,
	ID_DEMO_ZOOMOUT,
	ID_DEMO_ZOOMRESET,
	ID_DEMO_CENTERED,
	ID_DEMO_CONTENTS,
	ID_DEMO_DARKMODE,
	ID_DEMO_COLORS,
	ID_DEMO_SERIF,
	ID_DEMO_NOHTML,
	ID_DEMO_HARDBREAKS,
	ID_DEMO_NOEMOJI,
};

// ---- State --------------------------------------------------------------------

static CMarkdownViewCtrl g_view;
static CListBox g_toc;
static HWND g_main;
static CString g_path;
static bool g_showToc = true;
static bool g_customColors;
static bool g_serif;
static HFONT g_tocFont;
static bool g_syncing;     // the list follows the view: not the other way round

// ---- The sample -----------------------------------------------------------------

static const wchar_t g_sample[] = LR"md(---
title: MarkdownView sample
---

<p align="center">
  <img src="logo.png" width="96" height="96" alt="Logo">
</p>

<h1 align="center">MarkdownView</h1>

<p align="center">
  <a href="https://example.com/ci"><img src="build.svg" width="90" height="20" alt="build"></a>
  <a href="https://example.com/license"><img src="license.svg" width="80" height="20" alt="license"></a>
</p>

A **Markdown** viewer for *WTL* apps, drawn with Direct2D and DirectWrite. It shows
CommonMark with GitHub's extensions :tada: and the HTML readmes use.

## Text

Paragraphs wrap to the window. Text can be **bold**, *italic*, ***both***, ~~struck~~,
<u>underlined</u>, `code`, a <kbd>Ctrl</kbd>+<kbd>C</kbd> key, <mark>marked</mark>,
H<sub>2</sub>O and E = mc<sup>2</sup>. Links look like [this one](https://example.com),
autolinks like https://github.com, and entities like &copy; &mdash; &frac12; work.
Emoji come in color: :rocket: :sparkles: :heart: :+1:

A line ending with a backslash\
breaks there, and so does an HTML<br>break.

Math is shown as its source: $e^{i\pi} + 1 = 0$.

## Lists

- First item
- Second item with a longer text that wraps onto the next line when the window is narrow enough to need it
  - Nested item
    - Deeper still
- Third item

1. One
2. Two
   1. Two point one
   2. Two point two
3. Three

- [x] Write the model
- [x] Draw the text
- [ ] Tables and images
- [ ] Selection

A loose list:

- Paragraph one of a loose list.

- Paragraph two, with its own space.

## Quotes and alerts

> A quote is drawn with a bar and in a muted color.
>
> > And quotes nest.

> [!NOTE]
> Useful information that users should know, even when skimming content.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know to achieve their goal.

> [!WARNING]
> Urgent info that needs immediate user attention to avoid problems.

> [!CAUTION]
> Advises about risks or negative outcomes of certain actions.

## Code

```cpp
// a code block keeps its lines: long ones are cut at the edge (they scroll in phase 3)
int main() {
    CMarkdownViewCtrl view;
    view.Create(hWndParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP);
    view.LoadFile(L"README.md");
}
```

    an indented code block
    of two lines

## Details

<details>
<summary>Click to open</summary>

Hidden content: a paragraph, and a list.

- one
- two

</details>

## Tables

| Feature   | Status | Phase |
|-----------|:------:|------:|
| Model     | done   | 1     |
| Text      | done   | 2     |
| Tables    | soon   | 3     |

---

## Footnotes

Footnotes have a number[^1] that links to the note[^long].

[^1]: The first note.
[^long]: A longer note, with *emphasis* and a [link](https://example.com).

## The end

Back to the [top](#markdownview), or to [the lists](#lists).
)md";

static std::wstring LargeSample() {
	std::wstring text = L"# A large document\n\n";
	for (int i = 1; i <= 2000; i++) {
		text += L"## Section " + std::to_wstring(i) + L"\n\n";
		text += L"Paragraph " + std::to_wstring(i) + L" with **bold** and *italic* text, a [link](#section-1) and `code`. "
			L"It goes on for a while so that it wraps over a couple of lines in a normal window, as text does.\n\n";
		if (i % 5 == 0)
			text += L"- an item\n- another item\n- [x] a done task\n\n";
		if (i % 7 == 0)
			text += L"```\nsome code " + std::to_wstring(i) + L"\nand a second line\n```\n\n";
		if (i % 11 == 0)
			text += L"> A quote in section " + std::to_wstring(i) + L".\n\n";
	}
	return text;
}

// ---- Helpers ---------------------------------------------------------------------

static void UpdateTitle() {
	CString title = g_path.IsEmpty() ? CString(L"Sample") : CString(::PathFindFileNameW(g_path));
	CString text;
	text.Format(L"%s - MarkdownView Demo (%d%%)", (LPCWSTR)title, g_view.GetZoom());
	::SetWindowTextW(g_main, text);
}

static void FillToc() {
	g_toc.SetRedraw(FALSE);
	g_toc.ResetContent();
	for (int i = 0; i < g_view.GetHeadingCount(); i++) {
		MDVHEADING h;
		g_view.GetHeading(i, h);
		CString item;
		for (int l = 1; l < h.level; l++)
			item += L"    ";
		item += h.pszText;
		g_toc.AddString(item);
	}
	g_toc.SetRedraw(TRUE);
	g_toc.Invalidate();
}

static void Loaded() {
	FillToc();
	UpdateTitle();
}

static void LoadPath(LPCWSTR path) {
	if (!g_view.LoadFile(path)) {
		CString msg;
		msg.Format(L"Can't open %s.", path);
		::MessageBoxW(g_main, msg, L"MarkdownView Demo", MB_ICONERROR);
		return;
	}
	g_path = path;
	Loaded();
}

static void OpenFile() {
	wchar_t path[MAX_PATH] = L"";
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = g_main;
	ofn.lpstrFilter = L"Markdown (*.md;*.markdown)\0*.md;*.markdown\0Text (*.txt)\0*.txt\0All files\0*.*\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (::GetOpenFileNameW(&ofn))
		LoadPath(path);
}

static void Layout() {
	RECT rc;
	::GetClientRect(g_main, &rc);
	const int toc = g_showToc ? ::MulDiv(240, ::GetDpiForWindow(g_main), 96) : 0;
	g_toc.ShowWindow(g_showToc ? SW_SHOW : SW_HIDE);
	if (g_showToc)
		g_toc.SetWindowPos(nullptr, 0, 0, toc, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	g_view.SetWindowPos(nullptr, toc, 0, rc.right - toc, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

static MDVCOLORS SepiaColors() {
	MDVCOLORS c = g_view.GetColors();
	c.crBack = RGB(0xF6, 0xEE, 0xDC);
	c.crText = c.crHeading = RGB(0x43, 0x34, 0x22);
	c.crMuted = RGB(0x80, 0x6C, 0x52);
	c.crLink = RGB(0xA0, 0x4A, 0x10);
	c.crBorder = RGB(0xD8, 0xC8, 0xA8);
	c.crCodeBack = RGB(0xEC, 0xE0, 0xC6);
	c.crCodeText = RGB(0x5A, 0x3A, 0x1A);
	return c;
}

// ---- Commands ----------------------------------------------------------------------

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_DEMO_CENTERED, (g_view.GetStyle() & MDVS_CENTERED) != 0);
	check(ID_DEMO_CONTENTS, g_showToc);
	check(ID_DEMO_COLORS, g_customColors);
	check(ID_DEMO_SERIF, g_serif);
	const UINT flags = g_view.GetParseFlags();
	check(ID_DEMO_NOHTML, (flags & MDVF_NOHTML) != 0);
	check(ID_DEMO_HARDBREAKS, (flags & MDVF_HARDBREAKS) != 0);
	check(ID_DEMO_NOEMOJI, (flags & MDVF_NOEMOJI) != 0);
	::EnableMenuItem(menu, ID_DEMO_RELOAD, MF_BYCOMMAND | (g_path.IsEmpty() ? MF_GRAYED : MF_ENABLED));
#ifdef DEMO_WTLHELPER
	check(ID_DEMO_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_DEMO_OPEN: OpenFile(); break;
		case ID_DEMO_SAMPLE:
			g_path.Empty();
			g_view.SetText(g_sample);
			Loaded();
			break;
		case ID_DEMO_LARGE: {
			g_path.Empty();
			const std::wstring text = LargeSample();
			g_view.SetText(text.c_str(), text.size());
			Loaded();
			break;
		}
		case ID_DEMO_RELOAD:
			if (!g_path.IsEmpty()) {
				const size_t top = g_view.GetTopOffset();
				LoadPath(g_path);
				g_view.ScrollToOffset(top);
			}
			break;
		case ID_DEMO_EXIT:
			::DestroyWindow(hwnd);
			return;
		case ID_DEMO_ZOOMIN: g_view.SetZoom(g_view.GetZoom() + 10); UpdateTitle(); break;
		case ID_DEMO_ZOOMOUT: g_view.SetZoom(g_view.GetZoom() - 10); UpdateTitle(); break;
		case ID_DEMO_ZOOMRESET: g_view.SetZoom(100); UpdateTitle(); break;
		case ID_DEMO_CENTERED:
			g_view.ModifyStyle(MDVS_CENTERED, ~g_view.GetStyle() & MDVS_CENTERED);
			break;
		case ID_DEMO_CONTENTS:
			g_showToc = !g_showToc;
			Layout();
			break;
		case ID_DEMO_COLORS: {
			g_customColors = !g_customColors;
			MDVCOLORS colors = SepiaColors();
			g_view.SetColors(g_customColors ? &colors : nullptr);
			break;
		}
		case ID_DEMO_SERIF: {
			g_serif = !g_serif;
			MDVSTYLE style{};
			style.mask = MDVSM_FONT | MDVSM_FONTSIZE;
			wcscpy_s(style.szFont, g_serif ? L"Georgia" : L"");
			style.fontSize = g_serif ? 17.0f : 16.0f;
			g_view.SetTextStyle(style);
			break;
		}
		case ID_DEMO_NOHTML: g_view.SetParseFlags(g_view.GetParseFlags() ^ MDVF_NOHTML); FillToc(); break;
		case ID_DEMO_HARDBREAKS: g_view.SetParseFlags(g_view.GetParseFlags() ^ MDVF_HARDBREAKS); break;
		case ID_DEMO_NOEMOJI: g_view.SetParseFlags(g_view.GetParseFlags() ^ MDVF_NOEMOJI); FillToc(); break;
#ifdef DEMO_WTLHELPER
		case ID_DEMO_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
	}
	UpdateChecks(::GetMenu(hwnd));
}

// ---- Main window ------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			g_main = hwnd;
			g_toc.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
				0, IDC_TOC);
			{
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
				g_tocFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
				g_toc.SetFont(g_tocFont);
			}
			g_view.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | MDVS_CENTERED, 0, IDC_VIEW);
			g_view.SetText(g_sample);
			Loaded();
			::DragAcceptFiles(hwnd, TRUE);
			return 0;

		case WM_SIZE:
			Layout();
			return 0;

		case WM_SETFOCUS:
			g_view.SetFocus();
			return 0;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_TOC) {
				if (HIWORD(wParam) == LBN_SELCHANGE && !g_syncing)
					g_view.ScrollToHeading(g_toc.GetCurSel());
				return 0;
			}
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_SCROLLED) {
				// the contents follow the view
				auto nm = (NMMDVSCROLL*)lParam;
				if (nm->heading != g_toc.GetCurSel()) {
					g_syncing = true;
					g_toc.SetCurSel(nm->heading);
					g_syncing = false;
				}
			}
			return 0;
		}

		case WM_DROPFILES: {
			HDROP drop = (HDROP)wParam;
			wchar_t path[MAX_PATH];
			if (::DragQueryFileW(drop, 0, path, MAX_PATH))
				LoadPath(path);
			::DragFinish(drop);
			return 0;
		}

		case WM_DESTROY:
			::DeleteObject(g_tocFont);
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
	wc.lpszClassName = L"MarkdownViewDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_OPEN, L"&Open...\tCtrl+O");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_RELOAD, L"&Reload\tF5");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_SAMPLE, L"&Sample");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_LARGE, L"&Large (2,000 Sections)");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMIN, L"Zoom &In\tCtrl++");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMOUT, L"Zoom &Out\tCtrl+-");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMRESET, L"&Actual Size\tCtrl+0");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_CENTERED, L"&Centered Column");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_CONTENTS, L"C&ontents");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_COLORS, L"&Sepia Colors");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_SERIF, L"Se&rif Font");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_DEMO_DARKMODE, L"&Dark Mode");
#endif
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_NOHTML, L"Raw HTML as &Text");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HARDBREAKS, L"&Hard Line Breaks");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_NOEMOJI, L"&No Emoji Shortcodes");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

	ACCEL accels[] = {
		{ FVIRTKEY | FCONTROL, 'O', ID_DEMO_OPEN },
		{ FVIRTKEY, VK_F5, ID_DEMO_RELOAD },
	};
	HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)std::size(accels));

	HWND hwnd = ::CreateWindowExW(0, L"MarkdownViewDemoWnd", L"MarkdownView Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 860, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);

	int argc = 0;
	if (auto argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
		if (argc >= 2)
			LoadPath(argv[1]);
		::LocalFree(argv);
	}
	::ShowWindow(hwnd, nCmdShow);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (!::TranslateAccelerator(hwnd, hAccel, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return 0;
}
