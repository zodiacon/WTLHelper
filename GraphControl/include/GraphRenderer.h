#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include "GraphModel.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace GraphCtrl {

enum class ResizeHandle { None, NW, N, NE, E, SE, S, SW, W };

struct ViewTransform {
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    float Scale   = 1.0f;

    // Graph space -> screen space
    D2D1_POINT_2F ToScreen(float gx, float gy) const {
        return { gx * Scale + OffsetX, gy * Scale + OffsetY };
    }
    // Screen space -> graph space
    D2D1_POINT_2F ToGraph(float sx, float sy) const {
        return { (sx - OffsetX) / Scale, (sy - OffsetY) / Scale };
    }
};

struct EdgePreview {
    bool   Active   = false;
    NodeId FromNode = InvalidNode;
    float  ScreenX  = 0;  // current mouse position in client/screen coords
    float  ScreenY  = 0;
};

struct RubberBand {
    bool  Active = false;
    float X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;  // screen coords
};

struct MinimapConfig {
    bool  Visible = false;
    float X = 0.0f, Y = 0.0f;          // top-left in client coords
    float Width = 180.0f, Height = 130.0f;
};

struct ResizeOverlay {
    NodeId       Node          = InvalidNode;
    ResizeHandle HoveredHandle = ResizeHandle::None;
};

class GraphRenderer {
public:
    GraphRenderer() = default;
    ~GraphRenderer();

    HRESULT Init(HINSTANCE hInstance);
    void    Shutdown();

    // Called on WM_PAINT: binds render target to dc, draws everything.
    void Render(HDC hdc, const RECT& clientRect,
                const GraphModel& model, const ViewTransform& vt,
                const std::vector<NodeId>& selectedNodes, EdgeId selectedEdge,
                bool drawGrid, const EdgePreview& preview = {},
                const RubberBand& rubberBand = {},
                const MinimapConfig& minimap = {},
                const ResizeOverlay& resizeOverlay = {});

    // Hit testing in graph space.
    NodeId HitTestNode(const GraphModel& model, float gx, float gy) const;
    EdgeId HitTestEdge(const GraphModel& model, float gx, float gy, float tolerance = 5.0f) const;

    // Resize handle hit test in screen space. Returns None if no handle is hit.
    ResizeHandle HitTestResizeHandle(const Node& n, const ViewTransform& vt,
                                     float sx, float sy) const;

private:
    HRESULT EnsureDeviceResources(HDC hdc, const RECT& rc);
    void    DiscardDeviceResources();

    void DrawGrid(const RECT& clientRect, const ViewTransform& vt);
    void DrawEdge(const Edge& e, const GraphModel& model,
                  const ViewTransform& vt, bool selected);
    void DrawEdgePreview(const EdgePreview& preview, const GraphModel& model,
                         const ViewTransform& vt);
    void DrawNode(const Node& n, const ViewTransform& vt, bool selected);
    void DrawArrowhead(D2D1_POINT_2F tip, D2D1_POINT_2F dir);
    void DrawRubberBand(const RubberBand& rb);
    void DrawMinimap(const MinimapConfig& cfg, const RECT& clientRect,
                     const GraphModel& model, const ViewTransform& vt);
    void DrawResizeHandles(const Node& n, const ViewTransform& vt,
                           ResizeHandle hovered);

    // Device-independent resources (created once).
    ComPtr<ID2D1Factory>        m_D2dFactory;
    ComPtr<IDWriteFactory>      m_DwFactory;
    ComPtr<IDWriteTextFormat>   m_TextFormat;      // 13pt, used for node labels
    ComPtr<IDWriteTextFormat>   m_EdgeTextFormat;  // 11pt, used for edge labels
    ComPtr<ID2D1StrokeStyle>    m_DashStyle;

    // Device-dependent resources (recreated if device lost).
    ComPtr<ID2D1DCRenderTarget>  m_RenderTarget;
    ComPtr<ID2D1SolidColorBrush> m_Brush;
};

} // namespace GraphCtrl
