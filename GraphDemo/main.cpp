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
static constexpr int IDC_PAUSE      = 200;
static constexpr int IDC_CLEAR      = 201;
static constexpr int IDC_AUTOSCALE  = 202;
static constexpr int IDC_HIST60     = 203;
static constexpr int IDC_HIST120    = 204;
static constexpr int IDC_HIST300    = 205;
static constexpr int IDC_RATE250    = 206;
static constexpr int IDC_RATE500    = 207;
static constexpr int IDC_RATE1000   = 208;
static constexpr int IDC_DARK       = 209;
static constexpr int IDC_LIGHT      = 210;
static constexpr int IDC_EXIT       = 211;
static constexpr int IDC_GRID       = 212;
static constexpr int IDC_SCROLLGRID = 213;
static constexpr int IDC_FILL       = 214;
static constexpr int IDC_LEGEND     = 215;
static constexpr int IDC_STACKED    = 216;
static constexpr int IDC_CROSSHAIR  = 217;

static constexpr UINT_PTR TIMER_MEMORY = 1;

static CGraphControl g_cpu;      // pull mode: the control asks for samples
static CGraphControl g_mem;      // push mode: this app feeds it from its own timer
static HWND          g_status = nullptr;

static SeriesId g_cpuUser   = InvalidSeries;
static SeriesId g_cpuKernel = InvalidSeries;
static SeriesId g_memInUse  = InvalidSeries;

static bool   g_paused    = false;
static UINT   g_interval  = 1000;
static size_t g_history   = 60;
static float  g_totalPhys = 0.0f;
static int    g_hover     = -1;
static UINT_PTR g_hoverFrom = 0;   // which graph the hover reading belongs to

// The style both graphs are created with, and the one the View menu edits.
static DWORD g_style = GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS |
                       GCS_LEGEND | GCS_TOOLTIP;

// ---- Data sources -----------------------------------------------------------

// User and kernel shares of each interval. They sum to total utilization, so
// the two series stack into the familiar Task Manager total.
static void SampleCpu(float& user, float& kernel) {
    static ULONGLONG s_prevIdle = 0, s_prevKernel = 0, s_prevUser = 0;
    static bool s_first = true;

    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        user = kernel = 0.0f;
        return;
    }

    auto toU64 = [](const FILETIME& ft) {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    const ULONGLONG idle = toU64(idleFt), krnl = toU64(kernelFt), usr = toU64(userFt);

    if (s_first) {
        s_first = false;
        s_prevIdle = idle; s_prevKernel = krnl; s_prevUser = usr;
        user = kernel = 0.0f;
        return;
    }

    // The kernel time reported by GetSystemTimes already includes idle time.
    const ULONGLONG dIdle   = idle - s_prevIdle;
    const ULONGLONG dKernel = krnl - s_prevKernel;
    const ULONGLONG dUser   = usr - s_prevUser;
    s_prevIdle = idle; s_prevKernel = krnl; s_prevUser = usr;

    const ULONGLONG dTotal = dKernel + dUser;
    if (dTotal == 0) {
        user = kernel = 0.0f;
        return;
    }

    user   = static_cast<float>(dUser * 100.0 / dTotal);
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

    const Series* user   = g_cpu.GetData().GetSeries(g_cpuUser);
    const Series* kernel = g_cpu.GetData().GetSeries(g_cpuKernel);
    const Series* mem    = g_mem.GetData().GetSeries(g_memInUse);

    const float cpuTotal = (user ? user->Last() : 0.0f) + (kernel ? kernel->Last() : 0.0f);

    wchar_t used[32] = L"-", peak[32] = L"-";
    if (mem && mem->Count()) {
        Format::Bytes(mem->Last(), used, std::size(used));
        Format::Bytes(mem->Max(), peak, std::size(peak));
    }

    wchar_t hover[48] = L"";
    if (g_hover >= 0)
        swprintf_s(hover, L"  |  %s -%.1f s",
                   g_hoverFrom == IDC_CPU ? L"CPU at" : L"memory at",
                   g_hover * g_interval / 1000.0);

    wchar_t buf[320];
    swprintf_s(buf,
        L"CPU %.0f%% (kernel %.0f%%)  |  Memory %s (peak %s)  |  history %zu samples  |  every %u ms  |  %s%s",
        cpuTotal,
        kernel ? kernel->Last() : 0.0f,
        used, peak,
        g_history, g_interval,
        g_paused ? L"PAUSED" : L"running",
        hover);
    SetWindowText(g_status, buf);
}

