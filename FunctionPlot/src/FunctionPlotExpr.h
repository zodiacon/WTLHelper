#pragma once

// The function plot's expression engine, window-free: parsing math as people type it ("2x sin x", "y = x^2 {0<x<2}",
// "(cos t, sin t)", "r = 1 + cos 3theta", "x^2 + y^2 = 4") into a tree, telling what kind of plot it is, differentiating
// it, and compiling it to a small stack machine that the sampler runs millions of times.
//
// Text positions are UTF-16 offsets into the source, so an app can underline an error.

#include <cstdint>
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
};

enum class FplRel : uint8_t { Eq, Lt, Le, Gt, Ge };

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

// The standard variables. Any other name is a parameter.
inline constexpr wchar_t FplTheta[] = L"\x03B8";
bool FplIsVariable(std::wstring_view name);

// ---- Parsing ---------------------------------------------------------------------------------------------------------

struct FplParseOptions {
	const std::vector<std::wstring>* parameters = nullptr;	// the names an app has defined
	bool degrees = false;	// trigonometric functions in degrees
};

struct FplParseResult {
	FplAst ast;
	int root = -1;
	FplError error;
};

// The grammar, loosely: an equation or inequality (one relation at most) of sums of products, where a product can be
// implicit ("2x", "x(x+1)", "2 sin x"). '^' binds tighter than unary minus and is right associative; a function's
// argument can go without parentheses, taking the implicit product after it up to an operator or the next function
// ("sin 2x cos x" is sin(2x) cos(x)); "sin^2 x" is (sin x)^2. A name that isn't known is split into known ones
// ("xy", "pix", "sinx"). Also: |x|, x!, {x < 0: -x, x}, {0 < x < 1} (1 inside, nothing outside), (a, b), "**" for
// '^', "<=" ">=", and the characters people paste: times and dots (U+00D7 U+00B7 U+22C5), U+00F7, the minus U+2212,
// U+2264 U+2265, the square root sign U+221A (of the factor after it), superscript 2 and 3 (U+00B2 U+00B3), and the
// Greek letters (pi, tau and theta mean what their names do). An unknown name alone before '=' is a definition
// ("a = 2").
FplParseResult FplParse(std::wstring_view text, const FplParseOptions& options = {});

// ---- What a parsed expression draws ----------------------------------------------------------------------------------

enum class FplKind : uint8_t {
	Function,		// y = f(x); expr[0] is f
	FunctionOfY,	// x = f(y); expr[0] is f
	Polar,			// r = f(theta); expr[0] is f
	Parametric,		// (x(t), y(t)); expr[0], expr[1]
	Implicit,		// F(x, y) rel 0; expr[0] is F (theta and r replaced by atan2(y, x) and hypot(x, y))
	Point,			// (a, b) with no variables; expr[0], expr[1]
	Definition,		// name = value with no variables; expr[0] is the value
};

struct FplPlotDef {
	FplKind kind = FplKind::Function;
	FplRel rel = FplRel::Eq;	// for Function, FunctionOfY, Polar and Implicit: y rel f(x), r rel f(theta), F rel 0
	int expr[2] = { -1, -1 };
	std::wstring name;			// Definition's
	FplError error;
};

// Looks at the whole expression, turning it around where needed ("x^2 > y" is y < x^2). Adds nodes to the tree.
FplPlotDef FplClassify(FplAst& ast, int root);

// Bit per standard variable, for FplVariablesOf.
enum : unsigned { FplVarX = 1, FplVarY = 2, FplVarT = 4, FplVarTheta = 8, FplVarR = 16 };
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
enum : int { FplSlotX = 0, FplSlotY = 1, FplSlotT = 2, FplSlotTheta = 3, FplFirstParam = 4 };

enum class FplCode : uint8_t {
	Const, Load,
	Neg, Add, Sub, Mul, Div, Pow, Sq, PowInt,
	Call1, Call2, CallN,
	Lt, Le, Gt, Ge, Eq, And,
	JumpIfFalse, Jump,
};

struct FplInstr {
	FplCode code;
	int arg = 0;					// Load: slot; PowInt: exponent; CallN: count, Jump*: target
	double value = 0;				// Const
	double (*f1)(double) = nullptr;
	double (*f2)(double, double) = nullptr;
	double (*fn)(const double*, int) = nullptr;
};

class FplProgram {
public:
	double Eval(const double* slots) const;
	bool Empty() const { return m_Code.empty(); }
	bool UsesSlot(int slot) const { return slot < 64 && (m_Slots >> slot) & 1; }
	bool IsConstant() const { return m_Slots == 0; }
	size_t Size() const { return m_Code.size(); }

private:
	friend class FplCompiler;
	std::vector<FplInstr> m_Code;
	int m_MaxDepth = 0;
	uint64_t m_Slots = 0;
};

// Compiles one node (not a relation or a point: their sides). Parts without names are folded to constants; constant
// integer powers multiply.
FplProgram FplCompile(const FplAst& ast, int node, const std::vector<std::wstring>& parameters, FplError* error = nullptr);

// x^y as a grapher means it: a negative x to a rational power with an odd denominator is real ((-8)^(1/3) = -2).
double FplPow(double x, double y);

// ---- All of it -------------------------------------------------------------------------------------------------------

struct FplExpression {
	FplKind kind = FplKind::Function;
	FplRel rel = FplRel::Eq;
	FplProgram f, g;			// g only for Parametric and Point
	std::wstring name;			// Definition's
	FplError error;
};

// Parses, classifies and compiles text; the slots are as for FplCompile with the same parameters.
FplExpression FplBuild(std::wstring_view text, const std::vector<std::wstring>& parameters, bool degrees = false);
