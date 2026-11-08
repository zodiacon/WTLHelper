// Unit tests for the function plot's model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number of failed
// checks. Covered: the expression engine (lexing, implicit products, name splitting, parsing, errors and their
// positions, what an expression draws, constant folding, compiling and the stack machine against the tree, the math
// functions, degrees, derivatives against differences, simplifying and printing), and the 2D model (the view, ticks
// and labels, clipping, sampling functions, curves, implicit curves and regions, roots, extrema and intersections), and the
// control's graphs (definitions, sampling, features, tracing, coordinates).

#include "FunctionPlotExpr.h"
#include "FunctionPlotGraphs.h"
#include "FunctionPlotModel.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
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
	double s[8];
	Slots(s, x, y);
	return e.f.Eval(s);
}

static double G(std::wstring_view text, double x = 0) {
	auto e = FplBuild(text, g_Params);
	if (e.error || e.g.Empty())
		return Failed;
	double s[8];
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
	double s[8];
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
	CHECK_INT(ParseError(L"0 < y < 1").code, FplErrorCode::TooManyRelations);
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
	CHECK_INT(BuildError(L"(1, 2, 3)").code, FplErrorCode::BadPoint);
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
			double s[8];
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
	double s[8] = { 1 };
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
	double s[8];
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
		double s[8];
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
	double sl[8];
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
	auto plot = MakePlot({ L"y = x^2 - 1", L"y = x", L"y = 2 +", L"(cos t, sin t)" });
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
	// only functions have them
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

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
