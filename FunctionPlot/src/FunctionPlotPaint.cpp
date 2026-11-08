// The function plot's painting: Direct2D resources, the grid and axes with their labels, the graphs, the trace, and
// the panel of the legend and the sliders.

#include "FunctionPlotData.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// ---- Factories -----------------------------------------------------------------------

ID2D1Factory* FplD2DFactory() {
	static ComPtr<ID2D1Factory> factory = [] {
		ComPtr<ID2D1Factory> f;
		::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, f.GetAddressOf());
		return f;
	}();
	return factory.Get();
}

IDWriteFactory* FplDWriteFactory() {
	static ComPtr<IDWriteFactory> factory = [] {
		ComPtr<IDWriteFactory> f;
		::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)f.GetAddressOf());
		return f;
	}();
	return factory.Get();
}

namespace {

D2D1_COLOR_F Color(COLORREF c, float alpha = 1) {
	return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, alpha);
}

D2D1_POINT_2F Pt(FplPoint p) {
	return D2D1::Point2F((float)p.x, (float)p.y);
}

ComPtr<IDWriteTextFormat> MakeFormat(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
	const wchar_t* family = L"Segoe UI") {
	ComPtr<IDWriteTextFormat> format;
	if (FAILED(FplDWriteFactory()->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, size, L"", format.GetAddressOf())))
		return nullptr;
	format->SetTextAlignment(align);
	format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	// a long expression ends in an ellipsis
	ComPtr<IDWriteInlineObject> sign;
	if (SUCCEEDED(FplDWriteFactory()->CreateEllipsisTrimmingSign(format.Get(), sign.GetAddressOf()))) {
		DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
		format->SetTrimming(&trimming, sign.Get());
	}
	return format;
}

void Text(ID2D1RenderTarget* rt, const std::wstring& text, IDWriteTextFormat* format, D2D1_RECT_F rc,
	ID2D1Brush* brush) {
	if (format && brush && !text.empty())
		rt->DrawText(text.c_str(), (UINT32)text.size(), format, rc, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float TextWidth(const std::wstring& text, IDWriteTextFormat* format) {
	ComPtr<IDWriteTextLayout> layout;
	if (!format || FAILED(FplDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, 10000,
		100, layout.GetAddressOf())))
		return 0;
	DWRITE_TEXT_METRICS m;
	layout->GetMetrics(&m);
	return m.widthIncludingTrailingWhitespace;
}

const wchar_t* FeatureName(FplFeatureKind kind) {
	switch (kind) {
		case FplFeatureKind::Root: return L"Root";
		case FplFeatureKind::Minimum: return L"Minimum";
		case FplFeatureKind::Maximum: return L"Maximum";
		case FplFeatureKind::Intersection: return L"Intersection";
		case FplFeatureKind::YIntercept: return L"y-intercept";
	}
	return L"";
}

}  // namespace

ID2D1SolidColorBrush* FplState::Brush(COLORREF color) {
	auto& brush = brushes[color];
	if (!brush && rt)
		rt->CreateSolidColorBrush(Color(color), brush.GetAddressOf());
	return brush.Get();
}

// ---- Resources -------------------------------------------------------------------------

// The stroke styles and text formats: the factories', for any target.
static void MakeResources(FplState& s) {
	if (!s.roundStroke) {
		auto round = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
			D2D1_LINE_JOIN_ROUND);
		FplD2DFactory()->CreateStrokeStyle(round, nullptr, 0, s.roundStroke.GetAddressOf());
		auto dash = round;
		dash.dashStyle = D2D1_DASH_STYLE_CUSTOM;
		dash.dashCap = D2D1_CAP_STYLE_FLAT;
		const float dashes[] = { 3, 2 };
		FplD2DFactory()->CreateStrokeStyle(dash, dashes, 2, s.dashStroke.GetAddressOf());
		// dots: no dash, round caps, two and a half widths apart
		auto dot = round;
		dot.dashStyle = D2D1_DASH_STYLE_CUSTOM;
		const float dots[] = { 0, 2.5f };
		FplD2DFactory()->CreateStrokeStyle(dot, dots, 2, s.dotStroke.GetAddressOf());
	}
	if (!s.labelCenter) {
		s.labelCenter = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
		s.labelLeft = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
		s.labelRight = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
		s.panelText = MakeFormat(14, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
		s.panelSmall = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
		s.traceText = MakeFormat(13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
		s.icons = MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets");
	}
}

bool CFunctionPlotCtrl::CreateTarget() {
	auto& s = *m_State;
	if (s.target)
		return true;
	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
		(float)s.dpi, (float)s.dpi);
	const auto hwndProps = D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(s.client.cx, s.client.cy),
		D2D1_PRESENT_OPTIONS_IMMEDIATELY);
	if (FAILED(FplD2DFactory()->CreateHwndRenderTarget(props, hwndProps, s.target.GetAddressOf())))
		return false;
	MakeResources(s);
	return true;
}

void CFunctionPlotCtrl::DiscardTarget() {
	auto& s = *m_State;
	s.target.Reset();
	s.brushes.clear();
	// the heat maps' bitmaps belong to the target
	for (auto& p : s.paths) {
		p.heat.Reset();
		if (p.stale)
			p.stale->heat.Reset();
	}
}

namespace {

void AddOpen(ID2D1GeometrySink* sink, const FplPolyline& line, std::vector<D2D1_POINT_2F>& points) {
	if (line.size() < 2)
		return;
	sink->BeginFigure(Pt(line[0]), D2D1_FIGURE_BEGIN_HOLLOW);
	points.clear();
	for (size_t k = 1; k < line.size(); k++)
		points.push_back(Pt(line[k]));
	sink->AddLines(points.data(), (UINT32)points.size());
	sink->EndFigure(D2D1_FIGURE_END_OPEN);
}

// An arrow: its shaft and a head of two strokes.
// What a heavy graph shows while its full version is made after the view moved: the coarse drawing around the old view's
// rectangle, and the last full one, stretched, in it.
template <class Own, class Stale>
void DrawAroundStale(ID2D1RenderTarget* rt, const FplGraphPaths& p, const FplViewport& view, Own own, Stale stale) {
	if (!p.stale) {
		own();
		return;
	}
	const float w = (float)view.width, h = (float)view.height;
	const D2D1_RECT_F r = { std::clamp(p.staleRect.left, 0.0f, w), std::clamp(p.staleRect.top, 0.0f, h),
		std::clamp(p.staleRect.right, 0.0f, w), std::clamp(p.staleRect.bottom, 0.0f, h) };
	const D2D1_RECT_F around[4] = { { 0, 0, w, r.top }, { 0, r.bottom, w, h }, { 0, r.top, r.left, r.bottom }, { r.right, r.top, w, r.bottom } };
	for (auto& strip : around) {
		if (strip.right <= strip.left || strip.bottom <= strip.top)
			continue;
		rt->PushAxisAlignedClip(strip, D2D1_ANTIALIAS_MODE_ALIASED);
		own();
		rt->PopAxisAlignedClip();
	}
	rt->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
	stale();
	rt->PopAxisAlignedClip();
}

void AddArrow(ID2D1GeometrySink* sink, const FplArrow& a, std::vector<D2D1_POINT_2F>& points) {
	FplPoint l, r;
	if (!FplArrowHead(a, l, r))
		return;
	AddOpen(sink, { a.from, a.to }, points);
	AddOpen(sink, { l, a.to, r }, points);
}

}  // namespace

void CFunctionPlotCtrl::BuildGeometries() {
	auto& s = *m_State;
	s.paths.assign(s.plot.graphs.size(), {});
	for (size_t i = 0; i < s.plot.graphs.size(); i++)
		BuildGeometry((int)i);
}

