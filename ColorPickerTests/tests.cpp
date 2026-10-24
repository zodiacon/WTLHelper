// Unit tests for the ColorPicker model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Only the pure parts are covered: HSV <-> RGB, hex text, the layout, points <-> values
// and the pixel drawing. Anything that needs an HWND is checked by hand with ColorPickerDemo.

#include "ColorPickerModel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ---- Tiny harness ------------------------------------------------------------

static int g_checks = 0;
static int g_failures = 0;
static const wchar_t* g_currentTest = L"";

static void Check(bool ok, const char* expr, int line) {
	g_checks++;
	if (!ok) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
	}
}

static void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got:  \"%s\"\n        want: \"%s\"\n", g_currentTest, line, expr, actual.c_str(), expected.c_str());
	}
}

#define CHECK(expr)      Check((expr), #expr, __LINE__)
#define CHECK_STR(a, b)  CheckStr((a), (b), #a " == " #b, __LINE__)

#define TEST(name)															\
	static void name();														\
	static void Run_##name() {												\
		g_currentTest = L## #name;											\
		const int before = g_failures;										\
		name();																\
		wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name);	\
	}																		\
	static void name()

// ---- Helpers --------------------------------------------------------------------

static bool Near(double a, double b, double eps = 1e-9) {
	return std::abs(a - b) <= eps;
}

struct Buffer {
	std::vector<uint32_t> bits;
	CpPixels px;

	Buffer(int w, int h, uint32_t fill = 0) : bits((size_t)w * h, fill) {
		px = { bits.data(), w, h };
	}
};

// ---- Tests -------------------------------------------------------------------------

TEST(HsvToRgb_Primaries) {
	CHECK(CpHsvToRgb({ 0, 1, 1 }) == RGB(255, 0, 0));
	CHECK(CpHsvToRgb({ 60, 1, 1 }) == RGB(255, 255, 0));
	CHECK(CpHsvToRgb({ 120, 1, 1 }) == RGB(0, 255, 0));
	CHECK(CpHsvToRgb({ 180, 1, 1 }) == RGB(0, 255, 255));
	CHECK(CpHsvToRgb({ 240, 1, 1 }) == RGB(0, 0, 255));
	CHECK(CpHsvToRgb({ 300, 1, 1 }) == RGB(255, 0, 255));
	CHECK(CpHsvToRgb({ 360, 1, 1 }) == RGB(255, 0, 0));
	CHECK(CpHsvToRgb({ 30, 1, 1 }) == RGB(255, 128, 0));
	CHECK(CpHsvToRgb({ 0, 0, 1 }) == RGB(255, 255, 255));
	CHECK(CpHsvToRgb({ 200, 0.7, 0 }) == RGB(0, 0, 0));
	CHECK(CpHsvToRgb({ 123, 0, 0.5 }) == RGB(128, 128, 128));
	CHECK(CpHsvToRgb({ 0, 0.5, 1 }) == RGB(255, 128, 128));
}

TEST(RgbToHsv_Values) {
	auto hsv = CpRgbToHsv(RGB(255, 0, 0));
	CHECK(Near(hsv.h, 0) && Near(hsv.s, 1) && Near(hsv.v, 1));
	hsv = CpRgbToHsv(RGB(0, 0, 255));
	CHECK(Near(hsv.h, 240) && Near(hsv.s, 1) && Near(hsv.v, 1));
	hsv = CpRgbToHsv(RGB(255, 0, 255));
	CHECK(Near(hsv.h, 300));
	hsv = CpRgbToHsv(RGB(0, 128, 0));
	CHECK(Near(hsv.h, 120) && Near(hsv.s, 1) && Near(hsv.v, 128 / 255.0));
	hsv = CpRgbToHsv(RGB(255, 0, 1));   // just under 360
	CHECK(hsv.h > 359 && hsv.h < 360);
}

TEST(RgbToHsv_KeepsUndefinedParts) {
	CpHsv prev{ 200, 0.6, 0.8 };
	// a gray: the hue stays, saturation 0
	auto hsv = CpRgbToHsv(RGB(100, 100, 100), prev);
	CHECK(Near(hsv.h, 200) && Near(hsv.s, 0) && Near(hsv.v, 100 / 255.0));
	// black: the hue and saturation stay
	hsv = CpRgbToHsv(RGB(0, 0, 0), prev);
	CHECK(Near(hsv.h, 200) && Near(hsv.s, 0.6) && Near(hsv.v, 0));
	// white
	hsv = CpRgbToHsv(RGB(255, 255, 255), prev);
	CHECK(Near(hsv.h, 200) && Near(hsv.s, 0) && Near(hsv.v, 1));
	// red at the bottom of the hue bar stays at 360
	hsv = CpRgbToHsv(RGB(200, 0, 0), { 360, 1, 1 });
	CHECK(Near(hsv.h, 360));
	hsv = CpRgbToHsv(RGB(200, 0, 0), { 10, 1, 1 });
	CHECK(Near(hsv.h, 0));
}

