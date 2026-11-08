// Function plot expressions: what a parsed expression draws, derivatives, simplifying and printing.

#include "FunctionPlotExpr.h"

#include <cmath>
#include <cstdio>
#include <numbers>

namespace {

unsigned VarBit(std::wstring_view name) {
	if (name == L"x") return FplVarX;
	if (name == L"y") return FplVarY;
	if (name == L"t") return FplVarT;
	if (name == L"r") return FplVarR;
	if (name == FplTheta) return FplVarTheta;
	return 0;
}

const wchar_t* VarName(unsigned bit) {
	switch (bit) {
		case FplVarX: return L"x";
		case FplVarY: return L"y";
		case FplVarT: return L"t";
		case FplVarR: return L"r";
		default: return FplTheta;
	}
}

// The first node using one of the variables in mask, for the error's position.
int FindVar(const FplAst& ast, int node, unsigned mask) {
	auto& n = ast[node];
	if (n.op == FplOp::Var)
		return (VarBit(n.name) & mask) ? node : -1;
	for (int k : n.kids) {
		int f = FindVar(ast, k, mask);
		if (f >= 0)
			return f;
	}
	return -1;
}

void NotAllowed(FplPlotDef& def, const FplAst& ast, int node, unsigned mask) {
	int v = FindVar(ast, node, mask);
	def.error.code = FplErrorCode::VariableNotAllowed;
	if (v >= 0) {
		def.error.pos = ast[v].pos;
		def.error.len = ast[v].len;
		def.error.name = ast[v].name;
	}
}

FplRel Flip(FplRel rel) {
	switch (rel) {
		case FplRel::Lt: return FplRel::Gt;
		case FplRel::Gt: return FplRel::Lt;
		case FplRel::Le: return FplRel::Ge;
		case FplRel::Ge: return FplRel::Le;
		default: return rel;
	}
}

bool IsVarNamed(const FplAst& ast, int node, std::wstring_view name) {
	return ast[node].op == FplOp::Var && ast[node].name == name;
}

// theta and r in an implicit equation: atan2(y, x) and hypot(x, y).
int SubstitutePolar(FplAst& ast, int node, std::vector<int>& memo) {
	if (memo[node] >= 0)
		return memo[node];
	FplNode n = ast[node];
	int result = node;
	if (n.op == FplOp::Var && (n.name == L"r" || n.name == FplTheta)) {
		int x = ast.Var(L"x"), y = ast.Var(L"y");
		result = n.name == L"r" ? ast.Call(FplFunc::Hypot, { x, y }) : ast.Call(FplFunc::Atan2, { y, x });
		ast.nodes[result].pos = n.pos;
		ast.nodes[result].len = n.len;
	}
	else {
		bool changed = false;
		for (auto& k : n.kids) {
			int nk = SubstitutePolar(ast, k, memo);
			changed = changed || nk != k;
			k = nk;
		}
		if (changed)
			result = ast.Add(std::move(n));
	}
	memo[node] = result;
	return result;
}

}	// namespace

unsigned FplVariablesOf(const FplAst& ast, int node) {
	auto& n = ast[node];
	if (n.op == FplOp::Var)
		return VarBit(n.name);
	unsigned mask = 0;
	for (int k : n.kids)
		mask |= FplVariablesOf(ast, k);
	return mask;
}

bool FplHasNames(const FplAst& ast, int node) {
	auto& n = ast[node];
	if (n.op == FplOp::Var)
		return true;
	for (int k : n.kids)
		if (FplHasNames(ast, k))
			return true;
	return false;
}

// ---- Classifying -----------------------------------------------------------------------------------------------------

