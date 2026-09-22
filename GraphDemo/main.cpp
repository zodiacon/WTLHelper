#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "GraphControl.h"

#pragma comment(lib, "GraphControl.lib")
#pragma comment(lib, "comctl32.lib")

#include <iterator>

using namespace GraphCtrl;

// Control IDs
static constexpr int IDC_CPU    = 101;
static constexpr int IDC_MEM    = 102;
static constexpr int IDC_STATUS = 103;

// Commands
static constexpr int IDC_PAUSE     = 200;
static constexpr int IDC_CLEAR     = 201;
static constexpr int IDC_AUTOSCALE = 202;
static constexpr int IDC_HIST60    = 203;
static constexpr int IDC_HIST120   = 204;
static constexpr int IDC_HIST300   = 205;
static constexpr int IDC_RATE250   = 206;
static constexpr int IDC_RATE500   = 207;
static constexpr int IDC_RATE1000  = 208;
static constexpr int IDC_DARK      = 209;
static constexpr int IDC_LIGHT     = 210;
static constexpr int IDC_EXIT      = 211;

static constexpr UINT_PTR TIMER_MEMORY = 1;

static CGraphControl g_cpu;      // pull mode: the control asks for samples
static CGraphControl g_mem;      // push mode: this app feeds it from its own timer
static HWND          g_status = nullptr;

static SeriesId g_cpuTotal  = InvalidSeries;
static SeriesId g_cpuKernel = InvalidSeries;
static SeriesId g_memInUse  = InvalidSeries;

static bool  g_paused    = false;
static bool  g_autoScale = false;
static UINT  g_interval  = 1000;
static size_t g_history  = 60;
static float g_totalPhys = 0.0f;

// ---- Data sources -----------------------------------------------------------

// Percentage of each interval spent busy, and the part of that spent in kernel.
static void SampleCpu(float& total, float& kernel) {
    static ULONGLONG s_prevIdle = 0, s_prevKernel = 0, s_prevUser = 0;
    static bool s_first = true;

    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        total = kernel = 0.0f;
        return;
    }

    auto toU64 = [](const FILETIME& ft) {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const ULONGLONG idle = toU64(idleFt), krnl = toU64(kernelFt), user = toU64(userFt);

    if (s_first) {
        s_first = false;
        s_prevIdle = idle; s_prevKernel = krnl; s_prevUser = user;
        total = kernel = 0.0f;
        return;
    }

    // The kernel time reported by GetSystemTimes already includes idle time.
    const ULONGLONG dIdle   = idle - s_prevIdle;
    const ULONGLONG dKernel = krnl - s_prevKernel;
    const ULONGLONG dUser   = user - s_prevUser;
    s_prevIdle = idle; s_prevKernel = krnl; s_prevUser = user;

    const ULONGLONG dTotal = dKernel + dUser;
    if (dTotal == 0) {
        total = kernel = 0.0f;
        return;
    }

    total  = static_cast<float>((dTotal - dIdle) * 100.0 / dTotal);
    kernel = static_cast<float>((dKernel - dIdle) * 100.0 / dTotal);
}

static float SampleMemoryInUse() {
    MEMORYSTATUSEX ms{ sizeof(ms) };
    if (!GlobalMemoryStatusEx(&ms)) return 0.0f;
    return static_cast<float>(ms.ullTotalPhys - ms.ullAvailPhys);
}

static float TotalPhysicalMemory() {
    MEMORYSTATUSEX ms{ sizeof(ms) };
    return GlobalMemoryStatusEx(&ms) ? static_cast<float>(ms.ullTotalPhys) : 1.0f;
}

// ---- UI ---------------------------------------------------------------------

static void UpdateStatus() {
    if (!g_status) return;

    const Series* cpu = g_cpu.GetData().GetSeries(g_cpuTotal);
    const Series* mem = g_mem.GetData().GetSeries(g_memInUse);

    wchar_t used[32] = L"-", peak[32] = L"-";
    if (mem && mem->Count()) {
        Format::Bytes(mem->Last(), used, std::size(used));
        Format::Bytes(mem->Max(), peak, std::size(peak));
    }

    wchar_t buf[256];
    swprintf_s(buf,
        L"CPU %.0f%% (avg %.0f%%)  |  Memory %s (peak %s)  |  history %zu samples  |  every %u ms  |  %s",
        cpu ? cpu->Last() : 0.0f,
        cpu ? cpu->Average() : 0.0f,
        used, peak,
        g_history, g_interval,
        g_paused ? L"PAUSED" : L"running");
    SetWindowText(g_status, buf);
}