TEST(RoundTrip_RgbHsvRgb) {
	int bad = 0;
	for (int r = 0; r < 256; r += 5) {
		for (int g = 0; g < 256; g += 3) {
			for (int b = 0; b < 256; b += 7) {
				COLORREF c = RGB(r, g, b);
				if (CpHsvToRgb(CpRgbToHsv(c, { 77, 0.3, 0.3 })) != c)
					bad++;
			}
		}
	}
	CHECK(bad == 0);
	// the corners of the cube
	for (int i = 0; i < 8; i++) {
		COLORREF c = RGB(i & 1 ? 255 : 0, i & 2 ? 255 : 0, i & 4 ? 255 : 0);
		CHECK(CpHsvToRgb(CpRgbToHsv(c)) == c);
	}
}

TEST(UpdateHsv_KeepsPrevWhenSameColor) {
	CpHsv prev{ 123.456, 0.4321, 0.789 };
	auto c = CpHsvToRgb(prev);
	auto hsv = CpUpdateHsv(prev, c);
	CHECK(hsv == prev);
	// another color is converted
	hsv = CpUpdateHsv(prev, RGB(0, 0, 255));
	CHECK(Near(hsv.h, 240) && Near(hsv.s, 1) && Near(hsv.v, 1));
	// echoing black back keeps the hue and saturation
	CpHsv black{ 45, 0.9, 0 };
	CHECK(CpUpdateHsv(black, RGB(0, 0, 0)) == black);
}

TEST(Normalize) {
	auto c = CpNormalize({ 370, 1.5, -0.5 });
	CHECK(Near(c.h, 10) && Near(c.s, 1) && Near(c.v, 0));
	c = CpNormalize({ -30, 0.5, 0.5 });
	CHECK(Near(c.h, 330));
	c = CpNormalize({ 360, 0.5, 0.5 });
	CHECK(Near(c.h, 360));
	c = CpNormalize({ NAN, NAN, NAN });
	CHECK(c.h == 0 && c.s == 0 && c.v == 0);
	c = CpNormalize({ 720, 0, 0 });
	CHECK(Near(c.h, 0));
}

TEST(Hex_Format) {
	CHECK_STR(CpFormatHex(RGB(0x12, 0xAB, 0xEF)), L"#12ABEF");
	CHECK_STR(CpFormatHex(RGB(0, 0, 0)), L"#000000");
	CHECK_STR(CpFormatHex(RGB(0x12, 0xAB, 0xEF), 0x80, true), L"#8012ABEF");
	CHECK_STR(CpFormatHex(RGB(255, 255, 255), 0x80, false), L"#FFFFFF");
}

TEST(Hex_Parse) {
	COLORREF c = 0;
	BYTE a = 7;
	bool hasAlpha = true;
	CHECK(CpParseHex(L"#12abEF", &c, &a, &hasAlpha));
	CHECK(c == RGB(0x12, 0xAB, 0xEF) && a == 7 && !hasAlpha);
	CHECK(CpParseHex(L"  12ABEF ", &c));
	CHECK(c == RGB(0x12, 0xAB, 0xEF));
	CHECK(CpParseHex(L"#f80", &c));
	CHECK(c == RGB(0xFF, 0x88, 0x00));
	CHECK(CpParseHex(L"#4012ABEF", &c, &a, &hasAlpha));
	CHECK(c == RGB(0x12, 0xAB, 0xEF) && a == 0x40 && hasAlpha);

	c = 5;
	CHECK(!CpParseHex(L"", &c));
	CHECK(!CpParseHex(L"#", &c));
	CHECK(!CpParseHex(L"#12345", &c));
	CHECK(!CpParseHex(L"#1234567", &c));
	CHECK(!CpParseHex(L"#12345G", &c));
	CHECK(!CpParseHex(L"##123456", &c));
	CHECK(!CpParseHex(L"12 3456", &c));
	CHECK(c == 5);
}

