#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "CellGraphs.h"
#include "TreeListView.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <algorithm>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

using namespace GraphCtrl;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_LIST = 101,
	IDC_TREE = 102,

	ID_FILE_EXIT = 201,

	ID_VIEW_DARKMODE = 301,
	ID_VIEW_PAUSE,
	ID_VIEW_ALTROWS,
	ID_VIEW_EXPLORER,
	ID_VIEW_GRIDLINES,

	IDT_UPDATE = 1,
};

// list view columns
enum {
	COL_NAME,
	COL_PID,
	COL_CPU,
	COL_CPUHISTORY,
	COL_IO,
	COL_MEMORY,
	COL_MEMPARTS,
};

// TreeListView columns
enum {
	TCOL_NAME,
	TCOL_CPUHISTORY,
	TCOL_MEMORY,
	TCOL_THREADS,
};

// ---- Simulated processes ----------------------------------------------------

struct Process {
	std::wstring Name;
	int   Pid;
	int   Group;          // the tree's group
	float Cpu;            // %
	float Io;             // KB/s
	float Private, Shared, Image;   // MB
	float Busy;           // how active it tends to be
	int   Threads;

	float Memory() const { return Private + Shared + Image; }
};

static const wchar_t* g_groups[] = { L"Applications", L"Background processes", L"Services" };
static const wchar_t* g_names[] = {
	L"explorer.exe", L"msedge.exe", L"code.exe", L"devenv.exe", L"Teams.exe", L"OUTLOOK.EXE", L"svchost.exe",
	L"SearchHost.exe", L"RuntimeBroker.exe", L"dwm.exe", L"csrss.exe", L"lsass.exe", L"spoolsv.exe",
	L"MsMpEng.exe", L"OneDrive.exe", L"WINWORD.EXE", L"notepad.exe", L"cmd.exe", L"conhost.exe", L"audiodg.exe",
	L"ctfmon.exe", L"sihost.exe", L"taskhostw.exe", L"WmiPrvSE.exe", L"SecurityHealthService.exe",
};
constexpr float MemoryMax = 2048;   // MB, the meter's full scale

static std::mt19937 g_rng{ 2026 };
static std::vector<Process> g_procs;
static int g_nextPid = 1200;
static CellHistory<int> g_cpuHistory(60), g_ioHistory(40);
static CellGraphs g_listCells, g_treeCells;
static CListViewCtrl g_list;
static CTreeListViewCtrl g_tree;
static HTLITEM g_groupItems[std::size(g_groups)];
static bool g_paused, g_explorer = true, g_gridLines;

static float Random(float lo, float hi) {
	return std::uniform_real_distribution<float>(lo, hi)(g_rng);
}

static Process NewProcess() {
	Process p;
	p.Name = g_names[g_rng() % std::size(g_names)];
	p.Pid = g_nextPid += 4 * (1 + g_rng() % 40);
	p.Group = (int)(g_rng() % std::size(g_groups));
	p.Busy = Random(0.05f, 1);
	p.Cpu = Random(0, 30) * p.Busy;
	p.Io = 0;
	p.Private = Random(20, 900);
	p.Shared = Random(10, 300);
	p.Image = Random(5, 150);
	p.Threads = 4 + g_rng() % 60;
	return p;
}

// a step of every process' random walk; now and then one exits and another starts
static void Simulate() {
	if (g_rng() % 6 == 0 && g_procs.size() > 10)
		g_procs.erase(g_procs.begin() + g_rng() % g_procs.size());
	if (g_rng() % 6 == 0 && g_procs.size() < 40)
		g_procs.push_back(NewProcess());

	for (auto& p : g_procs) {
		p.Cpu = std::clamp(p.Cpu + Random(-12, 12) * p.Busy + (g_rng() % 40 == 0 ? 50 * p.Busy : 0) - p.Cpu * 0.08f, 0.0f, 100.0f);
		p.Io = g_rng() % 4 == 0 ? Random(0, 4000) * p.Busy : p.Io * 0.5f;
		p.Private = std::clamp(p.Private + Random(-15, 18), 5.0f, 1500.0f);
		p.Shared = std::clamp(p.Shared + Random(-3, 3), 5.0f, 400.0f);
	}

	g_cpuHistory.BeginUpdate();
	g_ioHistory.BeginUpdate();
	for (auto& p : g_procs) {
		g_cpuHistory.Push(p.Pid, p.Cpu);
		// a gap now and then, as if a sample was lost
		g_ioHistory.Push(p.Pid, g_rng() % 50 == 0 ? MissingSample : p.Io);
	}
	g_cpuHistory.EndUpdate();
	g_ioHistory.EndUpdate();
}

