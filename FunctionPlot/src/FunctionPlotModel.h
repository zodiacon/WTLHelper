#pragma once

// The function plot's 2D model, window-free: the view's mapping between the plane and the screen, nice axis ticks
// (pi multiples too), sampling curves into polylines (adaptive, breaking at discontinuities and domain edges), implicit
// curves and regions by marching squares, and finding roots, extrema and intersections.
//
// Screen coordinates are DIPs from the view's top left, y down. Sampled geometry is in screen coordinates, clipped (lines)
// or clamped (fills) to the view grown by its own size on every side, so it stays in float range for drawing.

#include "FunctionPlotExpr.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

struct FplPoint {
	double x = 0, y = 0;
};
using FplPolyline = std::vector<FplPoint>;
using FplPolygon = std::vector<FplPoint>;

// ---- The view --------------------------------------------------------------------------------------------------------

// An axis is linear or logarithmic. On a log axis the ranges stay in the plane's values (positive), and the mapping,
// panning and zooming work in their logarithms ("axis units"): a value that isn't positive is NaN on the screen.
struct FplViewport {
	double xMin = -10, xMax = 10, yMin = -10, yMax = 10;	// the plane's part shown
	double width = 800, height = 600;						// on the screen
	bool logX = false, logY = false;						// log10 scales

	bool Valid() const;
	double AxisX(double x) const { return logX ? (x > 0 ? std::log10(x) : std::numeric_limits<double>::quiet_NaN()) : x; }
	double AxisY(double y) const { return logY ? (y > 0 ? std::log10(y) : std::numeric_limits<double>::quiet_NaN()) : y; }
	double FromAxisX(double a) const { return logX ? std::pow(10.0, a) : a; }
	double FromAxisY(double a) const { return logY ? std::pow(10.0, a) : a; }
	double ScaleX() const { return width / (AxisX(xMax) - AxisX(xMin)); }		// DIPs per axis unit
	double ScaleY() const { return height / (AxisY(yMax) - AxisY(yMin)); }
	// DIPs per unit of the plane at a value (the scale on a linear axis)
	double DipsPerUnitX(double x) const { return logX ? ScaleX() / (x * 2.302585092994046) : ScaleX(); }
	double DipsPerUnitY(double y) const { return logY ? ScaleY() / (y * 2.302585092994046) : ScaleY(); }
	double ToScreenX(double x) const { return (AxisX(x) - AxisX(xMin)) * ScaleX(); }
	double ToScreenY(double y) const { return (AxisY(yMax) - AxisY(y)) * ScaleY(); }
	double ToPlaneX(double sx) const { return FromAxisX(AxisX(xMin) + sx / ScaleX()); }
	double ToPlaneY(double sy) const { return FromAxisY(AxisY(yMax) - sy / ScaleY()); }
	// Where the axes are drawn on the screen: at 0, or (a log axis has none) far off, so labels go along the edge.
	double AxisLineX() const { return logX ? -1e9 : ToScreenX(0); }		// the y axis's x
	double AxisLineY() const { return logY ? 1e9 : ToScreenY(0); }		// the x axis's y
	// Makes the ranges fit the scales: a log axis's range positive (keeping its top, at least 3 decades), clamped.
	void FixLogRanges();
	FplPoint ToScreen(FplPoint p) const { return { ToScreenX(p.x), ToScreenY(p.y) }; }
	FplPoint ToPlane(FplPoint s) const { return { ToPlaneX(s.x), ToPlaneY(s.y) }; }

	// factor > 1 zooms in, keeping the plane's point under the screen point where it is.
	void Zoom(double factorX, double factorY, FplPoint screen);
	void Pan(double dxScreen, double dyScreen);		// the contents follow the mouse moving by this much
	void Resize(double newWidth, double newHeight);	// keeps the center and the scales
	void EqualScale();								// grows the axis with the bigger scale around its center
	// The ranges of the screen rectangle between two corners (any order); false (unchanged) if it has no area.
	bool ZoomTo(FplPoint a, FplPoint b);
};

// ---- Axis ticks ------------------------------------------------------------------------------------------------------

struct FplTicks {
	double step = 0;				// between majors
	bool pi = false;				// the step is piNum/piDen pi
	int piNum = 1, piDen = 1;
	std::vector<double> major;		// in the range, ascending
	std::vector<double> minor;		// between the majors (not on them)
	bool log = false;				// powers of 10 on a log axis
};

