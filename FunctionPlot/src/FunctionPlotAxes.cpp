// The view's mapping and the axes' ticks and labels.

#include "FunctionPlotModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>

namespace {

constexpr double Pi = std::numbers::pi;

// A range's limits: zooming stops before the doubles can't tell the ends apart, or past any sensible size.
constexpr double MinRelativeRange = 1e-12;
constexpr double MaxRange = 1e100;

void ClampRange(double& lo, double& hi, double anchor) {
	double range = hi - lo;
	double minRange = MinRelativeRange * std::max({ 1.0, std::abs(lo), std::abs(hi) });
	if (range < minRange || range > MaxRange) {
		double target = range < minRange ? minRange : MaxRange;
		double share = range > 0 ? (anchor - lo) / range : 0.5;
		lo = anchor - share * target;
		hi = lo + target;
	}
}

} // namespace

// ---- The view --------------------------------------------------------------------------------------------------------

bool FplViewport::Valid() const {
	return std::isfinite(xMin) && std::isfinite(xMax) && std::isfinite(yMin) && std::isfinite(yMax) && xMax > xMin &&
		yMax > yMin && width > 0 && height > 0 && std::isfinite(width) && std::isfinite(height);
}

void FplViewport::Zoom(double factorX, double factorY, FplPoint screen) {
	if (!(factorX > 0) || !(factorY > 0))
		return;
	FplPoint p = ToPlane(screen);
	double xRange = (xMax - xMin) / factorX, yRange = (yMax - yMin) / factorY;
	xMin = p.x - screen.x / width * xRange;
	xMax = xMin + xRange;
	yMax = p.y + screen.y / height * yRange;
	yMin = yMax - yRange;
	ClampRange(xMin, xMax, p.x);
	ClampRange(yMin, yMax, p.y);
}

void FplViewport::Pan(double dxScreen, double dyScreen) {
	double dx = dxScreen / ScaleX(), dy = dyScreen / ScaleY();
	xMin -= dx;
	xMax -= dx;
	yMin += dy;
	yMax += dy;
}

void FplViewport::Resize(double newWidth, double newHeight) {
	if (!(newWidth > 0) || !(newHeight > 0))
		return;
	double sx = ScaleX(), sy = ScaleY();
	double cx = (xMin + xMax) / 2, cy = (yMin + yMax) / 2;
	width = newWidth;
	height = newHeight;
	xMin = cx - width / sx / 2;
	xMax = cx + width / sx / 2;
	yMin = cy - height / sy / 2;
	yMax = cy + height / sy / 2;
}

void FplViewport::EqualScale() {
	double s = std::min(ScaleX(), ScaleY());
	double cx = (xMin + xMax) / 2, cy = (yMin + yMax) / 2;
	xMin = cx - width / s / 2;
	xMax = cx + width / s / 2;
	yMin = cy - height / s / 2;
	yMax = cy + height / s / 2;
}

// ---- Ticks -----------------------------------------------------------------------------------------------------------