static const Process* FindProcess(int pid) {
	auto it = std::find_if(g_procs.begin(), g_procs.end(), [=](auto& p) { return p.Pid == pid; });
	return it == g_procs.end() ? nullptr : &*it;
}

// ---- Graph columns ----------------------------------------------------------

static const CellThreshold g_cpuThresholds[] = { { 0, RGB(60, 170, 60) }, { 50, RGB(230, 160, 0) }, { 80, RGB(220, 60, 60) } };
static const CellThreshold g_memThresholds[] = { { 0, CLR_DEFAULT }, { 1024, RGB(220, 150, 0) }, { 1536, RGB(200, 40, 40) } };

// the values in the tooltips
static void FormatKBPerSec(float value, wchar_t* buffer, size_t cch) {
	Format::BytesPerSec(value * 1024, buffer, cch);
}

static void FormatMB(float value, wchar_t* buffer, size_t cch) {
	Format::Bytes(value * 1024 * 1024, buffer, cch);
}

static void SetupGraphs() {
	// the line turns orange and red with the CPU, with a dashed line at 80%
	SparklineStyle cpu;
	cpu.Min = 0;
	cpu.Max = 100;
	cpu.Reference = 80;
	cpu.Thresholds = g_cpuThresholds + 1;   // the default blue below 50%
	cpu.ThresholdCount = std::size(g_cpuThresholds) - 1;
	g_listCells.SetSparkline(COL_CPUHISTORY, cpu);
	g_listCells.SetFormat(COL_CPUHISTORY, Format::Percent);
	g_treeCells.SetFormat(TCOL_CPUHISTORY, Format::Percent);
	ColumnStyle cpuBars;
	cpuBars.Slots = 30;
	cpuBars.Thresholds = g_cpuThresholds;
	cpuBars.ThresholdCount = std::size(g_cpuThresholds);
	cpuBars.Reference = 80;
	g_treeCells.SetColumns(TCOL_CPUHISTORY, cpuBars);

	ColumnStyle io;
	io.AutoScale = true;
	io.MinSpan = 500;
	g_listCells.SetColumns(COL_IO, io);
	g_listCells.SetFormat(COL_IO, FormatKBPerSec);

	MeterStyle mem;
	mem.Max = MemoryMax;
	g_listCells.SetMeter(COL_MEMORY, mem);   // the whole height, with its text
	mem.Thresholds = g_memThresholds;
	mem.ThresholdCount = std::size(g_memThresholds);
	g_treeCells.SetMeter(TCOL_MEMORY, mem);

	StackedStyle parts;
	parts.Total = MemoryMax;
	parts.BarHeight = 12;
	g_listCells.SetStacked(COL_MEMPARTS, parts);
	g_listCells.SetFormat(COL_MEMPARTS, FormatMB);

	g_listCells.SetDataCallback([](const CellRef& cell, CellData& data) {
		if (cell.Item >= g_procs.size())
			return false;
		auto& p = g_procs[cell.Item];
		static float parts[3];
		static wchar_t text[64];
		static std::wstring tip;
		switch (cell.Column) {
			case COL_CPUHISTORY:
				data.Samples = g_cpuHistory.Find(p.Pid);
				break;
			case COL_IO:
				data.Samples = g_ioHistory.Find(p.Pid);
				break;
			case COL_MEMORY:
				data.Value = p.Memory();
				FormatMB(data.Value, text, std::size(text));
				data.Text = text;
				break;
			case COL_MEMPARTS:
				parts[0] = p.Private;
				parts[1] = p.Shared;
				parts[2] = p.Image;
				data.Values = parts;
				data.Count = 3;
				// a tooltip of its own, with the parts' names
				tip.clear();
				for (int i = 0; i < 3; i++) {
					static const wchar_t* names[] = { L"Private", L"Shared", L"Image" };
					FormatMB(parts[i], text, std::size(text));
					tip += std::wstring(i ? L"\n" : L"") + names[i] + L": " + text;
				}
				data.Tooltip = tip.c_str();
				break;
		}
		return true;
	});

	g_treeCells.SetDataCallback([](const CellRef& cell, CellData& data) {
		auto p = FindProcess((int)g_tree.GetItemData((HTLITEM)cell.Item));
		if (!p)
			return false;   // a group
		static wchar_t text[64];
		if (cell.Column == TCOL_CPUHISTORY) {
			data.Samples = g_cpuHistory.Find(p->Pid);
		}
		else {
			data.Value = p->Memory();
			FormatMB(data.Value, text, std::size(text));
			data.Text = text;
		}
		return true;
	});
}

