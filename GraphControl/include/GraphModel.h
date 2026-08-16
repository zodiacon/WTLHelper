#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace GraphCtrl {

using NodeId = uint32_t;
using EdgeId = uint32_t;

constexpr NodeId InvalidNode = UINT32_MAX;
constexpr EdgeId InvalidEdge = UINT32_MAX;

struct NodeStyle {
    COLORREF FillColor   = RGB(70, 130, 180);
    COLORREF BorderColor = RGB(30,  80, 130);
    COLORREF TextColor   = RGB(255, 255, 255);
    float    BorderWidth  = 1.5f;
    float    CornerRadius = 6.0f;
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
    void*        UserData = nullptr;
};

struct Edge {
    EdgeId       Id;
    NodeId       From;
    NodeId       To;
    std::wstring Label;
    EdgeStyle    Style;
    void*        UserData = nullptr;
};

class GraphModel {
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

    void Clear();

    void  SetDefaultNodeSize(float w, float h);
    float GetDefaultNodeWidth()  const { return m_DefaultNodeWidth;  }
    float GetDefaultNodeHeight() const { return m_DefaultNodeHeight; }

    // Returns false on I/O or format error.
    bool Save(const wchar_t* path) const;
    bool Load(const wchar_t* path);

private:
    std::vector<Node> m_Nodes;
    std::vector<Edge> m_Edges;
    NodeId m_NextNodeId       = 0;
    EdgeId m_NextEdgeId       = 0;
    float  m_DefaultNodeWidth  = 120.0f;
    float  m_DefaultNodeHeight = 40.0f;
};

} // namespace GraphCtrl