void CFunctionPlotCtrl::BuildGeometry(int graph) {
	auto& s = *m_State;
	if (graph < 0 || graph >= (int)s.plot.graphs.size())
		return;
	if (s.paths.size() < s.plot.graphs.size())
		s.paths.resize(s.plot.graphs.size());
	auto& geometry = s.plot.graphs[graph].geometry;
	auto& paths = s.paths[graph];
	paths = {};
	if (!geometry.lines.empty() && SUCCEEDED(FplD2DFactory()->CreatePathGeometry(paths.lines.GetAddressOf()))) {
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(paths.lines->Open(sink.GetAddressOf()))) {
			std::vector<D2D1_POINT_2F> points;
			for (auto& line : geometry.lines) {
				sink->BeginFigure(Pt(line[0]), D2D1_FIGURE_BEGIN_HOLLOW);
				points.clear();
				points.reserve(line.size() - 1);
				for (size_t k = 1; k < line.size(); k++)
					points.push_back(Pt(line[k]));
				sink->AddLines(points.data(), (UINT32)points.size());
				sink->EndFigure(D2D1_FIGURE_END_OPEN);
			}
			sink->Close();
		}
	}
	if (!geometry.fills.empty() && SUCCEEDED(FplD2DFactory()->CreatePathGeometry(paths.fills.GetAddressOf()))) {
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(paths.fills->Open(sink.GetAddressOf()))) {
			// one geometry: the cells' polygons share edges and fill without seams
			sink->SetFillMode(D2D1_FILL_MODE_WINDING);
			std::vector<D2D1_POINT_2F> points;
			for (auto& poly : geometry.fills) {
				if (poly.size() < 3)
					continue;
				sink->BeginFigure(Pt(poly[0]), D2D1_FIGURE_BEGIN_FILLED);
				points.clear();
				points.reserve(poly.size() - 1);
				for (size_t k = 1; k < poly.size(); k++)
					points.push_back(Pt(poly[k]));
				sink->AddLines(points.data(), (UINT32)points.size());
				sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			}
			sink->Close();
		}
	}
	// a surface's contours, with a label at the middle of each level's longest line
	std::vector<D2D1_POINT_2F> points;
	if (!geometry.contours.empty() && SUCCEEDED(FplD2DFactory()->CreatePathGeometry(paths.contours.GetAddressOf()))) {
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(paths.contours->Open(sink.GetAddressOf()))) {
			for (auto& c : geometry.contours)
				for (auto& line : c.lines)
					AddOpen(sink.Get(), line, points);
			sink->Close();
		}
		const double step = geometry.contours.size() > 1
			? std::abs(geometry.contours[1].level - geometry.contours[0].level) : 1;
		for (auto& c : geometry.contours) {
			const FplPolyline* longest = nullptr;
			for (auto& line : c.lines)
				if (!longest || line.size() > longest->size())
					longest = &line;
			if (longest && longest->size() >= 8)
				paths.labels.push_back({ (*longest)[longest->size() / 2], FplFormatCoordinate(c.level, step / 100) });
		}
	}
	// a vector field's arrows
	if (!geometry.marks.empty() && SUCCEEDED(FplD2DFactory()->CreatePathGeometry(paths.marks.GetAddressOf()))) {
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(paths.marks->Open(sink.GetAddressOf()))) {
			for (auto& m : geometry.marks) {
				sink->BeginFigure(Pt(m.from), D2D1_FIGURE_BEGIN_HOLLOW);
				sink->AddLine(Pt(m.to));
				sink->EndFigure(D2D1_FIGURE_END_OPEN);
			}
			sink->Close();
		}
	}
	if (!geometry.arrows.empty() && SUCCEEDED(FplD2DFactory()->CreatePathGeometry(paths.arrows.GetAddressOf()))) {
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(paths.arrows->Open(sink.GetAddressOf()))) {
			for (auto& a : geometry.arrows)
				AddArrow(sink.Get(), a, points);
			sink->Close();
		}
	}
	// a heat map's pixels: its values on the colormap, transparent where undefined
	auto& hm = geometry.heatmap;
	if (!hm.Empty()) {
		paths.heatCols = hm.cols;
		paths.heatRows = hm.rows;
		paths.heatCell = (float)hm.cell;
		paths.heatPixels.resize(hm.values.size());
		const double range = hm.hi - hm.lo;
		for (size_t k = 0; k < hm.values.size(); k++) {
			float v = hm.values[k];
			if (!std::isfinite(v)) {
				paths.heatPixels[k] = 0;
				continue;
			}
			uint32_t c = FplColormap((v - hm.lo) / range);
			paths.heatPixels[k] = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
		}
	}
}

// ---- The panel's layout ----------------------------------------------------------------

void CFunctionPlotCtrl::LayoutPanel() {
	auto& s = *m_State;
	s.rows.clear();
	s.panel = {};
	s.toggle = {};
	if (!IsWindow())
		return;
	const DWORD style = GetStyle();
	const float width = std::clamp(ToDips(s.client.cx) * 0.4f, 200.0f, 340.0f);
	const float left = FplPanelMargin, right = left + width;
	float y = FplPanelMargin + FplPanelPad;
	if (!(style & FPLS_NOLEGEND)) {
		for (size_t i = 0; i < s.plot.graphs.size(); i++) {
			auto& g = s.plot.graphs[i];
			if (g.text.empty())
				continue;
			FplPanelRow row;
			row.kind = FplPanelRow::Graph;
			row.index = (int)i;
			row.error = (bool)g.expr.error;
			row.note = !row.error && g.expr.kind == FplKind::Regression;
			row.stopped = !row.error && (s.mode3D ? g.incomplete3D : g.geometry.incomplete);
			// the text as mathematics, if it reads and fits, with room left for a value after it
			if (!(style & FPLS_PLAINLEGEND) && !row.error) {
				const bool hasValue = g.definition || g.listDefinition || g.expr.kind == FplKind::Integral;
				const double room = width - 36 - (hasValue ? 70 : 0);
				const std::wstring key = g.text + L'\n' + std::to_wstring((int)room);
				if (s.mathCache.size() > 256)
					s.mathCache.clear();
				auto found = s.mathCache.find(key);
				if (found == s.mathCache.end()) {
					FplMathLayout layout;
					std::shared_ptr<FplMathDrawing> drawing;
					if (FplFitMath(g.text, s.plot.DisplayOptions(), { 15, 13, 11 }, room, FplDWriteMeasure(), layout))
						drawing = FplMakeMathDrawing(std::move(layout));
					found = s.mathCache.emplace(key, std::move(drawing)).first;
				}
				row.math = found->second;
				if (row.math)
					row.main = std::max(FplGraphRow, (float)(row.math->layout.ascent + row.math->layout.descent) + 8);
			}
			float h = row.main + (row.error || row.note || row.stopped ? FplErrorLine : 0);
			row.rect = { left, y, right, y + h };
			s.rows.push_back(row);
			y += h;
		}
	}
	if (!(style & FPLS_NOSLIDERS)) {
		for (size_t i = 0; i < s.plot.params.size(); i++) {
			if (s.plot.params[i].flags & FPLPF_NOSLIDER)
				continue;
			FplPanelRow row;
			row.kind = FplPanelRow::Slider;
			row.index = (int)i;
			row.rect = { left, y, right, y + FplSliderRow };
			const float mid = y + FplSliderRow / 2;
			row.play = { left + 6, mid - 11, left + 28, mid + 11 };
			row.track = { left + 76, mid - 2, right - 66, mid + 2 };
			s.rows.push_back(row);
			y += FplSliderRow;
		}
	}
	if (s.rows.empty()) {
		s.focusRow = s.hotRow = -1;
		return;
	}
	if (s.collapsed) {
		// only the button that opens it again
		s.rows.clear();
		s.focusRow = s.hotRow = -1;
		s.toggle = { FplPanelMargin, FplPanelMargin, FplPanelMargin + 26, FplPanelMargin + 26 };
		return;
	}
	s.panel = { left, FplPanelMargin, right, y + FplPanelPad };
	s.toggle = { right + 4, FplPanelMargin, right + 30, FplPanelMargin + 26 };
	if (s.hotRow >= (int)s.rows.size())
		s.hotRow = -1;
	if (s.focusRow >= (int)s.rows.size() || (s.focusRow >= 0 && s.rows[s.focusRow].kind != FplPanelRow::Slider))
		s.focusRow = -1;
}

