// Laying a parsed expression out as mathematics (FunctionPlotMath.h). Sizes are in ems of the size being laid out, loosely after
// TeX's: a text's box is a fixed height whatever its letters, so a row's baseline and heights don't jump as it's typed.

#include "FunctionPlotMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace {

struct GreekName {
	const wchar_t* word;
	wchar_t letter;
};
// Not the capitals that look like Latin letters, or omicron, which TeX has no names for.
const GreekName g_Greek[] = {
	{ L"alpha", 0x3B1 }, { L"beta", 0x3B2 }, { L"gamma", 0x3B3 }, { L"delta", 0x3B4 }, { L"epsilon", 0x3B5 }, { L"zeta", 0x3B6 },
	{ L"eta", 0x3B7 }, { L"theta", 0x3B8 }, { L"iota", 0x3B9 }, { L"kappa", 0x3BA }, { L"lambda", 0x3BB }, { L"mu", 0x3BC },
	{ L"nu", 0x3BD }, { L"xi", 0x3BE }, { L"pi", 0x3C0 }, { L"rho", 0x3C1 }, { L"sigma", 0x3C3 }, { L"tau", 0x3C4 },
	{ L"upsilon", 0x3C5 }, { L"phi", 0x3C6 }, { L"chi", 0x3C7 }, { L"psi", 0x3C8 }, { L"omega", 0x3C9 },
	{ L"Gamma", 0x393 }, { L"Delta", 0x394 }, { L"Theta", 0x398 }, { L"Lambda", 0x39B }, { L"Xi", 0x39E }, { L"Pi", 0x3A0 },
	{ L"Sigma", 0x3A3 }, { L"Upsilon", 0x3A5 }, { L"Phi", 0x3A6 }, { L"Psi", 0x3A8 }, { L"Omega", 0x3A9 },
};

constexpr double Ascent = 0.72, Descent = 0.22;		// a text's box
constexpr double Axis = 0.26;						// where a fraction's bar is, above the baseline
constexpr double ScriptScale = 0.72, FractionScale = 0.88, MinSize = 7;

const wchar_t Minus = 0x2212, Dot = 0x00B7, PrimeSign = 0x2032;

struct Box {
	std::vector<FplMathItem> items;
	double w = 0, asc = 0, desc = 0;
};

void Place(Box& into, const Box& box, double x, double y) {
	for (auto item : box.items) {
		item.x += x;
		item.y += y;
		into.items.push_back(std::move(item));
	}
	into.w = std::max(into.w, x + box.w);
	into.asc = std::max(into.asc, box.asc - y);
	into.desc = std::max(into.desc, box.desc + y);
}

void Append(Box& row, const Box& box, double gap = 0) {
	Place(row, box, row.w + gap, 0);
}

class Layouter {
public:
	Layouter(const FplAst& ast, const FplMathMeasure& measure) : m_Ast(ast), m_Measure(measure) {}

