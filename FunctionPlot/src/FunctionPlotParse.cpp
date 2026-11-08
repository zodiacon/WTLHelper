// Function plot expressions: the tree, the function table, error messages, the lexer and the parser.

#include "FunctionPlotExpr.h"

#include <cmath>
#include <cwctype>
#include <numbers>

namespace {

const FplFuncInfo g_Funcs[] = {
	{ L"sin", 1, 1 }, { L"cos", 1, 1 }, { L"tan", 1, 1 }, { L"sec", 1, 1 }, { L"csc", 1, 1 }, { L"cot", 1, 1 },
	{ L"asin", 1, 1 }, { L"acos", 1, 1 }, { L"atan", 1, 1 }, { L"atan2", 2, 2 },
	{ L"sinh", 1, 1 }, { L"cosh", 1, 1 }, { L"tanh", 1, 1 }, { L"asinh", 1, 1 }, { L"acosh", 1, 1 }, { L"atanh", 1, 1 },
	{ L"exp", 1, 1 }, { L"ln", 1, 1 }, { L"log", 1, 1 }, { L"log2", 1, 1 },
	{ L"sqrt", 1, 1 }, { L"cbrt", 1, 1 }, { L"root", 2, 2 },
	{ L"abs", 1, 1 }, { L"sign", 1, 1 }, { L"floor", 1, 1 }, { L"ceil", 1, 1 }, { L"round", 1, 1 },
	{ L"min", 1, -1 }, { L"max", 1, -1 }, { L"mod", 2, 2 }, { L"hypot", 2, 2 },
	{ L"gamma", 1, 1 }, { L"digamma", 1, 1 }, { L"erf", 1, 1 },
};
static_assert(std::size(g_Funcs) == size_t(FplFunc::Count));

struct Alias {
	const wchar_t* name;
	FplFunc func;
};
const Alias g_Aliases[] = {
	{ L"arcsin", FplFunc::Asin }, { L"arccos", FplFunc::Acos }, { L"arctan", FplFunc::Atan },
	{ L"arsinh", FplFunc::Asinh }, { L"arcosh", FplFunc::Acosh }, { L"artanh", FplFunc::Atanh },
	{ L"arcsinh", FplFunc::Asinh }, { L"arccosh", FplFunc::Acosh }, { L"arctanh", FplFunc::Atanh },
	{ L"sgn", FplFunc::Sign }, { L"lg", FplFunc::Log },
};

constexpr wchar_t Pi[] = L"\x03C0";
constexpr wchar_t Tau[] = L"\x03C4";
constexpr wchar_t LessEq = L'\x2264';
constexpr wchar_t GreaterEq = L'\x2265';
constexpr wchar_t SqrtSign = L'\x221A';

}	// namespace

const FplFuncInfo& FplFuncInfoOf(FplFunc f) {
	return g_Funcs[size_t(f)];
}

bool FplIsVariable(std::wstring_view name) {
	return name == L"x" || name == L"y" || name == L"t" || name == L"r" || name == FplTheta;
}

// ---- The tree --------------------------------------------------------------------------------------------------------

int FplAst::Add(FplNode n) {
	nodes.push_back(std::move(n));
	return int(nodes.size()) - 1;
}

int FplAst::Num(double v, std::wstring_view name) {
	FplNode n;
	n.op = FplOp::Num;
	n.value = v;
	n.name = name;
	return Add(std::move(n));
}

int FplAst::Var(std::wstring_view name) {
	FplNode n;
	n.op = FplOp::Var;
	n.name = name;
	return Add(std::move(n));
}

int FplAst::Unary(FplOp op, int a) {
	FplNode n;
	n.op = op;
	n.kids = { a };
	return Add(std::move(n));
}

int FplAst::Binary(FplOp op, int a, int b) {
	FplNode n;
	n.op = op;
	n.kids = { a, b };
	return Add(std::move(n));
}

int FplAst::Call(FplFunc f, std::vector<int> args) {
	FplNode n;
	n.op = FplOp::Call;
	n.func = f;
	n.kids = std::move(args);
	return Add(std::move(n));
}

