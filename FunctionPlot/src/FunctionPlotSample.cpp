// Sampling: curves into polylines, implicit curves and regions by marching squares, an expression's geometry.

#include "FunctionPlotModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

bool Finite(FplPoint p) {
	return std::isfinite(p.x) && std::isfinite(p.y);
}

double Dist(FplPoint a, FplPoint b) {
	return std::hypot(b.x - a.x, b.y - a.y);
}

double DistToSegment(FplPoint p, FplPoint a, FplPoint b) {
	double dx = b.x - a.x, dy = b.y - a.y;
	double len2 = dx * dx + dy * dy;
	double u = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0;
	return std::hypot(p.x - (a.x + u * dx), p.y - (a.y + u * dy));
}

// The view grown by its own size on every side: what's sampled is clipped or clamped to it.
struct FarBox {
	double left, top, right, bottom;

	explicit FarBox(const FplViewport& v) : left(-v.width), top(-v.height), right(2 * v.width), bottom(2 * v.height) {
	}

	// Which sides of the box a point is beyond, as bits.
	unsigned Outside(FplPoint p) const {
		return (p.x < left ? 1u : 0u) | (p.x > right ? 2u : 0u) | (p.y < top ? 4u : 0u) | (p.y > bottom ? 8u : 0u);
	}

	FplPoint Clamp(FplPoint p) const {
		return { std::clamp(p.x, left, right), std::clamp(p.y, top, bottom) };
	}
};

// ---- Curves ----------------------------------------------------------------------------------------------------------

class CurveSampler {
public:
	CurveSampler(const std::function<FplPoint(double)>& curve, const FplViewport& view, const FplSampleOptions& o)
		: m_Curve(curve), m_View(view), m_Options(o), m_Far(view) {
	}

	std::vector<FplPolyline> Run(double t0, double t1, int samples) {
		samples = std::clamp(samples, 1, 1'000'000);
		Sample a = Eval(t0);
		if (a.ok)
			Add(a.p);
		for (int i = 1; i <= samples; i++) {
			Sample b = Eval(i == samples ? t1 : t0 + (t1 - t0) * i / samples);
			Segment(a, b, 0);
			a = b;
		}
		Break();
		return std::move(m_Lines);
	}

	size_t Evals() const { return m_Evals; }

private:
	struct Sample {
		double t = 0;
		FplPoint p;
		bool ok = false;
	};

	Sample Eval(double t) {
		m_Evals++;
		Sample s;
		s.t = t;
		FplPoint w = m_Curve(t);
		s.p = m_View.ToScreen(w);
		s.ok = Finite(s.p);
		return s;
	}

	bool Splittable(int depth) const {
		return depth < m_Options.maxDepth && m_Evals < m_Options.maxEvals;
	}

	void Add(FplPoint p) {
		m_Line.push_back(p);
	}

	void Break() {
		if (m_Line.size() >= 2)
			m_Lines.push_back(std::move(m_Line));
		m_Line.clear();
	}

	// The part after a up to b; a's point (if it's defined) is the last one added.
	void Segment(const Sample& a, const Sample& b, int depth) {
		if (!a.ok && !b.ok) {
			// undefined at both ends: look once inside for a piece that is defined
			if (depth >= 1 || m_Evals >= m_Options.maxEvals)
				return;
			Sample m = Eval((a.t + b.t) / 2);
			Segment(a, m, depth + 1);
			Segment(m, b, depth + 1);
			return;
		}
		if (a.ok != b.ok) {
			if (Splittable(depth)) {
				Sample m = Eval((a.t + b.t) / 2);
				Segment(a, m, depth + 1);
				Segment(m, b, depth + 1);
				return;
			}
			if (m_Evals < m_Options.maxEvals)
				Edge(a, b);
			else if (a.ok)
				Break();
			else
				Add(b.p);
			return;
		}
		Sample m = Eval((a.t + b.t) / 2);
		if (!m.ok) {
			if (Splittable(depth)) {
				Segment(a, m, depth + 1);
				Segment(m, b, depth + 1);
			}
			else {
				Break();
				Add(b.p);
			}
			return;
		}
		// far off on the same side: nothing there to see
		if (m_Far.Outside(a.p) & m_Far.Outside(m.p) & m_Far.Outside(b.p)) {
			Add(m.p);
			Add(b.p);
			return;
		}
		if (DistToSegment(m.p, a.p, b.p) <= m_Options.tolerance && Dist(a.p, b.p) <= m_Options.maxChord) {
			Add(m.p);
			Add(b.p);
			return;
		}
		if (Splittable(depth)) {
			Segment(a, m, depth + 1);
			Segment(m, b, depth + 1);
			return;
		}
		if (m_Evals >= m_Options.maxEvals) {
			Add(m.p);
			Add(b.p);
			return;
		}
		Jump(a, m, b);
	}

