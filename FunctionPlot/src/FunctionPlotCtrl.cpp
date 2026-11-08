// The function plot control: the API, the view, tracing, and the mouse and keys. Painting is in FunctionPlotPaint.cpp.

#include "FunctionPlotData.h"

#include <uxtheme.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "windowscodecs.lib")

static_assert(FPLGF_HIDDEN == FplGraphHidden && FPLGF_DASHED == FplGraphDashed && FPLGF_NOTRACE == FplGraphNoTrace &&
	FPLGF_NOFEATURES == FplGraphNoFeatures && FPLGF_HEATMAP == FplGraphHeatmap && FPLGF_NOCONTOURS == FplGraphNoContours &&
	FPLGF_NODRAG == FplGraphNoDrag && FPLGF_DOTTED == FplGraphDotted && FPLGF_HOLLOW == FplGraphHollow &&
	FPLGF_MESHLINES == FplGraphMeshLines && FPLGF_LEVELLINES == FplGraphLevelLines &&
	FPLGF_NOSINGULAR == FplGraphNoSingular);
static_assert(FPLK_FUNCTION == (int)FplKind::Function && FPLK_DEFINITION == (int)FplKind::Definition &&
	FPLK_IMPLICIT == (int)FplKind::Implicit && FPLK_SURFACE == (int)FplKind::Surface &&
	FPLK_VECTORFIELD == (int)FplKind::VectorField && FPLK_SPACECURVE == (int)FplKind::SpaceCurve &&
	FPLK_PARAMSURFACE == (int)FplKind::ParametricSurface && FPLK_POINT3D == (int)FplKind::Point3D &&
	FPLK_IMPLICITSURFACE == (int)FplKind::ImplicitSurface && FPLK_INTEGRAL == (int)FplKind::Integral &&
	FPLK_REGRESSION == (int)FplKind::Regression && FPLK_BETWEEN == (int)FplKind::Between &&
	FPLK_SLOPEFIELD == (int)FplKind::SlopeField && FPLK_SECONDORDER == (int)FplKind::SecondOrder &&
	FPLK_ODESYSTEM == (int)FplKind::OdeSystem);
static_assert(FPLE_REDEFINED == (int)FplErrorCode::Redefined && FPLE_INTERNAL == (int)FplErrorCode::Internal);
static_assert(FPLF_ROOT == (int)FplFeatureKind::Root && FPLF_YINTERCEPT == (int)FplFeatureKind::YIntercept);

namespace {

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

bool IsDarkColor(COLORREF color) {
	return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
}

constexpr double Pi = 3.14159265358979323846;

}  // namespace

// ---- Construction ------------------------------------------------------------------

CFunctionPlotCtrl::CFunctionPlotCtrl() : m_State(std::make_unique<FplState>()) {
}

CFunctionPlotCtrl::~CFunctionPlotCtrl() = default;

LRESULT CFunctionPlotCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	s.dpi = ::GetDpiForWindow(m_hWnd);
	RECT rc;
	GetClientRect(&rc);
	s.client = { rc.right, rc.bottom };
	// the sampling thread tells the window (a message posted to a window gone is dropped)
	HWND hwnd = m_hWnd;
	s.worker = std::make_unique<FplWorker>([hwnd] { ::PostMessageW(hwnd, WM_FPL_SAMPLED, 0, 0); });
	Rebuild();
	BOOL handled;
	OnSize(0, 0, MAKELPARAM(rc.right, rc.bottom), handled);
	return 0;
}

LRESULT CFunctionPlotCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	KillTimer(AnimationTimer);
	m_State->worker.reset();
	DiscardTarget();
	m_State->gpu.clear();
	m_State->boxEdges = m_State->floorGrid = {};
	m_State->renderer.Reset();
	if (m_State->view3D) {
		::DestroyWindow(m_State->view3D);
		m_State->view3D = nullptr;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

float CFunctionPlotCtrl::ToDips(int pixels) const {
	return pixels * 96.0f / m_State->dpi;
}

LRESULT CFunctionPlotCtrl::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	s.client = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (s.target)
		s.target->Resize(D2D1::SizeU(s.client.cx, s.client.cy));
	if (s.view3D)
		::SetWindowPos(s.view3D, nullptr, 0, 0, s.client.cx, s.client.cy, SWP_NOZORDER | SWP_NOACTIVATE);
	if (s.renderer.Ready())
		s.renderer.Resize(s.client.cx, s.client.cy, (float)s.dpi);
	double w = ToDips(s.client.cx), h = ToDips(s.client.cy);
	if (w <= 0 || h <= 0)
		return 0;
	if (!s.viewSet) {
		s.view.width = w;
		s.view.height = h;
		s.viewSet = true;
		if (s.pendingView) {
			// SetState's ranges, given before there was a size
			s.pendingView = false;
			SetViewport(s.view.xMin, s.view.xMax, s.view.yMin, s.view.yMax);
			ViewChanged(false);
		}
		else
			ResetView();
	}
	else {
		s.view.Resize(w, h);
		ViewChanged(false);
	}
	LayoutPanel();
	return 0;
}

LRESULT CFunctionPlotCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto ss = (const STYLESTRUCT*)lParam;
	if (wParam == (WPARAM)GWL_STYLE) {
		if ((ss->styleNew & FPLS_EQUALSCALE) && !(ss->styleOld & FPLS_EQUALSCALE) && m_State->viewSet) {
			m_State->view.EqualScale();
			ViewChanged(false);
		}
		if ((ss->styleOld ^ ss->styleNew) & FPLS_NOTRACE)
			ClearTrace();
		LayoutPanel();
		Invalidate();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// everything is in DIPs: the target's DPI changes, and WM_SIZE follows with the new size
	auto& s = *m_State;
	s.dpi = ::GetDpiForWindow(m_hWnd);
	if (s.target)
		s.target->SetDpi((float)s.dpi, (float)s.dpi);
	if (s.renderer.Ready())
		s.renderer.Resize(s.client.cx, s.client.cy, (float)s.dpi);
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_State->brushes.clear();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETHIGHCONTRAST) {
		BOOL handled;
		OnColorsChanged(0, 0, 0, handled);
	}
	bHandled = FALSE;
	return 0;
}

// ---- Look ------------------------------------------------------------------------------

void CFunctionPlotCtrl::ForceHighContrast(int value) {
	m_State->forceHighContrast = value;
	BOOL handled;
	OnColorsChanged(0, 0, 0, handled);
}