	Box Node(int node, double size) {
		auto& n = m_Ast[node];
		auto& k = n.kids;
		switch (n.op) {
			case FplOp::Num: return Number(n, size);
			case FplOp::Var: return Name(n.name, n.primes, size);
			case FplOp::Neg: {
				Box box = Text(std::wstring(1, Minus), size, false);
				Append(box, Node(k[0], size), 0.04 * size);
				return box;
			}
			case FplOp::Add: case FplOp::Sub: {
				Box box = Node(k[0], size);
				Append(box, Text(std::wstring(1, n.op == FplOp::Add ? L'+' : Minus), size, false), 0.2 * size);
				Append(box, Node(k[1], size), 0.2 * size);
				return box;
			}
			case FplOp::Mul: return Product(n, size);
			case FplOp::Div: return Fraction(Node(Strip(k[0]), Smaller(size)), Node(Strip(k[1]), Smaller(size)), size);
			case FplOp::Pow:
				// sin^2 x, with the power on the name
				if (IsClassicCall(m_Ast[k[0]]))
					return Call(m_Ast[k[0]], size, Strip(k[1]));
				return Script(Whole(k[0], size), Node(Strip(k[1]), Script(size)), size, true);
			case FplOp::Fact: {
				Box box = Whole(k[0], size);
				Append(box, Text(L"!", size, false), 0.02 * size);
				return box;
			}
			case FplOp::Call: return Call(n, size, -1);
			case FplOp::Cmp: {
				Box box = Node(k[0], size);
				Append(box, Text(RelText(n.rel), size, false), 0.28 * size);
				Append(box, Node(k[1], size), 0.28 * size);
				return box;
			}
			case FplOp::And: {
				// a < b < c: the shared middle once
				Box box = Node(k[0], size);
				auto& second = m_Ast[k[1]];
				Append(box, Text(RelText(second.rel), size, false), 0.28 * size);
				Append(box, Node(second.kids[1], size), 0.28 * size);
				return box;
			}
			case FplOp::Group: return Wrap(Node(k[0], size), n.name == L"[" ? Delim::Bracket : Delim::Paren, size);
			case FplOp::Tuple: return Wrap(Separated(k, size), Delim::Paren, size);
			case FplOp::List: return Wrap(Separated(k, size), Delim::Bracket, size);
			case FplOp::Sequence: return Separated(k, size, 0.5);
			case FplOp::Piecewise: return Piecewise(n, size);
			case FplOp::Integral: return Integral(n, size);
			case FplOp::Series: return Series(n, size);
			case FplOp::Deriv: {
				Box top = Text(L"d", Smaller(size), false);
				Box bottom = Text(L"d", Smaller(size), false);
				Append(bottom, Name(n.name, 0, Smaller(size)));
				Box box = Fraction(top, bottom, size);
				Append(box, Operand(k[0], size), 0.1 * size);
				return box;
			}
			case FplOp::Named: {
				Box box = Name(n.name, n.primes, size);
				Append(box, Wrap(Separated(k, size), Delim::Paren, size), 0.02 * size);
				return box;
			}
		}
		return Text(L"?", size, false);
	}

private:
	enum class Delim { Paren, Bracket, Bar, Floor, Ceil, Brace };

	static double Smaller(double size) { return std::max(size * FractionScale, MinSize); }
	static double Script(double size) { return std::max(size * ScriptScale, MinSize); }
	static double Stroke(double size) { return std::max(0.055 * size, 0.7); }

	// What a sum, a product, an integral or d/dx applies to. Their own argument's parentheses aren't kept in the tree, and without
	// them "the product of 1 + x/k" would read as 1 added to a product.
	Box Operand(int node, double size) {
		auto op = m_Ast[node].op;
		if (op == FplOp::Add || op == FplOp::Sub)
			return Wrap(Node(node, size), Delim::Paren, size);
		return Node(node, size);
	}

	// What's applied to all of a sum, a product, an integral or d/dx: on its own, a power or a factor after one would look like
	// part of what it's applied to.
	Box Whole(int node, double size) {
		auto op = m_Ast[node].op;
		if (op == FplOp::Series || op == FplOp::Integral || op == FplOp::Deriv)
			return Wrap(Node(node, size), Delim::Paren, size);
		return Node(node, size);
	}

	// what parentheses are only there to group: a fraction and a power show that themselves
	int Strip(int node) const {
		while (m_Ast[node].op == FplOp::Group)
			node = m_Ast[node].kids[0];
		return node;
	}

	Box Text(const std::wstring& text, double size, bool italic) {
		Box box;
		FplMathItem item;
		item.text = text;
		item.italic = italic;
		item.size = size;
		box.w = m_Measure ? m_Measure(text, size, italic) : text.size() * size * 0.5;
		box.asc = Ascent * size;
		box.desc = Descent * size;
		box.items.push_back(std::move(item));
		return box;
	}

	static Box Line(std::vector<FplPoint> points, double size) {
		Box box;
		FplMathItem item;
		item.isLine = true;
		item.points = std::move(points);
		item.stroke = Stroke(size);
		box.items.push_back(std::move(item));
		return box;
	}

	Box Number(const FplNode& n, double size) {
		if (n.name == L"e")
			return Text(L"e", size, true);
		if (!n.name.empty())
			return Text(n.name, size, false);		// as it was typed, or pi
		wchar_t buf[32];
		swprintf_s(buf, L"%g", n.value);
		return Text(buf, size, false);
	}

