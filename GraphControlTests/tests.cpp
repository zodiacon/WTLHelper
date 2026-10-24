// Unit tests for the GraphControl data model.
//
// Deliberately dependency-free: a console exe that returns the number of failed
// checks, so it drops into any build without pulling in a test framework. Only
// the pure parts are covered -- the ring buffer, the scale maths, the formatters
// and GraphData. Anything that needs an HWND or a render target is not here.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <string>

#include "GraphData.h"
#include "CellGraphs.h"
#include "CellGraphsModel.h"

#include <vector>

using namespace GraphCtrl;

// ---- Tiny harness ------------------------------------------------------------

static int g_checks   = 0;
static int g_failures = 0;
static const wchar_t* g_currentTest = L"";

static void Fail(const char* expr, int line) {
    g_failures++;
    wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
}

static void Check(bool ok, const char* expr, int line) {
    g_checks++;
    if (!ok) Fail(expr, line);
}

static void CheckNear(float actual, float expected, float tolerance,
                      const char* expr, int line) {
    g_checks++;
    if (std::isnan(actual) || std::fabs(actual - expected) > tolerance) {
        g_failures++;
        wprintf(L"  FAIL  %s:%d  %S  (got %g, want %g)\n",
                g_currentTest, line, expr, actual, expected);
    }
}

static void CheckStr(const wchar_t* actual, const wchar_t* expected,
                     const char* expr, int line) {
    g_checks++;
    if (wcscmp(actual, expected) != 0) {
        g_failures++;
        wprintf(L"  FAIL  %s:%d  %S  (got \"%s\", want \"%s\")\n",
                g_currentTest, line, expr, actual, expected);
    }
}

#define CHECK(expr)                  Check((expr), #expr, __LINE__)
#define CHECK_NEAR(a, b)             CheckNear((a), (b), 0.0001f, #a " ~= " #b, __LINE__)
#define CHECK_NEAR_T(a, b, t)        CheckNear((a), (b), (t), #a " ~= " #b, __LINE__)
#define CHECK_STR(a, b)              CheckStr((a), (b), #a " == " #b, __LINE__)
#define CHECK_MISSING(expr)          Check(IsMissing(expr), "IsMissing(" #expr ")", __LINE__)

#define TEST(name)                                     \
    static void name();                                \
    static void Run_##name() {                         \
        g_currentTest = L## #name;                     \
        const int before = g_failures;                 \
        name();                                        \
        wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name); \
    }                                                  \
    static void name()

// The formatters emit the user's decimal separator, so expectations are written
// with a point and translated here. That also keeps the tests locale-agnostic.
static std::wstring Localized(const wchar_t* text) {
    wchar_t separator[8] = L".";
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL,
                        separator, _countof(separator)) <= 0) {
        wcscpy_s(separator, L".");
    }

    std::wstring out;
    for (const wchar_t* p = text; *p; p++) {
        if (*p == L'.') out += separator;
        else            out += *p;
    }
    return out;
}

// ---- Series: the ring buffer -------------------------------------------------

TEST(Series_PartiallyFilled) {
    Series s(0, L"s", {}, 5);
    CHECK(s.Capacity() == 5);
    CHECK(s.Count() == 0);
    CHECK(s.ValidCount() == 0);
    CHECK_MISSING(s.Last());

    s.Push(10.0f);
    s.Push(20.0f);
    s.Push(30.0f);

    CHECK(s.Count() == 3);
    CHECK(s.ValidCount() == 3);
    CHECK_NEAR(s.At(0), 10.0f);        // oldest
    CHECK_NEAR(s.At(2), 30.0f);        // newest
    CHECK_NEAR(s.AtFromEnd(0), 30.0f); // newest
    CHECK_NEAR(s.AtFromEnd(2), 10.0f);
    CHECK_NEAR(s.Last(), 30.0f);
    CHECK_NEAR(s.Average(), 20.0f);
    CHECK_NEAR(s.Min(), 10.0f);
    CHECK_NEAR(s.Max(), 30.0f);

    // Reading past what is retained is a gap, not a zero.
    CHECK_MISSING(s.At(3));
    CHECK_MISSING(s.AtFromEnd(3));
}

TEST(Series_WrapsAndDropsOldest) {
    Series s(0, L"s", {}, 3);
    for (int i = 1; i <= 5; i++)
        s.Push(static_cast<float>(i));   // 1,2,3,4,5 into a ring of 3

    CHECK(s.Count() == 3);
    CHECK_NEAR(s.At(0), 3.0f);
    CHECK_NEAR(s.At(1), 4.0f);
    CHECK_NEAR(s.At(2), 5.0f);
    CHECK_NEAR(s.Last(), 5.0f);
    CHECK_NEAR(s.Average(), 4.0f);      // the evicted 1 and 2 are gone from the sum
    CHECK_NEAR(s.Min(), 3.0f);
    CHECK_NEAR(s.Max(), 5.0f);
}

TEST(Series_CapacityIsClamped) {
    Series s(0, L"s", {}, 0);
    CHECK(s.Capacity() == k_MinCapacity);

    s.SetCapacity(1);
    CHECK(s.Capacity() == k_MinCapacity);
}

