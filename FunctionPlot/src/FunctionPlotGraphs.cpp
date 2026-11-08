// The graphs: building them with their definitions, sampling, features and tracing.

#include "FunctionPlotGraphs.h"

#include <algorithm>
#include <cmath>
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

void FplPlot::Build() {
	m_Names.clear();
	m_Names.reserve(params.size() + graphs.size());
	for (auto& p : params)
		m_Names.push_back(p.name);
	m_Definitions.clear();
	for (auto& g : graphs) {
		g.expr = {};
		g.definition = false;
		g.slot = -1;
		g.geometry = {};
		g.features.clear();
	}

	// the definitions, found round after round: "c = 2d" is one once d is known
	FplParseOptions options;
	options.degrees = degrees;
	for (bool found = true; found;) {
		found = false;
		for (size_t i = 0; i < graphs.size(); i++) {
			auto& g = graphs[i];
			if (g.definition || g.expr.error.code == FplErrorCode::Redefined)
				continue;
			options.parameters = &m_Names;
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
			g.definition = true;
			g.slot = FplFirstParam + static_cast<int>(m_Names.size());
			m_Names.push_back(def.name);
			m_Definitions.push_back(static_cast<int>(i));
			found = true;
		}
	}

	for (auto& g : graphs) {
		if (g.expr.error.code == FplErrorCode::Redefined)
			continue;
		if (!g.definition) {
			g.expr = FplBuild(g.text, m_Names, degrees);
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
	}
	UpdateValues();
}

void FplPlot::UpdateValues() {
	m_Slots.assign(FplFirstParam + m_Names.size(), 0.0);
	for (size_t i = 0; i < params.size() && FplFirstParam + i < m_Slots.size(); i++)
		m_Slots[FplFirstParam + i] = params[i].value;
	for (int d : m_Definitions)
		m_Slots[graphs[d].slot] = NaN;
	// in order, round after round, so a definition may use one after it; a cycle stays NaN
	for (size_t round = 0; round < m_Definitions.size(); round++) {
		for (int d : m_Definitions) {
			auto& g = graphs[d];
			m_Slots[g.slot] = g.expr.error || g.expr.f.Empty() ? NaN : g.expr.f.Eval(m_Slots.data());
		}
	}
}

double FplPlot::DefinitionValue(int graph) const {
	if (graph < 0 || graph >= static_cast<int>(graphs.size()) || !graphs[graph].definition ||
		graphs[graph].slot >= static_cast<int>(m_Slots.size()))
		return NaN;
	return m_Slots[graphs[graph].slot];
}

void FplPlot::Sample(const FplViewport& view, const FplSampleOptions& options) {
	for (auto& g : graphs) {
		if (!g.Drawable()) {
			g.geometry = {};
			continue;
		}
		FplSampleOptions o = options;
		if (g.hasRange) {
			o.tMin = o.thetaMin = g.tMin;
			o.tMax = o.thetaMax = g.tMax;
		}
		g.geometry = FplSample(g.expr, m_Slots, view, o);
	}
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
	if (!g.Drawable() || g.expr.kind != FplKind::Function || (g.flags & FplGraphNoFeatures) || !view.Valid())
		return;
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
	g.features = FplFindFeatures(f, df, view.xMin, view.xMax, samples, yTolerance);
	for (size_t h = 0; h < graphs.size(); h++) {
		auto& other = graphs[h];
		if (static_cast<int>(h) == graph || !other.Drawable() || other.expr.kind != FplKind::Function)
			continue;
		std::function<double(double)> fo = [this, h](double x) { return Eval(static_cast<int>(h), x); };
		auto meets = FplFindIntersections(f, fo, view.xMin, view.xMax, samples, yTolerance);
		g.features.insert(g.features.end(), meets.begin(), meets.end());
	}
	std::stable_sort(g.features.begin(), g.features.end(), [](const FplFeature& a, const FplFeature& b) {
		return a.x < b.x;
	});
	if (g.features.size() > 1000)
		g.features.resize(1000);
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
	// a function's point exactly on it
	if (g.expr.kind == FplKind::Function || g.expr.kind == FplKind::FunctionOfY) {
		bool ofX = g.expr.kind == FplKind::Function;
		double v = ofX ? hit.plane.x : hit.plane.y;
		double w = Eval(graph, v);
		if (std::isfinite(w)) {
			hit.plane = ofX ? FplPoint{ v, w } : FplPoint{ w, v };
			hit.screen = view.ToScreen(hit.plane);
		}
	}
	else if (g.expr.kind == FplKind::Point && !g.geometry.points.empty()) {
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
	double snapDistance) const {
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
	return best;
}

FplTraceHit FplPlot::TraceAt(int graph, FplPoint screen, const FplViewport& view, double snapDistance) const {
	FplTraceHit hit;
	if (graph < 0 || graph >= static_cast<int>(graphs.size()) || !graphs[graph].Drawable())
		return hit;
	auto& g = graphs[graph];
	bool function = g.expr.kind == FplKind::Function || g.expr.kind == FplKind::FunctionOfY;
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