TEST(Layout_WithAndWithoutAlpha) {
	CpMetrics m{ 8, 20, 12 };
	auto l = CpComputeLayout(300, 200, m, false);
	// the hue bar: 20 wide with its frame, at the right
	CHECK(l.hue.right == 300 - 8 - 1 && l.hue.left == 300 - 8 - 20 + 1);
	CHECK(l.hue.top == 9 && l.hue.bottom == 200 - 9);
	CHECK(::IsRectEmpty(&l.alpha));
	// the SV area: from the pad to the gap before the bar
	CHECK(l.sv.left == 9 && l.sv.top == 9 && l.sv.bottom == 191);
	CHECK(l.sv.right == 300 - 8 - 20 - 12 - 1);

	auto la = CpComputeLayout(300, 200, m, true);
	CHECK(la.alpha.right == 300 - 8 - 1);
	CHECK(la.hue.right == la.alpha.left - 1 - 12 - 1);
	CHECK(la.sv.right == la.hue.left - 1 - 12 - 1);
	CHECK(la.hue.right - la.hue.left == 18 && la.alpha.right - la.alpha.left == 18);
}

TEST(Layout_TooSmall) {
	CpMetrics m{ 8, 20, 12 };
	auto l = CpComputeLayout(10, 5, m, true);
	CHECK(l.sv.right >= l.sv.left && l.sv.bottom >= l.sv.top);
	CHECK(l.hue.right >= l.hue.left && l.hue.bottom >= l.hue.top);
	CHECK(l.alpha.right >= l.alpha.left && l.alpha.bottom >= l.alpha.top);
	auto z = CpComputeLayout(0, 0, m, false);
	CHECK(z.sv.right >= z.sv.left && z.hue.right >= z.hue.left);
}

TEST(Mapping_Sv) {
	RECT rc{ 10, 20, 111, 221 };   // 101 x 201
	double s, v;
	CpSvFromPoint(rc, { 10, 20 }, &s, &v);
	CHECK(Near(s, 0) && Near(v, 1));
	CpSvFromPoint(rc, { 110, 220 }, &s, &v);
	CHECK(Near(s, 1) && Near(v, 0));
	CpSvFromPoint(rc, { 60, 120 }, &s, &v);
	CHECK(Near(s, 0.5) && Near(v, 0.5));
	// clamped outside
	CpSvFromPoint(rc, { -50, 500 }, &s, &v);
	CHECK(Near(s, 0) && Near(v, 0));
	CpSvFromPoint(rc, { 500, -50 }, &s, &v);
	CHECK(Near(s, 1) && Near(v, 1));
	// back
	auto pt = CpPointFromSv(rc, 0.5, 0.5);
	CHECK(pt.x == 60 && pt.y == 120);
	pt = CpPointFromSv(rc, 0, 1);
	CHECK(pt.x == 10 && pt.y == 20);
	pt = CpPointFromSv(rc, 1, 0);
	CHECK(pt.x == 110 && pt.y == 220);
	// every pixel maps back to itself
	int bad = 0;
	for (int x = rc.left; x < rc.right; x++) {
		for (int y = rc.top; y < rc.bottom; y += 7) {
			CpSvFromPoint(rc, { x, y }, &s, &v);
			auto p = CpPointFromSv(rc, s, v);
			if (p.x != x || p.y != y)
				bad++;
		}
	}
	CHECK(bad == 0);
	// a one-pixel area doesn't divide by zero
	RECT tiny{ 5, 5, 6, 6 };
	CpSvFromPoint(tiny, { 5, 5 }, &s, &v);
	CHECK(Near(s, 0) && Near(v, 1));
}

TEST(Mapping_Bars) {
	RECT rc{ 0, 10, 20, 371 };   // 361 high: a degree per pixel
	CHECK(Near(CpHueFromBar(CpBarFromY(rc, 10)), 0));
	CHECK(Near(CpHueFromBar(CpBarFromY(rc, 370)), 360));
	CHECK(Near(CpHueFromBar(CpBarFromY(rc, 130)), 120));
	CHECK(Near(CpHueFromBar(CpBarFromY(rc, 1000)), 360));
	CHECK(CpYFromBar(rc, CpBarFromHue(240)) == 250);
	CHECK(CpAlphaFromBar(0) == 255 && CpAlphaFromBar(1) == 0);
	CHECK(CpAlphaFromBar(CpBarFromAlpha(128)) == 128);
	for (int a = 0; a < 256; a++)
		CHECK(CpAlphaFromBar(CpBarFromAlpha((BYTE)a)) == a);
}

TEST(Pixels_Sv) {
	Buffer b(51, 41);
	CpRenderSv(b.px, 240);
	CHECK(CpColor(b.px.At(0, 0)) == RGB(255, 255, 255));    // top left: white
	CHECK(CpColor(b.px.At(50, 0)) == RGB(0, 0, 255));       // top right: the hue
	CHECK(CpColor(b.px.At(0, 40)) == RGB(0, 0, 0));         // bottom: black
	CHECK(CpColor(b.px.At(50, 40)) == RGB(0, 0, 0));
	// every pixel is the color its point picks
	RECT rc{ 0, 0, 51, 41 };
	int bad = 0;
	for (int y = 0; y < 41; y++) {
		for (int x = 0; x < 51; x++) {
			double s, v;
			CpSvFromPoint(rc, { x, y }, &s, &v);
			if (CpColor(b.px.At(x, y)) != CpHsvToRgb({ 240, s, v }))
				bad++;
		}
	}
	CHECK(bad == 0);
}

