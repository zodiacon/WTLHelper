#pragma once

// The function plot control's state, kept out of the public header: the graphs (FplPlot), the view, the Direct2D and
// DirectWrite resources with the graphs' geometries, the legend's and sliders' rows, tracing and dragging.

#include "FunctionPlot.h"
#include "FunctionPlotGraphs.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

ID2D1Factory* FplD2DFactory();
IDWriteFactory* FplDWriteFactory();

// a graph's sampled geometry for drawing
struct FplGraphPaths {
	ComPtr<ID2D1PathGeometry> lines;
	ComPtr<ID2D1PathGeometry> fills;
};

// a row of the panel over the plot's top left: a graph in the legend, or a parameter's slider
struct FplPanelRow {
	enum Kind : uint8_t { Graph, Slider } kind = Graph;
	int index = 0;						// in the plot's graphs or params
	D2D1_RECT_F rect{};					// client DIPs
	D2D1_RECT_F track{};				// a slider's
	bool error = false;					// a graph's: a second line with the message
};

enum class FplPanelPart : uint8_t { None, Panel, Graph, Slider };

struct FplPanelHit {
	FplPanelPart part = FplPanelPart::None;
	int row = -1;
};

enum class FplDrag : uint8_t { None, Pan, Trace, Slider };

// sizes, in DIPs
constexpr float FplPanelMargin = 10, FplPanelPad = 6, FplGraphRow = 26, FplErrorLine = 16, FplSliderRow = 30;
constexpr float FplTraceDistance = 10, FplSnapDistance = 8;

struct FplState {
	FplPlot plot;
	FplViewport view;
	bool viewSet = false;				// the view has been given a size once
	bool homeSet = false;
	FPLVIEW home{};
	UINT dpi = 96;
	SIZE client{};						// pixels

	// Direct2D
	ComPtr<ID2D1HwndRenderTarget> target;
	std::unordered_map<COLORREF, ComPtr<ID2D1SolidColorBrush>> brushes;
	ComPtr<ID2D1StrokeStyle> roundStroke, dashStroke;
	ComPtr<IDWriteTextFormat> labelCenter, labelLeft, labelRight, panelText, panelSmall, traceText;
	std::vector<FplGraphPaths> paths;	// by graph

	// the panel
	std::vector<FplPanelRow> rows;
	D2D1_RECT_F panel{};				// empty: none
	int hotRow = -1;

	// tracing
	int traceGraph = -1;
	int traceFeature = -1;
	double traceX = 0, traceY = 0;
	bool traceValid = false;
	bool traceDragged = false;			// stays while the mouse is away (until a click elsewhere or Escape)
	int featuresGraph = -1;				// the graph whose features are found and shown

	// dragging
	FplDrag drag = FplDrag::None;
	POINT dragLast{};
	int dragRow = -1;
	bool trackingLeave = false;

	// look
	bool customColors = false;
	FPLCOLORS colors{};
	int forceHighContrast = -1;

	ID2D1SolidColorBrush* Brush(COLORREF color);
};