int FplAst::Cmp(FplRel rel, int a, int b) {
	FplNode n;
	n.op = FplOp::Cmp;
	n.rel = rel;
	n.kids = { a, b };
	return Add(std::move(n));
}

// ---- Errors ----------------------------------------------------------------------------------------------------------

std::wstring FplError::Message() const {
	switch (code) {
		case FplErrorCode::None: return {};
		case FplErrorCode::Empty: return L"Nothing to plot";
		case FplErrorCode::UnexpectedChar: return L"Unexpected character '" + name + L"'";
		case FplErrorCode::BadNumber: return L"Malformed number '" + name + L"'";
		case FplErrorCode::UnexpectedToken: return L"Unexpected '" + name + L"'";
		case FplErrorCode::UnexpectedEnd: return L"The expression is incomplete";
		case FplErrorCode::MissingClose: return L"Missing '" + name + L"'";
		case FplErrorCode::UnknownName: return L"Unknown name '" + name + L"'";
		case FplErrorCode::ArgCount: return L"Wrong number of arguments to '" + name + L"'";
		case FplErrorCode::MissingArgument: return L"'" + name + L"' needs an argument";
		case FplErrorCode::TooManyRelations: return L"Only one '=', '<' or '>' is allowed";
		case FplErrorCode::MisplacedRelation: return L"A comparison can only be the whole equation or a condition in { }";
		case FplErrorCode::MisplacedPoint: return L"A point can't be used as a number";
		case FplErrorCode::MisplacedCondition: return L"Only the last item in { } can be a value without a condition";
		case FplErrorCode::VariableNotAllowed: return L"'" + name + L"' can't be used here";
		case FplErrorCode::NotAPlot: return L"Nothing to plot: write an equation such as y = f(x)";
		case FplErrorCode::BadPoint: return L"A point has two coordinates";
		case FplErrorCode::NotDifferentiable: return L"Can't differentiate an equation or a point";
		case FplErrorCode::Redefined: return L"'" + name + L"' is already defined";
		case FplErrorCode::Internal: return L"Internal error";
	}
	return {};
}

// ---- The lexer -------------------------------------------------------------------------------------------------------

namespace {

enum class Tk : uint8_t { Num, Name, Op, Sup, End };
enum class NameKind : uint8_t { Func, Const, Var, Param, Unknown };

struct Token {
	Tk type = Tk::End;
	wchar_t op = 0;				// Op: '+' '-' '*' '/' '^' '(' ')' '[' ']' '{' '}' ',' ':' '|' '!' '<' '>' '=' LessEq GreaterEq SqrtSign
	NameKind kind = NameKind::Unknown;
	FplFunc func = FplFunc::Sin;
	double value = 0;			// Num, Const, Sup
	std::wstring name;			// Name: canonical (theta is theta, pi is pi); Num: its text
	std::wstring hint;			// Unknown: the part to report (an unknown letter of "ax"), at hintPos
	int hintPos = 0;
	bool definable = false;		// Unknown: no part of it is known, so "name = ..." may define it
	int pos = 0, len = 0;
};

bool IsLetter(wchar_t c) {
	return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= 0x391 && c <= 0x3C9 && c != 0x3A2);
}

bool IsDigit(wchar_t c) {
	return c >= L'0' && c <= L'9';
}

class Lexer {
public:
	Lexer(std::wstring_view text, const std::vector<std::wstring>* params) : m_Text(text), m_Params(params) {}