bool CFunctionPlotCtrl::IsHighContrast() const {
	if (m_State->forceHighContrast >= 0)
		return m_State->forceHighContrast != 0;
	HIGHCONTRASTW hc{ sizeof(hc) };
	return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

void CFunctionPlotCtrl::SetColors(const FPLCOLORS* colors) {
	m_State->customColors = colors != nullptr;
	if (colors)
		m_State->colors = *colors;
	m_State->brushes.clear();
	if (IsWindow())
		Invalidate();
}

FPLCOLORS CFunctionPlotCtrl::GetColors() const {
	FPLCOLORS c{};
	if (IsHighContrast()) {
		c.crBack = c.crPanelBack = ::GetSysColor(COLOR_WINDOW);
		// no minor lines: one gray for both would be a thicket
		c.crGrid = ::GetSysColor(COLOR_GRAYTEXT);
		c.crGridMinor = c.crBack;
		c.crAxis = c.crLabel = c.crPanelText = c.crPanelBorder = ::GetSysColor(COLOR_WINDOWTEXT);
		c.crError = ::GetSysColor(COLOR_WINDOWTEXT);
		const COLORREF cycle[] = { ::GetSysColor(COLOR_WINDOWTEXT), ::GetSysColor(COLOR_HOTLIGHT),
			::GetSysColor(COLOR_HIGHLIGHT), ::GetSysColor(COLOR_GRAYTEXT) };
		for (int i = 0; i < FPL_PALETTE; i++)
			c.crPalette[i] = cycle[i % 4];
		return c;
	}
	if (m_State->customColors)
		return m_State->colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	const COLORREF text = ::GetSysColor(COLOR_WINDOWTEXT);
	const bool dark = IsDarkColor(c.crBack);
	c.crGridMinor = Blend(c.crBack, text, dark ? 9 : 7);
	c.crGrid = Blend(c.crBack, text, dark ? 20 : 17);
	c.crAxis = Blend(c.crBack, text, 70);
	c.crLabel = Blend(c.crBack, text, 72);
	c.crPanelBack = Blend(c.crBack, text, dark ? 6 : 3);
	c.crPanelText = text;
	c.crPanelBorder = Blend(c.crBack, text, dark ? 25 : 18);
	c.crError = dark ? RGB(255, 153, 164) : RGB(196, 43, 28);
	static const COLORREF Light[FPL_PALETTE] = { RGB(0xC7, 0x44, 0x40), RGB(0x2D, 0x70, 0xB3), RGB(0x38, 0x8C, 0x46),
		RGB(0x60, 0x42, 0xA6), RGB(0xFA, 0x7E, 0x19), RGB(0x1B, 0x8E, 0x8E), RGB(0xB0, 0x36, 0x7C),
		RGB(0x40, 0x40, 0x40) };
	static const COLORREF Dark[FPL_PALETTE] = { RGB(0xF0, 0x6E, 0x69), RGB(0x5F, 0xA2, 0xE8), RGB(0x5C, 0xC2, 0x6D),
		RGB(0xA4, 0x88, 0xE8), RGB(0xFF, 0xA1, 0x49), RGB(0x4F, 0xD1, 0xD1), RGB(0xE8, 0x6E, 0xB0),
		RGB(0xD8, 0xD8, 0xD8) };
	std::copy(std::begin(dark ? Dark : Light), std::end(dark ? Dark : Light), c.crPalette);
	return c;
}

COLORREF CFunctionPlotCtrl::GraphColor(int index) const {
	auto& g = m_State->plot.graphs[index];
	if (!g.autoColor && !IsHighContrast())
		return g.color;
	return GetColors().crPalette[index % FPL_PALETTE];
}

// ---- Graphs ----------------------------------------------------------------------------

namespace {

void Apply(FplGraph& g, const FPLGRAPH& in, bool& rebuild) {
	if (in.mask & FPLGM_TEXT) {
		std::wstring text = in.pszText ? in.pszText : L"";
		rebuild = rebuild || text != g.text;
		g.text = std::move(text);
	}
	if (in.mask & FPLGM_COLOR) {
		g.autoColor = in.color == CLR_DEFAULT;
		g.color = in.color;
	}
	if (in.mask & FPLGM_WIDTH)
		g.width = in.width > 0 ? std::min(in.width, 50.0f) : 2.5f;
	if (in.mask & FPLGM_FLAGS) {
		// hiding needs sampling (and a hidden function no longer meets the others)
		rebuild = rebuild || ((g.flags ^ in.flags) & FplGraphHidden);
		g.flags = in.flags;
	}
	if (in.mask & FPLGM_RANGE) {
		g.hasRange = std::isfinite(in.tMin) && std::isfinite(in.tMax) && in.tMin != in.tMax;
		g.tMin = in.tMin;
		g.tMax = in.tMax;
		rebuild = true;
	}
	if (in.mask & FPLGM_LPARAM)
		g.lParam = in.lParam;
	if (in.mask & FPLGM_LABEL)
		g.label = in.pszLabel ? in.pszLabel : L"";
	if (in.mask & FPLGM_OPACITY) {
		g.opacity = std::isfinite(in.opacity) ? std::clamp(in.opacity, 0.0f, 1.0f) : 1.0f;
		g.fillOpacity = std::isfinite(in.fillOpacity) ? std::min(in.fillOpacity, 1.0f) : -1.0f;
	}
}

}  // namespace

BOOL CFunctionPlotCtrl::AddGraph(const FPLGRAPH& graph) {
	auto& plot = m_State->plot;
	if (graph.id == 0 || plot.FindGraph(graph.id) >= 0)
		return FALSE;
	FplGraph g;
	g.id = graph.id;
	bool rebuild = true;
	Apply(g, graph, rebuild);
	plot.graphs.push_back(std::move(g));
	Rebuild();
	return TRUE;
}

BOOL CFunctionPlotCtrl::SetGraph(const FPLGRAPH& graph) {
	auto& plot = m_State->plot;
	int i = plot.FindGraph(graph.id);
	if (i < 0)
		return FALSE;
	bool rebuild = false;
	Apply(plot.graphs[i], graph, rebuild);
	if (rebuild)
		Rebuild();
	else {
		LayoutPanel();
		if (IsWindow())
			Invalidate();
	}
	return TRUE;
}

BOOL CFunctionPlotCtrl::GetGraph(FPLGRAPH& graph) const {
	auto& plot = m_State->plot;
	int i = plot.FindGraph(graph.id);
	if (i < 0)
		return FALSE;
	auto& g = plot.graphs[i];
	if (graph.mask & FPLGM_TEXT)
		graph.pszText = g.text.c_str();
	if (graph.mask & FPLGM_COLOR)
		graph.color = g.autoColor ? CLR_DEFAULT : g.color;
	if (graph.mask & FPLGM_WIDTH)
		graph.width = g.width;
	if (graph.mask & FPLGM_FLAGS)
		graph.flags = g.flags;
	if (graph.mask & FPLGM_RANGE) {
		graph.tMin = g.hasRange ? g.tMin : 0;
		graph.tMax = g.hasRange ? g.tMax : 2 * Pi;
	}
	if (graph.mask & FPLGM_LPARAM)
		graph.lParam = g.lParam;
	if (graph.mask & FPLGM_LABEL)
		graph.pszLabel = g.label.c_str();
	if (graph.mask & FPLGM_OPACITY) {
		graph.opacity = g.opacity;
		graph.fillOpacity = g.fillOpacity;
	}
	return TRUE;
}

BOOL CFunctionPlotCtrl::GetGraphInfo(UINT id, FPLGRAPHINFO& info) const {
	auto& plot = m_State->plot;
	int i = plot.FindGraph(id);
	if (i < 0)
		return FALSE;
	auto& g = plot.graphs[i];
	info = {};
	info.kind = (FPLKIND)g.expr.kind;
	info.error = (FPLERROR)g.expr.error.code;
	info.errorPos = g.expr.error.pos;
	info.errorLen = g.expr.error.len;
	wcsncpy_s(info.szError, g.expr.error.Message().c_str(), _TRUNCATE);
	const std::wstring& name = g.expr.error ? g.expr.error.name : g.expr.name;
	wcsncpy_s(info.szName, name.c_str(), _TRUNCATE);
	info.value = g.expr.kind == FplKind::Integral ? plot.IntegralValue(i)
		: g.expr.kind == FplKind::Regression ? g.r2 : plot.DefinitionValue(i);
	info.color = GraphColor(i);
	return TRUE;
}

BOOL CFunctionPlotCtrl::RemoveGraph(UINT id) {
	auto& plot = m_State->plot;
	int i = plot.FindGraph(id);
	if (i < 0)
		return FALSE;
	plot.graphs.erase(plot.graphs.begin() + i);
	ClearTrace();
	m_State->featuresGraph = -1;
	Rebuild();
	return TRUE;
}

void CFunctionPlotCtrl::RemoveAllGraphs() {
	m_State->plot.graphs.clear();
	ClearTrace();
	m_State->featuresGraph = -1;
	Rebuild();
}

int CFunctionPlotCtrl::GetGraphCount() const {
	return (int)m_State->plot.graphs.size();
}

UINT CFunctionPlotCtrl::GetGraphId(int index) const {
	auto& graphs = m_State->plot.graphs;
	return index >= 0 && index < (int)graphs.size() ? graphs[index].id : 0;
}

BOOL CFunctionPlotCtrl::MoveGraph(UINT id, int index) {
	auto& graphs = m_State->plot.graphs;
	int i = m_State->plot.FindGraph(id);
	if (i < 0)
		return FALSE;
	index = std::clamp(index, 0, (int)graphs.size() - 1);
	FplGraph g = std::move(graphs[i]);
	graphs.erase(graphs.begin() + i);
	graphs.insert(graphs.begin() + index, std::move(g));
	ClearTrace();
	m_State->featuresGraph = -1;
	Rebuild();
	return TRUE;
}

BOOL CFunctionPlotCtrl::ShowGraph(UINT id, BOOL show) {
	int i = m_State->plot.FindGraph(id);
	if (i < 0)
		return FALSE;
	FPLGRAPH g{ FPLGM_FLAGS, id };
	g.flags = show ? m_State->plot.graphs[i].flags & ~FplGraphHidden : m_State->plot.graphs[i].flags | FplGraphHidden;
	return SetGraph(g);
}

// ---- Parameters ----------------------------------------------------------------------

BOOL CFunctionPlotCtrl::SetParameter(const FPLPARAM& param) {
	if (!param.pszName || !*param.pszName)
		return FALSE;
	auto& plot = m_State->plot;
	int i = plot.FindParam(param.pszName);
	bool added = i < 0;
	if (added) {
		plot.params.push_back({});
		i = (int)plot.params.size() - 1;
		plot.params[i].name = param.pszName;
	}
	auto& p = plot.params[i];
	p.min = std::isfinite(param.min) ? param.min : -10;
	p.max = std::isfinite(param.max) && param.max > p.min ? param.max : p.min + 1;
	p.step = std::isfinite(param.step) && param.step > 0 ? param.step : 0;
	p.value = std::isfinite(param.value) ? param.value : 0;
	p.flags = param.flags;
	if (added)
		Rebuild();
	else {
		ValuesChanged();
		LayoutPanel();
	}
	return TRUE;
}

BOOL CFunctionPlotCtrl::GetParameter(LPCWSTR pszName, FPLPARAM& param) const {
	int i = pszName ? m_State->plot.FindParam(pszName) : -1;
	return i >= 0 && GetParameterAt(i, param);
}

BOOL CFunctionPlotCtrl::GetParameterAt(int index, FPLPARAM& param) const {
	auto& params = m_State->plot.params;
	if (index < 0 || index >= (int)params.size())
		return FALSE;
	auto& p = params[index];
	param.pszName = p.name.c_str();
	param.value = p.value;
	param.min = p.min;
	param.max = p.max;
	param.step = p.step;
	param.flags = p.flags;
	return TRUE;
}

BOOL CFunctionPlotCtrl::SetParameterValue(LPCWSTR pszName, double value) {
	int i = pszName ? m_State->plot.FindParam(pszName) : -1;
	if (i < 0 || !std::isfinite(value))
		return FALSE;
	m_State->plot.params[i].value = value;
	ValuesChanged();
	return TRUE;
}

double CFunctionPlotCtrl::GetParameterValue(LPCWSTR pszName) const {
	int i = pszName ? m_State->plot.FindParam(pszName) : -1;
	return i < 0 ? std::numeric_limits<double>::quiet_NaN() : m_State->plot.params[i].value;
}

BOOL CFunctionPlotCtrl::RemoveParameter(LPCWSTR pszName) {
	int i = pszName ? m_State->plot.FindParam(pszName) : -1;
	if (i < 0)
		return FALSE;
	m_State->plot.params.erase(m_State->plot.params.begin() + i);
	Rebuild();
	return TRUE;
}

int CFunctionPlotCtrl::GetParameterCount() const {
	return (int)m_State->plot.params.size();
}

void CFunctionPlotCtrl::SetDegrees(BOOL degrees) {
	if (m_State->plot.degrees == (degrees != FALSE))
		return;
	m_State->plot.degrees = degrees != FALSE;
	Rebuild();
}

double CFunctionPlotCtrl::GetValue(LPCWSTR pszName) const {
	return pszName ? m_State->plot.ValueOf(pszName) : std::numeric_limits<double>::quiet_NaN();
}

BOOL CFunctionPlotCtrl::SetList(LPCWSTR pszName, const double* values, int count) {
	if (!pszName || !*pszName || count < 0 || (count > 0 && !values))
		return FALSE;
	auto& lists = m_State->plot.lists;
	auto it = std::find_if(lists.begin(), lists.end(), [&](const FplList& l) { return l.name == pszName; });
	if (it == lists.end()) {
		lists.push_back({ pszName, std::vector<double>(values, values + count) });
		Rebuild();
	}
	else {
		it->values.assign(values, values + count);
		ValuesChanged();
	}
	return TRUE;
}

int CFunctionPlotCtrl::GetList(LPCWSTR pszName, double* values, int max) const {
	auto* list = pszName ? m_State->plot.ListValues(pszName) : nullptr;
	if (!list)
		return 0;
	for (int i = 0; values && i < max && i < (int)list->size(); i++)
		values[i] = (*list)[i];
	return (int)list->size();
}

BOOL CFunctionPlotCtrl::RemoveList(LPCWSTR pszName) {
	auto& lists = m_State->plot.lists;
	auto it = std::find_if(lists.begin(), lists.end(), [&](const FplList& l) { return pszName && l.name == pszName; });
	if (it == lists.end())
		return FALSE;
	lists.erase(it);
	Rebuild();
	return TRUE;
}

int CFunctionPlotCtrl::GetListCount() const {
	return (int)m_State->plot.lists.size();
}

LPCWSTR CFunctionPlotCtrl::GetListName(int index) const {
	auto& lists = m_State->plot.lists;
	return index >= 0 && index < (int)lists.size() ? lists[index].name.c_str() : nullptr;
}

BOOL CFunctionPlotCtrl::GetDegrees() const {
	return m_State->plot.degrees;
}

// ---- Building and sampling -------------------------------------------------------------

void CFunctionPlotCtrl::Rebuild() {
	auto& s = *m_State;
	// the graphs change: what's being sampled for the old ones isn't wanted
	if (s.worker)
		s.worker->Cancel();
	s.generations.assign(s.plot.graphs.size(), 0);
	s.plot.Build();
	if (s.traceGraph >= (int)s.plot.graphs.size() || s.featuresGraph >= (int)s.plot.graphs.size()) {
		s.traceGraph = s.featuresGraph = -1;
		s.traceValid = false;
	}
	Resample();
	LayoutPanel();
	if (IsWindow())
		Invalidate();
}

void CFunctionPlotCtrl::ValuesChanged() {
	auto& s = *m_State;
	s.plot.UpdateValues();
	Resample();
	// the traced point follows its function (in 3D it's let go: the surface moved under it)
	if (s.traceValid && s.mode3D) {
		s.traceValid = false;
	}
	else if (s.traceValid && s.traceGraph >= 0 && s.traceFeature < 0) {
		auto kind = s.plot.graphs[s.traceGraph].expr.kind;
		if (kind == FplKind::Surface)
			s.traceZ = s.plot.EvalSurface(s.traceGraph, s.traceX, s.traceY);
		else if (kind == FplKind::Function)
			s.traceY = s.plot.Eval(s.traceGraph, s.traceX);
		else if (kind == FplKind::FunctionOfY)
			s.traceX = s.plot.Eval(s.traceGraph, s.traceY);
		else
			s.traceValid = false;
		if (!std::isfinite(s.traceX) || !std::isfinite(s.traceY))
			s.traceValid = false;
	}
	else if (s.traceFeature >= 0) {
		s.traceValid = false;
	}
	LayoutPanel();
	if (IsWindow())
		Invalidate();
}

void CFunctionPlotCtrl::Resample() {
	auto& s = *m_State;
	const bool async = s.worker && !s.syncSampling;
	s.generations.resize(s.plot.graphs.size(), 0);
	// a heavy graph: a coarse version now, the full one from the thread (each job numbered, so only the latest's
	// result is taken)
	auto submit = [&](int i, bool mesh) {
		FplJob job;
		job.graph = i;
		job.generation = s.generations[i] = s.nextGeneration++;
		job.expr = s.plot.graphs[i].expr;
		job.slots = s.plot.Slots();
		job.view = s.view;
		job.options = s.plot.OptionsFor(i, {});
		job.mesh = mesh;
		job.box = s.box;
		s.worker->Submit(std::move(job));
	};
	if (s.mode3D) {
		for (size_t i = 0; i < s.plot.graphs.size(); i++) {
			if (async && s.plot.IsHeavy((int)i, true)) {
				s.plot.Build3DGraph((int)i, s.box, 16);
				submit((int)i, true);
			}
			else {
				s.generations[i] = 0;
				s.plot.Build3DGraph((int)i, s.box);
			}
		}
		s.gpuStale = true;
		return;
	}
	if (!s.viewSet)
		return;
	FplSampleOptions coarse;
	coarse.cellSize = 16;
	coarse.cellDepth = 1;
	coarse.heatCell = 12;
	for (size_t i = 0; i < s.plot.graphs.size(); i++) {
		if (async && s.plot.IsHeavy((int)i, false)) {
			s.plot.SampleGraph((int)i, s.view, coarse);
			submit((int)i, false);
		}
		else {
			s.generations[i] = 0;
			s.plot.SampleGraph((int)i, s.view);
		}
	}
	if (s.featuresGraph >= 0 && !(GetStyle() & FPLS_NOTRACE))
		s.plot.FindFeatures(s.featuresGraph, s.view);
	BuildGeometries();
}

void CFunctionPlotCtrl::ViewChanged(bool notify) {
	Resample();
	if (IsWindow())
		Invalidate();
	if (notify)
		NotifyView();
}

// ---- The view --------------------------------------------------------------------------

void CFunctionPlotCtrl::SetViewport(double xMin, double xMax, double yMin, double yMax) {
	auto& v = m_State->view;
	FplViewport next = v;
	next.xMin = xMin;
	next.xMax = xMax;
	next.yMin = yMin;
	next.yMax = yMax;
	next.FixLogRanges();
	if (!next.Valid())
		return;
	if (IsWindow() && (GetStyle() & FPLS_EQUALSCALE))
		next.EqualScale();
	v = next;
}

void CFunctionPlotCtrl::SetView(const FPLVIEW& view) {
	SetViewport(view.xMin, view.xMax, view.yMin, view.yMax);
	ViewChanged(false);
}

FPLVIEW CFunctionPlotCtrl::GetView() const {
	auto& v = m_State->view;
	return { v.xMin, v.xMax, v.yMin, v.yMax };
}

void CFunctionPlotCtrl::SetLogAxes(BOOL logX, BOOL logY) {
	auto& s = *m_State;
	if (s.view.logX == (logX != FALSE) && s.view.logY == (logY != FALSE))
		return;
	s.view.logX = logX != FALSE;
	s.view.logY = logY != FALSE;
	s.view.FixLogRanges();
	if (!s.view.logX && !s.view.logY && IsWindow() && (GetStyle() & FPLS_EQUALSCALE))
		s.view.EqualScale();
	ClearTrace();
	s.featuresGraph = -1;
	s.history.Clear();		// the snapshots' ranges were for the other scales
	if (IsWindow() && s.viewSet)
		ViewChanged(true);
}

BOOL CFunctionPlotCtrl::IsLogX() const {
	return m_State->view.logX;
}

BOOL CFunctionPlotCtrl::IsLogY() const {
	return m_State->view.logY;
}

void CFunctionPlotCtrl::SetHomeView(const FPLVIEW* view) {
	m_State->homeSet = view != nullptr;
	if (view)
		m_State->home = *view;
}

void CFunctionPlotCtrl::ResetView() {
	auto& s = *m_State;
	if (s.homeSet) {
		SetViewport(s.home.xMin, s.home.xMax, s.home.yMin, s.home.yMax);
	}
	else {
		// x from -10 to 10, y at the same scale around 0
		double half = 10 * s.view.height / std::max(s.view.width, 1.0);
		SetViewport(-10, 10, -half, half);
	}
	ViewChanged(false);
}

void CFunctionPlotCtrl::Zoom(double factor) {
	auto& v = m_State->view;
	if (!(factor > 0))
		return;
	v.Zoom(factor, factor, { v.width / 2, v.height / 2 });
	ViewChanged(false);
}

BOOL CFunctionPlotCtrl::ClientToPlane(POINT pt, double& x, double& y) const {
	auto& v = m_State->view;
	if (!v.Valid())
		return FALSE;
	x = v.ToPlaneX(ToDips(pt.x));
	y = v.ToPlaneY(ToDips(pt.y));
	return TRUE;
}

POINT CFunctionPlotCtrl::PlaneToClient(double x, double y) const {
	auto& v = m_State->view;
	const double scale = m_State->dpi / 96.0;
	auto clampToInt = [](double d) { return (LONG)std::clamp(std::lround(d), -1000000L, 1000000L); };
	return { clampToInt(v.ToScreenX(x) * scale), clampToInt(v.ToScreenY(y) * scale) };
}

void CFunctionPlotCtrl::NotifyView() {
	if (!IsWindow())
		return;
	NMFPLVIEW nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_VIEWCHANGED };
	nm.view = GetView();
	::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

// ---- 3D --------------------------------------------------------------------------------

BOOL CFunctionPlotCtrl::SetMode(FPLMODE mode) {
	auto& s = *m_State;
	const bool to3D = mode == FPLM_3D;
	if (to3D == s.mode3D)
		return TRUE;
	ClearTrace();
	s.drag = FplDrag::None;
	// a window has one way of presenting at a time: the Direct2D target or the swap chain
	s.brushes.clear();
	if (to3D) {
		DiscardTarget();
		s.mode3D = true;
		if (IsWindow() && !Ensure3D()) {
			s.mode3D = false;
			return FALSE;
		}
		Resample();
	}
	else {
		s.gpu.clear();
		s.boxEdges = s.floorGrid = {};
		s.renderer.Reset();
		if (s.view3D) {
			::DestroyWindow(s.view3D);
			s.view3D = nullptr;
		}
		s.mode3D = false;
		for (auto& g : s.plot.graphs)
			g.mesh = {};
		Resample();
	}
	if (IsWindow())
		Invalidate();
	return TRUE;
}

BOOL CFunctionPlotCtrl::Invalidate(BOOL bErase) {
	if (m_State && m_State->view3D)
		::InvalidateRect(m_State->view3D, nullptr, FALSE);
	return CWindow::Invalidate(bErase);
}

LRESULT CFunctionPlotCtrl::OnSampled(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	if (!s.worker)
		return 0;
	bool changed = false;
	for (auto& r : s.worker->Take()) {
		if (r.graph < 0 || r.graph >= (int)s.plot.graphs.size() || r.graph >= (int)s.generations.size() ||
			s.generations[r.graph] != r.generation || r.mesh != s.mode3D)
			continue;
		changed = true;
		if (r.mesh) {
			s.plot.graphs[r.graph].mesh = std::move(r.triangles);
			if (s.renderer.Ready() && r.graph < (int)s.gpu.size() && !s.gpuStale)
				s.gpu[r.graph] = s.renderer.Create(s.plot.graphs[r.graph].mesh);
			else
				s.gpuStale = true;
		}
		else {
			s.plot.graphs[r.graph].geometry = std::move(r.geometry);
			BuildGeometry(r.graph);
		}
	}
	if (changed) {
		// the features come from what was sampled: now the full version
		if (!s.mode3D && s.featuresGraph >= 0 && s.featuresGraph < (int)s.plot.graphs.size())
			s.plot.FindFeatures(s.featuresGraph, s.view);
		Invalidate();
	}
	return 0;
}

LRESULT CFunctionPlotCtrl::OnPaint3D(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_State->mode3D)
		Paint3D(true);
	return 0;
}

