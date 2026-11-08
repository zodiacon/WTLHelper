// FunctionPlotDemo: an expression list beside a function plot. Each line of the editor is a graph; a name the
// expressions use that nothing defines becomes a slider (View > Automatic Sliders). The status bar shows the traced
// point and the view.

#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "FunctionPlot.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_PLOT = 101,
	IDC_EDIT,
	IDC_STATUS,

	ID_DEMO_EXIT = 201,
	ID_EXAMPLE_FIRST = 210,     // one per example

	ID_DEMO_HOME = 301,
	ID_DEMO_ZOOMIN,
	ID_DEMO_ZOOMOUT,
	ID_DEMO_EQUAL,
	ID_DEMO_PITICKS,
	ID_DEMO_DEGREES,
	ID_DEMO_LEGEND,
	ID_DEMO_SLIDERS,
	ID_DEMO_GRID,
	ID_DEMO_AXES,
	ID_DEMO_TRACE,
	ID_DEMO_AUTOSLIDERS,
	ID_DEMO_DARKMODE,
	ID_DEMO_HIGHCONTRAST,
	ID_DEMO_FEATURES,
};

// ---- Examples -------------------------------------------------------------------

struct Example {
	const wchar_t* name;
	const wchar_t* text;        // the lines, as typed (code points for what isn't ASCII)
	bool piTicks;
};

static const Example g_Examples[] = {
	{ L"&Functions", L"y = sin x\r\ny = x^2/4 - 2\r\ny = tan x\r\ny = 1/x\r\ny = floor(x)/2 {x > 3}\r\n(2, 1)", true },
	{ L"&Parameters", L"y = a sin(bx + c)\r\ny = a\r\nk = 2b", true },
	{ L"&Curves", L"(4cos 3t, 4sin 2t)\r\nr = 2 + 4cos 5\x03B8\r\nr = \x03B8/2\r\n(t cos t, t sin t)", false },
	{ L"&Implicit", L"x^2 + y^2 = 16\r\ny^2 = x^3 - 4x\r\nx^2 - y^2 = 1\r\nsin x + cos y = 0.5", false },
	{ L"I&nequalities", L"y < 4 - x^2\r\nx^2/16 + y^2/4 \x2264 1\r\ny > sin x + 2\r\nr < 2 + cos 6\x03B8", false },
	{ L"&Steps and Domains", L"y = floor x\r\ny = sqrt(9 - x^2)\r\ny = |x - 2| - 3\r\ny = x^3/10 {-4 < x < 4}\r\ny = sign(x) - 5", false },
	{ L"&Errors", L"y = 2 +\r\ny = sin(x\r\nx = y = 1\r\ny = sin()\r\ny = 2 * * 3", false },
};

// ---- State --------------------------------------------------------------------

static CFunctionPlotCtrl g_plot;
static CEdit g_edit;
static CStatusBarCtrl g_status;
static CFont g_editFont;
static HWND g_main;
static bool g_autoSliders = true;
static std::set<std::wstring> g_autoParams;   // the sliders the demo added

// ---- The graphs from the editor's lines --------------------------------------------

static std::vector<std::wstring> EditorLines() {
	int len = g_edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	g_edit.GetWindowText(text.data(), len + 1);
	text.resize(len);
	std::vector<std::wstring> lines;
	size_t start = 0;
	for (;;) {
		size_t end = text.find(L'\n', start);
		std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		lines.push_back(line);
		if (end == std::wstring::npos)
			break;
		start = end + 1;
	}
	return lines;
}

static bool Unknown(const std::wstring& name) {
	for (int i = 0; i < g_plot.GetGraphCount(); i++) {
		FPLGRAPHINFO info;
		if (g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) && info.error == FPLE_UNKNOWNNAME && name == info.szName)
			return true;
	}
	return false;
}

static bool ValidName(const wchar_t* name) {
	if (!*name || wcslen(name) > 8)
		return false;
	for (const wchar_t* p = name; *p; p++)
		if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || (*p >= 0x3B1 && *p <= 0x3C9)))
			return false;
	return true;
}

