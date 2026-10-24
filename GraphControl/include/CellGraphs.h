#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "GraphData.h"

// Small graphs inside list view and TreeListView cells: line and column sparklines, meters and stacked bars.
// They're drawn over the list's own painting of the cell (selection, hot tracking and dark mode stay the
// list's), so a cell can keep its text for screen readers and sorting.

namespace GraphCtrl {

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
