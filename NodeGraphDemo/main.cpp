#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cmath>
#include "NodeGraphControl.h"

#pragma comment(lib, "NodeGraphControl.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

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
static constexpr int IDC_CONTENT = 113;
static constexpr int IDC_EXPORTSVG = 114;
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

// A stock shell icon at a size that stays sharp when zoomed in.
static ImageId AddStockIcon(NodeGraphModel& m, SHSTOCKICONID id) {
    SHSTOCKICONINFO info{ sizeof(info) };
    if (FAILED(SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &info)))
        return InvalidImage;
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(info.szPath, info.iIcon, 0, &icon, nullptr, 64)) || !icon)
        return InvalidImage;
    ImageId image = m.AddImage(icon);
    DestroyIcon(icon);
    return image;
}

// A picture made up on the spot: color gradients with a few rings, w x h pixels.
static ImageId AddPicture(NodeGraphModel& m, int w, int h, int seed) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return InvalidImage;
    auto pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = x - w * 0.5f, dy = y - h * 0.5f;
            int ring = (int)(sqrtf(dx * dx + dy * dy) / 12) % 2;
            BYTE r = (BYTE)(255 * x / w), g = (BYTE)(255 * y / h), b = (BYTE)(seed ? 200 : 90);
            if (ring) { r = (BYTE)(r * 0.8f); g = (BYTE)(g * 0.8f); b = (BYTE)(b * 0.8f); }
            pixels[y * w + x] = RGB(b, g, r);   // a DIB is BGR
        }
    }
    ImageId image = m.AddImage(bitmap);
    DeleteObject(bitmap);
    return image;
}

// Draws the owner drawn nodes of the content sample. Which one a node is goes in its UserData.
enum class DemoDraw : intptr_t { None, Progress, Sparkline, Circle };

class DemoDrawHandler : public INodeDrawHandler {
public:
    uint32_t PrePaint(const NodeDrawContext& ctx) override {
        Prepare(ctx);
        switch ((DemoDraw)(intptr_t)ctx.GraphNode.UserData) {
            case DemoDraw::Progress:
            case DemoDraw::Sparkline:
                return NDF_POSTPAINT;           // over the default node
            case DemoDraw::Circle:
                DrawCircle(ctx);
                return NDF_SKIPBACKGROUND | NDF_SKIPBORDER;     // the default label on it
            default:
                return NDF_DEFAULT;
        }
    }

    void PostPaint(const NodeDrawContext& ctx) override {
        switch ((DemoDraw)(intptr_t)ctx.GraphNode.UserData) {
            case DemoDraw::Progress:  DrawProgress(ctx); break;
            case DemoDraw::Sparkline: DrawSparkline(ctx); break;
            default: break;
        }
    }

private:
    // the brush belongs to the target: made again when it is
    void Prepare(const NodeDrawContext& ctx) {
        if (!m_Brush || m_Generation != ctx.DeviceGeneration) {
            m_Brush.Reset();
            ctx.Target->CreateSolidColorBrush(D2D1::ColorF(0), m_Brush.GetAddressOf());
            m_Generation = ctx.DeviceGeneration;
        }
    }

    // a bar along the bottom of the node
    void DrawProgress(const NodeDrawContext& ctx) {
        const float percent = 65;
        const Rect& c = ctx.Layout.Content;
        D2D1_RECT_F bar{ c.Left + 8, c.Bottom - 12, c.Right - 8, c.Bottom - 6 };
        m_Brush->SetColor(D2D1::ColorF(0, 0, 0, 0.35f));
        ctx.Target->FillRoundedRectangle({ bar, 3, 3 }, m_Brush.Get());
        bar.right = bar.left + (bar.right - bar.left) * percent / 100;
        m_Brush->SetColor(ctx.Hovered ? D2D1::ColorF(0.55f, 1.0f, 0.55f) : D2D1::ColorF(0.3f, 0.85f, 0.4f));
        ctx.Target->FillRoundedRectangle({ bar, 3, 3 }, m_Brush.Get());
    }

