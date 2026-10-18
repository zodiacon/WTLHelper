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
static constexpr int IDC_SCROLLBARS = 115;
static constexpr int IDC_WHEELSCROLL = 116;
static constexpr int IDC_HIGHLIGHT = 117;
static constexpr int IDC_CURVED = 118;
static constexpr int IDC_READONLY = 119;
static constexpr int IDC_FIND = 120;
static constexpr int IDC_FINDNEXT = 121;
static constexpr int IDC_FINDPREV = 122;
static constexpr int IDC_SNAPGRID = 123;
static constexpr int IDC_GUIDES = 124;
static constexpr int IDC_DISTRIBUTEH = 125;
static constexpr int IDC_DISTRIBUTEV = 126;
static constexpr int IDC_ALIGN = 130;   // IDC_ALIGN + (int)Alignment
static constexpr int IDC_GROUP = 140;
static constexpr int IDC_UNGROUP = 141;
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

public:
    float Progress = 60;    // of the Download node, percent

    // the bar along the bottom of the node (a little taller to click)
    static Rect ProgressBar(const NodeLayout& layout) {
        const Rect& c = layout.Content;
        return { c.Left + 8, c.Bottom - 12, c.Right - 8, c.Bottom - 6 };
    }
    static bool OnProgressBar(const NodeLayout& layout, float x, float y) {
        Rect bar = ProgressBar(layout);
        return x >= bar.Left && x < bar.Right && y >= bar.Top - 4 && y < bar.Bottom + 4;
    }

private:
    void DrawProgress(const NodeDrawContext& ctx) {
        const float percent = Progress;
        const Rect r = ProgressBar(ctx.Layout);
        D2D1_RECT_F bar{ r.Left, r.Top, r.Right, r.Bottom };
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
        n.Style.Shape = NodeShape::Ellipse;     // clicks and edges follow the circle
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

    // shapes
    NodeId decision = node(L"Done?", 270, 760, 120, 80, [&](Node& n) {
        n.Style.Shape = NodeShape::Diamond;
        n.Style.FillColor = RGB(150, 110, 40);
        n.Style.BorderColor = RGB(100, 70, 20);
    });
    NodeId oval = node(L"Retry", 470, 760, 130, 60, [&](Node& n) {
        n.Style.Shape = NodeShape::Ellipse;
        n.Content.Title = L"Ellipse";
    });
    m.AddEdge(download, decision);
    m.AddEdge(decision, oval, L"no");
    m.AddEdge(oval, download);
    m.AddEdge(decision, decision);
    m.AddEdge(oval, oval, L"wait");     // the label right of the loop (where nothing else is)
    m.AddGroup(L"Pipeline", { start, download, cpu });
    const GroupId shapes = m.AddGroup(L"Shapes", { decision, oval });
    m.GetGroup(shapes)->Collapsed = true;   // its box stands for them: the edges to them end on it
    m.GetGroup(shapes)->FillColor = RGB(120, 90, 50);
    m.GetGroup(shapes)->BorderColor = RGB(170, 130, 70);

    // edge styles: lines, arrowheads, a curve, a label of a fixed size
    const struct { const wchar_t* label; EdgeLine line; ArrowHead arrow; } styles[] = {
        { L"dash", EdgeLine::Dash, ArrowHead::Filled }, { L"dot", EdgeLine::Dot, ArrowHead::Open },
        { L"dash dot", EdgeLine::DashDot, ArrowHead::Diamond }, { L"circle", EdgeLine::Solid, ArrowHead::Circle },
    };
    std::vector<NodeId> styled;
    styled.reserve(10);
    for (int i = 0; i < 4; i++) {
        NodeId a = node(styles[i].label, 100 + i * 170.0f, 900, 100, 36, [](Node&) {});
        NodeId b = node(L"", 100 + i * 170.0f, 1010, 60, 30, [](Node&) {});
        styled.push_back(a);
        styled.push_back(b);
        Edge& e = *m.GetEdge(m.AddEdge(a, b));
        e.Style.Line = styles[i].line;
        e.Style.Arrow = styles[i].arrow;
        e.Style.Width = 2;
    }
    NodeId c1 = node(L"curved", 770, 880, 100, 36, [](Node&) {});
    NodeId c2 = node(L"", 900, 1030, 60, 30, [](Node&) {});
    Edge& curved = *m.GetEdge(m.AddEdge(c1, c2, L"through 2 bends"));
    curved.Waypoints = { { 900, 900 }, { 790, 990 } };
    curved.Style.Curved = true;
    curved.Style.ScaleLabel = false;   // the same size at any zoom
    styled.push_back(c1);
    styled.push_back(c2);
    m.AddGroup(L"Edge styles", styled);
}