// ---- Painting --------------------------------------------------------------------------

void CFunctionPlotCtrl::Paint() {
	auto& s = *m_State;
	if (s.mode3D) {
		// the frames are the 3D view's window's to ask for (once it's there)
		if (!s.view3D)
			Paint3D(true);
		return;
	}
	if (!CreateTarget())
		return;
	if (PaintTo(s.target.Get(), (float)s.dpi, false) == D2DERR_RECREATE_TARGET) {
		DiscardTarget();
		Invalidate();
	}
}

HRESULT CFunctionPlotCtrl::PaintTo(ID2D1RenderTarget* target, float dpi, bool exporting) {
	auto& s = *m_State;
	s.rt = target;
	s.paintDpi = dpi;
	s.exporting = exporting;
	const FPLCOLORS c = GetColors();
	target->BeginDraw();
	target->SetTransform(D2D1::Matrix3x2F::Identity());
	target->Clear(Color(c.crBack));
	if (s.view.Valid()) {
		PaintHeatmaps();
		PaintGrid();
		PaintGraphs();
		PaintContourLabels();
		PaintLabels();
		PaintGraphLabels();
		PaintColorScales();
		PaintTrace();
		PaintBox();
		PaintPanel();
	}
	HRESULT hr = target->EndDraw();
	s.rt = s.target.Get();
	s.paintDpi = (float)s.dpi;
	s.exporting = false;
	return hr;
}

void CFunctionPlotCtrl::PaintHeatmaps() {
	auto& s = *m_State;
	auto* rt = s.rt;
	for (size_t i = 0; i < s.plot.graphs.size() && i < s.paths.size(); i++) {
		auto& p = s.paths[i];
		if (!s.plot.graphs[i].Drawable())
			continue;
		auto make = [&](FplGraphPaths& paths) {
			if (paths.heatPixels.empty())
				return false;
			if (paths.heat)
				return true;
			auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			return SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(paths.heatCols, paths.heatRows), paths.heatPixels.data(),
				paths.heatCols * 4, props, paths.heat.GetAddressOf()));
		};
		const float opacity = s.plot.graphs[i].fillOpacity >= 0 ? s.plot.graphs[i].fillOpacity : 0.85f;
		DrawAroundStale(rt, p, s.view, [&] {
			if (make(p))
				rt->DrawBitmap(p.heat.Get(), D2D1::RectF(0, 0, p.heatCols * p.heatCell, p.heatRows * p.heatCell), opacity,
					D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		}, [&] {
			auto& old = *p.stale;
			if (!make(old))
				return;
			const FplPoint a = p.staleMap.Apply({ 0, 0 }), b = p.staleMap.Apply({ old.heatCols * (double)old.heatCell, old.heatRows * (double)old.heatCell });
			rt->DrawBitmap(old.heat.Get(), D2D1::RectF((float)a.x, (float)a.y, (float)b.x, (float)b.y), opacity,
				D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
		});
	}
}

void CFunctionPlotCtrl::PaintContourLabels() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	auto* patch = s.Brush(c.crBack);
	for (size_t i = 0; i < s.plot.graphs.size() && i < s.paths.size(); i++) {
		if (!s.plot.graphs[i].Drawable())
			continue;
		auto* color = s.Brush(GraphColor((int)i));
		auto& paths = s.paths[i];
		// the last full version's where it's shown, stretched
		std::vector<FplGraphPaths::Label> labels;
		if (paths.stale) {
			for (auto& label : paths.stale->labels)
				labels.push_back({ paths.staleMap.Apply(label.at), label.text });
			for (auto& label : paths.labels)
				if (label.at.x < paths.staleRect.left || label.at.x > paths.staleRect.right ||
					label.at.y < paths.staleRect.top || label.at.y > paths.staleRect.bottom)
					labels.push_back(label);
		}
		for (auto& label : paths.stale ? labels : paths.labels) {
			const float x = (float)label.at.x, y = (float)label.at.y;
			if (x < 0 || y < 0 || x > s.view.width || y > s.view.height)
				continue;
			const float tw = TextWidth(label.text, s.labelCenter.Get()) + 6;
			patch->SetOpacity(0.8f);
			rt->FillRoundedRectangle({ { x - tw / 2, y - 8, x + tw / 2, y + 8 }, 3, 3 }, patch);
			patch->SetOpacity(1);
			Text(rt, label.text, s.labelCenter.Get(), { x - 60, y - 9, x + 60, y + 9 }, color);
		}
	}
}

