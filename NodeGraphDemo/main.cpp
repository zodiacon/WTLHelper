#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "NodeGraphControl.h"

#pragma comment(lib, "NodeGraphControl.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <commdlg.h>

using namespace NodeGraphCtrl;

// Control IDs
static constexpr int IDC_GRAPH   = 101;
static constexpr int IDC_STATUS  = 102;
static constexpr int IDC_BTNFIT  = 103;
static constexpr int IDC_BTNADD  = 104;
static constexpr int IDC_BTNCLR  = 105;
static constexpr int IDC_SAVE    = 106;
static constexpr int IDC_LOAD    = 107;
static constexpr int IDC_DELSEL  = 108;
static constexpr int IDC_UNDO    = 109;
static constexpr int IDC_REDO    = 110;
static constexpr int IDC_MINIMAP = 111;
static constexpr int IDC_SAMPLE  = 112;
// IDC_LAYOUT + (int)LayoutAlgorithm, IDC_DIRECTION + (int)LayoutDirection
static constexpr int IDC_LAYOUT    = 200;
static constexpr int IDC_DIRECTION = 220;

static CNodeGraphControl g_graph;
static HWND g_status = nullptr;
static LayoutDirection g_direction = LayoutDirection::TopToBottom;

static void PopulateDemo(NodeGraphModel& m) {
    auto kernel  = m.AddNode(L"kernel32",   100, 100);
    auto ntdll   = m.AddNode(L"ntdll",      100, 220);
    auto user32  = m.AddNode(L"user32",     280, 100);
    auto gdi32   = m.AddNode(L"gdi32",      280, 220);
    auto shell32 = m.AddNode(L"shell32",    460, 100);
    auto app     = m.AddNode(L"app.exe",    280, 340);

    m.AddEdge(app,     kernel,  L"imports");
    m.AddEdge(app,     user32,  L"imports");
    m.AddEdge(app,     shell32, L"imports");
    m.AddEdge(user32,  kernel,  L"imports");
    m.AddEdge(user32,  gdi32,   L"imports");
    m.AddEdge(shell32, kernel,  L"imports");
    m.AddEdge(kernel,  ntdll,   L"imports");
    m.AddEdge(gdi32,   ntdll,   L"imports");
}

// A larger graph to try the layouts on: a few trees of modules that share some of their dependencies, with a cycle
// and a node by itself.
static void PopulateSample(NodeGraphModel& m) {
    m.Clear();
    std::vector<NodeId> n;
    for (int i = 0; i < 28; i++) {
        wchar_t label[16];
        swprintf_s(label, L"m%d", i);
        n.push_back(m.AddNode(label));
    }
    const int links[][2] = {
        {0,1},{0,2},{0,3},{1,4},{1,5},{2,6},{2,7},{3,8},{3,9},{4,10},{5,10},{6,11},{7,11},{8,12},{9,12},
        {10,13},{11,13},{12,13},{13,14},{14,15},{15,13},{0,16},{16,17},{17,18},{18,14},{1,16},{9,5},
        {19,20},{19,21},{20,22},{21,22},{22,23},{20,23},{2,20},{24,25},{25,26},{26,24},
    };
    for (const auto& l : links)
        m.AddEdge(n[l[0]], n[l[1]]);
}

