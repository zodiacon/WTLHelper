#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>   // CLR_DEFAULT, CLR_NONE
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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

// a color from a value up, in ascending order of From (a meter's, a column's, a sparkline's by its newest sample)
struct CellThreshold {
    float    From;
    COLORREF Color;
};

// A color left at CLR_DEFAULT is picked by the cell's background: a darker blue on a light one, a lighter one on a
// dark one, white or black on a blue one (a selection highlight). A threshold's color may be CLR_DEFAULT too.

struct SparklineStyle {
    COLORREF LineColor   = CLR_DEFAULT;
    COLORREF FillColor   = CLR_DEFAULT;
    const CellThreshold* Thresholds = nullptr;   // the line and the fill colored by the newest sample
    size_t   ThresholdCount = 0;
    float    FillOpacity = 0.35f;     // under the line; 0: the line only
    float    LineWidth   = 1.5f;      // DIPs
    size_t   Slots       = 0;         // samples across the cell; 0: the series' capacity
    float    Min         = 0;
    float    Max         = 100;
    bool     AutoScale   = false;     // up to the highest sample shown (from Min, or a lower sample)
    float    MinSpan     = 0;         // auto scale: at least Min + MinSpan at the top
    float    Reference   = MissingSample;   // a dashed line at this value (a limit); missing: none
    int      Padding     = 2;         // DIPs inside the cell
};

struct ColumnStyle {
    COLORREF Color       = CLR_DEFAULT;
    const CellThreshold* Thresholds = nullptr;   // each bar colored by its value
    size_t   ThresholdCount = 0;
    size_t   Slots       = 0;         // 0: the series' capacity
    float    Min         = 0;
    float    Max         = 100;
    bool     AutoScale   = false;
    float    MinSpan     = 0;
    float    Gap         = 1;         // DIPs between bars, when they're at least three times as wide
    float    Reference   = MissingSample;   // a dashed line at this value; missing: none
    int      Padding     = 2;
};

struct MeterStyle {
    float    Min         = 0;
    float    Max         = 100;
    COLORREF BarColor    = CLR_DEFAULT;
    const CellThreshold* Thresholds = nullptr;   // the bar's color by the value (else BarColor)
    size_t   ThresholdCount = 0;
    COLORREF TrackColor  = CLR_DEFAULT;   // the empty part; CLR_DEFAULT: a tint of the cell's background
    int      BarHeight   = 0;         // DIPs, centered; 0: the cell's height less the padding
    int      Padding     = 2;
    // text over the bar (DrawMeter's text), in the DC's font: DT_LEFT, DT_CENTER or DT_RIGHT
    UINT     TextAlign   = DT_CENTER;
    COLORREF TextColor   = CLR_DEFAULT;   // CLR_DEFAULT: black or white, whichever reads on what's under it
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
// with text over the bar ("512 MB"), in the font selected into hdc
void DrawMeter(HDC hdc, const RECT& cell, float value, const wchar_t* text, const MeterStyle& style = {}, UINT dpi = 96);
void DrawStacked(HDC hdc, const RECT& cell, const float* values, size_t count, const StackedStyle& style = {}, UINT dpi = 96);

// ---- List views --------------------------------------------------------------------
//
// Graphs in the columns of a list view (report view) or a TreeListView, drawn from the parent's handling of their
// NM_CUSTOMDRAW. The app gives a column a kind and a style, and a callback supplies a cell's data when it's
// painted. The list paints the cell as usual (its selection, hot and background colors), and the graph replaces
// the text it drew there; the text stays the item's, for screen readers, sorting and copying.
// Column 0 can't have a graph.

enum class CellKind {
    None,
    Sparkline,
    Columns,
    Meter,
    Stacked,
};

// the cell being painted (or under the mouse, for a tooltip)
struct CellRef {
    HWND     Window;      // the control sending the custom draw (a list view, a TreeListView); for a tooltip, the
                          // list view given to EnableTooltips
    UINT_PTR Item;        // a list view's row index, a TreeListView's HTLITEM
    LPARAM   ItemParam;   // a list view item's lParam (0 in a virtual list)
    int      Column;
};

// what a cell shows, by the column's kind
struct CellData {
    const Series* Samples = nullptr;         // sparkline, columns
    float         Value   = MissingSample;   // meter
    const float*  Values  = nullptr;         // stacked
    size_t        Count   = 0;
    const wchar_t* Text    = nullptr;        // meter: over the bar
    const wchar_t* Tooltip = nullptr;        // instead of the one made from the data (see EnableTooltips)
};
// The pointers need to stay valid only until the callback's caller is done with them (the cell is drawn, the
// tooltip's text is copied): a buffer the callback reuses is fine.

class CellGraphs {
public:
    // fills in a cell's data; false leaves the cell empty
    using DataCallback = std::function<bool(const CellRef& cell, CellData& data)>;