static void ApplyStyle() {
    g_cpu.SetGraphStyle(g_style);
    g_mem.SetGraphStyle(g_style);

    if (!(g_style & GCS_AUTOSCALE)) {
        g_cpu.SetRange(0, 100);
        g_mem.SetRange(0, g_totalPhys);
    }
}

static void SyncMenu(HWND hwnd) {
    HMENU menu = GetMenu(hwnd);
    if (!menu) return;

    auto check = [menu](int id, bool on) {
        CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };

    check(IDC_PAUSE,      g_paused);
    check(IDC_AUTOSCALE,  (g_style & GCS_AUTOSCALE) != 0);
    check(IDC_GRID,       (g_style & GCS_GRID) != 0);
    check(IDC_SCROLLGRID, (g_style & GCS_SCROLLGRID) != 0);
    check(IDC_FILL,       (g_style & GCS_FILL) != 0);
    check(IDC_LEGEND,     (g_style & GCS_LEGEND) != 0);
    check(IDC_STACKED,    (g_style & GCS_STACKED) != 0);
    check(IDC_CROSSHAIR,  (g_style & GCS_TOOLTIP) != 0);

    const int hist = (g_history == 60) ? IDC_HIST60 : (g_history == 120) ? IDC_HIST120 : IDC_HIST300;
    CheckMenuRadioItem(menu, IDC_HIST60, IDC_HIST300, hist, MF_BYCOMMAND);

    const int rate = (g_interval == 250) ? IDC_RATE250 : (g_interval == 500) ? IDC_RATE500 : IDC_RATE1000;
    CheckMenuRadioItem(menu, IDC_RATE250, IDC_RATE1000, rate, MF_BYCOMMAND);
}