static void UpdateStatus(HWND /*hwnd*/) {
    NodeId node = g_graph.GetSelectedNode();
    EdgeId edge = g_graph.GetSelectedEdge();

    wchar_t buf[256];
    if (node != InvalidNode) {
        NodeGraphModel* m = g_graph.GetModel();
        const Node* n = m ? m->GetNode(node) : nullptr;
        if (n) {
            swprintf_s(buf, L"Node selected: %s  (id=%u, pos=%.0f,%.0f)",
                n->Label.c_str(), node, n->X, n->Y);
            SetWindowText(g_status, buf);
            return;
        }
    }
    if (edge != InvalidEdge) {
        swprintf_s(buf, L"Edge selected: id=%u", edge);
        SetWindowText(g_status, buf);
        return;
    }
    SetWindowText(g_status, L"Ready  |  scroll=zoom  |  middle-drag=pan  |  left-drag=move node  |  dbl-click=rename");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int sbH = 22;

        g_graph.Create(hwnd, 0, 0, rc.right, rc.bottom - sbH,
                       WS_CHILD | WS_VISIBLE, NGCS_GRID | NGCS_AUTOZOOM);

        g_status = CreateWindowEx(0, STATUSCLASSNAME, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_STATUS, nullptr, nullptr);

        PopulateDemo(*g_graph.GetModel());
        g_graph.FitInView();
        g_graph.SetMinimapVisible(true);
        UpdateStatus(hwnd);
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        int sbH = 22;
        if (g_graph.m_hWnd)
            SetWindowPos(g_graph.m_hWnd, nullptr, 0, 0, w, h - sbH, SWP_NOZORDER);
        if (g_status)
            SendMessage(g_status, WM_SIZE, 0, 0);
        return 0;
    }

    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->idFrom == IDC_GRAPH) {
            switch (nm->code) {
            case NGCN_SELCHANGED:
                UpdateStatus(hwnd);
                break;
            case NGCN_LABELCHANGED: {
                auto* gln = reinterpret_cast<NODEGRAPHLABELNOTIFY*>(lParam);
                wchar_t buf[300];
                swprintf_s(buf, L"Label changed: node %u = \"%s\"", gln->NodeId, gln->SzNewLabel);
                SetWindowText(g_status, buf);
                break;
            }
            case NGCN_UNDOCHANGED: {
                wchar_t buf[128];
                swprintf_s(buf, L"Undo: %s  |  Redo: %s",
                    g_graph.CanUndo() ? L"available" : L"none",
                    g_graph.CanRedo() ? L"available" : L"none");
                SetWindowText(g_status, buf);
                break;
            }
            }
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) >= IDC_LAYOUT && LOWORD(wParam) <= IDC_LAYOUT + (int)LayoutAlgorithm::Grid) {
            LayoutOptions options;
            options.Direction = g_direction;
            g_graph.ApplyLayout((LayoutAlgorithm)(LOWORD(wParam) - IDC_LAYOUT), options);
            return 0;
        }
        if (LOWORD(wParam) >= IDC_DIRECTION && LOWORD(wParam) <= IDC_DIRECTION + (int)LayoutDirection::RightToLeft) {
            g_direction = (LayoutDirection)(LOWORD(wParam) - IDC_DIRECTION);
            CheckMenuRadioItem(GetMenu(hwnd), IDC_DIRECTION, IDC_DIRECTION + (int)LayoutDirection::RightToLeft,
                LOWORD(wParam), MF_BYCOMMAND);
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDC_SAMPLE:
            PopulateSample(*g_graph.GetModel());
            g_graph.ApplyLayout(LayoutAlgorithm::Layered);
            UpdateStatus(hwnd);
            break;
        case IDC_MINIMAP:
            g_graph.SetMinimapVisible(!g_graph.IsMinimapVisible());
            break;
        case IDC_BTNFIT:
            g_graph.FitInView();
            break;
        case IDC_DELSEL:
            g_graph.DeleteSelected();
            UpdateStatus(hwnd);
            break;
        case IDC_UNDO:
            g_graph.Undo();
            break;
        case IDC_REDO:
            g_graph.Redo();
            break;
        case IDC_BTNADD: {
            NodeGraphModel* m = g_graph.GetModel();
            if (m) {
                static int s_count = 0;
                wchar_t label[32];
                swprintf_s(label, L"node%d", ++s_count);
                float cx = 200.0f + (s_count % 5) * 160.0f;
                float cy = 200.0f + (s_count / 5) * 100.0f;
                m->AddNode(label, cx, cy);
                InvalidateRect(g_graph.m_hWnd, nullptr, FALSE);
            }
            break;
        }
        case IDC_BTNCLR: {
            NodeGraphModel* m = g_graph.GetModel();
            if (m) {
                m->Clear();
                InvalidateRect(g_graph.m_hWnd, nullptr, FALSE);
                UpdateStatus(hwnd);
            }
            break;
        }
        case IDC_SAVE: {
            NodeGraphModel* m = g_graph.GetModel();
            if (!m) break;
            OPENFILENAMEW ofn{};
            wchar_t path[MAX_PATH] = L"graph.gcf";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"Graph Control File\0*.gcf\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrDefExt = L"gcf";
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn))
                m->Save(path);
            break;
        }
        case IDC_LOAD: {
            NodeGraphModel* m = g_graph.GetModel();
            if (!m) break;
            OPENFILENAMEW ofn{};
            wchar_t path[MAX_PATH] = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"Graph Control File\0*.gcf\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                if (m->Load(path)) {
                    g_graph.FitInView();
                    UpdateStatus(hwnd);
                } else {
                    MessageBox(hwnd, L"Failed to load file.", L"Error", MB_ICONERROR);
                }
            }
            break;
        }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    //if (!NodeGraphCtrl::Register(hInstance)) {
    //    MessageBox(nullptr, L"Failed to register NodeGraphControl window class.", L"Error", MB_ICONERROR);
    //    return 1;
    //}

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NodeGraphDemoWindow";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"NodeGraphDemoWindow", L"NodeGraphControl Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, hInstance, nullptr);

    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, IDC_SAVE, L"&Save...\tCtrl+S");
    AppendMenu(hFile, MF_STRING, IDC_LOAD, L"&Load...\tCtrl+O");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");

    HMENU hEdit = CreatePopupMenu();
    AppendMenu(hEdit, MF_STRING, IDC_UNDO, L"&Undo\tCtrl+Z");
    AppendMenu(hEdit, MF_STRING, IDC_REDO, L"&Redo\tCtrl+Y");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"&Edit");

    HMENU hGraph = CreatePopupMenu();
    AppendMenu(hGraph, MF_STRING, IDC_BTNFIT,  L"&Fit in View\tF");
    AppendMenu(hGraph, MF_STRING, IDC_BTNADD,  L"&Add Node\tA");
    AppendMenu(hGraph, MF_STRING, IDC_DELSEL,  L"&Delete Selected\tDel");
    AppendMenu(hGraph, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hGraph, MF_STRING, IDC_MINIMAP, L"Toggle &Minimap\tM");
    AppendMenu(hGraph, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hGraph, MF_STRING, IDC_SAMPLE,  L"&Sample Graph");
    AppendMenu(hGraph, MF_STRING, IDC_BTNCLR,  L"&Clear All");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hGraph, L"&Graph");

    HMENU hLayout = CreatePopupMenu();
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::Layered,       L"&Layered\tCtrl+1");
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::Tree,          L"&Tree\tCtrl+2");
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::Radial,        L"&Radial\tCtrl+3");
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::ForceDirected, L"&Force Directed\tCtrl+4");
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::Circular,      L"&Circular\tCtrl+5");
    AppendMenu(hLayout, MF_STRING, IDC_LAYOUT + (int)LayoutAlgorithm::Grid,          L"&Grid\tCtrl+6");
    AppendMenu(hLayout, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hLayout, MF_STRING, IDC_DIRECTION + (int)LayoutDirection::TopToBottom, L"Top to &Bottom");
    AppendMenu(hLayout, MF_STRING, IDC_DIRECTION + (int)LayoutDirection::BottomToTop, L"Bottom to T&op");
    AppendMenu(hLayout, MF_STRING, IDC_DIRECTION + (int)LayoutDirection::LeftToRight, L"Left to R&ight");
    AppendMenu(hLayout, MF_STRING, IDC_DIRECTION + (int)LayoutDirection::RightToLeft, L"Right to L&eft");
    CheckMenuRadioItem(hLayout, IDC_DIRECTION, IDC_DIRECTION + (int)LayoutDirection::RightToLeft,
        IDC_DIRECTION + (int)g_direction, MF_BYCOMMAND);
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hLayout, L"&Layout");

    SetMenu(hwnd, hMenu);

    // Keyboard accelerators
    ACCEL accels[] = {
        { FVIRTKEY,               'M', IDC_MINIMAP },
        { FVIRTKEY,               'F', IDC_BTNFIT },
        { FVIRTKEY,               'A', IDC_BTNADD },
        { FVIRTKEY,        VK_DELETE, IDC_DELSEL  },
        { FVIRTKEY | FCONTROL,    'S', IDC_SAVE   },
        { FVIRTKEY | FCONTROL,    'O', IDC_LOAD   },
        { FVIRTKEY | FCONTROL,    'Z', IDC_UNDO   },
        { FVIRTKEY | FCONTROL,    'Y', IDC_REDO   },
        { FVIRTKEY | FCONTROL,    '1', IDC_LAYOUT + (int)LayoutAlgorithm::Layered },
        { FVIRTKEY | FCONTROL,    '2', IDC_LAYOUT + (int)LayoutAlgorithm::Tree },
        { FVIRTKEY | FCONTROL,    '3', IDC_LAYOUT + (int)LayoutAlgorithm::Radial },
        { FVIRTKEY | FCONTROL,    '4', IDC_LAYOUT + (int)LayoutAlgorithm::ForceDirected },
        { FVIRTKEY | FCONTROL,    '5', IDC_LAYOUT + (int)LayoutAlgorithm::Circular },
        { FVIRTKEY | FCONTROL,    '6', IDC_LAYOUT + (int)LayoutAlgorithm::Grid },
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, (int)std::size(accels));

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (g_graph.IsEditingLabel() || !TranslateAccelerator(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
