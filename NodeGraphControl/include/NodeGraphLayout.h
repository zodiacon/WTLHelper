#pragma once
#include <cstdint>
#include "NodeGraphModel.h"

namespace NodeGraphCtrl {

// Automatic arrangement of the nodes of a graph.
enum class LayoutAlgorithm {
    Layered,        // Sugiyama style: the edges point one way (down by default) in layers, few crossings,
                    // long edges bend around the nodes in between. The best for dependency / call / flow graphs.
    Tree,           // a tidy tree of a spanning tree (from the roots); the other edges are drawn straight
    Radial,         // the spanning tree in rings around the root
    ForceDirected,  // Fruchterman-Reingold: edges as springs, nodes repel each other. For general (undirected) graphs.
    Circular,       // the nodes on a circle, neighbors kept close
    Grid,           // rows and columns, in the order of the nodes in the model
};

enum class LayoutDirection {
    TopToBottom,
    BottomToTop,
    LeftToRight,
    RightToLeft,
};

struct LayoutOptions {
    LayoutDirection Direction = LayoutDirection::TopToBottom;  // Layered and Tree
    float    NodeSpacing    = 40.0f;   // the gap between nodes next to each other
    float    LayerSpacing   = 80.0f;   // the gap between layers (Layered, Tree) and rings (Radial)
    int      Iterations     = 300;     // ForceDirected
    int      CrossingPasses = 24;      // Layered: the sweeps to reduce the crossings of the edges
    int      GridColumns    = 0;       // Grid: 0 makes it about square
    NodeId   Root           = InvalidNode;  // Tree, Radial: InvalidNode picks the nodes without incoming edges (Tree) or
                                            // the one of them that reaches the most nodes, else the center (Radial)
    uint32_t Seed           = 1;       // ForceDirected: the same seed and graph give the same result
};

// Moves the nodes of the model (their sizes are not changed) and sets the waypoints of the edges: Layered routes
// the edges that cross layers through waypoints, the others clear them. Each connected part of the graph is laid
// out by itself and the parts are placed next to each other. The top left of the graph stays where it was.
// Self loops are kept but play no part.
void Layout(NodeGraphModel& model, LayoutAlgorithm algorithm, const LayoutOptions& options = {});

} // namespace NodeGraphCtrl