	bool Run(std::vector<Token>& tokens, FplError& error) {
		size_t i = 0;
		while (i < m_Text.size()) {
			wchar_t c = m_Text[i];
			if (std::iswspace(c)) {
				i++;
				continue;
			}
			int start = int(i);
			if (IsDigit(c) || (c == L'.' && i + 1 < m_Text.size() && IsDigit(m_Text[i + 1]))) {
				if (!Number(i, tokens, error))
					return false;
				continue;
			}
			if (IsLetter(c)) {
				size_t j = i + 1;
				while (j < m_Text.size() && (IsLetter(m_Text[j]) || IsDigit(m_Text[j]) || m_Text[j] == L'_'))
					j++;
				Identifier(m_Text.substr(i, j - i), start, tokens);
				i = j;
				continue;
			}
			Token t;
			t.type = Tk::Op;
			t.pos = start;
			t.len = 1;
			wchar_t next = i + 1 < m_Text.size() ? m_Text[i + 1] : 0;
			switch (c) {
				case L'+': case L'/': case L'^': case L'(': case L')': case L'[': case L']': case L'{': case L'}':
				case L',': case L':': case L'|': case L'!': case SqrtSign:
					t.op = c;
					break;
				case L'-': case L'\x2212':
					t.op = L'-';
					break;
				case L'*':
					t.op = L'*';
					if (next == L'*') {
						t.op = L'^';
						t.len = 2;
					}
					break;
				case L'\x00D7': case L'\x00B7': case L'\x22C5':
					t.op = L'*';
					break;
				case L'\x00F7':
					t.op = L'/';
					break;
				case L'<':
					t.op = c;
					if (next == L'=') {
						t.op = LessEq;
						t.len = 2;
					}
					break;
				case L'>':
					t.op = c;
					if (next == L'=') {
						t.op = GreaterEq;
						t.len = 2;
					}
					break;
				case L'=':
					t.op = c;
					if (next == L'=')
						t.len = 2;
					break;
				case LessEq: case GreaterEq:
					t.op = c;
					break;
				case L'\x00B2': case L'\x00B3':
					t.type = Tk::Sup;
					t.value = c == L'\x00B2' ? 2 : 3;
					break;
				default:
					error.code = FplErrorCode::UnexpectedChar;
					error.pos = start;
					error.len = 1;
					error.name = std::wstring(1, c);
					return false;
			}
			tokens.push_back(std::move(t));
			i += tokens.back().len;
		}
		Token end;
		end.pos = int(m_Text.size());
		tokens.push_back(end);
		return true;
	}

private:
	bool Number(size_t& i, std::vector<Token>& tokens, FplError& error) {
		size_t start = i;
		int dots = 0;
		while (i < m_Text.size() && (IsDigit(m_Text[i]) || m_Text[i] == L'.')) {
			if (m_Text[i] == L'.')
				dots++;
			i++;
		}
		// An exponent only when digits follow: "2e" is 2 times e, "2e-x" is 2e minus x.
		if (i < m_Text.size() && (m_Text[i] == L'e' || m_Text[i] == L'E')) {
			size_t j = i + 1;
			if (j < m_Text.size() && (m_Text[j] == L'+' || m_Text[j] == L'-'))
				j++;
			if (j < m_Text.size() && IsDigit(m_Text[j])) {
				while (j < m_Text.size() && IsDigit(m_Text[j]))
					j++;
				i = j;
			}
		}
		std::wstring s(m_Text.substr(start, i - start));
		if (dots > 1) {
			error.code = FplErrorCode::BadNumber;
			error.pos = int(start);
			error.len = int(s.size());
			error.name = s;
			return false;
		}
		Token t;
		t.type = Tk::Num;
		t.value = wcstod(s.c_str(), nullptr);
		t.name = s;
		t.pos = int(start);
		t.len = int(s.size());
		tokens.push_back(std::move(t));
		return true;
	}

