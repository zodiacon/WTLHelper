// The markdown view's painting: Direct2D and DirectWrite resources, the boxes' text layouts (paragraphs, code
// blocks with their colors, tables' cells), and drawing them.

#include "MarkdownViewData.h"
#include "MarkdownViewSyntax.h"

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

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
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

// a table's cells' padding (GitHub's 6px 13px at 16px) and borders
constexpr float CellPadX = 0.8125f, CellPadY = 0.375f, Border = 1;
constexpr float TableBar = 10;          // room under a table that scrolls sideways, for its bar

}  // namespace

// ---- Resources -------------------------------------------------------------------------

ID2D1SolidColorBrush* MdvState::Brush(COLORREF color) {
	auto& brush = brushes[color];
	if (!brush && target)
		target->CreateSolidColorBrush(Color(color), brush.GetAddressOf());
	return brush.Get();
}

IDWriteTextFormat* MdvState::Format(int family, float size, DWRITE_FONT_WEIGHT weight, float lineHeight, int align) {
	// a format is shared by many layouts: everything about it is in its key, and it's never changed after
	const auto key = std::make_tuple(family, (int)std::lround(size * 64), (int)weight, (int)std::lround(lineHeight * 100) * 4 + align);
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
	if (align & MdvAlignMiddle)
		format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
	if (align & MdvAlignCenter)
		format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	return format.Get();
}

void MdvState::ResetEffects() {
	for (auto& [key, cache] : layouts) {
		cache.text.effects = false;
		for (auto& cell : cache.cells)
			cell.effects = false;
	}
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
	s.ResetEffects();
	return true;
}

void CMarkdownViewCtrl::DiscardTarget() {
	auto& s = *m_State;
	s.target.Reset();
	s.brushes.clear();
	s.ResetEffects();
	// the images' bitmaps belong to the target
	for (auto& [key, entry] : s.images) {
		entry->bitmap.Reset();
		entry->svgDocument.Reset();
	}
}

void CMarkdownViewCtrl::DiscardLayouts() {
	m_State->layouts.clear();
}

// ---- Text layouts -----------------------------------------------------------------------

namespace {

void ApplyRuns(MdvState& s, IDWriteTextLayout* layout, const MdvInline& content, float em) {
	for (auto& run : content.runs) {
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
			layout->SetFontSize(em * s.metrics.codeScale, range);
		}
		if (run.style & (MdvSub | MdvSup))
			layout->SetFontSize(em * 0.75f, range);
	}
}

void LineMetrics(MdvText& t) {
	UINT32 lines = 0;
	t.layout->GetLineMetrics(nullptr, 0, &lines);
	if (lines == 0)
		return;
	std::vector<DWRITE_LINE_METRICS> lm(lines);
	t.layout->GetLineMetrics(lm.data(), lines, &lines);
	t.firstLine = lm[0].height;
	t.firstBaseline = lm[0].baseline;
}

// the shades behind code spans, keys and <mark>
void MakeBacks(MdvText& t, const MdvInline& content, float em) {
	t.backs.clear();
	for (auto& run : content.runs) {
		if (!(run.style & (MdvCode | MdvKbd | MdvMath | MdvMark)))
			continue;
		UINT32 count = 0;
		t.layout->HitTestTextRange((UINT32)run.start, (UINT32)run.length, 0, 0, nullptr, 0, &count);
		if (count == 0)
			continue;
		std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
		if (FAILED(t.layout->HitTestTextRange((UINT32)run.start, (UINT32)run.length, 0, 0, hits.data(), count, &count)))
			continue;
		const auto kind = run.style & MdvKbd ? MdvBackRect::Kbd : run.style & MdvMark ? MdvBackRect::Mark : MdvBackRect::Code;
		const float pad = kind == MdvBackRect::Mark ? 0.05f * em : 0.2f * em;
		for (auto& h : hits)
			t.backs.push_back({ kind, D2D1::RectF(h.left - pad, h.top + 0.08f * h.height, h.left + h.width + pad, h.top + 0.92f * h.height) });
	}
}

