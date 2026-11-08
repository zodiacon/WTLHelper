// Function plot expressions: the math functions, evaluating a tree, compiling it, and the stack machine.

#include "FunctionPlotExpr.h"

#include <algorithm>
#include <utility>
#include <functional>
#include <memory>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

double Sec(double x) { return 1 / std::cos(x); }
double Csc(double x) { return 1 / std::sin(x); }
double Cot(double x) { return std::cos(x) / std::sin(x); }
double Sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }
double Fact(double x) { return std::tgamma(x + 1); }

double Digamma(double x) {
	if (std::isnan(x) || (x <= 0 && std::floor(x) == x))
		return NaN;
	if (x < 0)	// reflection: digamma(1 - x) - digamma(x) = pi cot(pi x)
		return Digamma(1 - x) - std::numbers::pi / std::tan(std::numbers::pi * x);
	double r = 0;
	while (x < 10) {
		r -= 1 / x;
		x += 1;
	}
	double f = 1 / (x * x);
	return r + std::log(x) - 0.5 / x - f * (1.0 / 12 - f * (1.0 / 120 - f * (1.0 / 252 - f * (1.0 / 240 - f / 132))));
}

double Mod(double a, double b) {
	if (b == 0)
		return NaN;
	return a - b * std::floor(a / b);	// the sign of b, as graphers do
}

double Root(double n, double x) { return FplPow(x, 1 / n); }
double Atan2(double y, double x) { return std::atan2(y, x); }
double Hypot(double a, double b) { return std::hypot(a, b); }

double Min(const double* a, int n) {
	double m = a[0];
	for (int i = 1; i < n; i++) {
		if (std::isnan(a[i]))
			return a[i];
		m = a[i] < m ? a[i] : m;
	}
	return m;
}

double Max(const double* a, int n) {
	double m = a[0];
	for (int i = 1; i < n; i++) {
		if (std::isnan(a[i]))
			return a[i];
		m = a[i] > m ? a[i] : m;
	}
	return m;
}

double Square(double x) {
	const double w = x / (2 * std::numbers::pi);
	if (!std::isfinite(w))
		return std::numeric_limits<double>::quiet_NaN();
	return w - std::floor(w) < 0.5 ? 1 : -1;
}

double Triangle(double x) {
	return 2 / std::numbers::pi * std::asin(std::sin(x));
}

double Sawtooth(double x) {
	const double w = x / (2 * std::numbers::pi);
	return 2 * (w - std::floor(w + 0.5));
}

double Step(double x) {
	return std::isnan(x) ? x : x >= 0 ? 1 : 0;
}

// sin(x)/x and its derivatives, by their series near 0, where the formulas lose everything to rounding.
double Sinc(double x) {
	if (std::abs(x) < 1e-3)
		return 1 - x * x / 6;
	return std::sin(x) / x;
}

double SincD1(double x) {
	if (std::abs(x) < 0.02)
		return -x / 3 + x * x * x / 30;
	return (std::cos(x) - std::sin(x) / x) / x;
}

double SincD2(double x) {
	const double x2 = x * x;
	if (std::abs(x) < 0.1)
		return -1.0 / 3 + x2 / 10 - x2 * x2 / 168;
	return -std::sin(x) / x - 2 * SincD1(x) / x;
}

double Normpdf(double x) {
	return std::exp(-x * x / 2) / std::sqrt(2 * std::numbers::pi);
}

double Normcdf(double x) {
	return std::erfc(-x / std::numbers::sqrt2) / 2;
}

bool NearInteger(double x) {
	return std::abs(x - std::round(x)) < 1e-9;
}

// Gamma(a + 1) / Gamma(b + 1), which overflows only if the answer does.
double GammaRatio(double a, double b) {
	if (a < 170 && b < 170 && a > -170 && b > -170)
		return std::tgamma(a + 1) / std::tgamma(b + 1);
	return std::exp(std::lgamma(a + 1) - std::lgamma(b + 1));
}

double NCr(double n, double r) {
	if (std::isnan(n) || std::isnan(r))
		return std::numeric_limits<double>::quiet_NaN();
	if (NearInteger(n) && NearInteger(r) && n >= 0) {
		n = std::round(n);
		r = std::round(r);
		if (r < 0 || r > n)
			return 0;
		r = std::min(r, n - r);
		double value = 1;
		for (double i = 1; i <= r && std::isfinite(value); i++)
			value = value * (n - r + i) / i;
		return std::round(value);
	}
	return GammaRatio(n, r) / std::tgamma(n - r + 1);
}

double NPr(double n, double r) {
	if (std::isnan(n) || std::isnan(r))
		return std::numeric_limits<double>::quiet_NaN();
	if (NearInteger(n) && NearInteger(r) && n >= 0) {
		n = std::round(n);
		r = std::round(r);
		if (r < 0 || r > n)
			return 0;
		double value = 1;
		for (double i = 0; i < r && std::isfinite(value); i++)
			value *= n - i;
		return value;
	}
	return GammaRatio(n, n - r);
}

double Gcd(double a, double b) {
	a = std::abs(std::round(a));
	b = std::abs(std::round(b));
	if (!(a < 9e15) || !(b < 9e15))
		return std::numeric_limits<double>::quiet_NaN();
	while (b > 0) {
		const double rest = std::fmod(a, b);
		a = b;
		b = rest;
	}
	return a;
}

double Lcm(double a, double b) {
	const double g = Gcd(a, b);
	if (std::isnan(g))
		return g;
	return g == 0 ? 0 : std::abs(std::round(a)) / g * std::abs(std::round(b));
}

// ---- Special functions, over the standard library's, which take non-negative orders and arguments.

bool IsWhole(double v) {
	return std::floor(v) == v;
}

double OddSign(double order) {
	return std::fmod(std::abs(order), 2.0) == 1 ? -1 : 1;
}

double BesselYPositive(double order, double x) {
	return x > 0 ? std::cyl_neumann(order, x) : x == 0 ? -std::numeric_limits<double>::infinity() : NaN;
}

double BesselKPositive(double order, double x) {
	return x > 0 ? std::cyl_bessel_k(order, x) : x == 0 ? std::numeric_limits<double>::infinity() : NaN;
}

double BesselJ(double order, double x) {
	if (std::isnan(order) || std::isnan(x))
		return NaN;
	if (x < 0)
		return IsWhole(order) ? OddSign(order) * BesselJ(order, -x) : NaN;
	if (order >= 0)
		return std::cyl_bessel_j(order, x);
	const double v = -order;
	if (IsWhole(v))
		return OddSign(v) * std::cyl_bessel_j(v, x);
	return std::cos(v * std::numbers::pi) * std::cyl_bessel_j(v, x) - std::sin(v * std::numbers::pi) * BesselYPositive(v, x);
}

double BesselY(double order, double x) {
	if (std::isnan(order) || std::isnan(x))
		return NaN;
	if (order >= 0)
		return BesselYPositive(order, x);
	const double v = -order;
	if (IsWhole(v))
		return OddSign(v) * BesselYPositive(v, x);
	return x > 0 ? std::sin(v * std::numbers::pi) * std::cyl_bessel_j(v, x) + std::cos(v * std::numbers::pi) * BesselYPositive(v, x) : NaN;
}

double BesselI(double order, double x) {
	if (std::isnan(order) || std::isnan(x))
		return NaN;
	if (x < 0)
		return IsWhole(order) ? OddSign(order) * BesselI(order, -x) : NaN;
	const double v = std::abs(order);
	if (order >= 0 || IsWhole(v))
		return std::cyl_bessel_i(v, x);
	return x > 0 ? std::cyl_bessel_i(v, x) + 2 / std::numbers::pi * std::sin(v * std::numbers::pi) * std::cyl_bessel_k(v, x) : NaN;
}

