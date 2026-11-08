// The graphs: building them with their definitions, sampling, features and tracing.

#include "FunctionPlotGraphs.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <limits>

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

double Dist(FplPoint a, FplPoint b) {
	return std::hypot(b.x - a.x, b.y - a.y);
}

FplPoint Project(FplPoint p, FplPoint a, FplPoint b) {
	double dx = b.x - a.x, dy = b.y - a.y;
	double len2 = dx * dx + dy * dy;
	double u = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0;
	return { a.x + u * dx, a.y + u * dy };
}

// "a = 3" where a is already a name: an equation with nothing to draw, which means a second definition.
bool RedefinesName(const FplParseResult& r, const std::vector<std::wstring>& names, std::wstring& name) {
	if (r.error || r.root < 0)
		return false;
	auto& root = r.ast[r.root];
	if (root.op != FplOp::Cmp || root.rel != FplRel::Eq)
		return false;
	auto& lhs = r.ast[root.kids[0]];
	if (lhs.op != FplOp::Var || FplIsVariable(lhs.name) ||
		std::find(names.begin(), names.end(), lhs.name) == names.end())
		return false;
	if (FplVariablesOf(r.ast, root.kids[1]) != 0)
		return false;
	name = lhs.name;
	return true;
}

} // namespace

int FplPlot::FindGraph(unsigned id) const {
	for (size_t i = 0; i < graphs.size(); i++)
		if (graphs[i].id == id)
			return static_cast<int>(i);
	return -1;
}

int FplPlot::FindParam(std::wstring_view name) const {
	for (size_t i = 0; i < params.size(); i++)
		if (params[i].name == name)
			return static_cast<int>(i);
	return -1;
}

void FplPlot::AddList(const std::wstring& name) {
	ListSlot l;
	l.name = name;
	l.slot = FplFirstParam + static_cast<int>(m_Names.size());
	m_Names.push_back(name);
	// its aggregates right after it
	for (auto a : FplAggregates)
		m_Names.push_back(FplAggregateName(a, name));
	m_ListNames.push_back(name);
	m_Lists.push_back(std::move(l));
}

void FplPlot::Build() {
	m_Names.clear();
	m_Names.reserve(params.size() + graphs.size() + lists.size() * 8);
	for (auto& p : params)
		m_Names.push_back(p.name);
	m_Definitions.clear();
	m_Functions.clear();
	m_Lists.clear();
	m_ListNames.clear();
	m_Regressions.clear();
	for (size_t i = 0; i < lists.size(); i++) {
		if (std::find(m_Names.begin(), m_Names.end(), lists[i].name) != m_Names.end())
			continue;
		AddList(lists[i].name);
		m_Lists.back().app = static_cast<int>(i);
	}
	for (auto& g : graphs) {
		g.expr = {};
		g.definition = false;
		g.listDefinition = false;
		g.items.clear();
		g.listSlots.clear();
		g.fitted.clear();
		g.r2 = NaN;
		g.function = -1;
		g.slot = -1;
		g.dragX = g.dragY = -1;
		g.drags.clear();
		g.geometry = {};
		g.features.clear();
	}

	// the definitions and the functions, found round after round: "c = 2d" is one once d is known, "g(x) = 2f(x)"
	// once f is
	FplParseOptions options;
	options.degrees = degrees;
	options.functions = &m_Functions;
	options.lists = &m_ListNames;
	std::vector<FplError> functionErrors(graphs.size());
	auto taken = [&](const std::wstring& name) {
		if (std::find(m_Names.begin(), m_Names.end(), name) != m_Names.end())
			return true;
		for (auto& f : m_Functions)
			if (f.name == name)
				return true;
		return false;
	};
	// then the regressions (their unknown names are fitted), and the definitions again: one may use a fitted name
	for (int pass = 0; pass < 2; pass++) {
	for (bool found = true; found;) {
		found = false;
		for (size_t i = 0; i < graphs.size(); i++) {
			auto& g = graphs[i];
			if (g.definition || g.listDefinition || g.function >= 0 || g.expr.error.code == FplErrorCode::Redefined ||
				!g.fitted.empty() || g.text.find(L'~') != std::wstring::npos || FplSplitOde(g.text).isOde)
				continue;
			options.parameters = &m_Names;
			auto fd = FplParseFunction(g.text, options);
			if (fd.isDefinition) {
				if (fd.error.code == FplErrorCode::Redefined || taken(fd.function.name)) {
					g.expr.error = fd.error.code == FplErrorCode::Redefined ? fd.error
						: FplError{ FplErrorCode::Redefined, 0, static_cast<int>(fd.function.name.size()), fd.function.name };
					continue;
				}
				functionErrors[i] = fd.error;
				if (fd.error)
					continue;
				g.function = static_cast<int>(m_Functions.size());
				m_Functions.push_back(std::move(fd.function));
				found = true;
				continue;
			}
			auto r = FplParse(g.text, options);
			std::wstring name;
			if (RedefinesName(r, m_Names, name)) {
				g.expr.error = { FplErrorCode::Redefined, r.ast[r.ast[r.root].kids[0]].pos,
					r.ast[r.ast[r.root].kids[0]].len, name };
				continue;
			}
			if (r.error)
				continue;
			auto def = FplClassify(r.ast, r.root);
			if (def.error || def.kind != FplKind::Definition)
				continue;
			if (taken(def.name)) {
				g.expr.error = { FplErrorCode::Redefined, 0, static_cast<int>(def.name.size()), def.name };
				continue;
			}
			if (r.ast[def.expr[0]].op == FplOp::List) {
				// a list: its element's slot and aggregates
				g.listDefinition = true;
				AddList(def.name);
				m_Lists.back().graph = static_cast<int>(i);
				g.slot = m_Lists.back().slot;
				found = true;
				continue;
			}
			g.definition = true;
			g.slot = FplFirstParam + static_cast<int>(m_Names.size());
			m_Names.push_back(def.name);
			m_Definitions.push_back(static_cast<int>(i));
			found = true;
		}
	}
	if (pass > 0)
		break;
	bool fits = false;
	for (auto& g : graphs)
		if (g.text.find(L'~') != std::wstring::npos && g.fitted.empty() && g.function < 0 &&
			g.expr.error.code != FplErrorCode::Redefined)
			fits = TryRegression(g, options) || fits;
	if (!fits)
		break;
	}

	for (size_t i = 0; i < graphs.size(); i++) {
		auto& g = graphs[i];
		if (g.expr.error.code == FplErrorCode::Redefined)
			continue;
		if (g.function >= 0) {
			// one parameter that is a variable: drawn as what that variable makes it; else nothing to draw
			auto& fn = m_Functions[g.function];
			const std::wstring& p = fn.params.size() == 1 ? fn.params[0] : std::wstring();
			std::wstring call = fn.name + L"(" + p + L")";
			std::wstring text = p == L"x" ? call : p == L"y" ? L"x = " + call : p == L"t" ? call
				: p == FplTheta ? L"r = " + call : std::wstring();
			if (!text.empty()) {
				g.expr = FplBuild(text, m_Names, degrees, &m_Functions, &m_ListNames);
			}
			else {
				g.expr.kind = FplKind::Definition;
				g.expr.name = fn.name;
			}
			continue;
		}
		if (functionErrors[i]) {
			g.expr.error = functionErrors[i];
			continue;
		}
		if (g.listDefinition) {
			// its items, each against every name but its own
			std::wstring own = m_Names[g.slot - FplFirstParam];
			std::vector<std::wstring> others;
			for (auto& n : m_Names)
				if (n != own)
					others.push_back(n);
			options.parameters = &others;
			auto r = FplParse(g.text, options);
			auto def = FplClassify(r.ast, r.root);
			g.expr.kind = FplKind::Definition;
			g.expr.name = own;
			g.expr.error = r.error ? r.error : def.error;
			if (!g.expr.error)
				for (int item : r.ast[def.expr[0]].kids) {
					g.items.push_back(FplCompile(r.ast, item, m_Names, &g.expr.error));
					if (g.expr.error)
						break;
				}
			continue;
		}
		if (!g.definition) {
			if (!g.fitted.empty()) {
				// a regression: built against every name, its fitted ones too
				auto fittedSlots = g.fitted;
				g.expr = FplBuild(g.text, m_Names, degrees, &m_Functions, &m_ListNames);
				g.fitted = fittedSlots;
				if (!g.expr.error && g.expr.kind != FplKind::Regression)
					g.expr.error = { FplErrorCode::NotAPlot, 0, static_cast<int>(g.text.size()) };
				if (!g.expr.error)
					m_Regressions.push_back(static_cast<int>(i));
			}
			else if (g.text.find(L'~') != std::wstring::npos && g.expr.error) {
				// a regression that couldn't be one: its error stays
				continue;
			}
			else {
				g.expr = FplBuild(g.text, m_Names, degrees, &m_Functions, &m_ListNames);
				if (!g.expr.error && (g.expr.kind == FplKind::Point || g.expr.kind == FplKind::SlopeField ||
					g.expr.kind == FplKind::SecondOrder || g.expr.kind == FplKind::OdeSystem))
					FindDragged(g, options);
				// a regression with nothing to fit
				if (!g.expr.error && g.expr.kind == FplKind::Regression)
					g.expr.error = { FplErrorCode::NotAPlot, 0, static_cast<int>(g.text.size()) };
			}
			// the lists it reads
			if (!g.expr.error)
				for (auto& l : m_Lists)
					if (g.expr.f.Reads(l.slot) || g.expr.g.Reads(l.slot) || g.expr.h.Reads(l.slot) ||
						g.expr.value.Reads(l.slot) || std::any_of(g.expr.starts.begin(), g.expr.starts.end(),
							[&](const FplProgram& p) { return p.Reads(l.slot); }))
						g.listSlots.push_back(l.slot);
			if (!g.listSlots.empty()) {
				g.dragX = g.dragY = -1;
				g.drags.clear();
			}
			continue;
		}
		// its value, compiled against every name but its own
		std::wstring own = m_Names[g.slot - FplFirstParam];
		std::vector<std::wstring> others;
		others.reserve(m_Names.size());
		for (auto& n : m_Names)
			if (n != own)
				others.push_back(n);
		options.parameters = &others;
		auto r = FplParse(g.text, options);
		auto def = FplClassify(r.ast, r.root);
		g.expr.kind = FplKind::Definition;
		g.expr.name = own;
		g.expr.error = r.error ? r.error : def.error;
		if (!g.expr.error)
			g.expr.f = FplCompile(r.ast, def.expr[0], m_Names, &g.expr.error);
		// one number: a list in it needs an aggregate
		if (!g.expr.error)
			for (auto& l : m_Lists)
				if (g.expr.f.Reads(l.slot)) {
					g.expr.error = { FplErrorCode::ListNotAllowed, 0, static_cast<int>(g.text.size()), l.name };
					g.expr.f = {};
					break;
				}
	}
	UpdateValues();
}