	// Where the domain stops between a and b (one of them defined): the last defined point, then a break.
	void Edge(const Sample& a, const Sample& b) {
		Sample good = a.ok ? a : b, bad = a.ok ? b : a;
		for (int i = 0; i < 64; i++) {
			double t = (good.t + bad.t) / 2;
			if (t == good.t || t == bad.t)
				break;
			Sample m = Eval(t);
			(m.ok ? good : bad) = m;
		}
		if (a.ok) {
			if (good.t != a.t)
				Add(good.p);
			Break();
		}
		else {
			Break();
			Add(good.p);
			if (good.t != b.t)
				Add(b.p);
		}
	}

	// Still bent as far as it's split: halve toward the bigger gap; one that doesn't close is a discontinuity.
	void Jump(const Sample& a, const Sample& m, const Sample& b) {
		Sample lo = a, hi = b;
		for (int i = 0; i < 64; i++) {
			double t = (lo.t + hi.t) / 2;
			if (t == lo.t || t == hi.t)
				break;
			Sample c = Eval(t);
			if (!c.ok) {
				// a hole
				if (lo.t != a.t)
					Add(lo.p);
				Break();
				if (hi.t != b.t)
					Add(hi.p);
				Add(b.p);
				return;
			}
			if (Dist(lo.p, c.p) >= Dist(c.p, hi.p))
				hi = c;
			else
				lo = c;
		}
		if (Dist(lo.p, hi.p) > m_Options.jump) {
			if (lo.t != a.t)
				Add(lo.p);
			Break();
			Add(hi.p);
			if (hi.t != b.t)
				Add(b.p);
		}
		else {
			Add(m.p);
			Add(b.p);
		}
	}

	const std::function<FplPoint(double)>& m_Curve;
	const FplViewport& m_View;
	const FplSampleOptions& m_Options;
	FarBox m_Far;
	size_t m_Evals = 0;
	std::vector<FplPolyline> m_Lines;
	FplPolyline m_Line;
};

std::vector<FplPolyline> SampleRaw(const std::function<FplPoint(double)>& curve, double t0, double t1, int samples,
	const FplViewport& view, const FplSampleOptions& options, size_t* evals) {
	if (!view.Valid() || !std::isfinite(t0) || !std::isfinite(t1) || t0 == t1)
		return {};
	CurveSampler sampler(curve, view, options);
	auto lines = sampler.Run(t0, t1, samples);
	if (evals)
		*evals += sampler.Evals();
	return lines;
}

// ---- Marching squares ------------------------------------------------------------------------------------------------

class Marcher {
public:
	Marcher(const std::function<double(double, double)>& F, const FplViewport& view, const FplSampleOptions& o)
		: m_F(F), m_View(view), m_Options(o) {
		double cell = std::max(o.cellSize, 1.0);
		// one cell more on every side, so curves reach the view's edges
		m_X0 = -cell;
		m_Y0 = -cell;
		m_Cx = static_cast<int>(std::ceil(view.width / cell)) + 2;
		m_Cy = static_cast<int>(std::ceil(view.height / cell)) + 2;
		// the fine grid's vertices are kept in arrays: no more than 8 million of them
		int depth = std::clamp(o.cellDepth, 0, 8);
		while (depth > 0 && static_cast<double>(m_Cx << depth) * (m_Cy << depth) > 8e6)
			depth--;
		m_Fine = 1 << depth;
		m_H = cell / m_Fine;
		m_Nx = m_Cx * m_Fine + 1;
		m_Ny = m_Cy * m_Fine + 1;
		size_t vertices = static_cast<size_t>(m_Nx) * m_Ny;
		m_Values.resize(vertices);
		m_Known.assign(vertices, 0);
		m_EdgeSlot.assign(vertices * 2, -1);
		for (int j = 0; j <= m_Cy; j++)
			for (int i = 0; i <= m_Cx; i++)
				Value(i * m_Fine, j * m_Fine);
		m_Active.assign(static_cast<size_t>(m_Cx) * m_Cy, 0);
	}

	size_t Evals() const { return m_Evals; }

	// Finds the cells the curve goes through, at the fine level.
	void March(bool fills) {
		m_Fills = fills;
		std::vector<int> work;
		for (int cy = 0; cy < m_Cy; cy++) {
			for (int cx = 0; cx < m_Cx; cx++) {
				double v[4] = { CoarseValue(cx, cy), CoarseValue(cx + 1, cy), CoarseValue(cx + 1, cy + 1),
					CoarseValue(cx, cy + 1) };
				int inside = 0, outside = 0;
				for (double x : v) {
					if (x < 0)
						inside++;
					else if (x >= 0)
						outside++;
				}
				if (inside && outside)
					Activate(cx, cy, work);
			}
		}
		while (!work.empty()) {
			int c = work.back();
			work.pop_back();
			Cell(c % m_Cx, c / m_Cx, work);
		}
		if (fills)
			FillQuietCells();
	}