TEST(Pixels_HueAndAlpha) {
	Buffer b(4, 7);   // rows at 0, 60, ... 360 degrees
	CpRenderHue(b.px);
	CHECK(CpColor(b.px.At(0, 0)) == RGB(255, 0, 0));
	CHECK(CpColor(b.px.At(3, 2)) == RGB(0, 255, 0));
	CHECK(CpColor(b.px.At(1, 4)) == RGB(0, 0, 255));
	CHECK(CpColor(b.px.At(2, 6)) == RGB(255, 0, 0));

	Buffer a(4, 3);
	CpRenderAlpha(a.px, RGB(255, 0, 0), 2, RGB(255, 255, 255), RGB(0, 0, 0));
	CHECK(CpColor(a.px.At(0, 0)) == RGB(255, 0, 0));   // opaque at the top
	CHECK(CpColor(a.px.At(3, 0)) == RGB(255, 0, 0));
	CHECK(CpColor(a.px.At(0, 2)) == RGB(0, 0, 0));   // the checkerboard at the bottom (row 2: the second cell)
	CHECK(CpColor(a.px.At(2, 2)) == RGB(255, 255, 255));
	CHECK(CpColor(a.px.At(0, 1)) == RGB(255, 128, 128));   // halfway over white
	CHECK(CpChecker(0, 0, 4, 1, 2) == CpPixel(1) && CpChecker(4, 0, 4, 1, 2) == CpPixel(2) && CpChecker(4, 4, 4, 1, 2) == CpPixel(1));
}

TEST(Pixels_FillFrameCopy) {
	Buffer b(10, 8, 0);
	CpFillRect(b.px, { -5, -5, 3, 2 }, RGB(1, 2, 3));   // clipped
	CHECK(b.px.At(0, 0) == CpPixel(RGB(1, 2, 3)) && b.px.At(2, 1) == CpPixel(RGB(1, 2, 3)));
	CHECK(b.px.At(3, 0) == 0 && b.px.At(0, 2) == 0);
	CpFillRect(b.px, { 8, 6, 20, 20 }, RGB(9, 9, 9));
	CHECK(b.px.At(9, 7) == CpPixel(RGB(9, 9, 9)));

	Buffer f(10, 8, 0);
	CpFrameRect(f.px, { 1, 1, 9, 7 }, RGB(255, 0, 0));
	CHECK(f.px.At(1, 1) == 0xFF0000 && f.px.At(8, 6) == 0xFF0000 && f.px.At(1, 4) == 0xFF0000 && f.px.At(5, 6) == 0xFF0000);
	CHECK(f.px.At(2, 2) == 0 && f.px.At(0, 0) == 0 && f.px.At(9, 7) == 0);

	Buffer src(3, 3, 0x123456);
	Buffer dst(5, 5, 0);
	CpCopy(dst.px, 3, -1, src.px);   // clipped on two sides
	CHECK(dst.px.At(3, 0) == 0x123456 && dst.px.At(4, 1) == 0x123456);
	CHECK(dst.px.At(2, 0) == 0 && dst.px.At(3, 2) == 0);
}

TEST(Pixels_Ring) {
	Buffer b(40, 40, 0);
	CpDrawRing(b.px, 20, 20, 10, 2, RGB(255, 255, 255));
	// on the ring: full; at the center and well outside: untouched
	CHECK(b.px.At(29, 19) == 0xFFFFFF || b.px.At(30, 19) == 0xFFFFFF);
	CHECK(b.px.At(19, 19) == 0 && b.px.At(20, 20) == 0);
	CHECK(b.px.At(0, 0) == 0 && b.px.At(35, 20) == 0);
	// an edge pixel is partly covered
	bool partial = false;
	for (int x = 0; x < 40; x++) {
		auto p = b.px.At(x, 14);
		if (p != 0 && p != 0xFFFFFF)
			partial = true;
	}
	CHECK(partial);
	// clipped at the buffer's edges
	CpDrawRing(b.px, 0, 0, 5, 2, RGB(255, 0, 0));
	CpDrawRing(b.px, 40, 40, 5, 2, RGB(255, 0, 0));
	CHECK(true);
	CHECK(CpBlend(0x000000, 0xFFFFFF, 0.5) == 0x808080);
}