bool FplPlot::TryRegression(FplGraph& g, FplParseOptions options) {
	// the unknown names are the fit's: found one by one
	std::vector<std::wstring> names = m_Names, fitNames;
	options.parameters = &names;
	for (int tries = 0; tries < 8; tries++) {
		auto r = FplParse(g.text, options);
		if (r.error.code == FplErrorCode::UnknownName && !r.error.name.empty()) {
			fitNames.push_back(r.error.name);
			names.push_back(r.error.name);
			continue;
		}
		if (r.error) {
			g.expr.error = r.error;
			return false;
		}
		auto def = FplClassify(r.ast, r.root);
		if (def.error || def.kind != FplKind::Regression) {
			g.expr.error = def.error ? def.error : FplError{ FplErrorCode::NotAPlot, 0, static_cast<int>(g.text.size()) };
			return false;
		}
		if (fitNames.empty()) {
			g.expr.error = { FplErrorCode::NotAPlot, 0, static_cast<int>(g.text.size()) };
			return false;
		}
		std::vector<double> start = g.fit;
		g.fit.assign(fitNames.size(), 1.0);
		for (size_t k = 0; k < fitNames.size() && k < start.size(); k++)
			if (std::isfinite(start[k]))
				g.fit[k] = start[k];
		for (auto& n : fitNames) {
			g.fitted.push_back(FplFirstParam + static_cast<int>(m_Names.size()));
			m_Names.push_back(n);
		}
		return true;
	}
	return false;
}

void FplPlot::SetElement(std::vector<double>& slots, const FplGraph& g, size_t i) const {
	for (int slot : g.listSlots)
		for (auto& l : m_Lists)
			if (l.slot == slot)
				slots[slot] = i < l.values.size() ? l.values[i] : NaN;
}

size_t FplPlot::ElementCount(int graph) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return 0;
	auto& g = graphs[graph];
	if (g.listSlots.empty())
		return 0;
	size_t n = std::numeric_limits<size_t>::max();
	for (int slot : g.listSlots)
		for (auto& l : m_Lists)
			if (l.slot == slot)
				n = std::min(n, l.values.size());
	return n == std::numeric_limits<size_t>::max() ? 0 : n;
}

void FplPlot::Fit(FplGraph& g) {
	const size_t n = ElementCount(static_cast<int>(&g - graphs.data()));
	const size_t k = g.fitted.size();
	g.r2 = NaN;
	if (n == 0 || k == 0 || g.expr.f.Empty())
		return;
	std::vector<double> slots = m_Slots;
	std::vector<double> p = g.fit;
	p.resize(k, 1.0);
	auto residuals = [&](const std::vector<double>& q, std::vector<double>& r) {
		for (size_t j = 0; j < k; j++)
			slots[g.fitted[j]] = q[j];
		double cost = 0;
		r.resize(n);
		for (size_t i = 0; i < n; i++) {
			SetElement(slots, g, i);
			r[i] = g.expr.f.Eval(slots.data());
			cost += r[i] * r[i];
		}
		return cost;
	};
	std::vector<double> r, rTry;
	double cost = residuals(p, r);
	double lambda = 1e-3;
	std::vector<double> J(n * k), A(k * k), grad(k), delta(k), pTry(k);
	for (int iter = 0; iter < 200 && std::isfinite(cost); iter++) {
		// the Jacobian by forward differences
		for (size_t j = 0; j < k; j++) {
			double h = 1e-7 * std::max(1.0, std::abs(p[j]));
			pTry = p;
			pTry[j] += h;
			residuals(pTry, rTry);
			for (size_t i = 0; i < n; i++)
				J[i * k + j] = (rTry[i] - r[i]) / h;
		}
		for (size_t a = 0; a < k; a++) {
			grad[a] = 0;
			for (size_t i = 0; i < n; i++)
				grad[a] += J[i * k + a] * r[i];
			for (size_t b = 0; b < k; b++) {
				double sum = 0;
				for (size_t i = 0; i < n; i++)
					sum += J[i * k + a] * J[i * k + b];
				A[a * k + b] = sum;
			}
		}
		bool improved = false;
		while (lambda < 1e14) {
			// (A + lambda diag A) delta = -grad, by elimination with partial pivoting
			std::vector<double> M(k * (k + 1));
			for (size_t a = 0; a < k; a++) {
				for (size_t b = 0; b < k; b++)
					M[a * (k + 1) + b] = A[a * k + b] + (a == b ? lambda * (A[a * k + a] + 1e-12) : 0);
				M[a * (k + 1) + k] = -grad[a];
			}
			bool singular = false;
			for (size_t c = 0; c < k && !singular; c++) {
				size_t piv = c;
				for (size_t a = c + 1; a < k; a++)
					if (std::abs(M[a * (k + 1) + c]) > std::abs(M[piv * (k + 1) + c]))
						piv = a;
				if (std::abs(M[piv * (k + 1) + c]) < 1e-300) {
					singular = true;
					break;
				}
				for (size_t b = 0; b <= k; b++)
					std::swap(M[c * (k + 1) + b], M[piv * (k + 1) + b]);
				for (size_t a = 0; a < k; a++) {
					if (a == c)
						continue;
					double f = M[a * (k + 1) + c] / M[c * (k + 1) + c];
					for (size_t b = c; b <= k; b++)
						M[a * (k + 1) + b] -= f * M[c * (k + 1) + b];
				}
			}
			if (singular) {
				lambda *= 10;
				continue;
			}
			for (size_t a = 0; a < k; a++) {
				delta[a] = M[a * (k + 1) + k] / M[a * (k + 1) + a];
				pTry[a] = p[a] + delta[a];
			}
			double tryCost = residuals(pTry, rTry);
			if (std::isfinite(tryCost) && tryCost <= cost) {
				p = pTry;
				r = rTry;
				// stop once a step gains next to nothing
				bool negligible = std::abs(cost - tryCost) <= 1e-15 * std::max(cost, 1e-300);
				cost = tryCost;
				lambda = std::max(lambda / 10, 1e-12);
				improved = !negligible;
				break;
			}
			lambda *= 10;
		}
		if (!improved)
			break;
	}
	g.fit = p;
	for (size_t j = 0; j < k; j++)
		m_Slots[g.fitted[j]] = p[j];
	// R squared, from the lhs's values
	if (!g.expr.g.Empty()) {
		for (size_t j = 0; j < k; j++)
			slots[g.fitted[j]] = p[j];
		double mean = 0, total = 0, residual = 0;
		std::vector<double> ys(n);
		for (size_t i = 0; i < n; i++) {
			SetElement(slots, g, i);
			ys[i] = g.expr.g.Eval(slots.data());
			mean += ys[i] / n;
		}
		for (size_t i = 0; i < n; i++) {
			total += (ys[i] - mean) * (ys[i] - mean);
			residual += r[i] * r[i];
		}
		g.r2 = total > 0 ? 1 - residual / total : NaN;
	}
}

