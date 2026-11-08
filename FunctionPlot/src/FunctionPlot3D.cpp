// The 3D model: the box, the camera, and the meshes.

#include "FunctionPlot3D.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float Pi = 3.14159265358979f;

FplVec3 Sub(FplVec3 a, FplVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
FplVec3 Add(FplVec3 a, FplVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
FplVec3 Scale(FplVec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float Dot(FplVec3 a, FplVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
FplVec3 Cross(FplVec3 a, FplVec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Length(FplVec3 a) { return std::sqrt(Dot(a, a)); }

FplVec3 Normalize(FplVec3 a, FplVec3 fallback = { 0, 0, 1 }) {
	float l = Length(a);
	return l > 1e-12f ? Scale(a, 1 / l) : fallback;
}

bool Finite(FplPoint3 p) {
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

double Distance(FplVec3 a, FplVec3 b) {
	return Length(Sub(a, b));
}

// A gap in the cube between a and b (at s0 and s1) that doesn't close as it's halved toward its bigger half: a jump.
// point(s, out) gives the point at s in the cube, false where undefined.
template <typename F>
bool IsJump(F point, double s0, double s1, FplVec3 a, FplVec3 b) {
	const double gap = Distance(a, b);
	if (gap < 0.05)
		return false;
	double lo = s0, hi = s1;
	FplVec3 pa = a, pb = b;
	for (int i = 0; i < 16; i++) {
		double m = (lo + hi) / 2;
		FplVec3 pm;
		if (!point(m, pm))
			return true;
		if (Distance(pa, pm) >= Distance(pm, pb)) {
			hi = m;
			pb = pm;
		}
		else {
			lo = m;
			pa = pm;
		}
	}
	return Distance(pa, pb) > 0.5 * gap;
}

// A grid of points (or none where undefined) into a mesh: normals from the neighbors, cells across a jump left out.
// edgeJump(i0, j0, i1, j1) tells whether the edge between two grid points is a jump.
template <typename J>
FplMesh GridMesh(const std::vector<FplVec3>& p, const std::vector<char>& ok, int n, J edgeJump) {
	FplMesh mesh;
	const int w = n + 1;
	auto at = [&](int i, int j) { return j * w + i; };
	mesh.vertices.resize(p.size());
	for (int j = 0; j <= n; j++) {
		for (int i = 0; i <= n; i++) {
			int k = at(i, j);
			auto& v = mesh.vertices[k];
			v.pos = p[k];
			v.value = std::clamp((p[k].z + 1) / 2, 0.0f, 1.0f);
			v.u = static_cast<float>(i);
			v.v = static_cast<float>(j);
			if (!ok[k])
				continue;
			// tangents by the neighbors on each side (one side at an edge or an undefined neighbor)
			int i0 = i > 0 && ok[at(i - 1, j)] ? i - 1 : i, i1 = i < n && ok[at(i + 1, j)] ? i + 1 : i;
			int j0 = j > 0 && ok[at(i, j - 1)] ? j - 1 : j, j1 = j < n && ok[at(i, j + 1)] ? j + 1 : j;
			FplVec3 du = Sub(p[at(i1, j)], p[at(i0, j)]), dv = Sub(p[at(i, j1)], p[at(i, j0)]);
			v.normal = Normalize(Cross(du, dv));
		}
	}
	// the edges' jumps, each tested once: right of (i, j) and above it
	std::vector<char> right(p.size(), 0), up(p.size(), 0), diag(p.size(), 0);
	for (int j = 0; j <= n; j++)
		for (int i = 0; i <= n; i++) {
			int k = at(i, j);
			if (!ok[k])
				continue;
			if (i < n && ok[at(i + 1, j)])
				right[k] = edgeJump(i, j, i + 1, j);
			if (j < n && ok[at(i, j + 1)])
				up[k] = edgeJump(i, j, i, j + 1);
			if (i < n && j < n && ok[at(i + 1, j + 1)])
				diag[k] = edgeJump(i, j, i + 1, j + 1);
		}
	mesh.indices.reserve(static_cast<size_t>(n) * n * 6);
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			int a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
			// a b c and a c d, each when its corners are defined and none of its edges jumps
			if (ok[a] && ok[b] && ok[c] && !right[a] && !up[b] && !diag[a])
				mesh.indices.insert(mesh.indices.end(), { (uint32_t)a, (uint32_t)b, (uint32_t)c });
			if (ok[a] && ok[c] && ok[d] && !diag[a] && !right[d] && !up[a])
				mesh.indices.insert(mesh.indices.end(), { (uint32_t)a, (uint32_t)c, (uint32_t)d });
		}
	}
	return mesh;
}

} // namespace

// ---- Matrices ----------------------------------------------------------------------------------------------------------

FplMatrix4 FplMatrix4::Identity() {
	FplMatrix4 r;
	for (int i = 0; i < 4; i++)
		r.m[i][i] = 1;
	return r;
}

FplMatrix4 FplMatrix4::operator*(const FplMatrix4& b) const {
	FplMatrix4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + m[i][3] * b.m[3][j];
	return r;
}

void FplMatrix4::Transform(FplVec3 p, float out[4]) const {
	for (int j = 0; j < 4; j++)
		out[j] = p.x * m[0][j] + p.y * m[1][j] + p.z * m[2][j] + m[3][j];
}

// ---- The box -------------------------------------------------------------------------------------------------------------

bool FplBox3::Valid() const {
	return std::isfinite(xMin) && std::isfinite(xMax) && std::isfinite(yMin) && std::isfinite(yMax) &&
		std::isfinite(zMin) && std::isfinite(zMax) && xMax > xMin && yMax > yMin && zMax > zMin;
}

FplVec3 FplBox3::ToBox(double x, double y, double z) const {
	auto map = [](double v, double lo, double hi) {
		double t = (v - lo) / (hi - lo) * 2 - 1;
		return static_cast<float>(std::isnan(t) ? t : std::clamp(t, -4.0, 4.0));
	};
	return { map(x, xMin, xMax), map(y, yMin, yMax), map(z, zMin, zMax) };
}

FplPoint3 FplBox3::ToSpace(FplVec3 b) const {
	auto map = [](float t, double lo, double hi) { return lo + (t + 1) / 2 * (hi - lo); };
	return { map(b.x, xMin, xMax), map(b.y, yMin, yMax), map(b.z, zMin, zMax) };
}

void FplBox3::Zoom(double factor) {
	if (!(factor > 0))
		return;
	auto zoom = [&](double& lo, double& hi) {
		double c = (lo + hi) / 2, half = (hi - lo) / 2 / factor;
		half = std::clamp(half, 1e-9 * std::max(1.0, std::abs(c)), 1e100);
		lo = c - half;
		hi = c + half;
	};
	zoom(xMin, xMax);
	zoom(yMin, yMax);
	zoom(zMin, zMax);
}

void FplBox3::Pan(double dx, double dy) {
	xMin += dx;
	xMax += dx;
	yMin += dy;
	yMax += dy;
}

// ---- The camera ----------------------------------------------------------------------------------------------------------

FplVec3 FplCamera::Eye() const {
	return { distance * std::cos(pitch) * std::cos(yaw), distance * std::cos(pitch) * std::sin(yaw),
		distance * std::sin(pitch) };
}

FplMatrix4 FplCamera::View() const {
	// look at the origin with z up (right-handed)
	FplVec3 eye = Eye();
	FplVec3 zaxis = Normalize(eye);
	FplVec3 xaxis = Normalize(Cross({ 0, 0, 1 }, zaxis), { 1, 0, 0 });
	FplVec3 yaxis = Cross(zaxis, xaxis);
	FplMatrix4 r = FplMatrix4::Identity();
	r.m[0][0] = xaxis.x; r.m[0][1] = yaxis.x; r.m[0][2] = zaxis.x;
	r.m[1][0] = xaxis.y; r.m[1][1] = yaxis.y; r.m[1][2] = zaxis.y;
	r.m[2][0] = xaxis.z; r.m[2][1] = yaxis.z; r.m[2][2] = zaxis.z;
	r.m[3][0] = -Dot(xaxis, eye);
	r.m[3][1] = -Dot(yaxis, eye);
	r.m[3][2] = -Dot(zaxis, eye);
	return r;
}

FplMatrix4 FplCamera::Projection(float aspect) const {
	const float zn = 0.1f, zf = 100;
	float h = 1 / std::tan(fov / 2), w = h / std::max(aspect, 1e-3f);
	FplMatrix4 r;
	r.m[0][0] = w;
	r.m[1][1] = h;
	r.m[2][2] = zf / (zn - zf);
	r.m[2][3] = -1;
	r.m[3][2] = zn * zf / (zn - zf);
	return r;
}

void FplCamera::Orbit(float dYaw, float dPitch) {
	yaw = std::remainder(yaw + dYaw, 2 * Pi);
	pitch = std::clamp(pitch + dPitch, -Pi / 2 + 0.02f, Pi / 2 - 0.02f);
}

bool FplCamera::Project(FplVec3 p, float width, float height, FplPoint& screen) const {
	float c[4];
	ViewProjection(width / std::max(height, 1.0f)).Transform(p, c);
	if (c[3] <= 1e-6f)
		return false;
	screen.x = (c[0] / c[3] + 1) / 2 * width;
	screen.y = (1 - c[1] / c[3]) / 2 * height;
	return true;
}

void FplCamera::Ray(FplPoint screen, float width, float height, FplVec3& origin, FplVec3& dir) const {
	// the view's axes, as View() makes them; the camera looks down -z
	FplVec3 eye = Eye();
	FplVec3 zaxis = Normalize(eye);
	FplVec3 xaxis = Normalize(Cross({ 0, 0, 1 }, zaxis), { 1, 0, 0 });
	FplVec3 yaxis = Cross(zaxis, xaxis);
	const float aspect = width / std::max(height, 1.0f), tanHalf = std::tan(fov / 2);
	const float nx = static_cast<float>(screen.x / width * 2 - 1), ny = static_cast<float>(1 - screen.y / height * 2);
	origin = eye;
	dir = Normalize(Add(Add(Scale(xaxis, nx * tanHalf * aspect), Scale(yaxis, ny * tanHalf)), Scale(zaxis, -1)));
}

bool FplIntersect(const FplMesh& mesh, FplVec3 origin, FplVec3 dir, float& t) {
	bool found = false;
	float best = 0;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
		// Moller and Trumbore
		const FplVec3 a = mesh.vertices[mesh.indices[i]].pos, b = mesh.vertices[mesh.indices[i + 1]].pos,
			c = mesh.vertices[mesh.indices[i + 2]].pos;
		FplVec3 e1 = Sub(b, a), e2 = Sub(c, a);
		FplVec3 p = Cross(dir, e2);
		float det = Dot(e1, p);
		if (std::abs(det) < 1e-12f)
			continue;
		float inv = 1 / det;
		FplVec3 s0 = Sub(origin, a);
		float u = Dot(s0, p) * inv;
		if (u < 0 || u > 1)
			continue;
		FplVec3 q = Cross(s0, e1);
		float v = Dot(dir, q) * inv;
		if (v < 0 || u + v > 1)
			continue;
		float d = Dot(e2, q) * inv;
		if (d <= 0 || (found && d >= best))
			continue;
		FplVec3 at = Add(origin, Scale(dir, d));
		if (std::abs(at.x) > 1.001f || std::abs(at.y) > 1.001f || std::abs(at.z) > 1.001f)
			continue;
		best = d;
		found = true;
	}
	if (found)
		t = best;
	return found;
}

// ---- Meshes ----------------------------------------------------------------------------------------------------------

FplMesh FplSurfaceMesh(const std::function<double(double, double)>& F, const FplBox3& box, int n) {
	n = std::clamp(n, 2, 1000);
	const int w = n + 1;
	std::vector<FplVec3> p(static_cast<size_t>(w) * w);
	std::vector<char> ok(p.size());
	auto xAt = [&](double i) { return box.xMin + (box.xMax - box.xMin) * i / n; };
	auto yAt = [&](double j) { return box.yMin + (box.yMax - box.yMin) * j / n; };
	for (int j = 0; j <= n; j++)
		for (int i = 0; i <= n; i++) {
			double x = xAt(i), y = yAt(j), z = F(x, y);
			size_t k = static_cast<size_t>(j) * w + i;
			ok[k] = std::isfinite(z);
			p[k] = box.ToBox(x, y, ok[k] ? z : 0);
		}
	return GridMesh(p, ok, n, [&](int i0, int j0, int i1, int j1) {
		auto point = [&](double s, FplVec3& out) {
			double x = xAt(i0 + (i1 - i0) * s), y = yAt(j0 + (j1 - j0) * s), z = F(x, y);
			out = box.ToBox(x, y, z);
			return std::isfinite(z);
		};
		return IsJump(point, 0, 1, p[static_cast<size_t>(j0) * w + i0], p[static_cast<size_t>(j1) * w + i1]);
	});
}

FplMesh FplParametricMesh(const std::function<FplPoint3(double, double)>& f, const FplBox3& box, double u0, double u1,
	double v0, double v1, int n) {
	n = std::clamp(n, 2, 1000);
	const int w = n + 1;
	std::vector<FplVec3> p(static_cast<size_t>(w) * w);
	std::vector<char> ok(p.size());
	auto uAt = [&](double i) { return u0 + (u1 - u0) * i / n; };
	auto vAt = [&](double j) { return v0 + (v1 - v0) * j / n; };
	for (int j = 0; j <= n; j++)
		for (int i = 0; i <= n; i++) {
			FplPoint3 q = f(uAt(i), vAt(j));
			size_t k = static_cast<size_t>(j) * w + i;
			ok[k] = Finite(q);
			p[k] = ok[k] ? box.ToBox(q) : FplVec3{};
		}
	return GridMesh(p, ok, n, [&](int i0, int j0, int i1, int j1) {
		auto point = [&](double s, FplVec3& out) {
			FplPoint3 q = f(uAt(i0 + (i1 - i0) * s), vAt(j0 + (j1 - j0) * s));
			out = box.ToBox(q);
			return Finite(q);
		};
		return IsJump(point, 0, 1, p[static_cast<size_t>(j0) * w + i0], p[static_cast<size_t>(j1) * w + i1]);
	});
}

FplMesh FplImplicitMesh(const std::function<double(double, double, double)>& F, const FplBox3& box, int n) {
	FplMesh mesh;
	if (!box.Valid())
		return mesh;
	n = std::clamp(n, 2, 256);
	const int w = n + 1;
	const double dx = (box.xMax - box.xMin) / n, dy = (box.yMax - box.yMin) / n, dz = (box.zMax - box.zMin) / n;
	auto space = [&](int i, int j, int k) {
		return FplPoint3{ box.xMin + dx * i, box.yMin + dy * j, box.zMin + dz * k };
	};
	auto index = [&](int i, int j, int k) { return (static_cast<size_t>(k) * w + j) * w + i; };
	std::vector<double> values(static_cast<size_t>(w) * w * w);
	for (int k = 0; k <= n; k++)
		for (int j = 0; j <= n; j++)
			for (int i = 0; i <= n; i++) {
				FplPoint3 p = space(i, j, k);
				values[index(i, j, k)] = F(p.x, p.y, p.z);
			}

	// F's gradient at a grid point, in the cube's coordinates, from its neighbors (one side at the box's faces)
	auto gradient = [&](int i, int j, int k, double out[3]) {
		const int at[3] = { i, j, k };
		for (int axis = 0; axis < 3; axis++) {
			int lo[3] = { i, j, k }, hi[3] = { i, j, k };
			lo[axis] = std::max(at[axis] - 1, 0);
			hi[axis] = std::min(at[axis] + 1, n);
			double d = values[index(hi[0], hi[1], hi[2])] - values[index(lo[0], lo[1], lo[2])];
			out[axis] = std::isfinite(d) ? d / (hi[axis] - lo[axis]) * n / 2 : 0;
		}
	};

	// a crossing on the edge between two grid points: its vertex (shared by the tetrahedra around the edge), -1 when
	// it's a jump. Edges by their lower point and one of the 7 directions the tetrahedra use.
	const size_t steps[7] = { 1, (size_t)w, (size_t)w * w, 1 + (size_t)w, 1 + (size_t)w * w, (size_t)w + (size_t)w * w,
		1 + (size_t)w + (size_t)w * w };
	std::vector<int32_t> edges(values.size() * 7, -2);
	auto vertexOn = [&](size_t a, size_t b) -> int64_t {
		if (a > b)
			std::swap(a, b);
		int dir = 0;
		while (dir < 6 && steps[dir] != b - a)
			dir++;
		int32_t& slot = edges[a * 7 + dir];
		if (slot != -2)
			return slot;
		auto cell = [&](size_t g, int c[3]) {
			c[0] = static_cast<int>(g % w);
			c[1] = static_cast<int>(g / w % w);
			c[2] = static_cast<int>(g / w / w);
		};
		int ca[3], cb[3];
		cell(a, ca);
		cell(b, cb);
		FplPoint3 pa = space(ca[0], ca[1], ca[2]), pb = space(cb[0], cb[1], cb[2]);
		const double va = values[a], vb = values[b], start = std::abs(va) + std::abs(vb);
		auto at = [&](double t) {
			return FplPoint3{ pa.x + (pb.x - pa.x) * t, pa.y + (pb.y - pa.y) * t, pa.z + (pb.z - pa.z) * t };
		};
		// halved toward the zero, then the secant between the last two
		double lo = 0, hi = 1, flo = va, fhi = vb;
		for (int step = 0; step < 6; step++) {
			double m = (lo + hi) / 2;
			FplPoint3 q = at(m);
			double fm = F(q.x, q.y, q.z);
			if (std::isnan(fm))
				break;
			if ((fm < 0) == (flo < 0)) {
				lo = m;
				flo = fm;
			}
			else {
				hi = m;
				fhi = fm;
			}
		}
		// a zero's values shrink toward it; an asymptote's don't
		if (!(std::abs(flo) + std::abs(fhi) <= 0.25 * start)) {
			slot = -1;
			return slot;
		}
		double t = flo != fhi ? std::clamp(lo + (hi - lo) * flo / (flo - fhi), lo, hi) : (lo + hi) / 2;
		double ga[3], gb[3];
		gradient(ca[0], ca[1], ca[2], ga);
		gradient(cb[0], cb[1], cb[2], gb);
		FplVertex3 v;
		v.pos = box.ToBox(at(t));
		v.normal = Normalize({ static_cast<float>(ga[0] + (gb[0] - ga[0]) * t), static_cast<float>(ga[1] + (gb[1] - ga[1]) * t),
			static_cast<float>(ga[2] + (gb[2] - ga[2]) * t) });
		v.value = std::clamp((v.pos.z + 1) / 2, 0.0f, 1.0f);
		slot = static_cast<int32_t>(mesh.vertices.size());
		mesh.vertices.push_back(v);
		return slot;
	};
	auto triangle = [&](int64_t a, int64_t b, int64_t c) {
		if (a >= 0 && b >= 0 && c >= 0 && a != b && b != c && a != c)
			mesh.indices.insert(mesh.indices.end(), { (uint32_t)a, (uint32_t)b, (uint32_t)c });
	};

	// the 6 tetrahedra of a cell around its 0-7 diagonal (Kuhn's): corner bits are x 1, y 2, z 4
	static const int Tets[6][4] = { { 0, 1, 3, 7 }, { 0, 1, 5, 7 }, { 0, 2, 3, 7 }, { 0, 2, 6, 7 }, { 0, 4, 5, 7 },
		{ 0, 4, 6, 7 } };
	for (int k = 0; k < n; k++)
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++) {
				size_t corner[8];
				bool defined = true, any = false, all = true;
				for (int c = 0; c < 8; c++) {
					corner[c] = index(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1));
					double v = values[corner[c]];
					defined = defined && !std::isnan(v);
					any = any || v < 0;
					all = all && v < 0;
				}
				if (!defined || !any || all)
					continue;
				for (auto& t : Tets) {
					size_t g[4];
					int inside = 0, mask = 0;
					for (int c = 0; c < 4; c++) {
						g[c] = corner[t[c]];
						if (values[g[c]] < 0) {
							inside++;
							mask |= 1 << c;
						}
					}
					if (inside == 0 || inside == 4)
						continue;
					if (inside == 1 || inside == 3) {
						// one corner on its own side: a triangle across the edges from it
						int lone = 0;
						for (int c = 0; c < 4; c++)
							if (((mask >> c) & 1) == (inside == 1 ? 1 : 0))
								lone = c;
						int o[3], m = 0;
						for (int c = 0; c < 4; c++)
							if (c != lone)
								o[m++] = c;
						triangle(vertexOn(g[lone], g[o[0]]), vertexOn(g[lone], g[o[1]]), vertexOn(g[lone], g[o[2]]));
					}
					else {
						// two and two: a quad across the four edges between them
						int a[2], b[2], na = 0, nb = 0;
						for (int c = 0; c < 4; c++)
							((mask >> c) & 1 ? a[na++] : b[nb++]) = c;
						int64_t p0 = vertexOn(g[a[0]], g[b[0]]), p1 = vertexOn(g[a[0]], g[b[1]]);
						int64_t p2 = vertexOn(g[a[1]], g[b[1]]), p3 = vertexOn(g[a[1]], g[b[0]]);
						triangle(p0, p1, p2);
						triangle(p0, p2, p3);
					}
				}
			}
	return mesh;
}

