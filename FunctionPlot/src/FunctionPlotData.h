#pragma once

// The function plot control's state, kept out of the public header: the graphs (FplPlot), the view, the Direct2D and
// DirectWrite resources with the graphs' geometries, the legend's and sliders' rows, tracing and dragging.

#include "FunctionPlot.h"
#include "FunctionPlotGraphs.h"
#include "FunctionPlotRender3D.h"
#include "FunctionPlotWorker.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <memory>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

ID2D1Factory* FplD2DFactory();
IDWriteFactory* FplDWriteFactory();

// a graph's sampled geometry for drawing
struct FplGraphPaths {
	ComPtr<ID2D1PathGeometry> lines;
	ComPtr<ID2D1PathGeometry> fills;
	ComPtr<ID2D1PathGeometry> contours;
	ComPtr<ID2D1PathGeometry> arrows;
	ComPtr<ID2D1PathGeometry> marks;	// a slope field's
	struct Label {
		FplPoint at;
		std::wstring text;
	};
	std::vector<Label> labels;			// a contour level's, at the middle of its longest line
	// a heat map: its pixels (premultiplied BGRA) and the bitmap made from them on the target
	std::vector<uint32_t> heatPixels;
	int heatCols = 0, heatRows = 0;
	float heatCell = 0;
	ComPtr<ID2D1Bitmap> heat;
};

// a row of the panel over the plot's top left: a graph in the legend, or a parameter's slider
struct FplPanelRow {
	enum Kind : uint8_t { Graph, Slider } kind = Graph;
	int index = 0;						// in the plot's graphs or params
	D2D1_RECT_F rect{};					// client DIPs
	D2D1_RECT_F track{};				// a slider's
	D2D1_RECT_F play{};					// a slider's play button
	bool error = false;					// a graph's: a second line with the message
	bool note = false;					// a graph's: a second line with a regression's fit
	bool stopped = false;				// an ODE's solution ran out of steps
};

enum class FplPanelPart : uint8_t { None, Panel, Graph, Slider, Play, Toggle };

struct FplPanelHit {
	FplPanelPart part = FplPanelPart::None;
	int row = -1;
};

enum class FplDrag : uint8_t { None, Pan, Trace, Slider, Orbit, Pan3D, Point, Box, Point3D };

// sizes, in DIPs
constexpr float FplPanelMargin = 10, FplPanelPad = 6, FplGraphRow = 26, FplErrorLine = 16, FplSliderRow = 30;
constexpr float FplTraceDistance = 10, FplSnapDistance = 8;

struct FplState {
	FplPlot plot;
	FplViewport view;
	bool viewSet = false;				// the view has been given a size once
	bool pendingView = false;			// SetState's ranges, for the first size to keep
	bool homeSet = false;
	FPLVIEW home{};
	UINT dpi = 96;
	SIZE client{};						// pixels

	// Direct2D
	ComPtr<ID2D1HwndRenderTarget> target;
	ID2D1RenderTarget* rt = nullptr;	// what's painted on: the window's target, or an export's
	float paintDpi = 96;
	bool exporting = false;
	std::unordered_map<COLORREF, ComPtr<ID2D1SolidColorBrush>> brushes;
	ComPtr<ID2D1StrokeStyle> roundStroke, dashStroke, dotStroke;
	ComPtr<IDWriteTextFormat> labelCenter, labelLeft, labelRight, panelText, panelSmall, traceText, icons;
	std::vector<FplGraphPaths> paths;	// by graph

	// the panel
	std::vector<FplPanelRow> rows;
	D2D1_RECT_F panel{};				// empty: none
	D2D1_RECT_F toggle{};				// the button that folds it (empty: none)
	bool collapsed = false;
	int hotRow = -1;
	bool hotToggle = false;
	int focusRow = -1;					// the slider the keys move (-1: the keys pan)
	ULONGLONG lastTick = 0;				// the animation's

	// tracing
	int traceGraph = -1;
	int traceFeature = -1;
	double traceX = 0, traceY = 0;
	double traceZ = std::numeric_limits<double>::quiet_NaN();	// a surface's value, or 3D's z
	bool traceValid = false;
	bool traceDragged = false;			// stays while the mouse is away (until a click elsewhere or Escape)
	int featuresGraph = -1;				// the graph whose features are found and shown

	// dragging
	FplDrag drag = FplDrag::None;
	POINT dragLast{};
	int dragRow = -1;
	int dragGraph = -1;					// the point being dragged
	int dragPoint = 0;					// which of its graph's
	POINT boxStart{}, boxEnd{};			// a rectangle to zoom to
	bool boxShown = false;				// it's moved far enough to be one

	// undo
	FplHistory history;
	int lastKind = -1;					// the step being made (a burst of wheel notches, slider keys)
	ULONGLONG lastTime = 0;
	std::wstring axisTitles[3];
	int hotPoint = -1;					// the draggable point under the mouse: its graph
	int hotPointIndex = 0;				// and which of its
	bool trackingLeave = false;

	// 3D
	bool mode3D = false;
	HWND view3D = nullptr;				// the 3D frames' window: a child over the control's client area, which the
										// mouse goes through (a flip-model swap chain on the control's own window would
										// keep 2D's Direct2D target from showing afterwards)
	FplBox3 box;
	FplCamera camera;
	FplRenderer3D renderer;
	std::vector<FplGpuMesh> gpu;		// by graph
	std::vector<FplGpuMesh> gpuFills;	// regions' on the z = 0 plane
	std::vector<FplGpuMesh> gpuFields;	// fields' on the same plane
	FplGpuMesh boxEdges, floorGrid;
	bool gpuStale = true;				// the meshes changed since they were uploaded

	// sampling the heavy graphs on another thread: a coarse version now, the full one when it comes
	std::unique_ptr<FplWorker> worker;
	std::vector<uint64_t> generations;	// by graph: the job whose result is wanted
	uint64_t nextGeneration = 1;
	bool syncSampling = false;			// everything at full quality now (exports)

	// look
	bool customColors = false;
	FPLCOLORS colors{};
	int forceHighContrast = -1;

	ID2D1SolidColorBrush* Brush(COLORREF color);
};