// ---- List view ----------------------------------------------------------------

static void ApplyListTheme() {
	::SetWindowTheme(g_list, g_explorer ? L"Explorer" : nullptr, nullptr);
	g_list.SetExtendedListViewStyle(g_gridLines ? LVS_EX_GRIDLINES : 0, LVS_EX_GRIDLINES);
	g_list.Invalidate();
}

static void CreateList(HWND parent) {
	g_list.Create(parent, CWindow::rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
		WS_EX_CLIENTEDGE, IDC_LIST);
	g_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
	UINT dpi = ::GetDpiForWindow(parent);
	auto col = [&](int i, LPCWSTR name, int width, int fmt = LVCFMT_LEFT) {
		g_list.InsertColumn(i, name, fmt, ::MulDiv(width, dpi, 96));
	};
	col(COL_NAME, L"Name", 190);
	col(COL_PID, L"PID", 60, LVCFMT_RIGHT);
	col(COL_CPU, L"CPU", 55, LVCFMT_RIGHT);
	col(COL_CPUHISTORY, L"CPU History", 140);
	col(COL_IO, L"I/O", 110);
	col(COL_MEMORY, L"Memory", 120);
	col(COL_MEMPARTS, L"Private / Shared / Image", 160);
	ApplyListTheme();
}

static void GetListText(NMLVDISPINFO* di) {
	auto& item = di->item;
	if (!(item.mask & LVIF_TEXT) || item.iItem < 0 || item.iItem >= (int)g_procs.size())
		return;
	auto& p = g_procs[item.iItem];
	CString text;
	switch (item.iSubItem) {
		case COL_NAME:       text = p.Name.c_str(); break;
		case COL_PID:        text.Format(L"%d", p.Pid); break;
		case COL_CPU:
		case COL_CPUHISTORY: text.Format(L"%.1f%%", p.Cpu); break;   // the graph cell's text: for screen readers
		case COL_IO:         text.Format(L"%.0f KB/s", p.Io); break;
		case COL_MEMORY:     text.Format(L"%.0f MB", p.Memory()); break;
		case COL_MEMPARTS:   text.Format(L"%.0f / %.0f / %.0f MB", p.Private, p.Shared, p.Image); break;
	}
	wcsncpy_s(item.pszText, item.cchTextMax, text, _TRUNCATE);
}

// ---- TreeListView -------------------------------------------------------------

static void CreateTree(HWND parent) {
	g_tree.Create(parent, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, IDC_TREE);
	UINT dpi = ::GetDpiForWindow(parent);
	g_tree.InsertColumn(TCOL_NAME, L"Name", ::MulDiv(240, dpi, 96));
	g_tree.InsertColumn(TCOL_CPUHISTORY, L"CPU History", ::MulDiv(160, dpi, 96));
	g_tree.InsertColumn(TCOL_MEMORY, L"Memory", ::MulDiv(140, dpi, 96));
	g_tree.InsertColumn(TCOL_THREADS, L"Threads", ::MulDiv(70, dpi, 96), LVCFMT_RIGHT);
	for (size_t i = 0; i < std::size(g_groups); i++)
		g_groupItems[i] = g_tree.InsertItem(TLV_ROOT, TLV_LAST, g_groups[i], -1, 0);
}

// the tree's items follow the processes: added, removed, their text updated
static void UpdateTree() {
	g_tree.BeginUpdate();
	for (auto group : g_groupItems) {
		for (auto item = g_tree.GetChildItem(group); item;) {
			auto next = g_tree.GetNextSiblingItem(item);
			if (!FindProcess((int)g_tree.GetItemData(item)))
				g_tree.DeleteItem(item);
			item = next;
		}
	}
	for (auto& p : g_procs) {
		auto group = g_groupItems[p.Group];
		HTLITEM item = g_tree.GetChildItem(group);
		while (item && (int)g_tree.GetItemData(item) != p.Pid)
			item = g_tree.GetNextSiblingItem(item);
		if (!item)
			item = g_tree.InsertItem(group, TLV_LAST, (p.Name + L" (" + std::to_wstring(p.Pid) + L")").c_str(), -1, p.Pid);
		CString text;
		text.Format(L"%.1f%%", p.Cpu);
		g_tree.SetItemText(item, TCOL_CPUHISTORY, text);
		text.Format(L"%.0f MB", p.Memory());
		g_tree.SetItemText(item, TCOL_MEMORY, text);
		text.Format(L"%d", p.Threads);
		g_tree.SetItemText(item, TCOL_THREADS, text);
	}
	g_tree.EndUpdate();
}

