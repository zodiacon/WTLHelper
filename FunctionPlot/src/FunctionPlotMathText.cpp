// A display parse's tree as LaTeX or MathML (FunctionPlotMath.h): one walk over the tree, as FplLayoutMath's in what it shows, and
// a writer for each format.

#include "FunctionPlotMath.h"

#include <cstdio>
#include <cwctype>
#include <vector>

namespace {

const wchar_t PiSign = 0x03C0, TauSign = 0x03C4;

class Writer {
public:
	virtual ~Writer() = default;
	virtual std::wstring Number(const std::wstring& text) = 0;
	virtual std::wstring Name(const std::wstring& name) = 0;			// a letter or a word
	virtual std::wstring Function(const std::wstring& name) = 0;		// sin, gamma
	virtual std::wstring Operator(const std::wstring& op) = 0;			// + - = \cdot, by the walk's spelling (see Op)
	virtual std::wstring Row(const std::vector<std::wstring>& parts) = 0;
	virtual std::wstring Apply(const std::wstring& head, const std::wstring& argument) = 0;	// sin x
	virtual std::wstring Frac(const std::wstring& top, const std::wstring& bottom) = 0;
	virtual std::wstring Binomial(const std::wstring& top, const std::wstring& bottom) = 0;
	virtual std::wstring Sup(const std::wstring& base, const std::wstring& power) = 0;
	virtual std::wstring Sub(const std::wstring& base, const std::wstring& index) = 0;
	virtual std::wstring Primes(const std::wstring& base, int count) = 0;
	virtual std::wstring Sqrt(const std::wstring& inner) = 0;
	virtual std::wstring Root(const std::wstring& index, const std::wstring& inner) = 0;
	virtual std::wstring Fence(wchar_t open, wchar_t close, const std::wstring& inner) = 0;	// ( [ | { and F L for floor and ceiling
	virtual std::wstring Cases(const std::vector<std::pair<std::wstring, std::wstring>>& rows) = 0;
	virtual std::wstring Integral(const std::wstring& lo, const std::wstring& hi, const std::wstring& body, const std::wstring& variable) = 0;
	virtual std::wstring Series(bool product, const std::wstring& lo, const std::wstring& hi, const std::wstring& body) = 0;
	virtual std::wstring Differential() = 0;							// the upright d
	virtual std::wstring Top(const std::wstring& inner) = 0;
};

enum class Op { Plus, Minus, Dot, Comma, Eq, Lt, Gt, Le, Ge, Sim, Bang, Space };

// Functions with names written before a plain argument, as in the layout.
bool IsClassic(FplFunc f) {
	return (f >= FplFunc::Sin && f <= FplFunc::Cot) || (f >= FplFunc::Asin && f <= FplFunc::Acot) ||
		(f >= FplFunc::Sinh && f <= FplFunc::Acoth) || f == FplFunc::Ln || f == FplFunc::Log || f == FplFunc::Log2;
}

class Walk {
public:
	Walk(const FplAst& ast, Writer& writer) : m_Ast(ast), m_W(writer) {}