std::vector<std::vector<FplVec3>> FplSampleCurve3(const std::function<FplPoint3(double)>& f, const FplBox3& box,
	double t0, double t1, int samples) {
	std::vector<std::vector<FplVec3>> lines;
	samples = std::clamp(samples, 1, 1'000'000);
	std::vector<FplVec3> line;
	auto point = [&](double t, FplVec3& out) {
		FplPoint3 q = f(t);
		out = box.ToBox(q);
		return Finite(q);
	};
	auto flush = [&] {
		if (line.size() >= 2)
			lines.push_back(std::move(line));
		line.clear();
	};
	FplVec3 prev;
	bool prevOk = false;
	double prevT = t0;
	for (int i = 0; i <= samples; i++) {
		double t = i == samples ? t1 : t0 + (t1 - t0) * i / samples;
		FplVec3 p;
		bool ok = point(t, p);
		if (!ok) {
			flush();
		}
		else {
			if (prevOk && IsJump(point, prevT, t, prev, p))
				flush();
			line.push_back(p);
		}
		prev = p;
		prevOk = ok;
		prevT = t;
	}
	flush();
	return lines;
}

FplMesh FplTubes(const std::vector<std::vector<FplVec3>>& lines, float radius, int sides) {
	FplMesh mesh;
	sides = std::clamp(sides, 3, 64);
	for (auto& raw : lines) {
		// no points on top of each other: they have no direction
		std::vector<FplVec3> line;
		line.reserve(raw.size());
		for (auto& p : raw)
			if (line.empty() || Distance(line.back(), p) > 1e-6)
				line.push_back(p);
		if (line.size() < 2)
			continue;
		const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
		// a frame carried along the line (parallel transport), so the tube doesn't twist
		FplVec3 t0 = Normalize(Sub(line[1], line[0]));
		FplVec3 normal = std::abs(t0.z) < 0.9f ? Normalize(Cross(t0, { 0, 0, 1 })) : Normalize(Cross(t0, { 1, 0, 0 }));
		for (size_t k = 0; k < line.size(); k++) {
			FplVec3 a = line[k > 0 ? k - 1 : 0], b = line[k + 1 < line.size() ? k + 1 : k];
			FplVec3 tangent = Normalize(Sub(b, a), t0);
			normal = Normalize(Sub(normal, Scale(tangent, Dot(normal, tangent))), normal);
			FplVec3 binormal = Cross(tangent, normal);
			for (int s = 0; s < sides; s++) {
				float angle = 2 * Pi * s / sides;
				FplVec3 dir = Add(Scale(normal, std::cos(angle)), Scale(binormal, std::sin(angle)));
				FplVertex3 v;
				v.pos = Add(line[k], Scale(dir, radius));
				v.normal = dir;
				v.value = std::clamp((line[k].z + 1) / 2, 0.0f, 1.0f);
				mesh.vertices.push_back(v);
			}
		}
		for (uint32_t k = 0; k + 1 < line.size(); k++) {
			for (int s = 0; s < sides; s++) {
				uint32_t a = base + k * sides + s, b = base + k * sides + (s + 1) % sides;
				uint32_t c = a + sides, d = b + sides;
				mesh.indices.insert(mesh.indices.end(), { a, b, d, a, d, c });
			}
		}
	}
	return mesh;
}