double BesselK(double order, double x) {
	if (std::isnan(order) || std::isnan(x))
		return NaN;
	return BesselKPositive(std::abs(order), x);
}

// Airy's solutions: by their power series near 0 and Bessel functions of order 1/3 or 2/3 away from it.
struct AiryValues {
	double ai, bi;
};

AiryValues Airy(double x, bool derivative) {
	if (std::isnan(x))
		return { NaN, NaN };
	const double third = 1.0 / 3, order = derivative ? 2.0 / 3 : third, root3 = std::sqrt(3.0), pi = std::numbers::pi;
	if (std::abs(x) <= 1) {
		const double c1 = 0.355028053887817239, c2 = 0.258819403792806798;
		// f = 1 + x^3/6 + ..., g = x + x^4/12 + ..., each term from the last by y'' = x y
		double f = 1, g = x, df = 0, dg = 1, termF = 1, termG = x;
		for (int k = 0; k < 20; k++) {
			termF *= x * x * x / ((3.0 * k + 2) * (3.0 * k + 3));
			termG *= x * x * x / ((3.0 * k + 3) * (3.0 * k + 4));
			f += termF;
			g += termG;
			if (x != 0) {
				df += termF * (3.0 * k + 3) / x;
				dg += termG * (3.0 * k + 4) / x;
			}
		}
		return derivative ? AiryValues{ c1 * df - c2 * dg, root3 * (c1 * df + c2 * dg) }
			: AiryValues{ c1 * f - c2 * g, root3 * (c1 * f + c2 * g) };
	}
	const double a = std::abs(x), zeta = 2.0 / 3 * a * std::sqrt(a);
	if (x > 0) {
		const double k = std::cyl_bessel_k(order, zeta), i = std::cyl_bessel_i(order, zeta);
		const double both = 2 * i + 2 / pi * std::sin(order * pi) * k;
		if (derivative)
			return { -x / (pi * root3) * k, x / root3 * both };
		return { std::sqrt(x / 3) / pi * k, std::sqrt(x / 3) * both };
	}
	const double plus = BesselJ(order, zeta), minus = BesselJ(-order, zeta);
	if (derivative)
		return { a / 3 * (plus - minus), a / root3 * (minus + plus) };
	return { std::sqrt(a) / 3 * (plus + minus), std::sqrt(a / 3) * (minus - plus) };
}

double Beta(double a, double b) {
	if (std::isnan(a) || std::isnan(b))
		return NaN;
	if (a > 0 && b > 0)
		return std::beta(a, b);
	return std::tgamma(a) * std::tgamma(b) / std::tgamma(a + b);
}

double Zeta(double s) {
	return std::isnan(s) ? s : s == 1 ? NaN : std::riemann_zeta(s);
}

// Halley's method on w e^w = x, from a start on the right branch.
double Lambert(double x, double w) {
	for (int i = 0; i < 64; i++) {
		const double e = std::exp(w), f = w * e - x;
		if (f == 0)
			break;
		const double next = w - f / (e * (w + 1) - (w + 2) * f / (2 * w + 2));
		if (!std::isfinite(next) || std::abs(next - w) <= 1e-15 * (1 + std::abs(next))) {
			if (std::isfinite(next))
				w = next;
			break;
		}
		w = next;
	}
	return w;
}

double LambertW(double x) {
	const double least = -1 / std::numbers::e;
	if (std::isnan(x) || x < least * (1 + 1e-14))
		return NaN;
	if (x <= least)
		return -1;
	if (std::isinf(x))
		return x;
	if (x == 0)
		return 0;
	// near the branch point by its series, else by logarithms
	const double p = std::sqrt(2 * (std::numbers::e * x + 1));
	const double start = x < 1 ? -1 + p - p * p / 3 : std::log(x) - (x > 3 ? std::log(std::log(x)) : 0);
	return Lambert(x, start);
}

double LambertWm1(double x) {
	const double least = -1 / std::numbers::e;
	if (std::isnan(x) || x < least * (1 + 1e-14) || x > 0)
		return NaN;
	if (x <= least)
		return -1;
	if (x == 0)
		return -std::numeric_limits<double>::infinity();
	const double p = std::sqrt(2 * (std::numbers::e * x + 1));
	const double start = x < -0.25 ? -1 - p - p * p / 3 : std::log(-x) - std::log(-std::log(-x));
	return Lambert(x, start);
}

// By the parameter m = k^2; below 0 by the imaginary modulus transformation.
double EllipK(double m) {
	if (std::isnan(m) || m > 1)
		return NaN;
	if (m == 1)
		return std::numeric_limits<double>::infinity();
	if (m < 0)
		return std::comp_ellint_1(std::sqrt(m / (m - 1))) / std::sqrt(1 - m);
	return std::comp_ellint_1(std::sqrt(m));
}

double EllipE(double m) {
	if (std::isnan(m) || m > 1)
		return NaN;
	if (m < 0)
		return std::comp_ellint_2(std::sqrt(m / (m - 1))) * std::sqrt(1 - m);
	return std::comp_ellint_2(std::sqrt(m));
}

double EllipF(double phi, double m) {
	if (std::isnan(phi) || std::isnan(m) || m < 0 || m > 1 || std::isinf(phi) || (m == 1 && std::abs(phi) >= std::numbers::pi / 2))
		return NaN;
	return std::ellint_1(std::sqrt(m), phi);
}

double EllipEInc(double phi, double m) {
	if (std::isnan(phi) || std::isnan(m) || m < 0 || m > 1 || std::isinf(phi))
		return NaN;
	return std::ellint_2(std::sqrt(m), phi);
}

