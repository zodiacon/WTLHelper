// Unit tests for the function plot's model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number of failed
// checks. Covered: the expression engine (lexing, implicit products, name splitting, parsing, errors and their
// positions, what an expression draws, constant folding, compiling and the stack machine against the tree, the math
// functions, degrees, derivatives against differences, simplifying and printing), and the 2D model (the view, ticks
// and labels, clipping, sampling functions, curves, implicit curves and regions, roots, extrema and intersections), and the
// control's graphs (definitions, sampling, features, tracing, coordinates), user functions
// and d/dx, surfaces (heat maps, contours), vector fields and the SVG export, and 3D (the
// box, the camera, meshes, tubes and the graphs' meshes), tracing by keys, draggable points, integrals, lists and
// regressions, the saved state, fitting the view, labels, styles, undo, the sampling thread, log axes,
// ODEs, curves' intersections and surfaces' line grids.

#include <filesystem>
#include <fstream>
#include <set>
#include <iterator>
#include <cstring>
#include "FunctionPlotExpr.h"
#include "FunctionPlot3D.h"
#include "FunctionPlotGraphs.h"
#include "FunctionPlotModel.h"
#include "FunctionPlotShaders.h"

#pragma comment(lib, "d3d11.lib")

#include "FunctionPlotWorker.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <numbers>
#include <string>
#include <vector>

// ---- Tiny harness ------------------------------------------------------------

static int g_checks = 0;
static int g_failures = 0;
static const wchar_t* g_currentTest = L"";

static void Check(bool ok, const char* expr, int line) {
	g_checks++;
	if (!ok) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
	}
}

static void CheckInt(long long actual, long long expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: %lld, want: %lld\n", g_currentTest, line, expr, actual, expected);
	}
}

static std::wstring Visible(const std::wstring& s) {
	std::wstring out;
	for (wchar_t ch : s) {
		if (ch < 32 || ch > 126) {
			wchar_t buf[8];
			swprintf_s(buf, L"\\x%04X", ch);
			out += buf;
		}
		else
			out += ch;
	}
	return out;
}

static void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got:  \"%s\"\n        want: \"%s\"\n", g_currentTest, line, expr,
			Visible(actual).c_str(), Visible(expected).c_str());
	}
}

static bool Near(double a, double b, double tol = 1e-12) {
	if (std::isnan(a) || std::isnan(b))
		return std::isnan(a) && std::isnan(b);
	if (std::isinf(a) || std::isinf(b))
		return a == b;
	return std::abs(a - b) <= tol * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

static void CheckNear(double actual, double expected, double tol, const char* expr, int line) {
	g_checks++;
	if (!Near(actual, expected, tol)) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got: %.17g, want: %.17g\n", g_currentTest, line, expr, actual, expected);
	}
}

#define CHECK(expr)          Check((expr), #expr, __LINE__)
#define CHECK_INT(a, b)      CheckInt((long long)(a), (long long)(b), #a " == " #b, __LINE__)
#define CHECK_STR(a, b)      CheckStr((a), (b), #a " == " #b, __LINE__)
#define CHECK_NEAR(a, b)     CheckNear((a), (b), 1e-12, #a " ~ " #b, __LINE__)
#define CHECK_NEAR_TOL(a, b, t) CheckNear((a), (b), (t), #a " ~ " #b, __LINE__)

#define TEST(name)															\
	static void name();														\
	static void Run_##name() {												\
		g_currentTest = L## #name;											\
		const int before = g_failures;										\
		name();																\
		wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name);	\
	}																		\
	static void name()

// ---- Helpers -----------------------------------------------------------------

static const double NaN = std::numeric_limits<double>::quiet_NaN();
static const double Pi = std::numbers::pi;

static const std::vector<std::wstring> g_Params = { L"a", L"b" };
static const double g_ParamValues[] = { 2, 3 };

// The slots: x, y, t and theta all get x (so one argument tries any kind), then a = 2, b = 3.
static void Slots(double* s, double x, double y) {
	s[FplSlotX] = x;
	s[FplSlotY] = y;
	s[FplSlotT] = x;
	s[FplSlotTheta] = x;
	s[FplFirstParam] = g_ParamValues[0];
	s[FplFirstParam + 1] = g_ParamValues[1];
}

static const double Failed = -123456789;

// The compiled f of text at x (and y); Failed when it doesn't build.
static double F(std::wstring_view text, double x = 0, double y = 0, bool degrees = false) {
	auto e = FplBuild(text, g_Params, degrees);
	if (e.error)
		return Failed;
	double s[16];
	Slots(s, x, y);
	return e.f.Eval(s);
}

static double G(std::wstring_view text, double x = 0) {
	auto e = FplBuild(text, g_Params);
	if (e.error || e.g.Empty())
		return Failed;
	double s[16];
	Slots(s, x, 0);
	return e.g.Eval(s);
}

// The tree printed back, or the error's code.
static std::wstring P(std::wstring_view text, const std::vector<std::wstring>* params = &g_Params) {
	FplParseOptions o;
	o.parameters = params;
	auto r = FplParse(text, o);
	if (r.error)
		return L"error " + std::to_wstring(int(r.error.code));
	return FplToString(r.ast, r.root);
}

static FplError ParseError(std::wstring_view text, const std::vector<std::wstring>* params = &g_Params) {
	FplParseOptions o;
	o.parameters = params;
	return FplParse(text, o).error;
}

static FplError BuildError(std::wstring_view text) {
	return FplBuild(text, g_Params).error;
}

static FplKind KindOf(std::wstring_view text, FplRel* rel = nullptr) {
	auto e = FplBuild(text, g_Params);
	if (rel)
		*rel = e.rel;
	return e.kind;
}

static const std::vector<std::wstring> g_Names = { L"x", L"y", L"t", FplTheta, L"a", L"b" };

static double Tree(const FplAst& ast, int node, double x, double y = 0) {
	double v[] = { x, y, x, x, g_ParamValues[0], g_ParamValues[1] };
	return FplEvalTree(ast, node, g_Names, v);
}

// d/dx of text, compiled, at x; Failed when it can't be had.
static double Derivative(std::wstring_view text, double x, const wchar_t* var = L"x") {
	FplParseOptions o;
	o.parameters = &g_Params;
	auto r = FplParse(text, o);
	if (r.error)
		return Failed;
	int d = FplDerive(r.ast, r.root, var);
	if (d < 0)
		return Failed;
	auto prog = FplCompile(r.ast, d, g_Params);
	if (prog.Empty())
		return Failed;
	double s[16];
	Slots(s, x, 0);
	return prog.Eval(s);
}

static std::wstring DerivativeText(std::wstring_view text, const wchar_t* var = L"x") {
	FplParseOptions o;
	o.parameters = &g_Params;
	auto r = FplParse(text, o);
	if (r.error)
		return L"parse error";
	int d = FplDerive(r.ast, r.root, var);
	return d < 0 ? L"error" : FplToString(r.ast, d);
}

// ---- Tests -------------------------------------------------------------------

TEST(Numbers) {
	CHECK_NEAR(F(L"2"), 2);
	CHECK_NEAR(F(L"1.5e3"), 1500);
	CHECK_NEAR(F(L"2.5E-2"), 0.025);
	CHECK_NEAR(F(L".5"), 0.5);
	CHECK_NEAR(F(L"3."), 3);
	CHECK_NEAR(F(L"2e"), 2 * std::numbers::e);			// not an exponent without digits
	CHECK_NEAR(F(L"2e-x", 1), 2 * std::numbers::e - 1);
	CHECK_NEAR(F(L"pi"), Pi);
	CHECK_NEAR(F(L"\x03C0"), Pi);
	CHECK_NEAR(F(L"tau"), 2 * Pi);
	auto e = ParseError(L"1.2.3");
	CHECK_INT(e.code, FplErrorCode::BadNumber);
	CHECK_STR(e.name, L"1.2.3");
}

TEST(Precedence) {
	CHECK_NEAR(F(L"1+2*3"), 7);
	CHECK_NEAR(F(L"(1+2)*3"), 9);
	CHECK_NEAR(F(L"10-2-3"), 5);
	CHECK_NEAR(F(L"6/2*3"), 9);
	CHECK_NEAR(F(L"-2^2"), -4);
	CHECK_NEAR(F(L"2^3^2"), 512);
	CHECK_NEAR(F(L"2^-1"), 0.5);
	CHECK_NEAR(F(L"2**3"), 8);
	CHECK_NEAR(F(L"--2"), 2);
	CHECK_NEAR(F(L"+2"), 2);
	CHECK_NEAR(F(L"2*-3"), -6);
	CHECK_NEAR(F(L"[1+1]*2"), 4);
	CHECK_STR(P(L"-x^2"), L"-x^2");
	CHECK_STR(P(L"2^3^2"), L"2^3^2");
	CHECK_STR(P(L"(2^3)^2"), L"(2^3)^2");
	CHECK_STR(P(L"(-x)^2"), L"(-x)^2");
}

TEST(ImplicitProducts) {
	CHECK_NEAR(F(L"2x", 3), 6);
	CHECK_NEAR(F(L"2(x+1)", 3), 8);
	CHECK_NEAR(F(L"(x+1)(x-1)", 3), 8);
	CHECK_NEAR(F(L"3x^2", 2), 12);
	CHECK_NEAR(F(L"-2x", 3), -6);
	CHECK_NEAR(F(L"x sin x", 1), std::sin(1.0));
	CHECK_NEAR(F(L"2 sin x cos x", 0.3), 2 * std::sin(0.3) * std::cos(0.3));
	CHECK_NEAR(F(L"sin 2x", 0.3), std::sin(0.6));
	CHECK_NEAR(F(L"sin x^2", 0.3), std::sin(0.09));
	CHECK_NEAR(F(L"sin x/2", 0.3), std::sin(0.3) / 2);
	CHECK_NEAR(F(L"sin -x", 0.3), std::sin(-0.3));
	CHECK_NEAR(F(L"sin(x)^2", 0.3), std::sin(0.3) * std::sin(0.3));
	CHECK_NEAR(F(L"sin^2 x", 0.3), std::sin(0.3) * std::sin(0.3));
	CHECK_NEAR(F(L"sin\x00B2x", 0.3), std::sin(0.3) * std::sin(0.3));
	CHECK_NEAR(F(L"sin^-1 x", 0.3), std::asin(0.3));
	CHECK_NEAR(F(L"ln x + 1", 1), 1);
	CHECK_NEAR(F(L"2 pi x", 1), 2 * Pi);
	CHECK_NEAR(F(L"e^2x", 1), std::exp(2.0));			// (e^2) x, as calculators read it
	CHECK_NEAR(F(L"a x + b", 5), 13);
	CHECK_STR(P(L"2 sin x cos x"), L"2*sin(x)*cos(x)");
	CHECK_STR(P(L"sin x cos x"), L"sin(x)*cos(x)");
	// A number can't follow implicitly.
	CHECK_INT(ParseError(L"3 4").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"x 2").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"x 2").pos, 2);
}

TEST(Names) {
	CHECK_STR(P(L"pix"), L"\x03C0*x");
	CHECK_STR(P(L"2\x03C0x"), L"2*\x03C0*x");
	CHECK_STR(P(L"sinx"), L"sin(x)");
	CHECK_STR(P(L"asinx"), L"asin(x)");
	CHECK_STR(P(L"arcsin x"), L"asin(x)");
	CHECK_STR(P(L"expx"), L"exp(x)");
	CHECK_STR(P(L"xsinx"), L"x*sin(x)");
	CHECK_STR(P(L"xy"), L"x*y");
	CHECK_STR(P(L"theta"), FplTheta);
	CHECK_STR(P(L"ax"), L"a*x");
	CHECK_STR(P(L"sgn x"), L"sign(x)");
	// Unknown names report their first unknown letter, where it is.
	std::vector<std::wstring> none;
	auto e = ParseError(L"2ax", &none);
	CHECK_INT(e.code, FplErrorCode::UnknownName);
	CHECK_STR(e.name, L"a");
	CHECK_INT(e.pos, 1);
	CHECK_INT(e.len, 1);
	e = ParseError(L"x + foo2", &none);
	CHECK_INT(e.code, FplErrorCode::UnknownName);
	CHECK_STR(e.name, L"foo2");
	CHECK_INT(e.pos, 4);
	CHECK_STR(e.Message(), L"Unknown name 'foo2'");
	// A parameter named with a digit.
	std::vector<std::wstring> k1 = { L"k1" };
	CHECK_STR(P(L"k1 x", &k1), L"k1*x");
}

TEST(Absolute) {
	CHECK_NEAR(F(L"|x-3|", 1), 2);
	CHECK_NEAR(F(L"||x|-1|", -3), 2);
	CHECK_NEAR(F(L"2|x|", -2), 4);
	CHECK_NEAR(F(L"|x|+|x-1|", 3), 5);
	CHECK_NEAR(F(L"|(|x|)|", -2), 2);
	auto e = ParseError(L"|x");
	CHECK_INT(e.code, FplErrorCode::MissingClose);
	CHECK_STR(e.name, L"|");
}

TEST(Postfix) {
	CHECK_NEAR(F(L"5!"), 120);
	CHECK_NEAR(F(L"x!", 3), 6);
	CHECK_NEAR(F(L"2x!", 3), 12);
	CHECK_NEAR(F(L"x\x00B2", 3), 9);
	CHECK_NEAR(F(L"x\x00B3", 2), 8);
	CHECK_NEAR(F(L"\x221Ax", 4), 2);
	CHECK_NEAR(F(L"\x221A(x+5)", 4), 3);
	CHECK_NEAR(F(L"2\x221Ax", 4), 4);
	CHECK_NEAR(F(L"2\x00D7" L"3"), 6);
	CHECK_NEAR(F(L"2\x00B7" L"3"), 6);
	CHECK_NEAR(F(L"6\x00F7" L"2"), 3);
	CHECK_NEAR(F(L"5\x2212" L"2"), 3);
}

TEST(Piecewise) {
	CHECK_NEAR(F(L"{x<0: -x, x}", -2), 2);
	CHECK_NEAR(F(L"{x<0: -x, x}", 2), 2);
	CHECK_NEAR(F(L"{x<0: -1, x>0: 1}", 3), 1);
	CHECK(std::isnan(F(L"{x<0: -1, x>0: 1}", 0)));
	CHECK_NEAR(F(L"x^2 {0<x<2}", 1), 1);
	CHECK(std::isnan(F(L"x^2 {0<x<2}", 3)));
	CHECK(std::isnan(F(L"x^2 {0<x<2}", 0)));
	CHECK_NEAR(F(L"{0<=x<=2: 5, 7}", 2), 5);
	CHECK_NEAR(F(L"{x=1: 5, 7}", 1), 5);
	CHECK_NEAR(F(L"{x \x2264 1: 5, 7}", 1), 5);
	CHECK_NEAR(F(L"{x >= 1: 5, 7}", 1), 5);
	CHECK_STR(P(L"x^2 {0<x<2}"), L"x^2*{0 < x < 2}");
	CHECK_STR(P(L"{x<0: -x, x}"), L"{x < 0: -x, x}");
	CHECK_STR(P(L"{x<0: 1, x>0: 1}"), L"{x < 0: 1, x > 0: 1}");
	// Nested: the inner one's condition decides inside the outer's branch.
	CHECK_NEAR(F(L"{x<0: {x<-1: 1, 2}, 3}", -0.5), 2);
	CHECK_NEAR(F(L"{x<0: {x<-1: 1, 2}, 3}", -2), 1);
	CHECK_NEAR(F(L"{x<0: {x<-1: 1, 2}, 3}", 2), 3);
	auto e = ParseError(L"{1, x<0: 2}");
	CHECK_INT(e.code, FplErrorCode::MisplacedCondition);
	CHECK_INT(e.pos, 1);
	CHECK_INT(ParseError(L"{x: 2}").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"{x<0: 2").code, FplErrorCode::MissingClose);
}

TEST(Errors) {
	CHECK_INT(ParseError(L"").code, FplErrorCode::Empty);
	CHECK_INT(ParseError(L"   ").code, FplErrorCode::Empty);
	CHECK_INT(ParseError(L"2 +").code, FplErrorCode::UnexpectedEnd);
	auto e = ParseError(L"(2");
	CHECK_INT(e.code, FplErrorCode::MissingClose);
	CHECK_STR(e.name, L")");
	CHECK_INT(e.pos, 2);
	e = ParseError(L"(2]");
	CHECK_INT(e.code, FplErrorCode::UnexpectedToken);
	CHECK_INT(e.pos, 2);
	e = ParseError(L"2 + sin");
	CHECK_INT(e.code, FplErrorCode::MissingArgument);
	CHECK_STR(e.name, L"sin");
	CHECK_INT(e.pos, 4);
	e = ParseError(L"atan2(1)");
	CHECK_INT(e.code, FplErrorCode::ArgCount);
	CHECK_STR(e.name, L"atan2");
	CHECK_INT(e.len, 8);
	CHECK_INT(ParseError(L"sin(1, 2)").code, FplErrorCode::ArgCount);
	e = ParseError(L"2 # 3");
	CHECK_INT(e.code, FplErrorCode::UnexpectedChar);
	CHECK_INT(e.pos, 2);
	CHECK_STR(e.name, L"#");
	CHECK_INT(ParseError(L"2 * * 3").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"y = x = 2").code, FplErrorCode::TooManyRelations);
	CHECK_INT(ParseError(L"0 < y > 1").code, FplErrorCode::TooManyRelations);
	CHECK_INT(ParseError(L"0 < a < 1").code, FplErrorCode::TooManyRelations);
	CHECK_INT(ParseError(L"0 < y < 1 < 2").code, FplErrorCode::TooManyRelations);
	CHECK_INT(ParseError(L"0 = y < 1").code, FplErrorCode::TooManyRelations);
	CHECK_INT(ParseError(L"2*(1,2)").code, FplErrorCode::MisplacedPoint);
	CHECK_INT(ParseError(L"y = (1,2)").code, FplErrorCode::MisplacedPoint);
	CHECK_INT(ParseError(L"max()").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"2)").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(ParseError(L"(x < 1)").code, FplErrorCode::UnexpectedToken);
	CHECK(!ParseError(L"max(1, 2, 3, x)"));
	CHECK(!FplError().Message().size());
	CHECK(ParseError(L"2 +").Message().size() > 0);
}

TEST(Classify) {
	FplRel rel;
	CHECK_INT(KindOf(L"x^2"), FplKind::Function);
	CHECK_INT(KindOf(L"5"), FplKind::Function);
	CHECK_INT(KindOf(L"y = x^2", &rel), FplKind::Function);
	CHECK_INT(rel, FplRel::Eq);
	CHECK_NEAR(F(L"y = x^2", 3), 9);
	CHECK_INT(KindOf(L"x^2 > y", &rel), FplKind::Function);
	CHECK_INT(rel, FplRel::Lt);
	CHECK_INT(KindOf(L"y \x2265 x", &rel), FplKind::Function);
	CHECK_INT(rel, FplRel::Ge);
	CHECK_INT(KindOf(L"x = y^2"), FplKind::FunctionOfY);
	CHECK_NEAR(F(L"x = y^2", 0, 3), 9);
	CHECK_INT(KindOf(L"x = 3"), FplKind::FunctionOfY);
	CHECK_INT(KindOf(L"3 = y"), FplKind::Function);
	CHECK_INT(KindOf(L"r = 1 + cos(3\x03B8)"), FplKind::Polar);
	CHECK_NEAR(F(L"r = 1 + cos(3 theta)", 0.2), 1 + std::cos(0.6));
	CHECK_INT(KindOf(L"r < 2", &rel), FplKind::Polar);
	CHECK_INT(rel, FplRel::Lt);
	CHECK_INT(KindOf(L"(cos t, sin t)"), FplKind::Parametric);
	CHECK_NEAR(F(L"(cos t, sin t)", 0.4), std::cos(0.4));
	CHECK_NEAR(G(L"(cos t, sin t)", 0.4), std::sin(0.4));
	CHECK_INT(KindOf(L"(1, a)"), FplKind::Point);
	CHECK_NEAR(G(L"(1, a)"), 2);
	CHECK_INT(KindOf(L"x^2 + y^2 = 4", &rel), FplKind::Implicit);
	CHECK_NEAR(F(L"x^2 + y^2 = 4", 2, 0), 0);
	CHECK_NEAR(F(L"x^2 + y^2 = 4", 1, 1), -2);
	CHECK_INT(KindOf(L"x^2 + y^2 < 4", &rel), FplKind::Implicit);
	CHECK_INT(rel, FplRel::Lt);
	CHECK_INT(KindOf(L"y = y^2 + x"), FplKind::Implicit);
	CHECK_INT(KindOf(L"0 = sin x"), FplKind::Implicit);
	// theta and r in an implicit equation are the point's.
	CHECK_INT(KindOf(L"\x03B8 = 1"), FplKind::Implicit);
	CHECK_NEAR(F(L"\x03B8 = 1", std::cos(1.0), std::sin(1.0)), 0);
	CHECK_INT(KindOf(L"r = 2 + r/2"), FplKind::Implicit);
	CHECK_NEAR(F(L"r = 2 + r/2", 3, 4), 0.5);
	// Definitions.
	auto e = FplBuild(L"c = 2a", g_Params);
	CHECK(!e.error);
	CHECK_INT(e.kind, FplKind::Definition);
	CHECK_STR(e.name, L"c");
	CHECK_NEAR(F(L"c = 2a"), 4);
	CHECK_INT(KindOf(L"a = 5"), FplKind::Definition);
	std::vector<std::wstring> none;
	CHECK_STR(FplBuild(L"ab = 1", none).name, L"ab");		// a new name of two letters
	CHECK_INT(BuildError(L"ab = 1").code, FplErrorCode::NotAPlot);	// with a and b defined it is a*b = 1
	e = FplBuild(L"xa = 2", none);		// x is known, so it isn't a new name
	CHECK_INT(e.error.code, FplErrorCode::UnknownName);
	CHECK_STR(e.error.name, L"a");
	CHECK_INT(BuildError(L"c = x").code, FplErrorCode::VariableNotAllowed);
	// What isn't a plot.
	e = FplBuild(L"y = 2t", g_Params);
	CHECK_INT(e.error.code, FplErrorCode::VariableNotAllowed);
	CHECK_STR(e.error.name, L"t");
	CHECK_INT(e.error.pos, 5);
	CHECK_INT(BuildError(L"sin \x03B8").code, FplErrorCode::NotAPlot);
	CHECK_INT(BuildError(L"x + y").code, FplErrorCode::NotAPlot);
	CHECK_INT(BuildError(L"2 = 3").code, FplErrorCode::NotAPlot);
	CHECK_INT(BuildError(L"(1, 2, 3, 4)").code, FplErrorCode::BadPoint);
	e = FplBuild(L"(x, t)", g_Params);
	CHECK_INT(e.error.code, FplErrorCode::VariableNotAllowed);
	CHECK_STR(e.error.name, L"x");
	CHECK_INT(BuildError(L"y = r").code, FplErrorCode::None);	// y - hypot(x, y) = 0, implicit
	CHECK_INT(KindOf(L"y = r"), FplKind::Implicit);
}

TEST(Functions) {
	CHECK_NEAR(F(L"sec 0"), 1);
	CHECK_NEAR(F(L"csc(pi/2)"), 1);
	CHECK_NEAR(F(L"cot(pi/4)"), 1);
	CHECK_NEAR(F(L"atan2(1, 1)"), Pi / 4);
	CHECK_NEAR(F(L"log 100"), 2);
	CHECK_NEAR(F(L"log2 8"), 3);
	CHECK_NEAR(F(L"ln e"), 1);
	CHECK_NEAR(F(L"exp 1"), std::numbers::e);
	CHECK_NEAR(F(L"sqrt 9"), 3);
	CHECK_NEAR(F(L"cbrt(-8)"), -2);
	CHECK_NEAR(F(L"root(3, -27)"), -3);
	CHECK(std::isnan(F(L"root(2, -4)")));
	CHECK_NEAR(F(L"abs(-3)"), 3);
	CHECK_NEAR(F(L"sign(-2)"), -1);
	CHECK_NEAR(F(L"sign 0"), 0);
	CHECK_NEAR(F(L"floor(-1.5)"), -2);
	CHECK_NEAR(F(L"ceil(-1.5)"), -1);
	CHECK_NEAR(F(L"round 2.5"), 3);
	CHECK_NEAR(F(L"min(3, 1, 2)"), 1);
	CHECK_NEAR(F(L"max(3, 1, 2)"), 3);
	CHECK_NEAR(F(L"max(x, 1)", 5), 5);
	CHECK(std::isnan(F(L"max(x, 1)", NaN)));
	CHECK_NEAR(F(L"mod(7, 3)"), 1);
	CHECK_NEAR(F(L"mod(-1, 3)"), 2);
	CHECK_NEAR(F(L"mod(1, -3)"), -2);
	CHECK(std::isnan(F(L"mod(1, 0)")));
	CHECK_NEAR(F(L"hypot(3, 4)"), 5);
	CHECK_NEAR(F(L"gamma 5"), 24);
	CHECK_NEAR(F(L"erf 0"), 0);
	CHECK_NEAR_TOL(F(L"digamma 1"), -0.57721566490153286, 1e-12);
	CHECK_NEAR_TOL(F(L"digamma 0.5"), -1.9635100260214235, 1e-12);
	CHECK_NEAR_TOL(F(L"digamma(-0.5)"), 0.036489973978576520, 1e-11);
	CHECK(std::isnan(F(L"digamma(-2)")));
	CHECK_NEAR(F(L"sinh 1"), std::sinh(1.0));
	CHECK_NEAR(F(L"acosh 2"), std::acosh(2.0));
	CHECK(std::isnan(F(L"sqrt(x)", -1)));
	CHECK(std::isinf(F(L"1/x", 0)));
}

TEST(Powers) {
	CHECK_NEAR(FplPow(-8, 1.0 / 3), -2);
	CHECK_NEAR(FplPow(-8, 2.0 / 3), 4);
	CHECK_NEAR(FplPow(-32, 0.2), -2);
	CHECK(std::isnan(FplPow(-4, 0.5)));
	CHECK(std::isnan(FplPow(-4, 0.25)));
	CHECK_NEAR(FplPow(-2, 3), -8);
	CHECK_NEAR(F(L"x^(1/3)", -8), -2);
	CHECK_NEAR(F(L"x^(2/3)", -8), 4);
	CHECK(std::isnan(F(L"x^(1/2)", -4)));
	CHECK(std::isnan(F(L"(-8)^0.5")));
	CHECK_NEAR(F(L"x^a", -3), 9);			// a variable exponent, an integer
	CHECK_NEAR(F(L"x^(1/b)", -27), -3);		// and a rational one
	// Constant integer powers multiply, the same as pow.
	CHECK_NEAR(F(L"x^5", 1.1), std::pow(1.1, 5));
	CHECK_NEAR(F(L"x^-3", 2), 0.125);
	CHECK_NEAR(F(L"x^0", 7), 1);
	CHECK_NEAR(F(L"x^64", 1.01), std::pow(1.01, 64));
	CHECK(std::isinf(F(L"x^-1", 0)));
}

TEST(Degrees) {
	CHECK_NEAR(F(L"sin x", 90, 0, true), 1);
	CHECK_NEAR(F(L"cos(x)", 60, 0, true), 0.5);
	CHECK_NEAR(F(L"asin 1", 0, 0, true), 90);
	CHECK_NEAR(F(L"atan2(1, 1)", 0, 0, true), 45);
	CHECK_NEAR(F(L"sinh 1", 0, 0, true), std::sinh(1.0));	// not an angle
	CHECK_NEAR(F(L"sin x", Pi / 2), 1);
}

// The stack machine against the tree, over a set of expressions and points.
TEST(CompiledMatchesTree) {
	const wchar_t* exprs[] = {
		L"x", L"2x + 1", L"x^2 - 3x + 2", L"sin x cos 2x", L"exp(-x^2/4) sin(3x)", L"|x| - |x - 1|",
		L"{x < 0: x^2, x < 1: x, 2 - x}", L"x^(1/3)", L"x^(2/3)", L"x^x", L"x!", L"gamma(x)", L"max(x, x^2, -x)",
		L"min(sin x, cos x)", L"mod(x, 1.5)", L"floor(x) + ceil(x)/2", L"ln|x|", L"tan x", L"sec x + csc x + cot x",
		L"atan2(x, 1 - x)", L"hypot(x, a)", L"a x^2 + b x", L"root(b, x)", L"sqrt(1 - x^2)", L"x {x > 0}",
		L"2^x - x^2", L"erf(x) digamma(x + 3)", L"asinh x + acosh(x + 3) + atanh(x/4)", L"-x^2 + -(-x)",
		L"x/(1+x^2)/2", L"(x+1)^-2", L"1/x", L"0 x", L"{x = 0: 1, sin x / x}",
	};
	const double xs[] = { -3.5, -2, -1, -0.5, 0, 0.25, 1, 1.5, 2, 3.7 };
	for (auto text : exprs) {
		FplParseOptions o;
		o.parameters = &g_Params;
		auto r = FplParse(text, o);
		CHECK(!r.error);
		if (r.error)
			continue;
		auto def = FplClassify(r.ast, r.root);
		CHECK(!def.error);
		auto prog = FplCompile(r.ast, def.expr[0], g_Params);
		CHECK(!prog.Empty());
		for (double x : xs) {
			double s[16];
			Slots(s, x, 0);
			double a = prog.Eval(s), b = Tree(r.ast, def.expr[0], x);
			if (!Near(a, b, 1e-12))
				wprintf(L"        %s at %g: %.17g vs %.17g\n", text, x, a, b);
			CHECK(Near(a, b, 1e-12));
		}
	}
}

TEST(Compiling) {
	std::vector<std::wstring> params = { L"a" };
	FplParseOptions o;
	o.parameters = &params;
	auto r = FplParse(L"2*3 + 4", o);
	auto p = FplCompile(r.ast, r.root, params);
	CHECK(p.IsConstant());
	CHECK_INT(p.Size(), 1);
	r = FplParse(L"x^2 + a sin(pi/2)", o);
	p = FplCompile(r.ast, r.root, params);
	CHECK(p.UsesSlot(FplSlotX));
	CHECK(p.UsesSlot(FplFirstParam));
	CHECK(!p.UsesSlot(FplSlotY));
	CHECK(!p.IsConstant());
	CHECK_INT(p.Size(), 6);		// x, sq, a, 1, mul, add: sin(pi/2) folded
	// A parameter the program doesn't know.
	std::vector<std::wstring> other = { L"b" };
	FplError err;
	p = FplCompile(r.ast, r.root, other, &err);
	CHECK(p.Empty());
	CHECK_INT(err.code, FplErrorCode::UnknownName);
	CHECK_STR(err.name, L"a");
	// A relation isn't compiled whole.
	r = FplParse(L"y = x", o);
	p = FplCompile(r.ast, r.root, params, &err);
	CHECK(p.Empty());
	CHECK_INT(err.code, FplErrorCode::MisplacedRelation);
	// Deeper than the machine's stack on the stack.
	std::wstring deep = L"x";
	for (int i = 0; i < 40; i++)
		deep = L"1 + (2*" + deep + L")";
	r = FplParse(deep, o);
	p = FplCompile(r.ast, r.root, params);
	double s[16] = { 1 };
	CHECK_NEAR(p.Eval(s), Tree(r.ast, r.root, 1));
	CHECK(FplProgram().Empty());
	CHECK(std::isnan(FplProgram().Eval(s)));
}

TEST(Derivatives) {
	CHECK_STR(DerivativeText(L"x^2"), L"2*x");
	CHECK_STR(DerivativeText(L"x^3"), L"3*x^2");
	CHECK_STR(DerivativeText(L"sin x"), L"cos(x)");
	CHECK_STR(DerivativeText(L"3x"), L"3");
	CHECK_STR(DerivativeText(L"exp(2x)"), L"2*exp(2*x)");
	CHECK_STR(DerivativeText(L"a x"), L"a");
	CHECK_STR(DerivativeText(L"5"), L"0");
	CHECK_STR(DerivativeText(L"x y", L"y"), L"x");
	CHECK_STR(DerivativeText(L"x^2 + y^2 - 4", L"y"), L"2*y");
	CHECK_STR(DerivativeText(L"(cos t, sin t)", L"t"), L"(-sin(t), cos(t))");
	CHECK_STR(DerivativeText(L"y = x"), L"error");
	// Against central differences.
	const wchar_t* exprs[] = {
		L"x^3 - 2x", L"sin x cos x", L"exp(-x^2)", L"ln(x^2 + 1)", L"sqrt(x^2 + 1)", L"x^x", L"atan x", L"tan x",
		L"x!", L"gamma(x)", L"|x|", L"erf x", L"cbrt(x)", L"asinh x", L"2^x", L"hypot(x, 2)", L"atan2(x, 2)",
		L"atan2(2, x)", L"max(x, x^2)", L"min(x, x^2, 0.5)", L"{x < 1: x^2, 2x}", L"mod(x, 2)", L"x^(1/3)",
		L"root(3, x)", L"log x", L"log2 x", L"acosh(x + 1)", L"atanh(x/4)", L"asin(x/4)", L"acos(x/4)", L"sec x",
		L"csc x", L"cot x", L"sinh x cosh x", L"tanh x", L"x/(1 + x^2)", L"1/x", L"(x + 1)^-2", L"a x^b", L"x sin(1/x)",
		L"e^x", L"x^2 {x > 0}",
	};
	const double xs[] = { 0.3, 0.7, 1.3, 2.1 };
	for (auto text : exprs)
		for (double x : xs) {
			double d = Derivative(text, x);
			double h = 1e-5;
			double num = (F(text, x + h) - F(text, x - h)) / (2 * h);
			bool ok = Near(d, num, 1e-5);
			if (!ok)
				wprintf(L"        d/dx %s at %g: %.12g vs %.12g (%s)\n", text, x, d, num, DerivativeText(text).c_str());
			CHECK(ok);
		}
}

TEST(Simplify) {
	auto S = [](std::wstring_view text) {
		FplParseOptions o;
		o.parameters = &g_Params;
		auto r = FplParse(text, o);
		return FplToString(r.ast, FplSimplify(r.ast, r.root));
	};
	CHECK_STR(S(L"x + 0"), L"x");
	CHECK_STR(S(L"0 + x"), L"x");
	CHECK_STR(S(L"x - 0"), L"x");
	CHECK_STR(S(L"0 - x"), L"-x");
	CHECK_STR(S(L"1*x"), L"x");
	CHECK_STR(S(L"x*1"), L"x");
	CHECK_STR(S(L"0*x + 2"), L"2");
	CHECK_STR(S(L"x/1"), L"x");
	CHECK_STR(S(L"x^1"), L"x");
	CHECK_STR(S(L"x^0"), L"1");
	CHECK_STR(S(L"--x"), L"x");
	CHECK_STR(S(L"2*3*x"), L"6*x");
	CHECK_STR(S(L"x*2"), L"2*x");
	CHECK_STR(S(L"2*(3*x)"), L"6*x");
	CHECK_STR(S(L"-1*x"), L"-x");
	CHECK_STR(S(L"x + -y"), L"x - y");
	CHECK_STR(S(L"x - -y"), L"x + y");
	CHECK_STR(S(L"(-x)*(-y)"), L"x*y");
	CHECK_STR(S(L"sin(0) + x"), L"x");
	CHECK_STR(S(L"{x < 0: 0 + x, 1*x}"), L"{x < 0: x, x}");
	// Unchanged trees come back as they were.
	FplParseOptions o;
	auto r = FplParse(L"x + y", o);
	CHECK_INT(FplSimplify(r.ast, r.root), r.root);
}

TEST(Printing) {
	// Printing and parsing again gives the same text and the same values.
	const wchar_t* exprs[] = {
		L"x - (y - 1)", L"x/(y*2)", L"x/y/2", L"x*(y/2)", L"-(x + 1)", L"-(x*y)", L"(-2)^x", L"2^-x", L"x^y^2",
		L"(x^y)^2", L"x!^2", L"(x + 1)!", L"-x!", L"sin(x)^2", L"atan2(y, x)", L"max(1, x, y)", L"{0 < x <= 1: x, -x}",
		L"y < x^2", L"(cos(t), sin(t))", L"x - -y", L"x*-y", L"1e+20*x", L"2.5e-07 + x", L"|x - 1|", L"a x + b",
	};
	for (auto text : exprs) {
		auto once = P(text);
		auto twice = P(once);
		CHECK_STR(twice, once);
		FplParseOptions o;
		o.parameters = &g_Params;
		auto r1 = FplParse(text, o);
		auto r2 = FplParse(once, o);
		if (r1.error || r2.error) {
			CHECK(false);
			continue;
		}
		int n1 = r1.ast[r1.root].op == FplOp::Cmp ? r1.ast[r1.root].kids[1] : r1.root;
		int n2 = r2.ast[r2.root].op == FplOp::Cmp ? r2.ast[r2.root].kids[1] : r2.root;
		if (r1.ast[n1].op == FplOp::Tuple)
			n1 = r1.ast[n1].kids[0], n2 = r2.ast[n2].kids[0];
		for (double x : { -1.5, 0.5, 2.0 })
			CHECK(Near(Tree(r1.ast, n1, x, 0.7), Tree(r2.ast, n2, x, 0.7)));
	}
	CHECK_STR(P(L"x - (y - 1)"), L"x - (y - 1)");
	CHECK_STR(P(L"x/(y*2)"), L"x/(y*2)");
	CHECK_STR(P(L"x/y/2"), L"x/y/2");
	CHECK_STR(P(L"-(x + 1)"), L"-(x + 1)");
	CHECK_STR(P(L"(x + 1)!"), L"(x + 1)!");
	CHECK_STR(P(L"y \x2264 x"), L"y <= x");
	CHECK_STR(P(L"|x - 1|"), L"abs(x - 1)");
}

TEST(Positions) {
	FplParseOptions o;
	o.parameters = &g_Params;
	auto r = FplParse(L"y = 2 sin(x) + 1", o);
	CHECK(!r.error);
	auto& top = r.ast[r.root];
	CHECK_INT(top.pos, 0);
	CHECK_INT(top.len, 16);
	auto& rhs = r.ast[top.kids[1]];
	CHECK_INT(rhs.pos, 4);
	CHECK_INT(rhs.len, 12);
	auto& product = r.ast[rhs.kids[0]];
	auto& call = r.ast[product.kids[1]];
	CHECK_INT(call.op, FplOp::Call);
	CHECK_INT(call.pos, 6);
	CHECK_INT(call.len, 6);
	r = FplParse(L"-x", o);
	CHECK_INT(r.ast[r.root].pos, 0);
	CHECK_INT(r.ast[r.root].len, 2);
	r = FplParse(L"x!", o);
	CHECK_INT(r.ast[r.root].len, 2);
}

TEST(Speed) {
	auto e = FplBuild(L"sin(x) exp(-x^2/4) + x^3 - a x", g_Params);
	double s[16];
	Slots(s, 0, 0);
	const int n = 2'000'000;
	double sum = 0;
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < n; i++) {
		s[FplSlotX] = -5 + 10.0 * i / n;
		sum += e.f.Eval(s);
	}
	auto t1 = std::chrono::steady_clock::now();
	double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
	wprintf(L"        %.1f ns per evaluation (%zu instructions; sum %g)\n", ns, e.f.Size(), sum);
	CHECK(std::isfinite(sum));
}

// ---- The 2D model ------------------------------------------------------------

static FplViewport View(double xMin = -10, double xMax = 10, double yMin = -10, double yMax = 10, double w = 800,
	double h = 800) {
	FplViewport v;
	v.xMin = xMin;
	v.xMax = xMax;
	v.yMin = yMin;
	v.yMax = yMax;
	v.width = w;
	v.height = h;
	return v;
}

static std::vector<double> ParamSlots() {
	std::vector<double> s(FplFirstParam + 2, 0);
	s[FplFirstParam] = g_ParamValues[0];
	s[FplFirstParam + 1] = g_ParamValues[1];
	return s;
}

static FplGeometry Geometry(std::wstring_view text, const FplViewport& v, const FplSampleOptions& o = {}) {
	auto e = FplBuild(text, g_Params);
	return FplSample(e, ParamSlots(), v, o);
}

// f(x) of a built expression (or its derivative), for the analysis.
static std::function<double(double)> Fn(std::wstring_view text, bool derivative = false) {
	auto e = std::make_shared<FplExpression>(FplBuild(text, g_Params));
	return [e, derivative](double x) {
		double s[16];
		Slots(s, x, 0);
		return derivative ? e->df.Eval(s) : e->f.Eval(s);
	};
}

static double SignedArea(const FplPolygon& p) {
	double a = 0;
	for (size_t i = 0; i < p.size(); i++) {
		auto& q = p[i];
		auto& r = p[(i + 1) % p.size()];
		a += q.x * r.y - r.x * q.y;
	}
	return a / 2;
}

static double Dist2(FplPoint a, FplPoint b) {
	return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

static bool InFarBox(const FplGeometry& g, const FplViewport& v) {
	for (auto& line : g.lines)
		for (auto& p : line)
			if (!(p.x >= -v.width - 1e-9 && p.x <= 2 * v.width + 1e-9 && p.y >= -v.height - 1e-9 &&
				p.y <= 2 * v.height + 1e-9))
				return false;
	return true;
}

static size_t Points(const std::vector<FplPolyline>& lines) {
	size_t n = 0;
	for (auto& l : lines)
		n += l.size();
	return n;
}

TEST(Viewport) {
	auto v = View(-10, 10, -5, 5, 800, 400);
	CHECK(v.Valid());
	CHECK_NEAR(v.ScaleX(), 40);
	CHECK_NEAR(v.ToScreenX(0), 400);
	CHECK_NEAR(v.ToScreenY(5), 0);
	CHECK_NEAR(v.ToScreenY(-5), 400);
	auto p = v.ToPlane(v.ToScreen({ 3.25, -1.5 }));
	CHECK_NEAR(p.x, 3.25);
	CHECK_NEAR(p.y, -1.5);

	// zooming keeps the point under the mouse
	FplPoint mouse = { 600, 100 };
	auto before = v.ToPlane(mouse);
	v.Zoom(2, 2, mouse);
	auto after = v.ToPlane(mouse);
	CHECK_NEAR(before.x, after.x);
	CHECK_NEAR(before.y, after.y);
	CHECK_NEAR(v.xMax - v.xMin, 10);
	CHECK_NEAR(v.yMax - v.yMin, 5);
	v.Zoom(0.5, 1, mouse);
	CHECK_NEAR(v.xMax - v.xMin, 20);
	CHECK_NEAR(v.yMax - v.yMin, 5);

	// panning: what was under the mouse follows it
	v = View(-10, 10, -5, 5, 800, 400);
	auto under = v.ToPlane({ 100, 100 });
	v.Pan(50, -20);
	auto moved = v.ToPlane({ 150, 80 });
	CHECK_NEAR(under.x, moved.x);
	CHECK_NEAR(under.y, moved.y);

	// resizing keeps the center and the scales
	v = View(-10, 10, -5, 5, 800, 400);
	v.Resize(400, 800);
	CHECK_NEAR(v.ScaleX(), 40);
	CHECK_NEAR(v.ScaleY(), 40);
	CHECK_NEAR(v.xMin, -5);
	CHECK_NEAR(v.yMax, 10);

	v = View(-10, 10, -10, 10, 800, 400);
	v.EqualScale();
	CHECK_NEAR(v.ScaleX(), v.ScaleY());
	CHECK_NEAR(v.yMax - v.yMin, 20);
	CHECK_NEAR(v.xMax - v.xMin, 40);

	// zooming in forever stops while the ends can still be told apart
	v = View(1e6, 1e6 + 1, -1, 1);
	for (int i = 0; i < 200; i++)
		v.Zoom(10, 10, { 400, 400 });
	CHECK(v.xMax > v.xMin);
	CHECK(v.Valid());
	CHECK(!View(1, 1).Valid());
}

TEST(Ticks) {
	auto t = FplMakeTicks(0, 10, 500, 50);
	CHECK_NEAR(t.step, 1);
	CHECK_INT(t.major.size(), 11);
	CHECK_INT(t.minor.size(), 40);
	CHECK_STR(FplFormatTick(3, t), L"3");

	t = FplMakeTicks(-1, 1, 400, 80);
	CHECK_NEAR(t.step, 0.5);
	CHECK_INT(t.major.size(), 5);
	CHECK_STR(FplFormatTick(-0.5, t), L"\x2212" L"0.5");
	CHECK_STR(FplFormatTick(0, t), L"0");
	CHECK_STR(FplFormatTick(1, t), L"1.0");

	// tenths are exact (3 / 10, not 3 * 0.1)
	t = FplMakeTicks(0, 1, 500, 50);
	CHECK_NEAR(t.step, 0.1);
	CHECK(t.major[3] == 0.3);
	CHECK_STR(FplFormatTick(t.major[3], t), L"0.3");
	CHECK_NEAR(t.minor.front(), 0.02);

	t = FplMakeTicks(0, 20, 500, 50);
	CHECK_NEAR(t.step, 2);
	CHECK_NEAR(t.minor.front(), 0.5);

	// pi
	t = FplMakeTicks(-2 * Pi, 2 * Pi, 800, 80, true);
	CHECK(t.pi);
	CHECK_NEAR(t.step, Pi / 2);
	CHECK_INT(t.major.size(), 9);
	const wchar_t* labels[] = { L"\x2212" L"2\x03C0", L"\x2212" L"3\x03C0/2", L"\x2212\x03C0", L"\x2212\x03C0/2", L"0",
		L"\x03C0/2", L"\x03C0", L"3\x03C0/2", L"2\x03C0" };
	for (size_t i = 0; i < t.major.size() && i < 9; i++)
		CHECK_STR(FplFormatTick(t.major[i], t), labels[i]);
	CHECK_NEAR(t.minor.front(), -7 * Pi / 4);
	t = FplMakeTicks(0, 2 * Pi, 600, 50, true);
	CHECK_NEAR(t.step, Pi / 6);
	CHECK_STR(FplFormatTick(t.major[3], t), L"\x03C0/2");
	CHECK_STR(FplFormatTick(t.major[4], t), L"2\x03C0/3");
	t = FplMakeTicks(0, 100 * Pi, 500, 50, true);
	CHECK_NEAR(t.step, 10 * Pi);
	CHECK_STR(FplFormatTick(t.major[2], t), L"20\x03C0");
	// too fine for pi: decimals
	t = FplMakeTicks(0, 0.1, 500, 50, true);
	CHECK(!t.pi);
	CHECK_NEAR(t.step, 0.01);

	// large and small
	t = FplMakeTicks(0, 1e8, 500, 50);
	CHECK_NEAR(t.step, 1e7);
	CHECK_STR(FplFormatTick(1e8, t), L"10\x2078");
	CHECK_STR(FplFormatTick(2e7, t), L"2\x00D7" L"10\x2077");
	CHECK_STR(FplFormatTick(-3e7, t), L"\x2212" L"3\x00D7" L"10\x2077");
	t = FplMakeTicks(0, 1e-5, 500, 50);
	CHECK_STR(FplFormatTick(2e-6, t), L"0.000002");
	t = FplMakeTicks(0, 1e-6, 500, 50);
	CHECK_STR(FplFormatTick(2e-7, t), L"2\x00D7" L"10\x207B\x2077");
	t = FplMakeTicks(0, 5e6, 500, 100);
	CHECK_STR(FplFormatTick(2.5e6, t), L"2500000");
	// zoomed in far from zero: fixed decimals
	t = FplMakeTicks(5, 5.0001, 500, 50);
	CHECK_STR(FplFormatTick(t.major[1], t), L"5.00001");

	CHECK(FplMakeTicks(1, 1, 500, 50).major.empty());
	CHECK(FplMakeTicks(0, NaN, 500, 50).major.empty());
}

TEST(Clipping) {
	std::vector<FplPolyline> in = { { { -100, 50 }, { 200, 50 } } };
	auto out = FplClip(in, 0, 0, 100, 100);
	CHECK_INT(out.size(), 1);
	CHECK_NEAR(out[0][0].x, 0);
	CHECK_NEAR(out[0][1].x, 100);
	// out and back in: two pieces
	in = { { { 50, 50 }, { 50, 150 }, { 60, 150 }, { 60, 50 } } };
	out = FplClip(in, 0, 0, 100, 100);
	CHECK_INT(out.size(), 2);
	CHECK_NEAR(out[0][1].y, 100);
	CHECK_NEAR(out[1][0].y, 100);
	// all outside
	in = { { { -10, -10 }, { -5, -20 } } };
	CHECK(FplClip(in, 0, 0, 100, 100).empty());
	// inside stays whole
	in = { { { 1, 1 }, { 2, 2 }, { 3, 1 } } };
	out = FplClip(in, 0, 0, 100, 100);
	CHECK_INT(out.size(), 1);
	CHECK_INT(out[0].size(), 3);
}

TEST(SampleFunctions) {
	auto v = View();
	// a parabola: one piece, every chord close to the curve
	auto g = Geometry(L"x^2/4 - 3", v);
	CHECK_INT(g.lines.size(), 1);
	CHECK(InFarBox(g, v));
	double worst = 0;
	for (auto& line : g.lines) {
		for (size_t i = 0; i + 1 < line.size(); i++) {
			auto a = v.ToPlane(line[i]), b = v.ToPlane(line[i + 1]);
			double xm = (a.x + b.x) / 2;
			double ym = v.ToScreenY(xm * xm / 4 - 3);
			double chord = (v.ToScreenY(a.y) + v.ToScreenY(b.y)) / 2;
			worst = std::max(worst, std::abs(ym - chord));
		}
	}
	CHECK(worst < 0.5);
	auto first = v.ToPlane(g.lines[0].front());
	CHECK_NEAR(first.x, -10);
	CHECK(g.evals < 20000);

	// tan: 7 branches, none joined across an asymptote
	g = Geometry(L"tan x", v);
	CHECK_INT(g.lines.size(), 7);
	bool joined = false;
	for (auto& line : g.lines)
		for (size_t i = 0; i + 1 < line.size(); i++)
			if ((line[i].y < 0 && line[i + 1].y > v.height) || (line[i].y > v.height && line[i + 1].y < 0))
				joined = true;
	CHECK(!joined);
	CHECK(InFarBox(g, v));

	// steps: a piece per step, each flat
	g = Geometry(L"floor x", v);
	CHECK_INT(g.lines.size(), 20);
	bool flat = true;
	for (auto& line : g.lines)
		for (auto& p : line)
			flat = flat && std::abs(p.y - line[0].y) < 1e-9;
	CHECK(flat);
	// a step's piece reaches its jump
	auto piece = g.lines.size() > 10 ? g.lines[10] : FplPolyline{ { 0, 0 } };
	CHECK_NEAR_TOL(v.ToPlaneX(piece.front().x), 0, 1e-9);
	CHECK_NEAR_TOL(v.ToPlaneX(piece.back().x), 1, 1e-9);

	// continuous, however steep: one piece
	CHECK_INT(Geometry(L"abs x", v).lines.size(), 1);
	CHECK_INT(Geometry(L"cbrt x", v).lines.size(), 1);
	CHECK_INT(Geometry(L"atan(1000x)", v).lines.size(), 1);
	CHECK_INT(Geometry(L"sign x", v).lines.size(), 2);
	CHECK_INT(Geometry(L"1/x", v).lines.size(), 2);
	CHECK_INT(Geometry(L"1/x^2", v).lines.size(), 2);

	// where the domain stops: the ends found
	g = Geometry(L"sqrt x", v);
	CHECK_INT(g.lines.size(), 1);
	auto start = v.ToPlane(g.lines[0].front());
	CHECK_NEAR_TOL(start.x, 0, 1e-12);
	CHECK_NEAR_TOL(start.y, 0, 1e-5);
	g = Geometry(L"sqrt(25 - x^2)", v);
	CHECK_INT(g.lines.size(), 1);
	auto left = v.ToPlane(g.lines[0].front()), right = v.ToPlane(g.lines[0].back());
	CHECK_NEAR_TOL(left.x, -5, 1e-9);
	CHECK_NEAR_TOL(right.x, 5, 1e-9);
	CHECK_NEAR_TOL(left.y, 0, 1e-4);
	CHECK_NEAR_TOL(right.y, 0, 1e-4);
	// a restricted domain
	g = Geometry(L"x {0 < x < 2}", v);
	CHECK_INT(g.lines.size(), 1);
	CHECK_NEAR_TOL(v.ToPlaneX(g.lines[0].front().x), 0, 1e-9);
	CHECK_NEAR_TOL(v.ToPlaneX(g.lines[0].back().x), 2, 1e-9);
	CHECK_INT(Geometry(L"ln x", v).lines.size(), 1);
	CHECK(Geometry(L"sqrt(-1 - x^2)", v).lines.empty());

	// parameters
	g = Geometry(L"a x", v);
	auto p = v.ToPlane(g.lines[0].back());
	CHECK_NEAR(p.y, 2 * p.x);

	// x = f(y)
	g = Geometry(L"x = y^2 - 4", v);
	CHECK_INT(g.lines.size(), 1);
	auto q = v.ToPlane(g.lines[0][5]);
	CHECK_NEAR_TOL(q.x, q.y * q.y - 4, 1e-9);

	// the budget holds sin(1/x) back
	FplSampleOptions o;
	o.maxEvals = 5000;
	g = Geometry(L"sin(1/x)", v, o);
	CHECK(g.evals < 5000 + 800);
	CHECK(!g.lines.empty());
}

TEST(SampleCurves) {
	auto v = View();
	auto g = Geometry(L"(5 cos t, 5 sin t)", v);
	CHECK_INT(g.lines.size(), 1);
	auto& line = g.lines[0];
	CHECK_NEAR_TOL(line.front().x, line.back().x, 1e-9);
	CHECK_NEAR_TOL(line.front().y, line.back().y, 1e-9);
	bool round = true;
	for (auto& s : line) {
		auto p = v.ToPlane(s);
		round = round && std::abs(std::hypot(p.x, p.y) - 5) < 1e-9;
	}
	CHECK(round);

	// polar
	g = Geometry(L"r = 2 + 2cos theta", v);
	CHECK_INT(g.lines.size(), 1);
	auto p = v.ToPlane(g.lines[0].front());
	CHECK_NEAR(p.x, 4);
	CHECK_NEAR_TOL(p.y, 0, 1e-12);
	// a range of its own
	FplSampleOptions o;
	o.thetaMin = 0;
	o.thetaMax = Pi;
	g = Geometry(L"r = 3", v, o);
	p = v.ToPlane(g.lines[0].back());
	CHECK_NEAR_TOL(p.x, -3, 1e-12);

	// a curve leaving the view, grown by its size (y from -30 to 30), and coming back: clipped into three pieces
	g = Geometry(L"(t, 50 sin t)", View(-1, 7, -10, 10));
	CHECK_INT(g.lines.size(), 3);
	CHECK(InFarBox(g, View(-1, 7, -10, 10)));

	// the generic curve
	auto lines = FplSampleCurve([](double t) { return FplPoint{ t, t < 0 ? NaN : t }; }, -5, 5, 10, v);
	CHECK_INT(lines.size(), 1);
	CHECK_NEAR_TOL(v.ToPlaneX(lines[0].front().x), 0, 1e-12);
}

TEST(SampleImplicit) {
	auto v = View();
	auto g = Geometry(L"x^2 + y^2 = 25", v);
	CHECK_INT(g.lines.size(), 1);
	auto& ring = g.lines[0];
	CHECK(ring.size() > 100);
	CHECK_NEAR(ring.front().x, ring.back().x);
	CHECK_NEAR(ring.front().y, ring.back().y);
	double worst = 0;
	for (auto& s : ring) {
		auto p = v.ToPlane(s);
		worst = std::max(worst, std::abs(std::hypot(p.x, p.y) - 5));
	}
	CHECK(worst < 1e-6);

	// two loops
	g = Geometry(L"(x^2 + y^2 - 4)((x - 6)^2 + y^2 - 1) = 0", v);
	CHECK_INT(g.lines.size(), 2);

	// a hyperbola's two open branches
	g = Geometry(L"xy = 1", v);
	CHECK_INT(g.lines.size(), 2);
	for (auto& l : g.lines)
		CHECK(Dist2(l.front(), l.back()) > 100);

	// no lines along asymptotes: every point is on the curve
	g = Geometry(L"y - tan x = 0", v);
	CHECK(!g.lines.empty());
	bool onCurve = true;
	for (auto& l : g.lines) {
		for (auto& s : l) {
			auto p = v.ToPlane(s);
			onCurve = onCurve && std::abs(p.y - std::tan(p.x)) < 1e-4 * (1 + std::abs(std::tan(p.x)));
		}
	}
	CHECK(onCurve);

	// theta = 1 (atan2): the ray, not the branch cut on the negative x axis
	g = Geometry(L"theta = 1", v);
	CHECK(!g.lines.empty());
	bool ray = true;
	for (auto& l : g.lines)
		for (auto& s : l)
			ray = ray && v.ToPlaneX(s.x) > -0.1;
	CHECK(ray);

	// the saddle of x^2 = y^2: points on the diagonals
	g = Geometry(L"x^2 = y^2", v);
	CHECK(!g.lines.empty());
	bool diagonal = true;
	for (auto& l : g.lines)
		for (auto& s : l) {
			auto p = v.ToPlane(s);
			diagonal = diagonal && std::abs(std::abs(p.x) - std::abs(p.y)) < 1e-6;
		}
	CHECK(diagonal);

	// nothing
	CHECK(Geometry(L"x^2 + y^2 = -1", v).lines.empty());
}

TEST(SampleRegions) {
	auto v = View();
	double s = v.ScaleX();
	auto g = Geometry(L"x^2 + y^2 < 25", v);
	CHECK(g.dashed);
	CHECK_INT(g.lines.size(), 1);
	double area = 0;
	bool sameWay = true;
	for (auto& p : g.fills) {
		double a = SignedArea(p);
		sameWay = sameWay && a > 0;
		area += a;
	}
	CHECK(sameWay);
	CHECK_NEAR_TOL(area, Pi * 25 * s * s, 1e-3);
	g = Geometry(L"x^2 + y^2 >= 25", v);
	CHECK(!g.dashed);
	area = 0;
	for (auto& p : g.fills)
		area += SignedArea(p);
	// everything else in the grid, which reaches a cell past the view on every side
	double cell = 8, gw = std::ceil(v.width / cell) * cell + 2 * cell, gh = std::ceil(v.height / cell) * cell + 2 * cell;
	CHECK_NEAR_TOL(area, gw * gh - Pi * 25 * s * s, 1e-3);

	// y < 0: from the line down to the far box's bottom
	g = Geometry(L"y < 0", v);
	CHECK_INT(g.fills.size(), 1);
	CHECK_NEAR_TOL(std::abs(SignedArea(g.fills[0])), v.width * 1.5 * v.height, 1e-9);
	g = Geometry(L"y > x^2", v);
	CHECK_INT(g.fills.size(), 1);
	CHECK(!g.lines.empty());
	// x > 0: to the right
	g = Geometry(L"x > 0", v);
	CHECK_INT(g.fills.size(), 1);
	CHECK_NEAR_TOL(std::abs(SignedArea(g.fills[0])), 1.5 * v.width * v.height, 1e-9);
	// polar
	g = Geometry(L"r < 3", v);
	area = 0;
	for (auto& p : g.fills)
		area += SignedArea(p);
	CHECK_NEAR_TOL(area, Pi * 9 * s * s, 2e-3);
	// equations have no fills
	CHECK(Geometry(L"x^2 + y^2 = 25", v).fills.empty());
}

TEST(SampleOthers) {
	auto v = View();
	auto g = Geometry(L"(1, a)", v);
	CHECK_INT(g.points.size(), 1);
	CHECK_NEAR(g.points[0].x, v.ToScreenX(1));
	CHECK_NEAR(g.points[0].y, v.ToScreenY(2));
	g = Geometry(L"c = 2", v);
	CHECK(g.lines.empty() && g.points.empty());
	g = Geometry(L"2 +", v);
	CHECK(g.lines.empty());
	CHECK(Geometry(L"x", View(1, 1)).lines.empty());

	// the derivative comes with the expression
	auto e = FplBuild(L"x^3", g_Params);
	double sl[16];
	Slots(sl, 2, 0);
	CHECK_NEAR(e.df.Eval(sl), 12);
	e = FplBuild(L"x = sin y", g_Params);
	Slots(sl, 0, 0);
	CHECK_NEAR(e.df.Eval(sl), 1);
	CHECK(FplBuild(L"(cos t, sin t)", g_Params).df.Empty());
}

static bool Has(const std::vector<FplFeature>& fs, FplFeatureKind kind, double x, double y = NaN,
	double tol = 1e-12) {
	for (auto& f : fs)
		if (f.kind == kind && Near(f.x, x, tol) && (std::isnan(y) || Near(f.y, y, tol)))
			return true;
	return false;
}

static int Count(const std::vector<FplFeature>& fs, FplFeatureKind kind) {
	int n = 0;
	for (auto& f : fs)
		n += f.kind == kind;
	return n;
}

TEST(Analysis) {
	std::function<double(double)> cosine = [](double x) { return std::cos(x); };
	CHECK_NEAR_TOL(FplBrent(cosine, 0, 3, 1, std::cos(3.0)), Pi / 2, 1e-15);

	using K = FplFeatureKind;
	auto fs = FplFindFeatures(Fn(L"x^3 - x"), Fn(L"x^3 - x", true), -2, 2, 401, 1e-9);
	CHECK_INT(Count(fs, K::Root), 3);
	CHECK(Has(fs, K::Root, -1));
	CHECK(Has(fs, K::Root, 0));
	CHECK(Has(fs, K::Root, 1));
	double c = 1 / std::sqrt(3.0);
	CHECK(Has(fs, K::Maximum, -c, c - c * c * c));
	CHECK(Has(fs, K::Minimum, c, c * c * c - c));
	CHECK(Has(fs, K::YIntercept, 0, 0));
	CHECK_INT(fs.size(), 6);
	for (size_t i = 1; i < fs.size(); i++)
		CHECK(fs[i - 1].x <= fs[i].x);
	// by differences
	auto fd = FplFindFeatures(Fn(L"x^3 - x"), nullptr, -2, 2, 401, 1e-9);
	CHECK_INT(fd.size(), 6);
	CHECK(Has(fd, K::Maximum, -c, NaN, 1e-8));

	// asymptotes and steps are not roots
	fs = FplFindFeatures(Fn(L"tan x"), Fn(L"tan x", true), -3, 3, 300, 1e-9);
	CHECK_INT(Count(fs, K::Root), 1);
	CHECK(Has(fs, K::Root, 0));
	fs = FplFindFeatures(Fn(L"1/x"), Fn(L"1/x", true), -3, 3, 301, 1e-9);
	CHECK(fs.empty());
	fs = FplFindFeatures(Fn(L"floor x - 0.5"), Fn(L"floor x - 0.5", true), -3, 3, 301, 1e-9);
	CHECK_INT(Count(fs, K::Root), 0);
	// a pole is not an extremum
	fs = FplFindFeatures(Fn(L"1/x^2"), Fn(L"1/x^2", true), -3, 3, 301, 1e-9);
	CHECK(fs.empty());
	// a corner is
	fs = FplFindFeatures(Fn(L"abs(x - 1/3)"), Fn(L"abs(x - 1/3)", true), -3, 3, 300, 1e-9);
	CHECK(Has(fs, K::Minimum, 1.0 / 3, 0, 1e-12));
	CHECK(Has(fs, K::Root, 1.0 / 3, 0, 1e-12));
	// touching zero
	fs = FplFindFeatures(Fn(L"(x - 1/3)^2"), Fn(L"(x - 1/3)^2", true), -3, 3, 300, 1e-9);
	CHECK_INT(Count(fs, K::Root), 1);
	CHECK(Has(fs, K::Root, 1.0 / 3, 0, 1e-12));
	CHECK(Has(fs, K::Minimum, 1.0 / 3, NaN, 1e-12));
	// sin over a few periods
	fs = FplFindFeatures(Fn(L"sin x"), Fn(L"sin x", true), -10, 10, 400, 1e-9);
	CHECK_INT(Count(fs, K::Root), 7);
	CHECK_INT(Count(fs, K::Maximum), 3);
	CHECK_INT(Count(fs, K::Minimum), 3);
	CHECK(Has(fs, K::Root, -3 * Pi, 0, 1e-14));
	CHECK(Has(fs, K::Maximum, Pi / 2, 1, 1e-12));
	// undefined pieces
	fs = FplFindFeatures(Fn(L"sqrt(x) - 1"), Fn(L"sqrt(x) - 1", true), -3, 3, 300, 1e-9);
	CHECK_INT(fs.size(), 2);
	CHECK(Has(fs, K::YIntercept, 0, -1));
	CHECK(Has(fs, K::Root, 1));

	// intersections
	auto is = FplFindIntersections(Fn(L"sin x"), Fn(L"cos x"), 0, 2 * Pi, 300, 1e-9);
	CHECK_INT(is.size(), 2);
	CHECK(Has(is, K::Intersection, Pi / 4, std::sqrt(0.5), 1e-14));
	CHECK(Has(is, K::Intersection, 5 * Pi / 4, -std::sqrt(0.5), 1e-14));
	// tangent
	is = FplFindIntersections(Fn(L"x^2"), Fn(L"2x - 1"), -3, 3, 301, 1e-9);
	CHECK_INT(is.size(), 1);
	CHECK(Has(is, K::Intersection, 1, 1, 1e-7));
	// parallel
	CHECK(FplFindIntersections(Fn(L"x"), Fn(L"x + 1"), -3, 3, 300, 1e-9).empty());
}

TEST(SampleSpeed) {
	auto v = View(-10, 10, -10, 10, 1600, 1000);
	auto e = FplBuild(L"sin(x) exp(-x^2/40) + x/5", g_Params);
	auto t0 = std::chrono::steady_clock::now();
	auto g = FplSample(e, ParamSlots(), v);
	auto t1 = std::chrono::steady_clock::now();
	e = FplBuild(L"sin(x^2 + y^2) = 0.5", g_Params);
	auto g2 = FplSample(e, ParamSlots(), v);
	auto t2 = std::chrono::steady_clock::now();
	wprintf(L"        function: %.2f ms (%zu evaluations, %zu points); implicit: %.2f ms (%zu evaluations, %zu lines)\n",
		std::chrono::duration<double, std::milli>(t1 - t0).count(), g.evals, Points(g.lines),
		std::chrono::duration<double, std::milli>(t2 - t1).count(), g2.evals, g2.lines.size());
	CHECK(!g.lines.empty());
	CHECK(!g2.lines.empty());
	// a surface: its grid, heat map and contours
	FplSampleOptions o;
	o.heatmap = true;
	e = FplBuild(L"z = sin x + cos y", g_Params);
	auto t3 = std::chrono::steady_clock::now();
	auto g3 = FplSample(e, ParamSlots(), v, o);
	auto t4 = std::chrono::steady_clock::now();
	size_t contourLines = 0;
	for (auto& c : g3.contours)
		contourLines += c.lines.size();
	wprintf(L"        surface: %.2f ms (%zu evaluations, %zu levels, %zu lines)\n",
		std::chrono::duration<double, std::milli>(t4 - t3).count(), g3.evals, g3.contours.size(), contourLines);
	CHECK(!g3.contours.empty());
	// an implicit surface in 3D: a 48^3 grid
	e = FplBuild(L"sin x cos y + sin y cos z + sin z cos x = 0", g_Params);
	std::vector<double> sl3 = ParamSlots();
	auto t5 = std::chrono::steady_clock::now();
	auto mesh = FplImplicitMesh([&](double x, double y, double z) {
		sl3[FplSlotX] = x;
		sl3[FplSlotY] = y;
		sl3[FplSlotZ] = z;
		return e.f.Eval(sl3.data());
	}, FplBox3{}, 48);
	auto t6 = std::chrono::steady_clock::now();
	wprintf(L"        implicit 3D: %.2f ms (%zu vertices, %zu triangles)\n",
		std::chrono::duration<double, std::milli>(t6 - t5).count(), mesh.vertices.size(), mesh.indices.size() / 3);
	CHECK(!mesh.Empty());
}

// ---- The graphs ----------------------------------------------------------------

static FplPlot MakePlot(std::initializer_list<const wchar_t*> texts, std::initializer_list<FplParam> params = {}) {
	FplPlot plot;
	unsigned id = 1;
	for (auto t : texts) {
		FplGraph g;
		g.id = id++;
		g.text = t;
		plot.graphs.push_back(g);
	}
	plot.params = params;
	plot.Build();
	return plot;
}

static FplParam Param(const wchar_t* name, double value) {
	FplParam p;
	p.name = name;
	p.value = value;
	return p;
}

TEST(GraphDefinitions) {
	auto plot = MakePlot({ L"y = a x", L"c = 2a", L"y = c", L"d = c + 1" }, { Param(L"a", 3) });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(plot.graphs[1].definition);
	CHECK_NEAR(plot.DefinitionValue(1), 6);
	CHECK_NEAR(plot.DefinitionValue(3), 7);
	CHECK(std::isnan(plot.DefinitionValue(0)));
	CHECK_INT(plot.graphs[2].expr.kind, FplKind::Function);
	CHECK_NEAR(plot.Eval(2, 100), 6);
	CHECK_NEAR(plot.Eval(0, 2), 6);
	CHECK_INT(plot.Slots().size(), FplFirstParam + 3);

	// a definition used before it's written, and one using another after it
	plot = MakePlot({ L"y = k x", L"k = m/2", L"m = 10" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK_NEAR(plot.DefinitionValue(1), 5);
	CHECK_NEAR(plot.Eval(0, 3), 15);

	// the values change: the definitions follow
	plot = MakePlot({ L"c = 2a", L"y = c x" }, { Param(L"a", 1) });
	plot.params[0].value = 4;
	plot.UpdateValues();
	CHECK_NEAR(plot.DefinitionValue(0), 8);
	CHECK_NEAR(plot.Eval(1, 1), 8);

	// twice, or over the app's parameter
	plot = MakePlot({ L"c = 1", L"c = 2" });
	CHECK(plot.graphs[0].definition);
	CHECK_INT(plot.graphs[1].expr.error.code, FplErrorCode::Redefined);
	CHECK_STR(plot.graphs[1].expr.error.name, L"c");
	CHECK_INT(plot.graphs[1].expr.error.pos, 0);
	plot = MakePlot({ L"a = 3" }, { Param(L"a", 1) });
	CHECK_INT(plot.graphs[0].expr.error.code, FplErrorCode::Redefined);
	CHECK_STR((FplError{ FplErrorCode::Redefined, 0, 1, L"a" }.Message()), L"'a' is already defined");
	// a cycle defines nothing
	plot = MakePlot({ L"p = q", L"q = p" });
	CHECK(!plot.graphs[0].definition && !plot.graphs[1].definition);
	CHECK_INT(plot.graphs[0].expr.error.code, FplErrorCode::UnknownName);
	// an unknown name stays an error the app can offer to add
	plot = MakePlot({ L"y = 2bx" });
	CHECK_INT(plot.graphs[0].expr.error.code, FplErrorCode::UnknownName);
	CHECK_STR(plot.graphs[0].expr.error.name, L"b");
	// degrees
	plot = MakePlot({ L"y = sin x" });
	plot.degrees = true;
	plot.Build();
	CHECK_NEAR(plot.Eval(0, 90), 1);
	plot = MakePlot({});
	CHECK(plot.graphs.empty());
	CHECK_INT(plot.Slots().size(), FplFirstParam);
}

TEST(GraphSampling) {
	auto v = View();
	auto plot = MakePlot({ L"y = x", L"c = 2", L"y = 2 +", L"x^2 + y^2 < 4" });
	plot.graphs[0].flags = 0;
	plot.Sample(v);
	CHECK(!plot.graphs[0].geometry.lines.empty());
	CHECK(plot.graphs[1].geometry.lines.empty());
	CHECK(plot.graphs[2].geometry.lines.empty());
	CHECK(!plot.graphs[3].geometry.fills.empty());
	plot.graphs[0].flags = FplGraphHidden;
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.lines.empty());
	CHECK(!plot.graphs[0].Drawable());
	// a curve's own range
	plot = MakePlot({ L"(cos t, sin t)" });
	plot.graphs[0].hasRange = true;
	plot.graphs[0].tMin = 0;
	plot.graphs[0].tMax = Pi;
	plot.Sample(v);
	auto end = v.ToPlane(plot.graphs[0].geometry.lines[0].back());
	CHECK_NEAR_TOL(end.x, -1, 1e-12);
}

TEST(GraphFeatures) {
	auto v = View();
	auto plot = MakePlot({ L"y = x^2 - 1", L"y = x", L"y = 2 +", L"(3, 3)" });
	plot.Sample(v);
	plot.FindFeatures(0, v);
	auto& fs = plot.graphs[0].features;
	using K = FplFeatureKind;
	CHECK(Has(fs, K::Root, -1));
	CHECK(Has(fs, K::Root, 1));
	CHECK(Has(fs, K::Minimum, 0, -1));
	CHECK(Has(fs, K::YIntercept, 0, -1));
	CHECK(Has(fs, K::Intersection, (1 - std::sqrt(5.0)) / 2, NaN, 1e-12));
	CHECK(Has(fs, K::Intersection, (1 + std::sqrt(5.0)) / 2, NaN, 1e-12));
	CHECK_INT(fs.size(), 6);
	// what has no curve has none
	plot.FindFeatures(3, v);
	CHECK(plot.graphs[3].features.empty());
	plot.graphs[0].flags = FplGraphNoFeatures;
	plot.FindFeatures(0, v);
	CHECK(plot.graphs[0].features.empty());
	// a hidden function meets nothing
	plot.graphs[0].flags = 0;
	plot.graphs[1].flags = FplGraphHidden;
	plot.FindFeatures(0, v);
	CHECK_INT(Count(plot.graphs[0].features, K::Intersection), 0);
}

TEST(GraphTracing) {
	auto v = View();
	auto plot = MakePlot({ L"y = x^2/4", L"(3, 3)", L"x^2 + y^2 = 49" });
	plot.Sample(v);
	// near the parabola at x = 2: on it exactly
	FplPoint s = v.ToScreen({ 2, 1 });
	auto hit = plot.Nearest({ s.x + 3, s.y - 2 }, v, 10, 8);
	CHECK_INT(hit.graph, 0);
	CHECK_NEAR(hit.plane.y, hit.plane.x * hit.plane.x / 4);
	CHECK(hit.distance < 4);
	CHECK_INT(hit.feature, -1);
	// too far from everything
	CHECK(!plot.Nearest(v.ToScreen({ -6, 1 }), v, 10, 8));
	// the point
	hit = plot.Nearest(v.ToScreen({ 3, 3 }), v, 10, 8);
	CHECK_INT(hit.graph, 1);
	CHECK_NEAR(hit.plane.x, 3);
	// the circle: on its polyline
	hit = plot.Nearest(v.ToScreen({ 7.05, 0 }), v, 10, 8);
	CHECK_INT(hit.graph, 2);
	CHECK_NEAR_TOL(std::hypot(hit.plane.x, hit.plane.y), 7, 1e-3);
	// features snap
	plot.FindFeatures(0, v);
	FplPoint origin = v.ToScreen({ 0, 0 });
	hit = plot.Nearest({ origin.x + 5, origin.y - 3 }, v, 10, 8);
	CHECK_INT(hit.graph, 0);
	CHECK(hit.feature >= 0);
	CHECK_NEAR(hit.plane.x, 0);
	// following the function by the mouse's x, wherever its y is
	hit = plot.TraceAt(0, { v.ToScreenX(4), 5 }, v, 8);
	CHECK_NEAR(hit.plane.x, 4);
	CHECK_NEAR(hit.plane.y, 4);
	hit = plot.TraceAt(0, { origin.x + 4, 5 }, v, 8);
	CHECK(hit.feature >= 0);
	CHECK_NEAR(hit.plane.x, 0);
	CHECK(!plot.TraceAt(7, { 0, 0 }, v, 8));
	plot.graphs[0].flags = FplGraphNoTrace;
	CHECK_INT(plot.Nearest(v.ToScreen({ 2, 1 }), v, 10, 8).graph, -1);
}

TEST(Coordinates) {
	CHECK_STR(FplFormatCoordinate(1.23456, 0.025), L"1.235");
	CHECK_STR(FplFormatCoordinate(-1.5, 0.025), L"\x2212" L"1.5");
	CHECK_STR(FplFormatCoordinate(2, 0.025), L"2");
	CHECK_STR(FplFormatCoordinate(-0.00001, 0.025), L"0");
	CHECK_STR(FplFormatCoordinate(1.0 / 3, 1e-6), L"0.3333333");
	CHECK_STR(FplFormatCoordinate(2.5e8, 1e5), L"2.5\x00D7" L"10\x2078");
	CHECK_STR(FplFormatCoordinate(NaN, 1), L"undefined");
	CHECK_STR(FplFormatCoordinate(1234.5678, 50), L"1235");
}

// ---- User functions and derivatives ----------------------------------------------

// f of text built with the user's functions, at x.
static double UF(std::wstring_view text, const std::vector<FplUserFunction>& fns, double x,
	const std::vector<std::wstring>& params = {}, const double* values = nullptr, bool degrees = false) {
	auto e = FplBuild(text, params, degrees, &fns);
	if (e.error)
		return Failed;
	double s[16] = {};
	s[FplSlotX] = s[FplSlotT] = s[FplSlotTheta] = x;
	for (size_t i = 0; values && i < params.size(); i++)
		s[FplFirstParam + i] = values[i];
	return e.f.Eval(s);
}

static void Define(std::vector<FplUserFunction>& fns, std::wstring_view text, const std::vector<std::wstring>& params = {},
	bool degrees = false) {
	FplParseOptions o;
	o.parameters = &params;
	o.functions = &fns;
	o.degrees = degrees;
	auto d = FplParseFunction(text, o);
	if (d.isDefinition && !d.error)
		fns.push_back(std::move(d.function));
}

TEST(UserFunctionDefinitions) {
	auto d = FplParseFunction(L"f(x) = x^2");
	CHECK(d.isDefinition);
	CHECK(!d.error);
	CHECK_STR(d.function.name, L"f");
	CHECK_INT(d.function.params.size(), 1);
	CHECK_STR(FplToString(d.function.ast, d.function.body), L"x^2");
	d = FplParseFunction(L"area(w, h) = w h / 2");
	CHECK(d.isDefinition && !d.error);
	CHECK_INT(d.function.params.size(), 2);
	// not definitions
	CHECK(!FplParseFunction(L"y = x").isDefinition);
	CHECK(!FplParseFunction(L"sin(x) = 2").isDefinition);
	CHECK(!FplParseFunction(L"f(2) = 3").isDefinition);
	CHECK(!FplParseFunction(L"f(x) <= 3").isDefinition);
	std::vector<std::wstring> ab = { L"a", L"b" };
	FplParseOptions o;
	o.parameters = &ab;
	CHECK(!FplParseFunction(L"ab(x) = x", o).isDefinition);	// a times b
	// errors in it
	d = FplParseFunction(L"g(a, a) = a");
	CHECK(d.isDefinition);
	CHECK_INT(d.error.code, FplErrorCode::Redefined);
	d = FplParseFunction(L"f(x) = x +");
	CHECK_INT(d.error.code, FplErrorCode::UnexpectedEnd);
	CHECK_INT(d.error.pos, 10);
	d = FplParseFunction(L"f(x) = 2q x");
	CHECK_INT(d.error.code, FplErrorCode::UnknownName);
	CHECK_STR(d.error.name, L"q");
	CHECK_INT(d.error.pos, 8);
	d = FplParseFunction(L"f(x) = x < 2");
	CHECK_INT(d.error.code, FplErrorCode::MisplacedRelation);
}

TEST(UserFunctionCalls) {
	std::vector<FplUserFunction> fns;
	Define(fns, L"f(x) = x^2");
	Define(fns, L"g(a, b) = a - b");
	Define(fns, L"h(u) = f(u) + 1");		// using one before it
	CHECK_INT(fns.size(), 3);
	CHECK_NEAR(UF(L"y = f(2x) + 1", fns, 3), 37);
	CHECK_NEAR(UF(L"f'(x)", fns, 3), 6);
	CHECK_NEAR(UF(L"f''(x)", fns, 3), 2);
	CHECK_NEAR(UF(L"f\x2032(x)", fns, 3), 6);
	CHECK_NEAR(UF(L"g(x, 1)", fns, 3), 2);
	CHECK_NEAR(UF(L"g(1, x)", fns, 3), -2);
	CHECK_NEAR(UF(L"h(x)", fns, 3), 10);
	CHECK_NEAR(UF(L"h'(x)", fns, 3), 6);
	// as the built-in ones: without parentheses, split out of a name
	CHECK_NEAR(UF(L"f 2x", fns, 3), 36);
	CHECK_NEAR(UF(L"2fx", fns, 3), 18);
	CHECK_NEAR(UF(L"f(x) f(x)", fns, 3), 81);
	// arguments that use the parameters' names
	Define(fns, L"s(x, y) = x - 2y");
	CHECK_NEAR(UF(L"y = s(1, x)", fns, 3), -5);
	// wrong number of arguments
	auto e = FplBuild(L"g(x)", {}, false, &fns);
	CHECK_INT(e.error.code, FplErrorCode::ArgCount);
	CHECK_STR(e.error.name, L"g");
	CHECK_INT(e.error.pos, 0);
	CHECK_INT(e.error.len, 4);
	// a derivative of the first parameter only, of a point
	Define(fns, L"c(t) = (cos t, sin t)");
	e = FplBuild(L"c(t)", {}, false, &fns);
	CHECK_INT(e.kind, FplKind::Parametric);
	e = FplBuild(L"c'(t)", {}, false, &fns);
	CHECK_INT(e.kind, FplKind::Parametric);
	double sl[16] = {};
	CHECK_NEAR(e.f.Eval(sl), 0);
	CHECK_NEAR(e.g.Eval(sl), 1);
	// the app's parameters in a body
	std::vector<std::wstring> params = { L"k" };
	const double k = 5;
	std::vector<FplUserFunction> withK;
	Define(withK, L"m(x) = k x", params);
	CHECK_NEAR(UF(L"m(x) + 1", withK, 2, params, &k), 11);
	// an error points at the call
	e = FplBuild(L"y = 1 + f(x", {}, false, &fns);
	CHECK_INT(e.error.code, FplErrorCode::MissingClose);
}

TEST(DerivativeSyntax) {
	std::vector<FplUserFunction> none;
	CHECK_NEAR(UF(L"d/dx x^3", none, 2), 12);
	CHECK_NEAR(UF(L"y = d/dx (x sin x)", none, Pi / 2), 1);
	CHECK_NEAR(UF(L"d/dx sin x cos x", none, 0), 1);		// d/dx sin(x), times cos x
	CHECK_NEAR(UF(L"d/dx(x^2) + 1", none, 3), 7);
	CHECK_NEAR(UF(L"d/dx d/dx x^3", none, 2), 12);
	CHECK_NEAR(UF(L"r = d/d\x03B8 (\x03B8^2)", none, 3), 6);
	CHECK_NEAR(UF(L"r = d/dtheta (theta^2)", none, 3), 6);
	auto e = FplBuild(L"(d/dt cos t, d/dt sin t)", {});
	CHECK_INT(e.kind, FplKind::Parametric);
	double sl[16] = {};
	CHECK_NEAR(e.g.Eval(sl), 1);
	// partial: other variables are constants
	e = FplBuild(L"d/dx (x y) = 1", {});
	CHECK(!e.error);
	// mistakes
	e = FplBuild(L"y = d/dx", {});
	CHECK_INT(e.error.code, FplErrorCode::MissingArgument);
	CHECK_STR(e.error.name, L"d/dx");
	e = FplBuild(L"d/dx (1, 2)", {});
	CHECK_INT(e.error.code, FplErrorCode::ArgCount);
	// "d/dxy" isn't one: d over d x y
	e = FplBuild(L"d/dxy", {});
	CHECK_INT(e.error.code, FplErrorCode::UnknownName);
	// with a parameter d, "d/dx" is still the derivative
	std::vector<std::wstring> dp = { L"d" };
	const double dv = 7;
	CHECK_NEAR(UF(L"d/dx x^2", none, 3, dp, &dv), 6);
	CHECK_NEAR(UF(L"d/2", none, 3, dp, &dv), 3.5);
	// degrees: the derivative of sin(x degrees) is pi/180 cos(x degrees)
	CHECK_NEAR(UF(L"d/dx sin x", none, 0, {}, nullptr, true), Pi / 180);
	std::vector<FplUserFunction> deg;
	Define(deg, L"f(x) = sin x", {}, true);
	CHECK_NEAR(UF(L"f'(x)", deg, 0, {}, nullptr, true), Pi / 180);
	CHECK_NEAR(UF(L"f(90)", deg, 0, {}, nullptr, true), 1);
	// printed back
	auto r = FplParse(L"d/dx x^2");
	CHECK_STR(FplToString(r.ast, r.root), L"2*x");
}

TEST(GraphFunctions) {
	auto plot = MakePlot({ L"f(x) = x^2", L"y = f(x - 1)", L"g(t) = (cos t, sin t)", L"h(a, b) = a b",
		L"y = h(x, 2)", L"p(\x03B8) = 1 + \x03B8", L"q(y) = y^3" });
	CHECK_INT(plot.graphs[0].function, 0);
	CHECK_INT(plot.graphs[0].expr.kind, FplKind::Function);
	CHECK_NEAR(plot.Eval(0, 3), 9);
	CHECK_NEAR(plot.Eval(1, 3), 4);
	CHECK_INT(plot.graphs[2].expr.kind, FplKind::Parametric);
	CHECK_INT(plot.graphs[3].expr.kind, FplKind::Definition);
	CHECK(!plot.graphs[3].expr.error);
	CHECK(plot.graphs[3].expr.f.Empty());
	CHECK_NEAR(plot.Eval(4, 3), 6);
	CHECK_INT(plot.graphs[5].expr.kind, FplKind::Polar);
	CHECK_INT(plot.graphs[6].expr.kind, FplKind::FunctionOfY);
	CHECK_INT(plot.Functions().size(), 5);
	// features of a function's graph
	auto v = View();
	plot.Sample(v);
	plot.FindFeatures(0, v);
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Minimum, 0, 0));

	// any order, with definitions
	plot = MakePlot({ L"y = f(x)", L"f(x) = 2c x", L"c = 3", L"k = f(2)" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK_NEAR(plot.Eval(0, 1), 6);
	CHECK_NEAR(plot.DefinitionValue(3), 12);
	// twice, or over another name
	plot = MakePlot({ L"f(x) = x", L"f(x) = 2x" });
	CHECK_INT(plot.graphs[1].expr.error.code, FplErrorCode::Redefined);
	CHECK_INT(plot.graphs[1].expr.error.pos, 0);
	CHECK_INT(plot.graphs[1].expr.error.len, 1);
	plot = MakePlot({ L"a(x) = x" }, { Param(L"a", 1) });
	CHECK(!(plot.graphs[0].function >= 0));
	// a body that can't be read
	plot = MakePlot({ L"f(x) = q x" });
	CHECK_INT(plot.graphs[0].expr.error.code, FplErrorCode::UnknownName);
	CHECK_STR(plot.graphs[0].expr.error.name, L"q");
	CHECK_INT(plot.graphs[0].expr.error.pos, 7);
}

// ---- Surfaces and vector fields ------------------------------------------------------

TEST(Surfaces) {
	auto e = FplBuild(L"z = x^2 + y^2", {});
	CHECK_INT(e.kind, FplKind::Surface);
	CHECK(!e.error);
	double sl[16] = {};
	sl[FplSlotX] = 3;
	sl[FplSlotY] = 4;
	CHECK_NEAR(e.f.Eval(sl), 25);
	CHECK_INT(FplBuild(L"x y - 1 = z", {}).kind, FplKind::Surface);
	CHECK_INT(FplBuild(L"z = 2", {}).kind, FplKind::Surface);
	CHECK_INT(FplBuild(L"z < x", {}).error.code, FplErrorCode::NotAPlot);
	CHECK_INT(FplBuild(L"y = z", {}).kind, FplKind::Surface);	// z = y
	CHECK_INT(FplBuild(L"y = z x", {}).kind, FplKind::ImplicitSurface);	// a saddle, in 3D
	e = FplBuild(L"y = z t", {});
	CHECK_INT(e.error.code, FplErrorCode::VariableNotAllowed);
	CHECK_STR(e.error.name, L"z");
	CHECK_INT(FplBuild(L"z = t", {}).error.code, FplErrorCode::VariableNotAllowed);
	CHECK_INT(FplBuild(L"z", {}).error.code, FplErrorCode::NotAPlot);

	// the heat map's grid and its range
	auto v = View();
	auto h = FplSampleHeatmap([](double x, double y) { return x + y; }, v);
	CHECK_INT(h.cols, 200);
	CHECK_INT(h.rows, 200);
	CHECK_NEAR_TOL(h.values[0], 0, 1e-6);		// (-9.95, 9.95)
	CHECK_NEAR_TOL(h.values[199], 9.95 + 9.95, 1e-5);
	CHECK(h.lo < -10 && h.hi > 10);
	CHECK_NEAR_TOL(h.lo, -h.hi, 1e-6);
	h = FplSampleHeatmap([](double x, double) { return std::sqrt(x); }, v);
	CHECK(std::isnan(h.values[0]));
	CHECK(h.lo >= 0);
	h = FplSampleHeatmap([](double, double) { return NaN; }, v);
	CHECK(h.Empty());
	h = FplSampleHeatmap([](double, double) { return 3.0; }, v);
	CHECK(h.lo < 3 && h.hi > 3);
	// a pole doesn't stretch the range
	h = FplSampleHeatmap([](double x, double y) { return 1 / (x * x + y * y); }, v);
	CHECK(h.hi < 50);

	// levels
	auto levels = FplContourLevels(0, 1, 10);
	CHECK_INT(levels.size(), 11);
	CHECK(levels[3] == 0.3);
	levels = FplContourLevels(-3.7, 8.2, 5);
	CHECK_INT(levels.size(), 2);
	CHECK_NEAR(levels[1], 5);
	CHECK(FplContourLevels(1, 1, 10).empty());

	// contours of a paraboloid: circles
	auto g = Geometry(L"z = x^2 + y^2", v);
	CHECK(!g.contours.empty());
	CHECK(g.heatmap.Empty());
	CHECK(g.lines.empty());
	bool round = true;
	for (auto& c : g.contours) {
		for (auto& line : c.lines)
			for (auto& s : line) {
				auto p = v.ToPlane(s);
				round = round && std::abs(std::hypot(p.x, p.y) - std::sqrt(c.level)) < 1e-6;
			}
	}
	CHECK(round);
	FplSampleOptions o;
	o.heatmap = true;
	o.contours = false;
	g = Geometry(L"z = x^2 + y^2", v, o);
	CHECK(g.contours.empty());
	CHECK(!g.heatmap.Empty());
	// through the graphs' flags
	auto plot = MakePlot({ L"z = x y" });
	plot.graphs[0].flags = FplGraphHeatmap | FplGraphNoContours;
	plot.Sample(v);
	CHECK(!plot.graphs[0].geometry.heatmap.Empty());
	CHECK(plot.graphs[0].geometry.contours.empty());

	// the colormap's ends and middle
	CHECK_INT(FplColormap(0), 68 | (1 << 8) | (84 << 16));
	CHECK_INT(FplColormap(1), 253 | (231 << 8) | (37 << 16));
	CHECK_INT(FplColormap(0.5), 33 | (145 << 8) | (140 << 16));
	CHECK_INT(FplColormap(-1), FplColormap(0));
	CHECK_INT(FplColormap(NaN), FplColormap(0));
}

TEST(VectorFields) {
	auto e = FplBuild(L"(-y, x)", {});
	CHECK_INT(e.kind, FplKind::VectorField);
	CHECK_INT(FplBuild(L"(1, 2)", {}).kind, FplKind::Point);
	CHECK_INT(FplBuild(L"(x, t)", {}).error.code, FplErrorCode::VariableNotAllowed);
	CHECK_INT(FplBuild(L"(x, z)", {}).error.code, FplErrorCode::VariableNotAllowed);

	auto v = View();
	auto g = Geometry(L"(-y, x)", v);
	// a step of 1 (40 DIPs): 21 by 21 points, but none at the origin, where the field is zero
	CHECK_INT(g.arrows.size(), 440);
	auto origin = v.ToScreen({ 0, 0 });
	bool around = true, sized = true;
	double longest = 0;
	for (auto& a : g.arrows) {
		FplPoint c = { (a.from.x + a.to.x) / 2, (a.from.y + a.to.y) / 2 };
		double dot = (c.x - origin.x) * (a.to.x - a.from.x) + (c.y - origin.y) * (a.to.y - a.from.y);
		double len = std::hypot(a.to.x - a.from.x, a.to.y - a.from.y);
		around = around && std::abs(dot) < 1e-6 * (1 + len * std::hypot(c.x - origin.x, c.y - origin.y));
		sized = sized && len <= 0.85 * 40 + 1e-9 && len >= 0.2 * 0.85 * 40 - 1e-9;
		longest = std::max(longest, len);
	}
	CHECK(around);
	CHECK(sized);
	CHECK_NEAR(longest, 0.85 * 40);
	// directions on the screen: right is right, up is up
	auto right = FplSampleField([](double, double) { return FplPoint{ 1, 0 }; }, v);
	CHECK(right[0].to.x > right[0].from.x);
	CHECK_NEAR(right[0].to.y, right[0].from.y);
	auto up = FplSampleField([](double, double) { return FplPoint{ 0, 1 }; }, v);
	CHECK(up[0].to.y < up[0].from.y);
	// the points are the plane's: panning a little keeps them
	auto moved = v;
	moved.Pan(7, 0);
	auto shifted = FplSampleField([](double, double) { return FplPoint{ 1, 0 }; }, moved);
	CHECK_NEAR(shifted[0].from.x - right[0].from.x, 7);
	// none where it's undefined
	CHECK(FplSampleField([](double, double) { return FplPoint{ NaN, 1 }; }, v).empty());
}

// ---- SVG ----------------------------------------------------------------------------

static size_t Occurrences(const std::string& s, const std::string& what) {
	size_t n = 0;
	for (size_t i = s.find(what); i != std::string::npos; i = s.find(what, i + 1))
		n++;
	return n;
}

TEST(Svg) {
	auto v = View();
	auto plot = MakePlot({ L"y = x", L"x^2 + y^2 < 4", L"(1, 2)", L"z = x y", L"(-y, x)", L"y = 2 +", L"" });
	plot.Sample(v);
	FplSvgStyle style;
	style.graphColors = { 0x4044C7, 0xB3702D, 0x468C38, 0xA64260, 0x197EFA, 0x8E8E1B, 0 };
	std::string svg = FplWriteSvg(plot, v, style);
	CHECK(svg.rfind("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"800\"", 0) == 0);
	CHECK(svg.size() > 1000);
	CHECK(svg.substr(svg.size() - 7) == "</svg>\n");
	// the line in its color, the region translucent with a dashed edge, the point, contours and arrows
	CHECK(svg.find("stroke=\"#C74440\" stroke-width=\"2.5\"") != std::string::npos);
	CHECK_INT(Occurrences(svg, "fill-opacity=\"0.22\""), 1);
	CHECK_INT(Occurrences(svg, "stroke-dasharray"), 1);
	CHECK(svg.find("<circle cx=\"440\" cy=\"320\"") != std::string::npos);
	CHECK(svg.find("stroke=\"#6042A6\" stroke-width=\"1.25\"") != std::string::npos);
	CHECK(svg.find("stroke=\"#FA7E19\" stroke-width=\"1.5\"") != std::string::npos);
	// labels: a minus sign in UTF-8, the legend's text escaped, an empty line left out
	CHECK(svg.find(">\xE2\x88\x92" "5</text>") != std::string::npos);
	CHECK(svg.find(">x^2 + y^2 &lt; 4</text>") != std::string::npos);
	CHECK_INT(Occurrences(svg, "font-size=\"14\""), 6);
	// the failing graph: in the legend, no swatch
	CHECK_INT(Occurrences(svg, "r=\"6\""), 5);
	// without the grid, the axes or the legend
	style.showGrid = style.showAxes = style.showLegend = false;
	std::string bare = FplWriteSvg(plot, v, style);
	CHECK(bare.find("</text>") == std::string::npos);
	CHECK(bare.find("stroke=\"#DDDDDD\"") == std::string::npos);
	CHECK(bare.size() < svg.size());
	CHECK(FplWriteSvg(plot, View(1, 1), style).empty());
}

// ---- 3D -----------------------------------------------------------------------------

TEST(Kinds3D) {
	auto e = FplBuild(L"(cos t, sin t, t/4)", {});
	CHECK_INT(e.kind, FplKind::SpaceCurve);
	double sl[16] = {};
	sl[FplSlotT] = 0;
	CHECK_NEAR(e.f.Eval(sl), 1);
	CHECK_NEAR(e.h.Eval(sl), 0);
	e = FplBuild(L"(cos u sin v, sin u sin v, cos v)", {});
	CHECK_INT(e.kind, FplKind::ParametricSurface);
	sl[FplSlotU] = 0;
	sl[FplSlotV] = Pi / 2;
	CHECK_NEAR(e.f.Eval(sl), 1);
	CHECK_NEAR_TOL(e.h.Eval(sl), 0, 1e-15);
	CHECK_INT(FplBuild(L"(1, 2, 3)", {}).kind, FplKind::Point3D);
	CHECK_INT(FplBuild(L"(u, t, 1)", {}).error.code, FplErrorCode::VariableNotAllowed);
	CHECK_INT(FplBuild(L"(x, y, 1)", {}).error.code, FplErrorCode::VariableNotAllowed);
	CHECK_INT(FplBuild(L"(u, v)", {}).error.code, FplErrorCode::VariableNotAllowed);
	CHECK_INT(FplBuild(L"y = u x", {}).error.code, FplErrorCode::VariableNotAllowed);
	// u and v are variables now: no slider for them
	CHECK_INT(FplBuild(L"y = 2u", {}).error.code, FplErrorCode::VariableNotAllowed);
	// the slots: the parameters after them
	std::vector<std::wstring> p = { L"a" };
	e = FplBuild(L"(a u, v, 0)", p);
	double s2[16] = {};
	s2[FplFirstParam] = 3;
	s2[FplSlotU] = 2;
	CHECK_NEAR(e.f.Eval(s2), 6);
}

TEST(BoxAndCamera) {
	FplBox3 box;
	auto b = box.ToBox(10, -10, 0);
	CHECK_NEAR(b.x, 1);
	CHECK_NEAR(b.y, -1);
	CHECK_NEAR(b.z, 0);
	CHECK_NEAR(box.ToBox(0, 0, 1e300).z, 4);
	CHECK(std::isnan(box.ToBox(0, 0, NaN).z));
	auto back = box.ToSpace({ 0.5f, 0, -1 });
	CHECK_NEAR(back.x, 5);
	CHECK_NEAR(back.z, -10);
	box.Zoom(2);
	CHECK_NEAR(box.xMax, 5);
	CHECK_NEAR(box.zMin, -5);
	box.Pan(1, -2);
	CHECK_NEAR(box.xMin, -4);
	CHECK_NEAR(box.yMax, 3);
	CHECK(box.Valid());

	FplCamera cam;
	cam.yaw = 0;
	cam.pitch = 0;
	auto eye = cam.Eye();
	CHECK_NEAR(eye.x, cam.distance);
	CHECK_NEAR_TOL(eye.z, 0, 1e-6);
	// the center in the middle of the screen; up is up, and +y is to the right looking from +x
	FplPoint s;
	CHECK(cam.Project({ 0, 0, 0 }, 800, 600, s));
	CHECK_NEAR_TOL(s.x, 400, 1e-3);
	CHECK_NEAR_TOL(s.y, 300, 1e-3);
	FplPoint up, right;
	cam.Project({ 0, 0, 0.5f }, 800, 600, up);
	cam.Project({ 0, 0.5f, 0 }, 800, 600, right);
	CHECK(up.y < 300);
	CHECK_NEAR_TOL(up.x, 400, 1e-3);
	CHECK(right.x > 400);
	// nearer looks bigger
	FplPoint nearPt, farPt;
	cam.Project({ 1, 0.5f, 0 }, 800, 600, nearPt);
	cam.Project({ -1, 0.5f, 0 }, 800, 600, farPt);
	CHECK(nearPt.x - 400 > farPt.x - 400);
	// behind the camera
	CHECK(!cam.Project({ 10, 0, 0 }, 800, 600, s));
	// depth: nearer is smaller
	float c1[4], c2[4];
	auto vp = cam.ViewProjection(800.0f / 600);
	vp.Transform({ 1, 0, 0 }, c1);
	vp.Transform({ -1, 0, 0 }, c2);
	CHECK(c1[2] / c1[3] < c2[2] / c2[3]);
	CHECK(c1[2] / c1[3] > 0 && c2[2] / c2[3] < 1);
	// orbiting stops short of the pole
	cam.Orbit(0.5f, 10);
	CHECK(cam.pitch < 1.5708f);
	CHECK_NEAR_TOL(cam.yaw, 0.5, 1e-6);
	cam.Orbit(7, -20);
	CHECK(cam.pitch > -1.5708f);
	CHECK(std::abs(cam.yaw) <= 3.1416f);
}

TEST(Meshes3D) {
	FplBox3 box;
	// a plane: (n + 1)^2 vertices, 2n^2 triangles, normals up (or down), its height for the color
	auto m = FplSurfaceMesh([](double, double) { return 5.0; }, box, 10);
	CHECK_INT(m.vertices.size(), 121);
	CHECK_INT(m.indices.size(), 600);
	CHECK_NEAR_TOL(std::abs(m.vertices[60].normal.z), 1, 1e-6);
	CHECK_NEAR_TOL(m.vertices[60].pos.z, 0.5, 1e-6);
	CHECK_NEAR_TOL(m.vertices[60].value, 0.75, 1e-6);
	// a tilted plane's normal
	m = FplSurfaceMesh([](double x, double) { return x; }, box, 10);
	auto n = m.vertices[60].normal;
	CHECK_NEAR_TOL(std::abs(n.x), std::sqrt(0.5), 1e-5);
	CHECK_NEAR_TOL(std::abs(n.z), std::sqrt(0.5), 1e-5);
	// undefined for x < 0: the cells there left out
	m = FplSurfaceMesh([](double x, double) { return std::sqrt(x); }, box, 10);
	CHECK_INT(m.indices.size(), 300);
	// a jump: the cells across it left out, not the steep ones
	m = FplSurfaceMesh([](double x, double) { return std::floor(x / 3) * 3; }, box, 10);
	CHECK(m.indices.size() < 600);
	CHECK(m.indices.size() >= 600 - 7 * 60);
	m = FplSurfaceMesh([](double x, double) { return 20 * std::atan(x); }, box, 10);
	CHECK_INT(m.indices.size(), 600);
	// a sphere's points are on it, a radius 5 sphere in a box of +-10 is 0.5 in the cube
	auto sphere = FplParametricMesh([](double u, double v) {
		return FplPoint3{ 5 * std::cos(u) * std::sin(v), 5 * std::sin(u) * std::sin(v), 5 * std::cos(v) };
	}, box, 0, 2 * Pi, 0, Pi, 24);
	CHECK_INT(sphere.vertices.size(), 625);
	CHECK(!sphere.Empty());
	bool on = true;
	for (auto& v : sphere.vertices)
		on = on && std::abs(std::sqrt(v.pos.x * v.pos.x + v.pos.y * v.pos.y + v.pos.z * v.pos.z) - 0.5f) < 1e-5f;
	CHECK(on);

	// a helix: one piece; broken where undefined
	auto lines = FplSampleCurve3([](double t) { return FplPoint3{ 5 * std::cos(t), 5 * std::sin(t), t }; }, box, 0, 6, 100);
	CHECK_INT(lines.size(), 1);
	CHECK_INT(lines[0].size(), 101);
	lines = FplSampleCurve3([](double t) { return FplPoint3{ t, 0, t < 0 ? NaN : t }; }, box, -5, 5, 100);
	CHECK_INT(lines.size(), 1);
	lines = FplSampleCurve3([](double t) { return FplPoint3{ t, 0, std::tan(t) }; }, box, -3, 3, 300);
	CHECK_INT(lines.size(), 3);
	// a tube: sides vertices per point, each its radius from the line
	auto tube = FplTubes({ { { 0, 0, 0 }, { 0.5f, 0, 0 }, { 0.5f, 0.5f, 0 } } }, 0.1f, 8);
	CHECK_INT(tube.vertices.size(), 24);
	CHECK_INT(tube.indices.size(), 2 * 8 * 6);
	CHECK_NEAR_TOL(std::hypot(tube.vertices[3].pos.y, tube.vertices[3].pos.z), 0.1, 1e-6);
	CHECK(FplTubes({ { { 0, 0, 0 } } }, 0.1f).Empty());
	auto ball = FplSphere({ 0.5f, 0, 0 }, 0.2f, 12);
	bool round = true;
	for (auto& v : ball.vertices)
		round = round && std::abs(std::hypot(v.pos.x - 0.5f, std::hypot(v.pos.y, v.pos.z)) - 0.2f) < 1e-5f;
	CHECK(round);
	auto edges = FplBoxEdges();
	CHECK_INT(edges.vertices.size(), 8);
	CHECK_INT(edges.indices.size(), 24);
	FplMesh all = ball;
	FplAppend(all, tube);
	CHECK_INT(all.vertices.size(), ball.vertices.size() + 24);
	CHECK_INT(all.indices.back(), ball.vertices.size() + tube.indices.back());

	// ticks along the edges toward the camera, with the axes' names
	FplCamera cam;
	auto ticks = FplBoxTicks(box, cam);
	int names = 0;
	for (auto& t : ticks)
		names += t.name;
	CHECK_INT(names, 3);
	CHECK(ticks.size() > 12);
	auto eye = cam.Eye();
	bool toward = true;
	for (auto& t : ticks)
		if (!t.name && t.at.z == -1 && std::abs(t.at.x) > 1)
			toward = toward && (t.at.x > 0) == (eye.x > 0);
	CHECK(toward);
}

TEST(Graphs3D) {
	auto plot = MakePlot({ L"z = x y / 10", L"(cos u sin v, sin u sin v, cos v)", L"(5cos t, 5sin t, t)", L"(1, 2, 3)",
		L"y = x", L"(2, 1)", L"x^2 + y^2 = 4", L"c = 2", L"(1, 2, 30)", L"y = 2 +" });
	FplBox3 box;
	plot.Build3D(box);
	CHECK_INT(plot.graphs[0].mesh.vertices.size(), 97 * 97);
	CHECK(!plot.graphs[0].mesh.Empty());
	CHECK_INT(plot.graphs[1].mesh.vertices.size(), 65 * 65);
	CHECK(!plot.graphs[2].mesh.Empty());		// a tube
	CHECK(!plot.graphs[3].mesh.Empty());		// a sphere
	auto& ball = plot.graphs[3].mesh.vertices;
	FplVec3 center = box.ToBox(1, 2, 3);
	CHECK_NEAR_TOL(std::hypot(ball[0].pos.x - center.x, std::hypot(ball[0].pos.y - center.y, ball[0].pos.z - center.z)),
		0.01 + 0.005 * 2.5, 1e-5);
	CHECK(!plot.graphs[4].mesh.Empty());		// y = x on the floor's z = 0
	bool flat = true;
	for (auto& v : plot.graphs[4].mesh.vertices)
		flat = flat && std::abs(v.pos.z) < 0.02f;
	CHECK(flat);
	CHECK(!plot.graphs[5].mesh.Empty());
	CHECK(!plot.graphs[6].mesh.Empty());		// an implicit curve, on the plane
	CHECK(plot.graphs[7].mesh.Empty());
	CHECK(plot.graphs[8].mesh.Empty());			// outside the box
	CHECK(plot.graphs[9].mesh.Empty());
	// hidden graphs have none; a parametric range
	plot.graphs[0].flags = FplGraphHidden;
	plot.graphs[2].hasRange = true;
	plot.graphs[2].tMin = 0;
	plot.graphs[2].tMax = 1;
	plot.Build3D(box);
	CHECK(plot.graphs[0].mesh.Empty());
	// t up to 1: z up to 1 (0.1 in the cube), and the tube's radius
	float top = -1;
	for (auto& v : plot.graphs[2].mesh.vertices)
		top = std::max(top, v.pos.z);
	CHECK(top < 0.1f + 0.0126f && top > 0.1f);
}

TEST(ImplicitSurfaces) {
	auto e = FplBuild(L"x^2 + y^2 + z^2 = 25", {});
	CHECK_INT(e.kind, FplKind::ImplicitSurface);
	double sl[16] = {};
	sl[FplSlotZ] = 5;
	CHECK_NEAR(e.f.Eval(sl), 0);
	CHECK_INT(FplBuild(L"x = z^2", {}).kind, FplKind::ImplicitSurface);
	CHECK_INT(FplBuild(L"z = x y", {}).kind, FplKind::Surface);
	CHECK_INT(FplBuild(L"x^2 + z^2 < 4", {}).error.code, FplErrorCode::NotAPlot);
	CHECK_INT(FplBuild(L"x + z + t = 0", {}).error.code, FplErrorCode::VariableNotAllowed);

	// a sphere of radius 5: its points on it, its normals along the radius, no cracks
	FplBox3 box;
	auto m = FplImplicitMesh([](double x, double y, double z) { return x * x + y * y + z * z - 25; }, box, 24);
	CHECK(m.indices.size() > 300);
	bool on = true, radial = true;
	for (auto& v : m.vertices) {
		FplPoint3 p = box.ToSpace(v.pos);
		double r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
		on = on && std::abs(r - 5) < 1e-3;
		float len = std::sqrt(v.pos.x * v.pos.x + v.pos.y * v.pos.y + v.pos.z * v.pos.z);
		float dot = (v.normal.x * v.pos.x + v.normal.y * v.pos.y + v.normal.z * v.pos.z) / len;
		radial = radial && dot > 0.99f;
	}
	CHECK(on);
	CHECK(radial);
	std::map<std::pair<uint32_t, uint32_t>, int> edges;
	for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
		for (int k = 0; k < 3; k++) {
			uint32_t a = m.indices[i + k], b = m.indices[i + (k + 1) % 3];
			edges[{ std::min(a, b), std::max(a, b) }]++;
		}
	bool closed = true;
	for (auto& [edge, count] : edges)
		closed = closed && count == 2;
	CHECK(closed);
	// shared vertices: about one per crossing, not three per triangle
	CHECK(m.vertices.size() * 3 < m.indices.size());

	// a jump (z = tan x as F = z - tan x): nothing along the asymptotes
	m = FplImplicitMesh([](double x, double, double z) { return z - std::tan(x); }, box, 24);
	CHECK(!m.Empty());
	bool fits = true;
	for (auto& v : m.vertices) {
		FplPoint3 p = box.ToSpace(v.pos);
		fits = fits && std::abs(p.z - std::tan(p.x)) < 1e-2 * (1 + std::abs(std::tan(p.x)));
	}
	CHECK(fits);
	// nothing there, or undefined
	CHECK(FplImplicitMesh([](double x, double y, double z) { return x * x + y * y + z * z + 1; }, box, 12).Empty());
	CHECK(FplImplicitMesh([](double, double, double) { return NaN; }, box, 12).Empty());
	// through the graphs
	auto plot = MakePlot({ L"x^2 + y^2 + z^2 = 25", L"x^2 + y^2 = 4" });
	plot.Build3D(box);
	CHECK(!plot.graphs[0].mesh.Empty());
	CHECK(!plot.graphs[1].mesh.Empty());		// the 2D curve, on the plane
	plot.Sample(View());
	CHECK(plot.graphs[0].geometry.lines.empty());		// nothing in 2D
}

TEST(TraceSurfaces) {
	auto v = View();
	auto plot = MakePlot({ L"z = x + 2y", L"y = 1" });
	plot.Sample(v);
	// away from the line: the surface under the point, with its value
	auto hit = plot.Nearest(v.ToScreen({ 3, -4 }), v, 10, 8, true);
	CHECK_INT(hit.graph, 0);
	CHECK_NEAR_TOL(hit.plane.x, 3, 1e-9);
	CHECK_NEAR_TOL(hit.z, 3 - 8, 1e-9);
	// not without asking (a click pans)
	CHECK(!plot.Nearest(v.ToScreen({ 3, -4 }), v, 10, 8));
	// a curve near wins, and has no z
	hit = plot.Nearest(v.ToScreen({ 3, 1.05 }), v, 10, 8, true);
	CHECK_INT(hit.graph, 1);
	CHECK(std::isnan(hit.z));
	// following it by the mouse
	hit = plot.TraceAt(0, v.ToScreen({ -2, 2 }), v, 8);
	CHECK_NEAR_TOL(hit.z, 2, 1e-9);
	CHECK_NEAR(plot.EvalSurface(0, 1, 1), 3);
	CHECK(std::isnan(plot.EvalSurface(1, 1, 1)));
	// the topmost surface; none where it's undefined
	plot = MakePlot({ L"z = 1", L"z = sqrt(x)" });
	plot.Sample(v);
	CHECK_INT(plot.Nearest(v.ToScreen({ 4, 0 }), v, 10, 8, true).graph, 1);
	CHECK_INT(plot.Nearest(v.ToScreen({ -4, 0 }), v, 10, 8, true).graph, 0);
	plot.graphs[0].flags = FplGraphNoTrace;
	plot.Sample(v);
	CHECK(!plot.Nearest(v.ToScreen({ -4, 0 }), v, 10, 8, true));
}

TEST(Trace3D) {
	FplCamera cam;
	FplVec3 origin, dir;
	// the screen's middle looks at the cube's center
	cam.Ray({ 400, 300 }, 800, 600, origin, dir);
	auto eye = cam.Eye();
	CHECK_NEAR_TOL(origin.x, eye.x, 1e-6);
	CHECK_NEAR_TOL(dir.x, -eye.x / cam.distance, 1e-5);
	CHECK_NEAR_TOL(dir.z, -eye.z / cam.distance, 1e-5);
	// a ray through a point's projection passes through the point
	FplVec3 target = { 0.3f, -0.5f, 0.2f };
	FplPoint s;
	CHECK(cam.Project(target, 800, 600, s));
	cam.Ray(s, 800, 600, origin, dir);
	FplVec3 to = { target.x - origin.x, target.y - origin.y, target.z - origin.z };
	float along = to.x * dir.x + to.y * dir.y + to.z * dir.z;
	FplVec3 off = { to.x - dir.x * along, to.y - dir.y * along, to.z - dir.z * along };
	CHECK(std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z) < 1e-4f);

	// a flat surface hit from above; the nearer of two; nothing outside the cube
	FplBox3 box;
	auto plane = FplSurfaceMesh([](double, double) { return 5.0; }, box, 8);
	float t;
	CHECK(FplIntersect(plane, { 0.2f, 0.1f, 3 }, { 0, 0, -1 }, t));
	CHECK_NEAR_TOL(t, 2.5, 1e-5);
	CHECK(!FplIntersect(plane, { 0.2f, 0.1f, 3 }, { 0, 0, 1 }, t));
	CHECK(!FplIntersect(plane, { 1.5f, 0.1f, 3 }, { 0, 0, -1 }, t));
	auto high = FplSurfaceMesh([](double, double) { return 50.0; }, box, 8);		// clamped outside the cube
	CHECK(!FplIntersect(high, { 0.2f, 0.1f, 3 }, { 0, 0, -1 }, t));

	auto plot = MakePlot({ L"z = x y / 10", L"x^2 + y^2 + z^2 = 25", L"(1, 2, 8)" });
	plot.Build3D(box);
	// straight down onto the surface: on it exactly
	auto hit = plot.Pick3D({ 0.6f, -0.3f, 3 }, { 0, 0, -1 }, box);
	CHECK_INT(hit.graph, 0);
	CHECK_NEAR_TOL(hit.z, hit.plane.x * hit.plane.y / 10, 1e-9);
	CHECK_NEAR_TOL(hit.plane.x, 6, 1e-4);
	// the sphere's top before the surface under it
	hit = plot.Pick3D({ 0, 0, 3 }, { 0, 0, -1 }, box);
	CHECK_INT(hit.graph, 1);
	CHECK_NEAR_TOL(hit.z, 5, 0.05);
	// a point at its own place
	FplVec3 p = box.ToBox(1, 2, 8);
	hit = plot.Pick3D({ p.x, p.y, 3 }, { 0, 0, -1 }, box);
	CHECK_INT(hit.graph, 2);
	CHECK_NEAR(hit.z, 8);
	CHECK_NEAR(hit.plane.y, 2);
	// past everything
	CHECK(!plot.Pick3D({ 0.9f, 0.9f, 3 }, { 0, 0, 1 }, box));
}

TEST(KeyboardTrace) {
	auto v = View();
	auto plot = MakePlot({ L"y = x^2/4 - 1", L"y = 1/x", L"(5cos t, 5sin t)", L"y = 3" });
	plot.Sample(v);
	// a function: by 4 DIPs of x (0.1 here); past where it's undefined
	auto hit = plot.Step(0, { 2, 0 }, 4, v);
	CHECK_INT(hit.graph, 0);
	CHECK_NEAR(hit.plane.x, 2.1);
	CHECK_NEAR(hit.plane.y, 2.1 * 2.1 / 4 - 1);
	hit = plot.Step(0, { 2, 0 }, -1, v);
	CHECK_NEAR(hit.plane.x, 2.0 - 0.025);
	hit = plot.Step(1, { -0.05, -20 }, 4, v);		// over x = 0 (1/0)
	CHECK(hit.plane.x > 0);
	// a curve: along its polyline by arc length on the screen
	FplPoint start = { 5, 0 };
	hit = plot.Step(2, start, 40, v);
	CHECK_INT(hit.graph, 2);
	CHECK_NEAR_TOL(std::hypot(hit.plane.x, hit.plane.y), 5, 1e-3);
	CHECK_NEAR_TOL(std::hypot(hit.screen.x - v.ToScreenX(5), hit.screen.y - v.ToScreenY(0)), 40, 0.5);
	auto back = plot.Step(2, hit.plane, -40, v);
	CHECK_NEAR_TOL(back.plane.x, 5, 1e-3);
	// features: the next root or extremum along x
	plot.FindFeatures(0, v);
	hit = plot.NextFeature(0, -3, 1, v);
	CHECK(hit.feature >= 0);
	CHECK_NEAR(hit.plane.x, -2);
	hit = plot.NextFeature(0, -2, 1, v);
	CHECK_NEAR(hit.plane.x, 0);
	hit = plot.NextFeature(0, 0, -1, v);
	CHECK_NEAR(hit.plane.x, -2);
	CHECK(!plot.NextFeature(0, 100, 1, v));
	// the other functions at the same x, going round; not the curve
	hit = plot.NextGraphAt(0, 2, 1, v);
	CHECK_INT(hit.graph, 1);
	CHECK_NEAR(hit.plane.y, 0.5);
	hit = plot.NextGraphAt(1, 2, 1, v);
	CHECK_INT(hit.graph, 3);
	hit = plot.NextGraphAt(3, 2, 1, v);
	CHECK_INT(hit.graph, 0);
	CHECK_INT(plot.NextGraphAt(0, 0, -1, v).graph, 3);		// 1/x isn't defined at 0
	// slopes
	CHECK_NEAR(plot.Slope(0, 2), 1);
	CHECK_NEAR_TOL(plot.Slope(1, 2), -0.25, 1e-12);
	CHECK(std::isnan(plot.Slope(2, 0)));
	plot = MakePlot({ L"x = y^3" });
	CHECK_NEAR(plot.Slope(0, 2), 12);
}

TEST(DraggablePoints) {
	auto v = View();
	auto plot = MakePlot({ L"(a, b)", L"(a, 2)", L"(1, sin a)", L"(2a, b)", L"(c, 1)", L"c = 3" },
		{ Param(L"a", 1), Param(L"b", -2) });
	CHECK_INT(plot.graphs[0].dragX, 0);
	CHECK_INT(plot.graphs[0].dragY, 1);
	CHECK_INT(plot.graphs[1].dragX, 0);
	CHECK_INT(plot.graphs[1].dragY, -1);
	CHECK_INT(plot.graphs[2].dragX, -1);		// sin a: follows a, isn't a
	CHECK_INT(plot.graphs[2].dragY, -1);
	CHECK_INT(plot.graphs[3].dragX, -1);		// 2a
	CHECK_INT(plot.graphs[3].dragY, 1);
	CHECK_INT(plot.graphs[4].dragX, -1);		// a definition isn't the app's to set
	plot.Sample(v);
	// the one under the mouse: (a, b) and (2a, b) are apart; (a, 2) elsewhere
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 1, -2 }), 10), 0);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 1.1, 2 }), 10), 1);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 2, -2.1 }), 10), 3);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 5, 5 }), 10), -1);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 1, std::sin(1.0) }), 10), -1);
	plot.graphs[0].flags = FplGraphNoDrag;
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 1, -2 }), 10), -1);
}

TEST(Integrals) {
	// values: over x, over a variable named, the sign too
	auto r = FplParse(L"integral(x^2, 0, 3)");
	CHECK(!r.error);
	CHECK_NEAR_TOL(FplEvalTree(r.ast, r.root, {}, nullptr), 9, 1e-9);
	CHECK_STR(FplToString(r.ast, r.root), L"integral(x^2, x, 0, 3)");
	auto e = FplBuild(L"integral(x^2, 0, 3)", {});
	CHECK_INT(e.kind, FplKind::Integral);
	double sl[16] = {};
	CHECK_NEAR_TOL(e.value.Eval(sl), 9, 1e-9);
	CHECK_NEAR(e.g.Eval(sl), 0);
	CHECK_NEAR(e.h.Eval(sl), 3);
	CHECK_STR(e.name, L"x");
	CHECK_NEAR_TOL(F(L"A = integral(x^2, 3, 0)"), -9, 1e-9);
	CHECK_NEAR_TOL(F(L"y = \x222B(x, 0, 1)"), 0.5, 1e-12);
	CHECK_NEAR_TOL(F(L"y = integral(exp(-t^2), t, -10, 10)"), std::sqrt(Pi), 1e-9);
	CHECK_NEAR_TOL(F(L"y = integral(cos(40t), t, 0, 1)"), std::sin(40.0) / 40, 1e-9);
	// a function of x: the antiderivative; the bound variable doesn't make it a function of t
	e = FplBuild(L"y = integral(sin t, t, 0, x)", {});
	CHECK_INT(e.kind, FplKind::Function);
	CHECK_NEAR_TOL(F(L"y = integral(sin t, t, 0, x)", Pi), 2, 1e-9);
	CHECK_NEAR_TOL(F(L"y = integral(x t, t, 0, 1)", 3), 1.5, 1e-9);
	CHECK_NEAR_TOL(F(L"y = integral(t, t, 0, a)"), 2, 1e-9);		// a = 2
	// constant parts folded: x + 9
	e = FplBuild(L"y = x + integral(t^2, t, 0, 3)", {});
	CHECK(e.f.Size() <= 3);
	// undefined somewhere: undefined
	CHECK(std::isnan(F(L"y = integral(sqrt(t), t, -1, 1)")));
	CHECK_NEAR_TOL(FplIntegrate([](double t) { return t * t; }, 0, 1), 1.0 / 3, 1e-12);
	// derivatives: Leibniz
	auto d = FplDerive(r.ast, r.root, L"x");
	CHECK_NEAR(FplEvalTree(r.ast, d, {}, nullptr), 0);
	r = FplParse(L"integral(sin t, t, 0, x^2)");
	d = FplDerive(r.ast, r.root, L"x");
	std::vector<std::wstring> xs = { L"x" };
	double one = 1;
	CHECK_NEAR_TOL(FplEvalTree(r.ast, d, xs, &one), 2 * std::sin(1.0), 1e-9);
	r = FplParse(L"integral(x t, t, 0, 1)");
	d = FplDerive(r.ast, r.root, L"x");
	CHECK_NEAR_TOL(FplEvalTree(r.ast, d, xs, &one), 0.5, 1e-9);
	CHECK_NEAR_TOL(F(L"y = d/dx integral(cos t, t, 0, x)", 0.7), std::cos(0.7), 1e-9);
	// mistakes
	CHECK_INT(BuildError(L"integral(x, 0)").code, FplErrorCode::ArgCount);
	CHECK_INT(BuildError(L"integral(x, 2, 0, 1)").code, FplErrorCode::UnexpectedToken);
	CHECK_INT(BuildError(L"y = integral x").code, FplErrorCode::MissingArgument);
	// the area: shaded between the integrand and the axis, its value
	auto v = View();
	auto plot = MakePlot({ L"integral(x/2 + 1, 0, 4)", L"A = integral(x^2, 0, 3)", L"y = 2 integral(1, 0, 3)" });
	plot.Sample(v);
	CHECK_NEAR_TOL(plot.IntegralValue(0), 8, 1e-9);
	CHECK(std::isnan(plot.IntegralValue(1)));
	CHECK_NEAR_TOL(plot.DefinitionValue(1), 9, 1e-9);
	CHECK_INT(plot.graphs[0].geometry.fills.size(), 1);
	CHECK(!plot.graphs[0].geometry.lines.empty());
	double area = std::abs(SignedArea(plot.graphs[0].geometry.fills[0]));
	CHECK_NEAR_TOL(area, 8 * v.ScaleX() * v.ScaleY(), 1e-3);
	CHECK_INT(plot.graphs[2].expr.kind, FplKind::Function);
	CHECK_NEAR(plot.Eval(2, 5), 6);
}

static FplPlot MakeDataPlot(std::initializer_list<const wchar_t*> texts, std::initializer_list<FplList> data,
	std::initializer_list<FplParam> params = {}) {
	FplPlot plot;
	unsigned id = 1;
	for (auto t : texts) {
		FplGraph g;
		g.id = id++;
		g.text = t;
		plot.graphs.push_back(g);
	}
	plot.lists = data;
	plot.params = params;
	plot.Build();
	return plot;
}

TEST(Lists) {
	// a list defined, its aggregates
	auto plot = MakePlot({ L"L = [1, 2, 6]", L"c = mean(L)", L"d = max(L) - min(L)", L"n = count(L)" });
	CHECK(plot.graphs[0].listDefinition);
	CHECK(!plot.graphs[0].Drawable());
	auto* values = plot.ListValues(L"L");
	CHECK(values && values->size() == 3);
	CHECK_NEAR(plot.ValueOf(L"mean(L)"), 3);
	CHECK_NEAR(plot.ValueOf(L"median(L)"), 2);
	CHECK_NEAR(plot.ValueOf(L"stdev(L)"), std::sqrt(7.0));
	CHECK_NEAR(plot.ValueOf(L"sum(L)"), 9);
	CHECK_NEAR(plot.DefinitionValue(1), 3);
	CHECK_NEAR(plot.DefinitionValue(2), 5);
	CHECK_NEAR(plot.DefinitionValue(3), 3);
	// items from parameters follow them
	plot = MakePlot({ L"L = [a, 2a]", L"s = sum(L)" }, { Param(L"a", 3) });
	CHECK_NEAR(plot.DefinitionValue(1), 9);
	plot.params[0].value = 5;
	plot.UpdateValues();
	CHECK_NEAR((*plot.ListValues(L"L"))[1], 10);
	CHECK_NEAR(plot.DefinitionValue(1), 15);
	// mistakes: a list where one number goes, an aggregate of a number, a list inside an expression
	plot = MakePlot({ L"L = [1, 2]", L"c = L + 1", L"y = mean(3)", L"y = [1, 2] x" });
	CHECK_INT(plot.graphs[1].expr.error.code, FplErrorCode::ListNotAllowed);
	CHECK_INT(plot.graphs[2].expr.error.code, FplErrorCode::NotAList);
	CHECK_STR(plot.graphs[2].expr.error.name, L"mean");
	CHECK_INT(plot.graphs[3].expr.error.code, FplErrorCode::ListNotAllowed);
	CHECK_STR((FplError{ FplErrorCode::NotAList, 0, 0, L"mean" }.Message()), L"'mean' needs a list");

	// the app's lists: a scatter plot, a family of curves
	auto v = View();
	plot = MakeDataPlot({ L"(x1, y1)", L"y = x + x1", L"c = max(y1)" },
		{ { L"x1", { 1, 2, 3, 4 } }, { L"y1", { 2, 4, 6, 8 } } });
	CHECK_INT(plot.graphs[0].listSlots.size(), 2);
	CHECK_INT(plot.ElementCount(0), 4);
	CHECK_INT(plot.graphs[1].listSlots.size(), 1);
	CHECK_NEAR(plot.DefinitionValue(2), 8);
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.points.size(), 4);
	CHECK_NEAR(plot.graphs[0].geometry.points[3].x, v.ToScreenX(4));
	CHECK_NEAR(plot.graphs[0].geometry.points[3].y, v.ToScreenY(8));
	CHECK_INT(plot.graphs[1].geometry.lines.size(), 4);
	// a listed point isn't draggable, its trace is where it is; a listed function has no features
	CHECK_INT(plot.graphs[0].dragX, -1);
	auto hit = plot.Nearest(v.ToScreen({ 3, 6 }), v, 10, 8);
	CHECK_INT(hit.graph, 0);
	CHECK_NEAR_TOL(hit.plane.y, 6, 1e-9);
	plot.FindFeatures(1, v);
	CHECK(plot.graphs[1].features.empty());
	// the shortest list counts; the app changes the values
	plot.lists[1].values = { 1, 1 };
	plot.UpdateValues();
	CHECK_INT(plot.ElementCount(0), 2);
	// 3D: a sphere per element
	plot = MakeDataPlot({ L"(x1, y1, 0)" }, { { L"x1", { 1, 2, 3 } }, { L"y1", { 1, 1, 1 } } });
	plot.Build3D(FplBox3{});
	CHECK_INT(plot.graphs[0].mesh.vertices.size(), 3 * FplSphere({}, 1).vertices.size());
}

TEST(Regression) {
	// exact data: the line itself, R squared 1; the fitted names usable elsewhere
	auto plot = MakeDataPlot({ L"y1 ~ m x1 + b", L"y = m x + b", L"k = 2m" },
		{ { L"x1", { 0, 1, 2, 3, 4 } }, { L"y1", { 1, 3, 5, 7, 9 } } });
	CHECK_INT(plot.graphs[0].expr.kind, FplKind::Regression);
	CHECK(!plot.graphs[0].Drawable());
	auto fitted = plot.Fitted(0);
	CHECK_INT(fitted.size(), 2);
	CHECK_STR(fitted[0].first, L"m");
	CHECK_NEAR_TOL(fitted[0].second, 2, 1e-8);
	CHECK_NEAR_TOL(fitted[1].second, 1, 1e-8);
	CHECK_NEAR_TOL(plot.graphs[0].r2, 1, 1e-12);
	CHECK(!plot.graphs[1].expr.error);
	CHECK_NEAR_TOL(plot.Eval(1, 3), 7, 1e-8);
	CHECK_NEAR_TOL(plot.DefinitionValue(2), 4, 1e-8);
	CHECK_NEAR_TOL(plot.ValueOf(L"b"), 1, 1e-8);
	// not a line: y = 3 e^(x/2)
	std::vector<double> xs, ys;
	for (int i = 0; i <= 10; i++) {
		xs.push_back(i * 0.5);
		ys.push_back(3 * std::exp(0.25 * i));
	}
	plot = MakeDataPlot({ L"y1 ~ a e^(k x1)" }, { { L"x1", xs }, { L"y1", ys } });
	fitted = plot.Fitted(0);
	CHECK_INT(fitted.size(), 2);
	CHECK_NEAR_TOL(plot.ValueOf(L"a"), 3, 1e-6);
	CHECK_NEAR_TOL(plot.ValueOf(L"k"), 0.5, 1e-6);
	// noisy data: close, R squared below 1
	plot = MakeDataPlot({ L"y1 ~ m x1 + b" }, { { L"x1", { 0, 1, 2, 3, 4, 5 } }, { L"y1", { 0.1, 0.9, 2.2, 2.8, 4.1, 5.0 } } });
	CHECK_NEAR_TOL(plot.ValueOf(L"m"), 0.98, 0.05);
	CHECK(plot.graphs[0].r2 > 0.98 && plot.graphs[0].r2 < 1);
	// the data changes: fitted again from where it was
	plot.lists[1].values = { 0, 2, 4, 6, 8, 10 };
	plot.UpdateValues();
	CHECK_NEAR_TOL(plot.ValueOf(L"m"), 2, 1e-8);
	// nothing to fit, or a variable in it
	plot = MakeDataPlot({ L"y1 ~ 2x1", L"y1 ~ m x" }, { { L"x1", { 1, 2 } }, { L"y1", { 2, 4 } } });
	CHECK_INT(plot.graphs[0].expr.error.code, FplErrorCode::NotAPlot);
	CHECK((bool)plot.graphs[1].expr.error);
	// printed with its tilde
	auto r = FplParse(L"y ~ x");
	CHECK_STR(FplToString(r.ast, r.root), L"y ~ x");
}

TEST(SavedState) {
	FplSavedState st;
	FplGraph g;
	g.id = 3;
	g.text = L"y = a sin(\x03B8)\tx\n";		// a tab and a backslash to escape, a non-ASCII letter
	g.autoColor = false;
	g.color = 0x123456;
	g.width = 3.5f;
	g.flags = FplGraphHidden | FplGraphHeatmap;
	g.hasRange = true;
	g.tMin = -1;
	g.tMax = 0.1;
	st.graphs.push_back(g);
	FplGraph auto2;
	auto2.id = 7;
	auto2.text = L"x^2 + y^2 = 4\nsecond line";
	st.graphs.push_back(auto2);
	st.params.push_back(Param(L"a", 1.0 / 3));
	st.params[0].min = -2;
	st.params[0].max = 5;
	st.params[0].step = 0.25;
	st.params[0].flags = 1;
	st.lists.push_back({ L"x1", { 1.5, -2, 1e-300 } });
	st.lists.push_back({ L"empty", {} });
	st.degrees = true;
	st.mode3D = true;
	st.collapsed = true;
	st.view.xMin = -3.25;
	st.view.xMax = 7;
	st.view.yMin = -1e6;
	st.view.yMax = 2;
	st.box = { -1, 1, -2, 2, -3, 3 };
	st.yaw = 0.7f;
	st.pitch = -0.2f;

	std::wstring text = FplSaveState(st);
	CHECK(text.rfind(L"FunctionPlot 1\n", 0) == 0);
	FplSavedState back;
	CHECK(FplLoadState(text, back));
	CHECK_INT(back.graphs.size(), 2);
	CHECK_STR(back.graphs[0].text, g.text);
	CHECK_STR(back.graphs[1].text, auto2.text);
	CHECK_INT(back.graphs[0].id, 3);
	CHECK(!back.graphs[0].autoColor);
	CHECK_INT(back.graphs[0].color, 0x123456);
	CHECK(back.graphs[1].autoColor);
	CHECK_NEAR(back.graphs[0].width, 3.5);
	CHECK_INT(back.graphs[0].flags, FplGraphHidden | FplGraphHeatmap);
	CHECK(back.graphs[0].hasRange);
	CHECK(back.graphs[0].tMax == 0.1);
	CHECK(back.params[0].value == 1.0 / 3);
	CHECK_NEAR(back.params[0].step, 0.25);
	CHECK_INT(back.params[0].flags, 1);
	CHECK_INT(back.lists.size(), 2);
	CHECK(back.lists[0].values[2] == 1e-300);
	CHECK(back.lists[1].values.empty());
	CHECK(back.degrees && back.mode3D && back.collapsed);
	CHECK(back.view.yMin == -1e6);
	CHECK_NEAR(back.box.zMax, 3);
	CHECK_NEAR(back.yaw, 0.7f);
	CHECK_NEAR(back.pitch, -0.2f);
	// the same text again
	CHECK_STR(FplSaveState(back), text);
	// CR LF line ends, an unknown line, not a state at all
	std::wstring crlf;
	for (wchar_t c : text) {
		if (c == L'\n')
			crlf += L'\r';
		crlf += c;
	}
	crlf += L"future\tsomething new\r\n";
	CHECK(FplLoadState(crlf, back));
	CHECK_STR(back.graphs[1].text, auto2.text);
	CHECK(FplLoadState(L"FunctionPlot 1\ngraph\t1\t-1\t2\t0\t0\t0\t1\ty = 1 \\ x \\q", back));
	CHECK_STR(back.graphs[0].text, L"y = 1 \\ x \\q");		// not escapes: kept
	CHECK(FplLoadState(text, back));
	CHECK(!FplLoadState(L"y = x", back));
	CHECK_INT(back.graphs.size(), 2);		// unchanged by a failed load
	// a plot built from a loaded state
	FplPlot plot;
	plot.graphs = back.graphs;
	plot.graphs[1].text = L"y = a x + mean(x1)";
	plot.params = back.params;
	plot.lists = back.lists;
	plot.Build();
	CHECK(!plot.graphs[1].expr.error);
	CHECK_NEAR_TOL(plot.Eval(1, 3), 1 + (1.5 - 2 + 1e-300) / 3, 1e-12);
	CHECK(plot.ListValues(L"x1") != nullptr);
}

TEST(ZoomAndFit) {
	// a rectangle on the screen, corners either way round
	auto v = View();
	CHECK(v.ZoomTo(v.ToScreen({ 4, -1 }), v.ToScreen({ -2, 3 })));
	CHECK_NEAR(v.xMin, -2);
	CHECK_NEAR(v.xMax, 4);
	CHECK_NEAR(v.yMin, -1);
	CHECK_NEAR(v.yMax, 3);
	CHECK(!v.ZoomTo({ 10, 10 }, { 10.5, 200 }));
	CHECK_NEAR(v.xMin, -2);

	// points exactly, with room around
	v = View();
	double x0, x1, y0, y1;
	auto plot = MakePlot({ L"(1, 2)", L"(3, -4)" });
	plot.Sample(v);
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x0 < 1 && x0 > 0.5 && x1 > 3 && x1 < 3.5);
	CHECK(y0 < -4 && y1 > 2);
	// a curve over its range; a function's values over the view's x, an asymptote not stretching it
	plot = MakePlot({ L"(30cos t, 2sin t)" });
	plot.Sample(v);
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x1 > 30 && x1 < 36 && y1 > 2 && y1 < 3);
	plot = MakePlot({ L"y = 1/x" });
	plot.Sample(v);
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x0 < -10 && x1 > 10);
	CHECK(y1 < 50 && y0 > -50);
	// one point: some size around it; nothing: false
	plot = MakePlot({ L"(5, 5)" });
	plot.Sample(v);
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x0 < 5 && x1 > 5 && x1 - x0 >= 2);
	plot = MakePlot({ L"c = 2", L"y = 2 +" });
	plot.Sample(v);
	CHECK(!plot.FitBounds(v, x0, x1, y0, y1));
	// listed points, all of them
	plot = MakeDataPlot({ L"(x1, y1)" }, { { L"x1", { 0, 100 } }, { L"y1", { -50, 7 } } });
	plot.Sample(v);
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x1 > 100 && y0 < -50);
}

TEST(Labels) {
	CHECK_STR(FplFormatLabel(L"A({x}, {y})", 1.25, -2, 0.025), L"A(1.25, \x2212" L"2)");
	CHECK_STR(FplFormatLabel(L"{x}{x} {z}", 2, 0, 1), L"22 {z}");
	auto v = View();
	auto plot = MakePlot({ L"(1, 2)", L"y = x/2", L"(30, 1)", L"y = x" });
	plot.graphs[0].label = L"P {y}";
	plot.graphs[1].label = L"half";
	plot.graphs[2].label = L"out of view";
	plot.Sample(v);
	auto places = plot.LabelPlaces(v);
	CHECK_INT(places.size(), 2);
	CHECK_INT(places[0].graph, 0);
	CHECK_STR(places[0].text, L"P 2");
	CHECK_NEAR(places[0].at.x, v.ToScreenX(1));
	CHECK_INT(places[1].graph, 1);
	CHECK_NEAR_TOL(places[1].at.x, v.width, 1);		// the curve's right end in the view
}

TEST(History) {
	FplSnapshot a, b, c;
	a.params = { { L"a", 1 } };
	b.params = { { L"a", 2 } };
	c.params = { { L"a", 3 } };
	FplHistory h(3);
	CHECK(!h.CanUndo());
	h.Push(a);
	h.Push(a);		// the same again: one step
	CHECK_INT(h.Steps(), 1);
	h.Push(b);
	FplSnapshot to;
	CHECK(h.Undo(c, to));
	CHECK(to == b);
	CHECK(h.CanRedo());
	CHECK(h.Undo(b, to));
	CHECK(to == a);
	CHECK(!h.Undo(a, to));
	CHECK(h.Redo(a, to));
	CHECK(to == b);
	CHECK(h.Redo(b, to));
	CHECK(to == c);
	CHECK(!h.CanRedo());
	// a new step drops what could be redone
	h.Undo(c, to);
	h.Push(a);
	CHECK(!h.CanRedo());
	// a step that changed nothing is skipped
	FplHistory k;
	k.Push(a);
	k.Push(b);
	CHECK(k.Undo(b, to));
	CHECK(to == a);
	// the limit
	FplHistory limited(2);
	limited.Push(a);
	limited.Push(b);
	limited.Push(c);
	CHECK_INT(limited.Steps(), 2);
	limited.Clear();
	CHECK(!limited.CanUndo());
	FplSnapshot v1, v2;
	v2.view[0] = 1;
	CHECK(!(v1 == v2));
}

TEST(StylesOut) {
	auto v = View();
	auto plot = MakePlot({ L"y = x", L"(1, 1)", L"x^2 + y^2 < 4" });
	plot.graphs[0].flags = FplGraphDotted;
	plot.graphs[0].opacity = 0.5f;
	plot.graphs[0].label = L"diagonal";
	plot.graphs[1].flags = FplGraphHollow;
	plot.graphs[2].fillOpacity = 0.6f;
	plot.Sample(v);
	FplSvgStyle style;
	style.graphColors = { 0x0000FF, 0x00FF00, 0xFF0000 };
	style.axisTitles[0] = L"time";
	style.axisTitles[1] = L"distance";
	std::string svg = FplWriteSvg(plot, v, style);
	CHECK(svg.find("stroke-dasharray=\"0 6.25\" stroke-opacity=\"0.5\"") != std::string::npos);
	CHECK(svg.find("fill=\"#FFFFFF\" stroke=\"#00FF00\" stroke-width=\"2\"") != std::string::npos);
	CHECK(svg.find("fill-opacity=\"0.6\"") != std::string::npos);
	CHECK(svg.find(">diagonal</text>") != std::string::npos);
	CHECK(svg.find(">time</text>") != std::string::npos);
	CHECK(svg.find(">distance</text>") != std::string::npos);

	// kept in the state; a state without them still loads
	FplSavedState st;
	st.graphs = plot.graphs;
	st.axisTitles[2] = L"height\tz";
	std::wstring text = FplSaveState(st);
	FplSavedState back;
	CHECK(FplLoadState(text, back));
	CHECK_NEAR_TOL(back.graphs[0].opacity, 0.5, 1e-6);
	CHECK_STR(back.graphs[0].label, L"diagonal");
	CHECK_INT(back.graphs[0].flags, FplGraphDotted);
	CHECK_NEAR_TOL(back.graphs[2].fillOpacity, 0.6, 1e-6);
	CHECK_NEAR(back.graphs[1].fillOpacity, -1);
	CHECK_STR(back.axisTitles[2], L"height\tz");
	CHECK(text.find(L"style\t2") == std::wstring::npos);		// defaults aren't written
}

TEST(Worker) {
	auto v = View();
	auto plot = MakePlot({ L"x^2 + y^2 = 25", L"x^2 + y^2 + z^2 = 25", L"y = x" });
	CHECK(plot.IsHeavy(0, false));
	CHECK(!plot.IsHeavy(2, false));
	CHECK(plot.IsHeavy(1, true));
	CHECK(!plot.IsHeavy(0, true));
	auto job = [&](int graph, uint64_t generation, bool mesh) {
		FplJob j;
		j.graph = graph;
		j.generation = generation;
		j.expr = plot.graphs[graph].expr;
		j.slots = plot.Slots();
		j.view = v;
		j.options = plot.OptionsFor(graph, {});
		j.mesh = mesh;
		j.cells = 24;
		return j;
	};
	// directly: what sampling gives
	auto direct = FplWorker::Run(job(0, 7, false));
	plot.SampleGraph(0, v);
	CHECK_INT(direct.generation, 7);
	CHECK_INT(direct.geometry.lines.size(), plot.graphs[0].geometry.lines.size());
	CHECK_INT(Points(direct.geometry.lines), Points(plot.graphs[0].geometry.lines));

	// on the thread: told once, results taken
	std::mutex m;
	std::condition_variable cv;
	int told = 0;
	FplWorker worker([&] {
		std::lock_guard lock(m);
		told++;
		cv.notify_all();
	});
	auto waitFor = [&](auto done) {
		std::unique_lock lock(m);
		return cv.wait_for(lock, std::chrono::seconds(10), done);
	};
	worker.Submit(job(0, 1, false));
	CHECK(waitFor([&] { return told >= 1; }));
	auto results = worker.Take();
	CHECK_INT(results.size(), 1);
	CHECK_INT(results[0].generation, 1);
	CHECK(!results[0].geometry.lines.empty());
	// many for one graph: the latest's result is the one that stays
	for (uint64_t g = 2; g <= 6; g++)
		worker.Submit(job(0, g, false));
	uint64_t latest = 0;
	for (int tries = 0; tries < 50 && latest != 6; tries++) {
		waitFor([&] { return told >= 2; });
		for (auto& r : worker.Take())
			latest = std::max(latest, r.generation);
		told = 1;
		if (latest != 6)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	CHECK_INT(latest, 6);
	// a 3D mesh
	told = 0;
	worker.Submit(job(1, 9, true));
	CHECK(waitFor([&] { return told >= 1; }));
	results = worker.Take();
	CHECK_INT(results.size(), 1);
	CHECK(results[0].mesh);
	CHECK(!results[0].triangles.Empty());
	worker.Cancel();
	CHECK(worker.Take().empty());
}

TEST(LogAxes) {
	// the mapping: a decade per 100 DIPs
	FplViewport v = View(1, 1000, 0.01, 100, 300, 400);
	v.logX = v.logY = true;
	CHECK(v.Valid());
	CHECK_NEAR_TOL(v.ToScreenX(10), 100, 1e-9);
	CHECK_NEAR_TOL(v.ToPlaneX(200), 100, 1e-9);
	CHECK_NEAR_TOL(v.ToScreenY(1), 200, 1e-9);
	CHECK_NEAR_TOL(v.ToPlaneY(100), 10, 1e-9);
	CHECK(std::isnan(v.ToScreenX(-1)));
	CHECK(std::isnan(v.ToScreenX(0)));
	CHECK_NEAR_TOL(v.DipsPerUnitX(10), 100 / (10 * std::log(10.0)), 1e-9);
	// panning and zooming by decades
	FplViewport p = v;
	p.Pan(100, 0);
	CHECK_NEAR_TOL(p.xMin, 0.1, 1e-12);
	CHECK_NEAR_TOL(p.xMax, 100, 1e-9);
	p = v;
	p.Zoom(3, 1, { 150, 200 });
	CHECK_NEAR_TOL(std::log10(p.xMax / p.xMin), 1, 1e-9);
	CHECK_NEAR_TOL(p.ToScreenX(std::pow(10.0, 1.5)), 150, 1e-9);
	p = v;
	p.Resize(600, 400);
	CHECK_NEAR_TOL(std::log10(p.xMin), -1.5, 1e-9);		// the center (10^1.5) and a decade per 100 DIPs
	CHECK_NEAR_TOL(std::log10(p.xMax / p.xMin), 6, 1e-9);
	p = v;
	p.EqualScale();
	CHECK_NEAR(p.xMin, 1.0);
	CHECK(p.ZoomTo({ 100, 100 }, { 200, 300 }));
	CHECK_NEAR_TOL(p.xMin, 10, 1e-9);
	CHECK_NEAR_TOL(p.xMax, 100, 1e-9);
	CHECK_NEAR_TOL(p.yMin, 0.1, 1e-12);
	CHECK_NEAR_TOL(p.yMax, 10, 1e-9);
	// ranges that aren't positive
	FplViewport f = View(-10, 10, -5, 5);
	f.logX = true;
	CHECK(!f.Valid());
	f.FixLogRanges();
	CHECK(f.Valid());
	CHECK_NEAR_TOL(f.xMin, 0.01, 1e-15);
	CHECK_NEAR(f.xMax, 10.0);
	CHECK_NEAR(f.yMin, -5.0);
	f = View(-10, -1);
	f.logY = true;
	f.FixLogRanges();
	CHECK(f.Valid());
	CHECK(f.yMax > f.yMin && f.yMin > 0);

	// ticks
	FplTicks t = FplMakeLogTicks(1, 1e6, 600, 90);
	CHECK(t.log);
	CHECK_INT(t.major.size(), 7);
	CHECK_INT(t.minor.size(), 48);
	CHECK_STR(FplFormatTick(1, t), L"1");
	CHECK_STR(FplFormatTick(1000, t), L"1000");
	CHECK_STR(FplFormatTick(1e5, t), std::wstring(L"10") + wchar_t(0x2075));
	t = FplMakeLogTicks(1e-10, 1e10, 600, 90);
	CHECK_INT(t.major.size(), 5);		// every 5 decades
	CHECK_NEAR(t.major[1], 1e-5);
	CHECK_STR(FplFormatTick(0.01, FplMakeLogTicks(0.001, 1, 300, 60)), L"0.01");
	CHECK_STR(FplFormatTick(1e-5, t), std::wstring(L"10") + wchar_t(0x207B) + wchar_t(0x2075));
	CHECK_INT(t.minor.size(), 16);	// the other decades
	t = FplMakeLogTicks(2, 5, 600, 90);	// under a decade: linear
	CHECK(!t.log);
	CHECK(!t.major.empty());
	CHECK(std::is_sorted(t.major.begin(), t.major.end()));
	CHECK(FplMakeTicksX(v, 90).log);
	CHECK(!FplMakeTicksX(View(), 90).log);

	// y = x^2 on log-log is a straight line, y = log x on log x too
	auto plot = MakePlot({ L"y = x^2", L"y = log x", L"y = (x - 10)^2 - 1", L"y = 1/x {0 < x}" });
	FplViewport ll = View(0.1, 100, 0.01, 1e4, 600, 600);
	ll.logX = ll.logY = true;
	plot.Sample(ll);
	auto& lines = plot.graphs[0].geometry.lines;
	CHECK_INT(lines.size(), 1);
	double worst = 0;
	for (auto& q : lines[0]) {
		// screen y = 600 - (log10 x^2 + 2) * 100, screen x = (log10 x + 1) * 200
		double expect = 600 - (2 * (q.x / 200 - 1) + 2) * 100;
		worst = std::max(worst, std::abs(q.y - expect));
	}
	CHECK(worst < 1e-6);
	FplViewport lx = View(0.1, 1000, -3, 5, 800, 400);
	lx.logX = true;
	plot.Sample(lx);
	worst = 0;
	for (auto& q : plot.graphs[1].geometry.lines[0]) {
		double expect = lx.ToScreenY((q.x / 200) - 1);
		worst = std::max(worst, std::abs(q.y - expect));
	}
	CHECK(worst < 1e-6);
	// features in log x: the roots at 9 and 11, the minimum at 10
	plot.FindFeatures(2, lx);
	CHECK(Has(plot.graphs[2].features, FplFeatureKind::Root, 9, 0, 1e-9));
	CHECK(Has(plot.graphs[2].features, FplFeatureKind::Root, 11, 0, 1e-9));
	CHECK(Has(plot.graphs[2].features, FplFeatureKind::Minimum, 10, -1, 1e-6));
	// a log y axis can't show roots
	FplViewport ly = lx;
	ly.logY = true;
	ly.yMin = 0.01, ly.yMax = 1e4;
	plot.FindFeatures(2, ly);
	CHECK(!Has(plot.graphs[2].features, FplFeatureKind::Root, 9, 0, 1e-9));
	CHECK(Has(plot.graphs[2].features, FplFeatureKind::Minimum, 10, -1, 1e-6) == false);
	// stepping goes by the screen
	auto hit = plot.Step(1, { 10, 1 }, 100, ll);
	CHECK_NEAR_TOL(hit.plane.x, std::pow(10.0, 1.5), 1e-9);

	// fitting the view on log axes: in decades, positive
	auto pts = MakePlot({ L"(1, 10)", L"(100, 1000)", L"(-5, 3)" });
	pts.Sample(ll);
	double x0, x1, y0, y1;
	CHECK(pts.FitBounds(ll, x0, x1, y0, y1));
	CHECK(x0 > 0 && x0 < 1 && x1 > 100);
	CHECK(y0 > 0 && y0 < 10 && y1 > 1000);
	CHECK_NEAR_TOL(std::log10(x1) - 2, -std::log10(x0), 1e-9);	// as much room on either side

	// a field's arrows on log axes stay finite
	auto field = MakePlot({ L"(x, y)" });
	field.Sample(ll);
	CHECK(!field.graphs[0].geometry.arrows.empty());
	bool finite = true;
	for (auto& a : field.graphs[0].geometry.arrows)
		finite = finite && std::isfinite(a.from.x) && std::isfinite(a.from.y) && std::isfinite(a.to.x) && std::isfinite(a.to.y);
	CHECK(finite);

	// the saved state keeps the scales
	FplSavedState st;
	st.view = ll;
	FplSavedState back;
	CHECK(FplLoadState(FplSaveState(st), back));
	CHECK(back.view.logX && back.view.logY);
	CHECK_NEAR_TOL(back.view.xMin, 0.1, 1e-15);
	CHECK_NEAR_TOL(back.view.yMax, 1e4, 1e-9);
	st.view = View();
	CHECK(FplLoadState(FplSaveState(st), back));
	CHECK(!back.view.logX && !back.view.logY);
	CHECK(FplSaveState(st).find(L"log ") == std::wstring::npos);
}

TEST(Odes) {
	// the text
	std::wstring_view text = L"y' = x - y, y(0) = 1";
	auto o = FplSplitOde(text);
	CHECK(o.isOde);
	CHECK_INT(o.starts.size(), 1);
	CHECK(text.substr(o.rhs, o.rhsLen) == L" x - y");
	CHECK(text.substr(o.starts[0].a, o.starts[0].aLen) == L"0");
	CHECK(text.substr(o.starts[0].b, o.starts[0].bLen) == L" 1");
	text = L"y' = y, y(0) = 1, y(f(2, 3)) = -1";
	o = FplSplitOde(text);
	CHECK_INT(o.starts.size(), 2);
	CHECK(text.substr(o.starts[1].a, o.starts[1].aLen) == L"f(2, 3)");
	CHECK(text.substr(o.starts[1].b, o.starts[1].bLen) == L" -1");
	CHECK(text.substr(o.starts[0].b, o.starts[0].bLen) == L" 1");
	CHECK(o.errorAt == std::wstring_view::npos);
	CHECK(FplSplitOde(L"dy/dx = y").isOde);
	CHECK(FplSplitOde(std::wstring(L"y") + wchar_t(0x2032) + L" = y").isOde);
	CHECK(!FplSplitOde(L"y = x").isOde);
	CHECK(!FplSplitOde(L"y' x").isOde);
	o = FplSplitOde(L"y' = {x < 0: 1, 2}");
	CHECK(o.isOde && o.starts.empty());
	CHECK_INT(o.rhsLen, 14);
	CHECK(FplSplitOde(L"y' = x, z").errorAt == 8);
	o = FplSplitOde(L"y' = x, y(0 = 1");
	CHECK(o.missingClose);

	// building: errors in their places
	std::vector<std::wstring> params = { L"a", L"b" };
	auto e = FplBuild(L"y' = x - y, y(a) = b", params);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::SlopeField);
	CHECK(!e.f.Empty());
	CHECK_INT(e.starts.size(), 2);
	e = FplBuild(L"y' = y", params);
	CHECK(!e.error && e.starts.empty());
	e = FplBuild(L"y' = y, y(0) = 1, y(0) = -1, y(1) = a", params);
	CHECK(!e.error);
	CHECK_INT(e.starts.size(), 6);
	e = FplBuild(L"y' = y, y(0) = 1, y(t) = 2", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	CHECK_INT(e.error.pos, 20);
	CHECK(e.starts.empty());
	e = FplBuild(L"y' = t", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	CHECK_INT(e.error.pos, 5);
	CHECK(e.error.name == L"t");
	e = FplBuild(L"y' = x, y(x) = 1", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	CHECK_INT(e.error.pos, 10);
	e = FplBuild(L"y' = x + q", params);
	CHECK(e.error.code == FplErrorCode::UnknownName);
	CHECK_INT(e.error.pos, 9);
	CHECK(e.error.name == L"q");
	e = FplBuild(L"y' = x = 2", params);
	CHECK(e.error.code == FplErrorCode::MisplacedRelation);
	e = FplBuild(L"y' = x, w", params);
	CHECK(e.error.code == FplErrorCode::UnexpectedToken);
	CHECK_INT(e.error.pos, 8);

	// solutions against the exact ones
	auto worst = [](const FplPolyline& line, const FplViewport& v, auto exact) {
		double w = 0;
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			w = std::max(w, std::abs(q.y - exact(q.x)));
		}
		return w;
	};
	FplViewport v = View(-2, 3, -1, 25, 500, 500);
	std::function<double(double, double)> grow = [](double, double y) { return y; };
	auto line = FplSolveOde(grow, 0, 1, v);
	CHECK(line.size() > 100);
	CHECK(worst(line, v, [](double x) { return std::exp(x); }) < 1e-4);
	CHECK(v.ToPlaneX(line.front().x) < -2 && v.ToPlaneX(line.back().x) > 3);
	std::function<double(double, double)> linear = [](double x, double y) { return x - y; };
	line = FplSolveOde(linear, 0, 1, v);
	CHECK(worst(line, v, [](double x) { return x - 1 + 2 * std::exp(-x); }) < 1e-3);
	// a circle: it stops where the slope is infinite
	v = View(-3, 3, -3, 3, 600, 600);
	std::function<double(double, double)> circle = [](double x, double y) { return -x / y; };
	line = FplSolveOde(circle, 0, 2, v);
	double reach = 0, off = 0;
	for (auto& p : line) {
		FplPoint q = v.ToPlane(p);
		reach = std::max(reach, std::abs(q.x));
		off = std::max(off, std::abs(std::hypot(q.x, q.y) - 2));
	}
	CHECK(reach > 1.99 && reach < 2.002);		// to a fifth of a DIP
	CHECK(off < 1e-3);
	// steps of at most odeStep DIPs in x
	double longest = 0;
	for (size_t k = 1; k < line.size(); k++)
		longest = std::max(longest, std::abs(line[k].x - line[k - 1].x));
	CHECK(longest <= 2 + 1e-9);
	// on a log y axis, y' = y is straight
	FplViewport lv = View(-2, 3, 0.01, 100, 500, 400);
	lv.logY = true;
	line = FplSolveOde(grow, 0, 1, lv);
	double bend = 0;
	for (auto& p : line)
		bend = std::max(bend, std::abs(p.y - lv.ToScreenY(std::exp(lv.ToPlaneX(p.x)))));
	CHECK(bend < 0.05);
	// nothing where it isn't defined
	CHECK(FplSolveOde(circle, 0, 0, v).empty());

	// the slopes: y' = 1 at equal scales is 45 degrees
	std::function<double(double, double)> one = [](double, double) { return 1.0; };
	auto marks = FplSampleSlopes(one, View(-5, 5, -5, 5, 500, 500));
	CHECK(marks.size() > 50);
	bool diagonal = true;
	for (auto& m : marks)
		diagonal = diagonal && std::abs((m.to.x - m.from.x) + (m.to.y - m.from.y)) < 1e-9 && m.to.x > m.from.x;
	CHECK(diagonal);

	// in a plot: the point, its solution, dragging it
	auto plot = MakePlot({ L"y' = x - y, y(a) = b", L"dy/dx = sin x" }, { Param(L"a", 0), Param(L"b", 1) });
	CHECK(!plot.graphs[0].expr.error && !plot.graphs[1].expr.error);
	CHECK(plot.graphs[0].Drawable());
	CHECK_INT(plot.graphs[0].dragX, 0);
	CHECK_INT(plot.graphs[0].dragY, 1);
	CHECK_INT(plot.graphs[1].dragX, -1);
	v = View(-5, 5, -5, 5, 500, 500);
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.points.size(), 1);
	CHECK_INT(plot.graphs[0].geometry.lines.size(), 1);
	CHECK(!plot.graphs[0].geometry.marks.empty());
	CHECK(plot.graphs[1].geometry.lines.empty() && !plot.graphs[1].geometry.marks.empty());
	// several solutions of one ODE
	auto three = MakePlot({ L"y' = x - y, y(0) = 1, y(0) = -1, y(-2) = 3" });
	three.Sample(v);
	CHECK_INT(three.graphs[0].geometry.points.size(), 3);
	CHECK_INT(three.graphs[0].geometry.lines.size(), 3);
	CHECK_INT(three.graphs[0].dragX, -1);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 0, 1 }), 6), 0);
	// tracing finds the solution
	auto hit = plot.Nearest(v.ToScreen({ 1, 2 * std::exp(-1.0) }), v, 6, 6);
	CHECK_INT(hit.graph, 0);
	double x0, x1, y0, y1;
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x0 < -3 && x1 > 5);		// backward it leaves the screen near -3
	// a definition by that name isn't taken for one
	plot = MakePlot({ L"y' = k y, y(0) = 1", L"k = 0.5" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(!plot.graphs[1].expr.error && plot.graphs[1].definition);
}

TEST(CurveIntersections) {
	FplViewport v = View(-10, 10, -10, 10, 800, 800);
	auto meetings = [&](FplPlot& plot, int graph) {
		plot.Sample(v);
		plot.FindFeatures(graph, v);
		std::vector<FplFeature> out;
		for (auto& f : plot.graphs[graph].features)
			if (f.kind == FplFeatureKind::Intersection)
				out.push_back(f);
		return out;
	};
	const double r3 = std::sqrt(3.0), r8 = std::sqrt(8.0);

	// two equations, to the numbers
	auto plot = MakePlot({ L"x^2 + y^2 = 16", L"x^2 - y^2 = 8" });
	auto m = meetings(plot, 0);
	CHECK_INT(m.size(), 4);
	// x^2 = 12, y^2 = 4
	for (auto& f : m) {
		CHECK_NEAR_TOL(std::abs(f.x), 2 * r3, 1e-9);
		CHECK_NEAR_TOL(std::abs(f.y), 2, 1e-9);
	}
	CHECK(m[0].x < 0 && m[0].y < m[1].y && m[3].x > 0 && m[2].y < m[3].y);
	// the other graph has them too
	CHECK_INT(meetings(plot, 1).size(), 4);

	// a function and a circle, on the function too
	plot = MakePlot({ L"y = x", L"x^2 + y^2 = 8" });
	m = meetings(plot, 0);
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Intersection, 2, 2, 1e-9));
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Intersection, -2, -2, 1e-9));
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Root, 0, 0, 1e-9));
	m = meetings(plot, 1);
	CHECK_INT(m.size(), 2);
	// x = f(y) against y = f(x)
	plot = MakePlot({ L"x = y^2", L"y = x - 2" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 2);
	CHECK(Has(m, FplFeatureKind::Intersection, 1, -1, 1e-9));
	CHECK(Has(m, FplFeatureKind::Intersection, 4, 2, 1e-9));
	// parametric against an equation, polar against a function
	plot = MakePlot({ L"(3cos t, 3sin t)", L"y = 0" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 2);
	CHECK(Has(m, FplFeatureKind::Intersection, -3, 0, 1e-9));
	CHECK(Has(m, FplFeatureKind::Intersection, 3, 0, 1e-9));
	plot = MakePlot({ L"r = 4", L"x = 2" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 2);
	CHECK(Has(m, FplFeatureKind::Intersection, 2, 2 * r3, 1e-9));
	CHECK(Has(m, FplFeatureKind::Intersection, 2, -2 * r3, 1e-9));
	// two parametric curves, each with its own parameter
	plot = MakePlot({ L"(4cos t, 4sin t)", L"(t - 3, t - 3)" });
	plot.graphs[1].hasRange = true;
	plot.graphs[1].tMin = -5;
	plot.graphs[1].tMax = 10;
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 2);
	CHECK(Has(m, FplFeatureKind::Intersection, r8, r8, 1e-9));
	CHECK(Has(m, FplFeatureKind::Intersection, -r8, -r8, 1e-9));
	// an ODE's solution by its lines: y = 2 e^(-x) meets y = 1 at ln 2
	plot = MakePlot({ L"y' = -y, y(0) = 2", L"y = 1" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 1);
	CHECK_NEAR_TOL(m[0].x, std::log(2.0), 1e-3);
	CHECK_NEAR_TOL(m[0].y, 1, 1e-3);
	CHECK_INT(meetings(plot, 1).size(), 1);
	// regions' boundaries count
	plot = MakePlot({ L"x^2 + y^2 < 16", L"y = 0" });
	CHECK_INT(meetings(plot, 0).size(), 2);

	// touching without crossing, to about a DIP (40 DIPs a unit here)
	plot = MakePlot({ L"x^2 + y^2 = 4", L"y = 2" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 1);
	if (!m.empty()) {
		CHECK_NEAR_TOL(m[0].x, 0, 0.05);
		CHECK_NEAR_TOL(m[0].y, 2, 0.05);
	}
	// a near miss isn't a meeting; nor one out of the view; nor the same curve twice
	plot = MakePlot({ L"x^2 + y^2 = 4", L"y = 2.1" });
	CHECK_INT(meetings(plot, 0).size(), 0);
	plot = MakePlot({ L"(x - 20)^2 + y^2 = 16", L"y = 0" });
	CHECK_INT(meetings(plot, 0).size(), 0);
	plot = MakePlot({ L"x^2 + y^2 = 16", L"r = 4" });
	CHECK_INT(meetings(plot, 0).size(), 0);
	// two lines at a shallow angle do meet, though they run close for hundreds of DIPs
	plot = MakePlot({ L"y = x/1000 + 0.001", L"x = 1000y" });
	m = meetings(plot, 1);
	CHECK(m.empty());		// parallel: within a third of a DIP all along, no crossing
	plot = MakePlot({ L"x/1000 - y = 0", L"y = 0" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 1);
	if (!m.empty())
		CHECK_NEAR_TOL(m[0].x, 0, 1e-6);

	// a vertical line through a circle: Ctrl+arrows go between the two at one x
	plot = MakePlot({ L"x^2 + y^2 = 16", L"x = 0" });
	m = meetings(plot, 0);
	CHECK_INT(m.size(), 2);
	auto hit = plot.NextFeature(0, -5, 1, v);
	CHECK(hit && hit.plane.y < 0);
	hit = plot.NextFeature(0, hit.plane.x, 1, v, hit.plane.y);
	CHECK(hit && hit.plane.y > 0);
	CHECK(!plot.NextFeature(0, hit.plane.x, 1, v, hit.plane.y));
	CHECK(plot.NextFeature(0, hit.plane.x, -1, v, hit.plane.y).plane.y < 0);

	// flags: none asked for, a hidden one, a listed one
	plot = MakePlot({ L"x^2 + y^2 = 16", L"y = 0" });
	plot.graphs[0].flags |= FplGraphNoFeatures;
	CHECK_INT(meetings(plot, 0).size(), 0);
	plot = MakePlot({ L"x^2 + y^2 = 16", L"y = 0" });
	plot.graphs[1].flags |= FplGraphHidden;
	CHECK_INT(meetings(plot, 0).size(), 0);
	// on log axes
	FplViewport lv = View(0.1, 100, 0.1, 100, 600, 600);
	lv.logX = lv.logY = true;
	plot = MakePlot({ L"x y = 10", L"y = x" });
	plot.Sample(lv);
	plot.FindFeatures(0, lv);
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Intersection, std::sqrt(10.0), std::sqrt(10.0), 1e-9));
	// many curves stay cheap, and a busy pair isn't lost
	plot = MakePlot({ L"sin x + cos y = 0.5", L"x^2 + y^2 = 16" });
	CHECK(meetings(plot, 1).size() >= 2);
}

TEST(SurfaceLines) {
	FplBox3 box{ -10, 10, -10, 10, -5, 5 };
	// at the ticks: an integer at each tick's place in the cube, none between
	auto grid = FplMakeLineGrid(box, true, true, true, false);
	CHECK(grid.Any());
	auto t = [&](int k, float cube) { return (cube + 1) * grid.scale[k] + grid.offset[k]; };
	auto ticks = FplMakeTicks(box.xMin, box.xMax, 320, 64);
	CHECK(!ticks.major.empty());
	bool onLines = true;
	for (double x : ticks.major) {
		const float v = t(0, box.ToBox(x, 0, 0).x);
		onLines = onLines && std::abs(v - std::round(v)) < 1e-4f;
	}
	CHECK(onLines);
	const float between = t(0, box.ToBox(ticks.major[0] + ticks.step / 2, 0, 0).x);
	CHECK_NEAR_TOL(std::abs(between - std::floor(between)), 0.5, 1e-4);
	// a far off range keeps its precision: the offset is only a fraction
	FplBox3 far2{ 1e6 + 0.25, 1e6 + 10.25, -1, 1, -1, 1 };
	grid = FplMakeLineGrid(far2, true, false, false, false);
	CHECK(grid.offset[0] >= 0 && grid.offset[0] < 1);
	CHECK(grid.scale[0] > 0 && grid.scale[0] < 100);
	CHECK(grid.scale[1] == 0 && grid.scale[2] == 0);
	// height lines: closer than the z ticks
	auto mesh = FplMakeLineGrid(box, false, false, true, false);
	auto levels = FplMakeLineGrid(box, false, false, false, true);
	CHECK(levels.scale[2] > mesh.scale[2]);
	CHECK(levels.scale[0] == 0);
	CHECK(!FplLineGrid{}.Any());
	CHECK(!FplMakeLineGrid(FplBox3{ 1, 1, 0, 1, 0, 1 }, true, true, true, true).Any());

	// a grid mesh's vertices know their cells
	auto m = FplSurfaceMesh([](double x, double y) { return x + y; }, box, 8);
	CHECK_INT(m.vertices.size(), 81);
	CHECK_NEAR(m.vertices[0].u, 0.f);
	CHECK_NEAR(m.vertices[8].u, 8.f);
	CHECK_NEAR(m.vertices[9].v, 1.f);
	CHECK_NEAR(m.vertices[80].v, 8.f);

	// by the graphs' flags and kinds
	auto plot = MakePlot({ L"z = x y", L"(cos u, sin u, v)", L"x^2 + y^2 + z^2 = 9", L"y = x", L"(t, t, t)" });
	for (int i = 0; i < 5; i++)
		CHECK(!plot.LineGridOf(i, box).Any());
	for (auto& g : plot.graphs)
		g.flags |= FplGraphMeshLines;
	auto surface = plot.LineGridOf(0, box);
	CHECK(surface.scale[0] > 0 && surface.scale[1] > 0 && surface.scale[2] == 0 && surface.cells == 0);
	auto parametric = plot.LineGridOf(1, box);
	CHECK(parametric.cells > 0 && parametric.scale[0] == 0);
	auto implicit = plot.LineGridOf(2, box);
	CHECK(implicit.scale[0] > 0 && implicit.scale[1] > 0 && implicit.scale[2] > 0);
	CHECK(!plot.LineGridOf(3, box).Any());
	CHECK(!plot.LineGridOf(4, box).Any());
	for (auto& g : plot.graphs)
		g.flags = FplGraphLevelLines;
	surface = plot.LineGridOf(0, box);
	CHECK(surface.scale[2] > 0 && surface.scale[0] == 0);
	CHECK(plot.LineGridOf(1, box).scale[2] > 0 && plot.LineGridOf(1, box).cells == 0);

	// the flags and the opacity are kept by the saved state
	FplSavedState st;
	FplGraph g;
	g.id = 1;
	g.text = L"z = x y";
	g.flags = FplGraphMeshLines | FplGraphLevelLines;
	g.opacity = 0.5f;
	st.graphs.push_back(g);
	FplSavedState back;
	CHECK(FplLoadState(FplSaveState(st), back));
	CHECK_INT(back.graphs.size(), 1);
	CHECK_INT(back.graphs[0].flags, FplGraphMeshLines | FplGraphLevelLines);
	CHECK_NEAR_TOL(back.graphs[0].opacity, 0.5, 1e-6);
}

TEST(Shaders) {
	// the failure this catches shows nowhere else: a shader that doesn't compile leaves every plot in 2D
	Microsoft::WRL::ComPtr<ID3DBlob> vs;
	for (auto& e : FplShaderEntries) {
		std::string errors;
		auto code = FplCompileShader(e.entry, e.target, &errors);
		CHECK(code != nullptr);
		if (!code)
			printf("  %s: %s\n", e.entry, errors.c_str());
		if (std::string(e.entry) == "VS")
			vs = code;
	}
	// a bad entry says so
	std::string errors;
	CHECK(FplCompileShader("NoSuchEntry", "ps_4_0", &errors) == nullptr);
	CHECK(!errors.empty());
	// the vertices fit the vertex shader, and every shader makes one, on the software device
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
		device.GetAddressOf(), nullptr, nullptr);
	CHECK(SUCCEEDED(hr));
	if (FAILED(hr) || !vs)
		return;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
	CHECK(SUCCEEDED(device->CreateInputLayout(FplVertexLayout, FplVertexLayoutCount, vs->GetBufferPointer(),
		vs->GetBufferSize(), layout.GetAddressOf())));
	for (auto& e : FplShaderEntries) {
		auto code = FplCompileShader(e.entry, e.target);
		if (!code)
			continue;
		if (e.target[0] == 'v') {
			Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
			CHECK(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf())));
		}
		else {
			Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
			CHECK(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, shader.GetAddressOf())));
		}
	}
}

TEST(LogAxesKinds) {
	// what's drawn from the view's cells on log axes
	FplViewport v = View(0.1, 100, 0.1, 100, 600, 600);
	v.logX = v.logY = true;
	// an implicit curve lies on itself: x y = 10 is a straight line here
	auto plot = MakePlot({ L"x y = 10", L"y < x/2", L"x^2 + y^2 < 25", L"z = log(x) log(y)", L"(y, x)" });
	plot.graphs[3].flags |= FplGraphHeatmap;
	plot.Sample(v);
	double worst = 0;
	CHECK(!plot.graphs[0].geometry.lines.empty());
	for (auto& line : plot.graphs[0].geometry.lines)
		for (auto& p : line) {
			if (p.x < 0 || p.x > 600 || p.y < 0 || p.y > 600)
				continue;
			FplPoint q = v.ToPlane(p);
			worst = std::max(worst, std::abs(q.x * q.y - 10));
		}
	CHECK(worst < 1e-3);
	// a function's region is cut along its curve, a circle's reaches the low edges
	CHECK(!plot.graphs[1].geometry.fills.empty());
	CHECK(!plot.graphs[2].geometry.fills.empty());
	CHECK(!plot.graphs[2].geometry.lines.empty());
	// the heat map's value at a cell is the function's at that place on the log axes
	auto& heat = plot.graphs[3].geometry.heatmap;
	CHECK(!heat.Empty());
	CHECK(heat.lo < 0 && heat.hi > 0);
	const int r = heat.rows / 4, c = heat.cols * 3 / 4;
	const double hx = v.ToPlaneX((c + 0.5) * heat.cell), hy = v.ToPlaneY((r + 0.5) * heat.cell);
	CHECK_NEAR_TOL(heat.values[(size_t)r * heat.cols + c], std::log10(hx) * std::log10(hy), 1e-4);
	// contours: each on its level
	CHECK(!plot.graphs[3].geometry.contours.empty());
	worst = 0;
	for (auto& contour : plot.graphs[3].geometry.contours)
		for (auto& line : contour.lines)
			for (auto& p : line) {
				if (p.x < 0 || p.x > 600 || p.y < 0 || p.y > 600)
					continue;
				FplPoint q = v.ToPlane(p);
				worst = std::max(worst, std::abs(std::log10(q.x) * std::log10(q.y) - contour.level));
			}
	CHECK(worst < 1e-2);
	// a field's arrows are even on the screen
	auto& arrows = plot.graphs[4].geometry.arrows;
	CHECK(arrows.size() > 16);
	std::vector<double> xs;
	for (auto& a : arrows)
		xs.push_back(std::round(((a.from.x + a.to.x) / 2) * 100) / 100);
	std::sort(xs.begin(), xs.end());
	xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
	double smallest = 1e9, largest = 0;
	for (size_t k = 1; k < xs.size(); k++) {
		smallest = std::min(smallest, xs[k] - xs[k - 1]);
		largest = std::max(largest, xs[k] - xs[k - 1]);
	}
	CHECK(smallest > 0);
	CHECK(largest < smallest * 1.01);
	// a polar curve leaves a log y axis where y isn't positive
	FplViewport lv = View(-10, 10, 0.01, 1000, 600, 600);
	lv.logY = true;
	auto polar = MakePlot({ L"r = 3" });
	polar.Sample(lv);
	CHECK(!polar.graphs[0].geometry.lines.empty());
	bool finite = true;
	for (auto& line : polar.graphs[0].geometry.lines)
		for (auto& p : line)
			finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
	CHECK(finite);
}

TEST(BetweenCurves) {
	// kinds, which side is which, however it's written
	std::vector<std::wstring> none;
	auto e = FplBuild(L"x^2 < y < 4", none);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::Between);
	CHECK(e.name == L"y");
	CHECK(e.rel == FplRel::Lt);
	double slots[FplFirstParam] = {};
	slots[FplSlotX] = 1;
	CHECK_NEAR(e.f.Eval(slots), 1.0);
	CHECK_NEAR(e.g.Eval(slots), 4.0);
	e = FplBuild(L"4 > y > x^2", none);		// the same, downward
	CHECK(!e.error && e.kind == FplKind::Between);
	CHECK_NEAR(e.f.Eval(slots), 1.0);
	CHECK_NEAR(e.g.Eval(slots), 4.0);
	e = FplBuild(std::wstring(L"0 \x2264 y < 1"), none);
	CHECK(!e.error && e.rel == FplRel::Le);
	e = FplBuild(L"y^2 < x < 4", none);
	CHECK(!e.error && e.kind == FplKind::Between);
	CHECK(e.name == L"x");
	e = FplBuild(L"1 < x < 3", none);		// a band
	CHECK(!e.error && e.kind == FplKind::Between);
	// the sides may only use the other variable
	e = FplBuild(L"y < y < 2", none);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	CHECK(e.error.name == L"y");
	CHECK_INT(e.error.pos, 0);
	e = FplBuild(L"t < y < 2", none);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	e = FplBuild(L"1 < y < q", none);
	CHECK(e.error.code == FplErrorCode::UnknownName);
	// it prints so it reads back
	auto parsed = FplParse(L"x^2 < y < 4");
	CHECK(!parsed.error);
	auto again = FplParse(FplToString(parsed.ast, parsed.root));
	CHECK(!again.error);
	CHECK(again.ast[again.root].op == FplOp::And);

	// the fill and its sides: the parabola's cap from -2 to 2, its ends where the sides meet
	FplViewport v = View(-5, 5, -5, 5, 500, 500);
	auto plot = MakePlot({ L"x^2 < y < 4", L"1 < x < 3", L"y = 5" });
	plot.Sample(v);
	auto& g = plot.graphs[0].geometry;
	CHECK_INT(g.fills.size(), 1);
	CHECK(g.dashed);
	CHECK_INT(g.lines.size(), 2);
	double minX = 1e9, maxX = -1e9;
	bool within = true;
	for (auto& p : g.fills[0]) {
		FplPoint q = v.ToPlane(p);
		minX = std::min(minX, q.x);
		maxX = std::max(maxX, q.x);
		within = within && q.y >= q.x * q.x - 1e-6 && q.y <= 4 + 1e-6;
	}
	CHECK(within);
	CHECK_NEAR_TOL(minX, -2, 1e-6);
	CHECK_NEAR_TOL(maxX, 2, 1e-6);
	// the band runs the view's whole height
	auto& band = plot.graphs[1].geometry;
	CHECK_INT(band.fills.size(), 1);
	CHECK_INT(band.lines.size(), 2);
	double minY = 1e9, maxY = -1e9;
	for (auto& p : band.fills[0]) {
		FplPoint q = v.ToPlane(p);
		minY = std::min(minY, q.y);
		maxY = std::max(maxY, q.y);
		within = within && q.x >= 1 - 1e-6 && q.x <= 3 + 1e-6;
	}
	CHECK(within);
	CHECK_NEAR_TOL(minY, -5, 1e-6);
	CHECK_NEAR_TOL(maxY, 5, 1e-6);
	// lower over upper draws nothing; apart stretches are apart; holes in the domain break it
	plot = MakePlot({ L"4 < y < x^2 - 100", L"-sin(x) < y < sin(x)", L"-1 < y < sqrt(x)", L"x < y < x" });
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.fills.empty());
	CHECK(plot.graphs[0].geometry.lines.empty());
	CHECK_INT(plot.graphs[1].geometry.fills.size(), 2);		// 0 to pi, and -2pi+... to -pi in view
	CHECK_INT(plot.graphs[2].geometry.fills.size(), 1);
	minX = 1e9;
	for (auto& p : plot.graphs[2].geometry.fills[0])
		minX = std::min(minX, v.ToPlane(p).x);
	CHECK_NEAR_TOL(minX, 0, 1e-6);
	// not strict, not dashed; hidden, nothing
	plot = MakePlot({ L"-1 <= y <= 1" });
	plot.Sample(v);
	CHECK(!plot.graphs[0].geometry.dashed);
	// log axes: a side that isn't positive reaches the low edge
	FplViewport lv = View(0.1, 100, 0.1, 100, 600, 600);
	lv.logX = lv.logY = true;
	plot = MakePlot({ L"-1 < y < x", L"1 < y < x" });
	plot.Sample(lv);
	CHECK_INT(plot.graphs[0].geometry.fills.size(), 1);
	bool finite = true;
	for (auto& p : plot.graphs[0].geometry.fills[0])
		finite = finite && std::isfinite(p.x) && std::isfinite(p.y);
	CHECK(finite);
	// from x = 1 on only, the second
	minX = 1e9;
	for (auto& p : plot.graphs[1].geometry.fills[0])
		minX = std::min(minX, lv.ToPlane(p).x);
	CHECK_NEAR_TOL(minX, 1, 1e-6);
	// traced, intersected and fitted by its sides
	plot = MakePlot({ L"x^2 < y < 4", L"y = 2" });
	plot.Sample(v);
	auto hit = plot.Nearest(v.ToScreen({ 1, 4 }), v, 6, 6);
	CHECK_INT(hit.graph, 0);
	plot.FindFeatures(0, v);
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Intersection, std::sqrt(2.0), 2, 1e-3));
	CHECK(Has(plot.graphs[0].features, FplFeatureKind::Intersection, -std::sqrt(2.0), 2, 1e-3));
	double x0, x1, y0, y1;
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
	CHECK(x0 < -2 && x1 > 2);
	// the definitions' pass leaves it alone; a parameter may be a side
	plot = MakePlot({ L"a < y < b" }, { Param(L"a", -1), Param(L"b", 1) });
	CHECK(!plot.graphs[0].expr.error);
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.fills.size(), 1);
	// the saved state's text is read again
	FplSavedState st;
	FplGraph one;
	one.id = 1;
	one.text = L"x^2 < y < 4";
	st.graphs.push_back(one);
	FplSavedState back;
	CHECK(FplLoadState(FplSaveState(st), back));
	CHECK(back.graphs[0].text == L"x^2 < y < 4");
}

TEST(ColorScale) {
	auto t = FplMakeColorScale(-1, 1, 200);
	CHECK(t.size() >= 3);
	bool inside = true;
	for (size_t k = 0; k < t.size(); k++)
		inside = inside && t[k].fraction >= 0 && t[k].fraction <= 1 && (k == 0 || t[k].fraction > t[k - 1].fraction);
	CHECK(inside);
	// 0 in the middle, labeled as the axes are
	bool zero = false;
	for (auto& k : t)
		zero = zero || (k.text == L"0" && std::abs(k.fraction - 0.5) < 1e-9);
	CHECK(zero);
	CHECK(FplMakeColorScale(0, 1000, 200, 36).size() <= 6);
	// at least spaced as asked
	t = FplMakeColorScale(0, 1, 100, 40);
	bool spaced = true;
	for (size_t k = 1; k < t.size(); k++)
		spaced = spaced && (t[k].fraction - t[k - 1].fraction) * 100 >= 40 - 1e-9;
	CHECK(spaced);
	// a flat surface: its one value; nothing for no numbers
	t = FplMakeColorScale(3, 3, 200);
	CHECK_INT(t.size(), 1);
	CHECK_STR(t[0].text, L"3");
	t = FplMakeColorScale(-2.5, -2.5, 200);
	CHECK_STR(t[0].text, std::wstring(1, wchar_t(0x2212)) + L"2.5");
	// no nice value in the range: its ends
	t = FplMakeColorScale(1.21, 1.29, 20, 36);
	CHECK_INT(t.size(), 2);
	if (t.size() == 2) {
		CHECK_NEAR(t[0].fraction, 0.0);
		CHECK_NEAR(t[1].fraction, 1.0);
		CHECK_STR(t[1].text, L"1.29");
	}
	CHECK(FplMakeColorScale(NaN, 1, 200).empty());
	// the colors run from viridis's dark to its yellow
	const uint32_t low = FplColormap(0), high = FplColormap(1);
	CHECK((low & 0xFF) < 100 && (high & 0xFF) > 200);
}

TEST(HigherOdes) {
	// the text
	std::wstring_view text = L"y'' = -y, y(0) = 1, y'(0) = 2, y(3) = a, y'(3) = 0";
	auto o = FplSplitOde(text);
	CHECK(o.isOde);
	CHECK(o.order == FplOdeOrder::Second);
	CHECK_INT(o.starts.size(), 2);
	CHECK(text.substr(o.starts[0].more[0].at, o.starts[0].more[0].len) == L" 2");
	CHECK(text.substr(o.starts[1].b, o.starts[1].bLen) == L" a");
	CHECK(FplSplitOde(std::wstring(L"y") + wchar_t(0x2033) + L" = y").order == FplOdeOrder::Second);
	text = L"x' = y, y' = -x, (1, f(2, 3)), (0, 1)";
	o = FplSplitOde(text);
	CHECK(o.order == FplOdeOrder::System);
	CHECK(text.substr(o.rhs2, o.rhs2Len) == L" -x");
	CHECK_INT(o.starts.size(), 2);
	CHECK(text.substr(o.starts[0].b, o.starts[0].bLen) == L" f(2, 3)");
	CHECK(FplSplitOde(L"dx/dt = y, dy/dt = -x").order == FplOdeOrder::System);
	CHECK(FplSplitOde(L"y' = y").order == FplOdeOrder::First);
	CHECK(!FplSplitOde(L"x = y").isOde);

	// errors in their places
	std::vector<std::wstring> params = { L"a" };
	auto e = FplBuild(L"y'' = -y - y'/4, y(0) = a, y'(0) = 0", params);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::SecondOrder);
	CHECK_INT(e.starts.size(), 3);
	e = FplBuild(L"y'' = t", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	CHECK_INT(e.error.pos, 6);
	CHECK(e.error.name == L"t");
	e = FplBuild(L"y'' = y' + q", params);
	CHECK(e.error.code == FplErrorCode::UnknownName);
	CHECK_INT(e.error.pos, 11);
	e = FplBuild(L"y'' = y, y(0) = 1", params);
	CHECK(e.error.code == FplErrorCode::BadInitial);
	CHECK(!e.error.Message().empty());
	e = FplBuild(L"y'' = y, y(0) = 1, y'(1) = 0", params);
	CHECK(e.error.code == FplErrorCode::BadInitial);
	e = FplBuild(L"y'' = y, y(0) = 1, y(0) = 2", params);
	CHECK(e.error.code == FplErrorCode::BadInitial);
	e = FplBuild(L"x' = y", params);
	CHECK(e.error.code != FplErrorCode::None);
	e = FplBuild(L"x' = z, y' = 1", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	e = FplBuild(L"x' = y, y' = -x, (1)", params);
	CHECK(e.error.code != FplErrorCode::None);
	e = FplBuild(L"x' = y, y' = -x, (x, 1)", params);
	CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	e = FplBuild(L"x' = y, y' = -x, (a, 1)", params);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::OdeSystem);

	// second order against the exact: y'' = -y is sin and cos; y'' = y' is e^x; damped too
	FplViewport v = View(-10, 10, -10, 10, 800, 800);
	auto worst = [&](const FplPolyline& line, auto exact) {
		double w = 0;
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			w = std::max(w, std::abs(q.y - exact(q.x)));
		}
		return w;
	};
	std::function<double(double, double, double)> spring = [](double, double y, double) { return -y; };
	auto line = FplSolveOde2(spring, 0, 1, 2, v);
	CHECK(line.size() > 100);
	CHECK(worst(line, [](double x) { return std::cos(x) + 2 * std::sin(x); }) < 1e-3);
	CHECK(v.ToPlaneX(line.front().x) < -10 && v.ToPlaneX(line.back().x) > 10);
	std::function<double(double, double, double)> grow = [](double, double, double p) { return p; };
	line = FplSolveOde2(grow, 0, 1, 1, v);
	CHECK(worst(line, [](double x) { return std::exp(x); }) < 1e-3);
	// y'' = 2 is x^2 + x: through the formula's own numbers
	std::function<double(double, double, double)> two = [](double, double, double) { return 2.0; };
	line = FplSolveOde2(two, 1, 2, 3, v);
	CHECK(worst(line, [](double x) { return x * x + x; }) < 1e-4);
	// steps of at most odeStep DIPs in x
	double longest = 0;
	for (size_t k = 1; k < line.size(); k++)
		longest = std::max(longest, std::abs(line[k].x - line[k - 1].x));
	CHECK(longest <= 2 + 1e-9);
	CHECK(FplSolveOde2(grow, NaN, 1, 1, v).empty());

	// a system: x' = y, y' = -x is a circle, closed where it began, and it stops there
	std::function<FplPoint(double, double)> rotate = [](double x, double y) { return FplPoint{ y, -x }; };
	size_t used = 0;
	line = FplSolveSystem(rotate, 3, 0, v, {}, &used);
	CHECK(line.size() > 100);
	double off = 0;
	for (auto& p : line) {
		FplPoint q = v.ToPlane(p);
		off = std::max(off, std::abs(std::hypot(q.x, q.y) - 3));
	}
	CHECK(off < 1e-3);
	CHECK(std::hypot(line.front().x - line.back().x, line.front().y - line.back().y) < 2);
	CHECK(line.size() < 800);		// once around: 2 pi 120 DIPs in steps of about 2
	CHECK(used < 100000);
	// settling on a point; leaving the view; none at a rest point
	std::function<FplPoint(double, double)> sink = [](double x, double y) { return FplPoint{ -x, -y }; };
	line = FplSolveSystem(sink, 4, 3, v);
	CHECK(line.size() > 10);
	CHECK(std::hypot(line.back().x - 400, line.back().y - 400) < 2);
	std::function<FplPoint(double, double)> away = [](double x, double y) { return FplPoint{ x, y }; };
	line = FplSolveSystem(away, 1, 1, v);
	CHECK(v.ToPlaneX(line.back().x) > 10);
	CHECK(FplSolveSystem(sink, 0, 0, v).empty());
	// on log axes it's still the same curves
	FplViewport lv = View(0.1, 100, 0.1, 100, 600, 600);
	lv.logX = lv.logY = true;
	std::function<double(double, double, double)> square = [](double x, double, double) { return 2.0 + 0 * x; };
	line = FplSolveOde2(square, 1, 1, 2, lv);
	double bend = 0;
	for (auto& p : line) {
		FplPoint q = lv.ToPlane(p);
		bend = std::max(bend, std::abs(q.y - q.x * q.x) / q.y);
	}
	CHECK(bend < 1e-3);

	// in a plot: points and lines, every point dragged by its own parameters
	auto plot = MakePlot({ L"y'' = -y, y(a) = b, y'(a) = 0, y(c) = 2, y'(c) = 1", L"x' = y, y' = -x, (d, k), (5, 0)",
		L"y' = -y, y(a) = 1, y(d) = k" },
		{ Param(L"a", 0), Param(L"b", 1), Param(L"c", 4), Param(L"d", 2), Param(L"k", 3) });
	for (auto& g : plot.graphs)
		CHECK(!g.expr.error);
	CHECK(plot.graphs[0].Drawable());
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.points.size(), 2);
	CHECK_INT(plot.graphs[0].geometry.lines.size(), 2);
	CHECK_INT(plot.graphs[1].geometry.points.size(), 2);
	CHECK_INT(plot.graphs[1].geometry.lines.size(), 2);
	CHECK(!plot.graphs[1].geometry.arrows.empty());
	CHECK(plot.graphs[0].geometry.arrows.empty() && plot.graphs[0].geometry.marks.empty());
	auto pair = plot.graphs[0].DragOf(0);
	CHECK_INT(pair.first, 0);
	CHECK_INT(pair.second, 1);
	pair = plot.graphs[0].DragOf(1);
	CHECK_INT(pair.first, 2);
	CHECK_INT(pair.second, -1);		// y(c) = 2: only its x
	pair = plot.graphs[1].DragOf(0);
	CHECK_INT(pair.first, 3);
	CHECK_INT(pair.second, 4);
	pair = plot.graphs[1].DragOf(1);
	CHECK(pair.first == -1 && pair.second == -1);	// (5, 0)
	CHECK(plot.graphs[1].DragOf(7).first == -1);
	pair = plot.graphs[2].DragOf(1);		// a first-order ODE's second point
	CHECK_INT(pair.first, 3);
	CHECK_INT(pair.second, 4);
	int point = -1;
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 4, 2 }), 3, &point), 0);
	CHECK_INT(point, 1);
	point = -1;
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 2, 3 }), 3, &point), 2);	// two there: the last drawn, on top
	CHECK_INT(point, 1);
	CHECK_INT(plot.DraggableAt(v.ToScreen({ 5, 0 }), 3), -1);		// no parameters
	// a Point's own stays as it was
	auto one = MakePlot({ L"(a, 2)" }, { Param(L"a", 1) });
	one.Sample(v);
	CHECK_INT(one.graphs[0].DragOf(0).first, 0);
	CHECK(one.graphs[0].DragOf(1).first == -1);
	// tracing and meeting: a solution and a trajectory are curves
	plot = MakePlot({ L"y'' = 0, y(0) = 1, y'(0) = 1", L"x^2 + y^2 = 25", L"x' = y, y' = -x, (3, 0)" });
	plot.Sample(v);
	auto hit = plot.Nearest(v.ToScreen({ 2, 3 }), v, 6, 6);
	CHECK_INT(hit.graph, 0);
	plot.FindFeatures(0, v);
	CHECK(Count(plot.graphs[0].features, FplFeatureKind::Intersection) >= 2);
	plot.FindFeatures(2, v);
	CHECK(Count(plot.graphs[2].features, FplFeatureKind::Intersection) >= 2);
	double x0, x1, y0, y1;
	CHECK(plot.FitBounds(v, x0, x1, y0, y1));
}

TEST(Singularities) {
	FplViewport v = View(-10, 10, -10, 10, 800, 800);
	auto one = [&](const wchar_t* text) {
		auto plot = MakePlot({ text });
		plot.Sample(v);
		return plot.graphs[0].geometry;
	};
	auto xs = [&](const std::vector<double>& screens) {
		std::vector<double> out;
		for (double s : screens)
			out.push_back(v.ToPlaneX(s));
		std::sort(out.begin(), out.end());
		return out;
	};
	const double Pi2 = 1.5707963267948966;
	// poles
	auto g = one(L"y = 1/x");
	auto a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 0, 1e-9);
	CHECK(g.holes.empty());
	g = one(L"y = 1/(x - 3)^2");		// no sign change, through the power
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 3, 1e-9);
	g = one(L"y = 1/(x^2 - 2x + 1)");	// no sign change, not factored
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 1, 1e-4);
	g = one(L"y = x^-1");
	CHECK_INT(g.asymptotes.size(), 1);
	g = one(L"y = tan x");
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 6);				// odd multiples of pi/2 in -10 to 10
	if (a.size() == 6) {
		CHECK_NEAR_TOL(a[2], -Pi2, 1e-9);
		CHECK_NEAR_TOL(a[3], Pi2, 1e-9);
	}
	CHECK_INT(one(L"y = cot x").asymptotes.size(), 7);
	CHECK_INT(one(L"y = sec x").asymptotes.size(), 6);
	// one side only
	g = one(L"y = ln x");
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 0, 1e-9);
	g = one(L"y = ln(x - 2)");
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 2, 1e-9);
	// holes, at their limits
	g = one(L"y = (x^2 - 1)/(x - 1)");
	CHECK(g.asymptotes.empty());
	CHECK_INT(g.holes.size(), 1);
	if (g.holes.size() == 1) {
		FplPoint q = v.ToPlane(g.holes[0]);
		CHECK_NEAR_TOL(q.x, 1, 1e-9);
		CHECK_NEAR_TOL(q.y, 2, 1e-4);
	}
	g = one(L"y = sin(x)/x");
	CHECK_INT(g.holes.size(), 1);
	if (g.holes.size() == 1)
		CHECK_NEAR_TOL(v.ToPlane(g.holes[0]).y, 1, 1e-4);
	CHECK(g.asymptotes.empty());
	g = one(L"y = (x^2 - 4)/(x^2 - 2x)");		// a hole at 2, a pole at 0
	CHECK_INT(g.holes.size(), 1);
	if (g.holes.size() == 1) {
		FplPoint q = v.ToPlane(g.holes[0]);
		CHECK_NEAR_TOL(q.x, 2, 1e-9);
		CHECK_NEAR_TOL(q.y, 2, 1e-4);
	}
	CHECK_INT(g.asymptotes.size(), 1);
	// nothing: no zero; a jump; a value it has; swinging; out of the view
	g = one(L"y = 1/(x^2 + 1)");
	CHECK(g.asymptotes.empty() && g.holes.empty());
	g = one(L"y = x/|x|");						// differing limits
	CHECK(g.asymptotes.empty() && g.holes.empty());
	g = one(L"y = {x > 1: 1/x, x}");			// its denominator's zero in the branch not taken
	CHECK(g.asymptotes.empty() && g.holes.empty());
	g = one(L"y = {x < 1: 1/x, x}");			// but a branch taken still runs off
	a = xs(g.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 0, 1e-9);
	g = one(L"y = sin(1/x)");
	CHECK(g.asymptotes.empty() && g.holes.empty());
	g = one(L"y = 1/(x - 50)");
	CHECK(g.asymptotes.empty());
	g = one(L"y = x^2");
	CHECK(g.asymptotes.empty() && g.holes.empty());
	g = one(L"y = 2/3 + x/2");					// constant denominators
	CHECK(g.asymptotes.empty() && g.holes.empty());
	// other kinds of graph
	g = one(L"x = 1/y");
	CHECK_INT(g.asymptotes.size(), 1);
	if (g.asymptotes.size() == 1)
		CHECK_NEAR_TOL(v.ToPlaneY(g.asymptotes[0]), 0, 1e-9);
	CHECK(one(L"x y = 1").asymptotes.empty());
	// through the user's functions, parameters and definitions
	auto plot = MakePlot({ L"f(x) = 1/(x - 2)", L"f(x)", L"y = 1/(x - a)" }, { Param(L"a", 4) });
	plot.Sample(v);
	a = xs(plot.graphs[1].geometry.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 2, 1e-9);
	a = xs(plot.graphs[2].geometry.asymptotes);
	CHECK_INT(a.size(), 1);
	if (a.size() == 1)
		CHECK_NEAR_TOL(a[0], 4, 1e-9);
	// degrees
	auto deg = FplBuild(L"y = tan x", {}, true);
	CHECK(!deg.error);
	FplGeometry dg = FplSample(deg, std::vector<double>(FplFirstParam, 0.0), View(-180, 180, -10, 10, 720, 400));
	CHECK_INT(dg.asymptotes.size(), 2);
	// off by flag
	plot = MakePlot({ L"y = 1/x" });
	plot.graphs[0].flags |= FplGraphNoSingular;
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.asymptotes.empty());
	// log axes: where x is positive only
	FplViewport lv = View(0.1, 100, 0.1, 100, 600, 600);
	lv.logX = lv.logY = true;
	plot = MakePlot({ L"y = 1/(x - 10)", L"y = 1/x" });
	plot.Sample(lv);
	CHECK_INT(plot.graphs[0].geometry.asymptotes.size(), 1);
	if (!plot.graphs[0].geometry.asymptotes.empty())
		CHECK_NEAR_TOL(lv.ToPlaneX(plot.graphs[0].geometry.asymptotes[0]), 10, 1e-6);
	CHECK(plot.graphs[1].geometry.asymptotes.empty());
	// many of them at once stay cheap, and past what can be seen there are none
	auto many = one(L"y = tan(100x)");
	CHECK(many.asymptotes.size() <= 256);
	// the SVG draws them
	FplSvgStyle style;
	style.graphColors = { 0x0000FF };
	plot = MakePlot({ L"y = 1/x", L"y = sin(x)/x" });
	plot.Sample(v);
	auto svg = FplWriteSvg(plot, v, style);
	CHECK(svg.find("stroke-dasharray=\"6 4\"") != std::string::npos);
	CHECK(svg.find("<circle") != std::string::npos);
	CHECK(svg.find("\"nan") == std::string::npos && svg.find("\"-nan") == std::string::npos);
}

TEST(OdesOfAnyOrder) {
	// the text
	std::wstring_view text = L"y''' = y'' - y, y(0) = 1, y'(0) = 2, y''(0) = 3";
	auto o = FplSplitOde(text);
	CHECK(o.isOde && o.order == FplOdeOrder::Second);
	CHECK_INT(o.degree, 3);
	CHECK_INT(o.starts.size(), 1);
	CHECK_INT(o.starts[0].more.size(), 2);
	CHECK(text.substr(o.starts[0].more[1].at, o.starts[0].more[1].len) == L" 3");
	CHECK_INT(FplSplitOde(std::wstring(L"y") + wchar_t(0x2034) + L" = y").degree, 3);
	CHECK_INT(FplSplitOde(std::wstring(L"y") + wchar_t(0x2033) + wchar_t(0x2033) + L" = y").degree, 4);
	CHECK(FplSplitOde(L"y''''''= y").tooHigh);

	std::vector<std::wstring> none;
	auto e = FplBuild(L"y''' = y'' - y' + y - x, y(0) = 1, y'(0) = 2, y''(0) = 3", none);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::SecondOrder);
	CHECK_INT(e.odeOrder, 3);
	CHECK_INT(e.starts.size(), 4);
	// its derivatives are in their slots, however they're written
	double slots[FplFirstParam] = {};
	slots[FplSlotX] = 1, slots[FplSlotY] = 2, slots[FplSlotT] = 4, slots[FplSlotU] = 8;
	CHECK_NEAR(e.f.Eval(slots), 8.0 - 4 + 2 - 1);
	e = FplBuild(L"y'''' = 2y''' + y'y'' + xy", none);
	CHECK(!e.error);
	slots[FplSlotV] = 16;
	CHECK_NEAR(e.f.Eval(slots), 32.0 + 32 + 2);
	// products with them read, at the second order too
	e = FplBuild(L"y'' = 2y' + y' y + 3y'", none);
	CHECK(!e.error);
	slots[FplSlotX] = 1, slots[FplSlotY] = 2, slots[FplSlotT] = 4;
	CHECK_NEAR(e.f.Eval(slots), 8.0 + 8 + 12);
	e = FplBuild(L"y'' = x y'", none);
	CHECK(!e.error);
	CHECK_NEAR(e.f.Eval(slots), 4.0);
	e = FplBuild(std::wstring(L"y''' = y") + wchar_t(0x2033) + L" + y" + wchar_t(0x2032), none);
	CHECK(!e.error);
	CHECK_NEAR(e.f.Eval(slots), 12.0);
	// errors
	e = FplBuild(L"y'''''' = y", none);
	CHECK(e.error.code == FplErrorCode::OdeOrder);
	CHECK(!e.error.Message().empty());
	e = FplBuild(L"y''' = y'''", none);			// itself
	CHECK(e.error.code != FplErrorCode::None);
	e = FplBuild(L"y'' = y''", none);
	CHECK(e.error.code != FplErrorCode::None);
	for (const wchar_t* letter : { L"y''' = t", L"y''' = u", L"y''' = v", L"y''' = z", L"y'''' = t", L"y' = u" }) {
		e = FplBuild(letter, none);
		CHECK(e.error.code == FplErrorCode::VariableNotAllowed);
	}
	e = FplBuild(L"y''' = t", none);
	CHECK_INT(e.error.pos, 7);
	e = FplBuild(L"y''' = y'' + q", none);
	CHECK(e.error.code == FplErrorCode::UnknownName);
	CHECK_INT(e.error.pos, 13);
	e = FplBuild(L"y''' = y, y(0) = 1, y'(0) = 2", none);		// one short
	CHECK(e.error.code == FplErrorCode::BadInitial);
	e = FplBuild(L"y''' = y, y(0) = 1, y''(0) = 2, y'(0) = 3", none);	// out of order
	CHECK(e.error.code == FplErrorCode::BadInitial);
	e = FplBuild(L"y''' = y, y(0) = 1, y'(0) = 2, y''(1) = 3", none);	// another a
	CHECK(e.error.code == FplErrorCode::BadInitial);

	// against the exact: y''' = 0 a parabola, y''' = y e^x, y'''' = -4y e^-x cos x, y''''' = 0 a quartic
	FplViewport v = View(-4, 4, -8, 8, 800, 800);
	auto worst = [&](const FplPolyline& line, auto exact) {
		double w = 0;
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			w = std::max(w, std::abs(q.y - exact(q.x)));
		}
		return w;
	};
	std::function<double(double, const double*)> zero = [](double, const double*) { return 0.0; };
	const double parabola[] = { 1, 2, 4 };
	auto line = FplSolveOdeN(zero, 3, 0, parabola, v);
	CHECK(line.size() > 100);
	CHECK(worst(line, [](double x) { return 1 + 2 * x + 2 * x * x; }) < 1e-4);
	std::function<double(double, const double*)> same = [](double, const double* y) { return y[0]; };
	const double exp1[] = { 1, 1, 1 };
	line = FplSolveOdeN(same, 3, 0, exp1, v);
	CHECK(worst(line, [](double x) { return std::exp(x); }) < 1e-3);
	std::function<double(double, const double*)> fourth = [](double, const double* y) { return -4 * y[0]; };
	const double damped[] = { 1, -1, 0, 2 };
	line = FplSolveOdeN(fourth, 4, 0, damped, v);
	CHECK(line.size() > 10);
	CHECK(worst(line, [](double x) { return std::exp(-x) * std::cos(x); }) < 1e-3);
	const double quartic[] = { 0, 0, 0, 0, 24 };
	line = FplSolveOdeN(zero, 5, 0, quartic, v);
	CHECK(worst(line, [](double x) { return x * x * x * x; }) < 1e-3);
	// out of the solver's range, or not numbers
	CHECK(FplSolveOdeN(zero, 6, 0, quartic, v).empty());
	CHECK(FplSolveOdeN(zero, 0, 0, quartic, v).empty());
	const double bad[] = { 1, NaN };
	CHECK(FplSolveOdeN(zero, 2, 0, bad, v).empty());
	// a blow-up ends, rather than running forever
	std::function<double(double, const double*)> blow = [](double, const double* y) { return y[0] * y[0] * y[0]; };
	const double high[] = { 1, 5, 5 };
	size_t used = 0;
	line = FplSolveOdeN(blow, 3, 0, high, v, {}, &used);
	CHECK(line.size() > 1);
	CHECK(used < 2000000);
	// the older one is the same thing
	std::function<double(double, double, double)> spring = [](double, double y, double) { return -y; };
	CHECK(worst(FplSolveOde2(spring, 0, 1, 0, v), [](double x) { return std::cos(x); }) < 1e-3);

	// in a plot: one solution per group, each point dragged by its parameters
	auto plot = MakePlot({ L"y''' = -y', y(a) = b, y'(a) = 0, y''(a) = 1, y(1) = 2, y'(1) = 0, y''(1) = 0" },
		{ Param(L"a", 0), Param(L"b", 1) });
	CHECK(!plot.graphs[0].expr.error);
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.points.size(), 2);
	CHECK_INT(plot.graphs[0].geometry.lines.size(), 2);
	CHECK_INT(plot.graphs[0].DragOf(0).first, 0);
	CHECK_INT(plot.graphs[0].DragOf(0).second, 1);
	CHECK(plot.graphs[0].DragOf(1).first == -1);
	// y''' = -y' with y(0) = 1, y'(0) = 0, y''(0) = 1: 2 - cos x
	FplPoint q = v.ToPlane(plot.graphs[0].geometry.lines[0][plot.graphs[0].geometry.lines[0].size() / 2]);
	CHECK_NEAR_TOL(q.y, 2 - std::cos(q.x), 1e-3);
	auto traced = plot.Nearest(v.ToScreen({ 1.5, 2 - std::cos(1.5) }), v, 6, 6);
	CHECK_INT(traced.graph, 0);
}

TEST(SystemsWithTime) {
	std::vector<std::wstring> none;
	auto e = FplBuild(L"x' = y, y' = -x + cos(t), (1, 0)", none);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::OdeSystem);
	CHECK(e.g.Reads(FplSlotT));
	CHECK(!e.f.Reads(FplSlotT));
	CHECK(FplBuild(L"x' = y, y' = -x, (1, 2, 3)", none).error.code != FplErrorCode::None);

	// x' = 1, y' = cos t from the origin is y = sin x
	FplViewport v = View(-10, 10, -10, 10, 800, 800);
	std::function<FplPoint(double, double, double)> wave = [](double t, double, double) { return FplPoint{ 1, std::cos(t) }; };
	auto line = FplSolveSystemT(wave, false, 0, 0, v);
	CHECK(line.size() > 100);
	double off = 0, reach = 0;
	for (auto& p : line) {
		FplPoint q = v.ToPlane(p);
		off = std::max(off, std::abs(q.y - std::sin(q.x)));
		reach = std::max(reach, q.x);
	}
	CHECK(off < 1e-3);
	CHECK(reach > 10);
	// over a range of t only
	FplSampleOptions o;
	o.odeRange = true;
	o.tMin = 0;
	o.tMax = 2;
	line = FplSolveSystemT(wave, false, 0, 0, v, o);
	CHECK_NEAR_TOL(v.ToPlaneX(line.back().x), 2, 1e-6);
	CHECK_NEAR_TOL(v.ToPlaneX(line.front().x), 0, 1e-9);
	// it doesn't stop at a pause: x' = sin t is at rest at t = 0, and moves after
	std::function<FplPoint(double, double, double)> pause = [](double t, double, double) { return FplPoint{ std::sin(t), 0 }; };
	line = FplSolveSystemT(pause, false, 0, 0, v, o);
	CHECK(v.ToPlaneX(line.back().x) > 1);
	// nor close: x' = -sin t, y' = cos t comes back round, and carries on
	std::function<FplPoint(double, double, double)> loop = [](double t, double, double) { return FplPoint{ -3 * std::sin(t), 3 * std::cos(t) }; };
	size_t once = 0, around = 0;
	o.tMin = 0, o.tMax = 12.5;
	around = FplSolveSystemT(loop, false, 3, 0, v, o).size();
	o.tMax = 6.28;
	once = FplSolveSystemT(loop, false, 3, 0, v, o).size();
	CHECK(around > once * 3 / 2);

	// no arrows with t in it, as they'd only be true of one moment
	auto plot = MakePlot({ L"x' = y, y' = -x + cos(t), (1, 0)", L"x' = y, y' = -x, (1, 0)" });
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.arrows.empty());
	CHECK_INT(plot.graphs[0].geometry.lines.size(), 1);
	CHECK(!plot.graphs[1].geometry.arrows.empty());
	// FPLGM_RANGE reaches it
	plot.graphs[0].hasRange = true;
	plot.graphs[0].tMin = 0;
	plot.graphs[0].tMax = 1;
	CHECK(plot.OptionsFor(0, {}).odeRange);
	CHECK(!plot.OptionsFor(1, {}).odeRange);
}

TEST(Systems3D) {
	// the text
	std::wstring_view text = L"x' = y, y' = -x, z' = 1, (1, f(2, 3), 0), (0, 1, 2)";
	auto o = FplSplitOde(text);
	CHECK(o.isOde && o.order == FplOdeOrder::System3D);
	CHECK(text.substr(o.rhs3, o.rhs3Len) == L" 1");
	CHECK_INT(o.starts.size(), 2);
	CHECK(text.substr(o.starts[0].b, o.starts[0].bLen) == L" f(2, 3)");
	CHECK(text.substr(o.starts[0].more[0].at, o.starts[0].more[0].len) == L" 0");
	CHECK(FplSplitOde(L"dx/dt = y, dy/dt = -x, dz/dt = 1").order == FplOdeOrder::System3D);
	CHECK(FplSplitOde(L"x' = y, y' = -x, (1, 2)").order == FplOdeOrder::System);

	std::vector<std::wstring> params = { L"a" };
	auto e = FplBuild(L"x' = y z, y' = -x + t, z' = a, (1, 0, a)", params);
	CHECK(!e.error);
	CHECK(e.kind == FplKind::OdeSystem3D);
	CHECK_INT(e.starts.size(), 3);
	CHECK(!e.h.Empty());
	// errors
	CHECK(FplBuild(L"x' = y, y' = -x, z' = 1, (1, 2)", params).error.code != FplErrorCode::None);
	CHECK(FplBuild(L"x' = y, y' = -x, z' = 1, (1, 2, 3, 4)", params).error.code != FplErrorCode::None);
	CHECK(FplBuild(L"x' = u, y' = -x, z' = 1", params).error.code == FplErrorCode::VariableNotAllowed);
	CHECK(FplBuild(L"x' = y, y' = -x, z' = 1, (x, 2, 3)", params).error.code == FplErrorCode::VariableNotAllowed);
	CHECK(FplBuild(L"x' = y, y' = -x, z' = q", params).error.code == FplErrorCode::UnknownName);
	CHECK(FplBuild(L"x' = y, y' = -x, z' = 1, (1, 2, 3", params).error.code == FplErrorCode::MissingClose);

	FplBox3 box{ -5, 5, -5, 5, -5, 5 };
	auto plane = [&](FplVec3 c) { return FplPoint3{ -5 + (c.x + 1) * 5.0, -5 + (c.y + 1) * 5.0, -5 + (c.z + 1) * 5.0 }; };
	// a helix: x = cos t, y = sin t, z = t, exact, and out of the box's reach it ends
	std::function<FplPoint3(double, double, double, double)> helix = [](double, double x, double y, double) {
		return FplPoint3{ -y, x, 1 };
	};
	auto line = FplSolveSystem3(helix, true, { 1, 0, 0 }, box);
	CHECK(line.size() > 100);
	double off = 0;
	for (auto& p : line) {
		FplPoint3 q = plane(p);
		off = std::max({ off, std::abs(q.x - std::cos(q.z)), std::abs(q.y - std::sin(q.z)) });
	}
	CHECK(off < 1e-3);
	CHECK(std::abs(line.front().z) > 3.8 && std::abs(line.back().z) > 3.8);	// each way, to the cube's far limit
	// steps of at most about 0.02 of the cube
	double longest = 0;
	for (size_t k = 1; k < line.size(); k++) {
		const double dx = line[k].x - line[k - 1].x, dy = line[k].y - line[k - 1].y, dz = line[k].z - line[k - 1].z;
		longest = std::max(longest, std::sqrt(dx * dx + dy * dy + dz * dz));
	}
	CHECK(longest < 0.05);
	// a closed one stops where it began
	std::function<FplPoint3(double, double, double, double)> circle = [](double, double x, double y, double) {
		return FplPoint3{ -y, x, 0 };
	};
	line = FplSolveSystem3(circle, true, { 3, 0, 1 }, box);
	CHECK(line.size() > 50);
	CHECK(line.size() < 400);	// once around
	// settles; a rest point has no line; not a number has none
	std::function<FplPoint3(double, double, double, double)> sink = [](double, double x, double y, double z) {
		return FplPoint3{ -x, -y, -z };
	};
	line = FplSolveSystem3(sink, true, { 2, 2, 2 }, box);
	CHECK(line.size() > 10);
	CHECK(std::abs(line.back().x) < 0.05);
	CHECK(FplSolveSystem3(sink, true, { 0, 0, 0 }, box).empty());
	CHECK(FplSolveSystem3(sink, true, { NaN, 0, 0 }, box).empty());
	// a range of t
	FplSampleOptions ranged;
	ranged.odeRange = true;
	ranged.tMin = 0;
	ranged.tMax = 2;
	line = FplSolveSystem3(helix, true, { 1, 0, 0 }, box, ranged);
	CHECK_NEAR_TOL(plane(line.back()).z, 2, 1e-3);
	CHECK_NEAR_TOL(plane(line.front()).z, 0, 1e-3);
	// t in it: z' = t is z = t^2/2, and it doesn't stop for the pause at t = 0
	std::function<FplPoint3(double, double, double, double)> timed = [](double t, double, double, double) {
		return FplPoint3{ 0, 0, t };
	};
	line = FplSolveSystem3(timed, false, { 0, 0, 0 }, box, ranged);
	CHECK_NEAR_TOL(plane(line.back()).z, 2, 1e-3);
	// Lorenz's never ends by itself: the budget does, and it stays on its attractor, in the box
	FplBox3 big{ -25, 25, -30, 30, 0, 55 };
	std::function<FplPoint3(double, double, double, double)> lorenz = [](double, double x, double y, double z) {
		return FplPoint3{ 10 * (y - x), x * (28 - z) - y, x * y - 8 * z / 3 };
	};
	size_t used = 0;
	FplSampleOptions plain;
	plain.odeRange = true;
	plain.tMin = 0;
	plain.tMax = 1e6;
	line = FplSolveSystem3(lorenz, true, { 1, 1, 20 }, big, plain, &used);
	CHECK(line.size() > 1000);
	CHECK(line.size() < 40000);
	CHECK(used < 500000);
	bool inside = true;
	for (auto& p : line)
		inside = inside && std::abs(p.x) <= 1.2 && std::abs(p.y) <= 1.2 && std::abs(p.z) <= 1.2;
	CHECK(inside);

	// in a plot: tubes in 3D, nothing in 2D, not draggable
	auto plot = MakePlot({ L"x' = -y, y' = x, z' = 1, (1, 0, 0), (2, 0, 0)" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(plot.graphs[0].Drawable());
	plot.Sample(View());
	CHECK(plot.graphs[0].geometry.lines.empty() && plot.graphs[0].geometry.points.empty());
	plot.Build3D(box);
	CHECK(!plot.graphs[0].mesh.Empty());
	auto oneStart = MakePlot({ L"x' = -y, y' = x, z' = 1, (1, 0, 0)" });
	oneStart.Build3D(box);
	CHECK(plot.graphs[0].mesh.indices.size() > oneStart.graphs[0].mesh.indices.size());
	CHECK(plot.graphs[0].DragOf(0).first == -1 && plot.graphs[0].DragOf(0).second == -1 && plot.graphs[0].DragZOf(0) == -1);
	bool finite = true;
	for (auto& vertex : plot.graphs[0].mesh.vertices)
		finite = finite && std::isfinite(vertex.pos.x) && std::isfinite(vertex.normal.x);
	CHECK(finite);
	// no starting points: nothing to draw, and no error
	auto bare = MakePlot({ L"x' = -y, y' = x, z' = 1" });
	CHECK(!bare.graphs[0].expr.error);
	bare.Build3D(box);
	CHECK(bare.graphs[0].mesh.Empty());
}

TEST(StiffOdes) {
	FplViewport v = View(-5, 10, -3, 3, 1500, 600);
	struct Reach {
		double hi = -1e300, worst = 0;
		size_t points = 0;
	};
	auto reach = [&](const FplPolyline& line, auto exact) {
		Reach r;
		r.points = line.size();
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			r.hi = std::max(r.hi, q.x);
			if (q.x >= 0 && q.y > -3 && q.y < 3)
				r.worst = std::max(r.worst, std::abs(q.y - exact(q.x)));
		}
		return r;
	};
	// y' = -L (y - cos x) from y(0) = 1: stiff the more L is. A stable step is small but reachable: these once drew
	// nothing at all past 1e4, as cutting by the first trial's huge move went past the smallest step.
	for (double L : { 1e2, 1e3, 1e4 }) {
		std::function<double(double, double)> F = [L](double x, double y) { return -L * (y - std::cos(x)); };
		auto r = reach(FplSolveOde(F, 0, 1, v), [L](double x) { return (L * L * std::cos(x) + L * std::sin(x)) / (L * L + 1); });
		CHECK(r.hi > 10);
		CHECK(r.worst < 0.002);		// a fifth of a DIP
	}
	// starting off the slow solution, as steep as a tangent: a stiff transient falls, so it isn't one. Down to it almost
	// at once, and then along it.
	{
		const double L = 1e4;
		std::function<double(double, double)> F = [L](double x, double y) { return -L * (y - std::cos(x)); };
		auto line = FplSolveOde(F, 0, 2.5, v);
		CHECK(line.size() > 100);
		double hi = -1e300, worst = 0;
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			hi = std::max(hi, q.x);
			if (q.x > 0.01 && q.y > -3 && q.y < 3)
				worst = std::max(worst, std::abs(q.y - (L * L * std::cos(q.x) + L * std::sin(q.x)) / (L * L + 1)));
		}
		CHECK(hi > 10);
		CHECK(worst < 0.002);
		std::function<double(double, const double*)> G = [L](double, const double* y) { return -L * y[1] - y[0]; };
		const double fast[] = { 1, 5000 };			// y' falls from 5000 to the slow mode
		line = FplSolveOdeN(G, 2, 0, fast, v);
		hi = -1e300;
		for (auto& p : line)
			hi = std::max(hi, v.ToPlaneX(p.x));
		CHECK(hi > 10);
	}
	// while a jump in F, steep on both sides, doesn't end it
	std::function<double(double, double)> jump = [](double x, double) { return x < 1 ? 50.0 : -50.0; };
	auto jumped = FplSolveOde(jump, 0, -2, v);
	double beyond = -1e300;
	for (auto& p : jumped)
		beyond = std::max(beyond, v.ToPlaneX(p.x));
	CHECK(beyond > 1.05);
	// as stiff as you like: the whole view, by turning implicit
	for (double L : { 1e5, 1e6, 1e8, 1e12 }) {
		std::function<double(double, double)> F = [L](double x, double y) { return -L * (y - std::cos(x)); };
		size_t used = 0;
		bool ranOut = false;
		auto line = FplSolveOde(F, 0, 1, v, {}, &used, &ranOut);
		auto r = reach(line, [L](double x) { return (L * L * std::cos(x) + L * std::sin(x)) / (L * L + 1); });
		CHECK(r.hi > 10);
		CHECK(r.worst < 0.002);
		CHECK(!ranOut);
		CHECK(used < 200000);		// not the budget, as it was when these were explicit
	}
	// off the slow solution at the start, by far: straight down to it, then along it
	{
		const double L = 1e8;
		std::function<double(double, double)> F = [L](double x, double y) { return -L * (y - std::cos(x)); };
		auto line = FplSolveOde(F, 0, 2.5, v);
		double hi = -1e300, worst = 0;
		for (auto& p : line) {
			FplPoint q = v.ToPlane(p);
			hi = std::max(hi, q.x);
			if (q.x > 0.01 && q.y > -3 && q.y < 3)
				worst = std::max(worst, std::abs(q.y - std::cos(q.x)));
		}
		CHECK(hi > 10);
		CHECK(worst < 0.002);
	}
	// and on any view: the smallest step is recognized whatever the rounding (some widths once spent the whole budget on
	// the transient, drawing nothing, and one size passing proves little)
	{
		auto plot = MakePlot({ L"y' = -100000(y - cos x), y(0) = 2.5" });
		int bad = 0;
		for (double w = 900; w <= 1100; w += 1) {
			const FplViewport view = View(-10, 10, -8, 8, w, 800);
			plot.Sample(view);
			auto& g = plot.graphs[0].geometry;
			size_t points = 0;
			for (auto& line : g.lines)
				points += line.size();
			if (g.incomplete || points < 100 || g.evals > 100000)
				bad++;
		}
		CHECK_INT(bad, 0);
	}
	// second order: the slow mode e^(-x/L)
	for (double L : { 1e3, 1e4, 1e5, 1e8 }) {
		std::function<double(double, const double*)> F = [L](double, const double* y) { return -L * y[1] - y[0]; };
		const double start[] = { 1, -1 / L };
		bool ranOut = false;
		auto r = reach(FplSolveOdeN(F, 2, 0, start, v, {}, nullptr, &ranOut), [L](double x) { return std::exp(-x / L); });
		CHECK(r.hi > 10);
		CHECK(r.worst < 0.002);
		CHECK(!ranOut);
	}
	// a system
	for (double L : { 1e3, 1e5, 1e8 }) {
		std::function<FplPoint(double, double)> F = [L](double x, double y) { return FplPoint{ 1, -L * (y - std::sin(x)) }; };
		auto r = reach(FplSolveSystem(F, 0, 0, v), [L](double x) { return (L * L * std::sin(x) - L * std::cos(x)) / (L * L + 1); });
		CHECK(r.hi > 10);
		CHECK(r.worst < 0.002);
	}
	// and in 3D
	FplBox3 box{ -5, 5, -5, 5, -5, 5 };
	std::function<FplPoint3(double, double, double, double)> stiff3 = [](double, double x, double y, double z) {
		return FplPoint3{ 1, -1e8 * (y - std::sin(x)), 0 * z };
	};
	auto line3 = FplSolveSystem3(stiff3, true, { 0, 0, 0 }, box);
	CHECK(line3.size() > 10);
	double off3 = 0, far3 = 0;
	for (auto& p : line3) {
		const FplPoint3 q{ -5 + (p.x + 1) * 5.0, -5 + (p.y + 1) * 5.0, 0 };
		off3 = std::max(off3, q.x > 0.01 && q.x < 5 ? std::abs(q.y - std::sin(q.x)) : 0.0);
		far3 = std::max(far3, q.x);
	}
	CHECK(far3 > 5);
	CHECK(off3 < 0.01);
	// Van der Pol, the classic: fast and slow in turn, on a cycle of size about 2
	std::function<FplPoint(double, double)> vdp = [](double x, double y) { return FplPoint{ y, 1000 * (1 - x * x) * y - x }; };
	FplViewport wide = View(-3, 3, -2000, 2000, 800, 800);
	size_t vdpEvals = 0;
	auto cycle = FplSolveSystem(vdp, 2, 0, wide, {}, &vdpEvals);
	CHECK(cycle.size() > 10);
	double most = 0;
	for (auto& p : cycle)
		most = std::max(most, std::abs(wide.ToPlaneX(p.x)));
	CHECK(most < 3);
	CHECK(most > 1.9);
	CHECK(vdpEvals <= FplSampleOptions{}.maxEvals + 100);

	// not everything turns implicit: tangents and blow-ups spread apart, so those stay as they were (Odes tests them), and
	// a plain equation is as cheap as it was
	std::function<double(double, double)> plain = [](double, double y) { return -y; };
	size_t plainEvals = 0;
	FplSolveOde(plain, 0, 1, v, {}, &plainEvals);
	CHECK(plainEvals < 40000);
	// a fast oscillation isn't stiffness: nothing shrinks, so it can only run out of steps, and says so
	std::function<double(double, const double*)> fast = [](double, const double* y) { return -1e8 * y[0]; };
	const double wave[] = { 1, 0 };
	bool stopped = false;
	auto waved = FplSolveOdeN(fast, 2, 0, wave, v, {}, nullptr, &stopped);
	CHECK(waved.size() > 1);
	CHECK(stopped);
	// while reaching the view's edge, leaving it or a vertical tangent doesn't
	std::function<double(double, double)> circle = [](double x, double y) { return -x / y; };
	stopped = false;
	FplSolveOde(circle, 0, 2, View(-3, 3, -3, 3, 600, 600), {}, nullptr, &stopped);
	CHECK(!stopped);
	stopped = false;
	FplSolveOde(plain, 0, 1, v, {}, nullptr, &stopped);
	CHECK(!stopped);
	// in a plot
	auto plot = MakePlot({ L"y'' = -100000000 y, y(0) = 1, y'(0) = 0", L"y' = -y, y(0) = 1", L"y = x" });
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.incomplete);
	CHECK(!plot.graphs[1].geometry.incomplete);
	CHECK(!plot.graphs[2].geometry.incomplete);
	plot.Build3D(box);
	CHECK(plot.graphs[0].incomplete3D);
	CHECK(!plot.graphs[1].incomplete3D);
}

TEST(Odes3DAndPicking) {
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	FplCamera cam;
	const float w = 800, h = 600;
	auto pick = [&](FplPlot& plot, FplPoint3 at) {
		FplPoint s;
		FplVec3 origin, dir;
		CHECK(cam.Project(box.ToBox(at), w, h, s));
		cam.Ray(s, w, h, origin, dir);
		return plot.Pick3D(origin, dir, box);
	};
	// 2D ODEs show on the z = 0 plane
	auto plot = MakePlot({ L"y' = -y, y(0) = 3", L"y'' = -y, y(0) = 2, y'(0) = 0", L"x' = y, y' = -x, (4, 0)", L"y' = x", L"x' = y, y' = -x" });
	plot.Build3D(box);
	for (int i = 0; i < 3; i++)
		CHECK(!plot.graphs[i].mesh.Empty());
	// no solutions: nothing, as a vector field has nothing in 3D
	CHECK(plot.graphs[3].mesh.Empty());
	CHECK(plot.graphs[4].mesh.Empty());
	// on the plane, within the tube's radius or the start's dot
	const float flat = box.ToBox(0, 0, 0).z;
	bool onPlane = true;
	for (auto& vertex : plot.graphs[0].mesh.vertices)
		onPlane = onPlane && std::abs(vertex.pos.z - flat) < 0.05f;
	CHECK(onPlane);
	// following the solution: the mesh's points near y = 3 e^-x, in the box
	double off = 0;
	for (auto& vertex : plot.graphs[0].mesh.vertices) {
		const FplPoint3 q = box.ToSpace(vertex.pos);
		if (q.x > 0.5 && q.x < 5)
			off = std::max(off, std::abs(q.y - 3 * std::exp(-q.x)));
	}
	CHECK(off < 1.0);		// a tube's radius, measured up a slope of at most 3, and the dot
	// implicit curves and what's between two curves on the plane too, picked on it
	auto flatKinds = MakePlot({ L"x^2 + y^2 = 16", L"x^2 + y^2 < 4", L"x^2/4 - 3 < y < 4 - x^2/2", L"y > x" });
	flatKinds.Build3D(box);
	for (int i = 0; i < 4; i++)
		CHECK(!flatKinds.graphs[i].mesh.Empty());
	bool flatAll = true;
	for (auto& vertex : flatKinds.graphs[0].mesh.vertices)
		flatAll = flatAll && std::abs(vertex.pos.z - flat) < 0.05f;
	CHECK(flatAll);
	auto circle3 = MakePlot({ L"x^2 + y^2 = 16" });
	circle3.Build3D(box);
	auto onCircle = pick(circle3, { 4, 0, 0 });
	CHECK_INT(onCircle.graph, 0);
	if (onCircle)
		CHECK_NEAR(onCircle.z, 0.0);
	// its point moves the sphere: a start outside the box has none
	auto outside = MakePlot({ L"y'' = -y, y(50) = 2, y'(50) = 0" });
	auto inside = MakePlot({ L"y'' = -y, y(0) = 2, y'(0) = 0" });
	outside.Build3D(box);
	inside.Build3D(box);
	CHECK(inside.graphs[0].mesh.indices.size() > outside.graphs[0].mesh.indices.size());

	// hover: a 3D trajectory's tube is hit, near where it is
	auto helix = MakePlot({ L"x' = -6y/6, y' = x, z' = 1, (5, 0, 0)" });
	helix.Build3D(box);
	CHECK(!helix.graphs[0].mesh.Empty());
	const double t = 2.0;
	auto hit = pick(helix, { 5 * std::cos(t), 5 * std::sin(t), t });
	CHECK_INT(hit.graph, 0);
	if (hit) {
		CHECK_NEAR_TOL(hit.plane.x, 5 * std::cos(t), 0.5);
		CHECK_NEAR_TOL(hit.plane.y, 5 * std::sin(t), 0.5);
		CHECK_NEAR_TOL(hit.z, t, 0.5);
	}
	// away from it, nothing
	CHECK(!pick(helix, { 9, 9, 9 }));
	// the nearer of two: the same curve, with the other drawn behind it
	// 2D ODEs: on their plane exactly
	auto ode = MakePlot({ L"y' = -y/4, y(0) = 3" });
	ode.Build3D(box);
	const double x1 = 2.0, y1 = 3 * std::exp(-0.5);
	hit = pick(ode, { x1, y1, 0 });
	CHECK_INT(hit.graph, 0);
	if (hit) {
		CHECK_NEAR(hit.z, 0.0);
		CHECK_NEAR_TOL(hit.plane.x, x1, 0.5);
		CHECK_NEAR_TOL(hit.plane.y, y1, 0.5);
	}
	// not traced when it's asked not to be
	helix.graphs[0].flags |= FplGraphNoTrace;
	CHECK(!pick(helix, { 5 * std::cos(t), 5 * std::sin(t), t }));
}

TEST(PrimesOnBuiltIns) {
	std::vector<std::wstring> none;
	auto at = [&](const wchar_t* text, double x, bool degrees = false) {
		auto e = FplBuild(text, none, degrees);
		CHECK(!e.error);
		double slots[FplFirstParam] = {};
		slots[FplSlotX] = x;
		return e.error ? NaN : e.f.Eval(slots);
	};
	CHECK_NEAR_TOL(at(L"y = sin'(x)", 0.7), std::cos(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"y = sin''(x)", 0.7), -std::sin(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"y = sin'''(x)", 0.7), -std::cos(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"cos'(x)", 0.7), -std::sin(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"exp'(x)", 0.7), std::exp(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"ln'(x)", 2), 0.5, 1e-12);
	CHECK_NEAR_TOL(at(L"tan'(x)", 0.7), 1 / (std::cos(0.7) * std::cos(0.7)), 1e-12);
	CHECK_NEAR_TOL(at(std::wstring(L"sin").append(1, wchar_t(0x2032)).append(L"(x)").c_str(), 0.7), std::cos(0.7), 1e-12);
	// the derivative of the function, at its argument: no chain rule, as f'(2x)
	CHECK_NEAR_TOL(at(L"sin'(2x)", 0.7), std::cos(1.4), 1e-12);
	// without parentheses, in products, with a power
	CHECK_NEAR_TOL(at(L"sin' 2x", 0.7), std::cos(1.4), 1e-12);
	CHECK_NEAR_TOL(at(L"3sin'(x) + 1", 0.7), 3 * std::cos(0.7) + 1, 1e-12);
	CHECK_NEAR_TOL(at(L"sin'^2(x)", 0.7), std::cos(0.7) * std::cos(0.7), 1e-12);
	CHECK_NEAR_TOL(at(L"sin'(x) cos'(x)", 0.7), -std::cos(0.7) * std::sin(0.7), 1e-12);
	// degrees: as d/dx sees them
	CHECK_NEAR_TOL(at(L"sin'(x)", 60, true), std::cos(60 * Pi / 180) * Pi / 180, 1e-12);
	CHECK_NEAR_TOL(at(L"d/dx sin x", 60, true), at(L"sin'(x)", 60, true), 1e-12);
	// as d/dx does
	CHECK_NEAR_TOL(at(L"sin''(x)", 1.1), at(L"d/dx d/dx sin x", 1.1), 1e-12);
	// its argument isn't lost: another name, an expression, a nested one
	CHECK_NEAR_TOL(at(L"sin'(cos'(x))", 0.7), std::cos(-std::sin(0.7)), 1e-12);
	// errors
	CHECK(FplBuild(L"y = min'(x, 2)", none).error.code == FplErrorCode::NotDifferentiable);
	CHECK(FplBuild(L"y = sin'", none).error.code == FplErrorCode::MissingArgument);
	CHECK(FplBuild(L"y = x'", none).error.code != FplErrorCode::None);
	// and an ODE's y' next to them
	auto e = FplBuild(L"y'' = sin'(y) + y'", none);
	CHECK(!e.error);
	double slots[FplFirstParam] = {};
	slots[FplSlotY] = 0.5;
	slots[FplSlotT] = 2;
	CHECK_NEAR_TOL(e.f.Eval(slots), std::cos(0.5) + 2, 1e-12);
}

TEST(Fills3D) {
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	// the area of a mesh's triangles, in the space's units
	auto area = [&](const FplMesh& m) {
		double total = 0;
		for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
			auto a = box.ToSpace(m.vertices[m.indices[i]].pos), b = box.ToSpace(m.vertices[m.indices[i + 1]].pos),
				c = box.ToSpace(m.vertices[m.indices[i + 2]].pos);
			total += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
		}
		return total;
	};
	// a square, whichever way it winds; two apart; two overlapping count once; a hole wound the other way stays out
	FplPolygon square = { { -2, -2 }, { 2, -2 }, { 2, 2 }, { -2, 2 } };
	FplPolygon backward(square.rbegin(), square.rend());
	CHECK_NEAR_TOL(area(FplFillMesh({ square }, box)), 16, 0.5);
	CHECK_NEAR_TOL(area(FplFillMesh({ backward }, box)), 16, 0.5);
	FplPolygon far2 = { { 5, 5 }, { 7, 5 }, { 7, 7 }, { 5, 7 } };
	CHECK_NEAR_TOL(area(FplFillMesh({ square, far2 }, box)), 20, 0.7);
	FplPolygon over = { { 0, -2 }, { 4, -2 }, { 4, 2 }, { 0, 2 } };
	CHECK_NEAR_TOL(area(FplFillMesh({ square, over }, box)), 24, 0.7);
	FplPolygon hole = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
	CHECK_NEAR_TOL(area(FplFillMesh({ square, hole }, box)), 12, 0.7);
	// a triangle: the slanted edge counts
	CHECK_NEAR_TOL(area(FplFillMesh({ { { 0, 0 }, { 6, 0 }, { 0, 6 } } }, box)), 18, 0.5);
	// cut to the box; flat and on the plane; nothing for nothing
	FplPolygon large = { { -50, -50 }, { 50, -50 }, { 50, 50 }, { -50, 50 } };
	auto whole = FplFillMesh({ large }, box);
	CHECK_NEAR_TOL(area(whole), 400, 1);
	bool flat = true;
	for (auto& v : whole.vertices)
		flat = flat && std::abs(v.pos.z) < 1e-6f && std::abs(v.pos.x) <= 1.0001f && std::abs(v.pos.y) <= 1.0001f && v.normal.z == 1;
	CHECK(flat);
	CHECK(FplFillMesh({}, box).Empty());
	CHECK(FplFillMesh({ { { 1, 1 }, { 2, 1 } } }, box).Empty());
	CHECK(FplFillMesh({ square }, FplBox3{ 1, 1, 0, 1, 0, 1 }).Empty());

	// regions of every kind, through the graphs, by their true areas
	const double Pi2 = 3.141592653589793;
	struct Case { const wchar_t* text; double expected; };
	const Case cases[] = {
		{ L"x^2 + y^2 < 16", 16 * Pi2 },			// marching squares' cells
		{ L"y < 4 - x^2", 0 },						// a function's, checked below
		{ L"x^2 - 4 < y < 4 - x^2", 64.0 * 2 / 3 },	// what's between two curves: 2 (4 - x^2) over -2..2
		{ L"1 < x < 3", 40 },						// a band
		{ L"r < 5", 25 * Pi2 },						// polar
	};
	for (auto& c : cases) {
		auto plot = MakePlot({ c.text });
		CHECK(!plot.graphs[0].expr.error);
		plot.Build3D(box);
		CHECK(!plot.graphs[0].fillMesh.Empty());
		if (c.expected > 0)
			CHECK_NEAR_TOL(area(plot.graphs[0].fillMesh), c.expected, c.expected * 0.03);
	}
	// no region, no fill; hidden, none; curves keep their tubes beside it
	auto plain = MakePlot({ L"y = x", L"x^2 + y^2 = 4", L"y' = -y, y(0) = 1", L"(1, 2)" });
	plain.Build3D(box);
	for (auto& g : plain.graphs) {
		CHECK(g.fillMesh.Empty());
		CHECK(!g.mesh.Empty());
	}
	auto region = MakePlot({ L"y < x" });
	region.Build3D(box);
	CHECK(!region.graphs[0].mesh.Empty());
	region.graphs[0].flags |= FplGraphHidden;
	region.Build3D(box);
	CHECK(region.graphs[0].fillMesh.Empty());
	// a listed one's together
	auto listed = MakePlot({ L"L = [0, 1]", L"(x - 5L)^2 + y^2 < 1" });
	listed.Build3D(box);
	CHECK_NEAR_TOL(area(listed.graphs[1].fillMesh), 2 * Pi2, 0.2);
}

TEST(Dragging3D) {
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	FplCamera cam;
	const float w = 800, h = 600;
	auto params = { Param(L"a", 2), Param(L"b", 3), Param(L"c", 4), Param(L"d", -2) };
	auto plot = MakePlot({ L"(a, b, c)", L"(d, 1)", L"y' = -y, y(a) = b", L"y'' = -y, y(d) = 1, y'(d) = 0",
		L"x' = -y, y' = x, z' = 0, (a, b, c), (1, 2, 3)", L"(1, 2, 3)", L"(a, a, 2)", L"y = x", L"x^2 + y^2 = 4" }, params);
	for (auto& g : plot.graphs)
		CHECK(!g.expr.error);
	// which points each has, in which order
	auto near3 = [](FplPoint3 p, double x, double y, double z) {
		return std::abs(p.x - x) < 1e-12 && std::abs(p.y - y) < 1e-12 && std::abs(p.z - z) < 1e-12;
	};
	CHECK_INT(plot.Points3D(0).size(), 1);
	CHECK(near3(plot.Points3D(0)[0], 2, 3, 4));
	CHECK(near3(plot.Points3D(1)[0], -2, 1, 0));
	CHECK(near3(plot.Points3D(2)[0], 2, 3, 0));
	CHECK(near3(plot.Points3D(3)[0], -2, 1, 0));
	CHECK_INT(plot.Points3D(4).size(), 2);
	CHECK(near3(plot.Points3D(4)[1], 1, 2, 3));
	CHECK(plot.Points3D(7).empty());
	CHECK(plot.Points3D(8).empty());
	// what each one sets
	CHECK_INT(plot.graphs[0].DragOf(0).first, 0);
	CHECK_INT(plot.graphs[0].DragOf(0).second, 1);
	CHECK_INT(plot.graphs[0].DragZOf(0), 2);
	CHECK_INT(plot.graphs[1].DragOf(0).first, 3);
	CHECK_INT(plot.graphs[1].DragZOf(0), -1);
	CHECK_INT(plot.graphs[4].DragZOf(0), 2);
	CHECK_INT(plot.graphs[4].DragZOf(1), -1);
	CHECK_INT(plot.graphs[4].DragOf(1).first, -1);
	CHECK_INT(plot.graphs[5].DragOf(0).first, -1);
	CHECK_INT(plot.graphs[6].DragZOf(0), -1);

	// found by where they show
	auto only = MakePlot({ L"(a, b, c)", L"(1, 2, 3)", L"(d, 1)" }, params);
	auto screen = [&](FplPoint3 p) {
		FplPoint s;
		CHECK(cam.Project(box.ToBox(p), w, h, s));
		return s;
	};
	int index = -1;
	CHECK_INT(only.DraggableAt3D(screen({ 2, 3, 4 }), cam, box, w, h, 6, &index), 0);
	CHECK_INT(index, 0);
	CHECK_INT(only.DraggableAt3D(screen({ -2, 1, 0 }), cam, box, w, h, 6), 2);
	CHECK_INT(only.DraggableAt3D(screen({ 1, 2, 3 }), cam, box, w, h, 6), -1);	// no parameters
	FplPoint far3 = screen({ 2, 3, 4 });
	far3.x += 40;
	CHECK_INT(only.DraggableAt3D(far3, cam, box, w, h, 6), -1);
	// not when it's asked not to be, nor outside the box
	only.graphs[0].flags |= FplGraphNoDrag;
	CHECK_INT(only.DraggableAt3D(screen({ 2, 3, 4 }), cam, box, w, h, 6), -1);
	only.graphs[0].flags = 0;
	FplBox3 tiny{ -1, 1, -1, 1, -1, 1 };
	CHECK(only.DraggableAt3D(screen({ 2, 3, 4 }), cam, tiny, w, h, 6) != 0);

	// a ray through a point on the plane comes back as that point
	auto through = [&](FplPoint3 p, FplVec3& origin, FplVec3& dir) {
		FplPoint s;
		cam.Project(box.ToBox(p), w, h, s);
		cam.Ray(s, w, h, origin, dir);
	};
	FplVec3 origin, dir;
	through({ 5, -3, 2 }, origin, dir);
	FplPoint3 at;
	CHECK(FplDragOnPlane(origin, dir, box, 2, at));
	CHECK_NEAR_TOL(at.x, 5, 1e-3);
	CHECK_NEAR_TOL(at.y, -3, 1e-3);
	CHECK_NEAR_TOL(at.z, 2, 1e-12);
	// the height nearest the ray, on the upright line through it
	double z = 0;
	CHECK(FplDragUpright(origin, dir, box, 5, -3, z));
	CHECK_NEAR_TOL(z, 2, 1e-3);
	// kept in the box
	through({ 9, 9, 9 }, origin, dir);
	CHECK(FplDragOnPlane(origin, dir, box, -9, at));
	CHECK(std::abs(at.x) <= 10 && std::abs(at.y) <= 10);
	// a level ray never meets a plane; one pointing away doesn't either; straight down never meets a line's nearest height
	CHECK(!FplDragOnPlane({ 0, 0, 3 }, { 1, 0, 0 }, box, 2, at));
	CHECK(!FplDragOnPlane({ 0, 0, 3 }, { 0, 0, 1 }, box, 2, at));
	CHECK(!FplDragUpright({ 0, 0, 3 }, { 0, 0, -1 }, box, 1, 1, z));
	FplBox3 bad{ 1, 1, 0, 1, 0, 1 };
	CHECK(!FplDragOnPlane(origin, dir, bad, 2, at));
	CHECK(!FplDragUpright(origin, dir, bad, 0, 0, z));
}

TEST(Substitutions) {
	auto at = [](FplPlot& plot, int graph, double x) {
		auto slots = plot.Slots();
		slots[FplSlotX] = x;
		return plot.graphs[graph].expr.f.Eval(slots.data());
	};
	// it stands for its text, and draws nothing itself
	auto plot = MakePlot({ L"a = x - 1", L"y = 3a" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(!plot.graphs[1].expr.error);
	CHECK(plot.graphs[0].expr.kind == FplKind::Definition);
	CHECK(!plot.graphs[0].Drawable() || plot.graphs[0].expr.f.Empty());
	CHECK_NEAR(at(plot, 1, 3), 6.0);
	FplViewport v = View();
	plot.Sample(v);
	CHECK(plot.graphs[0].geometry.lines.empty());
	CHECK(!plot.graphs[1].geometry.lines.empty());
	// whichever line comes first; through another; with a parameter; in products and powers
	plot = MakePlot({ L"y = 3a", L"a = x - 1" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK_NEAR(at(plot, 0, 3), 6.0);
	plot = MakePlot({ L"y = b + 1", L"b = a^2", L"a = x - 1" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK_NEAR(at(plot, 0, 3), 5.0);
	plot = MakePlot({ L"a = k x", L"y = a + 1" }, { Param(L"k", 2) });
	CHECK(!plot.graphs[1].expr.error);
	CHECK_NEAR(at(plot, 1, 3), 7.0);
	plot = MakePlot({ L"a = x - 1", L"y = 2a^2 + a(x + 1)", L"y = sin a" });
	CHECK(!plot.graphs[1].expr.error);
	CHECK_NEAR(at(plot, 1, 3), 8.0 + 2 * 4);	// a(x + 1) is a times x + 1
	CHECK_NEAR(at(plot, 2, 3), std::sin(2.0));
	// its whole text at once: a = x + 1 in 2a is 2(x + 1), not 2x + 1
	plot = MakePlot({ L"a = x + 1", L"y = 2a" });
	CHECK_NEAR(at(plot, 1, 3), 8.0);
	// a definition by a number stays one, with a value; one through a parameter too
	plot = MakePlot({ L"c = 2", L"d = c + 1", L"y = d x" });
	CHECK(plot.graphs[0].definition && plot.graphs[1].definition);
	CHECK_NEAR(at(plot, 2, 3), 9.0);
	// other kinds of graph use it; a function; features see through it; derivatives
	plot = MakePlot({ L"a = x - 1", L"x^2 + a^2 = 4", L"f(x) = a^2", L"y = d/dx a^2", L"y = f'(x)" });
	for (int i = 0; i < 5; i++)
		CHECK(!plot.graphs[i].expr.error);
	CHECK_NEAR(at(plot, 3, 3), 4.0);
	CHECK_NEAR(at(plot, 4, 3), 4.0);
	plot.Sample(v);
	CHECK(!plot.graphs[1].geometry.lines.empty());
	plot = MakePlot({ L"a = x - 1", L"y = a^2 - 4" });
	plot.Sample(v);
	plot.FindFeatures(1, v);
	CHECK(Has(plot.graphs[1].features, FplFeatureKind::Root, 3, 0, 1e-9));
	CHECK(Has(plot.graphs[1].features, FplFeatureKind::Root, -1, 0, 1e-9));
	CHECK(Has(plot.graphs[1].features, FplFeatureKind::Minimum, 1, -4, 1e-6));
	plot = MakePlot({ L"a = x - y", L"z = a^2" });
	CHECK(!plot.graphs[1].expr.error);
	CHECK(plot.graphs[1].expr.kind == FplKind::Surface);
	plot = MakePlot({ L"a = 2x", L"y' = -a y, y(0) = 1" });
	CHECK(!plot.graphs[1].expr.error);
	plot = MakePlot({ L"a = sin t", L"(a, cos t)" });
	CHECK(!plot.graphs[1].expr.error);
	CHECK(plot.graphs[1].expr.kind == FplKind::Parametric);
	// a variable the graph hasn't: as if it were written out
	plot = MakePlot({ L"a = t", L"y = a" });
	CHECK(plot.graphs[1].expr.error.code == FplErrorCode::VariableNotAllowed);

	// a parameter by that name has it (and the equations with one read as they did)
	plot = MakePlot({ L"a = x - 1", L"x - 1 = a", L"x = a + 1" }, { Param(L"a", 2) });
	CHECK(plot.graphs[0].expr.error.code == FplErrorCode::Redefined);
	CHECK(plot.graphs[0].expr.error.name == L"a");
	CHECK(!plot.graphs[1].expr.error);
	CHECK(!plot.graphs[2].expr.error);
	// the name taken: twice, or a function of that name
	plot = MakePlot({ L"a = x - 1", L"a = x + 1" });
	CHECK(plot.graphs[1].expr.error.code == FplErrorCode::Redefined);
	plot = MakePlot({ L"a = x - 1", L"a(x) = x" });
	CHECK(plot.graphs[1].expr.error.code == FplErrorCode::Redefined);
	plot = MakePlot({ L"a = x - 1", L"a = 2" });
	CHECK(plot.graphs[1].expr.error.code == FplErrorCode::Redefined);
	// around in a circle, or of itself: nothing defined
	plot = MakePlot({ L"a = x + b", L"b = x + a" });
	CHECK(plot.graphs[0].expr.error.code == FplErrorCode::UnknownName);
	plot = MakePlot({ L"a = a + x" });
	CHECK(plot.graphs[0].expr.error.code == FplErrorCode::UnknownName);
	// no derivative by prime (of what?); d/dx it
	plot = MakePlot({ L"a = x^2", L"y = a'", L"y = d/dx a" });
	CHECK(plot.graphs[1].expr.error.code == FplErrorCode::NotDifferentiable);
	CHECK(!plot.graphs[2].expr.error);
	CHECK_NEAR(at(plot, 2, 3), 6.0);

	// a variable's name: says so
	std::vector<std::wstring> none;
	for (const wchar_t* text : { L"u(x) = x - 1", L"t(x) = x", L"v(x, y) = x y", L"u = x - 1", L"v = x", L"t = x + 1",
		L"  u (x) = x - 1" }) {
		auto e = FplBuild(text, none);
		CHECK(e.error.code == FplErrorCode::ReservedName);
		CHECK(!e.error.Message().empty());
	}
	auto e = FplBuild(L"  u(x) = x", none);
	CHECK_INT(e.error.pos, 2);
	CHECK_INT(e.error.len, 1);
	CHECK(e.error.name == L"u");
	CHECK(e.error.Message().find(L"'u'") != std::wstring::npos);
	// what reads stays as it was: plots with those on the left, errors of another kind
	CHECK(FplBuild(L"z = x y", none).kind == FplKind::Surface);
	CHECK(!FplBuild(L"z = x y", none).error);
	CHECK(FplBuild(L"r = 2", none).kind == FplKind::Polar);
	CHECK(FplBuild(L"y = x", none).kind == FplKind::Function);
	CHECK(FplBuild(L"x = y^2", none).kind == FplKind::FunctionOfY);
	CHECK(!FplBuild(L"y(x) = x^2", none).error);		// y times x, an equation
	CHECK(FplBuild(L"u(x) = x +", none).error.code == FplErrorCode::UnexpectedEnd);
	CHECK(FplBuild(L"y = t", none).error.code == FplErrorCode::VariableNotAllowed);
	CHECK(FplBuild(L"sin t", none).error.code != FplErrorCode::ReservedName);
	CHECK(FplBuild(L"z = t", none).error.code == FplErrorCode::VariableNotAllowed);
	CHECK(FplBuild(L"y = u x", none).error.code == FplErrorCode::VariableNotAllowed);
	CHECK(!FplBuild(L"z(x) = x", none).error);			// z times x, a surface in 3D
}

TEST(PolarGrid) {
	auto within = [](const FplPolyline& line, const FplViewport& v) {
		for (auto& p : line)
			if (p.x < -1e-6 || p.y < -1e-6 || p.x > v.width + 1e-6 || p.y > v.height + 1e-6)
				return false;
		return true;
	};
	// the origin in view: whole circles, 12 spokes and their labels at 30 degrees
	FplViewport v = View(-10, 10, -10, 10, 800, 800);
	auto grid = FplMakePolarGrid(v);
	CHECK(!grid.Empty());
	CHECK_NEAR_TOL(grid.angleStep, Pi / 6, 1e-12);
	CHECK_INT(grid.majorSpokes.size(), 12);
	CHECK_INT(grid.minorSpokes.size(), 12);
	CHECK_INT(grid.angleLabels.size(), 12);
	CHECK(!grid.majorRings.empty());
	CHECK(!grid.radiusLabels.empty());
	// every point of a ring is on one of the radii, every point of a spoke on one of the angles
	bool onRing = true, inView = true;
	for (auto& line : grid.majorRings) {
		inView = inView && within(line, v);
		// (a piece cut at the view's edge ends on a chord, a tenth of a DIP off at most: 0.0025 here)
		const double r = std::hypot(v.ToPlaneX(line[0].x), v.ToPlaneY(line[0].y));
		for (auto& p : line)
			onRing = onRing && std::abs(std::hypot(v.ToPlaneX(p.x), v.ToPlaneY(p.y)) - r) < 0.006;
		bool known = false;
		for (double tick : grid.radii.major)
			known = known || std::abs(tick - r) < 0.006;
		onRing = onRing && known;
	}
	CHECK(onRing);
	CHECK(inView);
	bool onSpoke = true;
	for (auto& line : grid.majorSpokes) {
		CHECK(within(line, v));
		const double x = v.ToPlaneX(line.back().x), y = v.ToPlaneY(line.back().y);
		const double k = std::atan2(y, x) / (Pi / 6);
		onSpoke = onSpoke && std::abs(k - std::round(k)) < 1e-9;
	}
	CHECK(onSpoke);
	// no more points than a smooth circle takes
	for (auto& line : grid.majorRings)
		CHECK(line.size() < 2000);
	// and whole ones smooth: the smallest circle's is in one piece, its chords short
	CHECK(grid.majorRings[0].size() >= 16);
	// the radii on the positive x axis, all different and positive; the angles' labels all different
	for (auto& l : grid.radiusLabels) {
		CHECK(l.value > 0);
		CHECK_NEAR_TOL(l.at.y, v.ToScreenY(0), 1e-9);
		CHECK(l.at.x > v.ToScreenX(0));
	}
	std::vector<double> angles;
	for (auto& l : grid.angleLabels) {
		CHECK(l.value >= 0 && l.value < 2 * Pi);
		CHECK(l.at.x >= 0 && l.at.x <= v.width && l.at.y >= 0 && l.at.y <= v.height);
		angles.push_back(l.value);
	}
	std::sort(angles.begin(), angles.end());
	CHECK(std::adjacent_find(angles.begin(), angles.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }) == angles.end());
	// no labels off the panel: the positive x axis gone, another direction takes them
	auto left = FplMakePolarGrid(View(-20, 1, -10, 10, 800, 800));
	CHECK(!left.radiusLabels.empty());
	for (auto& l : left.radiusLabels)
		CHECK(l.at.x < 800);
	CHECK_INT(left.majorSpokes.size(), 12);

	// the origin out of view: part of the circles, and only the angles in view
	FplViewport off = View(20, 30, 20, 30, 800, 800);
	auto part = FplMakePolarGrid(off);
	CHECK(!part.Empty());
	CHECK(part.angleStep < Pi / 6);
	CHECK(part.majorSpokes.size() >= 3);
	CHECK(part.majorSpokes.size() <= 26);
	for (auto& line : part.majorRings)
		CHECK(within(line, off));
	for (auto& line : part.majorSpokes) {
		const double a = std::atan2(off.ToPlaneY(line[0].y), off.ToPlaneX(line[0].x));
		CHECK(a > 0 && a < Pi / 2);
		CHECK(within(line, off));
	}
	for (auto& l : part.radiusLabels)
		CHECK(l.value >= std::hypot(20, 20) - 1e-9 && l.value <= std::hypot(30, 30) + 1e-9);
	// far in, many screens across: still a handful of points, drawn right
	FplViewport deep = View(1000, 1000.001, 1000, 1000.001, 800, 800);
	auto zoomed = FplMakePolarGrid(deep);
	CHECK(!zoomed.Empty());
	size_t count = 0;
	for (auto& line : zoomed.majorRings)
		count += line.size();
	CHECK(count < 4000);
	// a view straddling an axis, with the origin off to one side
	CHECK(!FplMakePolarGrid(View(-5, 5, 10, 20, 800, 800)).Empty());
	CHECK(!FplMakePolarGrid(View(-20, -10, -5, 5, 800, 800)).Empty());
	// different scales: ellipses on the screen, circles on the plane, and not crowded
	FplViewport wide = View(-100, 100, -5, 5, 800, 800);
	auto squeezed = FplMakePolarGrid(wide);
	CHECK(!squeezed.Empty());
	CHECK(squeezed.radii.major.size() < 30);
	// nothing on log axes, or for no view
	FplViewport log = View(1, 100, 1, 100, 800, 800);
	log.logX = true;
	CHECK(FplMakePolarGrid(log).Empty());
	FplViewport none = v;
	none.width = 0;
	CHECK(FplMakePolarGrid(none).Empty());

	// angles' labels
	const std::wstring deg(1, wchar_t(0x00B0)), pi(1, wchar_t(0x03C0));
	CHECK_STR(FplFormatAngle(0), L"0" + deg);
	CHECK_STR(FplFormatAngle(Pi / 6), L"30" + deg);
	CHECK_STR(FplFormatAngle(-Pi / 2), L"270" + deg);
	CHECK_STR(FplFormatAngle(2 * Pi), L"0" + deg);
	CHECK_STR(FplFormatAngle(0.5 * Pi / 180), L"0.5" + deg);
	CHECK_STR(FplFormatAngle(0, true), L"0");
	CHECK_STR(FplFormatAngle(Pi, true), pi);
	CHECK_STR(FplFormatAngle(Pi / 6, true), pi + L"/6");
	CHECK_STR(FplFormatAngle(5 * Pi / 6, true), L"5" + pi + L"/6");
	CHECK_STR(FplFormatAngle(3 * Pi / 2, true), L"3" + pi + L"/2");
	CHECK_STR(FplFormatAngle(2 * Pi, true), L"0");
	CHECK_STR(FplFormatAngle(1.5, true), L"1.5");
	CHECK_STR(FplFormatAngle(10 * Pi / 180, true), pi + L"/18");
	CHECK_STR(FplFormatAngle(50 * Pi / 180, true), L"5" + pi + L"/18");
	CHECK_STR(FplFormatAngle(-Pi / 6, true), L"11" + pi + L"/6");
	// labels leave room for their text at every edge
	for (auto& l : grid.angleLabels)
		CHECK(l.at.x >= 28 && l.at.x <= 800 - 28 && l.at.y >= 11 && l.at.y <= 800 - 11);

	// the SVG: the grid's own lines instead of straight ones, the angles labeled
	auto plot = MakePlot({ L"r = 3" });
	FplSvgStyle style;
	style.graphColors = { 0xFF0000 };
	plot.Sample(v);
	const std::string square = FplWriteSvg(plot, v, style);
	style.polarGrid = true;
	const std::string round = FplWriteSvg(plot, v, style);
	CHECK(round != square);
	CHECK(round.find("\xC2\xB0") != std::string::npos);
	CHECK(square.find("\xC2\xB0") == std::string::npos);
	style.showGrid = false;
	style.showAxes = false;
	CHECK(FplWriteSvg(plot, v, style).find("\xC2\xB0") == std::string::npos);
	style.showGrid = true;
	style.showAxes = true;
	style.piTicks = true;
	CHECK(FplWriteSvg(plot, v, style).find("\xCF\x80/6") != std::string::npos);
	// on log axes it stays the square one
	style.piTicks = false;
	const std::string logSquare = FplWriteSvg(plot, log, style);
	CHECK(logSquare.find("\xC2\xB0") == std::string::npos);
}

TEST(Fields3D) {
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	// segments flat on the plane: as wide as asked, and around what was given
	auto one = FplFlatSegments({ { { -5, 0 }, { 5, 0 } } }, box, 0.01f);
	CHECK_INT(one.vertices.size(), 4);
	CHECK_INT(one.indices.size(), 6);
	float minX = 9, maxX = -9, minY = 9, maxY = -9;
	for (auto& p : one.vertices) {
		CHECK(std::abs(p.pos.z) < 1e-6f);
		minX = std::min(minX, p.pos.x), maxX = std::max(maxX, p.pos.x);
		minY = std::min(minY, p.pos.y), maxY = std::max(maxY, p.pos.y);
	}
	CHECK_NEAR_TOL(minX, -0.51, 1e-5);
	CHECK_NEAR_TOL(maxX, 0.51, 1e-5);
	CHECK_NEAR_TOL(minY, -0.01, 1e-5);
	CHECK_NEAR_TOL(maxY, 0.01, 1e-5);
	CHECK(FplFlatSegments({ { { 1, 1 }, { 1, 1 } }, { { NaN, 0 }, { 1, 1 } } }, box, 0.01f).Empty());
	CHECK(FplFlatSegments({ { { 0, 0 }, { 1, 1 } } }, FplBox3{ 1, 1, 0, 1, 0, 1 }, 0.01f).Empty());
	// the head's strokes end at the tip, on either side of the shaft
	FplArrow arrow{ { 0, 0 }, { 10, 0 }, 1 };
	FplPoint l, r;
	CHECK(FplArrowHead(arrow, l, r));
	CHECK(l.x < 10 && r.x < 10);
	CHECK(l.y * r.y < 0);
	CHECK(!FplArrowHead({ { 3, 3 }, { 3, 3 }, 0 }, l, r));

	// each kind that has a field, alone and with solutions, fainter then; no pick, no tubes where there's nothing else
	auto plot = MakePlot({ L"(y, -x)", L"y' = x", L"y' = x, y(0) = 1", L"x' = y, y' = -x", L"x' = y, y' = -x, (1, 2)",
		L"x' = t, y' = 1, (1, 2)", L"y = x", L"z = x", L"(1, 2)" });
	for (auto& g : plot.graphs)
		CHECK(!g.expr.error);
	plot.Build3D(box);
	CHECK(!plot.graphs[0].fieldMesh.Empty());
	CHECK(plot.graphs[0].mesh.Empty());
	CHECK_NEAR(plot.graphs[0].fieldOpacity, 1.0f);
	CHECK(!plot.graphs[1].fieldMesh.Empty());
	CHECK(plot.graphs[1].mesh.Empty());
	CHECK_NEAR(plot.graphs[1].fieldOpacity, 0.75f);
	CHECK(!plot.graphs[2].fieldMesh.Empty());
	CHECK(!plot.graphs[2].mesh.Empty());
	CHECK_NEAR(plot.graphs[2].fieldOpacity, 0.4f);
	CHECK(!plot.graphs[3].fieldMesh.Empty());
	CHECK_NEAR(plot.graphs[3].fieldOpacity, 1.0f);
	CHECK(!plot.graphs[4].fieldMesh.Empty());
	CHECK_NEAR(plot.graphs[4].fieldOpacity, 0.45f);
	CHECK(plot.graphs[5].fieldMesh.Empty());		// with t: true of one moment
	CHECK(plot.graphs[6].fieldMesh.Empty());
	CHECK(plot.graphs[7].fieldMesh.Empty());
	CHECK(plot.graphs[8].fieldMesh.Empty());
	// it's on the plane, and inside the box
	bool flat = true;
	for (auto& v : plot.graphs[0].fieldMesh.vertices)
		flat = flat && std::abs(v.pos.z) < 1e-6f && std::abs(v.pos.x) < 1.2f && std::abs(v.pos.y) < 1.2f;
	CHECK(flat);
	// a field gives no trace: hovering goes to what's solid
	FplCamera cam;
	FplVec3 origin, dir;
	cam.Ray({ 400, 300 }, 800, 600, origin, dir);
	auto field = MakePlot({ L"(y, -x)" });
	field.Build3D(box);
	CHECK(!field.Pick3D(origin, dir, box));
	// hidden, none; parameters move it; a listed one's together
	field.graphs[0].flags |= FplGraphHidden;
	field.Build3D(box);
	CHECK(field.graphs[0].fieldMesh.Empty());
	auto listed = MakePlot({ L"L = [1, 2]", L"(L y, -x)" });
	auto single = MakePlot({ L"(y, -x)" });
	listed.Build3D(box);
	single.Build3D(box);
	CHECK(listed.graphs[1].fieldMesh.indices.size() > single.graphs[0].fieldMesh.indices.size());
}

TEST(HiddenPoints3D) {
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	FplCamera cam;
	const float w = 800, h = 600;
	auto params = { Param(L"a", 0), Param(L"b", 0), Param(L"c", 1) };
	auto screen = [&](FplPoint3 p) {
		FplPoint s;
		cam.Project(box.ToBox(p), w, h, s);
		return s;
	};
	// the camera's above: a surface over the point hides it, one under it doesn't, nor does one it's on
	auto plot = MakePlot({ L"(a, b, c)", L"z = 5" }, params);
	plot.Build3D(box);
	CHECK(plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
	CHECK(!plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 8 }), cam, w, h));
	CHECK(!plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 5 }), cam, w, h));
	CHECK(!plot.HiddenBySurface(1, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));	// its own doesn't
	CHECK_INT(plot.DraggableAt3D(screen({ 0, 0, 1 }), cam, box, w, h, 6), -1);
	// from under it, the other way round
	FplCamera under = cam;
	under.pitch = -cam.pitch;
	CHECK(!plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), under, w, h));
	CHECK(plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 8 }), under, w, h));
	// see-through or hidden surfaces don't count, and curves don't
	plot.graphs[1].opacity = 0.5f;
	CHECK(!plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
	CHECK_INT(plot.DraggableAt3D(screen({ 0, 0, 1 }), cam, box, w, h, 6), 0);
	plot.graphs[1].opacity = 1;
	plot.graphs[1].flags |= FplGraphHidden;
	CHECK(!plot.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
	auto curve = MakePlot({ L"(a, b, c)", L"y = x", L"x^2 + y^2 = 4" }, params);
	curve.Build3D(box);
	CHECK(!curve.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
	// any solid kind: a sphere around it, and parametric
	auto ball = MakePlot({ L"(a, b, c)", L"x^2 + y^2 + z^2 = 36" }, params);
	ball.Build3D(box);
	CHECK(ball.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
	auto round = MakePlot({ L"(a, b, c)", L"(6cos u cos v, 6sin u cos v, 6sin v)" }, params);
	round.Build3D(box);
	CHECK(round.HiddenBySurface(0, box.ToBox(FplPoint3{ 0, 0, 1 }), cam, w, h));
}

TEST(QIsTheta) {
	std::vector<std::wstring> none;
	auto at = [&](const wchar_t* text, double theta, const std::vector<std::wstring>& names = {}, double param = 0) {
		auto e = FplBuild(text, names);
		CHECK(!e.error);
		std::vector<double> slots(FplFirstParam + names.size(), param);
		slots[FplSlotT] = slots[FplSlotX] = slots[FplSlotY] = 0;
		slots[FplSlotTheta] = theta;
		return e.error ? NaN : e.f.Eval(slots.data());
	};
	// polar, as with the letter and the word
	CHECK(FplBuild(L"r = 2 + cos 3q", none).kind == FplKind::Polar);
	CHECK_NEAR(at(L"r = 2 + cos 3q", 0.4), 2 + std::cos(1.2));
	CHECK_NEAR(at(L"r = q/2", 0.4), 0.2);
	CHECK_NEAR(at(L"r = 2q", 0.4), 0.8);
	CHECK_NEAR(at(L"r = sin q", 0.4), std::sin(0.4));
	CHECK_NEAR(at(L"r = sin(q)^2", 0.4), std::sin(0.4) * std::sin(0.4));
	CHECK_NEAR(at(L"r = d/dq (q^2)", 3), 6);
	CHECK_NEAR(at(L"r = d/dq (q^2)", 3), at(L"r = d/dtheta (theta^2)", 3));
	CHECK(FplBuild(L"r < 3sin 2q", none).rel == FplRel::Lt);
	// with r too, as theta alongside r is the plane's angle
	CHECK(!FplBuild(L"r < q", none).error);
	// beside another variable, or in an ODE, it's a name the app can offer to add, at its place
	for (const wchar_t* text : { L"y = 2 q x", L"x^2 + q = 1", L"y = q", L"r = q x", L"(q, t)", L"z = q x", L"y' = q", L"y' = q y",
		L"1 < y < q", L"x' = y, y' = q" }) {
		auto bad = FplBuild(text, none);
		CHECK(bad.error.code == FplErrorCode::UnknownName);
		CHECK_STR(bad.error.name, L"q");
		CHECK_INT(bad.error.len, 1);
		CHECK(text[bad.error.pos] == L'q');
	}
	// while the letter and the word beside x stay the plane's angle
	CHECK(FplBuild(L"y = \x03B8 x", none).kind == FplKind::Implicit);
	CHECK(FplBuild(L"y = theta x", none).kind == FplKind::Implicit);
	// where theta doesn't read either
	CHECK(FplBuild(L"(cos q, sin q)", none).error.code == FplErrorCode::UnknownName);
	// a definition
	CHECK(FplBuild(L"q = 1", none).kind == FplKind::Definition);
	// names that contain it are as they were; known names first
	CHECK_NEAR(at(L"r = sqrt(q)", 4), 2);
	CHECK(FplBuild(L"r = q1", none).error.code == FplErrorCode::UnknownName);
	// a parameter by that name has it
	CHECK(FplBuild(L"y = q x", { L"q" }).kind == FplKind::Function);
	CHECK(!FplBuild(L"y = q x", { L"q" }).error);
	CHECK(FplBuild(L"r = d/dq (q^2)", { L"q" }).error.code != FplErrorCode::None);
	// the same name in a plot where a slider is added for it
	auto sliders = MakePlot({ L"r = q", L"y = q x" }, { Param(L"q", 2) });
	CHECK(sliders.graphs[0].expr.kind == FplKind::Polar);
	CHECK(!sliders.graphs[1].expr.error);

	// a line defining it: a number, what depends on it, whichever line comes first
	auto plot = MakePlot({ L"q = 2", L"y = q x" });
	CHECK(plot.graphs[0].definition);
	CHECK(!plot.graphs[1].expr.error);
	plot = MakePlot({ L"y = q x", L"q = 2" });
	CHECK(plot.graphs[1].definition);
	CHECK(!plot.graphs[0].expr.error);
	auto slots = plot.Slots();
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[0].expr.f.Eval(slots.data()), 6.0);
	plot = MakePlot({ L"p = q", L"q = 2", L"y = p x" });
	CHECK(plot.graphs[0].definition);
	CHECK(!plot.graphs[2].expr.error);
	// a substitution, a function of that name, a function's parameter
	plot = MakePlot({ L"q = x - 1", L"y = 3q" });
	CHECK(!plot.graphs[1].expr.error);
	slots = plot.Slots();
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 6.0);
	plot = MakePlot({ L"q(x) = x^2", L"y = q(x) + 1" });
	CHECK(!plot.graphs[1].expr.error);
	slots = plot.Slots();
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 10.0);
	plot = MakePlot({ L"f(q, x) = q + x", L"y = f(2, x)" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(!plot.graphs[1].expr.error);
	slots = plot.Slots();
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 5.0);
	// a function of q alone is an ordinary parameter's, so it draws nothing, while one of theta is polar
	plot = MakePlot({ L"f(q) = q^2" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(plot.graphs[0].expr.kind != FplKind::Polar);
	// a body may use it as theta
	plot = MakePlot({ L"g(\x03B8) = 1 + \x03B8", L"r = g(q)" });
	CHECK(!plot.graphs[1].expr.error);
	CHECK(plot.graphs[1].expr.kind == FplKind::Polar);
	// a regression's unknowns are fitted
	auto fit = MakePlot({ L"X = [1, 2, 3, 4]", L"Y = [2, 4, 6, 8]", L"Y ~ q X" });
	CHECK(!fit.graphs[2].expr.error);
	CHECK(fit.graphs[2].expr.kind == FplKind::Regression);
	CHECK_NEAR_TOL(fit.ValueOf(L"q"), 2.0, 1e-6);
	// and a list's name
	plot = MakePlot({ L"q = [1, 2]", L"y = q x" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(!plot.graphs[1].expr.error);
	// draws: as the same line with the letter
	FplViewport v = View();
	auto one = MakePlot({ L"r = 2 + cos 3q" }), two = MakePlot({ L"r = 2 + cos 3\x03B8" });
	one.Sample(v);
	two.Sample(v);
	CHECK(!one.graphs[0].geometry.lines.empty());
	CHECK_INT(one.graphs[0].geometry.lines[0].size(), two.graphs[0].geometry.lines[0].size());
	// saved as it's written
	CHECK(one.graphs[0].text == L"r = 2 + cos 3q");
}

TEST(Transitions) {
	// the easing: the ends exact, always on the way, slow at both ends
	CHECK_NEAR(FplEase(0), 0.0);
	CHECK_NEAR(FplEase(1), 1.0);
	CHECK_NEAR(FplEase(0.5), 0.5);
	CHECK_NEAR(FplEase(-1), 0.0);
	CHECK_NEAR(FplEase(2), 1.0);
	CHECK(FplEase(0.1) < 0.1);
	CHECK(FplEase(0.9) > 0.9);
	for (double t = 0.05; t < 1; t += 0.05)
		CHECK(FplEase(t) > FplEase(t - 0.05));

	// a range: the ends exact; the same width, a straight line; zooming, geometric
	double lo, hi;
	FplInterpolateRange(-10, 10, 2, 3, 0, lo, hi);
	CHECK(lo == -10 && hi == 10);
	FplInterpolateRange(-10, 10, 2, 3, 1, lo, hi);
	CHECK(lo == 2 && hi == 3);
	FplInterpolateRange(0, 10, 10, 20, 0.25, lo, hi);
	CHECK_NEAR(lo, 2.5);
	CHECK_NEAR(hi, 12.5);
	FplInterpolateRange(0, 1000, 0, 1, 0.5, lo, hi);
	CHECK_NEAR(hi - lo, std::sqrt(1000.0));
	// about a fixed point: going from 0..10 to 2..3 the point at 20/9 stays at the same fraction of the range
	FplInterpolateRange(0, 10, 2, 3, 0.5, lo, hi);
	CHECK_NEAR((20.0 / 9 - lo) / (hi - lo), 2.0 / 9);
	CHECK(lo > 0 && hi < 10);
	CHECK(lo < 2 && hi > 3);
	// always nested on the way in, whatever the step
	double lastLo = 0, lastHi = 10;
	for (double t = 0.1; t < 1; t += 0.1) {
		FplInterpolateRange(0, 10, 2, 3, t, lo, hi);
		CHECK(lo > lastLo && hi < lastHi);
		lastLo = lo, lastHi = hi;
	}
	// no width to go by
	FplInterpolateRange(5, 5, 0, 1, 0.5, lo, hi);
	CHECK_NEAR(lo, 2.5);
	CHECK_NEAR(hi, 3.0);

	// a view: exact at the ends, keeping the window's size
	FplViewport a = View(-10, 10, -8, 8, 1000, 800), b = View(1, 2, 3, 4, 1000, 800);
	auto at0 = FplInterpolateView(a, b, 0), at1 = FplInterpolateView(a, b, 1);
	CHECK(at0.xMin == a.xMin && at0.xMax == a.xMax && at0.yMin == a.yMin && at0.yMax == a.yMax);
	CHECK(at1.xMin == b.xMin && at1.xMax == b.xMax && at1.yMin == b.yMin && at1.yMax == b.yMax);
	auto mid = FplInterpolateView(a, b, 0.5);
	CHECK(mid.Valid());
	CHECK_NEAR(mid.xMax - mid.xMin, std::sqrt(20.0));
	CHECK_NEAR(mid.yMax - mid.yMin, 4.0);
	CHECK_INT(mid.width, 1000);
	// a log axis goes by decades
	FplViewport la = View(1, 10, 1, 10, 800, 800), lb = View(1, 1000, 1, 10, 800, 800);
	la.logX = lb.logX = true;
	auto lm = FplInterpolateView(la, lb, 0.5);
	CHECK(lm.logX);
	CHECK_NEAR_TOL(lm.xMin, 1.0, 1e-9);
	CHECK_NEAR_TOL(lm.xMax, std::pow(10.0, std::sqrt(3.0)), 1e-9);
	CHECK(lm.Valid());
	// different axes: straight there
	auto cross = FplInterpolateView(a, la, 0.5);
	CHECK(cross.xMin == la.xMin && cross.logX);

	// an angle, the short way round
	CHECK_NEAR_TOL(FplInterpolateAngle(0.1f, 0.3f, 0.5), 0.2f, 1e-6);
	CHECK_NEAR_TOL(FplInterpolateAngle(0.1f, -0.1f, 0.5), 0.0f, 1e-6);
	const float nearly = static_cast<float>(2 * Pi) - 0.1f;
	CHECK_NEAR_TOL(FplInterpolateAngle(0.1f, nearly, 0.5), 0.0f, 1e-5);
	CHECK(FplInterpolateAngle(0.1f, nearly, 0) == 0.1f);
	CHECK(FplInterpolateAngle(0.1f, nearly, 1) == nearly);
}

TEST(StretchingDrawings) {
	FplViewMap map;
	// the same view: nothing moves
	FplViewport v = View(-10, 10, -8, 8, 1000, 800);
	CHECK(FplMapBetween(v, v, map));
	CHECK_NEAR(map.sx, 1.0);
	CHECK_NEAR(map.tx, 0.0);
	CHECK_NEAR(map.ty, 0.0);
	// any move: a point on the plane lands where the new view draws it
	auto check = [&](const FplViewport& from, const FplViewport& to) {
		FplViewMap m;
		CHECK(FplMapBetween(from, to, m));
		for (auto& [x, y] : { std::pair{ 1.5, 2.5 }, std::pair{ 3.0, 4.0 }, std::pair{ 2.0, 7.0 } }) {
			const FplPoint old = { from.ToScreenX(x), from.ToScreenY(y) };
			const FplPoint moved = m.Apply(old);
			CHECK_NEAR_TOL(moved.x, to.ToScreenX(x), 1e-7);
			CHECK_NEAR_TOL(moved.y, to.ToScreenY(y), 1e-7);
		}
	};
	check(v, View(-8, 12, -8, 8, 1000, 800));		// panned
	check(v, View(-8, 12, -5, 11, 1000, 800));
	check(v, View(-5, 5, -4, 4, 1000, 800));		// zoomed in
	check(v, View(-20, 20, -16, 16, 1000, 800));	// out
	check(v, View(-10, 10, -2, 2, 1000, 800));		// one axis
	check(v, View(-10, 10, -8, 8, 500, 400));		// the window's size
	// on log axes, by decades
	FplViewport la = View(1, 100, 1, 100, 800, 800), lb = View(1, 10, 2, 50, 800, 800);
	la.logX = la.logY = lb.logX = lb.logY = true;
	check(la, lb);
	// nothing to gain from: too far zoomed, no overlap, other axes, no view
	CHECK(!FplMapBetween(v, View(-1, 1, -8, 8, 1000, 800), map));
	CHECK(!FplMapBetween(v, View(-100, 100, -8, 8, 1000, 800), map));
	CHECK(FplMapBetween(v, View(-5, 5, -8, 8, 1000, 800), map, 3));
	CHECK(!FplMapBetween(v, View(-1, 1, -8, 8, 1000, 800), map, 3));
	CHECK(!FplMapBetween(v, View(100, 120, -8, 8, 1000, 800), map));
	CHECK(!FplMapBetween(v, View(-10, 10, 100, 116, 1000, 800), map));
	CHECK(!FplMapBetween(v, la, map));
	FplViewport none = v;
	none.width = 0;
	CHECK(!FplMapBetween(none, v, map));
	CHECK(!FplMapBetween(v, none, map));
}

TEST(MoreFunctions) {
	std::vector<std::wstring> none;
	auto at = [&](const std::wstring& text, double x, bool degrees = false) {
		auto e = FplBuild(L"y = " + text, none, degrees);
		CHECK(!e.error);
		double slots[FplFirstParam] = {};
		slots[FplSlotX] = x;
		return e.error ? NaN : e.f.Eval(slots);
	};
	const double x = 0.7, big = 2.5;
	// inverse reciprocals, and their other names
	CHECK_NEAR(at(L"asec(x)", big), std::acos(1 / big));
	CHECK_NEAR(at(L"acsc(x)", big), std::asin(1 / big));
	CHECK_NEAR(at(L"acot(x)", x), Pi / 2 - std::atan(x));
	CHECK_NEAR(at(L"acot(x)", -x), Pi / 2 + std::atan(x));		// continuous: 0 to pi
	CHECK_NEAR(at(L"arcsec(x)", big), at(L"asec(x)", big));
	CHECK_NEAR(at(L"arccsc(x)", big), at(L"acsc(x)", big));
	CHECK_NEAR(at(L"arccot(x)", x), at(L"acot(x)", x));
	CHECK_NEAR(at(L"sec^-1(x)", big), at(L"asec(x)", big));
	CHECK_NEAR(at(L"cot^-1 x", x), at(L"acot(x)", x));
	CHECK_NEAR(at(L"asec(sec(x))", x), x);
	CHECK_NEAR(at(L"acsc(csc(x))", x), x);
	CHECK_NEAR(at(L"acot(cot(x))", x), x);
	// reciprocal hyperbolic
	CHECK_NEAR(at(L"sech(x)", x), 1 / std::cosh(x));
	CHECK_NEAR(at(L"csch(x)", x), 1 / std::sinh(x));
	CHECK_NEAR(at(L"coth(x)", x), 1 / std::tanh(x));
	CHECK_NEAR(at(L"asech(sech(x))", x), x);
	CHECK_NEAR(at(L"acsch(csch(x))", x), x);
	CHECK_NEAR(at(L"acoth(coth(x))", x), x);
	CHECK_NEAR(at(L"arsech(x)", 0.5), at(L"arcsech(x)", 0.5));
	CHECK_NEAR(at(L"arcsch(x)", 0.5), at(L"arccsch(x)", 0.5));
	CHECK_NEAR(at(L"arcoth(x)", 2), at(L"arccoth(x)", 2));
	CHECK_NEAR(at(L"coth^-1(x)", 2), at(L"acoth(x)", 2));
	CHECK_NEAR(at(L"sech^-1(x)", 0.5), at(L"asech(x)", 0.5));
	CHECK_NEAR(at(L"csch^-1(x)", 0.5), at(L"acsch(x)", 0.5));
	// logarithms
	CHECK_NEAR(at(L"log10(x)", 1000), 3);
	CHECK_NEAR(at(L"log(x)", 1000), 3);
	CHECK_NEAR(at(L"log(x, 2)", 8), 3);
	CHECK_NEAR(at(L"log(8, x)", 2), 3);
	CHECK(FplBuild(L"y = log(x, 2, 3)", none).error.code == FplErrorCode::ArgCount);
	// rounding
	CHECK_NEAR(at(L"trunc(x)", 2.7), 2);
	CHECK_NEAR(at(L"trunc(x)", -2.7), -2);
	CHECK_NEAR(at(L"frac(x)", 2.75), 0.75);
	CHECK_NEAR(at(L"frac(x)", -0.25), 0.75);
	CHECK_NEAR(at(L"clamp(x, 0, 1)", -3), 0);
	CHECK_NEAR(at(L"clamp(x, 0, 1)", 0.5), 0.5);
	CHECK_NEAR(at(L"clamp(x, 0, 1)", 3), 1);
	CHECK(FplBuild(L"y = clamp(x, 1)", none).error.code == FplErrorCode::ArgCount);
	// error and gamma
	CHECK_NEAR(at(L"erfc(x)", x), 1 - std::erf(x));
	CHECK_NEAR_TOL(at(L"erfc(x)", 6), 2.1519736712498913e-17, 1e-28);	// where 1 - erf is 0
	CHECK_NEAR(at(L"lgamma(x)", 5), std::log(24.0));
	CHECK_NEAR(at(L"lgamma(x)", 1000), std::lgamma(1000.0));		// gamma overflows
	// combinatorics: whole numbers exact, smooth between
	CHECK_NEAR(at(L"nCr(5, 2)", 0), 10);
	CHECK_NEAR(at(L"nCr(x, 2)", 10), 45);
	CHECK_NEAR(at(L"nCr(60, 30)", 0), 118264581564861424.0);
	CHECK_NEAR(at(L"nCr(5, x)", 0), 1);
	CHECK_NEAR(at(L"nCr(5, x)", 7), 0);
	CHECK_NEAR(at(L"nCr(5, x)", -1), 0);
	CHECK_NEAR(at(L"ncr(5, 2)", 0), 10);
	CHECK_NEAR(at(L"nPr(5, 2)", 0), 20);
	CHECK_NEAR(at(L"npr(5, 2)", 0), 20);
	CHECK_NEAR(at(L"nPr(5, 0)", 0), 1);
	CHECK_NEAR(at(L"nPr(5, 7)", 0), 0);
	CHECK_NEAR_TOL(at(L"nCr(4.5, x)", 2), std::tgamma(5.5) / (std::tgamma(3.0) * std::tgamma(3.5)), 1e-9);
	CHECK_NEAR_TOL(at(L"nCr(5, x)", 2.0000001), 10, 1e-4);			// continuous at the integers
	CHECK_NEAR_TOL(at(L"nPr(5, x)", 2.0000001), 20, 1e-4);
	CHECK(std::isinf(at(L"nPr(x, 200)", 300)));
	// rounded arguments
	CHECK_NEAR(at(L"gcd(12, 18)", 0), 6);
	CHECK_NEAR(at(L"gcd(-12, 18)", 0), 6);
	CHECK_NEAR(at(L"gcd(0, 5)", 0), 5);
	CHECK_NEAR(at(L"gcd(0, 0)", 0), 0);
	CHECK_NEAR(at(L"gcd(x, 18)", 11.8), 6);
	CHECK_NEAR(at(L"lcm(4, 6)", 0), 12);
	CHECK_NEAR(at(L"lcm(0, 6)", 0), 0);
	CHECK(std::isnan(at(L"gcd(x, 2)", 1e300)));
	// step and sinc
	CHECK_NEAR(at(L"step(x)", -0.1), 0);
	CHECK_NEAR(at(L"step(x)", 0), 1);
	CHECK_NEAR(at(L"step(x)", 0.1), 1);
	CHECK_NEAR(at(L"heaviside(x)", 2), 1);
	CHECK_NEAR(at(L"sinc(x)", 0), 1);
	CHECK_NEAR(at(L"sinc(x)", x), std::sin(x) / x);
	CHECK_NEAR_TOL(at(L"sinc(x)", 1e-5), 1, 1e-9);
	// waves: period 2 pi, as sin
	CHECK_NEAR(at(L"square(x)", 1), 1);
	CHECK_NEAR(at(L"square(x)", 4), -1);
	CHECK_NEAR(at(L"square(x)", 1 + 2 * Pi), 1);
	CHECK_NEAR(at(L"square(x)", -1), -1);
	CHECK_NEAR(at(L"triangle(x)", 0), 0);
	CHECK_NEAR(at(L"triangle(x)", Pi / 2), 1);
	CHECK_NEAR(at(L"triangle(x)", Pi / 4), 0.5);
	CHECK_NEAR_TOL(at(L"triangle(x)", Pi), 0, 1e-12);
	CHECK_NEAR(at(L"triangle(x)", -Pi / 2), -1);
	CHECK_NEAR(at(L"sawtooth(x)", 0), 0);
	CHECK_NEAR(at(L"sawtooth(x)", Pi / 2), 0.5);
	CHECK_NEAR(at(L"sawtooth(x)", 3), 3 / Pi);
	CHECK_NEAR(at(L"sawtooth(x)", 4), 4 / Pi - 2);
	CHECK(std::isnan(at(L"square(x)", std::numeric_limits<double>::infinity())));
	// in degrees: 360 is the period, as sin's
	CHECK_NEAR(at(L"square(x)", 90, true), 1);
	CHECK_NEAR(at(L"square(x)", 270, true), -1);
	CHECK_NEAR(at(L"triangle(x)", 90, true), 1);
	CHECK_NEAR(at(L"sawtooth(x)", 90, true), 0.5);
	CHECK_NEAR(at(L"asec(x)", 2, true), 60);
	CHECK_NEAR(at(L"acot(x)", 1, true), 45);
	// normal distribution
	CHECK_NEAR(at(L"normpdf(x)", 0), 1 / std::sqrt(2 * Pi));
	CHECK_NEAR(at(L"normcdf(x)", 0), 0.5);
	CHECK_NEAR_TOL(at(L"normcdf(x)", 1.96), 0.9750021048517795, 1e-12);
	CHECK_NEAR_TOL(at(L"normcdf(x)", -8), 6.22096057427178e-16, 1e-25);	// out in the tail, by erfc
	CHECK_NEAR(at(L"normpdf(x, 1)", 1), 1 / std::sqrt(2 * Pi));
	CHECK_NEAR(at(L"normpdf(x, 3, 2)", 3), 1 / (2 * std::sqrt(2 * Pi)));
	CHECK_NEAR(at(L"normpdf(x, 3, 2)", 5), at(L"normpdf(x)", 1) / 2);
	CHECK_NEAR(at(L"normcdf(x, 3, 2)", 5), at(L"normcdf(x)", 1));
	CHECK_NEAR(at(L"normcdf(x, 3, 2)", 3), 0.5);
	CHECK(std::isnan(at(L"normpdf(x, 0, -1)", 1)));
	CHECK(std::isnan(at(L"normcdf(x, 0, -1)", 1)));
	CHECK(!std::isfinite(at(L"normpdf(x, 0, 0)", 1)) || at(L"normpdf(x, 0, 0)", 1) == 0);
	CHECK(FplBuild(L"y = normpdf(x, 1, 2, 3)", none).error.code == FplErrorCode::ArgCount);
	// conversions
	CHECK_NEAR(at(L"deg(x)", Pi), 180);
	CHECK_NEAR(at(L"rad(x)", 180), Pi);
	CHECK_NEAR(at(L"sin(rad(x))", 90), 1);
	// other names
	CHECK_NEAR(at(L"tg(x)", x), std::tan(x));
	CHECK_NEAR(at(L"ctg(x)", x), 1 / std::tan(x));
	CHECK_NEAR(at(L"cosec(x)", x), 1 / std::sin(x));
	CHECK_NEAR(at(L"arctg(x)", x), std::atan(x));
	CHECK_NEAR(at(L"arcctg(x)", x), at(L"acot(x)", x));

	// every one: compiled and the slow tree agree, derivatives against differences, printed and read back
	struct Case { const wchar_t* text; double x; };
	const Case cases[] = {
		{ L"asec(x)", 2.5 }, { L"acsc(x)", 2.5 }, { L"acot(x)", 0.7 }, { L"asec(x)", -2.5 }, { L"acsc(x)", -2.5 },
		{ L"sech(x)", 0.7 }, { L"csch(x)", 0.7 }, { L"coth(x)", 0.7 },
		{ L"asech(x)", 0.5 }, { L"acsch(x)", 0.5 }, { L"acsch(x)", -0.5 }, { L"acoth(x)", 2 },
		{ L"log(x, 2)", 3 }, { L"log10(x)", 3 }, { L"erfc(x)", 0.7 }, { L"lgamma(x)", 3.3 },
		{ L"nCr(x, 2.5)", 6.3 }, { L"nCr(6.3, x)", 2.5 }, { L"nPr(x, 2.5)", 6.3 }, { L"nPr(6.3, x)", 2.5 },
		{ L"sinc(x)", 0.7 }, { L"sinc(2x)", 3 }, { L"triangle(x)", 0.7 }, { L"triangle(x)", 4 }, { L"sawtooth(2x)", 0.7 },
		{ L"normpdf(x)", 0.7 }, { L"normcdf(x)", 0.7 }, { L"normpdf(x, 1, 2)", 0.3 }, { L"normcdf(x, 1, 2)", 0.3 },
		{ L"frac(x)", 2.3 }, { L"clamp(x, 0, 1)", 0.4 }, { L"clamp(2x, 0, 1)", 0.2 }, { L"deg(x)", 1 }, { L"rad(x)", 1 },
		{ L"trunc(x)", 2.3 }, { L"step(x)", 2.3 }, { L"square(x)", 0.7 }, { L"gcd(x, 6)", 3.2 }, { L"lcm(x, 6)", 3.2 },
	};
	for (auto& c : cases) {
		FplParseOptions options;
		auto r = FplParse(c.text, options);
		CHECK(!r.error);
		if (r.error)
			continue;
		std::vector<std::wstring> names;
		auto program = FplCompile(r.ast, r.root, names);
		double slots[FplFirstParam] = {};
		slots[FplSlotX] = c.x;
		const double value = program.Eval(slots);
		CHECK_NEAR_TOL(Tree(r.ast, r.root, c.x), value, 1e-9 * (1 + std::abs(value)));
		// its derivative
		FplError error;
		int d = FplDerive(r.ast, r.root, L"x", &error);
		CHECK(d >= 0);
		if (d < 0)
			continue;
		auto dp = FplCompile(r.ast, d, names);
		const double h = 1e-5;
		slots[FplSlotX] = c.x + h;
		const double up = program.Eval(slots);
		slots[FplSlotX] = c.x - h;
		const double down = program.Eval(slots);
		slots[FplSlotX] = c.x;
		CHECK_NEAR_TOL(dp.Eval(slots), (up - down) / (2 * h), 1e-5 * (1 + std::abs(value)));
		// printed, it reads back the same
		auto again = FplParse(FplToString(r.ast, r.root), options);
		CHECK(!again.error);
		if (!again.error)
			CHECK_NEAR_TOL(FplCompile(again.ast, again.root, names).Eval(slots), value, 1e-9 * (1 + std::abs(value)));
	}
	// sinc's derivatives are right through 0, where the formulas aren't, and read back
	const double near0[] = { 0.0, 1e-9, 1e-6, 1e-3, 0.0199, 0.0201, 0.0999, 0.1001, 0.5, -0.5, 3 };
	for (double v : near0) {
		const double d1 = v == 0 ? 0 : (v * v < 1e-6 ? -v / 3 : (std::cos(v) - std::sin(v) / v) / v);
		CHECK_NEAR_TOL(at(L"sinc'(x)", v), d1, 1e-7);
		CHECK_NEAR_TOL(at(L"d/dx sinc(x)", v), d1, 1e-7);
	}
	CHECK_NEAR_TOL(at(L"sinc''(x)", 0), -1.0 / 3, 1e-12);
	CHECK_NEAR_TOL(at(L"sinc''(x)", 0.1 - 1e-9), at(L"sinc''(x)", 0.1 + 1e-9), 1e-6);
	CHECK_NEAR_TOL(at(L"sinc'(x)", 0.02 - 1e-9), at(L"sinc'(x)", 0.02 + 1e-9), 1e-6);
	CHECK_NEAR_TOL(at(L"d/dx d/dx sinc(x)", 0.5), (at(L"sinc'(x)", 0.5 + 1e-5) - at(L"sinc'(x)", 0.5 - 1e-5)) / 2e-5, 1e-6);
	CHECK(FplBuild(L"y = sinc'''(x)", none).error.code == FplErrorCode::NotDifferentiable);
	// primes on those written in terms of others, and error for those of two
	CHECK_NEAR(at(L"frac'(x)", 2.3), 1);
	CHECK_NEAR(at(L"deg'(x)", 2.3), 180 / Pi);
	CHECK_NEAR(at(L"normcdf'(x)", 0.7), at(L"normpdf(x)", 0.7));
	CHECK(FplBuild(L"y = gcd'(x, 2)", none).error.code == FplErrorCode::NotDifferentiable);

	// what the sampler does with them: the steps break, poles are asymptotes
	FplViewport v = View(-10, 10, -8, 8, 1000, 800);
	auto plot = MakePlot({ L"y = step(x)", L"y = square(x)", L"y = sawtooth(x)", L"y = triangle(x)", L"y = coth(x)", L"y = csch(x)", L"y = sinc(x)", L"y = normpdf(x)" });
	plot.Sample(v);
	CHECK_INT(plot.graphs[0].geometry.lines.size(), 2);
	CHECK(plot.graphs[1].geometry.lines.size() >= 6);
	CHECK(plot.graphs[2].geometry.lines.size() >= 3);
	CHECK_INT(plot.graphs[3].geometry.lines.size(), 1);
	CHECK_INT(plot.graphs[4].geometry.asymptotes.size(), 1);
	CHECK_INT(plot.graphs[5].geometry.asymptotes.size(), 1);
	CHECK_INT(plot.graphs[6].geometry.lines.size(), 1);
	CHECK(plot.graphs[6].geometry.holes.empty());
	CHECK_INT(plot.graphs[7].geometry.lines.size(), 1);

	// the new names give way to the user's
	auto param = FplBuild(L"y = step x", { L"step" });
	CHECK(!param.error);
	double slots[FplFirstParam + 1] = {};
	slots[FplSlotX] = 3;
	slots[FplFirstParam] = 2;
	CHECK_NEAR(param.f.Eval(slots), 6);
	CHECK_NEAR(FplBuild(L"y = sin x", { L"sin" }).f.Eval(slots), std::sin(3.0));	// not the old ones
	plot = MakePlot({ L"step = 2", L"y = step x" });
	CHECK(plot.graphs[0].definition);
	CHECK(!plot.graphs[1].expr.error);
	auto values = plot.Slots();
	values[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(values.data()), 6);
	plot = MakePlot({ L"y = rad x", L"rad = 2" });
	CHECK(plot.graphs[1].definition);
	CHECK(!plot.graphs[0].expr.error);
	plot = MakePlot({ L"square(x) = x^2", L"y = square(x) + 1", L"frac = x - 1", L"y = 2frac" });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(!plot.graphs[1].expr.error);
	CHECK(!plot.graphs[3].expr.error);
	values = plot.Slots();
	values[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(values.data()), 10);
	CHECK_NEAR(plot.graphs[3].expr.f.Eval(values.data()), 4);
	// short ones are functions only when called: products as they were, with a slider for what's missing
	auto curve = FplBuild(L"(t, tg)", { L"g" });
	CHECK(!curve.error);
	CHECK(curve.kind == FplKind::Parametric);
	CHECK(FplBuild(L"y = rad", none).error.code == FplErrorCode::UnknownName);
	CHECK(!FplBuild(L"y = x deg", { L"d", L"g" }).error);
	CHECK(FplBuild(L"y = gcd", none).error.code == FplErrorCode::UnknownName);
	// others take an argument without parentheses, as sin does
	CHECK_NEAR(at(L"sech x", x), 1 / std::cosh(x));
	CHECK_NEAR(at(L"step x", 1), 1);
	CHECK_NEAR(at(L"3 square 2x", 1), 3);
	CHECK_NEAR(at(L"sinc^2(x)", x), std::pow(std::sin(x) / x, 2));
}

TEST(SumsAndProducts) {
	std::vector<std::wstring> none;
	auto at = [&](const std::wstring& text, double x, std::vector<std::wstring> names = {}, std::vector<double> values = {}) {
		auto e = FplBuild(L"y = " + text, names);
		CHECK(!e.error);
		std::vector<double> slots(FplFirstParam + names.size());
		slots[FplSlotX] = x;
		for (size_t i = 0; i < values.size(); i++)
			slots[FplFirstParam + i] = values[i];
		return e.error ? NaN : e.f.Eval(slots.data());
	};
	CHECK_NEAR(at(L"sum(k, k, 1, 10)", 0), 55);
	CHECK_NEAR(at(L"sum(k^2, k, 1, 3)", 0), 14);
	CHECK_NEAR(at(L"prod(k, k, 1, 5)", 0), 120);
	CHECK_NEAR(at(L"product(k, k, 1, 5)", 0), 120);
	// with the variable, parameters in any place, and in expressions
	CHECK_NEAR_TOL(at(L"sum(x^k/k!, k, 0, 20)", 1.5), std::exp(1.5), 1e-10);
	CHECK_NEAR(at(L"sum(k x, k, 1, 3)", 2), 12);
	CHECK_NEAR(at(L"sum(k a, k, 1, n)", 2, { L"a", L"n" }, { 3, 4 }), 30);
	CHECK_NEAR(at(L"sum(k, k, a, a + 2)", 0, { L"a" }, { 5 }), 18);
	CHECK_NEAR(at(L"2 sum(k, k, 1, 3) + 1", 0), 13);
	CHECK_NEAR(at(L"sum(k, k, 1, 3)^2", 0), 36);
	CHECK_NEAR(at(L"x sum(k, k, 1, 3)", 2), 12);
	// the letters, implicit products of the index, a longer name, symbols
	CHECK_NEAR(at(L"sum(2k, k, 1, 3)", 0), 12);
	CHECK_NEAR(at(L"sum(kx, k, 1, 3)", 2), 12);
	CHECK_NEAR(at(L"sum(idx^2, idx, 1, 3)", 0), 14);
	CHECK_NEAR(at(std::wstring(1, wchar_t(0x2211)) + L"(k, k, 1, 4)", 0), 10);
	CHECK_NEAR(at(std::wstring(1, wchar_t(0x220F)) + L"(k, k, 1, 4)", 0), 24);
	// limits rounded, empty, one term, negative
	CHECK_NEAR(at(L"sum(k, k, 0.6, 3.4)", 0), 6);
	CHECK_NEAR(at(L"sum(k, k, 3, 1)", 0), 0);
	CHECK_NEAR(at(L"prod(k, k, 3, 1)", 0), 1);
	CHECK_NEAR(at(L"sum(k, k, 2, 2)", 0), 2);
	CHECK_NEAR(at(L"sum(k, k, -2, 2)", 0), 0);
	// too many, undefined limits or terms
	CHECK_NEAR(at(L"sum(1, k, 1, 100000)", 0), 100000);
	CHECK(std::isnan(at(L"sum(1, k, 1, 100001)", 0)));
	CHECK(std::isnan(at(L"sum(1, k, 1, sqrt(x))", -1)));
	CHECK(std::isnan(at(L"sum(sqrt(k), k, -1, 1)", 0)));
	// nested: the inner limits can use the outer index, and each index is its own
	CHECK_NEAR(at(L"sum(sum(j, j, 1, k), k, 1, 3)", 0), 10);
	CHECK_NEAR(at(L"sum(k prod(j, j, 1, k), k, 1, 3)", 0), 1 + 4 + 18);
	CHECK_NEAR(at(L"sum(k, k, 1, 2) + sum(k, k, 1, 3)", 0), 9);
	CHECK_NEAR(at(L"sum(sum(k, k, 1, 2), k, 1, 3)", 0), 9);
	// an index named as a parameter: it's the index inside, the parameter outside
	CHECK_NEAR(at(L"a + sum(a, a, 1, 3)", 0, { L"a" }, { 100 }), 106);
	CHECK_NEAR(at(L"sum(a + b, a, 1, 2)", 0, { L"a", L"b" }, { 100, 10 }), 23);
	// in an integral, and one in it
	CHECK_NEAR_TOL(at(L"integral(sum(x^k, k, 0, 2), 0, 1)", 0), 1 + 0.5 + 1.0 / 3, 1e-9);
	CHECK_NEAR_TOL(at(L"sum(integral(k x, 0, 1), k, 1, 3)", 0), 3, 1e-9);
	// in a user's function, and one that takes the index
	auto plot = MakePlot({ L"f(a) = sum(a^k, k, 0, 2)", L"y = f(x) + f(2)" });
	CHECK(!plot.graphs[1].expr.error);
	auto slots = plot.Slots();
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 13 + 7);
	plot = MakePlot({ L"g(a) = a^2", L"y = sum(g(k), k, 1, 3)" });
	CHECK(!plot.graphs[1].expr.error);
	slots = plot.Slots();
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 14);
	// a definition takes its value; a substitution works
	plot = MakePlot({ L"c = sum(k, k, 1, 4)", L"y = c x" });
	CHECK(plot.graphs[0].definition);
	CHECK_NEAR(plot.DefinitionValue(0), 10);
	plot = MakePlot({ L"s1 = sum(x^k, k, 0, 2)", L"y = 2s1" });
	CHECK(!plot.graphs[1].expr.error);
	// an unknown limit asks for a parameter, not the index
	auto unknown = FplBuild(L"y = sum(k, k, 1, n)", none);
	CHECK(unknown.error.code == FplErrorCode::UnknownName);
	CHECK_STR(unknown.error.name, L"n");
	// errors: the index outside, as a limit, a variable's name, not a name, the wrong count
	auto outside = FplBuild(L"y = k + sum(k, k, 1, 3)", none);
	CHECK(outside.error.code == FplErrorCode::UnknownName);
	CHECK_STR(outside.error.name, L"k");
	CHECK_INT(outside.error.pos, 4);
	CHECK(FplBuild(L"y = sum(k, k, 1, k)", none).error.code == FplErrorCode::UnknownName);
	CHECK(FplBuild(L"y = sum(x, x, 1, 3)", none).error.code == FplErrorCode::ReservedName);
	// an index that isn't a name is said to be that, wherever it is, whatever the rest is
	for (const wchar_t* text : { L"y = sum(k, 2, 1, 3)", L"y = sum(x, 2, 1, 3)", L"y = sum(k, 2k, 1, 3)", L"y = 1 + sum(k, 2, 1, 3)" }) {
		auto bad = FplBuild(text, none);
		CHECK(bad.error.code == FplErrorCode::BadIndex);
		CHECK_STR(bad.error.name, L"sum");
		CHECK(text[bad.error.pos] == L'2');
		CHECK(bad.error.Message().find(L"index") != std::wstring::npos);
	}
	auto badProduct = FplBuild(L"y = prod(k, 2, 1, 3)", none);
	CHECK(badProduct.error.code == FplErrorCode::BadIndex);
	CHECK_STR(badProduct.error.name, L"prod");
	CHECK(FplBuild(L"y = sum(x, , 1, 3)", none).error.code == FplErrorCode::BadIndex);
	CHECK(FplBuild(L"y = prod(2, 2)", none).error.code == FplErrorCode::ArgCount);
	CHECK(FplBuild(L"y = sum(3)", none).error.code == FplErrorCode::NotAList);
	// sum of a list is as it was; prod gives way to the user's
	auto list = MakePlot({ L"L = [1, 2, 3]", L"y = sum(L) x" });
	CHECK(!list.graphs[1].expr.error);
	slots = list.Slots();
	slots[FplSlotX] = 2;
	CHECK_NEAR(list.graphs[1].expr.f.Eval(slots.data()), 12);
	plot = MakePlot({ L"prod = 2", L"y = prod x" });
	CHECK(plot.graphs[0].definition);
	// q as the index is the index
	CHECK_NEAR(at(L"sum(q, q, 1, 3)", 0), 6);

	// compiled and the slow tree; derivatives; printed and read back
	struct Case { const wchar_t* text; double x; };
	const Case cases[] = {
		{ L"sum(x^k/k!, k, 0, 6)", 0.7 }, { L"sum(sin(k x)/k, k, 1, 5)", 0.7 }, { L"prod(x + k, k, 1, 3)", 0.7 },
		{ L"prod(1 + x/k, k, 1, 4)", 0.7 }, { L"sum(prod(x + j, j, 1, k), k, 1, 3)", 0.7 }, { L"sum(k x, k, 1, 3) sin x", 0.7 },
	};
	for (auto& c : cases) {
		FplParseOptions options;
		auto r = FplParse(c.text, options);
		CHECK(!r.error);
		if (r.error)
			continue;
		auto program = FplCompile(r.ast, r.root, none);
		double values[FplFirstParam] = {};
		values[FplSlotX] = c.x;
		const double value = program.Eval(values);
		CHECK_NEAR_TOL(Tree(r.ast, r.root, c.x), value, 1e-9);
		FplError error;
		int d = FplDerive(r.ast, r.root, L"x", &error);
		CHECK(d >= 0);
		if (d < 0)
			continue;
		auto derivative = FplCompile(r.ast, d, none);
		const double h = 1e-5;
		values[FplSlotX] = c.x + h;
		const double up = program.Eval(values);
		values[FplSlotX] = c.x - h;
		const double down = program.Eval(values);
		values[FplSlotX] = c.x;
		CHECK_NEAR_TOL(derivative.Eval(values), (up - down) / (2 * h), 1e-5 * (1 + std::abs(value)));
		auto again = FplParse(FplToString(r.ast, r.root), options);
		CHECK(!again.error);
		if (!again.error)
			CHECK_NEAR_TOL(FplCompile(again.ast, again.root, none).Eval(values), value, 1e-9 * (1 + std::abs(value)));
	}
	CHECK_NEAR(at(L"d/dx sum(k x, k, 1, 3)", 5), 6);
	// constant ones fold, so they're cheap and read nothing
	auto folded = FplBuild(L"y = sum(k, k, 1, 1000)", none);
	CHECK(folded.f.IsConstant());
	CHECK(!FplBuild(L"y = sum(x, k, 1, 3)", none).f.IsConstant());
	// it draws, and a slot array just big enough is enough
	FplViewport v = View();
	plot = MakePlot({ L"y = sum(sin((2k - 1)x)/(2k - 1), k, 1, 5)" });
	plot.Sample(v);
	CHECK(!plot.graphs[0].geometry.lines.empty());
}

TEST(AutoQ) {
	FplParam automatic = Param(L"q", 2), chosen = Param(L"q", 2);
	automatic.flags = FplParamAuto;
	// a slider only added for another line leaves q as theta in a line with r
	auto plot = MakePlot({ L"r = 2 + cos 3q", L"y = q x", L"sin q" }, { automatic });
	CHECK(!plot.graphs[0].expr.error);
	CHECK(plot.graphs[0].expr.kind == FplKind::Polar);
	auto slots = plot.Slots();
	slots[FplSlotTheta] = 0.4;
	slots[FplSlotX] = 3;
	CHECK_NEAR(plot.graphs[0].expr.f.Eval(slots.data()), 2 + std::cos(1.2));
	// the other lines have the slider
	CHECK_NEAR(plot.graphs[1].expr.f.Eval(slots.data()), 6.0);
	CHECK(plot.graphs[2].expr.kind == FplKind::Function);
	CHECK_NEAR(plot.graphs[2].expr.f.Eval(slots.data()), std::sin(2.0));
	// and one the app chose has it everywhere, in a line with r too
	plot = MakePlot({ L"r = q", L"y = q x" }, { chosen });
	CHECK(plot.graphs[0].expr.kind == FplKind::Polar);
	slots = plot.Slots();
	slots[FplSlotTheta] = 0.4;
	CHECK_NEAR(plot.graphs[0].expr.f.Eval(slots.data()), 2.0);
	// no parameter: as ever. A line defining q: it's that, even over an auto one
	plot = MakePlot({ L"r = q" });
	slots = plot.Slots();
	slots[FplSlotTheta] = 0.4;
	CHECK_NEAR(plot.graphs[0].expr.f.Eval(slots.data()), 0.4);
}

TEST(TicksInState) {
	FplSavedState a;
	a.piTicks = true;
	a.polarGrid = true;
	FplSavedState b;
	CHECK(FplLoadState(FplSaveState(a), b));
	CHECK(b.hasTicks && b.piTicks && b.polarGrid);
	a.piTicks = a.polarGrid = false;
	CHECK(FplLoadState(FplSaveState(a), b));
	CHECK(b.hasTicks && !b.piTicks && !b.polarGrid);
	a.polarGrid = true;
	CHECK(FplLoadState(FplSaveState(a), b));
	CHECK(b.hasTicks && !b.piTicks && b.polarGrid);
	// a text from before says nothing, so the control's stay
	CHECK(FplLoadState(L"FunctionPlot 1\ndegrees 0\n", b));
	CHECK(!b.hasTicks);
}

TEST(HeatmapsInSvg) {
	// the PNG: signature, size, and every chunk's check; its pixels through the stored blocks
	std::vector<uint8_t> rgba;
	const int w = 300, h = 300;		// past one stored block
	rgba.reserve(w * h * 4);
	for (int i = 0; i < w * h; i++)
		for (int c = 0; c < 4; c++)
			rgba.push_back(static_cast<uint8_t>((i * 7 + c * 31) & 255));
	const std::string png = FplEncodePng(w, h, rgba);
	CHECK(png.compare(0, 8, std::string("\x89PNG\r\n\x1A\n", 8)) == 0);
	auto be32 = [&](size_t at) {
		uint32_t v = 0;
		for (int i = 0; i < 4; i++)
			v = (v << 8) | static_cast<uint8_t>(png[at + i]);
		return v;
	};
	auto crc32 = [&](size_t at, size_t n) {
		uint32_t crc = 0xFFFFFFFFu;
		for (size_t i = 0; i < n; i++) {
			crc ^= static_cast<uint8_t>(png[at + i]);
			for (int k = 0; k < 8; k++)
				crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
		}
		return crc ^ 0xFFFFFFFFu;
	};
	std::string data;
	std::vector<std::string> types;
	for (size_t at = 8; at < png.size();) {
		const uint32_t n = be32(at);
		types.push_back(png.substr(at + 4, 4));
		CHECK(crc32(at + 4, n + 4) == be32(at + 8 + n));
		if (types.back() == "IDAT")
			data.append(png, at + 8, n);
		if (types.back() == "IHDR") {
			CHECK(be32(at + 8) == uint32_t(w));
			CHECK(be32(at + 12) == uint32_t(h));
		}
		at += 12 + n;
	}
	CHECK(types == (std::vector<std::string>{ "IHDR", "IDAT", "IEND" }));
	std::string raw;
	size_t pos = 2;
	for (bool last = false; !last && pos < data.size();) {
		last = data[pos] & 1;
		const size_t n = static_cast<uint8_t>(data[pos + 1]) | (size_t(static_cast<uint8_t>(data[pos + 2])) << 8);
		const size_t inverse = static_cast<uint8_t>(data[pos + 3]) | (size_t(static_cast<uint8_t>(data[pos + 4])) << 8);
		CHECK((n ^ inverse) == 0xFFFF);
		raw.append(data, pos + 5, n);
		pos += 5 + n;
	}
	CHECK_INT(raw.size(), (w * 4 + 1) * h);
	bool same = true;
	for (int y = 0; y < h; y++) {
		same = same && raw[y * (size_t(w) * 4 + 1)] == 0;
		same = same && std::memcmp(raw.data() + y * (size_t(w) * 4 + 1) + 1, rgba.data() + size_t(y) * w * 4, size_t(w) * 4) == 0;
	}
	CHECK(same);
	uint32_t a = 1, b = 0;
	for (unsigned char c : raw) {
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	uint32_t adler = 0;
	for (int i = 0; i < 4; i++)
		adler = (adler << 8) | static_cast<uint8_t>(data[pos + i]);
	CHECK(adler == ((b << 16) | a));
	CHECK(FplEncodePng(0, 5, {}).empty());
	CHECK(FplEncodePng(2, 2, { 1, 2, 3 }).empty());

	// the SVG: a picture under the grid, and the scale's colors and values
	FplViewport v = View(-10, 10, -8, 8, 1000, 800);
	auto plot = MakePlot({ L"z = sin x + cos y" });
	plot.graphs[0].flags |= FplGraphHeatmap;
	plot.Sample(v);
	CHECK(!plot.graphs[0].geometry.heatmap.Empty());
	FplSvgStyle style;
	style.graphColors = { 0xFF0000 };
	std::string svg = FplWriteSvg(plot, v, style);
	const size_t image = svg.find("<image ");
	CHECK(image != std::string::npos);
	CHECK(svg.find("data:image/png;base64,") != std::string::npos);
	CHECK(svg.find("opacity=\"0.85\"") != std::string::npos);
	CHECK(image < svg.find("stroke=\"#dddddd\""));
	CHECK(svg.find("<linearGradient") != std::string::npos);
	CHECK(svg.find("url(#scale0)") != std::string::npos);
	style.showColorScale = false;
	CHECK(FplWriteSvg(plot, v, style).find("<linearGradient") == std::string::npos);
	CHECK(FplWriteSvg(plot, v, style).find("<image ") != std::string::npos);
	// hidden, or no heat map: nothing
	style.showColorScale = true;
	plot.graphs[0].flags |= FplGraphHidden;
	CHECK(FplWriteSvg(plot, v, style).find("<image ") == std::string::npos);
	auto curve = MakePlot({ L"y = x" });
	curve.Sample(v);
	CHECK(FplWriteSvg(curve, v, style).find("<linearGradient") == std::string::npos);
	// two: their marks
	auto both = MakePlot({ L"z = sin x", L"z = cos y" });
	both.graphs[0].flags |= FplGraphHeatmap;
	both.graphs[1].flags |= FplGraphHeatmap;
	both.Sample(v);
	style.graphColors = { 0xFF0000, 0x00FF00 };
	svg = FplWriteSvg(both, v, style);
	CHECK(svg.find("url(#scale1)") != std::string::npos);
}

TEST(ReadmeMatchesParser) {
	// next to the sources, wherever the tests were started from
	std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path() / L"FunctionPlot" / L"README.md";
	std::ifstream file(path, std::ios::binary);
	CHECK(file.good());
	if (!file.good())
		return;
	const std::string doc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::set<std::wstring> words;
	std::wstring word;
	for (char c : doc) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			word += static_cast<wchar_t>(c);
		else if (!word.empty()) {
			words.insert(word);
			word.clear();
		}
	}
	for (auto& name : FplFunctionNames())
		if (!words.count(name)) {
			wprintf(L"  the README doesn't list '%s'\n", name.c_str());
			CHECK(words.count(name) != 0);
		}
	// its lists of names are exact, in either direction
	auto listAfter = [&](const char* marker) {
		std::set<std::wstring> names;
		const size_t at = doc.find(marker);
		CHECK(at != std::string::npos);
		if (at == std::string::npos)
			return names;
		const size_t from = doc.find('`', at) + 1, to = doc.find('`', from);
		std::wstring current;
		for (size_t i = from; i <= to; i++) {
			if (doc[i] == ' ' || i == to) {
				if (!current.empty())
					names.insert(current);
				current.clear();
			}
			else
				current += static_cast<wchar_t>(doc[i]);
		}
		return names;
	};
	auto yielding = FplYieldingNames(), callOnly = FplCallOnlyNames();
	CHECK(listAfter("These are the names:") == std::set<std::wstring>(yielding.begin(), yielding.end()));
	CHECK(listAfter("is still t times g:") == std::set<std::wstring>(callOnly.begin(), callOnly.end()));
	// and those names are functions
	auto all = FplFunctionNames();
	for (auto& name : yielding)
		CHECK(std::find(all.begin(), all.end(), name) != all.end());
	for (auto& name : callOnly)
		CHECK(std::find(yielding.begin(), yielding.end(), name) != yielding.end());
}

TEST(SlowCurves) {
	FplViewport v = View(-10, 10, -8, 8, 1000, 800);
	// a plain curve isn't, however long; nor a short sum's programs are hidden from the clock
	auto plain = MakePlot({ L"y = sin x + cos x + tan x + exp x + sin 2x + cos 2x + sin 3x + cos 3x" });
	CHECK(!plain.graphs[0].expr.f.HasSubPrograms());
	CHECK_NEAR(plain.EvalCost(0), 0.0);
	CHECK(!plain.IsHeavy(0, false));
	// a long sum is, for each kind of curve
	const wchar_t* slow = L"sum(sin(k x)/k, k, 1, 20000)";
	auto plot = MakePlot({ (std::wstring(L"y = ") + slow).c_str(), L"x = sum(sin(k y)/k, k, 1, 20000)",
		L"r = 3 + sum(sin(k \x03B8)/k, k, 1, 20000)", L"(sum(sin(k t)/k, k, 1, 20000), t)", L"y = sum(sin(k x)/k, k, 1, 3)",
		L"y = integral(sum(sin(k t)/k, k, 1, 20), t, 0, x)", L"z = sum(sin(k x)/k, k, 1, 20000)" });
	for (auto& g : plot.graphs)
		CHECK(!g.expr.error);
	CHECK(plot.graphs[0].expr.f.HasSubPrograms());
	CHECK(plot.graphs[4].expr.f.HasSubPrograms());
	for (int i : { 0, 1, 2, 3, 5 }) {
		CHECK(plot.EvalCost(i) > 8e-6);
		CHECK(plot.IsHeavy(i, false));
		CHECK(!plot.IsHeavy(i, true));
	}
	CHECK(plot.IsHeavy(6, false));
	CHECK(plot.IsHeavy(6, true));
	// sampled at once: few evaluations, unrefined, no analysis, still the curve
	for (int i : { 0, 1, 2, 3 }) {
		auto coarse = plot.CoarseOptions(i, v);
		CHECK(coarse.maxDepth == 0);
		CHECK(!coarse.singularities);
		plot.SampleGraph(i, v, coarse);
		auto& g = plot.graphs[i].geometry;
		CHECK(!g.lines.empty());
		CHECK(g.evals <= 300);
		CHECK(g.evals >= 16);
	}
	// its flags still count for the full one, and a surface's cells are the coarse ones
	CHECK(plot.OptionsFor(0, {}).singularities);
	CHECK_NEAR(plot.CoarseOptions(6, v).cellSize, 16.0);
	auto implicit = MakePlot({ L"x^2 + y^2 = 4" });
	CHECK_NEAR(implicit.CoarseOptions(0, v).cellSize, 16.0);
	CHECK(implicit.IsHeavy(0, false));
	// the thread's result is the whole curve: as sampling it here is
	auto medium = MakePlot({ L"y = sum(sin(k x)/k, k, 1, 200)" });
	FplJob job;
	job.graph = 0;
	job.generation = 7;
	job.expr = medium.graphs[0].expr;
	job.slots = medium.Slots();
	job.view = v;
	job.options = medium.OptionsFor(0, {});
	auto result = FplWorker::Run(job);
	medium.SampleGraph(0, v);
	CHECK_INT(result.generation, 7);
	CHECK_INT(result.geometry.lines.size(), medium.graphs[0].geometry.lines.size());
	CHECK_INT(result.geometry.evals, medium.graphs[0].geometry.evals);
	// and a slow surface's mesh on the thread, 96 by 96 as ever
	auto surface = MakePlot({ L"z = sum(sin(k x)/k, k, 1, 5)" });
	FplBox3 box{ -10, 10, -10, 10, -10, 10 };
	FplJob meshJob;
	meshJob.graph = 0;
	meshJob.expr = surface.graphs[0].expr;
	meshJob.slots = surface.Slots();
	meshJob.mesh = true;
	meshJob.box = box;
	auto meshed = FplWorker::Run(meshJob);
	surface.Build3D(box);
	CHECK_INT(meshed.triangles.indices.size(), surface.graphs[0].mesh.indices.size());
	surface.Build3DGraph(0, box, 16);
	CHECK(surface.graphs[0].mesh.indices.size() < meshed.triangles.indices.size());
	// features: found for a quick sum; not looked for on a very slow one, or against one
	auto quick = MakePlot({ L"y = sum(x^k, k, 1, 2) - 2" });
	quick.Sample(v);
	quick.FindFeatures(0, v);
	CHECK(!quick.graphs[0].features.empty());
	auto two = MakePlot({ L"y = sum(sin(k x)/k, k, 1, 100000)", L"y = x" });
	two.FindFeatures(0, v);
	CHECK(two.graphs[0].features.empty());
	two.FindFeatures(1, v);
	for (auto& f : two.graphs[1].features)
		CHECK(f.kind != FplFeatureKind::Intersection);
}

TEST(SpecialFunctions) {
	std::vector<std::wstring> none;
	auto at = [&](const std::wstring& text, double x) {
		auto e = FplBuild(L"y = " + text, none);
		CHECK(!e.error);
		double slots[FplFirstParam] = {};
		slots[FplSlotX] = x;
		return e.error ? NaN : e.f.Eval(slots);
	};
	const double close = 1e-9;
	// beta
	CHECK_NEAR_TOL(at(L"beta(2, 3)", 0), 1.0 / 12, close);
	CHECK_NEAR_TOL(at(L"beta(0.5, 0.5)", 0), Pi, close);
	CHECK_NEAR_TOL(at(L"beta(x, 3)", 2.5), std::tgamma(2.5) * 2 / std::tgamma(5.5), close);
	CHECK_NEAR_TOL(at(L"beta(-0.5, 2)", 0), std::tgamma(-0.5) * 1 / std::tgamma(1.5), close);
	// Bessel at known values
	CHECK_NEAR_TOL(at(L"besselj(0, 1)", 0), 0.7651976865579666, close);
	CHECK_NEAR_TOL(at(L"besselj(1, 1)", 0), 0.44005058574493355, close);
	CHECK_NEAR_TOL(at(L"bessely(0, 1)", 0), 0.08825696421567696, close);
	CHECK_NEAR_TOL(at(L"besseli(0, 1)", 0), 1.2660658777520084, close);
	CHECK_NEAR_TOL(at(L"besselk(0, 1)", 0), 0.42102443824070834, close);
	CHECK_NEAR_TOL(at(L"besselj(0, 0)", 0), 1, close);
	CHECK_NEAR_TOL(at(L"besselj(2, 0)", 0), 0, close);
	CHECK_NEAR_TOL(at(L"besselj(0.5, 2)", 0), std::sqrt(2 / (Pi * 2)) * std::sin(2.0), close);	// half orders are elementary
	CHECK_NEAR_TOL(at(L"besselj(-0.5, 2)", 0), std::sqrt(2 / (Pi * 2)) * std::cos(2.0), close);
	CHECK_NEAR_TOL(at(L"bessely(0.5, 2)", 0), -std::sqrt(2 / (Pi * 2)) * std::cos(2.0), close);
	CHECK_NEAR_TOL(at(L"bessely(-0.5, 2)", 0), std::sqrt(2 / (Pi * 2)) * std::sin(2.0), close);
	CHECK_NEAR_TOL(at(L"besseli(0.5, 2)", 0), std::sqrt(2 / (Pi * 2)) * std::sinh(2.0), close);
	CHECK_NEAR_TOL(at(L"besseli(-0.5, 2)", 0), std::sqrt(2 / (Pi * 2)) * std::cosh(2.0), close);
	CHECK_NEAR_TOL(at(L"besselk(0.5, 2)", 0), std::sqrt(Pi / 4) * std::exp(-2.0), close);
	CHECK_NEAR_TOL(at(L"besselk(-0.5, 2)", 0), at(L"besselk(0.5, 2)", 0), close);
	// whole orders below 0 and x below 0
	CHECK_NEAR_TOL(at(L"besselj(-1, 1)", 0), -0.44005058574493355, close);
	CHECK_NEAR_TOL(at(L"besselj(-2, 1)", 0), at(L"besselj(2, 1)", 0), close);
	CHECK_NEAR_TOL(at(L"besselj(1, -1)", 0), -0.44005058574493355, close);
	CHECK_NEAR_TOL(at(L"besselj(2, -1)", 0), at(L"besselj(2, 1)", 0), close);
	CHECK_NEAR_TOL(at(L"bessely(-1, 1)", 0), -at(L"bessely(1, 1)", 0), close);
	CHECK_NEAR_TOL(at(L"besseli(1, -1)", 0), -at(L"besseli(1, 1)", 0), close);
	CHECK(std::isnan(at(L"besselj(0.5, -1)", 0)));
	CHECK(std::isnan(at(L"bessely(0, -1)", 0)));
	CHECK(std::isnan(at(L"besseli(0.5, -1)", 0)));
	CHECK(std::isnan(at(L"besselk(0, -1)", 0)));
	CHECK(at(L"bessely(0, 0)", 0) < -1e300);
	CHECK(at(L"besselk(0, 0)", 0) > 1e300);
	// far out, without complaint
	CHECK(std::abs(at(L"besselj(0, 1000000)", 0)) < 0.01);
	CHECK(std::isfinite(at(L"bessely(3, 1000)", 0)));
	// Airy at known values, through the changes of method
	CHECK_NEAR_TOL(at(L"airyai(0)", 0), 0.35502805388781723, close);
	CHECK_NEAR_TOL(at(L"airybi(0)", 0), 0.6149266274460007, close);
	CHECK_NEAR_TOL(at(L"airyai(1)", 0), 0.13529242, 1e-7);
	CHECK_NEAR_TOL(at(L"airybi(1)", 0), 1.2074235950, 1e-8);
	CHECK_NEAR_TOL(at(L"airyai(-1)", 0), 0.53556088329, 1e-9);
	CHECK_NEAR_TOL(at(L"airybi(-1)", 0), 0.10399738949, 1e-9);
	CHECK_NEAR_TOL(at(L"airyai(-2)", 0), 0.22740742820, 1e-9);
	CHECK_NEAR_TOL(at(L"airybi(-2)", 0), -0.41230258795, 1e-9);
	CHECK_NEAR_TOL(at(L"airyai'(0)", 0), -0.25881940379, 1e-9);
	CHECK_NEAR_TOL(at(L"airyai'(0)", 0), -1 / (std::cbrt(3.0) * std::tgamma(1.0 / 3)), 1e-12);
	CHECK_NEAR_TOL(at(L"airybi'(0)", 0), std::pow(3.0, 1.0 / 6) / std::tgamma(1.0 / 3), 1e-12);
	for (double edge : { 1.0, -1.0 }) {
		CHECK_NEAR_TOL(at(L"airyai(x)", edge - 1e-9), at(L"airyai(x)", edge + 1e-9), 1e-8);
		CHECK_NEAR_TOL(at(L"airybi(x)", edge - 1e-9), at(L"airybi(x)", edge + 1e-9), 1e-8);
		CHECK_NEAR_TOL(at(L"airyai'(x)", edge - 1e-9), at(L"airyai'(x)", edge + 1e-9), 1e-8);
		CHECK_NEAR_TOL(at(L"airybi'(x)", edge - 1e-9), at(L"airybi'(x)", edge + 1e-9), 1e-8);
	}
	// the equation they solve, and their Wronskian, all over
	for (double v : { -8.0, -3.3, -1.5, -0.5, 0.0, 0.5, 1.5, 3.3, 6.0 }) {
		const double h = 1e-4;
		auto second = [&](const wchar_t* f) { return (at(f, v + h) - 2 * at(f, v) + at(f, v - h)) / (h * h); };
		CHECK_NEAR_TOL(second(L"airyai(x)"), v * at(L"airyai(x)", v), 1e-5);
		CHECK_NEAR_TOL(second(L"airybi(x)"), v * at(L"airybi(x)", v), 1e-3 * (1 + std::abs(at(L"airybi(x)", v))));
		CHECK_NEAR_TOL(at(L"airyai(x)", v) * at(L"airybi'(x)", v) - at(L"airyai'(x)", v) * at(L"airybi(x)", v), 1 / Pi, 1e-9);
	}
	CHECK(at(L"airyai(x)", 50) < 1e-50);
	// zeta
	CHECK_NEAR_TOL(at(L"zeta(2)", 0), Pi * Pi / 6, close);
	CHECK_NEAR_TOL(at(L"zeta(4)", 0), Pi * Pi * Pi * Pi / 90, close);
	CHECK_NEAR_TOL(at(L"zeta(0)", 0), -0.5, close);
	CHECK_NEAR_TOL(at(L"zeta(-1)", 0), -1.0 / 12, close);
	CHECK_NEAR_TOL(at(L"zeta(-2)", 0), 0, close);
	CHECK(std::isnan(at(L"zeta(1)", 0)));
	CHECK(FplBuild(L"y = zeta'(x)", none).error.code == FplErrorCode::NotDifferentiable);
	CHECK(!FplBuild(L"y = zeta(x)", none).error);		// its analysis goes by differences
	// Lambert W: what it's the inverse of, on both branches, up to the branch point
	CHECK_NEAR_TOL(at(L"lambertw(1)", 0), 0.5671432904097838, close);
	CHECK_NEAR_TOL(at(L"lambertw(0)", 0), 0, close);
	CHECK_NEAR_TOL(at(L"lambertw(e)", 0), 1, close);
	CHECK_NEAR_TOL(at(L"lambertwm1(-0.2)", 0), -2.542641357773526, close);
	for (double v : { -0.3678, -0.3, -0.1, -1e-5, 1e-5, 0.1, 1.0, 2.0, 2.9, 3.1, 10.0, 1e5, 1e100 }) {
		const double w = at(L"lambertw(x)", v);
		CHECK(w >= -1);
		CHECK_NEAR_TOL(w * std::exp(w), v, 1e-9 * (std::abs(v) + 1e-6));
	}
	for (double v : { -0.3678, -0.3, -0.25001, -0.2499, -0.1, -1e-5, -1e-100 }) {
		const double w = at(L"lambertwm1(x)", v);
		CHECK(w <= -1);
		CHECK_NEAR_TOL(w * std::exp(w), v, 1e-9 * std::abs(v));
	}
	CHECK_NEAR_TOL(at(L"lambertw(-1/e)", 0), -1, 1e-6);
	CHECK_NEAR_TOL(at(L"lambertwm1(-1/e)", 0), -1, 1e-6);
	CHECK(std::isnan(at(L"lambertw(-1)", 0)));
	CHECK(std::isnan(at(L"lambertwm1(-1)", 0)));
	CHECK(std::isnan(at(L"lambertwm1(1)", 0)));
	CHECK_NEAR_TOL(at(L"lambertw'(x)", 0), 1, close);		// through 0, where w/(x(1 + w)) isn't defined
	// elliptic: by their own integrals, by m not k
	CHECK_NEAR_TOL(at(L"ellipk(0)", 0), Pi / 2, close);
	CHECK_NEAR_TOL(at(L"ellipe(0)", 0), Pi / 2, close);
	CHECK_NEAR_TOL(at(L"ellipe(1)", 0), 1, close);
	CHECK_NEAR_TOL(at(L"ellipk(0.5)", 0), 1.8540746773013719, close);
	CHECK_NEAR_TOL(at(L"ellipe(0.5)", 0), 1.3506438810476755, close);
	for (double m : { -2.0, -0.5, 0.3, 0.5, 0.9 }) {
		const std::wstring mm = std::to_wstring(m);
		CHECK_NEAR_TOL(at(L"ellipk(" + mm + L")", 0), at(L"integral(1/sqrt(1 - (" + mm + L") sin(x)^2), 0, pi/2)", 0), 1e-7);
		CHECK_NEAR_TOL(at(L"ellipe(" + mm + L")", 0), at(L"integral(sqrt(1 - (" + mm + L") sin(x)^2), 0, pi/2)", 0), 1e-7);
	}
	for (double phi : { 0.3, 1.0, 1.5 }) {
		const std::wstring p = std::to_wstring(phi);
		CHECK_NEAR_TOL(at(L"ellipf(" + p + L", 0.5)", 0), at(L"integral(1/sqrt(1 - 0.5 sin(x)^2), 0, " + p + L")", 0), 1e-7);
		CHECK_NEAR_TOL(at(L"ellipe(" + p + L", 0.5)", 0), at(L"integral(sqrt(1 - 0.5 sin(x)^2), 0, " + p + L")", 0), 1e-7);
	}
	CHECK_NEAR_TOL(at(L"ellipf(pi/2, 0.5)", 0), at(L"ellipk(0.5)", 0), close);
	CHECK_NEAR_TOL(at(L"ellipe(pi/2, 0.5)", 0), at(L"ellipe(0.5)", 0), close);
	CHECK(at(L"ellipk(1)", 0) > 1e300);
	CHECK(std::isnan(at(L"ellipk(1.5)", 0)));
	CHECK(std::isnan(at(L"ellipe(1.5)", 0)));
	CHECK(std::isnan(at(L"ellipf(1, 1.5)", 0)));
	CHECK(std::isnan(at(L"ellipf(1, -1)", 0)));
	CHECK(FplBuild(L"y = ellipe(1, 2, 3)", none).error.code == FplErrorCode::ArgCount);
	CHECK(FplBuild(L"y = besselj(1)", none).error.code == FplErrorCode::ArgCount);

	// compiled and the slow tree; derivatives against differences; printed and read back
	struct Case { const wchar_t* text; double x; };
	const Case cases[] = {
		{ L"beta(x, 2)", 1.5 }, { L"beta(3, 2x)", 1.5 }, { L"beta(x, x)", 1.5 },
		{ L"besselj(1, x)", 2.3 }, { L"besselj(0, x)", 2.3 }, { L"besselj(0.5, 2x)", 2.3 }, { L"besselj(-1.5, x)", 2.3 },
		{ L"bessely(0, x)", 2.3 }, { L"bessely(2.5, x)", 2.3 }, { L"besseli(1, x)", 2.3 }, { L"besseli(0, x)", 2.3 },
		{ L"besselk(1, x)", 2.3 }, { L"besselk(0, x)", 2.3 },
		{ L"airyai(x)", 0.5 }, { L"airyai(x)", -3 }, { L"airyai(x)", 3 }, { L"airybi(x)", 0.5 }, { L"airybi(x)", -3 }, { L"airybi(2x)", 1.5 },
		{ L"lambertw(x)", 2.3 }, { L"lambertw(x)", -0.1 }, { L"lambertwm1(x)", -0.1 },
		{ L"ellipk(x)", 0.5 }, { L"ellipe(x)", 0.5 }, { L"ellipk(x)", -0.5 }, { L"ellipe(x)", -0.5 }, { L"ellipf(x, 0.5)", 0.7 }, { L"ellipe(x, 0.5)", 0.7 },
		{ L"ellipf(2x, 0.5)", 0.3 },
	};
	for (auto& c : cases) {
		FplParseOptions options;
		auto r = FplParse(c.text, options);
		CHECK(!r.error);
		if (r.error)
			continue;
		auto program = FplCompile(r.ast, r.root, none);
		double values[FplFirstParam] = {};
		values[FplSlotX] = c.x;
		const double value = program.Eval(values);
		CHECK(std::isfinite(value));
		CHECK_NEAR_TOL(Tree(r.ast, r.root, c.x), value, 1e-9 * (1 + std::abs(value)));
		FplError error;
		int d = FplDerive(r.ast, r.root, L"x", &error);
		CHECK(d >= 0);
		if (d < 0)
			continue;
		auto derivative = FplCompile(r.ast, d, none);
		const double h = 1e-5;
		values[FplSlotX] = c.x + h;
		const double up = program.Eval(values);
		values[FplSlotX] = c.x - h;
		const double down = program.Eval(values);
		values[FplSlotX] = c.x;
		CHECK_NEAR_TOL(derivative.Eval(values), (up - down) / (2 * h), 2e-5 * (1 + std::abs(value)));
		auto again = FplParse(FplToString(r.ast, r.root), options);
		CHECK(!again.error);
		if (!again.error)
			CHECK_NEAR_TOL(FplCompile(again.ast, again.root, none).Eval(values), value, 1e-9 * (1 + std::abs(value)));
	}
	CHECK(FplBuild(L"y = d/dx besselj(x, 2)", none).error.code == FplErrorCode::NotDifferentiable);
	CHECK(FplBuild(L"y = d/dx ellipf(1, x)", none).error.code == FplErrorCode::NotDifferentiable);
	CHECK(!FplBuild(L"y = d/dx besselj(a, x)", { L"a" }).error);
	CHECK_NEAR_TOL(at(L"d/dx d/dx airyai(x)", 2), 2 * at(L"airyai(x)", 2), 1e-9);	// y'' = x y, by the derivatives
	// the names give way
	CHECK(!FplBuild(L"y = beta x", { L"beta" }).error);
	CHECK(FplBuild(L"y = beta", none).error.code == FplErrorCode::UnknownName);
	auto plot = MakePlot({ L"zeta = 2", L"y = zeta x" });
	CHECK(plot.graphs[0].definition);
	CHECK(!plot.graphs[1].expr.error);
	// it draws
	FplViewport v = View();
	plot = MakePlot({ L"y = besselj(0, x)", L"y = airyai(x)", L"y = zeta(x)", L"y = lambertw(x)", L"y = ellipk(x)" });
	plot.Sample(v);
	for (auto& g : plot.graphs)
		CHECK(!g.geometry.lines.empty());
}

TEST(AnimationSpeed) {
	// across the range in its seconds, whatever the range
	FplParam p;
	p.min = 0;
	p.max = 10;
	p.value = 0;
	CHECK_NEAR(p.sweepSeconds, 4.0);
	CHECK(FplSweep(p, 1));
	CHECK_NEAR(p.value, 2.5);
	p.value = 0;
	p.sweepSeconds = 20;
	CHECK(FplSweep(p, 1));
	CHECK_NEAR(p.value, 0.5);
	p.value = 0;
	p.sweepSeconds = 0.5;
	CHECK(FplSweep(p, 0.1));
	CHECK_NEAR(p.value, 2.0);
	// turning back at the ends, and carrying on the other way
	p.sweepSeconds = 4;
	p.value = 9.9;
	p.direction = 1;
	CHECK(FplSweep(p, 1));
	CHECK_NEAR(p.value, 10.0);
	CHECK_INT(p.direction, -1);
	CHECK(FplSweep(p, 1));
	CHECK_NEAR(p.value, 7.5);
	p.value = 0.1;
	CHECK(FplSweep(p, 1));
	CHECK_NEAR(p.value, 0.0);
	CHECK_INT(p.direction, 1);
	// a whole sweep takes its time, by small steps
	p.sweepSeconds = 2;
	p.value = 0;
	p.direction = 1;
	int steps = 0;
	while (p.direction == 1 && steps < 1000) {
		FplSweep(p, 0.016);
		steps++;
	}
	CHECK_NEAR_TOL(steps * 0.016, 2.0, 0.02);
	// nothing to do
	p.sweepSeconds = 0;
	CHECK(!FplSweep(p, 1));
	p.sweepSeconds = 4;
	p.max = p.min;
	CHECK(!FplSweep(p, 1));
	// kept in the saved state; a text from before has the first one
	FplSavedState a, b;
	FplParam q;
	q.name = L"q";
	q.sweepSeconds = 7.5;
	a.params.push_back(q);
	CHECK(FplLoadState(FplSaveState(a), b));
	CHECK_INT(b.params.size(), 1);
	if (!b.params.empty())
		CHECK_NEAR(b.params[0].sweepSeconds, 7.5);
	FplSavedState old;
	CHECK(FplLoadState(L"FunctionPlot 1\nparam\ta\t1\t0\t2\t0\t0\n", old));
	CHECK_INT(old.params.size(), 1);
	if (!old.params.empty())
		CHECK_NEAR(old.params[0].sweepSeconds, 4.0);
	FplSavedState zero;
	CHECK(FplLoadState(L"FunctionPlot 1\nparam\ta\t1\t0\t2\t0\t0\t-3\n", zero));
	if (!zero.params.empty())
		CHECK_NEAR(zero.params[0].sweepSeconds, 4.0);
}

TEST(MathDisplay) {
	// a measure that needs no fonts: half an em a character
	FplMathMeasure measure = [](const std::wstring& text, double size, bool) { return text.size() * size * 0.5; };
	auto layout = [&](const wchar_t* text, FplParseOptions options = {}) {
		FplMathLayout out;
		CHECK(FplFitMath(text, options, { 20 }, 0, measure, out));
		return out;
	};
	auto said = [&](const wchar_t* text) { return FplDescribeMath(layout(text)); };
	const std::wstring minus(1, wchar_t(0x2212)), dot(1, wchar_t(0x00B7)), prime(1, wchar_t(0x2032)), theta(1, wchar_t(0x03B8));
	const std::wstring le(1, wchar_t(0x2264));

	// what's typed comes out, in order, with a stroke for what's drawn
	CHECK_STR(said(L"y = 2x"), L"y=2x");
	CHECK_STR(said(L"y = 2*x"), L"y=2" + dot + L"x");
	CHECK_STR(said(L"x - 1"), L"x" + minus + L"1");
	CHECK_STR(said(L"-x"), minus + L"x");
	CHECK_STR(said(L"sqrt(x^2 + y^2)"), L"|x2+y2");
	CHECK_STR(said(L"\x221Ax"), L"|x");
	CHECK_STR(said(L"root(3, x)"), L"|x3");
	CHECK_STR(said(L"|x - 1|"), L"|x" + minus + L"1|");
	CHECK_STR(said(L"abs(x)"), L"|x|");
	CHECK_STR(said(L"floor(x)"), L"|x|");
	CHECK_STR(said(L"exp(x)"), L"ex");
	CHECK_STR(said(L"e^x"), L"ex");
	CHECK_STR(said(L"pi x"), L"\x03C0x");
	CHECK_STR(said(L"x!"), L"x!");
	CHECK_STR(said(L"x^2 <= 1"), L"x2" + le + L"1");
	CHECK_STR(said(L"0.50 x"), L"0.50x");		// numbers as typed
	CHECK_STR(said(L"y1 + y"), L"y1+y");

	// parentheses as typed, except where the layout shows the grouping itself
	CHECK_STR(said(L"(x + 1)(x - 1)"), L"|x+1||x" + minus + L"1|");
	CHECK_STR(said(L"(x + 1)^2"), L"|x+1|2");
	CHECK_STR(said(L"(x + 1)/(x - 1)"), L"x+1x" + minus + L"1|");
	CHECK_STR(said(L"2^(x + 1)"), L"2x+1");
	CHECK_STR(said(L"sqrt((x))"), L"|x");
	CHECK_STR(said(L"f(x) = (x)"), L"f|x|=|x|");		// f is nothing known here
	CHECK_STR(said(L"[x]"), L"|x|");

	// functions: bare with a plain argument, else in parentheses
	CHECK_STR(said(L"sin x"), L"sinx");
	CHECK_STR(said(L"sin(x)"), L"sinx");
	CHECK_STR(said(L"sin 2x"), L"sin2x");
	CHECK_STR(said(L"sin(x + 1)"), L"sin|x+1|");
	CHECK_STR(said(L"gamma(x)"), L"gamma|x|");
	CHECK_STR(said(L"atan2(y, x)"), L"atan2|y,x|");
	CHECK_STR(said(L"sin^2 x"), L"sin2x");
	CHECK_STR(said(L"sin^-1 x"), L"sin" + minus + L"1x");
	CHECK_STR(said(L"asin(x)"), L"sin" + minus + L"1x");
	CHECK_STR(said(L"log2(x)"), L"log2x");
	CHECK_STR(said(L"log(x, 3)"), L"log3x");
	CHECK_STR(said(L"sin'(x)"), L"sin" + prime + L"x");
	CHECK_STR(said(L"sin''(2x)"), L"sin" + prime + prime + L"2x");

	// as typed, not as it means: no derivative taken, nothing expanded, degrees left alone
	CHECK_STR(said(L"d/dx x^3"), L"ddx|x3");
	CHECK_STR(said(L"clamp(x, 0, 1)"), L"clamp|x,0,1|");
	CHECK_STR(said(L"frac(x)"), L"frac|x|");
	FplParseOptions degrees;
	degrees.degrees = true;
	CHECK_STR(FplDescribeMath(layout(L"sin x", degrees)), L"sinx");
	// with a plot's names: its functions and substitutions stay as written, a parameter is a name
	auto plot = MakePlot({ L"f(x) = x^2", L"a = x - 1", L"y = f(2x) + a + f'(x) + k" }, { Param(L"k", 2) });
	CHECK_STR(FplDescribeMath(layout(L"y = f(2x) + a + f'(x) + k", plot.DisplayOptions())),
		L"y=f|2x|+a+f" + prime + L"|x|+k");
	CHECK_STR(FplDescribeMath(layout(L"f(x) = x^2", plot.DisplayOptions())), L"f|x|=x2");
	// q as what it reads as: theta in a polar line
	CHECK_STR(said(L"r = 2 + cos 3q"), L"r=2+cos3" + theta);
	// names nothing knows don't stop it; an ODE, with its conditions; systems; dy/dx
	CHECK_STR(said(L"y = a b"), L"y=ab");
	CHECK_STR(said(L"y' = -x/y, y(0) = 3"), L"y" + prime + L"=" + minus + L"xy|,y|0|=3");
	CHECK_STR(said(L"y'' = -y, y(0) = 1, y'(0) = 0"), L"y" + prime + prime + L"=" + minus + L"y,y|0|=1,y" + prime + L"|0|=0");
	CHECK_STR(said(L"x' = y, y' = -x, (1, 2)"), L"x" + prime + L"=y,y" + prime + L"=" + minus + L"x,|1,2|");
	CHECK_STR(said(L"dy/dx = x"), L"dydx|=x");
	CHECK_STR(said(L"(cos t, sin t)"), L"|cost,sint|");
	CHECK_STR(said(L"L = [1, 2, 3]"), L"L=|1,2,3|");
	CHECK_STR(said(L"y1 ~ m x1 + b"), L"y1\x223C" L"mx1+b");
	CHECK_STR(said(L"mean(L)"), L"mean|L|");
	// a chain, a condition, cases
	CHECK_STR(said(L"x^2 < y < 4"), L"x2<y<4");
	CHECK_STR(said(L"x^2 {0 < x < 2}"), L"x2|0<x<2|");
	CHECK_STR(said(L"{x < 0: -x, x^2}"), L"|" + minus + L"xx<0x2");
	// sums, products and integrals, with their limits
	CHECK_STR(said(L"sum(x^k/k!, k, 0, n)"), L"\x2211k=0nxkk!|");
	CHECK_STR(said(L"prod(k, k, 1, 3)"), L"\x220Fk=13k");
	CHECK_STR(said(L"integral(x^2, 0, 1)"), L"\x222B" L"10x2dx");
	CHECK_STR(said(L"integral(t, t, 0, x)"), L"\x222Bx0tdt");

	// it doesn't read: no layout
	FplMathLayout none;
	for (const wchar_t* text : { L"x +", L"(x", L"", L"a: 0..5", L"sin", L"1 2" })
		CHECK(!FplFitMath(text, {}, { 20 }, 0, measure, none));

	// the shapes. A power: smaller, and above where its base's text sits
	auto power = layout(L"x^2");
	CHECK_INT(power.items.size(), 2);
	CHECK(power.items[1].size < power.items[0].size);
	CHECK(power.items[1].y < power.items[0].y);
	CHECK(power.items[1].x >= 10);		// past the x
	CHECK(power.width > 10);
	auto subscript = layout(L"y1");
	CHECK(subscript.items[1].y > 0);
	// a fraction: over and under its bar, centered, around the axis
	auto fraction = layout(L"1/abcd");
	CHECK_INT(fraction.items.size(), 3);
	auto& top = fraction.items[0];
	auto& bottom = fraction.items[1];
	auto& bar = fraction.items[2];
	CHECK(bar.isLine);
	CHECK(top.y < bar.points[0].y);
	CHECK(bottom.y > bar.points[0].y);
	CHECK(bar.points[0].y < 0);
	CHECK(top.x > bottom.x);
	CHECK_NEAR_TOL(top.x + top.text.size() * top.size * 0.25, bottom.x + bottom.text.size() * bottom.size * 0.25, 1e-9);
	CHECK(fraction.ascent > 20 * 0.72);		// taller than a line
	CHECK(fraction.descent > 20 * 0.22);
	// a radical: over all of what's in it, however tall, and wider
	auto radical = layout(L"sqrt(1/x)");
	auto plainRoot = layout(L"sqrt(x)");
	CHECK(radical.ascent > plainRoot.ascent);
	const FplMathItem* sign = nullptr;
	for (auto& item : radical.items)
		if (item.isLine && item.points.size() == 5)
			sign = &item;
	CHECK(sign != nullptr);
	if (sign) {
		for (auto& item : radical.items)
			if (!item.isLine) {
				CHECK(item.x > sign->points[3].x);
				CHECK(item.y - 0.72 * item.size > sign->points[3].y);
			}
		CHECK_NEAR(sign->points.back().x + 0.04 * 20, radical.width);
	}
	// delimiters grow with what's in them
	auto tall = layout(L"(1/x)");
	auto low = layout(L"(x)");
	auto extent = [](const FplMathLayout& l) {
		double lo = 1e9, hi = -1e9;
		for (auto& item : l.items)
			if (item.isLine && item.points.size() > 2)
				for (auto& p : item.points)
					lo = std::min(lo, p.y), hi = std::max(hi, p.y);
		return hi - lo;
	};
	CHECK(extent(tall) > extent(low) * 1.3);
	// everything is inside its box, for every kind
	for (const wchar_t* text : { L"sqrt(x^2 + y^2)", L"(x + 1)/(x - 1)", L"sum(x^k/k!, k, 0, n)", L"integral(sin x, 0, 1)", L"{x < 0: -x, x^2}",
		L"root(3, x + 1)", L"|x/2|", L"floor(x)/ceil(x)", L"e^(x^2)", L"y' = -x/y, y(0) = 3" }) {
		auto l = layout(text);
		CHECK(l.width > 0);
		bool inside = true;
		for (auto& item : l.items) {
			if (item.isLine) {
				for (auto& p : item.points)
					inside = inside && p.x + item.x >= -1e-9 && p.x + item.x <= l.width + 1e-9 && p.y + item.y >= -l.ascent - 1e-9 && p.y + item.y <= l.descent + 1e-9;
			}
			else {
				const double w = item.text.size() * item.size * 0.5;
				inside = inside && item.x >= -1e-9 && item.x + w <= l.width + 1e-9;
				inside = inside && item.y - 0.72 * item.size >= -l.ascent - 1e-9 && item.y + 0.22 * item.size <= l.descent + 1e-9;
			}
		}
		CHECK(inside);
	}
	// nothing runs over what's before it in a row: each item starts past where the last text in a plain row ends
	auto row = layout(L"x + y + z");
	for (size_t i = 1; i < row.items.size(); i++)
		CHECK(row.items[i].x >= row.items[i - 1].x + row.items[i - 1].text.size() * row.items[i - 1].size * 0.5 - 1e-9);

	// the size that fits, or none
	FplMathLayout fit;
	CHECK(FplFitMath(L"x + y + z + w", {}, { 20, 10 }, 200, measure, fit));
	CHECK(fit.width <= 200);
	CHECK_NEAR(fit.items[0].size, 20.0);
	CHECK(FplFitMath(L"x + y + z + w", {}, { 20, 10 }, 60, measure, fit));
	CHECK_NEAR(fit.items[0].size, 10.0);
	CHECK(!FplFitMath(L"x + y + z + w", {}, { 20, 10 }, 5, measure, fit));

	// a display parse changes nothing for one that's evaluated
	std::vector<std::wstring> names;
	auto e = FplBuild(L"y = sin 2x", names);
	double slots[FplFirstParam] = {};
	slots[FplSlotX] = 0.5;
	CHECK_NEAR(e.f.Eval(slots), std::sin(1.0));

	// the SVG's legend: as text and strokes with a measure, as typed without one
	FplViewport v = View(-10, 10, -8, 8, 1000, 800);
	auto drawn = MakePlot({ L"y = sqrt(x^2 + 1)/2", L"x +" });
	drawn.Sample(v);
	FplSvgStyle style;
	style.graphColors = { 0xFF0000, 0x00FF00 };
	std::string svg = FplWriteSvg(drawn, v, style);
	CHECK(svg.find(">y = sqrt(x^2 + 1)/2<") != std::string::npos);
	style.mathMeasure = measure;
	svg = FplWriteSvg(drawn, v, style);
	CHECK(svg.find(">y = sqrt(x^2 + 1)/2<") == std::string::npos);
	CHECK(svg.find("font-style=\"italic\">x<") != std::string::npos);
	CHECK(svg.find("font-family=\"Cambria Math, Cambria, serif\">=<") != std::string::npos);
	CHECK(svg.find(">x +<") != std::string::npos);		// one with an error stays as typed
}

int wmain() {
	setvbuf(stdout, nullptr, _IONBF, 0);
	Run_Numbers();
	Run_Precedence();
	Run_ImplicitProducts();
	Run_Names();
	Run_Absolute();
	Run_Postfix();
	Run_Piecewise();
	Run_Errors();
	Run_Classify();
	Run_Functions();
	Run_Powers();
	Run_Degrees();
	Run_CompiledMatchesTree();
	Run_Compiling();
	Run_Derivatives();
	Run_Simplify();
	Run_Printing();
	Run_Positions();
	Run_Speed();
	Run_Viewport();
	Run_Ticks();
	Run_Clipping();
	Run_SampleFunctions();
	Run_SampleCurves();
	Run_SampleImplicit();
	Run_SampleRegions();
	Run_SampleOthers();
	Run_Analysis();
	Run_SampleSpeed();
	Run_GraphDefinitions();
	Run_GraphSampling();
	Run_GraphFeatures();
	Run_GraphTracing();
	Run_Coordinates();
	Run_UserFunctionDefinitions();
	Run_UserFunctionCalls();
	Run_DerivativeSyntax();
	Run_GraphFunctions();
	Run_Surfaces();
	Run_VectorFields();
	Run_Svg();
	Run_Kinds3D();
	Run_BoxAndCamera();
	Run_Meshes3D();
	Run_Graphs3D();
	Run_ImplicitSurfaces();
	Run_TraceSurfaces();
	Run_Trace3D();
	Run_KeyboardTrace();
	Run_DraggablePoints();
	Run_Integrals();
	Run_Lists();
	Run_Regression();
	Run_SavedState();
	Run_ZoomAndFit();
	Run_Labels();
	Run_History();
	Run_StylesOut();
	Run_Worker();
	Run_LogAxes();
	Run_Odes();
	Run_CurveIntersections();
	Run_SurfaceLines();
	Run_Shaders();
	Run_LogAxesKinds();
	Run_BetweenCurves();
	Run_ColorScale();
	Run_HigherOdes();
	Run_Singularities();
	Run_OdesOfAnyOrder();
	Run_SystemsWithTime();
	Run_Systems3D();
	Run_StiffOdes();
	Run_Odes3DAndPicking();
	Run_PrimesOnBuiltIns();
	Run_Fills3D();
	Run_Dragging3D();
	Run_Substitutions();
	Run_PolarGrid();
	Run_Fields3D();
	Run_HiddenPoints3D();
	Run_QIsTheta();
	Run_Transitions();
	Run_StretchingDrawings();
	Run_MoreFunctions();
	Run_SumsAndProducts();
	Run_AutoQ();
	Run_TicksInState();
	Run_HeatmapsInSvg();
	Run_ReadmeMatchesParser();
	Run_SlowCurves();
	Run_SpecialFunctions();
	Run_AnimationSpeed();
	Run_MathDisplay();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
