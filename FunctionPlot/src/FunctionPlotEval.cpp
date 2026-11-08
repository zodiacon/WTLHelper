// Function plot expressions: the math functions, evaluating a tree, compiling it, and the stack machine.

#include "FunctionPlotExpr.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

double Sec(double x) { return 1 / std::cos(x); }
double Csc(double x) { return 1 / std::sin(x); }
double Cot(double x) { return std::cos(x) / std::sin(x); }
double Sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : x; }
double Fact(double x) { return std::tgamma(x + 1); }

double Digamma(double x) {
	if (std::isnan(x) || (x <= 0 && std::floor(x) == x))
		return NaN;
	if (x < 0)	// reflection: digamma(1 - x) - digamma(x) = pi cot(pi x)
		return Digamma(1 - x) - std::numbers::pi / std::tan(std::numbers::pi * x);
	double r = 0;
	while (x < 10) {
		r -= 1 / x;
		x += 1;
	}
	double f = 1 / (x * x);
	return r + std::log(x) - 0.5 / x - f * (1.0 / 12 - f * (1.0 / 120 - f * (1.0 / 252 - f * (1.0 / 240 - f / 132))));
}

double Mod(double a, double b) {
	if (b == 0)
		return NaN;
	return a - b * std::floor(a / b);	// the sign of b, as graphers do
}

double Root(double n, double x) { return FplPow(x, 1 / n); }
double Atan2(double y, double x) { return std::atan2(y, x); }
double Hypot(double a, double b) { return std::hypot(a, b); }

double Min(const double* a, int n) {
	double m = a[0];
	for (int i = 1; i < n; i++) {
		if (std::isnan(a[i]))
			return a[i];
		m = a[i] < m ? a[i] : m;
	}
	return m;
}

double Max(const double* a, int n) {
	double m = a[0];
	for (int i = 1; i < n; i++) {
		if (std::isnan(a[i]))
			return a[i];
		m = a[i] > m ? a[i] : m;
	}
	return m;
}

double (*Func1(FplFunc f))(double) {
	switch (f) {
		case FplFunc::Sin: return [](double x) { return std::sin(x); };
		case FplFunc::Cos: return [](double x) { return std::cos(x); };
		case FplFunc::Tan: return [](double x) { return std::tan(x); };
		case FplFunc::Sec: return Sec;
		case FplFunc::Csc: return Csc;
		case FplFunc::Cot: return Cot;
		case FplFunc::Asin: return [](double x) { return std::asin(x); };
		case FplFunc::Acos: return [](double x) { return std::acos(x); };
		case FplFunc::Atan: return [](double x) { return std::atan(x); };
		case FplFunc::Sinh: return [](double x) { return std::sinh(x); };
		case FplFunc::Cosh: return [](double x) { return std::cosh(x); };
		case FplFunc::Tanh: return [](double x) { return std::tanh(x); };
		case FplFunc::Asinh: return [](double x) { return std::asinh(x); };
		case FplFunc::Acosh: return [](double x) { return std::acosh(x); };
		case FplFunc::Atanh: return [](double x) { return std::atanh(x); };
		case FplFunc::Exp: return [](double x) { return std::exp(x); };
		case FplFunc::Ln: return [](double x) { return std::log(x); };
		case FplFunc::Log: return [](double x) { return std::log10(x); };
		case FplFunc::Log2: return [](double x) { return std::log2(x); };
		case FplFunc::Sqrt: return [](double x) { return std::sqrt(x); };
		case FplFunc::Cbrt: return [](double x) { return std::cbrt(x); };
		case FplFunc::Abs: return [](double x) { return std::abs(x); };
		case FplFunc::Sign: return Sign;
		case FplFunc::Floor: return [](double x) { return std::floor(x); };
		case FplFunc::Ceil: return [](double x) { return std::ceil(x); };
		case FplFunc::Round: return [](double x) { return std::round(x); };
		case FplFunc::Gamma: return [](double x) { return std::tgamma(x); };
		case FplFunc::Digamma: return Digamma;
		case FplFunc::Erf: return [](double x) { return std::erf(x); };
		default: return nullptr;
	}
}

double (*Func2(FplFunc f))(double, double) {
	switch (f) {
		case FplFunc::Atan2: return Atan2;
		case FplFunc::Root: return Root;
		case FplFunc::Mod: return Mod;
		case FplFunc::Hypot: return Hypot;
		default: return nullptr;
	}
}

double (*FuncN(FplFunc f))(const double*, int) {
	switch (f) {
		case FplFunc::Min: return Min;
		case FplFunc::Max: return Max;
		default: return nullptr;
	}
}