	// What a whole name is, if it's known.
	bool Lookup(std::wstring_view s, Token& t) const {
		t.type = Tk::Name;
		for (size_t f = 0; f < std::size(g_Funcs); f++)
			if (s == g_Funcs[f].name) {
				t.kind = NameKind::Func;
				t.func = FplFunc(f);
				t.name = s;
				return true;
			}
		for (auto& a : g_Aliases)
			if (s == a.name) {
				t.kind = NameKind::Func;
				t.func = a.func;
				t.name = g_Funcs[size_t(a.func)].name;
				return true;
			}
		if (s == L"pi" || s == Pi) {
			t.kind = NameKind::Const;
			t.value = std::numbers::pi;
			t.name = Pi;
			return true;
		}
		if (s == L"tau" || s == Tau) {
			t.kind = NameKind::Const;
			t.value = 2 * std::numbers::pi;
			t.name = Tau;
			return true;
		}
		if (s == L"e") {
			t.kind = NameKind::Const;
			t.value = std::numbers::e;
			t.name = L"e";
			return true;
		}
		if (s == L"theta" || FplIsVariable(s)) {
			t.kind = NameKind::Var;
			t.name = s == L"theta" ? std::wstring(FplTheta) : std::wstring(s);
			return true;
		}
		if (m_Params)
			for (auto& p : *m_Params)
				if (s == p) {
					t.kind = NameKind::Param;
					t.name = s;
					return true;
				}
		return false;
	}

	// A name that isn't known whole is split into known names ("xy", "sinx", "pix"): the fewest unknown single letters
	// first, then the fewest parts. Only names of letters are split. One that can't be split whole stays one unknown
	// token, reporting its first unknown letter (an app may offer it as a parameter), else the whole name.
	void Identifier(std::wstring_view s, int pos, std::vector<Token>& tokens) const {
		Token t;
		t.pos = pos;
		t.len = int(s.size());
		if (Lookup(s, t)) {
			tokens.push_back(std::move(t));
			return;
		}
		t.type = Tk::Name;
		t.kind = NameKind::Unknown;
		t.name = s;
		t.hint = s;
		t.hintPos = pos;
		t.definable = true;
		bool letters = true;
		for (wchar_t c : s)
			letters = letters && IsLetter(c);
		if (letters && s.size() > 1) {
			// best[i]: the best split of s[i..]: (unknowns, parts) and the length of the first part.
			size_t n = s.size();
			struct Best { int unknown = 1 << 20, parts = 1 << 20, len = 0; };
			std::vector<Best> best(n + 1);
			best[n] = { 0, 0, 0 };
			Token probe;
			for (size_t i = n; i-- > 0; ) {
				for (size_t len = 1; i + len <= n; len++) {
					bool known = Lookup(s.substr(i, len), probe);
					if (!known && len > 1)
						continue;
					Best b = { best[i + len].unknown + (known ? 0 : 1), best[i + len].parts + 1, int(len) };
					if (b.unknown < best[i].unknown || (b.unknown == best[i].unknown && b.parts < best[i].parts))
						best[i] = b;
				}
			}
			if (best[0].unknown == 0) {
				for (size_t i = 0; i < n; i += best[i].len) {
					Token part;
					part.pos = pos + int(i);
					part.len = best[i].len;
					Lookup(s.substr(i, best[i].len), part);
					tokens.push_back(std::move(part));
				}
				return;
			}
			bool hinted = false;
			for (size_t i = 0; i < n; i += best[i].len) {
				if (Lookup(s.substr(i, best[i].len), probe))
					t.definable = false;
				else if (!hinted) {
					t.hint = s.substr(i, best[i].len);
					t.hintPos = pos + int(i);
					hinted = true;
				}
			}
		}
		tokens.push_back(std::move(t));
	}

	std::wstring_view m_Text;
	const std::vector<std::wstring>* m_Params;
};

// ---- The parser ------------------------------------------------------------------------------------------------------

struct ParseError {};

class Parser {
public:
	Parser(std::vector<Token> tokens, FplAst& ast, FplError& error) : m_Tokens(std::move(tokens)), m_Ast(ast), m_Error(error) {}