// The demo keeps which owner drawing a node gets in its UserData: saved (and copied) as one byte.
struct DemoSerializer : IUserDataSerializer {
    std::vector<uint8_t> SaveNode(const Node& n) override {
        return n.UserData ? std::vector<uint8_t>{ (uint8_t)(intptr_t)n.UserData } : std::vector<uint8_t>{};
    }
    void LoadNode(Node& n, const uint8_t* data, size_t size) override {
        if (size == 1 && data[0] <= (uint8_t)DemoDraw::Circle)
            n.UserData = (void*)(intptr_t)data[0];
    }
};
static DemoSerializer g_serializer;

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

// ---- The find bar: a small tool window with an edit box. Enter: the next node with the text, Shift+Enter: the
// previous one, Ctrl+Enter: all of them; Esc closes it.

static HWND g_findWnd = nullptr, g_findEdit = nullptr;
static std::wstring g_findText;

static void FindAndReport(bool backwards) {
    wchar_t buf[200];
    const size_t count = g_graph.GetModel()->FindNodes(g_findText).size();
    if (g_graph.FindNext(g_findText, backwards))
        swprintf_s(buf, L"\"%s\": %zu node(s)  |  F3: next, Shift+F3: previous", g_findText.c_str(), count);
    else
        swprintf_s(buf, L"\"%s\": not found", g_findText.c_str());
    SetWindowText(g_status, buf);
}