// an image's size in the text: the HTML's width and height, else its own once it's loaded (a frame until then),
// never wider than the text
void ImageSize(MdvState& s, const MdvImage& img, const MdvImageEntry& e, float em, float width, float& w, float& h) {
	const float zoom = s.zoom / 100.0f;
	w = img.width * zoom;
	h = img.height * zoom;
	const bool ready = e.state == MdvImageEntry::Ready && e.width > 0 && e.height > 0;
	if (w <= 0 && h <= 0) {
		if (ready) {
			w = e.width * zoom;
			h = e.height * zoom;
		}
		else if (e.state == MdvImageEntry::Failed && !img.alt.empty()) {
			// as wide as its alternative text
			w = 1.25f * em;
			if (auto format = s.Format(0, em * 0.875f, DWRITE_FONT_WEIGHT_NORMAL, s.metrics.lineHeight)) {
				ComPtr<IDWriteTextLayout> layout;
				if (SUCCEEDED(MdvDWriteFactory()->CreateTextLayout(img.alt.c_str(), (UINT32)img.alt.size(), format, width, em * 4, layout.GetAddressOf()))) {
					DWRITE_TEXT_METRICS tm{};
					layout->GetMetrics(&tm);
					w = tm.widthIncludingTrailingWhitespace + 10;
				}
			}
			h = 1.4f * em;
		}
		else {
			w = 1.25f * em;
			h = em;
		}
	}
	else if (w <= 0)
		w = ready ? h * e.width / e.height : h;
	else if (h <= 0)
		h = ready ? w * e.height / e.width : w;
	if (w > width && w > 0) {
		h *= width / w;
		w = width;
	}
	w = std::max(w, 1.0f);
	h = std::max(h, 1.0f);
}

}  // namespace

// a paragraph's, a heading's, a summary's or a cell's text
static bool MakeText(MdvState& s, HWND hwnd, MdvText& t, const MdvInline& content, uint64_t user, float em,
	DWRITE_FONT_WEIGHT weight, float lineHeight, float width, MdvAlign align) {
	auto format = s.Format(0, em, weight, lineHeight);
	t = {};
	if (!format || FAILED(MdvDWriteFactory()->CreateTextLayout(content.text.c_str(), (UINT32)content.text.size(), format, width, 1e7f, t.layout.GetAddressOf())))
		return false;
	if (align == MdvAlign::Center)
		t.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	else if (align == MdvAlign::Right)
		t.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
	ApplyRuns(s, t.layout.Get(), content, em);
	bool tall = false;
	for (auto& img : content.images) {
		auto entry = MdvRequestImage(s, hwnd, img.src);
		entry->users.insert(user);
		float w, h;
		ImageSize(s, img, *entry, em, width, w, h);
		tall = tall || h > 2 * lineHeight * em;
		ComPtr<IDWriteInlineObject> obj;
		obj.Attach(MdvCreateImageObject(entry, w, h, img.alt));
		t.layout->SetInlineObject(obj.Get(), { (UINT32)img.pos, 1 });
		t.images.push_back(obj);
	}
	// proportional spacing would add a share of a tall image's height above it: the font's own spacing instead
	if (tall)
		t.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0, 0);
	return true;
}

static void ApplyEffects(MdvState& s, MdvText& t, const MdvInline* content, const MDVCOLORS& c) {
	if (t.effects || !t.layout)
		return;
	if (content)
		for (auto& run : content->runs) {
			const DWRITE_TEXT_RANGE range{ (UINT32)run.start, (UINT32)run.length };
			if (run.link >= 0)
				t.layout->SetDrawingEffect(s.Brush(c.crLink), range);
			else if (run.style & (MdvCode | MdvKbd | MdvMath) && c.crCodeText != c.crText)
				t.layout->SetDrawingEffect(s.Brush(c.crCodeText), range);
		}
	for (auto& run : t.syntax) {
		const COLORREF color = run.kind >= 0 ? c.crSyntax[run.kind] : run.color;
		t.layout->SetDrawingEffect(s.Brush(color), { run.start, run.length });
	}
	t.effects = true;
}