	int Top() {
		if (Peek().type == Tk::End)
			Fail(FplErrorCode::Empty, Peek());
		// "a = 2" with an unknown a defines it.
		if (Peek().type == Tk::Name && Peek().kind == NameKind::Unknown && Peek().definable && IsOp(m_Tokens[m_Pos + 1], L'=')) {
			auto& t = Next();
			int lhs = Make(m_Ast.Var(t.name), t.pos, t.len);
			Next();
			int rhs = Sum();
			Expect(Tk::End);
			return Span(m_Ast.Cmp(FplRel::Eq, lhs, rhs), lhs, rhs);
		}
		int node = Sum();
		if (IsRelation(Peek())) {
			FplRel rel = RelOf(Next());
			int rhs = Sum();
			if (IsRelation(Peek()))
				Fail(FplErrorCode::TooManyRelations, Peek());
			node = Span(m_Ast.Cmp(rel, node, rhs), node, rhs);
		}
		Expect(Tk::End);
		CheckTuples(node, true);
		return node;
	}

private:
	const Token& Peek() const { return m_Tokens[m_Pos]; }
	const Token& Next() { return m_Tokens[m_Pos < m_Tokens.size() - 1 ? m_Pos++ : m_Pos]; }

	static bool IsOp(const Token& t, wchar_t op) { return t.type == Tk::Op && t.op == op; }
	static bool IsRelation(const Token& t) {
		return t.type == Tk::Op && (t.op == L'<' || t.op == L'>' || t.op == L'=' || t.op == LessEq || t.op == GreaterEq);
	}
	static FplRel RelOf(const Token& t) {
		switch (t.op) {
			case L'<': return FplRel::Lt;
			case L'>': return FplRel::Gt;
			case LessEq: return FplRel::Le;
			case GreaterEq: return FplRel::Ge;
			default: return FplRel::Eq;
		}
	}

	[[noreturn]] void Fail(FplErrorCode code, const Token& t, std::wstring name = {}) {
		m_Error.code = code;
		m_Error.pos = t.pos;
		m_Error.len = t.len;
		if (code == FplErrorCode::UnknownName) {
			m_Error.pos = t.hintPos;
			m_Error.len = int(t.hint.size());
			name = t.hint;
		}
		if (name.empty()) {
			if (t.type == Tk::Name || t.type == Tk::Num)
				name = t.name;
			else if (t.type == Tk::Op)
				name = std::wstring(1, t.op);
			else if (t.type == Tk::Sup)
				name = t.value == 2 ? L"\x00B2" : L"\x00B3";
		}
		m_Error.name = std::move(name);
		if (t.type == Tk::End && code == FplErrorCode::UnexpectedToken)
			m_Error.code = FplErrorCode::UnexpectedEnd;
		throw ParseError();
	}

	void Expect(Tk type) {
		if (Peek().type != type)
			Fail(FplErrorCode::UnexpectedToken, Peek());
	}

	int Make(int node, int pos, int len) {
		m_Ast.nodes[node].pos = pos;
		m_Ast.nodes[node].len = len;
		return node;
	}

	int Span(int node, int first, int last) {
		int pos = m_Ast[first].pos;
		return Make(node, pos, m_Ast[last].pos + m_Ast[last].len - pos);
	}

	// A token that can start a factor of an implicit product. Not a number ("x 2" and "3 4" are mistakes), and not a
	// '|' that closes an open one.
	bool StartsImplicitFactor(const Token& t) const {
		if (t.type == Tk::Name)
			return true;
		if (t.type != Tk::Op)
			return false;
		if (t.op == L'|')
			return m_AbsDepth == 0;
		return t.op == L'(' || t.op == L'[' || t.op == L'{' || t.op == SqrtSign;
	}

	int Sum() {
		int node = Product();
		while (IsOp(Peek(), L'+') || IsOp(Peek(), L'-')) {
			FplOp op = Next().op == L'+' ? FplOp::Add : FplOp::Sub;
			int rhs = Product();
			node = Span(m_Ast.Binary(op, node, rhs), node, rhs);
		}
		return node;
	}

