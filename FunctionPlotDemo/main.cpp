// FunctionPlotDemo: an expression list beside a function plot. Each line of the editor is a graph; a name the
// expressions use that nothing defines becomes a slider (View > Automatic Sliders). The status bar shows the traced
// point and the view. File > Examples has functions, parameters, curves, implicit curves, inequalities, the user's
// functions and d/dx, a surface, a vector field and an animation; File > Export writes PNG or SVG.

#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include "FunctionPlot.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#include <CustomSplitterWindow.h>
#else
#include <atlsplit.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_PLOT = 101,
	IDC_EDIT,
	IDC_STATUS,

	ID_DEMO_EXIT = 201,
	ID_DEMO_EXPORTPNG,
	ID_DEMO_EXPORTSVG,
	ID_DEMO_OPEN,
	ID_DEMO_SAVE,
	ID_EXAMPLE_FIRST = 210,     // one per example

	ID_DEMO_HOME = 301,
	ID_DEMO_ZOOMIN,
	ID_DEMO_ZOOMOUT,
	ID_DEMO_EQUAL,
	ID_DEMO_PITICKS,
	ID_DEMO_DEGREES,
	ID_DEMO_LEGEND,
	ID_DEMO_SLIDERS,
	ID_DEMO_GRID,
	ID_DEMO_AXES,
	ID_DEMO_TRACE,
	ID_DEMO_AUTOSLIDERS,
	ID_DEMO_DARKMODE,
	ID_DEMO_HIGHCONTRAST,
	ID_DEMO_FEATURES,
	ID_DEMO_HEATMAPS,
	ID_DEMO_COLLAPSE,
	ID_DEMO_3D,
	ID_DEMO_TANGENT,
	ID_DEMO_FIT,
	ID_DEMO_UNDO,
	ID_DEMO_REDO,
	ID_DEMO_MESHLINES,
	ID_DEMO_LEVELLINES,
	ID_DEMO_SEETHROUGH,
	ID_DEMO_LOGX,
	ID_DEMO_LOGY,
	ID_DEMO_COPY,
	ID_DEMO_PRINT,
	ID_DEMO_POLARGRID,
	ID_DEMO_ANIMATE,
	ID_DEMO_SLOWTRANSITIONS,
};

// ---- Examples -------------------------------------------------------------------

enum class Setup { None, FieldSlider, Animate, Bounds, Data, Styles, Log, Ode, Glass, Lorenz, Polar };

struct Example {
	const wchar_t* name;
	const wchar_t* text;        // the lines, as typed (code points for what isn't ASCII)
	bool piTicks;
	bool is3D = false;
	Setup setup = Setup::None;
};

static const Example g_Examples[] = {
	{ L"&Functions", L"y = sin x\r\ny = x^2/4 - 2\r\ny = tan x\r\ny = 1/x\r\ny = floor(x)/2 {x > 3}\r\n(2, 1)", true },
	{ L"&Parameters", L"y = a sin(bx + c)\r\ny = a\r\nk = 2b\r\na: 0..5\r\nb: 0..4, 0.5\r\nc: -pi..pi", true },
	{ L"&Curves", L"(4cos 3t, 4sin 2t)\r\nr = 2 + 4cos 5\x03B8\r\nr = \x03B8/2\r\n(t cos t, t sin t)", false },
	{ L"&Implicit", L"x^2 + y^2 = 16\r\ny^2 = x^3 - 4x\r\nx^2 - y^2 = 1\r\nsin x + cos y = 0.5", false },
	{ L"I&nequalities", L"y < 4 - x^2\r\nx^2/16 + y^2/4 \x2264 1\r\ny > sin x + 2\r\nr < 2 + cos 6\x03B8", false },
	{ L"&Between Curves", L"x^2/4 - 3 < y < 4 - x^2/2\r\n6 < x < 8\r\ny^2 - 8 \x2264 x \x2264 -y^2 - 2\r\ny = 3sin x", false },
	{ L"&Steps and Domains", L"y = floor x\r\ny = sqrt(9 - x^2)\r\ny = |x - 2| - 3\r\ny = x^3/10 {-4 < x < 4}\r\ny = sign(x) - 5", false },
	{ L"&User Functions", L"f(x) = x^3/6 - x\r\nf'(x)\r\nf''(x)\r\ng(x) = f(x - a) + 1\r\ny = d/dx sin(x^2/4)", false },
	{ L"Waves and Steps", L"y = square(x) + 5\r\ny = triangle(x) + 2.5\r\ny = sawtooth(x)\r\ny = step(x - 1) - 2.5\r\ny = 2sinc(2x) - 5", false },
	{ L"Normal Distribution", L"y = 10normpdf(x, m, s)\r\ny = 3normcdf(x, m, s)\r\nm: -4..4\r\ns: 0.2..3", false },
	{ L"Special Functions", L"y = 2besselj(0, x)\r\ny = 2airyai(x) - 3\r\ny = zeta(x) + 3\r\ny = lambertw(x) - 6\r\ny = ellipk(x/10) + 4", false },
	{ L"Animation Speeds", L"y = sin(x - a) + 3\r\ny = sin(x - b)\r\ny = sin(x - c) - 3\r\na: 0..6.3, 16s\r\nb: 0..6.3, 4s\r\nc: 0..6.3, 0.1, 1s", false },
	{ L"Series", L"y = sum(sin((2k - 1)x)/(2k - 1), k, 1, n) + 3\r\ny = sum(x^k/k!, k, 0, n) - 3\r\nn: 1..30, 1", false },
	{ L"Substitutions", L"a = x - 1\r\nb = a^2\r\ny = 3a\r\ny = b/2 - 4\r\nx^2 + a^2 + y^2 = 16\r\ny' = -a y, y(0) = 3", false },
	{ L"S&urfaces", L"z = sin x + cos y", false },
	{ L"&Vector Field", L"(y, -sin x)\r\ny^2/2 - cos x = c", false, false, Setup::FieldSlider },
	{ L"&Animation", L"x^2 + y^2 = 4\r\n(2cos p, 2sin p)\r\ny = 2sin(x - p)\r\nr = 3 + cos(5\x03B8 + 2p)", false, false,
		Setup::Animate },
	{ L"I&ntegrals", L"y = sin x + x/4\r\nintegral(sin x + x/4, a, b)\r\ny = integral(sin t + t/4, t, 0, x) - 4\r\nA = integral(x^2, 0, 3)\r\n(a, 0)\r\n(b, 0)", false, false, Setup::Bounds },
	{ L"D&ata and Regression", L"(x1, y1)\r\ny1 ~ a e^(k x1)\r\ny = a e^(k x)\r\ny = mean(y1)", false, false,
		Setup::Data },
	{ L"&Lists", L"L = [1, 2, 3, 4, 5]\r\ny = 3sin(x L/2)/L\r\n(L, mean(L))", false },
	{ L"Polar Gri&d", L"r = 2 + 4cos 5\x03B8\r\nr = 6\r\nr < 3sin 2\x03B8\r\nr = \x03B8/2", true, false, Setup::Polar },
	{ L"In&tersections", L"x^2 + y^2 = 16\r\nr = 2 + 2cos \x03B8\r\n(4cos t + 1, 2sin t)\r\nx = y^2/2 - 3\r\ny = x/2\r\ny' = -y/2, y(-6) = 5", false },
	{ L"&ODEs and Slope Fields", L"y' = x - y, y(0) = -2, y(a) = b, y(-3) = 4", false, false, Setup::Ode },
	{ L"Second-Order OD&Es", L"y'' = -y - y'/4, y(0) = a, y'(0) = 0, y(0) = -3, y'(0) = 3", false },
	{ L"Higher-Order ODEs", L"y''' = -y' - y''/5, y(0) = a, y'(0) = 0, y''(0) = 1\r\ny'''' = -y/4, y(0) = -2, y'(0) = 1, y''(0) = 0, y'''(0) = 0", false },
	{ L"Forced Oscillator", L"x' = y, y' = -x - y/5 + cos(t), (2, 0)", false },
	{ L"Phase Plane", L"x' = y, y' = -sin x - y/5, (a, b), (-6, 3), (1, 0)", false, false, Setup::Ode },
	{ L"As&ymptotes and Holes", L"y = tan x\r\ny = (x^2 - 1)/(x - 1) + 3\r\ny = 1/(x + 5) - 4\r\ny = sin(x)/x - 5\r\ny = ln(x - 7)", false },
	{ L"Stiff &Equations", L"y' = -100000(y - cos x), y(0) = 2.5\r\nx' = y, y' = 100(1 - x^2)y - x, (2, 0)\r\ny'' = -100000000 y, y(0) = 3, y'(0) = 0", false },
	{ L"Logistic Gro&wth", L"dy/dx = k y(1 - y/4), y(-6) = 0.2, y(-6) = 6, y(0) = 2", false },
	{ L"Lo&g Scales", L"y = x^2\r\ny = 2^x\r\ny = 1000/x\r\ny = 50 log x\r\n(10, 100)\r\ny' = y/x, y(1) = 3", false, false, Setup::Log },
	{ L"La&bels and Styles", L"(2, 3)\r\n(a, 1)\r\ny = x^2/4\r\ny = 2sin x\r\nx^2 + y^2 < 9", false, false, Setup::Styles },
	{ L"&Draggable Points", L"(a, b)\r\ny = (x - a)^2/2 + b\r\n(c, 3sin c)\r\ny = 3sin x", false },
	{ L"&Errors", L"y = 2 +\r\ny = sin(x\r\nx = y = 1\r\ny = sin()\r\ny = 2 * * 3", false },
	{ L"3D: Su&rfaces", L"z = 4sin(x/2)cos(y/2)\r\nz = (x^2 - y^2)/20 - 6\r\ny = 6cos(x/2)", false, true },
	{ L"3D: Curves and S&olids", L"(5cos 4t, 5sin 4t, 3t - 9)\r\n(4cos u sin(v/2), 4sin u sin(v/2), 4cos(v/2))\r\n"
		L"((7 + 2cos v)cos u, (7 + 2cos v)sin u, 2sin v)\r\n(0, 0, 8)", false, true },
	{ L"3D: &Implicit Surfaces", L"(sqrt(x^2 + y^2) - 6)^2 + z^2 = 4\r\nx^2 + y^2 + (z - 5)^2 = 9\r\n"
		L"x^2 + y^2 - z^2/4 = 1", false, true },
	{ L"3D: &Gyroid", L"sin(x/2)cos(y/2) + sin(y/2)cos(z/2) + sin(z/2)cos(x/2) = 0", false, true },
	{ L"3D: See-&Through", L"x^2 + y^2 + z^2 = 36\r\nz = (x^2 - y^2)/12\r\n((7 + 2cos v)cos u, (7 + 2cos v)sin u, 2sin v)\r\n(5cos 4t, 5sin 4t, 3t - 9)", false, true, Setup::Glass },
	{ L"3D: Fields on the Plane", L"(y, -x)\r\ny' = x - y, y(-6) = 4\r\nz = -7 - (x^2 + y^2)/30", false, true },
	{ L"3D: Points and &Regions", L"(a, b, c)\r\nx^2 + y^2 < 25\r\nx^2/4 - 8 < y < -x^2/8 - 1\r\ny = 3sin x\r\nx' = -y + z/10, y' = x, z' = -z/4, (d, 0, 7)", false, true },
	{ L"3D: &Lorenz Attractor", L"x' = 10(y - x), y' = x(28 - z) - y, z' = x y - 8z/3, (1, 1, 20)\r\nx' = 10(y - x), y' = x(28 - z) - y, z' = x y - 8z/3, (1.01, 1, 20)", false, true, Setup::Lorenz },
	{ L"3D: Animated &Wave", L"z = 3sin(sqrt(x^2 + y^2) - 2p)/(1 + sqrt(x^2 + y^2)/4)", false, true, Setup::Animate },
};

