// Roots, extrema and intersections of y = f(x), and intersections of curves of any kind.

#include "FunctionPlotModel.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <utility>

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

// ---- Intersections of any curves --------------------------------------------------------------------------------------

namespace {

constexpr double GridCell = 24;			// DIPs
constexpr double TouchGap = 1.0 / 3;	// DIPs: lines this close touch
constexpr double LongestTouch = 150;	// DIPs: past this they run together
constexpr double MaxRefineMove = 3;		// DIPs: a refined point further from the lines' crossing isn't this crossing

// A curve's segments by the grid cells of the view their boxes overlap.
class SegmentGrid {
public:
	SegmentGrid(const std::vector<FplPolyline>& lines, const FplViewport& view) : m_Lines(lines) {
		m_Cols = std::max(1, static_cast<int>(std::ceil(view.width / GridCell)));
		m_Rows = std::max(1, static_cast<int>(std::ceil(view.height / GridCell)));
		m_Cells.resize(static_cast<size_t>(m_Cols) * m_Rows);
		for (int l = 0; l < static_cast<int>(lines.size()); l++)
			for (int i = 0; i + 1 < static_cast<int>(lines[l].size()); i++) {
				auto& p = lines[l][i];
				auto& q = lines[l][i + 1];
				if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(q.x) || !std::isfinite(q.y))
					continue;
				ForCells(std::min(p.x, q.x), std::min(p.y, q.y), std::max(p.x, q.x), std::max(p.y, q.y),
					[&](std::vector<std::pair<int, int>>& cell) { cell.push_back({ l, i }); });
			}
	}

	template<typename F>
	void ForCells(double left, double top, double right, double bottom, F f) {
		const int c0 = static_cast<int>(std::floor(left / GridCell)), c1 = static_cast<int>(std::floor(right / GridCell));
		const int r0 = static_cast<int>(std::floor(top / GridCell)), r1 = static_cast<int>(std::floor(bottom / GridCell));
		for (int r = std::max(r0, 0); r <= std::min(r1, m_Rows - 1); r++)
			for (int c = std::max(c0, 0); c <= std::min(c1, m_Cols - 1); c++)
				f(m_Cells[static_cast<size_t>(r) * m_Cols + c]);
	}

	FplPoint At(int line, int i) const { return m_Lines[line][i]; }

private:
	const std::vector<FplPolyline>& m_Lines;
	int m_Cols = 0, m_Rows = 0;
	std::vector<std::vector<std::pair<int, int>>> m_Cells;
};

FplPoint Closest(FplPoint p, FplPoint a, FplPoint b) {
	const double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
	const double t = len > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0, 1.0) : 0;
	return { a.x + t * dx, a.y + t * dy };
}

double Distance(FplPoint a, FplPoint b) {
	return std::hypot(a.x - b.x, a.y - b.y);
}

