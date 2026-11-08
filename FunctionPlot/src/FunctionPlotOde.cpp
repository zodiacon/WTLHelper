// The ODE solvers: Dormand-Prince 5(4) for one value, for an equation's y and its derivatives, and for 2D and 3D systems.

#include "FunctionPlot3D.h"
#include "FunctionPlotModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool Finite(FplPoint p) {
	return std::isfinite(p.x) && std::isfinite(p.y);
}

// Dormand-Prince 5(4)
constexpr double C[] = { 0, 1.0 / 5, 3.0 / 10, 4.0 / 5, 8.0 / 9, 1, 1 };
constexpr double A[7][6] = {
	{},
	{ 1.0 / 5 },
	{ 3.0 / 40, 9.0 / 40 },
	{ 44.0 / 45, -56.0 / 15, 32.0 / 9 },
	{ 19372.0 / 6561, -25360.0 / 2187, 64448.0 / 6561, -212.0 / 729 },
	{ 9017.0 / 3168, -355.0 / 33, 46732.0 / 5247, 49.0 / 176, -5103.0 / 18656 },
	{ 35.0 / 384, 0, 500.0 / 1113, 125.0 / 192, -2187.0 / 6784, 11.0 / 84 },
};
constexpr double B5[] = { 35.0 / 384, 0, 500.0 / 1113, 125.0 / 192, -2187.0 / 6784, 11.0 / 84, 0 };
constexpr double B4[] = { 5179.0 / 57600, 0, 7571.0 / 16695, 393.0 / 640, -92097.0 / 339200, 187.0 / 2100,
	1.0 / 40 };

// A step for n values: f gives their rates at (s, y). The 5th order result and each one's error; false if something
// wasn't a number. stiffness gets h times how fast the last two stages' difference is shrinking (StiffWatch).
template<int Most, typename D>
bool StepN(D f, int n, double s, const double* y, double h, double* y5, double* error, double* stiffness = nullptr) {
	double k[7][Most];
	double at[Most], before[Most];
	for (int st = 0; st < 7; st++) {
		for (int v = 0; v < n; v++) {
			if (st == 6)
				before[v] = at[v];
			at[v] = y[v];
			for (int j = 0; j < st; j++)
				at[v] += h * A[st][j] * k[j][v];
		}
		f(s + C[st] * h, at, k[st]);
		for (int v = 0; v < n; v++)
			if (!std::isfinite(k[st][v]))
				return false;
	}
	if (stiffness) {
		// the Rayleigh quotient of the Jacobian along the stages' difference
		double dot = 0, size = 0;
		for (int v = 0; v < n; v++) {
			const double dy = at[v] - before[v];
			dot += (k[6][v] - k[5][v]) * dy;
			size += dy * dy;
		}
		*stiffness = size > 0 ? h * dot / size : 0;
	}
	for (int v = 0; v < n; v++) {
		double five = y[v], four = y[v];
		for (int st = 0; st < 7; st++) {
			five += h * B5[st] * k[st][v];
			four += h * B4[st] * k[st][v];
		}
		if (!std::isfinite(five))
			return false;
		y5[v] = five;
		error[v] = std::abs(five - four);
	}
	return true;
}