double (*Func1(FplFunc f))(double) {
	switch (f) {
		case FplFunc::Sin: return [](double x) { return std::sin(x); };
		case FplFunc::Cos: return [](double x) { return std::cos(x); };
		case FplFunc::Tan: return [](double x) { return std::tan(x); };
		case FplFunc::Sec: return Sec;
		case FplFunc::Csc: return Csc;
		case FplFunc::Cot: return Cot;
		case FplFunc::Square: return Square;
		case FplFunc::Triangle: return Triangle;
		case FplFunc::Sawtooth: return Sawtooth;
		case FplFunc::Asec: return [](double x) { return std::acos(1 / x); };
		case FplFunc::Acsc: return [](double x) { return std::asin(1 / x); };
		case FplFunc::Acot: return [](double x) { return std::numbers::pi / 2 - std::atan(x); };
		case FplFunc::Sech: return [](double x) { return 1 / std::cosh(x); };
		case FplFunc::Csch: return [](double x) { return 1 / std::sinh(x); };
		case FplFunc::Coth: return [](double x) { return 1 / std::tanh(x); };
		case FplFunc::Asech: return [](double x) { return std::acosh(1 / x); };
		case FplFunc::Acsch: return [](double x) { return std::asinh(1 / x); };
		case FplFunc::Acoth: return [](double x) { return std::atanh(1 / x); };
		case FplFunc::Trunc: return [](double x) { return std::trunc(x); };
		case FplFunc::Lgamma: return [](double x) { return std::lgamma(x); };
		case FplFunc::Erfc: return [](double x) { return std::erfc(x); };
		case FplFunc::Step: return Step;
		case FplFunc::Sinc: return Sinc;
		case FplFunc::SincD1: return SincD1;
		case FplFunc::SincD2: return SincD2;
		case FplFunc::Normpdf: return Normpdf;
		case FplFunc::Normcdf: return Normcdf;
		case FplFunc::AiryAi: return [](double x) { return Airy(x, false).ai; };
		case FplFunc::AiryBi: return [](double x) { return Airy(x, false).bi; };
		case FplFunc::AiryAiD: return [](double x) { return Airy(x, true).ai; };
		case FplFunc::AiryBiD: return [](double x) { return Airy(x, true).bi; };
		case FplFunc::Zeta: return Zeta;
		case FplFunc::LambertW: return LambertW;
		case FplFunc::LambertWm1: return LambertWm1;
		case FplFunc::EllipK: return EllipK;
		case FplFunc::EllipE: return EllipE;
		case FplFunc::Asin: return [](double x) { return std::asin(x); };
		case FplFunc::Acos: return [](double x) { return std::acos(x); };
		case FplFunc::Atan: return [](double x) { return std::atan(x); };
		case FplFunc::Sinh: return [](double x) { return std::sinh(x); };
		case FplFunc::Cosh: return [](double x) { return std::cosh(x); };
		case FplFunc::Tanh: return [](double x) { return std::tanh(x); };
		case FplFunc::Asinh: return [](double x) { return std::asinh(x); };
		case FplFunc::Acosh: return [](double x) { return std::acosh(x); };
		case FplFunc::Atanh: return [](double x) { return std::atanh(x); };
		case FplFunc::Exp: return [](double x) { return std::exp(x); };
		case FplFunc::Ln: return [](double x) { return std::log(x); };
		case FplFunc::Log: return [](double x) { return std::log10(x); };
		case FplFunc::Log2: return [](double x) { return std::log2(x); };
		case FplFunc::Sqrt: return [](double x) { return std::sqrt(x); };
		case FplFunc::Cbrt: return [](double x) { return std::cbrt(x); };
		case FplFunc::Abs: return [](double x) { return std::abs(x); };
		case FplFunc::Sign: return Sign;
		case FplFunc::Floor: return [](double x) { return std::floor(x); };
		case FplFunc::Ceil: return [](double x) { return std::ceil(x); };
		case FplFunc::Round: return [](double x) { return std::round(x); };
		case FplFunc::Gamma: return [](double x) { return std::tgamma(x); };
		case FplFunc::Digamma: return Digamma;
		case FplFunc::Erf: return [](double x) { return std::erf(x); };
		default: return nullptr;
	}
}

double (*Func2(FplFunc f))(double, double) {
	switch (f) {
		case FplFunc::Atan2: return Atan2;
		case FplFunc::Root: return Root;
		case FplFunc::Mod: return Mod;
		case FplFunc::Hypot: return Hypot;
		case FplFunc::NCr: return NCr;
		case FplFunc::NPr: return NPr;
		case FplFunc::Gcd: return Gcd;
		case FplFunc::Lcm: return Lcm;
		case FplFunc::Beta: return Beta;
		case FplFunc::BesselJ: return BesselJ;
		case FplFunc::BesselY: return BesselY;
		case FplFunc::BesselI: return BesselI;
		case FplFunc::BesselK: return BesselK;
		case FplFunc::EllipF: return EllipF;
		case FplFunc::EllipEInc: return EllipEInc;
		default: return nullptr;
	}
}

double (*FuncN(FplFunc f))(const double*, int) {
	switch (f) {
		case FplFunc::Min: return Min;
		case FplFunc::Max: return Max;
		default: return nullptr;
	}
}

bool Compare(FplRel rel, double a, double b) {
	switch (rel) {
		case FplRel::Lt: return a < b;
		case FplRel::Le: return a <= b;
		case FplRel::Gt: return a > b;
		case FplRel::Ge: return a >= b;
		default: return a == b;
	}
}

double PowInt(double x, int n) {
	unsigned e = unsigned(n < 0 ? -n : n);
	double r = 1, b = x;
	while (e) {
		if (e & 1)
			r *= b;
		b *= b;
		e >>= 1;
	}
	return n < 0 ? 1 / r : r;
}

int StandardSlot(std::wstring_view name) {
	if (name == L"x") return FplSlotX;
	if (name == L"y") return FplSlotY;
	if (name == L"t") return FplSlotT;
	if (name == FplTheta) return FplSlotTheta;
	if (name == L"z") return FplSlotZ;
	if (name == L"u") return FplSlotU;
	if (name == L"v") return FplSlotV;
	return -1;
}

}	// namespace

double FplPow(double x, double y) {
	double r = std::pow(x, y);
	if (x < 0 && std::isnan(r) && std::isfinite(y)) {
		// A rational power with an odd denominator: (-8)^(1/3) = -2, (-8)^(2/3) = 4.
		for (int q = 3; q <= 99; q += 2) {
			double pq = y * q;
			double p = std::round(pq);
			if (std::abs(pq - p) < 1e-9 * std::max(1.0, std::abs(pq))) {
				double m = std::pow(-x, y);
				return std::fmod(p, 2) != 0 ? -m : m;
			}
		}
	}
	return r;
}

// ---- Evaluating the tree ---------------------------------------------------------------------------------------------

namespace {

// Adaptive Simpson with Richardson's correction; budget: how many more evaluations it may make.
template <typename F>
double Simpson(F& f, double a, double b, double fa, double fm, double fb, double whole, double eps, int depth,
	int& budget) {
	double m = (a + b) / 2, l = (a + m) / 2, r = (m + b) / 2;
	double fl = f(l), fr = f(r);
	budget -= 2;
	if (std::isnan(fl) || std::isnan(fr))
		return NaN;
	double left = (m - a) / 6 * (fa + 4 * fl + fm), right = (b - m) / 6 * (fm + 4 * fr + fb);
	double delta = left + right - whole;
	if (depth <= 0 || budget <= 0 || std::abs(delta) <= 15 * eps)
		return left + right + delta / 15;
	return Simpson(f, a, m, fa, fl, fm, left, eps / 2, depth - 1, budget) +
		Simpson(f, m, b, fm, fr, fb, right, eps / 2, depth - 1, budget);
}

// A sum or product over the whole numbers from lo to hi (both rounded): empty is 0 or 1. Too many terms to draw isn't a value.
template <typename F>
double RunSeries(F& f, double lo, double hi, bool product) {
	if (!std::isfinite(lo) || !std::isfinite(hi))
		return NaN;
	const double first = std::round(lo), last = std::round(hi);
	if (last - first >= 100000)
		return NaN;
	double total = product ? 1 : 0;
	for (double k = first; k <= last; k++) {
		const double term = f(k);
		total = product ? total * term : total + term;
	}
	return total;
}

template <typename F>
double Integrate(F& f, double a, double b) {
	if (!std::isfinite(a) || !std::isfinite(b))
		return NaN;
	if (a == b)
		return 0;
	// a first pass over a few pieces, so a narrow feature isn't missed between three points
	constexpr int Pieces = 8;
	double xs[Pieces + 1], fx[Pieces + 1], fm[Pieces], piece[Pieces];
	for (int i = 0; i <= Pieces; i++) {
		xs[i] = i == Pieces ? b : a + (b - a) * i / Pieces;
		fx[i] = f(xs[i]);
		if (std::isnan(fx[i]))
			return NaN;
	}
	double rough = 0;
	for (int i = 0; i < Pieces; i++) {
		fm[i] = f((xs[i] + xs[i + 1]) / 2);
		if (std::isnan(fm[i]))
			return NaN;
		piece[i] = (xs[i + 1] - xs[i]) / 6 * (fx[i] + 4 * fm[i] + fx[i + 1]);
		rough += std::abs(piece[i]);
	}
	const double eps = 1e-10 * std::max(rough, 1e-300);
	int budget = 200000;
	double sum = 0;
	for (int i = 0; i < Pieces; i++)
		sum += Simpson(f, xs[i], xs[i + 1], fx[i], fm[i], fx[i + 1], piece[i], eps / Pieces, 40, budget);
	return sum;
}

} // namespace