// ---- State --------------------------------------------------------------------

static CFunctionPlotCtrl g_plot;
static CEdit g_edit;
// the lines on the left, the plot on the right: WTLHelper's hairline, following its dark mode, where it's linked
#ifdef DEMO_WTLHELPER
static CCustomSplitterWindow g_splitter;
#else
static CSplitterWindow g_splitter;
#endif
static CStatusBarCtrl g_status;
static CFont g_editFont;
static HWND g_main;
static bool g_autoSliders = true;
static bool g_heatmaps = true;
static bool g_meshLines = false, g_levelLines = false, g_seeThrough = false;
// FunctionPlotDemo.exe /example 7 /export plot.png: an example, written to a file (.png or .svg; .bmp: printed onto
// a 1600 x 1200 bitmap page; .fplot: as File > Save does), then exit;
// /open plot.fplot starts with a saved plot (File > Save Plot)
static int g_startExample = 0;
static std::wstring g_exportPath;
static std::wstring g_openPath;			// /open file.fplot: a saved plot to start with
static int g_exitCode = 0;
static std::set<std::wstring> g_autoParams;   // the sliders the demo added

// ---- The graphs from the editor's lines --------------------------------------------

static std::vector<std::wstring> EditorLines() {
	int len = g_edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	g_edit.GetWindowText(text.data(), len + 1);
	text.resize(len);
	std::vector<std::wstring> lines;
	size_t start = 0;
	for (;;) {
		size_t end = text.find(L'\n', start);
		std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
		if (!line.empty() && line.back() == L'\r')
			line.pop_back();
		lines.push_back(line);
		if (end == std::wstring::npos)
			break;
		start = end + 1;
	}
	return lines;
}

static bool Unknown(const std::wstring& name) {
	for (int i = 0; i < g_plot.GetGraphCount(); i++) {
		FPLGRAPHINFO info;
		if (g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) && info.error == FPLE_UNKNOWNNAME && name == info.szName)
			return true;
	}
	return false;
}

static bool ValidName(const wchar_t* name) {
	if (!*name || wcslen(name) > 8)
		return false;
	for (const wchar_t* p = name; *p; p++)
		if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || (*p >= 0x3B1 && *p <= 0x3C9)))
			return false;
	return true;
}

// ---- Slider ranges: a line like "a: -8..8" ------------------------------------------

enum class RangeKind { NotOne, Good, Bad };
struct RangeLine {
	double seconds = 0;		// to play across it (0: as it was)
	std::wstring name;
	double min = 0, max = 0, step = 0;
	std::wstring problem;		// Bad's
	size_t at = 0;				// where what's after the colon starts
};

static std::wstring Trimmed(std::wstring_view text) {
	while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
		text.remove_suffix(1);
	return std::wstring(text);
}

// A number, or one in terms of pi: "-8", "0.5", "2pi", "-pi/2", U+03C0 too.
static bool ParseRangeNumber(const std::wstring& text, double& value) {
	const wchar_t* p = text.c_str();
	bool negative = false;
	if (*p == L'-' || *p == wchar_t(0x2212)) {
		negative = true;
		p++;
	}
	double v = 1;
	bool any = false;
	if ((*p >= L'0' && *p <= L'9') || *p == L'.') {
		wchar_t* end = nullptr;
		v = wcstod(p, &end);
		if (end == p)
			return false;
		p = end;
		any = true;
	}
	if (p[0] == L'p' && p[1] == L'i') {
		v *= 3.141592653589793;
		p += 2;
		any = true;
	}
	else if (*p == wchar_t(0x3C0)) {
		v *= 3.141592653589793;
		p++;
		any = true;
	}
	if (!any)
		return false;
	if (*p == L'/') {
		wchar_t* end = nullptr;
		const double d = wcstod(p + 1, &end);
		if (end == p + 1 || d == 0)
			return false;
		v /= d;
		p = end;
	}
	if (*p || !std::isfinite(v))
		return false;
	value = negative ? -v : v;
	return true;
}

