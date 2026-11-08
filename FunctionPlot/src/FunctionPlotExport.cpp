// The graphs as SVG: what the control draws, as paths and text (window-free).

#include "FunctionPlotGraphs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

std::string Utf8(std::wstring_view text) {
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		uint32_t c = text[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
			c = 0x10000 + ((c - 0xD800) << 10) + (text[++i] - 0xDC00);
		if (c < 0x80)
			out += static_cast<char>(c);
		else if (c < 0x800) {
			out += static_cast<char>(0xC0 | (c >> 6));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000) {
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else {
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}
	return out;
}

std::string Escape(std::wstring_view text) {
	std::string s = Utf8(text), out;
	out.reserve(s.size());
	for (char c : s) {
		switch (c) {
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '&': out += "&amp;"; break;
			case '"': out += "&quot;"; break;
			default: out += c;
		}
	}
	return out;
}

std::string Color(uint32_t c) {
	char buf[16];
	snprintf(buf, sizeof(buf), "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
	return buf;
}

std::string Num(double v) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%.2f", v);
	std::string s = buf;
	while (s.back() == '0')
		s.pop_back();
	if (s.back() == '.')
		s.pop_back();
	return s == "-0" ? "0" : s;
}

void AddPolyline(std::string& d, const FplPolyline& line, bool close) {
	for (size_t i = 0; i < line.size(); i++) {
		d += i == 0 ? 'M' : 'L';
		d += Num(line[i].x);
		d += ' ';
		d += Num(line[i].y);
	}
	if (close)
		d += 'Z';
}

void Text(std::string& svg, double x, double y, const char* anchor, uint32_t color, double size, std::wstring_view text) {
	svg += "<text x=\"" + Num(x) + "\" y=\"" + Num(y) + "\" text-anchor=\"" + anchor + "\" fill=\"" + Color(color) +
		"\" font-size=\"" + Num(size) + "\" dominant-baseline=\"central\">" + Escape(text) + "</text>\n";
}

} // namespace

std::string FplWriteSvg(const FplPlot& plot, const FplViewport& view, const FplSvgStyle& style) {
	std::string svg;
	if (!view.Valid())
		return svg;
	const double w = view.width, h = view.height;
	svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + Num(w) + "\" height=\"" + Num(h) + "\" viewBox=\"0 0 " +
		Num(w) + " " + Num(h) + "\" font-family=\"Segoe UI, sans-serif\">\n";
	svg += "<rect width=\"100%\" height=\"100%\" fill=\"" + Color(style.back) + "\"/>\n";

	FplTicks tx = FplMakeTicks(view.xMin, view.xMax, view.width, 90, style.piTicks);
	FplTicks ty = FplMakeTicks(view.yMin, view.yMax, view.height, 60);
	auto gridPath = [&](const std::vector<double>& xs, const std::vector<double>& ys, uint32_t color) {
		std::string d;
		for (double x : xs)
			d += "M" + Num(view.ToScreenX(x)) + " 0V" + Num(h);
		for (double y : ys)
			d += "M0 " + Num(view.ToScreenY(y)) + "H" + Num(w);
		if (!d.empty())
			svg += "<path d=\"" + d + "\" stroke=\"" + Color(color) + "\" stroke-width=\"1\" fill=\"none\"/>\n";
	};
	if (style.showGrid) {
		gridPath(tx.minor, ty.minor, style.gridMinor);
		gridPath(tx.major, ty.major, style.grid);
	}
	const double ax = view.ToScreenX(0), ay = view.ToScreenY(0);
	const bool xAxis = ay >= 0 && ay <= h, yAxis = ax >= 0 && ax <= w;
	if (style.showAxes) {
		std::string d;
		if (xAxis)
			d += "M0 " + Num(ay) + "H" + Num(w);
		if (yAxis)
			d += "M" + Num(ax) + " 0V" + Num(h);
		if (!d.empty())
			svg += "<path d=\"" + d + "\" stroke=\"" + Color(style.axis) + "\" stroke-width=\"1.5\" fill=\"none\"/>\n";
	}

	// the graphs: regions, contours, curves, arrows, points
	auto colorOf = [&](size_t i) { return i < style.graphColors.size() ? style.graphColors[i] : 0x000000u; };
	for (size_t i = 0; i < plot.graphs.size(); i++) {
		auto& g = plot.graphs[i];
		if (!g.Drawable() || g.geometry.fills.empty())
			continue;
		std::string d;
		for (auto& poly : g.geometry.fills)
			AddPolyline(d, poly, true);
		svg += "<path d=\"" + d + "\" fill=\"" + Color(colorOf(i)) + "\" fill-opacity=\"0.22\"/>\n";
	}
	for (size_t i = 0; i < plot.graphs.size(); i++) {
		auto& g = plot.graphs[i];
		if (!g.Drawable())
			continue;
		const std::string color = Color(colorOf(i));
		if (!g.geometry.contours.empty()) {
			std::string d;
			for (auto& c : g.geometry.contours)
				for (auto& line : c.lines)
					AddPolyline(d, line, false);
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.25\"/>\n";
		}
		if (!g.geometry.lines.empty()) {
			std::string d;
			for (auto& line : g.geometry.lines)
				AddPolyline(d, line, false);
			bool dashed = g.geometry.dashed || (g.flags & FplGraphDashed);
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"" + Num(g.width) +
				"\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" +
				(dashed ? " stroke-dasharray=\"" + Num(3 * g.width) + " " + Num(2 * g.width) + "\"" : std::string()) + "/>\n";
		}
		if (!g.geometry.arrows.empty()) {
			std::string d;
			for (auto& a : g.geometry.arrows) {
				double len = std::hypot(a.to.x - a.from.x, a.to.y - a.from.y);
				if (len <= 0)
					continue;
				double ux = (a.to.x - a.from.x) / len, uy = (a.to.y - a.from.y) / len, head = std::min(7.0, 0.35 * len);
				const double c = std::cos(0.45), s = std::sin(0.45);
				FplPoint l = { a.to.x - head * (ux * c - uy * s), a.to.y - head * (uy * c + ux * s) };
				FplPoint r = { a.to.x - head * (ux * c + uy * s), a.to.y - head * (uy * c - ux * s) };
				AddPolyline(d, { a.from, a.to }, false);
				AddPolyline(d, { l, a.to, r }, false);
			}
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color +
				"\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
		}
		for (auto& p : g.geometry.points)
			svg += "<circle cx=\"" + Num(p.x) + "\" cy=\"" + Num(p.y) + "\" r=\"" + Num(2.5 + g.width) + "\" fill=\"" +
				color + "\"/>\n";
	}

	// the ticks' numbers
	if (style.showAxes) {
		const double ly = std::clamp(ay + 11, 10.0, h - 10);
		for (double x : tx.major) {
			double sx = view.ToScreenX(x);
			if (x != 0 && sx >= 12 && sx <= w - 12)
				Text(svg, sx, ly, "middle", style.label, 12, FplFormatTick(x, tx));
		}
		const bool leftEdge = ax < 40;
		for (double y : ty.major) {
			double sy = view.ToScreenY(y);
			if (y == 0 || sy < 10 || sy > h - 10)
				continue;
			if (leftEdge)
				Text(svg, std::max(ax, 0.0) + 5, sy, "start", style.label, 12, FplFormatTick(y, ty));
			else
				Text(svg, std::min(ax, w) - 5, sy, "end", style.label, 12, FplFormatTick(y, ty));
		}
		if (xAxis && yAxis)
			Text(svg, ax - 5, ay + 11, "end", style.label, 12, L"0");
	}

	// the legend
	if (style.showLegend) {
		std::vector<size_t> rows;
		for (size_t i = 0; i < plot.graphs.size(); i++)
			if (!plot.graphs[i].text.empty())
				rows.push_back(i);
		if (!rows.empty()) {
			const double left = 10, top = 10, width = std::clamp(w * 0.4, 200.0, 340.0), row = 26;
			svg += "<rect x=\"" + Num(left) + "\" y=\"" + Num(top) + "\" width=\"" + Num(width) + "\" height=\"" +
				Num(rows.size() * row + 12) + "\" rx=\"6\" fill=\"" + Color(style.panelBack) + "\" stroke=\"" +
				Color(style.panelBorder) + "\"/>\n";
			double y = top + 6 + row / 2;
			for (size_t i : rows) {
				auto& g = plot.graphs[i];
				if (!g.definition && !g.expr.error)
					svg += "<circle cx=\"" + Num(left + 15) + "\" cy=\"" + Num(y) + "\" r=\"6\" fill=\"" +
						Color(colorOf(i)) + "\"" + ((g.flags & FplGraphHidden) ? " fill-opacity=\"0.25\"" : "") + "/>\n";
				Text(svg, left + 28, y, "start", style.panelText, 14, g.text);
				y += row;
			}
		}
	}
	svg += "</svg>\n";
	return svg;
}