	int Product() {
		int node = Unary();
		for (;;) {
			if (IsOp(Peek(), L'*') || IsOp(Peek(), L'/')) {
				FplOp op = Next().op == L'*' ? FplOp::Mul : FplOp::Div;
				int rhs = Unary();
				node = Span(m_Ast.Binary(op, node, rhs), node, rhs);
			}
			else if (StartsImplicitFactor(Peek())) {
				int rhs = Power();
				node = Span(m_Ast.Binary(FplOp::Mul, node, rhs), node, rhs);
			}
			else if (Peek().type == Tk::Num) {
				Fail(FplErrorCode::UnexpectedToken, Peek());	// "3 4", "x 2"
			}
			else
				return node;
		}
	}

	int Unary() {
		if (IsOp(Peek(), L'-')) {
			auto& t = Next();
			int a = Unary();
			return Make(m_Ast.Unary(FplOp::Neg, a), t.pos, m_Ast[a].pos + m_Ast[a].len - t.pos);
		}
		if (IsOp(Peek(), L'+')) {
			Next();
			return Unary();
		}
		return Power();
	}

	int Power() {
		int node = Postfix();
		if (IsOp(Peek(), L'^')) {
			Next();
			int e = Unary();
			node = Span(m_Ast.Binary(FplOp::Pow, node, e), node, e);
		}
		return node;
	}

	int Postfix() {
		int node = Primary();
		for (;;) {
			auto& t = Peek();
			if (IsOp(t, L'!')) {
				Next();
				node = Make(m_Ast.Unary(FplOp::Fact, node), m_Ast[node].pos, t.pos + t.len - m_Ast[node].pos);
			}
			else if (t.type == Tk::Sup) {
				Next();
				int e = Make(m_Ast.Num(t.value), t.pos, t.len);
				node = Span(m_Ast.Binary(FplOp::Pow, node, e), node, e);
			}
			else
				return node;
		}
	}

	int Primary() {
		auto& t = Next();
		switch (t.type) {
			case Tk::Num:
				return Make(m_Ast.Num(t.value), t.pos, t.len);

			case Tk::Name:
				switch (t.kind) {
					case NameKind::Const: return Make(m_Ast.Num(t.value, t.name), t.pos, t.len);
					case NameKind::Var:
					case NameKind::Param: return Make(m_Ast.Var(t.name), t.pos, t.len);
					case NameKind::Func: return Function(t);
					case NameKind::Unknown: Fail(FplErrorCode::UnknownName, t);
				}
				break;

			case Tk::Op:
				switch (t.op) {
					case L'(':
					case L'[': {
						wchar_t close = t.op == L'(' ? L')' : L']';
						int saved = m_AbsDepth;
						m_AbsDepth = 0;
						std::vector<int> items = { Sum() };
						while (IsOp(Peek(), L','))
							Next(), items.push_back(Sum());
						Close(close);
						m_AbsDepth = saved;
						if (items.size() == 1)
							return items[0];
						FplNode n;
						n.op = FplOp::Tuple;
						n.kids = std::move(items);
						return Make(m_Ast.Add(std::move(n)), t.pos, m_Tokens[m_Pos - 1].pos + 1 - t.pos);
					}
					case L'|': {
						m_AbsDepth++;
						int a = Sum();
						m_AbsDepth--;
						Close(L'|');
						return Make(m_Ast.Call(FplFunc::Abs, { a }), t.pos, m_Tokens[m_Pos - 1].pos + 1 - t.pos);
					}
					case L'{':
						return Piecewise(t);
					case SqrtSign: {
						int a = Postfix();
						return Make(m_Ast.Call(FplFunc::Sqrt, { a }), t.pos, m_Ast[a].pos + m_Ast[a].len - t.pos);
					}
				}
				break;

			default:
				break;
		}
		Fail(FplErrorCode::UnexpectedToken, t);
	}

	void Close(wchar_t close) {
		if (IsOp(Peek(), close)) {
			Next();
			return;
		}
		if (Peek().type == Tk::End)
			Fail(FplErrorCode::MissingClose, Peek(), std::wstring(1, close));
		Fail(FplErrorCode::UnexpectedToken, Peek());
	}