// Majors at least minSpacing DIPs apart over [min, max] drawn over pixels DIPs: 1, 2 or 5 times a power of 10, or with pi
// multiples of pi (pi/12, pi/6, pi/4, pi/2, pi, then 2, 5, 10... times), falling back to decimals below pi/12.
FplTicks FplMakeTicks(double min, double max, double pixels, double minSpacing, bool pi = false);

// A log axis's ticks over [min, max] (positive): majors at powers of 10 (every 1, 2, 5, 10... decades as there's room),
// minors at 2 to 9 times them (or the decades between); under a decade, the linear ticks spaced for the range's top.
FplTicks FplMakeLogTicks(double min, double max, double pixels, double minSpacing);
// The ticks of the view's x and y axes, linear or log.
FplTicks FplMakeTicksX(const FplViewport& view, double minSpacing, bool pi = false);
FplTicks FplMakeTicksY(const FplViewport& view, double minSpacing);

// A tick's label: "-0.5" (with U+2212), "3pi/4" (U+03C0), "2.5x10^6" (U+00D7 and superscript digits) past 1e6 or below
// 1e-4; on a log axis "0.01" to "1000", else 10 with a superscript power.
std::wstring FplFormatTick(double value, const FplTicks& ticks);

// ---- Sampling --------------------------------------------------------------------------------------------------------

struct FplSampleOptions {
	double tolerance = 0.2;			// DIPs: how far a sample may be from the chord before it's split
	double initialStep = 2;			// DIPs between the first samples of y = f(x) and x = f(y)
	int curveSamples = 512;			// first samples of a parametric or polar curve over its range
	int maxDepth = 10;				// halvings of a first interval
	double maxChord = 8;			// DIPs: a longer chord is split too (a step's midpoint is on it)
	double jump = 0.5;				// DIPs: a gap that doesn't close as it's halved is a discontinuity
	double tMin = 0, tMax = 6.283185307179586;		// parametric
	double thetaMin = 0, thetaMax = 6.283185307179586;
	double cellSize = 8;			// DIPs: the implicit grid's cells
	int cellDepth = 2;				// halvings of a cell with the curve in it
	size_t maxEvals = 2'000'000;	// a budget: past it, intervals and cells aren't split any more
	// surfaces and vector fields
	bool contours = true;			// a surface's contour lines
	bool heatmap = false;			// a surface's values on a grid, for a heat map
	int contourLevels = 10;			// about this many
	double heatCell = 4;			// DIPs: the heat map's cells
	double arrowSpacing = 40;		// DIPs: at least this far apart
	double slopeSpacing = 26;		// DIPs: a slope field's marks at least this far apart
	double odeStep = 2;				// DIPs: an ODE's solution's longest step in x
	double odeTolerance = 0.02;		// DIPs: its error in y a step
	bool singularities = true;		// a function's asymptotes and holes
	bool odeRange = false;			// a system's trajectories only over t in [tMin, tMax], not for as long as they last
};

// A curve given as t -> a point in the plane (NaN where undefined), sampled over [t0, t1] from samples + 1 first
// points: split where it bends (in screen space), broken where it's undefined or jumps, with the ends found where the
// domain stops. Polylines in screen coordinates, clipped.
std::vector<FplPolyline> FplSampleCurve(const std::function<FplPoint(double)>& curve, double t0, double t1,
	int samples, const FplViewport& view, const FplSampleOptions& options = {}, size_t* evals = nullptr);

// The zero set of F(x, y) (plane coordinates) by marching squares over the view, cells with a sign change split
// further; a sign change that isn't a zero (an asymptote, a branch cut) is dropped.
std::vector<FplPolyline> FplSampleImplicit(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr);

// Where F(x, y) < 0 (inside), as polygons in screen coordinates, all wound the same way so one geometry fills them
// without seams.
std::vector<FplPolygon> FplSampleRegion(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr);

// Clips polylines to a rectangle (in their coordinates), splitting where they leave it.
std::vector<FplPolyline> FplClip(const std::vector<FplPolyline>& lines, double left, double top, double right,
	double bottom);

// ---- Surfaces and vector fields --------------------------------------------------------------------------------------