FPLMODE CFunctionPlotCtrl::GetMode() const {
	return m_State->mode3D ? FPLM_3D : FPLM_2D;
}

void CFunctionPlotCtrl::SetView3D(const FPLVIEW3D& view) {
	auto& s = *m_State;
	FplBox3 box{ view.xMin, view.xMax, view.yMin, view.yMax, view.zMin, view.zMax };
	if (!box.Valid())
		return;
	s.box = box;
	s.camera.yaw = 0;
	s.camera.pitch = 0;
	s.camera.Orbit(view.yaw, view.pitch);
	if (s.mode3D) {
		Resample();
		if (IsWindow())
			Invalidate();
	}
}

FPLVIEW3D CFunctionPlotCtrl::GetView3D() const {
	auto& s = *m_State;
	return { s.box.xMin, s.box.xMax, s.box.yMin, s.box.yMax, s.box.zMin, s.box.zMax, s.camera.yaw, s.camera.pitch };
}

void CFunctionPlotCtrl::ResetView3D() {
	auto& s = *m_State;
	s.box = {};
	s.camera = {};
	if (s.mode3D) {
		Resample();
		if (IsWindow())
			Invalidate();
	}
}

BOOL CFunctionPlotCtrl::IsSoftwareRendering() const {
	return m_State->renderer.Ready() && m_State->renderer.IsWarp();
}