FplMesh FplSphere(FplVec3 center, float radius, int slices) {
	FplMesh mesh;
	slices = std::clamp(slices, 4, 64);
	const int stacks = slices / 2;
	for (int i = 0; i <= stacks; i++) {
		float phi = Pi * i / stacks;
		for (int j = 0; j <= slices; j++) {
			float theta = 2 * Pi * j / slices;
			FplVec3 dir = { std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi) };
			FplVertex3 v;
			v.pos = Add(center, Scale(dir, radius));
			v.normal = dir;
			v.value = std::clamp((center.z + 1) / 2, 0.0f, 1.0f);
			mesh.vertices.push_back(v);
		}
	}
	for (int i = 0; i < stacks; i++)
		for (int j = 0; j < slices; j++) {
			uint32_t a = i * (slices + 1) + j, b = a + 1, c = a + slices + 1, d = c + 1;
			mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
		}
	return mesh;
}

FplMesh FplBoxEdges() {
	FplMesh mesh;
	for (int k = 0; k < 8; k++) {
		FplVertex3 v;
		v.pos = { (k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 4) ? 1.0f : -1.0f };
		mesh.vertices.push_back(v);
	}
	// the corners differing in one bit
	for (uint32_t a = 0; a < 8; a++)
		for (uint32_t bit = 1; bit < 8; bit <<= 1)
			if (!(a & bit))
				mesh.indices.insert(mesh.indices.end(), { a, a | bit });
	return mesh;
}