// A surface's values at the centers of a grid of cells over the view (NaN where undefined), and the range they're
// colored over: the 2nd to the 98th percentile, so a pole doesn't wash the rest out.
struct FplHeatmap {
	int cols = 0, rows = 0;
	double cell = 0;					// DIPs; cell (0, 0)'s top left is the view's
	std::vector<float> values;			// row by row
	double lo = 0, hi = 0;
	bool Empty() const { return values.empty(); }
};

FplHeatmap FplSampleHeatmap(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr);

// Nice levels between lo and hi, about count of them (1, 2 or 5 times a power of ten apart).
std::vector<double> FplContourLevels(double lo, double hi, int count);

struct FplContour {
	double level = 0;
	std::vector<FplPolyline> lines;		// screen coordinates
};

// Where F(x, y) is each level, by marching squares (FplSampleImplicit of F - level).
std::vector<FplContour> FplSampleContours(const std::function<double(double, double)>& F,
	const std::vector<double>& levels, const FplViewport& view, const FplSampleOptions& options = {},
	size_t* evals = nullptr);

// A field's arrow, centered on its grid point, as long as its magnitude against the field's largest (the 95th
// percentile), but never shorter than a fifth of the longest so its direction shows.
struct FplArrow {
	FplPoint from, to;					// screen coordinates
	double magnitude = 0;
};

// Arrows at the plane's points a nice step apart (at least options.arrowSpacing DIPs), so they stay put as the view
// pans; field(x, y) gives the vector (NaN: none there).
std::vector<FplArrow> FplSampleField(const std::function<FplPoint(double, double)>& field, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr);

// t from 0 to 1 to a color (a COLORREF, 0x00BBGGRR) on a perceptual scale from dark blue through green to yellow.
uint32_t FplColormap(double t);

// A heat map's color scale over lo to hi, drawn pixels DIPs tall: labels at nice values at least minSpacing apart, each
// at a fraction of the bar from its bottom.
struct FplColorScaleTick {
	double fraction = 0;
	std::wstring text;
};
std::vector<FplColorScaleTick> FplMakeColorScale(double lo, double hi, double pixels, double minSpacing = 36);

// ---- An expression's geometry ----------------------------------------------------------------------------------------

struct FplGeometry {
	std::vector<FplPolyline> lines;		// the curve, or a region's boundary
	std::vector<FplPolygon> fills;		// an inequality's region
	std::vector<FplPoint> points;		// Point: in screen coordinates (may be outside the view)
	std::vector<FplContour> contours;	// Surface (options.contours)
	FplHeatmap heatmap;					// Surface (options.heatmap)
	std::vector<FplArrow> arrows;		// VectorField
	std::vector<FplArrow> marks;		// SlopeField: its slopes, short segments with no heads
	std::vector<double> asymptotes;		// y = f(x): screen x's of vertical asymptotes; x = f(y): screen y's
	std::vector<FplPoint> holes;		// where it isn't defined but its limit is, the same from both sides
	bool incomplete = false;			// an ODE's solution ran out of its step budget before the view's edge
	bool dashed = false;				// a strict inequality's boundary
	size_t evals = 0;
};

// A slope field's marks: on the plane's points a nice step apart (as FplSampleField's arrows), each the slope F(x, y)
// on the screen, 0.7 of the room between them long.
std::vector<FplArrow> FplSampleSlopes(const std::function<double(double, double)>& F, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr);
// The solution of y' = F(x, y) through (x0, y0), both ways to the view grown by its size (as lines are clipped), by
// Dormand-Prince 5(4) with steps of at most odeStep DIPs in x and odeTolerance DIPs of error in y; stopped where it
// isn't defined, blows up off the screen or the steps get too small. In screen coordinates, not clipped.
FplPolyline FplSolveOde(const std::function<double(double, double)>& F, double x0, double y0, const FplViewport& view,
	const FplSampleOptions& options = {}, size_t* evals = nullptr,
	bool* ranOut = nullptr);