// A step of ROS2 (Verwer's, L-stable, 2nd order) for n values: a Rosenbrock method, so a linear solve in place of Newton's
// iterations, with a numerical Jacobian. The independent variable is one more value, so f's dependence on it counts. The
// 2nd order result and each one's error against the 1st order one; false if something wasn't a number or the matrix is
// singular. Costs n + 3 of f.
template<int Most, typename D>
bool Ros2Step(D f, int n, double s, const double* y, double h, double* y2, double* error) {
	constexpr double Gamma = 0.29289321881345247;		// 1 - 1/sqrt(2)
	const int m = n + 1;
	double z[Most + 1], f0[Most + 1], moved[Most + 1], d[Most + 1];
	double a[Most + 1][Most + 1];
	auto rates = [&](const double* at, double* out) {
		out[0] = 1;
		f(at[0], at + 1, out + 1);
	};
	z[0] = s;
	std::copy(y, y + n, z + 1);
	rates(z, f0);
	for (int i = 0; i < m; i++)
		if (!std::isfinite(f0[i]))
			return false;
	// I - gamma h J, by forward differences
	for (int j = 0; j < m; j++) {
		std::copy(z, z + m, moved);
		const double delta = 1.5e-8 * std::max(std::abs(z[j]), 1e-3);
		moved[j] += delta;
		rates(moved, d);
		for (int i = 0; i < m; i++) {
			const double dfdz = (d[i] - f0[i]) / (moved[j] - z[j]);
			if (!std::isfinite(dfdz))
				return false;
			a[i][j] = (i == j ? 1.0 : 0.0) - Gamma * h * dfdz;
		}
	}
	// LU with partial pivoting
	int pivot[Most + 1];
	for (int c = 0; c < m; c++) {
		int best = c;
		for (int r = c + 1; r < m; r++)
			if (std::abs(a[r][c]) > std::abs(a[best][c]))
				best = r;
		if (a[best][c] == 0 || !std::isfinite(a[best][c]))
			return false;
		pivot[c] = best;
		if (best != c)
			for (int j = 0; j < m; j++)
				std::swap(a[c][j], a[best][j]);
		for (int r = c + 1; r < m; r++) {
			a[r][c] /= a[c][c];
			for (int j = c + 1; j < m; j++)
				a[r][j] -= a[r][c] * a[c][j];
		}
	}
	auto solve = [&](double* x) {
		for (int c = 0; c < m; c++) {
			std::swap(x[c], x[pivot[c]]);
			for (int r = c + 1; r < m; r++)
				x[r] -= a[r][c] * x[c];
		}
		for (int c = m - 1; c >= 0; c--) {
			for (int j = c + 1; j < m; j++)
				x[c] -= a[c][j] * x[j];
			x[c] /= a[c][c];
		}
	};
	double k1[Most + 1], k2[Most + 1];
	std::copy(f0, f0 + m, k1);
	solve(k1);
	for (int i = 0; i < m; i++)
		moved[i] = z[i] + h * k1[i];
	rates(moved, k2);
	for (int i = 0; i < m; i++) {
		if (!std::isfinite(k2[i]))
			return false;
		k2[i] -= 2 * k1[i];
	}
	solve(k2);
	for (int v = 0; v < n; v++) {
		y2[v] = y[v] + h * (1.5 * k1[v + 1] + 0.5 * k2[v + 1]);
		error[v] = std::abs(0.5 * h * (k1[v + 1] + k2[v + 1]));
		if (!std::isfinite(y2[v]) || !std::isfinite(error[v]))
			return false;
	}
	return true;
}

// Whether an equation has turned stiff: what limits the explicit steps is stability, not accuracy. Hairer's test: h times
// the rate at which the last stages' difference shrinks, past the method's stability limit, a few trials running (rejected
// ones too, as a very stiff equation never gets as far as accepting one). The sign matters: near a vertical tangent or a
// blow-up that rate is as large but solutions are spreading apart, and there the steps must stay short. A wrong guess only
// costs an order of accuracy, the error being watched either way.
struct StiffWatch {
	bool stiff = false;
	int trials = 0;
	void Trial(double rate) {
		if (rate < -3.25) {
			if (++trials >= 3)
				stiff = true;
		}
		else
			trials = 0;
	}
	// what the step size controller's error goes as
	double Power() const { return stiff ? 0.5 : 0.2; }
};

// (The smallest step is set a little over the limit and recognized with a margin: without one, rounding on some view sizes
// left it just past "at the smallest", to be cut and set back to the same value until the budget ran out.)

// How far to cut a step that moved too far, at most tenfold: on a stiff equation an unstable trial's move is astronomical,
// and cutting by its ratio went straight past the smallest step allowed, so nothing was drawn at all.
double Shrink(double ratio) {
	return std::max(0.9 * ratio, 0.1);
}

// Steps a solution may take each way in 2D: enough for a view at any explicit stiffness short of turning implicit, while a
// fast oscillation, which can only run out, costs a redraw little (a solid band of about 700,000 evaluations took 24 ms).
constexpr int MaxOdeSteps = 30000;

// DIPs per unit where it's defined, else the axis's scale.
double DipsX(const FplViewport& view, double x) {
	const double d = view.DipsPerUnitX(std::abs(x));
	return std::isfinite(d) && d > 0 ? d : view.ScaleX();
}

double DipsY(const FplViewport& view, double y) {
	const double d = view.DipsPerUnitY(std::abs(y));
	return std::isfinite(d) && d > 0 ? d : view.ScaleY();
}

} // namespace