void CFunctionPlotCtrl::NotifyView3D() {
	if (!IsWindow())
		return;
	NMFPLVIEW3D nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_VIEW3DCHANGED };
	nm.view = GetView3D();
	::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

// ---- Fitting, undo, axis titles ---------------------------------------------------------

BOOL CFunctionPlotCtrl::ZoomToFit() {
	auto& s = *m_State;
	double x0, x1, y0, y1;
	if (s.mode3D || !s.view.Valid() || !s.plot.FitBounds(s.view, x0, x1, y0, y1))
		return FALSE;
	SetViewport(x0, x1, y0, y1);
	ViewChanged(true);
	return TRUE;
}

FplSnapshot CFunctionPlotCtrl::TakeSnapshot() const {
	auto& s = *m_State;
	FplSnapshot snap;
	for (auto& p : s.plot.params)
		snap.params.emplace_back(p.name, p.value);
	for (auto& g : s.plot.graphs)
		snap.hidden.emplace_back(g.id, (g.flags & FplGraphHidden) != 0);
	const double view[] = { s.view.xMin, s.view.xMax, s.view.yMin, s.view.yMax };
	std::copy(std::begin(view), std::end(view), snap.view);
	const double box[] = { s.box.xMin, s.box.xMax, s.box.yMin, s.box.yMax, s.box.zMin, s.box.zMax };
	std::copy(std::begin(box), std::end(box), snap.box);
	snap.yaw = s.camera.yaw;
	snap.pitch = s.camera.pitch;
	return snap;
}

void CFunctionPlotCtrl::Remember(int kind) {
	auto& s = *m_State;
	const ULONGLONG now = ::GetTickCount64();
	if (kind < 0 || kind != s.lastKind || now - s.lastTime > 700)
		s.history.Push(TakeSnapshot());
	s.lastKind = kind;
	s.lastTime = now;
}

void CFunctionPlotCtrl::Restore(const FplSnapshot& snap) {
	auto& s = *m_State;
	s.lastKind = -1;
	// the parameters by name: each that moves is notified
	std::vector<int> moved;
	for (auto& [name, value] : snap.params) {
		int i = s.plot.FindParam(name);
		if (i >= 0 && s.plot.params[i].value != value) {
			s.plot.params[i].value = value;
			moved.push_back(i);
		}
	}
	// the graphs shown or hidden, by id
	std::vector<unsigned> toggled;
	for (auto& [id, hidden] : snap.hidden) {
		int i = s.plot.FindGraph(id);
		if (i >= 0 && ((s.plot.graphs[i].flags & FplGraphHidden) != 0) != hidden) {
			s.plot.graphs[i].flags ^= FplGraphHidden;
			toggled.push_back(id);
		}
	}
	FplViewport v = s.view;
	v.xMin = snap.view[0], v.xMax = snap.view[1], v.yMin = snap.view[2], v.yMax = snap.view[3];
	const bool viewMoved = v.xMin != s.view.xMin || v.xMax != s.view.xMax || v.yMin != s.view.yMin || v.yMax != s.view.yMax;
	if (v.Valid())
		s.view = v;
	FplBox3 box{ snap.box[0], snap.box[1], snap.box[2], snap.box[3], snap.box[4], snap.box[5] };
	const bool box3D = box.Valid() && std::memcmp(&box, &s.box, sizeof(box)) != 0;
	const bool camera = snap.yaw != s.camera.yaw || snap.pitch != s.camera.pitch;
	if (box.Valid())
		s.box = box;
	s.camera.yaw = snap.yaw;
	s.camera.pitch = snap.pitch;
	ClearTrace();
	if (!toggled.empty())
		Rebuild();
	else if (!moved.empty())
		ValuesChanged();
	else
		Resample();
	Invalidate();
	for (int i : moved) {
		NMFPLPARAM nm{};
		nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_PARAMCHANGED };
		nm.pszName = s.plot.params[i].name.c_str();
		nm.value = s.plot.params[i].value;
		::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
	}
	for (unsigned id : toggled) {
		NMFPLGRAPH nm{};
		nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_GRAPHTOGGLED };
		nm.id = id;
		int i = s.plot.FindGraph(id);
		nm.hidden = i >= 0 && (s.plot.graphs[i].flags & FplGraphHidden);
		::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
	}
	if (viewMoved)
		NotifyView();
	if (box3D || camera)
		NotifyView3D();
}

BOOL CFunctionPlotCtrl::Undo() {
	FplSnapshot to;
	if (!m_State->history.Undo(TakeSnapshot(), to))
		return FALSE;
	Restore(to);
	return TRUE;
}

BOOL CFunctionPlotCtrl::Redo() {
	FplSnapshot to;
	if (!m_State->history.Redo(TakeSnapshot(), to))
		return FALSE;
	Restore(to);
	return TRUE;
}

BOOL CFunctionPlotCtrl::CanUndo() const {
	return m_State->history.CanUndo();
}

BOOL CFunctionPlotCtrl::CanRedo() const {
	return m_State->history.CanRedo();
}

void CFunctionPlotCtrl::ClearHistory() {
	m_State->history.Clear();
	m_State->lastKind = -1;
}

void CFunctionPlotCtrl::SetAxisTitles(LPCWSTR pszX, LPCWSTR pszY, LPCWSTR pszZ) {
	auto& s = *m_State;
	s.axisTitles[0] = pszX ? pszX : L"";
	s.axisTitles[1] = pszY ? pszY : L"";
	s.axisTitles[2] = pszZ ? pszZ : L"";
	if (IsWindow())
		Invalidate();
}

LPCWSTR CFunctionPlotCtrl::GetAxisTitle(int axis) const {
	return axis >= 0 && axis < 3 ? m_State->axisTitles[axis].c_str() : nullptr;
}

// ---- Tracing ---------------------------------------------------------------------------

BOOL CFunctionPlotCtrl::GetTrace(FPLTRACE& trace) const {
	auto& s = *m_State;
	trace = {};
	trace.feature = FPLF_NONE;
	if (!s.traceValid || s.traceGraph < 0)
		return FALSE;
	auto& g = s.plot.graphs[s.traceGraph];
	trace.id = g.id;
	trace.valid = TRUE;
	trace.x = s.traceX;
	trace.y = s.traceY;
	trace.hasZ = std::isfinite(s.traceZ);
	trace.z = trace.hasZ ? s.traceZ : 0;
	if (!s.mode3D) {
		double slope = s.plot.Slope(s.traceGraph, g.expr.kind == FplKind::FunctionOfY ? s.traceY : s.traceX);
		trace.hasSlope = std::isfinite(slope);
		trace.slope = trace.hasSlope ? slope : 0;
	}
	if (s.traceFeature >= 0 && s.traceFeature < (int)g.features.size())
		trace.feature = (FPLFEATUREKIND)g.features[s.traceFeature].kind;
	return TRUE;
}

void CFunctionPlotCtrl::ClearTrace() {
	auto& s = *m_State;
	bool had = s.traceValid || s.featuresGraph >= 0;
	if (s.featuresGraph >= 0 && s.featuresGraph < (int)s.plot.graphs.size())
		s.plot.graphs[s.featuresGraph].features.clear();
	s.featuresGraph = -1;
	s.traceDragged = false;
	SetTraceHit(-1, -1, 0, 0, false);
	if (had && IsWindow())
		Invalidate();
}

