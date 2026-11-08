// Unit tests for the function plot's model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number of failed
// checks. Covered: the expression engine (lexing, implicit products, name splitting, parsing, errors and their
// positions, what an expression draws, constant folding, compiling and the stack machine against the tree, the math
// functions, degrees, derivatives against differences, simplifying and printing).

#include "FunctionPlotExpr.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
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

int wmain() {
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

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
