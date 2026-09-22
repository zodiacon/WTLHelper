#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "GraphControl.h"
#include "GraphGrid.h"

#pragma comment(lib, "GraphControl.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <iterator>

using namespace GraphCtrl;

// Control IDs
static constexpr int IDC_CPU    = 101;
static constexpr int IDC_MEM    = 102;
static constexpr int IDC_STATUS = 103;
static constexpr int IDC_CORES  = 104;   // base id for the per-core tiles

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
static constexpr int IDC_PERCORE    = 218;
static constexpr int IDC_BARS       = 219;
static constexpr int IDC_VALUE      = 220;
static constexpr int IDC_GAP        = 221;
static constexpr int IDC_COPY       = 222;
static constexpr int IDC_SAVEPNG    = 223;
static constexpr int IDC_STALL      = 224;
static constexpr int IDC_ANIMATE    = 225;
static constexpr int IDC_BIGFONT    = 226;

static constexpr UINT_PTR TIMER_MEMORY = 1;

static CGraphControl g_cpu;      // pull mode: the control asks for samples
static CGraphControl g_mem;      // push mode: this app feeds it from its own timer
static CGraphGrid    g_cores;    // one tile per logical processor
static HWND          g_status = nullptr;

static SeriesId g_cpuUser   = InvalidSeries;
static SeriesId g_cpuKernel = InvalidSeries;
static SeriesId g_memInUse  = InvalidSeries;

static bool   g_paused    = false;
static UINT   g_interval  = 1000;
static size_t g_history   = 60;
static float  g_totalPhys = 0.0f;
static int    g_hover     = -1;
static bool   g_showCores = false;
static DWORD  g_coreCount = 1;
static CGraphControl* g_menuTarget = nullptr;   // graph the context menu came from
static bool   g_animate  = true;
static bool   g_bigFont  = false;
static UINT_PTR g_hoverFrom = 0;   // which graph the hover reading belongs to

// The style both graphs are created with, and the one the View menu edits.
static DWORD g_style = GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_AXISLABELS |
                       GCS_LEGEND | GCS_TOOLTIP | GCS_VALUEOVERLAY;

// The tiles are small, so they skip the axis captions and the legend.
static DWORD CoreTileStyle() {
    return (g_style & (GCS_GRID | GCS_SCROLLGRID | GCS_FILL | GCS_BARS |
                       GCS_AUTOSCALE | GCS_VALUEOVERLAY | GCS_TOOLTIP));
}

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

// Per-processor times are not in any Win32 API, so go to the native one.
typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG         InterruptCount;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
static constexpr ULONG SystemProcessorPerformanceInformation = 8;

// Fills one busy percentage per logical processor. Entries stay at
// MissingSample if the query fails, so the tiles show a gap, not a fake zero.
static void SamplePerCore(std::vector<float>& out) {
    static NtQuerySystemInformationFn s_query = reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    static std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> s_prev;
    static bool s_first = true;

    if (!s_query || out.empty()) return;

    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> now(out.size());
    ULONG needed = 0;
    const ULONG bytes = static_cast<ULONG>(now.size() * sizeof(now[0]));
    if (s_query(SystemProcessorPerformanceInformation, now.data(), bytes, &needed) < 0)
        return;

    if (s_first || s_prev.size() != now.size()) {
        s_first = false;
        s_prev  = now;
        std::fill(out.begin(), out.end(), 0.0f);
        return;
    }

    for (size_t i = 0; i < now.size(); i++) {
        const LONGLONG dIdle   = now[i].IdleTime.QuadPart   - s_prev[i].IdleTime.QuadPart;
        const LONGLONG dKernel = now[i].KernelTime.QuadPart - s_prev[i].KernelTime.QuadPart;
        const LONGLONG dUser   = now[i].UserTime.QuadPart   - s_prev[i].UserTime.QuadPart;
        const LONGLONG dTotal  = dKernel + dUser;   // kernel time already includes idle
        out[i] = dTotal > 0 ? static_cast<float>((dTotal - dIdle) * 100.0 / dTotal) : 0.0f;
    }
    s_prev = now;
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

static CGraphControl* GraphFromId(UINT_PTR id) {
    if (id == IDC_CPU) return &g_cpu;
    if (id == IDC_MEM) return &g_mem;
    if (id >= IDC_CORES && g_cores.GetGraphCount())
        return g_cores.GetGraph(id - IDC_CORES);
    return nullptr;
}

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

    wchar_t hover[64] = L"";
    if (g_hover >= 0) {
        const CGraphControl* from = GraphFromId(g_hoverFrom);
        const float age = from ? const_cast<CGraphControl*>(from)->GetSampleAgeSeconds(g_hover)
                               : 0.0f;
        swprintf_s(hover, L"  |  %s -%.1f s",
                   g_hoverFrom == IDC_CPU ? L"CPU at" : L"memory at", age);
    }

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
    g_cores.SetChildStyle(CoreTileStyle());

    if (!(g_style & GCS_AUTOSCALE)) {
        g_cpu.SetRange(0, 100);
        g_mem.SetRange(0, g_totalPhys);
        g_cores.SetRange(0, 100);
    }
    else {
        g_cores.SetAutoScale(true);
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
    check(IDC_BARS,       (g_style & GCS_BARS) != 0);
    check(IDC_VALUE,      (g_style & GCS_VALUEOVERLAY) != 0);
    check(IDC_PERCORE,    g_showCores);
    check(IDC_ANIMATE,    g_animate);
    check(IDC_BIGFONT,    g_bigFont);

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
        g_cores.Pause();
        KillTimer(hwnd, TIMER_MEMORY);
    }
    else {
        g_cpu.Resume();
        g_cores.Resume();
        SetTimer(hwnd, TIMER_MEMORY, g_interval, nullptr);
    }
    SyncMenu(hwnd);
    UpdateStatus();
}