static LRESULT CALLBACK FindEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        if (wParam == VK_ESCAPE) {
            ShowWindow(g_findWnd, SW_HIDE);
            SetFocus(g_graph);
            return 0;
        }
        wchar_t text[256];
        GetWindowText(hwnd, text, (int)std::size(text));
        g_findText = text;
        if (GetKeyState(VK_CONTROL) < 0) {
            auto found = g_graph.GetModel()->FindNodes(g_findText);
            g_graph.SelectNodes(found);
            if (!found.empty()) g_graph.FitSelected();
            wchar_t buf[200];
            swprintf_s(buf, L"\"%s\": %zu node(s) selected", g_findText.c_str(), found.size());
            SetWindowText(g_status, buf);
        }
        else
            FindAndReport(GetKeyState(VK_SHIFT) < 0);
        return 0;
    }
    if (msg == WM_CHAR && (wParam == L'\r' || wParam == 27))
        return 0;   // no beep
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK FindWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            SetFocus(g_graph);
            return 0;
        case WM_SIZE:
            if (g_findEdit) MoveWindow(g_findEdit, 4, 4, LOWORD(lParam) - 8, HIWORD(lParam) - 8, TRUE);
            return 0;
        case WM_SETFOCUS:
            SetFocus(g_findEdit);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ShowFindBar(HWND owner) {
    if (!g_findWnd) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = FindWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"NodeGraphDemoFind";
        RegisterClassW(&wc);
        const UINT dpi = GetDpiForWindow(owner);
        g_findWnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Find (Enter: next, Shift+Enter: previous, Ctrl+Enter: all)",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, MulDiv(420, dpi, 96), MulDiv(64, dpi, 96), owner, nullptr, wc.hInstance, nullptr);
        g_findEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, g_findWnd, nullptr, wc.hInstance, nullptr);
        SendMessage(g_findEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetWindowSubclass(g_findEdit, FindEditProc, 1, 0);
        RECT rc;
        GetClientRect(g_findWnd, &rc);
        SendMessage(g_findWnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
        // at the top right of the graph
        RECT gr;
        GetWindowRect(g_graph, &gr);
        RECT fr;
        GetWindowRect(g_findWnd, &fr);
        SetWindowPos(g_findWnd, nullptr, gr.right - (fr.right - fr.left) - 24, gr.top + 24, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    SetWindowText(g_findEdit, g_findText.c_str());
    SendMessage(g_findEdit, EM_SETSEL, 0, -1);
    ShowWindow(g_findWnd, SW_SHOW);
    SetFocus(g_findEdit);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int sbH = 22;

        g_graph.Create(hwnd, 0, 0, rc.right, rc.bottom - sbH,
                       WS_CHILD | WS_VISIBLE, NGCS_GRID | NGCS_AUTOZOOM | NGCS_SCROLLBARS);

        g_status = CreateWindowEx(0, STATUSCLASSNAME, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_STATUS, nullptr, nullptr);

        g_graph.SetDlgCtrlID(IDC_GRAPH);    // the notifications carry it
        g_graph.SetNodeDrawHandler(&g_drawHandler);
        g_graph.GetModel()->SetUserDataSerializer(&g_serializer);
        g_graph.SetUndoLimit(500);
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
            case NGCN_NODECLICK: {
                auto* gn = reinterpret_cast<NODEGRAPHNOTIFY*>(lParam);
                static const wchar_t* parts[] = { L"none", L"title", L"icon", L"image", L"label", L"content" };
                const Node* n = g_graph.GetModel()->GetNode(gn->NodeId);
                if (!n) break;
                // the progress bar of the Download node is a button: it takes the click (the node isn't selected or dragged)
                const bool button = (DemoDraw)(intptr_t)n->UserData == DemoDraw::Progress &&
                    DemoDrawHandler::OnProgressBar(g_graph.GetModel()->GetNodeLayout(*n), gn->NodeX, gn->NodeY);
                if (button) {
                    g_drawHandler.Progress = g_drawHandler.Progress >= 100 ? 0 : g_drawHandler.Progress + 10;
                    g_graph.Refresh();
                }
                wchar_t buf[200];
                swprintf_s(buf, L"Clicked node %u at %.0f,%.0f (the %s)  |  progress %.0f%%", gn->NodeId, gn->NodeX, gn->NodeY,
                    parts[(int)gn->Part], g_drawHandler.Progress);
                SetWindowText(g_status, buf);
                return button;
            }
            case NGCN_CONTEXTMENU: {
                // what a menu would have for a node, an edge or the canvas
                auto* cm = reinterpret_cast<NODEGRAPHCONTEXTMENUNOTIFY*>(lParam);
                NodeGraphModel& m = *g_graph.GetModel();
                enum { Rename = 1, Delete, Rectangle, Ellipse, Diamond, AddHere, PasteHere, Fit, Collapse, Ungroup };
                HMENU menu = CreatePopupMenu();
                if (const Group* g = m.GetGroup(cm->GroupId)) {
                    AppendMenu(menu, MF_STRING, Collapse, g->Collapsed ? L"&Expand" : L"&Collapse");
                    AppendMenu(menu, MF_STRING, Ungroup, L"&Ungroup\tCtrl+Shift+G");
                    AppendMenu(menu, MF_STRING, Delete, L"&Delete the Nodes\tDel");
                }
                else if (const Node* n = m.GetNode(cm->NodeId)) {
                    HMENU shapes = CreatePopupMenu();
                    AppendMenu(shapes, MF_STRING, Rectangle, L"&Rectangle");
                    AppendMenu(shapes, MF_STRING, Ellipse, L"&Ellipse");
                    AppendMenu(shapes, MF_STRING, Diamond, L"&Diamond");
                    CheckMenuRadioItem(shapes, Rectangle, Diamond, Rectangle + (int)n->Style.Shape, MF_BYCOMMAND);
                    AppendMenu(menu, MF_STRING, Rename, L"&Rename\tF2");
                    AppendMenu(menu, MF_STRING, Delete, L"&Delete\tDel");
                    AppendMenu(menu, MF_POPUP, (UINT_PTR)shapes, L"&Shape");
                }
                else if (cm->EdgeId != InvalidEdge)
                    AppendMenu(menu, MF_STRING, Delete, L"&Delete Edge\tDel");
                else {
                    AppendMenu(menu, MF_STRING, AddHere, L"&Add Node Here");
                    AppendMenu(menu, MF_STRING | (g_graph.CanPaste() ? 0 : MF_GRAYED), PasteHere, L"&Paste\tCtrl+V");
                    AppendMenu(menu, MF_STRING, Fit, L"&Fit in View\tHome");
                }
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cm->ScreenPt.x, cm->ScreenPt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                switch (cmd) {
                    case Rename: g_graph.BeginEditLabel(cm->NodeId); break;
                    case Delete: g_graph.DeleteSelected(); break;
                    case Rectangle: case Ellipse: case Diamond:
                        for (NodeId id : g_graph.GetSelectedNodes()) {
                            NodeStyle style = m.GetNode(id)->Style;
                            style.Shape = (NodeShape)(cmd - Rectangle);
                            g_graph.SetNodeStyle(id, style, true);
                        }
                        break;
                    case AddHere: {
                        POINT at = cm->ScreenPt;
                        ScreenToClient(g_graph, &at);
                        Point gp = g_graph.ClientToGraph(at);
                        g_graph.SelectNode(m.AddNode(L"new", gp.X, gp.Y));
                        break;
                    }
                    case PasteHere: g_graph.Paste(); break;
                    case Fit: g_graph.FitInView(); break;
                    case Collapse: g_graph.SetGroupCollapsed(cm->GroupId, !m.GetGroup(cm->GroupId)->Collapsed); break;
                    case Ungroup: g_graph.Ungroup(cm->GroupId); break;
                }
                return TRUE;
            }
            case NGCN_NODEDBLCLICK: {
                // a double click on the progress bar is two clicks of the button, not a rename
                auto* gn = reinterpret_cast<NODEGRAPHNOTIFY*>(lParam);
                const Node* n = g_graph.GetModel()->GetNode(gn->NodeId);
                return n && (DemoDraw)(intptr_t)n->UserData == DemoDraw::Progress &&
                    DemoDrawHandler::OnProgressBar(g_graph.GetModel()->GetNodeLayout(*n), gn->NodeX, gn->NodeY);
            }
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
        case IDC_SCROLLBARS:
            g_graph.SetScrollBars(!g_graph.HasScrollBars());
            CheckMenuItem(GetMenu(hwnd), IDC_SCROLLBARS, g_graph.HasScrollBars() ? MF_CHECKED : MF_UNCHECKED);
            break;
        case IDC_HIGHLIGHT:
            g_graph.SetHighlight(!g_graph.GetHighlight());
            CheckMenuItem(GetMenu(hwnd), IDC_HIGHLIGHT, g_graph.GetHighlight() ? MF_CHECKED : MF_UNCHECKED);
            break;
        case IDC_READONLY:
            g_graph.SetReadOnly(!g_graph.IsReadOnly());
            CheckMenuItem(GetMenu(hwnd), IDC_READONLY, g_graph.IsReadOnly() ? MF_CHECKED : MF_UNCHECKED);
            break;
        case IDC_CURVED: {
            // all the edges, as the menu item is
            const bool curved = !(GetMenuState(GetMenu(hwnd), IDC_CURVED, MF_BYCOMMAND) & MF_CHECKED);
            NodeGraphModel& m = *g_graph.GetModel();
            for (const auto& e : m.Edges())
                m.GetEdge(e.Id)->Style.Curved = curved;
            CheckMenuItem(GetMenu(hwnd), IDC_CURVED, curved ? MF_CHECKED : MF_UNCHECKED);
            g_graph.Refresh();
            break;
        }
        case IDC_SNAPGRID:
            g_graph.SetSnapToGrid(!g_graph.GetSnapToGrid());
            CheckMenuItem(GetMenu(hwnd), IDC_SNAPGRID, g_graph.GetSnapToGrid() ? MF_CHECKED : MF_UNCHECKED);
            break;
        case IDC_GUIDES:
            g_graph.SetAlignmentGuides(!g_graph.GetAlignmentGuides());
            CheckMenuItem(GetMenu(hwnd), IDC_GUIDES, g_graph.GetAlignmentGuides() ? MF_CHECKED : MF_UNCHECKED);
            break;
        case IDC_GROUP:
            if (!g_graph.GetSelectedNodes().empty()) {
                static int s_groups = 0;
                g_graph.GroupSelection(L"Group " + std::to_wstring(++s_groups));
            }
            break;
        case IDC_UNGROUP: {
            // the groups of the selected nodes
            NodeGraphModel& m = *g_graph.GetModel();
            std::vector<GroupId> groups;
            for (NodeId id : g_graph.GetSelectedNodes())
                if (GroupId g = m.GroupOf(id); g != InvalidGroup && std::find(groups.begin(), groups.end(), g) == groups.end())
                    groups.push_back(g);
            for (GroupId g : groups)
                g_graph.Ungroup(g);
            break;
        }
        case IDC_DISTRIBUTEH:
        case IDC_DISTRIBUTEV:
            g_graph.DistributeSelection(LOWORD(wParam) == IDC_DISTRIBUTEH);
            break;
        case IDC_ALIGN + (int)Alignment::Left:   case IDC_ALIGN + (int)Alignment::Center:
        case IDC_ALIGN + (int)Alignment::Right:  case IDC_ALIGN + (int)Alignment::Top:
        case IDC_ALIGN + (int)Alignment::Middle: case IDC_ALIGN + (int)Alignment::Bottom:
            g_graph.AlignSelection((Alignment)(LOWORD(wParam) - IDC_ALIGN));
            break;
        case IDC_FIND:
            ShowFindBar(hwnd);
            break;
        case IDC_FINDNEXT:
        case IDC_FINDPREV:
            if (g_findText.empty()) ShowFindBar(hwnd);
            else FindAndReport(LOWORD(wParam) == IDC_FINDPREV);
            break;
        case IDC_WHEELSCROLL:
            g_graph.SetWheelScrolls(!g_graph.GetWheelScrolls());
            CheckMenuItem(GetMenu(hwnd), IDC_WHEELSCROLL, g_graph.GetWheelScrolls() ? MF_CHECKED : MF_UNCHECKED);
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
    AppendMenu(hEdit, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hEdit, MF_STRING, IDC_FIND, L"&Find...\tCtrl+F");
    AppendMenu(hEdit, MF_STRING, IDC_FINDNEXT, L"Find &Next\tF3");
    AppendMenu(hEdit, MF_STRING, IDC_FINDPREV, L"Find &Previous\tShift+F3");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"&Edit");

    HMENU hGraph = CreatePopupMenu();
    AppendMenu(hGraph, MF_STRING, IDC_BTNFIT,  L"&Fit in View\tF");
    AppendMenu(hGraph, MF_STRING, IDC_BTNADD,  L"&Add Node\tA");
    AppendMenu(hGraph, MF_STRING, IDC_DELSEL,  L"&Delete Selected\tDel");
    AppendMenu(hGraph, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hGraph, MF_STRING, IDC_MINIMAP, L"Toggle &Minimap\tM");
    AppendMenu(hGraph, MF_STRING | MF_CHECKED, IDC_SCROLLBARS, L"Scroll &Bars");
    AppendMenu(hGraph, MF_STRING, IDC_WHEELSCROLL, L"&Wheel Scrolls (Ctrl+Wheel Zooms)");
    AppendMenu(hGraph, MF_STRING, IDC_HIGHLIGHT, L"&Highlight Neighbors");
    AppendMenu(hGraph, MF_STRING, IDC_CURVED, L"C&urved Edges");
    AppendMenu(hGraph, MF_STRING, IDC_READONLY, L"&Read Only");
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

    HMENU hArrange = CreatePopupMenu();
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Left,   L"Align &Left");
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Center, L"Align &Center");
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Right,  L"Align &Right");
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Top,    L"Align &Top");
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Middle, L"Align &Middle");
    AppendMenu(hArrange, MF_STRING, IDC_ALIGN + (int)Alignment::Bottom, L"Align &Bottom");
    AppendMenu(hArrange, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hArrange, MF_STRING, IDC_DISTRIBUTEH, L"Distribute &Horizontally");
    AppendMenu(hArrange, MF_STRING, IDC_DISTRIBUTEV, L"Distribute &Vertically");
    AppendMenu(hArrange, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hArrange, MF_STRING, IDC_SNAPGRID, L"&Snap to Grid (Alt: Not)");
    AppendMenu(hArrange, MF_STRING, IDC_GUIDES, L"Alignment &Guides");
    AppendMenu(hArrange, MF_SEPARATOR, 0, nullptr);
    AppendMenu(hArrange, MF_STRING, IDC_GROUP, L"&Group\tCtrl+G");
    AppendMenu(hArrange, MF_STRING, IDC_UNGROUP, L"&Ungroup\tCtrl+Shift+G");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hArrange, L"&Arrange");

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
        { FVIRTKEY | FCONTROL,    'F', IDC_FIND   },
        { FVIRTKEY | FCONTROL,    'G', IDC_GROUP  },
        { FVIRTKEY | FCONTROL | FSHIFT, 'G', IDC_UNGROUP },
        { FVIRTKEY,             VK_F3, IDC_FINDNEXT },
        { FVIRTKEY | FSHIFT,    VK_F3, IDC_FINDPREV },
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
        // an edit box (the label being edited, the find bar) gets its keys
        if (g_graph.IsEditingLabel() || (g_findEdit && msg.hwnd == g_findEdit) || !TranslateAccelerator(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    return (int)msg.wParam;
}