FplPolyline FplSolveOdeN(const std::function<double(double, const double*)>& F, int order, double x0, const double* y0,
	const FplViewport& view, const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	FplPolyline result;
	if (!view.Valid() || !std::isfinite(x0) || order < 1 || order > FplMaxOdeOrder)
		return result;
	for (int v = 0; v < order; v++)
		if (!std::isfinite(y0[v]))
			return result;
	if (!std::isfinite(F(x0, y0)))
		return result;
	size_t count = 1;
	auto rates = [&](double x, const double* y, double* d) {
		for (int v = 0; v + 1 < order; v++)
			d[v] = y[v + 1];
		d[order - 1] = F(x, y);
	};
	const double maxStep = std::max(options.odeStep, 0.1), tolerance = std::max(options.odeTolerance, 1e-6);
	const double chord = std::max(options.maxChord / 2, 0.5);
	FplPolyline sides[2];
	for (int side = 0; side < 2; side++) {
		const double dir = side ? 1 : -1;
		const double end = view.ToPlaneX(side ? 2 * view.width : -view.width);
		auto& line = sides[side];
		double x = x0, y[FplMaxOdeOrder], y5[FplMaxOdeOrder], error[FplMaxOdeOrder];
		std::copy(y0, y0 + order, y);
		line.push_back(view.ToScreen({ x, y[0] }));
		double h = dir * maxStep / DipsX(view, x);
		StiffWatch watch;
		int steps = 0;
		for (; steps < MaxOdeSteps && count < options.maxEvals; steps++) {
			const double remaining = end - x;
			if (!(remaining * dir > 0))
				break;
			const double dipsX = DipsX(view, x), longest = maxStep / dipsX;
			if (std::abs(h) > longest)
				h = dir * longest;
			if (std::abs(h) > std::abs(remaining))
				h = remaining;
			bool finite;
			if (watch.stiff) {
				finite = Ros2Step<FplMaxOdeOrder>(rates, order, x, y, h, y5, error);
				count += order + 3;
			}
			else {
				double rate = 0;
				finite = StepN<FplMaxOdeOrder>(rates, order, x, y, h, y5, error, &rate);
				count += 7;
				watch.Trial(rate);
			}
			// implicit, the smallest step goes on whatever it does: a stiff transient can be a jump
			const bool atFloor = watch.stiff && std::abs(h) * dipsX <= 1.02e-4;
			if (!finite) {
				h /= 4;
				if (std::abs(h) * dipsX < 1e-4)
					break;
				continue;
			}
			const double dips = DipsY(view, y5[0]);
			const double rise = std::abs(y5[0] - y[0]) * dips;
			if (rise > chord && !atFloor) {
				h *= Shrink(chord / rise);
				if (std::abs(h) * dipsX < 1e-4) {
					if (!watch.stiff)
						break;
					h = dir * 1.01e-4 / dipsX;
				}
				continue;
			}
			// a derivative's error counts by what it does to y over the step: h^k / k!
			double err = 0, weight = 1;
			for (int v = 0; v < order; v++) {
				err = std::max(err, error[v] * weight);
				weight *= std::abs(h) / (v + 1);
			}
			err *= dips;
			if (err <= tolerance || atFloor) {
				x += h;
				std::copy(y5, y5 + order, y);
				FplPoint p = view.ToScreen({ x, y[0] });
				if (!Finite(p))
					break;
				line.push_back(p);
				if (p.y < -11 * view.height || p.y > 12 * view.height)
					break;
			}
			const double factor = err > 0 ? 0.9 * std::pow(tolerance / err, watch.Power()) : 5;
			h *= std::clamp(factor, 0.2, 5.0);
			if (std::abs(h) * dipsX < 1e-4) {
				if (!watch.stiff)
					break;
				h = dir * 1.01e-4 / dipsX;
			}
		}
		if (ranOut && (steps >= MaxOdeSteps || count >= options.maxEvals))
			*ranOut = true;
	}
	if (evals)
		*evals += count;
	result.reserve(sides[0].size() + sides[1].size());
	result.assign(sides[0].rbegin(), sides[0].rend());
	result.insert(result.end(), sides[1].begin() + 1, sides[1].end());
	return result;
}

FplPolyline FplSolveOde2(const std::function<double(double, double, double)>& F, double x0, double y0, double v0,
	const FplViewport& view, const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	const double start[] = { y0, v0 };
	std::function<double(double, const double*)> second = [&](double x, const double* y) { return F(x, y[0], y[1]); };
	return FplSolveOdeN(second, 2, x0, start, view, options, evals, ranOut);
}

