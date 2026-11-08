#pragma once

// The function plot's expression engine, window-free: parsing math as people type it ("2x sin x", "y = x^2 {0<x<2}",
// "(cos t, sin t)", "r = 1 + cos 3theta", "x^2 + y^2 = 4") into a tree, telling what kind of plot it is, differentiating
// it, and compiling it to a small stack machine that the sampler runs millions of times.
//
// Text positions are UTF-16 offsets into the source, so an app can underline an error.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ---- Errors ----------------------------------------------------------------------------------------------------------

enum class FplErrorCode : uint8_t {
	None,
	Empty,				// nothing typed
	UnexpectedChar,		// a character no token starts with
	BadNumber,			// "1.2.3"
	UnexpectedToken,	// a token where it can't be ("2 * * 3", "3 4")
	UnexpectedEnd,		// the text stops in the middle ("2 +")
	MissingClose,		// a '(' '[' '{' or '|' left open; name is the closing character
	UnknownName,		// name is the unknown name (an app may offer to add it as a parameter)
	ArgCount,			// a function called with the wrong number of arguments; name is the function
	MissingArgument,	// "sin" with nothing after it
	TooManyRelations,	// "y = x = 2" or "0 < y < 1" at the top
	MisplacedRelation,	// a comparison outside { } that isn't the whole equation ("2 + (x < 3)")
	MisplacedPoint,		// a point used as a number ("2 * (1, 2)")
	MisplacedCondition,	// a value with no condition before the last item of { }
	VariableNotAllowed,	// a variable the plot's kind doesn't have ("y = t"); name is the variable
	NotAPlot,			// an expression that isn't something to draw ("sin theta" alone, "x + y")
	BadPoint,			// a point that isn't (x, y)
	NotDifferentiable,	// a relation or a point where a derivative was asked for
	Redefined,			// a name defined twice, or a definition of a parameter the app has; name is the name
	ListNotAllowed,		// a list ([1, 2, 3]) anywhere but as a definition's value, or a list used where one number is
	NotAList,			// mean(3): name is the function
	BadInitial,			// a second-order ODE's conditions aren't y(a) = b, y'(a) = c at one a
	Internal,
};

struct FplError {
	FplErrorCode code = FplErrorCode::None;
	int pos = 0;			// the part of the text it's about
	int len = 0;
	std::wstring name;		// what the message names (a name, a function, a variable), if anything

	explicit operator bool() const { return code != FplErrorCode::None; }
	std::wstring Message() const;
};

// ---- The tree --------------------------------------------------------------------------------------------------------

enum class FplOp : uint8_t {
	Num,		// value; name is a constant's ("pi") for printing, else empty
	Var,		// name: a variable (x y t theta r) or a parameter
	Neg,		// -a
	Add, Sub, Mul, Div, Pow,
	Fact,		// a!
	Call,		// func(kids...)
	Cmp,		// a rel b (inside { } and as the whole equation)
	And,		// a chained comparison: 0 < x < 1 is And(0 < x, x < 1), the middle shared
	Piecewise,	// {cond: value, cond: value, default}: kids are cond, value pairs, then the default if hasDefault
	Tuple,		// (a, b): a point or a parametric curve, only as the whole expression
	Integral,	// integral(body, var, lo, hi): name is the variable, bound in the body; kids body, lo, hi
	List,		// [a, b, c]: only as a definition's value
};

enum class FplRel : uint8_t { Eq, Lt, Le, Gt, Ge, Fit };	// Fit: "~", a regression

enum class FplFunc : uint8_t {
	Sin, Cos, Tan, Sec, Csc, Cot,
	Asin, Acos, Atan, Atan2,
	Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,
	Exp, Ln, Log, Log2,
	Sqrt, Cbrt, Root,		// root(n, x)
	Abs, Sign, Floor, Ceil, Round,
	Min, Max, Mod, Hypot,
	Gamma, Digamma, Erf,
	Count
};

struct FplFuncInfo {
	const wchar_t* name;	// the canonical name, as printed
	int minArgs;
	int maxArgs;			// -1: any number
};
const FplFuncInfo& FplFuncInfoOf(FplFunc f);

struct FplNode {
	FplOp op = FplOp::Num;
	FplRel rel = FplRel::Eq;		// Cmp
	FplFunc func = FplFunc::Sin;	// Call
	bool hasDefault = false;		// Piecewise
	double value = 0;				// Num
	std::wstring name;				// Var; Num's constant name
	std::vector<int> kids;
	int pos = 0, len = 0;			// in the source; 0, 0 for nodes made later (a derivative's)
};