// "a: -8..8", or with a step "a: -8..8, 0.5". A name, a colon and two dots are meant as one, so one that doesn't read is
// Bad, with its own message rather than the parser's. Anything else is a graph (with whatever error it has).
static RangeKind ParseRangeLine(const std::wstring& line, RangeLine& range) {
	const size_t colon = line.find(L':');
	if (colon == std::wstring::npos)
		return RangeKind::NotOne;
	range.name = Trimmed(std::wstring_view(line).substr(0, colon));
	if (!ValidName(range.name.c_str()))
		return RangeKind::NotOne;
	std::wstring rest = line.substr(colon + 1);
	const size_t dots = rest.find(L"..");
	if (dots == std::wstring::npos)
		return RangeKind::NotOne;
	range.at = line.find_first_not_of(L" \t", colon + 1);
	auto bad = [&](std::wstring why) {
		range.problem = range.name + L": " + std::move(why);
		return RangeKind::Bad;
	};
	const std::wstring reads = L"a range reads like " + range.name + L": -8..8, or " + range.name + L": 0..2pi, 0.1";
	std::wstring right = rest.substr(dots + 2);
	range.step = 0;
	range.seconds = 0;
	// after the range, in this order: a step, and how long it takes to play across ("8s")
	for (size_t comma = right.find_last_of(L','); comma != std::wstring::npos; comma = right.find_last_of(L',')) {
		std::wstring part = Trimmed(std::wstring_view(right).substr(comma + 1));
		if (!part.empty() && part.back() == L's' && part != L"s") {
			part.pop_back();
			if (range.seconds != 0 || range.step != 0 || !ParseRangeNumber(part, range.seconds))
				return bad(reads);
			if (!(range.seconds > 0))
				return bad(L"playing across a range must take more than 0 seconds");
		}
		else {
			if (range.step != 0 || !ParseRangeNumber(part, range.step))
				return bad(reads);
			if (!(range.step > 0))
				return bad(L"a slider's step must be more than 0");
		}
		right.resize(comma);
	}
	if (!ParseRangeNumber(Trimmed(std::wstring_view(rest).substr(0, dots)), range.min) ||
		!ParseRangeNumber(Trimmed(right), range.max))
		return bad(reads);
	if (!(range.max > range.min))
		return bad(L"a range goes from low to high");
	// no step given: one a hundredth of the range or a little finer, a power of ten
	if (range.step == 0)
		range.step = std::pow(10.0, std::floor(std::log10((range.max - range.min) / 100)));
	return RangeKind::Good;
}

// What each ranged parameter's slider was before its line, to go back to when the line goes.
struct SliderRange {
	double min, max, step;
	double seconds = 4;
};
static std::map<std::wstring, SliderRange> g_rangedParams;

static void ApplyRangeLines(const std::vector<std::wstring>& lines) {
	std::map<std::wstring, RangeLine> wanted;
	for (auto& line : lines) {
		RangeLine range;
		if (ParseRangeLine(line, range) == RangeKind::Good)
			wanted[range.name] = range;
	}
	for (auto it = g_rangedParams.begin(); it != g_rangedParams.end();) {
		if (wanted.count(it->first)) {
			++it;
			continue;
		}
		FPLPARAM p{};
		if (g_plot.GetParameter(it->first.c_str(), p)) {
			std::wstring name = it->first;
			p.pszName = name.c_str();
			p.min = it->second.min;
			p.max = it->second.max;
			p.step = it->second.step;
			g_plot.SetParameter(p);
			g_plot.SetAnimationSpeed(name.c_str(), it->second.seconds);
		}
		it = g_rangedParams.erase(it);
	}
	for (auto& [name, range] : wanted) {
		FPLPARAM p{};
		if (!g_plot.GetParameter(name.c_str(), p))
			continue;		// nothing uses it (yet); ShowLineError says so
		auto [before, isNew] = g_rangedParams.try_emplace(name, SliderRange{ p.min, p.max, p.step, g_plot.GetAnimationSpeed(name.c_str()) });
		g_plot.SetAnimationSpeed(name.c_str(), range.seconds > 0 ? range.seconds : before->second.seconds);
		const double value = std::clamp(p.value, range.min, range.max);
		if (p.min == range.min && p.max == range.max && p.step == range.step && p.value == value)
			continue;
		p.pszName = name.c_str();
		p.min = range.min;
		p.max = range.max;
		p.step = range.step;
		p.value = value;
		g_plot.SetParameter(p);
	}
}

// Each line is a graph with the line's number as its id (so its color stays with it). A range line is an empty one, which
// draws nothing, with its text as the label: the saved state keeps that, for File > Open to bring the line back.
// The word "theta" in the editor becomes its letter, keeping the caret where it was. Not inside a longer name ("mytheta",
// or a pasted "thetas": typed, it's converted before the "s" arrives), but after "d/d", as the parser takes that whole. It
// can't be undone: Ctrl+Z would give back the word, which would turn straight back into the letter, and Ctrl+Z would seem
// to do nothing.
static void ConvertTheta() {
	static bool busy = false;		// replacing sends EN_CHANGE
	if (busy)
		return;
	busy = true;
	const int len = g_edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	g_edit.GetWindowText(text.data(), len + 1);
	text.resize(len);
	int start = 0, end = 0;
	g_edit.GetSel(start, end);
	const std::wstring_view word = L"theta";
	const int removed = static_cast<int>(word.size()) - 1;
	bool changed = false;
	// from the end, so the places found stay right
	for (size_t at = text.rfind(word); at != std::wstring::npos; at = at == 0 ? std::wstring::npos : text.rfind(word, at - 1)) {
		const size_t after = at + word.size();
		const bool letterBefore = at > 0 && ::IsCharAlphaW(text[at - 1]);
		const bool derivative = at >= 3 && text.compare(at - 3, 3, L"d/d") == 0;
		const bool letterAfter = after < text.size() && ::IsCharAlphaW(text[after]);
		if ((letterBefore && !derivative) || letterAfter)
			continue;
		g_edit.SetSel(static_cast<int>(at), static_cast<int>(after), TRUE);
		g_edit.ReplaceSel(L"\x03B8", FALSE);
		auto move = [&](int& place) {
			if (place >= static_cast<int>(after))
				place -= removed;
			else if (place > static_cast<int>(at))
				place = static_cast<int>(at) + 1;
		};
		move(start);
		move(end);
		changed = true;
	}
	if (changed)
		g_edit.SetSel(start, end, FALSE);
	busy = false;
}

static void SyncGraphs() {
	auto lines = EditorLines();
	for (size_t i = 0; i < lines.size(); i++) {
		RangeLine range;
		const bool isRange = ParseRangeLine(lines[i], range) != RangeKind::NotOne;
		FPLGRAPH g{ FPLGM_TEXT, (UINT)i + 1 };
		g.pszText = isRange ? L"" : lines[i].c_str();
		if (isRange) {
			g.mask |= FPLGM_LABEL;
			g.pszLabel = lines[i].c_str();
		}
		else {
			// was it one? its label goes
			FPLGRAPH old{ FPLGM_TEXT | FPLGM_LABEL, g.id };
			if (g_plot.GetGraph(old) && !*old.pszText && old.pszLabel && *old.pszLabel) {
				g.mask |= FPLGM_LABEL;
				g.pszLabel = nullptr;
			}
		}
		if (!g_plot.SetGraph(g))
			g_plot.AddGraph(g);
	}
	while (g_plot.GetGraphCount() > (int)lines.size())
		g_plot.RemoveGraph(g_plot.GetGraphId(g_plot.GetGraphCount() - 1));

	if (g_autoSliders) {
		// a slider for each name nothing defines
		for (int round = 0; round < 8; round++) {
			bool added = false;
			for (int i = 0; i < g_plot.GetGraphCount(); i++) {
				FPLGRAPHINFO info;
				if (!g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) || info.error != FPLE_UNKNOWNNAME ||
					!ValidName(info.szName))
					continue;
				FPLPARAM p{ info.szName, 1, -10, 10, 0.1, FPLPF_AUTO };
				g_plot.SetParameter(p);
				g_autoParams.insert(info.szName);
				added = true;
			}
			if (!added)
				break;
		}
	}
	// heat maps for the surfaces (View > Heat Maps)
	for (int i = 0; i < g_plot.GetGraphCount(); i++) {
		FPLGRAPH g{ FPLGM_FLAGS, g_plot.GetGraphId(i) };
		g_plot.GetGraph(g);
		UINT flags = g_heatmaps ? g.flags | FPLGF_HEATMAP : g.flags & ~FPLGF_HEATMAP;
		flags = g_meshLines ? flags | FPLGF_MESHLINES : flags & ~FPLGF_MESHLINES;
		flags = g_levelLines ? flags | FPLGF_LEVELLINES : flags & ~FPLGF_LEVELLINES;
		// the surfaces see-through (View > See-Through Surfaces), a curve in them stays solid
		FPLGRAPHINFO info;
		const bool surface = g_plot.GetGraphInfo(g.id, info) && (info.kind == FPLK_SURFACE || info.kind == FPLK_PARAMSURFACE ||
			info.kind == FPLK_IMPLICITSURFACE);
		FPLGRAPH o{ FPLGM_OPACITY, g.id };
		g_plot.GetGraph(o);
		const float opacity = g_seeThrough && surface ? 0.45f : 1.0f;
		if (flags != g.flags) {
			g.flags = flags;
			g_plot.SetGraph(g);
		}
		if (opacity != o.opacity) {
			o.opacity = opacity;
			g_plot.SetGraph(o);
		}
	}
	// and none for a name no longer used (or now defined by a line)
	for (auto it = g_autoParams.begin(); it != g_autoParams.end();) {
		FPLPARAM p{};
		if (!g_plot.GetParameter(it->c_str(), p)) {
			it = g_autoParams.erase(it);
			continue;
		}
		FPLPARAM saved = p;
		std::wstring name = *it;
		saved.pszName = name.c_str();
		bool playing = g_plot.IsParameterAnimating(name.c_str()) != FALSE;
		g_plot.RemoveParameter(name.c_str());
		if (g_autoSliders && Unknown(name)) {
			g_plot.SetParameter(saved);
			if (playing)
				g_plot.AnimateParameter(name.c_str(), TRUE);
			++it;
		}
		else {
			it = g_autoParams.erase(it);
		}
	}
	ApplyRangeLines(lines);
}

