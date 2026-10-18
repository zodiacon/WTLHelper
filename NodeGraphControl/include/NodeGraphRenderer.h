#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "NodeGraphModel.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace NodeGraphCtrl {

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

class NodeGraphRenderer;

// ---- Owner draw ----------------------------------------------------------------
// Nodes with NodeStyle::OwnerDraw are passed to the handler set with CNodeGraphControl::SetNodeDrawHandler while they
// are drawn. The node is drawn in this order: background (the fill and the title band), content (the image, the icon,
// the title's text and the label), border (with the selection). PrePaint comes first and says what of it to skip; it
// can draw instead of what it skips. PostPaint, if asked for, comes last, to draw over it all.
//
// While the handler is called, the target's transform is set to the node's own graph units: (0,0) is the node's
// top-left corner and the zoom is applied, so drawing scales with it. The drawing is clipped to the node's bounds (a
// rectangle: Shape has the rounded corners for drawing inside them). The renderer restores the transform and the clip
// afterwards. Don't call BeginDraw/EndDraw or change the target otherwise.
//
// Resources made on the target (brushes, bitmaps, layers) belong to it: keep them only while DeviceGeneration doesn't
// change. It is unique in the process for each target, so a handler shared by several controls (each has its own) can
// key its resources by it. Factory resources (geometries, text formats) can be kept for good.

enum NodeDrawFlags : uint32_t {
    NDF_DEFAULT        = 0,
    NDF_SKIPBACKGROUND = 1,     // no fill and title band
    NDF_SKIPCONTENT    = 2,     // no image, icon, title text and label
    NDF_SKIPBORDER     = 4,     // no border (so no selection either: draw it with Selected)
    NDF_POSTPAINT      = 8,     // call PostPaint after the node is drawn
};

struct NodeDrawContext {
    ID2D1RenderTarget*    Target;
    ID2D1Factory*         D2DFactory;
    IDWriteFactory*       DWriteFactory;
    NodeGraphRenderer&    Renderer;       // GetBitmap draws the model's images
    const NodeGraphModel& Model;
    const Node&           GraphNode;
    NodeLayout            Layout;         // where the default parts go, in the node's units (as the transform)
    D2D1_ROUNDED_RECT     Shape;          // the node's outline, in its units
    D2D1_RECT_F           ScreenRect;     // the node on the target, without the transform
    float                 Scale;          // the zoom: target pixels (DIPs) per graph unit
    bool                  Selected;
    bool                  Hovered;
    bool                  Exporting;      // drawn into an image file (CNodeGraphControl::SaveImage), not on the screen
    uint32_t              DeviceGeneration;
};

struct INodeDrawHandler {
    virtual uint32_t PrePaint(const NodeDrawContext& context) = 0;  // NodeDrawFlags
    virtual void     PostPaint(const NodeDrawContext& /*context*/) {}
protected:
    ~INodeDrawHandler() = default;
};

class NodeGraphRenderer {
public:
    NodeGraphRenderer() = default;
    ~NodeGraphRenderer();

    HRESULT Init(HINSTANCE hInstance);
    void    Shutdown();

    // Called on WM_PAINT: binds render target to dc, draws everything.
    void Render(HDC hdc, const RECT& clientRect,
                const NodeGraphModel& model, const ViewTransform& vt,
                const std::vector<NodeId>& selectedNodes, EdgeId selectedEdge,
                bool drawGrid, const EdgePreview& preview = {},
                const RubberBand& rubberBand = {},
                const MinimapConfig& minimap = {},
                const ResizeOverlay& resizeOverlay = {});

    // The canvas color. The grid and the selection are drawn in colors that show on it (light or dark).
    void     SetBackgroundColor(COLORREF color) { m_Background = color; }
    COLORREF GetBackgroundColor() const { return m_Background; }

    // Hit testing in graph space.
    NodeId HitTestNode(const NodeGraphModel& model, float gx, float gy) const;
    EdgeId HitTestEdge(const NodeGraphModel& model, float gx, float gy, float tolerance = 5.0f) const;

    // Resize handle hit test in screen space. Returns None if no handle is hit.
    ResizeHandle HitTestResizeHandle(const Node& n, const ViewTransform& vt,
                                     float sx, float sy) const;