TEST(Series_ShrinkKeepsNewest) {
    Series s(0, L"s", {}, 10);
    for (int i = 1; i <= 6; i++)
        s.Push(static_cast<float>(i));

    s.SetCapacity(3);
    CHECK(s.Capacity() == 3);
    CHECK(s.Count() == 3);
    CHECK_NEAR(s.At(0), 4.0f);
    CHECK_NEAR(s.At(2), 6.0f);
    CHECK_NEAR(s.Average(), 5.0f);      // the running sum was rebuilt, not stale

    // The ring still works after the rebuild.
    s.Push(7.0f);
    CHECK(s.Count() == 3);
    CHECK_NEAR(s.At(0), 5.0f);
    CHECK_NEAR(s.Last(), 7.0f);
}

TEST(Series_GrowKeepsEverything) {
    Series s(0, L"s", {}, 3);
    for (int i = 1; i <= 3; i++)
        s.Push(static_cast<float>(i));

    s.SetCapacity(8);
    CHECK(s.Capacity() == 8);
    CHECK(s.Count() == 3);
    CHECK_NEAR(s.At(0), 1.0f);
    CHECK_NEAR(s.Last(), 3.0f);
    CHECK_NEAR(s.Average(), 2.0f);

    s.Push(4.0f);
    CHECK(s.Count() == 4);
    CHECK_NEAR(s.Last(), 4.0f);
}

TEST(Series_Clear) {
    Series s(0, L"s", {}, 4);
    s.Push(1.0f);
    s.Push(2.0f);
    s.Clear();

    CHECK(s.Count() == 0);
    CHECK(s.ValidCount() == 0);
    CHECK_NEAR(s.Average(), 0.0f);
    CHECK_MISSING(s.Last());

    s.Push(9.0f);
    CHECK(s.Count() == 1);
    CHECK_NEAR(s.Last(), 9.0f);
    CHECK_NEAR(s.Average(), 9.0f);
}

// ---- Series: missing samples -------------------------------------------------

TEST(Series_MissingIgnoredByStats) {
    Series s(0, L"s", {}, 5);
    s.Push(10.0f);
    s.Push(MissingSample);
    s.Push(30.0f);

    CHECK(s.Count() == 3);        // it still occupies a slot
    CHECK(s.ValidCount() == 2);   // but is not a reading
    CHECK_MISSING(s.At(1));
    CHECK_NEAR(s.Average(), 20.0f);
    CHECK_NEAR(s.Min(), 10.0f);
    CHECK_NEAR(s.Max(), 30.0f);
}

TEST(Series_EvictingMissingKeepsSumIntact) {
    Series s(0, L"s", {}, 3);
    s.Push(MissingSample);
    s.Push(10.0f);
    s.Push(20.0f);
    CHECK_NEAR(s.Average(), 15.0f);

    // Pushing again evicts the missing sample; the sum must not change.
    s.Push(30.0f);
    CHECK(s.Count() == 3);
    CHECK(s.ValidCount() == 3);
    CHECK_NEAR(s.Average(), 20.0f);

    // Now evict a real value.
    s.Push(40.0f);
    CHECK(s.ValidCount() == 3);
    CHECK_NEAR(s.Average(), 30.0f);
}

TEST(Series_AllMissing) {
    Series s(0, L"s", {}, 3);
    s.Push(MissingSample);
    s.Push(MissingSample);

    CHECK(s.Count() == 2);
    CHECK(s.ValidCount() == 0);
    CHECK_NEAR(s.Average(), 0.0f);
    CHECK_NEAR(s.Min(), 0.0f);
    CHECK_NEAR(s.Max(), 0.0f);
    CHECK_MISSING(s.Last());
}

TEST(Series_ShrinkRebuildsAcrossMissing) {
    Series s(0, L"s", {}, 8);
    s.Push(1.0f);
    s.Push(MissingSample);
    s.Push(3.0f);
    s.Push(MissingSample);
    s.Push(5.0f);

    s.SetCapacity(3);             // keeps 3, MissingSample, 5
    CHECK(s.Count() == 3);
    CHECK(s.ValidCount() == 2);
    CHECK_NEAR(s.Average(), 4.0f);
    CHECK_MISSING(s.At(1));
}

// ---- Scale maths -------------------------------------------------------------

TEST(NiceCeil_Values) {
    CHECK_NEAR(NiceCeil(0.0f), 1.0f);
    CHECK_NEAR(NiceCeil(-5.0f), 1.0f);
    CHECK_NEAR(NiceCeil(1.0f), 1.0f);
    CHECK_NEAR(NiceCeil(1.5f), 2.0f);
    CHECK_NEAR(NiceCeil(3.0f), 5.0f);
    CHECK_NEAR(NiceCeil(7.0f), 10.0f);
    CHECK_NEAR(NiceCeil(12.0f), 20.0f);
    CHECK_NEAR(NiceCeil(55.0f), 100.0f);
    CHECK_NEAR(NiceCeil(0.03f), 0.05f);
    CHECK_NEAR_T(NiceCeil(124.3e9f), 200.0e9f, 1.0e6f);
}

// ---- Formatters --------------------------------------------------------------

TEST(Format_Percent) {
    wchar_t buf[64];
    Format::Percent(0.0f, buf, _countof(buf));
    CHECK_STR(buf, L"0%");
    Format::Percent(85.4f, buf, _countof(buf));
    CHECK_STR(buf, L"85%");
}

TEST(Format_BytesScalesBy1024) {
    wchar_t buf[64];
    Format::Bytes(0.0f, buf, _countof(buf));
    CHECK_STR(buf, L"0 B");

    Format::Bytes(512.0f, buf, _countof(buf));
    CHECK_STR(buf, L"512 B");

    Format::Bytes(1024.0f, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"1.0 KB").c_str());

    Format::Bytes(8.0f * 1024 * 1024 * 1024, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"8.0 GB").c_str());

    Format::BytesPerSec(1024.0f * 1024, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"1.0 MB/s").c_str());
}