    void SetDataCallback(DataCallback callback) { m_Data = std::move(callback); }

    // a column's graph (the thresholds and colors a style points to are copied)
    void     SetSparkline(int column, const SparklineStyle& style = {});
    void     SetColumns(int column, const ColumnStyle& style = {});
    void     SetMeter(int column, const MeterStyle& style = {});
    void     SetStacked(int column, const StackedStyle& style = {});
    void     RemoveColumn(int column);
    void     ClearColumns() { m_Columns.clear(); }
    CellKind GetKind(int column) const;
    // how a column's values read in its tooltip (GraphCtrl::Format::Percent, say); none: "12.5"
    void     SetFormat(int column, ValueFormatFn format);

    // Tooltips over the graph cells of a list view: a sparkline's or columns' last, lowest, highest and average
    // of the samples shown, a meter's value (its text, if it has one), a stacked bar's values; or the data's
    // Tooltip. For a TreeListView, its GetListCtrl(), with rowToItem mapping a row to the item the custom draw
    // passes (GetVisibleItem). One list at a time; the tooltip goes when the list does.
    void EnableTooltips(HWND list, std::function<UINT_PTR(int row)> rowToItem = {});
    void DisableTooltips();

    // A list view's NM_CUSTOMDRAW, from the parent: pass what the handler would return otherwise (after setting
    // any colors it wants) and return what this returns.
    LRESULT CustomDraw(NMLVCUSTOMDRAW* cd, LRESULT result = CDRF_DODEFAULT);
    // The same for a control relaying a list view's custom draw, with its sub-item and its own handle for the
    // item. TreeListView's NMTLVCUSTOMDRAW:
    //     CustomDraw(&cd->nmcd, cd->iSubItem, (UINT_PTR)cd->hItem, result)
    LRESULT CustomDraw(NMCUSTOMDRAW* cd, int subItem, UINT_PTR item, LRESULT result = CDRF_DODEFAULT);

    CellGraphs() = default;
    CellGraphs(const CellGraphs&) = delete;
    CellGraphs& operator=(const CellGraphs&) = delete;
    ~CellGraphs() { DisableTooltips(); }

private:
    struct Column {
        CellKind       Kind = CellKind::None;
        SparklineStyle Sparkline;
        ColumnStyle    Columns;
        MeterStyle     Meter;
        StackedStyle   Stacked;
        std::vector<CellThreshold> Thresholds;
        std::vector<COLORREF>      Colors;
        ValueFormatFn              Format;
    };

    Column& Set(int column, CellKind kind);
    void    Draw(const Column& column, const CellRef& cell, HDC hdc, const RECT& rc);
    std::wstring TipText();
    void    TrackTip(POINT pt);
    static LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    std::vector<Column>   m_Columns;   // by column index
    DataCallback          m_Data;
    // the cell being painted, before the list painted it, and after
    std::vector<uint32_t> m_Before, m_After;
    RECT                  m_BeforeRect{};
    // the tooltip, and the cell it's for
    HWND                  m_TipList = nullptr;
    HWND                  m_Tip     = nullptr;
    std::function<UINT_PTR(int)> m_RowToItem;
    int                   m_TipRow    = -1;
    int                   m_TipColumn = -1;
    UINT_PTR              m_TipItem   = 0;
    std::wstring          m_TipText;
};

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