// ---- Status bar --------------------------------------------------------------------

static std::wstring Number(double v) {
	wchar_t buf[64];
	swprintf_s(buf, L"%.6g", v);
	return buf;
}

static void ShowView() {
	std::wstring text;
	if (g_plot.GetMode() == FPLM_3D) {
		FPLVIEW3D v = g_plot.GetView3D();
		text = L"x: " + Number(v.xMin) + L" to " + Number(v.xMax) + L"   y: " + Number(v.yMin) + L" to " + Number(v.yMax) +
			L"   z: " + Number(v.zMin) + L" to " + Number(v.zMax);
		if (g_plot.IsSoftwareRendering())
			text += L"   (WARP)";
	}
	else {
		FPLVIEW v = g_plot.GetView();
		text = L"x: " + Number(v.xMin) + L" to " + Number(v.xMax) + L"   y: " + Number(v.yMin) + L" to " + Number(v.yMax);
	}
	g_status.SetText(1, text.c_str());
}

static void ShowTrace(const FPLTRACE& t) {
	if (!t.valid) {
		g_status.SetText(0, g_plot.GetMode() == FPLM_3D
			? L"Drag to orbit (Shift: move), wheel to zoom, point at a graph to trace it"
			: L"Drag to pan, wheel to zoom (Ctrl: x only, Shift: y only), point at a graph to trace it");
		return;
	}
	static const wchar_t* Kinds[] = { L"root", L"minimum", L"maximum", L"intersection", L"y-intercept" };
	std::wstring text = L"Line " + std::to_wstring(t.id) + L":  (" + Number(t.x) + L", " + Number(t.y) +
		(t.hasZ && g_plot.GetMode() == FPLM_3D ? L", " + Number(t.z) + L")" : L")");
	if (t.hasZ && g_plot.GetMode() != FPLM_3D)
		text += L"  z = " + Number(t.z);
	if (t.hasSlope)
		text += L"  slope " + Number(t.slope);
	if (t.feature != FPLF_NONE)
		text += std::wstring(L"  ") + Kinds[t.feature];
	g_status.SetText(0, text.c_str());
}

// The error of the line with the caret, in the third part.
// The line the caret is on, counted by line breaks: with wrapping the edit's own lines aren't the graphs'.
static int CaretLine() {
	int start = 0, end = 0;
	g_edit.GetSel(start, end);
	int len = g_edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	g_edit.GetWindowText(text.data(), len + 1);
	return (int)std::count(text.begin(), text.begin() + std::min(end, len), L'\n');
}

static void ShowLineError() {
	const int line = CaretLine();
	FPLGRAPHINFO info;
	std::wstring text;
	auto lines = EditorLines();
	RangeLine range;
	const RangeKind rangeKind = line < (int)lines.size() ? ParseRangeLine(lines[line], range) : RangeKind::NotOne;
	if (rangeKind == RangeKind::Bad)
		text = range.problem;
	else if (rangeKind == RangeKind::Good) {
		FPLPARAM p{};
		text = g_plot.GetParameter(range.name.c_str(), p)
			? range.name + L": a slider from " + Number(p.min) + L" to " + Number(p.max) + L", steps of " + Number(p.step)
			: L"No line uses " + range.name + L" as a parameter";
	}
	else if (g_plot.GetGraphInfo((UINT)line + 1, info)) {
		if (info.error != FPLE_NONE && info.error != FPLE_EMPTY)
			text = L"Line " + std::to_wstring(line + 1) + L": " + info.szError + L" (at " + std::to_wstring(info.errorPos + 1) + L")";
		else if (info.kind == FPLK_DEFINITION && std::isnan(info.value) && g_plot.GetList(info.szName, nullptr, 0) > 0)
			text = std::wstring(info.szName) + L": " + std::to_wstring(g_plot.GetList(info.szName, nullptr, 0)) + L" items";
		else if (info.kind == FPLK_DEFINITION && !std::isnan(info.value))
			text = std::wstring(info.szName) + L" = " + Number(info.value);
	}
	g_status.SetText(2, text.c_str());
}

// ---- Commands ------------------------------------------------------------------------

static void ToggleStyle(DWORD style) {
	DWORD s = g_plot.GetStyle();
	g_plot.SetWindowLong(GWL_STYLE, s ^ style);
}

static void LoadExample(int index) {
	auto& e = g_Examples[index];
	for (auto& name : g_autoParams)
		g_plot.RemoveParameter(name.c_str());
	g_autoParams.clear();
	g_rangedParams.clear();
	g_plot.RemoveAllGraphs();
	while (g_plot.GetListCount() > 0)
		g_plot.RemoveList(g_plot.GetListName(0));
	g_plot.SetAxisTitles(nullptr, nullptr, nullptr);
	g_plot.SetLogAxes(e.setup == Setup::Log, e.setup == Setup::Log);
	// on for the example that shows them, off again after it
	static bool glass = false;
	if (e.setup == Setup::Glass || glass) {
		glass = e.setup == Setup::Glass;
		g_seeThrough = g_meshLines = glass;
		g_heatmaps = !glass;
	}
	// a table's columns before the text that uses them
	if (e.setup == Setup::Data) {
		// a table: growth with noise, as if measured
		double xs[12], ys[12];
		const double noise[12] = { 0.3, -0.4, 0.2, 0.5, -0.6, 0.1, 0.7, -0.5, 0.4, -0.8, 0.6, -0.2 };
		for (int i = 0; i < 12; i++) {
			xs[i] = i * 0.8 - 2;
			ys[i] = 1.5 * std::exp(0.3 * xs[i]) + noise[i] * 0.4;
		}
		g_plot.SetList(L"x1", xs, 12);
		g_plot.SetList(L"y1", ys, 12);
	}
	DWORD style = g_plot.GetStyle();
	g_plot.SetWindowLong(GWL_STYLE, e.piTicks ? style | FPLS_PITICKS : style & ~FPLS_PITICKS);
	// on for the example that shows it, off for the others (a plot opened before may have turned it on)
	const DWORD now = g_plot.GetStyle();
	g_plot.SetWindowLong(GWL_STYLE, e.setup == Setup::Polar ? now | FPLS_POLARGRID : now & ~FPLS_POLARGRID);
	g_plot.SetMode(e.is3D ? FPLM_3D : FPLM_2D);
	if (e.is3D)
		g_plot.ResetView3D();
	g_plot.ClearHistory();
	g_edit.SetWindowText(e.text);   // a multiline edit sends no EN_CHANGE for this
	SyncGraphs();
	ShowLineError();
	g_edit.Invalidate(FALSE);
	if (e.setup == Setup::Animate) {
		// the animation: p around a circle, playing
		FPLPARAM p{ L"p", 0, 0, 6.283185307179586, 0, 0 };
		g_plot.SetParameter(p);
		g_plot.AnimateParameter(L"p", TRUE);
	}
	else if (e.setup == Setup::Styles) {
		// labels, a dotted line, hollow points, a faint curve, a darker region, the axes' titles
		auto setStyle = [](UINT id, UINT mask, LPCWSTR label, UINT flags, float opacity = 1, float fill = -1) {
			FPLGRAPH g{ mask, id };
			g.pszLabel = label;
			g.flags = flags;
			g.opacity = opacity;
			g.fillOpacity = fill;
			g_plot.SetGraph(g);
		};
		setStyle(1, FPLGM_LABEL | FPLGM_FLAGS, L"A({x}, {y})", FPLGF_HOLLOW);
		setStyle(2, FPLGM_LABEL, L"B: drag me", 0);
		setStyle(3, FPLGM_LABEL | FPLGM_FLAGS, L"parabola", FPLGF_DOTTED);
		setStyle(4, FPLGM_LABEL | FPLGM_OPACITY, L"faint", 0, 0.35f);
		setStyle(5, FPLGM_OPACITY, nullptr, 0, 1, 0.45f);
		g_plot.SetAxisTitles(L"time (s)", L"height (m)");
	}
	else if (e.setup == Setup::Bounds) {
		FPLPARAM a{ L"a", -2, -10, 10, 0.1, 0 }, b{ L"b", 4, -10, 10, 0.1, 0 };
		g_plot.SetParameter(a);
		g_plot.SetParameter(b);
	}
	else if (e.setup == Setup::FieldSlider) {
		FPLPARAM c{ L"c", 0.5, -1, 3, 0.05, 0 };
		g_plot.SetParameter(c);
	}
	else if (e.setup == Setup::Ode) {
		// the first solution's point: drag it
		FPLPARAM a{ L"a", 1, -8, 8, 0.1, 0 }, b{ L"b", 3, -6, 6, 0.1, 0 };
		g_plot.SetParameter(a);
		g_plot.SetParameter(b);
	}
	g_plot.ResetView();
	if (e.setup == Setup::Lorenz) {
		FPLVIEW3D v = g_plot.GetView3D();
		v.xMin = -25, v.xMax = 25, v.yMin = -30, v.yMax = 30, v.zMin = 0, v.zMax = 55;
		g_plot.SetView3D(v);
	}
	if (e.setup == Setup::Log) {
		FPLVIEW v{ 0.1, 1000, 0.01, 1e5 };
		g_plot.SetView(v);
		g_plot.SetAxisTitles(L"x (log)", L"y (log)");
	}
	ShowView();
}