// ---- Window -------------------------------------------------------------------

static void Update() {
	Simulate();
	g_list.SetItemCountEx((int)g_procs.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
	g_list.Invalidate(FALSE);
	UpdateTree();
}

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	int split = rc.bottom * 55 / 100;
	g_list.SetWindowPos(nullptr, 0, 0, rc.right, split, SWP_NOZORDER | SWP_NOACTIVATE);
	g_tree.SetWindowPos(nullptr, 0, split, rc.right, rc.bottom - split, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_VIEW_PAUSE, g_paused);
	check(ID_VIEW_ALTROWS, g_tree.GetAlternateRowColor() != CLR_NONE);
	check(ID_VIEW_EXPLORER, g_explorer);
	check(ID_VIEW_GRIDLINES, g_gridLines);
#ifdef DEMO_WTLHELPER
	check(ID_VIEW_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_FILE_EXIT:
			::DestroyWindow(hwnd);
			return;
		case ID_VIEW_PAUSE:
			g_paused = !g_paused;
			break;
		case ID_VIEW_ALTROWS:
		{
			bool dark = false;
#ifdef DEMO_WTLHELPER
			dark = WTLHelper::IsDarkMode();
#endif
			g_tree.SetAlternateRowColor(g_tree.GetAlternateRowColor() != CLR_NONE ? CLR_NONE : dark ? RGB(44, 44, 48) : RGB(235, 243, 255));
			g_tree.Invalidate();
			break;
		}
		case ID_VIEW_EXPLORER:
			g_explorer = !g_explorer;
			ApplyListTheme();
			break;
		case ID_VIEW_GRIDLINES:
			g_gridLines = !g_gridLines;
			ApplyListTheme();
			break;
#ifdef DEMO_WTLHELPER
		case ID_VIEW_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			if (g_tree.GetAlternateRowColor() != CLR_NONE)
				g_tree.SetAlternateRowColor(WTLHelper::IsDarkMode() ? RGB(44, 44, 48) : RGB(235, 243, 255));
			break;
#endif
		default:
			return;
	}
	UpdateChecks(::GetMenu(hwnd));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			SetupGraphs();
			for (int i = 0; i < 24; i++)
				g_procs.push_back(NewProcess());
			CreateList(hwnd);
			CreateTree(hwnd);
			// some history to start with
			for (int i = 0; i < 30; i++)
				Simulate();
			Update();
			g_tree.ExpandAll();
			g_listCells.EnableTooltips(g_list);
			g_treeCells.EnableTooltips(g_tree.GetListCtrl(), [](int row) { return (UINT_PTR)g_tree.GetVisibleItem(row); });
			::SetTimer(hwnd, IDT_UPDATE, 500, nullptr);
			g_list.SetFocus();
			return 0;

		case WM_SIZE:
			Layout(hwnd);
			return 0;

		case WM_TIMER:
			if (wParam == IDT_UPDATE && !g_paused)
				Update();
			return 0;

		case WM_DPICHANGED:
		{
			auto rc = (const RECT*)lParam;
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}

		case WM_NOTIFY:
		{
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom == IDC_LIST) {
				if (hdr->code == LVN_GETDISPINFO)
					GetListText((NMLVDISPINFO*)lParam);
				else if (hdr->code == NM_CUSTOMDRAW)
					return g_listCells.CustomDraw((NMLVCUSTOMDRAW*)lParam);
			}
			else if (hdr->idFrom == IDC_TREE && hdr->code == NM_CUSTOMDRAW) {
				auto cd = (NMTLVCUSTOMDRAW*)lParam;
				return g_treeCells.CustomDraw(&cd->nmcd, cd->iSubItem, (UINT_PTR)cd->hItem);
			}
			return 0;
		}

		case WM_COMMAND:
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_DESTROY:
			::KillTimer(hwnd, IDT_UPDATE);
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"CellGraphsDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	::AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_VIEW_DARKMODE, L"&Dark Mode");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
#endif
	::AppendMenuW(hView, MF_STRING, ID_VIEW_PAUSE, L"&Pause");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_EXPLORER, L"List: &Explorer Theme");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_GRIDLINES, L"List: &Grid Lines");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_ALTROWS, L"Tree: &Alternate Rows");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

	HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"Cell Graphs Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 980, 720, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);
	::ShowWindow(hwnd, nCmdShow);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	_Module.Term();
	return 0;
}