static void SyncMenu(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) return;

    CheckMenuItem(menu, IDC_PAUSE, MF_BYCOMMAND | (g_paused ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDC_AUTOSCALE, MF_BYCOMMAND | (g_autoScale ? MF_CHECKED : MF_UNCHECKED));

    const int hist = (g_history == 60) ? IDC_HIST60 : (g_history == 120) ? IDC_HIST120 : IDC_HIST300;
    CheckMenuRadioItem(menu, IDC_HIST60, IDC_HIST300, hist, MF_BYCOMMAND);

    const int rate = (g_interval == 250) ? IDC_RATE250 : (g_interval == 500) ? IDC_RATE500 : IDC_RATE1000;
    CheckMenuRadioItem(menu, IDC_RATE250, IDC_RATE1000, rate, MF_BYCOMMAND);
}

static void SetPaused(HWND hwnd, bool paused) {
    g_paused = paused;
    if (paused) {
        g_cpu.Pause();
        KillTimer(hwnd, TIMER_MEMORY);
    }
    else {
        g_cpu.Resume();
        SetTimer(hwnd, TIMER_MEMORY, g_interval, nullptr);
    }
    SyncMenu(hwnd);
    UpdateStatus();
}

static void SetInterval(HWND hwnd, UINT ms) {
    g_interval = ms;
    g_cpu.SetUpdateInterval(ms);
    if (!g_paused) SetTimer(hwnd, TIMER_MEMORY, ms, nullptr);

    // The time-span caption follows the history length and the sample rate.
    wchar_t span[32];
    swprintf_s(span, L"%.0f seconds", g_history * ms / 1000.0);
    g_cpu.SetTimeSpanText(span);
    g_mem.SetTimeSpanText(span);

    SyncMenu(hwnd);
    UpdateStatus();
}

static void SetHistory(HWND hwnd, size_t samples) {
    g_history = samples;
    g_cpu.SetHistoryLength(samples);
    g_mem.SetHistoryLength(samples);
    SetInterval(hwnd, g_interval);   // refreshes the caption and the menu
}