TEST(Format_BitsScaleBy1000) {
    wchar_t buf[64];
    Format::BitsPerSec(999.0f, buf, _countof(buf));
    CHECK_STR(buf, L"999 bps");

    Format::BitsPerSec(1500000.0f, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"1.5 Mbps").c_str());

    Format::Bits(2.0e9f, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"2.0 Gb").c_str());
}

TEST(Format_Number) {
    wchar_t buf[64];
    Format::Number(1234.0f, buf, _countof(buf));
    CHECK_STR(buf, L"1234");
    Format::Number(0.0f, buf, _countof(buf));
    CHECK_STR(buf, L"0");
    Format::Number(2.5f, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"2.5").c_str());
    Format::Number(0.25f, buf, _countof(buf));
    CHECK_STR(buf, Localized(L"0.25").c_str());
}

TEST(Format_UsesLocaleSeparator) {
    wchar_t expected[8] = L".";
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, expected, _countof(expected));

    wchar_t buf[64];
    Format::Number(2.5f, buf, _countof(buf));
    CHECK(wcsstr(buf, expected) != nullptr);
}

// ---- GraphData ---------------------------------------------------------------

TEST(GraphData_SeriesLifecycle) {
    GraphData d;
    const SeriesId a = d.AddSeries(L"a");
    const SeriesId b = d.AddSeries(L"b");

    CHECK(a != b);
    CHECK(d.SeriesCount() == 2);
    CHECK(d.GetSeries(a) != nullptr);
    CHECK(d.GetSeries(InvalidSeries) == nullptr);

    CHECK(d.RemoveSeries(a));
    CHECK(!d.RemoveSeries(a));           // already gone
    CHECK(d.SeriesCount() == 1);
    CHECK(d.GetSeries(a) == nullptr);
    CHECK(d.GetSeries(b) != nullptr);    // the survivor keeps its id after the shift

    d.RemoveAllSeries();
    CHECK(d.SeriesCount() == 0);
}

TEST(GraphData_CapacityPropagates) {
    GraphData d;
    const SeriesId a = d.AddSeries(L"a");
    d.SetCapacity(7);
    CHECK(d.Capacity() == 7);
    CHECK(d.GetSeries(a)->Capacity() == 7);

    // A series added later inherits it.
    const SeriesId b = d.AddSeries(L"b");
    CHECK(d.GetSeries(b)->Capacity() == 7);

    d.SetCapacity(0);
    CHECK(d.Capacity() == k_MinCapacity);
}

TEST(GraphData_PushSamplesKeepsSeriesAligned) {
    GraphData d;
    const SeriesId a = d.AddSeries(L"a");
    const SeriesId b = d.AddSeries(L"b");

    const float first[]  = { 1.0f, 10.0f };
    const float second[] = { 2.0f, 20.0f };
    d.PushSamples(first, 2);
    d.PushSamples(second, 2);

    CHECK(d.GetSeries(a)->Count() == 2);
    CHECK(d.GetSeries(b)->Count() == 2);
    CHECK_NEAR(d.GetSeries(a)->Last(), 2.0f);
    CHECK_NEAR(d.GetSeries(b)->Last(), 20.0f);
}

TEST(GraphData_ShortPushLeavesAGap) {
    GraphData d;
    const SeriesId a = d.AddSeries(L"a");
    const SeriesId b = d.AddSeries(L"b");

    const float only_a[] = { 5.0f };
    d.PushSamples(only_a, 1);

    // b advanced too, so the time base stays aligned...
    CHECK(d.GetSeries(b)->Count() == 1);
    // ...but with a gap rather than a fabricated zero.
    CHECK_MISSING(d.GetSeries(b)->Last());
    CHECK(d.GetSeries(b)->ValidCount() == 0);
    CHECK_NEAR(d.GetSeries(a)->Last(), 5.0f);

    d.PushSamples(nullptr, 0);
    CHECK_MISSING(d.GetSeries(a)->Last());
}

TEST(GraphData_ExtremesSkipHiddenAndMissing) {
    GraphData d;
    d.AddSeries(L"a");
    const SeriesId b = d.AddSeries(L"b");

    const float s1[] = { 5.0f, 100.0f };
    const float s2[] = { 7.0f, 200.0f };
    d.PushSamples(s1, 2);
    d.PushSamples(s2, 2);

    CHECK_NEAR(d.MaxValue(), 200.0f);
    CHECK_NEAR(d.MinValue(), 5.0f);

    // Hiding b takes it out of the reckoning.
    SeriesStyle hidden = d.GetSeries(b)->Style();
    hidden.Visible = false;
    d.GetSeries(b)->SetStyle(hidden);

    CHECK_NEAR(d.MaxValue(), 7.0f);
    CHECK_NEAR(d.MinValue(), 5.0f);

    // With no data at all the fallback is returned.
    GraphData empty;
    CHECK_NEAR(empty.MaxValue(42.0f), 42.0f);
    CHECK_NEAR(empty.MinValue(-1.0f), -1.0f);
}