void CFunctionPlotCtrl::PaintGrid() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	const DWORD style = GetStyle();
	auto& v = s.view;
	const float w = (float)v.width, h = (float)v.height;
	const float pixel = 96.0f / s.paintDpi;
	// a line on a pixel's middle, so a one-pixel line is sharp
	auto snap = [&](double d) { return (float)((std::floor(d / pixel) + 0.5) * pixel); };

	FplTicks tx = FplMakeTicksX(v, 90, (style & FPLS_PITICKS) != 0);
	FplTicks ty = FplMakeTicksY(v, 60);
	// circles and spokes: antialiased, as they're rarely on a pixel's middle
	const FplPolarGrid polar = (style & FPLS_POLARGRID) ? FplMakePolarGrid(v) : FplPolarGrid{};
	if (!(style & FPLS_NOGRID) && !polar.Empty()) {
		std::vector<D2D1_POINT_2F> points;
		auto draw = [&](const std::vector<FplPolyline>& rings, const std::vector<FplPolyline>& spokes, COLORREF color) {
			ComPtr<ID2D1PathGeometry> path;
			ComPtr<ID2D1GeometrySink> sink;
			if (FAILED(FplD2DFactory()->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
				return;
			for (auto& line : rings)
				AddOpen(sink.Get(), line, points);
			for (auto& line : spokes)
				AddOpen(sink.Get(), line, points);
			sink->Close();
			rt->DrawGeometry(path.Get(), s.Brush(color), pixel);
		};
		if (c.crGridMinor != c.crBack)
			draw(polar.minorRings, polar.minorSpokes, c.crGridMinor);
		draw(polar.majorRings, polar.majorSpokes, c.crGrid);
	}
	else if (!(style & FPLS_NOGRID)) {
		auto* minor = s.Brush(c.crGridMinor);
		auto* major = s.Brush(c.crGrid);
		rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		for (double x : c.crGridMinor == c.crBack ? std::vector<double>{} : tx.minor) {
			float sx = snap(v.ToScreenX(x));
			rt->DrawLine({ sx, 0 }, { sx, h }, minor, pixel);
		}
		for (double y : c.crGridMinor == c.crBack ? std::vector<double>{} : ty.minor) {
			float sy = snap(v.ToScreenY(y));
			rt->DrawLine({ 0, sy }, { w, sy }, minor, pixel);
		}
		for (double x : tx.major) {
			float sx = snap(v.ToScreenX(x));
			rt->DrawLine({ sx, 0 }, { sx, h }, major, pixel);
		}
		for (double y : ty.major) {
			float sy = snap(v.ToScreenY(y));
			rt->DrawLine({ 0, sy }, { w, sy }, major, pixel);
		}
		rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	}
	if (style & FPLS_NOAXES)
		return;

	// the axes where they are, or the labels along the edge they're beyond
	const float ax = (float)v.AxisLineX(), ay = (float)v.AxisLineY();
	const bool xAxis = ay >= 0 && ay <= h, yAxis = ax >= 0 && ax <= w;
	auto* axis = s.Brush(c.crAxis);
	rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	if (xAxis)
		rt->DrawLine({ 0, snap(ay) }, { w, snap(ay) }, axis, std::max(pixel, std::round(1.5f / pixel) * pixel));
	if (yAxis)
		rt->DrawLine({ snap(ax), 0 }, { snap(ax), h }, axis, std::max(pixel, std::round(1.5f / pixel) * pixel));
	rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

// The ticks' numbers, over the graphs on a patch of the background so a curve doesn't run through them.
void CFunctionPlotCtrl::PaintLabels() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	const DWORD style = GetStyle();
	if (style & FPLS_NOAXES)
		return;
	auto& v = s.view;
	const float w = (float)v.width, h = (float)v.height;
	FplTicks tx = FplMakeTicksX(v, 90, (style & FPLS_PITICKS) != 0);
	FplTicks ty = FplMakeTicksY(v, 60);
	const float ax = (float)v.AxisLineX(), ay = (float)v.AxisLineY();
	const bool xAxis = ay >= 0 && ay <= h, yAxis = ax >= 0 && ax <= w;
	auto* patch = s.Brush(c.crBack);
	patch->SetOpacity(0.75f);
	auto label = [&](const std::wstring& text, IDWriteTextFormat* format, D2D1_RECT_F rc) {
		float tw = TextWidth(text, format) + 4;
		float x0 = format == s.labelCenter.Get() ? (rc.left + rc.right - tw) / 2
			: format == s.labelRight.Get() ? rc.right - tw + 2 : rc.left - 2;
		rt->FillRectangle({ x0, (rc.top + rc.bottom) / 2 - 8, x0 + tw, (rc.top + rc.bottom) / 2 + 8 }, patch);
		Text(rt, text, format, rc, s.Brush(c.crLabel));
	};
	const float labelHeight = 16;
	const FplPolarGrid polar = (style & FPLS_POLARGRID) ? FplMakePolarGrid(v) : FplPolarGrid{};
	if (!polar.Empty()) {
		for (auto& l : polar.radiusLabels) {
			const float lx = (float)l.at.x, top = (float)l.at.y + 3;
			label(FplFormatTick(l.value, polar.radii), s.labelCenter.Get(), { lx - 60, top, lx + 60, top + labelHeight });
		}
		for (auto& l : polar.angleLabels) {
			const float lx = (float)l.at.x, mid = (float)l.at.y;
			label(FplFormatAngle(l.value, (style & FPLS_PITICKS) != 0), s.labelCenter.Get(), { lx - 60, mid - 9, lx + 60, mid + 9 });
		}
		if (xAxis && yAxis)
			label(L"0", s.labelRight.Get(), { ax - 40, ay + 3, ax - 5, ay + 3 + labelHeight });
		patch->SetOpacity(1);
		return;
	}
	// x labels under the axis (above it at the bottom)
	float ly = std::clamp(ay + 3, 2.0f, h - labelHeight - 2);
	for (double x : tx.major) {
		if (x == 0)
			continue;
		float sx = (float)v.ToScreenX(x);
		if (sx < 12 || sx > w - 12)
			continue;
		label(FplFormatTick(x, tx), s.labelCenter.Get(), { sx - 60, ly, sx + 60, ly + labelHeight });
	}
	// y labels left of the axis (right of the left edge when it's off to the left)
	const bool leftEdge = ax < 40;
	for (double y : ty.major) {
		if (y == 0)
			continue;
		float sy = (float)v.ToScreenY(y);
		if (sy < 10 || sy > h - 10)
			continue;
		std::wstring text = FplFormatTick(y, ty);
		if (leftEdge)
			label(text, s.labelLeft.Get(), { std::max(ax, 0.0f) + 5, sy - 9, std::max(ax, 0.0f) + 150, sy + 9 });
		else
			label(text, s.labelRight.Get(), { std::min(ax, w) - 150, sy - 9, std::min(ax, w) - 5, sy + 9 });
	}
	if (xAxis && yAxis)
		label(L"0", s.labelRight.Get(), { ax - 40, ay + 3, ax - 5, ay + 3 + labelHeight });
	patch->SetOpacity(1);
}

// The graphs' labels beside their points (or their curves' ends), and the axes' titles.
void CFunctionPlotCtrl::PaintGraphLabels() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	auto& v = s.view;
	const float w = (float)v.width, h = (float)v.height;
	auto* patch = s.Brush(c.crBack);
	auto place = [&](const std::wstring& text, float x, float y, ID2D1Brush* brush) {
		const float tw = TextWidth(text, s.traceText.Get()) + 6;
		if (x + tw > w - 4)
			x -= tw + 18;
		x = std::max(x, 4.0f);
		y = std::clamp(y, 2.0f, h - 20);
		patch->SetOpacity(0.75f);
		rt->FillRoundedRectangle({ { x, y, x + tw, y + 18 }, 3, 3 }, patch);
		patch->SetOpacity(1);
		Text(rt, text, s.traceText.Get(), { x + 3, y, x + tw, y + 18 }, brush);
	};
	for (auto& p : s.plot.LabelPlaces(v))
		place(p.text, (float)p.at.x + 9, (float)p.at.y - 22, s.Brush(GraphColor(p.graph)));
	if (GetStyle() & FPLS_NOAXES)
		return;
	const float ax = (float)v.AxisLineX(), ay = (float)v.AxisLineY();
	auto* title = s.Brush(c.crPanelText);
	if (!s.axisTitles[0].empty()) {
		const float tw = TextWidth(s.axisTitles[0], s.traceText.Get()) + 6;
		place(s.axisTitles[0], w - tw - 6, std::clamp(ay, 0.0f, h) - 24, title);
	}
	if (!s.axisTitles[1].empty()) {
		// beside the panel when the axis runs under it
		float x = std::clamp(ax, 0.0f, w) + 8;
		if (x < s.panel.right && s.panel.bottom > 6)
			x = s.panel.right + 40;
		place(s.axisTitles[1], x, 6, title);
	}
}