TEST(Panel_Layout) {
	CpPanelMetrics m;   // label 28, field 64, height 22, row gap 6, col gap 10, swatch 56 + 12
	CHECK(CpPanelWidth(m) == 2 * (28 + 64) + 10);
	CpPanelOptions o;
	o.eyedropper = false;
	auto p = CpComputePanel(100, 8, m, o);
	CHECK(p.swatch.left == 100 && p.swatch.top == 8 && p.swatch.right == 100 + CpPanelWidth(m) && p.swatch.bottom == 64);
	CHECK(::IsRectEmpty(&p.eyedropper));
	// the first column: H S V A; the second: R G B Hex
	CHECK(p.label[CpH].left == 100 && p.label[CpH].top == 76 && p.frame[CpH].left == 128 && p.frame[CpH].right == 192);
	CHECK(p.frame[CpS].top == 76 + 28 && p.frame[CpA].top == 76 + 3 * 28);
	CHECK(p.label[CpR].left == 100 + 28 + 64 + 10 && p.frame[CpR].top == 76);
	CHECK(p.frame[CpHex].top == p.frame[CpA].top && p.frame[CpHex].right == p.swatch.right);

	// no alpha: no A field; no swatch: the fields start at the top
	auto q = CpComputePanel(0, 0, m, { false, false, true, false, false, 0 });
	CHECK(::IsRectEmpty(&q.swatch) && ::IsRectEmpty(&q.frame[CpA]) && ::IsRectEmpty(&q.label[CpA]));
	CHECK(q.frame[CpH].top == 0 && !::IsRectEmpty(&q.frame[CpHex]));
	// no fields
	auto s = CpComputePanel(0, 0, m, { true, false, false, true, false, 24 });
	CHECK(!::IsRectEmpty(&s.swatch));
	for (int f = 0; f < CpFieldCount; f++)
		CHECK(::IsRectEmpty(&s.frame[f]));
	for (int g = 0; g < CpGroupCount; g++)
		CHECK(::IsRectEmpty(&s.group[g]));
}

TEST(Panel_EyedropperAndGroups) {
	CpPanelMetrics m;   // width 194, field height 22, button gap 6, cell gap 3, group gap 8
	auto p = CpComputePanel(0, 0, m, { true, true, true, true, true, 24 });
	CHECK(p.eyedropper.right == 194 && p.eyedropper.left == 194 - 22 && p.eyedropper.bottom == 56);
	CHECK(p.swatch.right == 194 - 22 - 6);
	// cells: (194 - 11 * 3) / 12 = 13
	CHECK(p.cell == 13);
	int fieldsBottom = 68 + 4 * 28 - 6;
	CHECK(p.group[CpPalette].top == fieldsBottom + 12);
	CHECK(p.group[CpPalette].bottom - p.group[CpPalette].top == 2 * 13 + 3);
	CHECK(p.group[CpPalette].right == 12 * 13 + 11 * 3);
	CHECK(p.group[CpCustom].top == p.group[CpPalette].bottom + 8 && p.group[CpCustom].bottom - p.group[CpCustom].top == 13);
	CHECK(p.group[CpRecent].top == p.group[CpCustom].bottom + 8);
	CHECK(CpGroupRows(CpPalette, 25) == 3 && CpGroupRows(CpPalette, 1000) == 4 && CpGroupRows(CpCustom, 24) == 1);

	auto c = CpSwatchCell(p, CpPalette, 13, 3);   // second row, second column
	CHECK(c.left == 16 && c.top == p.group[CpPalette].top + 16 && c.right - c.left == 13);
	POINT in{ c.left + 5, c.top + 5 };
	CHECK(CpSwatchAt(p, CpPalette, 24, 3, in) == 13);
	CHECK(CpSwatchAt(p, CpPalette, 13, 3, in) == -1);   // past the count
	CHECK(CpSwatchAt(p, CpPalette, 24, 3, { c.right + 1, c.top + 5 }) == -1);   // in a gap
	CHECK(CpSwatchAt(p, CpCustom, 12, 3, in) == -1);
	auto r = CpSwatchCell(p, CpRecent, 11, 3);
	CHECK(CpSwatchAt(p, CpRecent, 12, 3, { r.right - 1, r.bottom - 1 }) == 11);

	// no palette rows: custom and recent stay, starting where the palette would
	auto n = CpComputePanel(0, 0, m, { true, true, true, true, true, 0 });
	CHECK(::IsRectEmpty(&n.group[CpPalette]) && n.group[CpCustom].top == p.group[CpPalette].top && !::IsRectEmpty(&n.group[CpRecent]));

	int count = 0;
	auto palette = CpDefaultPalette(&count);
	CHECK(count == 24 && palette[0] == RGB(0, 0, 0));
}

