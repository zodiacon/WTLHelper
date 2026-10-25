#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "RibbonBar.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>
#include <map>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Ids ------------------------------------------------------------------------

enum {
	IDC_RIBBON = 101,
	IDC_EDIT,
	IDC_LOG,

	TAB_HOME = 1, TAB_INSERT, TAB_VIEW, TAB_RIBBON,

	GRP_UNDO = 11, GRP_CLIPBOARD, GRP_FONT, GRP_PARAGRAPH, GRP_EDITING, GRP_PAGES, GRP_TABLES, GRP_ILLUSTRATIONS, GRP_LINKS,
	GRP_TEXT, GRP_VIEWS, GRP_SHOW, GRP_ZOOM, GRP_WINDOW, GRP_APPEARANCE, GRP_STATES,

	CMD_UNDO = 1001, CMD_REDO, CMD_PASTE, CMD_CUT, CMD_COPY, CMD_FORMATPAINTER, CMD_CLIPBOARD_DIALOG,
	CMD_BOLD, CMD_ITALIC, CMD_UNDERLINE, CMD_GROW, CMD_SHRINK, CMD_FONTCOLOR, CMD_FONT_DIALOG,
	CMD_ALIGNLEFT, CMD_ALIGNCENTER, CMD_ALIGNRIGHT, CMD_BULLETS, CMD_PARAGRAPH_DIALOG,
	CMD_FIND, CMD_REPLACE, CMD_SELECTALL, CMD_GOTO,
	CMD_COVER = 1101, CMD_BLANKPAGE, CMD_PAGEBREAK, CMD_TABLE, CMD_PICTURE, CMD_SHAPES, CMD_CHART, CMD_SCREENSHOT,
	CMD_LINK, CMD_BOOKMARK, CMD_COMMENT, CMD_TEXTBOX, CMD_DATETIME, CMD_SYMBOL,
	CMD_READMODE = 1201, CMD_PRINTLAYOUT, CMD_WEBLAYOUT, CMD_RULER, CMD_GRIDLINES, CMD_NAVPANE,
	CMD_ZOOM, CMD_ZOOM100, CMD_ZOOMIN, CMD_ZOOMOUT, CMD_NEWWINDOW, CMD_ARRANGE, CMD_SPLIT,
	CMD_DARKMODE = 1301, CMD_CUSTOMCOLORS, CMD_LARGEFONT, CMD_HIDEEDITING, CMD_DISABLECLIPBOARD,
};

// ---- Images ----------------------------------------------------------------------
//
// Glyphs of the system's icon font (Segoe Fluent Icons, or MDL2 Assets before Windows 11) drawn at the window's DPI,
// in the text color (made again when dark mode switches) or a color of their own.

enum {
	IMG_UNDO, IMG_REDO, IMG_PASTE, IMG_CUT, IMG_COPY, IMG_PAINTER, IMG_BOLD, IMG_ITALIC, IMG_UNDERLINE, IMG_GROW, IMG_SHRINK,
	IMG_FONTCOLOR, IMG_ALIGNLEFT, IMG_ALIGNCENTER, IMG_ALIGNRIGHT, IMG_BULLETS, IMG_FIND, IMG_REPLACE, IMG_SELECTALL, IMG_GOTO,
	IMG_PAGE, IMG_DOCUMENT, IMG_TABLE, IMG_PICTURE, IMG_SHAPES, IMG_CHART, IMG_SCREENSHOT, IMG_LINK, IMG_BOOKMARK, IMG_COMMENT,
	IMG_FONT, IMG_CALENDAR, IMG_SYMBOL, IMG_READ, IMG_PRINT, IMG_WEB, IMG_RULER, IMG_GRID, IMG_NAV, IMG_ZOOM, IMG_ZOOMIN,
	IMG_ZOOMOUT, IMG_WINDOW, IMG_SPLIT, IMG_DARK, IMG_COLORS, IMG_HIDE, IMG_LOCK,
};

struct Glyph {
	wchar_t ch;
	COLORREF color;    // CLR_DEFAULT: the text color
};