double FplIntegrate(const std::function<double(double)>& f, double a, double b) {
	auto g = [&](double t) { return f(t); };
	return Integrate(g, a, b);
}

int FplStandardSlot(std::wstring_view name) {
	return StandardSlot(name);
}

double FplEvalTree(const FplAst& ast, int node, const std::vector<std::wstring>& names, const double* values) {
	auto& n = ast[node];
	auto& k = n.kids;
	auto ev = [&](int i) { return FplEvalTree(ast, k[i], names, values); };
	switch (n.op) {
		case FplOp::Num: return n.value;
		case FplOp::Var:
			for (size_t i = 0; i < names.size(); i++)
				if (names[i] == n.name)
					return values[i];
			return NaN;
		case FplOp::Neg: return -ev(0);
		case FplOp::Add: return ev(0) + ev(1);
		case FplOp::Sub: return ev(0) - ev(1);
		case FplOp::Mul: return ev(0) * ev(1);
		case FplOp::Div: return ev(0) / ev(1);
		case FplOp::Pow: return FplPow(ev(0), ev(1));
		case FplOp::Fact: return Fact(ev(0));
		case FplOp::Call: {
			if (auto f = Func1(n.func))
				return f(ev(0));
			if (auto f = Func2(n.func))
				return f(ev(0), ev(1));
			std::vector<double> args(k.size());
			for (size_t i = 0; i < k.size(); i++)
				args[i] = ev(int(i));
			return FuncN(n.func)(args.data(), int(args.size()));
		}
		case FplOp::Cmp: return Compare(n.rel, ev(0), ev(1)) ? 1 : 0;
		case FplOp::And: return ev(0) != 0 && ev(1) != 0 ? 1 : 0;
		case FplOp::Piecewise: {
			size_t pairs = (k.size() - (n.hasDefault ? 1 : 0)) / 2;
			for (size_t i = 0; i < pairs; i++)
				if (ev(int(2 * i)) != 0)
					return ev(int(2 * i + 1));
			return n.hasDefault ? ev(int(k.size() - 1)) : NaN;
		}
		case FplOp::Tuple: case FplOp::List: return NaN;
		case FplOp::Series: {
			std::vector<std::wstring> inner;
			inner.reserve(names.size() + 1);
			inner.push_back(n.name);
			inner.insert(inner.end(), names.begin(), names.end());
			std::vector<double> vals(names.size() + 1);
			for (size_t i = 0; i < names.size(); i++)
				vals[i + 1] = values[i];
			auto f = [&](double t) {
				vals[0] = t;
				return FplEvalTree(ast, k[0], inner, vals.data());
			};
			return RunSeries(f, ev(1), ev(2), n.product);
		}
		case FplOp::Integral: {
			// the body with its variable first among the names
			std::vector<std::wstring> inner;
			inner.reserve(names.size() + 1);
			inner.push_back(n.name);
			inner.insert(inner.end(), names.begin(), names.end());
			std::vector<double> vals(names.size() + 1);
			for (size_t i = 0; i < names.size(); i++)
				vals[i + 1] = values[i];
			auto f = [&](double t) {
				vals[0] = t;
				return FplEvalTree(ast, k[0], inner, vals.data());
			};
			return Integrate(f, ev(1), ev(2));
		}
	}
	return NaN;
}

// ---- Compiling -------------------------------------------------------------------------------------------------------

class FplCompiler {
public:
	FplCompiler(const FplAst& ast, const std::vector<std::wstring>& params, FplProgram& prog, FplError& error)
		: m_Ast(ast), m_Params(params), m_Prog(prog), m_Error(error) {}

	bool Compile(int node) {
		if (!Emit(node))
			return false;
		m_Prog.m_MaxDepth = m_Max;
		return true;
	}

private:
	void Push(FplInstr in, int delta) {
		m_Prog.m_Code.push_back(in);
		m_Depth += delta;
		m_Max = std::max(m_Max, m_Depth);
	}

	void Const(double v) {
		FplInstr in{ FplCode::Const };
		in.value = v;
		Push(in, 1);
	}

	bool Fail(FplErrorCode code, int node) {
		m_Error.code = code;
		m_Error.pos = m_Ast[node].pos;
		m_Error.len = m_Ast[node].len;
		if (m_Ast[node].op == FplOp::Var)
			m_Error.name = m_Ast[node].name;
		return false;
	}

	// Names that aren't bound by an integral around them.
	bool HasFreeNames(int node, std::vector<std::wstring>& bound) {
		auto& n = m_Ast[node];
		if (n.op == FplOp::Var)
			return std::find(bound.begin(), bound.end(), n.name) == bound.end();
		if (n.op == FplOp::Integral || n.op == FplOp::Series) {
			if (HasFreeNames(n.kids[1], bound) || HasFreeNames(n.kids[2], bound))
				return true;
			bound.push_back(n.name);
			bool free = HasFreeNames(n.kids[0], bound);
			bound.pop_back();
			return free;
		}
		for (int k : n.kids)
			if (HasFreeNames(k, bound))
				return true;
		return false;
	}

	// The constant a part folds to, if it has no free names.
	bool ConstantOf(int node, double& v) {
		std::vector<std::wstring> bound;
		if (HasFreeNames(node, bound) || m_Ast[node].op == FplOp::Tuple || m_Ast[node].op == FplOp::List)
			return false;
		v = FplEvalTree(m_Ast, node, {}, nullptr);
		return true;
	}

