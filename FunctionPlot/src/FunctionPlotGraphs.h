#pragma once

// The function plot's graphs, window-free: the app's graphs and parameters, definitions ("c = 2a") worked out in
// order, sampling every visible graph for a view, the features of a function (roots, extrema, intersections with the
// other functions), and what a trace lands on.

#include "FunctionPlotMath.h"
#include "FunctionPlot3D.h"
#include "FunctionPlotModel.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// A graph's flags (the control's FPLGF_* have the same values).
enum : unsigned {
	FplGraphHidden = 0x0001,
	FplGraphDashed = 0x0002,
	FplGraphNoTrace = 0x0004,
	FplGraphNoFeatures = 0x0008,
	FplGraphHeatmap = 0x0010,		// a surface: a heat map of its values
	FplGraphNoContours = 0x0020,	// a surface: no contour lines
	FplGraphNoDrag = 0x0040,		// a point the user can't drag
	FplGraphDotted = 0x0080,		// a dotted line
	FplGraphHollow = 0x0100,		// points as rings
	FplGraphMeshLines = 0x0200,		// 3D: lines on a surface (its grid, or the box's ticks)
	FplGraphLevelLines = 0x0400,	// 3D: lines on a surface at heights
	FplGraphNoSingular = 0x0800,	// no asymptotes or holes
};

struct FplGraph {
	unsigned id = 0;
	std::wstring text;
	uint32_t color = 0;				// a COLORREF
	bool autoColor = true;			// color from the palette by position
	float width = 2.5f;				// DIPs
	unsigned flags = 0;
	bool hasRange = false;			// tMin, tMax for a parametric or polar graph (else 0 to 2 pi)
	double tMin = 0, tMax = 6.283185307179586;
	intptr_t lParam = 0;
	float opacity = 1;				// the lines' and points'
	float fillOpacity = -1;			// regions' (0.22) and heat maps' (0.85) when negative
	std::wstring label;				// beside its points or at its curve's end; {x} and {y} its coordinates

	// built
	FplExpression expr;				// expr.error: why it draws nothing
	bool definition = false;		// "c = 2a": defines the name, draws nothing
	int function = -1;				// "f(x) = x^2": the user function it defines (drawn when its parameter is x, y, t or
									// theta, as y = f(x), x = f(y), a curve, r = f(theta))
	int slot = -1;					// a definition's slot
	int dragX = -1, dragY = -1;		// a point whose coordinate is a parameter: the parameter's index (dragging it
									// sets the parameter)
	std::vector<std::pair<int, int>> drags;	// an ODE's: each initial point's (the first is also dragX, dragY)
	int dragZ = -1;					// (a, b, c): c's parameter
	std::vector<int> dragsZ;		// a 3D system's, by point
	bool listDefinition = false;	// "L = [1, 2, a]": defines a list (definition is false)
	std::vector<FplProgram> items;	// its items
	std::vector<int> listSlots;		// the lists it reads: it's drawn once per element (the shortest's count)
	std::vector<int> fitted;		// a regression's: the slots of the names it fits
	std::vector<double> fit;		// their values (where the next fit starts)
	double r2 = std::numeric_limits<double>::quiet_NaN();	// a regression's R squared
	FplGeometry geometry;			// from the last Sample
	FplMesh mesh;					// from the last Build3D
	std::vector<FplFeature> features;	// from the last FindFeatures (a function's only)
	bool incomplete3D = false;			// as geometry.incomplete, for the mesh
	FplMesh fillMesh;					// a region's, on the z = 0 plane in 3D
	FplMesh fieldMesh;					// a field's slopes or arrows, on the same plane
	float fieldOpacity = 1;				// fainter under solutions, as in 2D

	bool Drawable() const {
		return !definition && !listDefinition && !expr.error && !(flags & FplGraphHidden) &&
			expr.kind != FplKind::Regression;
	}
	bool Listed() const { return !listSlots.empty(); }
	int DragZOf(size_t point) const {
		if (!dragsZ.empty())
			return point < dragsZ.size() ? dragsZ[point] : -1;
		return point == 0 ? dragZ : -1;
	}
	// What dragging its geometry's point sets: x's and y's parameters (-1: none).
	std::pair<int, int> DragOf(size_t point) const {
		if (!drags.empty())
			return point < drags.size() ? drags[point] : std::pair<int, int>{ -1, -1 };
		return point == 0 ? std::pair<int, int>{ dragX, dragY } : std::pair<int, int>{ -1, -1 };
	}
};

// An app's list of numbers (a table's column): "x1", "y1".
struct FplList {
	std::wstring name;
	std::vector<double> values;
};

constexpr unsigned FplParamAuto = 0x0002;		// FPLPF_AUTO