int CFunctionPlotCtrl::GetFeatures(UINT id, FPLFEATURE* features, int max) const {
	auto& s = *m_State;
	int i = s.plot.FindGraph(id);
	if (i < 0)
		return 0;
	// found for the asking (the traced graph's are kept; others are found here and dropped)
	FplPlot& plot = const_cast<FplPlot&>(s.plot);
	std::vector<FplFeature> saved = plot.graphs[i].features;
	if (i != s.featuresGraph)
		plot.FindFeatures(i, s.view);
	auto& found = plot.graphs[i].features;
	int n = (int)found.size();
	for (int k = 0; k < n && k < max && features; k++)
		features[k] = { (FPLFEATUREKIND)found[k].kind, found[k].x, found[k].y };
	if (i != s.featuresGraph)
		plot.graphs[i].features = std::move(saved);
	return n;
}

void CFunctionPlotCtrl::SetTraceHit(int graph, int feature, double x, double y, bool valid, double z) {
	auto& s = *m_State;
	auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
	if (s.traceGraph == graph && s.traceFeature == feature && s.traceValid == valid &&
		(!valid || (s.traceX == x && s.traceY == y && same(s.traceZ, z))))
		return;
	s.traceGraph = graph;
	s.traceFeature = feature;
	s.traceX = x;
	s.traceY = y;
	s.traceZ = z;
	s.traceValid = valid;
	NotifyTrace();
	if (IsWindow())
		Invalidate();
}

void CFunctionPlotCtrl::NotifyTrace() {
	if (!IsWindow())
		return;
	NMFPLTRACE nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_TRACE };
	GetTrace(nm.trace);
	::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

BOOL CFunctionPlotCtrl::StartTrace() {
	auto& s = *m_State;
	if (s.mode3D || (GetStyle() & FPLS_NOTRACE) || !s.view.Valid())
		return FALSE;
	const double x = (s.view.xMin + s.view.xMax) / 2;
	FplTraceHit hit = s.plot.NextGraphAt(-1, x, 1, s.view);
	if (!hit)
		return FALSE;
	ApplyTraceHit(hit);
	return TRUE;
}

void CFunctionPlotCtrl::ApplyTraceHit(const FplTraceHit& hit) {
	auto& s = *m_State;
	if (!hit)
		return;
	if (hit.graph != s.featuresGraph) {
		if (s.featuresGraph >= 0 && s.featuresGraph < (int)s.plot.graphs.size())
			s.plot.graphs[s.featuresGraph].features.clear();
		s.featuresGraph = hit.graph;
		s.plot.FindFeatures(hit.graph, s.view);
	}
	// pinned: the mouse moving doesn't take it away
	s.traceDragged = true;
	// kept in view: the view follows a point that would leave it
	FplPoint p = s.view.ToScreen(hit.plane);
	double dx = 0, dy = 0;
	if (p.x < 0 || p.x > s.view.width)
		dx = s.view.width / 2 - p.x;
	if (p.y < 0 || p.y > s.view.height)
		dy = s.view.height / 2 - p.y;
	if ((dx != 0 || dy != 0) && !(GetStyle() & FPLS_NOPANZOOM)) {
		s.view.Pan(dx, dy);
		ViewChanged(true);
		if (s.featuresGraph >= 0)
			s.plot.FindFeatures(s.featuresGraph, s.view);
	}
	SetTraceHit(hit.graph, hit.feature, hit.plane.x, hit.plane.y, true, hit.z);
	Invalidate();
}

bool CFunctionPlotCtrl::TraceKey(UINT vk) {
	auto& s = *m_State;
	if (s.mode3D || (GetStyle() & FPLS_NOTRACE))
		return false;
	if (vk == 'T' && !s.traceValid)
		return StartTrace() != FALSE;
	if (!s.traceValid || s.traceGraph < 0 || s.traceGraph >= (int)s.plot.graphs.size())
		return false;
	const bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0;
	const FplPoint at = { s.traceX, s.traceY };
	FplTraceHit hit;
	switch (vk) {
		case VK_LEFT: case VK_RIGHT: {
			const int dir = vk == VK_RIGHT ? 1 : -1;
			if (ctrl)
				hit = s.plot.NextFeature(s.traceGraph, s.traceX, dir, s.view, s.traceY);
			else
				hit = s.plot.Step(s.traceGraph, at, dir * (shift ? 1.0 : 4.0), s.view);
			break;
		}
		case VK_UP: case VK_DOWN:
			hit = s.plot.NextGraphAt(s.traceGraph, s.traceX, vk == VK_UP ? 1 : -1, s.view);
			break;
		default:
			return false;
	}
	ApplyTraceHit(hit);
	return true;
}

void CFunctionPlotCtrl::UpdateTrace(POINT pt, bool dragging) {
	auto& s = *m_State;
	if (GetStyle() & FPLS_NOTRACE)
		return;
	FplPoint p = { ToDips(pt.x), ToDips(pt.y) };
	if (s.mode3D) {
		// what the ray through the mouse meets first
		FplVec3 origin, dir;
		s.camera.Ray(p, ToDips(s.client.cx), ToDips(s.client.cy), origin, dir);
		FplTraceHit hit = s.plot.Pick3D(origin, dir, s.box);
		if (hit)
			SetTraceHit(hit.graph, -1, hit.plane.x, hit.plane.y, true, hit.z);
		else
			SetTraceHit(-1, -1, 0, 0, false);
		return;
	}
	FplTraceHit hit;
	if (dragging)
		hit = s.plot.TraceAt(s.traceGraph, p, s.view, FplSnapDistance);
	else
		hit = s.plot.Nearest(p, s.view, FplTraceDistance, FplSnapDistance, true);
	if (hit && hit.graph != s.featuresGraph) {
		// a new graph: its features found, the last one's dropped
		if (s.featuresGraph >= 0 && s.featuresGraph < (int)s.plot.graphs.size())
			s.plot.graphs[s.featuresGraph].features.clear();
		s.featuresGraph = hit.graph;
		s.plot.FindFeatures(hit.graph, s.view);
		hit = dragging ? s.plot.TraceAt(hit.graph, p, s.view, FplSnapDistance)
			: s.plot.Nearest(p, s.view, FplTraceDistance, FplSnapDistance, true);
		Invalidate();
	}
	if (hit)
		SetTraceHit(hit.graph, hit.feature, hit.plane.x, hit.plane.y, true, hit.z);
	else if (!dragging && !s.traceDragged) {
		if (s.featuresGraph >= 0) {
			s.plot.graphs[s.featuresGraph].features.clear();
			s.featuresGraph = -1;
			Invalidate();
		}
		SetTraceHit(-1, -1, 0, 0, false);
	}
}

// ---- The panel -------------------------------------------------------------------------

FplPanelHit CFunctionPlotCtrl::HitPanel(POINT pt) const {
	auto& s = *m_State;
	FplPanelHit hit;
	float x = ToDips(pt.x), y = ToDips(pt.y);
	if (s.toggle.right > s.toggle.left && x >= s.toggle.left && x < s.toggle.right && y >= s.toggle.top &&
		y < s.toggle.bottom) {
		hit.part = FplPanelPart::Toggle;
		return hit;
	}
	if (s.panel.right <= s.panel.left || x < s.panel.left || x >= s.panel.right || y < s.panel.top ||
		y >= s.panel.bottom)
		return hit;
	hit.part = FplPanelPart::Panel;
	for (size_t i = 0; i < s.rows.size(); i++) {
		auto& r = s.rows[i];
		if (y >= r.rect.top && y < r.rect.bottom) {
			hit.part = r.kind == FplPanelRow::Graph ? FplPanelPart::Graph : FplPanelPart::Slider;
			if (r.kind == FplPanelRow::Slider && x >= r.play.left && x < r.play.right)
				hit.part = FplPanelPart::Play;
			hit.row = (int)i;
			break;
		}
	}
	return hit;
}

void CFunctionPlotCtrl::DragSlider(int row, float x) {
	auto& s = *m_State;
	if (row < 0 || row >= (int)s.rows.size() || s.rows[row].kind != FplPanelRow::Slider)
		return;
	auto& r = s.rows[row];
	auto& p = s.plot.params[r.index];
	float width = r.track.right - r.track.left;
	double t = width > 0 ? std::clamp((x - r.track.left) / width, 0.0f, 1.0f) : 0;
	SetParam(r.index, p.min + t * (p.max - p.min), true);
}

void CFunctionPlotCtrl::SetParam(int param, double value, bool snap) {
	auto& s = *m_State;
	auto& p = s.plot.params[param];
	if (snap && p.step > 0)
		value = p.min + std::round((value - p.min) / p.step) * p.step;
	value = std::clamp(value, p.min, p.max);
	if (value == p.value)
		return;
	p.value = value;
	ValuesChanged();
	NMFPLPARAM nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_PARAMCHANGED };
	nm.pszName = p.name.c_str();
	nm.value = value;
	::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

int CFunctionPlotCtrl::NextSlider(bool back) const {
	auto& s = *m_State;
	const int n = (int)s.rows.size();
	if (back) {
		if (s.focusRow < 0)
			return -2;
		for (int r = s.focusRow - 1; r >= 0; r--)
			if (s.rows[r].kind == FplPanelRow::Slider)
				return r;
		return -1;
	}
	for (int r = s.focusRow + 1; r < n; r++)
		if (s.rows[r].kind == FplPanelRow::Slider)
			return r;
	return -2;
}

// ---- Animation -------------------------------------------------------------------------

BOOL CFunctionPlotCtrl::AnimateParameter(LPCWSTR pszName, BOOL play) {
	auto& s = *m_State;
	int i = pszName ? s.plot.FindParam(pszName) : -1;
	if (i < 0)
		return FALSE;
	s.plot.params[i].animating = play != FALSE;
	bool any = false;
	for (auto& p : s.plot.params)
		any = any || p.animating;
	if (IsWindow()) {
		if (any) {
			s.lastTick = ::GetTickCount64();
			SetTimer(AnimationTimer, 16);
		}
		else
			KillTimer(AnimationTimer);
		Invalidate();
	}
	return TRUE;
}

BOOL CFunctionPlotCtrl::IsParameterAnimating(LPCWSTR pszName) const {
	int i = pszName ? m_State->plot.FindParam(pszName) : -1;
	return i >= 0 && m_State->plot.params[i].animating;
}