FplPolyline FplSolveSystemT(const std::function<FplPoint(double, double, double)>& field, bool autonomous, double x0,
	double y0, const FplViewport& view, const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	FplPolyline result;
	if (!view.Valid() || !std::isfinite(x0) || !std::isfinite(y0))
		return result;
	size_t count = 0;
	auto rates = [&](double t, const double* y, double* d) {
		const FplPoint v = field(t, y[0], y[1]);
		d[0] = v.x;
		d[1] = v.y;
	};
	auto speedAt = [&](double t, double x, double y) {
		const FplPoint d = field(t, x, y);
		count++;
		return std::hypot(d.x * DipsX(view, x), d.y * DipsY(view, y));
	};
	const double maxStep = std::max(options.odeStep, 0.1), tolerance = std::max(options.odeTolerance, 1e-6);
	const double chord = std::max(options.maxChord / 2, 0.5);
	const double budget = 8 * (view.width + view.height);
	const FplPoint start = view.ToScreen({ x0, y0 });
	if (!Finite(start))
		return result;
	const double inf = std::numeric_limits<double>::infinity();
	const double tMin = options.odeRange ? options.tMin : -inf, tMax = options.odeRange ? options.tMax : inf;
	const double t0 = options.odeRange ? std::clamp(0.0, std::min(tMin, tMax), std::max(tMin, tMax)) : 0.0;
	// steps too small to get anywhere; with t in it, a slow moment isn't that (x' = sin t rests at t = 0)
	auto tooSmall = [&](double h, double pace, double t) {
		return autonomous ? std::abs(h) * pace < 1e-4 : std::abs(h) < 1e-10 * (1 + std::abs(t));
	};
	auto smallest = [&](double h, double pace, double t) {
		return std::copysign(1.01 * (autonomous ? 1e-4 / pace : 1e-10 * (1 + std::abs(t))), h);
	};
	FplPolyline sides[2];
	bool closed = false;
	for (int side = 1; side >= 0 && !closed; side--) {
		const double dir = side ? 1 : -1;
		const double end = side ? tMax : tMin;
		auto& line = sides[side];
		line.push_back(start);
		double t = t0, y[2] = { x0, y0 }, length = 0;
		double speed = speedAt(t, y[0], y[1]);
		if (!std::isfinite(speed) || (autonomous && speed < 1e-9))
			continue;
		double h = dir * maxStep / std::max(speed, 1e-6);
		StiffWatch watch;
		int steps = 0;
		for (; steps < MaxOdeSteps && count < options.maxEvals && length < budget; steps++) {
			const double remaining = end - t;
			if (!(remaining * dir > 0) || std::abs(t) > 1e9)
				break;
			speed = speedAt(t, y[0], y[1]);
			if (!std::isfinite(speed))
				break;
			// it's settled on a point (with t in it, it may only be pausing)
			if (autonomous && speed < 1e-6)
				break;
			const double pace = std::max(speed, 1e-6), longest = maxStep / pace;
			if (std::abs(h) > longest)
				h = dir * longest;
			if (std::abs(h) > std::abs(remaining))
				h = remaining;
			double y5[2], error[2];
			bool finite;
			if (watch.stiff) {
				finite = Ros2Step<2>(rates, 2, t, y, h, y5, error);
				count += 5;
			}
			else {
				double rate = 0;
				finite = StepN<2>(rates, 2, t, y, h, y5, error, &rate);
				count += 7;
				watch.Trial(rate);
			}
			// implicit, the smallest step goes on whatever it does: a stiff transient can be a jump
			const bool atFloor = watch.stiff && tooSmall(h * 0.98, pace, t);
			FplPoint p = finite ? view.ToScreen({ y5[0], y5[1] }) : FplPoint{};
			if (!finite || !Finite(p)) {
				h /= 4;
				if (tooSmall(h, pace, t))
					break;
				continue;
			}
			const double move = std::hypot(p.x - line.back().x, p.y - line.back().y);
			if (move > chord && !atFloor) {
				h *= Shrink(chord / move);
				if (watch.stiff && tooSmall(h, pace, t))
					h = smallest(h, pace, t);
				continue;
			}
			const double err = std::max(error[0] * DipsX(view, y5[0]), error[1] * DipsY(view, y5[1]));
			if (err <= tolerance || atFloor) {
				t += h;
				y[0] = y5[0];
				y[1] = y5[1];
				line.push_back(p);
				length += move;
				if (p.x < -view.width || p.x > 2 * view.width || p.y < -view.height || p.y > 2 * view.height)
					break;
				// back where it began: an orbit, and the other way is the same
				if (autonomous && length > 20 && std::hypot(p.x - start.x, p.y - start.y) < 1.5) {
					line.push_back(start);
					closed = true;
					break;
				}
			}
			const double factor = err > 0 ? 0.9 * std::pow(tolerance / err, watch.Power()) : 5;
			h *= std::clamp(factor, 0.2, 5.0);
			if (tooSmall(h, pace, t)) {
				if (!watch.stiff)
					break;
				h = smallest(h, pace, t);
			}
		}
		if (ranOut && (steps >= MaxOdeSteps || count >= options.maxEvals))
			*ranOut = true;
	}
	if (evals)
		*evals += count;
	if (sides[0].empty())
		return sides[1].size() > 1 ? sides[1] : result;
	result.reserve(sides[0].size() + sides[1].size());
	result.assign(sides[0].rbegin(), sides[0].rend());
	result.insert(result.end(), sides[1].begin() + 1, sides[1].end());
	return result.size() > 1 ? result : FplPolyline{};
}