	bool Emit(int node) {
		auto& n = m_Ast[node];
		auto& k = n.kids;
		if (n.op == FplOp::Tuple)
			return Fail(FplErrorCode::MisplacedPoint, node);
		if (n.op == FplOp::List)
			return Fail(FplErrorCode::ListNotAllowed, node);
		double v;
		if (n.op != FplOp::Cmp && n.op != FplOp::And && ConstantOf(node, v)) {
			Const(v);
			return true;
		}
		switch (n.op) {
			case FplOp::Num:
				Const(n.value);
				return true;
			case FplOp::Var: {
				int slot = StandardSlot(n.name);
				// the innermost sum's index by that name, over a parameter
				for (auto it = m_Indexes.rbegin(); it != m_Indexes.rend(); ++it)
					if (it->first == n.name) {
						slot = it->second;
						break;
					}
				if (slot < 0) {
					auto it = std::find(m_Params.begin(), m_Params.end(), n.name);
					if (it == m_Params.end() || n.name == L"r")
						return Fail(n.name == L"r" ? FplErrorCode::VariableNotAllowed : FplErrorCode::UnknownName, node);
					slot = FplFirstParam + int(it - m_Params.begin());
				}
				FplInstr in{ FplCode::Load };
				in.arg = slot;
				Push(in, 1);
				if (slot < 64 && !Bound(n.name))
					m_Prog.m_Slots |= uint64_t(1) << slot;
				m_Prog.m_SlotCount = std::max(m_Prog.m_SlotCount, slot + 1);
				return true;
			}
			case FplOp::Neg:
				return Emit(k[0]) && Op(FplCode::Neg, 0);
			case FplOp::Add:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Add, -1);
			case FplOp::Sub:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Sub, -1);
			case FplOp::Mul:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Mul, -1);
			case FplOp::Div:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Div, -1);
			case FplOp::Pow: {
				if (!Emit(k[0]))
					return false;
				double e;
				if (ConstantOf(k[1], e) && e == std::floor(e) && std::abs(e) <= 64) {
					if (e == 2)
						return Op(FplCode::Sq, 0);
					FplInstr in{ FplCode::PowInt };
					in.arg = int(e);
					Push(in, 0);
					return true;
				}
				return Emit(k[1]) && Op(FplCode::Pow, -1);
			}
			case FplOp::Fact: {
				if (!Emit(k[0]))
					return false;
				FplInstr in{ FplCode::Call1 };
				in.f1 = Fact;
				Push(in, 0);
				return true;
			}
			case FplOp::Call: {
				for (int a : k)
					if (!Emit(a))
						return false;
				FplInstr in{ FplCode::Call1 };
				if ((in.f1 = Func1(n.func)) != nullptr)
					Push(in, 0);
				else if ((in.f2 = Func2(n.func)) != nullptr) {
					in.code = FplCode::Call2;
					Push(in, -1);
				}
				else {
					in.code = FplCode::CallN;
					in.fn = FuncN(n.func);
					in.arg = int(k.size());
					Push(in, 1 - int(k.size()));
				}
				return true;
			}
			case FplOp::Cmp: {
				static const FplCode codes[] = { FplCode::Eq, FplCode::Lt, FplCode::Le, FplCode::Gt, FplCode::Ge };
				return Emit(k[0]) && Emit(k[1]) && Op(codes[int(n.rel)], -1);
			}
			case FplOp::And:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::And, -1);
			case FplOp::Piecewise: {
				size_t pairs = (k.size() - (n.hasDefault ? 1 : 0)) / 2;
				std::vector<size_t> toEnd;
				int base = m_Depth;
				for (size_t i = 0; i < pairs; i++) {
					if (!Emit(k[2 * i]))
						return false;
					size_t jump = m_Prog.m_Code.size();
					Push({ FplCode::JumpIfFalse }, -1);
					if (!Emit(k[2 * i + 1]))
						return false;
					toEnd.push_back(m_Prog.m_Code.size());
					Push({ FplCode::Jump }, 0);
					m_Prog.m_Code[jump].arg = int(m_Prog.m_Code.size());
					m_Depth = base;	// the next branch starts where this one did
				}
				if (n.hasDefault) {
					if (!Emit(k.back()))
						return false;
				}
				else
					Const(NaN);
				for (size_t j : toEnd)
					m_Prog.m_Code[j].arg = int(m_Prog.m_Code.size());
				return true;
			}
			case FplOp::Series: {
				// the body as a program of its own, its index in a slot past all the names, one more for each sum around it
				const int slot = FplFirstParam + int(m_Params.size()) + int(m_Indexes.size());
				auto body = std::make_shared<FplProgram>();
				FplCompiler inner(m_Ast, m_Params, *body, m_Error);
				inner.m_Bound = m_Bound;
				inner.m_Bound.push_back(n.name);
				inner.m_Indexes = m_Indexes;
				inner.m_Indexes.push_back({ n.name, slot });
				if (!inner.Compile(k[0]) || !Emit(k[1]) || !Emit(k[2]))
					return false;
				m_Prog.m_Slots |= body->m_Slots;
				m_Prog.m_SlotCount = std::max(m_Prog.m_SlotCount, std::min(body->m_SlotCount, slot));
				FplInstr in{ n.product ? FplCode::Product : FplCode::Sum };
				in.arg = int(m_Prog.m_Subs.size());
				in.value = slot;
				m_Prog.m_Subs.push_back(std::move(body));
				Push(in, -1);
				return true;
			}
			case FplOp::Integral: {
				// the body as a program of its own, run over the variable's slot
				int slot = StandardSlot(n.name);
				if (slot < 0)
					return Fail(FplErrorCode::Internal, node);
				auto body = std::make_shared<FplProgram>();
				FplCompiler inner(m_Ast, m_Params, *body, m_Error);
				inner.m_Bound = m_Bound;
				inner.m_Bound.push_back(n.name);
				inner.m_Indexes = m_Indexes;
				if (!inner.Compile(k[0]) || !Emit(k[1]) || !Emit(k[2]))
					return false;
				// what the body reads, but not its variable, is read here too
				m_Prog.m_Slots |= body->m_Slots;
				m_Prog.m_SlotCount = std::max({ m_Prog.m_SlotCount, body->m_SlotCount, slot + 1 });
				FplInstr in{ FplCode::Integral };
				in.arg = int(m_Prog.m_Subs.size());
				in.value = slot;
				m_Prog.m_Subs.push_back(std::move(body));
				Push(in, -1);
				return true;
			}
			default:
				return Fail(FplErrorCode::Internal, node);
		}
	}

	bool Bound(const std::wstring& name) const {
		return std::find(m_Bound.begin(), m_Bound.end(), name) != m_Bound.end();
	}

	bool Op(FplCode code, int delta) {
		Push({ code }, delta);
		return true;
	}

	const FplAst& m_Ast;
	const std::vector<std::wstring>& m_Params;
	FplProgram& m_Prog;
	FplError& m_Error;
	int m_Depth = 0;
	int m_Max = 0;
	std::vector<std::wstring> m_Bound;	// the integrals' variables around what's compiled
	std::vector<std::pair<std::wstring, int>> m_Indexes;	// the sums' around it, with their slots
};

FplProgram FplCompile(const FplAst& ast, int node, const std::vector<std::wstring>& parameters, FplError* error) {
	FplProgram prog;
	FplError local;
	FplError& err = error ? *error : local;
	if (node < 0) {
		err.code = FplErrorCode::Empty;
		return prog;
	}
	if (ast[node].op == FplOp::Cmp) {
		err.code = FplErrorCode::MisplacedRelation;
		err.pos = ast[node].pos;
		err.len = ast[node].len;
		return prog;
	}
	FplCompiler c(ast, parameters, prog, err);
	if (!c.Compile(node))
		return FplProgram();
	return prog;
}

bool FplProgram::Reads(int slot) const {
	for (auto& in : m_Code)
		if (in.code == FplCode::Load && in.arg == slot)
			return true;
	for (auto& sub : m_Subs)
		if (sub->Reads(slot))
			return true;
	return false;
}

// ---- The stack machine -----------------------------------------------------------------------------------------------