	std::vector<FplPolyline> Lines() {
		std::vector<FplPolyline> lines;
		// segments by edge
		std::vector<bool> used(m_Segments.size());
		auto other = [&](size_t key, size_t seg) -> int {
			auto& c = Cross(key);
			for (int s : c.segs)
				if (s >= 0 && static_cast<size_t>(s) != seg && !used[s])
					return s;
			return -1;
		};
		for (size_t i = 0; i < m_Segments.size(); i++) {
			if (used[i])
				continue;
			used[i] = true;
			std::vector<size_t> keys = { m_Segments[i].a, m_Segments[i].b };
			// forward from b
			for (;;) {
				size_t end = keys.back();
				int s = other(end, i);
				if (s < 0)
					break;
				used[s] = true;
				keys.push_back(m_Segments[s].a == end ? m_Segments[s].b : m_Segments[s].a);
				if (keys.back() == keys.front())
					break;
			}
			if (keys.back() != keys.front()) {
				// backward from a
				std::vector<size_t> back;
				size_t end = keys.front();
				for (;;) {
					int s = other(end, i);
					if (s < 0)
						break;
					used[s] = true;
					end = m_Segments[s].a == end ? m_Segments[s].b : m_Segments[s].a;
					back.push_back(end);
				}
				keys.insert(keys.begin(), back.rbegin(), back.rend());
			}
			FplPolyline line;
			line.reserve(keys.size());
			for (size_t k : keys)
				line.push_back(Cross(k).p);
			lines.push_back(std::move(line));
		}
		return lines;
	}

	std::vector<FplPolygon> TakeFills() { return std::move(m_Polygons); }

private:
	struct Crossing {
		FplPoint p;
		bool jump = false;
		int segs[2] = { -1, -1 };
	};

	struct Seg {
		size_t a, b;
	};

	double Eval(int i, int j) {
		m_Evals++;
		return m_F(m_View.ToPlaneX(m_X0 + i * m_H), m_View.ToPlaneY(m_Y0 + j * m_H));
	}

	size_t Vertex(int i, int j) const {
		return static_cast<size_t>(j) * m_Nx + i;
	}

	double CoarseValue(int cx, int cy) const {
		return m_Values[Vertex(cx * m_Fine, cy * m_Fine)];
	}

	double Value(int i, int j) {
		size_t k = Vertex(i, j);
		if (!m_Known[k]) {
			m_Values[k] = Eval(i, j);
			m_Known[k] = 1;
		}
		return m_Values[k];
	}

	Crossing& Cross(size_t key) {
		return m_CrossingList[m_EdgeSlot[key]];
	}

	FplPoint Screen(double i, double j) const {
		return { m_X0 + i * m_H, m_Y0 + j * m_H };
	}

	void Activate(int cx, int cy, std::vector<int>& work) {
		if (cx < 0 || cy < 0 || cx >= m_Cx || cy >= m_Cy)
			return;
		auto& a = m_Active[static_cast<size_t>(cy) * m_Cx + cx];
		if (a)
			return;
		a = 1;
		work.push_back(cy * m_Cx + cx);
	}

	// Edge key: the vertex it starts at and its direction (0 right, 1 down).
	size_t EdgeKey(int i, int j, int dir) const {
		return Vertex(i, j) * 2 + dir;
	}

	// Where F is zero on an edge whose ends' signs differ: halved down to the doubles; a sign change whose values
	// don't shrink toward zero is a jump.
	Crossing& CrossingAt(int i, int j, int dir, double v0, double v1) {
		size_t key = EdgeKey(i, j, dir);
		if (m_EdgeSlot[key] >= 0)
			return Cross(key);
		Crossing c;
		FplPoint p0 = Screen(i, j), p1 = dir == 0 ? Screen(i + 1, j) : Screen(i, j + 1);
		auto at = [&](double u) { return FplPoint{ p0.x + (p1.x - p0.x) * u, p0.y + (p1.y - p0.y) * u }; };
		// Illinois: regula falsi with the end that stays put halved, so it doesn't stall; ra and rb are the real
		// values at the ends
		double a = 0, b = 1, va = v0, vb = v1, ra = v0, rb = v1;
		int side = 0;
		double last = -1;
		const double small = 1e-12 * (std::abs(v0) + std::abs(v1));
		for (int k = 0; k < 20 && m_Evals < m_Options.maxEvals; k++) {
			double u = a + (b - a) * va / (va - vb);
			if (!(u > a && u < b))
				u = (a + b) / 2;
			FplPoint p = at(u);
			m_Evals++;
			double v = m_F(m_View.ToPlaneX(p.x), m_View.ToPlaneY(p.y));
			if (std::isnan(v)) {
				c.jump = true;
				break;
			}
			if ((v < 0) == (va < 0)) {
				a = u;
				va = ra = v;
				if (side == -1)
					vb /= 2;
				side = -1;
			}
			else {
				b = u;
				vb = rb = v;
				if (side == 1)
					va /= 2;
				side = 1;
			}
			// a millionth of an edge is plenty
			if (std::abs(v) <= small || b - a < 1e-9 || std::abs(u - last) < 1e-6)
				break;
			last = u;
		}
		// a zero's values shrink toward it; a jump's (an asymptote, a branch cut, a step) don't
		if (std::min(std::abs(ra), std::abs(rb)) > 0.25 * std::max(std::abs(v0), std::abs(v1)))
			c.jump = true;
		double u = std::abs(ra) <= std::abs(rb) ? a : b;
		if (std::isfinite(ra) && std::isfinite(rb) && ra != rb)
			u = std::clamp(a + (b - a) * ra / (ra - rb), a, b);
		c.p = at(u);
		m_EdgeSlot[key] = static_cast<int>(m_CrossingList.size());
		m_CrossingList.push_back(c);
		return m_CrossingList.back();
	}