LRESULT CFunctionPlotCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& s = *m_State;
	if (wParam != AnimationTimer) {
		bHandled = FALSE;
		return 0;
	}
	const ULONGLONG now = ::GetTickCount64();
	// at most a tenth of a second a step, so a stall doesn't jump
	const double dt = std::min(0.1, (now - s.lastTick) / 1000.0);
	s.lastTick = now;
	bool any = false;
	for (size_t i = 0; i < s.plot.params.size(); i++) {
		auto& p = s.plot.params[i];
		if (!p.animating)
			continue;
		any = true;
		// the range in 4 seconds, back and forth
		double v = p.value + p.direction * (p.max - p.min) * dt / 4;
		if (v >= p.max) {
			v = p.max;
			p.direction = -1;
		}
		else if (v <= p.min) {
			v = p.min;
			p.direction = 1;
		}
		SetParam((int)i, v, false);
	}
	if (!any)
		KillTimer(AnimationTimer);
	return 0;
}

// ---- State -----------------------------------------------------------------------------

CString CFunctionPlotCtrl::GetState() const {
	auto& s = *m_State;
	FplSavedState st;
	st.graphs.reserve(s.plot.graphs.size());
	for (auto& g : s.plot.graphs) {
		FplGraph copy;
		copy.id = g.id;
		copy.text = g.text;
		copy.color = g.color;
		copy.autoColor = g.autoColor;
		copy.width = g.width;
		copy.flags = g.flags;
		copy.hasRange = g.hasRange;
		copy.tMin = g.tMin;
		copy.tMax = g.tMax;
		copy.opacity = g.opacity;
		copy.fillOpacity = g.fillOpacity;
		copy.label = g.label;
		st.graphs.push_back(std::move(copy));
	}
	st.params = s.plot.params;
	st.lists = s.plot.lists;
	st.degrees = s.plot.degrees;
	st.mode3D = s.mode3D;
	st.collapsed = s.collapsed;
	st.view = s.view;
	st.box = s.box;
	st.yaw = s.camera.yaw;
	st.pitch = s.camera.pitch;
	for (int k = 0; k < 3; k++)
		st.axisTitles[k] = s.axisTitles[k];
	return FplSaveState(st).c_str();
}

BOOL CFunctionPlotCtrl::SetState(LPCWSTR pszState) {
	auto& s = *m_State;
	FplSavedState st;
	if (!pszState || !FplLoadState(pszState, st))
		return FALSE;
	if (IsWindow())
		KillTimer(AnimationTimer);
	ClearTrace();
	s.plot.graphs = std::move(st.graphs);
	s.plot.params = std::move(st.params);
	s.plot.lists = std::move(st.lists);
	s.plot.degrees = st.degrees;
	s.collapsed = st.collapsed;
	for (int k = 0; k < 3; k++)
		s.axisTitles[k] = st.axisTitles[k];
	s.history.Clear();
	s.box = st.box;
	s.camera.yaw = 0;
	s.camera.pitch = 0;
	s.camera.Orbit(st.yaw, st.pitch);
	s.featuresGraph = -1;
	s.view.logX = st.view.logX;
	s.view.logY = st.view.logY;
	if (s.viewSet)
		SetViewport(st.view.xMin, st.view.xMax, st.view.yMin, st.view.yMax);
	else {
		s.view.xMin = st.view.xMin;
		s.view.xMax = st.view.xMax;
		s.view.yMin = st.view.yMin;
		s.view.yMax = st.view.yMax;
		s.pendingView = true;
	}
	if (!SetMode(st.mode3D ? FPLM_3D : FPLM_2D))
		SetMode(FPLM_2D);
	Rebuild();
	return TRUE;
}

// ---- The panel's button ----------------------------------------------------------------

void CFunctionPlotCtrl::SetPanelCollapsed(BOOL collapsed) {
	m_State->collapsed = collapsed != FALSE;
	LayoutPanel();
	if (IsWindow())
		Invalidate();
}

BOOL CFunctionPlotCtrl::IsPanelCollapsed() const {
	return m_State->collapsed;
}

// ---- Export ----------------------------------------------------------------------------

namespace {

// A PNG of the pixels (premultiplied BGRA) into a stream.
bool EncodePng(IWICImagingFactory* wic, IStream* stream, const std::vector<uint32_t>& pixels, UINT width, UINT height) {
	ComPtr<IWICBitmap> bitmap;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	return SUCCEEDED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
			width * height * 4, (BYTE*)pixels.data(), bitmap.GetAddressOf())) &&
		SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
		SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
		SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr)) &&
		SUCCEEDED(frame->Initialize(nullptr)) &&
		SUCCEEDED(frame->SetSize(width, height)) &&
		SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
		SUCCEEDED(frame->Commit()) &&
		SUCCEEDED(encoder->Commit());
}

ComPtr<IWICImagingFactory> Wic() {
	ComPtr<IWICImagingFactory> wic;
	::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
	return wic;
}

// Bytes as a movable global for the clipboard.
HGLOBAL ToGlobal(const void* data, size_t size) {
	HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, size);
	if (!h)
		return nullptr;
	if (void* p = ::GlobalLock(h)) {
		memcpy(p, data, size);
		::GlobalUnlock(h);
		return h;
	}
	::GlobalFree(h);
	return nullptr;
}

}  // namespace

bool CFunctionPlotCtrl::RenderImage(float scale, std::vector<uint32_t>& pixels, UINT& width, UINT& height) {
	auto& s = *m_State;
	if (!s.view.Valid())
		return false;
	// everything at full quality, now: not the coarse versions the thread is refining
	s.syncSampling = true;
	Resample();
	s.syncSampling = false;

	if (s.mode3D) {
		// the frame as it's shown: drawn, and read back before it's presented
		Paint3D(false);
		bool captured = s.renderer.Ready() && s.renderer.Capture(pixels, width, height);
		if (s.renderer.Ready())
			s.renderer.Present();
		return captured;
	}

	auto wic = Wic();
	if (!wic)
		return false;
	scale = std::clamp(scale, 0.25f, 8.0f);
	width = (UINT)std::ceil(s.view.width * scale);
	height = (UINT)std::ceil(s.view.height * scale);
	ComPtr<IWICBitmap> bitmap;
	ComPtr<ID2D1RenderTarget> target;
	if (FAILED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())))
		return false;
	const float dpi = 96 * scale;
	auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
	if (FAILED(FplD2DFactory()->CreateWicBitmapRenderTarget(bitmap.Get(), props, target.GetAddressOf())))
		return false;
	// the window's brushes and bitmaps belong to its target: set aside while the export paints
	if (!CreateTarget())
		return false;
	auto brushes = std::move(s.brushes);
	s.brushes.clear();
	std::vector<ComPtr<ID2D1Bitmap>> heats;
	for (auto& p : s.paths)
		heats.push_back(std::move(p.heat));
	HRESULT hr = PaintTo(target.Get(), dpi, true);
	for (size_t i = 0; i < s.paths.size() && i < heats.size(); i++)
		s.paths[i].heat = std::move(heats[i]);
	s.brushes = std::move(brushes);
	target.Reset();
	pixels.resize((size_t)width * height);
	return SUCCEEDED(hr) && SUCCEEDED(bitmap->CopyPixels(nullptr, width * 4, width * height * 4, (BYTE*)pixels.data()));
}

std::string CFunctionPlotCtrl::BuildSvg() {
	auto& s = *m_State;
	s.syncSampling = true;
	Resample();
	s.syncSampling = false;
	const FPLCOLORS c = GetColors();
	const DWORD style = GetStyle();
	FplSvgStyle svg;
	svg.back = c.crBack;
	svg.grid = c.crGrid;
	svg.gridMinor = c.crGridMinor == c.crBack ? c.crGrid : c.crGridMinor;
	svg.axis = c.crAxis;
	svg.label = c.crLabel;
	svg.panelBack = c.crPanelBack;
	svg.panelText = c.crPanelText;
	svg.panelBorder = c.crPanelBorder;
	for (size_t i = 0; i < s.plot.graphs.size(); i++)
		svg.graphColors.push_back(GraphColor((int)i));
	svg.showGrid = !(style & FPLS_NOGRID);
	svg.showAxes = !(style & FPLS_NOAXES);
	svg.piTicks = (style & FPLS_PITICKS) != 0;
	svg.showLegend = !(style & FPLS_NOLEGEND) && !s.collapsed;
	svg.axisTitles[0] = s.axisTitles[0];
	svg.axisTitles[1] = s.axisTitles[1];
	return FplWriteSvg(s.plot, s.view, svg);
}

BOOL CFunctionPlotCtrl::ExportPng(LPCWSTR pszPath, float scale) {
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	auto wic = Wic();
	ComPtr<IWICStream> stream;
	return pszPath && wic && RenderImage(scale, pixels, width, height) &&
		SUCCEEDED(wic->CreateStream(stream.GetAddressOf())) &&
		SUCCEEDED(stream->InitializeFromFilename(pszPath, GENERIC_WRITE)) &&
		EncodePng(wic.Get(), stream.Get(), pixels, width, height);
}

BOOL CFunctionPlotCtrl::ExportSvg(LPCWSTR pszPath) {
	auto& s = *m_State;
	if (!pszPath || !s.view.Valid() || s.mode3D)
		return FALSE;
	std::string text = BuildSvg();
	FILE* f = nullptr;
	if (_wfopen_s(&f, pszPath, L"wb") || !f)
		return FALSE;
	bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	ok = fclose(f) == 0 && ok;
	return ok;
}

