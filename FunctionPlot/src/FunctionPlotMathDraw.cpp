// Drawing a laid out expression with Direct2D and DirectWrite, and the control that shows one (CFunctionPlotMathCtrl).

#include "FunctionPlotData.h"

#include <algorithm>
#include <map>

namespace {

IDWriteTextFormat* MathFormat(double size, bool italic) {
	// each is kept: there are few sizes
	static std::map<std::pair<int, bool>, ComPtr<IDWriteTextFormat>> formats;
	const std::pair<int, bool> key{ static_cast<int>(std::lround(size * 16)), italic };
	auto it = formats.find(key);
	if (it != formats.end())
		return it->second.Get();
	ComPtr<IDWriteTextFormat> format;
	// Cambria Math has no italics of its own, and Cambria's go with it
	if (FAILED(FplDWriteFactory()->CreateTextFormat(italic ? L"Cambria" : L"Cambria Math", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
		italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(size), L"",
		format.GetAddressOf())))
		return nullptr;
	format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	formats[key] = format;
	return format.Get();
}

ComPtr<IDWriteTextLayout> MakeLayout(const std::wstring& text, double size, bool italic) {
	ComPtr<IDWriteTextLayout> layout;
	auto* format = MathFormat(size, italic);
	if (format)
		FplDWriteFactory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 10000, 1000, layout.GetAddressOf());
	return layout;
}

}  // namespace

FplMathMeasure FplDWriteMeasure() {
	return [](const std::wstring& text, double size, bool italic) {
		auto layout = MakeLayout(text, size, italic);
		if (!layout)
			return text.size() * size * 0.5;
		DWRITE_TEXT_METRICS m{};
		layout->GetMetrics(&m);
		return static_cast<double>(m.widthIncludingTrailingWhitespace);
	};
}

std::shared_ptr<FplMathDrawing> FplMakeMathDrawing(FplMathLayout layout) {
	auto drawing = std::make_shared<FplMathDrawing>();
	drawing->layout = std::move(layout);
	const size_t n = drawing->layout.items.size();
	drawing->texts.resize(n);
	drawing->baselines.resize(n);
	drawing->lines.resize(n);
	for (size_t i = 0; i < n; i++) {
		auto& item = drawing->layout.items[i];
		if (item.isLine) {
			ComPtr<ID2D1GeometrySink> sink;
			if (item.points.size() < 2 || FAILED(FplD2DFactory()->CreatePathGeometry(drawing->lines[i].GetAddressOf())) ||
				FAILED(drawing->lines[i]->Open(sink.GetAddressOf())))
				continue;
			sink->BeginFigure({ static_cast<float>(item.points[0].x + item.x), static_cast<float>(item.points[0].y + item.y) },
				D2D1_FIGURE_BEGIN_HOLLOW);
			for (size_t p = 1; p < item.points.size(); p++)
				sink->AddLine({ static_cast<float>(item.points[p].x + item.x), static_cast<float>(item.points[p].y + item.y) });
			sink->EndFigure(D2D1_FIGURE_END_OPEN);
			sink->Close();
			continue;
		}
		drawing->texts[i] = MakeLayout(item.text, item.size, item.italic);
		if (drawing->texts[i]) {
			// Cambria Math's lines are very tall: where its baseline is has to be asked
			DWRITE_LINE_METRICS metrics{};
			UINT32 count = 0;
			if (SUCCEEDED(drawing->texts[i]->GetLineMetrics(&metrics, 1, &count)) && count > 0)
				drawing->baselines[i] = metrics.baseline;
		}
	}
	return drawing;
}

void FplDrawMath(ID2D1RenderTarget* rt, const FplMathDrawing& drawing, float x, float baseline, ID2D1Brush* brush, ID2D1StrokeStyle* stroke) {
	const auto saved = [&] {
		D2D1_MATRIX_3X2_F m;
		rt->GetTransform(&m);
		return m;
	}();
	auto& items = drawing.layout.items;
	for (size_t i = 0; i < items.size(); i++) {
		auto& item = items[i];
		if (item.isLine) {
			if (!drawing.lines[i])
				continue;
			rt->SetTransform(D2D1::Matrix3x2F::Translation(x, baseline) * saved);
			rt->DrawGeometry(drawing.lines[i].Get(), brush, static_cast<float>(item.stroke), stroke);
			rt->SetTransform(saved);
		}
		else if (drawing.texts[i]) {
			rt->DrawTextLayout({ x + static_cast<float>(item.x), baseline + static_cast<float>(item.y) - drawing.baselines[i] },
				drawing.texts[i].Get(), brush);
		}
	}
}

// ---- The control ------------------------------------------------------------------------------------------------------