static void DrawTextAt(MdvState& s, MdvText& t, float x, float y, COLORREF color, const MDVCOLORS& c, float em) {
	auto* rt = s.target.Get();
	for (auto& back : t.backs) {
		const D2D1_RECT_F rc = D2D1::RectF(x + back.rect.left, y + back.rect.top, x + back.rect.right, y + back.rect.bottom);
		const float r = 0.2f * em;
		if (back.kind == MdvBackRect::Mark)
			rt->FillRectangle(rc, s.Brush(c.crMarkBack));
		else {
			rt->FillRoundedRectangle(D2D1::RoundedRect(rc, r, r), s.Brush(c.crCodeBack));
			if (back.kind == MdvBackRect::Kbd)
				rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f), r, r), s.Brush(c.crBorder));
		}
	}
	g_mdvPaint.target = rt;
	g_mdvPaint.frame = s.Brush(c.crBorder);
	g_mdvPaint.altText = s.Brush(c.crMuted);
	g_mdvPaint.altFormat = s.Format(0, em * 0.875f, DWRITE_FONT_WEIGHT_NORMAL, s.metrics.lineHeight, MdvAlignMiddle);
	rt->DrawTextLayout(D2D1::Point2F(x, y), t.layout.Get(), s.Brush(color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
	g_mdvPaint = {};
}

bool CMarkdownViewCtrl::Measure(size_t index) {
	auto& s = *m_State;
	auto& box = s.flow.boxes[index];
	if (box.kind == MdvBoxKind::Rule || box.kind == MdvBoxKind::AlertTitle) {
		box.measured = true;
		return false;
	}
	if (box.kind == MdvBoxKind::Table)
		return MeasureTable(index);
	auto& block = s.doc.blocks[box.block];
	auto& m = s.metrics;
	const float em = m.fontSize * box.scale;
	const float width = std::max(1.0f, box.TextWidth(ColumnWidth()));
	const uint64_t key = MdvState::Key(box.block, box.kind);
	MdvLayoutCache cache;
	cache.width = width;

	if (box.kind == MdvBoxKind::Code) {
		auto& content = block.content;
		auto format = s.Format(1, em, DWRITE_FONT_WEIGHT_NORMAL, m.codeLineHeight);
		auto& t = cache.text;
		if (!format || FAILED(MdvDWriteFactory()->CreateTextLayout(content.text.c_str(), (UINT32)content.text.size(), format, 100000.0f, 1e7f, t.layout.GetAddressOf()))) {
			box.measured = true;
			return false;
		}
		t.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		// its colors: the app's highlighter, else the built-in one
		int count = -1;
		if (s.highlighter) {
			std::vector<MDVSPAN> spans(256);
			count = s.highlighter(block.lang.c_str(), content.text.c_str(), (UINT)content.text.size(), spans.data(), (int)spans.size(), s.highlighterParam);
			if (count > (int)spans.size()) {
				spans.resize(count);
				count = s.highlighter(block.lang.c_str(), content.text.c_str(), (UINT)content.text.size(), spans.data(), (int)spans.size(), s.highlighterParam);
			}
			for (int i = 0; i < std::min(count, (int)spans.size()); i++) {
				auto& sp = spans[i];
				if (sp.start >= content.text.size())
					continue;
				const UINT len = std::min<UINT>(sp.length, (UINT)content.text.size() - sp.start);
				t.syntax.push_back({ sp.start, len, -1, sp.color });
				if (sp.flags & MDVSPF_BOLD)
					t.layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, { sp.start, len });
				if (sp.flags & MDVSPF_ITALIC)
					t.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, { sp.start, len });
			}
		}
		if (count < 0)
			for (auto& tok : MdvHighlight(block.lang, content.text)) {
				t.syntax.push_back({ (UINT)tok.start, (UINT)tok.length, (int)tok.kind, 0 });
				if (tok.kind == MdvTokenKind::Comment)
					t.layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, { (UINT32)tok.start, (UINT32)tok.length });
			}
		DWRITE_TEXT_METRICS tm{};
		t.layout->GetMetrics(&tm);
		LineMetrics(t);
		cache.height = tm.height;
		cache.contentWidth = tm.widthIncludingTrailingWhitespace + box.padLeft + box.padRight;
	}
	else {
		const bool heading = block.kind == MdvKind::Heading;
		if (!MakeText(s, m_hWnd, cache.text, block.content, key, em, heading ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
			heading ? m.headingLineHeight : m.lineHeight, width, block.align)) {
			box.measured = true;
			return false;
		}
		DWRITE_TEXT_METRICS tm{};
		cache.text.layout->GetMetrics(&tm);
		LineMetrics(cache.text);
		MakeBacks(cache.text, block.content, em);
		cache.height = tm.height;
	}
	box.height = cache.height;
	box.measured = true;
	s.layouts[key] = std::move(cache);
	return true;
}