	void AddSegment(size_t a, size_t b) {
		auto& ca = Cross(a);
		auto& cb = Cross(b);
		if (ca.jump || cb.jump)
			return;
		int index = static_cast<int>(m_Segments.size());
		m_Segments.push_back({ a, b });
		(ca.segs[0] < 0 ? ca.segs[0] : ca.segs[1]) = index;
		(cb.segs[0] < 0 ? cb.segs[0] : cb.segs[1]) = index;
	}

	// One active cell at the fine level: segments, fills, and the neighbors the curve goes on into.
	void Cell(int cx, int cy, std::vector<int>& work) {
		int i0 = cx * m_Fine, j0 = cy * m_Fine;
		for (int fj = 0; fj < m_Fine; fj++) {
			int runStart = -1;	// a run of fine cells all inside, filled as one rectangle
			for (int fi = 0; fi <= m_Fine; fi++) {
				bool full = false;
				if (fi < m_Fine)
					full = FineCell(i0 + fi, j0 + fj, cx, cy, work);
				if (full && runStart < 0)
					runStart = fi;
				if (!full && runStart >= 0) {
					Rect(Screen(i0 + runStart, j0 + fj), Screen(i0 + fi, j0 + fj + 1));
					runStart = -1;
				}
			}
		}
	}

	// Returns true when the cell is all inside (for the caller to fill with its neighbors).
	bool FineCell(int i, int j, int cx, int cy, std::vector<int>& work) {
		// corners clockwise on the screen: top left, top right, bottom right, bottom left
		const int ci[4] = { i, i + 1, i + 1, i }, cj[4] = { j, j, j + 1, j + 1 };
		double v[4];
		bool in[4];
		int count = 0;
		for (int k = 0; k < 4; k++) {
			v[k] = Value(ci[k], cj[k]);
			if (std::isnan(v[k]))
				return false;
			in[k] = v[k] < 0;
			count += in[k];
		}
		if (count == 4)
			return true;
		if (count == 0)
			return false;

		// edge k goes from corner k to corner k + 1
		auto edgeKeyOf = [&](int k, double& a, double& b) {
			a = v[k];
			b = v[(k + 1) & 3];
			switch (k) {
			case 0: return EdgeKey(i, j, 0);
			case 1: return EdgeKey(i + 1, j, 1);
			case 2: a = v[3]; b = v[2]; return EdgeKey(i, j + 1, 0);
			default: a = v[0]; b = v[3]; return EdgeKey(i, j, 1);
			}
		};
		size_t keys[4] = {};
		for (int k = 0; k < 4; k++) {
			if (in[k] == in[(k + 1) & 3]) {
				continue;
			}
			double a, b;
			keys[k] = edgeKeyOf(k, a, b);
			int ei = k == 1 ? i + 1 : i, ej = k == 2 ? j + 1 : j;
			CrossingAt(ei, ej, k == 0 || k == 2 ? 0 : 1, a, b);
			// a crossing on the coarse cell's border: the curve goes on into the neighbor
			if (k == 0 && j % m_Fine == 0)
				Activate(cx, cy - 1, work);
			if (k == 2 && (j + 1) % m_Fine == 0)
				Activate(cx, cy + 1, work);
			if (k == 3 && i % m_Fine == 0)
				Activate(cx - 1, cy, work);
			if (k == 1 && (i + 1) % m_Fine == 0)
				Activate(cx + 1, cy, work);
		}

		// runs of the boundary inside: each enters at an edge, goes through inside corners, leaves at an edge
		struct Run {
			int enter, exit;
		} runs[2];
		int nruns = 0;
		for (int k = 0; k < 4; k++) {
			if (!in[k] && in[(k + 1) & 3]) {
				int e = (k + 1) & 3;
				while (in[(e + 1) & 3])
					e = (e + 1) & 3;
				runs[nruns++] = { k, e };
			}
		}
		bool joined = false;
		if (nruns == 2) {
			m_Evals++;
			FplPoint c = Screen(i + 0.5, j + 0.5);
			double center = m_F(m_View.ToPlaneX(c.x), m_View.ToPlaneY(c.y));
			if (std::isnan(center))
				center = (v[0] + v[1] + v[2] + v[3]) / 4;
			joined = center < 0;
		}
		if (joined) {
			AddSegment(keys[runs[0].exit], keys[runs[1].enter]);
			AddSegment(keys[runs[1].exit], keys[runs[0].enter]);
		}
		else {
			for (int r = 0; r < nruns; r++)
				AddSegment(keys[runs[r].exit], keys[runs[r].enter]);
		}
		if (m_Fills) {
			auto runPoints = [&](const Run& run, FplPolygon& poly) {
				poly.push_back(Cross(keys[run.enter]).p);
				for (int k = (run.enter + 1) & 3;; k = (k + 1) & 3) {
					poly.push_back(Screen(ci[k], cj[k]));
					if (k == run.exit)
						break;
				}
				poly.push_back(Cross(keys[run.exit]).p);
			};
			if (joined) {
				FplPolygon poly;
				runPoints(runs[0], poly);
				runPoints(runs[1], poly);
				m_Polygons.push_back(std::move(poly));
			}
			else {
				for (int r = 0; r < nruns; r++) {
					FplPolygon poly;
					runPoints(runs[r], poly);
					m_Polygons.push_back(std::move(poly));
				}
			}
		}
		return false;
	}