FplPlotDef FplClassify(FplAst& ast, int root) {
	FplPlotDef def;
	if (root < 0) {
		def.error.code = FplErrorCode::Empty;
		return def;
	}
	const FplNode top = ast[root];

	if (top.op == FplOp::Tuple) {
		if (top.kids.size() != 2) {
			def.error.code = FplErrorCode::BadPoint;
			def.error.pos = top.pos;
			def.error.len = top.len;
			return def;
		}
		unsigned vars = FplVariablesOf(ast, root);
		if (vars & ~FplVarT) {
			NotAllowed(def, ast, root, ~unsigned(FplVarT));
			return def;
		}
		def.kind = vars ? FplKind::Parametric : FplKind::Point;
		def.expr[0] = top.kids[0];
		def.expr[1] = top.kids[1];
		return def;
	}

	if (top.op != FplOp::Cmp) {
		// A bare expression is y = f(x).
		unsigned vars = FplVariablesOf(ast, root);
		if (vars & ~FplVarX) {
			def.error.code = FplErrorCode::NotAPlot;
			def.error.pos = top.pos;
			def.error.len = top.len;
			return def;
		}
		def.kind = FplKind::Function;
		def.expr[0] = root;
		return def;
	}

	int lhs = top.kids[0], rhs = top.kids[1];
	FplRel rel = top.rel;
	unsigned lv = FplVariablesOf(ast, lhs), rv = FplVariablesOf(ast, rhs);

	// name = value: a definition.
	if (ast[lhs].op == FplOp::Var && !FplIsVariable(ast[lhs].name) && rel == FplRel::Eq) {
		if (rv) {
			NotAllowed(def, ast, rhs, ~0u);
			return def;
		}
		def.kind = FplKind::Definition;
		def.name = ast[lhs].name;
		def.expr[0] = rhs;
		return def;
	}

	// Turn "f(x) > y" around to "y < f(x)".
	auto oneSide = [&](std::wstring_view var, unsigned bit, unsigned allowed, FplKind kind) {
		int side = -1, other = -1;
		FplRel r = rel;
		if (IsVarNamed(ast, lhs, var) && !(rv & bit))
			side = lhs, other = rhs;
		else if (IsVarNamed(ast, rhs, var) && !(lv & bit))
			side = rhs, other = lhs, r = Flip(rel);
		if (side < 0 || (FplVariablesOf(ast, other) & ~allowed))
			return false;
		def.kind = kind;
		def.rel = r;
		def.expr[0] = other;
		return true;
	};
	if (oneSide(L"y", FplVarY, FplVarX, FplKind::Function) || oneSide(L"x", FplVarX, FplVarY, FplKind::FunctionOfY)
		|| oneSide(L"r", FplVarR, FplVarTheta, FplKind::Polar))
		return def;

	// Anything else in x and y is F(x, y) rel 0, with theta and r as atan2(y, x) and hypot(x, y).
	unsigned vars = lv | rv;
	if (vars & FplVarT) {
		NotAllowed(def, ast, root, FplVarT);
		return def;
	}
	if (!(vars & (FplVarX | FplVarY | FplVarR | FplVarTheta))) {
		def.error.code = FplErrorCode::NotAPlot;
		def.error.pos = top.pos;
		def.error.len = top.len;
		return def;
	}
	int f = ast.Binary(FplOp::Sub, lhs, rhs);
	ast.nodes[f].pos = top.pos;
	ast.nodes[f].len = top.len;
	if (vars & (FplVarR | FplVarTheta)) {
		std::vector<int> memo(ast.nodes.size(), -1);
		f = SubstitutePolar(ast, f, memo);
	}
	def.kind = FplKind::Implicit;
	def.rel = rel;
	def.expr[0] = f;
	return def;
}

// ---- Simplifying -----------------------------------------------------------------------------------------------------