TEST(GraphData_CommonCountIsTheShortestVisible) {
    GraphData d;
    const SeriesId a = d.AddSeries(L"a");
    const SeriesId b = d.AddSeries(L"b");

    d.GetSeries(a)->Push(1.0f);
    d.GetSeries(a)->Push(2.0f);
    d.GetSeries(b)->Push(10.0f);

    CHECK(d.CommonCount() == 1);

    // A hidden short series must not hold the others back.
    SeriesStyle hidden = d.GetSeries(b)->Style();
    hidden.Visible = false;
    d.GetSeries(b)->SetStyle(hidden);
    CHECK(d.CommonCount() == 2);

    GraphData empty;
    CHECK(empty.CommonCount() == 0);
}

TEST(GraphData_MaxStackedSumsPerSlot) {
    GraphData d;
    d.AddSeries(L"a");
    d.AddSeries(L"b");

    const float s1[] = { 1.0f, 10.0f };
    const float s2[] = { 2.0f, 20.0f };
    const float s3[] = { 3.0f, 30.0f };
    d.PushSamples(s1, 2);
    d.PushSamples(s2, 2);
    d.PushSamples(s3, 2);

    CHECK_NEAR(d.MaxStackedValue(), 33.0f);   // the newest slot
    CHECK(d.MaxValue() > 29.9f);              // overlaid, the tallest single value

    // A gap in the newest slot drops that slot's total, not the whole series.
    GraphData g;
    g.AddSeries(L"a");
    g.AddSeries(L"b");
    const float p1[] = { 1.0f, 10.0f };
    const float p2[] = { 2.0f, 20.0f };
    const float p3[] = { 3.0f, MissingSample };
    g.PushSamples(p1, 2);
    g.PushSamples(p2, 2);
    g.PushSamples(p3, 2);

    CHECK_NEAR(g.MaxStackedValue(), 22.0f);
}

TEST(GraphData_VersionTracksMutations) {
    GraphData d;
    const uint64_t v0 = d.Version();

    const SeriesId a = d.AddSeries(L"a");
    const uint64_t v1 = d.Version();
    CHECK(v1 != v0);

    d.PushSample(a, 1.0f);
    const uint64_t v2 = d.Version();
    CHECK(v2 != v1);

    d.SetCapacity(9);
    CHECK(d.Version() != v2);

    const uint64_t v3 = d.Version();
    d.Touch();
    CHECK(d.Version() != v3);
}

// ---- GraphData: the shared time base -----------------------------------------

TEST(TimeBase_StampsEveryPush) {
    GraphData d;
    d.AddSeries(L"a");

    CHECK(d.NewestTimestamp() == 0);      // nothing pushed yet
    CHECK(d.TimestampFromEnd(0) == 0);

    const uint64_t before = GetTickCount64();
    const float v[] = { 1.0f };
    d.PushSamples(v, 1);
    const uint64_t after = GetTickCount64();

    const uint64_t stamp = d.NewestTimestamp();
    CHECK(stamp >= before && stamp <= after);
    CHECK(d.TimestampFromEnd(0) == stamp);
    CHECK(d.OldestTimestamp() == stamp);  // only one slot so far
}

TEST(TimeBase_ExplicitTicksAreKeptInOrder) {
    GraphData d;
    d.AddSeries(L"a");

    const float v[] = { 1.0f };
    d.PushSamplesAt(v, 1, 1000);
    d.PushSamplesAt(v, 1, 1250);
    d.PushSamplesAt(v, 1, 1500);

    CHECK(d.TimestampFromEnd(0) == 1500);   // newest
    CHECK(d.TimestampFromEnd(1) == 1250);
    CHECK(d.TimestampFromEnd(2) == 1000);
    CHECK(d.TimestampFromEnd(3) == 0);      // beyond what is held
    CHECK(d.NewestTimestamp() == 1500);
    CHECK(d.OldestTimestamp() == 1000);
}

TEST(TimeBase_WrapsWithTheSamples) {
    GraphData d;
    d.SetCapacity(3);
    d.AddSeries(L"a");

    const float v[] = { 1.0f };
    for (uint64_t t = 1; t <= 5; t++)
        d.PushSamplesAt(v, 1, t * 100);

    CHECK(d.NewestTimestamp() == 500);
    CHECK(d.OldestTimestamp() == 300);      // 100 and 200 aged out
    CHECK(d.TimestampFromEnd(2) == 300);
    CHECK(d.TimestampFromEnd(3) == 0);
}

TEST(TimeBase_ShrinkKeepsStampsAlignedWithSamples) {
    GraphData d;
    d.SetCapacity(10);
    const SeriesId a = d.AddSeries(L"a");

    for (uint64_t i = 1; i <= 6; i++) {
        const float v = static_cast<float>(i);
        d.PushSamplesAt(&v, 1, i * 100);
    }

    d.SetCapacity(3);

    // The newest three of each must still line up: value i has stamp i*100.
    const Series* s = d.GetSeries(a);
    CHECK(s->Count() == 3);
    CHECK_NEAR(s->AtFromEnd(0), 6.0f);
    CHECK(d.TimestampFromEnd(0) == 600);
    CHECK_NEAR(s->AtFromEnd(1), 5.0f);
    CHECK(d.TimestampFromEnd(1) == 500);
    CHECK_NEAR(s->AtFromEnd(2), 4.0f);
    CHECK(d.TimestampFromEnd(2) == 400);
    CHECK(d.OldestTimestamp() == 400);
}

TEST(TimeBase_ClearResetsIt) {
    GraphData d;
    d.AddSeries(L"a");
    const float v[] = { 1.0f };
    d.PushSamplesAt(v, 1, 900);

    d.Clear();
    CHECK(d.NewestTimestamp() == 0);
    CHECK(d.OldestTimestamp() == 0);
    CHECK(d.TimestampFromEnd(0) == 0);
}

