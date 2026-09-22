#include "../include/GraphData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>

namespace GraphCtrl {

// ---- Series ------------------------------------------------------------------

Series::Series(SeriesId id, std::wstring name, const SeriesStyle& style, size_t capacity)
    : m_Id(id), m_Name(std::move(name)), m_Style(style) {
    m_Samples.resize(std::max(capacity, k_MinCapacity), 0.0f);
}

void Series::SetCapacity(size_t capacity) {
    capacity = std::max(capacity, k_MinCapacity);
    if (capacity == m_Samples.size()) return;

    // Keep the most recent min(count, capacity) samples, oldest first.
    const size_t keep = std::min(m_Count, capacity);
    std::vector<float> fresh(capacity, 0.0f);
    for (size_t i = 0; i < keep; i++)
        fresh[i] = At(m_Count - keep + i);

    m_Samples = std::move(fresh);
    m_Count   = keep;
    m_Head    = (keep == capacity) ? 0 : keep;

    m_Sum = 0;
    for (size_t i = 0; i < m_Count; i++)
        m_Sum += m_Samples[i];
}

void Series::Push(float value) {
    const size_t cap = m_Samples.size();
    if (m_Count == cap)
        m_Sum -= m_Samples[m_Head];   // evict the oldest
    else
        m_Count++;

    m_Samples[m_Head] = value;
    m_Sum += value;
    m_Head = (m_Head + 1) % cap;
}

void Series::Clear() {
    m_Head  = 0;
    m_Count = 0;
    m_Sum   = 0;
    std::fill(m_Samples.begin(), m_Samples.end(), 0.0f);
}

float Series::At(size_t index) const {
    if (index >= m_Count) return 0.0f;
    const size_t cap = m_Samples.size();
    return m_Samples[(m_Head + cap - m_Count + index) % cap];
}

float Series::AtFromEnd(size_t k) const {
    return k < m_Count ? At(m_Count - 1 - k) : 0.0f;
}

float Series::Last() const {
    return m_Count ? At(m_Count - 1) : 0.0f;
}

// Scanned rather than tracked incrementally: a ring eviction can drop the
// current extreme, and the capacity is small enough that it does not matter.
float Series::Min() const {
    if (!m_Count) return 0.0f;
    float lo = FLT_MAX;
    for (size_t i = 0; i < m_Count; i++)
        lo = std::min(lo, At(i));
    return lo;
}

float Series::Max() const {
    if (!m_Count) return 0.0f;
    float hi = -FLT_MAX;
    for (size_t i = 0; i < m_Count; i++)
        hi = std::max(hi, At(i));
    return hi;
}

float Series::Average() const {
    return m_Count ? static_cast<float>(m_Sum / m_Count) : 0.0f;
}

// ---- Formatters --------------------------------------------------------------

namespace Format {