namespace {

bool IsNum(const FplAst& ast, int node, double v) {
	return ast[node].op == FplOp::Num && ast[node].value == v;
}

bool IsNum(const FplAst& ast, int node) {
	return ast[node].op == FplOp::Num;
}

int Simplify(FplAst& ast, int node, std::vector<int>& memo) {
	if (size_t(node) < memo.size() && memo[node] >= 0)
		return memo[node];
	FplNode n = ast[node];
	bool changed = false;
	for (auto& k : n.kids) {
		int nk = Simplify(ast, k, memo);
		changed = changed || nk != k;
		k = nk;
	}
	int result = -1;
	auto& kids = n.kids;
	bool allNums = !kids.empty() && n.op != FplOp::Tuple && n.op != FplOp::Piecewise;
	for (int k : kids)
		allNums = allNums && IsNum(ast, k);
	if (allNums) {
		int tmp = ast.Add(n);
		result = ast.Num(FplEvalTree(ast, tmp, {}, nullptr));
	}
	else {
		switch (n.op) {
			case FplOp::Neg:
				if (ast[kids[0]].op == FplOp::Neg)
					result = ast[kids[0]].kids[0];
				break;
			case FplOp::Add:
				if (IsNum(ast, kids[0], 0))
					result = kids[1];
				else if (IsNum(ast, kids[1], 0))
					result = kids[0];
				else if (ast[kids[1]].op == FplOp::Neg)
					result = ast.Binary(FplOp::Sub, kids[0], ast[kids[1]].kids[0]);
				break;
			case FplOp::Sub:
				if (IsNum(ast, kids[1], 0))
					result = kids[0];
				else if (IsNum(ast, kids[0], 0))
					result = Simplify(ast, ast.Unary(FplOp::Neg, kids[1]), memo);
				else if (ast[kids[1]].op == FplOp::Neg)
					result = ast.Binary(FplOp::Add, kids[0], ast[kids[1]].kids[0]);
				break;
			case FplOp::Mul:
				if (IsNum(ast, kids[0], 0) || IsNum(ast, kids[1], 0))
					result = ast.Num(0);
				else if (IsNum(ast, kids[0], 1))
					result = kids[1];
				else if (IsNum(ast, kids[1], 1))
					result = kids[0];
				else if (IsNum(ast, kids[0], -1))
					result = Simplify(ast, ast.Unary(FplOp::Neg, kids[1]), memo);
				else if (IsNum(ast, kids[1], -1))
					result = Simplify(ast, ast.Unary(FplOp::Neg, kids[0]), memo);
				else if (IsNum(ast, kids[1]) && !IsNum(ast, kids[0]))
					result = Simplify(ast, ast.Binary(FplOp::Mul, kids[1], kids[0]), memo);	// numbers first
				else if (IsNum(ast, kids[0]) && ast[kids[1]].op == FplOp::Mul && IsNum(ast, ast[kids[1]].kids[0]))
					result = Simplify(ast, ast.Binary(FplOp::Mul, ast.Num(ast[kids[0]].value * ast[ast[kids[1]].kids[0]].value),
						ast[kids[1]].kids[1]), memo);
				else if (ast[kids[0]].op == FplOp::Neg && ast[kids[1]].op == FplOp::Neg)
					result = ast.Binary(FplOp::Mul, ast[kids[0]].kids[0], ast[kids[1]].kids[0]);
				break;
			case FplOp::Div:
				if (IsNum(ast, kids[1], 1))
					result = kids[0];
				else if (IsNum(ast, kids[0], 0))
					result = ast.Num(0);
				break;
			case FplOp::Pow:
				if (IsNum(ast, kids[1], 1))
					result = kids[0];
				else if (IsNum(ast, kids[1], 0))
					result = ast.Num(1);
				break;
			default:
				break;
		}
	}
	if (result < 0)
		result = changed ? ast.Add(std::move(n)) : node;
	if (size_t(node) < memo.size())
		memo[node] = result;
	return result;
}

}	// namespace

int FplSimplify(FplAst& ast, int node) {
	std::vector<int> memo(ast.nodes.size(), -1);
	return Simplify(ast, node, memo);
}

// ---- Derivatives -----------------------------------------------------------------------------------------------------

namespace {

class Deriver {
public:
	Deriver(FplAst& ast, std::wstring_view var) : m_Ast(ast), m_Var(var), m_Memo(ast.nodes.size(), -1) {}

	int D(int node) {
		if (size_t(node) < m_Memo.size() && m_Memo[node] >= 0)
			return m_Memo[node];
		int result = Derive(node);
		if (size_t(node) < m_Memo.size())
			m_Memo[node] = result;
		return result;
	}

	bool failed = false;

private:
	int Num(double v) { return m_Ast.Num(v); }
	int Add(int a, int b) { return m_Ast.Binary(FplOp::Add, a, b); }
	int Sub(int a, int b) { return m_Ast.Binary(FplOp::Sub, a, b); }
	int Mul(int a, int b) { return m_Ast.Binary(FplOp::Mul, a, b); }
	int Div(int a, int b) { return m_Ast.Binary(FplOp::Div, a, b); }
	int Pow(int a, int b) { return m_Ast.Binary(FplOp::Pow, a, b); }
	int Neg(int a) { return m_Ast.Unary(FplOp::Neg, a); }
	int Call(FplFunc f, int a) { return m_Ast.Call(f, { a }); }
	int Sq(int a) { return Pow(a, Num(2)); }