// ---- Entry point -------------------------------------------------------------

// ---- Cell graphs --------------------------------------------------------------

using namespace GraphCtrl::Cells;

static Series MakeSeries(std::initializer_list<float> values, size_t capacity = 10) {
    Series s(0, L"s", {}, capacity);
    for (float v : values)
        s.Push(v);
    return s;
}

struct Buffer {
    std::vector<uint32_t> Bits;
    Pixels Px;
    Buffer(int w, int h, uint32_t fill = 0) : Bits((size_t)w * h, fill) { Px = { Bits.data(), w, h }; }
};

TEST(Cells_ScaleRangeFixedAndAuto) {
    auto s = MakeSeries({ 10, 50, 30 });
    auto fixed = ScaleRange(s, 10, 0, 100, false, 0);
    CHECK_NEAR(fixed.Min, 0);
    CHECK_NEAR(fixed.Max, 100);
    // auto: from min (or lower) to the highest sample
    auto r = ScaleRange(s, 10, 0, 100, true, 0);
    CHECK_NEAR(r.Min, 0);
    CHECK_NEAR(r.Max, 50);
    // a minimum span keeps a small line from filling the cell
    r = ScaleRange(MakeSeries({ 1, 2 }), 10, 0, 100, true, 10);
    CHECK_NEAR(r.Max, 10);
    // a sample below min lowers it
    r = ScaleRange(MakeSeries({ -5, 3 }), 10, 0, 100, true, 0);
    CHECK_NEAR(r.Min, -5);
    CHECK_NEAR(r.Max, 3);
    // only the slots shown count, and missing ones don't
    r = ScaleRange(MakeSeries({ 90, 5, MissingSample, 7 }), 3, 0, 100, true, 0);
    CHECK_NEAR(r.Max, 7);
    // nothing: a span of one
    r = ScaleRange(MakeSeries({}), 10, 0, 100, true, 0);
    CHECK_NEAR(r.Min, 0);
    CHECK_NEAR(r.Max, 1);
}

TEST(Cells_ValueToY) {
    RectF area{ 0, 10, 100, 30 };
    CHECK_NEAR(ValueToY(0, { 0, 100 }, area), 30);
    CHECK_NEAR(ValueToY(100, { 0, 100 }, area), 10);
    CHECK_NEAR(ValueToY(50, { 0, 100 }, area), 20);
    CHECK_NEAR(ValueToY(150, { 0, 100 }, area), 10);    // clamped
    CHECK_NEAR(ValueToY(-5, { 0, 100 }, area), 30);
    CHECK_NEAR(ValueToY(5, { 3, 3 }, area), 30);        // no span
}

TEST(Cells_SparkRunsGrowFromTheRight) {
    RectF area{ 0, 0, 90, 10 };
    auto runs = SparkRuns(MakeSeries({ 0, 100 }), 10, { 0, 100 }, area);   // 10 slots: 10 px apart
    CHECK(runs.size() == 1 && runs[0].size() == 2);
    CHECK_NEAR(runs[0][0].X, 80);    // the older sample one slot left of the edge
    CHECK_NEAR(runs[0][0].Y, 10);
    CHECK_NEAR(runs[0][1].X, 90);    // the newest at the right edge
    CHECK_NEAR(runs[0][1].Y, 0);
    // more samples than slots: the oldest shown is at the left edge
    auto full = SparkRuns(MakeSeries({ 1, 2, 3, 4, 5 }, 5), 3, { 0, 10 }, area);
    CHECK(full.size() == 1 && full[0].size() == 3);
    CHECK_NEAR(full[0][0].X, 0);
    CHECK_NEAR(full[0][0].Y, ValueToY(3, { 0, 10 }, area));
    // a missing sample splits the line; a lone sample is a run of one
    auto gaps = SparkRuns(MakeSeries({ 1, MissingSample, 2, 3, MissingSample, 4 }), 6, { 0, 10 }, area);
    CHECK(gaps.size() == 3);
    CHECK(gaps[0].size() == 1 && gaps[1].size() == 2 && gaps[2].size() == 1);
    CHECK_NEAR(gaps[2][0].X, 90);
    // one slot: at the right edge
    auto one = SparkRuns(MakeSeries({ 5 }), 1, { 0, 10 }, area);
    CHECK(one.size() == 1 && one[0].size() == 1);
    CHECK_NEAR(one[0][0].X, 90);
    CHECK(SparkRuns(MakeSeries({ 5 }), 0, { 0, 10 }, area).empty());
}

TEST(Cells_ColumnBars) {
    RectF area{ 0, 0, 40, 20 };
    auto bars = ColumnBars(MakeSeries({ 50, 0, 100, MissingSample }), 4, { 0, 100 }, area, 2);
    // 4 slots of 10 px; the newest (missing) and the zero get no bar
    CHECK(bars.size() == 2);
    CHECK(bars[0].Slot == 1);   // 100, the second newest
    CHECK_NEAR(bars[0].Rect.Left, 21);
    CHECK_NEAR(bars[0].Rect.Right, 29);
    CHECK_NEAR(bars[0].Rect.Top, 0);
    CHECK_NEAR(bars[0].Rect.Bottom, 20);
    CHECK(bars[1].Slot == 3);   // 50, the oldest
    CHECK_NEAR(bars[1].Rect.Left, 1);
    CHECK_NEAR(bars[1].Rect.Top, 10);
    // narrow slots: no gap
    auto narrow = ColumnBars(MakeSeries({ 50 }), 10, { 0, 100 }, { 0, 0, 40, 20 }, 2);
    CHECK(narrow.size() == 1);
    CHECK_NEAR(narrow[0].Rect.Left, 36);
    CHECK_NEAR(narrow[0].Rect.Right, 40);
}

