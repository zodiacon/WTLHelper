// The markdown view's painting: Direct2D and DirectWrite resources, the boxes' text layouts, and drawing them.

#include "MarkdownViewData.h"

#include <dwrite_3.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// ---- Factories -----------------------------------------------------------------------

ID2D1Factory* MdvD2DFactory() {
	// shared by the views on every thread
	static ComPtr<ID2D1Factory> factory = [] {
		ComPtr<ID2D1Factory> f;
		::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, f.GetAddressOf());
		return f;
	}();
	return factory.Get();
}

IDWriteFactory* MdvDWriteFactory() {
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

bool HasFamily(const wchar_t* name) {
	ComPtr<IDWriteFontCollection> fonts;
	if (FAILED(MdvDWriteFactory()->GetSystemFontCollection(fonts.GetAddressOf())))
		return false;
	UINT32 index;
	BOOL exists = FALSE;
	return SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists;
}

MdvFontMetrics FamilyMetrics(const std::wstring& name) {
	MdvFontMetrics m;
	ComPtr<IDWriteFontCollection> fonts;
	ComPtr<IDWriteFontFamily> family;
	ComPtr<IDWriteFont> font;
	UINT32 index;
	BOOL exists = FALSE;
	if (FAILED(MdvDWriteFactory()->GetSystemFontCollection(fonts.GetAddressOf())) ||
		FAILED(fonts->FindFamilyName(name.c_str(), &index, &exists)) || !exists ||
		FAILED(fonts->GetFontFamily(index, family.GetAddressOf())) ||
		FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())))
		return m;
	DWRITE_FONT_METRICS fm;
	font->GetMetrics(&fm);
	m.ascent = (float)fm.ascent / fm.designUnitsPerEm;
	m.descent = (float)fm.descent / fm.designUnitsPerEm;
	m.lineGap = (float)fm.lineGap / fm.designUnitsPerEm;
	return m;
}

// the target the images' inline objects draw on while a box is painted
ID2D1RenderTarget* g_paintTarget = nullptr;
ID2D1Brush* g_paintFrame = nullptr;

}  // namespace

// ---- Images in the text ------------------------------------------------------------

// an image's place in a text layout: its size, drawn as a frame until images load (phase 3)
class MdvImageObject : public IDWriteInlineObject {
public:
	MdvImageObject(float width, float height) : m_Width(width), m_Height(height) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) {
			*ppv = static_cast<IDWriteInlineObject*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_Refs; }
	ULONG STDMETHODCALLTYPE Release() override {
		const ULONG refs = --m_Refs;
		if (refs == 0)
			delete this;
		return refs;
	}

	HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL, IUnknown*) override {
		if (g_paintTarget && g_paintFrame)
			g_paintTarget->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 0.5f, y + 0.5f, x + m_Width - 0.5f, y + m_Height - 0.5f), 2, 2), g_paintFrame);
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override {
		*metrics = { m_Width, m_Height, m_Height, FALSE };   // on the baseline
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override {
		*overhangs = {};
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override {
		*before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
		return S_OK;
	}

private:
	ULONG m_Refs = 1;
	float m_Width, m_Height;
};

// ---- Resources -------------------------------------------------------------------------

ID2D1SolidColorBrush* MdvState::Brush(COLORREF color) {
	auto& brush = brushes[color];
	if (!brush && target)
		target->CreateSolidColorBrush(Color(color), brush.GetAddressOf());
	return brush.Get();
}

IDWriteTextFormat* MdvState::Format(int family, float size, DWRITE_FONT_WEIGHT weight, float lineHeight) {
	const auto key = std::make_tuple(family, (int)std::lround(size * 64), (int)weight, (int)std::lround(lineHeight * 100));
	auto& format = formats[key];
	if (format)
		return format.Get();
	const std::wstring& name = family == 1 ? codeFamily : family == 2 ? iconFamily : bodyFamily;
	wchar_t locale[LOCALE_NAME_MAX_LENGTH] = L"en-us";
	::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
	if (FAILED(MdvDWriteFactory()->CreateTextFormat(name.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL, size, locale, format.GetAddressOf())))
		return nullptr;
	// proportional spacing keeps tall inline images from overlapping the lines; the extra space is shared above
	// and below the text, as CSS's line-height does
	const MdvFontMetrics& fm = family == 1 ? codeFont : bodyFont;
	const float natural = fm.ascent + fm.descent + fm.lineGap;
	const float factor = lineHeight / natural;
	const float baseline = ((lineHeight - fm.ascent - fm.descent) / 2 + fm.ascent) / (fm.ascent + fm.lineGap / 2);
	format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, factor, baseline);
	return format.Get();
}