// Each line is a graph with the line's number as its id (so its color stays with it).
static void SyncGraphs() {
	auto lines = EditorLines();
	for (size_t i = 0; i < lines.size(); i++) {
		FPLGRAPH g{ FPLGM_TEXT, (UINT)i + 1 };
		g.pszText = lines[i].c_str();
		if (!g_plot.SetGraph(g))
			g_plot.AddGraph(g);
	}
	while (g_plot.GetGraphCount() > (int)lines.size())
		g_plot.RemoveGraph(g_plot.GetGraphId(g_plot.GetGraphCount() - 1));

	if (g_autoSliders) {
		// a slider for each name nothing defines
		for (int round = 0; round < 8; round++) {
			bool added = false;
			for (int i = 0; i < g_plot.GetGraphCount(); i++) {
				FPLGRAPHINFO info;
				if (!g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) || info.error != FPLE_UNKNOWNNAME ||
					!ValidName(info.szName))
					continue;
				FPLPARAM p{ info.szName, 1, -10, 10, 0.1, 0 };
				g_plot.SetParameter(p);
				g_autoParams.insert(info.szName);
				added = true;
			}
			if (!added)
				break;
		}
	}
	// and none for a name no longer used (or now defined by a line)
	for (auto it = g_autoParams.begin(); it != g_autoParams.end();) {
		FPLPARAM p{};
		if (!g_plot.GetParameter(it->c_str(), p)) {
			it = g_autoParams.erase(it);
			continue;
		}
		FPLPARAM saved = p;
		std::wstring name = *it;
		saved.pszName = name.c_str();
		g_plot.RemoveParameter(name.c_str());
		if (g_autoSliders && Unknown(name)) {
			g_plot.SetParameter(saved);
			++it;
		}
		else {
			it = g_autoParams.erase(it);
		}
	}
}

// ---- Status bar --------------------------------------------------------------------

static std::wstring Number(double v) {
	wchar_t buf[64];
	swprintf_s(buf, L"%.6g", v);
	return buf;
}

static void ShowView() {
	FPLVIEW v = g_plot.GetView();
	std::wstring text = L"x: " + Number(v.xMin) + L" to " + Number(v.xMax) + L"   y: " + Number(v.yMin) + L" to " + Number(v.yMax);
	g_status.SetText(1, text.c_str());
}

static void ShowTrace(const FPLTRACE& t) {
	if (!t.valid) {
		g_status.SetText(0, L"Drag to pan, wheel to zoom (Ctrl: x only, Shift: y only), point at a graph to trace it");
		return;
	}
	static const wchar_t* Kinds[] = { L"root", L"minimum", L"maximum", L"intersection", L"y-intercept" };
	std::wstring text = L"Line " + std::to_wstring(t.id) + L":  (" + Number(t.x) + L", " + Number(t.y) + L")";
	if (t.feature != FPLF_NONE)
		text += std::wstring(L"  ") + Kinds[t.feature];
	g_status.SetText(0, text.c_str());
}

// The error of the line with the caret, in the third part.
static void ShowLineError() {
	int line = g_edit.LineFromChar(-1);
	FPLGRAPHINFO info;
	std::wstring text;
	if (g_plot.GetGraphInfo((UINT)line + 1, info)) {
		if (info.error != FPLE_NONE && info.error != FPLE_EMPTY)
			text = L"Line " + std::to_wstring(line + 1) + L": " + info.szError + L" (at " + std::to_wstring(info.errorPos + 1) + L")";
		else if (info.kind == FPLK_DEFINITION)
			text = std::wstring(info.szName) + L" = " + Number(info.value);
	}
	g_status.SetText(2, text.c_str());
}

// ---- Commands ------------------------------------------------------------------------

static void ToggleStyle(DWORD style) {
	DWORD s = g_plot.GetStyle();
	g_plot.SetWindowLong(GWL_STYLE, s ^ style);
}

static void LoadExample(int index) {
	auto& e = g_Examples[index];
	for (auto& name : g_autoParams)
		g_plot.RemoveParameter(name.c_str());
	g_autoParams.clear();
	g_plot.RemoveAllGraphs();
	DWORD style = g_plot.GetStyle();
	g_plot.SetWindowLong(GWL_STYLE, e.piTicks ? style | FPLS_PITICKS : style & ~FPLS_PITICKS);
	g_edit.SetWindowText(e.text);   // a multiline edit sends no EN_CHANGE for this
	SyncGraphs();
	ShowLineError();
	g_plot.ResetView();
	ShowView();
}