struct FplParam {
	std::wstring name;
	double value = 0;
	double min = -10, max = 10;
	double step = 0;				// 0: any value
	unsigned flags = 0;
	bool animating = false;			// sweeping its range (the control's)
	int direction = 1;
	double sweepSeconds = 4;		// to cross its range once, when it's playing
};

// A playing parameter a step on: across its range in seconds, turning back at the ends. false if it can't move.
bool FplSweep(FplParam& param, double dt);

// Where a graph's label goes (screen coordinates) and what it says.
struct FplLabelPlace {
	int graph = -1;
	FplPoint at;					// the point it's beside
	std::wstring text;
};

// A label's text with {x} and {y} put in, to the precision a DIP needs (unitsPerDip) plus one.
std::wstring FplFormatLabel(std::wstring_view label, double x, double y, double unitsPerDip);

// What undo brings back: the parameters' values (by name), the graphs' hiding (by id), the 2D view's ranges, the 3D
// box and camera.
struct FplSnapshot {
	std::vector<std::pair<std::wstring, double>> params;
	std::vector<std::pair<unsigned, bool>> hidden;
	double view[4] = {};
	double box[6] = {};
	float yaw = 0, pitch = 0;
	bool operator==(const FplSnapshot& other) const;
};

// The user's steps back and forth.
class FplHistory {
public:
	explicit FplHistory(size_t limit = 200) : m_Limit(limit) {}
	// Before a step: the state it starts from (the redo steps are gone).
	void Push(FplSnapshot before);
	bool CanUndo() const { return !m_Undo.empty(); }
	bool CanRedo() const { return !m_Redo.empty(); }
	// The state to go back (or forward) to, given the current one, which the other way keeps; steps that change
	// nothing are skipped. false if there's none.
	bool Undo(const FplSnapshot& current, FplSnapshot& to);
	bool Redo(const FplSnapshot& current, FplSnapshot& to);
	void Clear();
	size_t Steps() const { return m_Undo.size(); }

private:
	std::vector<FplSnapshot> m_Undo, m_Redo;
	size_t m_Limit;
};

// What a trace is on: a graph's point, maybe one of its features.
struct FplTraceHit {
	int graph = -1;					// -1: nothing
	int feature = -1;				// in the graph's features
	FplPoint plane;
	FplPoint screen;
	double z = std::numeric_limits<double>::quiet_NaN();	// a surface's value there (2D), the point's z (3D)
	double distance = 0;			// DIPs from where it was asked
	explicit operator bool() const { return graph >= 0; }
};

class FplPlot {
public:
	std::vector<FplGraph> graphs;
	std::vector<FplParam> params;
	std::vector<FplList> lists;		// the app's (a list defined by a graph is that graph's)
	bool degrees = false;

	int FindGraph(unsigned id) const;
	int FindParam(std::wstring_view name) const;

	// Builds every graph again: after texts, parameters' names or the angle unit changed. Then UpdateValues.
	void Build();
	// The parameters' values changed: the slots and the definitions again (the geometry is stale until Sample).
	void UpdateValues();
	const std::vector<double>& Slots() const { return m_Slots; }
	const std::vector<FplUserFunction>& Functions() const { return m_Functions; }
	// A definition's value (NaN if it has none).
	double DefinitionValue(int graph) const;
	// An integral's (kind Integral) value (NaN for other kinds).
	double IntegralValue(int graph) const;
	// Any name's value now: a parameter, a definition, a fitted name, an aggregate ("mean(L)"); NaN if none.
	double ValueOf(std::wstring_view name) const;
	// A list's values (the app's or a defined one), null if there's no such list.
	const std::vector<double>* ListValues(std::wstring_view name) const;
	// A regression's fitted names and values.
	std::vector<std::pair<std::wstring, double>> Fitted(int graph) const;
	// The lists' count a listed graph is drawn for (0 when it isn't listed or a list is empty).
	size_t ElementCount(int graph) const;

	// The extent of what the drawable graphs draw (points exactly, curves over their range, a function's values over
	// the view's x, implicit curves' sampled lines), the 2nd to 98th percentile of a curve's points so an asymptote
	// doesn't stretch it, and a little room around; false if nothing has an extent.
	bool FitBounds(const FplViewport& view, double& xMin, double& xMax, double& yMin, double& yMax) const;
	// Where the labels go, from the last Sample: beside each point, at the right end of a curve's part in the view.
	std::vector<FplLabelPlace> LabelPlaces(const FplViewport& view) const;