bool Compare(FplRel rel, double a, double b) {
	switch (rel) {
		case FplRel::Lt: return a < b;
		case FplRel::Le: return a <= b;
		case FplRel::Gt: return a > b;
		case FplRel::Ge: return a >= b;
		default: return a == b;
	}
}

double PowInt(double x, int n) {
	unsigned e = unsigned(n < 0 ? -n : n);
	double r = 1, b = x;
	while (e) {
		if (e & 1)
			r *= b;
		b *= b;
		e >>= 1;
	}
	return n < 0 ? 1 / r : r;
}

int StandardSlot(std::wstring_view name) {
	if (name == L"x") return FplSlotX;
	if (name == L"y") return FplSlotY;
	if (name == L"t") return FplSlotT;
	if (name == FplTheta) return FplSlotTheta;
	return -1;
}

}	// namespace

double FplPow(double x, double y) {
	double r = std::pow(x, y);
	if (x < 0 && std::isnan(r) && std::isfinite(y)) {
		// A rational power with an odd denominator: (-8)^(1/3) = -2, (-8)^(2/3) = 4.
		for (int q = 3; q <= 99; q += 2) {
			double pq = y * q;
			double p = std::round(pq);
			if (std::abs(pq - p) < 1e-9 * std::max(1.0, std::abs(pq))) {
				double m = std::pow(-x, y);
				return std::fmod(p, 2) != 0 ? -m : m;
			}
		}
	}
	return r;
}

// ---- Evaluating the tree ---------------------------------------------------------------------------------------------

double FplEvalTree(const FplAst& ast, int node, const std::vector<std::wstring>& names, const double* values) {
	auto& n = ast[node];
	auto& k = n.kids;
	auto ev = [&](int i) { return FplEvalTree(ast, k[i], names, values); };
	switch (n.op) {
		case FplOp::Num: return n.value;
		case FplOp::Var:
			for (size_t i = 0; i < names.size(); i++)
				if (names[i] == n.name)
					return values[i];
			return NaN;
		case FplOp::Neg: return -ev(0);
		case FplOp::Add: return ev(0) + ev(1);
		case FplOp::Sub: return ev(0) - ev(1);
		case FplOp::Mul: return ev(0) * ev(1);
		case FplOp::Div: return ev(0) / ev(1);
		case FplOp::Pow: return FplPow(ev(0), ev(1));
		case FplOp::Fact: return Fact(ev(0));
		case FplOp::Call: {
			if (auto f = Func1(n.func))
				return f(ev(0));
			if (auto f = Func2(n.func))
				return f(ev(0), ev(1));
			std::vector<double> args(k.size());
			for (size_t i = 0; i < k.size(); i++)
				args[i] = ev(int(i));
			return FuncN(n.func)(args.data(), int(args.size()));
		}
		case FplOp::Cmp: return Compare(n.rel, ev(0), ev(1)) ? 1 : 0;
		case FplOp::And: return ev(0) != 0 && ev(1) != 0 ? 1 : 0;
		case FplOp::Piecewise: {
			size_t pairs = (k.size() - (n.hasDefault ? 1 : 0)) / 2;
			for (size_t i = 0; i < pairs; i++)
				if (ev(int(2 * i)) != 0)
					return ev(int(2 * i + 1));
			return n.hasDefault ? ev(int(k.size() - 1)) : NaN;
		}
		case FplOp::Tuple: return NaN;
	}
	return NaN;
}

// ---- Compiling -------------------------------------------------------------------------------------------------------

class FplCompiler {
public:
	FplCompiler(const FplAst& ast, const std::vector<std::wstring>& params, FplProgram& prog, FplError& error)
		: m_Ast(ast), m_Params(params), m_Prog(prog), m_Error(error) {}

	bool Compile(int node) {
		if (!Emit(node))
			return false;
		m_Prog.m_MaxDepth = m_Max;
		return true;
	}

private:
	void Push(FplInstr in, int delta) {
		m_Prog.m_Code.push_back(in);
		m_Depth += delta;
		m_Max = std::max(m_Max, m_Depth);
	}

	void Const(double v) {
		FplInstr in{ FplCode::Const };
		in.value = v;
		Push(in, 1);
	}

	bool Fail(FplErrorCode code, int node) {
		m_Error.code = code;
		m_Error.pos = m_Ast[node].pos;
		m_Error.len = m_Ast[node].len;
		if (m_Ast[node].op == FplOp::Var)
			m_Error.name = m_Ast[node].name;
		return false;
	}

