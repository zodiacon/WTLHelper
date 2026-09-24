#include "../include/GraphData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>

namespace GraphCtrl {

// ---- Series ------------------------------------------------------------------

Series::Series(SeriesId id, std::wstring name, const SeriesStyle& style, size_t capacity)
    : m_Id(id), m_Name(std::move(name)), m_Style(style) {
    m_Samples.resize(std::max(capacity, k_MinCapacity), MissingSample);
}

void Series::SetCapacity(size_t capacity) {
    capacity = std::max(capacity, k_MinCapacity);
    if (capacity == m_Samples.size()) return;

    // Keep the most recent min(count, capacity) samples, oldest first.
    const size_t keep = std::min(m_Count, capacity);
    std::vector<float> fresh(capacity, MissingSample);
    for (size_t i = 0; i < keep; i++)
        fresh[i] = At(m_Count - keep + i);

    m_Samples = std::move(fresh);
    m_Count   = keep;
    m_Head    = (keep == capacity) ? 0 : keep;

    m_Sum        = 0;
    m_ValidCount = 0;
    for (size_t i = 0; i < m_Count; i++) {
        if (IsMissing(m_Samples[i])) continue;
        m_Sum += m_Samples[i];
        m_ValidCount++;
    }
}

void Series::Push(float value) {
    const size_t cap = m_Samples.size();

    if (m_Count == cap) {
        const float evicted = m_Samples[m_Head];   // the oldest
        if (!IsMissing(evicted)) {
            m_Sum -= evicted;
            m_ValidCount--;
        }
    }
    else {
        m_Count++;
    }

    m_Samples[m_Head] = value;
    if (!IsMissing(value)) {
        m_Sum += value;
        m_ValidCount++;
    }
    m_Head = (m_Head + 1) % cap;
}

void Series::Clear() {
    m_Head       = 0;
    m_Count      = 0;
    m_ValidCount = 0;
    m_Sum        = 0;
    std::fill(m_Samples.begin(), m_Samples.end(), MissingSample);
}

float Series::At(size_t index) const {
    if (index >= m_Count) return MissingSample;
    const size_t cap = m_Samples.size();
    return m_Samples[(m_Head + cap - m_Count + index) % cap];
}

float Series::AtFromEnd(size_t k) const {
    return k < m_Count ? At(m_Count - 1 - k) : MissingSample;
}

float Series::Last() const {
    return m_Count ? At(m_Count - 1) : MissingSample;
}

// Scanned rather than tracked incrementally: a ring eviction can drop the
// current extreme, and the capacity is small enough that it does not matter.
float Series::Min() const {
    float lo = FLT_MAX;
    bool any = false;
    for (size_t i = 0; i < m_Count; i++) {
        const float v = At(i);
        if (IsMissing(v)) continue;
        lo  = std::min(lo, v);
        any = true;
    }
    return any ? lo : 0.0f;
}

float Series::Max() const {
    float hi = -FLT_MAX;
    bool any = false;
    for (size_t i = 0; i < m_Count; i++) {
        const float v = At(i);
        if (IsMissing(v)) continue;
        hi  = std::max(hi, v);
        any = true;
    }
    return any ? hi : 0.0f;
}

float Series::Average() const {
    return m_ValidCount ? static_cast<float>(m_Sum / m_ValidCount) : 0.0f;
}

// ---- Formatters --------------------------------------------------------------

namespace Format {

    // The user's decimal separator, read once. swprintf_s always writes a point.
    static const wchar_t* LocaleDecimal() {
        static wchar_t s_separator[8] = {};
        if (!s_separator[0]) {
            if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL,
                                s_separator, _countof(s_separator)) <= 0) {
                wcscpy_s(s_separator, L".");
            }
        }
        return s_separator;
    }

    static void Localize(wchar_t* buffer, size_t cch) {
        const wchar_t* separator = LocaleDecimal();
        if (separator[0] == L'.' && separator[1] == 0) return;   // nothing to do

        wchar_t* point = wcschr(buffer, L'.');
        if (!point) return;

        const size_t sepLen = wcslen(separator);
        const size_t len    = wcslen(buffer);
        const size_t head   = static_cast<size_t>(point - buffer);
        if (len + sepLen - 1 >= cch) return;   // would not fit; leave the point

        // Slide the tail (including the terminator) to make room, then splice.
        memmove(point + sepLen, point + 1, (len - head) * sizeof(wchar_t));
        memcpy(point, separator, sepLen * sizeof(wchar_t));
    }

    // Storage: 1024-based, "8.0 GB".
    static void ScaledBinary(float value, wchar_t* buffer, size_t cch, const wchar_t* suffix) {
        static const wchar_t* k_Units[] = { L"B", L"KB", L"MB", L"GB", L"TB", L"PB" };
        double v = value < 0 ? 0 : value;
        int unit = 0;
        while (v >= 1024.0 && unit < static_cast<int>(_countof(k_Units)) - 1) {
            v /= 1024.0;
            unit++;
        }
        const wchar_t* fmt = (unit == 0 || v >= 100.0) ? L"%.0f %s%s" : L"%.1f %s%s";
        swprintf_s(buffer, cch, fmt, v, k_Units[unit], suffix);
        Localize(buffer, cch);
    }

    // Network rates: 1000-based, "1.5 Mbps" -- the convention those fields use.
    static void ScaledDecimal(float value, wchar_t* buffer, size_t cch, const wchar_t* suffix) {
        static const wchar_t* k_Units[] = { L"", L"K", L"M", L"G", L"T", L"P" };
        double v = value < 0 ? 0 : value;
        int unit = 0;
        while (v >= 1000.0 && unit < static_cast<int>(_countof(k_Units)) - 1) {
            v /= 1000.0;
            unit++;
        }
        const wchar_t* fmt = (unit == 0 || v >= 100.0) ? L"%.0f %s%s" : L"%.1f %s%s";
        swprintf_s(buffer, cch, fmt, v, k_Units[unit], suffix);
        Localize(buffer, cch);
    }

    void Percent(float value, wchar_t* buffer, size_t cch) {
        swprintf_s(buffer, cch, L"%.0f%%", value);
    }

    void Bytes(float value, wchar_t* buffer, size_t cch) {
        ScaledBinary(value, buffer, cch, L"");
    }

    void BytesPerSec(float value, wchar_t* buffer, size_t cch) {
        ScaledBinary(value, buffer, cch, L"/s");
    }

    void Bits(float value, wchar_t* buffer, size_t cch) {
        ScaledDecimal(value, buffer, cch, L"b");
    }

    void BitsPerSec(float value, wchar_t* buffer, size_t cch) {
        ScaledDecimal(value, buffer, cch, L"bps");
    }

    void Number(float value, wchar_t* buffer, size_t cch) {
        const float a = std::fabs(value);
        if (a >= 100.0f || a == 0.0f)
            swprintf_s(buffer, cch, L"%.0f", value);
        else if (a >= 1.0f)
            swprintf_s(buffer, cch, L"%.1f", value);
        else
            swprintf_s(buffer, cch, L"%.2f", value);
        Localize(buffer, cch);
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
    Touch();
    return id;
}