TEST(Cells_MeterAndThresholds) {
    CHECK_NEAR(MeterFraction(25, 0, 100), 0.25f);
    CHECK_NEAR(MeterFraction(150, 0, 100), 1);
    CHECK_NEAR(MeterFraction(-1, 0, 100), 0);
    CHECK_NEAR(MeterFraction(MissingSample, 0, 100), 0);
    CHECK_NEAR(MeterFraction(5, 10, 10), 0);
    CellThreshold t[] = { { 0, RGB(0, 200, 0) }, { 70, RGB(240, 160, 0) }, { 90, RGB(220, 0, 0) } };
    CHECK(ThresholdColor(10, t, 3, 1) == RGB(0, 200, 0));
    CHECK(ThresholdColor(70, t, 3, 1) == RGB(240, 160, 0));
    CHECK(ThresholdColor(95, t, 3, 1) == RGB(220, 0, 0));
    CHECK(ThresholdColor(-5, t, 3, 1) == 1);            // below every threshold: the fallback
    CHECK(ThresholdColor(MissingSample, t, 3, 1) == 1);
    CHECK(ThresholdColor(50, nullptr, 0, 7) == 7);
}

TEST(Cells_StackSegments) {
    RectF area{ 10, 0, 110, 8 };
    float values[] = { 20, MissingSample, 30, -5 };
    auto segs = StackSegments(values, 4, 100, area);
    CHECK(segs.size() == 4);
    CHECK_NEAR(segs[0].Left, 10);
    CHECK_NEAR(segs[0].Right, 30);
    CHECK_NEAR(segs[1].Width(), 0);                     // missing: no room
    CHECK_NEAR(segs[2].Left, 30);
    CHECK_NEAR(segs[2].Right, 60);
    CHECK_NEAR(segs[3].Width(), 0);                     // negative: no room
    // no total: their sum fills the area
    auto whole = StackSegments(values, 4, 0, area);
    CHECK_NEAR(whole[2].Right, 110);
    CHECK_NEAR(whole[0].Right, 50);
    // more than the total: clipped at the right
    float big[] = { 80, 80 };
    auto over = StackSegments(big, 2, 100, area);
    CHECK_NEAR(over[1].Right, 110);
    // nothing to show
    float zeros[] = { 0, 0 };
    auto none = StackSegments(zeros, 2, 0, area);
    CHECK(none.size() == 2 && none[1].Width() == 0);
}

TEST(Cells_BlendAndFillRect) {
    CHECK(Blend(0x000000, RGB(255, 255, 255), 0.5f) == 0x808080);
    CHECK(Blend(0x102030, RGB(255, 0, 0), 0) == 0x102030);
    CHECK(Blend(0x102030, RGB(255, 0, 0), 1) == 0xFF0000);
    Buffer b(6, 4);
    FillRect(b.Px, { 1, 1, 3.5f, 3 }, RGB(255, 255, 255));
    CHECK(b.Px.At(1, 1) == 0xFFFFFF && b.Px.At(2, 2) == 0xFFFFFF);
    CHECK(b.Px.At(3, 1) == 0x808080);                   // half covered
    CHECK(b.Px.At(0, 0) == 0 && b.Px.At(4, 1) == 0 && b.Px.At(1, 3) == 0);
    FillRect(b.Px, { -10, -10, 100, 100 }, RGB(1, 2, 3));   // clipped
    CHECK(b.Px.At(5, 3) == 0x010203);
}

TEST(Cells_FillArea) {
    Buffer b(10, 10);
    // a flat line at y = 4 from x 2 to 8: columns 2..7 filled from 4 down to 10
    std::vector<PointF> run = { { 2, 4 }, { 8, 4 } };
    FillArea(b.Px, run, 10, RGB(255, 255, 255));
    CHECK(b.Px.At(2, 4) == 0xFFFFFF && b.Px.At(7, 9) == 0xFFFFFF);
    CHECK(b.Px.At(2, 3) == 0 && b.Px.At(1, 5) == 0 && b.Px.At(8, 5) == 0);
    // a slope: higher on the right
    Buffer s(10, 10);
    std::vector<PointF> slope = { { 0, 10 }, { 10, 0 } };
    FillArea(s.Px, slope, 10, RGB(255, 255, 255));
    CHECK(s.Px.At(9, 1) == 0xFFFFFF && s.Px.At(9, 0) != 0);
    CHECK(s.Px.At(0, 8) == 0 && s.Px.At(0, 9) != 0);
    // one point: nothing
    Buffer d(4, 4);
    FillArea(d.Px, { { 2, 2 } }, 4, RGB(255, 255, 255));
    CHECK(d.Px.At(2, 3) == 0);
}