	std::wstring Node(int node) {
		auto& n = m_Ast[node];
		auto& k = n.kids;
		switch (n.op) {
			case FplOp::Num:
				if (n.name == std::wstring(1, PiSign))
					return m_W.Name(L"\x03C0");
				if (n.name == std::wstring(1, TauSign))
					return m_W.Name(L"\x03C4");
				if (n.name == L"e")
					return m_W.Name(L"e");
				if (!n.name.empty())
					return m_W.Number(n.name);
				else {
					wchar_t buf[32];
					swprintf_s(buf, L"%g", n.value);
					return m_W.Number(buf);
				}
			case FplOp::Var: return Variable(n.name, n.primes);
			case FplOp::Neg: return m_W.Row({ Operator(Op::Minus), Node(k[0]) });
			case FplOp::Add: return m_W.Row({ Node(k[0]), Operator(Op::Plus), Node(k[1]) });
			case FplOp::Sub: return m_W.Row({ Node(k[0]), Operator(Op::Minus), Node(k[1]) });
			case FplOp::Mul:
				if (n.implicit)
					return m_W.Row({ Whole(k[0]), Node(k[1]) });
				return m_W.Row({ Whole(k[0]), Operator(Op::Dot), Node(k[1]) });
			case FplOp::Div: return m_W.Frac(Node(Strip(k[0])), Node(Strip(k[1])));
			case FplOp::Pow: {
				auto& base = m_Ast[k[0]];
				if (base.op == FplOp::Call && IsClassic(base.func) && base.primes == 0 && base.kids.size() == 1)
					return Call(base, Strip(k[1]));
				return m_W.Sup(Whole(k[0]), Node(Strip(k[1])));
			}
			case FplOp::Fact: return m_W.Row({ Whole(k[0]), Operator(Op::Bang) });
			case FplOp::Call: return Call(n, -1);
			case FplOp::Cmp: return m_W.Row({ Node(k[0]), Relation(n.rel), Node(k[1]) });
			case FplOp::And: {
				auto& second = m_Ast[k[1]];
				return m_W.Row({ Node(k[0]), Relation(second.rel), Node(second.kids[1]) });
			}
			case FplOp::Group:
				return n.name == L"[" ? m_W.Fence(L'[', L']', Node(k[0])) : m_W.Fence(L'(', L')', Node(k[0]));
			case FplOp::Tuple: return m_W.Fence(L'(', L')', Separated(k));
			case FplOp::List: return m_W.Fence(L'[', L']', Separated(k));
			case FplOp::Sequence: return Separated(k, true);
			case FplOp::Piecewise: {
				if (k.size() == 2 && !n.hasDefault && m_Ast[k[1]].op == FplOp::Num && m_Ast[k[1]].pos == 0 && m_Ast[k[1]].len == 0)
					return m_W.Fence(L'{', L'}', Node(k[0]));
				std::vector<std::pair<std::wstring, std::wstring>> rows;
				for (size_t i = 0; i + 1 < k.size(); i += 2)
					rows.push_back({ Node(k[i + 1]), Node(k[i]) });
				if (n.hasDefault)
					rows.push_back({ Node(k.back()), L"" });
				return m_W.Cases(rows);
			}
			case FplOp::Integral:
				return m_W.Integral(Node(Strip(k[1])), Node(Strip(k[2])), Operand(k[0]), Variable(n.name, 0));
			case FplOp::Series: {
				const std::wstring lo = m_W.Row({ Variable(n.name, 0), Relation(FplRel::Eq), Node(Strip(k[1])) });
				return m_W.Series(n.product, lo, Node(Strip(k[2])), Operand(k[0]));
			}
			case FplOp::Deriv:
				return m_W.Row({ m_W.Frac(m_W.Differential(), m_W.Row({ m_W.Differential(), Variable(n.name, 0) })), Operand(k[0]) });
			case FplOp::Named:
				return m_W.Row({ Variable(n.name, n.primes), m_W.Fence(L'(', L')', Separated(k)) });
		}
		return {};
	}

private:
	// what a sum, a product, an integral or d/dx applies to, as the layout's Operand
	std::wstring Operand(int node) {
		auto op = m_Ast[node].op;
		if (op == FplOp::Add || op == FplOp::Sub)
			return m_W.Fence(L'(', L')', Node(node));
		return Node(node);
	}

	// what's applied to all of a sum, a product, an integral or d/dx, as the layout's Whole
	std::wstring Whole(int node) {
		auto op = m_Ast[node].op;
		if (op == FplOp::Series || op == FplOp::Integral || op == FplOp::Deriv)
			return m_W.Fence(L'(', L')', Node(node));
		return Node(node);
	}

	std::wstring Operator(Op op) {
		switch (op) {
			case Op::Plus: return m_W.Operator(L"+");
			case Op::Minus: return m_W.Operator(L"-");
			case Op::Dot: return m_W.Operator(L"dot");
			case Op::Comma: return m_W.Operator(L",");
			case Op::Eq: return m_W.Operator(L"=");
			case Op::Lt: return m_W.Operator(L"<");
			case Op::Gt: return m_W.Operator(L">");
			case Op::Le: return m_W.Operator(L"le");
			case Op::Ge: return m_W.Operator(L"ge");
			case Op::Sim: return m_W.Operator(L"sim");
			case Op::Bang: return m_W.Operator(L"!");
			case Op::Space: return m_W.Operator(L"space");
		}
		return {};
	}

	std::wstring Relation(FplRel rel) {
		switch (rel) {
			case FplRel::Lt: return Operator(Op::Lt);
			case FplRel::Gt: return Operator(Op::Gt);
			case FplRel::Le: return Operator(Op::Le);
			case FplRel::Ge: return Operator(Op::Ge);
			case FplRel::Fit: return Operator(Op::Sim);
			default: return Operator(Op::Eq);
		}
	}