double FplProgram::Eval(const double* slots) const {
	if (m_Code.empty())
		return NaN;
	double small[32];
	std::vector<double> big;
	double* st = small;
	if (m_MaxDepth > 32) {
		big.resize(m_MaxDepth);
		st = big.data();
	}
	int sp = 0;	// the next free entry
	const FplInstr* code = m_Code.data();
	const size_t count = m_Code.size();
	for (size_t pc = 0; pc < count; ) {
		const FplInstr& in = code[pc++];
		switch (in.code) {
			case FplCode::Const: st[sp++] = in.value; break;
			case FplCode::Load: st[sp++] = slots[in.arg]; break;
			case FplCode::Neg: st[sp - 1] = -st[sp - 1]; break;
			case FplCode::Add: sp--; st[sp - 1] += st[sp]; break;
			case FplCode::Sub: sp--; st[sp - 1] -= st[sp]; break;
			case FplCode::Mul: sp--; st[sp - 1] *= st[sp]; break;
			case FplCode::Div: sp--; st[sp - 1] /= st[sp]; break;
			case FplCode::Pow: sp--; st[sp - 1] = FplPow(st[sp - 1], st[sp]); break;
			case FplCode::Sq: st[sp - 1] *= st[sp - 1]; break;
			case FplCode::PowInt: st[sp - 1] = PowInt(st[sp - 1], in.arg); break;
			case FplCode::Call1: st[sp - 1] = in.f1(st[sp - 1]); break;
			case FplCode::Call2: sp--; st[sp - 1] = in.f2(st[sp - 1], st[sp]); break;
			case FplCode::CallN: sp -= in.arg - 1; st[sp - 1] = in.fn(st + sp - 1, in.arg); break;
			case FplCode::Lt: sp--; st[sp - 1] = st[sp - 1] < st[sp] ? 1 : 0; break;
			case FplCode::Le: sp--; st[sp - 1] = st[sp - 1] <= st[sp] ? 1 : 0; break;
			case FplCode::Gt: sp--; st[sp - 1] = st[sp - 1] > st[sp] ? 1 : 0; break;
			case FplCode::Ge: sp--; st[sp - 1] = st[sp - 1] >= st[sp] ? 1 : 0; break;
			case FplCode::Eq: sp--; st[sp - 1] = st[sp - 1] == st[sp] ? 1 : 0; break;
			case FplCode::And: sp--; st[sp - 1] = st[sp - 1] != 0 && st[sp] != 0 ? 1 : 0; break;
			case FplCode::JumpIfFalse:
				if (st[--sp] == 0)
					pc = size_t(in.arg);
				break;
			case FplCode::Jump: pc = size_t(in.arg); break;
			case FplCode::Sum: case FplCode::Product: {
				sp--;
				const FplProgram& body = *m_Subs[in.arg];
				const int slot = int(in.value);
				// what the body reads, copied, with its index past that
				const int read = std::min(body.m_SlotCount, slot);
				std::vector<double> local(std::max(body.m_SlotCount, slot + 1), 0.0);
				std::copy(slots, slots + read, local.begin());
				auto f = [&](double k) {
					local[slot] = k;
					return body.Eval(local.data());
				};
				st[sp - 1] = RunSeries(f, st[sp - 1], st[sp], in.code == FplCode::Product);
				break;
			}
			case FplCode::Integral: {
				sp--;
				const FplProgram& body = *m_Subs[in.arg];
				const int slot = int(in.value);
				// the slots copied, so the body's variable can move without touching the caller's
				std::vector<double> local(slots, slots + std::max(m_SlotCount, slot + 1));
				auto f = [&](double t) {
					local[slot] = t;
					return body.Eval(local.data());
				};
				st[sp - 1] = Integrate(f, st[sp - 1], st[sp]);
				break;
			}
		}
	}
	return st[0];
}

// ---- All of it -------------------------------------------------------------------------------------------------------

FplOdeText FplSplitOde(std::wstring_view text) {
	FplOdeText o;
	const size_t n = text.size();
	size_t i = 0;
	auto spaces = [&] {
		while (i < n && (text[i] == L' ' || text[i] == L'\t'))
			i++;
	};
	auto take = [&](wchar_t c) {
		spaces();
		if (i < n && text[i] == c) {
			i++;
			return true;
		}
		return false;
	};
	auto word = [&](std::wstring_view w) {
		const size_t at = i;
		for (wchar_t c : w)
			if (!take(c)) {
				i = at;
				return false;
			}
		return true;
	};
	// how many primes come next
	auto primes = [&] {
		spaces();
		int count = 0;
		for (; i < n; i++) {
			if (text[i] == L'\'' || text[i] == wchar_t(0x2032))
				count++;
			else if (text[i] == wchar_t(0x2033))
				count += 2;
			else if (text[i] == wchar_t(0x2034))
				count += 3;
			else
				break;
		}
		return count;
	};
	spaces();
	const size_t start = i;
	// what it starts with: the order
	if (take(L'y') && (o.degree = primes()) > 0)
		o.order = o.degree == 1 ? FplOdeOrder::First : FplOdeOrder::Second;
	else {
		i = start;
		o.degree = 1;
		if (take(L'x') && primes() == 1)
			o.order = FplOdeOrder::System;
		else {
			i = start;
			if (word(L"dy/dx"))
				o.order = FplOdeOrder::First;
			else if (word(L"dx/dt"))
				o.order = FplOdeOrder::System;
			else
				return o;
		}
	}
	if (!take(L'='))
		return o;
	o.isOde = true;
	o.rhs = i;
	// the commas at the top: F, then its conditions
	auto nextComma = [&](size_t from) {
		int depth = 0;
		for (size_t k = from; k < n; k++) {
			wchar_t c = text[k];
			if (c == L'(' || c == L'[' || c == L'{')
				depth++;
			else if (c == L')' || c == L']' || c == L'}')
				depth--;
			else if (c == L',' && depth == 0)
				return k;
		}
		return n;
	};
	auto stop = [&](bool missingClose = false) {
		spaces();
		o.errorAt = missingClose ? n : i;
		o.missingClose = missingClose;
		return o;
	};
	if (o.degree > 5) {
		o.tooHigh = true;
		i = start;
		return stop();
	}
	// what's inside the parentheses just opened
	auto inside = [&](size_t& at, size_t& len) {
		at = i;
		int depth = 1;
		for (size_t k = i; k < n; k++) {
			if (text[k] == L'(')
				depth++;
			else if (text[k] == L')' && --depth == 0) {
				len = k - at;
				i = k + 1;
				return true;
			}
		}
		return false;
	};
	auto trim = [&](size_t at, size_t len) {
		std::wstring_view v = text.substr(at, len);
		while (!v.empty() && v.front() == L' ')
			v.remove_prefix(1);
		while (!v.empty() && v.back() == L' ')
			v.remove_suffix(1);
		return v;
	};
	size_t comma = nextComma(i);
	o.rhsLen = comma - o.rhs;
	if (o.order == FplOdeOrder::System) {
		if (comma >= n) {
			i = n;
			return stop();
		}
		i = comma + 1;
		const size_t second = i;
		if (!(take(L'y') && primes() == 1)) {
			i = second;
			if (!word(L"dy/dt"))
				return stop();
		}
		if (!take(L'='))
			return stop();
		o.rhs2 = i;
		comma = nextComma(i);
		o.rhs2Len = comma - i;
		// z' = R next makes it 3D
		if (comma < n) {
			i = comma + 1;
			const size_t third = i;
			if ((take(L'z') && primes() == 1) || ((i = third), word(L"dz/dt"))) {
				if (!take(L'='))
					return stop();
				o.order = FplOdeOrder::System3D;
				o.rhs3 = i;
				comma = nextComma(i);
				o.rhs3Len = comma - i;
			}
			else
				i = third;
		}
	}
	while (comma < n) {
		i = comma + 1;
		FplOdeStart st;
		if (o.order == FplOdeOrder::System || o.order == FplOdeOrder::System3D) {
			// (a, b), (a, b, c)
			if (!take(L'('))
				return stop();
			size_t at = 0, len = 0;
			if (!inside(at, len))
				return stop(true);
			const size_t close = at + len, after = i;
			auto within = [&](size_t from) {
				// a comma of this point's: nextComma runs past the parenthesis only by depth
				const size_t k = nextComma(from);
				return k < close ? k : close;
			};
			const size_t mid = within(at);
			if (mid >= close) {
				i = at;
				return stop();
			}
			st.a = at;
			st.aLen = mid - at;
			st.b = mid + 1;
			if (o.order == FplOdeOrder::System3D) {
				const size_t mid2 = within(mid + 1);
				if (mid2 >= close) {
					i = at;
					return stop();
				}
				st.bLen = mid2 - st.b;
				st.more.push_back({ mid2 + 1, close - mid2 - 1 });
				if (within(mid2 + 1) < close) {
					i = at;
					return stop();
				}
			}
			else {
				st.bLen = close - st.b;
				if (within(mid + 1) < close) {
					i = at;
					return stop();
				}
			}
			i = after;
			spaces();
			if (i < n && text[i] != L',')
				return stop();
			comma = i < n ? i : n;
		}
		else {
			// y(a) = b
			if (!take(L'y') || !take(L'('))
				return stop();
			if (!inside(st.a, st.aLen))
				return stop(true);
			if (!take(L'='))
				return stop();
			st.b = i;
			comma = nextComma(i);
			st.bLen = comma - i;
			// y'(a) = c and on, at the same a
			for (int k = 1; k < o.degree; k++) {
				o.badInitial = true;
				if (comma >= n) {
					i = n;
					return stop();
				}
				i = comma + 1;
				size_t a2 = 0, a2Len = 0;
				if (!take(L'y') || primes() != k || !take(L'('))
					return stop();
				if (!inside(a2, a2Len)) {
					o.badInitial = false;
					return stop(true);
				}
				if (trim(a2, a2Len) != trim(st.a, st.aLen)) {
					i = a2;
					return stop();
				}
				if (!take(L'='))
					return stop();
				o.badInitial = false;
				FplOdeRange value{ i, 0 };
				comma = nextComma(i);
				value.len = comma - i;
				st.more.push_back(value);
			}
		}
		o.starts.push_back(std::move(st));
	}
	return o;
}