static void UpdateMenu(HMENU menu) {
	DWORD s = g_plot.GetStyle();
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	check(ID_DEMO_EQUAL, s & FPLS_EQUALSCALE);
	check(ID_DEMO_PITICKS, s & FPLS_PITICKS);
	check(ID_DEMO_POLARGRID, s & FPLS_POLARGRID);
	check(ID_DEMO_ANIMATE, !(s & FPLS_NOANIMATION));
	check(ID_DEMO_SLOWTRANSITIONS, g_plot.GetTransitionTime() != 220);
	check(ID_DEMO_DEGREES, g_plot.GetDegrees() != FALSE);
	check(ID_DEMO_LEGEND, !(s & FPLS_NOLEGEND));
	check(ID_DEMO_SLIDERS, !(s & FPLS_NOSLIDERS));
	check(ID_DEMO_GRID, !(s & FPLS_NOGRID));
	check(ID_DEMO_AXES, !(s & FPLS_NOAXES));
	check(ID_DEMO_TRACE, !(s & FPLS_NOTRACE));
	check(ID_DEMO_AUTOSLIDERS, g_autoSliders);
	check(ID_DEMO_HIGHCONTRAST, g_plot.IsHighContrast());
	check(ID_DEMO_HEATMAPS, g_heatmaps);
	check(ID_DEMO_COLLAPSE, g_plot.IsPanelCollapsed() != FALSE);
	check(ID_DEMO_3D, g_plot.GetMode() == FPLM_3D);
	check(ID_DEMO_TANGENT, s & FPLS_TANGENT);
	check(ID_DEMO_MESHLINES, g_meshLines);
	check(ID_DEMO_LEVELLINES, g_levelLines);
	check(ID_DEMO_SEETHROUGH, g_seeThrough);
	check(ID_DEMO_LOGX, g_plot.IsLogX() != FALSE);
	check(ID_DEMO_LOGY, g_plot.IsLogY() != FALSE);
	::EnableMenuItem(menu, ID_DEMO_UNDO, MF_BYCOMMAND | (g_plot.CanUndo() ? MF_ENABLED : MF_GRAYED));
	::EnableMenuItem(menu, ID_DEMO_REDO, MF_BYCOMMAND | (g_plot.CanRedo() ? MF_ENABLED : MF_GRAYED));
#ifdef DEMO_WTLHELPER
	check(ID_DEMO_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void ShowFeatures() {
	// the traced graph's (or the first function's) features, in a message box
	FPLTRACE t;
	UINT id = g_plot.GetTrace(t) ? t.id : 0;
	for (int i = 0; !id && i < g_plot.GetGraphCount(); i++) {
		FPLGRAPHINFO info;
		if (g_plot.GetGraphInfo(g_plot.GetGraphId(i), info) && info.error == FPLE_NONE &&
			info.kind != FPLK_DEFINITION && info.kind != FPLK_POINT)
			id = g_plot.GetGraphId(i);
	}
	if (!id) {
		::MessageBoxW(g_main, L"There's no curve to look at.", L"Features", MB_ICONINFORMATION);
		return;
	}
	int n = g_plot.GetFeatures(id, nullptr, 0);
	std::vector<FPLFEATURE> features(n);
	g_plot.GetFeatures(id, features.data(), n);
	static const wchar_t* Kinds[] = { L"Root", L"Minimum", L"Maximum", L"Intersection", L"y-intercept" };
	std::wstring text = L"Line " + std::to_wstring(id) + L", over the view:\n\n";
	for (int i = 0; i < n && i < 40; i++)
		text += std::wstring(Kinds[features[i].kind]) + L"  (" + Number(features[i].x) + L", " + Number(features[i].y) + L")\n";
	if (n == 0)
		text += L"(none)";
	else if (n > 40)
		text += L"...and " + std::to_wstring(n - 40) + L" more";
	::MessageBoxW(g_main, text.c_str(), L"Features", MB_ICONINFORMATION);
}

// ---- Saving and opening a plot (GetState / SetState as UTF-8) ---------------------------------

static bool SavePlot(LPCWSTR path) {
	CString state = g_plot.GetState();
	int bytes = ::WideCharToMultiByte(CP_UTF8, 0, state, state.GetLength(), nullptr, 0, nullptr, nullptr);
	std::string utf8(bytes, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, state, state.GetLength(), utf8.data(), bytes, nullptr, nullptr);
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"wb") || !f)
		return false;
	bool ok = fwrite("\xEF\xBB\xBF", 1, 3, f) == 3 && fwrite(utf8.data(), 1, utf8.size(), f) == utf8.size();
	return fclose(f) == 0 && ok;
}

static bool OpenPlot(LPCWSTR path) {
	FILE* f = nullptr;
	if (_wfopen_s(&f, path, L"rb") || !f)
		return false;
	std::string bytes;
	char buf[4096];
	for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;)
		bytes.append(buf, n);
	fclose(f);
	if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0)
		bytes.erase(0, 3);
	int chars = ::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
	std::wstring text(chars, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), text.data(), chars);
	if (!g_plot.SetState(text.c_str()))
		return false;
	// the editor shows the graphs' texts, a line each; the parameters count as the demo's sliders
	std::wstring lines;
	for (int i = 0; i < g_plot.GetGraphCount(); i++) {
		FPLGRAPH g{ FPLGM_TEXT | FPLGM_LABEL, g_plot.GetGraphId(i) };
		g_plot.GetGraph(g);
		if (i)
			lines += L"\r\n";
		// a range line is kept as an empty graph's label
		lines += *g.pszText || !g.pszLabel ? g.pszText : g.pszLabel;
	}
	g_autoParams.clear();
	g_rangedParams.clear();
	for (int i = 0; i < g_plot.GetParameterCount(); i++) {
		FPLPARAM p{};
		g_plot.GetParameterAt(i, p);
		g_autoParams.insert(p.pszName);
	}
	g_edit.SetWindowText(lines.c_str());
	ConvertTheta();
	SyncGraphs();
	ShowLineError();
	ShowView();
	return true;
}