void CMarkdownViewCtrl::UpdateFormats() {
	auto& s = *m_State;
	s.bodyFamily = s.style.szFont[0] ? s.style.szFont : L"Segoe UI";
	if (s.style.szCodeFont[0])
		s.codeFamily = s.style.szCodeFont;
	else
		s.codeFamily = HasFamily(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
	s.iconFamily = HasFamily(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
	s.bodyFont = FamilyMetrics(s.bodyFamily);
	s.codeFont = FamilyMetrics(s.codeFamily);
	s.formats.clear();
}

bool CMarkdownViewCtrl::CreateTarget() {
	auto& s = *m_State;
	if (s.target)
		return true;
	const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), (float)s.dpi, (float)s.dpi);
	const auto hwndProps = D2D1::HwndRenderTargetProperties(m_hWnd, D2D1::SizeU(s.client.cx, s.client.cy), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
	if (FAILED(MdvD2DFactory()->CreateHwndRenderTarget(props, hwndProps, s.target.GetAddressOf())))
		return false;
	for (auto& [key, cache] : s.layouts)
		cache.effects = false;
	return true;
}

void CMarkdownViewCtrl::DiscardTarget() {
	auto& s = *m_State;
	s.target.Reset();
	s.brushes.clear();
	for (auto& [key, cache] : s.layouts)
		cache.effects = false;
}

void CMarkdownViewCtrl::DiscardLayouts() {
	m_State->layouts.clear();
}

// ---- Layouts ---------------------------------------------------------------------------

// a table, until phase 3 lays it out: its rows as lines of text
static std::wstring TablePlaceholder(const MdvDocument& doc, const MdvBlock& table) {
	std::wstring text;
	for (int row : table.children) {
		if (!text.empty())
			text += L'\n';
		bool first = true;
		for (int cell : doc.blocks[row].children) {
			if (!first)
				text += L"    ";
			first = false;
			for (wchar_t ch : doc.blocks[cell].content.text)
				text += ch == MdvObjectChar ? L' ' : ch;
		}
	}
	return text;
}

bool CMarkdownViewCtrl::Measure(size_t index) {
	auto& s = *m_State;
	auto& box = s.flow.boxes[index];
	if (box.kind == MdvBoxKind::Rule || box.kind == MdvBoxKind::AlertTitle) {
		box.measured = true;
		return false;
	}
	auto& block = s.doc.blocks[box.block];
	auto& m = s.metrics;
	const float em = m.fontSize * box.scale;
	const float width = std::max(1.0f, box.TextWidth(ColumnWidth()));
	const bool code = box.kind == MdvBoxKind::Code;
	const bool heading = block.kind == MdvKind::Heading;

	std::wstring tableText;
	const MdvInline* content = nullptr;
	const wchar_t* text;
	size_t length;
	if (box.kind == MdvBoxKind::Table) {
		tableText = TablePlaceholder(s.doc, block);
		text = tableText.c_str();
		length = tableText.size();
	}
	else {
		content = &block.content;
		text = content->text.c_str();
		length = content->text.size();
	}

	auto format = s.Format(code ? 1 : 0, em, heading ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
		code ? m.codeLineHeight : heading ? m.headingLineHeight : m.lineHeight);
	ComPtr<IDWriteTextLayout> layout;
	if (!format || FAILED(MdvDWriteFactory()->CreateTextLayout(text, (UINT32)length, format, code ? 100000.0f : width, 1e7f, layout.GetAddressOf()))) {
		box.measured = true;
		return false;
	}
	MdvLayoutCache cache;
	if (code)
		layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	else if (block.align == MdvAlign::Center)
		layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	else if (block.align == MdvAlign::Right)
		layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

	if (content && !code) {
		for (auto& run : content->runs) {
			const DWRITE_TEXT_RANGE range{ (UINT32)run.start, (UINT32)run.length };
			if (run.style & MdvBold)
				layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
			if (run.style & MdvItalic)
				layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
			if (run.style & MdvStrike)
				layout->SetStrikethrough(TRUE, range);
			if (run.style & MdvUnderline)
				layout->SetUnderline(TRUE, range);
			if (run.style & (MdvCode | MdvKbd | MdvMath)) {
				layout->SetFontFamilyName(s.codeFamily.c_str(), range);
				layout->SetFontSize(em * m.codeScale, range);
			}
			if (run.style & (MdvSub | MdvSup))
				layout->SetFontSize(em * 0.75f, range);
		}
		const float zoom = s.zoom / 100.0f;
		for (auto& img : content->images) {
			float w = img.width * zoom, h = img.height * zoom;
			if (w <= 0 && h <= 0) {
				w = 1.25f * em;
				h = em;
			}
			else if (w <= 0)
				w = h;
			else if (h <= 0)
				h = w;
			w = std::min(w, width);
			ComPtr<MdvImageObject> obj;
			obj.Attach(new MdvImageObject(w, h));
			layout->SetInlineObject(obj.Get(), { (UINT32)img.pos, 1 });
			cache.images.push_back(obj);
		}
	}

	DWRITE_TEXT_METRICS tm{};
	layout->GetMetrics(&tm);
	cache.height = tm.height;
	UINT32 lines = 0;
	layout->GetLineMetrics(nullptr, 0, &lines);
	if (lines > 0) {
		std::vector<DWRITE_LINE_METRICS> lm(lines);
		layout->GetLineMetrics(lm.data(), lines, &lines);
		cache.firstLine = lm[0].height;
		cache.firstBaseline = lm[0].baseline;
	}

	// the shades behind code spans, keys and <mark>
	if (content && !code) {
		for (auto& run : content->runs) {
			if (!(run.style & (MdvCode | MdvKbd | MdvMath | MdvMark)))
				continue;
			UINT32 count = 0;
			layout->HitTestTextRange((UINT32)run.start, (UINT32)run.length, 0, 0, nullptr, 0, &count);
			if (count == 0)
				continue;
			std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
			if (FAILED(layout->HitTestTextRange((UINT32)run.start, (UINT32)run.length, 0, 0, hits.data(), count, &count)))
				continue;
			const auto kind = run.style & MdvKbd ? MdvBackRect::Kbd : run.style & MdvMark ? MdvBackRect::Mark : MdvBackRect::Code;
			const float pad = kind == MdvBackRect::Mark ? 0.05f * em : 0.2f * em;
			for (auto& h : hits)
				cache.backs.push_back({ kind, D2D1::RectF(h.left - pad, h.top + 0.08f * h.height, h.left + h.width + pad, h.top + 0.92f * h.height) });
		}
	}

	cache.layout = layout;
	cache.width = width;
	box.height = cache.height;
	box.measured = true;
	s.layouts[MdvState::Key(box.block, box.kind)] = std::move(cache);
	return true;
}

// ---- Painting -----------------------------------------------------------------------------

LRESULT CMarkdownViewCtrl::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	PAINTSTRUCT ps;
	BeginPaint(&ps);
	Paint();
	EndPaint(&ps);
	return 0;
}