	// x, y1 (a subscript), alpha (upright), each with its primes
	Box Name(const std::wstring& name, int primes, double size) {
		size_t letters = 0;
		while (letters < name.size() && !(name[letters] >= L'0' && name[letters] <= L'9'))
			letters++;
		std::wstring word = name.substr(0, letters);
		if (const wchar_t* greek = FplGreekSymbol(word))
			word = greek;
		// a capital Greek letter stands upright
		const bool italic = word.size() == 1 && !(word[0] >= 0x391 && word[0] <= 0x3A9);
		Box box = Text(word, size, italic);
		if (letters < name.size())
			box = Script(std::move(box), Text(name.substr(letters), Script(size), false), size, false);
		if (primes > 0)
			Append(box, Text(std::wstring(primes, PrimeSign), size, false));
		return box;
	}

	static std::wstring RelText(FplRel rel) {
		switch (rel) {
			case FplRel::Lt: return L"<";
			case FplRel::Gt: return L">";
			case FplRel::Le: return std::wstring(1, wchar_t(0x2264));
			case FplRel::Ge: return std::wstring(1, wchar_t(0x2265));
			case FplRel::Fit: return std::wstring(1, wchar_t(0x223C));
			default: return L"=";
		}
	}

	Box Separated(const std::vector<int>& kids, double size, double gap = 0.28) {
		Box box;
		for (size_t i = 0; i < kids.size(); i++) {
			if (i > 0) {
				Append(box, Text(L",", size, false), 0.02 * size);
				box.w += gap * size;
			}
			Append(box, Node(kids[i], size));
		}
		return box;
	}

	Box Product(const FplNode& n, double size) {
		Box box = Whole(n.kids[0], size);
		if (!n.implicit) {
			Append(box, Text(std::wstring(1, Dot), size, false), 0.12 * size);
			Append(box, Node(n.kids[1], size), 0.12 * size);
			return box;
		}
		// a little room where letters of a name would otherwise run into what's next to them
		auto& left = m_Ast[n.kids[0]];
		auto& right = m_Ast[n.kids[1]];
		const bool spaced = IsWord(left) || IsWord(right) || left.op == FplOp::Div || right.op == FplOp::Div ||
			left.op == FplOp::Series || left.op == FplOp::Integral;
		Append(box, Node(n.kids[1], size), spaced ? 0.16 * size : 0.03 * size);
		return box;
	}

	// a function's name, or a name of several letters
	bool IsWord(const FplNode& n) const {
		if (n.op == FplOp::Pow)
			return IsWord(m_Ast[n.kids[0]]);
		if (n.op == FplOp::Mul)
			return IsWord(m_Ast[n.kids[1]]);
		return n.op == FplOp::Call || n.op == FplOp::Named || (n.op == FplOp::Var && n.name.size() > 1);
	}

	Box Fraction(const Box& top, const Box& bottom, double size, bool withBar = true) {
		const double gap = 0.11 * size, axis = Axis * size, width = std::max(top.w, bottom.w) + 0.16 * size;
		Box box;
		Place(box, top, (width - top.w) / 2, -axis - gap - top.desc);
		Place(box, bottom, (width - bottom.w) / 2, -axis + gap + bottom.asc);
		if (withBar) {
			Box bar = Line({ { 0, -axis }, { width, -axis } }, size);
			Place(box, bar, 0, 0);
		}
		box.w = width + 0.04 * size;
		return box;
	}

	// a superscript, raised so it clears what's under it, or a subscript
	Box Script(Box base, const Box& script, double size, bool up) {
		Box box = std::move(base);
		const double x = box.w + 0.03 * size;
		const double y = up ? -std::max(0.42 * size, box.asc - 0.5 * script.asc) : 0.24 * size;
		Place(box, script, x, y);
		return box;
	}