static void SaveOrOpen(bool save) {
	wchar_t path[MAX_PATH] = L"";
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = g_main;
	ofn.lpstrFilter = L"Plots (*.fplot)\0*.fplot\0All Files\0*.*\0";
	ofn.lpstrDefExt = L"fplot";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST;
	if (!(save ? ::GetSaveFileNameW(&ofn) : ::GetOpenFileNameW(&ofn)))
		return;
	if (!(save ? SavePlot(path) : OpenPlot(path)))
		::MessageBoxW(g_main, save ? L"The plot couldn't be saved." : L"That isn't a plot this demo saved.",
			save ? L"Save" : L"Open", MB_ICONERROR);
}

static void Export(bool svg) {
	wchar_t path[MAX_PATH] = L"plot";
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = g_main;
	ofn.lpstrFilter = svg ? L"SVG (*.svg)\0*.svg\0" : L"PNG (*.png)\0*.png\0";
	ofn.lpstrDefExt = svg ? L"svg" : L"png";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (!::GetSaveFileNameW(&ofn))
		return;
	// a PNG at twice the size, for a sharp picture
	BOOL ok = svg ? g_plot.ExportSvg(path) : g_plot.ExportPng(path, 2);
	if (!ok)
		::MessageBoxW(g_main, L"The file couldn't be written.", L"Export", MB_ICONERROR);
}

// The plot on a page: as large as fits inside half-inch margins.
static void PrintPlot() {
	PRINTDLGW pd{ sizeof(pd) };
	pd.hwndOwner = g_main;
	pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
	if (!::PrintDlgW(&pd) || !pd.hDC)
		return;
	DOCINFOW di{ sizeof(di), L"FunctionPlot" };
	bool ok = false;
	if (::StartDocW(pd.hDC, &di) > 0) {
		if (::StartPage(pd.hDC) > 0) {
			const int dpiX = ::GetDeviceCaps(pd.hDC, LOGPIXELSX), dpiY = ::GetDeviceCaps(pd.hDC, LOGPIXELSY);
			const int offX = ::GetDeviceCaps(pd.hDC, PHYSICALOFFSETX), offY = ::GetDeviceCaps(pd.hDC, PHYSICALOFFSETY);
			const int pageW = ::GetDeviceCaps(pd.hDC, PHYSICALWIDTH), pageH = ::GetDeviceCaps(pd.hDC, PHYSICALHEIGHT);
			RECT rc = { dpiX / 2 - offX, dpiY / 2 - offY, pageW - dpiX / 2 - offX, pageH - dpiY / 2 - offY };
			ok = g_plot.PrintTo(pd.hDC, rc) != FALSE;
			ok = ::EndPage(pd.hDC) > 0 && ok;
		}
		ok = ::EndDoc(pd.hDC) > 0 && ok;
	}
	::DeleteDC(pd.hDC);
	if (pd.hDevMode)
		::GlobalFree(pd.hDevMode);
	if (pd.hDevNames)
		::GlobalFree(pd.hDevNames);
	if (!ok)
		::MessageBoxW(g_main, L"The plot couldn't be printed.", L"Print", MB_ICONERROR);
}

// What PrintTo draws, into a bitmap file instead of a page (/export file.bmp): a 1600 x 1200 page with the plot in it.
static bool PrintToBitmap(const wchar_t* path) {
	const int w = 1600, h = 1200;
	BITMAPINFO bi{};
	bi.bmiHeader = { sizeof(bi.bmiHeader), w, h, 1, 24, BI_RGB };
	void* bits = nullptr;
	HDC dc = ::CreateCompatibleDC(nullptr);
	HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!bmp) {
		::DeleteDC(dc);
		return false;
	}
	auto old = ::SelectObject(dc, bmp);
	RECT page = { 0, 0, w, h };
	::FillRect(dc, &page, (HBRUSH)::GetStockObject(GRAY_BRUSH));
	RECT rc = { 50, 50, w - 50, h - 50 };
	bool ok = g_plot.PrintTo(dc, rc) != FALSE;
	::GdiFlush();
	::SelectObject(dc, old);
	::DeleteDC(dc);
	const DWORD stride = (w * 3 + 3) & ~3u, size = stride * h;
	const DWORD headers = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	BITMAPFILEHEADER fh{ 0x4D42, headers + size, 0, 0, headers };
	FILE* f = nullptr;
	if (ok && !_wfopen_s(&f, path, L"wb") && f) {
		ok = fwrite(&fh, sizeof(fh), 1, f) == 1 && fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f) == 1 &&
			fwrite(bits, size, 1, f) == 1;
		ok = fclose(f) == 0 && ok;
	}
	else
		ok = false;
	::DeleteObject(bmp);
	return ok;
}

static void OnCommand(HWND hwnd, UINT id) {
	if (id >= ID_EXAMPLE_FIRST && id < ID_EXAMPLE_FIRST + std::size(g_Examples)) {
		LoadExample(id - ID_EXAMPLE_FIRST);
		return;
	}
	switch (id) {
		case ID_DEMO_EXIT: ::DestroyWindow(hwnd); break;
		case ID_DEMO_HOME:
			if (g_plot.GetMode() == FPLM_3D)
				g_plot.ResetView3D(TRUE);
			else
				g_plot.ResetView(TRUE);
			ShowView();
			break;
		case ID_DEMO_ZOOMIN: g_plot.Zoom(1.5); ShowView(); break;
		case ID_DEMO_ZOOMOUT: g_plot.Zoom(1 / 1.5); ShowView(); break;
		case ID_DEMO_EQUAL: ToggleStyle(FPLS_EQUALSCALE); ShowView(); break;
		case ID_DEMO_PITICKS: ToggleStyle(FPLS_PITICKS); break;
		case ID_DEMO_POLARGRID: ToggleStyle(FPLS_POLARGRID); break;
		case ID_DEMO_DEGREES: g_plot.SetDegrees(!g_plot.GetDegrees()); break;
		case ID_DEMO_LEGEND: ToggleStyle(FPLS_NOLEGEND); break;
		case ID_DEMO_SLIDERS: ToggleStyle(FPLS_NOSLIDERS); break;
		case ID_DEMO_GRID: ToggleStyle(FPLS_NOGRID); break;
		case ID_DEMO_AXES: ToggleStyle(FPLS_NOAXES); break;
		case ID_DEMO_TRACE: ToggleStyle(FPLS_NOTRACE); break;
		case ID_DEMO_TANGENT: ToggleStyle(FPLS_TANGENT); break;
		case ID_DEMO_FIT: g_plot.ZoomToFit(TRUE); ShowView(); break;
		case ID_DEMO_ANIMATE: ToggleStyle(FPLS_NOANIMATION | FPLS_ALWAYSANIMATE); break;
		case ID_DEMO_SLOWTRANSITIONS: g_plot.SetTransitionTime(g_plot.GetTransitionTime() == 220 ? 1500 : 220); break;
		case ID_DEMO_LOGX: g_plot.SetLogAxes(!g_plot.IsLogX(), g_plot.IsLogY()); ShowView(); break;
		case ID_DEMO_LOGY: g_plot.SetLogAxes(g_plot.IsLogX(), !g_plot.IsLogY()); ShowView(); break;
		case ID_DEMO_COPY:
			if (!g_plot.Copy())
				::MessageBoxW(hwnd, L"The plot couldn't be copied.", L"Copy", MB_ICONERROR);
			break;
		case ID_DEMO_PRINT: PrintPlot(); break;
		case ID_DEMO_UNDO: g_plot.Undo(); ShowView(); break;
		case ID_DEMO_REDO: g_plot.Redo(); ShowView(); break;
		case ID_DEMO_AUTOSLIDERS: g_autoSliders = !g_autoSliders; SyncGraphs(); break;
		case ID_DEMO_HIGHCONTRAST: g_plot.ForceHighContrast(g_plot.IsHighContrast() ? 0 : 1); break;
		case ID_DEMO_FEATURES: ShowFeatures(); break;
		case ID_DEMO_EXPORTPNG: Export(false); break;
		case ID_DEMO_OPEN: SaveOrOpen(false); break;
		case ID_DEMO_SAVE: SaveOrOpen(true); break;
		case ID_DEMO_EXPORTSVG: Export(true); break;
		case ID_DEMO_HEATMAPS: g_heatmaps = !g_heatmaps; SyncGraphs(); break;
		case ID_DEMO_MESHLINES: g_meshLines = !g_meshLines; SyncGraphs(); break;
		case ID_DEMO_LEVELLINES: g_levelLines = !g_levelLines; SyncGraphs(); break;
		case ID_DEMO_SEETHROUGH: g_seeThrough = !g_seeThrough; SyncGraphs(); break;
		case ID_DEMO_COLLAPSE: g_plot.SetPanelCollapsed(!g_plot.IsPanelCollapsed()); break;
		case ID_DEMO_3D:
			if (!g_plot.SetMode(g_plot.GetMode() == FPLM_3D ? FPLM_2D : FPLM_3D))
				::MessageBoxW(hwnd, L"3D needs Direct3D 11.", L"3D", MB_ICONERROR);
			ShowView();
			break;
#ifdef DEMO_WTLHELPER
		case ID_DEMO_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
	}
}