std::vector<FplTick3> FplBoxTicks(const FplBox3& box, const FplCamera& camera) {
	std::vector<FplTick3> ticks;
	if (!box.Valid())
		return ticks;
	FplVec3 eye = camera.Eye();
	// the edges on the camera's side, a little outside the box
	const float sx = eye.x >= 0 ? 1.0f : -1.0f, sy = eye.y >= 0 ? 1.0f : -1.0f, out = 1.15f;
	auto add = [&](double lo, double hi, auto place, const wchar_t* name) {
		FplTicks t = FplMakeTicks(lo, hi, 320, 64);
		for (double v : t.major) {
			float b = static_cast<float>((v - lo) / (hi - lo) * 2 - 1);
			ticks.push_back({ place(b), FplFormatTick(v, t), false });
		}
		ticks.push_back({ place(1.3f), name, true });
	};
	add(box.xMin, box.xMax, [&](float b) { return FplVec3{ b, sy * out, -1 }; }, L"x");
	add(box.yMin, box.yMax, [&](float b) { return FplVec3{ sx * out, b, -1 }; }, L"y");
	add(box.zMin, box.zMax, [&](float b) { return FplVec3{ sx * out, -sy * out, b }; }, L"z");
	return ticks;
}

void FplAppend(FplMesh& a, const FplMesh& b) {
	uint32_t base = static_cast<uint32_t>(a.vertices.size());
	a.vertices.insert(a.vertices.end(), b.vertices.begin(), b.vertices.end());
	a.indices.reserve(a.indices.size() + b.indices.size());
	for (uint32_t i : b.indices)
		a.indices.push_back(base + i);
}