static void ToggleStyle(HWND hwnd, DWORD flag) {
    g_style ^= flag;
    ApplyStyle();
    SyncMenu(hwnd);
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

        // ---- CPU graph: two series that stack into total, 0..100, pull mode ----
        g_cpu.Create(hwnd, 0, 0, rc.right, graphH, WS_CHILD | WS_VISIBLE, g_style, IDC_CPU);

        SeriesStyle user;
        user.LineColor = RGB(17, 125, 187);
        user.FillColor = RGB(105, 185, 235);
        g_cpuUser = g_cpu.AddSeries(L"User", user);

        SeriesStyle kernel;
        kernel.LineColor   = RGB(12, 80, 125);
        kernel.FillColor   = RGB(40, 105, 160);
        kernel.FillOpacity = 0.55f;
        g_cpuKernel = g_cpu.AddSeries(L"Kernel", kernel);

        g_cpu.SetTitle(L"% Utilization");
        g_cpu.SetRange(0, 100);
        g_cpu.SetValueFormatter(Format::Percent);
        g_cpu.SetSampleSource([](std::vector<float>& v) {
            float user = 0, kernel = 0;
            SampleCpu(user, kernel);
            v[0] = user;
            v[1] = kernel;
        });

        // ---- Memory graph: one series, scaled to installed RAM, push mode ----
        g_mem.Create(hwnd, 0, graphH, rc.right, rc.bottom - sbH - graphH,
                     WS_CHILD | WS_VISIBLE, g_style, IDC_MEM);

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
        switch (nm->code) {
        case GCN_RANGECHANGED: {
            auto* rn = reinterpret_cast<GRAPHRANGENOTIFY*>(lParam);
            wchar_t maximum[32];
            if (nm->idFrom == IDC_CPU) Format::Percent(rn->Max, maximum, std::size(maximum));
            else                       Format::Bytes(rn->Max, maximum, std::size(maximum));

            wchar_t buf[128];
            swprintf_s(buf, L"%s graph auto-scaled to %s",
                       nm->idFrom == IDC_CPU ? L"CPU" : L"Memory", maximum);
            SetWindowText(g_status, buf);
            break;
        }
        case GCN_HOVERSAMPLE: {
            auto* hn = reinterpret_cast<GRAPHHOVERNOTIFY*>(lParam);
            // Moving between the two graphs means the one being left reports -1
            // after the one being entered reports its sample; ignore that.
            if (hn->SampleIndex >= 0) {
                g_hover     = hn->SampleIndex;
                g_hoverFrom = nm->idFrom;
            }
            else if (nm->idFrom == g_hoverFrom) {
                g_hover     = -1;
                g_hoverFrom = 0;
            }
            UpdateStatus();
            break;
        }
        case GCN_GETTOOLTIP: {
            // Prepend how far back the hovered sample is, then keep the default lines.
            auto* tn = reinterpret_cast<GRAPHTOOLTIPNOTIFY*>(lParam);
            wchar_t buf[256];
            swprintf_s(buf, L"-%.1f s\n%s", tn->SampleIndex * g_interval / 1000.0, tn->SzText);
            wcsncpy_s(tn->SzText, buf, _TRUNCATE);
            break;
        }
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
        case IDC_AUTOSCALE:  ToggleStyle(hwnd, GCS_AUTOSCALE);  break;
        case IDC_GRID:       ToggleStyle(hwnd, GCS_GRID);       break;
        case IDC_SCROLLGRID: ToggleStyle(hwnd, GCS_SCROLLGRID); break;
        case IDC_FILL:       ToggleStyle(hwnd, GCS_FILL);       break;
        case IDC_LEGEND:     ToggleStyle(hwnd, GCS_LEGEND);     break;
        case IDC_STACKED:    ToggleStyle(hwnd, GCS_STACKED);    break;
        case IDC_CROSSHAIR:  ToggleStyle(hwnd, GCS_TOOLTIP);    break;
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
    AppendMenu(hView, MF_STRING, IDC_GRID,       L"&Grid\tG");
    AppendMenu(hView, MF_STRING, IDC_SCROLLGRID, L"Sc&rolling grid\tR");
    AppendMenu(hView, MF_STRING, IDC_FILL,       L"Area &fill\tF");
    AppendMenu(hView, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hView, MF_STRING, IDC_LEGEND,     L"L&egend\tE");
    AppendMenu(hView, MF_STRING, IDC_STACKED,    L"&Stacked\tS");
    AppendMenu(hView, MF_STRING, IDC_CROSSHAIR,  L"Hover &crosshair\tT");
    AppendMenu(hView, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hView, MF_STRING, IDC_DARK,       L"&Dark theme\tD");
    AppendMenu(hView, MF_STRING, IDC_LIGHT,      L"&Light theme\tL");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

    SetMenu(hwnd, hMenu);

    ACCEL accels[] = {
        { FVIRTKEY, VK_SPACE, IDC_PAUSE },
        { FVIRTKEY,      'C', IDC_CLEAR },
        { FVIRTKEY,      'A', IDC_AUTOSCALE },
        { FVIRTKEY,      'G', IDC_GRID },
        { FVIRTKEY,      'R', IDC_SCROLLGRID },
        { FVIRTKEY,      'F', IDC_FILL },
        { FVIRTKEY,      'E', IDC_LEGEND },
        { FVIRTKEY,      'S', IDC_STACKED },
        { FVIRTKEY,      'T', IDC_CROSSHAIR },
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
