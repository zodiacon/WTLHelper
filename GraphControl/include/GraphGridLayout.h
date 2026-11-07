#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <vector>

namespace GraphCtrl {

// Where CGraphGrid puts its tiles; no window, so the tests can check it.
struct GridLayoutInput {
    int width = 0, height = 0;   // the grid's client area
    int count = 0;               // tiles
    int columns = 0;             // 0 = auto (roughly square)
    int rows = 0;                // 0 = as many as the tiles take
    int spacing = 4;             // around the tiles, and between groups
    // A group key per tile (empty: no groups): a run of tiles with the same key is a group (the logical
    // processors of one core), kept in one row when it fits and drawn `groupSpacing` apart, the room this
    // saves going to the gaps between the row's groups, so each full row still spans the grid.
    std::vector<uint32_t> groups;
    int groupSpacing = 1;
};

// One rectangle per tile, in the grid's client coordinates; an empty one for a tile that doesn't fit.
std::vector<RECT> LayoutGridTiles(const GridLayoutInput& input);

} // namespace GraphCtrl