static void UpdateMenu(HMENU menu) {
	DWORD s = g_plot.GetStyle();
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_DEMO_EQUAL, s & FPLS_EQUALSCALE);
	check(ID_DEMO_PITICKS, s & FPLS_PITICKS);
	check(ID_DEMO_DEGREES, g_plot.GetDegrees() != FALSE);
	check(ID_DEMO_LEGEND, !(s & FPLS_NOLEGEND));
	check(ID_DEMO_SLIDERS, !(s & FPLS_NOSLIDERS));
	check(ID_DEMO_GRID, !(s & FPLS_NOGRID));
	check(ID_DEMO_AXES, !(s & FPLS_NOAXES));
	check(ID_DEMO_TRACE, !(s & FPLS_NOTRACE));
	check(ID_DEMO_AUTOSLIDERS, g_autoSliders);
	check(ID_DEMO_HIGHCONTRAST, g_plot.IsHighContrast());
#ifdef DEMO_WTLHELPER
	check(ID_DEMO_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void ShowFeatures() {
	// the traced graph's (or the first function's) features, in a message box
	FPLTRACE t;
	UINT id = g_plot.GetTrace(t) ? t.id : 0;
	for (int i = 0; !id && i < g_plot.GetGraphCount(); i++) {
		FPLGRAPHINFO info;
		if (g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) && info.kind == FPLK_FUNCTION && info.error == FPLE_NONE)
			id = g_plot.GetGraphId(i);
	}
	if (!id) {
		::MessageBoxW(g_main, L"There's no function to look at.", L"Features", MB_ICONINFORMATION);
		return;
	}
	int n = g_plot.GetFeatures(id, nullptr, 0);
	std::vector<FPLFEATURE> features(n);
	g_plot.GetFeatures(id, features.data(), n);
	static const wchar_t* Kinds[] = { L"Root", L"Minimum", L"Maximum", L"Intersection", L"y-intercept" };
	std::wstring text = L"Line " + std::to_wstring(id) + L", over the view:\n\n";
	for (int i = 0; i < n && i < 40; i++)
		text += std::wstring(Kinds[features[i].kind]) + L"  (" + Number(features[i].x) + L", " + Number(features[i].y) + L")\n";
	if (n == 0)
		text += L"(none)";
	else if (n > 40)
		text += L"...and " + std::to_wstring(n - 40) + L" more";
	::MessageBoxW(g_main, text.c_str(), L"Features", MB_ICONINFORMATION);
}

static void OnCommand(HWND hwnd, UINT id) {
	if (id >= ID_EXAMPLE_FIRST && id < ID_EXAMPLE_FIRST + std::size(g_Examples)) {
		LoadExample(id - ID_EXAMPLE_FIRST);
		return;
	}
	switch (id) {
		case ID_DEMO_EXIT: ::DestroyWindow(hwnd); break;
		case ID_DEMO_HOME: g_plot.ResetView(); ShowView(); break;
		case ID_DEMO_ZOOMIN: g_plot.Zoom(1.5); ShowView(); break;
		case ID_DEMO_ZOOMOUT: g_plot.Zoom(1 / 1.5); ShowView(); break;
		case ID_DEMO_EQUAL: ToggleStyle(FPLS_EQUALSCALE); ShowView(); break;
		case ID_DEMO_PITICKS: ToggleStyle(FPLS_PITICKS); break;
		case ID_DEMO_DEGREES: g_plot.SetDegrees(!g_plot.GetDegrees()); break;
		case ID_DEMO_LEGEND: ToggleStyle(FPLS_NOLEGEND); break;
		case ID_DEMO_SLIDERS: ToggleStyle(FPLS_NOSLIDERS); break;
		case ID_DEMO_GRID: ToggleStyle(FPLS_NOGRID); break;
		case ID_DEMO_AXES: ToggleStyle(FPLS_NOAXES); break;
		case ID_DEMO_TRACE: ToggleStyle(FPLS_NOTRACE); break;
		case ID_DEMO_AUTOSLIDERS: g_autoSliders = !g_autoSliders; SyncGraphs(); break;
		case ID_DEMO_HIGHCONTRAST: g_plot.ForceHighContrast(g_plot.IsHighContrast() ? 0 : 1); break;
		case ID_DEMO_FEATURES: ShowFeatures(); break;
#ifdef DEMO_WTLHELPER
		case ID_DEMO_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
	}
}