	Box Delimiter(Delim kind, bool open, double asc, double desc, double size) {
		const double height = asc + desc;
		const double w = kind == Delim::Bar ? 0 : kind == Delim::Paren ? std::min(0.32 * size, 0.1 * size + 0.1 * height)
			: kind == Delim::Brace ? std::min(0.4 * size, 0.2 * size + 0.05 * height) : 0.24 * size;
		const double inset = (kind == Delim::Bar ? 0.1 : 0.06) * size;
		auto X = [&](double offset) { return open ? inset + offset : inset + w - offset; };
		std::vector<FplPoint> points;
		switch (kind) {
			case Delim::Paren:
				// an arc of an ellipse: flat through the middle, as a sine's point would read as an angle bracket when small
				for (int i = 0; i <= 16; i++) {
					const double t = i / 16.0, u = 2 * t - 1;
					points.push_back({ X(w * (1 - std::sqrt(std::max(0.0, 1 - u * u)))), -asc + height * t });
				}
				break;
			case Delim::Bracket:
				points = { { X(w), -asc }, { X(0), -asc }, { X(0), desc }, { X(w), desc } };
				break;
			case Delim::Bar:
				points = { { X(0), -asc }, { X(0), desc } };
				break;
			case Delim::Floor:
				points = { { X(0), -asc }, { X(0), desc }, { X(w), desc } };
				break;
			case Delim::Ceil:
				points = { { X(0), desc }, { X(0), -asc }, { X(w), -asc } };
				break;
			case Delim::Brace:
				for (int i = 0; i <= 32; i++) {
					const double t = i / 16.0 - 1;
					const double at = 0.5 * w + 0.5 * w * t * t * t * t - 0.5 * w * std::exp(-t * t / 0.004);
					points.push_back({ X(at), -asc + height * i / 32.0 });
				}
				break;
		}
		Box box = Line(std::move(points), size);
		box.w = w + 2 * inset;
		box.asc = asc;
		box.desc = desc;
		return box;
	}

	Box Wrap(const Box& inner, Delim open, double size, Delim close) {
		const double asc = inner.asc + 0.03 * size, desc = inner.desc + 0.03 * size;
		Box box = Delimiter(open, true, asc, desc, size);
		Append(box, inner);
		Append(box, Delimiter(close, false, asc, desc, size));
		return box;
	}

	Box Wrap(const Box& inner, Delim kind, double size) {
		return Wrap(inner, kind, size, kind);
	}

	Box Radical(const Box& inner, double size, const Box* index) {
		const double top = inner.asc + 0.14 * size, bottom = inner.desc + 0.02 * size;
		const double mid = -top + (top + bottom) * 0.58;
		const double start = index ? std::max(index->w - 0.22 * size, 0.0) : 0;
		const double foot = start + 0.26 * size, peak = start + 0.46 * size, inside = peak + 0.06 * size;
		const double end = inside + inner.w + 0.06 * size;
		Box box = Line({ { start, mid + 0.06 * size }, { start + 0.1 * size, mid }, { foot, bottom }, { peak, -top }, { end, -top } }, size);
		box.w = end;
		box.asc = top + Stroke(size);
		box.desc = bottom;
		Place(box, inner, inside, 0);
		if (index)
			Place(box, *index, 0, mid - 0.14 * size);
		box.w = end + 0.04 * size;
		return box;
	}

	// the functions with names that are written before their arguments, sin x rather than gamma(x)
	static bool IsClassic(FplFunc f) {
		return (f >= FplFunc::Sin && f <= FplFunc::Cot) || (f >= FplFunc::Asin && f <= FplFunc::Acot) ||
			(f >= FplFunc::Sinh && f <= FplFunc::Acoth) || f == FplFunc::Ln || f == FplFunc::Log || f == FplFunc::Log2;
	}

	static bool IsClassicCall(const FplNode& n) {
		return n.op == FplOp::Call && IsClassic(n.func) && n.primes == 0 && n.kids.size() == 1;
	}

	// what needs no parentheses after a name: sin x, sin 2x, sin x^2
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