namespace {

// An ODE's part: a value (no relation, point or list), with no variables but those allowed; errors at their places in
// the whole text.
void BuildOdePart(std::wstring_view text, size_t at, size_t len, unsigned allowed, const FplParseOptions& options,
	const std::vector<std::wstring>& parameters, FplProgram& out, FplError& error) {
	auto r = FplParse(text.substr(at, len), options);
	FplError err = r.error;
	if (!err) {
		auto& root = r.ast[r.root];
		if (root.op == FplOp::Cmp || root.op == FplOp::And)
			err = { FplErrorCode::MisplacedRelation, root.pos, root.len };
		else if (root.op == FplOp::Tuple)
			err = { FplErrorCode::MisplacedPoint, root.pos, root.len };
		else if (root.op == FplOp::List)
			err = { FplErrorCode::ListNotAllowed, root.pos, root.len };
		else if (unsigned extra = FplVariablesOf(r.ast, r.root) & ~allowed) {
			const wchar_t* name = (extra & FplVarX) ? L"x" : (extra & FplVarY) ? L"y" : (extra & FplVarT) ? L"t"
				: (extra & FplVarTheta) ? FplTheta : (extra & FplVarR) ? L"r" : (extra & FplVarZ) ? L"z"
				: (extra & FplVarU) ? L"u" : L"v";
			err = { FplErrorCode::VariableNotAllowed, root.pos, root.len, name };
		}
	}
	if (!err)
		out = FplCompile(r.ast, r.root, parameters, &err);
	if (err) {
		err.pos += static_cast<int>(at);
		error = err;
		out = {};
	}
}

// What's zero where the expression may be singular: the factors of denominators and of bases to negative powers, the
// arguments of logarithms, those of tan and sec (where their cos is zero), csc and cot (their sin).
struct SingularAtom {
	int node;
	uint8_t wrap;
	bool guarded;
};

void CollectSingular(const FplAst& ast, int node, unsigned variable, bool guarded, std::vector<SingularAtom>& out) {
	constexpr size_t Most = 32;
	std::function<void(int, uint8_t)> factors = [&](int k, uint8_t wrap) {
		auto& n = ast[k];
		if (wrap == 0 && n.op == FplOp::Mul) {
			for (int kid : n.kids)
				factors(kid, 0);
			return;
		}
		if (wrap == 0 && n.op == FplOp::Neg) {
			factors(n.kids[0], 0);
			return;
		}
		if (wrap == 0 && n.op == FplOp::Pow && ast[n.kids[1]].op == FplOp::Num && ast[n.kids[1]].value > 0) {
			factors(n.kids[0], 0);
			return;
		}
		if ((FplVariablesOf(ast, k) & variable) && out.size() < Most)
			out.push_back({ k, wrap, guarded });
	};
	auto& n = ast[node];
	if (n.op == FplOp::Integral || n.op == FplOp::Series)
		return;		// its body's variable is its own
	if (n.op == FplOp::Div)
		factors(n.kids[1], 0);
	else if (n.op == FplOp::Pow) {
		auto& power = ast[n.kids[1]];
		if ((power.op == FplOp::Num && power.value < 0) || (power.op == FplOp::Neg && ast[power.kids[0]].op == FplOp::Num))
			factors(n.kids[0], 0);
	}
	else if (n.op == FplOp::Call && !n.kids.empty()) {
		switch (n.func) {
			case FplFunc::Tan: case FplFunc::Sec: factors(n.kids[0], 1); break;
			case FplFunc::Csc: case FplFunc::Cot: factors(n.kids[0], 2); break;
			case FplFunc::Ln: case FplFunc::Log: case FplFunc::Log2: case FplFunc::Csch: case FplFunc::Coth:
				factors(n.kids[0], 0);
				break;
			default: break;
		}
	}
	for (int kid : n.kids)
		CollectSingular(ast, kid, variable, guarded || n.op == FplOp::Piecewise, out);
}

} // namespace

