// A plot's state as text: what GetState gives and SetState takes (window-free).
//
//	FunctionPlot 1
//	degrees 0
//	mode 2d
//	panel 0
//	view <xMin> <xMax> <yMin> <yMax>
//	box <xMin> <xMax> <yMin> <yMax> <zMin> <zMax> <yaw> <pitch>
//	param<TAB>name<TAB>value<TAB>min<TAB>max<TAB>step<TAB>flags
//	list<TAB>name<TAB>v1 v2 v3 ...
//	graph<TAB>id<TAB>color (-1: the palette's)<TAB>width<TAB>flags<TAB>range (0 or 1)<TAB>tMin<TAB>tMax<TAB>text
//	style<TAB>id<TAB>opacity<TAB>fill opacity (-1: the default)		after its graph, when not the default
//	label<TAB>id<TAB>text												after its graph, when it has one
//	axes<TAB>x title<TAB>y title<TAB>z title							when there are any
//
// Names and texts escape \ tab and line breaks as \\ \t \n \r. Lines it doesn't know are skipped, so a later version's
// text still loads.

#include "FunctionPlotGraphs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace {

std::wstring Escape(std::wstring_view s) {
	std::wstring out;
	out.reserve(s.size());
	for (wchar_t c : s) {
		switch (c) {
			case L'\\': out += L"\\\\"; break;
			case L'\t': out += L"\\t"; break;
			case L'\n': out += L"\\n"; break;
			case L'\r': out += L"\\r"; break;
			default: out += c;
		}
	}
	return out;
}

std::wstring Unescape(std::wstring_view s) {
	std::wstring out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != L'\\' || i + 1 == s.size()) {
			out += s[i];
			continue;
		}
		switch (s[++i]) {
			case L't': out += L'\t'; break;
			case L'n': out += L'\n'; break;
			case L'r': out += L'\r'; break;
			case L'\\': out += L'\\'; break;
			default:
				// not an escape (a text written by hand): kept as it is
				out += L'\\';
				out += s[i];
		}
	}
	return out;
}

std::wstring Number(double v) {
	wchar_t buf[40];
	swprintf_s(buf, L"%.17g", v);
	return buf;
}

std::vector<std::wstring_view> Split(std::wstring_view s, wchar_t sep) {
	std::vector<std::wstring_view> parts;
	size_t start = 0;
	for (;;) {
		size_t end = s.find(sep, start);
		parts.push_back(s.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));
		if (end == std::wstring_view::npos)
			break;
		start = end + 1;
	}
	return parts;
}

double ToNumber(std::wstring_view s) {
	std::wstring text(s);
	wchar_t* end = nullptr;
	double v = wcstod(text.c_str(), &end);
	return end == text.c_str() ? std::numeric_limits<double>::quiet_NaN() : v;
}

}  // namespace

std::wstring FplSaveState(const FplSavedState& state) {
	std::wstring out = L"FunctionPlot 1\n";
	out += L"degrees " + std::wstring(state.degrees ? L"1" : L"0") + L"\n";
	out += L"mode " + std::wstring(state.mode3D ? L"3d" : L"2d") + L"\n";
	out += L"panel " + std::wstring(state.collapsed ? L"1" : L"0") + L"\n";
	auto& v = state.view;
	out += L"view " + Number(v.xMin) + L" " + Number(v.xMax) + L" " + Number(v.yMin) + L" " + Number(v.yMax) + L"\n";
	auto& b = state.box;
	out += L"box " + Number(b.xMin) + L" " + Number(b.xMax) + L" " + Number(b.yMin) + L" " + Number(b.yMax) + L" " +
		Number(b.zMin) + L" " + Number(b.zMax) + L" " + Number(state.yaw) + L" " + Number(state.pitch) + L"\n";
	for (auto& p : state.params)
		out += L"param\t" + Escape(p.name) + L"\t" + Number(p.value) + L"\t" + Number(p.min) + L"\t" + Number(p.max) +
			L"\t" + Number(p.step) + L"\t" + std::to_wstring(p.flags) + L"\n";
	for (auto& l : state.lists) {
		out += L"list\t" + Escape(l.name) + L"\t";
		for (size_t i = 0; i < l.values.size(); i++)
			out += (i ? L" " : L"") + Number(l.values[i]);
		out += L"\n";
	}
	for (auto& g : state.graphs) {
		out += L"graph\t" + std::to_wstring(g.id) + L"\t" + (g.autoColor ? std::wstring(L"-1") : std::to_wstring(g.color)) +
			L"\t" + Number(g.width) + L"\t" + std::to_wstring(g.flags) + L"\t" + (g.hasRange ? L"1" : L"0") + L"\t" +
			Number(g.tMin) + L"\t" + Number(g.tMax) + L"\t" + Escape(g.text) + L"\n";
		if (g.opacity != 1 || g.fillOpacity >= 0)
			out += L"style\t" + std::to_wstring(g.id) + L"\t" + Number(g.opacity) + L"\t" + Number(g.fillOpacity) + L"\n";
		if (!g.label.empty())
			out += L"label\t" + std::to_wstring(g.id) + L"\t" + Escape(g.label) + L"\n";
	}
	if (!state.axisTitles[0].empty() || !state.axisTitles[1].empty() || !state.axisTitles[2].empty())
		out += L"axes\t" + Escape(state.axisTitles[0]) + L"\t" + Escape(state.axisTitles[1]) + L"\t" +
			Escape(state.axisTitles[2]) + L"\n";
	return out;
}

