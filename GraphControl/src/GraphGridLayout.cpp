// GraphGridLayout.cpp : where CGraphGrid's tiles go, groups kept together.
//

#include "../include/GraphGridLayout.h"

#include <algorithm>
#include <cmath>

namespace GraphCtrl {

std::vector<RECT> LayoutGridTiles(const GridLayoutInput& in) {
    const int n = std::max(in.count, 0);
    std::vector<RECT> rects(n, RECT{});
    if (n == 0 || in.width <= 0 || in.height <= 0) return rects;

    const int spacing = std::max(in.spacing, 0);
    const int inner = std::max(in.groupSpacing, 0);
    int cols = in.columns > 0 ? in.columns : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    cols = std::max(cols, 1);

    const bool grouped = in.groups.size() >= static_cast<size_t>(n);
    // whether tile i is in the same group as tile i - 1
    auto joined = [&](int i) { return grouped && i > 0 && in.groups[i] == in.groups[i - 1]; };

    // the cells: a group that doesn't fit what's left of a row starts the next one (unless it's wider than a row)
    std::vector<int> rowOf(n), colOf(n);
    int row = 0, col = 0;
    for (int i = 0; i < n; ) {
        int size = 1;
        while (i + size < n && joined(i + size)) size++;
        if (col > 0 && col + size > cols && size <= cols) {
            row++;
            col = 0;
        }
        for (int j = 0; j < size; j++, i++) {
            if (col == cols) {
                row++;
                col = 0;
            }
            rowOf[i] = row;
            colOf[i] = col++;
        }
    }
    const int rows = std::max(in.rows > 0 ? in.rows : row + 1, 1);

    // sized as if every gap were a full one, so any row fits; the room a row's inner gaps leave goes to its others
    const int cellW = (in.width - spacing * (cols + 1)) / cols;
    const int cellH = (in.height - spacing * (rows + 1)) / rows;
    if (cellW <= 0 || cellH <= 0) return rects;

    for (int first = 0; first < n; ) {
        int last = first;
        while (last + 1 < n && rowOf[last + 1] == rowOf[first]) last++;
        const int r = rowOf[first];
        if (r < rows) {
            int innerGaps = 0;
            for (int i = first + 1; i <= last; i++)
                if (joined(i)) innerGaps++;
            // a row that isn't full is spaced as if its empty cells held more groups like its last one, so its
            // tiles line up with the full rows'
            int lastSize = 1;
            while (last - lastSize >= first && joined(last - lastSize + 1)) lastSize++;
            for (int empty = cols - (last - first + 1); empty > 0; empty -= lastSize)
                innerGaps += std::min(empty, lastSize) - 1;
            // the other gaps: the two edges and those between groups
            const int outerGaps = cols + 1 - innerGaps;
            const int spare = in.width - cols * cellW - innerGaps * inner;
            const int y = spacing + r * (cellH + spacing);
            int outer = 0;
            auto outerGap = [&] {
                const int gap = spare * (outer + 1) / outerGaps - spare * outer / outerGaps;
                outer++;
                return gap;
            };
            int x = outerGap();
            for (int i = first; i <= last; i++) {
                if (i > first)
                    x += joined(i) ? inner : outerGap();
                rects[i] = RECT{ x, y, x + cellW, y + cellH };
                x += cellW;
            }
        }
        first = last + 1;
    }
    return rects;
}

} // namespace GraphCtrl