static const Glyph g_glyphs[] = {
	{ L'\xE7A7', CLR_DEFAULT }, { L'\xE7A6', CLR_DEFAULT }, { L'\xE77F', RGB(0xC2, 0x7C, 0x0E) }, { L'\xE8C6', CLR_DEFAULT },
	{ L'\xE8C8', CLR_DEFAULT }, { L'\xE771', RGB(0xC2, 0x7C, 0x0E) }, { L'\xE8DD', CLR_DEFAULT }, { L'\xE8DB', CLR_DEFAULT },
	{ L'\xE8DC', CLR_DEFAULT }, { L'\xE8E8', CLR_DEFAULT }, { L'\xE8E7', CLR_DEFAULT }, { L'\xE8D3', RGB(0xD1, 0x34, 0x38) },
	{ L'\xE8E4', CLR_DEFAULT }, { L'\xE8E3', CLR_DEFAULT }, { L'\xE8E2', CLR_DEFAULT }, { L'\xE8FD', CLR_DEFAULT },
	{ L'\xE721', RGB(0x2B, 0x7C, 0xD3) }, { L'\xE8AB', CLR_DEFAULT }, { L'\xE8B3', CLR_DEFAULT }, { L'\xE72A', CLR_DEFAULT },
	{ L'\xE7C3', RGB(0x2B, 0x7C, 0xD3) }, { L'\xE8A5', CLR_DEFAULT }, { L'\xE8A9', RGB(0x2B, 0x7C, 0xD3) }, { L'\xE91B', RGB(0x10, 0x89, 0x3E) },
	{ L'\xE790', RGB(0x88, 0x5C, 0xD6) }, { L'\xE9D2', RGB(0x10, 0x89, 0x3E) }, { L'\xE7F4', CLR_DEFAULT }, { L'\xE71B', RGB(0x2B, 0x7C, 0xD3) },
	{ L'\xE8A4', CLR_DEFAULT }, { L'\xE90A', RGB(0xC2, 0x7C, 0x0E) }, { L'\xE8D2', CLR_DEFAULT }, { L'\xE787', CLR_DEFAULT },
	{ L'\xE76E', CLR_DEFAULT }, { L'\xE736', CLR_DEFAULT }, { L'\xE749', CLR_DEFAULT }, { L'\xE774', RGB(0x2B, 0x7C, 0xD3) },
	{ L'\xED5E', CLR_DEFAULT }, { L'\xE80A', CLR_DEFAULT }, { L'\xE700', CLR_DEFAULT }, { L'\xE71E', CLR_DEFAULT },
	{ L'\xE8A3', CLR_DEFAULT }, { L'\xE71F', CLR_DEFAULT }, { L'\xE78B', CLR_DEFAULT }, { L'\xE90D', CLR_DEFAULT },
	{ L'\xE706', RGB(0xC2, 0x7C, 0x0E) }, { L'\xE790', RGB(0xD1, 0x34, 0x38) }, { L'\xED1A', CLR_DEFAULT }, { L'\xE72E', CLR_DEFAULT },
};
static_assert(std::size(g_glyphs) == IMG_LOCK + 1);

static HFONT IconFont(int size) {
	for (auto face : { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }) {
		HFONT font = ::CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
		HDC dc = ::CreateCompatibleDC(nullptr);
		auto old = ::SelectObject(dc, font);
		wchar_t name[LF_FACESIZE] = L"";
		::GetTextFaceW(dc, LF_FACESIZE, name);
		::SelectObject(dc, old);
		::DeleteDC(dc);
		if (::_wcsicmp(name, face) == 0)
			return font;
		::DeleteObject(font);
	}
	return nullptr;
}