// ---- The window --------------------------------------------------------------------

static void Layout(HWND hwnd) {
	RECT rc;
	::GetClientRect(hwnd, &rc);
	g_status.SendMessage(WM_SIZE);
	RECT rs;
	g_status.GetWindowRect(&rs);
	const int statusHeight = rs.bottom - rs.top;
	g_splitter.SetWindowPos(nullptr, 0, 0, rc.right, rc.bottom - statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	// WTL keeps its limits on the bar's moves only: a narrower window mustn't squeeze the plot away
	if (g_splitter.GetSplitterPos() >= 0)
		g_splitter.SetSplitterPos(g_splitter.GetSplitterPos());
	int parts[] = { rc.right * 45 / 100, rc.right * 75 / 100, -1 };
	g_status.SetParts(3, parts);
}

// ---- The lines' errors, marked in the edit ------------------------------------------

static void SetEditMargins(HWND hwnd) {
	// on the left, room for a dot beside a line that doesn't read
	const UINT dpi = ::GetDpiForWindow(hwnd);
	g_edit.SetMargins(::MulDiv(14, dpi, 96), ::MulDiv(6, dpi, 96));
}

static bool g_marksShown = false;

// A dot in the margin and a wavy line under the part the error is about, over what the edit has painted.
static void PaintErrorMarks(HWND hwnd) {
	CEdit edit(hwnd);
	const int length = edit.GetWindowTextLength();
	std::wstring text(length + 1, L'\0');
	edit.GetWindowText(text.data(), length + 1);
	text.resize(length);
	HDC dc = ::GetDC(hwnd);
	const UINT dpi = ::GetDpiForWindow(hwnd);
	const bool dark = GetRValue(::GetSysColor(COLOR_WINDOW)) < 128;
	const COLORREF color = dark ? RGB(255, 107, 107) : RGB(196, 43, 28);
	HPEN pen = ::CreatePen(PS_SOLID, std::max(1, ::MulDiv(1, dpi, 96)), color);
	HBRUSH brush = ::CreateSolidBrush(color);
	HGDIOBJ oldPen = ::SelectObject(dc, pen), oldBrush = ::SelectObject(dc, brush);
	HGDIOBJ oldFont = ::SelectObject(dc, edit.GetFont());
	TEXTMETRICW tm{};
	::GetTextMetricsW(dc, &tm);
	const int wave = std::max(2, ::MulDiv(2, dpi, 96));
	auto posOf = [&](int ch) {
		const LRESULT r = edit.SendMessage(EM_POSFROMCHAR, ch);
		return POINT{ (short)LOWORD(r), (short)HIWORD(r) };
	};
	bool any = false;
	int start = 0, id = 1;
	for (;;) {
		size_t end = text.find(L'\n', start);
		const int lineEnd = end == std::wstring::npos ? length : (int)end - (end > 0 && text[end - 1] == L'\r' ? 1 : 0);
		FPLGRAPHINFO info{};
		RangeLine range;
		const bool badRange = lineEnd > start && ParseRangeLine(text.substr(start, lineEnd - start), range) == RangeKind::Bad;
		if (badRange) {
			// what's after the colon
			info.errorPos = (int)range.at;
			info.errorLen = lineEnd - start - (int)range.at;
		}
		if (badRange || (g_plot.GetGraphInfo((UINT)id, info) && info.error != FPLE_NONE && info.error != FPLE_EMPTY && lineEnd > start)) {
			any = true;
			// the dot, at the line's first row
			const POINT first = posOf(start);
			const int d = ::MulDiv(6, dpi, 96), x = ::MulDiv(4, dpi, 96), y = first.y + (tm.tmHeight - d) / 2;
			::Ellipse(dc, x, y, x + d, y + d);
			// the wave, row by row: a part at the very end of the line goes under its last character
			int a = std::clamp(start + info.errorPos, start, lineEnd - 1);
			int b = std::clamp(a + std::max(info.errorLen, 1), a + 1, lineEnd);
			const int rowA = edit.LineFromChar(a), rowB = edit.LineFromChar(b - 1);
			for (int row = rowA; row <= rowB; row++) {
				const int rowStart = edit.LineIndex(row);
				const int from = std::max(a, rowStart), to = std::min(b, rowStart + edit.LineLength(rowStart));
				if (to <= from)
					continue;
				const POINT left = posOf(from), last = posOf(to - 1);
				SIZE one{};
				::GetTextExtentPoint32W(dc, text.c_str() + to - 1, 1, &one);
				const int base = left.y + tm.tmHeight - wave;
				int px = left.x;
				bool up = false;
				::MoveToEx(dc, px, base, nullptr);
				while (px < last.x + one.cx) {
					px += wave;
					::LineTo(dc, px, up ? base : base + wave);
					up = !up;
				}
			}
		}
		if (end == std::wstring::npos)
			break;
		start = (int)end + 1;
		id++;
	}
	::SelectObject(dc, oldFont);
	::SelectObject(dc, oldBrush);
	::SelectObject(dc, oldPen);
	::DeleteObject(brush);
	::DeleteObject(pen);
	::ReleaseDC(hwnd, dc);
	g_marksShown = any;
}

// The edit draws its text and selection directly as well as in WM_PAINT, over the marks: asked to paint again after what
// does that, while there are marks to keep.
static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
	switch (msg) {
		case WM_PAINT:
			PaintErrorMarks(hwnd);
			break;
		case WM_KEYUP: case WM_LBUTTONUP: case WM_MOUSEWHEEL: case WM_VSCROLL: case WM_SETFOCUS: case WM_KILLFOCUS:
			if (g_marksShown)
				::InvalidateRect(hwnd, nullptr, FALSE);
			break;
		case WM_MOUSEMOVE:
			if (g_marksShown && (wParam & MK_LBUTTON))
				::InvalidateRect(hwnd, nullptr, FALSE);
			break;
	}
	return result;
}

// The plot's resizing changes what's in view without a notification (those are for the user's moves): the status bar's
// range follows here, as dragging the splitter doesn't reach the main window.
static LRESULT CALLBACK PlotSizeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
	if (msg == WM_SIZE && g_status.IsWindow())
		ShowView();
	return result;
}

// A double click on the bar goes back to this; neither pane can be dragged smaller than the least.
static void SetSplitterLimits(UINT dpi) {
	g_splitter.m_xySplitterDefPos = ::MulDiv(300, dpi, 96);
	g_splitter.m_cxyMin = ::MulDiv(120, dpi, 96);
}