TEST(Recent) {
	std::vector<COLORREF> recent;
	CpAddRecent(recent, RGB(1, 0, 0), 3);
	CpAddRecent(recent, RGB(2, 0, 0), 3);
	CpAddRecent(recent, RGB(3, 0, 0), 3);
	CHECK(recent.size() == 3 && recent[0] == RGB(3, 0, 0) && recent[2] == RGB(1, 0, 0));
	CpAddRecent(recent, RGB(1, 0, 0), 3);   // to the front, once
	CHECK(recent.size() == 3 && recent[0] == RGB(1, 0, 0) && recent[1] == RGB(3, 0, 0));
	CpAddRecent(recent, RGB(4, 0, 0), 3);   // the oldest goes
	CHECK(recent.size() == 3 && recent[0] == RGB(4, 0, 0) && recent[2] == RGB(3, 0, 0));
	CpAddRecent(recent, 0xFF000004, 3);     // high byte ignored
	CHECK(recent.size() == 3 && recent[0] == RGB(4, 0, 0));
}

TEST(Magnifier_Placement) {
	RECT work{ 0, 0, 1000, 800 };
	SIZE size{ 120, 140 };
	auto pt = CpPlaceMagnifier({ 100, 100 }, size, work, 20);
	CHECK(pt.x == 120 && pt.y == 120);
	pt = CpPlaceMagnifier({ 950, 100 }, size, work, 20);   // flipped left
	CHECK(pt.x == 950 - 20 - 120 && pt.y == 120);
	pt = CpPlaceMagnifier({ 100, 750 }, size, work, 20);   // flipped up
	CHECK(pt.y == 750 - 20 - 140);
	pt = CpPlaceMagnifier({ 50, 50 }, { 1200, 900 }, work, 20);   // bigger than the work area
	CHECK(pt.x == 0 && pt.y == 0);
	RECT second{ -1920, 0, 0, 1080 };
	pt = CpPlaceMagnifier({ -10, 500 }, size, second, 20);
	CHECK(pt.x == -10 - 20 - 120 && pt.y == 520);
}

TEST(Panel_Pieces) {
	RECT frame{ 10, 20, 74, 42 };
	RECT up, down;
	CpSpinRects(frame, 14, &up, &down);
	CHECK(up.left == 59 && up.right == 73 && up.top == 21 && up.bottom == 31);
	CHECK(down.top == 31 && down.bottom == 41 && down.left == 59);
	auto e = CpEditRect(frame, CpR, 14, 16, 3);
	CHECK(e.left == 14 && e.right == 58 && e.top == 23 && e.bottom == 39);
	auto h = CpEditRect(frame, CpHex, 14, 16, 3);
	CHECK(h.right == 72);
	CHECK(CpHasSpin(CpH) && !CpHasSpin(CpHex));

	RECT sw{ 0, 0, 100, 50 }, n, o;
	CpSwatchRects(sw, true, &n, &o);
	CHECK(n.top == 0 && n.bottom == 25 && o.top == 25 && o.bottom == 50 && o.right == 100);
	CpSwatchRects(sw, false, &n, &o);
	CHECK(n.bottom == 50 && ::IsRectEmpty(&o));
}

TEST(Fields_Text) {
	CpHsv hsv{ 210.4, 0.604, 0.899 };
	auto rgb = CpHsvToRgb(hsv);
	CHECK_STR(CpFieldText(CpH, hsv, 77), L"210");
	CHECK_STR(CpFieldText(CpS, hsv, 77), L"60");
	CHECK_STR(CpFieldText(CpV, hsv, 77), L"90");
	CHECK_STR(CpFieldText(CpA, hsv, 77), L"77");
	CHECK_STR(CpFieldText(CpR, hsv, 77), std::to_wstring(GetRValue(rgb)));
	CHECK_STR(CpFieldText(CpB, hsv, 77), std::to_wstring(GetBValue(rgb)));
	CHECK_STR(CpFieldText(CpHex, hsv, 77), CpFormatHex(rgb));
	CHECK_STR(CpFieldText(CpH, { 360, 1, 1 }, 255), L"360");
}

TEST(Fields_Parse) {
	int v = 0;
	CHECK(CpParseInt(L" 42 ", &v) && v == 42);
	CHECK(CpParseInt(L"-5", &v) && v == -5);
	CHECK(CpParseInt(L"007", &v) && v == 7);
	CHECK(!CpParseInt(L"", &v) && !CpParseInt(L"-", &v) && !CpParseInt(L"4 2", &v) && !CpParseInt(L"1.5", &v));
	CHECK(!CpParseInt(L"12a", &v) && !CpParseInt(L"1234567890", &v));
}

