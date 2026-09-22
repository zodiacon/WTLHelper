#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace GraphCtrl {

using SeriesId = uint32_t;
constexpr SeriesId InvalidSeries = UINT32_MAX;

constexpr size_t k_MinCapacity     = 2;
constexpr size_t k_DefaultCapacity = 60;   // 60 samples = 60 seconds at 1 Hz

struct SeriesStyle {
    COLORREF LineColor   = RGB(17, 125, 187);
    COLORREF FillColor   = RGB(105, 185, 235);
    float    FillOpacity = 0.35f;   // applied to the area gradient; 0 = line only
    float    LineWidth   = 1.5f;
    bool     Dashed      = false;
    bool     Visible     = true;
};

// A fixed-capacity ring of samples. Push never allocates once the capacity is
// set, so a live graph runs without touching the heap.
class Series {
public:
    explicit Series(SeriesId id = InvalidSeries, std::wstring name = {},
                    const SeriesStyle& style = {}, size_t capacity = k_DefaultCapacity);

    SeriesId Id() const { return m_Id; }

    const std::wstring& Name() const     { return m_Name; }
    void SetName(std::wstring name)      { m_Name = std::move(name); }

    const SeriesStyle& Style() const     { return m_Style; }
    void SetStyle(const SeriesStyle& s)  { m_Style = s; }

    // Keeps the most recent samples when shrinking.
    void   SetCapacity(size_t capacity);
    size_t Capacity() const { return m_Samples.size(); }

    void   Push(float value);
    void   Clear();

    size_t Count() const { return m_Count; }
    float  At(size_t index) const;   // 0 = oldest retained sample
    float  Last() const;

    float  Min() const;
    float  Max() const;
    float  Average() const;

private:
    std::vector<float> m_Samples;    // ring buffer; size() is the capacity
    size_t             m_Head  = 0;  // index of the next write
    size_t             m_Count = 0;
    double             m_Sum   = 0;  // running, for O(1) Average
    SeriesId           m_Id;
    std::wstring       m_Name;
    SeriesStyle        m_Style;
};

// Y axis range. AutoScale recomputes Max from the data after every push.
struct AxisRange {
    float Min       = 0.0f;
    float Max       = 100.0f;
    bool  AutoScale = false;
    float Headroom  = 1.1f;    // pad the observed peak by this factor
    bool  RoundNice = true;    // snap the padded peak to 1/2/5 x 10^n
};

// Formats a Y-axis value into buffer. See the Format namespace for built-ins.
using ValueFormatFn = std::function<void(float value, wchar_t* buffer, size_t cch)>;

namespace Format {
    void Percent(float value, wchar_t* buffer, size_t cch);       // "85%"
    void Bytes(float value, wchar_t* buffer, size_t cch);         // "8.0 GB"
    void BytesPerSec(float value, wchar_t* buffer, size_t cch);   // "1.2 MB/s"
    void Number(float value, wchar_t* buffer, size_t cch);        // "1234"
}

// Rounds up to the next 1/2/5 x 10^n, so auto-scaled axis labels stay readable.
float NiceCeil(float value);

// N series over one shared time base: PushSamples advances every series by one
// slot, which is what keeps overlaid series (total/kernel, send/receive) aligned.
class GraphData {
public:
    SeriesId AddSeries(std::wstring name, const SeriesStyle& style = {});
    bool     RemoveSeries(SeriesId id);

    Series*       GetSeries(SeriesId id);
    const Series* GetSeries(SeriesId id) const;

    const std::vector<Series>& AllSeries() const { return m_Series; }
    size_t SeriesCount() const { return m_Series.size(); }

    void PushSample(SeriesId id, float value);
    // One value per series, in AddSeries order; missing entries are pushed as 0.
    void PushSamples(const float* values, size_t count);

    void Clear();             // drops the samples, keeps the series
    void RemoveAllSeries();

    void   SetCapacity(size_t samples);
    size_t Capacity() const { return m_Capacity; }

    // Across visible series; returns the fallback when there is no data.
    float MaxValue(float fallback = 0.0f) const;
    float MinValue(float fallback = 0.0f) const;

private:
    std::vector<Series> m_Series;
    size_t              m_Capacity     = k_DefaultCapacity;
    SeriesId            m_NextSeriesId = 0;
};

} // namespace GraphCtrl