static void SetInterval(HWND hwnd, UINT ms) {
    g_interval = ms;
    g_cpu.SetUpdateInterval(ms);
    g_cores.SetUpdateInterval(ms);
    g_mem.SetExpectedInterval(ms);      // driven by this app's timer, not its own
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
    g_cores.SetHistoryLength(samples);
    SetInterval(hwnd, g_interval);   // refreshes the caption and the menu
}

static void LayoutChildren(int w, int h) {
    const int sbH = 22;
    const int graphH = (h - sbH) / 2;
    const int lowerH = h - sbH - graphH;

    if (g_cpu.m_hWnd)
        SetWindowPos(g_cpu.m_hWnd, nullptr, 0, 0, w, graphH, SWP_NOZORDER);
    if (g_mem.m_hWnd)
        SetWindowPos(g_mem.m_hWnd, nullptr, 0, graphH, w, lowerH, SWP_NOZORDER);
    if (g_cores.m_hWnd)
        SetWindowPos(g_cores.m_hWnd, nullptr, 0, graphH, w, lowerH, SWP_NOZORDER);
    if (g_status)
        SendMessage(g_status, WM_SIZE, 0, 0);
}

// The lower pane shows one of the two; the hidden one keeps collecting.
static void ShowCoreGrid(HWND hwnd, bool show) {
    g_showCores = show;
    if (g_mem.m_hWnd)   ::ShowWindow(g_mem.m_hWnd,   show ? SW_HIDE : SW_SHOW);
    if (g_cores.m_hWnd) ::ShowWindow(g_cores.m_hWnd, show ? SW_SHOW : SW_HIDE);
    SyncMenu(hwnd);
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

        // A threshold marker on the CPU graph.
        g_cpu.AddReferenceLine(80.0f, RGB(214, 94, 94), L"80%");

        // ---- Per-core grid: one tile per logical processor ----
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        g_coreCount = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

        g_cores.Create(hwnd, 0, graphH, rc.right, rc.bottom - sbH - graphH,
                       WS_CHILD | WS_CLIPCHILDREN, CoreTileStyle(), 0, IDC_CORES);

        SeriesStyle core;
        core.LineColor = RGB(17, 125, 187);
        core.FillColor = RGB(105, 185, 235);
        g_cores.SetSeriesStyle(core);
        g_cores.SetTitlePrefix(L"CPU");
        g_cores.SetRange(0, 100);
        g_cores.SetValueFormatter(Format::Percent);
        g_cores.SetGraphCount(g_coreCount);
        g_cores.SetSampleSource([](std::vector<float>& v) { SamplePerCore(v); });

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
        case GCN_CONTEXTMENU: {
            auto* cn = reinterpret_cast<GRAPHCONTEXTNOTIFY*>(lParam);
            g_menuTarget = (nm->idFrom == IDC_CPU) ? &g_cpu
                         : (nm->idFrom == IDC_MEM) ? &g_mem
                         : nullptr;
            if (!g_menuTarget && g_cores.GetGraphCount()) {
                const size_t tile = nm->idFrom - IDC_CORES;
                g_menuTarget = g_cores.GetGraph(tile);
            }
            if (!g_menuTarget) break;

            HMENU popup = CreatePopupMenu();
            AppendMenu(popup, MF_STRING, IDC_COPY,    L"&Copy image");
            AppendMenu(popup, MF_STRING, IDC_SAVEPNG, L"&Save image...");
            AppendMenu(popup, MF_SEPARATOR, 0, nullptr);
            AppendMenu(popup, MF_STRING, IDC_PAUSE,
                       g_paused ? L"&Resume" : L"&Pause");
            TrackPopupMenu(popup, TPM_RIGHTBUTTON, cn->Pt.x, cn->Pt.y, 0, hwnd, nullptr);
            DestroyMenu(popup);
            break;
        }
        case GCN_GETTOOLTIP: {
            // Prepend how far back the hovered sample is, then keep the default lines.
            auto* tn = reinterpret_cast<GRAPHTOOLTIPNOTIFY*>(lParam);
            CGraphControl* from = GraphFromId(nm->idFrom);
            const float age = from ? from->GetSampleAgeSeconds(tn->SampleIndex) : 0.0f;

            wchar_t buf[256];
            swprintf_s(buf, L"-%.1f s\n%s", age, tn->SzText);
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
            g_cores.Clear();
            UpdateStatus();
            break;
        case IDC_AUTOSCALE:  ToggleStyle(hwnd, GCS_AUTOSCALE);  break;
        case IDC_GRID:       ToggleStyle(hwnd, GCS_GRID);       break;
        case IDC_SCROLLGRID: ToggleStyle(hwnd, GCS_SCROLLGRID); break;
        case IDC_FILL:       ToggleStyle(hwnd, GCS_FILL);       break;
        case IDC_LEGEND:     ToggleStyle(hwnd, GCS_LEGEND);     break;
        case IDC_STACKED:    ToggleStyle(hwnd, GCS_STACKED);    break;
        case IDC_CROSSHAIR:  ToggleStyle(hwnd, GCS_TOOLTIP);    break;
        case IDC_BARS:       ToggleStyle(hwnd, GCS_BARS);       break;
        case IDC_VALUE:      ToggleStyle(hwnd, GCS_VALUEOVERLAY); break;
        case IDC_PERCORE:
            ShowCoreGrid(hwnd, !g_showCores);
            break;
        case IDC_GAP:
            // Simulate a source that stopped answering for a few intervals.
            for (int i = 0; i < 4; i++) {
                const float missing[2] = { MissingSample, MissingSample };
                g_cpu.PushSamples(missing, 2);
                g_mem.PushSample(g_memInUse, MissingSample);
            }
            UpdateStatus();
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
            g_cores.SetTheme(GraphTheme::Dark());
            break;
        case IDC_LIGHT:
            g_cpu.SetTheme(GraphTheme::Light());
            g_mem.SetTheme(GraphTheme::Light());
            g_cores.SetTheme(GraphTheme::Light());
            break;
        case IDC_STALL: {
            // Block the UI thread outright. The control notices the intervals
            // that went by and back-fills them, so the axis keeps its scale.
            SetWindowText(g_status, L"Stalling the UI thread for 3 seconds...");
            UpdateWindow(g_status);
            Sleep(3000);
            UpdateStatus();
            break;
        }
        case IDC_ANIMATE:
            g_animate = !g_animate;
            g_cpu.SetRangeAnimation(g_animate ? 250 : 0);
            g_mem.SetRangeAnimation(g_animate ? 250 : 0);
            g_cores.SetRangeAnimation(g_animate ? 250 : 0);
            SyncMenu(hwnd);
            break;
        case IDC_BIGFONT:
            g_bigFont = !g_bigFont;
            if (g_bigFont) {
                g_cpu.SetFont(L"Consolas", 14.0f, 17.0f, 30.0f);
                g_mem.SetFont(L"Consolas", 14.0f, 17.0f, 30.0f);
                g_cores.SetFont(L"Consolas", 14.0f, 17.0f, 30.0f);
            }
            else {
                g_cpu.ResetFont();
                g_mem.ResetFont();
                g_cores.SetFont(nullptr);
            }
            SyncMenu(hwnd);
            break;
        case IDC_COPY: {
            CGraphControl* target = g_menuTarget ? g_menuTarget : &g_cpu;
            SetWindowText(g_status, target->CopyImageToClipboard()
                                    ? L"Graph copied to the clipboard"
                                    : L"Could not copy the graph");
            break;
        }
        case IDC_SAVEPNG: {
            CGraphControl* target = g_menuTarget ? g_menuTarget : &g_cpu;

            wchar_t path[MAX_PATH] = L"graph.png";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"PNG Image\0*.png\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrDefExt = L"png";
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn)) {
                SetWindowText(g_status, target->SaveImage(path)
                                        ? L"Graph saved"
                                        : L"Could not save the graph");
            }
            break;
        }
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
    AppendMenu(hFile, MF_STRING, IDC_COPY,    L"&Copy image\tCtrl+C");
    AppendMenu(hFile, MF_STRING, IDC_SAVEPNG, L"&Save image...\tCtrl+S");
    AppendMenu(hFile, MF_SEPARATOR, 0, nullptr);
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
        { FVIRTKEY,      'B', IDC_BARS },
        { FVIRTKEY,      'V', IDC_VALUE },
        { FVIRTKEY,      'P', IDC_PERCORE },
        { FVIRTKEY,      'N', IDC_GAP },
        { FVIRTKEY | FCONTROL, 'C', IDC_COPY },
        { FVIRTKEY | FCONTROL, 'S', IDC_SAVEPNG },
        { FVIRTKEY,      'K', IDC_STALL },
        { FVIRTKEY,      'Y', IDC_ANIMATE },
        { FVIRTKEY,      'Z', IDC_BIGFONT },
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