// All these start explicit and turn implicit (ROS2, L-stable) when an equation shows itself stiff, so stiffness only costs
// them accuracy's order, not stopping. ranOut is set if the step budget ended it before the view's edge.
// y^(n) = F(x, y, y', ... y^(n-1)) from y and its derivatives at x0 (y0[0] is y), as FplSolveOde; F is given them the same
// way. Up to FplMaxOdeOrder.
inline constexpr int FplMaxOdeOrder = 5;
FplPolyline FplSolveOdeN(const std::function<double(double, const double*)>& F, int order, double x0, const double* y0,
	const FplViewport& view, const FplSampleOptions& options = {}, size_t* evals = nullptr,
	bool* ranOut = nullptr);
// y'' = F(x, y, y') through (x0, y0) with slope v0.
FplPolyline FplSolveOde2(const std::function<double(double, double, double)>& F, double x0, double y0, double v0,
	const FplViewport& view, const FplSampleOptions& options = {}, size_t* evals = nullptr,
	bool* ranOut = nullptr);
// The trajectory of (x', y') = field(x, y) through (x0, y0), both ways in time, with steps of at most odeStep DIPs on
// the screen; ended where it leaves the view grown by its size, settles on a point, closes on itself, or has run 8 times
// around the view. A point where the field is zero has no line.
FplPolyline FplSolveSystem(const std::function<FplPoint(double, double)>& field, double x0, double y0,
	const FplViewport& view, const FplSampleOptions& options = {}, size_t* evals = nullptr,
	bool* ranOut = nullptr);
// The same with t in it (field(t, x, y), starting at t = 0): not autonomous, it neither settles nor closes.
FplPolyline FplSolveSystemT(const std::function<FplPoint(double, double, double)>& field, bool autonomous, double x0,
	double y0, const FplViewport& view, const FplSampleOptions& options = {}, size_t* evals = nullptr,
	bool* ranOut = nullptr);

// Samples a built expression with its parameters' values (slots from FplFirstParam on; the others are set here).
// Definitions draw nothing.
FplGeometry FplSample(const FplExpression& e, std::vector<double> slots, const FplViewport& view,
	const FplSampleOptions& options = {});

// ---- Analysis --------------------------------------------------------------------------------------------------------

// A root of f in [a, b], where fa and fb have opposite signs (Brent's method), to the doubles' precision. other gets the
// end of the last bracket, so a caller can tell a root from a jump through zero.
double FplBrent(const std::function<double(double)>& f, double a, double b, double fa, double fb,
	double* other = nullptr);

enum class FplFeatureKind : uint8_t { Root, Minimum, Maximum, Intersection, YIntercept };

struct FplFeature {
	FplFeatureKind kind = FplFeatureKind::Root;
	double x = 0, y = 0;
};

// Roots (sign changes, and extrema touching zero within yTolerance), local minima and maxima, and the y intercept of
// y = f(x) over [x0, x1], scanned at samples + 1 points. df is the derivative (null: differences). Asymptotes and
// jumps are not roots or extrema; a corner (|x|) is an extremum. Sorted by x.
std::vector<FplFeature> FplFindFeatures(const std::function<double(double)>& f, const std::function<double(double)>& df,
	double x0, double x1, int samples, double yTolerance);

// Where y = f(x) and y = g(x) meet over [x0, x1] (crossing or touching), as Intersection features.
std::vector<FplFeature> FplFindIntersections(const std::function<double(double)>& f,
	const std::function<double(double)>& g, double x0, double x1, int samples, double yTolerance);

// A curve as FplFindCurveIntersections takes it: its sampled lines (screen coordinates), and what it can be refined by:
// G(x, y) = 0 where it's an equation, or C(t) over [t0, t1] where it's a parametric curve; neither (an ODE's solution):
// the lines alone.
struct FplCurve {
	const std::vector<FplPolyline>* lines = nullptr;
	std::function<double(double, double)> G;
	std::function<FplPoint(double)> C;
	double t0 = 0, t1 = 0;
};

// Where two curves of any kind meet in the view, as Intersection features sorted by x then y. Their lines' crossings
// are found through a grid of the view, then refined on the curves themselves by Newton's method (kept only if that
// stays within a few DIPs, else the lines' crossing stands). Touching without crossing: the lines coming within a third
// of a DIP for a short stretch, at its middle, so to about a DIP (the view's own precision); a longer stretch is two
// curves running together, not a point.
std::vector<FplFeature> FplFindCurveIntersections(const FplCurve& a, const FplCurve& b, const FplViewport& view);