// ---- The window --------------------------------------------------------------------

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	g_status.SendMessage(WM_SIZE);
	RECT rs;
	g_status.GetWindowRect(&rs);
	const int statusHeight = rs.bottom - rs.top;
	const UINT dpi = ::GetDpiForWindow(hwnd);
	const int editWidth = ::MulDiv(300, dpi, 96), gap = ::MulDiv(4, dpi, 96);
	const int h = rc.bottom - statusHeight;
	g_edit.SetWindowPos(nullptr, 0, 0, editWidth, h, SWP_NOZORDER | SWP_NOACTIVATE);
	g_plot.SetWindowPos(nullptr, editWidth + gap, 0, rc.right - editWidth - gap, h, SWP_NOZORDER | SWP_NOACTIVATE);
	int parts[] = { rc.right * 45 / 100, rc.right * 75 / 100, -1 };
	g_status.SetParts(3, parts);
}

static void MakeEditFont(HWND hwnd) {
	const UINT dpi = ::GetDpiForWindow(hwnd);
	if (!g_editFont.IsNull())
		g_editFont.DeleteObject();
	g_editFont.CreateFont(-::MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_edit.SetFont(g_editFont);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_edit.Create(hwnd, CWindow::rcDefault, nullptr,
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
				ES_WANTRETURN | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_EDIT);
			MakeEditFont(hwnd);
			g_edit.SetMargins(6, 6);
			g_plot.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | FPLS_EQUALSCALE,
				0, IDC_PLOT);
			g_status.Create(hwnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, IDC_STATUS);
			Layout(hwnd);
			LoadExample(0);
			FPLTRACE none{};
			ShowTrace(none);
			g_edit.SetFocus();
			return 0;
		}
		case WM_SIZE:
			Layout(hwnd);
			return 0;
		case WM_DPICHANGED: {
			auto rc = (const RECT*)lParam;
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			MakeEditFont(hwnd);
			return 0;
		}
		case WM_INITMENUPOPUP:
			UpdateMenu((HMENU)wParam);
			return 0;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_EDIT) {
				if (HIWORD(wParam) == EN_CHANGE) {
					SyncGraphs();
					ShowLineError();
				}
				return 0;
			}
			OnCommand(hwnd, LOWORD(wParam));
			return 0;
		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom == IDC_PLOT) {
				switch (hdr->code) {
					case FPLN_TRACE: ShowTrace(((NMFPLTRACE*)lParam)->trace); break;
					case FPLN_VIEWCHANGED: ShowView(); break;
					case FPLN_PARAMCHANGED: ShowLineError(); break;
				}
			}
			return 0;
		}
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
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
	wc.lpszClassName = L"FunctionPlotDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	HMENU hExamples = ::CreatePopupMenu();
	for (UINT i = 0; i < std::size(g_Examples); i++)
		::AppendMenuW(hExamples, MF_STRING, ID_EXAMPLE_FIRST + i, g_Examples[i].name);
	::AppendMenuW(hFile, MF_POPUP, (UINT_PTR)hExamples, L"&Examples");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HOME, L"&Reset View\tCtrl+Home");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMIN, L"Zoom &In\tCtrl++");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMOUT, L"Zoom &Out\tCtrl+-");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_EQUAL, L"&Equal Scales");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_LEGEND, L"&Legend");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_SLIDERS, L"&Sliders");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_AUTOSLIDERS, L"&Automatic Sliders");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_GRID, L"&Grid");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_AXES, L"A&xes");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_PITICKS, L"&Pi Ticks");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_TRACE, L"&Tracing");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_DEGREES, L"&Degrees");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HIGHCONTRAST, L"&High Contrast Drawing");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_DEMO_DARKMODE, L"Dar&k Mode");
#endif

	HMENU hAnalysis = ::CreatePopupMenu();
	::AppendMenuW(hAnalysis, MF_STRING, ID_DEMO_FEATURES, L"&Features of the Traced Graph...\tF2");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAnalysis, L"&Analysis");

	ACCEL accels[] = {
		{ FVIRTKEY | FCONTROL, VK_HOME, ID_DEMO_HOME },
		{ FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_DEMO_ZOOMIN },
		{ FVIRTKEY | FCONTROL, VK_ADD, ID_DEMO_ZOOMIN },
		{ FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_DEMO_ZOOMOUT },
		{ FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_DEMO_ZOOMOUT },
		{ FVIRTKEY, VK_F2, ID_DEMO_FEATURES },
	};
	HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)std::size(accels));

	HWND hwnd = ::CreateWindowExW(0, L"FunctionPlotDemoWnd", L"FunctionPlot Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1300, 860, nullptr, hMenu, hInstance, nullptr);
	::ShowWindow(hwnd, nCmdShow);
	::UpdateWindow(hwnd);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (!::TranslateAccelerator(hwnd, hAccel, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return (int)msg.wParam;
}