	int Strip(int node) const {
		while (m_Ast[node].op == FplOp::Group)
			node = m_Ast[node].kids[0];
		return node;
	}

	std::wstring Variable(const std::wstring& name, int primes) {
		size_t letters = 0;
		while (letters < name.size() && !(name[letters] >= L'0' && name[letters] <= L'9'))
			letters++;
		std::wstring out = m_W.Name(name.substr(0, letters));
		if (letters < name.size())
			out = m_W.Sub(out, m_W.Number(name.substr(letters)));
		return primes > 0 ? m_W.Primes(out, primes) : out;
	}

	std::wstring Separated(const std::vector<int>& kids, bool wide = false) {
		std::vector<std::wstring> parts;
		for (size_t i = 0; i < kids.size(); i++) {
			if (i > 0) {
				parts.push_back(Operator(Op::Comma));
				if (wide)
					parts.push_back(Operator(Op::Space));
			}
			parts.push_back(Node(kids[i]));
		}
		return m_W.Row(parts);
	}

	bool IsPlainArgument(int node) const {
		auto& n = m_Ast[node];
		switch (n.op) {
			case FplOp::Var: case FplOp::Num:
				return true;
			case FplOp::Pow:
				return IsPlainArgument(n.kids[0]) && m_Ast[n.kids[0]].op != FplOp::Pow;
			case FplOp::Mul:
				return n.implicit && IsPlainArgument(n.kids[0]) && IsPlainArgument(n.kids[1]);
			default:
				return false;
		}
	}

	std::wstring Call(const FplNode& n, int power) {
		auto& k = n.kids;
		switch (n.func) {
			case FplFunc::Sqrt: return m_W.Sqrt(Node(Strip(k[0])));
			case FplFunc::Root: return m_W.Root(Node(Strip(k[0])), Node(Strip(k[1])));
			case FplFunc::Abs: return m_W.Fence(L'|', L'|', Node(Strip(k[0])));
			case FplFunc::Floor: return m_W.Fence(L'F', L'f', Node(Strip(k[0])));
			case FplFunc::Ceil: return m_W.Fence(L'C', L'c', Node(Strip(k[0])));
			case FplFunc::Exp: return m_W.Sup(m_W.Name(L"e"), Node(Strip(k[0])));
			default: break;
		}
		std::wstring name = FplFuncInfoOf(n.func).name;
		bool inverse = false;
		switch (n.func) {
			case FplFunc::Asin: name = L"sin"; inverse = true; break;
			case FplFunc::Acos: name = L"cos"; inverse = true; break;
			case FplFunc::Atan: name = L"tan"; inverse = true; break;
			case FplFunc::Asec: name = L"sec"; inverse = true; break;
			case FplFunc::Acsc: name = L"csc"; inverse = true; break;
			case FplFunc::Acot: name = L"cot"; inverse = true; break;
			case FplFunc::Asinh: name = L"sinh"; inverse = true; break;
			case FplFunc::Acosh: name = L"cosh"; inverse = true; break;
			case FplFunc::Atanh: name = L"tanh"; inverse = true; break;
			case FplFunc::Asech: name = L"sech"; inverse = true; break;
			case FplFunc::Acsch: name = L"csch"; inverse = true; break;
			case FplFunc::Acoth: name = L"coth"; inverse = true; break;
			case FplFunc::Log2: name = L"log"; break;
			default: break;
		}
		const FplFuncSymbol symbol = FplSymbolOf(n.func);
		if (n.func == FplFunc::NCr)
			return m_W.Binomial(Node(Strip(k[0])), Node(Strip(k[1])));
		if (n.func == FplFunc::Mod)
			return m_W.Row({ Operand(k[0]), m_W.Operator(L"mod"), Operand(k[1]) });
		std::wstring head = !*symbol.text ? m_W.Function(name) : symbol.italic ? m_W.Name(symbol.text) : m_W.Function(symbol.text);
		std::vector<int> args = k;
		if (n.func == FplFunc::BesselJ || n.func == FplFunc::BesselY || n.func == FplFunc::BesselI || n.func == FplFunc::BesselK) {
			head = m_W.Sub(head, Node(Strip(args[0])));
			args.erase(args.begin());
		}
		else if (n.func == FplFunc::LambertWm1)
			head = m_W.Sub(head, m_W.Row({ Operator(Op::Minus), m_W.Number(L"1") }));
		else if (n.func == FplFunc::Lgamma)
			head = m_W.Row({ m_W.Function(L"ln"), head });
		if (n.func == FplFunc::Log2)
			head = m_W.Sub(head, m_W.Number(L"2"));
		else if (n.func == FplFunc::Log && args.size() == 2) {
			head = m_W.Sub(head, Node(Strip(args[1])));
			args.pop_back();
		}
		if (inverse)
			head = m_W.Sup(head, m_W.Row({ Operator(Op::Minus), m_W.Number(L"1") }));
		if (power >= 0)
			head = m_W.Sup(head, Node(power));
		if (n.primes > 0)
			head = m_W.Primes(head, n.primes);
		if (args.size() == 1 && IsClassic(n.func) && IsPlainArgument(args[0]))
			return m_W.Apply(head, Node(args[0]));
		return m_W.Row({ head, m_W.Fence(L'(', L')', args.size() == 1 ? Node(Strip(args[0])) : Separated(args)) });
	}

