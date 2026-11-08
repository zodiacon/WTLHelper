// The graphs as SVG: what the control draws, as paths and text (window-free).

#include "FunctionPlotGraphs.h"

#include <algorithm>
#include <array>
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

std::string FplEncodePng(int width, int height, const std::vector<uint8_t>& rgba) {
	std::string png;
	if (width <= 0 || height <= 0 || rgba.size() != size_t(width) * height * 4)
		return png;
	static const std::array<uint32_t, 256> table = [] {
		std::array<uint32_t, 256> t{};
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
		return t;
	}();
	auto be32 = [](std::string& out, uint32_t v) {
		for (int shift = 24; shift >= 0; shift -= 8)
			out += static_cast<char>((v >> shift) & 255);
	};
	auto chunk = [&](const char* type, const std::string& data) {
		be32(png, static_cast<uint32_t>(data.size()));
		const size_t from = png.size();
		png += type;
		png += data;
		uint32_t crc = 0xFFFFFFFFu;
		for (size_t i = from; i < png.size(); i++)
			crc = table[(crc ^ static_cast<uint8_t>(png[i])) & 255] ^ (crc >> 8);
		be32(png, crc ^ 0xFFFFFFFFu);
	};
	png.assign("\x89PNG\r\n\x1A\n", 8);
	std::string header;
	be32(header, width);
	be32(header, height);
	header += "\x08\x06";		// 8 bits, RGBA
	header.append(3, '\0');
	chunk("IHDR", header);
	// each row after a filter byte of none, in deflate's stored blocks
	std::string raw;
	const size_t stride = size_t(width) * 4;
	raw.reserve((stride + 1) * height);
	for (int y = 0; y < height; y++) {
		raw += '\0';
		raw.append(reinterpret_cast<const char*>(rgba.data()) + stride * y, stride);
	}
	std::string z("\x78\x01", 2);
	uint32_t a = 1, b = 0;
	for (unsigned char c : raw) {
		a = (a + c) % 65521;
		b = (b + a) % 65521;
	}
	for (size_t at = 0; at < raw.size();) {
		const size_t length = std::min<size_t>(raw.size() - at, 65535);
		z += static_cast<char>(at + length == raw.size() ? 1 : 0);
		z += static_cast<char>(length & 255);
		z += static_cast<char>(length >> 8);
		z += static_cast<char>(~length & 255);
		z += static_cast<char>((~length >> 8) & 255);
		z.append(raw, at, length);
		at += length;
	}
	be32(z, (b << 16) | a);
	chunk("IDAT", z);
	chunk("IEND", {});
	return png;
}

namespace {

std::string Base64(const std::string& data) {
	static const char Digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (size_t i = 0; i < data.size(); i += 3) {
		const uint32_t b0 = static_cast<uint8_t>(data[i]);
		const uint32_t b1 = i + 1 < data.size() ? static_cast<uint8_t>(data[i + 1]) : 0;
		const uint32_t b2 = i + 2 < data.size() ? static_cast<uint8_t>(data[i + 2]) : 0;
		const uint32_t v = (b0 << 16) | (b1 << 8) | b2;
		out += Digits[(v >> 18) & 63];
		out += Digits[(v >> 12) & 63];
		out += i + 1 < data.size() ? Digits[(v >> 6) & 63] : '=';
		out += i + 2 < data.size() ? Digits[v & 63] : '=';
	}
	return out;
}

}  // namespace