// A bar of the colors beside each heat map (2D) or each surface colored by height (3D), at the right, with its values.
void CFunctionPlotCtrl::PaintColorScales() {
	auto& s = *m_State;
	if ((GetStyle() & FPLS_NOCOLORSCALE))
		return;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	const float w = ToDips(s.client.cx), h = ToDips(s.client.cy);
	const float barWidth = 14, barHeight = std::min(220.0f, h * 0.4f), slot = 76;
	struct Scale {
		int graph;
		double lo, hi;
		float opacity;
	};
	std::vector<Scale> scales;
	for (size_t i = 0; i < s.plot.graphs.size(); i++) {
		auto& g = s.plot.graphs[i];
		if (!g.Drawable())
			continue;
		if (s.mode3D) {
			const bool surface = g.expr.kind == FplKind::Surface || g.expr.kind == FplKind::ParametricSurface ||
				g.expr.kind == FplKind::ImplicitSurface;
			// colored by height in the box, every one alike: one bar says it
			if (surface && (g.flags & FplGraphHeatmap) && scales.empty())
				scales.push_back({ (int)i, s.box.zMin, s.box.zMax, 1 });
		}
		else if (!g.geometry.heatmap.Empty())
			scales.push_back({ (int)i, g.geometry.heatmap.lo, g.geometry.heatmap.hi,
				g.fillOpacity >= 0 ? g.fillOpacity : 0.85f });
	}
	if (scales.empty() || barHeight < 60)
		return;
	const float pixel = 96.0f / s.paintDpi;
	float right = w - 12;
	const float top = 14;
	for (auto& scale : scales) {
		const float left = right - barWidth;
		// not over the panel; nor past the left, with many
		if (left - slot + barWidth < (s.panel.right > s.panel.left ? s.panel.right : 0))
			break;
		// its labels readable over a heat map (3D's background is plain, and a patch would fade the box's ticks)
		if (!s.mode3D) {
			auto* patch = s.Brush(c.crBack);
			patch->SetOpacity(0.75f);
			rt->FillRoundedRectangle({ { right - slot + 4, top - 8, right + 6, top + barHeight + 8 }, 4, 4 }, patch);
			patch->SetOpacity(1);
		}
		// the colors, a strip per pixel or two, drawn over the background as the map is
		const int strips = std::clamp((int)(barHeight / 2), 16, 128);
		rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		for (int k = 0; k < strips; k++) {
			const float y0 = top + barHeight * k / strips, y1 = top + barHeight * (k + 1) / strips;
			auto* strip = s.Brush(FplColormap(1 - (k + 0.5) / strips));
			strip->SetOpacity(scale.opacity);
			rt->FillRectangle({ left, y0, right, y1 + pixel }, strip);
			strip->SetOpacity(1);
		}
		rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		rt->DrawRectangle({ left, top, right, top + barHeight }, s.Brush(c.crAxis), pixel);
		// whose it is, with more than one
		if (scales.size() > 1)
			rt->FillRectangle({ left, top - 5, right, top - 2 }, s.Brush(GraphColor(scale.graph)));
		auto* label = s.Brush(c.crLabel);
		auto* axis = s.Brush(c.crAxis);
		for (auto& tick : FplMakeColorScale(scale.lo, scale.hi, barHeight)) {
			const float y = top + barHeight * (1 - (float)tick.fraction);
			rt->DrawLine({ left - 3, y }, { left, y }, axis, pixel);
			Text(rt, tick.text, s.labelRight.Get(), { left - slot, y - 9, left - 5, y + 9 }, label);
		}
		right -= slot;
	}
}

// The rectangle being dragged to zoom to.
void CFunctionPlotCtrl::PaintBox() {
	auto& s = *m_State;
	if (s.drag != FplDrag::Box || !s.boxShown || s.exporting)
		return;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	D2D1_RECT_F r = { ToDips(std::min(s.boxStart.x, s.boxEnd.x)), ToDips(std::min(s.boxStart.y, s.boxEnd.y)),
		ToDips(std::max(s.boxStart.x, s.boxEnd.x)), ToDips(std::max(s.boxStart.y, s.boxEnd.y)) };
	auto* brush = s.Brush(c.crPalette[1]);
	brush->SetOpacity(0.12f);
	rt->FillRectangle(r, brush);
	brush->SetOpacity(1);
	rt->DrawRectangle(r, brush, 96.0f / s.paintDpi);
}

void CFunctionPlotCtrl::PaintGraphs() {
	auto& s = *m_State;
	auto* rt = s.rt;
	auto& graphs = s.plot.graphs;
	// the regions first, under every curve
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		auto& p = s.paths[i];
		if (!graphs[i].Drawable() || (!p.fills && !p.staleFills))
			continue;
		auto* brush = s.Brush(GraphColor((int)i));
		const float fill = graphs[i].fillOpacity >= 0 ? graphs[i].fillOpacity : IsHighContrast() ? 0.35f : 0.22f;
		brush->SetOpacity(fill);
		DrawAroundStale(rt, p, s.view, [&] {
			if (p.fills)
				rt->FillGeometry(p.fills.Get(), brush);
		}, [&] {
			if (p.staleFills)
				rt->FillGeometry(p.staleFills.Get(), brush);
		});
		brush->SetOpacity(1);
	}
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		auto& p = s.paths[i];
		if (graphs[i].Drawable() && (p.contours || p.staleContours)) {
			auto* brush = s.Brush(GraphColor((int)i));
			brush->SetOpacity(graphs[i].opacity);
			DrawAroundStale(rt, p, s.view, [&] {
				if (p.contours)
					rt->DrawGeometry(p.contours.Get(), brush, 1.25f, s.roundStroke.Get());
			}, [&] {
				if (p.staleContours)
					rt->DrawGeometry(p.staleContours.Get(), brush, 1.25f, s.roundStroke.Get());
			});
			brush->SetOpacity(1);
		}
	}
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		auto& g = graphs[i];
		if (!g.Drawable())
			continue;
		auto* brush = s.Brush(GraphColor((int)i));
		// asymptotes first, thin and faint: they're not the curve
		if (!g.geometry.asymptotes.empty()) {
			auto& v = s.view;
			brush->SetOpacity(g.opacity * 0.5f);
			const bool upright = g.expr.kind == FplKind::Function;
			for (double a : g.geometry.asymptotes) {
				const float at = (float)a;
				rt->DrawLine(upright ? D2D1::Point2F(at, 0) : D2D1::Point2F(0, at),
					upright ? D2D1::Point2F(at, (float)v.height) : D2D1::Point2F((float)v.width, at),
					brush, 1.25f, s.dashStroke.Get());
			}
		}
		brush->SetOpacity(g.opacity);
		// a system's arrows fainter under its trajectories
		if (s.paths[i].arrows) {
			if (g.expr.kind == FplKind::OdeSystem && !g.geometry.lines.empty())
				brush->SetOpacity(g.opacity * 0.45f);
			rt->DrawGeometry(s.paths[i].arrows.Get(), brush, 1.5f, s.roundStroke.Get());
			brush->SetOpacity(g.opacity);
		}
		if (s.paths[i].marks) {
			// fainter under a solution
			brush->SetOpacity(g.opacity * (g.geometry.lines.empty() ? 0.75f : 0.4f));
			rt->DrawGeometry(s.paths[i].marks.Get(), brush, 1.25f, s.roundStroke.Get());
			brush->SetOpacity(g.opacity);
		}
		float width = g.width;
		if ((int)i == s.traceGraph && s.traceValid)
			width += 1;
		if (s.paths[i].lines || s.paths[i].staleLines) {
			bool dashed = g.geometry.dashed || (g.flags & FplGraphDashed);
			auto* stroke = (g.flags & FplGraphDotted) ? s.dotStroke.Get() : dashed ? s.dashStroke.Get() : s.roundStroke.Get();
			auto& p = s.paths[i];
			DrawAroundStale(rt, p, s.view, [&] {
				if (p.lines)
					rt->DrawGeometry(p.lines.Get(), brush, width, stroke);
			}, [&] {
				if (p.staleLines)
					rt->DrawGeometry(p.staleLines.Get(), brush, width, stroke);
			});
		}
		brush->SetOpacity(1);
		const bool hollow = (g.flags & FplGraphHollow) != 0;
		auto* back = s.Brush(GetColors().crBack);
		// holes: open circles on the curve
		for (auto& p : g.geometry.holes) {
			D2D1_ELLIPSE ring = D2D1::Ellipse(Pt(p), 2.5f + width, 2.5f + width);
			rt->FillEllipse(ring, back);
			brush->SetOpacity(g.opacity);
			rt->DrawEllipse(ring, brush, 2);
			brush->SetOpacity(1);
		}
		for (auto& p : g.geometry.points) {
			if (!std::isfinite(p.x) || !std::isfinite(p.y))
				continue;
			D2D1_ELLIPSE dot = D2D1::Ellipse(Pt(p), 2.5f + width, 2.5f + width);
			brush->SetOpacity(g.opacity);
			if (hollow) {
				rt->FillEllipse(dot, back);
				rt->DrawEllipse(dot, brush, 2);
			}
			else
				rt->FillEllipse(dot, brush);
			brush->SetOpacity(1);
			// a draggable point: a ring around it, stronger under the mouse
			const size_t index = &p - g.geometry.points.data();
			const auto [dragX, dragY] = g.DragOf(index);
			if ((dragX >= 0 || dragY >= 0) && !(g.flags & FplGraphNoDrag)) {
				const bool hot = (int)i == s.hotPoint && (int)index == s.hotPointIndex;
				const float ring = 6.5f + width + (hot ? 2 : 0);
				brush->SetOpacity(hot ? 0.55f : 0.3f);
				rt->DrawEllipse(D2D1::Ellipse(Pt(p), ring, ring), brush, 2);
				brush->SetOpacity(1);
			}
		}
	}
}