	Box Call(const FplNode& n, double size, int power) {
		auto& k = n.kids;
		switch (n.func) {
			case FplFunc::Sqrt: return Radical(Node(Strip(k[0]), size), size, nullptr);
			case FplFunc::Root: {
				const Box index = Node(Strip(k[0]), Script(Script(size)));
				return Radical(Node(Strip(k[1]), size), size, &index);
			}
			case FplFunc::Abs: return Wrap(Node(Strip(k[0]), size), Delim::Bar, size);
			case FplFunc::Floor: return Wrap(Node(Strip(k[0]), size), Delim::Floor, size);
			case FplFunc::Ceil: return Wrap(Node(Strip(k[0]), size), Delim::Ceil, size);
			case FplFunc::Exp:
				return Script(Text(L"e", size, true), Node(Strip(k[0]), Script(size)), size, true);
			default:
				break;
		}
		// the name, with what's written on it
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
			default: break;
		}
		Box box;
		std::vector<int> args = k;
		const FplFuncSymbol symbol = FplSymbolOf(n.func);
		if (n.func == FplFunc::NCr)
			return Wrap(Fraction(Node(Strip(k[0]), Smaller(size)), Node(Strip(k[1]), Smaller(size)), size, false), Delim::Paren, size);
		if (n.func == FplFunc::Mod) {
			box = Operand(k[0], size);
			Append(box, Text(L"mod", size, false), 0.22 * size);
			Append(box, Operand(k[1], size), 0.22 * size);
			return box;
		}
		if (n.func == FplFunc::BesselJ || n.func == FplFunc::BesselY || n.func == FplFunc::BesselI || n.func == FplFunc::BesselK) {
			box = Script(Text(symbol.text, size, symbol.italic), Node(Strip(args[0]), Script(size)), size, false);
			args.erase(args.begin());
		}
		else if (n.func == FplFunc::LambertWm1)
			box = Script(Text(symbol.text, size, symbol.italic), Text(std::wstring(1, Minus) + L"1", Script(size), false), size, false);
		else if (n.func == FplFunc::Lgamma) {
			box = Text(L"ln", size, false);
			Append(box, Text(symbol.text, size, symbol.italic), 0.14 * size);
		}
		else if (*symbol.text)
			box = Text(symbol.text, size, symbol.italic);
		else if (n.func == FplFunc::Log2) {
			box = Script(Text(L"log", size, false), Text(L"2", Script(size), false), size, false);
		}
		else if (n.func == FplFunc::Log && args.size() == 2) {
			box = Script(Text(L"log", size, false), Node(Strip(args[1]), Script(size)), size, false);
			args.pop_back();
		}
		else
			box = Text(name, size, false);
		if (inverse)
			box = Script(std::move(box), Text(std::wstring(1, Minus) + L"1", Script(size), false), size, true);
		if (power >= 0)
			box = Script(std::move(box), Node(power, Script(size)), size, true);
		if (n.primes > 0)
			Append(box, Text(std::wstring(n.primes, PrimeSign), size, false));
		if (args.size() == 1 && IsClassic(n.func) && IsPlainArgument(args[0])) {
			Append(box, Node(args[0], size), 0.16 * size);
			return box;
		}
		if (args.size() == 1)
			Append(box, Wrap(Node(Strip(args[0]), size), Delim::Paren, size), 0.02 * size);
		else
			Append(box, Wrap(Separated(args, size), Delim::Paren, size), 0.02 * size);
		return box;
	}

	Box Piecewise(const FplNode& n, double size) {
		auto& k = n.kids;
		// {0 < x < 2}: a condition alone, with no value of its own
		if (k.size() == 2 && !n.hasDefault && m_Ast[k[1]].op == FplOp::Num && m_Ast[k[1]].pos == 0 && m_Ast[k[1]].len == 0)
			return Wrap(Node(k[0], size), Delim::Brace, size);
		std::vector<Box> rows;
		for (size_t i = 0; i + 1 < k.size(); i += 2) {
			Box row = Node(k[i + 1], size);
			Append(row, Node(k[i], Smaller(size)), 0.7 * size);
			rows.push_back(std::move(row));
		}
		if (n.hasDefault)
			rows.push_back(Node(k.back(), size));
		const double gap = 0.2 * size;
		double height = gap * (rows.size() - 1);
		for (auto& row : rows)
			height += row.asc + row.desc;
		// around the axis
		double y = -Axis * size - height / 2;
		Box column;
		for (auto& row : rows) {
			Place(column, row, 0, y + row.asc);
			y += row.asc + row.desc + gap;
		}
		const double asc = column.asc + 0.02 * size, desc = column.desc + 0.02 * size;
		Box box = Delimiter(Delim::Brace, true, asc, desc, size);
		Append(box, column, 0.04 * size);
		return box;
	}

	Box Integral(const FplNode& n, double size) {
		auto& k = n.kids;
		// the sign larger, the limits to its side
		Box sign = Text(std::wstring(1, wchar_t(0x222B)), size * 1.5, false);
		const double script = Script(size);
		const Box lo = Node(Strip(k[1]), script), hi = Node(Strip(k[2]), script);
		Box box;
		Place(box, sign, 0, 0.1 * size);
		const double at = sign.w + 0.02 * size;
		Place(box, hi, at, -0.7 * size);
		Place(box, lo, at, 0.42 * size);
		box.w = at + std::max(lo.w, hi.w);
		Append(box, Operand(k[0], size), 0.1 * size);
		Append(box, Text(L"d", size, false), 0.14 * size);
		Append(box, Name(n.name, 0, size));
		return box;
	}

	Box Series(const FplNode& n, double size) {
		auto& k = n.kids;
		Box sign = Text(std::wstring(1, n.product ? wchar_t(0x220F) : wchar_t(0x2211)), size * 1.4, false);
		const double script = Script(size);
		Box lo = Name(n.name, 0, script);
		Append(lo, Text(L"=", script, false), 0.06 * script);
		Append(lo, Node(Strip(k[1]), script), 0.06 * script);
		const Box hi = Node(Strip(k[2]), script);
		const double width = std::max({ sign.w, lo.w, hi.w });
		Box box;
		Place(box, sign, (width - sign.w) / 2, 0.14 * size);
		Place(box, lo, (width - lo.w) / 2, sign.desc + 0.14 * size + lo.asc);
		Place(box, hi, (width - hi.w) / 2, -sign.asc + 0.14 * size - hi.desc);
		box.w = width;
		Append(box, Operand(k[0], size), 0.12 * size);
		return box;
	}

	const FplAst& m_Ast;
	const FplMathMeasure& m_Measure;
};

}  // namespace