	void Rect(FplPoint tl, FplPoint br) {
		if (m_Fills)
			m_Polygons.push_back({ tl, { br.x, tl.y }, br, { tl.x, br.y } });
	}

	// The cells the curve doesn't go through that are all inside, a row's runs as one rectangle each.
	void FillQuietCells() {
		for (int cy = 0; cy < m_Cy; cy++) {
			int runStart = -1;
			for (int cx = 0; cx <= m_Cx; cx++) {
				bool full = false;
				if (cx < m_Cx && !m_Active[static_cast<size_t>(cy) * m_Cx + cx])
					full = CoarseValue(cx, cy) < 0 && CoarseValue(cx + 1, cy) < 0 && CoarseValue(cx + 1, cy + 1) < 0 &&
						CoarseValue(cx, cy + 1) < 0;
				if (full && runStart < 0)
					runStart = cx;
				if (!full && runStart >= 0) {
					Rect(Screen(runStart * m_Fine, cy * m_Fine), Screen(cx * m_Fine, (cy + 1) * m_Fine));
					runStart = -1;
				}
			}
		}
	}

	const std::function<double(double, double)>& m_F;
	const FplViewport& m_View;
	const FplSampleOptions& m_Options;
	int m_Fine = 1;
	double m_H = 1, m_X0 = 0, m_Y0 = 0;
	int m_Cx = 0, m_Cy = 0;
	std::vector<char> m_Active;
	int m_Nx = 0, m_Ny = 0;					// the fine grid's vertices across and down
	std::vector<double> m_Values;			// by vertex, once known
	std::vector<char> m_Known;
	std::vector<int> m_EdgeSlot;			// by edge (vertex * 2 + direction): its crossing's index, or -1
	std::vector<Crossing> m_CrossingList;
	std::vector<Seg> m_Segments;
	std::vector<FplPolygon> m_Polygons;
	bool m_Fills = false;
	size_t m_Evals = 0;
};

// A function's piece filled to the far box's edge (below, above, left or right).
FplPolygon FillTo(const FplPolyline& line, const FarBox& box, bool vertical, double edge) {
	FplPolygon poly;
	poly.reserve(line.size() + 2);
	for (auto& p : line)
		poly.push_back(box.Clamp(p));
	if (vertical) {
		poly.push_back({ poly.back().x, edge });
		poly.push_back({ poly.front().x, edge });
	}
	else {
		poly.push_back({ edge, poly.back().y });
		poly.push_back({ edge, poly.front().y });
	}
	return poly;
}

} // namespace