	const FplAst& m_Ast;
	Writer& m_W;
};

// ---- LaTeX ------------------------------------------------------------------------------------------------------------

class LatexWriter : public Writer {
public:
	std::wstring Number(const std::wstring& text) override { return text; }

	std::wstring Name(const std::wstring& name) override {
		if (const wchar_t* greek = FplGreekSymbol(name))
			return Name(greek);
		if (name.size() > 1)
			return L"\\mathrm{" + name + L"}";
		if (name.empty())
			return name;
		if (const wchar_t* word = FplGreekWord(name[0]))
			return std::wstring(L"\\") + word;
		return name;
	}

	std::wstring Function(const std::wstring& name) override {
		// LaTeX's own, which set their spacing
		static const wchar_t* const Own[] = { L"sin", L"cos", L"tan", L"sec", L"csc", L"cot", L"sinh", L"cosh", L"tanh", L"coth", L"ln",
			L"log", L"min", L"max", L"exp" };
		for (auto own : Own)
			if (name == own)
				return L"\\" + name;
		if (name.size() == 1) {
			if (const wchar_t* word = FplGreekWord(name[0]))
				return std::wstring(L"\\") + word;
			return L"\\mathrm{" + name + L"}";
		}
		return L"\\operatorname{" + name + L"}";
	}

	std::wstring Operator(const std::wstring& op) override {
		if (op == L"dot") return L"\\cdot";
		if (op == L"le") return L"\\le";
		if (op == L"ge") return L"\\ge";
		if (op == L"sim") return L"\\sim";
		if (op == L"space") return L"\\quad";
		if (op == L"mod") return L"\\bmod";
		return op;
	}

	std::wstring Row(const std::vector<std::wstring>& parts) override {
		std::wstring out;
		for (auto& part : parts) {
			if (part.empty())
				continue;
			// a command's name would run on into a letter after it
			if (!out.empty() && std::iswalpha(part[0]) && EndsInCommand(out))
				out += L' ';
			// spaces around what's spaced by the reader anyway, for reading the text
			const bool spaced = IsSpacedOperator(part);
			if (spaced && !out.empty())
				out += L' ';
			out += part;
			if (spaced)
				out += L' ';
		}
		return out;
	}

	std::wstring Apply(const std::wstring& head, const std::wstring& argument) override { return head + L" " + argument; }
	std::wstring Frac(const std::wstring& top, const std::wstring& bottom) override { return L"\\frac{" + top + L"}{" + bottom + L"}"; }
	std::wstring Binomial(const std::wstring& top, const std::wstring& bottom) override { return L"\\binom{" + top + L"}{" + bottom + L"}"; }
	std::wstring Sup(const std::wstring& base, const std::wstring& power) override { return base + L"^{" + power + L"}"; }
	std::wstring Sub(const std::wstring& base, const std::wstring& index) override { return base + L"_{" + index + L"}"; }
	std::wstring Primes(const std::wstring& base, int count) override { return base + std::wstring(count, L'\''); }
	std::wstring Sqrt(const std::wstring& inner) override { return L"\\sqrt{" + inner + L"}"; }
	std::wstring Root(const std::wstring& index, const std::wstring& inner) override { return L"\\sqrt[" + index + L"]{" + inner + L"}"; }