void FplPlot::FindDragged(FplGraph& g, FplParseOptions options) {
	// a coordinate that is an app's parameter alone: (a, b), (a, 2), (a, f(a)); an ODE's y(a) = b
	options.parameters = &m_Names;
	auto paramOf = [&](std::wstring_view text) {
		auto part = FplParse(text, options);
		if (part.error)
			return -1;
		auto& n = part.ast[part.root];
		return n.op == FplOp::Var ? FindParam(n.name) : -1;
	};
	if (auto ode = FplSplitOde(g.text); ode.isOde) {
		// each condition's point
		if (ode.errorAt == std::wstring_view::npos) {
			std::wstring_view text = g.text;
			g.drags.reserve(ode.starts.size());
			for (auto& st : ode.starts)
				g.drags.push_back({ paramOf(text.substr(st.a, st.aLen)), paramOf(text.substr(st.b, st.bLen)) });
			if (!g.drags.empty()) {
				g.dragX = g.drags[0].first;
				g.dragY = g.drags[0].second;
			}
		}
		return;
	}
	auto r = FplParse(g.text, options);
	if (r.error)
		return;
	auto def = FplClassify(r.ast, r.root);
	if (def.error || def.kind != FplKind::Point)
		return;
	auto param = [&](int node) {
		auto& n = r.ast[node];
		return n.op == FplOp::Var ? FindParam(n.name) : -1;
	};
	g.dragX = param(def.expr[0]);
	g.dragY = param(def.expr[1]);
}

int FplPlot::DraggableAt(FplPoint screen, double maxDistance, int* point) const {
	int best = -1;
	double bestD = maxDistance;
	for (size_t i = 0; i < graphs.size(); i++) {
		auto& g = graphs[i];
		if (!g.Drawable() || (g.flags & FplGraphNoDrag))
			continue;
		for (size_t k = 0; k < g.geometry.points.size(); k++) {
			const auto [px, py] = g.DragOf(k);
			if (px < 0 && py < 0)
				continue;
			auto& p = g.geometry.points[k];
			double d = std::hypot(p.x - screen.x, p.y - screen.y);
			if (d <= bestD) {
				bestD = d;
				best = static_cast<int>(i);
				if (point)
					*point = static_cast<int>(k);
			}
		}
	}
	return best;
}

void FplPlot::UpdateValues() {
	m_Slots.assign(FplFirstParam + m_Names.size(), 0.0);
	for (size_t i = 0; i < params.size() && FplFirstParam + i < m_Slots.size(); i++)
		m_Slots[FplFirstParam + i] = params[i].value;
	for (int d : m_Definitions)
		m_Slots[graphs[d].slot] = NaN;
	for (int rg : m_Regressions)
		for (size_t j = 0; j < graphs[rg].fitted.size(); j++)
			m_Slots[graphs[rg].fitted[j]] = j < graphs[rg].fit.size() ? graphs[rg].fit[j] : 1.0;
	// in order, round after round, so a definition may use one after it (a cycle stays NaN): the lists, their
	// aggregates, the definitions, the fits
	const size_t rounds = std::max<size_t>(1, m_Definitions.size() + (m_Lists.empty() ? 0 : 1) + m_Regressions.size());
	for (size_t round = 0; round < rounds; round++) {
		for (auto& l : m_Lists) {
			if (l.app >= 0 && l.app < static_cast<int>(lists.size()))
				l.values = lists[l.app].values;
			else if (l.graph >= 0) {
				auto& g = graphs[l.graph];
				l.values.resize(g.items.size());
				for (size_t i = 0; i < g.items.size(); i++)
					l.values[i] = g.expr.error ? NaN : g.items[i].Eval(m_Slots.data());
			}
			// mean, median, stdev, sum, count, min, max
			std::vector<double> v;
			for (double x : l.values)
				if (std::isfinite(x))
					v.push_back(x);
			double sum = 0;
			for (double x : v)
				sum += x;
			const double count = static_cast<double>(v.size());
			const double mean = v.empty() ? NaN : sum / count;
			double var = 0;
			for (double x : v)
				var += (x - mean) * (x - mean);
			std::vector<double> sorted = v;
			std::sort(sorted.begin(), sorted.end());
			double median = sorted.empty() ? NaN : sorted.size() % 2 ? sorted[sorted.size() / 2]
				: (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2;
			const double values[] = { mean, median, v.size() > 1 ? std::sqrt(var / (count - 1)) : NaN, sum, count,
				sorted.empty() ? NaN : sorted.front(), sorted.empty() ? NaN : sorted.back() };
			for (size_t a = 0; a < std::size(values); a++)
				m_Slots[l.slot + 1 + a] = values[a];
			m_Slots[l.slot] = l.values.empty() ? NaN : l.values[0];
		}
		for (int d : m_Definitions) {
			auto& g = graphs[d];
			m_Slots[g.slot] = g.expr.error || g.expr.f.Empty() ? NaN : g.expr.f.Eval(m_Slots.data());
		}
		for (int rg : m_Regressions)
			Fit(graphs[rg]);
	}
}

double FplPlot::ValueOf(std::wstring_view name) const {
	for (size_t i = 0; i < m_Names.size(); i++)
		if (m_Names[i] == name && FplFirstParam + i < m_Slots.size())
			return m_Slots[FplFirstParam + i];
	return NaN;
}

const std::vector<double>* FplPlot::ListValues(std::wstring_view name) const {
	for (auto& l : m_Lists)
		if (l.name == name)
			return &l.values;
	return nullptr;
}

std::vector<std::pair<std::wstring, double>> FplPlot::Fitted(int graph) const {
	std::vector<std::pair<std::wstring, double>> out;
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return out;
	for (int slot : graphs[graph].fitted)
		out.emplace_back(m_Names[slot - FplFirstParam], slot < static_cast<int>(m_Slots.size()) ? m_Slots[slot] : NaN);
	return out;
}

double FplPlot::DefinitionValue(int graph) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()) || !graphs[graph].definition ||
		graphs[graph].slot >= static_cast<int>(m_Slots.size()))
		return NaN;
	return m_Slots[graphs[graph].slot];
}