FplLineGrid FplMakeLineGrid(const FplBox3& box, bool x, bool y, bool z, bool levels, float cells) {
	FplLineGrid grid;
	grid.cells = cells;
	if (!box.Valid())
		return grid;
	auto axis = [&](int k, double lo, double hi, double spacing) {
		// as FplBoxTicks' 320 DIPs an axis
		const double step = FplMakeTicks(lo, hi, 320, spacing).step;
		if (!(step > 0))
			return;
		grid.scale[k] = static_cast<float>((hi - lo) / (2 * step));
		// only its fraction, so a far off range keeps its precision in floats
		const double at = lo / step;
		grid.offset[k] = static_cast<float>(at - std::floor(at));
	};
	if (x)
		axis(0, box.xMin, box.xMax, 64);
	if (y)
		axis(1, box.yMin, box.yMax, 64);
	if (z)
		axis(2, box.zMin, box.zMax, 64);
	if (levels)
		axis(2, box.zMin, box.zMax, 24);
	return grid;
}

FplMesh FplFillMesh(const std::vector<FplPolygon>& polygons, const FplBox3& box, int rows) {
	FplMesh mesh;
	if (!box.Valid() || polygons.empty())
		return mesh;
	rows = std::clamp(rows, 2, 4000);
	struct Crossing {
		double x;
		int winding;
	};
	const double rowHeight = (box.yMax - box.yMin) / rows;
	std::vector<std::vector<Crossing>> bands(rows);
	for (auto& polygon : polygons) {
		const size_t n = polygon.size();
		for (size_t i = 0; i < n; i++) {
			const FplPoint a = polygon[i], b = polygon[(i + 1) % n];
			if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) || a.y == b.y)
				continue;
			// the rows whose middle this edge crosses
			const double lo = std::min(a.y, b.y), hi = std::max(a.y, b.y);
			const int first = std::max(0, static_cast<int>(std::ceil((lo - box.yMin) / rowHeight - 0.5)));
			const int last = std::min(rows - 1, static_cast<int>(std::floor((hi - box.yMin) / rowHeight - 0.5)));
			for (int r = first; r <= last; r++) {
				const double y = box.yMin + (r + 0.5) * rowHeight;
				if (y < lo || y >= hi)
					continue;
				bands[r].push_back({ a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y), b.y > a.y ? 1 : -1 });
			}
		}
	}
	FplVec3 up{ 0, 0, 1 };
	auto corner = [&](double x, double y) {
		FplVertex3 v;
		v.pos = box.ToBox(x, y, 0);
		v.normal = up;
		v.value = std::clamp((v.pos.z + 1) / 2, 0.0f, 1.0f);
		mesh.vertices.push_back(v);
	};
	for (int r = 0; r < rows; r++) {
		auto& band = bands[r];
		if (band.size() < 2)
			continue;
		std::sort(band.begin(), band.end(), [](const Crossing& p, const Crossing& q) { return p.x < q.x; });
		const double y0 = box.yMin + r * rowHeight, y1 = y0 + rowHeight;
		int winding = 0;
		for (size_t i = 0; i + 1 < band.size(); i++) {
			winding += band[i].winding;
			if (winding == 0)
				continue;
			const double x0 = std::max(band[i].x, box.xMin), x1 = std::min(band[i + 1].x, box.xMax);
			if (!(x1 > x0))
				continue;
			const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
			corner(x0, y0);
			corner(x1, y0);
			corner(x1, y1);
			corner(x0, y1);
			mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}
	}
	return mesh;
}

