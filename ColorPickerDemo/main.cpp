#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "ColorPicker.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only): the 32-bit demo has the
// control's own dark colors only
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>
#include <random>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_PICKER = 101,
	IDC_RESET = 102,

	ID_FILE_EXIT = 201,

	ID_VIEW_ALPHA = 301,
	ID_VIEW_DARK,
	ID_VIEW_DARKMODE,

	ID_COLOR_RED = 401,
	ID_COLOR_BLACK,
	ID_COLOR_GRAY,
	ID_COLOR_HALF,
	ID_COLOR_RANDOM,
	ID_COLOR_HSV,
};

// ---- State --------------------------------------------------------------------

static CColorPickerCtrl g_picker;
static HWND g_reset;
static bool g_darkColors;
static int g_changing, g_changed;
static CString g_lastEvent = L"(none)";
static HFONT g_font;

static const CPKCOLORS g_dark = { RGB(0x20, 0x20, 0x20), RGB(0xE0, 0xE0, 0xE0), RGB(0x60, 0x60, 0x60),
	RGB(0x5A, 0x5A, 0x5A), RGB(0x3A, 0x3A, 0x3A) };

static CPKCOLORS Colors() {
	return g_picker.GetColors();
}

static int Scale(HWND hwnd, int dips) {
	return ::MulDiv(dips, ::GetDpiForWindow(hwnd), 96);
}

static void UpdateFont(HWND hwnd) {
	if (g_font)
		::DeleteObject(g_font);
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(hwnd));
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	if (g_reset)
		::SendMessage(g_reset, WM_SETFONT, (WPARAM)g_font, TRUE);
}

// the panel on the right: the color and its numbers
static RECT PanelRect(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	rc.left = rc.right - Scale(hwnd, 230);
	return rc;
}

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	auto panel = PanelRect(hwnd);
	g_picker.SetWindowPos(nullptr, 0, 0, panel.left, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_reset, nullptr, panel.left + Scale(hwnd, 10), rc.bottom - Scale(hwnd, 38), Scale(hwnd, 100), Scale(hwnd, 28),
		SWP_NOZORDER | SWP_NOACTIVATE);
	::InvalidateRect(hwnd, &panel, TRUE);
}

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_VIEW_ALPHA, (g_picker.GetStyle() & CPKS_NOALPHA) == 0);
	check(ID_VIEW_DARK, g_darkColors);