	bool Depends(int node) {
		auto& n = m_Ast[node];
		if (n.op == FplOp::Var)
			return n.name == m_Var;
		for (int k : n.kids)
			if (Depends(k))
				return true;
		return false;
	}

	int Derive(int node) {
		if (!Depends(node))
			return Num(0);
		const FplNode n = m_Ast[node];
		const auto& k = n.kids;
		switch (n.op) {
			case FplOp::Num:
				return Num(0);
			case FplOp::Var:
				return Num(1);
			case FplOp::Neg:
				return Neg(D(k[0]));
			case FplOp::Add:
				return Add(D(k[0]), D(k[1]));
			case FplOp::Sub:
				return Sub(D(k[0]), D(k[1]));
			case FplOp::Mul:
				return Add(Mul(D(k[0]), k[1]), Mul(k[0], D(k[1])));
			case FplOp::Div:
				if (!Depends(k[1]))
					return Div(D(k[0]), k[1]);
				return Div(Sub(Mul(D(k[0]), k[1]), Mul(k[0], D(k[1]))), Sq(k[1]));
			case FplOp::Pow:
				if (!Depends(k[1])) {
					// n u^(n-1) u'
					return Mul(Mul(k[1], Pow(k[0], Sub(k[1], Num(1)))), D(k[0]));
				}
				if (!Depends(k[0])) {
					// a^v ln(a) v'
					return Mul(Mul(node, Call(FplFunc::Ln, k[0])), D(k[1]));
				}
				// u^v (v' ln u + v u'/u)
				return Mul(node, Add(Mul(D(k[1]), Call(FplFunc::Ln, k[0])), Div(Mul(k[1], D(k[0])), k[0])));
			case FplOp::Fact:
				// u! = gamma(u + 1), whose derivative is gamma(u + 1) digamma(u + 1)
				return Mul(Mul(node, Call(FplFunc::Digamma, Add(k[0], Num(1)))), D(k[0]));
			case FplOp::Call:
				return Function(node, n);
			case FplOp::Piecewise: {
				FplNode p = n;
				for (size_t i = 1; i < p.kids.size(); i += 2)
					p.kids[i] = D(p.kids[i]);
				if (p.hasDefault)
					p.kids.back() = D(p.kids.back());
				p.pos = p.len = 0;
				return m_Ast.Add(std::move(p));
			}
			case FplOp::Tuple: {
				FplNode t = n;
				for (auto& kid : t.kids)
					kid = D(kid);
				t.pos = t.len = 0;
				return m_Ast.Add(std::move(t));
			}
			case FplOp::Cmp:
			case FplOp::And:
				failed = true;
				return Num(0);
		}
		failed = true;
		return Num(0);
	}

