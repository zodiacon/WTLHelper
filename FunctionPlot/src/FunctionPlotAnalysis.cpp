// Roots, extrema and intersections of y = f(x).

#include "FunctionPlotModel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

double FplBrent(const std::function<double(double)>& f, double a, double b, double fa, double fb, double* other) {
	// Brent's zeroin: b is the best guess, c the other end of the bracket, a the previous b.
	double c = a, fc = fa, d = b - a, e = d;
	const double absTol = 1e-15 * std::abs(b - a) + DBL_MIN;
	for (int iter = 0; iter < 300; iter++) {
		if ((fb > 0 && fc > 0) || (fb < 0 && fc < 0)) {
			c = a;
			fc = fa;
			d = e = b - a;
		}
		if (std::abs(fc) < std::abs(fb)) {
			a = b;
			b = c;
			c = a;
			fa = fb;
			fb = fc;
			fc = fa;
		}
		double tol = 2 * DBL_EPSILON * std::abs(b) + absTol;
		double m = (c - b) / 2;
		if (fb == 0) {
			c = b;
			break;
		}
		if (std::abs(m) <= tol)
			break;
		if (std::abs(e) >= tol && std::abs(fa) > std::abs(fb) && std::isfinite(fa) && std::isfinite(fb) &&
			std::isfinite(fc)) {
			double s = fb / fa, p, q;
			if (a == c) {
				p = 2 * m * s;
				q = 1 - s;
			}
			else {
				double qq = fa / fc, r = fb / fc;
				p = s * (2 * m * qq * (qq - r) - (b - a) * (r - 1));
				q = (qq - 1) * (r - 1) * (s - 1);
			}
			if (p > 0)
				q = -q;
			else
				p = -p;
			if (2 * p < std::min(3 * m * q - std::abs(tol * q), std::abs(e * q))) {
				e = d;
				d = p / q;
			}
			else {
				d = m;
				e = m;
			}
		}
		else {
			d = m;
			e = m;
		}
		a = b;
		fa = fb;
		b += std::abs(d) > tol ? d : (m > 0 ? tol : -tol);
		fb = f(b);
		if (std::isnan(fb)) {
			// undefined inside the bracket: no root to find
			if (other)
				*other = c;
			return std::numeric_limits<double>::quiet_NaN();
		}
	}
	if (other)
		*other = c;
	return b;
}

