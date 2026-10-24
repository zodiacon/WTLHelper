#include "ColorPickerModel.h"

#include <algorithm>
#include <cmath>

// ---- Colors ------------------------------------------------------------------------

static BYTE ToByte(double x) {
	return (BYTE)std::lround(std::clamp(x, 0.0, 1.0) * 255);
}

// the hue's color at full saturation and value, channels 0..1
static void HueColor(double h, double rgb[3]) {
	h = std::fmod(h, 360.0);
	if (h < 0)
		h += 360;
	double x = h / 60;
	int sector = std::min((int)x, 5);
	double f = x - sector;
	switch (sector) {
		case 0: rgb[0] = 1;     rgb[1] = f;     rgb[2] = 0;     break;
		case 1: rgb[0] = 1 - f; rgb[1] = 1;     rgb[2] = 0;     break;
		case 2: rgb[0] = 0;     rgb[1] = 1;     rgb[2] = f;     break;
		case 3: rgb[0] = 0;     rgb[1] = 1 - f; rgb[2] = 1;     break;
		case 4: rgb[0] = f;     rgb[1] = 0;     rgb[2] = 1;     break;
		default: rgb[0] = 1;    rgb[1] = 0;     rgb[2] = 1 - f; break;
	}
}

// a channel of the color with the hue's channel hc: v at the top of the range, v * (1 - s) at the bottom
static double Channel(double hc, double s, double v) {
	return v * (1 - s * (1 - hc));
}

COLORREF CpHsvToRgb(const CpHsv& hsv) {
	auto c = CpNormalize(hsv);
	double rgb[3];
	HueColor(c.h, rgb);
	return RGB(ToByte(Channel(rgb[0], c.s, c.v)), ToByte(Channel(rgb[1], c.s, c.v)), ToByte(Channel(rgb[2], c.s, c.v)));
}

CpHsv CpRgbToHsv(COLORREF color, const CpHsv& prev) {
	double r = GetRValue(color) / 255.0, g = GetGValue(color) / 255.0, b = GetBValue(color) / 255.0;
	double max = std::max({ r, g, b }), min = std::min({ r, g, b });
	double delta = max - min;
	auto p = CpNormalize(prev);
	CpHsv hsv;
	hsv.v = max;
	if (max == 0) {
		// black: any hue and saturation
		hsv.h = p.h;
		hsv.s = p.s;
		return hsv;
	}
	hsv.s = delta / max;
	if (delta == 0) {
		hsv.h = p.h;   // a gray: any hue
		return hsv;
	}
	double h;
	if (max == r)
		h = (g - b) / delta;
	else if (max == g)
		h = (b - r) / delta + 2;
	else
		h = (r - g) / delta + 4;
	h *= 60;
	if (h < 0)
		h += 360;
	// red at the bottom of the hue bar stays there
	if (h == 0 && p.h == 360)
		h = 360;
	hsv.h = h;
	return hsv;
}

CpHsv CpUpdateHsv(const CpHsv& prev, COLORREF color) {
	auto p = CpNormalize(prev);
	return CpHsvToRgb(p) == color ? p : CpRgbToHsv(color, p);
}

CpHsv CpNormalize(const CpHsv& hsv) {
	CpHsv c = hsv;
	if (std::isnan(c.h))
		c.h = 0;
	if (c.h < 0 || c.h > 360) {
		c.h = std::fmod(c.h, 360.0);
		if (c.h < 0)
			c.h += 360;
	}
	c.s = std::isnan(c.s) ? 0 : std::clamp(c.s, 0.0, 1.0);
	c.v = std::isnan(c.v) ? 0 : std::clamp(c.v, 0.0, 1.0);
	return c;
}

std::wstring CpFormatHex(COLORREF color, BYTE alpha, bool withAlpha) {
	wchar_t text[16];
	if (withAlpha)
		swprintf_s(text, L"#%02X%02X%02X%02X", alpha, GetRValue(color), GetGValue(color), GetBValue(color));
	else
		swprintf_s(text, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
	return text;
}

static int HexDigit(wchar_t ch) {
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	if (ch >= L'a' && ch <= L'f')
		return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'F')
		return ch - L'A' + 10;
	return -1;
}