double FplPlot::IntegralValue(int graph) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return NaN;
	auto& g = graphs[graph];
	if (g.expr.error || g.expr.kind != FplKind::Integral || g.expr.value.Empty())
		return NaN;
	return g.expr.value.Eval(m_Slots.data());
}

void FplPlot::Sample(const FplViewport& view, const FplSampleOptions& options) {
	for (size_t i = 0; i < graphs.size(); i++)
		SampleGraph(static_cast<int>(i), view, options);
}

FplSampleOptions FplPlot::OptionsFor(int graph, const FplSampleOptions& base) const {
	auto& g = graphs[graph];
	FplSampleOptions o = base;
	o.heatmap = (g.flags & FplGraphHeatmap) != 0;
	o.contours = !(g.flags & FplGraphNoContours);
	o.singularities = !(g.flags & FplGraphNoSingular);
	o.odeRange = g.hasRange;
	if (g.hasRange) {
		o.tMin = o.thetaMin = g.tMin;
		o.tMax = o.thetaMax = g.tMax;
	}
	return o;
}

bool FplPlot::IsHeavy(int graph, bool in3D) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return false;
	auto& g = graphs[graph];
	if (!g.Drawable() || g.Listed())
		return false;
	return in3D ? g.expr.kind == FplKind::ImplicitSurface
		: g.expr.kind == FplKind::Implicit || g.expr.kind == FplKind::Surface;
}

void FplPlot::SampleGraph(int graph, const FplViewport& view, const FplSampleOptions& options) {
	auto& g = graphs[graph];
	if (!g.Drawable()) {
		g.geometry = {};
		return;
	}
	FplSampleOptions o = OptionsFor(graph, options);
	if (!g.Listed()) {
		g.geometry = FplSample(g.expr, m_Slots, view, o);
		return;
	}
	// once per element: points for every one, curves for the first 200
	size_t n = ElementCount(graph);
	const bool points = g.expr.kind == FplKind::Point;
	n = std::min(n, points ? size_t(100000) : size_t(200));
	FplGeometry all;
	std::vector<double> slots = m_Slots;
	for (size_t i = 0; i < n; i++) {
		SetElement(slots, g, i);
		FplGeometry one = FplSample(g.expr, slots, view, o);
		all.lines.insert(all.lines.end(), one.lines.begin(), one.lines.end());
		all.fills.insert(all.fills.end(), one.fills.begin(), one.fills.end());
		all.points.insert(all.points.end(), one.points.begin(), one.points.end());
		all.contours.insert(all.contours.end(), one.contours.begin(), one.contours.end());
		all.arrows.insert(all.arrows.end(), one.arrows.begin(), one.arrows.end());
		all.marks.insert(all.marks.end(), one.marks.begin(), one.marks.end());
		all.dashed = one.dashed;
		all.evals += one.evals;
	}
	g.geometry = std::move(all);
}

void FplPlot::Build3D(const FplBox3& box) {
	for (size_t i = 0; i < graphs.size(); i++)
		Build3DGraph(static_cast<int>(i), box);
}

void FplPlot::Build3DGraph(int graph, const FplBox3& box, int implicitCells) {
	auto& g = graphs[graph];
	g.mesh = {};
	if (!g.Drawable() || !box.Valid())
		return;
	if (!g.Listed()) {
		g.mesh = Mesh3D(g, box, implicitCells);
		return;
	}
	// once per element (the first 200), the meshes together
	const size_t n = std::min<size_t>(ElementCount(graph), 200);
	std::vector<double> saved = m_Slots;
	for (size_t i = 0; i < n; i++) {
		SetElement(m_Slots, g, i);
		FplAppend(g.mesh, Mesh3D(g, box, implicitCells));
	}
	m_Slots = std::move(saved);
}

FplLineGrid FplPlot::LineGridOf(int graph, const FplBox3& box) const {
	auto& g = graphs[graph];
	const bool mesh = (g.flags & FplGraphMeshLines) != 0, levels = (g.flags & FplGraphLevelLines) != 0;
	switch (g.expr.kind) {
		case FplKind::Surface:
			return FplMakeLineGrid(box, mesh, mesh, false, levels);
		case FplKind::ParametricSurface:
			// 16 lines each way, on its 64 cells
			return FplMakeLineGrid(box, false, false, false, levels, mesh ? 4.0f : 0.0f);
		case FplKind::ImplicitSurface:
			return FplMakeLineGrid(box, mesh, mesh, mesh, levels);
		default:
			return {};
	}
}

FplMesh FplPlot::Mesh3D(const FplGraph& g, const FplBox3& box, int implicitCells) {
	constexpr double TwoPi = 6.283185307179586;
	double* s = m_Slots.data();
	FplMesh mesh;
	auto& e = g.expr;
	const double t0 = g.hasRange ? g.tMin : 0, t1 = g.hasRange ? g.tMax : TwoPi;
	const float radius = 0.005f * g.width, dot = 0.01f + 0.005f * g.width;
	auto tubes = [&](const std::function<FplPoint3(double)>& f, double a, double b) {
		mesh = FplTubes(FplSampleCurve3(f, box, a, b, 1024), radius);
	};
	auto sphere = [&](FplPoint3 p) {
		FplVec3 c = box.ToBox(p);
		if (std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && std::abs(c.x) <= 1 &&
			std::abs(c.y) <= 1 && std::abs(c.z) <= 1)
			mesh = FplSphere(c, dot);
	};
	switch (e.kind) {
		case FplKind::Surface:
			mesh = FplSurfaceMesh([&](double x, double y) {
				s[FplSlotX] = x;
				s[FplSlotY] = y;
				return e.f.Eval(s);
			}, box, 96);
			break;
		case FplKind::ParametricSurface:
			mesh = FplParametricMesh([&](double u, double v) {
				s[FplSlotU] = u;
				s[FplSlotV] = v;
				return FplPoint3{ e.f.Eval(s), e.g.Eval(s), e.h.Eval(s) };
			}, box, t0, t1, t0, t1, 64);
			break;
		case FplKind::SpaceCurve:
			tubes([&](double t) {
				s[FplSlotT] = t;
				return FplPoint3{ e.f.Eval(s), e.g.Eval(s), e.h.Eval(s) };
			}, t0, t1);
			break;
		case FplKind::Point3D:
			sphere({ e.f.Eval(s), e.g.Eval(s), e.h.Eval(s) });
			break;
		case FplKind::OdeSystem3D: {
			std::function<FplPoint3(double, double, double, double)> field = [&](double t, double x, double y, double z) {
				s[FplSlotT] = t;
				s[FplSlotX] = x;
				s[FplSlotY] = y;
				s[FplSlotZ] = z;
				return FplPoint3{ e.f.Eval(s), e.g.Eval(s), e.h.Eval(s) };
			};
			const bool autonomous = !e.f.Reads(FplSlotT) && !e.g.Reads(FplSlotT) && !e.h.Reads(FplSlotT);
			const FplSampleOptions options = OptionsFor(static_cast<int>(&g - graphs.data()), {});
			for (size_t k = 0; k + 2 < e.starts.size(); k += 3) {
				const FplPoint3 p0{ e.starts[k].Eval(s), e.starts[k + 1].Eval(s), e.starts[k + 2].Eval(s) };
				auto line = FplSolveSystem3(field, autonomous, p0, box, options);
				if (!line.empty())
					FplAppend(mesh, FplTubes({ std::move(line) }, radius));
				// a dot where it starts, in the box
				FplVec3 c = box.ToBox(p0);
				if (std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && std::abs(c.x) <= 1 &&
					std::abs(c.y) <= 1 && std::abs(c.z) <= 1)
					FplAppend(mesh, FplSphere(c, dot));
			}
			break;
		}
		case FplKind::ImplicitSurface:
			mesh = FplImplicitMesh([&](double x, double y, double z) {
				s[FplSlotX] = x;
				s[FplSlotY] = y;
				s[FplSlotZ] = z;
				return e.f.Eval(s);
			}, box, implicitCells);
			break;
		// the 2D kinds that are curves or points, on the z = 0 plane
		case FplKind::Function:
			tubes([&](double x) {
				s[FplSlotX] = x;
				return FplPoint3{ x, e.f.Eval(s), 0 };
			}, box.xMin, box.xMax);
			break;
		case FplKind::FunctionOfY:
			tubes([&](double y) {
				s[FplSlotY] = y;
				return FplPoint3{ e.f.Eval(s), y, 0 };
			}, box.yMin, box.yMax);
			break;
		case FplKind::Polar:
			tubes([&](double theta) {
				s[FplSlotTheta] = theta;
				double r = e.f.Eval(s);
				return FplPoint3{ r * std::cos(theta), r * std::sin(theta), 0 };
			}, t0, t1);
			break;
		case FplKind::Parametric:
			tubes([&](double t) {
				s[FplSlotT] = t;
				return FplPoint3{ e.f.Eval(s), e.g.Eval(s), 0 };
			}, t0, t1);
			break;
		case FplKind::Point:
			sphere({ e.f.Eval(s), e.g.Eval(s), 0 });
			break;
		default:
			break;
	}
	return mesh;
}