	int Function(int node, const FplNode& n) {
		const auto& k = n.kids;
		int u = k[0];
		auto chain = [&](int outer) { return Mul(outer, D(u)); };
		switch (n.func) {
			case FplFunc::Sin: return chain(Call(FplFunc::Cos, u));
			case FplFunc::Cos: return chain(Neg(Call(FplFunc::Sin, u)));
			case FplFunc::Tan: return chain(Sq(Call(FplFunc::Sec, u)));
			case FplFunc::Sec: return chain(Mul(node, Call(FplFunc::Tan, u)));
			case FplFunc::Csc: return chain(Neg(Mul(node, Call(FplFunc::Cot, u))));
			case FplFunc::Cot: return chain(Neg(Sq(Call(FplFunc::Csc, u))));
			case FplFunc::Asin: return Div(D(u), Call(FplFunc::Sqrt, Sub(Num(1), Sq(u))));
			case FplFunc::Acos: return Neg(Div(D(u), Call(FplFunc::Sqrt, Sub(Num(1), Sq(u)))));
			case FplFunc::Atan: return Div(D(u), Add(Num(1), Sq(u)));
			case FplFunc::Atan2: {
				// atan2(y, x)' = (x y' - y x') / (x^2 + y^2)
				int y = k[0], x = k[1];
				return Div(Sub(Mul(x, D(y)), Mul(y, D(x))), Add(Sq(x), Sq(y)));
			}
			case FplFunc::Sinh: return chain(Call(FplFunc::Cosh, u));
			case FplFunc::Cosh: return chain(Call(FplFunc::Sinh, u));
			case FplFunc::Tanh: return chain(Sub(Num(1), Sq(node)));
			case FplFunc::Asinh: return Div(D(u), Call(FplFunc::Sqrt, Add(Sq(u), Num(1))));
			case FplFunc::Acosh: return Div(D(u), Call(FplFunc::Sqrt, Sub(Sq(u), Num(1))));
			case FplFunc::Atanh: return Div(D(u), Sub(Num(1), Sq(u)));
			case FplFunc::Exp: return chain(node);
			case FplFunc::Ln: return Div(D(u), u);
			case FplFunc::Log: return Div(D(u), Mul(u, Num(std::numbers::ln10)));
			case FplFunc::Log2: return Div(D(u), Mul(u, Num(std::numbers::ln2)));
			case FplFunc::Sqrt: return Div(D(u), Mul(Num(2), node));
			case FplFunc::Cbrt: return Div(D(u), Mul(Num(3), Sq(node)));
			case FplFunc::Root:
				// root(n, x) = x^(1/n)
				return D(m_Ast.Binary(FplOp::Pow, k[1], Div(Num(1), k[0])));
			case FplFunc::Abs: return chain(Call(FplFunc::Sign, u));
			case FplFunc::Sign:
			case FplFunc::Floor:
			case FplFunc::Ceil:
			case FplFunc::Round:
				return Num(0);
			case FplFunc::Min:
			case FplFunc::Max: {
				// Folded pairwise: min(a, b) is {a < b: a', b'}.
				int acc = k[0];
				int dacc = D(k[0]);
				for (size_t i = 1; i < k.size(); i++) {
					FplNode p;
					p.op = FplOp::Piecewise;
					p.hasDefault = true;
					int cond = m_Ast.Cmp(n.func == FplFunc::Min ? FplRel::Lt : FplRel::Gt, acc, k[i]);
					p.kids = { cond, dacc, D(k[i]) };
					dacc = m_Ast.Add(std::move(p));
					acc = m_Ast.Call(n.func, { acc, k[i] });
				}
				return dacc;
			}
			case FplFunc::Mod:
				// mod(a, b) = a - b floor(a/b)
				return Sub(D(k[0]), Mul(D(k[1]), Call(FplFunc::Floor, Div(k[0], k[1]))));
			case FplFunc::Hypot:
				return Div(Add(Mul(k[0], D(k[0])), Mul(k[1], D(k[1]))), node);
			case FplFunc::Gamma:
				return chain(Mul(node, Call(FplFunc::Digamma, u)));
			case FplFunc::Erf:
				return chain(Mul(Num(2 / std::sqrt(std::numbers::pi)), Call(FplFunc::Exp, Neg(Sq(u)))));
			default:
				failed = true;
				return Num(0);
		}
	}

	FplAst& m_Ast;
	std::wstring m_Var;
	std::vector<int> m_Memo;
};

}	// namespace

int FplDerive(FplAst& ast, int node, std::wstring_view var, FplError* error) {
	auto op = ast[node].op;
	if (op == FplOp::Cmp || op == FplOp::And) {
		if (error) {
			error->code = FplErrorCode::NotDifferentiable;
			error->pos = ast[node].pos;
			error->len = ast[node].len;
		}
		return -1;
	}
	Deriver d(ast, var);
	int result = d.D(node);
	if (d.failed) {
		if (error) {
			error->code = FplErrorCode::NotDifferentiable;
			error->pos = ast[node].pos;
			error->len = ast[node].len;
		}
		return -1;
	}
	return FplSimplify(ast, result);
}

// ---- Printing --------------------------------------------------------------------------------------------------------