	std::wstring Fence(wchar_t open, wchar_t close, const std::wstring& inner) override {
		// (\\lfloor is a command: a letter after it would be part of its name)
		const std::wstring before = Delim(open);
		const bool space = before.size() > 1 && std::iswalpha(before.back()) && !inner.empty() && std::iswalpha(inner[0]);
		return L"\\left" + before + (space ? L" " : L"") + inner + L"\\right" + Delim(close);
	}

	std::wstring Cases(const std::vector<std::pair<std::wstring, std::wstring>>& rows) override {
		std::wstring out = L"\\begin{cases} ";
		for (size_t i = 0; i < rows.size(); i++) {
			if (i > 0)
				out += L" \\\\ ";
			out += rows[i].first;
			if (!rows[i].second.empty())
				out += L" & " + rows[i].second;
		}
		return out + L" \\end{cases}";
	}

	std::wstring Integral(const std::wstring& lo, const std::wstring& hi, const std::wstring& body, const std::wstring& variable) override {
		return L"\\int_{" + lo + L"}^{" + hi + L"} " + body + L" \\, " + Differential() + L" " + variable;
	}

	std::wstring Series(bool product, const std::wstring& lo, const std::wstring& hi, const std::wstring& body) override {
		return std::wstring(product ? L"\\prod" : L"\\sum") + L"_{" + lo + L"}^{" + hi + L"} " + body;
	}

	std::wstring Differential() override { return L"\\mathrm{d}"; }
	std::wstring Top(const std::wstring& inner) override { return inner; }

private:
	static std::wstring Delim(wchar_t c) {
		switch (c) {
			case L'{': return L"\\{";
			case L'}': return L"\\}";
			case L'F': return L"\\lfloor";
			case L'f': return L"\\rfloor";
			case L'C': return L"\\lceil";
			case L'c': return L"\\rceil";
			default: return std::wstring(1, c);
		}
	}

	static bool EndsInCommand(const std::wstring& text) {
		size_t i = text.size();
		while (i > 0 && std::iswalpha(text[i - 1]))
			i--;
		return i > 0 && i < text.size() && text[i - 1] == L'\\';
	}

	static bool IsSpacedOperator(const std::wstring& part) {
		return part == L"+" || part == L"=" || part == L"<" || part == L">" || part == L"\\cdot" || part == L"\\le" ||
			part == L"\\ge" || part == L"\\sim";
	}
};

// ---- MathML -----------------------------------------------------------------------------------------------------------

class MathMLWriter : public Writer {
public:
	std::wstring Number(const std::wstring& text) override { return L"<mn>" + text + L"</mn>"; }

	std::wstring Name(const std::wstring& name) override {
		if (const wchar_t* greek = FplGreekSymbol(name))
			return Name(greek);
		// one letter is italic by itself, but for a capital Greek one; a word is set upright
		if (name.size() > 1 || (name.size() == 1 && name[0] >= 0x391 && name[0] <= 0x3A9))
			return L"<mi mathvariant=\"normal\">" + Escape(name) + L"</mi>";
		return L"<mi>" + Escape(name) + L"</mi>";
	}

	std::wstring Function(const std::wstring& name) override { return L"<mi mathvariant=\"normal\">" + Escape(name) + L"</mi><mo>&#x2061;</mo>"; }

	std::wstring Operator(const std::wstring& op) override {
		if (op == L"-") return L"<mo>&#x2212;</mo>";
		if (op == L"dot") return L"<mo>&#xB7;</mo>";
		if (op == L"le") return L"<mo>&#x2264;</mo>";
		if (op == L"ge") return L"<mo>&#x2265;</mo>";
		if (op == L"sim") return L"<mo>&#x223C;</mo>";
		if (op == L"space") return L"<mspace width=\"1em\"/>";
		if (op == L"mod") return L"<mo>mod</mo>";
		return L"<mo>" + Escape(op) + L"</mo>";
	}

	std::wstring Row(const std::vector<std::wstring>& parts) override {
		std::wstring out = L"<mrow>";
		for (auto& part : parts)
			out += part;
		return out + L"</mrow>";
	}

