#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace NodeGraphCtrl {

using NodeId  = uint32_t;
using EdgeId  = uint32_t;
using ImageId = uint32_t;

constexpr NodeId  InvalidNode  = UINT32_MAX;
constexpr EdgeId  InvalidEdge  = UINT32_MAX;
constexpr ImageId InvalidImage = UINT32_MAX;

// An image of the graph, kept once and shared by the nodes that show it.
struct Image {
    ImageId              Id;
    uint64_t             Key = 0;              // unique in the process: renderers cache their bitmaps by it
    std::vector<uint8_t> Data;                 // encoded (PNG, JPEG, BMP, ICO, ... anything WIC decodes)
    uint32_t             Width = 0, Height = 0; // in pixels (the largest frame of an icon file)
};

struct NodeStyle {
    COLORREF FillColor   = RGB(70, 130, 180);
    COLORREF BorderColor = RGB(30,  80, 130);
    COLORREF TextColor   = RGB(255, 255, 255);
    float    BorderWidth  = 1.5f;
    float    CornerRadius = 6.0f;
    bool     Code         = false;   // monospaced, left aligned text that scales with the zoom (a block of code)
};

struct EdgeStyle {
    COLORREF Color    = RGB(80, 80, 80);
    float    Width    = 1.5f;
    bool     Directed = true;  // draw arrowhead
};

struct Node {
    NodeId       Id;
    std::wstring Label;
    float        X = 0.0f, Y = 0.0f;   // center position in graph space
    float        Width = 120.0f, Height = 40.0f;
    NodeStyle    Style;
    std::wstring Tooltip;               // shown instead of the label (nothing is shown for a Code node without one)
    void*        UserData = nullptr;
};

struct Point {
    float X = 0.0f, Y = 0.0f;
};

struct Edge {
    EdgeId       Id;
    NodeId       From;
    NodeId       To;
    std::wstring Label;
    EdgeStyle    Style;
    std::wstring Tooltip;               // shown instead of "from -> to"
    std::vector<Point> Waypoints;       // in graph space: the edge goes from the center of From through these to the center of To
    void*        UserData = nullptr;
};

class NodeGraphModel {
public:
    // w=0 or h=0 means use the current default size.
    NodeId AddNode(std::wstring label, float x = 0, float y = 0,
                   float w = 0.0f, float h = 0.0f);
    EdgeId AddEdge(NodeId from, NodeId to, std::wstring label = {});

    bool   RemoveNode(NodeId id);   // also removes incident edges
    bool   RemoveEdge(EdgeId id);

    // Restore previously captured node/edge (push_back, no ID counter change).
    void   RestoreNode(const Node& n);
    void   RestoreEdge(const Edge& e);

    Node*  GetNode(NodeId id);
    Edge*  GetEdge(EdgeId id);
    const Node* GetNode(NodeId id) const;
    const Edge* GetEdge(EdgeId id) const;

    const std::vector<Node>& Nodes() const { return m_Nodes; }
    const std::vector<Edge>& Edges() const { return m_Edges; }

    // Images. They are decoded with WIC (COM is initialized for the call if the thread hasn't done it).
    // Adding the same bytes again returns the existing image. InvalidImage if the data can't be decoded.
    ImageId AddImage(const void* data, size_t size);
    ImageId AddImage(const wchar_t* path);                       // the file's bytes as they are
    ImageId AddImage(HICON icon);                                // stored as PNG
    ImageId AddImage(HBITMAP bitmap, bool hasAlpha = false);     // stored as PNG; hasAlpha: a 32 bit DIB with straight alpha
    bool    RemoveImage(ImageId id);
    const Image* GetImage(ImageId id) const;
    const std::vector<Image>& Images() const { return m_Images; }

    void Clear();   // the nodes, edges and images

    void  SetDefaultNodeSize(float w, float h);
    float GetDefaultNodeWidth()  const { return m_DefaultNodeWidth;  }
    float GetDefaultNodeHeight() const { return m_DefaultNodeHeight; }

    // Returns false on I/O or format error.
    // A binary file of the nodes and edges with their styles, tooltips and waypoints (not UserData).
    // Load also reads the first version of the file (without the Code style, tooltips and waypoints).
    bool Save(const wchar_t* path) const;
    bool Load(const wchar_t* path);

    // Writes the graph as it is drawn (the colors, the bends of the edges) to a vector image. False for an empty graph or on error.
    // background: the canvas color (CNodeGraphControl::GetBackgroundColor for the control's).
    bool SaveSvg(const wchar_t* path, COLORREF background = RGB(38, 38, 38)) const;

private:
    std::vector<Node> m_Nodes;
    std::vector<Edge> m_Edges;
    std::vector<Image> m_Images;
    NodeId m_NextNodeId       = 0;
    EdgeId m_NextEdgeId       = 0;
    ImageId m_NextImageId     = 0;
    float  m_DefaultNodeWidth  = 120.0f;
    float  m_DefaultNodeHeight = 40.0f;
};

} // namespace NodeGraphCtrl
