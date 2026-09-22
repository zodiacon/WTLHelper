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

    wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