// A curve's parameter nearest a screen point, from a table of its samples.
class ParamTable {
public:
	ParamTable(const FplCurve& c, const FplViewport& view) : m_T0(c.t0), m_T1(c.t1) {
		if (!c.C)
			return;
		m_Screen.resize(Samples + 1);
		for (int i = 0; i <= Samples; i++)
			m_Screen[i] = view.ToScreen(c.C(c.t0 + (c.t1 - c.t0) * i / Samples));
	}
	double Nearest(FplPoint screen) const {
		int best = 0;
		double bestD = std::numeric_limits<double>::infinity();
		for (int i = 0; i <= Samples; i++) {
			double d = Distance(m_Screen[i], screen);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		return m_T0 + (m_T1 - m_T0) * best / Samples;
	}
	double Step() const { return (m_T1 - m_T0) / Samples; }

private:
	static constexpr int Samples = 2048;
	double m_T0, m_T1;
	std::vector<FplPoint> m_Screen;
};

// Newton's method on two unknowns; false if it doesn't settle or leaves the numbers.
template<typename R>
bool Newton2(R residual, double& u, double& v, double hu, double hv) {
	for (int iter = 0; iter < 16; iter++) {
		double r0, r1;
		if (!residual(u, v, r0, r1) || !std::isfinite(r0) || !std::isfinite(r1))
			return false;
		double a0, a1, b0, b1, c0, c1, d0, d1;
		if (!residual(u + hu, v, a0, a1) || !residual(u - hu, v, b0, b1) || !residual(u, v + hv, c0, c1) ||
			!residual(u, v - hv, d0, d1))
			return false;
		const double j00 = (a0 - b0) / (2 * hu), j10 = (a1 - b1) / (2 * hu);
		const double j01 = (c0 - d0) / (2 * hv), j11 = (c1 - d1) / (2 * hv);
		const double det = j00 * j11 - j01 * j10;
		if (!std::isfinite(det) || det == 0)
			return false;
		const double du = (r0 * j11 - r1 * j01) / det, dv = (r1 * j00 - r0 * j10) / det;
		u -= du;
		v -= dv;
		if (std::abs(du) < hu * 1e-4 && std::abs(dv) < hv * 1e-4)
			return true;
	}
	return false;
}

// The crossing of the lines refined on the curves; the plane's point.
FplPoint Refine(const FplCurve& a, const FplCurve& b, const ParamTable& ta, const ParamTable& tb, FplPoint screen,
	const FplViewport& view) {
	FplPoint start = view.ToPlane(screen), p = start;
	bool ok = false;
	if (a.G && b.G) {
		const double hx = 1e-3 / view.DipsPerUnitX(p.x), hy = 1e-3 / view.DipsPerUnitY(p.y);
		ok = Newton2([&](double x, double y, double& r0, double& r1) {
			r0 = a.G(x, y);
			r1 = b.G(x, y);
			return true;
		}, p.x, p.y, hx, hy);
	}
	else if ((a.G && b.C) || (a.C && b.G)) {
		auto& G = a.G ? a.G : b.G;
		auto& C = a.G ? b.C : a.C;
		auto& table = a.G ? tb : ta;
		std::function<double(double)> along = [&](double t) {
			FplPoint q = C(t);
			return G(q.x, q.y);
		};
		// a bracket around the nearest sample, widening
		const double t = table.Nearest(screen);
		for (double h = table.Step(); h <= table.Step() * 64 && !ok; h *= 2) {
			const double t0 = t - h, t1 = t + h, f0 = along(t0), f1 = along(t1);
			if (std::isfinite(f0) && std::isfinite(f1) && f0 * f1 <= 0) {
				p = C(FplBrent(along, t0, t1, f0, f1));
				ok = true;
			}
		}
	}
	else if (a.C && b.C) {
		double t = ta.Nearest(screen), u = tb.Nearest(screen);
		ok = Newton2([&](double s, double w, double& r0, double& r1) {
			FplPoint q = a.C(s), r = b.C(w);
			r0 = q.x - r.x;
			r1 = q.y - r.y;
			return true;
		}, t, u, ta.Step() / 8, tb.Step() / 8);
		if (ok)
			p = a.C(t);
	}
	// the lines' crossing stands if it went anywhere else
	if (!ok || !std::isfinite(p.x) || !std::isfinite(p.y) || !(Distance(view.ToScreen(p), screen) <= MaxRefineMove))
		return start;
	return p;
}

// Stretches of one curve's vertices within TouchGap of the other's lines: the middles of the short ones, on the lines,
// and the long ones as they are.
void FindTouches(const std::vector<FplPolyline>& lines, SegmentGrid& other, std::vector<FplPoint>& touches,
	const std::vector<FplPoint>& crossings, std::vector<std::vector<FplPoint>>& together) {
	for (auto& line : lines) {
		double length = 0;
		std::vector<FplPoint> run;
		auto finish = [&] {
			if (length > LongestTouch)
				together.push_back(run);
			else if (run.size() >= 2) {
				// the gap grows the same way on either side of where they touch: the stretch's middle
				double half = length / 2, walked = 0;
				FplPoint mid = run.front();
				for (size_t i = 1; i < run.size(); i++) {
					const double d = Distance(run[i - 1], run[i]);
					if (walked + d >= half) {
						const double t = d > 0 ? (half - walked) / d : 0;
						mid = { run[i - 1].x + (run[i].x - run[i - 1].x) * t, run[i - 1].y + (run[i].y - run[i - 1].y) * t };
						break;
					}
					walked += d;
				}
				// a crossing near it is the same meeting
				bool crossed = false;
				for (auto& c : crossings)
					crossed = crossed || Distance(c, mid) <= length / 2 + 2;
				if (!crossed)
					touches.push_back(mid);
			}
			run.clear();
			length = 0;
		};
		for (auto& p : line) {
			double gap = std::numeric_limits<double>::infinity();
			other.ForCells(p.x - TouchGap, p.y - TouchGap, p.x + TouchGap, p.y + TouchGap, [&](auto& cell) {
				for (auto& [l, i] : cell)
					gap = std::min(gap, Distance(p, Closest(p, other.At(l, i), other.At(l, i + 1))));
			});
			if (gap <= TouchGap) {
				if (!run.empty())
					length += Distance(run.back(), p);
				run.push_back(p);
			}
			else
				finish();
		}
		finish();
	}
}

} // namespace