bool FplDragOnPlane(FplVec3 origin, FplVec3 dir, const FplBox3& box, double z, FplPoint3& at) {
	if (!box.Valid() || !std::isfinite(z) || std::abs(dir.z) < 1e-3f)
		return false;
	const float cz = box.ToBox(0, 0, z).z;
	const float t = (cz - origin.z) / dir.z;
	if (!(t > 0))
		return false;
	const FplVec3 hit{ std::clamp(origin.x + dir.x * t, -1.0f, 1.0f), std::clamp(origin.y + dir.y * t, -1.0f, 1.0f), cz };
	at = box.ToSpace(hit);
	at.z = z;
	return true;
}

bool FplDragUpright(FplVec3 origin, FplVec3 dir, const FplBox3& box, double x, double y, double& z) {
	if (!box.Valid() || !std::isfinite(x) || !std::isfinite(y))
		return false;
	const FplVec3 base = box.ToBox(x, y, 0);
	// the closest points of the ray and the line through base along z
	const float wx = origin.x - base.x, wy = origin.y - base.y, wz = origin.z;
	const float b = dir.z, d = dir.x * wx + dir.y * wy + dir.z * wz;
	const float denominator = 1 - b * b;
	if (denominator < 1e-4f)
		return false;
	// the ray's parameter there, and so the height on the line
	const float t = (b * wz - d) / denominator;
	const float cube = std::clamp(wz + t * b, -1.0f, 1.0f);
	z = box.ToSpace({ base.x, base.y, cube }).z;
	return true;
}