static void LayoutChildren(int w, int h) {
    const int sbH = 22;
    const int graphH = (h - sbH) / 2;

    if (g_cpu.m_hWnd)
        SetWindowPos(g_cpu.m_hWnd, nullptr, 0, 0, w, graphH, SWP_NOZORDER);
    if (g_mem.m_hWnd)
        SetWindowPos(g_mem.m_hWnd, nullptr, 0, graphH, w, h - sbH - graphH, SWP_NOZORDER);
    if (g_status)
        SendMessage(g_status, WM_SIZE, 0, 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        const int sbH = 22;
        const int graphH = (rc.bottom - sbH) / 2;

        g_totalPhys = TotalPhysicalMemory();

        // ---- CPU graph: two overlaid series, fixed 0..100, pull mode ----
        g_cpu.Create(hwnd, 0, 0, rc.right, graphH, WS_CHILD | WS_VISIBLE,
                     GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS, IDC_CPU);

        SeriesStyle total;
        total.LineColor = RGB(17, 125, 187);
        total.FillColor = RGB(105, 185, 235);
        g_cpuTotal = g_cpu.AddSeries(L"Total", total);

        SeriesStyle kernel;
        kernel.LineColor   = RGB(12, 80, 125);
        kernel.FillColor   = RGB(40, 105, 160);
        kernel.FillOpacity = 0.55f;
        g_cpuKernel = g_cpu.AddSeries(L"Kernel", kernel);

        g_cpu.SetTitle(L"% Utilization");
        g_cpu.SetRange(0, 100);
        g_cpu.SetValueFormatter(Format::Percent);
        g_cpu.SetSampleSource([](std::vector<float>& v) {
            float total = 0, kernel = 0;
            SampleCpu(total, kernel);
            v[0] = total;
            v[1] = kernel;
        });

        // ---- Memory graph: one series, scaled to installed RAM, push mode ----
        g_mem.Create(hwnd, 0, graphH, rc.right, rc.bottom - sbH - graphH,
                     WS_CHILD | WS_VISIBLE,
                     GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS, IDC_MEM);

        SeriesStyle inUse;
        inUse.LineColor = RGB(139, 92, 196);
        inUse.FillColor = RGB(165, 130, 215);
        g_memInUse = g_mem.AddSeries(L"In use", inUse);

        g_mem.SetTitle(L"Memory in use");
        g_mem.SetRange(0, g_totalPhys);
        g_mem.SetValueFormatter(Format::Bytes);

        g_status = CreateWindowEx(0, STATUSCLASSNAME, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_STATUS, nullptr, nullptr);

        SetHistory(hwnd, g_history);   // also starts the timers via SetInterval
        return 0;
    }

    case WM_SIZE:
        LayoutChildren(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_MEMORY) {
            g_mem.PushSample(g_memInUse, SampleMemoryInUse());
            UpdateStatus();
        }
        return 0;

    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->code == GCN_RANGECHANGED) {
            auto* rn = reinterpret_cast<GRAPHRANGENOTIFY*>(lParam);
            wchar_t maximum[32];
            if (nm->idFrom == IDC_CPU) Format::Percent(rn->Max, maximum, std::size(maximum));
            else                       Format::Bytes(rn->Max, maximum, std::size(maximum));

            wchar_t buf[128];
            swprintf_s(buf, L"%s graph auto-scaled to %s",
                       nm->idFrom == IDC_CPU ? L"CPU" : L"Memory", maximum);
            SetWindowText(g_status, buf);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PAUSE:
            SetPaused(hwnd, !g_paused);
            break;
        case IDC_CLEAR:
            g_cpu.Clear();
            g_mem.Clear();
            UpdateStatus();
            break;
        case IDC_AUTOSCALE:
            g_autoScale = !g_autoScale;
            g_cpu.SetAutoScale(g_autoScale);
            g_mem.SetAutoScale(g_autoScale);
            if (!g_autoScale) {
                g_cpu.SetRange(0, 100);
                g_mem.SetRange(0, g_totalPhys);
            }
            SyncMenu(hwnd);
            break;
        case IDC_HIST60:   SetHistory(hwnd, 60);  break;
        case IDC_HIST120:  SetHistory(hwnd, 120); break;
        case IDC_HIST300:  SetHistory(hwnd, 300); break;
        case IDC_RATE250:  SetInterval(hwnd, 250);  break;
        case IDC_RATE500:  SetInterval(hwnd, 500);  break;
        case IDC_RATE1000: SetInterval(hwnd, 1000); break;
        case IDC_DARK:
            g_cpu.SetTheme(GraphTheme::Dark());
            g_mem.SetTheme(GraphTheme::Dark());
            break;
        case IDC_LIGHT:
            g_cpu.SetTheme(GraphTheme::Light());
            g_mem.SetTheme(GraphTheme::Light());
            break;
        case IDC_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_MEMORY);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GraphDemoWindow";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"GraphDemoWindow", L"GraphControl Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 640,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, IDC_EXIT, L"E&xit\tAlt+F4");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");

    HMENU hGraph = CreatePopupMenu();
    AppendMenu(hGraph, MF_STRING, IDC_PAUSE,     L"&Pause\tSpace");
    AppendMenu(hGraph, MF_STRING, IDC_CLEAR,     L"&Clear\tC");
    AppendMenu(hGraph, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hGraph, MF_STRING, IDC_AUTOSCALE, L"&Auto-scale\tA");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hGraph, L"&Graph");

    HMENU hHistory = CreatePopupMenu();
    AppendMenu(hHistory, MF_STRING, IDC_HIST60,  L"&60 samples");
    AppendMenu(hHistory, MF_STRING, IDC_HIST120, L"&120 samples");
    AppendMenu(hHistory, MF_STRING, IDC_HIST300, L"&300 samples");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHistory, L"&History");

    HMENU hRate = CreatePopupMenu();
    AppendMenu(hRate, MF_STRING, IDC_RATE250,  L"&250 ms");
    AppendMenu(hRate, MF_STRING, IDC_RATE500,  L"&500 ms");
    AppendMenu(hRate, MF_STRING, IDC_RATE1000, L"&1 second");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hRate, L"&Rate");

    HMENU hView = CreatePopupMenu();
    AppendMenu(hView, MF_STRING, IDC_DARK,  L"&Dark theme\tD");
    AppendMenu(hView, MF_STRING, IDC_LIGHT, L"&Light theme\tL");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

    SetMenu(hwnd, hMenu);

    ACCEL accels[] = {
        { FVIRTKEY, VK_SPACE, IDC_PAUSE },
        { FVIRTKEY,      'C', IDC_CLEAR },
        { FVIRTKEY,      'A', IDC_AUTOSCALE },
        { FVIRTKEY,      'D', IDC_DARK },
        { FVIRTKEY,      'L', IDC_LIGHT },
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, (int)std::size(accels));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
