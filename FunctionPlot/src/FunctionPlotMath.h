#pragma once

// A graph's text as mathematics, window-free: the parser's display mode (FplParseOptions::display) keeps what was typed, and
// this lays that tree out as text and strokes around a baseline: fractions, powers, roots, delimiters that grow, sums. Whoever
// draws it measures the text and draws the items (Direct2D in FunctionPlotMathDraw.cpp, SVG in FunctionPlotExport.cpp).

#include "FunctionPlotExpr.h"
#include "FunctionPlotModel.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct FplMathItem {
	bool isLine = false;
	// text
	std::wstring text;
	bool italic = false;				// a variable's: Cambria italic, else Cambria Math
	double size = 0;
	double x = 0, y = 0;				// its start, on its baseline, from the layout's start on its baseline
	// a stroke: a radical sign, a fraction's bar, a delimiter
	std::vector<FplPoint> points;		// from the same place, y down
	double stroke = 0;
};

struct FplMathLayout {
	std::vector<FplMathItem> items;
	double width = 0, ascent = 0, descent = 0;
	bool Empty() const { return items.empty(); }
};

// A text's width at a size.
using FplMathMeasure = std::function<double(const std::wstring& text, double size, bool italic)>;

// The tree parsed with options.display, at a font size.
FplMathLayout FplLayoutMath(const FplAst& ast, int root, double size, const FplMathMeasure& measure);

// A text at the first of the sizes that fits maxWidth (0: the first). false if it doesn't read, or none fits.
bool FplFitMath(std::wstring_view text, FplParseOptions options, std::initializer_list<double> sizes, double maxWidth,
	const FplMathMeasure& measure, FplMathLayout& layout);

// A text as LaTeX ("\sqrt{x^{2} + y^{2}}") or a MathML document (<math>...), showing what the layout shows. Empty if it doesn't read.
std::wstring FplToLatex(std::wstring_view text, const FplParseOptions& options = {});
std::wstring FplToMathML(std::wstring_view text, const FplParseOptions& options = {});

// Its texts in order, with '|' for a stroke: for tests.
std::wstring FplDescribeMath(const FplMathLayout& layout);