FplTicks FplMakeTicks(double min, double max, double pixels, double minSpacing, bool pi) {
	FplTicks t;
	if (!std::isfinite(min) || !std::isfinite(max) || !(max > min) || !(pixels > 0) || !(minSpacing > 0))
		return t;
	// a hair less, so a range made to fit a step (2 pi over 12 labels) gets that step
	double raw = (max - min) * minSpacing / pixels * (1 - 1e-9);
	int parts = 5;
	// value of tick k is k * mult / div (div exact, so 3 * 1 / 10 is 0.3 and not 3 * 0.1)
	double mult = 1, div = 1;
	if (pi && raw >= Pi / 12 * (1 - 1e-12)) {
		static const struct { int num, den, parts; } Fractions[] = {
			{ 1, 12, 1 }, { 1, 6, 2 }, { 1, 4, 2 }, { 1, 2, 2 }, { 1, 1, 2 }, { 2, 1, 2 },
		};
		bool found = false;
		for (auto& f : Fractions) {
			if (f.num * Pi / f.den >= raw) {
				t.piNum = f.num;
				t.piDen = f.den;
				parts = f.parts;
				found = true;
				break;
			}
		}
		if (!found) {
			double m = raw / Pi;
			double mag = std::pow(10.0, std::floor(std::log10(m)));
			double n = m / mag;
			int k = n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10;
			if (mag * k > 1e9)
				return t;
			t.piNum = static_cast<int>(std::lround(mag * k));
			t.piDen = 1;
			parts = k == 2 ? 4 : 5;
		}
		t.pi = true;
		mult = t.piNum * Pi;
		div = t.piDen;
	}
	else {
		int e = static_cast<int>(std::floor(std::log10(raw)));
		double mag = std::pow(10.0, e);
		double n = raw / mag;
		int k = n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10;
		parts = k == 2 ? 4 : 5;
		if (e >= 0) {
			mult = k * mag;
		}
		else {
			mult = k;
			div = std::pow(10.0, -e);
		}
	}
	t.step = mult / div;

	auto collect = [&](double m, double d, int every, std::vector<double>& out) {
		double step = m / d;
		double k0 = std::ceil(min / step - 1e-9), k1 = std::floor(max / step + 1e-9);
		if (k1 - k0 > 100000)
			return;
		out.reserve(static_cast<size_t>(std::max(0.0, k1 - k0 + 1)));
		for (double k = k0; k <= k1; k++) {
			if (every > 1 && std::fmod(k, every) == 0)
				continue;
			out.push_back(k == 0 ? 0.0 : k * m / d);
		}
	};
	collect(mult, div, 1, t.major);
	if (parts > 1)
		collect(mult, div * parts, parts, t.minor);
	return t;
}

namespace {

const wchar_t Minus = 0x2212;

std::wstring Superscript(int n) {
	static const wchar_t Digits[] = { 0x2070, 0x00B9, 0x00B2, 0x00B3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079 };
	std::wstring digits = std::to_wstring(std::abs(n)), s;
	if (n < 0)
		s += wchar_t(0x207B);
	for (wchar_t c : digits)
		s += Digits[c - L'0'];
	return s;
}

std::wstring Fixed(double v, int decimals) {
	wchar_t buf[400];
	swprintf_s(buf, L"%.*f", decimals, std::abs(v));
	std::wstring s = buf;
	if (v < 0)
		s.insert(s.begin(), Minus);
	return s;
}

} // namespace

std::wstring FplFormatTick(double value, const FplTicks& ticks) {
	if (!std::isfinite(value) || !(ticks.step > 0))
		return {};
	if (std::abs(value) < ticks.step * 1e-6)
		return L"0";
	if (ticks.pi) {
		long long p = std::llround(value / ticks.step) * ticks.piNum, q = ticks.piDen;
		long long g = std::gcd(p, q);
		p /= g;
		q /= g;
		std::wstring s;
		if (p < 0)
			s += Minus;
		if (std::llabs(p) != 1)
			s += std::to_wstring(std::llabs(p));
		s += wchar_t(0x03C0);
		if (q != 1)
			s += L"/" + std::to_wstring(q);
		return s;
	}
	int se = static_cast<int>(std::floor(std::log10(ticks.step) + 1e-9));
	int decimals = std::max(0, -se);
	double maxAbs = std::abs(value);
	if (!ticks.major.empty())
		maxAbs = std::max({ maxAbs, std::abs(ticks.major.front()), std::abs(ticks.major.back()) });
	int me = static_cast<int>(std::floor(std::log10(maxAbs)));
	int length = (me >= 0 ? me + 1 : 1) + (decimals ? decimals + 1 : 0);
	if (length <= 8)
		return Fixed(value, decimals);
	int e = static_cast<int>(std::floor(std::log10(std::abs(value)) + 1e-12));
	double mantissa = value / std::pow(10.0, e);
	std::wstring m = Fixed(mantissa, std::clamp(e - se, 0, 15));
	// "1.0" and the like with nothing after the point: drop it
	if (m.find(L'.') != std::wstring::npos) {
		while (m.back() == L'0')
			m.pop_back();
		if (m.back() == L'.')
			m.pop_back();
	}
	std::wstring s;
	if (m == L"1")
		s = L"10";
	else if (m.size() == 2 && m[0] == Minus && m[1] == L'1')
		s = std::wstring(1, Minus) + L"10";
	else
		s = m + wchar_t(0x00D7) + L"10";
	return s + Superscript(e);
}