FplPolyline FplSolveSystem(const std::function<FplPoint(double, double)>& field, double x0, double y0,
	const FplViewport& view, const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	std::function<FplPoint(double, double, double)> withTime = [&](double, double x, double y) { return field(x, y); };
	return FplSolveSystemT(withTime, true, x0, y0, view, options, evals, ranOut);
}

std::vector<FplVec3> FplSolveSystem3(const std::function<FplPoint3(double, double, double, double)>& field,
	bool autonomous, FplPoint3 p0, const FplBox3& box, const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	std::vector<FplVec3> result;
	if (!box.Valid() || !std::isfinite(p0.x) || !std::isfinite(p0.y) || !std::isfinite(p0.z))
		return result;
	// in the cube's units: the box's size doesn't matter, nor do the axes' different ranges
	const double sx = 2 / (box.xMax - box.xMin), sy = 2 / (box.yMax - box.yMin), sz = 2 / (box.zMax - box.zMin);
	constexpr double MaxStep = 0.02, Tolerance = 2e-4, Budget = 100, Beyond = 3.9, Small = 1e-5;
	size_t count = 0;
	auto rates = [&](double t, const double* y, double* d) {
		const FplPoint3 v = field(t, y[0], y[1], y[2]);
		d[0] = v.x;
		d[1] = v.y;
		d[2] = v.z;
	};
	auto speedAt = [&](double t, const double* y) {
		const FplPoint3 v = field(t, y[0], y[1], y[2]);
		count++;
		return std::sqrt(v.x * sx * v.x * sx + v.y * sy * v.y * sy + v.z * sz * v.z * sz);
	};
	const double inf = std::numeric_limits<double>::infinity();
	const double tMin = options.odeRange ? options.tMin : -inf, tMax = options.odeRange ? options.tMax : inf;
	const double t0 = options.odeRange ? std::clamp(0.0, std::min(tMin, tMax), std::max(tMin, tMax)) : 0.0;
	const FplVec3 start = box.ToBox(p0);
	auto tooSmall = [&](double h, double pace, double t) {
		return autonomous ? std::abs(h) * pace < Small : std::abs(h) < 1e-10 * (1 + std::abs(t));
	};
	auto smallest = [&](double h, double pace, double t) {
		return std::copysign(1.01 * (autonomous ? Small / pace : 1e-10 * (1 + std::abs(t))), h);
	};
	std::vector<FplVec3> sides[2];
	bool closed = false;
	for (int side = 1; side >= 0 && !closed; side--) {
		const double dir = side ? 1 : -1;
		const double end = side ? tMax : tMin;
		auto& line = sides[side];
		line.push_back(start);
		double t = t0, y[3] = { p0.x, p0.y, p0.z }, length = 0;
		double speed = speedAt(t, y);
		if (!std::isfinite(speed) || (autonomous && speed < 1e-9))
			continue;
		double h = dir * MaxStep / std::max(speed, 1e-6);
		StiffWatch watch;
		int steps = 0;
		for (; steps < 100000 && count < options.maxEvals && length < Budget; steps++) {
			const double remaining = end - t;
			if (!(remaining * dir > 0) || std::abs(t) > 1e9)
				break;
			speed = speedAt(t, y);
			if (!std::isfinite(speed) || (autonomous && speed < 1e-6))
				break;
			const double pace = std::max(speed, 1e-6), longest = MaxStep / pace;
			if (std::abs(h) > longest)
				h = dir * longest;
			if (std::abs(h) > std::abs(remaining))
				h = remaining;
			double y5[3], error[3];
			bool finite;
			if (watch.stiff) {
				finite = Ros2Step<3>(rates, 3, t, y, h, y5, error);
				count += 6;
			}
			else {
				double rate = 0;
				finite = StepN<3>(rates, 3, t, y, h, y5, error, &rate);
				count += 7;
				watch.Trial(rate);
			}
			const bool atFloor = watch.stiff && tooSmall(h * 0.98, pace, t);
			if (!finite) {
				h /= 4;
				if (tooSmall(h, pace, t))
					break;
				continue;
			}
			const double dx = (y5[0] - y[0]) * sx, dy = (y5[1] - y[1]) * sy, dz = (y5[2] - y[2]) * sz;
			const double move = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (move > 2 * MaxStep && !atFloor) {
				h *= Shrink(MaxStep / move);
				if (watch.stiff && tooSmall(h, pace, t))
					h = smallest(h, pace, t);
				continue;
			}
			const double err = std::max({ error[0] * sx, error[1] * sy, error[2] * sz });
			if (err <= Tolerance || atFloor) {
				t += h;
				std::copy(y5, y5 + 3, y);
				const FplVec3 p = box.ToBox(y[0], y[1], y[2]);
				line.push_back(p);
				length += move;
				// the cube's mapping stops at 4: past that it couldn't be drawn where it is
				if (std::abs(p.x) > Beyond || std::abs(p.y) > Beyond || std::abs(p.z) > Beyond)
					break;
				const double back = std::sqrt((p.x - start.x) * (p.x - start.x) + (p.y - start.y) * (p.y - start.y) +
					(p.z - start.z) * (p.z - start.z));
				if (autonomous && length > 0.5 && back < 1.5 * MaxStep) {
					line.push_back(start);
					closed = true;
					break;
				}
			}
			const double factor = err > 0 ? 0.9 * std::pow(Tolerance / err, watch.Power()) : 5;
			h *= std::clamp(factor, 0.2, 5.0);
			if (tooSmall(h, pace, t)) {
				if (!watch.stiff)
					break;
				h = smallest(h, pace, t);
			}
		}
		if (ranOut && (steps >= 100000 || count >= options.maxEvals))
			*ranOut = true;
	}
	if (evals)
		*evals += count;
	result.reserve(sides[0].size() + sides[1].size());
	if (!sides[0].empty())
		result.assign(sides[0].rbegin(), sides[0].rend());
	if (!sides[1].empty())
		result.insert(result.end(), result.empty() ? sides[1].begin() : sides[1].begin() + 1, sides[1].end());
	if (result.size() < 2)
		result.clear();
	return result;
}