TEST(Fields_Apply) {
	CpHsv hsv{ 200, 0.5, 0.5 };
	BYTE alpha = 255;
	CHECK(CpApplyField(CpH, L"120", hsv, alpha) && hsv.h == 120);
	CHECK(CpApplyField(CpH, L"400", hsv, alpha) && hsv.h == 360);     // clamped
	CHECK(CpApplyField(CpS, L"75", hsv, alpha) && Near(hsv.s, 0.75));
	CHECK(CpApplyField(CpV, L"-3", hsv, alpha) && hsv.v == 0);
	CHECK(CpApplyField(CpA, L"300", hsv, alpha) && alpha == 255);
	CHECK(CpApplyField(CpA, L"128", hsv, alpha) && alpha == 128);
	// bad text changes nothing
	auto before = hsv;
	CHECK(!CpApplyField(CpS, L"x", hsv, alpha) && hsv == before && alpha == 128);
	CHECK(!CpApplyField(CpHex, L"#12", hsv, alpha) && hsv == before);

	// RGB: the other channels stay
	hsv = CpRgbToHsv(RGB(10, 20, 30));
	CHECK(CpApplyField(CpG, L"200", hsv, alpha) && CpHsvToRgb(hsv) == RGB(10, 200, 30));
	CHECK(CpApplyField(CpR, L"999", hsv, alpha) && CpHsvToRgb(hsv) == RGB(255, 200, 30));
	// the value shown already keeps the fraction behind it
	CpHsv exact{ 33.3, 0.333, 0.777 };
	CHECK(CpApplyField(CpH, L"33", exact, alpha) && exact.h == 33.3);
	CHECK(CpApplyField(CpS, L"33", exact, alpha) && exact.s == 0.333);
	// RGB to gray keeps the hue
	hsv = { 250, 0.5, 0.5 };
	CHECK(CpApplyField(CpHex, L"#808080", hsv, alpha) && hsv.h == 250 && hsv.s == 0 && alpha == 128);
	// hex with alpha
	CHECK(CpApplyField(CpHex, L"#40FF0000", hsv, alpha) && CpHsvToRgb(hsv) == RGB(255, 0, 0) && alpha == 0x40);
}

TEST(Fields_Step) {
	CpHsv hsv{ 359.6, 0.5, 0.5 };
	BYTE alpha = 250;
	CHECK(CpStepField(CpH, 1, hsv, alpha) && hsv.h == 360);   // shown as 360: stays
	CHECK(CpStepField(CpH, -10, hsv, alpha) && hsv.h == 350);
	CHECK(CpStepField(CpS, 1, hsv, alpha) && Near(hsv.s, 0.51));
	CHECK(CpStepField(CpA, 10, hsv, alpha) && alpha == 255);
	CHECK(CpStepField(CpA, -1, hsv, alpha) && alpha == 254);
	hsv = CpRgbToHsv(RGB(0, 100, 255));
	CHECK(CpStepField(CpB, 1, hsv, alpha) && CpHsvToRgb(hsv) == RGB(0, 100, 255));
	CHECK(CpStepField(CpR, 5, hsv, alpha) && CpHsvToRgb(hsv) == RGB(5, 100, 255));
	CHECK(!CpStepField(CpHex, 1, hsv, alpha));
}

TEST(Pixels_Swatch) {
	Buffer b(10, 4);
	CpRenderSwatch(b.px, RGB(255, 0, 0), 0, 2, RGB(255, 255, 255), RGB(0, 0, 0));
	CHECK(CpColor(b.px.At(0, 0)) == RGB(255, 0, 0) && CpColor(b.px.At(4, 3)) == RGB(255, 0, 0));
	CHECK(CpColor(b.px.At(5, 0)) == RGB(255, 255, 255) && CpColor(b.px.At(7, 0)) == RGB(0, 0, 0));
	CpRenderSwatch(b.px, RGB(255, 0, 0), 255, 2, RGB(255, 255, 255), RGB(0, 0, 0));
	CHECK(CpColor(b.px.At(9, 3)) == RGB(255, 0, 0));
}

TEST(DropDown_And_Center) {
	RECT work{ 0, 0, 1000, 800 };
	RECT anchor{ 100, 100, 180, 124 };
	auto pt = CpPlaceDropDown(anchor, { 300, 200 }, work);
	CHECK(pt.x == 100 && pt.y == 124);
	RECT low{ 100, 700, 180, 724 };   // no room below: above
	pt = CpPlaceDropDown(low, { 300, 200 }, work);
	CHECK(pt.y == 500);
	RECT right{ 900, 100, 980, 124 };   // kept in from the right
	pt = CpPlaceDropDown(right, { 300, 200 }, work);
	CHECK(pt.x == 700 && pt.y == 124);
	RECT mid{ 100, 350, 180, 374 };   // fits neither: below, pushed up into the work area
	pt = CpPlaceDropDown(mid, { 300, 600 }, work);
	CHECK(pt.y == 200);

	pt = CpCenterOver({ 100, 100, 500, 400 }, { 200, 100 }, work);
	CHECK(pt.x == 200 && pt.y == 200);
	pt = CpCenterOver({ -300, -300, -100, -100 }, { 200, 100 }, work);   // owner off the work area
	CHECK(pt.x == 0 && pt.y == 0);
}

