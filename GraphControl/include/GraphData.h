#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include <cstdint>

namespace GraphCtrl {

using SeriesId = uint32_t;
constexpr SeriesId InvalidSeries = UINT32_MAX;

constexpr size_t k_MinCapacity     = 2;
constexpr size_t k_DefaultCapacity = 60;   // 60 samples = 60 seconds at 1 Hz

// Push this when a source could not produce a reading. The line and fill break
// across it, and it is left out of Min/Max/Average — a graph that admits a gap
// beats one that draws a fake dip to zero.
inline constexpr float MissingSample = std::numeric_limits<float>::quiet_NaN();
inline bool IsMissing(float value) { return std::isnan(value); }

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
    float  At(size_t index) const;     // 0 = oldest retained sample
    float  AtFromEnd(size_t k) const;  // 0 = newest sample; aligns series of unequal length
    float  Last() const;

    // Samples actually present; missing ones do not count.
    size_t ValidCount() const { return m_ValidCount; }

    // All three ignore missing samples; zero when nothing valid is retained.
    float  Min() const;
    float  Max() const;
    float  Average() const;

private:
    std::vector<float> m_Samples;    // ring buffer; size() is the capacity
    size_t             m_Head       = 0;  // index of the next write
    size_t             m_Count      = 0;
    size_t             m_ValidCount = 0;  // of those, how many are not missing
    double             m_Sum        = 0;  // running, for O(1) Average
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

// All of them write the user's decimal separator, not a hard-coded point.
// Storage scales by 1024, network rates by 1000, as those fields conventionally do.
namespace Format {
    void Percent(float value, wchar_t* buffer, size_t cch);       // "85%"
    void Bytes(float value, wchar_t* buffer, size_t cch);         // "8.0 GB"
    void BytesPerSec(float value, wchar_t* buffer, size_t cch);   // "1.2 MB/s"
    void Bits(float value, wchar_t* buffer, size_t cch);          // "1.5 Mb"
    void BitsPerSec(float value, wchar_t* buffer, size_t cch);    // "1.5 Mbps"
    void Number(float value, wchar_t* buffer, size_t cch);        // "1234"
}

// Rounds up to the next 1/2/5 x 10^n, so auto-scaled axis labels stay readable.
float NiceCeil(float value);

// N series over one shared time base: PushSamples advances every series by one
// slot, which is what keeps overlaid series (user/kernel, send/receive) aligned.
class GraphData {
public:
    SeriesId AddSeries(std::wstring name, const SeriesStyle& style = {});
    bool     RemoveSeries(SeriesId id);

    Series*       GetSeries(SeriesId id);
    const Series* GetSeries(SeriesId id) const;

    const std::vector<Series>& AllSeries() const { return m_Series; }
    size_t SeriesCount() const { return m_Series.size(); }

    void PushSample(SeriesId id, float value);
    // One value per series, in AddSeries order. Series the caller did not supply
    // get MissingSample, so a short read leaves a gap rather than a fake zero.
    void PushSamples(const float* values, size_t count);

    // Same, but stamped with a tick you supply rather than the current one --
    // used to back-fill intervals that went by while nothing was running.
    void PushSamplesAt(const float* values, size_t count, uint64_t tick);

    // The shared time base: every push advances it one slot and stamps it with
    // GetTickCount64. Zero means that slot predates any stamp we hold.
    uint64_t TimestampFromEnd(size_t k) const;   // 0 = newest
    uint64_t NewestTimestamp() const;
    uint64_t OldestTimestamp() const;

    void Clear();             // drops the samples, keeps the series
    void RemoveAllSeries();

    void   SetCapacity(size_t samples);
    size_t Capacity() const { return m_Capacity; }

    // Across visible series, ignoring missing samples; the fallback when empty.
    float MaxValue(float fallback = 0.0f) const;
    float MinValue(float fallback = 0.0f) const;

    // Samples every visible series holds, counted back from the newest. Stacked
    // drawing and hit testing use this so a shorter series cannot misalign them.
    size_t CommonCount() const;

    // Largest sum across visible series at any one time slot; the auto-scale
    // peak for a stacked graph. Missing samples contribute nothing.
    float MaxStackedValue(float fallback = 0.0f) const;

    // Bumped by every mutation. The renderer keys its geometry cache off this,
    // so a host that pokes a Series directly should call Touch() afterwards.
    uint64_t Version() const { return m_Version; }
    void     Touch() { m_Version++; }

private:
    void Stamp(uint64_t tick);

    std::vector<Series> m_Series;
    size_t              m_Capacity     = k_DefaultCapacity;
    SeriesId            m_NextSeriesId = 0;
    uint64_t            m_Version      = 1;

    // Ring of sample times, one slot per push, mirroring the series rings.
    std::vector<uint64_t> m_Stamps;
    size_t                m_StampHead  = 0;
    size_t                m_StampCount = 0;
};

} // namespace GraphCtrl