	// The constant an exponent folds to, if it has no names.
	bool ConstantOf(int node, double& v) {
		if (FplHasNames(m_Ast, node) || m_Ast[node].op == FplOp::Tuple)
			return false;
		v = FplEvalTree(m_Ast, node, {}, nullptr);
		return true;
	}

	bool Emit(int node) {
		auto& n = m_Ast[node];
		auto& k = n.kids;
		if (n.op == FplOp::Tuple)
			return Fail(FplErrorCode::MisplacedPoint, node);
		double v;
		if (n.op != FplOp::Cmp && n.op != FplOp::And && ConstantOf(node, v)) {
			Const(v);
			return true;
		}
		switch (n.op) {
			case FplOp::Num:
				Const(n.value);
				return true;
			case FplOp::Var: {
				int slot = StandardSlot(n.name);
				if (slot < 0) {
					auto it = std::find(m_Params.begin(), m_Params.end(), n.name);
					if (it == m_Params.end() || n.name == L"r")
						return Fail(n.name == L"r" ? FplErrorCode::VariableNotAllowed : FplErrorCode::UnknownName, node);
					slot = FplFirstParam + int(it - m_Params.begin());
				}
				FplInstr in{ FplCode::Load };
				in.arg = slot;
				Push(in, 1);
				if (slot < 64)
					m_Prog.m_Slots |= uint64_t(1) << slot;
				return true;
			}
			case FplOp::Neg:
				return Emit(k[0]) && Op(FplCode::Neg, 0);
			case FplOp::Add:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Add, -1);
			case FplOp::Sub:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Sub, -1);
			case FplOp::Mul:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Mul, -1);
			case FplOp::Div:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::Div, -1);
			case FplOp::Pow: {
				if (!Emit(k[0]))
					return false;
				double e;
				if (ConstantOf(k[1], e) && e == std::floor(e) && std::abs(e) <= 64) {
					if (e == 2)
						return Op(FplCode::Sq, 0);
					FplInstr in{ FplCode::PowInt };
					in.arg = int(e);
					Push(in, 0);
					return true;
				}
				return Emit(k[1]) && Op(FplCode::Pow, -1);
			}
			case FplOp::Fact: {
				if (!Emit(k[0]))
					return false;
				FplInstr in{ FplCode::Call1 };
				in.f1 = Fact;
				Push(in, 0);
				return true;
			}
			case FplOp::Call: {
				for (int a : k)
					if (!Emit(a))
						return false;
				FplInstr in{ FplCode::Call1 };
				if ((in.f1 = Func1(n.func)) != nullptr)
					Push(in, 0);
				else if ((in.f2 = Func2(n.func)) != nullptr) {
					in.code = FplCode::Call2;
					Push(in, -1);
				}
				else {
					in.code = FplCode::CallN;
					in.fn = FuncN(n.func);
					in.arg = int(k.size());
					Push(in, 1 - int(k.size()));
				}
				return true;
			}
			case FplOp::Cmp: {
				static const FplCode codes[] = { FplCode::Eq, FplCode::Lt, FplCode::Le, FplCode::Gt, FplCode::Ge };
				return Emit(k[0]) && Emit(k[1]) && Op(codes[int(n.rel)], -1);
			}
			case FplOp::And:
				return Emit(k[0]) && Emit(k[1]) && Op(FplCode::And, -1);
			case FplOp::Piecewise: {
				size_t pairs = (k.size() - (n.hasDefault ? 1 : 0)) / 2;
				std::vector<size_t> toEnd;
				int base = m_Depth;
				for (size_t i = 0; i < pairs; i++) {
					if (!Emit(k[2 * i]))
						return false;
					size_t jump = m_Prog.m_Code.size();
					Push({ FplCode::JumpIfFalse }, -1);
					if (!Emit(k[2 * i + 1]))
						return false;
					toEnd.push_back(m_Prog.m_Code.size());
					Push({ FplCode::Jump }, 0);
					m_Prog.m_Code[jump].arg = int(m_Prog.m_Code.size());
					m_Depth = base;	// the next branch starts where this one did
				}
				if (n.hasDefault) {
					if (!Emit(k.back()))
						return false;
				}
				else
					Const(NaN);
				for (size_t j : toEnd)
					m_Prog.m_Code[j].arg = int(m_Prog.m_Code.size());
				return true;
			}
			default:
				return Fail(FplErrorCode::Internal, node);
		}
	}

	bool Op(FplCode code, int delta) {
		Push({ code }, delta);
		return true;
	}

	const FplAst& m_Ast;
	const std::vector<std::wstring>& m_Params;
	FplProgram& m_Prog;
	FplError& m_Error;
	int m_Depth = 0;
	int m_Max = 0;
};