std::vector<FplPolyline> FplClip(const std::vector<FplPolyline>& lines, double left, double top, double right,
	double bottom) {
	std::vector<FplPolyline> out;
	FplPolyline cur;
	auto flush = [&] {
		if (cur.size() >= 2)
			out.push_back(std::move(cur));
		cur.clear();
	};
	for (auto& line : lines) {
		for (size_t i = 0; i + 1 < line.size(); i++) {
			FplPoint p = line[i], q = line[i + 1];
			// Liang-Barsky, remembering which edge each end was cut at
			double t0 = 0, t1 = 1, dx = q.x - p.x, dy = q.y - p.y;
			int edge0 = -1, edge1 = -1;
			auto edge = [&](double pp, double qq, int which) {
				if (pp == 0)
					return qq >= 0;
				double r = qq / pp;
				if (pp < 0) {
					if (r > t1)
						return false;
					if (r > t0) {
						t0 = r;
						edge0 = which;
					}
				}
				else {
					if (r < t0)
						return false;
					if (r < t1) {
						t1 = r;
						edge1 = which;
					}
				}
				return true;
			};
			// both ends beyond the same edge: out (the parameters below can round to a touch when an end is far)
			bool visible = !((p.x < left && q.x < left) || (p.x > right && q.x > right) || (p.y < top && q.y < top) ||
				(p.y > bottom && q.y > bottom));
			visible = visible && edge(-dx, p.x - left, 0) && edge(dx, right - p.x, 1) && edge(-dy, p.y - top, 2) &&
				edge(dy, bottom - p.y, 3);
			if (!visible || t0 > t1) {
				flush();
				continue;
			}
			// the point where the segment crosses an edge: on it exactly, the other coordinate interpolated from the
			// nearer end (an end can be 1e300 away, where p + t * d keeps no digits)
			auto cut = [&](int which) -> FplPoint {
				double e = which == 0 ? left : which == 1 ? right : which == 2 ? top : bottom;
				if (which < 2) {
					double y = std::abs(e - p.x) <= std::abs(q.x - e) ? p.y + (e - p.x) / dx * dy
						: q.y - (q.x - e) / dx * dy;
					return { e, std::clamp(y, top, bottom) };
				}
				double x = std::abs(e - p.y) <= std::abs(q.y - e) ? p.x + (e - p.y) / dy * dx
					: q.x - (q.y - e) / dy * dx;
				return { std::clamp(x, left, right), e };
			};
			if (cur.empty() || edge0 >= 0) {
				flush();
				cur.push_back(edge0 >= 0 ? cut(edge0) : p);
			}
			cur.push_back(edge1 >= 0 ? cut(edge1) : q);
			if (edge1 >= 0)
				flush();
		}
		flush();
	}
	return out;
}

std::vector<FplPolyline> FplSampleCurve(const std::function<FplPoint(double)>& curve, double t0, double t1,
	int samples, const FplViewport& view, const FplSampleOptions& options, size_t* evals) {
	auto lines = SampleRaw(curve, t0, t1, samples, view, options, evals);
	FarBox box(view);
	return FplClip(lines, box.left, box.top, box.right, box.bottom);
}

std::vector<FplPolyline> FplSampleImplicit(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options, size_t* evals) {
	if (!view.Valid())
		return {};
	Marcher m(F, view, options);
	m.March(false);
	if (evals)
		*evals += m.Evals();
	return m.Lines();
}

std::vector<FplPolygon> FplSampleRegion(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options, size_t* evals) {
	if (!view.Valid())
		return {};
	Marcher m(F, view, options);
	m.March(true);
	if (evals)
		*evals += m.Evals();
	return m.TakeFills();
}