bool CMarkdownViewCtrl::MeasureTable(size_t index) {
	auto& s = *m_State;
	auto& box = s.flow.boxes[index];
	auto& table = s.doc.blocks[box.block];
	auto& m = s.metrics;
	const float em = m.fontSize * box.scale;
	const float width = std::max(1.0f, box.TextWidth(ColumnWidth()));
	const uint64_t key = MdvState::Key(box.block, box.kind);
	const size_t columns = std::max<size_t>(table.columns, 1);
	const float padX = CellPadX * em, padY = CellPadY * em;

	MdvLayoutCache cache;
	cache.width = width;
	cache.cells.resize(table.children.size() * columns);
	std::vector<MdvColumnMeasure> measures(columns);
	for (size_t r = 0; r < table.children.size(); r++) {
		auto& row = s.doc.blocks[table.children[r]];
		for (size_t c = 0; c < columns && c < row.children.size(); c++) {
			auto& cell = s.doc.blocks[row.children[c]];
			auto& t = cache.cells[r * columns + c];
			if (!MakeText(s, m_hWnd, t, cell.content, key, em, cell.header ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
				m.lineHeight, 100000.0f, cell.align))
				continue;
			DWRITE_TEXT_METRICS tm{};
			t.layout->GetMetrics(&tm);
			float minWidth = 0;
			t.layout->DetermineMinWidth(&minWidth);
			measures[c].minWidth = std::max(measures[c].minWidth, std::ceil(minWidth));
			measures[c].maxWidth = std::max(measures[c].maxWidth, std::ceil(tm.widthIncludingTrailingWhitespace));
		}
	}
	cache.columns = MdvColumnWidths(measures, width - (columns + 1) * Border, 2 * padX, 25 * em);
	float y = Border;
	for (size_t r = 0; r < table.children.size(); r++) {
		float rowHeight = m.lineHeight * em;
		for (size_t c = 0; c < columns; c++) {
			auto& t = cache.cells[r * columns + c];
			if (!t.layout)
				continue;
			t.layout->SetMaxWidth(std::max(1.0f, cache.columns[c]));
			DWRITE_TEXT_METRICS tm{};
			t.layout->GetMetrics(&tm);
			LineMetrics(t);
			auto& row = s.doc.blocks[table.children[r]];
			MakeBacks(t, s.doc.blocks[row.children[c]].content, em);
			rowHeight = std::max(rowHeight, tm.height);
		}
		rowHeight += 2 * padY;
		cache.rowTops.push_back(y);
		cache.rowHeights.push_back(rowHeight);
		y += rowHeight + Border;
	}
	float tableWidth = Border;
	for (float w : cache.columns)
		tableWidth += w + 2 * padX + Border;
	cache.contentWidth = tableWidth;
	cache.height = y + (tableWidth > width + 0.5f ? TableBar : 0);
	box.height = cache.height;
	box.measured = true;
	s.layouts[key] = std::move(cache);
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
			if (auto icon = s.Format(2, em, DWRITE_FONT_WEIGHT_NORMAL, m.lineHeight, MdvAlignMiddle)) {
				rt->DrawText(&glyphs[a], 1, icon, D2D1::RectF(x, y, x + 1.5f * em, y + lh), brush);
			}
			if (auto title = s.Format(0, em, DWRITE_FONT_WEIGHT_SEMI_BOLD, m.lineHeight, MdvAlignMiddle)) {
				rt->DrawText(names[a], (UINT32)wcslen(names[a]), title, D2D1::RectF(x + 1.5f * em, y, x + w, y + lh), brush);
			}
			return;
		}
		default:
			break;
	}

	const uint64_t key = MdvState::Key(box.block, box.kind);
	auto it = s.layouts.find(key);
	if (it == s.layouts.end()) {
		if (!Measure(index))
			return;
		it = s.layouts.find(key);
	}
	auto& cache = it->second;
	const float sideways = SidewaysRoom(index) > 0 ? s.sideways[box.block] : 0;

	if (box.kind == MdvBoxKind::Table) {
		const size_t columns = std::max<size_t>(block.columns, 1);
		const float padX = CellPadX * em, padY = CellPadY * em;
		const float ox = x - sideways;
		const float tableBottom = y + (cache.rowTops.empty() ? 0 : cache.rowTops.back() + cache.rowHeights.back() + Border);
		rt->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
		size_t body = 0;
		for (size_t r = 0; r < cache.rowTops.size(); r++) {
			auto& row = s.doc.blocks[block.children[r]];
			const float ry = y + cache.rowTops[r], rh = cache.rowHeights[r];
			if (row.header)
				rt->FillRectangle(D2D1::RectF(ox, ry, ox + cache.contentWidth, ry + rh), s.Brush(c.crTableHeaderBack));
			else if (body++ % 2 == 1)
				rt->FillRectangle(D2D1::RectF(ox, ry, ox + cache.contentWidth, ry + rh), s.Brush(c.crTableStripeBack));
			float cx = ox + Border;
			for (size_t col = 0; col < columns && col < cache.columns.size(); col++) {
				auto& t = cache.cells[r * columns + col];
				if (t.layout && col < row.children.size()) {
					auto& content = s.doc.blocks[row.children[col]].content;
					ApplyEffects(s, t, &content, c);
					DrawTextAt(s, t, cx + padX, ry + padY, c.crText, c, em);
				}
				cx += cache.columns[col] + 2 * padX + Border;
			}
		}
		// the borders
		auto border = s.Brush(c.crTableBorder);
		rt->FillRectangle(D2D1::RectF(ox, y, ox + cache.contentWidth, y + Border), border);
		for (size_t r = 0; r < cache.rowTops.size(); r++) {
			const float ly = y + cache.rowTops[r] + cache.rowHeights[r];
			rt->FillRectangle(D2D1::RectF(ox, ly, ox + cache.contentWidth, ly + Border), border);
		}
		float vx = ox;
		rt->FillRectangle(D2D1::RectF(vx, y, vx + Border, tableBottom), border);
		for (float colWidth : cache.columns) {
			vx += colWidth + 2 * padX + Border;
			rt->FillRectangle(D2D1::RectF(vx - Border, y, vx, tableBottom), border);
		}
		rt->PopAxisAlignedClip();
	}
	else if (box.kind == MdvBoxKind::Code) {
		const D2D1_RECT_F rc = D2D1::RectF(x, y, x + w, bottom);
		rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 6, 6), s.Brush(c.crCodeBack));
		ApplyEffects(s, cache.text, nullptr, c);
		rt->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_ALIASED);
		DrawTextAt(s, cache.text, x + box.padLeft - sideways, y + box.padTop, c.crCodeText, c, em);
		rt->PopAxisAlignedClip();
		// the copy button, while the mouse is over the block
		float b[4];
		if (CopyButton(index, b)) {
			const D2D1_RECT_F brc = D2D1::RectF(b[0], b[1], b[2], b[3]);
			const float r = 0.25f * m.fontSize;
			rt->FillRoundedRectangle(D2D1::RoundedRect(brc, r, r), s.Brush(Blend(c.crCodeBack, c.crText, s.hotCopy ? 14 : 6)));
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(brc.left + 0.5f, brc.top + 0.5f, brc.right - 0.5f, brc.bottom - 0.5f), r, r), s.Brush(c.crBorder));
			const bool copied = s.copiedBlock == box.block;
			const wchar_t glyph = copied ? 0xE73E : 0xE8C8;
			if (auto icon = s.Format(2, m.fontSize, DWRITE_FONT_WEIGHT_NORMAL, 1.0f, MdvAlignMiddle | MdvAlignCenter)) {
				rt->DrawText(&glyph, 1, icon, brc, s.Brush(copied ? c.crAlert[MDVA_TIP] : c.crMuted));
			}
		}
	}
	else {
		const float tx = x + box.padLeft, ty = y + box.padTop;
		const COLORREF textColor = block.kind == MdvKind::Heading ? c.crHeading : box.muted ? c.crMuted : c.crText;
		if (box.kind == MdvBoxKind::Summary) {
			// the triangle: pointing right while closed, down while open
			const float cy = ty + (cache.text.firstLine > 0 ? cache.text.firstLine : m.lineHeight * em) / 2;
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
		ApplyEffects(s, cache.text, &block.content, c);
		DrawTextAt(s, cache.text, tx, ty, textColor, c, em);
		if (block.kind == MdvKind::Heading && block.level <= 2)
			rt->FillRectangle(D2D1::RectF(x, bottom - 1, x + w, bottom), s.Brush(c.crBorder));
	}

	// a code block's or a table's sideways bar
	float track[4], thumb[4];
	if (SidewaysBar(index, track, thumb)) {
		const float r = (thumb[3] - thumb[1]) / 2;
		rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(thumb[0], thumb[1], thumb[2], thumb[3]), r, r),
			s.Brush(Blend(c.crBack, c.crText, s.dragBox == (int)index ? 55 : 35)));
	}
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
	const MdvText* text = nullptr;
	auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
	if (it != s.layouts.end() && it->second.text.firstLine > 0) {
		text = &it->second.text;
		line = text->firstLine;
	}
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
			std::wstring number;
			if (item.kind == MdvKind::Footnote)
				number = std::to_wstring(item.footnote) + L".";
			else
				number = std::to_wstring(item.number) + s.doc.blocks[item.parent].mark;
			auto format = s.Format(0, em, DWRITE_FONT_WEIGHT_NORMAL, m.lineHeight);
			ComPtr<IDWriteTextLayout> layout;
			const float width = 1.75f * em;
			if (format && SUCCEEDED(MdvDWriteFactory()->CreateTextLayout(number.c_str(), (UINT32)number.size(), format, width, line, layout.GetAddressOf()))) {
				layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
				// on the first line's baseline
				DWRITE_LINE_METRICS lm{};
				UINT32 count = 0;
				layout->GetLineMetrics(&lm, 1, &count);
				const float baseline = text && text->firstBaseline > 0 ? text->firstBaseline : lm.baseline;
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
				auto mark = s.Brush(c.crBack);
				const float stroke = std::max(1.5f, 0.12f * em);
				rt->DrawLine(D2D1::Point2F(rc.left + 0.22f * size, rc.top + 0.52f * size), D2D1::Point2F(rc.left + 0.42f * size, rc.top + 0.72f * size), mark, stroke);
				rt->DrawLine(D2D1::Point2F(rc.left + 0.42f * size, rc.top + 0.72f * size), D2D1::Point2F(rc.left + 0.78f * size, rc.top + 0.3f * size), mark, stroke);
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
