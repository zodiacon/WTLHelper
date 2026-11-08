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

// A log axis's range in decades: 10^-300 to 10^300.
constexpr double MaxLog = 300;

void ClampRange(double& lo, double& hi, double anchor, bool log) {
	double range = hi - lo;
	double minRange = MinRelativeRange * std::max({ 1.0, std::abs(lo), std::abs(hi) });
	double maxRange = log ? 2 * MaxLog : MaxRange;
	if (range < minRange || range > maxRange) {
		double target = range < minRange ? minRange : maxRange;
		double share = range > 0 ? (anchor - lo) / range : 0.5;
		lo = anchor - share * target;
		hi = lo + target;
	}
	if (log) {
		// slid back inside the doubles' range
		if (lo < -MaxLog)
			hi += -MaxLog - lo, lo = -MaxLog;
		if (hi > MaxLog)
			lo -= hi - MaxLog, hi = MaxLog;
	}
}

} // namespace

// ---- The view --------------------------------------------------------------------------------------------------------

bool FplViewport::Valid() const {
	return std::isfinite(xMin) && std::isfinite(xMax) && std::isfinite(yMin) && std::isfinite(yMax) && xMax > xMin &&
		yMax > yMin && width > 0 && height > 0 && std::isfinite(width) && std::isfinite(height) && (!logX || xMin > 0) &&
		(!logY || yMin > 0);
}

void FplViewport::FixLogRanges() {
	auto fix = [](bool log, double& lo, double& hi) {
		if (!log || (lo > 0 && hi > lo && std::isfinite(hi)))
			return;
		if (!(hi > 0) || !std::isfinite(hi))
			hi = 100;
		// at least 3 decades below the top
		lo = std::min(hi / 1000, lo > 0 ? lo : hi / 1000);
		double a = std::log10(lo), b = std::log10(hi);
		ClampRange(a, b, b, true);
		lo = std::pow(10.0, a);
		hi = std::pow(10.0, b);
	};
	fix(logX, xMin, xMax);
	fix(logY, yMin, yMax);
}

// The work is done in axis units (logarithms on a log axis), then the ranges set back from them.
void FplViewport::Zoom(double factorX, double factorY, FplPoint screen) {
	if (!(factorX > 0) || !(factorY > 0))
		return;
	double x0 = AxisX(xMin), x1 = AxisX(xMax), y0 = AxisY(yMin), y1 = AxisY(yMax);
	double px = x0 + screen.x / width * (x1 - x0), py = y1 - screen.y / height * (y1 - y0);
	double xRange = (x1 - x0) / factorX, yRange = (y1 - y0) / factorY;
	x0 = px - screen.x / width * xRange;
	x1 = x0 + xRange;
	y1 = py + screen.y / height * yRange;
	y0 = y1 - yRange;
	ClampRange(x0, x1, px, logX);
	ClampRange(y0, y1, py, logY);
	xMin = FromAxisX(x0), xMax = FromAxisX(x1), yMin = FromAxisY(y0), yMax = FromAxisY(y1);
}

void FplViewport::Pan(double dxScreen, double dyScreen) {
	double dx = dxScreen / ScaleX(), dy = dyScreen / ScaleY();
	double x0 = AxisX(xMin) - dx, x1 = AxisX(xMax) - dx, y0 = AxisY(yMin) + dy, y1 = AxisY(yMax) + dy;
	if (logX)
		ClampRange(x0, x1, (x0 + x1) / 2, true);
	if (logY)
		ClampRange(y0, y1, (y0 + y1) / 2, true);
	xMin = FromAxisX(x0), xMax = FromAxisX(x1), yMin = FromAxisY(y0), yMax = FromAxisY(y1);
}

void FplViewport::Resize(double newWidth, double newHeight) {
	if (!(newWidth > 0) || !(newHeight > 0))
		return;
	double sx = ScaleX(), sy = ScaleY();
	double cx = (AxisX(xMin) + AxisX(xMax)) / 2, cy = (AxisY(yMin) + AxisY(yMax)) / 2;
	width = newWidth;
	height = newHeight;
	double x0 = cx - width / sx / 2, x1 = cx + width / sx / 2, y0 = cy - height / sy / 2, y1 = cy + height / sy / 2;
	if (logX)
		ClampRange(x0, x1, cx, true);
	if (logY)
		ClampRange(y0, y1, cy, true);
	xMin = FromAxisX(x0), xMax = FromAxisX(x1), yMin = FromAxisY(y0), yMax = FromAxisY(y1);
}