namespace {

int Sign(double v) {
	return v > 0 ? 1 : v < 0 ? -1 : 0;
}

std::function<double(double)> Differences(const std::function<double(double)>& f) {
	return [&f](double x) {
		double h = 6e-6 * std::max(1.0, std::abs(x));
		return (f(x + h) - f(x - h)) / (2 * h);
	};
}

// Is f continuous at a root or extremum found between lo and hi? Compared with how much it changes over the sample
// interval [xa, xb] around it.
bool Continuous(const std::function<double(double)>& f, double x, double other, double xa, double xb) {
	double fx = f(x), fo = f(other);
	if (!std::isfinite(fx) || !std::isfinite(fo))
		return false;
	double fa = f(xa), fb = f(xb);
	double scale = 0;
	if (std::isfinite(fa))
		scale = std::max(scale, std::abs(fa - fx));
	if (std::isfinite(fb))
		scale = std::max(scale, std::abs(fb - fx));
	return std::abs(fx - fo) <= 1e-6 * scale + 1e-300;
}

// Roots (as kind) and, if wanted, extrema of f over the samples; touching roots are extrema within yTolerance of zero.
void Scan(const std::function<double(double)>& f, const std::function<double(double)>& df, double x0, double x1,
	int n, double yTolerance, FplFeatureKind rootKind, bool extrema, const std::function<double(double)>& yOf,
	std::vector<FplFeature>& out) {
	n = std::clamp(n, 1, 10'000'000);
	std::vector<double> xs(n + 1), fs(n + 1), ds(n + 1);
	for (int i = 0; i <= n; i++) {
		xs[i] = i == n ? x1 : x0 + (x1 - x0) * i / n;
		fs[i] = f(xs[i]);
		ds[i] = df(xs[i]);
	}
	auto root = [&](double x) {
		out.push_back({ rootKind, x, yOf(x) });
	};
	// sign changes of f
	for (int i = 0; i <= n; i++) {
		if (fs[i] == 0) {
			root(xs[i]);
			continue;
		}
		if (i == n || !std::isfinite(fs[i]) || !std::isfinite(fs[i + 1]) || fs[i + 1] == 0 ||
			Sign(fs[i]) == Sign(fs[i + 1]))
			continue;
		double other;
		double x = FplBrent(f, xs[i], xs[i + 1], fs[i], fs[i + 1], &other);
		// a root's values shrink toward zero; a jump's (an asymptote, a step) don't
		if (std::isfinite(x) && std::abs(f(x)) + std::abs(f(other)) <= 1e-3 * (std::abs(fs[i]) + std::abs(fs[i + 1])))
			root(x);
	}
	// sign changes of f': extrema, and roots that only touch zero
	for (int i = 0; i < n; i++) {
		double x, other;
		int ia = i, ib = i + 1;
		if (ds[i] == 0 && i > 0 && Sign(ds[i - 1]) * Sign(ds[i + 1]) < 0) {
			x = other = xs[i];
			ia = i - 1;
		}
		else if (std::isfinite(ds[i]) && std::isfinite(ds[i + 1]) && Sign(ds[i]) * Sign(ds[i + 1]) < 0) {
			x = FplBrent(df, xs[i], xs[i + 1], ds[i], ds[i + 1], &other);
			if (!std::isfinite(x))
				continue;
		}
		else {
			continue;
		}
		if (!Continuous(f, x, other, xs[ia], xs[ib]))
			continue;
		double y = f(x), ya = fs[ia], yb = fs[ib];
		// no higher (or lower) than the slopes at the samples allow: not a pole between them
		double rise = 4 * std::max(std::abs(ds[ia]), std::abs(ds[ib])) * (xs[ib] - xs[ia]);
		if (!(std::abs(y - ya) <= rise + 1e-12 * std::abs(ya)) || !(std::abs(y - yb) <= rise + 1e-12 * std::abs(yb)))
			continue;
		FplFeatureKind kind;
		if (y <= ya && y <= yb)
			kind = FplFeatureKind::Minimum;
		else if (y >= ya && y >= yb)
			kind = FplFeatureKind::Maximum;
		else
			continue;
		if (extrema)
			out.push_back({ kind, x, y });
		// touching zero without crossing it (a crossing was found above)
		if (std::abs(y) <= yTolerance && Sign(ya) == Sign(yb) && ya != 0)
			root(x);
	}
}

void SortAndMerge(std::vector<FplFeature>& features, double x0, double x1) {
	std::stable_sort(features.begin(), features.end(), [](const FplFeature& a, const FplFeature& b) {
		return a.x < b.x;
	});
	double eps = 1e-9 * (x1 - x0);
	std::vector<FplFeature> out;
	for (auto& f : features) {
		bool dup = false;
		for (auto it = out.rbegin(); it != out.rend() && f.x - it->x <= eps; ++it) {
			if (it->kind == f.kind) {
				dup = true;
				break;
			}
		}
		if (!dup)
			out.push_back(f);
	}
	features = std::move(out);
}

} // namespace

std::vector<FplFeature> FplFindFeatures(const std::function<double(double)>& f, const std::function<double(double)>& df,
	double x0, double x1, int samples, double yTolerance) {
	std::vector<FplFeature> features;
	if (!(x1 > x0) || !std::isfinite(x0) || !std::isfinite(x1))
		return features;
	auto diff = Differences(f);
	auto zero = [](double) { return 0.0; };
	Scan(f, df ? df : diff, x0, x1, samples, yTolerance, FplFeatureKind::Root, true, zero, features);
	if (x0 <= 0 && 0 <= x1) {
		double y = f(0);
		if (std::isfinite(y))
			features.push_back({ FplFeatureKind::YIntercept, 0, y });
	}
	SortAndMerge(features, x0, x1);
	return features;
}

std::vector<FplFeature> FplFindIntersections(const std::function<double(double)>& f,
	const std::function<double(double)>& g, double x0, double x1, int samples, double yTolerance) {
	std::vector<FplFeature> features;
	if (!(x1 > x0) || !std::isfinite(x0) || !std::isfinite(x1))
		return features;
	std::function<double(double)> h = [&](double x) { return f(x) - g(x); };
	auto dh = Differences(h);
	Scan(h, dh, x0, x1, samples, yTolerance, FplFeatureKind::Intersection, false, f, features);
	SortAndMerge(features, x0, x1);
	return features;
}