FplGeometry FplSample(const FplExpression& e, std::vector<double> slots, const FplViewport& view,
	const FplSampleOptions& options) {
	FplGeometry g;
	if (e.error || !view.Valid() || e.kind == FplKind::Definition || e.f.Empty())
		return g;
	if (slots.size() < FplFirstParam)
		slots.resize(FplFirstParam, 0);
	double* s = slots.data();
	FarBox box(view);
	g.dashed = e.rel == FplRel::Lt || e.rel == FplRel::Gt;
	bool region = e.rel != FplRel::Eq;
	// "below" for y < f(x), x < f(y), r < f(theta), F < 0
	bool below = e.rel == FplRel::Lt || e.rel == FplRel::Le;

	switch (e.kind) {
	case FplKind::Function:
	case FplKind::FunctionOfY: {
		bool ofX = e.kind == FplKind::Function;
		int slot = ofX ? FplSlotX : FplSlotY;
		std::function<FplPoint(double)> curve = [&](double t) -> FplPoint {
			s[slot] = t;
			double v = e.f.Eval(s);
			return ofX ? FplPoint{ t, v } : FplPoint{ v, t };
		};
		double extent = ofX ? view.width : view.height;
		int samples = static_cast<int>(std::ceil(extent / std::max(options.initialStep, 0.1)));
		auto lines = ofX ? SampleRaw(curve, view.xMin, view.xMax, samples, view, options, &g.evals)
			: SampleRaw(curve, view.yMin, view.yMax, samples, view, options, &g.evals);
		if (region) {
			double edge = ofX ? (below ? box.bottom : box.top) : (below ? box.left : box.right);
			for (auto& line : lines)
				g.fills.push_back(FillTo(line, box, ofX, edge));
		}
		g.lines = FplClip(lines, box.left, box.top, box.right, box.bottom);
		break;
	}
	case FplKind::Polar: {
		std::function<FplPoint(double)> curve = [&](double theta) -> FplPoint {
			s[FplSlotTheta] = theta;
			double r = e.f.Eval(s);
			return { r * std::cos(theta), r * std::sin(theta) };
		};
		g.lines = FplSampleCurve(curve, options.thetaMin, options.thetaMax, options.curveSamples, view, options,
			&g.evals);
		if (region) {
			std::function<double(double, double)> F = [&](double x, double y) {
				s[FplSlotTheta] = std::atan2(y, x);
				double d = std::hypot(x, y) - e.f.Eval(s);
				return below ? d : -d;
			};
			g.fills = FplSampleRegion(F, view, options, &g.evals);
		}
		break;
	}
	case FplKind::Parametric: {
		std::function<FplPoint(double)> curve = [&](double t) -> FplPoint {
			s[FplSlotT] = t;
			return { e.f.Eval(s), e.g.Eval(s) };
		};
		g.lines = FplSampleCurve(curve, options.tMin, options.tMax, options.curveSamples, view, options, &g.evals);
		break;
	}
	case FplKind::Implicit: {
		std::function<double(double, double)> F = [&](double x, double y) {
			s[FplSlotX] = x;
			s[FplSlotY] = y;
			double v = e.f.Eval(s);
			return region && !below ? -v : v;
		};
		g.lines = FplSampleImplicit(F, view, options, &g.evals);
		if (region)
			g.fills = FplSampleRegion(F, view, options, &g.evals);
		break;
	}
	case FplKind::Surface: {
		std::function<double(double, double)> F = [&](double x, double y) {
			s[FplSlotX] = x;
			s[FplSlotY] = y;
			return e.f.Eval(s);
		};
		// the grid always: its range picks the contours' levels
		FplHeatmap heat = FplSampleHeatmap(F, view, options, &g.evals);
		if (options.contours && !heat.Empty())
			g.contours = FplSampleContours(F, FplContourLevels(heat.lo, heat.hi, options.contourLevels), view, options,
				&g.evals);
		if (options.heatmap)
			g.heatmap = std::move(heat);
		break;
	}
	case FplKind::VectorField: {
		std::function<FplPoint(double, double)> field = [&](double x, double y) {
			s[FplSlotX] = x;
			s[FplSlotY] = y;
			return FplPoint{ e.f.Eval(s), e.g.Eval(s) };
		};
		g.arrows = FplSampleField(field, view, options, &g.evals);
		break;
	}
	case FplKind::Integral: {
		// the area between the integrand and the x axis from lo to hi, and the integrand over it
		double lo = e.g.Eval(s), hi = e.h.Eval(s);
		int slot = FplStandardSlot(e.name);
		if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi || slot < 0)
			break;
		if (lo > hi)
			std::swap(lo, hi);
		std::function<FplPoint(double)> curve = [&](double t) -> FplPoint {
			s[slot] = t;
			return { t, e.f.Eval(s) };
		};
		int samples = std::clamp(static_cast<int>(std::ceil((hi - lo) * view.ScaleX() / std::max(options.initialStep, 0.1))),
			8, 100000);
		auto lines = SampleRaw(curve, lo, hi, samples, view, options, &g.evals);
		const double axis = std::clamp(view.ToScreenY(0), box.top, box.bottom);
		for (auto& line : lines) {
			FplPolygon poly;
			poly.reserve(line.size() + 2);
			for (auto& p : line)
				poly.push_back(box.Clamp(p));
			poly.push_back({ poly.back().x, axis });
			poly.push_back({ poly.front().x, axis });
			g.fills.push_back(std::move(poly));
		}
		g.lines = FplClip(lines, box.left, box.top, box.right, box.bottom);
		break;
	}
	case FplKind::Point: {
		FplPoint p = view.ToScreen({ e.f.Eval(s), e.g.Eval(s) });
		if (Finite(p))
			g.points.push_back(p);
		break;
	}
	default:
		break;
	}
	return g;
}

// ---- Surfaces and vector fields --------------------------------------------------------------------------------------

namespace {

// The p-th fraction of the values (they're reordered).
double Percentile(std::vector<double>& values, double p) {
	size_t k = std::min(values.size() - 1, static_cast<size_t>(p * (values.size() - 1) + 0.5));
	std::nth_element(values.begin(), values.begin() + k, values.end());
	return values[k];
}

} // namespace