void FplViewport::EqualScale() {
	// a log axis's units aren't the plane's: no equal scales
	if (logX || logY)
		return;
	double s = std::min(ScaleX(), ScaleY());
	double cx = (xMin + xMax) / 2, cy = (yMin + yMax) / 2;
	xMin = cx - width / s / 2;
	xMax = cx + width / s / 2;
	yMin = cy - height / s / 2;
	yMax = cy + height / s / 2;
}

bool FplViewport::ZoomTo(FplPoint a, FplPoint b) {
	if (std::abs(a.x - b.x) < 1 || std::abs(a.y - b.y) < 1)
		return false;
	FplViewport next = *this;
	next.xMin = ToPlaneX(std::min(a.x, b.x));
	next.xMax = ToPlaneX(std::max(a.x, b.x));
	next.yMin = ToPlaneY(std::max(a.y, b.y));
	next.yMax = ToPlaneY(std::min(a.y, b.y));
	if (!next.Valid())
		return false;
	*this = next;
	return true;
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

FplTicks FplMakeLogTicks(double min, double max, double pixels, double minSpacing) {
	FplTicks t;
	if (!(min > 0) || !std::isfinite(max) || !(max > min) || !(pixels > 0) || !(minSpacing > 0))
		return t;
	const double a = std::log10(min), b = std::log10(max), decades = b - a;
	if (decades < 1) {
		// the linear ticks, spaced for the range's top, where the log scale packs them closest
		const double dipsPerUnit = pixels / (decades * max * 2.302585092994046);
		return FplMakeTicks(min, max, dipsPerUnit * (max - min), minSpacing);
	}
	const double perDecade = pixels / decades;
	// every 1, 2, 5, 10, 20... decades
	double every = 1;
	for (int k = 0; every * perDecade < minSpacing && k < 30; k++)
		every *= (k % 3 == 1) ? 2.5 : 2;
	t.log = true;
	t.step = every;
	const double k0 = std::ceil(a / every - 1e-9), k1 = std::floor(b / every + 1e-9);
	for (double k = k0; k <= k1; k++)
		t.major.push_back(std::pow(10.0, k * every));
	if (every == 1) {
		// 2 to 9 times each decade, where they're at least a few DIPs apart (9 to 10 is the closest)
		if (perDecade * std::log10(10.0 / 9) >= 3) {
			for (double e = std::floor(a); e <= std::ceil(b); e++)
				for (int m = 2; m <= 9; m++) {
					double v = m * std::pow(10.0, e);
					if (v >= min && v <= max)
						t.minor.push_back(v);
				}
		}
	}
	else {
		for (double e = std::ceil(a - 1e-9); e <= std::floor(b + 1e-9); e++)
			if (std::fmod(e, every) != 0)
				t.minor.push_back(std::pow(10.0, e));
	}
	return t;
}

FplTicks FplMakeTicksX(const FplViewport& view, double minSpacing, bool pi) {
	return view.logX ? FplMakeLogTicks(view.xMin, view.xMax, view.width, minSpacing)
		: FplMakeTicks(view.xMin, view.xMax, view.width, minSpacing, pi);
}

FplTicks FplMakeTicksY(const FplViewport& view, double minSpacing) {
	return view.logY ? FplMakeLogTicks(view.yMin, view.yMax, view.height, minSpacing)
		: FplMakeTicks(view.yMin, view.yMax, view.height, minSpacing);
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
	if (ticks.log) {
		if (!(value > 0))
			return {};
		int e = static_cast<int>(std::lround(std::log10(value)));
		if (e >= -2 && e <= 3)
			return Fixed(std::pow(10.0, e), std::max(0, -e));
		return L"10" + Superscript(e);
	}
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

namespace {

// The nice angle at least this big, in degrees, at most 30.
double NiceAngle(double degrees) {
	if (degrees >= 15)
		return 30;
	if (degrees >= 10)
		return 15;
	const double power = std::pow(10.0, std::floor(std::log10(degrees)));
	const double m = degrees / power;
	return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * power;
}

// How far a ray from the point goes before it leaves the view.
double RoomAlong(FplPoint from, double dx, double dy, double width, double height) {
	double room = 1e300;
	if (dx > 0)
		room = std::min(room, (width - from.x) / dx);
	else if (dx < 0)
		room = std::min(room, -from.x / dx);
	if (dy > 0)
		room = std::min(room, (height - from.y) / dy);
	else if (dy < 0)
		room = std::min(room, -from.y / dy);
	return std::max(room, 0.0);
}

}  // namespace

FplPolarGrid FplMakePolarGrid(const FplViewport& view, double minSpacing) {
	FplPolarGrid grid;
	if (!view.Valid() || view.logX || view.logY)
		return grid;
	const double Tau = 2 * std::numbers::pi;
	const double w = view.width, h = view.height;
	const double sx = view.ScaleX(), sy = view.ScaleY();
	const double corners[4][2] = { { view.xMin, view.yMin }, { view.xMax, view.yMin }, { view.xMax, view.yMax }, { view.xMin, view.yMax } };
	double rMax = 0;
	for (auto& c : corners)
		rMax = std::max(rMax, std::hypot(c[0], c[1]));
	const double outX = std::max({ view.xMin, -view.xMax, 0.0 }), outY = std::max({ view.yMin, -view.yMax, 0.0 });
	const double rMin = std::hypot(outX, outY);
	const bool originInView = rMin == 0;
	if (!(rMax > rMin))
		return grid;

	// the angles in view, around the view's middle's as seen from the origin
	double from = 0, to = Tau;
	if (!originInView) {
		const double middle = std::atan2((view.yMin + view.yMax) / 2, (view.xMin + view.xMax) / 2);
		double lo = 0, hi = 0;
		for (auto& c : corners) {
			double a = std::atan2(c[1], c[0]) - middle;
			a = std::remainder(a, Tau);
			lo = std::min(lo, a);
			hi = std::max(hi, a);
		}
		from = middle + lo;
		to = middle + hi;
	}

	const double dips = std::min(sx, sy);
	grid.radii = FplMakeTicks(rMin, rMax, (rMax - rMin) * dips, minSpacing);
	auto toScreen = [&](double r, double angle) -> FplPoint {
		return { view.ToScreenX(r * std::cos(angle)), view.ToScreenY(r * std::sin(angle)) };
	};
	auto rings = [&](const std::vector<double>& radii, std::vector<FplPolyline>& out) {
		for (double r : radii) {
			if (r <= 0)
				continue;
			// chords off the circle by at most a tenth of a DIP
			const double big = r * std::max(sx, sy);
			const double step = std::min(std::sqrt(0.8 / big), Tau / 32);
			const int n = std::clamp(static_cast<int>(std::ceil((to - from) / step)), 2, 4096);
			FplPolyline line;
			line.reserve(n + 1);
			for (int i = 0; i <= n; i++)
				line.push_back(toScreen(r, from + (to - from) * i / n));
			auto cut = FplClip({ line }, 0, 0, w, h);
			out.insert(out.end(), std::make_move_iterator(cut.begin()), std::make_move_iterator(cut.end()));
		}
	};
	rings(grid.radii.major, grid.majorRings);
	rings(grid.radii.minor, grid.minorRings);

	// the spokes: the minor ones only where they aren't crowded
	const double degrees = NiceAngle((to - from) * 180 / std::numbers::pi / 12);
	grid.angleStep = degrees * std::numbers::pi / 180;
	const double minorFrom = std::max(rMin, 24 / (grid.angleStep / 2) / dips);
	const long long first = static_cast<long long>(std::ceil(from / grid.angleStep - 1e-9));
	const long long last = static_cast<long long>(std::floor(to / grid.angleStep + 1e-9)) - (originInView ? 1 : 0);
	for (long long k = first - 1; k <= last; k++) {
		const double angle = k * grid.angleStep;
		if (k >= first) {
			auto cut = FplClip({ { toScreen(rMin, angle), toScreen(rMax, angle) } }, 0, 0, w, h);
			if (!cut.empty() && cut[0].size() >= 2) {
				// its label, in from where it leaves the view
				const FplPoint a = cut[0].front(), b = cut[0].back();
				const double len = std::hypot(b.x - a.x, b.y - a.y);
				const FplPoint origin = toScreen(0, 0);
				const bool bFar = std::hypot(b.x - origin.x, b.y - origin.y) >= std::hypot(a.x - origin.x, a.y - origin.y);
				const FplPoint end = bFar ? b : a, start = bFar ? a : b;
				// far enough in for a label's text to fit, whichever edge it's near
				for (double d = 12; d < len - 12; d += 4) {
					const FplPoint at = { end.x + (start.x - end.x) / len * d, end.y + (start.y - end.y) / len * d };
					if (at.x >= 28 && at.x <= w - 28 && at.y >= 11 && at.y <= h - 11) {
						double normal = std::fmod(angle, Tau);
						grid.angleLabels.push_back({ at, normal < 0 ? normal + Tau : normal });
						break;
					}
				}
				grid.majorSpokes.push_back(cut[0]);
			}
		}
		const double half = (k + 0.5) * grid.angleStep;
		if (half >= from && half <= to && minorFrom < rMax) {
			auto cut = FplClip({ { toScreen(minorFrom, half), toScreen(rMax, half) } }, 0, 0, w, h);
			if (!cut.empty() && cut[0].size() >= 2)
				grid.minorSpokes.push_back(cut[0]);
		}
	}

	// the radii along a direction with room: the positive x axis if it's in view for long enough
	double labelAngle = (from + to) / 2;
	if (originInView) {
		const FplPoint origin = toScreen(0, 0);
		const double room[4] = { RoomAlong(origin, 1, 0, w, h), RoomAlong(origin, 0, -1, w, h), RoomAlong(origin, -1, 0, w, h), RoomAlong(origin, 0, 1, w, h) };
		const double most = *std::max_element(room, room + 4);
		for (int i = 0; i < 4; i++)
			if (room[i] >= 0.6 * most) {
				labelAngle = i * std::numbers::pi / 2;
				break;
			}
	}
	for (double r : grid.radii.major) {
		if (r <= 0)
			continue;
		const FplPoint at = toScreen(r, labelAngle);
		if (at.x >= 12 && at.x <= w - 12 && at.y >= 10 && at.y <= h - 10)
			grid.radiusLabels.push_back({ at, r });
	}
	return grid;
}

std::wstring FplFormatAngle(double radians, bool pi) {
	const double Tau = 2 * std::numbers::pi;
	double a = std::fmod(radians, Tau);
	if (a < 0)
		a += Tau;
	if (pi) {
		// whole degrees are fractions of pi
		const long long degrees = std::llround(a / (std::numbers::pi / 180));
		if (std::abs(a - degrees * std::numbers::pi / 180) < 1e-9) {
			if (degrees == 0 || degrees == 360)
				return L"0";
			const long long d = std::gcd(degrees, 180LL);
			const long long num = degrees / d, den = 180 / d;
			std::wstring s = num == 1 ? L"" : std::to_wstring(num);
			s += wchar_t(0x03C0);
			if (den != 1)
				s += L"/" + std::to_wstring(den);
			return s;
		}
	}
	double value = pi ? a : a * 180 / std::numbers::pi;
	if (!pi && std::abs(value - 360) < 1e-9)
		value = 0;
	wchar_t text[32];
	swprintf_s(text, L"%.6g", std::round(value * 1e6) / 1e6);
	std::wstring s = text;
	if (!pi)
		s += wchar_t(0x00B0);
	return s;
}

bool FplArrowHead(const FplArrow& a, FplPoint& left, FplPoint& right) {
	const double len = std::hypot(a.to.x - a.from.x, a.to.y - a.from.y);
	if (!(len > 0))
		return false;
	const double ux = (a.to.x - a.from.x) / len, uy = (a.to.y - a.from.y) / len, head = std::min(7.0, 0.35 * len);
	const double c = std::cos(0.45), s = std::sin(0.45);
	left = { a.to.x - head * (ux * c - uy * s), a.to.y - head * (uy * c + ux * s) };
	right = { a.to.x - head * (ux * c + uy * s), a.to.y - head * (uy * c - ux * s) };
	return true;
}