    // a line chart under the title
    void DrawSparkline(const NodeDrawContext& ctx) {
        static const float values[] = { 12, 30, 22, 45, 38, 60, 52, 75, 40, 55, 68, 90, 70, 62, 80 };
        const Rect& c = ctx.Layout.Content;
        const float left = c.Left + 8, right = c.Right - 8, top = c.Top + 8, bottom = c.Bottom - 8;
        if (right <= left || bottom <= top)
            return;

        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(ctx.D2DFactory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
            return;
        const int count = (int)std::size(values);
        for (int i = 0; i < count; i++) {
            D2D1_POINT_2F p{ left + (right - left) * i / (count - 1), bottom - (bottom - top) * values[i] / 100 };
            if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_HOLLOW);
            else sink->AddLine(p);
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();

        m_Brush->SetColor(D2D1::ColorF(1, 1, 1, 0.15f));
        ctx.Target->DrawLine({ left, bottom }, { right, bottom }, m_Brush.Get(), 1);
        m_Brush->SetColor(D2D1::ColorF(1.0f, 0.8f, 0.3f));
        ctx.Target->DrawGeometry(path.Get(), m_Brush.Get(), 1.5f);
    }

    // a circle instead of the rounded rectangle, lighter under the mouse
    void DrawCircle(const NodeDrawContext& ctx) {
        const float w = ctx.GraphNode.Width, h = ctx.GraphNode.Height;
        const D2D1_ELLIPSE circle{ { w / 2, h / 2 }, w / 2 - 2, h / 2 - 2 };
        m_Brush->SetColor(ctx.Hovered ? D2D1::ColorF(0.35f, 0.75f, 0.45f) : D2D1::ColorF(0.2f, 0.6f, 0.3f));
        ctx.Target->FillEllipse(circle, m_Brush.Get());
        m_Brush->SetColor(ctx.Selected ? D2D1::ColorF(D2D1::ColorF::Yellow) : D2D1::ColorF(0.1f, 0.35f, 0.15f));
        ctx.Target->DrawEllipse(circle, m_Brush.Get(), ctx.Selected ? 3.0f : 1.5f);
    }

    ComPtr<ID2D1SolidColorBrush> m_Brush;
    uint32_t m_Generation = 0;
};

static DemoDrawHandler g_drawHandler;

// Nodes with icons, titles and images, in each of the ways they can be placed.
static void PopulateContent(NodeGraphModel& m) {
    m.Clear();
    const ImageId folder = AddStockIcon(m, SIID_FOLDER);
    const ImageId app = AddStockIcon(m, SIID_APPLICATION);
    const ImageId warning = AddStockIcon(m, SIID_WARNING);
    const ImageId info = AddStockIcon(m, SIID_INFO);
    const ImageId wide = AddPicture(m, 240, 140, 0);
    const ImageId square = AddPicture(m, 120, 120, 1);

    // adds a node and sets it up (a Node& doesn't survive adding the next node)
    auto node = [&](const wchar_t* label, float x, float y, float w, float h, auto setup) {
        NodeId id = m.AddNode(label, x, y, w, h);
        setup(*m.GetNode(id));
        return id;
    };

    // icons
    NodeId left = node(L"Icon left", 100, 60, 140, 0, [&](Node& n) {
        n.Content.Icon = folder;
    });
    NodeId right = node(L"Icon right", 270, 60, 140, 0, [&](Node& n) {
        n.Content.Icon = app;
        n.Content.IconPos = IconPlacement::Right;
    });
    NodeId top = node(L"Icon on top", 440, 70, 120, 64, [&](Node& n) {
        n.Content.Icon = info;
        n.Content.IconPos = IconPlacement::Top;
        n.Content.IconSize = 28;
    });
    node(L"", 570, 70, 48, 48, [&](Node& n) {
        n.Content.Icon = warning;
        n.Content.IconSize = 32;
        n.Tooltip = L"An icon without a label";
    });
    node(L"Fixed size label", 700, 70, 130, 0, [&](Node& n) {
        n.Style.ScaleLabel = false;
        n.Tooltip = L"The label is the same size on the screen at any zoom";
    });

    // titles
    NodeId process = node(L"notepad.exe\nPID 4242", 100, 200, 160, 70, [&](Node& n) {
        n.Content.Title = L"Process";
        n.Content.Icon = app;
        n.Content.IconSize = 24;
    });
    NodeId module = node(L"kernel32.dll", 300, 200, 150, 60, [&](Node& n) {
        n.Content.Title = L"Module with a long title";
        n.Content.TitleFill = RGB(150, 80, 40);
        n.Style.FillColor = RGB(200, 120, 70);
        n.Style.BorderColor = RGB(120, 60, 30);
    });
    NodeId code = node(L"mov rax, [rcx]\nadd rax, 8\nret", 500, 210, 170, 90, [&](Node& n) {
        n.Content.Title = L"Code";
        n.Style.Code = true;
        n.Style.FillColor = RGB(40, 44, 52);
        n.Style.BorderColor = RGB(90, 96, 110);
    });

    // images
    NodeId fit = node(L"Fit, caption below", 100, 400, 170, 140, [&](Node& n) {
        n.Content.Image = wide;
        n.Content.LabelPos = LabelPlacement::Below;
    });
    NodeId fill = node(L"Fill, label overlaid", 290, 400, 150, 140, [&](Node& n) {
        n.Content.Image = wide;
        n.Content.Fit = ImageFit::Fill;
        n.Content.LabelPos = LabelPlacement::Overlay;
    });
    NodeId center = node(L"Center, no label", 460, 400, 150, 140, [&](Node& n) {
        n.Content.Image = square;
        n.Content.Fit = ImageFit::Center;
        n.Content.LabelPos = LabelPlacement::Hidden;
        n.Tooltip = L"An image at its size, without a label";
    });
    NodeId stretch = node(L"stretched", 630, 400, 150, 160, [&](Node& n) {
        n.Content.Title = L"Title and image";
        n.Content.Image = square;
        n.Content.Fit = ImageFit::Stretch;
        n.Content.LabelPos = LabelPlacement::Below;
        n.Content.Icon = folder;
    });

    m.AddEdge(left, process);
    m.AddEdge(right, module);
    m.AddEdge(process, fit);
    m.AddEdge(module, fill);
    m.AddEdge(code, center);
    m.AddEdge(code, stretch);
    m.AddEdge(top, code);

    // owner drawn (DemoDrawHandler)
    NodeId start = node(L"Start", 100, 600, 70, 70, [&](Node& n) {
        n.Style.OwnerDraw = true;
        n.UserData = (void*)DemoDraw::Circle;
    });
    NodeId download = node(L"setup.exe", 270, 600, 170, 70, [&](Node& n) {
        n.Content.Title = L"Download";
        n.Content.Icon = app;
        n.Style.OwnerDraw = true;
        n.UserData = (void*)DemoDraw::Progress;
    });
    NodeId cpu = node(L"", 470, 600, 180, 90, [&](Node& n) {
        n.Content.Title = L"CPU";
        n.Style.FillColor = RGB(45, 50, 60);
        n.Style.BorderColor = RGB(90, 96, 110);
        n.Style.OwnerDraw = true;
        n.UserData = (void*)DemoDraw::Sparkline;
    });
    m.AddEdge(start, download);
    m.AddEdge(download, cpu);
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

        g_graph.SetNodeDrawHandler(&g_drawHandler);
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
        case IDC_CONTENT:
            PopulateContent(*g_graph.GetModel());
            g_graph.FitInView();
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
        case IDC_EXPORTSVG: {
            OPENFILENAMEW ofn{};
            wchar_t path[MAX_PATH] = L"graph.svg";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"SVG Image\0*.svg\0All Files\0*.*\0";
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrDefExt = L"svg";
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn) && !g_graph.SaveSvg(path))
                MessageBox(hwnd, L"Failed to export the graph.", L"Error", MB_ICONERROR);
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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,	// or the window's background is painted over the graph on every resize (flicker)
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, hInstance, nullptr);

    HMENU hMenu = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, IDC_SAVE, L"&Save...\tCtrl+S");
    AppendMenu(hFile, MF_STRING, IDC_LOAD, L"&Load...\tCtrl+O");
    AppendMenu(hFile, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hFile, MF_STRING, IDC_EXPORTSVG, L"Export S&VG...");
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
    AppendMenu(hGraph, MF_STRING, IDC_CONTENT, L"C&ontent Sample");
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