TEST(Cells_Polyline) {
    Buffer b(20, 10);
    // horizontal, 1 px wide, through pixel centers on row 5
    DrawPolyline(b.Px, { { 2, 5.5f }, { 17, 5.5f } }, 1, RGB(255, 255, 255));
    CHECK(b.Px.At(8, 5) == 0xFFFFFF);
    CHECK(b.Px.At(8, 4) == 0 && b.Px.At(8, 6) == 0);
    // between rows: both half
    Buffer h(20, 10);
    DrawPolyline(h.Px, { { 2, 5 }, { 17, 5 } }, 1, RGB(255, 255, 255));
    CHECK(h.Px.At(8, 4) == 0x808080 && h.Px.At(8, 5) == 0x808080);
    // a joint isn't darker than the segments
    Buffer j(20, 20);
    DrawPolyline(j.Px, { { 2, 10.5f }, { 10.5f, 10.5f }, { 10.5f, 2 } }, 1, RGB(255, 255, 255));
    CHECK(j.Px.At(10, 10) == 0xFFFFFF);
    CHECK(j.Px.At(10, 5) == 0xFFFFFF && j.Px.At(5, 10) == 0xFFFFFF);
    // alpha
    Buffer a(10, 10);
    DrawPolyline(a.Px, { { 0, 5.5f }, { 10, 5.5f } }, 1, RGB(255, 255, 255), 0.5f);
    CHECK(a.Px.At(5, 5) == 0x808080);
    // one point: a dot; none: nothing; off the buffer: clipped
    Buffer d(10, 10);
    DrawPolyline(d.Px, { { 5, 5 } }, 3, RGB(255, 255, 255));
    CHECK(d.Px.At(4, 4) != 0 && d.Px.At(0, 0) == 0);
    DrawPolyline(d.Px, {}, 3, RGB(255, 0, 0));
    DrawPolyline(d.Px, { { -50, -50 }, { 50, -40 } }, 2, RGB(255, 0, 0));
    CHECK(d.Px.At(9, 9) == 0);
}

TEST(Cells_HistoryKeepsRowsSeen) {
    CellHistory<int> h(5);
    h.BeginUpdate();
    h.Push(1, 10);
    h.Push(2, 20);
    h.EndUpdate();
    CHECK(h.Count() == 2);
    CHECK(h.Find(1) && h.Find(1)->Count() == 1);
    // row 2 not seen this time: dropped; row 3 new: starts empty then gets one
    h.BeginUpdate();
    h.Push(1, 11);
    h.Push(3, 30);
    h.EndUpdate();
    CHECK(h.Count() == 2 && !h.Find(2));
    CHECK(h.Find(1)->Count() == 2 && h.Find(1)->Last() == 11);
    CHECK(h.Find(3)->Count() == 1);
    // Keep: through a refresh without a sample
    h.BeginUpdate();
    h.Push(1, 12);
    h.Keep(3);
    h.EndUpdate();
    CHECK(h.Find(3) && h.Find(3)->Count() == 1);
    // capacity: the newest kept
    for (int i = 0; i < 10; i++)
        h.Push(1, (float)i);
    CHECK(h.Find(1)->Count() == 5 && h.Find(1)->Last() == 9);
    h.SetCapacity(3);
    CHECK(h.Find(1)->Count() == 3 && h.Find(1)->AtFromEnd(2) == 7);
    CHECK(h.Remove(1) && !h.Remove(1));
    h.Clear();
    CHECK(h.Count() == 0);
    // keys of another type
    CellHistory<std::wstring> named;
    named.Push(L"a", 1);
    CHECK(named.Find(L"a") && !named.Find(L"b"));
}

TEST(Cells_Tint) {
    Buffer light(4, 1, 0xFFFFFF), dark(4, 1, 0x101010);
    TintRect(light.Px, { 0, 0, 2, 1 }, 0.5f);
    TintRect(dark.Px, { 0, 0, 2, 1 }, 0.5f);
    CHECK(light.Px.At(0, 0) == 0x808080 && light.Px.At(2, 0) == 0xFFFFFF);   // darker on light
    CHECK(dark.Px.At(1, 0) == 0x888888 && dark.Px.At(3, 0) == 0x101010);     // lighter on dark
}

// a memory DC with a DIB section, for the drawing functions
struct TestDc {
    HDC       Dc = nullptr;
    HBITMAP   Bitmap = nullptr;
    HGDIOBJ   Old = nullptr;
    uint32_t* Bits = nullptr;
    int       W, H;

    TestDc(int w, int h, uint32_t fill) : W(w), H(h) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        Bitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        Bits = (uint32_t*)bits;
        Dc = CreateCompatibleDC(nullptr);
        Old = SelectObject(Dc, Bitmap);
        std::fill_n(Bits, (size_t)w * h, fill);
    }
    ~TestDc() {
        SelectObject(Dc, Old);
        DeleteDC(Dc);
        DeleteObject(Bitmap);
    }
    uint32_t At(int x, int y) const {
        GdiFlush();
        return Bits[(size_t)y * W + x];
    }
};

TEST(Cells_DrawMeter) {
    TestDc d(120, 20, 0xFFFFFF);
    MeterStyle s;
    s.Padding = 0;
    s.BarColor = RGB(255, 0, 0);
    DrawMeter(d.Dc, { 10, 0, 110, 20 }, 50, s);
    CHECK(d.At(30, 10) == 0xFF0000);                   // the filled half
    CHECK(d.At(59, 10) == 0xFF0000);
    uint32_t track = d.At(80, 10);
    CHECK(track != 0xFFFFFF && track != 0xFF0000);    // the tinted track
    CHECK(d.At(5, 10) == 0xFFFFFF && d.At(115, 10) == 0xFFFFFF);   // outside the cell: untouched
    // thresholds pick the color; a bar height is centered
    CellThreshold t[] = { { 0, RGB(0, 255, 0) }, { 80, RGB(0, 0, 255) } };
    s.Thresholds = t;
    s.ThresholdCount = 2;
    s.BarHeight = 6;
    TestDc e(100, 20, 0xFFFFFF);
    DrawMeter(e.Dc, { 0, 0, 100, 20 }, 90, s);
    CHECK(e.At(50, 10) == 0x0000FF);
    CHECK(e.At(50, 2) == 0xFFFFFF && e.At(50, 17) == 0xFFFFFF);   // above and below the bar
    // the track on a dark background is lighter
    TestDc dark(100, 20, 0x202020);
    DrawMeter(dark.Dc, { 0, 0, 100, 20 }, 0, MeterStyle{});
    CHECK(((dark.At(50, 10) >> 16) & 0xFF) > 0x20);
}

