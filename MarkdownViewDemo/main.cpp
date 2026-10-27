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

#include <cwctype>
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
	IDC_STATUS,

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
	ID_DEMO_HIGHLIGHTER,

	ID_DEMO_COPY = 401,
	ID_DEMO_COPYMARKDOWN,
	ID_DEMO_COPYLINK,
	ID_DEMO_SELECTALL,
	ID_DEMO_FIND,
	ID_DEMO_FINDNEXT,
	ID_DEMO_FINDPREV,
	ID_DEMO_HIGHLIGHTALL,
};

// ---- State --------------------------------------------------------------------

static CMarkdownViewCtrl g_view;
static CListBox g_toc;
static CStatusBarCtrl g_status;
static HWND g_main;
static CString g_path;
static bool g_showToc = true;
static bool g_customColors;
static bool g_serif;
static bool g_appHighlighter;
static bool g_highlightAll;
static CString g_contextLink;         // the link the context menu was asked on

// the system's Find dialog: modeless, so the message loop passes it its messages
static const UINT g_findMessage = ::RegisterWindowMessageW(FINDMSGSTRINGW);
static FINDREPLACEW g_findInfo;
static wchar_t g_findText[256];
static HWND g_find;
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

Click the boxes: they change the text (the demo's status bar says so).

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

Code blocks are colored by their language, keep their lines and scroll sideways when they're wide
(Shift+wheel, or drag the bar). The mouse over one shows a copy button.

```cpp
#include "MarkdownView.h"

// a long line scrolls: int main() { CMarkdownViewCtrl view; view.Create(hWndParent, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP); }
int main() {
    CMarkdownViewCtrl view;
    view.Create(hWndParent, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP);
    view.LoadFile(L"README.md");   // UTF-8 or UTF-16
    return 0x2A;
}
```

```python
def fib(n: int) -> list:
    """The first n Fibonacci numbers."""
    a, b = 0, 1
    for _ in range(n):
        yield a
        a, b = b, a + b   # tuples
```

```powershell
$files = Get-ChildItem -Path $env:TEMP -Filter '*.md'
if ($files.Count -gt 0) { Write-Host "Found $($files.Count)" }
```

```json
{ "name": "MarkdownView", "phase": 3, "done": false, "tags": ["tables", "images"] }
```

```html
<!-- markup -->
<p align="center"><img src="logo.png" width="96"></p>
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
| Tables    | **done** | 3     |
| Selection | soon   | 4     |

Columns share the width as HTML's tables do; a table too wide for the window scrolls sideways.

| Control | Window class | Prefix | What it is | Phase notes and other remarks that make this column long |
|---|---|---|---|---|
| `CLogViewCtrl` | `WC_LogView` | `LGV` | a log viewer for millions of lines | filtering and searching in slices on a timer |
| `CDiffViewCtrl` | `WC_DiffView` | `DFV` | two texts compared | Myers in linear space, merges, a background diff |
| `CMarkdownViewCtrl` | `WC_MarkdownView` | `MDV` | this one | Direct2D and DirectWrite, md4c |

## Images

Images come from local files (relative to the document), `data:` URLs, or the app, which this demo is
for the logo and the badges above. ![a dot](data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAACsElEQVR42hXSEd+2MBzH0dsfz/M8z/N5nvcC8jyfz/Muns/newF7TnBon//vK/v7+/v9/WNgZGJmYSWw/f36zsHJxU30lnh4yRQqjc7f3z8BBkYmZpZ/v74S2Ng5OLm4iSQeXjeZQqXR/32BQYCBkYl5+PWFlcDGzsHJxU0k8fCS3RYqjT58gVGAgZFp/PWZhZXAxs7BycVNJPHwkik2Ko0+foFJgIFx+vWJmYWVwMbOwcnFTSTx8JIpVFuNPn2BWYBh/vWRiZmFlcDGzsHJxU0k8fCSKVSazT5/gUVg+fWBkYmZhZXAxs7BycVNJPHwkilUGn35AqvAKsDIxMzCSmBj5+Dk4iaSeHjJFCqNvn6BIBAEGJmYWVgJbOwcnFzcRBIPL5lCpdHDF9h+/R8DIxMzCyuBjZ2Dk4ubSOLhJVOoNDp/f7sAAyMTMwsrgY2dg5OLm0ji4SVTqDT6/gUOAQZGJmYWVgIbOwcnFzeRxMNLplBp9OMLnAIMjEzMLKwENnYOTi5uIomHl0yh0ujnF7gEGBiZmFlYCWzsHJxc3EQSDy+ZQqXRry9wCzAwMjGzsBLY2Dk4ubiJJB5eMoVKo99fIPqmUYCRiZmFlcDGzsHJxU0k8fCSKVQaPX7fNAkkAUYmZhZWAhs7BycXN5HEw0umUGn09AUegUeAkYmZhZXAxs7BycVNJPHwkilUGv35Aq8AwyvAxMzCSmBj5+Dk4iaSeHjJFCrNZn+/QBZgYMwCzCysBDZ2Dk4ubiKJh5dModpq9PwFigADI1MRYGElsLFzcHJxE0k8vGSKjUqjly9QBRgYmZirACuBjZ2Dk4ubSOLhJbstVBq9foEmwMDIxMzSBAhs7BycXNxEEg+vm0yh0ujtC3QBBkYmZhZWAlsX4ODk4iZ6Szy8ZAqVRu+/v/++ybw8PddBGQAAAABJRU5ErkJggg==) is a `data:` PNG in the text, and
![docs](https://img.shields.io/badge/docs-latest-blue) a badge the app draws. One that can't be had
shows its alternative text: ![a missing image](missing.png).

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

// ---- Images ---------------------------------------------------------------------------
//
// The sample's images aren't files: the demo gives them as SVGs when the view asks (MDVN_GETIMAGE).

static std::string Badge(const char* label, const char* value, const char* color) {
	const int lw = 6 * (int)strlen(label) + 12, vw = 6 * (int)strlen(value) + 12;
	char svg[1024];
	sprintf_s(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"20\">"
		"<rect width=\"%d\" height=\"20\" rx=\"3\" fill=\"#555\"/><rect x=\"%d\" width=\"%d\" height=\"20\" rx=\"3\" fill=\"%s\"/>"
		"<rect x=\"%d\" width=\"4\" height=\"20\" fill=\"%s\"/>"
		"<g fill=\"#fff\" font-family=\"Segoe UI,Verdana,sans-serif\" font-size=\"11\">"
		"<text x=\"6\" y=\"14\">%s</text><text x=\"%d\" y=\"14\">%s</text></g></svg>",
		lw + vw, lw + vw, lw, vw, color, lw, color, label, lw + 6, value);
	return svg;
}

static const char g_logo[] =
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 96 96\">"
	"<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"0\" stop-color=\"#4493f8\"/>"
	"<stop offset=\"1\" stop-color=\"#8250df\"/></linearGradient></defs>"
	"<rect x=\"4\" y=\"4\" width=\"88\" height=\"88\" rx=\"20\" fill=\"url(#g)\"/>"
	"<path d=\"M22 66V30h8l10 14 10-14h8v36h-8V44L40 58 30 44v22z\" fill=\"#fff\"/>"
	"<path d=\"M64 50h8V30h6v20h8L75 66z\" fill=\"#fff\"/></svg>";

static IStream* ImageFor(LPCWSTR url) {
	std::string svg;
	if (wcscmp(url, L"logo.png") == 0)
		svg = g_logo;
	else if (wcscmp(url, L"build.svg") == 0)
		svg = Badge("build", "passing", "#3fb950");
	else if (wcscmp(url, L"license.svg") == 0)
		svg = Badge("license", "MIT", "#0969da");
	else if (wcsncmp(url, L"https://img.shields.io/badge/", 29) == 0)
		svg = Badge("docs", "latest", "#8250df");
	else
		return nullptr;
	return ::SHCreateMemStream((const BYTE*)svg.data(), (UINT)svg.size());
}

// an app's highlighter: words in capitals, bold, in a color of their own (it replaces the built-in one; returning -1
// would keep it)
static int CALLBACK CapitalsHighlighter(LPCWSTR, LPCWSTR code, UINT cch, MDVSPAN* spans, int maxSpans, LPARAM) {
	int count = 0;
	for (UINT i = 0; i < cch;) {
		UINT e = i;
		while (e < cch && iswupper(code[e]))
			e++;
		if (e - i >= 2 && (e == cch || !iswalnum(code[e])) && (i == 0 || !iswalnum(code[i - 1]))) {
			if (count < maxSpans)
				spans[count] = { i, e - i, RGB(0xC0, 0x20, 0xA0), MDVSPF_BOLD };
			count++;
		}
		i = e > i ? e : i + 1;
	}
	return count;
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
	g_status.SendMessage(WM_SIZE);
	RECT status;
	g_status.GetWindowRect(&status);
	rc.bottom -= status.bottom - status.top;
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

static void ShowSelection();

static UINT FindFlags(bool up) {
	UINT flags = up ? MDVFF_UP : 0;
	if (g_findInfo.Flags & FR_MATCHCASE)
		flags |= MDVFF_MATCHCASE;
	if (g_findInfo.Flags & FR_WHOLEWORD)
		flags |= MDVFF_WHOLEWORD;
	return flags;
}

static void FindNext(bool up) {
	if (!g_findText[0])
		return;
	if (!g_view.Find(g_findText, FindFlags(up)))
		::MessageBeep(MB_ICONASTERISK);
	ShowSelection();      // Find changes the selection without MDVN_SELCHANGED: that's for the user's changes
	if (g_highlightAll)
		g_view.HighlightAll(g_findText, FindFlags(false));
}

static void ShowFind() {
	if (g_find) {
		::SetActiveWindow(g_find);
		return;
	}
	g_findInfo = { sizeof(g_findInfo) };
	g_findInfo.hwndOwner = g_main;
	g_findInfo.Flags = FR_DOWN;
	g_findInfo.lpstrFindWhat = g_findText;
	g_findInfo.wFindWhatLen = (WORD)std::size(g_findText);
	g_find = ::FindTextW(&g_findInfo);
}

static void CopyString(const CString& text) {
	if (text.IsEmpty() || !::OpenClipboard(g_main))
		return;
	::EmptyClipboard();
	const size_t bytes = (text.GetLength() + 1) * sizeof(wchar_t);
	if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		memcpy(::GlobalLock(mem), (LPCWSTR)text, bytes);
		::GlobalUnlock(mem);
		if (!::SetClipboardData(CF_UNICODETEXT, mem))
			::GlobalFree(mem);
	}
	::CloseClipboard();
}

static void ShowSelection() {
	CString text;
	if (g_view.HasSelection())
		text.Format(L"%d characters selected", g_view.GetSelectedText().GetLength());
	g_status.SetText(0, text);
}

// a link's click: #links the view scrolls to by itself; another document in the same folder opens here; web
// links open in the browser (the view never opens anything)
static bool OpenLink(LPCWSTR url) {
	if (url[0] == L'#')
		return false;
	if (wcsncmp(url, L"http://", 7) == 0 || wcsncmp(url, L"https://", 8) == 0 || wcsncmp(url, L"mailto:", 7) == 0) {
		::ShellExecuteW(g_main, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
		return true;
	}
	const CString base = g_view.GetBasePath();
	if (base.IsEmpty() || wcschr(url, L':'))
		return true;
	CString path = base + L"\\" + url;
	const int hash = path.Find(L'#');
	if (hash >= 0)
		path = path.Left(hash);
	path.Replace(L'/', L'\\');
	if (::PathFileExistsW(path))
		LoadPath(path);
	return true;
}

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_DEMO_CENTERED, (g_view.GetStyle() & MDVS_CENTERED) != 0);
	check(ID_DEMO_CONTENTS, g_showToc);
	check(ID_DEMO_COLORS, g_customColors);
	check(ID_DEMO_SERIF, g_serif);
	check(ID_DEMO_HIGHLIGHTER, g_appHighlighter);
	check(ID_DEMO_HIGHLIGHTALL, g_highlightAll);
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
		case ID_DEMO_HIGHLIGHTER:
			g_appHighlighter = !g_appHighlighter;
			g_view.SetHighlighter(g_appHighlighter ? CapitalsHighlighter : nullptr, 0);
			break;
		case ID_DEMO_COPY: g_view.Copy(); break;
		case ID_DEMO_COPYMARKDOWN: CopyString(g_view.GetSelectedMarkdown()); break;
		case ID_DEMO_COPYLINK: CopyString(g_contextLink); break;
		case ID_DEMO_SELECTALL: g_view.SelectAll(); ShowSelection(); break;
		case ID_DEMO_FIND: ShowFind(); break;
		case ID_DEMO_FINDNEXT: FindNext(false); break;
		case ID_DEMO_FINDPREV: FindNext(true); break;
		case ID_DEMO_HIGHLIGHTALL:
			g_highlightAll = !g_highlightAll;
			g_view.HighlightAll(g_highlightAll ? g_findText : nullptr, FindFlags(false));
			break;
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
			g_status.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, IDC_STATUS);
			g_view.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | MDVS_CENTERED | MDVS_TASKCLICK, 0, IDC_VIEW);
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
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_GETIMAGE) {
				auto nm = (NMMDVIMAGE*)lParam;
				nm->pStream = ImageFor(nm->pszUrl);
				if (!nm->pStream && wcscmp(nm->pszUrl, L"missing.png") == 0)
					g_view.SetImage(nm->pszUrl, nullptr);   // there's none: its alternative text shows
				return 0;
			}
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_LINKCLICK)
				return OpenLink(((NMMDVLINK*)lParam)->pszUrl);
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_LINKHOT) {
				auto nm = (NMMDVLINK*)lParam;
				if (nm->pszUrl)
					g_status.SetText(0, nm->pszUrl);
				else
					ShowSelection();
				return 0;
			}
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_SELCHANGED) {
				ShowSelection();
				return 0;
			}
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_TASKTOGGLE) {
				auto nm = (NMMDVTASK*)lParam;
				g_status.SetText(0, nm->checked ? L"Task checked" : L"Task unchecked");
				return 0;          // the view changes its text
			}
			if (hdr->idFrom == IDC_VIEW && hdr->code == MDVN_CONTEXTMENU) {
				auto nm = (NMMDVCONTEXTMENU*)lParam;
				g_contextLink = nm->hit.part == MDVHT_LINK ? nm->hit.pszUrl : L"";
				HMENU menu = ::CreatePopupMenu();
				::AppendMenuW(menu, MF_STRING | (nm->hasSelection ? 0 : MF_GRAYED), ID_DEMO_COPY, L"&Copy");
				::AppendMenuW(menu, MF_STRING | (nm->hasSelection ? 0 : MF_GRAYED), ID_DEMO_COPYMARKDOWN, L"Copy as &Markdown");
				if (!g_contextLink.IsEmpty())
					::AppendMenuW(menu, MF_STRING, ID_DEMO_COPYLINK, L"Copy &Link Address");
				::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
				::AppendMenuW(menu, MF_STRING, ID_DEMO_SELECTALL, L"Select &All");
				::AppendMenuW(menu, MF_STRING, ID_DEMO_FIND, L"&Find...");
				::TrackPopupMenu(menu, TPM_RIGHTBUTTON, nm->pt.x, nm->pt.y, 0, hwnd, nullptr);
				::DestroyMenu(menu);
				return 0;
			}
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

		default:
			if (msg == g_findMessage) {
				auto fr = (FINDREPLACEW*)lParam;
				if (fr->Flags & FR_DIALOGTERM)
					g_find = nullptr;
				else if (fr->Flags & FR_FINDNEXT)
					FindNext(!(fr->Flags & FR_DOWN));
				return 0;
			}
			break;

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

	HMENU hEdit = ::CreatePopupMenu();
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_COPY, L"&Copy\tCtrl+C");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_COPYMARKDOWN, L"Copy as &Markdown");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_SELECTALL, L"Select &All\tCtrl+A");
	::AppendMenuW(hEdit, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_FIND, L"&Find...\tCtrl+F");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_FINDNEXT, L"Find &Next\tF3");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_FINDPREV, L"Find &Previous\tShift+F3");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_HIGHLIGHTALL, L"&Highlight All Matches");

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
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HIGHLIGHTER, L"&App's Highlighter (Capitals)");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"&Edit");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

	ACCEL accels[] = {
		{ FVIRTKEY | FCONTROL, 'O', ID_DEMO_OPEN },
		{ FVIRTKEY, VK_F5, ID_DEMO_RELOAD },
		{ FVIRTKEY | FCONTROL, 'F', ID_DEMO_FIND },
		{ FVIRTKEY, VK_F3, ID_DEMO_FINDNEXT },
		{ FVIRTKEY | FSHIFT, VK_F3, ID_DEMO_FINDPREV },
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
		if (g_find && ::IsDialogMessageW(g_find, &msg))
			continue;
		if (!::TranslateAccelerator(hwnd, hAccel, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return 0;
}