// a glyph as an icon with alpha: drawn white on black (gray antialiasing), the gray level becoming the alpha
static HICON GlyphIcon(wchar_t ch, int size, COLORREF color, HFONT font) {
	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB };
	void* bits = nullptr;
	HBITMAP dib = ::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!dib)
		return nullptr;
	HDC dc = ::CreateCompatibleDC(nullptr);
	auto oldBitmap = ::SelectObject(dc, dib);
	auto oldFont = ::SelectObject(dc, font);
	::SetTextColor(dc, RGB(255, 255, 255));
	::SetBkMode(dc, TRANSPARENT);
	RECT rc{ 0, 0, size, size };
	::DrawTextW(dc, &ch, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
	::GdiFlush();
	auto pixels = (DWORD*)bits;
	DWORD rgb = (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
	for (int i = 0; i < size * size; i++) {
		DWORD p = pixels[i];
		DWORD a = std::max({ (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF });
		pixels[i] = a ? (a << 24) | rgb : 0;
	}
	::SelectObject(dc, oldFont);
	::SelectObject(dc, oldBitmap);
	::DeleteDC(dc);
	HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
	ICONINFO ii{ TRUE, 0, 0, mask, dib };
	HICON icon = ::CreateIconIndirect(&ii);
	::DeleteObject(mask);
	::DeleteObject(dib);
	return icon;
}

static HIMAGELIST BuildImages(int size, COLORREF text) {
	HIMAGELIST list = ::ImageList_Create(size, size, ILC_COLOR32, (int)std::size(g_glyphs), 0);
	HFONT font = IconFont(size);
	for (auto& g : g_glyphs) {
		HICON icon = font ? GlyphIcon(g.ch, size, g.color == CLR_DEFAULT ? text : g.color, font) : nullptr;
		if (icon) {
			::ImageList_AddIcon(list, icon);
			::DestroyIcon(icon);
		}
		else {   // keep the indexes
			HICON blank = GlyphIcon(L' ', size, text, (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
			::ImageList_AddIcon(list, blank);
			::DestroyIcon(blank);
		}
	}
	if (font)
		::DeleteObject(font);
	return list;
}

// ---- State --------------------------------------------------------------------

static CRibbonBarCtrl g_ribbon;
static HWND g_main, g_edit, g_log;
static HIMAGELIST g_large, g_small;
static HFONT g_largeFont;
static std::map<UINT, std::wstring> g_names;   // for the log
static bool g_disableClipboard;

static const RIBCOLORS g_colors = {
	RGB(0x1E, 0x3A, 0x5F), RGB(0xE8, 0xEE, 0xF5), RGB(0xF4, 0xF1, 0xEA), RGB(0x20, 0x20, 0x20), RGB(0x6E, 0x6A, 0x60), RGB(0xD6, 0xD0, 0xC2),
	RGB(0xE6, 0xDF, 0xCE), RGB(0xD8, 0xCE, 0xB6), RGB(0xF6, 0xDE, 0xA8), RGB(0xC2, 0x7C, 0x0E), RGB(0xFF, 0xC8, 0x5A),
};

static void Log(const wchar_t* text) {
	int i = (int)::SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)text);
	::SendMessageW(g_log, LB_SETCURSEL, i, 0);
}

static void UpdateImages() {
	int dpi = (int)::GetDpiForWindow(g_main);
	COLORREF text = g_ribbon.GetColors().crText;
	auto large = BuildImages(::MulDiv(32, dpi, 96), text);
	auto small_ = BuildImages(::MulDiv(16, dpi, 96), text);
	g_ribbon.SetImageLists(large, small_);
	if (g_large)
		::ImageList_Destroy(g_large);
	if (g_small)
		::ImageList_Destroy(g_small);
	g_large = large;
	g_small = small_;
	g_ribbon.Invalidate();
}

// ---- The ribbon ------------------------------------------------------------------

static void Add(UINT group, UINT id, const wchar_t* text, int image, UINT size = RIBSZ_LARGE, UINT kind = RIBIK_BUTTON, int radio = 0,
	UINT state = 0) {
	RIBITEM item;
	item.id = id;
	item.kind = kind;
	item.pszText = text;
	item.iImage = item.iSmallImage = image;
	item.size = size;
	item.radio = radio;
	item.state = state;
	g_ribbon.AddItem(group, &item);
	g_names[id] = text;
}

static void Group(UINT tab, UINT id, const wchar_t* text, int priority = 0, UINT launcher = 0) {
	RIBGROUP group;
	group.id = id;
	group.pszText = text;
	group.priority = priority;
	if (launcher) {
		group.flags = RIBGF_LAUNCHER;
		group.uLauncherId = launcher;
		g_names[launcher] = std::wstring(text) + L" Options";
	}
	g_ribbon.AddGroup(tab, &group);
}

static void BuildRibbon() {
	auto& r = g_ribbon;
	r.AddTab(TAB_HOME, L"Home");
	r.AddTab(TAB_INSERT, L"Insert");
	r.AddTab(TAB_VIEW, L"View");
	r.AddTab(TAB_RIBBON, L"Ribbon");

	// Home: Undo stays large longest, Editing shrinks first
	Group(TAB_HOME, GRP_UNDO, L"Undo", 2);
	Add(GRP_UNDO, CMD_UNDO, L"Undo", IMG_UNDO, RIBSZ_SMALL);
	Add(GRP_UNDO, CMD_REDO, L"Redo", IMG_REDO, RIBSZ_SMALL, RIBIK_BUTTON, 0, RIBIS_DISABLED);
	Group(TAB_HOME, GRP_CLIPBOARD, L"Clipboard", 1, CMD_CLIPBOARD_DIALOG);
	Add(GRP_CLIPBOARD, CMD_PASTE, L"Paste", IMG_PASTE, RIBSZ_LARGE, RIBIK_SPLIT);
	Add(GRP_CLIPBOARD, CMD_CUT, L"Cut", IMG_CUT, RIBSZ_SMALL);
	Add(GRP_CLIPBOARD, CMD_COPY, L"Copy", IMG_COPY, RIBSZ_SMALL);
	Add(GRP_CLIPBOARD, CMD_FORMATPAINTER, L"Format Painter", IMG_PAINTER, RIBSZ_SMALL, RIBIK_TOGGLE);
	Group(TAB_HOME, GRP_FONT, L"Font", 0, CMD_FONT_DIALOG);
	Add(GRP_FONT, CMD_BOLD, L"Bold", IMG_BOLD, RIBSZ_ICON, RIBIK_TOGGLE);
	Add(GRP_FONT, CMD_ITALIC, L"Italic", IMG_ITALIC, RIBSZ_ICON, RIBIK_TOGGLE);
	Add(GRP_FONT, CMD_UNDERLINE, L"Underline", IMG_UNDERLINE, RIBSZ_ICON, RIBIK_TOGGLE);
	Add(GRP_FONT, CMD_GROW, L"Grow Font", IMG_GROW, RIBSZ_ICON);
	Add(GRP_FONT, CMD_SHRINK, L"Shrink Font", IMG_SHRINK, RIBSZ_ICON);
	Add(GRP_FONT, CMD_FONTCOLOR, L"Font Color", IMG_FONTCOLOR, RIBSZ_ICON, RIBIK_SPLIT);
	Group(TAB_HOME, GRP_PARAGRAPH, L"Paragraph", 0, CMD_PARAGRAPH_DIALOG);
	Add(GRP_PARAGRAPH, CMD_ALIGNLEFT, L"Align Left", IMG_ALIGNLEFT, RIBSZ_SMALL, RIBIK_RADIO, 1, RIBIS_CHECKED);
	Add(GRP_PARAGRAPH, CMD_ALIGNCENTER, L"Center", IMG_ALIGNCENTER, RIBSZ_SMALL, RIBIK_RADIO, 1);
	Add(GRP_PARAGRAPH, CMD_ALIGNRIGHT, L"Align Right", IMG_ALIGNRIGHT, RIBSZ_SMALL, RIBIK_RADIO, 1);
	r.AddSeparator(GRP_PARAGRAPH);
	Add(GRP_PARAGRAPH, CMD_BULLETS, L"Bullets", IMG_BULLETS, RIBSZ_SMALL, RIBIK_DROPDOWN);
	Group(TAB_HOME, GRP_EDITING, L"Editing", -1);
	Add(GRP_EDITING, CMD_FIND, L"Find", IMG_FIND);
	Add(GRP_EDITING, CMD_REPLACE, L"Replace", IMG_REPLACE, RIBSZ_SMALL);
	Add(GRP_EDITING, CMD_SELECTALL, L"Select All", IMG_SELECTALL, RIBSZ_SMALL);
	Add(GRP_EDITING, CMD_GOTO, L"Go To", IMG_GOTO, RIBSZ_SMALL);

	Group(TAB_INSERT, GRP_PAGES, L"Pages");
	Add(GRP_PAGES, CMD_COVER, L"Cover Page", IMG_DOCUMENT, RIBSZ_LARGE, RIBIK_DROPDOWN);
	Add(GRP_PAGES, CMD_BLANKPAGE, L"Blank Page", IMG_PAGE, RIBSZ_SMALL);
	Add(GRP_PAGES, CMD_PAGEBREAK, L"Page Break", IMG_PAGE, RIBSZ_SMALL);
	Group(TAB_INSERT, GRP_TABLES, L"Tables", 1);
	Add(GRP_TABLES, CMD_TABLE, L"Table", IMG_TABLE, RIBSZ_LARGE, RIBIK_DROPDOWN);
	Group(TAB_INSERT, GRP_ILLUSTRATIONS, L"Illustrations");
	Add(GRP_ILLUSTRATIONS, CMD_PICTURE, L"Pictures", IMG_PICTURE);
	Add(GRP_ILLUSTRATIONS, CMD_SHAPES, L"Shapes", IMG_SHAPES, RIBSZ_LARGE, RIBIK_DROPDOWN);
	Add(GRP_ILLUSTRATIONS, CMD_CHART, L"Chart", IMG_CHART);
	Add(GRP_ILLUSTRATIONS, CMD_SCREENSHOT, L"Screenshot", IMG_SCREENSHOT, RIBSZ_LARGE, RIBIK_DROPDOWN);
	Group(TAB_INSERT, GRP_LINKS, L"Links");
	Add(GRP_LINKS, CMD_LINK, L"Link", IMG_LINK);
	Add(GRP_LINKS, CMD_BOOKMARK, L"Bookmark", IMG_BOOKMARK, RIBSZ_SMALL);
	Add(GRP_LINKS, CMD_COMMENT, L"Comment", IMG_COMMENT, RIBSZ_SMALL);
	Group(TAB_INSERT, GRP_TEXT, L"Text");
	Add(GRP_TEXT, CMD_TEXTBOX, L"Text Box", IMG_FONT, RIBSZ_LARGE, RIBIK_DROPDOWN);
	Add(GRP_TEXT, CMD_DATETIME, L"Date & Time", IMG_CALENDAR, RIBSZ_SMALL);
	Add(GRP_TEXT, CMD_SYMBOL, L"Symbol", IMG_SYMBOL, RIBSZ_SMALL, RIBIK_DROPDOWN);

	Group(TAB_VIEW, GRP_VIEWS, L"Views");
	Add(GRP_VIEWS, CMD_READMODE, L"Read Mode", IMG_READ, RIBSZ_LARGE, RIBIK_RADIO, 2);
	Add(GRP_VIEWS, CMD_PRINTLAYOUT, L"Print Layout", IMG_PRINT, RIBSZ_LARGE, RIBIK_RADIO, 2, RIBIS_CHECKED);
	Add(GRP_VIEWS, CMD_WEBLAYOUT, L"Web Layout", IMG_WEB, RIBSZ_LARGE, RIBIK_RADIO, 2);
	Group(TAB_VIEW, GRP_SHOW, L"Show");
	Add(GRP_SHOW, CMD_RULER, L"Ruler", IMG_RULER, RIBSZ_SMALL, RIBIK_TOGGLE);
	Add(GRP_SHOW, CMD_GRIDLINES, L"Gridlines", IMG_GRID, RIBSZ_SMALL, RIBIK_TOGGLE);
	Add(GRP_SHOW, CMD_NAVPANE, L"Navigation Pane", IMG_NAV, RIBSZ_SMALL, RIBIK_TOGGLE, 0, RIBIS_CHECKED);
	Group(TAB_VIEW, GRP_ZOOM, L"Zoom");
	Add(GRP_ZOOM, CMD_ZOOM, L"Zoom", IMG_ZOOM);
	Add(GRP_ZOOM, CMD_ZOOM100, L"100%", IMG_ZOOM);
	Add(GRP_ZOOM, CMD_ZOOMIN, L"Zoom In", IMG_ZOOMIN, RIBSZ_SMALL);
	Add(GRP_ZOOM, CMD_ZOOMOUT, L"Zoom Out", IMG_ZOOMOUT, RIBSZ_SMALL);
	Group(TAB_VIEW, GRP_WINDOW, L"Window");
	Add(GRP_WINDOW, CMD_NEWWINDOW, L"New Window", IMG_WINDOW);
	Add(GRP_WINDOW, CMD_ARRANGE, L"Arrange All", IMG_TABLE, RIBSZ_SMALL);
	Add(GRP_WINDOW, CMD_SPLIT, L"Split", IMG_SPLIT, RIBSZ_SMALL, RIBIK_TOGGLE);

	Group(TAB_RIBBON, GRP_APPEARANCE, L"Appearance");
#ifdef DEMO_WTLHELPER
	Add(GRP_APPEARANCE, CMD_DARKMODE, L"Dark Mode", IMG_DARK, RIBSZ_LARGE, RIBIK_TOGGLE);
#else
	Add(GRP_APPEARANCE, CMD_DARKMODE, L"Dark Mode", IMG_DARK, RIBSZ_LARGE, RIBIK_TOGGLE, 0, RIBIS_DISABLED);
#endif
	Add(GRP_APPEARANCE, CMD_CUSTOMCOLORS, L"Custom Colors", IMG_COLORS, RIBSZ_LARGE, RIBIK_TOGGLE);
	Add(GRP_APPEARANCE, CMD_LARGEFONT, L"Larger Font", IMG_FONT, RIBSZ_LARGE, RIBIK_TOGGLE);
	Group(TAB_RIBBON, GRP_STATES, L"States");
	Add(GRP_STATES, CMD_HIDEEDITING, L"Hide Editing", IMG_HIDE, RIBSZ_LARGE, RIBIK_TOGGLE);
	Add(GRP_STATES, CMD_DISABLECLIPBOARD, L"Disable Clipboard", IMG_LOCK, RIBSZ_LARGE, RIBIK_TOGGLE);
}

// ---- Window -----------------------------------------------------------------------

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	int dpi = (int)::GetDpiForWindow(hwnd);
	int ribbon = g_ribbon.GetIdealHeight();
	int log = ::MulDiv(130, dpi, 96);
	g_ribbon.SetWindowPos(nullptr, 0, 0, rc.right, ribbon, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_edit, nullptr, 0, ribbon, rc.right, std::max(0, (int)rc.bottom - ribbon - log), SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, 0, std::max(ribbon, (int)rc.bottom - log), rc.right, log, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts(HWND hwnd) {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
	static HFONT font;
	if (font)
		::DeleteObject(font);
	font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	::SendMessageW(g_edit, WM_SETFONT, (WPARAM)font, TRUE);
	::SendMessageW(g_log, WM_SETFONT, (WPARAM)font, TRUE);
	if (g_largeFont) {
		// Ribbon > Larger Font: a font of the app's, made again for the DPI
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_ribbon.SetFont(g_largeFont);
	}
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case CMD_UNDO: ::SendMessageW(g_edit, EM_UNDO, 0, 0); break;
		case CMD_CUT: ::SendMessageW(g_edit, WM_CUT, 0, 0); break;
		case CMD_COPY: ::SendMessageW(g_edit, WM_COPY, 0, 0); break;
		case CMD_PASTE: ::SendMessageW(g_edit, WM_PASTE, 0, 0); break;
		case CMD_SELECTALL:
			::SendMessageW(g_edit, EM_SETSEL, 0, -1);
			::SetFocus(g_edit);
			break;
#ifdef DEMO_WTLHELPER
		case CMD_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
		case CMD_CUSTOMCOLORS:
			g_ribbon.SetColors((g_ribbon.GetItemState(CMD_CUSTOMCOLORS) & RIBIS_CHECKED) ? &g_colors : nullptr);
			UpdateImages();   // the glyphs in the colors' text color
			break;
		case CMD_LARGEFONT:
			if (g_ribbon.GetItemState(CMD_LARGEFONT) & RIBIS_CHECKED) {
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
				ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
				g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
				g_ribbon.SetFont(g_largeFont);
			}
			else {
				g_ribbon.SetFont(nullptr);   // the default again
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		case CMD_HIDEEDITING: {
			bool hide = (g_ribbon.GetItemState(CMD_HIDEEDITING) & RIBIS_CHECKED) != 0;
			for (UINT item : { CMD_FIND, CMD_REPLACE, CMD_SELECTALL, CMD_GOTO })
				g_ribbon.SetItemState(item, hide ? RIBIS_HIDDEN : 0, RIBIS_HIDDEN);
			break;
		}
		case CMD_DISABLECLIPBOARD:
			g_disableClipboard = (g_ribbon.GetItemState(CMD_DISABLECLIPBOARD) & RIBIS_CHECKED) != 0;
			break;
	}
	auto it = g_names.find(id);
	CString line;
	UINT state = g_ribbon.GetItemState(id);
	line.Format(L"WM_COMMAND %u: %s%s", id, it != g_names.end() ? it->second.c_str() : L"?",
		state != (UINT)-1 && (state & RIBIS_CHECKED) ? L" (checked)" : L"");
	Log(line);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_ribbon.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, IDC_RIBBON);
			g_edit = ::CreateWindowExW(0, WC_EDITW, L"Select some text: Cut and Copy follow the selection, Paste the clipboard (RIBN_UPDATEUI).\r\n\r\n"
				L"Make the window narrower: the groups shrink from the right, Editing first, Undo last.",
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL, 0, 0, 0, 0, hwnd, (HMENU)IDC_EDIT, nullptr, nullptr);
			g_log = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_BORDER,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			UpdateFonts(hwnd);
			BuildRibbon();
			UpdateImages();
			Layout(hwnd);
			return 0;
		}

		case WM_SIZE:
			Layout(hwnd);
			return 0;

		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			UpdateFonts(hwnd);
			UpdateImages();
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout(hwnd);
			return 0;
		}

		case WM_COMMAND:
			if (lParam == 0)    // the ribbon's, as a menu's
				OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom != IDC_RIBBON)
				break;
			CString line;
			switch (hdr->code) {
				case RIBN_UPDATEUI: {
					// the app's state: the edit's selection, its undo, the clipboard
					DWORD start = 0, end = 0;
					::SendMessageW(g_edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
					bool selection = start != end;
					g_ribbon.EnableItem(CMD_CUT, selection && !g_disableClipboard);
					g_ribbon.EnableItem(CMD_COPY, selection && !g_disableClipboard);
					g_ribbon.EnableItem(CMD_PASTE, ::IsClipboardFormatAvailable(CF_UNICODETEXT) && !g_disableClipboard);
					g_ribbon.EnableItem(CMD_UNDO, ::SendMessageW(g_edit, EM_CANUNDO, 0, 0) != 0);
					return 0;
				}
				case RIBN_TABCHANGED:
					line.Format(L"RIBN_TABCHANGED: tab %u", ((NMRIBTAB*)lParam)->id);
					Log(line);
					return 0;
				case RIBN_HEIGHTCHANGED:
					Log(L"RIBN_HEIGHTCHANGED");
					Layout(hwnd);
					return 0;
			}
			return 0;
		}

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	if (msg == CRibbonBarCtrl::ThemeChangedMessage && g_ribbon.IsWindow())
		UpdateImages();   // the glyphs in the mode's text color
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

// as a WTL app does it: the states set when the app has nothing else to do
struct IdleUpdate : CIdleHandler {
	BOOL OnIdle() override {
		if (g_ribbon.IsWindow())
			g_ribbon.UpdateUI();
		return FALSE;
	}
};

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
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"RibbonBarDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"RibbonBarDemoWnd", L"RibbonBar Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1100, 640, nullptr, nullptr, hInstance, nullptr);
	::ShowWindow(hwnd, nCmdShow);

	CMessageLoop loop;
	IdleUpdate idle;
	_Module.AddMessageLoop(&loop);
	loop.AddIdleHandler(&idle);
	loop.Run();
	loop.RemoveIdleHandler(&idle);
	_Module.RemoveMessageLoop();

	if (g_large)
		::ImageList_Destroy(g_large);
	if (g_small)
		::ImageList_Destroy(g_small);
	_Module.Term();
	return 0;
}