struct FplMathViewState {
	ComPtr<ID2D1HwndRenderTarget> target;
	ComPtr<ID2D1StrokeStyle> stroke;
	std::wstring text;
	const CFunctionPlotCtrl* plot = nullptr;
	std::shared_ptr<FplMathDrawing> math;
	ComPtr<IDWriteTextFormat> plain;
};

namespace {

D2D1_COLOR_F ToColor(COLORREF c) {
	return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f);
}

}  // namespace

CFunctionPlotMathCtrl::CFunctionPlotMathCtrl() : m_State(std::make_unique<FplMathViewState>()) {
}

CFunctionPlotMathCtrl::~CFunctionPlotMathCtrl() = default;

void CFunctionPlotMathCtrl::Fit() {
	auto& s = *m_State;
	s.math = nullptr;
	if (s.text.empty() || !IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	const float scale = 96.0f / ::GetDpiForWindow(m_hWnd);
	const double width = std::max(0.0f, rc.right * scale - 16);
	FplParseOptions options;
	if (s.plot)
		options = s.plot->m_State->plot.DisplayOptions();
	FplMathLayout layout;
	if (FplFitMath(s.text, options, { 22, 19, 16, 14, 12 }, width, FplDWriteMeasure(), layout))
		s.math = FplMakeMathDrawing(std::move(layout));
}

void CFunctionPlotMathCtrl::SetText(LPCWSTR text, const CFunctionPlotCtrl* plot) {
	auto& s = *m_State;
	s.text = text ? text : L"";
	s.plot = plot;
	Fit();
	if (IsWindow())
		Invalidate();
}

int CFunctionPlotMathCtrl::GetIdealHeight() const {
	auto& s = *m_State;
	const double dips = s.math ? s.math->layout.ascent + s.math->layout.descent + 20 : 44;
	return ::MulDiv(static_cast<int>(std::ceil(std::max(dips, 44.0))), ::GetDpiForWindow(m_hWnd), 96);
}

LRESULT CFunctionPlotMathCtrl::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	if (s.target)
		s.target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
	Fit();
	Invalidate();
	return 0;
}

LRESULT CFunctionPlotMathCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_State->target.Reset();
	Fit();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotMathCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CFunctionPlotMathCtrl::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	PAINTSTRUCT ps;
	BeginPaint(&ps);
	RECT rc;
	GetClientRect(&rc);
	const float dpi = static_cast<float>(::GetDpiForWindow(m_hWnd));
	if (!s.target) {
		const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
		const auto hwnd = D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(rc.right, rc.bottom), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
		FplD2DFactory()->CreateHwndRenderTarget(props, hwnd, s.target.GetAddressOf());
	}
	if (!s.stroke) {
		D2D1_STROKE_STYLE_PROPERTIES round{};
		round.startCap = round.endCap = D2D1_CAP_STYLE_ROUND;
		round.lineJoin = D2D1_LINE_JOIN_ROUND;
		FplD2DFactory()->CreateStrokeStyle(round, nullptr, 0, s.stroke.GetAddressOf());
	}
	if (!s.plain)
		FplDWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, 13, L"", s.plain.GetAddressOf());
	if (s.target) {
		s.target->BeginDraw();
		s.target->SetTransform(D2D1::Matrix3x2F::Identity());
		s.target->Clear(ToColor(::GetSysColor(COLOR_WINDOW)));
		const float w = rc.right * 96 / dpi, h = rc.bottom * 96 / dpi;
		ComPtr<ID2D1SolidColorBrush> text, muted;
		s.target->CreateSolidColorBrush(ToColor(::GetSysColor(COLOR_WINDOWTEXT)), text.GetAddressOf());
		s.target->CreateSolidColorBrush(ToColor(::GetSysColor(COLOR_GRAYTEXT)), muted.GetAddressOf());
		if (muted)
			s.target->DrawLine({ 0, 0.5f }, { w, 0.5f }, muted.Get(), 96 / dpi);
		if (s.math && text) {
			auto& layout = s.math->layout;
			FplDrawMath(s.target.Get(), *s.math, 8, (h + static_cast<float>(layout.ascent - layout.descent)) / 2, text.Get(), s.stroke.Get());
		}
		else if (!s.text.empty() && muted && s.plain) {
			// what doesn't read is shown as it is
			const D2D1_RECT_F place = { 8, h / 2 - 10, w - 8, h / 2 + 10 };
			s.target->DrawText(s.text.c_str(), static_cast<UINT32>(s.text.size()), s.plain.Get(), place, muted.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
		if (s.target->EndDraw() == D2DERR_RECREATE_TARGET)
			s.target.Reset();
	}
	EndPaint(&ps);
	return 0;
}