#ifdef DEMO_WTLHELPER
	check(ID_VIEW_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void PaintPanel(HWND hwnd, HDC hdc) {
	auto c = Colors();
	auto panel = PanelRect(hwnd);
	auto brush = ::CreateSolidBrush(c.crBack);
	::FillRect(hdc, &panel, brush);
	::DeleteObject(brush);

	// the color over a checkerboard, the left half without its alpha
	int pad = Scale(hwnd, 10);
	RECT sw{ panel.left + pad, panel.top + Scale(hwnd, 9), panel.right - pad, panel.top + Scale(hwnd, 89) };
	int cell = Scale(hwnd, 6);
	COLORREF color = g_picker.GetColor();
	BYTE alpha = g_picker.GetAlpha();
	for (int y = sw.top; y < sw.bottom; y += cell) {
		for (int x = sw.left; x < sw.right; x += cell) {
			RECT r{ x, y, std::min(x + cell, (int)sw.right), std::min(y + cell, (int)sw.bottom) };
			COLORREF check = (((x - sw.left) / cell + (y - sw.top) / cell) & 1) ? c.crCheck2 : c.crCheck1;
			bool opaque = x < (sw.left + sw.right) / 2;
			auto mix = [&](int a, int b) { return (BYTE)((a * (255 - alpha) + b * alpha + 127) / 255); };
			COLORREF out = opaque ? color : RGB(mix(GetRValue(check), GetRValue(color)), mix(GetGValue(check), GetGValue(color)),
				mix(GetBValue(check), GetBValue(color)));
			::SetBkColor(hdc, out);
			::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
		}
	}
	auto frame = ::CreateSolidBrush(c.crBorder);
	::FrameRect(hdc, &sw, frame);
	::DeleteObject(frame);

	auto hsv = g_picker.GetHsv();
	CString text;
	text.Format(L"Hex:  #%02X%02X%02X\nRGB:  %d, %d, %d\nAlpha:  %d\nHSV:  %.2f\x00B0, %.1f%%, %.1f%%\n\nChanging: %d\nChanged: %d\nLast: %s",
		GetRValue(color), GetGValue(color), GetBValue(color), GetRValue(color), GetGValue(color), GetBValue(color), alpha,
		hsv.h, hsv.s * 100, hsv.v * 100, g_changing, g_changed, (LPCWSTR)g_lastEvent);
	RECT tr{ sw.left, sw.bottom + pad, sw.right, panel.bottom - Scale(hwnd, 44) };
	auto oldFont = ::SelectObject(hdc, g_font);
	::SetTextColor(hdc, c.crText);
	::SetBkMode(hdc, TRANSPARENT);
	::DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_TOP | DT_NOPREFIX);
	::SelectObject(hdc, oldFont);
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_FILE_EXIT:
			::DestroyWindow(hwnd);
			return;
		case ID_VIEW_ALPHA:
			if (g_picker.GetStyle() & CPKS_NOALPHA)
				g_picker.ModifyStyle(CPKS_NOALPHA, 0);
			else
				g_picker.ModifyStyle(0, CPKS_NOALPHA);
			break;
		case ID_VIEW_DARK:
			g_darkColors = !g_darkColors;
			g_picker.SetColors(g_darkColors ? &g_dark : nullptr);
			break;
#ifdef DEMO_WTLHELPER
		case ID_VIEW_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
		case ID_COLOR_RED:
			g_picker.SetColor(RGB(255, 0, 0));
			break;
		case ID_COLOR_BLACK:
			g_picker.SetColor(RGB(0, 0, 0));   // the hue and saturation stay
			break;
		case ID_COLOR_GRAY:
			g_picker.SetColor(RGB(128, 128, 128));   // the hue stays
			break;
		case ID_COLOR_HALF:
			g_picker.SetAlpha(128);
			break;
		case ID_COLOR_RANDOM:
		{
			static std::mt19937 rng{ 12345 };
			g_picker.SetColor(RGB(rng() & 0xFF, rng() & 0xFF, rng() & 0xFF));
			break;
		}
		case ID_COLOR_HSV:
			g_picker.SetHsv({ 210, 0.6, 0.9 });
			break;
		case IDC_RESET:
			g_picker.SetHsv({ 0, 0, 1 });
			g_picker.SetAlpha(255);
			g_changing = g_changed = 0;
			g_lastEvent = L"(none)";
			break;
		default:
			return;
	}
	UpdateChecks(::GetMenu(hwnd));
	::InvalidateRect(hwnd, nullptr, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			UpdateFont(hwnd);
			g_picker.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, IDC_PICKER);
			g_picker.SetHsv({ 210, 0.6, 0.9 });
			g_reset = ::CreateWindowExW(0, WC_BUTTONW, L"&Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
				0, 0, 0, 0, hwnd, (HMENU)IDC_RESET, nullptr, nullptr);
			::SendMessage(g_reset, WM_SETFONT, (WPARAM)g_font, FALSE);
			g_picker.SetFocus();
			return 0;

		case WM_SIZE:
			Layout(hwnd);
			return 0;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = ::BeginPaint(hwnd, &ps);
			{
				// through a memory DC: drawn straight on the window, the swatch's cells flicker while dragging
				auto panel = PanelRect(hwnd);
				WTL::CMemoryDC mdc(hdc, panel);
				PaintPanel(hwnd, mdc);
			}
			::EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_DPICHANGED:
		{
			UpdateFont(hwnd);
			auto rc = (const RECT*)lParam;
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout(hwnd);
			return 0;
		}

		case WM_NOTIFY:
		{
			auto nm = (NMCOLORPICKER*)lParam;
			if (nm->hdr.idFrom == IDC_PICKER) {
				static const wchar_t* parts[] = { L"?", L"area", L"hue", L"alpha" };
				bool changed = nm->hdr.code == CPKN_COLORCHANGED;
				(changed ? g_changed : g_changing)++;
				g_lastEvent.Format(L"%s (%s)", changed ? L"changed" : L"changing", nm->part < std::size(parts) ? parts[nm->part] : L"?");
				auto panel = PanelRect(hwnd);
				::InvalidateRect(hwnd, &panel, FALSE);
			}
			return 0;
		}

		case WM_COMMAND:
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_ACTIVATE:
			if (LOWORD(wParam) != WA_INACTIVE)
				g_picker.SetFocus();
			return 0;

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	if (msg == CColorPickerCtrl::ThemeChangedMessage) {
		UpdateChecks(::GetMenu(hwnd));
		::InvalidateRect(hwnd, nullptr, TRUE);
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
	wc.lpszClassName = L"ColorPickerDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	::AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_VIEW_ALPHA, L"&Alpha Bar");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_DARK, L"Dar&k Colors (SetColors)");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_VIEW_DARKMODE, L"WTLHelper Dark &Mode");
#endif

	HMENU hColor = ::CreatePopupMenu();
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_RED, L"&Red");
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_BLACK, L"&Black (keeps hue and saturation)");
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_GRAY, L"&Gray (keeps hue)");
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_HALF, L"&Half Transparent");
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_RANDOM, L"R&andom\tF5");
	::AppendMenuW(hColor, MF_STRING, ID_COLOR_HSV, L"&HSV 210, 60%, 90%");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hColor, L"&Color");

	HWND hwnd = ::CreateWindowExW(0, L"ColorPickerDemoWnd", L"Color Picker Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 760, 460, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);

	ACCEL accels[] = {
		{ FVIRTKEY, VK_F5, ID_COLOR_RANDOM },
	};
	HACCEL hAccel = ::CreateAcceleratorTable(accels, (int)std::size(accels));

	::ShowWindow(hwnd, nCmdShow);
	::UpdateWindow(hwnd);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (::TranslateAccelerator(hwnd, hAccel, &msg) || ::IsDialogMessage(hwnd, &msg))
			continue;
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	::DestroyAcceleratorTable(hAccel);
	if (g_font)
		::DeleteObject(g_font);
	_Module.Term();
	return (int)msg.wParam;
}