FplMesh FplFlatSegments(const std::vector<std::pair<FplPoint, FplPoint>>& segments, const FplBox3& box, float halfWidth) {
	FplMesh mesh;
	if (!box.Valid())
		return mesh;
	mesh.vertices.reserve(segments.size() * 4);
	mesh.indices.reserve(segments.size() * 6);
	for (auto& [from, to] : segments) {
		if (!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(to.x) || !std::isfinite(to.y))
			continue;
		const FplVec3 a = box.ToBox(from.x, from.y, 0), b = box.ToBox(to.x, to.y, 0);
		const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
		if (!(len > 0))
			continue;
		// across it, and a little past its ends, as a round cap would be
		const float ux = dx / len * halfWidth, uy = dy / len * halfWidth;
		const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
		const FplVec3 corners[4] = { { a.x - ux + uy, a.y - uy - ux, a.z }, { a.x - ux - uy, a.y - uy + ux, a.z },
			{ b.x + ux - uy, b.y + uy + ux, b.z }, { b.x + ux + uy, b.y + uy - ux, b.z } };
		for (auto& c : corners) {
			FplVertex3 v;
			v.pos = c;
			v.normal = { 0, 0, 1 };
			v.value = std::clamp((c.z + 1) / 2, 0.0f, 1.0f);
			mesh.vertices.push_back(v);
		}
		mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
	}
	return mesh;
}