std::vector<FplFeature> FplFindCurveIntersections(const FplCurve& a, const FplCurve& b, const FplViewport& view) {
	std::vector<FplFeature> features;
	if (!a.lines || !b.lines || a.lines->empty() || b.lines->empty() || !view.Valid())
		return features;
	SegmentGrid gridA(*a.lines, view), gridB(*b.lines, view);
	std::vector<FplPoint> crossings;
	auto add = [&](FplPoint p, double within) {
		for (auto& c : crossings)
			if (Distance(c, p) <= within)
				return;
		crossings.push_back(p);
	};
	for (auto& line : *b.lines) {
		for (size_t i = 0; i + 1 < line.size(); i++) {
			const FplPoint p = line[i], q = line[i + 1];
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(q.x) || !std::isfinite(q.y))
				continue;
			const double dx = q.x - p.x, dy = q.y - p.y;
			gridA.ForCells(std::min(p.x, q.x), std::min(p.y, q.y), std::max(p.x, q.x), std::max(p.y, q.y), [&](auto& cell) {
				for (auto& [l, k] : cell) {
					const FplPoint r = gridA.At(l, k), s = gridA.At(l, k + 1);
					const double ex = s.x - r.x, ey = s.y - r.y, den = dx * ey - dy * ex;
					if (den == 0)
						continue;
					const double t = ((r.x - p.x) * ey - (r.y - p.y) * ex) / den;
					const double u = ((r.x - p.x) * dy - (r.y - p.y) * dx) / den;
					if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
						FplPoint at = { p.x + t * dx, p.y + t * dy };
						if (at.x >= 0 && at.y >= 0 && at.x <= view.width && at.y <= view.height)
							add(at, 1);		// a shared vertex is found twice; a segment crosses many cells
					}
				}
			});
		}
	}
	std::vector<FplPoint> touches;
	std::vector<std::vector<FplPoint>> together;
	FindTouches(*a.lines, gridB, touches, crossings, together);
	FindTouches(*b.lines, gridA, touches, crossings, together);
	// running together: two samplings of one curve cross again and again at tiny angles, which is no meeting. One or two
	// crossings in such a stretch are real (two lines at a shallow angle).
	for (auto& run : together) {
		std::vector<size_t> inRun;
		for (size_t c = 0; c < crossings.size(); c++) {
			bool near_ = false;
			for (auto& p : run)
				if (Distance(p, crossings[c]) <= 8) {
					near_ = true;
					break;
				}
			if (near_)
				inRun.push_back(c);
		}
		if (inRun.size() >= 3)
			for (size_t k = inRun.size(); k-- > 0;)
				crossings.erase(crossings.begin() + inRun[k]);
	}

	ParamTable tableA(a, view), tableB(b, view);
	std::vector<FplPoint> found;
	found.reserve(crossings.size() + touches.size());
	for (auto& c : crossings)
		found.push_back(Refine(a, b, tableA, tableB, c, view));
	for (auto& t : touches) {
		// the same touch from either curve is one
		bool known = false;
		for (auto& f : found)
			known = known || Distance(view.ToScreen(f), t) <= 2;
		if (known || t.x < 0 || t.y < 0 || t.x > view.width || t.y > view.height)
			continue;
		found.push_back(view.ToPlane(t));
	}
	features.reserve(found.size());
	for (auto& f : found) {
		bool same = false;
		for (auto& g : features)
			same = same || Distance(view.ToScreen({ g.x, g.y }), view.ToScreen(f)) <= 1e-3;
		if (!same)
			features.push_back({ FplFeatureKind::Intersection, f.x, f.y });
	}
	std::sort(features.begin(), features.end(), [](const FplFeature& p, const FplFeature& q) {
		return p.x != q.x ? p.x < q.x : p.y < q.y;
	});
	return features;
}