BOOL CFunctionPlotCtrl::Copy() {
	auto& s = *m_State;
	// at the screen's resolution, so it pastes as sharp as it's shown
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (!RenderImage((float)s.dpi / 96, pixels, width, height) || !width || !height)
		return FALSE;
	// a bottom-up DIB, opaque
	BITMAPINFOHEADER bih{ sizeof(bih), (LONG)width, (LONG)height, 1, 32, BI_RGB };
	std::vector<BYTE> dib(sizeof(bih) + (size_t)width * height * 4);
	memcpy(dib.data(), &bih, sizeof(bih));
	auto* rows = (uint32_t*)(dib.data() + sizeof(bih));
	for (UINT y = 0; y < height; y++)
		for (UINT x = 0; x < width; x++)
			rows[(size_t)(height - 1 - y) * width + x] = pixels[(size_t)y * width + x] | 0xFF000000u;
	// a PNG too (Office, browsers), the SVG in 2D (vector editors), and the graphs as text
	std::vector<BYTE> png;
	if (auto wic = Wic()) {
		ComPtr<IStream> stream;
		if (SUCCEEDED(::CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf())) &&
			EncodePng(wic.Get(), stream.Get(), pixels, width, height)) {
			STATSTG st{};
			if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart < (1ull << 31)) {
				png.resize((size_t)st.cbSize.QuadPart);
				LARGE_INTEGER zero{};
				ULONG read = 0;
				if (FAILED(stream->Seek(zero, STREAM_SEEK_SET, nullptr)) ||
					FAILED(stream->Read(png.data(), (ULONG)png.size(), &read)) || read != png.size())
					png.clear();
			}
		}
	}
	std::string svg = s.mode3D ? std::string() : BuildSvg();
	std::wstring text;
	for (auto& g : s.plot.graphs)
		if (!g.text.empty())
			text += g.text + L"\r\n";
	if (!OpenClipboard())
		return FALSE;
	::EmptyClipboard();
	bool ok = false;
	if (HGLOBAL h = ToGlobal(dib.data(), dib.size()); h && ::SetClipboardData(CF_DIB, h))
		ok = true;
	else if (h)
		::GlobalFree(h);
	auto put = [](UINT format, const void* data, size_t size) {
		if (!format || !size)
			return;
		if (HGLOBAL h = ToGlobal(data, size); h && !::SetClipboardData(format, h))
			::GlobalFree(h);
	};
	put(::RegisterClipboardFormatW(L"PNG"), png.data(), png.size());
	put(::RegisterClipboardFormatW(L"image/svg+xml"), svg.data(), svg.size());
	put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
	::CloseClipboard();
	return ok;
}

BOOL CFunctionPlotCtrl::PrintTo(HDC hdc, const RECT& rc) {
	auto& s = *m_State;
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (!hdc || w <= 0 || h <= 0 || !s.view.Valid())
		return FALSE;
	// the plot's shape, as large as fits in the rectangle (at most 8 times the window's size), centered
	const double fit = std::min(w / s.view.width, h / s.view.height);
	std::vector<uint32_t> pixels;
	UINT width = 0, height = 0;
	if (!RenderImage((float)std::min(fit, 8.0), pixels, width, height) || !width || !height)
		return FALSE;
	const double shown = std::min((double)w / width, (double)h / height);
	const int outW = (int)std::lround(width * shown), outH = (int)std::lround(height * shown);
	for (auto& p : pixels)
		p |= 0xFF000000u;
	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(bi.bmiHeader), (LONG)width, -(LONG)height, 1, 32, BI_RGB };
	const int x = rc.left + (w - outW) / 2, y = rc.top + (h - outH) / 2;
	::SetStretchBltMode(hdc, HALFTONE);
	::SetBrushOrgEx(hdc, 0, 0, nullptr);
	return ::StretchDIBits(hdc, x, y, outW, outH, 0, 0, width, height, pixels.data(), &bi, DIB_RGB_COLORS,
		SRCCOPY) != GDI_ERROR;
}

// ---- Input -----------------------------------------------------------------------------

LRESULT CFunctionPlotCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM lParam, BOOL&) {
	LRESULT code = DLGC_WANTARROWS | DLGC_WANTCHARS;
	// Tab goes along the sliders, and out of the control past them
	auto msg = (const MSG*)lParam;
	if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_TAB && NextSlider(::GetKeyState(VK_SHIFT) < 0) != -2)
		code |= DLGC_WANTTAB;
	return code;
}

LRESULT CFunctionPlotCtrl::OnFocus(UINT, WPARAM, LPARAM, BOOL&) {
	Invalidate();
	return 0;
}

LRESULT CFunctionPlotCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& s = *m_State;
	if (::GetKeyState(VK_CONTROL) < 0 && (wParam == 'C' || wParam == VK_INSERT)) {
		Copy();
		return 0;
	}
	if (::GetKeyState(VK_CONTROL) < 0 && (wParam == 'Z' || wParam == 'Y')) {
		if (wParam == 'Y' || ::GetKeyState(VK_SHIFT) < 0)
			Redo();
		else
			Undo();
		return 0;
	}
	if (wParam == VK_TAB) {
		int next = NextSlider(::GetKeyState(VK_SHIFT) < 0);
		if (next != -2) {
			s.focusRow = next;
			Invalidate();
			return 0;
		}
		bHandled = FALSE;
		return 0;
	}
	// a slider with the keys: steps, pages, its ends, Space plays it
	if (s.focusRow >= 0 && s.focusRow < (int)s.rows.size() && s.rows[s.focusRow].kind == FplPanelRow::Slider) {
		const int param = s.rows[s.focusRow].index;
		auto& p = s.plot.params[param];
		const double step = p.step > 0 ? p.step : (p.max - p.min) / 100;
		if (wParam != VK_SPACE && wParam != VK_ESCAPE)
			Remember(1000 + param);
		switch (wParam) {
			case VK_LEFT: case VK_DOWN: SetParam(param, p.value - step, true); return 0;
			case VK_RIGHT: case VK_UP: SetParam(param, p.value + step, true); return 0;
			case VK_PRIOR: SetParam(param, p.value + 10 * step, true); return 0;
			case VK_NEXT: SetParam(param, p.value - 10 * step, true); return 0;
			case VK_HOME: SetParam(param, p.min, false); return 0;
			case VK_END: SetParam(param, p.max, false); return 0;
			case VK_SPACE: AnimateParameter(p.name.c_str(), !p.animating); return 0;
			case VK_ESCAPE: s.focusRow = -1; Invalidate(); return 0;
		}
	}
	// a trace takes the arrows (T starts one)
	if (TraceKey((UINT)wParam))
		return 0;
	const bool canMove = !(GetStyle() & FPLS_NOPANZOOM);
	if (wParam == 'F' && canMove && !s.mode3D) {
		Remember(-1);
		ZoomToFit();
		return 0;
	}
	switch (wParam) {
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_ADD: case VK_OEM_PLUS: case VK_SUBTRACT:
		case VK_OEM_MINUS:
			if (canMove || s.mode3D)
				Remember(1);
			break;
		case VK_HOME:
			Remember(-1);
			break;
	}
	if (s.mode3D) {
		// the arrows orbit, +/- zoom the box, Home starts over
		switch (wParam) {
			case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
				s.camera.Orbit(wParam == VK_LEFT ? 0.2f : wParam == VK_RIGHT ? -0.2f : 0,
					wParam == VK_UP ? 0.1f : wParam == VK_DOWN ? -0.1f : 0);
				Invalidate();
				NotifyView3D();
				return 0;
			case VK_ADD: case VK_OEM_PLUS: case VK_SUBTRACT: case VK_OEM_MINUS:
				if (!canMove)
					break;
				s.box.Zoom(wParam == VK_ADD || wParam == VK_OEM_PLUS ? 1.25 : 0.8);
				Resample();
				Invalidate();
				NotifyView3D();
				return 0;
			case VK_HOME:
				ResetView3D();
				NotifyView3D();
				return 0;
		}
		bHandled = FALSE;
		return 0;
	}
	auto& v = s.view;
	switch (wParam) {
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
			if (!canMove)
				break;
			v.Pan(wParam == VK_LEFT ? v.width / 10 : wParam == VK_RIGHT ? -v.width / 10 : 0,
				wParam == VK_UP ? v.height / 10 : wParam == VK_DOWN ? -v.height / 10 : 0);
			ViewChanged(true);
			return 0;
		case VK_ADD: case VK_OEM_PLUS: case VK_SUBTRACT: case VK_OEM_MINUS:
			if (!canMove)
				break;
			Zoom(wParam == VK_ADD || wParam == VK_OEM_PLUS ? 1.25 : 0.8);
			NotifyView();
			return 0;
		case VK_HOME:
			if (!canMove)
				break;
			ResetView();
			NotifyView();
			return 0;
		case VK_ESCAPE:
			if (s.drag != FplDrag::None)
				ReleaseCapture();
			ClearTrace();
			return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	SetFocus();
	auto hit = HitPanel(pt);
	if (hit.part != FplPanelPart::Slider && hit.part != FplPanelPart::Play && s.focusRow >= 0) {
		s.focusRow = -1;
		Invalidate();
	}
	if (hit.part == FplPanelPart::Toggle) {
		SetPanelCollapsed(!s.collapsed);
		return 0;
	}
	if (hit.part == FplPanelPart::Play) {
		s.focusRow = hit.row;
		auto& p = s.plot.params[s.rows[hit.row].index];
		AnimateParameter(p.name.c_str(), !p.animating);
		return 0;
	}
	if (hit.part == FplPanelPart::Graph) {
		auto& g = s.plot.graphs[s.rows[hit.row].index];
		if (!g.definition) {
			Remember(-1);
			bool hidden = !(g.flags & FplGraphHidden);
			ShowGraph(g.id, !hidden);
			if (s.traceGraph >= 0 && hidden)
				ClearTrace();
			NMFPLGRAPH nm{};
			nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), FPLN_GRAPHTOGGLED };
			nm.id = g.id;
			nm.hidden = hidden;
			::SendMessageW(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
		}
		return 0;
	}
	if (hit.part == FplPanelPart::Slider) {
		Remember(-1);
		s.focusRow = hit.row;
		s.drag = FplDrag::Slider;
		s.dragRow = hit.row;
		SetCapture();
		DragSlider(hit.row, ToDips(pt.x));
		return 0;
	}
	if (hit.part == FplPanelPart::Panel)
		return 0;
	if (s.mode3D) {
		// drag to orbit; Shift (or no orbiting wanted) to move the box's x and y
		Remember(-1);
		bool pan = (::GetKeyState(VK_SHIFT) < 0) && !(GetStyle() & FPLS_NOPANZOOM);
		s.drag = pan ? FplDrag::Pan3D : FplDrag::Orbit;
		s.dragLast = pt;
		SetCapture();
		return 0;
	}
	// a draggable point: move it (and so its parameters)
	int pointIndex = 0;
	int point = s.plot.DraggableAt({ ToDips(pt.x), ToDips(pt.y) }, FplTraceDistance, &pointIndex);
	if (point >= 0) {
		if (s.traceDragged)
			ClearTrace();
		Remember(-1);
		s.drag = FplDrag::Point;
		s.dragGraph = point;
		s.dragPoint = pointIndex;
		SetCapture();
		return 0;
	}
	// on a graph: trace it; elsewhere: pan
	if (!(GetStyle() & FPLS_NOTRACE)) {
		FplPoint p = { ToDips(pt.x), ToDips(pt.y) };
		auto onGraph = s.plot.Nearest(p, s.view, FplTraceDistance, FplSnapDistance);
		if (onGraph) {
			UpdateTrace(pt, false);
			s.drag = FplDrag::Trace;
			s.traceDragged = true;
			SetCapture();
			return 0;
		}
	}
	if (s.traceDragged) {
		s.traceDragged = false;
		ClearTrace();
	}
	if (!(GetStyle() & FPLS_NOPANZOOM)) {
		// Shift: a rectangle to zoom to; else pan
		Remember(-1);
		s.dragLast = pt;
		if (::GetKeyState(VK_SHIFT) < 0) {
			s.drag = FplDrag::Box;
			s.boxStart = s.boxEnd = pt;
			s.boxShown = false;
			SetCapture();
			return 0;
		}
		s.drag = FplDrag::Pan;
		SetCapture();
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
	}
	return 0;
}