	// A function name and its argument: "f(a, b)", or "f x" with the implicit product after it, or "f^2 x" ((f x)^2;
	// "sin^-1 x" is asin x as calculators write it).
	int Function(const Token& name) {
		FplFunc func = name.func;
		const auto& info = FplFuncInfoOf(func);
		int power = -1;
		if (Peek().type == Tk::Sup) {
			auto& s = Next();
			power = Make(m_Ast.Num(s.value), s.pos, s.len);
		}
		else if (IsOp(Peek(), L'^')) {
			Next();
			if (IsOp(Peek(), L'-') && m_Tokens[m_Pos + 1].type == Tk::Num && m_Tokens[m_Pos + 1].value == 1 && Inverse(func) != func) {
				Next();
				Next();
				func = Inverse(func);
			}
			else
				power = Postfix();
		}
		const auto& finfo = FplFuncInfoOf(func);
		std::vector<int> args;
		int end;
		if (IsOp(Peek(), L'(') || IsOp(Peek(), L'[')) {
			wchar_t close = Next().op == L'(' ? L')' : L']';
			int saved = m_AbsDepth;
			m_AbsDepth = 0;
			args.push_back(Sum());
			while (IsOp(Peek(), L','))
				Next(), args.push_back(Sum());
			Close(close);
			m_AbsDepth = saved;
			end = m_Tokens[m_Pos - 1].pos + 1;
		}
		else {
			if (!StartsArgument(Peek()))
				Fail(FplErrorCode::MissingArgument, name, info.name);
			int a = ImplicitArgument();
			args.push_back(a);
			end = m_Ast[a].pos + m_Ast[a].len;
		}
		if (int(args.size()) < finfo.minArgs || (finfo.maxArgs >= 0 && int(args.size()) > finfo.maxArgs)) {
			m_Error.code = FplErrorCode::ArgCount;
			m_Error.pos = name.pos;
			m_Error.len = end - name.pos;
			m_Error.name = finfo.name;
			throw ParseError();
		}
		int call = Make(m_Ast.Call(func, std::move(args)), name.pos, end - name.pos);
		if (power >= 0)
			call = Span(m_Ast.Binary(FplOp::Pow, call, power), call, power);
		return call;
	}

	static FplFunc Inverse(FplFunc f) {
		switch (f) {
			case FplFunc::Sin: return FplFunc::Asin;
			case FplFunc::Cos: return FplFunc::Acos;
			case FplFunc::Tan: return FplFunc::Atan;
			case FplFunc::Sinh: return FplFunc::Asinh;
			case FplFunc::Cosh: return FplFunc::Acosh;
			case FplFunc::Tanh: return FplFunc::Atanh;
			default: return f;
		}
	}

	bool StartsArgument(const Token& t) const {
		return t.type == Tk::Num || IsOp(t, L'-') || IsOp(t, L'+') || StartsImplicitFactor(t);
	}

	// "sin 2x" is sin(2x), "sin x cos x" is sin(x) cos(x), "sin x/2" is sin(x)/2, "sin -x" is sin(-x).
	int ImplicitArgument() {
		if (IsOp(Peek(), L'-')) {
			auto& t = Next();
			int a = ImplicitArgument();
			return Make(m_Ast.Unary(FplOp::Neg, a), t.pos, m_Ast[a].pos + m_Ast[a].len - t.pos);
		}
		if (IsOp(Peek(), L'+')) {
			Next();
			return ImplicitArgument();
		}
		int node = Power();
		while (StartsImplicitFactor(Peek()) && !(Peek().type == Tk::Name && Peek().kind == NameKind::Func)) {
			int rhs = Power();
			node = Span(m_Ast.Binary(FplOp::Mul, node, rhs), node, rhs);
		}
		return node;
	}

