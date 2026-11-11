#include "DiffView.h"
#include "DiffViewModel.h"

#include <algorithm>
#include <climits>

// Syntax highlighting: the palette, the spans the app gave for lines (asked at paint time, kept per line) and the
// fonts of the styles. The pure parts, normalizing spans and cutting a line into segments by columns, are in the
// model (DfNormalizeStyleSpans, DfStyleSegments); the painting is in PaintText.

void CDiffViewCtrl::SetStylePalette(const DFVSTYLE* styles, size_t count) {
	if (!styles || count == 0) {
		m_Styles.reset();
	}
	else {
		// the spans asked for stay: they hold indexes, which a new palette (a theme change) only colors again
		if (!m_Styles)
			m_Styles = std::make_unique<DfStyleStore>();
		m_Styles->palette.clear();
		m_Styles->palette.reserve(count);
		for (size_t i = 0; i < count; i++)
			m_Styles->palette.push_back({ styles[i].crText, styles[i].crBack, styles[i].flags });
	}
	if (IsWindow())
		Invalidate();
}

size_t CDiffViewCtrl::GetStylePaletteCount() const {
	return m_Styles ? m_Styles->palette.size() : 0;
}

void CDiffViewCtrl::ForgetStyles() {
	if (m_Styles) {
		for (auto& side : m_Styles->cache)
			side.clear();
	}
}

void CDiffViewCtrl::InvalidateStyles() {
	ForgetStyles();
	if (IsWindow())
		Invalidate();
}

bool CDiffViewCtrl::GetLineSegments(bool rightSide, size_t line, std::wstring_view text, std::vector<DfStyleSegment>& out) {
	if (!m_Styles || m_PaintHighContrast)
		return false;
	auto find = [&] { return m_Styles->cache[rightSide ? 1 : 0].find(line); };
	auto it = find();
	if (it == m_Styles->cache[rightSide ? 1 : 0].end()) {
		NMDFVLINESTYLE nm{};
		nm.right = rightSide;
		nm.line = line;
		nm.text = text.data();
		nm.length = text.size();
		FireNotify(&nm.hdr, DFVN_GETLINESTYLE);

		std::vector<DfStyleSpan> spans;
		if (nm.spans && nm.spanCount) {
			spans.reserve(nm.spanCount);
			for (size_t i = 0; i < nm.spanCount; i++) {
				auto& s = nm.spans[i];
				if (s.start >= text.size() || s.length == 0)
					continue;   // also keeps a start past 32 bits out
				spans.push_back({ (uint32_t)s.start, (uint32_t)std::min<size_t>(s.length, UINT32_MAX), s.style });
			}
			DfNormalizeStyleSpans(text.size(), spans);
		}
		if (!m_Styles)
			return false;   // the app turned it off while answering
		auto& cache = m_Styles->cache[rightSide ? 1 : 0];
		if (cache.size() >= DfStyleStore::MaxCached)
			cache.clear();
		it = cache.emplace(line, std::move(spans)).first;
	}
	if (it->second.empty())
		return false;   // plain: the same as no styles
	out = DfStyleSegments(text, it->second, m_TabSize);
	return true;
}

HFONT CDiffViewCtrl::StyleFont(UINT flags) {
	flags &= DFVSF_BOLD | DFVSF_ITALIC | DFVSF_UNDERLINE;
	if (flags == 0 || !m_hFont)
		return m_hFont;
	auto& font = m_StyleFonts[flags];
	if (font.IsNull()) {
		LOGFONTW lf{};
		if (::GetObjectW(m_hFont, sizeof(lf), &lf) == 0)
			return m_hFont;
		if (flags & DFVSF_BOLD)
			lf.lfWeight = std::max<LONG>(lf.lfWeight, FW_BOLD);
		if (flags & DFVSF_ITALIC)
			lf.lfItalic = TRUE;
		if (flags & DFVSF_UNDERLINE)
			lf.lfUnderline = TRUE;
		font.CreateFontIndirect(&lf);
		if (font.IsNull())
			return m_hFont;
	}
	return font;
}
