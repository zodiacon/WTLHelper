#pragma once

// The function plot's graphs, window-free: the app's graphs and parameters, definitions ("c = 2a") worked out in
// order, sampling every visible graph for a view, the features of a function (roots, extrema, intersections with the
// other functions), and what a trace lands on.

#include "FunctionPlotModel.h"

#include <cstdint>
#include <string>
#include <vector>

// A graph's flags (the control's FPLGF_* have the same values).
enum : unsigned {
	FplGraphHidden = 0x0001,
	FplGraphDashed = 0x0002,
	FplGraphNoTrace = 0x0004,
	FplGraphNoFeatures = 0x0008,
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

	// built
	FplExpression expr;				// expr.error: why it draws nothing
	bool definition = false;		// "c = 2a": defines the name, draws nothing
	int slot = -1;					// a definition's slot
	FplGeometry geometry;			// from the last Sample
	std::vector<FplFeature> features;	// from the last FindFeatures (a function's only)

	bool Drawable() const { return !definition && !expr.error && !(flags & FplGraphHidden); }
};

struct FplParam {
	std::wstring name;
	double value = 0;
	double min = -10, max = 10;
	double step = 0;				// 0: any value
	unsigned flags = 0;
};

// What a trace is on: a graph's point, maybe one of its features.
struct FplTraceHit {
	int graph = -1;					// -1: nothing
	int feature = -1;				// in the graph's features
	FplPoint plane;
	FplPoint screen;
	double distance = 0;			// DIPs from where it was asked
	explicit operator bool() const { return graph >= 0; }
};

class FplPlot {
public:
	std::vector<FplGraph> graphs;
	std::vector<FplParam> params;
	bool degrees = false;

	int FindGraph(unsigned id) const;
	int FindParam(std::wstring_view name) const;

	// Builds every graph again: after texts, parameters' names or the angle unit changed. Then UpdateValues.
	void Build();
	// The parameters' values changed: the slots and the definitions again (the geometry is stale until Sample).
	void UpdateValues();
	const std::vector<double>& Slots() const { return m_Slots; }
	// A definition's value (NaN if it has none).
	double DefinitionValue(int graph) const;

	// Samples every drawable graph; the others' geometry is cleared.
	void Sample(const FplViewport& view, const FplSampleOptions& options = {});
	// Roots, extrema and the y intercept of a function graph over the view, and where it meets the other drawable
	// functions (Intersection), into its features. Others get none.
	void FindFeatures(int graph, const FplViewport& view);

	// y = f(x) or x = f(y) of a function graph (NaN for other kinds).
	double Eval(int graph, double v) const;

	// The drawable, traceable graph nearest a screen point, within maxDistance DIPs; on a feature within
	// snapDistance of it. A function's point is exact (on the curve at the x found), others' on their polylines.
	FplTraceHit Nearest(FplPoint screen, const FplViewport& view, double maxDistance, double snapDistance) const;
	// Following one graph while the mouse drags: a function at the mouse's x (y for x = f(y)), snapping to a feature
	// within snapDistance in that direction; other kinds the nearest point on their polylines.
	FplTraceHit TraceAt(int graph, FplPoint screen, const FplViewport& view, double snapDistance) const;

private:
	FplTraceHit OnGraph(int graph, FplPoint screen, const FplViewport& view) const;
	void SnapToFeature(FplTraceHit& hit, FplPoint screen, const FplViewport& view, double snapDistance,
		bool alongOnly) const;

	std::vector<std::wstring> m_Names;	// the parameters, then the definitions
	std::vector<double> m_Slots;
	std::vector<int> m_Definitions;		// graphs, in order
};

// A traced coordinate with as many decimals as a DIP needs at this scale (unitsPerDip), plus one; trailing zeros
// dropped; U+2212 for minus; "0" for what rounds to zero.
std::wstring FplFormatCoordinate(double value, double unitsPerDip);