// Nodes in one vector, referred to by index. Nodes are never changed once made, so a subtree can be shared (a chained
// comparison's middle, a derivative reusing its function's parts).
struct FplAst {
	std::vector<FplNode> nodes;

	const FplNode& operator[](int i) const { return nodes[i]; }
	int Add(FplNode n);
	int Num(double v, std::wstring_view name = {});
	int Var(std::wstring_view name);
	int Unary(FplOp op, int a);
	int Binary(FplOp op, int a, int b);
	int Call(FplFunc f, std::vector<int> args);
	int Cmp(FplRel rel, int a, int b);
};

// The standard variables (x y z t theta r u v). Any other name is a parameter. u and v are a parametric surface's.
inline constexpr wchar_t FplTheta[] = L"\x03B8";
bool FplIsVariable(std::wstring_view name);

// ---- Parsing ---------------------------------------------------------------------------------------------------------

// A function the user defined ("f(x) = x^2", "g(a, b) = a b"): its parameters and its body, in a tree of its own. A
// call is inlined: the body copied with the arguments for the parameters ("f'(x)": its derivative first).
struct FplUserFunction {
	std::wstring name;
	std::vector<std::wstring> params;
	FplAst ast;
	int body = -1;
};

struct FplParseOptions {
	const std::vector<std::wstring>* parameters = nullptr;	// the names an app has defined
	bool degrees = false;	// trigonometric functions in degrees
	const std::vector<FplUserFunction>* functions = nullptr;	// the user's functions, known by name
	// the names (among parameters) that are lists: mean(L), median, stdev, sum, count, min and max of one are the
	// parameter named "mean(L)" and so on (FplAggregateName), which the app works out
	const std::vector<std::wstring>* lists = nullptr;
};

// The list aggregates, and the parameter name one of a list is.
inline constexpr const wchar_t* FplAggregates[] = { L"mean", L"median", L"stdev", L"sum", L"count", L"min", L"max" };
std::wstring FplAggregateName(std::wstring_view aggregate, std::wstring_view list);

struct FplParseResult {
	FplAst ast;
	int root = -1;
	FplError error;
};

// The grammar, loosely: an equation or inequality (one relation at most) of sums of products, where a product can be
// implicit ("2x", "x(x+1)", "2 sin x"). '^' binds tighter than unary minus and is right associative; a function's
// argument can go without parentheses, taking the implicit product after it up to an operator or the next function
// ("sin 2x cos x" is sin(2x) cos(x)); "sin^2 x" is (sin x)^2. A name that isn't known is split into known ones
// ("xy", "pix", "sinx"). integral(f, lo, hi) integrates over x, integral(f, t, lo, hi) over a variable named (U+222B
// too); the variable is bound inside. The user's functions are called as the built-in ones are, f'(x) and f''(x) their
// derivatives; "d/dx" (d/dy, d/dt, d/dtheta) is the derivative of what follows, as a function's argument. Also: |x|, x!, {x < 0: -x, x}, {0 < x < 1} (1 inside, nothing outside), (a, b), "**" for
// '^', "<=" ">=", and the characters people paste: times and dots (U+00D7 U+00B7 U+22C5), U+00F7, the minus U+2212,
// U+2264 U+2265, the square root sign U+221A (of the factor after it), superscript 2 and 3 (U+00B2 U+00B3), and the
// Greek letters (pi, tau and theta mean what their names do). An unknown name alone before '=' is a definition
// ("a = 2").
FplParseResult FplParse(std::wstring_view text, const FplParseOptions& options = {});

// "f(x) = x^2": a function's definition, if the text is one (a name nothing knows, its parameters, '='); the body is
// parsed with the parameters known. error: the body's (positions in the whole text), or Redefined for a parameter
// named twice.
struct FplFunctionDefinition {
	bool isDefinition = false;
	FplUserFunction function;
	FplError error;
};
FplFunctionDefinition FplParseFunction(std::wstring_view text, const FplParseOptions& options = {});

// ---- What a parsed expression draws ----------------------------------------------------------------------------------