TEST(Cells_DrawSparklineAndColumns) {
    TestDc d(60, 20, 0xFFFFFF);
    SparklineStyle s;
    s.Padding = 0;
    s.LineWidth = 2;
    s.LineColor = RGB(0, 0, 0);
    s.FillColor = RGB(255, 0, 0);
    s.FillOpacity = 1;
    auto series = MakeSeries({ 50, 50, 50, 50 }, 4);
    DrawSparkline(d.Dc, { 0, 0, 60, 20 }, series, s);
    // a flat line at half height: dark on the line, the fill below it, white above
    CHECK(d.At(30, 10) == 0x000000 || d.At(30, 9) == 0x000000);
    CHECK(d.At(30, 16) == 0xFF0000);
    CHECK(d.At(30, 3) == 0xFFFFFF);

    TestDc c(40, 20, 0xFFFFFF);
    ColumnStyle cs;
    cs.Padding = 0;
    cs.Gap = 0;
    CellThreshold t[] = { { 0, RGB(0, 255, 0) }, { 75, RGB(255, 0, 0) } };
    cs.Thresholds = t;
    cs.ThresholdCount = 2;
    DrawColumns(c.Dc, { 0, 0, 40, 20 }, MakeSeries({ 100, 50 }, 4), cs);   // 4 slots of 10 px
    CHECK(c.At(35, 15) == 0x00FF00 && c.At(35, 5) == 0xFFFFFF);   // the newest, 50: half high, green
    CHECK(c.At(25, 2) == 0xFF0000);                                // 100: full, red
    CHECK(c.At(5, 15) == 0xFFFFFF);                                // no samples there yet
}

TEST(Cells_DrawStacked) {
    TestDc d(100, 10, 0xFFFFFF);
    StackedStyle s;
    s.Padding = 0;
    s.Total = 100;
    COLORREF colors[] = { RGB(255, 0, 0), RGB(0, 0, 255) };
    s.Colors = colors;
    s.ColorCount = 2;
    float values[] = { 25, 25 };
    DrawStacked(d.Dc, { 0, 0, 100, 10 }, values, 2, s);
    CHECK(d.At(10, 5) == 0xFF0000);
    CHECK(d.At(40, 5) == 0x0000FF);
    CHECK(d.At(80, 5) != 0xFFFFFF && d.At(80, 5) != 0x0000FF);   // the track
    // an empty cell or no values: no harm
    DrawStacked(d.Dc, { 0, 0, 0, 0 }, values, 2, s);
    DrawStacked(d.Dc, { 0, 0, 100, 10 }, nullptr, 0, s);
}

int wmain() {
    wprintf(L"GraphControl data model tests\n\n");

    Run_Series_PartiallyFilled();
    Run_Series_WrapsAndDropsOldest();
    Run_Series_CapacityIsClamped();
    Run_Series_ShrinkKeepsNewest();
    Run_Series_GrowKeepsEverything();
    Run_Series_Clear();

    Run_Series_MissingIgnoredByStats();
    Run_Series_EvictingMissingKeepsSumIntact();
    Run_Series_AllMissing();
    Run_Series_ShrinkRebuildsAcrossMissing();

    Run_NiceCeil_Values();

    Run_Format_Percent();
    Run_Format_BytesScalesBy1024();
    Run_Format_BitsScaleBy1000();
    Run_Format_Number();
    Run_Format_UsesLocaleSeparator();

    Run_GraphData_SeriesLifecycle();
    Run_GraphData_CapacityPropagates();
    Run_GraphData_PushSamplesKeepsSeriesAligned();
    Run_GraphData_ShortPushLeavesAGap();
    Run_GraphData_ExtremesSkipHiddenAndMissing();
    Run_GraphData_CommonCountIsTheShortestVisible();
    Run_GraphData_MaxStackedSumsPerSlot();
    Run_GraphData_VersionTracksMutations();

    Run_TimeBase_StampsEveryPush();
    Run_TimeBase_ExplicitTicksAreKeptInOrder();
    Run_TimeBase_WrapsWithTheSamples();
    Run_TimeBase_ShrinkKeepsStampsAlignedWithSamples();
    Run_TimeBase_ClearResetsIt();

    Run_Cells_ScaleRangeFixedAndAuto();
    Run_Cells_ValueToY();
    Run_Cells_SparkRunsGrowFromTheRight();
    Run_Cells_ColumnBars();
    Run_Cells_MeterAndThresholds();
    Run_Cells_StackSegments();
    Run_Cells_BlendAndFillRect();
    Run_Cells_FillArea();
    Run_Cells_Polyline();
    Run_Cells_HistoryKeepsRowsSeen();
    Run_Cells_Tint();
    Run_Cells_DrawMeter();
    Run_Cells_DrawSparklineAndColumns();
    Run_Cells_DrawStacked();

    wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