void CMarkdownViewCtrl::Paint() {
	auto& s = *m_State;
	if (!CreateTarget())
		return;
	auto& flow = s.flow;
	const MDVCOLORS c = GetColors();
	s.target->BeginDraw();
	s.target->SetTransform(D2D1::Matrix3x2F::Identity());
	s.target->Clear(Color(c.crBack));

	const float left = ColumnLeft(), view = ViewHeight(), top = s.scrollY;
	// the bars of quotes and alerts
	for (auto& span : flow.spans) {
		const float y0 = flow.boxes[span.first].top - top, y1 = flow.boxes[span.last].Bottom() - top;
		if (y1 < 0 || y0 > view)
			continue;
		auto& block = s.doc.blocks[span.block];
		const COLORREF color = block.kind == MdvKind::Alert ? c.crAlert[(int)block.alert - 1] : c.crBorder;
		const float x = left + span.indent, w = std::max(1.0f, 0.25f * s.metrics.fontSize * flow.boxes[span.first].scale);
		s.target->FillRectangle(D2D1::RectF(x, y0, x + w, y1), s.Brush(color));
	}
	for (size_t i = flow.BoxAt(top); i < flow.boxes.size(); i++) {
		auto& box = flow.boxes[i];
		if (box.top - top > view)
			break;
		PaintBox(i, left + box.indent, box.top - top);
		if (box.marker != MdvMarker::None)
			PaintMarker(i, left + box.indent, box.top - top);
	}
	if (s.target->EndDraw() == D2DERR_RECREATE_TARGET) {
		DiscardTarget();
		Invalidate();
	}
}

