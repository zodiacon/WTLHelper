#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>   // CLR_DEFAULT, CLR_NONE
#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "GraphData.h"

// Small graphs inside list view and TreeListView cells: line and column sparklines, meters and stacked bars.
// They're drawn over the list's own painting of the cell (selection, hot tracking and dark mode stay the
// list's), so a cell can keep its text for screen readers and sorting.

namespace GraphCtrl {

// ---- Drawing --------------------------------------------------------------------------
//
// Each draws into a cell of an HDC over what's there already (the list's background, its selection or hot
// highlight): the cell's pixels are copied out, drawn over with anti-aliasing and copied back. Sizes are in
// DIPs, scaled by dpi. Called in a list's custom draw for the sub-item (its post-paint stage, after the list
// painted the cell), or anywhere else with any DC that can be read back (not a printer's).

// a meter's (or a column's) color from a value up, in ascending order of From
struct CellThreshold {
    float    From;
    COLORREF Color;
};

struct SparklineStyle {
    COLORREF LineColor   = RGB(17, 125, 187);
    COLORREF FillColor   = RGB(105, 185, 235);
    float    FillOpacity = 0.35f;     // under the line; 0: the line only
    float    LineWidth   = 1.5f;      // DIPs
    size_t   Slots       = 0;         // samples across the cell; 0: the series' capacity
    float    Min         = 0;
    float    Max         = 100;
    bool     AutoScale   = false;     // up to the highest sample shown (from Min, or a lower sample)
    float    MinSpan     = 0;         // auto scale: at least Min + MinSpan at the top
    int      Padding     = 2;         // DIPs inside the cell
};

struct ColumnStyle {
    COLORREF Color       = RGB(17, 125, 187);
    const CellThreshold* Thresholds = nullptr;   // each bar colored by its value
    size_t   ThresholdCount = 0;
    size_t   Slots       = 0;         // 0: the series' capacity
    float    Min         = 0;
    float    Max         = 100;
    bool     AutoScale   = false;
    float    MinSpan     = 0;
    float    Gap         = 1;         // DIPs between bars, when they're at least three times as wide
    int      Padding     = 2;
};

struct MeterStyle {
    float    Min         = 0;
    float    Max         = 100;
    COLORREF BarColor    = RGB(17, 125, 187);
    const CellThreshold* Thresholds = nullptr;   // the bar's color by the value (else BarColor)
    size_t   ThresholdCount = 0;
    COLORREF TrackColor  = CLR_DEFAULT;   // the empty part; CLR_DEFAULT: a tint of the cell's background
    int      BarHeight   = 0;         // DIPs, centered; 0: the cell's height less the padding
    int      Padding     = 2;
};

struct StackedStyle {
    const COLORREF* Colors = nullptr; // segment i: Colors[i % ColorCount]; none: a default set
    size_t   ColorCount  = 0;
    float    Total       = 0;         // the whole bar; 0: the values' sum
    COLORREF TrackColor  = CLR_DEFAULT;
    int      BarHeight   = 0;
    int      Padding     = 2;
};

void DrawSparkline(HDC hdc, const RECT& cell, const Series& series, const SparklineStyle& style = {}, UINT dpi = 96);
void DrawColumns(HDC hdc, const RECT& cell, const Series& series, const ColumnStyle& style = {}, UINT dpi = 96);
void DrawMeter(HDC hdc, const RECT& cell, float value, const MeterStyle& style = {}, UINT dpi = 96);
void DrawStacked(HDC hdc, const RECT& cell, const float* values, size_t count, const StackedStyle& style = {}, UINT dpi = 96);

// ---- Per-row history ----------------------------------------------------------------
//
// A series for each row, by a key the app picks (a process id, say). A refresh pushes a sample for every row
// it sees between BeginUpdate and EndUpdate; the rows it didn't see are dropped then (the process exited).
// A row seen for the first time starts with no samples, so its sparkline grows in from the right.

template<typename Key, typename Hash = std::hash<Key>>
class CellHistory {
public:
    explicit CellHistory(size_t capacity = k_DefaultCapacity) : m_Capacity(std::max(capacity, k_MinCapacity)) {}

    void BeginUpdate() {
        m_Generation++;
    }

    // drops the rows not pushed (or kept) since BeginUpdate
    void EndUpdate() {
        for (auto it = m_Rows.begin(); it != m_Rows.end();) {
            if (it->second.Generation != m_Generation)
                it = m_Rows.erase(it);
            else
                ++it;
        }
    }

    Series& Push(const Key& key, float value) {
        auto it = m_Rows.find(key);
        if (it == m_Rows.end())
            it = m_Rows.emplace(key, Row{ Series(InvalidSeries, {}, {}, m_Capacity), m_Generation }).first;
        it->second.Generation = m_Generation;
        it->second.Samples.Push(value);
        return it->second.Samples;
    }

    // keeps a row through this refresh without a sample (or with a gap: Push(key, MissingSample))
    void Keep(const Key& key) {
        auto it = m_Rows.find(key);
        if (it != m_Rows.end())
            it->second.Generation = m_Generation;
    }

    const Series* Find(const Key& key) const {
        auto it = m_Rows.find(key);
        return it == m_Rows.end() ? nullptr : &it->second.Samples;
    }

    bool   Remove(const Key& key) { return m_Rows.erase(key) > 0; }
    void   Clear()                { m_Rows.clear(); }
    size_t Count() const          { return m_Rows.size(); }

    // every row's series keeps its most recent samples
    void SetCapacity(size_t capacity) {
        m_Capacity = std::max(capacity, k_MinCapacity);
        for (auto& [key, row] : m_Rows)
            row.Samples.SetCapacity(m_Capacity);
    }
    size_t Capacity() const { return m_Capacity; }

private:
    struct Row {
        Series   Samples;
        uint64_t Generation;
    };

    std::unordered_map<Key, Row, Hash> m_Rows;
    size_t   m_Capacity;
    uint64_t m_Generation = 0;
};

} // namespace GraphCtrl