bool GraphData::RemoveSeries(SeriesId id) {
    auto it = std::find_if(m_Series.begin(), m_Series.end(),
                           [id](const Series& s) { return s.Id() == id; });
    if (it == m_Series.end()) return false;
    m_Series.erase(it);
    Touch();
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
    if (Series* s = GetSeries(id)) {
        s->Push(value);
        Stamp(GetTickCount64());
        Touch();
    }
}

void GraphData::PushSamples(const float* values, size_t count) {
    PushSamplesAt(values, count, GetTickCount64());
}

void GraphData::PushSamplesAt(const float* values, size_t count, uint64_t tick) {
    for (size_t i = 0; i < m_Series.size(); i++)
        m_Series[i].Push((values && i < count) ? values[i] : MissingSample);
    Stamp(tick);
    Touch();
}

void GraphData::Stamp(uint64_t tick) {
    if (m_Stamps.size() != m_Capacity)
        m_Stamps.resize(m_Capacity, 0);

    if (m_StampCount < m_Stamps.size())
        m_StampCount++;
    m_Stamps[m_StampHead] = tick;
    m_StampHead = (m_StampHead + 1) % m_Stamps.size();
}

uint64_t GraphData::TimestampFromEnd(size_t k) const {
    if (k >= m_StampCount || m_Stamps.empty()) return 0;
    const size_t cap   = m_Stamps.size();
    const size_t index = m_StampCount - 1 - k;
    return m_Stamps[(m_StampHead + cap - m_StampCount + index) % cap];
}

uint64_t GraphData::NewestTimestamp() const {
    return TimestampFromEnd(0);
}

uint64_t GraphData::OldestTimestamp() const {
    return m_StampCount ? TimestampFromEnd(m_StampCount - 1) : 0;
}

void GraphData::Clear() {
    for (auto& s : m_Series)
        s.Clear();
    std::fill(m_Stamps.begin(), m_Stamps.end(), 0);
    m_StampHead  = 0;
    m_StampCount = 0;
    Touch();
}

void GraphData::RemoveAllSeries() {
    m_Series.clear();
    Touch();
}

void GraphData::SetCapacity(size_t samples) {
    m_Capacity = std::max(samples, k_MinCapacity);
    for (auto& s : m_Series)
        s.SetCapacity(m_Capacity);

    // Keep the most recent stamps, in step with what the series kept.
    const size_t keep = std::min(m_StampCount, m_Capacity);
    std::vector<uint64_t> fresh(m_Capacity, 0);
    for (size_t i = 0; i < keep; i++)
        fresh[i] = TimestampFromEnd(keep - 1 - i);

    m_Stamps     = std::move(fresh);
    m_StampCount = keep;
    m_StampHead  = (keep == m_Capacity) ? 0 : keep;

    Touch();
}

float GraphData::MaxValue(float fallback) const {
    bool any = false;
    float hi = -FLT_MAX;
    for (const auto& s : m_Series) {
        if (!s.Style().Visible || s.ValidCount() == 0) continue;
        hi  = std::max(hi, s.Max());
        any = true;
    }
    return any ? hi : fallback;
}

float GraphData::MinValue(float fallback) const {
    bool any = false;
    float lo = FLT_MAX;
    for (const auto& s : m_Series) {
        if (!s.Style().Visible || s.ValidCount() == 0) continue;
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
            if (!s.Style().Visible) continue;
            const float v = s.AtFromEnd(k);
            if (!IsMissing(v)) sum += v;
        }
        hi = std::max(hi, sum);
    }
    return hi > -FLT_MAX ? hi : fallback;
}

} // namespace GraphCtrl