FplMathLayout FplLayoutMath(const FplAst& ast, int root, double size, const FplMathMeasure& measure) {
	Layouter layouter(ast, measure);
	Box box = layouter.Node(root, size);
	FplMathLayout layout;
	layout.items = std::move(box.items);
	layout.width = box.w;
	layout.ascent = box.asc;
	layout.descent = box.desc;
	return layout;
}

bool FplFitMath(std::wstring_view text, FplParseOptions options, std::initializer_list<double> sizes, double maxWidth,
	const FplMathMeasure& measure, FplMathLayout& layout) {
	options.display = true;
	options.degrees = false;
	auto parsed = FplParse(text, options);
	if (parsed.error || parsed.root < 0)
		return false;
	for (double size : sizes) {
		FplMathLayout attempt = FplLayoutMath(parsed.ast, parsed.root, size, measure);
		if (maxWidth <= 0 || attempt.width <= maxWidth) {
			layout = std::move(attempt);
			return true;
		}
	}
	return false;
}

std::wstring FplDescribeMath(const FplMathLayout& layout) {
	std::wstring out;
	for (auto& item : layout.items) {
		if (item.isLine)
			out += L'|';
		else
			out += item.text;
	}
	return out;
}

const wchar_t* FplGreekSymbol(std::wstring_view word) {
	static std::wstring letters[std::size(g_Greek)];
	for (size_t i = 0; i < std::size(g_Greek); i++)
		if (word == g_Greek[i].word) {
			if (letters[i].empty())
				letters[i] = std::wstring(1, g_Greek[i].letter);
			return letters[i].c_str();
		}
	return nullptr;
}

const wchar_t* FplGreekWord(wchar_t letter) {
	for (auto& greek : g_Greek)
		if (greek.letter == letter)
			return greek.word;
	return nullptr;
}

FplFuncSymbol FplSymbolOf(FplFunc func) {
	switch (func) {
		case FplFunc::Gamma: case FplFunc::Lgamma: return { L"\x0393", false };
		case FplFunc::Digamma: return { L"\x03C8", true };
		case FplFunc::Beta: return { L"B", false };
		case FplFunc::Zeta: return { L"\x03B6", true };
		case FplFunc::AiryAi: return { L"Ai", false };
		case FplFunc::AiryBi: return { L"Bi", false };
		case FplFunc::BesselJ: return { L"J", true };
		case FplFunc::BesselY: return { L"Y", true };
		case FplFunc::BesselI: return { L"I", true };
		case FplFunc::BesselK: return { L"K", true };
		case FplFunc::LambertW: case FplFunc::LambertWm1: return { L"W", true };
		case FplFunc::EllipK: return { L"K", true };
		case FplFunc::EllipE: return { L"E", true };
		case FplFunc::EllipF: return { L"F", true };
		case FplFunc::Step: return { L"H", true };
		case FplFunc::Normpdf: return { L"\x03C6", true };
		case FplFunc::Normcdf: return { L"\x03A6", false };
		case FplFunc::Sign: return { L"sgn", false };
		case FplFunc::NPr: return { L"P", true };
		default: return {};
	}
}