double FplPlot::Eval(int graph, double v) const {
	auto& g = graphs[graph];
	if (g.expr.error || (g.expr.kind != FplKind::Function && g.expr.kind != FplKind::FunctionOfY))
		return NaN;
	auto& slots = const_cast<std::vector<double>&>(m_Slots);
	slots[g.expr.kind == FplKind::Function ? FplSlotX : FplSlotY] = v;
	return g.expr.f.Eval(slots.data());
}

void FplPlot::FindFeatures(int graph, const FplViewport& view) {
	auto& g = graphs[graph];
	g.features.clear();
	if (!g.Drawable() || (g.flags & FplGraphNoFeatures) || !view.Valid() || g.Listed())
		return;
	FindCurveMeetings(graph, view);
	if (g.expr.kind != FplKind::Function) {
		SortFeatures(g);
		return;
	}
	std::vector<FplFeature> meetings = std::move(g.features);
	std::function<double(double)> f = [this, graph](double x) { return Eval(graph, x); };
	std::function<double(double)> df;
	if (!g.expr.df.Empty()) {
		df = [this, graph](double x) {
			auto& slots = const_cast<std::vector<double>&>(m_Slots);
			slots[FplSlotX] = x;
			return graphs[graph].expr.df.Eval(slots.data());
		};
	}
	int samples = std::max(50, static_cast<int>(view.width / 3));
	double yTolerance = (view.yMax - view.yMin) * 1e-9;
	double a = view.xMin, b = view.xMax;
	if (view.logX) {
		// in the logarithm of x, so the samples are even on the screen (d/du f(10^u) has f''s sign)
		a = std::log10(a), b = std::log10(b);
		auto fx = f;
		f = [fx](double u) { return fx(std::pow(10.0, u)); };
		if (df) {
			auto dfx = df;
			df = [dfx](double u) {
				double x = std::pow(10.0, u);
				return dfx(x) * x * 2.302585092994046;
			};
		}
	}
	g.features = FplFindFeatures(f, df, a, b, samples, yTolerance);
	g.features.insert(g.features.end(), meetings.begin(), meetings.end());
	for (size_t h = 0; h < graphs.size(); h++) {
		auto& other = graphs[h];
		if (static_cast<int>(h) == graph || !other.Drawable() || other.expr.kind != FplKind::Function || other.Listed())
			continue;
		std::function<double(double)> fo = [this, h](double x) { return Eval(static_cast<int>(h), x); };
		if (view.logX)
			fo = [this, h](double u) { return Eval(static_cast<int>(h), std::pow(10.0, u)); };
		auto meets = FplFindIntersections(f, fo, a, b, samples, yTolerance);
		g.features.insert(g.features.end(), meets.begin(), meets.end());
	}
	if (view.logX)
		for (auto& feature : g.features)
			feature.x = std::pow(10.0, feature.x);
	// what a log y axis can't show (roots, where y is 0)
	if (view.logY)
		g.features.erase(std::remove_if(g.features.begin(), g.features.end(),
			[](const FplFeature& feature) { return !(feature.y > 0); }), g.features.end());
	SortFeatures(g);
}

void FplPlot::SortFeatures(FplGraph& g) {
	std::stable_sort(g.features.begin(), g.features.end(), [](const FplFeature& a, const FplFeature& b) {
		return a.x != b.x ? a.x < b.x : a.y < b.y;
	});
	if (g.features.size() > 1000)
		g.features.resize(1000);
}

// Where the graph's curve meets the other graphs', except that two y = f(x) meet by their formulas (FindFeatures).
void FplPlot::FindCurveMeetings(int graph, const FplViewport& view) {
	auto hasCurve = [](const FplGraph& g) {
		if (!g.Drawable() || g.Listed() || g.geometry.lines.empty())
			return false;
		switch (g.expr.kind) {
			case FplKind::Function: case FplKind::FunctionOfY: case FplKind::Polar: case FplKind::Parametric:
			case FplKind::Implicit: case FplKind::SlopeField: case FplKind::Between: case FplKind::SecondOrder:
			case FplKind::OdeSystem:
				return true;
			default:
				return false;
		}
	};
	auto& g = graphs[graph];
	if (!hasCurve(g))
		return;
	std::vector<double> slots = m_Slots;
	double* s = slots.data();
	auto curveOf = [&](const FplGraph& c) {
		FplCurve curve;
		curve.lines = &c.geometry.lines;
		const FplExpression* e = &c.expr;
		switch (c.expr.kind) {
			case FplKind::Function:
				curve.G = [s, e](double x, double y) {
					s[FplSlotX] = x;
					return y - e->f.Eval(s);
				};
				break;
			case FplKind::FunctionOfY:
				curve.G = [s, e](double x, double y) {
					s[FplSlotY] = y;
					return x - e->f.Eval(s);
				};
				break;
			case FplKind::Implicit:
				curve.G = [s, e](double x, double y) {
					s[FplSlotX] = x;
					s[FplSlotY] = y;
					return e->f.Eval(s);
				};
				break;
			case FplKind::Parametric:
				curve.C = [s, e](double t) {
					s[FplSlotT] = t;
					return FplPoint{ e->f.Eval(s), e->g.Eval(s) };
				};
				break;
			case FplKind::Polar:
				curve.C = [s, e](double theta) {
					s[FplSlotTheta] = theta;
					const double r = e->f.Eval(s);
					return FplPoint{ r * std::cos(theta), r * std::sin(theta) };
				};
				break;
			default:
				break;		// an ODE's solutions, the sides of what's between two curves: the lines alone
		}
		const FplSampleOptions o = OptionsFor(static_cast<int>(&c - graphs.data()), {});
		curve.t0 = c.expr.kind == FplKind::Polar ? o.thetaMin : o.tMin;
		curve.t1 = c.expr.kind == FplKind::Polar ? o.thetaMax : o.tMax;
		return curve;
	};
	const FplCurve mine = curveOf(g);
	for (size_t h = 0; h < graphs.size(); h++) {
		auto& other = graphs[h];
		if (static_cast<int>(h) == graph || !hasCurve(other) ||
			(g.expr.kind == FplKind::Function && other.expr.kind == FplKind::Function))
			continue;
		auto meets = FplFindCurveIntersections(mine, curveOf(other), view);
		g.features.insert(g.features.end(), meets.begin(), meets.end());
	}
}

