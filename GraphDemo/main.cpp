#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "GraphControl.h"

#pragma comment(lib, "GraphControl.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

#include <commdlg.h>

using namespace GraphCtrl;

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

static CGraphControl g_graph;
static HWND g_status = nullptr;

static void PopulateDemo(GraphModel& m) {
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

static void UpdateStatus(HWND /*hwnd*/) {
    NodeId node = g_graph.GetSelectedNode();
    EdgeId edge = g_graph.GetSelectedEdge();

    wchar_t buf[256];
    if (node != InvalidNode) {
        GraphModel* m = g_graph.GetModel();
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
                       WS_CHILD | WS_VISIBLE, GCS_GRID | GCS_AUTOZOOM);

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
            case GCN_SELCHANGED:
                UpdateStatus(hwnd);
                break;
            case GCN_LABELCHANGED: {
                auto* gln = reinterpret_cast<GRAPHLABELNOTIFY*>(lParam);
                wchar_t buf[300];
                swprintf_s(buf, L"Label changed: node %u = \"%s\"", gln->NodeId, gln->SzNewLabel);
                SetWindowText(g_status, buf);
                break;
            }
            case GCN_UNDOCHANGED: {
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
        switch (LOWORD(wParam)) {
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
            GraphModel* m = g_graph.GetModel();
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
            GraphModel* m = g_graph.GetModel();
            if (m) {
                m->Clear();
                InvalidateRect(g_graph.m_hWnd, nullptr, FALSE);
                UpdateStatus(hwnd);
            }
            break;
        }
        case IDC_SAVE: {
            GraphModel* m = g_graph.GetModel();
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
            GraphModel* m = g_graph.GetModel();
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

    //if (!GraphCtrl::Register(hInstance)) {
    //    MessageBox(nullptr, L"Failed to register GraphControl window class.", L"Error", MB_ICONERROR);
    //    return 1;
    //}

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
    AppendMenu(hGraph, MF_STRING, IDC_BTNCLR,  L"&Clear All");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hGraph, L"&Graph");

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