enum class FplKind : uint8_t {
	Function,		// y = f(x); expr[0] is f
	FunctionOfY,	// x = f(y); expr[0] is f
	Polar,			// r = f(theta); expr[0] is f
	Parametric,		// (x(t), y(t)); expr[0], expr[1]
	Implicit,		// F(x, y) rel 0; expr[0] is F (theta and r replaced by atan2(y, x) and hypot(x, y))
	Point,			// (a, b) with no variables; expr[0], expr[1]
	Definition,		// name = value with no variables; expr[0] is the value
	Surface,		// z = F(x, y); expr[0] is F
	VectorField,	// (P(x, y), Q(x, y)); expr[0], expr[1]
	SpaceCurve,		// (x(t), y(t), z(t)); expr[0..2] (3D)
	ParametricSurface,	// (x(u, v), y(u, v), z(u, v)); expr[0..2] (3D)
	Point3D,		// (a, b, c) with no variables; expr[0..2] (3D)
	ImplicitSurface,	// F(x, y, z) = 0, an equation with z that isn't z = F(x, y); expr[0] is F (3D)
	Integral,		// integral(f, var, lo, hi) alone, with no free variables: its area; expr[0] f (in the variable,
					// the def's name), expr[1] lo, expr[2] hi
	Regression,		// lhs ~ rhs: the unknown names in it fitted over lists; expr[0] lhs - rhs, expr[1] lhs
	Between,		// f(x) < y < g(x), or f(y) < x < g(y) (name says which is in the middle): expr[0] lower, expr[1] upper;
					// rel Lt if both are strict, else Le
	SlopeField,		// y' = F(x, y) (FplBuild only, from FplSplitOde): its slopes, and with ", y(a) = b" (any number)
					// the solutions through (a, b); FplExpression's f is F, its starts a and b of each
	SecondOrder,	// y'' = F(x, y, y') with "y(a) = b, y'(a) = c" pairs: the solutions; f is F (y' in the t slot),
					// its starts a, b and c of each
	OdeSystem,		// x' = P(x, y), y' = Q(x, y) with "(a, b)" points: the field's arrows and the trajectories through
					// them; f is P, g is Q, its starts a and b of each
};

struct FplPlotDef {
	FplKind kind = FplKind::Function;
	FplRel rel = FplRel::Eq;	// for Function, FunctionOfY, Polar and Implicit: y rel f(x), r rel f(theta), F rel 0
	int expr[3] = { -1, -1, -1 };
	std::wstring name;			// Definition's
	FplError error;
};

// Looks at the whole expression, turning it around where needed ("x^2 > y" is y < x^2). Adds nodes to the tree.
FplPlotDef FplClassify(FplAst& ast, int root);

// Bit per standard variable, for FplVariablesOf.
enum : unsigned {
	FplVarX = 1, FplVarY = 2, FplVarT = 4, FplVarTheta = 8, FplVarR = 16, FplVarZ = 32, FplVarU = 64, FplVarV = 128
};
unsigned FplVariablesOf(const FplAst& ast, int node);
bool FplHasNames(const FplAst& ast, int node);	// any Var at all (variables or parameters)

// ---- Calculus and printing -------------------------------------------------------------------------------------------

// d/d(var) of the node; other variables and parameters are constants (a partial derivative). The result is simplified.
// A point gives a point of derivatives. Returns -1 with the error set for a relation.
int FplDerive(FplAst& ast, int node, std::wstring_view var, FplError* error = nullptr);

// Folds constants and drops the trivial parts (x+0, 1*x, x^1, --x). Returns the node itself when nothing changed.
int FplSimplify(FplAst& ast, int node);

// Back to text the parser reads the same way, with explicit '*' and as few parentheses as that allows.
std::wstring FplToString(const FplAst& ast, int node);

// Evaluates the tree directly (slow; constant folding and tests). Names are looked up in names/values.
double FplEvalTree(const FplAst& ast, int node, const std::vector<std::wstring>& names, const double* values);

// ---- Compiled form ---------------------------------------------------------------------------------------------------

// The slots a program reads: the standard variables, then the app's parameters in the order given at compile time.
enum : int {
	FplSlotX = 0, FplSlotY = 1, FplSlotT = 2, FplSlotTheta = 3, FplSlotZ = 4, FplSlotU = 5, FplSlotV = 6,
	FplFirstParam = 7
};

enum class FplCode : uint8_t {
	Const, Load,
	Neg, Add, Sub, Mul, Div, Pow, Sq, PowInt,
	Call1, Call2, CallN,
	Lt, Le, Gt, Ge, Eq, And,
	JumpIfFalse, Jump,
	Integral,			// pops hi and lo: the integral of a sub-program (arg) over the slot in value
};

struct FplInstr {
	FplCode code;
	int arg = 0;					// Load: slot; PowInt: exponent; CallN: count, Jump*: target
	double value = 0;				// Const
	double (*f1)(double) = nullptr;
	double (*f2)(double, double) = nullptr;
	double (*fn)(const double*, int) = nullptr;
};