	// Samples every drawable graph; the others' geometry is cleared.
	void Sample(const FplViewport& view, const FplSampleOptions& options = {});
	void SampleGraph(int graph, const FplViewport& view, const FplSampleOptions& options = {});
	// The options a graph is sampled with (its heat map and contours, its range) on top of the base ones.
	FplSampleOptions OptionsFor(int graph, const FplSampleOptions& base) const;
	// Worth sampling on another thread: implicit curves and regions and surfaces in 2D, implicit surfaces in 3D (not
	// listed ones, which go element by element here).
	bool IsHeavy(int graph, bool in3D, const FplViewport* view = nullptr) const;
	// Seconds an evaluation takes on average, measured now at a few places over the view (an integral's cost grows with its
	// limits, so one place can look cheap), if the graph has an integral, a sum or a product, whose cost can't be seen from
	// its size (else 0). It stops counting once that's clearly slow. A curve past about 8 microseconds is heavy, and past 50
	// its features aren't looked for.
	double EvalCost(int graph, const FplViewport* view = nullptr) const;
	// For drawing a text as mathematics with the names this plot knows.
	FplParseOptions DisplayOptions() const;
	// How to sample a heavy graph at once, on the UI thread: a surface or implicit curve on large cells, a slow curve as many
	// straight lines as about 6 ms of evaluations make, with no refining or analysis.
	FplSampleOptions CoarseOptions(int graph, const FplViewport& view) const;
	// The drawable graphs' meshes for a 3D box: surfaces (z = F(x, y), parametric), curves as tubes (space curves, and
	// the 2D curves on the z = 0 plane), points as spheres. An ODE's solutions, implicit curves and regions' boundaries
	// too, and what's between two curves' sides (their slopes, arrows and fills stay 2D's); vector fields have none.
	void Build3D(const FplBox3& box);
	void Build3DGraph(int graph, const FplBox3& box, int implicitCells = 48);
	// The lines a surface has by its flags: z = F(x, y) in x and y at the box's ticks, a parametric surface along its u
	// and v, an implicit one in x, y and z. Other kinds have none.
	FplLineGrid LineGridOf(int graph, const FplBox3& box) const;

	// Roots, extrema and the y intercept of a function graph over the view, and where it meets the other drawable
	// functions (Intersection), into its features. Any graph with a curve (x = f(y), polar, parametric, implicit, an
	// ODE's solutions, regions' boundaries) also gets where it meets the others' curves in the view, from what was
	// sampled last. Others get none.
	void FindFeatures(int graph, const FplViewport& view);

	// y = f(x) or x = f(y) of a function graph (NaN for other kinds).
	double Eval(int graph, double v) const;

	// The drawable, traceable graph nearest a screen point, within maxDistance DIPs; on a feature within
	// snapDistance of it. A function's point is exact (on the curve at the x found), others' on their polylines.
	// With surfaces, when no curve is near, the topmost surface (z = F(x, y)) at the point itself, with its value.
	FplTraceHit Nearest(FplPoint screen, const FplViewport& view, double maxDistance, double snapDistance,
		bool surfaces = false) const;
	// Following one graph while the mouse drags: a function at the mouse's x (y for x = f(y)), snapping to a feature
	// within snapDistance in that direction; other kinds the nearest point on their polylines.
	FplTraceHit TraceAt(int graph, FplPoint screen, const FplViewport& view, double snapDistance) const;
	// Keyboard tracing: the point dips DIPs further along the graph from a plane point (negative: back), a function by
	// its x (y for x = f(y)), past where it's undefined, other kinds along their polylines (stopping at their ends).
	FplTraceHit Step(int graph, FplPoint plane, double dips, const FplViewport& view) const;
	// The graph's next feature (FindFeatures') past x, dir +1 or -1; none if there isn't one. Features are in order of x
	// then y: at the same x, the one past y (a vertical line through a circle has two).
	FplTraceHit NextFeature(int graph, double x, int dir, const FplViewport& view,
		double y = std::numeric_limits<double>::quiet_NaN()) const;
	// The next drawable, traceable function graph after graph (dir +1 or -1, going round) defined at x, at x.
	FplTraceHit NextGraphAt(int graph, double x, int dir, const FplViewport& view) const;
	// dy/dx of y = f(x) (dx/dy of x = f(y)) at v: the derivative's program, else differences; NaN for other kinds.
	double Slope(int graph, double v) const;

	// The draggable point (a point graph with a parameter for a coordinate) within maxDistance DIPs of a screen
	// point, the nearest; -1 if none.
	// point gets which of the graph's points it is.
	int DraggableAt(FplPoint screen, double maxDistance, int* point = nullptr) const;
	// The points a graph has in 3D, in the space's coordinates: (a, b, c), a 2D point or an ODE's start (on z = 0), a 3D
	// system's start. In the order DragOf numbers them.
	std::vector<FplPoint3> Points3D(int graph) const;
	// Whether a solid surface is in front of this place in the box, seen from the camera (the graph's own mesh doesn't count).
	bool HiddenBySurface(int graph, FplVec3 place, const FplCamera& camera, float width, float height) const;
	// The draggable point at a screen position in 3D (in the box, and with a parameter to set), the graph, or -1.
	int DraggableAt3D(FplPoint screen, const FplCamera& camera, const FplBox3& box, float width, float height,
		double maxDistance, int* point = nullptr) const;