	std::wstring Apply(const std::wstring& head, const std::wstring& argument) override { return Row({ head, argument }); }
	std::wstring Frac(const std::wstring& top, const std::wstring& bottom) override { return L"<mfrac>" + One(top) + One(bottom) + L"</mfrac>"; }
	std::wstring Binomial(const std::wstring& top, const std::wstring& bottom) override {
		return L"<mrow><mo stretchy=\"true\">(</mo><mfrac linethickness=\"0\">" + One(top) + One(bottom) + L"</mfrac><mo stretchy=\"true\">)</mo></mrow>";
	}
	std::wstring Sup(const std::wstring& base, const std::wstring& power) override { return L"<msup>" + One(base) + One(power) + L"</msup>"; }
	std::wstring Sub(const std::wstring& base, const std::wstring& index) override { return L"<msub>" + One(base) + One(index) + L"</msub>"; }

	std::wstring Primes(const std::wstring& base, int count) override {
		std::wstring primes;
		for (int i = 0; i < count; i++)
			primes += L"&#x2032;";
		return L"<msup>" + One(base) + L"<mo>" + primes + L"</mo></msup>";
	}

	std::wstring Sqrt(const std::wstring& inner) override { return L"<msqrt>" + inner + L"</msqrt>"; }
	std::wstring Root(const std::wstring& index, const std::wstring& inner) override { return L"<mroot>" + One(inner) + One(index) + L"</mroot>"; }

	std::wstring Fence(wchar_t open, wchar_t close, const std::wstring& inner) override {
		return L"<mrow><mo stretchy=\"true\">" + Delim(open) + L"</mo>" + inner + L"<mo stretchy=\"true\">" + Delim(close) + L"</mo></mrow>";
	}

	std::wstring Cases(const std::vector<std::pair<std::wstring, std::wstring>>& rows) override {
		std::wstring out = L"<mrow><mo stretchy=\"true\">{</mo><mtable columnalign=\"left\">";
		for (auto& [value, condition] : rows) {
			out += L"<mtr><mtd>" + value + L"</mtd><mtd>" + condition + L"</mtd></mtr>";
		}
		return out + L"</mtable></mrow>";
	}

	std::wstring Integral(const std::wstring& lo, const std::wstring& hi, const std::wstring& body, const std::wstring& variable) override {
		return L"<mrow><msubsup><mo>&#x222B;</mo>" + One(lo) + One(hi) + L"</msubsup>" + body + L"<mspace width=\"0.2em\"/>" + Differential() + variable + L"</mrow>";
	}

	std::wstring Series(bool product, const std::wstring& lo, const std::wstring& hi, const std::wstring& body) override {
		return std::wstring(L"<mrow><munderover><mo>") + (product ? L"&#x220F;" : L"&#x2211;") + L"</mo>" + One(lo) + One(hi) + L"</munderover>" + body + L"</mrow>";
	}

	std::wstring Differential() override { return L"<mi mathvariant=\"normal\">d</mi>"; }

	std::wstring Top(const std::wstring& inner) override {
		return L"<math xmlns=\"http://www.w3.org/1998/Math/MathML\">" + inner + L"</math>";
	}

private:
	// fractions and scripts take exactly one child each
	static std::wstring One(const std::wstring& part) {
		return L"<mrow>" + part + L"</mrow>";
	}

	static std::wstring Delim(wchar_t c) {
		switch (c) {
			case L'F': return L"&#x230A;";
			case L'f': return L"&#x230B;";
			case L'C': return L"&#x2308;";
			case L'c': return L"&#x2309;";
			default: return std::wstring(1, c);
		}
	}

	static std::wstring Escape(const std::wstring& text) {
		std::wstring out;
		for (wchar_t c : text) {
			switch (c) {
				case L'&': out += L"&amp;"; break;
				case L'<': out += L"&lt;"; break;
				case L'>': out += L"&gt;"; break;
				default: out += c;
			}
		}
		return out;
	}
};

std::wstring Convert(std::wstring_view text, FplParseOptions options, Writer& writer) {
	options.display = true;
	options.degrees = false;
	auto parsed = FplParse(text, options);
	if (parsed.error || parsed.root < 0)
		return {};
	Walk walk(parsed.ast, writer);
	return writer.Top(walk.Node(parsed.root));
}

}  // namespace

std::wstring FplToLatex(std::wstring_view text, const FplParseOptions& options) {
	LatexWriter writer;
	return Convert(text, options, writer);
}

std::wstring FplToMathML(std::wstring_view text, const FplParseOptions& options) {
	MathMLWriter writer;
	return Convert(text, options, writer);
}