void CMarkdownViewCtrl::PaintBox(size_t index, float x, float y) {
	auto& s = *m_State;
	auto& box = s.flow.boxes[index];
	auto& block = s.doc.blocks[box.block];
	auto& m = s.metrics;
	auto* rt = s.target.Get();
	const MDVCOLORS c = GetColors();
	const float em = m.fontSize * box.scale;
	const float w = ColumnWidth() - box.indent;
	const float bottom = y + (box.Bottom() - box.top);

	switch (box.kind) {
		case MdvBoxKind::Rule:
			rt->FillRectangle(D2D1::RectF(x, y, x + w, y + box.height), s.Brush(c.crBorder));
			return;
		case MdvBoxKind::AlertTitle: {
			static const wchar_t* names[] = { L"Note", L"Tip", L"Important", L"Warning", L"Caution" };
			static const wchar_t glyphs[] = { 0xE946, 0xEA80, 0xE171, 0xE7BA, 0xE783 };
			const int a = std::clamp((int)block.alert - 1, 0, 4);
			auto brush = s.Brush(c.crAlert[a]);
			const float lh = m.lineHeight * em;
			if (auto icon = s.Format(2, em, DWRITE_FONT_WEIGHT_NORMAL, m.lineHeight)) {
				icon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				rt->DrawText(&glyphs[a], 1, icon, D2D1::RectF(x, y, x + 1.5f * em, y + lh), brush);
			}
			if (auto title = s.Format(0, em, DWRITE_FONT_WEIGHT_SEMI_BOLD, m.lineHeight)) {
				title->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
				rt->DrawText(names[a], (UINT32)wcslen(names[a]), title, D2D1::RectF(x + 1.5f * em, y, x + w, y + lh), brush);
			}
			return;
		}
		default:
			break;
	}

	auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
	if (it == s.layouts.end() || !it->second.layout) {
		if (!Measure(index))
			return;
		it = s.layouts.find(MdvState::Key(box.block, box.kind));
	}
	auto& cache = it->second;
	const MdvInline* content = box.kind == MdvBoxKind::Table ? nullptr : &block.content;
	const bool code = box.kind == MdvBoxKind::Code;
	if (!cache.effects) {
		// the links' color, and code's when it has its own
		if (content && !code)
			for (auto& run : content->runs) {
				const DWRITE_TEXT_RANGE range{ (UINT32)run.start, (UINT32)run.length };
				if (run.link >= 0)
					cache.layout->SetDrawingEffect(s.Brush(c.crLink), range);
				else if (run.style & (MdvCode | MdvKbd | MdvMath) && c.crCodeText != c.crText)
					cache.layout->SetDrawingEffect(s.Brush(c.crCodeText), range);
			}
		cache.effects = true;
	}

	const float tx = x + box.padLeft, ty = y + box.padTop;
	COLORREF textColor = block.kind == MdvKind::Heading ? c.crHeading : box.muted ? c.crMuted : c.crText;
	if (code) {
		const D2D1_RECT_F rc = D2D1::RectF(x, y, x + w, bottom);
		rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 6, 6), s.Brush(c.crCodeBack));
		rt->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_ALIASED);
		rt->DrawTextLayout(D2D1::Point2F(tx, ty), cache.layout.Get(), s.Brush(c.crCodeText), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
		rt->PopAxisAlignedClip();
		return;
	}
	if (box.kind == MdvBoxKind::Table)
		rt->DrawRectangle(D2D1::RectF(x + 0.5f, y + 0.5f, x + w - 0.5f, bottom - 0.5f), s.Brush(c.crTableBorder));

	for (auto& back : cache.backs) {
		const D2D1_RECT_F rc = D2D1::RectF(tx + back.rect.left, ty + back.rect.top, tx + back.rect.right, ty + back.rect.bottom);
		const float r = 0.2f * em;
		if (back.kind == MdvBackRect::Mark)
			rt->FillRectangle(rc, s.Brush(c.crMarkBack));
		else {
			rt->FillRoundedRectangle(D2D1::RoundedRect(rc, r, r), s.Brush(c.crCodeBack));
			if (back.kind == MdvBackRect::Kbd)
				rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f), r, r), s.Brush(c.crBorder));
		}
	}

	if (box.kind == MdvBoxKind::Summary) {
		// the triangle: pointing right while closed, down while open
		const float cy = ty + (cache.firstLine > 0 ? cache.firstLine : m.lineHeight * em) / 2;
		const float cx = x + 0.55f * em, r = 0.3f * em;
		ComPtr<ID2D1PathGeometry> path;
		ComPtr<ID2D1GeometrySink> sink;
		if (SUCCEEDED(MdvD2DFactory()->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
			if (block.open) {
				sink->BeginFigure(D2D1::Point2F(cx - r, cy - r * 0.5f), D2D1_FIGURE_BEGIN_FILLED);
				sink->AddLine(D2D1::Point2F(cx + r, cy - r * 0.5f));
				sink->AddLine(D2D1::Point2F(cx, cy + r * 0.6f));
			}
			else {
				sink->BeginFigure(D2D1::Point2F(cx - r * 0.5f, cy - r), D2D1_FIGURE_BEGIN_FILLED);
				sink->AddLine(D2D1::Point2F(cx + r * 0.6f, cy));
				sink->AddLine(D2D1::Point2F(cx - r * 0.5f, cy + r));
			}
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			sink->Close();
			rt->FillGeometry(path.Get(), s.Brush(textColor));
		}
	}

	g_paintTarget = rt;
	g_paintFrame = s.Brush(c.crBorder);
	rt->DrawTextLayout(D2D1::Point2F(tx, ty), cache.layout.Get(), s.Brush(textColor), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
	g_paintTarget = nullptr;
	g_paintFrame = nullptr;

	if (block.kind == MdvKind::Heading && block.level <= 2)
		rt->FillRectangle(D2D1::RectF(x, bottom - 1, x + w, bottom), s.Brush(c.crBorder));
}

void CMarkdownViewCtrl::PaintMarker(size_t index, float x, float y) {
	auto& s = *m_State;
	auto& box = s.flow.boxes[index];
	auto& m = s.metrics;
	auto* rt = s.target.Get();
	const MDVCOLORS c = GetColors();
	auto& item = s.doc.blocks[box.markerBlock];
	// the marker's size follows the item's text, not a heading in it
	float scale = box.scale;
	if (s.doc.blocks[box.block].kind == MdvKind::Heading)
		scale = item.kind == MdvKind::Footnote ? m.footnoteScale : 1;
	const float em = m.fontSize * scale;
	float line = m.lineHeight * em;
	auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
	if (it != s.layouts.end() && it->second.firstLine > 0)
		line = it->second.firstLine;
	const float ty = y + box.padTop;
	const float cy = ty + line / 2;
	const float cx = x - 1.0f * em;
	const COLORREF color = box.muted ? c.crMuted : c.crText;

	switch (box.marker) {
		case MdvMarker::Disc:
			rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 0.17f * em, 0.17f * em), s.Brush(color));
			break;
		case MdvMarker::Circle:
			rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 0.17f * em, 0.17f * em), s.Brush(color), std::max(1.0f, 0.08f * em));
			break;
		case MdvMarker::Square:
			rt->FillRectangle(D2D1::RectF(cx - 0.15f * em, cy - 0.15f * em, cx + 0.15f * em, cy + 0.15f * em), s.Brush(color));
			break;
		case MdvMarker::Number: {
			std::wstring text;
			if (item.kind == MdvKind::Footnote)
				text = std::to_wstring(item.footnote) + L".";
			else
				text = std::to_wstring(item.number) + s.doc.blocks[item.parent].mark;
			auto format = s.Format(0, em, DWRITE_FONT_WEIGHT_NORMAL, m.lineHeight);
			ComPtr<IDWriteTextLayout> layout;
			const float width = 1.75f * em;
			if (format && SUCCEEDED(MdvDWriteFactory()->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, width, line, layout.GetAddressOf()))) {
				layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
				// on the first line's baseline
				DWRITE_LINE_METRICS lm{};
				UINT32 count = 0;
				layout->GetLineMetrics(&lm, 1, &count);
				float baseline = it != s.layouts.end() && it->second.firstBaseline > 0 ? it->second.firstBaseline : lm.baseline;
				rt->DrawTextLayout(D2D1::Point2F(x - 0.35f * em - width, ty + baseline - lm.baseline), layout.Get(),
					s.Brush(item.kind == MdvKind::Footnote ? c.crMuted : color));
			}
			break;
		}
		case MdvMarker::Unchecked:
		case MdvMarker::Checked: {
			const float size = 0.9f * em, r = 0.18f * em;
			const D2D1_RECT_F rc = D2D1::RectF(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2);
			if (box.marker == MdvMarker::Checked) {
				rt->FillRoundedRectangle(D2D1::RoundedRect(rc, r, r), s.Brush(c.crLink));
				auto white = s.Brush(c.crBack);
				const float stroke = std::max(1.5f, 0.12f * em);
				rt->DrawLine(D2D1::Point2F(rc.left + 0.22f * size, rc.top + 0.52f * size), D2D1::Point2F(rc.left + 0.42f * size, rc.top + 0.72f * size), white, stroke);
				rt->DrawLine(D2D1::Point2F(rc.left + 0.42f * size, rc.top + 0.72f * size), D2D1::Point2F(rc.left + 0.78f * size, rc.top + 0.3f * size), white, stroke);
			}
			else
				rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f), r, r),
					s.Brush(c.crMuted), std::max(1.0f, 0.07f * em));
			break;
		}
		default:
			break;
	}
}
