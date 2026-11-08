// The function plot's painting: Direct2D resources, the grid and axes with their labels, the graphs, the trace, and
// the panel of the legend and the sliders.

#include "FunctionPlotData.h"

#include <algorithm>
#include <cmath>
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
	for (auto& p : s.paths)
		p.heat.Reset();
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
void AddArrow(ID2D1GeometrySink* sink, const FplArrow& a, std::vector<D2D1_POINT_2F>& points) {
	double len = std::hypot(a.to.x - a.from.x, a.to.y - a.from.y);
	if (len <= 0)
		return;
	double ux = (a.to.x - a.from.x) / len, uy = (a.to.y - a.from.y) / len, head = std::min(7.0, 0.35 * len);
	const double c = std::cos(0.45), sn = std::sin(0.45);
	FplPoint l = { a.to.x - head * (ux * c - uy * sn), a.to.y - head * (uy * c + ux * sn) };
	FplPoint r = { a.to.x - head * (ux * c + uy * sn), a.to.y - head * (uy * c - ux * sn) };
	AddOpen(sink, { a.from, a.to }, points);
	AddOpen(sink, { l, a.to, r }, points);
}

}  // namespace

void CFunctionPlotCtrl::BuildGeometries() {
	auto& s = *m_State;
	s.paths.assign(s.plot.graphs.size(), {});
	for (size_t i = 0; i < s.plot.graphs.size(); i++) {
		auto& geometry = s.plot.graphs[i].geometry;
		auto& paths = s.paths[i];
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
			float h = FplGraphRow + (row.error ? FplErrorLine : 0);
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
		PaintTrace();
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
		if (!s.plot.graphs[i].Drawable() || p.heatPixels.empty())
			continue;
		if (!p.heat) {
			auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
			if (FAILED(rt->CreateBitmap(D2D1::SizeU(p.heatCols, p.heatRows), p.heatPixels.data(), p.heatCols * 4, props,
				p.heat.GetAddressOf())))
				continue;
		}
		rt->DrawBitmap(p.heat.Get(), D2D1::RectF(0, 0, p.heatCols * p.heatCell, p.heatRows * p.heatCell), 0.85f,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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
		for (auto& label : s.paths[i].labels) {
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

	FplTicks tx = FplMakeTicks(v.xMin, v.xMax, v.width, 90, (style & FPLS_PITICKS) != 0);
	FplTicks ty = FplMakeTicks(v.yMin, v.yMax, v.height, 60);
	if (!(style & FPLS_NOGRID)) {
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
	const float ax = (float)v.ToScreenX(0), ay = (float)v.ToScreenY(0);
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
	FplTicks tx = FplMakeTicks(v.xMin, v.xMax, v.width, 90, (style & FPLS_PITICKS) != 0);
	FplTicks ty = FplMakeTicks(v.yMin, v.yMax, v.height, 60);
	const float ax = (float)v.ToScreenX(0), ay = (float)v.ToScreenY(0);
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

void CFunctionPlotCtrl::PaintGraphs() {
	auto& s = *m_State;
	auto* rt = s.rt;
	auto& graphs = s.plot.graphs;
	// the regions first, under every curve
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		if (!graphs[i].Drawable() || !s.paths[i].fills)
			continue;
		auto* brush = s.Brush(GraphColor((int)i));
		brush->SetOpacity(IsHighContrast() ? 0.35f : 0.22f);
		rt->FillGeometry(s.paths[i].fills.Get(), brush);
		brush->SetOpacity(1);
	}
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		if (graphs[i].Drawable() && s.paths[i].contours)
			rt->DrawGeometry(s.paths[i].contours.Get(), s.Brush(GraphColor((int)i)), 1.25f, s.roundStroke.Get());
	}
	for (size_t i = 0; i < graphs.size() && i < s.paths.size(); i++) {
		auto& g = graphs[i];
		if (!g.Drawable())
			continue;
		auto* brush = s.Brush(GraphColor((int)i));
		if (s.paths[i].arrows)
			rt->DrawGeometry(s.paths[i].arrows.Get(), brush, 1.5f, s.roundStroke.Get());
		float width = g.width;
		if ((int)i == s.traceGraph && s.traceValid)
			width += 1;
		if (s.paths[i].lines) {
			bool dashed = g.geometry.dashed || (g.flags & FplGraphDashed);
			rt->DrawGeometry(s.paths[i].lines.Get(), brush, width, dashed ? s.dashStroke.Get() : s.roundStroke.Get());
		}
		for (auto& p : g.geometry.points) {
			D2D1_ELLIPSE dot = D2D1::Ellipse(Pt(p), 2.5f + width, 2.5f + width);
			rt->FillEllipse(dot, brush);
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
	if (!std::isfinite(p.x) || !std::isfinite(p.y))
		return;
	auto* color = s.Brush(GraphColor(s.traceGraph));
	D2D1_ELLIPSE dot = D2D1::Ellipse(Pt(p), 5.5f, 5.5f);
	rt->FillEllipse(dot, color);
	rt->DrawEllipse(dot, s.Brush(c.crBack), 2);

	// the label: the feature's name, then the point
	const double unitsX = 1 / v.ScaleX(), unitsY = 1 / v.ScaleY();
	std::wstring text;
	auto& g = s.plot.graphs[s.traceGraph];
	if (s.traceFeature >= 0 && s.traceFeature < (int)g.features.size())
		text = std::wstring(FeatureName(g.features[s.traceFeature].kind)) + L"  ";
	text += L"(" + FplFormatCoordinate(s.traceX, s.traceFeature >= 0 ? 1e-6 : unitsX) + L", " +
		FplFormatCoordinate(s.traceY, s.traceFeature >= 0 ? 1e-6 : unitsY) + L")";
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
			const float mid = top + FplGraphRow / 2;
			const bool hidden = (g.flags & FplGraphHidden) != 0;
			if (!g.definition && !(g.expr.kind == FplKind::Definition && !g.expr.error)) {
				auto* color = s.Brush(GraphColor(row.index));
				D2D1_ELLIPSE swatch = D2D1::Ellipse({ left + 15, mid }, 6, 6);
				if (hidden || g.expr.error)
					rt->DrawEllipse(swatch, color, 1.5f);
				else
					rt->FillEllipse(swatch, color);
			}
			std::wstring label = g.text;
			if (g.definition && !g.expr.error) {
				double value = s.plot.DefinitionValue(row.index);
				label += L"   = " + FplFormatCoordinate(value, 1e-6);
			}
			Text(rt, label, s.panelText.Get(), { left + 28, top, right - 8, top + FplGraphRow }, hidden ? muted : text);
			if (row.error) {
				Text(rt, g.expr.error.Message(), s.panelSmall.Get(),
					{ left + 28, top + FplGraphRow - 6, right - 8, top + FplGraphRow - 6 + FplErrorLine },
					s.Brush(c.crError));
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
	for (size_t i = 0; i < s.plot.graphs.size(); i++)
		s.gpu[i] = s.renderer.Create(s.plot.graphs[i].mesh);
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
		r.Draw(s.floorGrid, c.crGrid, false, true, false);
	if (!(GetStyle() & FPLS_NOAXES))
		r.Draw(s.boxEdges, c.crAxis, false, true, false);
	for (size_t i = 0; i < s.plot.graphs.size() && i < s.gpu.size(); i++) {
		auto& g = s.plot.graphs[i];
		if (!g.Drawable())
			continue;
		bool surface = g.expr.kind == FplKind::Surface || g.expr.kind == FplKind::ParametricSurface;
		r.Draw(s.gpu[i], GraphColor((int)i), surface && (g.flags & FplGraphHeatmap), false, true);
	}
	if (auto* dc = r.BeginOverlay()) {
		s.rt = dc;
		s.paintDpi = (float)s.dpi;
		s.exporting = !present;
		// an exported frame's text without ClearType's colored fringes
		dc->SetTextAntialiasMode(present ? D2D1_TEXT_ANTIALIAS_MODE_DEFAULT : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		PaintTicks3D();
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
		s.brushes.clear();
		Invalidate();
	}
	ValidateRect(nullptr);
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
		Text(rt, t.text, s.labelCenter.Get(), { x - 60, y - 10, x + 60, y + 10 }, t.name ? name : label);
	}
}