FplProgram FplCompile(const FplAst& ast, int node, const std::vector<std::wstring>& parameters, FplError* error) {
	FplProgram prog;
	FplError local;
	FplError& err = error ? *error : local;
	if (node < 0) {
		err.code = FplErrorCode::Empty;
		return prog;
	}
	if (ast[node].op == FplOp::Cmp) {
		err.code = FplErrorCode::MisplacedRelation;
		err.pos = ast[node].pos;
		err.len = ast[node].len;
		return prog;
	}
	FplCompiler c(ast, parameters, prog, err);
	if (!c.Compile(node))
		return FplProgram();
	return prog;
}

// ---- The stack machine -----------------------------------------------------------------------------------------------

double FplProgram::Eval(const double* slots) const {
	if (m_Code.empty())
		return NaN;
	double small[32];
	std::vector<double> big;
	double* st = small;
	if (m_MaxDepth > 32) {
		big.resize(m_MaxDepth);
		st = big.data();
	}
	int sp = 0;	// the next free entry
	const FplInstr* code = m_Code.data();
	const size_t count = m_Code.size();
	for (size_t pc = 0; pc < count; ) {
		const FplInstr& in = code[pc++];
		switch (in.code) {
			case FplCode::Const: st[sp++] = in.value; break;
			case FplCode::Load: st[sp++] = slots[in.arg]; break;
			case FplCode::Neg: st[sp - 1] = -st[sp - 1]; break;
			case FplCode::Add: sp--; st[sp - 1] += st[sp]; break;
			case FplCode::Sub: sp--; st[sp - 1] -= st[sp]; break;
			case FplCode::Mul: sp--; st[sp - 1] *= st[sp]; break;
			case FplCode::Div: sp--; st[sp - 1] /= st[sp]; break;
			case FplCode::Pow: sp--; st[sp - 1] = FplPow(st[sp - 1], st[sp]); break;
			case FplCode::Sq: st[sp - 1] *= st[sp - 1]; break;
			case FplCode::PowInt: st[sp - 1] = PowInt(st[sp - 1], in.arg); break;
			case FplCode::Call1: st[sp - 1] = in.f1(st[sp - 1]); break;
			case FplCode::Call2: sp--; st[sp - 1] = in.f2(st[sp - 1], st[sp]); break;
			case FplCode::CallN: sp -= in.arg - 1; st[sp - 1] = in.fn(st + sp - 1, in.arg); break;
			case FplCode::Lt: sp--; st[sp - 1] = st[sp - 1] < st[sp] ? 1 : 0; break;
			case FplCode::Le: sp--; st[sp - 1] = st[sp - 1] <= st[sp] ? 1 : 0; break;
			case FplCode::Gt: sp--; st[sp - 1] = st[sp - 1] > st[sp] ? 1 : 0; break;
			case FplCode::Ge: sp--; st[sp - 1] = st[sp - 1] >= st[sp] ? 1 : 0; break;
			case FplCode::Eq: sp--; st[sp - 1] = st[sp - 1] == st[sp] ? 1 : 0; break;
			case FplCode::And: sp--; st[sp - 1] = st[sp - 1] != 0 && st[sp] != 0 ? 1 : 0; break;
			case FplCode::JumpIfFalse:
				if (st[--sp] == 0)
					pc = size_t(in.arg);
				break;
			case FplCode::Jump: pc = size_t(in.arg); break;
		}
	}
	return st[0];
}

// ---- All of it -------------------------------------------------------------------------------------------------------

FplExpression FplBuild(std::wstring_view text, const std::vector<std::wstring>& parameters, bool degrees) {
	FplExpression e;
	FplParseOptions options;
	options.parameters = &parameters;
	options.degrees = degrees;
	auto parsed = FplParse(text, options);
	if (parsed.error) {
		e.error = parsed.error;
		return e;
	}
	auto def = FplClassify(parsed.ast, parsed.root);
	if (def.error) {
		e.error = def.error;
		return e;
	}
	e.kind = def.kind;
	e.rel = def.rel;
	e.name = def.name;
	e.f = FplCompile(parsed.ast, def.expr[0], parameters, &e.error);
	if (!e.error && def.expr[1] >= 0)
		e.g = FplCompile(parsed.ast, def.expr[1], parameters, &e.error);
	return e;
}