TEST(Panel_Height) {
	CpPanelMetrics m;
	auto p = CpComputePanel(0, 10, m, { true, true, true, true, true, 24 });
	CHECK(CpPanelHeight(p, 10) == p.group[CpRecent].bottom - 10);
	auto s = CpComputePanel(0, 10, m, { true, false, false, true, false, 0 });
	CHECK(CpPanelHeight(s, 10) == m.swatchHeight);
	auto none = CpComputePanel(0, 10, m, { false, false, false, true, false, 0 });
	CHECK(CpPanelHeight(none, 10) == 0);
}

TEST(Swatch_Navigation) {
	int counts[CpGroupCount] = { 24, 12, 3 };   // palette rows 0-1, custom row 2, recent row 3 (3 cells)
	int g = CpPalette, i = 5;
	CHECK(CpSwatchMove(counts, g, i, CpNavRight) && g == CpPalette && i == 6);
	i = 11;   // the end of a row: on to the next one
	CHECK(CpSwatchMove(counts, g, i, CpNavRight) && g == CpPalette && i == 12);
	CHECK(CpSwatchMove(counts, g, i, CpNavLeft) && g == CpPalette && i == 11);
	g = CpPalette; i = 0;   // the very first stays
	CHECK(CpSwatchMove(counts, g, i, CpNavLeft) && g == CpPalette && i == 0);
	CHECK(CpSwatchMove(counts, g, i, CpNavUp) && g == CpPalette && i == 0);
	i = 17;   // down from the palette's second row into the custom row, same column
	CHECK(CpSwatchMove(counts, g, i, CpNavDown) && g == CpCustom && i == 5);
	// down into a short row: its last cell
	CHECK(CpSwatchMove(counts, g, i, CpNavDown) && g == CpRecent && i == 2);
	CHECK(CpSwatchMove(counts, g, i, CpNavDown) && g == CpRecent && i == 2);   // the last row stays
	CHECK(CpSwatchMove(counts, g, i, CpNavRight) && g == CpRecent && i == 2);   // the very last stays
	CHECK(CpSwatchMove(counts, g, i, CpNavUp) && g == CpCustom && i == 2);
	CHECK(CpSwatchMove(counts, g, i, CpNavEnd) && g == CpCustom && i == 11);
	CHECK(CpSwatchMove(counts, g, i, CpNavHome) && g == CpCustom && i == 0);
	CHECK(CpSwatchMove(counts, g, i, CpNavLast) && g == CpRecent && i == 2);
	CHECK(CpSwatchMove(counts, g, i, CpNavFirst) && g == CpPalette && i == 0);
	// a cell that's gone (the recent colors shrank): to the first
	g = CpRecent; i = 7;
	CHECK(CpSwatchMove(counts, g, i, CpNavRight) && g == CpPalette && i == 0);
	// no recent: the custom row is the last
	int noRecent[CpGroupCount] = { 24, 12, 0 };
	g = CpCustom; i = 4;
	CHECK(CpSwatchMove(noRecent, g, i, CpNavDown) && g == CpCustom && i == 4);
	// nothing at all
	int none[CpGroupCount] = { 0, 0, 0 };
	CHECK(!CpSwatchMove(none, g, i, CpNavRight));
}

int wmain() {
	Run_HsvToRgb_Primaries();
	Run_RgbToHsv_Values();
	Run_RgbToHsv_KeepsUndefinedParts();
	Run_RoundTrip_RgbHsvRgb();
	Run_UpdateHsv_KeepsPrevWhenSameColor();
	Run_Normalize();
	Run_Hex_Format();
	Run_Hex_Parse();
	Run_Layout_WithAndWithoutAlpha();
	Run_Layout_TooSmall();
	Run_Mapping_Sv();
	Run_Mapping_Bars();
	Run_Pixels_Sv();
	Run_Pixels_HueAndAlpha();
	Run_Pixels_FillFrameCopy();
	Run_Pixels_Ring();
	Run_Panel_Layout();
	Run_Panel_Pieces();
	Run_Panel_EyedropperAndGroups();
	Run_Recent();
	Run_Magnifier_Placement();
	Run_DropDown_And_Center();
	Run_Panel_Height();
	Run_Swatch_Navigation();
	Run_Fields_Text();
	Run_Fields_Parse();
	Run_Fields_Apply();
	Run_Fields_Step();
	Run_Pixels_Swatch();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