void CFunctionPlotCtrl::PaintTrace() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	auto& v = s.view;
	// the features of the graph being traced
	if (s.featuresGraph >= 0 && s.featuresGraph < (int)s.plot.graphs.size()) {
		auto* ring = s.Brush(c.crLabel);
		auto* back = s.Brush(c.crBack);
		for (auto& f : s.plot.graphs[s.featuresGraph].features) {
			FplPoint p = v.ToScreen({ f.x, f.y });
			if (p.x < -10 || p.y < -10 || p.x > v.width + 10 || p.y > v.height + 10)
				continue;
			D2D1_ELLIPSE e = D2D1::Ellipse(Pt(p), 4, 4);
			rt->FillEllipse(e, back);
			rt->DrawEllipse(e, ring, 1.5f);
		}
	}
	if (!s.traceValid || s.traceGraph < 0 || s.traceGraph >= (int)s.plot.graphs.size())
		return;
	FplPoint p = v.ToScreen({ s.traceX, s.traceY });
	if (s.mode3D) {
		FplVec3 at = s.box.ToBox(s.traceX, s.traceY, s.traceZ);
		if (!s.camera.Project(at, (float)v.width, (float)v.height, p))
			return;
	}
	if (!std::isfinite(p.x) || !std::isfinite(p.y))
		return;
	auto* color = s.Brush(GraphColor(s.traceGraph));
	// the tangent through it, across the view
	double slope = std::numeric_limits<double>::quiet_NaN();
	if (!s.mode3D && !v.logX && !v.logY && (GetStyle() & FPLS_TANGENT)) {
		auto kind = s.plot.graphs[s.traceGraph].expr.kind;
		slope = s.plot.Slope(s.traceGraph, kind == FplKind::FunctionOfY ? s.traceY : s.traceX);
		if (std::isfinite(slope)) {
			const double span = (v.xMax - v.xMin) + (v.yMax - v.yMin);
			FplPoint a, b;
			if (kind == FplKind::Function) {
				a = v.ToScreen({ s.traceX - span, s.traceY - span * slope });
				b = v.ToScreen({ s.traceX + span, s.traceY + span * slope });
			}
			else {
				a = v.ToScreen({ s.traceX - span * slope, s.traceY - span });
				b = v.ToScreen({ s.traceX + span * slope, s.traceY + span });
			}
			auto clampPt = [](FplPoint q) {
				return D2D1::Point2F((float)std::clamp(q.x, -1e6, 1e6), (float)std::clamp(q.y, -1e6, 1e6));
			};
			color->SetOpacity(0.7f);
			rt->DrawLine(clampPt(a), clampPt(b), color, 1.5f, s.dashStroke.Get());
			color->SetOpacity(1);
		}
	}
	D2D1_ELLIPSE dot = D2D1::Ellipse(Pt(p), 5.5f, 5.5f);
	rt->FillEllipse(dot, color);
	rt->DrawEllipse(dot, s.Brush(c.crBack), 2);

	// the label: the feature's name, then the point
	const double unitsX = 1 / v.DipsPerUnitX(s.traceX), unitsY = 1 / v.DipsPerUnitY(s.traceY);
	std::wstring text;
	auto& g = s.plot.graphs[s.traceGraph];
	if (s.traceFeature >= 0 && s.traceFeature < (int)g.features.size())
		text = std::wstring(FeatureName(g.features[s.traceFeature].kind)) + L"  ";
	if (s.mode3D) {
		// to a 400th of each range
		auto& b = s.box;
		text += L"(" + FplFormatCoordinate(s.traceX, (b.xMax - b.xMin) / 4000) + L", " +
			FplFormatCoordinate(s.traceY, (b.yMax - b.yMin) / 4000) + L", " +
			FplFormatCoordinate(s.traceZ, (b.zMax - b.zMin) / 4000) + L")";
	}
	else {
		text += L"(" + FplFormatCoordinate(s.traceX, s.traceFeature >= 0 ? 1e-6 : unitsX) + L", " +
			FplFormatCoordinate(s.traceY, s.traceFeature >= 0 ? 1e-6 : unitsY) + L")";
		// a surface's value: to about five figures
		if (std::isfinite(s.traceZ))
			text += L"   z = " + FplFormatCoordinate(s.traceZ, std::max(std::abs(s.traceZ), 1.0) * 1e-5);
		if (std::isfinite(slope))
			text += L"   slope " + FplFormatCoordinate(slope, std::max(std::abs(slope), 1.0) * 1e-5);
	}
	const float tw = TextWidth(text, s.traceText.Get()) + 14, th = 24;
	float x = (float)p.x + 10, y = (float)p.y - th - 10;
	if (x + tw > v.width - 4)
		x = (float)p.x - 10 - tw;
	if (y < 4)
		y = (float)p.y + 10;
	D2D1_ROUNDED_RECT box = { { x, y, x + tw, y + th }, 4, 4 };
	rt->FillRoundedRectangle(box, s.Brush(c.crPanelBack));
	rt->DrawRoundedRectangle(box, s.Brush(c.crPanelBorder), 96.0f / s.paintDpi);
	Text(rt, text, s.traceText.Get(), { x + 7, y, x + tw, y + th }, s.Brush(c.crPanelText));
}