FplTraceHit FplPlot::OnGraph(int graph, FplPoint screen, const FplViewport& view) const {
	FplTraceHit hit;
	auto& g = graphs[graph];
	double best = std::numeric_limits<double>::infinity();
	FplPoint nearest;
	for (auto& p : g.geometry.points) {
		double d = Dist(p, screen);
		if (d < best) {
			best = d;
			nearest = p;
		}
	}
	for (auto& line : g.geometry.lines) {
		for (size_t i = 0; i + 1 < line.size(); i++) {
			FplPoint p = Project(screen, line[i], line[i + 1]);
			double d = Dist(p, screen);
			if (d < best) {
				best = d;
				nearest = p;
			}
		}
	}
	if (!std::isfinite(best))
		return hit;
	hit.graph = graph;
	hit.distance = best;
	hit.screen = nearest;
	hit.plane = view.ToPlane(nearest);
	// a function's point exactly on it (not a listed one's: which element's?)
	if ((g.expr.kind == FplKind::Function || g.expr.kind == FplKind::FunctionOfY) && !g.Listed()) {
		bool ofX = g.expr.kind == FplKind::Function;
		double v = ofX ? hit.plane.x : hit.plane.y;
		double w = Eval(graph, v);
		if (std::isfinite(w)) {
			hit.plane = ofX ? FplPoint{ v, w } : FplPoint{ w, v };
			hit.screen = view.ToScreen(hit.plane);
		}
	}
	else if (g.expr.kind == FplKind::Point && !g.geometry.points.empty() && !g.Listed()) {
		auto& slots = const_cast<std::vector<double>&>(m_Slots);
		hit.plane = { g.expr.f.Eval(slots.data()), g.expr.g.Eval(slots.data()) };
	}
	return hit;
}

void FplPlot::SnapToFeature(FplTraceHit& hit, FplPoint screen, const FplViewport& view, double snapDistance,
	bool alongOnly) const {
	auto& g = graphs[hit.graph];
	bool ofX = g.expr.kind == FplKind::Function;
	double best = snapDistance;
	for (size_t i = 0; i < g.features.size(); i++) {
		auto& f = g.features[i];
		FplPoint s = view.ToScreen({ f.x, f.y });
		double d = alongOnly ? std::abs(ofX ? s.x - screen.x : s.y - screen.y) : Dist(s, screen);
		if (d <= best) {
			best = d;
			hit.feature = static_cast<int>(i);
			hit.plane = { f.x, f.y };
			hit.screen = s;
			hit.distance = Dist(s, screen);
		}
	}
}

FplTraceHit FplPlot::Nearest(FplPoint screen, const FplViewport& view, double maxDistance,
	double snapDistance, bool surfaces) const {
	FplTraceHit best;
	// a feature near the point first: they're what a user aims at
	for (size_t g = 0; g < graphs.size(); g++) {
		auto& graph = graphs[g];
		if (!graph.Drawable() || (graph.flags & FplGraphNoTrace) || graph.features.empty())
			continue;
		FplTraceHit hit;
		hit.graph = static_cast<int>(g);
		SnapToFeature(hit, screen, view, std::min(snapDistance, best ? best.distance : snapDistance), false);
		if (hit.feature >= 0)
			best = hit;
	}
	if (best)
		return best;
	for (size_t g = 0; g < graphs.size(); g++) {
		auto& graph = graphs[g];
		if (!graph.Drawable() || (graph.flags & FplGraphNoTrace))
			continue;
		FplTraceHit hit = OnGraph(static_cast<int>(g), screen, view);
		if (hit && hit.distance <= maxDistance && (!best || hit.distance < best.distance))
			best = hit;
	}
	if (best || !surfaces)
		return best;
	// no curve near: the surface under the point, the last drawn (it's on top)
	for (size_t g = graphs.size(); g-- > 0;) {
		auto& graph = graphs[g];
		if (!graph.Drawable() || (graph.flags & FplGraphNoTrace) || graph.expr.kind != FplKind::Surface)
			continue;
		FplPoint p = view.ToPlane(screen);
		double z = EvalSurface(static_cast<int>(g), p.x, p.y);
		if (!std::isfinite(z))
			continue;
		best.graph = static_cast<int>(g);
		best.plane = p;
		best.screen = screen;
		best.z = z;
		return best;
	}
	return best;
}

FplTraceHit FplPlot::Step(int graph, FplPoint plane, double dips, const FplViewport& view) const {
	FplTraceHit hit;
	if (graph < 0 || graph >= static_cast<int>(graphs.size()) || !graphs[graph].Drawable() || dips == 0)
		return hit;
	auto& g = graphs[graph];
	if ((g.expr.kind == FplKind::Function || g.expr.kind == FplKind::FunctionOfY) && !g.Listed()) {
		const bool ofX = g.expr.kind == FplKind::Function;
		const double unit = ofX ? 1 / view.ScaleX() : 1 / view.ScaleY();
		const bool log = ofX ? view.logX : view.logY;
		double v = ofX ? view.AxisX(plane.x) : view.AxisY(plane.y);
		// on past an undefined stretch, up to a view's width of tries (in axis units: a log axis's logarithm)
		for (int k = 1; k <= 4000; k++) {
			double next = v + dips * k * unit;
			if (log)
				next = std::pow(10.0, next);
			double w = Eval(graph, next);
			if (std::isfinite(w)) {
				hit.graph = graph;
				hit.plane = ofX ? FplPoint{ next, w } : FplPoint{ w, next };
				hit.screen = view.ToScreen(hit.plane);
				return hit;
			}
		}
		return hit;
	}
	// along the polylines: from the nearest place on them, by arc length on the screen
	FplPoint at = view.ToScreen(plane);
	int bestLine = -1, bestSeg = -1;
	double bestU = 0, bestD = std::numeric_limits<double>::infinity();
	auto& lines = g.geometry.lines;
	for (size_t l = 0; l < lines.size(); l++)
		for (size_t i = 0; i + 1 < lines[l].size(); i++) {
			FplPoint a = lines[l][i], b = lines[l][i + 1];
			double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
			double u = len2 > 0 ? std::clamp(((at.x - a.x) * dx + (at.y - a.y) * dy) / len2, 0.0, 1.0) : 0;
			double d = std::hypot(a.x + u * dx - at.x, a.y + u * dy - at.y);
			if (d < bestD) {
				bestD = d;
				bestLine = static_cast<int>(l);
				bestSeg = static_cast<int>(i);
				bestU = u;
			}
		}
	if (bestLine < 0)
		return hit;
	auto& line = lines[bestLine];
	double left = std::abs(dips);
	int i = bestSeg;
	double u = bestU;
	FplPoint p;
	for (;;) {
		FplPoint a = line[i], b = line[i + 1];
		double len = std::hypot(b.x - a.x, b.y - a.y);
		double room = dips > 0 ? (1 - u) * len : u * len;
		if (room >= left || (dips > 0 ? i + 2 >= static_cast<int>(line.size()) : i == 0)) {
			double du = len > 0 ? std::min(left, room) / len : 0;
			u = dips > 0 ? u + du : u - du;
			p = { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u };
			break;
		}
		left -= room;
		if (dips > 0) {
			i++;
			u = 0;
		}
		else {
			i--;
			u = 1;
		}
	}
	hit.graph = graph;
	hit.screen = p;
	hit.plane = view.ToPlane(p);
	return hit;
}

FplTraceHit FplPlot::NextFeature(int graph, double x, int dir, const FplViewport& view, double y) const {
	FplTraceHit hit;
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return hit;
	auto& fs = graphs[graph].features;
	const double eps = 1e-9 * (view.xMax - view.xMin);
	int found = -1;
	for (size_t i = 0; i < fs.size(); i++) {
		const bool sameX = std::abs(fs[i].x - x) <= eps;
		if (dir > 0 && (fs[i].x > x + eps || (sameX && fs[i].y > y + eps))) {
			found = static_cast<int>(i);
			break;
		}
		if (dir < 0 && (fs[i].x < x - eps || (sameX && fs[i].y < y - eps)))
			found = static_cast<int>(i);
	}
	if (found < 0)
		return hit;
	hit.graph = graph;
	hit.feature = found;
	hit.plane = { fs[found].x, fs[found].y };
	hit.screen = view.ToScreen(hit.plane);
	return hit;
}