static void MakeEditFont(HWND hwnd) {
	const UINT dpi = ::GetDpiForWindow(hwnd);
	if (!g_editFont.IsNull())
		g_editFont.DeleteObject();
	g_editFont.CreateFont(-::MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_edit.SetFont(g_editFont);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_splitter.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
			// the lines keep their width as the window's size changes
			g_splitter.SetSplitterExtendedStyle(0, SPLIT_PROPORTIONAL);
			SetSplitterLimits(::GetDpiForWindow(hwnd));
			g_edit.Create(g_splitter, CWindow::rcDefault, nullptr,
				WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_TABSTOP,
				WS_EX_CLIENTEDGE, IDC_EDIT);
			::SetWindowSubclass(g_edit, EditProc, 1, 0);
			MakeEditFont(hwnd);
			SetEditMargins(hwnd);
			g_plot.Create(g_splitter, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | FPLS_EQUALSCALE | FPLS_ALWAYSANIMATE,
				0, IDC_PLOT);
			g_splitter.SetSplitterPanes(g_edit, g_plot);
			::SetWindowSubclass(g_plot, PlotSizeProc, 1, 0);
			g_status.Create(hwnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, IDC_STATUS);
			Layout(hwnd);
			g_splitter.SetSplitterPos();		// its default: there's a size to place it in now
			LoadExample(g_startExample);
			if (!g_openPath.empty() && !OpenPlot(g_openPath.c_str()))
				g_exitCode = 2;
			if (!g_exportPath.empty()) {
				auto is = [](LPCWSTR ext) {
					return g_exportPath.size() > 4 && _wcsicmp(g_exportPath.c_str() + g_exportPath.size() - 4, ext) == 0;
				};
				BOOL ok = is(L".svg") ? g_plot.ExportSvg(g_exportPath.c_str())
					: is(L".bmp") ? PrintToBitmap(g_exportPath.c_str())
					: g_exportPath.size() > 6 && _wcsicmp(g_exportPath.c_str() + g_exportPath.size() - 6, L".fplot") == 0 ? SavePlot(g_exportPath.c_str())
					: g_plot.ExportPng(g_exportPath.c_str(), 1);
				// a quit posted while the window is being made is lost: close it once the loop runs
				g_exitCode = ok ? 0 : 1;
				::PostMessage(hwnd, WM_CLOSE, 0, 0);
			}
			FPLTRACE none{};
			ShowTrace(none);
			g_edit.SetFocus();
			return 0;
		}
		case WM_SIZE:
			Layout(hwnd);
			return 0;
		case WM_DPICHANGED: {
			auto rc = (const RECT*)lParam;
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			MakeEditFont(hwnd);
			SetEditMargins(hwnd);
			{
				const int before = g_splitter.m_xySplitterDefPos;
				SetSplitterLimits(HIWORD(wParam));
#ifndef DEMO_WTLHELPER
				// (the custom splitter scales its own)
				g_splitter.SetSplitterPos(::MulDiv(g_splitter.GetSplitterPos(), g_splitter.m_xySplitterDefPos, before));
#else
				(void)before;
#endif
			}
			return 0;
		}
		case WM_INITMENUPOPUP:
			UpdateMenu((HMENU)wParam);
			return 0;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_EDIT) {
				if (HIWORD(wParam) == EN_CHANGE) {
					ConvertTheta();
					SyncGraphs();
					ShowLineError();
					// the graphs' errors may have changed
					g_edit.Invalidate(FALSE);
				}
				return 0;
			}
			OnCommand(hwnd, LOWORD(wParam));
			return 0;
		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom == IDC_PLOT) {
				switch (hdr->code) {
					case FPLN_TRACE: ShowTrace(((NMFPLTRACE*)lParam)->trace); break;
					case FPLN_VIEWCHANGED: case FPLN_VIEW3DCHANGED: ShowView(); break;
					case FPLN_PARAMCHANGED: ShowLineError(); break;
				}
			}
			return 0;
		}
		case WM_DESTROY:
			::PostQuitMessage(g_exitCode);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	int argc = 0;
	if (LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
		for (int i = 1; i + 1 < argc; i++) {
			if (_wcsicmp(argv[i], L"/example") == 0)
				g_startExample = std::clamp(_wtoi(argv[++i]), 0, (int)std::size(g_Examples) - 1);
			else if (_wcsicmp(argv[i], L"/export") == 0)
				g_exportPath = argv[++i];
			else if (_wcsicmp(argv[i], L"/open") == 0)
				g_openPath = argv[++i];
		}
		::LocalFree(argv);
	}
	// WIC (Export PNG) needs COM
	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES };
	::InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"FunctionPlotDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	HMENU hExamples = ::CreatePopupMenu();
	for (UINT i = 0; i < std::size(g_Examples); i++)
		::AppendMenuW(hExamples, MF_STRING, ID_EXAMPLE_FIRST + i, g_Examples[i].name);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_OPEN, L"&Open Plot...\tCtrl+O");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_SAVE, L"&Save Plot...\tCtrl+S");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_POPUP, (UINT_PTR)hExamples, L"&Examples");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_EXPORTPNG, L"Export &PNG...");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_EXPORTSVG, L"Export &SVG...");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_COPY, L"&Copy Plot\tCtrl+Shift+C");
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_PRINT, L"P&rint...\tCtrl+P");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_DEMO_EXIT, L"E&xit");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_DEMO_3D, L"&3D\tCtrl+3");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HOME, L"&Reset View\tCtrl+Home");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMIN, L"Zoom &In\tCtrl++");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ZOOMOUT, L"Zoom &Out\tCtrl+-");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_FIT, L"Zoom to &Fit\tCtrl+F");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_UNDO, L"&Undo Plot Change");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_REDO, L"Red&o Plot Change");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_EQUAL, L"&Equal Scales");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_LOGX, L"Lo&g X Axis");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_LOGY, L"Log &Y Axis");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_LEGEND, L"&Legend");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_SLIDERS, L"&Sliders");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_COLLAPSE, L"&Collapse Panel\tCtrl+L");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HEATMAPS, L"&Heat Maps");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_MESHLINES, L"3D: &Mesh Lines");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_LEVELLINES, L"3D: Height Li&nes");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_SEETHROUGH, L"3D: See-Th&rough Surfaces");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_AUTOSLIDERS, L"&Automatic Sliders");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_GRID, L"&Grid");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_AXES, L"A&xes");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_PITICKS, L"&Pi Ticks");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_POLARGRID, L"Pola&r Grid");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_ANIMATE, L"Animate Transitions");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_SLOWTRANSITIONS, L"Slow Transitions");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_TRACE, L"&Tracing");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_TANGENT, L"Ta&ngent Line");
	::AppendMenuW(hView, MF_STRING, ID_DEMO_DEGREES, L"&Degrees");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_DEMO_HIGHCONTRAST, L"&High Contrast Drawing");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_DEMO_DARKMODE, L"Dar&k Mode");
#endif

	HMENU hAnalysis = ::CreatePopupMenu();
	::AppendMenuW(hAnalysis, MF_STRING, ID_DEMO_FEATURES, L"&Features of the Traced Graph...\tF2");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hAnalysis, L"&Analysis");

	ACCEL accels[] = {
		{ FVIRTKEY | FCONTROL, VK_HOME, ID_DEMO_HOME },
		{ FVIRTKEY | FCONTROL, VK_OEM_PLUS, ID_DEMO_ZOOMIN },
		{ FVIRTKEY | FCONTROL, VK_ADD, ID_DEMO_ZOOMIN },
		{ FVIRTKEY | FCONTROL, VK_OEM_MINUS, ID_DEMO_ZOOMOUT },
		{ FVIRTKEY | FCONTROL, VK_SUBTRACT, ID_DEMO_ZOOMOUT },
		{ FVIRTKEY, VK_F2, ID_DEMO_FEATURES },
		{ FVIRTKEY | FCONTROL, 'L', ID_DEMO_COLLAPSE },
		{ FVIRTKEY | FCONTROL, '3', ID_DEMO_3D },
		{ FVIRTKEY | FCONTROL, 'O', ID_DEMO_OPEN },
		{ FVIRTKEY | FCONTROL, 'F', ID_DEMO_FIT },
		{ FVIRTKEY | FCONTROL | FSHIFT, 'C', ID_DEMO_COPY },
		{ FVIRTKEY | FCONTROL, 'P', ID_DEMO_PRINT },
		{ FVIRTKEY | FCONTROL, 'S', ID_DEMO_SAVE },
	};
	HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)std::size(accels));

	HWND hwnd = ::CreateWindowExW(0, L"FunctionPlotDemoWnd", L"FunctionPlot Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1300, 860, nullptr, hMenu, hInstance, nullptr);
	::ShowWindow(hwnd, nCmdShow);
	::UpdateWindow(hwnd);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (!::TranslateAccelerator(hwnd, hAccel, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	_Module.Term();
	::CoUninitialize();
	return (int)msg.wParam;
}