    // A bitmap of an image of the model for the render target, made the first time it is asked for. Its size in DIPs is
    // the image's size in pixels. Use it only while drawing; null before the first Render, or if the image isn't in the
    // model or can't be decoded.
    ID2D1Bitmap* GetBitmap(const NodeGraphModel& model, ImageId id);

    // Owner draw (see INodeDrawHandler). The handler isn't owned.
    void SetNodeDrawHandler(INodeDrawHandler* handler) { m_DrawHandler = handler; }
    INodeDrawHandler* GetNodeDrawHandler() const { return m_DrawHandler; }
    void SetHoveredNode(NodeId id) { m_HoveredNode = id; }
    void SetExporting(bool exporting) { m_Exporting = exporting; }

private:
    HRESULT EnsureDeviceResources(HDC hdc, const RECT& rc);
    void    DiscardDeviceResources();

    void DrawGrid(const RECT& clientRect, const ViewTransform& vt);
    void DrawEdge(const Edge& e, const NodeGraphModel& model,
                  const ViewTransform& vt, bool selected);
    void DrawSelfLoop(const Edge& e, const Node& n, const ViewTransform& vt, bool selected);
    void DrawEdgePreview(const EdgePreview& preview, const NodeGraphModel& model,
                         const ViewTransform& vt);
    void DrawNode(const NodeGraphModel& model, const Node& n, const ViewTransform& vt, bool selected);
    void DrawCodeText(const Node& n, const Rect& area, const ViewTransform& vt);
    void DrawTitle(const Node& n, const Rect& area, const ViewTransform& vt);
    void DrawLabel(const Node& n, const Rect& area, const D2D1_RECT_F& node, const ViewTransform& vt);
    void FillBand(const D2D1_RECT_F& rect, float radius, bool roundTop, bool roundBottom);
    void DrawImage(const NodeGraphModel& model, ImageId id, const D2D1_RECT_F& dst, const Rect* src = nullptr);
    void DrawArrowhead(D2D1_POINT_2F tip, D2D1_POINT_2F dir);
    void DrawRubberBand(const RubberBand& rb);
    void DrawMinimap(const MinimapConfig& cfg, const RECT& clientRect,
                     const NodeGraphModel& model, const ViewTransform& vt);
    void DrawResizeHandles(const Node& n, const ViewTransform& vt,
                           ResizeHandle hovered);

    // Device-independent resources (created once).
    ComPtr<ID2D1Factory>        m_D2dFactory;
    ComPtr<IDWriteFactory>      m_DwFactory;
    ComPtr<IDWriteTextFormat>   m_TextFormat;      // 13pt, used for node labels
    ComPtr<IDWriteTextFormat>   m_EdgeTextFormat;  // 11pt, used for edge labels
    ComPtr<IDWriteTextFormat>   m_CodeTextFormat;  // 12 units monospaced, left aligned, for nodes with NodeStyle::Code
    ComPtr<IDWriteTextFormat>   m_TitleTextFormat; // 12 units bold, left aligned, one line with an ellipsis, for NodeContent::Title
    ComPtr<IDWriteInlineObject> m_Ellipsis;
    ComPtr<ID2D1StrokeStyle>    m_DashStyle;

    // Device-dependent resources (recreated if device lost).
    ComPtr<ID2D1DCRenderTarget>  m_RenderTarget;
    ComPtr<ID2D1SolidColorBrush> m_Brush;

    // the (from, to) pairs of the edges of the model that is being drawn: edges that have one going the other way are drawn apart
    std::unordered_set<uint64_t> m_EdgePairs;

    // Image::Key -> its bitmap (null if it couldn't be made, so that it isn't tried on every paint). Device dependent.
    std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> m_Bitmaps;
    void PruneBitmaps(const NodeGraphModel& model);
    ComPtr<ID2D1Bitmap> CreateBitmap(const Image& image);

    INodeDrawHandler* m_DrawHandler = nullptr;
    NodeId   m_HoveredNode = InvalidNode;
    bool     m_Exporting = false;
    uint32_t m_DeviceGeneration = 0;    // of the current render target, unique in the process
    uint32_t CallOwnerDraw(const NodeGraphModel& model, const Node& n, const NodeLayout& layout,
                           const D2D1_ROUNDED_RECT& rr, const ViewTransform& vt, bool selected, bool post);

    COLORREF m_Background = RGB(38, 38, 38);
    bool IsLightBackground() const;
    D2D1_COLOR_F SelectionColor() const;
};

} // namespace NodeGraphCtrl