void CFunctionPlotCtrl::PaintPanel() {
	auto& s = *m_State;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	const float pixel = 96.0f / s.paintDpi;
	auto* text = s.Brush(c.crPanelText);
	auto* muted = s.Brush(c.crLabel);
	// the button that folds the panel (not in an export)
	if (s.toggle.right > s.toggle.left && !s.exporting) {
		D2D1_ROUNDED_RECT button = { s.toggle, 4, 4 };
		rt->FillRoundedRectangle(button, s.Brush(c.crPanelBack));
		if (s.hotToggle) {
			auto* hot = s.Brush(c.crPanelBorder);
			hot->SetOpacity(0.5f);
			rt->FillRoundedRectangle(button, hot);
			hot->SetOpacity(1);
		}
		rt->DrawRoundedRectangle(button, s.Brush(c.crPanelBorder), pixel);
		// ChevronLeft / ChevronRight
		Text(rt, s.collapsed ? L"\xE76C" : L"\xE76B", s.icons.Get(), s.toggle, text);
	}
	if (s.rows.empty())
		return;
	D2D1_ROUNDED_RECT box = { s.panel, 6, 6 };
	rt->FillRoundedRectangle(box, s.Brush(c.crPanelBack));
	rt->DrawRoundedRectangle(box, s.Brush(c.crPanelBorder), pixel);
	const bool focused = ::GetFocus() == m_hWnd && !s.exporting;
	for (size_t r = 0; r < s.rows.size(); r++) {
		auto& row = s.rows[r];
		const float left = row.rect.left, right = row.rect.right, top = row.rect.top;
		if ((int)r == s.hotRow && !s.exporting) {
			auto* hot = s.Brush(c.crPanelBorder);
			hot->SetOpacity(0.35f);
			rt->FillRectangle({ left + 1, top, right - 1, row.rect.bottom }, hot);
			hot->SetOpacity(1);
		}
		if (row.kind == FplPanelRow::Graph) {
			auto& g = s.plot.graphs[row.index];
			const float mid = top + row.main / 2;
			const bool hidden = (g.flags & FplGraphHidden) != 0;
			if (!g.definition && !g.listDefinition && g.expr.kind != FplKind::Regression &&
				!(g.expr.kind == FplKind::Definition && !g.expr.error)) {
				auto* color = s.Brush(GraphColor(row.index));
				D2D1_ELLIPSE swatch = D2D1::Ellipse({ left + 15, mid }, 6, 6);
				if (hidden || g.expr.error)
					rt->DrawEllipse(swatch, color, 1.5f);
				else
					rt->FillEllipse(swatch, color);
			}
			std::wstring label = row.math ? std::wstring() : g.text;
			if (g.definition && !g.expr.error) {
				double value = s.plot.DefinitionValue(row.index);
				label += L"   = " + FplFormatCoordinate(value, 1e-6);
			}
			else if (g.expr.kind == FplKind::Integral && !g.expr.error) {
				double value = s.plot.IntegralValue(row.index);
				label += L"   = " + FplFormatCoordinate(value, 1e-6);
			}
			else if (g.listDefinition && !g.expr.error) {
				auto* values = s.plot.ListValues(g.expr.name);
				size_t n = values ? values->size() : 0;
				label += L"   (" + std::to_wstring(n) + (n == 1 ? L" item)" : L" items)");
			}
			float textLeft = left + 28;
			if (row.math) {
				auto& layout = row.math->layout;
				FplDrawMath(rt, *row.math, textLeft, mid + (float)(layout.ascent - layout.descent) / 2, hidden ? muted : text, s.roundStroke.Get());
				textLeft += (float)layout.width;
			}
			Text(rt, label, s.panelText.Get(), { textLeft, mid - FplGraphRow / 2, right - 8, mid + FplGraphRow / 2 }, hidden ? muted : text);
			if (row.error) {
				Text(rt, g.expr.error.Message(), s.panelSmall.Get(),
					{ left + 28, top + row.main - 6, right - 8, top + row.main - 6 + FplErrorLine },
					s.Brush(c.crError));
			}
			else if (row.stopped) {
				Text(rt, L"Stops early: it ran out of steps", s.panelSmall.Get(),
					{ left + 28, top + row.main - 6, right - 8, top + row.main - 6 + FplErrorLine }, muted);
			}
			else if (row.note) {
				// the fit: its names' values and R squared (U+00B2)
				std::wstring fit;
				for (auto& [name, value] : s.plot.Fitted(row.index))
					fit += name + L" = " + FplFormatCoordinate(value, std::max(std::abs(value), 1.0) * 1e-5) + L",  ";
				fit += L"R\x00B2 = " + FplFormatCoordinate(g.r2, 1e-5);
				Text(rt, fit, s.panelSmall.Get(),
					{ left + 28, top + row.main - 6, right - 8, top + row.main - 6 + FplErrorLine }, muted);
			}
		}
		else {
			auto& p = s.plot.params[row.index];
			const float mid = top + FplSliderRow / 2;
			auto* accent = s.Brush(c.crPalette[1]);
			// Play / Pause
			Text(rt, p.animating ? L"\xE769" : L"\xE768", s.icons.Get(), row.play, accent);
			Text(rt, p.name, s.panelText.Get(), { row.play.right + 4, top, row.track.left - 8, top + FplSliderRow }, text);
			// the track, filled up to the value
			const double t = p.max > p.min ? std::clamp((p.value - p.min) / (p.max - p.min), 0.0, 1.0) : 0;
			const float tx = row.track.left + (float)t * (row.track.right - row.track.left);
			D2D1_ROUNDED_RECT track = { row.track, 2, 2 };
			rt->FillRoundedRectangle(track, s.Brush(c.crPanelBorder));
			D2D1_ROUNDED_RECT filled = { { row.track.left, row.track.top, tx, row.track.bottom }, 2, 2 };
			rt->FillRoundedRectangle(filled, accent);
			D2D1_ELLIPSE thumb = D2D1::Ellipse({ tx, mid }, 7, 7);
			rt->FillEllipse(thumb, accent);
			rt->DrawEllipse(thumb, s.Brush(c.crPanelBack), 2);
			const double unit = p.step > 0 ? p.step / 10 : (p.max - p.min) / 1000;
			Text(rt, FplFormatCoordinate(p.value, unit), s.panelText.Get(),
				{ row.track.right + 12, top, right - 8, top + FplSliderRow }, text);
			// the keys' slider
			if (focused && (int)r == s.focusRow)
				rt->DrawRectangle({ left + 3, top + 2, right - 3, row.rect.bottom - 2 }, text, pixel, s.dashStroke.Get());
		}
	}
}

// ---- 3D --------------------------------------------------------------------------------

namespace {

// The 3D frames' window: paints by asking the control, and lets the mouse through to it.
LRESULT CALLBACK View3DProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_NCHITTEST:
			return HTTRANSPARENT;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			::BeginPaint(hwnd, &ps);
			::EndPaint(hwnd, &ps);
			::SendMessageW(::GetParent(hwnd), CFunctionPlotCtrl::WM_FPL_PAINT3D, 0, 0);
			return 0;
		}
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateView3D(HWND parent, SIZE size) {
	static const ATOM atom = [] {
		WNDCLASSEXW wc{ sizeof(wc) };
		wc.lpfnWndProc = View3DProc;
		wc.hInstance = ATL::_AtlBaseModule.GetModuleInstance();
		wc.lpszClassName = L"WC_FunctionPlot3DView";
		return ::RegisterClassExW(&wc);
	}();
	if (!atom)
		return nullptr;
	return ::CreateWindowExW(0, L"WC_FunctionPlot3DView", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, size.cx,
		size.cy, parent, nullptr, ATL::_AtlBaseModule.GetModuleInstance(), nullptr);
}

}  // namespace

bool CFunctionPlotCtrl::Ensure3D() {
	auto& s = *m_State;
	if (!s.view3D) {
		s.view3D = CreateView3D(m_hWnd, s.client);
		if (!s.view3D)
			return false;
	}
	if (!s.renderer.Ready()) {
		s.gpu.clear();
		s.gpuFills.clear();
		s.gpuFields.clear();
		s.boxEdges = s.floorGrid = {};
		s.brushes.clear();
		if (!s.renderer.Init(s.view3D, s.client.cx, s.client.cy, (float)s.dpi))
			return false;
		s.gpuStale = true;
		MakeResources(s);
	}
	if (s.gpuStale)
		Upload3D();
	return true;
}

void CFunctionPlotCtrl::Upload3D() {
	auto& s = *m_State;
	s.gpuStale = false;
	s.gpu.assign(s.plot.graphs.size(), {});
	s.gpuFills.assign(s.plot.graphs.size(), {});
	s.gpuFields.assign(s.plot.graphs.size(), {});
	for (size_t i = 0; i < s.plot.graphs.size(); i++) {
		s.gpu[i] = s.renderer.Create(s.plot.graphs[i].mesh);
		s.gpuFills[i] = s.renderer.Create(s.plot.graphs[i].fillMesh);
		s.gpuFields[i] = s.renderer.Create(s.plot.graphs[i].fieldMesh);
	}
	s.boxEdges = s.renderer.Create(FplBoxEdges());
	// the floor's grid at the x and y ticks
	FplMesh grid;
	auto line = [&](FplVec3 a, FplVec3 b) {
		uint32_t base = (uint32_t)grid.vertices.size();
		FplVertex3 va, vb;
		va.pos = a;
		vb.pos = b;
		grid.vertices.push_back(va);
		grid.vertices.push_back(vb);
		grid.indices.push_back(base);
		grid.indices.push_back(base + 1);
	};
	FplTicks tx = FplMakeTicks(s.box.xMin, s.box.xMax, 320, 64), ty = FplMakeTicks(s.box.yMin, s.box.yMax, 320, 64);
	for (double x : tx.major) {
		float b = s.box.ToBox(x, 0, 0).x;
		line({ b, -1, -1 }, { b, 1, -1 });
	}
	for (double y : ty.major) {
		float b = s.box.ToBox(0, y, 0).y;
		line({ -1, b, -1 }, { 1, b, -1 });
	}
	s.floorGrid = s.renderer.Create(grid);
}