    static void Scaled(float value, wchar_t* buffer, size_t cch, const wchar_t* suffix) {
        static const wchar_t* k_Units[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
        double v = value < 0 ? 0 : value;
        int unit = 0;
        while (v >= 1024.0 && unit < static_cast<int>(_countof(k_Units)) - 1) {
            v /= 1024.0;
            unit++;
        }
        const wchar_t* fmt = (unit == 0 || v >= 100.0) ? L"%.0f %s%s" : L"%.1f %s%s";
        swprintf_s(buffer, cch, fmt, v, k_Units[unit], suffix);
    }

    void Percent(float value, wchar_t* buffer, size_t cch) {
        swprintf_s(buffer, cch, L"%.0f%%", value);
    }

    void Bytes(float value, wchar_t* buffer, size_t cch) {
        Scaled(value, buffer, cch, L"");
    }

    void BytesPerSec(float value, wchar_t* buffer, size_t cch) {
        Scaled(value, buffer, cch, L"/s");
    }

    void Number(float value, wchar_t* buffer, size_t cch) {
        const float a = std::fabs(value);
        if (a >= 100.0f || a == 0.0f)
            swprintf_s(buffer, cch, L"%.0f", value);
        else if (a >= 1.0f)
            swprintf_s(buffer, cch, L"%.1f", value);
        else
            swprintf_s(buffer, cch, L"%.2f", value);
    }

} // namespace Format

float NiceCeil(float value) {
    if (value <= 0.0f) return 1.0f;

    const float exponent = std::floor(std::log10(value));
    const float pow10    = std::pow(10.0f, exponent);
    const float frac     = value / pow10;

    float nice;
    if (frac <= 1.0f)      nice = 1.0f;
    else if (frac <= 2.0f) nice = 2.0f;
    else if (frac <= 5.0f) nice = 5.0f;
    else                   nice = 10.0f;

    return nice * pow10;
}

// ---- GraphData ---------------------------------------------------------------

SeriesId GraphData::AddSeries(std::wstring name, const SeriesStyle& style) {
    const SeriesId id = m_NextSeriesId++;
    m_Series.emplace_back(id, std::move(name), style, m_Capacity);
    return id;
}

bool GraphData::RemoveSeries(SeriesId id) {
    auto it = std::find_if(m_Series.begin(), m_Series.end(),
                           [id](const Series& s) { return s.Id() == id; });
    if (it == m_Series.end()) return false;
    m_Series.erase(it);
    return true;
}

Series* GraphData::GetSeries(SeriesId id) {
    auto it = std::find_if(m_Series.begin(), m_Series.end(),
                           [id](const Series& s) { return s.Id() == id; });
    return it == m_Series.end() ? nullptr : &*it;
}

const Series* GraphData::GetSeries(SeriesId id) const {
    return const_cast<GraphData*>(this)->GetSeries(id);
}

void GraphData::PushSample(SeriesId id, float value) {
    if (Series* s = GetSeries(id))
        s->Push(value);
}

void GraphData::PushSamples(const float* values, size_t count) {
    for (size_t i = 0; i < m_Series.size(); i++)
        m_Series[i].Push((values && i < count) ? values[i] : 0.0f);
}

void GraphData::Clear() {
    for (auto& s : m_Series)
        s.Clear();
}

void GraphData::RemoveAllSeries() {
    m_Series.clear();
}

void GraphData::SetCapacity(size_t samples) {
    m_Capacity = std::max(samples, k_MinCapacity);
    for (auto& s : m_Series)
        s.SetCapacity(m_Capacity);
}

float GraphData::MaxValue(float fallback) const {
    bool any = false;
    float hi = -FLT_MAX;
    for (const auto& s : m_Series) {
        if (!s.Style().Visible || s.Count() == 0) continue;
        hi  = std::max(hi, s.Max());
        any = true;
    }
    return any ? hi : fallback;
}

float GraphData::MinValue(float fallback) const {
    bool any = false;
    float lo = FLT_MAX;
    for (const auto& s : m_Series) {
        if (!s.Style().Visible || s.Count() == 0) continue;
        lo  = std::min(lo, s.Min());
        any = true;
    }
    return any ? lo : fallback;
}

size_t GraphData::CommonCount() const {
    bool any = false;
    size_t n = SIZE_MAX;
    for (const auto& s : m_Series) {
        if (!s.Style().Visible) continue;
        n   = std::min(n, s.Count());
        any = true;
    }
    return any ? n : 0;
}

float GraphData::MaxStackedValue(float fallback) const {
    const size_t n = CommonCount();
    if (n == 0) return fallback;

    float hi = -FLT_MAX;
    for (size_t k = 0; k < n; k++) {
        float sum = 0.0f;
        for (const auto& s : m_Series) {
            if (s.Style().Visible)
                sum += s.AtFromEnd(k);
        }
        hi = std::max(hi, sum);
    }
    return hi;
}

} // namespace GraphCtrl