bool CpParseHex(std::wstring_view text, COLORREF* pColor, BYTE* pAlpha, bool* pHasAlpha) {
	while (!text.empty() && iswspace(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && iswspace(text.back()))
		text.remove_suffix(1);
	if (!text.empty() && text.front() == L'#')
		text.remove_prefix(1);
	if (text.size() != 3 && text.size() != 6 && text.size() != 8)
		return false;
	BYTE digits[8];
	for (size_t i = 0; i < text.size(); i++) {
		int d = HexDigit(text[i]);
		if (d < 0)
			return false;
		digits[i] = (BYTE)d;
	}
	BYTE alpha = 255, r, g, b;
	bool hasAlpha = text.size() == 8;
	if (text.size() == 3) {
		r = digits[0] * 17;
		g = digits[1] * 17;
		b = digits[2] * 17;
	}
	else {
		auto pair = [&](size_t i) { return (BYTE)(digits[i] << 4 | digits[i + 1]); };
		size_t i = 0;
		if (hasAlpha) {
			alpha = pair(0);
			i = 2;
		}
		r = pair(i);
		g = pair(i + 2);
		b = pair(i + 4);
	}
	if (pColor)
		*pColor = RGB(r, g, b);
	if (pAlpha && hasAlpha)
		*pAlpha = alpha;
	if (pHasAlpha)
		*pHasAlpha = hasAlpha;
	return true;
}

// ---- Layout --------------------------------------------------------------------------

CpLayout CpComputeLayout(int width, int height, const CpMetrics& m, bool alpha) {
	CpLayout l;
	int top = m.pad, bottom = std::max(top, height - m.pad);
	int right = std::max(m.pad, width - m.pad);
	auto bar = [&](RECT& rc) {
		int left = std::max(m.pad, right - m.barWidth);
		rc = { left + 1, top + 1, std::max(left + 1, right - 1), std::max(top + 1, bottom - 1) };
		right = left - m.gap;
	};
	if (alpha)
		bar(l.alpha);
	bar(l.hue);
	l.sv = { m.pad + 1, top + 1, std::max(m.pad + 1, right - 1), std::max(top + 1, bottom - 1) };
	return l;
}

int CpPanelWidth(const CpPanelMetrics& m) {
	return 2 * (m.labelWidth + m.fieldWidth) + m.colGap;
}

CpPanel CpComputePanel(int left, int top, const CpPanelMetrics& m, bool swatch, bool fields, bool alpha) {
	CpPanel p;
	if (swatch) {
		p.swatch = { left, top, left + CpPanelWidth(m), top + m.swatchHeight };
		top += m.swatchHeight + m.swatchGap;
	}
	if (!fields)
		return p;
	for (int f = 0; f < CpFieldCount; f++) {
		if (f == CpA && !alpha)
			continue;
		int col = f < CpR ? 0 : 1, row = f < CpR ? f : f - CpR;
		int x = left + col * (m.labelWidth + m.fieldWidth + m.colGap);
		int y = top + row * (m.fieldHeight + m.rowGap);
		p.label[f] = { x, y, x + m.labelWidth, y + m.fieldHeight };
		p.frame[f] = { x + m.labelWidth, y, x + m.labelWidth + m.fieldWidth, y + m.fieldHeight };
	}
	return p;
}

bool CpHasSpin(CpField field) {
	return field != CpHex;
}

void CpSpinRects(const RECT& frame, int spinWidth, RECT* pUp, RECT* pDown) {
	RECT rc{ std::max(frame.left + 1, frame.right - 1 - spinWidth), frame.top + 1, frame.right - 1, frame.bottom - 1 };
	int mid = (rc.top + rc.bottom) / 2;
	*pUp = { rc.left, rc.top, rc.right, mid };
	*pDown = { rc.left, mid, rc.right, rc.bottom };
}

RECT CpEditRect(const RECT& frame, CpField field, int spinWidth, int textHeight, int padX) {
	int right = frame.right - 1 - (CpHasSpin(field) ? spinWidth : 0) - 1;
	int top = frame.top + (frame.bottom - frame.top - textHeight) / 2;
	RECT rc{ frame.left + 1 + padX, top, right, top + textHeight };
	rc.right = std::max(rc.left, rc.right);
	return rc;
}

void CpSwatchRects(const RECT& swatch, bool hasOriginal, RECT* pNew, RECT* pOld) {
	*pNew = swatch;
	*pOld = {};
	if (hasOriginal) {
		pNew->bottom = (swatch.top + swatch.bottom) / 2;
		*pOld = { swatch.left, pNew->bottom, swatch.right, swatch.bottom };
	}
}

// ---- Fields --------------------------------------------------------------------------

bool CpParseInt(std::wstring_view text, int* pValue) {
	while (!text.empty() && iswspace(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && iswspace(text.back()))
		text.remove_suffix(1);
	bool negative = !text.empty() && text.front() == L'-';
	if (negative)
		text.remove_prefix(1);
	if (text.empty() || text.size() > 9)
		return false;
	int value = 0;
	for (auto ch : text) {
		if (ch < L'0' || ch > L'9')
			return false;
		value = value * 10 + (ch - L'0');
	}
	*pValue = negative ? -value : value;
	return true;
}

static int ShownValue(CpField field, const CpHsv& hsv, BYTE alpha) {
	auto rgb = CpHsvToRgb(hsv);
	switch (field) {
		case CpH: return (int)std::lround(hsv.h);
		case CpS: return (int)std::lround(hsv.s * 100);
		case CpV: return (int)std::lround(hsv.v * 100);
		case CpA: return alpha;
		case CpR: return GetRValue(rgb);
		case CpG: return GetGValue(rgb);
		case CpB: return GetBValue(rgb);
	}
	return 0;
}

// a whole value for a numeric field, clamped
static void ApplyValue(CpField field, int value, CpHsv& hsv, BYTE& alpha) {
	// the value shown already: nothing changes (the fraction behind it stays)
	if (value == ShownValue(field, hsv, alpha))
		return;
	auto rgb = CpHsvToRgb(hsv);
	BYTE r = GetRValue(rgb), g = GetGValue(rgb), b = GetBValue(rgb);
	switch (field) {
		case CpH: hsv.h = std::clamp(value, 0, 360); return;
		case CpS: hsv.s = std::clamp(value, 0, 100) / 100.0; return;
		case CpV: hsv.v = std::clamp(value, 0, 100) / 100.0; return;
		case CpA: alpha = (BYTE)std::clamp(value, 0, 255); return;
		case CpR: r = (BYTE)std::clamp(value, 0, 255); break;
		case CpG: g = (BYTE)std::clamp(value, 0, 255); break;
		case CpB: b = (BYTE)std::clamp(value, 0, 255); break;
		default: return;
	}
	hsv = CpUpdateHsv(hsv, RGB(r, g, b));
}

std::wstring CpFieldText(CpField field, const CpHsv& hsv, BYTE alpha) {
	if (field == CpHex)
		return CpFormatHex(CpHsvToRgb(hsv));
	return std::to_wstring(ShownValue(field, hsv, alpha));
}

bool CpApplyField(CpField field, std::wstring_view text, CpHsv& hsv, BYTE& alpha) {
	if (field == CpHex) {
		COLORREF color;
		BYTE a = alpha;
		if (!CpParseHex(text, &color, &a))
			return false;
		hsv = CpUpdateHsv(hsv, color);
		alpha = a;
		return true;
	}
	int value;
	if (field < 0 || field >= CpFieldCount || !CpParseInt(text, &value))
		return false;
	ApplyValue(field, value, hsv, alpha);
	return true;
}

bool CpStepField(CpField field, int delta, CpHsv& hsv, BYTE& alpha) {
	if (field == CpHex || field < 0 || field >= CpFieldCount)
		return false;
	ApplyValue(field, ShownValue(field, hsv, alpha) + delta, hsv, alpha);
	return true;
}

static double Fraction(int pos, int start, int size) {
	return size <= 1 ? 0 : std::clamp((double)(pos - start) / (size - 1), 0.0, 1.0);
}

static int Position(double t, int start, int size) {
	return start + (size <= 1 ? 0 : (int)std::lround(std::clamp(t, 0.0, 1.0) * (size - 1)));
}

void CpSvFromPoint(const RECT& rc, POINT pt, double* pS, double* pV) {
	*pS = Fraction(pt.x, rc.left, rc.right - rc.left);
	*pV = 1 - Fraction(pt.y, rc.top, rc.bottom - rc.top);
}

POINT CpPointFromSv(const RECT& rc, double s, double v) {
	return { Position(s, rc.left, rc.right - rc.left), Position(1 - v, rc.top, rc.bottom - rc.top) };
}

double CpBarFromY(const RECT& rc, int y) {
	return Fraction(y, rc.top, rc.bottom - rc.top);
}

int CpYFromBar(const RECT& rc, double t) {
	return Position(t, rc.top, rc.bottom - rc.top);
}

BYTE CpAlphaFromBar(double t) {
	return ToByte(1 - t);
}

double CpBarFromAlpha(BYTE alpha) {
	return 1 - alpha / 255.0;
}

// ---- Pixels ------------------------------------------------------------------------

uint32_t CpBlend(uint32_t b, uint32_t a, double alpha) {
	auto mix = [&](int shift) {
		double x = (b >> shift) & 0xFF, y = (a >> shift) & 0xFF;
		return (uint32_t)std::lround(x + (y - x) * alpha) << shift;
	};
	return mix(16) | mix(8) | mix(0);
}

void CpRenderSv(const CpPixels& px, double hue) {
	double hc[3];
	HueColor(hue, hc);
	for (int y = 0; y < px.height; y++) {
		double v = 1 - Fraction(y, 0, px.height);
		for (int x = 0; x < px.width; x++) {
			double s = Fraction(x, 0, px.width);
			px.At(x, y) = (uint32_t)ToByte(Channel(hc[0], s, v)) << 16 | (uint32_t)ToByte(Channel(hc[1], s, v)) << 8 |
				ToByte(Channel(hc[2], s, v));
		}
	}
}

void CpRenderHue(const CpPixels& px) {
	for (int y = 0; y < px.height; y++) {
		auto p = CpPixel(CpHsvToRgb({ CpHueFromBar(Fraction(y, 0, px.height)), 1, 1 }));
		std::fill_n(&px.At(0, y), px.width, p);
	}
}

uint32_t CpChecker(int x, int y, int cell, COLORREF check1, COLORREF check2) {
	cell = std::max(1, cell);
	return CpPixel(((x / cell + y / cell) & 1) ? check2 : check1);
}

void CpRenderAlpha(const CpPixels& px, COLORREF color, int cell, COLORREF check1, COLORREF check2) {
	auto p = CpPixel(color);
	for (int y = 0; y < px.height; y++) {
		double a = 1 - Fraction(y, 0, px.height);
		for (int x = 0; x < px.width; x++)
			px.At(x, y) = CpBlend(CpChecker(x, y, cell, check1, check2), p, a);
	}
}

void CpRenderSwatch(const CpPixels& px, COLORREF color, BYTE alpha, int cell, COLORREF check1, COLORREF check2) {
	auto p = CpPixel(color);
	int half = px.width / 2;
	for (int y = 0; y < px.height; y++) {
		for (int x = 0; x < px.width; x++)
			px.At(x, y) = x < half ? p : CpBlend(CpChecker(x - half, y, cell, check1, check2), p, alpha / 255.0);
	}
}

static RECT Clip(const CpPixels& px, const RECT& rc) {
	return { std::max(0L, rc.left), std::max(0L, rc.top), std::min((LONG)px.width, rc.right), std::min((LONG)px.height, rc.bottom) };
}

void CpFillRect(const CpPixels& px, const RECT& rc, COLORREF color) {
	auto c = Clip(px, rc);
	auto p = CpPixel(color);
	for (int y = c.top; y < c.bottom; y++) {
		if (c.right > c.left)
			std::fill_n(&px.At(c.left, y), c.right - c.left, p);
	}
}

void CpFrameRect(const CpPixels& px, const RECT& rc, COLORREF color, int thickness) {
	if (rc.right - rc.left <= 2 * thickness || rc.bottom - rc.top <= 2 * thickness) {
		CpFillRect(px, rc, color);
		return;
	}
	CpFillRect(px, { rc.left, rc.top, rc.right, rc.top + thickness }, color);
	CpFillRect(px, { rc.left, rc.bottom - thickness, rc.right, rc.bottom }, color);
	CpFillRect(px, { rc.left, rc.top + thickness, rc.left + thickness, rc.bottom - thickness }, color);
	CpFillRect(px, { rc.right - thickness, rc.top + thickness, rc.right, rc.bottom - thickness }, color);
}

void CpCopy(const CpPixels& dst, int x, int y, const CpPixels& src) {
	auto c = Clip(dst, { x, y, x + src.width, y + src.height });
	for (int row = c.top; row < c.bottom; row++) {
		if (c.right > c.left)
			std::copy_n(&src.At(c.left - x, row - y), c.right - c.left, &dst.At(c.left, row));
	}
}

void CpDrawRing(const CpPixels& px, double cx, double cy, double radius, double thickness, COLORREF color) {
	double reach = radius + thickness / 2 + 1;
	int x0 = std::max(0, (int)std::floor(cx - reach)), x1 = std::min(px.width, (int)std::ceil(cx + reach));
	int y0 = std::max(0, (int)std::floor(cy - reach)), y1 = std::min(px.height, (int)std::ceil(cy + reach));
	auto p = CpPixel(color);
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
			double coverage = std::clamp(thickness / 2 + 0.5 - std::abs(d - radius), 0.0, 1.0);
			if (coverage > 0)
				px.At(x, y) = CpBlend(px.At(x, y), p, coverage);
		}
	}
}