void CFunctionPlotCtrl::Paint3D(bool present) {
	auto& s = *m_State;
	if (!Ensure3D()) {
		// no Direct3D 11 at all: say so (a plain GDI paint)
		if (s.view3D) {
			::DestroyWindow(s.view3D);
			s.view3D = nullptr;
		}
		HDC dc = ::GetDC(m_hWnd);
		RECT rc;
		GetClientRect(&rc);
		::FillRect(dc, &rc, ::GetSysColorBrush(COLOR_WINDOW));
		::SetBkMode(dc, TRANSPARENT);
		::SetTextColor(dc, ::GetSysColor(COLOR_WINDOWTEXT));
		::DrawTextW(dc, L"3D needs Direct3D 11", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		::ReleaseDC(m_hWnd, dc);
		ValidateRect(nullptr);
		return;
	}
	const FPLCOLORS c = GetColors();
	const float w = ToDips(s.client.cx), h = ToDips(s.client.cy);
	auto& r = s.renderer;
	r.Begin(c.crBack, s.camera.ViewProjection(w / std::max(h, 1.0f)), s.camera.Eye());
	if (!(GetStyle() & FPLS_NOGRID))
		r.Draw(s.floorGrid, { c.crGrid, false, true, false });
	if (!(GetStyle() & FPLS_NOAXES))
		r.Draw(s.boxEdges, { c.crAxis, false, true, false });
	auto styleOf = [&](size_t i) {
		auto& g = s.plot.graphs[i];
		const bool surface = g.expr.kind == FplKind::Surface || g.expr.kind == FplKind::ParametricSurface ||
			g.expr.kind == FplKind::ImplicitSurface;
		FplDrawStyle style;
		style.color = GraphColor((int)i);
		style.colormap = surface && (g.flags & FplGraphHeatmap);
		style.clip = true;
		style.grid = s.plot.LineGridOf((int)i, s.box);
		if (IsHighContrast())
			style.lineColor = c.crLabel;
		return style;
	};
	// what's solid first: the see-through graphs are seen against it and behind it
	bool translucent = false;
	for (size_t i = 0; i < s.plot.graphs.size() && i < s.gpu.size(); i++) {
		auto& g = s.plot.graphs[i];
		if (!g.Drawable())
			continue;
		if (g.opacity < 1)
			translucent = translucent || s.gpu[i];
		else
			r.Draw(s.gpu[i], styleOf(i));
	}
	// regions are always see-through, as in 2D
	auto fillOpacity = [&](const FplGraph& g) {
		return g.fillOpacity >= 0 ? g.fillOpacity : IsHighContrast() ? 0.35f : 0.22f;
	};
	for (size_t i = 0; i < s.plot.graphs.size() && i < s.gpuFills.size(); i++) {
		auto& g = s.plot.graphs[i];
		translucent = translucent || (g.Drawable() && s.gpuFills[i] && fillOpacity(g) > 0) ||
			(g.Drawable() && i < s.gpuFields.size() && s.gpuFields[i] && g.opacity > 0);
	}
	if (translucent && r.BeginTranslucent()) {
		for (size_t i = 0; i < s.plot.graphs.size() && i < s.gpu.size(); i++) {
			auto& g = s.plot.graphs[i];
			if (g.Drawable() && g.opacity < 1 && g.opacity > 0)
				r.DrawTranslucent(s.gpu[i], styleOf(i), g.opacity);
			if (g.Drawable() && i < s.gpuFills.size() && s.gpuFills[i] && fillOpacity(g) > 0) {
				FplDrawStyle fill;
				fill.color = GraphColor((int)i);
				fill.clip = true;
				fill.unlit = true;
				r.DrawTranslucent(s.gpuFills[i], fill, fillOpacity(g));
			}
			if (g.Drawable() && i < s.gpuFields.size() && s.gpuFields[i] && g.opacity > 0) {
				FplDrawStyle field;
				field.color = GraphColor((int)i);
				field.clip = true;
				field.unlit = true;
				r.DrawTranslucent(s.gpuFields[i], field, g.opacity * g.fieldOpacity);
			}
		}
		r.EndTranslucent();
	}
	if (auto* dc = r.BeginOverlay()) {
		s.rt = dc;
		s.paintDpi = (float)s.dpi;
		s.exporting = !present;
		// an exported frame's text without ClearType's colored fringes
		dc->SetTextAntialiasMode(present ? D2D1_TEXT_ANTIALIAS_MODE_DEFAULT : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		PaintTicks3D();
		PaintColorScales();
		PaintPoints3D();
		PaintTrace();
		PaintPanel();
		r.EndOverlay();
		s.rt = nullptr;
		s.exporting = false;
		// the overlay's brushes belong to its device: kept for the next frame
	}
	if (present && !r.Present()) {
		// the device was lost: start again on the next paint
		r.Reset();
		s.gpu.clear();
		s.gpuFills.clear();
		s.gpuFields.clear();
		s.brushes.clear();
		Invalidate();
	}
	ValidateRect(nullptr);
}

// A ring around each point that can be dragged, stronger under the mouse, as in 2D.
void CFunctionPlotCtrl::PaintPoints3D() {
	auto& s = *m_State;
	if (s.exporting)
		return;
	auto* rt = s.rt;
	const float w = ToDips(s.client.cx), h = ToDips(s.client.cy);
	for (size_t i = 0; i < s.plot.graphs.size(); i++) {
		auto& g = s.plot.graphs[i];
		if (!g.Drawable() || (g.flags & FplGraphNoDrag))
			continue;
		const auto points = s.plot.Points3D((int)i);
		for (size_t k = 0; k < points.size(); k++) {
			const auto [px, py] = g.DragOf(k);
			if (px < 0 && py < 0 && g.DragZOf(k) < 0)
				continue;
			const FplVec3 c = s.box.ToBox(points[k]);
			FplPoint on;
			if (!(std::abs(c.x) <= 1 && std::abs(c.y) <= 1 && std::abs(c.z) <= 1) || !s.camera.Project(c, w, h, on) ||
				s.plot.HiddenBySurface((int)i, c, s.camera, w, h))
				continue;
			const bool hot = (int)i == s.hotPoint && (int)k == s.hotPointIndex;
			auto* brush = s.Brush(GraphColor((int)i));
			brush->SetOpacity(hot ? 0.75f : 0.4f);
			const float ring = hot ? 10.0f : 8.0f;
			rt->DrawEllipse(D2D1::Ellipse(Pt(on), ring, ring), brush, 2);
			brush->SetOpacity(1);
		}
	}
}

void CFunctionPlotCtrl::PaintTicks3D() {
	auto& s = *m_State;
	if (GetStyle() & FPLS_NOAXES)
		return;
	auto* rt = s.rt;
	const FPLCOLORS c = GetColors();
	const float w = ToDips(s.client.cx), h = ToDips(s.client.cy);
	auto* label = s.Brush(c.crLabel);
	auto* name = s.Brush(c.crPanelText);
	for (auto& t : FplBoxTicks(s.box, s.camera)) {
		FplPoint p;
		if (!s.camera.Project(t.at, w, h, p))
			continue;
		const float x = (float)p.x, y = (float)p.y;
		std::wstring text = t.text;
		if (t.name) {
			int axis = text == L"x" ? 0 : text == L"y" ? 1 : 2;
			if (!s.axisTitles[axis].empty())
				text = s.axisTitles[axis];
		}
		Text(rt, text, s.labelCenter.Get(), { x - 80, y - 10, x + 80, y + 10 }, t.name ? name : label);
	}
}