FplTraceHit FplPlot::NextGraphAt(int graph, double x, int dir, const FplViewport& view) const {
	FplTraceHit hit;
	const int n = static_cast<int>(graphs.size());
	for (int k = 1; k <= n; k++) {
		int g = ((graph + dir * k) % n + n) % n;
		auto& gr = graphs[g];
		if (!gr.Drawable() || (gr.flags & FplGraphNoTrace) || gr.expr.kind != FplKind::Function || gr.Listed())
			continue;
		double y = Eval(g, x);
		if (!std::isfinite(y))
			continue;
		hit.graph = g;
		hit.plane = { x, y };
		hit.screen = view.ToScreen(hit.plane);
		return hit;
	}
	return hit;
}

double FplPlot::Slope(int graph, double v) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()))
		return NaN;
	auto& g = graphs[graph];
	if (g.expr.error || (g.expr.kind != FplKind::Function && g.expr.kind != FplKind::FunctionOfY) || g.Listed())
		return NaN;
	if (!g.expr.df.Empty()) {
		auto& slots = const_cast<std::vector<double>&>(m_Slots);
		slots[g.expr.kind == FplKind::Function ? FplSlotX : FplSlotY] = v;
		double d = g.expr.df.Eval(slots.data());
		if (std::isfinite(d))
			return d;
	}
	double h = 1e-6 * std::max(1.0, std::abs(v));
	return (Eval(graph, v + h) - Eval(graph, v - h)) / (2 * h);
}

double FplPlot::EvalSurface(int graph, double x, double y) const {
	auto& g = graphs[graph];
	if (g.expr.error || g.expr.kind != FplKind::Surface)
		return NaN;
	auto& slots = const_cast<std::vector<double>&>(m_Slots);
	slots[FplSlotX] = x;
	slots[FplSlotY] = y;
	return g.expr.f.Eval(slots.data());
}

FplTraceHit FplPlot::Pick3D(FplVec3 origin, FplVec3 dir, const FplBox3& box) const {
	FplTraceHit hit;
	float best = 0;
	for (size_t g = 0; g < graphs.size(); g++) {
		auto& graph = graphs[g];
		if (!graph.Drawable() || (graph.flags & FplGraphNoTrace) || graph.mesh.Empty())
			continue;
		float t;
		if (FplIntersect(graph.mesh, origin, dir, t) && (!hit || t < best)) {
			best = t;
			hit.graph = static_cast<int>(g);
		}
	}
	if (!hit)
		return hit;
	FplVec3 at = { origin.x + dir.x * best, origin.y + dir.y * best, origin.z + dir.z * best };
	FplPoint3 p = box.ToSpace(at);
	auto& graph = graphs[hit.graph];
	auto& slots = const_cast<std::vector<double>&>(m_Slots);
	switch (graph.expr.kind) {
		case FplKind::Surface: {
			double z = EvalSurface(hit.graph, p.x, p.y);
			if (std::isfinite(z))
				p.z = z;
			break;
		}
		case FplKind::Point3D:
			if (!graph.Listed())
				p = { graph.expr.f.Eval(slots.data()), graph.expr.g.Eval(slots.data()), graph.expr.h.Eval(slots.data()) };
			break;
		case FplKind::Point:
			if (!graph.Listed())
				p = { graph.expr.f.Eval(slots.data()), graph.expr.g.Eval(slots.data()), 0 };
			break;
		default:
			break;
	}
	hit.plane = { p.x, p.y };
	hit.z = p.z;
	return hit;
}

FplTraceHit FplPlot::TraceAt(int graph, FplPoint screen, const FplViewport& view, double snapDistance) const {
	FplTraceHit hit;
	if (graph < 0 || graph >= static_cast<int>(graphs.size()) || !graphs[graph].Drawable())
		return hit;
	auto& g = graphs[graph];
	bool function = (g.expr.kind == FplKind::Function || g.expr.kind == FplKind::FunctionOfY) && !g.Listed();
	if (function) {
		bool ofX = g.expr.kind == FplKind::Function;
		double v = ofX ? view.ToPlaneX(screen.x) : view.ToPlaneY(screen.y);
		double w = Eval(graph, v);
		if (std::isfinite(w)) {
			hit.graph = graph;
			hit.plane = ofX ? FplPoint{ v, w } : FplPoint{ w, v };
			hit.screen = view.ToScreen(hit.plane);
			hit.distance = Dist(hit.screen, screen);
		}
	}
	if (!hit && g.expr.kind == FplKind::Surface) {
		// a surface: the point under the mouse
		FplPoint p = view.ToPlane(screen);
		double z = EvalSurface(graph, p.x, p.y);
		if (std::isfinite(z)) {
			hit.graph = graph;
			hit.plane = p;
			hit.screen = screen;
			hit.z = z;
		}
		return hit;
	}
	if (!hit)
		hit = OnGraph(graph, screen, view);
	if (hit)
		SnapToFeature(hit, screen, view, snapDistance, function);
	return hit;
}

// ---- Coordinates -----------------------------------------------------------------------------------------------------

std::wstring FplFormatCoordinate(double value, double unitsPerDip) {
	if (std::isnan(value))
		return L"undefined";
	if (std::isinf(value))
		return value > 0 ? L"\x221E" : L"\x2212\x221E";
	const wchar_t minus = 0x2212;
	double a = std::abs(value);
	std::wstring s;
	if (a >= 1e7) {
		int e = static_cast<int>(std::floor(std::log10(a)));
		wchar_t buf[64];
		swprintf_s(buf, L"%.4f", a / std::pow(10.0, e));
		s = buf;
		while (s.back() == L'0')
			s.pop_back();
		if (s.back() == L'.')
			s.pop_back();
		static const wchar_t Digits[] = { 0x2070, 0x00B9, 0x00B2, 0x00B3, 0x2074, 0x2075, 0x2076, 0x2077, 0x2078, 0x2079 };
		s += L"\x00D7" L"10";
		for (wchar_t c : std::to_wstring(e))
			s += Digits[c - L'0'];
	}
	else {
		int decimals = 3;
		if (unitsPerDip > 0 && std::isfinite(unitsPerDip))
			decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(unitsPerDip))) + 1, 0, 15);
		wchar_t buf[400];
		swprintf_s(buf, L"%.*f", decimals, a);
		s = buf;
		if (s.find(L'.') != std::wstring::npos) {
			while (s.back() == L'0')
				s.pop_back();
			if (s.back() == L'.')
				s.pop_back();
		}
		if (s == L"0")
			return s;
	}
	if (value < 0)
		s.insert(s.begin(), minus);
	return s;
}

// ---- Fitting the view --------------------------------------------------------------------------------------------------

namespace {

// The 2nd and 98th percentiles (all of them when there are few).
bool Robust(std::vector<double>& v, double& lo, double& hi) {
	v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
	if (v.empty())
		return false;
	std::sort(v.begin(), v.end());
	size_t cut = v.size() >= 50 ? v.size() / 50 : 0;
	lo = v[cut];
	hi = v[v.size() - 1 - cut];
	return true;
}

} // namespace