	// {cond: value, ..., default}; a condition alone is worth 1 ({0 < x < 1}).
	int Piecewise(const Token& open) {
		FplNode n;
		n.op = FplOp::Piecewise;
		int saved = m_AbsDepth;
		m_AbsDepth = 0;
		for (;;) {
			auto& first = Peek();
			bool isCondition = false;
			int item = Condition(isCondition);
			if (IsOp(Peek(), L':')) {
				if (!isCondition)
					Fail(FplErrorCode::UnexpectedToken, Peek());
				Next();
				n.kids.push_back(item);
				n.kids.push_back(Sum());
			}
			else if (isCondition) {
				n.kids.push_back(item);
				n.kids.push_back(m_Ast.Num(1));
			}
			else {
				if (!IsOp(Peek(), L'}'))
					Fail(FplErrorCode::MisplacedCondition, first);
				n.kids.push_back(item);
				n.hasDefault = true;
			}
			if (IsOp(Peek(), L','))
				Next();
			else
				break;
		}
		Close(L'}');
		m_AbsDepth = saved;
		return Make(m_Ast.Add(std::move(n)), open.pos, m_Tokens[m_Pos - 1].pos + 1 - open.pos);
	}

	int Condition(bool& isCondition) {
		int node = Sum();
		isCondition = false;
		int prev = node;
		while (IsRelation(Peek())) {
			FplRel rel = RelOf(Next());
			int rhs = Sum();
			int cmp = Span(m_Ast.Cmp(rel, prev, rhs), prev, rhs);
			node = isCondition ? Span(m_Ast.Binary(FplOp::And, node, cmp), node, cmp) : cmp;
			isCondition = true;
			prev = rhs;
		}
		return node;
	}

	void CheckTuples(int node, bool top) {
		auto& n = m_Ast[node];
		if (n.op == FplOp::Tuple && !top) {
			m_Error.code = FplErrorCode::MisplacedPoint;
			m_Error.pos = n.pos;
			m_Error.len = n.len;
			throw ParseError();
		}
		for (int k : n.kids)
			CheckTuples(k, false);
	}

	std::vector<Token> m_Tokens;
	size_t m_Pos = 0;
	int m_AbsDepth = 0;
	FplAst& m_Ast;
	FplError& m_Error;
};

bool IsTrig(FplFunc f) {
	return f >= FplFunc::Sin && f <= FplFunc::Cot;
}

bool IsInverseTrig(FplFunc f) {
	return f >= FplFunc::Asin && f <= FplFunc::Atan2;
}

// Degrees: the trigonometric functions' arguments times pi/180, the inverse ones' results times 180/pi.
int ToDegrees(FplAst& ast, int node, std::vector<int>& memo) {
	if (memo[node] >= 0)
		return memo[node];
	FplNode n = ast[node];
	bool changed = false;
	for (auto& k : n.kids) {
		int nk = ToDegrees(ast, k, memo);
		changed = changed || nk != k;
		k = nk;
	}
	int result = node;
	if (n.op == FplOp::Call && IsTrig(n.func)) {
		int arg = ast.Binary(FplOp::Mul, n.kids[0], ast.Num(std::numbers::pi / 180));
		n.kids[0] = arg;
		result = ast.Add(std::move(n));
	}
	else if (n.op == FplOp::Call && IsInverseTrig(n.func)) {
		int pos = n.pos, len = n.len;
		int call = ast.Add(std::move(n));
		result = ast.Binary(FplOp::Mul, call, ast.Num(180 / std::numbers::pi));
		ast.nodes[result].pos = pos;
		ast.nodes[result].len = len;
	}
	else if (changed)
		result = ast.Add(std::move(n));
	memo[node] = result;
	return result;
}

}	// namespace

FplParseResult FplParse(std::wstring_view text, const FplParseOptions& options) {
	FplParseResult result;
	std::vector<Token> tokens;
	Lexer lexer(text, options.parameters);
	if (!lexer.Run(tokens, result.error))
		return result;
	Parser parser(std::move(tokens), result.ast, result.error);
	try {
		result.root = parser.Top();
	}
	catch (const ParseError&) {
		result.root = -1;
		return result;
	}
	if (options.degrees) {
		std::vector<int> memo(result.ast.nodes.size(), -1);
		result.root = ToDegrees(result.ast, result.root, memo);
	}
	return result;
}