class FplProgram;
using FplProgramPtr = std::shared_ptr<const FplProgram>;

class FplProgram {
public:
	double Eval(const double* slots) const;
	bool Empty() const { return m_Code.empty(); }
	bool UsesSlot(int slot) const { return slot < 64 && (m_Slots >> slot) & 1; }
	bool Reads(int slot) const;		// any slot (UsesSlot only knows the first 64)
	bool IsConstant() const { return m_Slots == 0; }
	size_t Size() const { return m_Code.size(); }

private:
	friend class FplCompiler;
	std::vector<FplInstr> m_Code;
	std::vector<FplProgramPtr> m_Subs;	// integrals' bodies
	int m_MaxDepth = 0;
	int m_SlotCount = 0;				// one past the highest slot it reads (its bodies' too)
	uint64_t m_Slots = 0;
};

// Compiles one node (not a relation or a point: their sides). Parts without names are folded to constants; constant
// integer powers multiply.
FplProgram FplCompile(const FplAst& ast, int node, const std::vector<std::wstring>& parameters, FplError* error = nullptr);

// A standard variable's slot (x, y, t, theta, z, u, v), or -1.
int FplStandardSlot(std::wstring_view name);

// The integral of f over [a, b] by adaptive Simpson (an error of about 1e-10 of its size; NaN where f is undefined
// somewhere it looks).
double FplIntegrate(const std::function<double(double)>& f, double a, double b);

// x^y as a grapher means it: a negative x to a rational power with an odd denominator is real ((-8)^(1/3) = -2).
double FplPow(double x, double y);

// ---- All of it -------------------------------------------------------------------------------------------------------

struct FplExpression {
	FplKind kind = FplKind::Function;
	FplRel rel = FplRel::Eq;
	FplProgram f, g;			// g only for Parametric and Point (and the 3D kinds)
	FplProgram h;				// the 3D kinds' z
	FplProgram value;			// Integral's: its value
	FplProgram df;				// Function's df/dx, FunctionOfY's df/dy (empty if it can't be derived)
	std::vector<FplProgram> starts;	// an ODE's initial conditions: a and b of each (SecondOrder: a, b and c)
	// y = f(x), x = f(y): what's zero where it may be singular (a denominator, a logarithm's argument, tan's and the
	// like's); wrap 1: cos of it, 2: sin of it. FplSample tells what each zero is by the limits on its two sides.
	struct Singular {
		FplProgram program;
		uint8_t wrap = 0;
		bool guarded = false;	// inside { }: its zero may be in a branch not taken, so only running off counts
	};
	std::vector<Singular> singular;
	std::wstring name;			// Definition's
	FplError error;
};

// "y' = F" (or U+2032 for the prime, or "dy/dx = F"), with ", y(a) = b" after it (any number of them) for the
// solutions through (a, b); "y'' = F" (or U+2033), with ", y(a) = b, y'(a) = c" pairs; "x' = P, y' = Q" (or dx/dt, dy/dt),
// with ", (a, b)" points: the
// parts' places in the text. A comma inside ( ) [ ] { } is the expression's. errorAt: where a condition after the
// comma stopped making sense (npos when it's fine).
struct FplOdeStart {
	size_t a = 0, aLen = 0, b = 0, bLen = 0;	// y(a) = b, or the point (a, b)
	size_t c = 0, cLen = 0;						// y'(a) = c
};
enum class FplOdeOrder : uint8_t { First, Second, System };
struct FplOdeText {
	bool isOde = false;
	FplOdeOrder order = FplOdeOrder::First;
	size_t rhs = 0, rhsLen = 0;
	size_t rhs2 = 0, rhs2Len = 0;				// a system's y' = Q
	std::vector<FplOdeStart> starts;			// any number, comma-separated
	size_t errorAt = std::wstring_view::npos;
	bool missingClose = false;
	bool badInitial = false;					// a second-order condition without its other half
};
FplOdeText FplSplitOde(std::wstring_view text);

// Parses, classifies and compiles text; the slots are as for FplCompile with the same parameters. An ODE (FplSplitOde)
// is a SlopeField: F may use x, y and the parameters, a and b only the parameters.
FplExpression FplBuild(std::wstring_view text, const std::vector<std::wstring>& parameters, bool degrees = false,
	const std::vector<FplUserFunction>* functions = nullptr, const std::vector<std::wstring>* lists = nullptr);