bool FplLoadState(std::wstring_view text, FplSavedState& state) {
	auto lines = Split(text, L'\n');
	if (lines.empty() || lines[0].substr(0, 13) != L"FunctionPlot ")
		return false;
	FplSavedState s;
	for (size_t i = 1; i < lines.size(); i++) {
		std::wstring_view line = lines[i];
		if (!line.empty() && line.back() == L'\r')
			line.remove_suffix(1);
		if (line.empty())
			continue;
		size_t space = line.find_first_of(L" \t");
		std::wstring_view key = line.substr(0, space);
		std::wstring_view rest = space == std::wstring_view::npos ? std::wstring_view() : line.substr(space + 1);
		auto words = Split(rest, L' ');
		auto fields = Split(rest, L'\t');
		if (key == L"degrees")
			s.degrees = rest == L"1";
		else if (key == L"mode")
			s.mode3D = rest == L"3d";
		else if (key == L"panel")
			s.collapsed = rest == L"1";
		else if (key == L"view" && words.size() >= 4) {
			FplViewport v = s.view;
			v.xMin = ToNumber(words[0]);
			v.xMax = ToNumber(words[1]);
			v.yMin = ToNumber(words[2]);
			v.yMax = ToNumber(words[3]);
			if (v.Valid())
				s.view = v;
		}
		else if (key == L"box" && words.size() >= 8) {
			FplBox3 b{ ToNumber(words[0]), ToNumber(words[1]), ToNumber(words[2]), ToNumber(words[3]),
				ToNumber(words[4]), ToNumber(words[5]) };
			if (b.Valid())
				s.box = b;
			double yaw = ToNumber(words[6]), pitch = ToNumber(words[7]);
			if (std::isfinite(yaw) && std::isfinite(pitch)) {
				s.yaw = static_cast<float>(yaw);
				s.pitch = static_cast<float>(pitch);
			}
		}
		else if (key == L"param" && fields.size() >= 6) {
			FplParam p;
			p.name = Unescape(fields[0]);
			p.value = ToNumber(fields[1]);
			p.min = ToNumber(fields[2]);
			p.max = ToNumber(fields[3]);
			p.step = ToNumber(fields[4]);
			p.flags = static_cast<unsigned>(ToNumber(fields[5]));
			if (!p.name.empty())
				s.params.push_back(std::move(p));
		}
		else if (key == L"list" && fields.size() >= 2) {
			FplList l;
			l.name = Unescape(fields[0]);
			if (!fields[1].empty())
				for (auto w : Split(fields[1], L' '))
					l.values.push_back(ToNumber(w));
			if (!l.name.empty())
				s.lists.push_back(std::move(l));
		}
		else if ((key == L"style" || key == L"label") && fields.size() >= 2) {
			// a graph's, read before it
			unsigned id = static_cast<unsigned>(ToNumber(fields[0]));
			for (auto& g : s.graphs) {
				if (g.id != id)
					continue;
				if (key == L"label")
					g.label = Unescape(fields[1]);
				else if (fields.size() >= 3) {
					double o = ToNumber(fields[1]), f = ToNumber(fields[2]);
					g.opacity = std::isfinite(o) ? static_cast<float>(std::clamp(o, 0.0, 1.0)) : 1.0f;
					g.fillOpacity = std::isfinite(f) ? static_cast<float>(std::min(f, 1.0)) : -1.0f;
				}
			}
		}
		else if (key == L"axes") {
			for (size_t k = 0; k < 3 && k < fields.size(); k++)
				s.axisTitles[k] = Unescape(fields[k]);
		}
		else if (key == L"graph" && fields.size() >= 8) {
			FplGraph g;
			g.id = static_cast<unsigned>(ToNumber(fields[0]));
			double color = ToNumber(fields[1]);
			g.autoColor = color < 0;
			g.color = g.autoColor ? 0 : static_cast<uint32_t>(color);
			g.width = static_cast<float>(ToNumber(fields[2]));
			g.flags = static_cast<unsigned>(ToNumber(fields[3]));
			g.hasRange = fields[4] == L"1";
			g.tMin = ToNumber(fields[5]);
			g.tMax = ToNumber(fields[6]);
			// the text is the rest (it can't hold a tab: they're escaped)
			g.text = Unescape(fields[7]);
			if (g.id != 0)
				s.graphs.push_back(std::move(g));
		}
	}
	state = std::move(s);
	return true;
}