FplHeatmap FplSampleHeatmap(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options, size_t* evals) {
	FplHeatmap h;
	if (!view.Valid())
		return h;
	h.cell = std::max(options.heatCell, 1.0);
	h.cols = static_cast<int>(std::ceil(view.width / h.cell));
	h.rows = static_cast<int>(std::ceil(view.height / h.cell));
	h.values.resize(static_cast<size_t>(h.cols) * h.rows);
	std::vector<double> finite;
	finite.reserve(h.values.size());
	for (int r = 0; r < h.rows; r++) {
		double y = view.ToPlaneY((r + 0.5) * h.cell);
		for (int c = 0; c < h.cols; c++) {
			double v = F(view.ToPlaneX((c + 0.5) * h.cell), y);
			h.values[static_cast<size_t>(r) * h.cols + c] = static_cast<float>(v);
			if (std::isfinite(v))
				finite.push_back(v);
		}
	}
	if (evals)
		*evals += h.values.size();
	if (finite.empty()) {
		h.values.clear();
		return h;
	}
	h.lo = Percentile(finite, 0.02);
	h.hi = Percentile(finite, 0.98);
	if (!(h.hi > h.lo)) {
		double pad = std::max(1.0, std::abs(h.lo)) * 0.5;
		h.lo -= pad;
		h.hi += pad;
	}
	return h;
}

std::vector<double> FplContourLevels(double lo, double hi, int count) {
	std::vector<double> levels;
	if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo) || count < 1)
		return levels;
	// ticks over the range as if it were count * 10 DIPs with 10 between them
	FplTicks t = FplMakeTicks(lo, hi, count * 10.0, 10.0);
	return t.major;
}

std::vector<FplContour> FplSampleContours(const std::function<double(double, double)>& F,
	const std::vector<double>& levels, const FplViewport& view, const FplSampleOptions& options, size_t* evals) {
	std::vector<FplContour> contours;
	contours.reserve(levels.size());
	for (double level : levels) {
		std::function<double(double, double)> shifted = [&](double x, double y) { return F(x, y) - level; };
		FplContour c;
		c.level = level;
		c.lines = FplSampleImplicit(shifted, view, options, evals);
		if (!c.lines.empty())
			contours.push_back(std::move(c));
	}
	return contours;
}

std::vector<FplArrow> FplSampleField(const std::function<FplPoint(double, double)>& field, const FplViewport& view,
	const FplSampleOptions& options, size_t* evals) {
	std::vector<FplArrow> arrows;
	if (!view.Valid())
		return arrows;
	const double spacing = std::max(options.arrowSpacing, 8.0);
	FplTicks tx = FplMakeTicks(view.xMin, view.xMax, view.width, spacing);
	FplTicks ty = FplMakeTicks(view.yMin, view.yMax, view.height, spacing);
	if (tx.major.empty() || ty.major.empty())
		return arrows;
	// the room an arrow has: the grid's smaller step on the screen
	const double room = std::min(tx.step * view.ScaleX(), ty.step * view.ScaleY());
	struct Raw {
		FplPoint center;
		double dx, dy, m;
	};
	std::vector<Raw> raw;
	raw.reserve(tx.major.size() * ty.major.size());
	std::vector<double> magnitudes;
	for (double y : ty.major) {
		for (double x : tx.major) {
			FplPoint v = field(x, y);
			if (!std::isfinite(v.x) || !std::isfinite(v.y))
				continue;
			// the direction on the screen (y down; the axes' scales may differ)
			double dx = v.x * view.ScaleX(), dy = -v.y * view.ScaleY();
			double m = std::hypot(v.x, v.y);
			raw.push_back({ view.ToScreen({ x, y }), dx, dy, m });
			magnitudes.push_back(m);
		}
	}
	if (evals)
		*evals += tx.major.size() * ty.major.size();
	if (raw.empty())
		return arrows;
	double top = Percentile(magnitudes, 0.95);
	const double longest = 0.85 * room;
	arrows.reserve(raw.size());
	for (auto& r : raw) {
		double screen = std::hypot(r.dx, r.dy);
		if (r.m == 0 || screen == 0)
			continue;
		double len = top > 0 ? longest * std::min(1.0, r.m / top) : longest;
		len = std::max(len, 0.2 * longest);
		double ux = r.dx / screen * len / 2, uy = r.dy / screen * len / 2;
		arrows.push_back({ { r.center.x - ux, r.center.y - uy }, { r.center.x + ux, r.center.y + uy }, r.m });
	}
	return arrows;
}

uint32_t FplColormap(double t) {
	// viridis, by its stops at 0, 1/4, 1/2, 3/4, 1
	static const double Stops[5][3] = {
		{ 68, 1, 84 }, { 59, 82, 139 }, { 33, 145, 140 }, { 94, 201, 98 }, { 253, 231, 37 },
	};
	if (!std::isfinite(t))
		t = 0;
	t = std::clamp(t, 0.0, 1.0) * 4;
	int i = std::min(3, static_cast<int>(t));
	double f = t - i;
	uint32_t rgb[3];
	for (int k = 0; k < 3; k++)
		rgb[k] = static_cast<uint32_t>(std::lround(Stops[i][k] + (Stops[i + 1][k] - Stops[i][k]) * f));
	return rgb[0] | (rgb[1] << 8) | (rgb[2] << 16);
}