namespace {

// Binding strength: relations 0, + - 1, * / 2, unary minus 3, ^ 4, ! 5, atoms 6.
int Prec(const FplAst& ast, int node) {
	auto& n = ast[node];
	switch (n.op) {
		case FplOp::Cmp: case FplOp::And: return 0;
		case FplOp::Add: case FplOp::Sub: return 1;
		case FplOp::Mul: case FplOp::Div: return 2;
		case FplOp::Neg: return 3;
		case FplOp::Pow: return 4;
		case FplOp::Fact: return 5;
		case FplOp::Num: return n.value < 0 || std::signbit(n.value) ? 3 : 6;
		default: return 6;
	}
}

std::wstring Number(double v) {
	if (std::isnan(v))
		return L"NaN";
	if (std::isinf(v))
		return v > 0 ? L"inf" : L"-inf";
	wchar_t buf[40];
	swprintf_s(buf, L"%.15g", v);
	return buf;
}

const wchar_t* RelText(FplRel rel) {
	switch (rel) {
		case FplRel::Lt: return L" < ";
		case FplRel::Le: return L" <= ";
		case FplRel::Gt: return L" > ";
		case FplRel::Ge: return L" >= ";
		default: return L" = ";
	}
}

void Print(const FplAst& ast, int node, std::wstring& out);

void Wrapped(const FplAst& ast, int node, bool paren, std::wstring& out) {
	if (paren)
		out += L'(';
	Print(ast, node, out);
	if (paren)
		out += L')';
}

void Print(const FplAst& ast, int node, std::wstring& out) {
	auto& n = ast[node];
	auto& k = n.kids;
	int p = Prec(ast, node);
	switch (n.op) {
		case FplOp::Num:
			out += n.name.empty() ? Number(n.value) : n.name;
			break;
		case FplOp::Var:
			out += n.name;
			break;
		case FplOp::Neg:
			out += L'-';
			Wrapped(ast, k[0], Prec(ast, k[0]) <= 3, out);
			break;
		case FplOp::Add:
		case FplOp::Sub:
		case FplOp::Mul:
		case FplOp::Div: {
			Wrapped(ast, k[0], Prec(ast, k[0]) < p, out);
			out += n.op == FplOp::Add ? L" + " : n.op == FplOp::Sub ? L" - " : n.op == FplOp::Mul ? L"*" : L"/";
			// The right side binds as if it came first: a - (b + c), a/(b*c); a + -b reads fine but not a*-b.
			bool paren = n.op == FplOp::Add ? Prec(ast, k[1]) < 1 || (Prec(ast, k[1]) == 3) : Prec(ast, k[1]) <= p;
			Wrapped(ast, k[1], paren, out);
			break;
		}
		case FplOp::Pow:
			Wrapped(ast, k[0], Prec(ast, k[0]) <= 4, out);
			out += L'^';
			Wrapped(ast, k[1], Prec(ast, k[1]) < 3, out);
			break;
		case FplOp::Fact:
			Wrapped(ast, k[0], Prec(ast, k[0]) < 5, out);
			out += L'!';
			break;
		case FplOp::Call:
			out += FplFuncInfoOf(n.func).name;
			out += L'(';
			for (size_t i = 0; i < k.size(); i++) {
				if (i)
					out += L", ";
				Print(ast, k[i], out);
			}
			out += L')';
			break;
		case FplOp::Cmp:
			Print(ast, k[0], out);
			out += RelText(n.rel);
			Print(ast, k[1], out);
			break;
		case FplOp::And: {
			// A chain sharing its middle prints as one: 0 < x < 1.
			auto& a = ast[k[0]];
			auto& b = ast[k[1]];
			Print(ast, k[0], out);
			if (a.op == FplOp::Cmp && b.op == FplOp::Cmp && a.kids[1] == b.kids[0]) {
				out += RelText(b.rel);
				Print(ast, b.kids[1], out);
			}
			else {
				out += L" and ";
				Print(ast, k[1], out);
			}
			break;
		}
		case FplOp::Piecewise: {
			out += L'{';
			size_t pairs = (k.size() - (n.hasDefault ? 1 : 0)) / 2;
			for (size_t i = 0; i < pairs; i++) {
				if (i)
					out += L", ";
				Print(ast, k[2 * i], out);
				if (!IsNum(ast, k[2 * i + 1], 1) || !ast[k[2 * i + 1]].name.empty() || n.hasDefault || pairs > 1) {
					out += L": ";
					Print(ast, k[2 * i + 1], out);
				}
			}
			if (n.hasDefault) {
				if (pairs)
					out += L", ";
				Print(ast, k.back(), out);
			}
			out += L'}';
			break;
		}
		case FplOp::Tuple:
			out += L'(';
			for (size_t i = 0; i < k.size(); i++) {
				if (i)
					out += L", ";
				Print(ast, k[i], out);
			}
			out += L')';
			break;
	}
}

}	// namespace

std::wstring FplToString(const FplAst& ast, int node) {
	std::wstring out;
	if (node >= 0)
		Print(ast, node, out);
	return out;
}