std::string FplWriteSvg(const FplPlot& plot, const FplViewport& view, const FplSvgStyle& style) {
	std::string svg;
	if (!view.Valid())
		return svg;
	const double w = view.width, h = view.height;
	svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + Num(w) + "\" height=\"" + Num(h) + "\" viewBox=\"0 0 " +
		Num(w) + " " + Num(h) + "\" font-family=\"Segoe UI, sans-serif\">\n";
	svg += "<rect width=\"100%\" height=\"100%\" fill=\"" + Color(style.back) + "\"/>\n";

	// heat maps first, under the grid, as a picture each: their colors over the view, clear where they're undefined
	for (auto& g : plot.graphs) {
		auto& map = g.geometry.heatmap;
		if (!g.Drawable() || map.Empty() || !(map.hi > map.lo || map.hi == map.lo))
			continue;
		std::vector<uint8_t> rgba(map.values.size() * 4);
		for (size_t k = 0; k < map.values.size(); k++) {
			const float v = map.values[k];
			if (!std::isfinite(v))
				continue;
			const double t = map.hi > map.lo ? std::clamp((v - map.lo) / (map.hi - map.lo), 0.0, 1.0) : 0.5;
			const uint32_t c = FplColormap(t);
			rgba[k * 4] = static_cast<uint8_t>(c & 255);
			rgba[k * 4 + 1] = static_cast<uint8_t>((c >> 8) & 255);
			rgba[k * 4 + 2] = static_cast<uint8_t>((c >> 16) & 255);
			rgba[k * 4 + 3] = 255;
		}
		const float opacity = g.fillOpacity >= 0 ? g.fillOpacity : 0.85f;
		svg += "<image x=\"0\" y=\"0\" width=\"" + Num(map.cols * map.cell) + "\" height=\"" + Num(map.rows * map.cell) +
			"\" preserveAspectRatio=\"none\" opacity=\"" + Num(opacity) + "\" href=\"data:image/png;base64," +
			Base64(FplEncodePng(map.cols, map.rows, rgba)) + "\"/>\n";
	}

	FplTicks tx = FplMakeTicksX(view, 90, style.piTicks);
	FplTicks ty = FplMakeTicksY(view, 60);
	auto gridPath = [&](const std::vector<double>& xs, const std::vector<double>& ys, uint32_t color) {
		std::string d;
		for (double x : xs)
			d += "M" + Num(view.ToScreenX(x)) + " 0V" + Num(h);
		for (double y : ys)
			d += "M0 " + Num(view.ToScreenY(y)) + "H" + Num(w);
		if (!d.empty())
			svg += "<path d=\"" + d + "\" stroke=\"" + Color(color) + "\" stroke-width=\"1\" fill=\"none\"/>\n";
	};
	const FplPolarGrid polar = style.polarGrid ? FplMakePolarGrid(view) : FplPolarGrid{};
	auto polarPath = [&](const std::vector<FplPolyline>& rings, const std::vector<FplPolyline>& spokes, uint32_t color) {
		std::string d;
		for (auto& line : rings)
			AddPolyline(d, line, false);
		for (auto& line : spokes)
			AddPolyline(d, line, false);
		if (!d.empty())
			svg += "<path d=\"" + d + "\" stroke=\"" + Color(color) + "\" stroke-width=\"1\" fill=\"none\"/>\n";
	};
	if (style.showGrid && !polar.Empty()) {
		polarPath(polar.minorRings, polar.minorSpokes, style.gridMinor);
		polarPath(polar.majorRings, polar.majorSpokes, style.grid);
	}
	else if (style.showGrid) {
		gridPath(tx.minor, ty.minor, style.gridMinor);
		gridPath(tx.major, ty.major, style.grid);
	}
	const double ax = view.AxisLineX(), ay = view.AxisLineY();
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
		const double fill = g.fillOpacity >= 0 ? g.fillOpacity : 0.22;
		svg += "<path d=\"" + d + "\" fill=\"" + Color(colorOf(i)) + "\" fill-opacity=\"" + Num(fill) + "\"/>\n";
	}
	for (size_t i = 0; i < plot.graphs.size(); i++) {
		auto& g = plot.graphs[i];
		if (!g.Drawable())
			continue;
		const std::string color = Color(colorOf(i));
		const std::string opacity = g.opacity < 1 ? " stroke-opacity=\"" + Num(g.opacity) + "\"" : std::string();
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
			bool dotted = (g.flags & FplGraphDotted) != 0;
			std::string dashes = dotted ? " stroke-dasharray=\"0 " + Num(2.5 * g.width) + "\""
				: dashed ? " stroke-dasharray=\"" + Num(3 * g.width) + " " + Num(2 * g.width) + "\"" : std::string();
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"" + Num(g.width) +
				"\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" + dashes + opacity + "/>\n";
		}
		if (!g.geometry.asymptotes.empty()) {
			std::string d;
			for (double a : g.geometry.asymptotes)
				d += g.expr.kind == FplKind::Function ? "M" + Num(a) + " 0V" + Num(h) : "M0 " + Num(a) + "H" + Num(w);
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.25\" stroke-dasharray=\"6 4\"" +
				" stroke-opacity=\"" + Num(g.opacity * 0.5) + "\"/>\n";
		}
		for (auto& p : g.geometry.holes)
			svg += "<circle cx=\"" + Num(p.x) + "\" cy=\"" + Num(p.y) + "\" r=\"" + Num(2.5 + g.width) + "\" fill=\"" +
				Color(style.back) + "\" stroke=\"" + color + "\" stroke-width=\"2\"/>\n";
		if (!g.geometry.marks.empty()) {
			std::string d;
			for (auto& m : g.geometry.marks)
				AddPolyline(d, { m.from, m.to }, false);
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.25\" stroke-linecap=\"round\"" +
				" stroke-opacity=\"" + Num(g.opacity * (g.geometry.lines.empty() ? 0.75 : 0.4)) + "\"/>\n";
		}
		if (!g.geometry.arrows.empty()) {
			std::string d;
			for (auto& a : g.geometry.arrows) {
				FplPoint l, r;
				if (!FplArrowHead(a, l, r))
					continue;
				AddPolyline(d, { a.from, a.to }, false);
				AddPolyline(d, { l, a.to, r }, false);
			}
			svg += "<path d=\"" + d + "\" fill=\"none\" stroke=\"" + color +
				"\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"" +
				(g.expr.kind == FplKind::OdeSystem && !g.geometry.lines.empty() ? " stroke-opacity=\"0.45\"" : "") + "/>\n";
		}
		for (auto& p : g.geometry.points) {
			if (!std::isfinite(p.x) || !std::isfinite(p.y))
				continue;
			if (g.flags & FplGraphHollow)
				svg += "<circle cx=\"" + Num(p.x) + "\" cy=\"" + Num(p.y) + "\" r=\"" + Num(2.5 + g.width) + "\" fill=\"" +
					Color(style.back) + "\" stroke=\"" + color + "\" stroke-width=\"2\"" + opacity + "/>\n";
			else
				svg += "<circle cx=\"" + Num(p.x) + "\" cy=\"" + Num(p.y) + "\" r=\"" + Num(2.5 + g.width) + "\" fill=\"" +
					color + "\"" + (g.opacity < 1 ? " fill-opacity=\"" + Num(g.opacity) + "\"" : std::string()) + "/>\n";
		}
	}

	// a heat map's color scale at the right, as the control's: the colors, and the values on a patch of the background
	if (style.showColorScale) {
		const double barWidth = 14, barHeight = std::min(220.0, h * 0.4), slot = 76, top = 14;
		double right = w - 12;
		size_t count = 0;
		for (auto& g : plot.graphs)
			count += g.Drawable() && !g.geometry.heatmap.Empty();
		for (size_t i = 0; i < plot.graphs.size() && barHeight >= 60; i++) {
			auto& g = plot.graphs[i];
			auto& map = g.geometry.heatmap;
			if (!g.Drawable() || map.Empty())
				continue;
			const double left = right - barWidth;
			if (left - slot + barWidth < 0)
				break;
			const float opacity = g.fillOpacity >= 0 ? g.fillOpacity : 0.85f;
			const std::string id = "scale" + std::to_string(i);
			svg += "<rect x=\"" + Num(right - slot + 4) + "\" y=\"" + Num(top - 8) + "\" width=\"" + Num(slot + 2) +
				"\" height=\"" + Num(barHeight + 16) + "\" rx=\"4\" fill=\"" + Color(style.back) + "\" fill-opacity=\"0.75\"/>\n";
			svg += "<linearGradient id=\"" + id + "\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">";
			for (int k = 0; k <= 16; k++)
				svg += "<stop offset=\"" + Num(k / 16.0) + "\" stop-color=\"" + Color(FplColormap(1 - k / 16.0)) + "\"/>";
			svg += "</linearGradient>\n";
			svg += "<rect x=\"" + Num(left) + "\" y=\"" + Num(top) + "\" width=\"" + Num(barWidth) + "\" height=\"" +
				Num(barHeight) + "\" fill=\"url(#" + id + ")\" fill-opacity=\"" + Num(opacity) + "\" stroke=\"" + Color(style.axis) + "\"/>\n";
			if (count > 1)
				svg += "<rect x=\"" + Num(left) + "\" y=\"" + Num(top - 5) + "\" width=\"" + Num(barWidth) +
					"\" height=\"3\" fill=\"" + Color(colorOf(i)) + "\"/>\n";
			for (auto& tick : FplMakeColorScale(map.lo, map.hi, barHeight)) {
				const double y = top + barHeight * (1 - tick.fraction);
				svg += "<path d=\"M" + Num(left - 3) + " " + Num(y) + "H" + Num(left) + "\" stroke=\"" + Color(style.axis) + "\"/>\n";
				Text(svg, left - 5, y + 4, "end", style.label, 12, tick.text);
			}
			right -= slot;
		}
	}

	// the ticks' numbers
	if (style.showAxes && !polar.Empty()) {
		for (auto& l : polar.radiusLabels)
			Text(svg, l.at.x, l.at.y + 11, "middle", style.label, 12, FplFormatTick(l.value, polar.radii));
		for (auto& l : polar.angleLabels)
			Text(svg, l.at.x, l.at.y + 4, "middle", style.label, 12, FplFormatAngle(l.value, style.piTicks));
		if (xAxis && yAxis)
			Text(svg, ax - 5, ay + 11, "end", style.label, 12, L"0");
	}
	else if (style.showAxes) {
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

	// the graphs' labels, and the axes' titles
	for (auto& place : plot.LabelPlaces(view))
		Text(svg, place.at.x + 9, place.at.y - 11, "start", colorOf(static_cast<size_t>(place.graph)), 13, place.text);
	if (style.showAxes) {
		if (!style.axisTitles[0].empty())
			Text(svg, w - 8, std::clamp(ay - 12, 10.0, h - 10), "end", style.label, 13, style.axisTitles[0]);
		if (!style.axisTitles[1].empty())
			Text(svg, std::clamp(ax + 8, 8.0, w - 8), 14, "start", style.label, 13, style.axisTitles[1]);
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
