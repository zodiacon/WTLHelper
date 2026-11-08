#pragma once

// The function plot's 2D model, window-free: the view's mapping between the plane and the screen, nice axis ticks
// (pi multiples too), sampling curves into polylines (adaptive, breaking at discontinuities and domain edges), implicit
// curves and regions by marching squares, and finding roots, extrema and intersections.
//
// Screen coordinates are DIPs from the view's top left, y down. Sampled geometry is in screen coordinates, clipped (lines)
// or clamped (fills) to the view grown by its own size on every side, so it stays in float range for drawing.

#include "FunctionPlotExpr.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct FplPoint {
	double x = 0, y = 0;
};
using FplPolyline = std::vector<FplPoint>;
using FplPolygon = std::vector<FplPoint>;

// ---- The view --------------------------------------------------------------------------------------------------------

struct FplViewport {
	double xMin = -10, xMax = 10, yMin = -10, yMax = 10;	// the plane's part shown
	double width = 800, height = 600;						// on the screen

	bool Valid() const;
	double ScaleX() const { return width / (xMax - xMin); }		// DIPs per unit
	double ScaleY() const { return height / (yMax - yMin); }
	double ToScreenX(double x) const { return (x - xMin) * ScaleX(); }
	double ToScreenY(double y) const { return (yMax - y) * ScaleY(); }
	double ToPlaneX(double sx) const { return xMin + sx / ScaleX(); }
	double ToPlaneY(double sy) const { return yMax - sy / ScaleY(); }
	FplPoint ToScreen(FplPoint p) const { return { ToScreenX(p.x), ToScreenY(p.y) }; }
	FplPoint ToPlane(FplPoint s) const { return { ToPlaneX(s.x), ToPlaneY(s.y) }; }

	// factor > 1 zooms in, keeping the plane's point under the screen point where it is.
	void Zoom(double factorX, double factorY, FplPoint screen);
	void Pan(double dxScreen, double dyScreen);		// the contents follow the mouse moving by this much
	void Resize(double newWidth, double newHeight);	// keeps the center and the scales
	void EqualScale();								// grows the axis with the bigger scale around its center
};

// ---- Axis ticks ------------------------------------------------------------------------------------------------------

struct FplTicks {
	double step = 0;				// between majors
	bool pi = false;				// the step is piNum/piDen pi
	int piNum = 1, piDen = 1;
	std::vector<double> major;		// in the range, ascending
	std::vector<double> minor;		// between the majors (not on them)
};

// Majors at least minSpacing DIPs apart over [min, max] drawn over pixels DIPs: 1, 2 or 5 times a power of 10, or with pi
// multiples of pi (pi/12, pi/6, pi/4, pi/2, pi, then 2, 5, 10... times), falling back to decimals below pi/12.
FplTicks FplMakeTicks(double min, double max, double pixels, double minSpacing, bool pi = false);

// A tick's label: "-0.5" (with U+2212), "3pi/4" (U+03C0), "2.5x10^6" (U+00D7 and superscript digits) past 1e6 or below
// 1e-4.
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

// ---- An expression's geometry ----------------------------------------------------------------------------------------

struct FplGeometry {
	std::vector<FplPolyline> lines;		// the curve, or a region's boundary
	std::vector<FplPolygon> fills;		// an inequality's region
	std::vector<FplPoint> points;		// Point: in screen coordinates (may be outside the view)
	std::vector<FplContour> contours;	// Surface (options.contours)
	FplHeatmap heatmap;					// Surface (options.heatmap)
	std::vector<FplArrow> arrows;		// VectorField
	bool dashed = false;				// a strict inequality's boundary
	size_t evals = 0;
};

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