namespace {

FplExpression BuildExpression(std::wstring_view text, const std::vector<std::wstring>& parameters, bool degrees,
	const std::vector<FplUserFunction>* functions, const std::vector<std::wstring>* lists, bool plainQ, bool weakQ) {
	FplExpression e;
	FplParseOptions options;
	options.parameters = &parameters;
	options.degrees = degrees;
	options.functions = functions;
	options.lists = lists;
	options.plainQ = plainQ;
	options.weakQ = weakQ;
	if (auto ode = FplSplitOde(text); ode.isOde) {
		options.plainQ = true;		// theta has no place in one
		const bool system = ode.order == FplOdeOrder::System || ode.order == FplOdeOrder::System3D;
		e.kind = ode.order == FplOdeOrder::First ? FplKind::SlopeField : ode.order == FplOdeOrder::Second ? FplKind::SecondOrder
			: ode.order == FplOdeOrder::System ? FplKind::OdeSystem : FplKind::OdeSystem3D;
		e.odeOrder = ode.degree;
		if (ode.tooHigh) {
			e.error = { FplErrorCode::OdeOrder, static_cast<int>(ode.errorAt), 1 };
			return e;
		}
		if (ode.order == FplOdeOrder::Second) {
			// y', y'' ... in F are the t, u, v and z slots. Written over their own characters, so errors stay in their
			// places; a t of the user's own is caught first, with them as e (a name: "2y'" as "2 0" wouldn't read). A y
			// with as many primes as the equation itself, or more, stays as written: an error.
			static constexpr wchar_t Names[] = { L't', L'u', L'v', L'z' };
			auto withPrimes = [&](bool stand) {
				std::wstring copy(text);
				const size_t end = ode.rhs + ode.rhsLen;
				for (size_t k = ode.rhs; k + 1 < end; k++) {
					if (copy[k] != L'y')
						continue;
					size_t last = k;
					int count = 0;
					for (size_t j = k + 1; j < end; j++) {
						const wchar_t c = copy[j];
						const int more = c == L'\'' || c == wchar_t(0x2032) ? 1 : c == wchar_t(0x2033) ? 2 : c == wchar_t(0x2034) ? 3 : 0;
						if (!more)
							break;
						count += more;
						last = j;
					}
					if (count < 1 || count >= ode.degree)
						continue;
					for (size_t j = k; j < last; j++)
						copy[j] = L' ';
					copy[last] = stand ? L'e' : Names[count - 1];
				}
				return copy;
			};
			unsigned allowed = FplVarX | FplVarY;
			static constexpr unsigned Bits[] = { FplVarT, FplVarU, FplVarV, FplVarZ };
			for (int k = 0; k + 1 < ode.degree; k++)
				allowed |= Bits[k];
			FplProgram check;
			BuildOdePart(withPrimes(true), ode.rhs, ode.rhsLen, FplVarX | FplVarY, options, parameters, check, e.error);
			if (!e.error)
				BuildOdePart(withPrimes(false), ode.rhs, ode.rhsLen, allowed, options, parameters, e.f, e.error);
		}
		else {
			const unsigned allowed = !system ? FplVarX | FplVarY : ode.order == FplOdeOrder::System ? FplVarX | FplVarY | FplVarT
				: FplVarX | FplVarY | FplVarZ | FplVarT;
			BuildOdePart(text, ode.rhs, ode.rhsLen, allowed, options, parameters, e.f, e.error);
			if (!e.error && system && ode.rhs2Len)
				BuildOdePart(text, ode.rhs2, ode.rhs2Len, allowed, options, parameters, e.g, e.error);
			if (!e.error && ode.order == FplOdeOrder::System3D)
				BuildOdePart(text, ode.rhs3, ode.rhs3Len, allowed, options, parameters, e.h, e.error);
		}
		if (!e.error && ode.errorAt != std::wstring_view::npos) {
			e.error = { ode.badInitial ? FplErrorCode::BadInitial
				: ode.missingClose ? FplErrorCode::MissingClose : FplErrorCode::UnexpectedToken,
				static_cast<int>(std::min(ode.errorAt, text.size())), ode.errorAt < text.size() ? 1 : 0,
				ode.missingClose ? L")" : L"" };
		}
		else if (!e.error) {
			// each condition's values, one after the other
			for (auto& st : ode.starts) {
				FplProgram a, b;
				BuildOdePart(text, st.a, st.aLen, 0, options, parameters, a, e.error);
				if (!e.error)
					BuildOdePart(text, st.b, st.bLen, 0, options, parameters, b, e.error);
				if (e.error)
					break;
				e.starts.push_back(std::move(a));
				e.starts.push_back(std::move(b));
				for (auto& value : st.more) {
					FplProgram c;
					BuildOdePart(text, value.at, value.len, 0, options, parameters, c, e.error);
					if (e.error)
						break;
					e.starts.push_back(std::move(c));
				}
				if (e.error)
					break;
			}
		}
		if (e.error) {
			e.f = {};
			e.g = {};
			e.h = {};
			e.starts.clear();
		}
		return e;
	}
	auto parsed = FplParse(text, options);
	if (parsed.error) {
		e.error = parsed.error;
		return e;
	}
	auto def = FplClassify(parsed.ast, parsed.root);
	if (def.error) {
		e.error = def.error;
		return e;
	}
	e.kind = def.kind;
	e.rel = def.rel;
	e.name = def.name;
	e.f = FplCompile(parsed.ast, def.expr[0], parameters, &e.error);
	if (!e.error && def.kind == FplKind::Integral)
		e.value = FplCompile(parsed.ast, parsed.root, parameters, &e.error);
	if (def.kind == FplKind::Regression)
		e.df = {};
	if (!e.error && def.expr[1] >= 0)
		e.g = FplCompile(parsed.ast, def.expr[1], parameters, &e.error);
	if (!e.error && def.expr[2] >= 0)
		e.h = FplCompile(parsed.ast, def.expr[2], parameters, &e.error);
	if (!e.error && (def.kind == FplKind::Function || def.kind == FplKind::FunctionOfY)) {
		std::vector<SingularAtom> atoms;
		const unsigned bit = def.kind == FplKind::Function ? FplVarX : FplVarY;
		CollectSingular(parsed.ast, def.expr[0], bit, false, atoms);
		e.singular.reserve(atoms.size());
		for (auto& atom : atoms) {
			FplError ignored;
			FplProgram program = FplCompile(parsed.ast, atom.node, parameters, &ignored);
			if (!ignored)
				e.singular.push_back({ std::move(program), atom.wrap, atom.guarded });
		}
	}
	if (!e.error && (def.kind == FplKind::Function || def.kind == FplKind::FunctionOfY)) {
		FplError ignored;
		int d = FplDerive(parsed.ast, def.expr[0], def.kind == FplKind::Function ? L"x" : L"y", &ignored);
		if (d >= 0 && !ignored)
			e.df = FplCompile(parsed.ast, d, parameters, &ignored);
		if (ignored)
			e.df = {};
	}
	return e;
}

// Whether the text starts like the definition of a variable's name: "u(x) =" (y(x) = x^2 reads as an equation), or "u =" for
// those never on the left of a plot (z = t and y = t are errors about t). Where's its name.
bool DefinesVariable(std::wstring_view text, size_t& at, size_t& length) {
	const size_t n = text.size();
	size_t i = 0;
	while (i < n && text[i] == L' ')
		i++;
	at = i;
	while (i < n && ((text[i] >= L'a' && text[i] <= L'z') || (text[i] >= L'A' && text[i] <= L'Z') || text[i] == wchar_t(0x3B8)))
		i++;
	length = i - at;
	if (length == 0 || !FplIsVariable(text.substr(at, length)))
		return false;
	const std::wstring_view name = text.substr(at, length);
	while (i < n && text[i] == L' ')
		i++;
	if (i >= n || text[i] != L'(') {
		if (name != L"t" && name != L"u" && name != L"v")
			return false;
	}
	else {
		if (name == L"y")
			return false;
		int depth = 0;
		for (; i < n; i++) {
			if (text[i] == L'(')
				depth++;
			else if (text[i] == L')' && --depth == 0)
				break;
		}
		if (i >= n)
			return false;
		i++;
		while (i < n && text[i] == L' ')
			i++;
	}
	return i < n && text[i] == L'=';
}

} // namespace

FplExpression FplBuild(std::wstring_view text, const std::vector<std::wstring>& parameters, bool degrees,
	const std::vector<FplUserFunction>* functions, const std::vector<std::wstring>* lists, bool plainQ, bool weakQ) {
	FplExpression e = BuildExpression(text, parameters, degrees, functions, lists, plainQ, weakQ);
	// a q that doesn't read as theta may be a name the app can offer to add
	if (e.error && !plainQ && text.find(L'q') != text.npos) {
		FplExpression plain = BuildExpression(text, parameters, degrees, functions, lists, true, false);
		if (plain.error.code == FplErrorCode::UnknownName)
			return plain;
	}
	// "u(x) = x - 1" reads as u times x, and "u = x - 1" as an equation with a variable that has no place in it: say what's
	// wrong (only when it doesn't read: z = x y and r = 2 are plots)
	if (e.error.code == FplErrorCode::VariableNotAllowed || e.error.code == FplErrorCode::NotAPlot ||
		e.error.code == FplErrorCode::MisplacedPoint) {
		size_t at = 0, length = 0;
		if (DefinesVariable(text, at, length))
			e.error = { FplErrorCode::ReservedName, static_cast<int>(at), static_cast<int>(length), std::wstring(text.substr(at, length)) };
	}
	return e;
}