LRESULT CFunctionPlotCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_State->drag == FplDrag::Box)
		EndBox({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
	if (m_State->drag != FplDrag::None)
		ReleaseCapture();
	return 0;
}

LRESULT CFunctionPlotCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto& s = *m_State;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (s.mode3D || s.drag != FplDrag::None || (GetStyle() & FPLS_NOPANZOOM) || HitPanel(pt).part != FplPanelPart::None) {
		bHandled = FALSE;
		return 0;
	}
	// a rectangle to zoom to, if it's dragged far enough (else it's a click: the context menu)
	SetFocus();
	s.drag = FplDrag::Box;
	s.boxStart = s.boxEnd = pt;
	s.boxShown = false;
	SetCapture();
	return 0;
}

LRESULT CFunctionPlotCtrl::OnRButtonUp(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto& s = *m_State;
	if (s.drag != FplDrag::Box) {
		bHandled = FALSE;
		return 0;
	}
	bool shown = s.boxShown;
	if (shown) {
		Remember(-1);
		EndBox({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
	}
	ReleaseCapture();
	// not dragged: a click, whose WM_CONTEXTMENU DefWindowProc makes
	bHandled = shown ? TRUE : FALSE;
	return 0;
}

void CFunctionPlotCtrl::EndBox(POINT pt) {
	auto& s = *m_State;
	if (!s.boxShown)
		return;
	s.boxShown = false;
	FplViewport v = s.view;
	if (v.ZoomTo({ ToDips(s.boxStart.x), ToDips(s.boxStart.y) }, { ToDips(pt.x), ToDips(pt.y) })) {
		SetViewport(v.xMin, v.xMax, v.yMin, v.yMax);
		ViewChanged(true);
	}
	Invalidate();
}

LRESULT CFunctionPlotCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	if (s.drag == FplDrag::Box) {
		s.boxShown = false;
		Invalidate();
	}
	s.drag = FplDrag::None;
	s.dragRow = -1;
	s.dragGraph = -1;
	return 0;
}

LRESULT CFunctionPlotCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (!s.trackingLeave) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		s.trackingLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
	switch (s.drag) {
		case FplDrag::Pan:
			if (pt.x != s.dragLast.x || pt.y != s.dragLast.y) {
				s.view.Pan(ToDips(pt.x - s.dragLast.x), ToDips(pt.y - s.dragLast.y));
				s.dragLast = pt;
				ViewChanged(true);
			}
			return 0;
		case FplDrag::Trace:
			UpdateTrace(pt, true);
			return 0;
		case FplDrag::Slider:
			DragSlider(s.dragRow, ToDips(pt.x));
			return 0;
		case FplDrag::Box:
			s.boxEnd = pt;
			if (!s.boxShown && (std::abs(pt.x - s.boxStart.x) > ::GetSystemMetrics(SM_CXDRAG) ||
				std::abs(pt.y - s.boxStart.y) > ::GetSystemMetrics(SM_CYDRAG)))
				s.boxShown = true;
			Invalidate();
			return 0;
		case FplDrag::Point:
			if (s.dragGraph >= 0 && s.dragGraph < (int)s.plot.graphs.size()) {
				auto& g = s.plot.graphs[s.dragGraph];
				FplPoint p = s.view.ToPlane({ ToDips(pt.x), ToDips(pt.y) });
				// read both first: setting one samples again
				const auto [px, py] = g.DragOf(s.dragPoint);
				if (px >= 0)
					SetParam(px, p.x, true);
				if (py >= 0)
					SetParam(py, p.y, true);
			}
			return 0;
		case FplDrag::Orbit:
			if (pt.x != s.dragLast.x || pt.y != s.dragLast.y) {
				s.camera.Orbit(-0.01f * ToDips(pt.x - s.dragLast.x), 0.01f * ToDips(pt.y - s.dragLast.y));
				s.dragLast = pt;
				Invalidate();
				NotifyView3D();
			}
			return 0;
		case FplDrag::Pan3D:
			if (pt.x != s.dragLast.x || pt.y != s.dragLast.y) {
				// the floor follows the mouse: screen right and up as directions on the xy plane
				const float dx = ToDips(pt.x - s.dragLast.x), dy = ToDips(pt.y - s.dragLast.y);
				const float side = 0.5f * std::min(ToDips(s.client.cx), ToDips(s.client.cy));
				const double rx = -std::sin(s.camera.yaw), ry = std::cos(s.camera.yaw);
				const double fx = -std::cos(s.camera.yaw), fy = -std::sin(s.camera.yaw);
				const double mx = (dx * rx - dy * fx) / side, my = (dx * ry - dy * fy) / side;
				s.box.Pan(-mx * (s.box.xMax - s.box.xMin) / 2, -my * (s.box.yMax - s.box.yMin) / 2);
				s.dragLast = pt;
				Resample();
				Invalidate();
				NotifyView3D();
			}
			return 0;
		default:
			break;
	}
	auto hit = HitPanel(pt);
	int hot = hit.part == FplPanelPart::Graph || hit.part == FplPanelPart::Slider || hit.part == FplPanelPart::Play
		? hit.row : -1;
	bool hotToggle = hit.part == FplPanelPart::Toggle;
	if (hot != s.hotRow || hotToggle != s.hotToggle) {
		s.hotRow = hot;
		s.hotToggle = hotToggle;
		Invalidate();
	}
	int hotIndex = 0;
	int hotPoint = hit.part == FplPanelPart::None && !s.mode3D
		? s.plot.DraggableAt({ ToDips(pt.x), ToDips(pt.y) }, FplTraceDistance, &hotIndex) : -1;
	if (hotPoint != s.hotPoint || hotIndex != s.hotPointIndex) {
		s.hotPoint = hotPoint;
		s.hotPointIndex = hotIndex;
		Invalidate();
	}
	if (hit.part == FplPanelPart::None && hotPoint < 0) {
		if (!s.traceDragged)
			UpdateTrace(pt, false);
	}
	else if (!s.traceDragged && s.traceValid) {
		UpdateTrace({ -10000, -10000 }, false);
	}
	return 0;
}

LRESULT CFunctionPlotCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	s.trackingLeave = false;
	if (s.hotRow >= 0 || s.hotToggle) {
		s.hotRow = -1;
		s.hotToggle = false;
		Invalidate();
	}
	if (s.drag == FplDrag::None && !s.traceDragged)
		UpdateTrace({ -10000, -10000 }, false);
	return 0;
}

LRESULT CFunctionPlotCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (LOWORD(lParam) != HTCLIENT || (HWND)wParam != m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	auto& s = *m_State;
	LPCWSTR cursor = IDC_ARROW;
	if (s.drag == FplDrag::Box)
		cursor = IDC_CROSS;
	else if (s.drag == FplDrag::Point || (s.drag == FplDrag::None && s.hotPoint >= 0))
		cursor = IDC_SIZEALL;
	else if (s.drag == FplDrag::Pan || s.drag == FplDrag::Pan3D)
		cursor = IDC_SIZEALL;
	else if (s.drag == FplDrag::Slider || s.hotRow >= 0 || s.hotToggle)
		cursor = IDC_HAND;
	else if (s.drag == FplDrag::Trace || s.traceValid)
		cursor = IDC_CROSS;
	::SetCursor(::LoadCursor(nullptr, cursor));
	return TRUE;
}

LRESULT CFunctionPlotCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	if (GetStyle() & FPLS_NOPANZOOM)
		return 0;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	ScreenToClient(&pt);
	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	Remember(2);
	if (s.mode3D) {
		s.box.Zoom(std::pow(1.2, delta / 120.0));
		Resample();
		Invalidate();
		NotifyView3D();
		return 0;
	}
	const UINT keys = GET_KEYSTATE_WPARAM(wParam);
	const double factor = std::pow(1.2, delta / 120.0);
	double fx = factor, fy = factor;
	if (!(GetStyle() & FPLS_EQUALSCALE)) {
		if (keys & MK_CONTROL)
			fy = 1;     // x only
		else if (keys & MK_SHIFT)
			fx = 1;     // y only
	}
	s.view.Zoom(fx, fy, { ToDips(pt.x), ToDips(pt.y) });
	ViewChanged(true);
	if (s.traceValid && !s.traceDragged)
		UpdateTrace(pt, false);
	return 0;
}

LRESULT CFunctionPlotCtrl::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	PAINTSTRUCT ps;
	BeginPaint(&ps);
	Paint();
	EndPaint(&ps);
	return 0;
}