	// z = F(x, y) of a surface graph (NaN for other kinds).
	double EvalSurface(int graph, double x, double y) const;
	// 3D: the nearest graph the ray (in the cube) meets, by its mesh (Build3D's): the point in space (plane and z),
	// exactly on the surface for z = F(x, y) and at a point's own place.
	FplTraceHit Pick3D(FplVec3 origin, FplVec3 dir, const FplBox3& box) const;

private:
	void FindDragged(FplGraph& g, FplParseOptions options);
	void AddList(const std::wstring& name);
	bool TryRegression(FplGraph& g, FplParseOptions options);
	void Fit(FplGraph& g);
	void SetElement(std::vector<double>& slots, const FplGraph& g, size_t i) const;
	FplMesh Mesh3D(const FplGraph& g, const FplBox3& box, int implicitCells);
	bool m_MeshIncomplete = false;
	bool m_PlainQ = false, m_WeakQ = false;	// what Build found q to be
	FplMesh m_FillMesh;				// what Mesh3D made of a region's fills
	FplMesh m_FieldMesh;			// and of a field's slopes or arrows
	float m_FieldOpacity = 1;
	void FindCurveMeetings(int graph, const FplViewport& view);
	static void SortFeatures(FplGraph& g);
	FplTraceHit OnGraph(int graph, FplPoint screen, const FplViewport& view) const;
	void SnapToFeature(FplTraceHit& hit, FplPoint screen, const FplViewport& view, double snapDistance,
		bool alongOnly) const;

	std::vector<std::wstring> m_Names;	// the parameters, the app's lists, then the definitions (and lists, fitted names)
	std::vector<double> m_Slots;
	std::vector<int> m_Definitions;		// graphs, in order
	std::vector<FplUserFunction> m_Functions;
	struct ListSlot {
		std::wstring name;
		int slot = -1;					// its element's slot; its aggregates' follow
		int graph = -1;					// a defined list's graph; -1: the app's (lists[app])
		int app = -1;
		std::vector<double> values;
	};
	std::vector<ListSlot> m_Lists;
	std::vector<std::wstring> m_ListNames;
	std::vector<int> m_Regressions;		// graphs
};

// A plot's state (the control's GetState and SetState): the graphs' settings (not what's built from them, not lParam),
// the parameters, the lists, the angle unit, the 2D view's ranges, the 3D box and camera, the mode and the panel's fold.
struct FplSavedState {
	std::vector<FplGraph> graphs;
	std::vector<FplParam> params;
	std::vector<FplList> lists;
	bool degrees = false, mode3D = false, collapsed = false;
	bool piTicks = false, polarGrid = false;
	bool hasTicks = false;				// read from a text that said (older ones leave the control's as they are)
	FplViewport view;					// its ranges (the size is the window's)
	FplBox3 box;
	float yaw = FplCamera{}.yaw, pitch = FplCamera{}.pitch;
	std::wstring axisTitles[3];
};
// As text: a line per setting, tab-separated fields, names and texts escaped (FunctionPlotState.cpp has the format).
std::wstring FplSaveState(const FplSavedState& state);
// false if it isn't a plot's state; lines it doesn't know are skipped.
bool FplLoadState(std::wstring_view text, FplSavedState& state);

// The graphs as SVG: the grid, the axes and their labels, the fills, contours, curves, arrows and points (no heat
// maps), and a legend of the graphs. Colors are COLORREFs; graphColors by graph.
struct FplSvgStyle {
	uint32_t back = 0xFFFFFF, grid = 0xDDDDDD, gridMinor = 0xEEEEEE, axis = 0x404040, label = 0x404040;
	uint32_t panelBack = 0xF8F8F8, panelText = 0, panelBorder = 0xCCCCCC;
	std::vector<uint32_t> graphColors;
	bool showGrid = true, showAxes = true, piTicks = false, showLegend = true, polarGrid = false, showColorScale = true;
	FplMathMeasure mathMeasure;			// the legend's texts as mathematics, measured by this (none: as they were typed)
	std::wstring axisTitles[2];			// x, y: at the axes' ends
};
std::string FplWriteSvg(const FplPlot& plot, const FplViewport& view, const FplSvgStyle& style);
// A PNG of rows of RGBA bytes (straight alpha), uncompressed: small enough for a heat map, and needing no codec.
std::string FplEncodePng(int width, int height, const std::vector<uint8_t>& rgba);

// A traced coordinate with as many decimals as a DIP needs at this scale (unitsPerDip), plus one; trailing zeros
// dropped; U+2212 for minus; "0" for what rounds to zero.
std::wstring FplFormatCoordinate(double value, double unitsPerDip);