bool FplPlot::FitBounds(const FplViewport& view, double& xMin, double& xMax, double& yMin, double& yMax) const {
	constexpr double TwoPi = 6.283185307179586;
	bool any = false;
	double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
	auto add = [&](double a, double b, double c, double d) {
		if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d))
			return;
		if (!any) {
			x0 = a, x1 = b, y0 = c, y1 = d;
			any = true;
		}
		else {
			x0 = std::min(x0, a), x1 = std::max(x1, b), y0 = std::min(y0, c), y1 = std::max(y1, d);
		}
	};
	auto addSets = [&](std::vector<double>& xs, std::vector<double>& ys) {
		double a, b, c, d;
		if (Robust(xs, a, b) && Robust(ys, c, d))
			add(a, b, c, d);
	};
	std::vector<double> slots = m_Slots;
	double* s = slots.data();
	for (size_t i = 0; i < graphs.size(); i++) {
		auto& g = graphs[i];
		if (!g.Drawable())
			continue;
		auto& e = g.expr;
		const double t0 = g.hasRange ? g.tMin : 0, t1 = g.hasRange ? g.tMax : TwoPi;
		const size_t elements = g.Listed() ? std::min<size_t>(ElementCount(static_cast<int>(i)), 100000) : 1;
		std::vector<double> xs, ys;
		for (size_t el = 0; el < elements; el++) {
			if (g.Listed())
				SetElement(slots, g, el);
			const int n = g.Listed() ? 100 : 400;
			switch (e.kind) {
				case FplKind::Point:
					xs.push_back(e.f.Eval(s));
					ys.push_back(e.g.Eval(s));
					break;
				case FplKind::Function: case FplKind::FunctionOfY: {
					// the view's x (y), its values fitted
					const bool ofX = e.kind == FplKind::Function;
					const double a = ofX ? view.AxisX(view.xMin) : view.AxisY(view.yMin);
					const double b = ofX ? view.AxisX(view.xMax) : view.AxisY(view.yMax);
					for (int k = 0; k <= n; k++) {
						double v = ofX ? view.FromAxisX(a + (b - a) * k / n) : view.FromAxisY(a + (b - a) * k / n);
						s[ofX ? FplSlotX : FplSlotY] = v;
						double w = e.f.Eval(s);
						if (!std::isfinite(w))
							continue;
						(ofX ? xs : ys).push_back(v);
						(ofX ? ys : xs).push_back(w);
					}
					break;
				}
				case FplKind::Parametric: case FplKind::Polar:
					for (int k = 0; k <= n * 2; k++) {
						double t = t0 + (t1 - t0) * k / (n * 2);
						if (e.kind == FplKind::Parametric) {
							s[FplSlotT] = t;
							xs.push_back(e.f.Eval(s));
							ys.push_back(e.g.Eval(s));
						}
						else {
							s[FplSlotTheta] = t;
							double r = e.f.Eval(s);
							xs.push_back(r * std::cos(t));
							ys.push_back(r * std::sin(t));
						}
					}
					break;
				case FplKind::Integral: {
					double lo = e.g.Eval(s), hi = e.h.Eval(s);
					int slot = FplStandardSlot(e.name);
					if (!std::isfinite(lo) || !std::isfinite(hi) || slot < 0)
						break;
					if (!view.logY)
						ys.push_back(0);
					for (int k = 0; k <= n; k++) {
						double t = lo + (hi - lo) * k / n;
						s[slot] = t;
						xs.push_back(t);
						ys.push_back(e.f.Eval(s));
					}
					break;
				}
				case FplKind::Implicit: case FplKind::SlopeField: case FplKind::Between: case FplKind::SecondOrder:
				case FplKind::OdeSystem:
					// what was sampled (the view grown by its size; an ODE's solution)
					for (auto& line : g.geometry.lines)
						for (auto& p : line) {
							FplPoint q = view.ToPlane(p);
							xs.push_back(q.x);
							ys.push_back(q.y);
						}
					break;
				default:
					break;
			}
		}
		// on log axes in their logarithms (what isn't positive drops out as NaN)
		if (view.logX)
			for (auto& x : xs)
				x = view.AxisX(x);
		if (view.logY)
			for (auto& y : ys)
				y = view.AxisY(y);
		if (e.kind == FplKind::Point) {
			// points exactly
			double a, b, c, d;
			std::vector<double> px = xs, py = ys;
			px.erase(std::remove_if(px.begin(), px.end(), [](double x) { return !std::isfinite(x); }), px.end());
			py.erase(std::remove_if(py.begin(), py.end(), [](double x) { return !std::isfinite(x); }), py.end());
			if (!px.empty() && !py.empty()) {
				a = *std::min_element(px.begin(), px.end());
				b = *std::max_element(px.begin(), px.end());
				c = *std::min_element(py.begin(), py.end());
				d = *std::max_element(py.begin(), py.end());
				add(a, b, c, d);
			}
		}
		else
			addSets(xs, ys);
	}
	if (!any)
		return false;
	// a little room, and some size when it's a point or a line
	auto widen = [](double& lo, double& hi) {
		double span = hi - lo;
		if (!(span > 1e-12 * std::max({ 1.0, std::abs(lo), std::abs(hi) }))) {
			double half = std::max(1.0, std::abs(lo) * 0.1);
			lo -= half;
			hi += half;
		}
		else {
			lo -= span * 0.08;
			hi += span * 0.08;
		}
	};
	widen(x0, x1);
	widen(y0, y1);
	xMin = view.FromAxisX(x0), xMax = view.FromAxisX(x1), yMin = view.FromAxisY(y0), yMax = view.FromAxisY(y1);
	return true;
}

// ---- Labels ------------------------------------------------------------------------------------------------------------

std::wstring FplFormatLabel(std::wstring_view label, double x, double y, double unitsPerDip) {
	std::wstring out;
	out.reserve(label.size() + 16);
	for (size_t i = 0; i < label.size(); i++) {
		if (label.substr(i, 3) == L"{x}") {
			out += FplFormatCoordinate(x, unitsPerDip);
			i += 2;
		}
		else if (label.substr(i, 3) == L"{y}") {
			out += FplFormatCoordinate(y, unitsPerDip);
			i += 2;
		}
		else
			out += label[i];
	}
	return out;
}

std::vector<FplLabelPlace> FplPlot::LabelPlaces(const FplViewport& view) const {
	std::vector<FplLabelPlace> places;
	// a DIP's worth of the plane where the label is (log axes differ from place to place)
	auto units = [&](FplPoint q) { return std::min(1 / view.DipsPerUnitX(q.x), 1 / view.DipsPerUnitY(q.y)); };
	for (size_t i = 0; i < graphs.size(); i++) {
		auto& g = graphs[i];
		if (!g.Drawable() || g.label.empty())
			continue;
		auto inside = [&](FplPoint p) { return p.x >= 0 && p.y >= 0 && p.x <= view.width && p.y <= view.height; };
		if (!g.geometry.points.empty()) {
			for (auto& p : g.geometry.points) {
				if (!inside(p))
					continue;
				FplPoint q = view.ToPlane(p);
				places.push_back({ static_cast<int>(i), p, FplFormatLabel(g.label, q.x, q.y, units(q)) });
				if (places.size() > 2000)
					return places;
			}
			continue;
		}
		// a curve: its rightmost point in the view
		bool found = false;
		FplPoint best;
		for (auto& line : g.geometry.lines)
			for (auto& p : line)
				if (inside(p) && (!found || p.x > best.x)) {
					best = p;
					found = true;
				}
		if (found) {
			FplPoint q = view.ToPlane(best);
			places.push_back({ static_cast<int>(i), best, FplFormatLabel(g.label, q.x, q.y, units(q)) });
		}
	}
	return places;
}

// ---- Undo --------------------------------------------------------------------------------------------------------------

bool FplSnapshot::operator==(const FplSnapshot& o) const {
	return params == o.params && hidden == o.hidden && std::equal(std::begin(view), std::end(view), std::begin(o.view)) &&
		std::equal(std::begin(box), std::end(box), std::begin(o.box)) && yaw == o.yaw && pitch == o.pitch;
}

void FplHistory::Push(FplSnapshot before) {
	m_Redo.clear();
	if (!m_Undo.empty() && m_Undo.back() == before)
		return;
	m_Undo.push_back(std::move(before));
	if (m_Undo.size() > m_Limit)
		m_Undo.erase(m_Undo.begin());
}

bool FplHistory::Undo(const FplSnapshot& current, FplSnapshot& to) {
	// a step that ended where it began changed nothing
	while (!m_Undo.empty() && m_Undo.back() == current)
		m_Undo.pop_back();
	if (m_Undo.empty())
		return false;
	to = std::move(m_Undo.back());
	m_Undo.pop_back();
	m_Redo.push_back(current);
	return true;
}

bool FplHistory::Redo(const FplSnapshot& current, FplSnapshot& to) {
	while (!m_Redo.empty() && m_Redo.back() == current)
		m_Redo.pop_back();
	if (m_Redo.empty())
		return false;
	to = std::move(m_Redo.back());
	m_Redo.pop_back();
	m_Undo.push_back(current);
	return true;
}

void FplHistory::Clear() {
	m_Undo.clear();
	m_Redo.clear();
}