FplPolyline FplSolveOde(const std::function<double(double, double)>& F, double x0, double y0, const FplViewport& view,
	const FplSampleOptions& options, size_t* evals, bool* ranOut) {
	FplPolyline result;
	if (!view.Valid() || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(F(x0, y0)))
		return result;
	size_t count = 1;
	const double maxStep = std::max(options.odeStep, 0.1), tolerance = std::max(options.odeTolerance, 1e-6);
	const double top = -view.height, bottom = 2 * view.height;
	FplPolyline sides[2];
	for (int side = 0; side < 2; side++) {
		const double dir = side ? 1 : -1;
		// to the view grown by its width
		const double end = view.ToPlaneX(side ? 2 * view.width : -view.width);
		auto& line = sides[side];
		double x = x0, y = y0;
		line.push_back(view.ToScreen({ x, y }));
		double h = dir * maxStep / view.DipsPerUnitX(x);
		StiffWatch watch;
		int steps = 0;
		for (; steps < MaxOdeSteps && count < options.maxEvals; steps++) {
			const double remaining = end - x;
			if (!(remaining * dir > 0))
				break;
			const double longest = maxStep / view.DipsPerUnitX(x);
			if (std::abs(h) > longest)
				h = dir * longest;
			if (std::abs(h) > std::abs(remaining))
				h = remaining;
			double k[7];
			bool finite = true;
			double y5 = y, y4 = y;
			if (watch.stiff) {
				auto rate = [&](double at, const double* value, double* d) { d[0] = F(at, value[0]); };
				double e = 0;
				finite = Ros2Step<1>(rate, 1, x, &y, h, &y5, &e);
				y4 = y5 - e;
				count += 4;
			}
			else {
				double stage5 = 0, stage6 = 0;
				for (int st = 0; st < 7 && finite; st++) {
					double yy = y;
					for (int j = 0; j < st; j++)
						yy += h * A[st][j] * k[j];
					k[st] = F(x + C[st] * h, yy);
					finite = std::isfinite(k[st]);
					(st == 5 ? stage5 : stage6) = yy;
				}
				count += 7;
				if (finite) {
					for (int st = 0; st < 7; st++) {
						y5 += h * B5[st] * k[st];
						y4 += h * B4[st] * k[st];
					}
					watch.Trial(stage6 != stage5 ? h * (k[6] - k[5]) / (stage6 - stage5) : 0);
				}
			}
			// the error in DIPs on the screen (a log axis's at the value, or its scale where it has none)
			double dips = view.DipsPerUnitY(std::abs(y5));
			if (!std::isfinite(dips) || dips <= 0)
				dips = view.ScaleY();
			// a steep slope changing by more than half over the step (k[6] is the slope at its end): near a vertical tangent
			// the error estimate alone lets steps through that jump about, so closer ones. A stiff transient pays a little
			// for this too, but its slope is falling. At the smallest steps it's let through: a jump in F is no tangent.
			const double dipsX = view.DipsPerUnitX(x);
			const double steep = 10 * dipsX / dips;
			// implicit, the smallest step goes on whatever it does: a stiff transient can be a jump
			const bool atFloor = watch.stiff && std::abs(h) * dipsX <= 1.02e-4;
			if (!watch.stiff && finite && std::max(std::abs(k[0]), std::abs(k[6])) > steep &&
				std::abs(k[6] - k[0]) > 0.5 * std::max(std::abs(k[0]), std::abs(k[6])) && std::abs(h) * dipsX >= 1e-4) {
				h /= 2;
				continue;
			}
			const double err = std::abs(y5 - y4) * dips;
			if (!finite || !std::isfinite(y5)) {
				// undefined ahead: closer steps up to its edge
				h /= 4;
				if (std::abs(h) * view.DipsPerUnitX(x) < 1e-4)
					break;
				continue;
			}
			// a chord no longer than maxChord/2 on the screen: smooth, and it stops at a vertical tangent instead of
			// going through it
			const double rise = std::abs(y5 - y) * dips, chord = std::max(options.maxChord / 2, 0.5);
			if (rise > chord && !atFloor) {
				h *= Shrink(chord / rise);
				if (std::abs(h) * dipsX < 1e-4) {
					if (!watch.stiff)
						break;
					h = dir * 1.01e-4 / dipsX;
				}
				continue;
			}
			if (err <= tolerance || atFloor) {
				x += h;
				y = y5;
				FplPoint p = view.ToScreen({ x, y });
				if (!Finite(p))
					break;
				line.push_back(p);
				// gone off the screen for good
				if (p.y < top - 10 * view.height || p.y > bottom + 10 * view.height)
					break;
				// upright on the screen and getting steeper: a vertical tangent, where the solution ends (a stiff
				// transient starts as steep, but falls). Not when implicit: solutions are drawing together there, so
				// it's no tangent, and F off the slow solution by a rounding error is a huge number that means nothing.
				if (!watch.stiff && std::abs(k[6]) * dips > 1000 * dipsX && std::abs(k[6]) >= std::abs(k[0]))
					break;
			}
			const double factor = err > 0 ? 0.9 * std::pow(tolerance / err, watch.Power()) : 5;
			h *= std::clamp(factor, 0.2, 5.0);
			if (std::abs(h) * view.DipsPerUnitX(x) < 1e-4) {
				if (!watch.stiff)
					break;
				h = dir * 1.01e-4 / view.DipsPerUnitX(x);
			}
		}
		if (ranOut && (steps >= MaxOdeSteps || count >= options.maxEvals))
			*ranOut = true;
	}
	if (evals)
		*evals += count;
	result.reserve(sides[0].size() + sides[1].size());
	result.assign(sides[0].rbegin(), sides[0].rend());
	result.insert(result.end(), sides[1].begin() + 1, sides[1].end());
	return result;
}

