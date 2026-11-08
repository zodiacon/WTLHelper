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

ComPtr<IDWriteTextFormat> MakeFormat(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
	ComPtr<IDWriteTextFormat> format;
	if (FAILED(FplDWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
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
	if (!brush && target)
		target->CreateSolidColorBrush(Color(color), brush.GetAddressOf());
	return brush.Get();
}

// ---- Resources -------------------------------------------------------------------------

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
	}
	return true;
}

void CFunctionPlotCtrl::DiscardTarget() {
	auto& s = *m_State;
	s.target.Reset();
	s.brushes.clear();
}

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
	}
}

// ---- The panel's layout ----------------------------------------------------------------

void CFunctionPlotCtrl::LayoutPanel() {
	auto& s = *m_State;
	s.rows.clear();
	s.panel = {};
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
			row.track = { left + 52, mid - 2, right - 66, mid + 2 };
			s.rows.push_back(row);
			y += FplSliderRow;
		}
	}
	if (s.rows.empty())
		return;
	s.panel = { left, FplPanelMargin, right, y + FplPanelPad };
	if (s.hotRow >= (int)s.rows.size())
		s.hotRow = -1;
}

// ---- Painting --------------------------------------------------------------------------

void CFunctionPlotCtrl::Paint() {
	auto& s = *m_State;
	if (!CreateTarget())
		return;
	const FPLCOLORS c = GetColors();
	s.target->BeginDraw();
	s.target->SetTransform(D2D1::Matrix3x2F::Identity());
	s.target->Clear(Color(c.crBack));
	if (s.view.Valid()) {
		PaintGrid();
		PaintGraphs();
		PaintLabels();
		PaintTrace();
		PaintPanel();
	}
	if (s.target->EndDraw() == D2DERR_RECREATE_TARGET) {
		DiscardTarget();
		Invalidate();
	}
}

void CFunctionPlotCtrl::PaintGrid() {
	auto& s = *m_State;
	auto* rt = s.target.Get();
	const FPLCOLORS c = GetColors();
	const DWORD style = GetStyle();
	auto& v = s.view;
	const float w = (float)v.width, h = (float)v.height;
	const float pixel = 96.0f / s.dpi;
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
	auto* rt = s.target.Get();
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
	auto* rt = s.target.Get();
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
		auto& g = graphs[i];
		if (!g.Drawable())
			continue;
		auto* brush = s.Brush(GraphColor((int)i));
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
	auto* rt = s.target.Get();
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
	rt->DrawRoundedRectangle(box, s.Brush(c.crPanelBorder), 96.0f / s.dpi);
	Text(rt, text, s.traceText.Get(), { x + 7, y, x + tw, y + th }, s.Brush(c.crPanelText));
}

void CFunctionPlotCtrl::PaintPanel() {
	auto& s = *m_State;
	if (s.rows.empty())
		return;
	auto* rt = s.target.Get();
	const FPLCOLORS c = GetColors();
	const float pixel = 96.0f / s.dpi;
	D2D1_ROUNDED_RECT box = { s.panel, 6, 6 };
	rt->FillRoundedRectangle(box, s.Brush(c.crPanelBack));
	rt->DrawRoundedRectangle(box, s.Brush(c.crPanelBorder), pixel);
	auto* text = s.Brush(c.crPanelText);
	auto* muted = s.Brush(c.crLabel);
	for (size_t r = 0; r < s.rows.size(); r++) {
		auto& row = s.rows[r];
		const float left = row.rect.left, right = row.rect.right, top = row.rect.top;
		if ((int)r == s.hotRow) {
			auto* hot = s.Brush(c.crPanelBorder);
			hot->SetOpacity(0.35f);
			rt->FillRectangle({ left + 1, top, right - 1, row.rect.bottom }, hot);
			hot->SetOpacity(1);
		}
		if (row.kind == FplPanelRow::Graph) {
			auto& g = s.plot.graphs[row.index];
			const float mid = top + FplGraphRow / 2;
			const bool hidden = (g.flags & FplGraphHidden) != 0;
			if (!g.definition) {
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
			Text(rt, label, s.panelText.Get(), { left + 28, top, right - 8, top + FplGraphRow },
				hidden ? muted : text);
			if (row.error) {
				Text(rt, g.expr.error.Message(), s.panelSmall.Get(),
					{ left + 28, top + FplGraphRow - 6, right - 8, top + FplGraphRow - 6 + FplErrorLine },
					s.Brush(c.crError));
			}
		}
		else {
			auto& p = s.plot.params[row.index];
			const float mid = top + FplSliderRow / 2;
			Text(rt, p.name, s.panelText.Get(), { left + 10, top, row.track.left - 8, top + FplSliderRow }, text);
			// the track, filled up to the value
			const double t = p.max > p.min ? std::clamp((p.value - p.min) / (p.max - p.min), 0.0, 1.0) : 0;
			const float tx = row.track.left + (float)t * (row.track.right - row.track.left);
			auto* accent = s.Brush(GetColors().crPalette[1]);
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
		}
	}
}
