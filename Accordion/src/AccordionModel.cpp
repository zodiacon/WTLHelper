#include "AccordionModel.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

// ---- Sections --------------------------------------------------------------------------

std::vector<size_t> AcdSetExpanded(std::vector<AcdSection>& sections, size_t index, bool expand, bool singleExpand) {
	std::vector<size_t> changed;
	if (index >= sections.size())
		return changed;
	auto& section = sections[index];
	if (!expand && !section.collapsible)
		return changed;
	if (section.expanded != expand) {
		section.expanded = expand;
		changed.push_back(index);
	}
	if (expand && singleExpand) {
		for (size_t i = 0; i < sections.size(); i++) {
			if (i != index && sections[i].collapsible && sections[i].expanded) {
				sections[i].expanded = false;
				changed.push_back(i);
			}
		}
	}
	return changed;
}

void AcdSnap(std::vector<AcdSection>& sections) {
	for (auto& s : sections)
		s.progress = s.expanded ? 1 : 0;
}

bool AcdAnimate(std::vector<AcdSection>& sections, double elapsed, double duration) {
	double step = duration > 0 ? elapsed / duration : 1;
	bool moving = false;
	for (auto& s : sections) {
		double target = s.expanded ? 1 : 0;
		if (s.progress < target)
			s.progress = std::min(target, s.progress + step);
		else if (s.progress > target)
			s.progress = std::max(target, s.progress - step);
		moving |= s.progress != target;
	}
	return moving;
}

double AcdEase(double progress) {
	double p = std::clamp(progress, 0.0, 1.0);
	return p * p * (3 - 2 * p);
}

// ---- Layout ------------------------------------------------------------------------------

int AcdScale(int dips, UINT dpi) {
	return ::MulDiv(dips, dpi ? dpi : 96, 96);
}

int AcdUnscale(int pixels, UINT dpi) {
	return ::MulDiv(pixels, 96, dpi ? dpi : 96);
}

AcdLayout AcdLayoutSections(const std::vector<AcdSection>& sections, int width, int clientHeight, int scrollPos, const AcdMetrics& metrics) {
	AcdLayout layout;
	size_t n = sections.size();
	layout.items.resize(n);
	if (n == 0)
		return layout;

	// the bodies' heights: fixed and fit first, then the fill sections share what's left
	std::vector<int> body(n, 0);
	int used = (int)n * metrics.headerHeight + (int)(n - 1) * metrics.gap;
	double fillTotal = 0, fillLargest = 0;
	for (size_t i = 0; i < n; i++) {
		const auto& s = sections[i];
		double eased = AcdEase(s.progress);
		int minimum = AcdScale(s.minHeight, metrics.dpi);
		if (s.mode == AcdHeight::Fill) {
			// a closing section's weight goes as it does: the others grow into its space
			if (eased > 0) {
				fillTotal += s.weight * eased;
				fillLargest = std::max<double>(fillLargest, s.weight);
			}
			continue;
		}
		int full = std::max(s.mode == AcdHeight::Fixed ? AcdScale(s.height, metrics.dpi) : s.fitHeight, minimum);
		layout.items[i].fullHeight = full;
		body[i] = (int)std::lround(full * eased);
		used += body[i];
	}
	// Shared by weight, but over at least the largest weight: a lone fill section closing shrinks (its share of
	// the whole would stay all of it).
	double divisor = std::max(fillTotal, fillLargest);
	if (divisor > 0) {
		int leftover = std::max(0, clientHeight - used);
		double before = 0;   // the shares so far, for rounding without losing a pixel
		for (size_t i = 0; i < n; i++) {
			const auto& s = sections[i];
			if (s.mode != AcdHeight::Fill)
				continue;
			double eased = AcdEase(s.progress);
			double share = eased > 0 ? leftover * s.weight * eased / divisor : 0;
			int top = (int)std::lround(before), bottom = (int)std::lround(before + share);
			before += share;
			int minimum = AcdScale(s.minHeight, metrics.dpi);
			body[i] = std::max(bottom - top, (int)std::lround(minimum * eased));
			layout.items[i].fullHeight = std::max(body[i], minimum);
		}
	}

	int y = -scrollPos;
	for (size_t i = 0; i < n; i++) {
		auto& item = layout.items[i];
		item.header = { 0, y, width, y + metrics.headerHeight };
		y += metrics.headerHeight;
		item.body = { 0, y, width, y + body[i] };
		y += body[i];
		if (i + 1 < n)
			y += metrics.gap;
	}
	layout.contentHeight = y + scrollPos;
	layout.scrollMax = std::max(0, layout.contentHeight - clientHeight);
	return layout;
}

int AcdClampScroll(int scrollPos, const AcdLayout& layout) {
	return std::clamp(scrollPos, 0, layout.scrollMax);
}

// ---- Hit testing ---------------------------------------------------------------------------

AcdHit AcdHitTest(const AcdLayout& layout, const std::vector<AcdSection>& sections, POINT pt, int edge) {
	size_t n = std::min(layout.items.size(), sections.size());
	// an edge first: it reaches into the next header
	for (size_t i = 0; i < n; i++) {
		const auto& body = layout.items[i].body;
		if (body.bottom > body.top && pt.x >= body.left && pt.x < body.right && std::abs(pt.y - body.bottom) <= edge &&
			AcdCanResize(sections, i))
			return { AcdPart::ResizeEdge, i };
	}
	for (size_t i = 0; i < n; i++) {
		if (::PtInRect(&layout.items[i].header, pt))
			return { AcdPart::Header, i };
		if (::PtInRect(&layout.items[i].body, pt))
			return { AcdPart::Body, i };
	}
	return {};
}

std::vector<RECT> AcdButtonRects(const RECT& header, int count, int size, int gap, int right, int minLeft) {
	std::vector<RECT> rects(std::max(0, count), RECT{});
	int top = (header.top + header.bottom - size) / 2;
	int x = right;
	for (int i = count - 1; i >= 0; i--) {
		if (x - size < minLeft)
			break;
		rects[i] = { x - size, top, x, top + size };
		x -= size + gap;
	}
	return rects;
}

// ---- Resizing ------------------------------------------------------------------------------

constexpr size_t k_None = (size_t)-1;

// the next open section after index, a fill one or a fixed or fit one; k_None if none
static size_t NextOpen(const std::vector<AcdSection>& sections, size_t index, bool fill) {
	for (size_t j = index + 1; j < sections.size(); j++) {
		const auto& s = sections[j];
		if (s.expanded && s.progress >= 1 && (s.mode == AcdHeight::Fill) == fill)
			return j;
	}
	return k_None;
}

bool AcdCanResize(const std::vector<AcdSection>& sections, size_t index) {
	if (index >= sections.size())
		return false;
	const auto& s = sections[index];
	if (!s.expanded || s.progress < 1)
		return false;
	return s.mode != AcdHeight::Fill || NextOpen(sections, index, true) != k_None || NextOpen(sections, index, false) != k_None;
}

void AcdResize(std::vector<AcdSection>& sections, size_t index, int height, const AcdLayout& layout, const AcdMetrics& metrics) {
	if (!AcdCanResize(sections, index) || index >= layout.items.size())
		return;
	auto& s = sections[index];
	int minimum = AcdScale(s.minHeight, metrics.dpi);
	auto heightOf = [&](size_t k) { return k < layout.items.size() ? layout.items[k].body.bottom - layout.items[k].body.top : 0; };
	size_t fill = NextOpen(sections, index, true);
	size_t fixed = NextOpen(sections, index, false);

	// a fill section below takes the difference: this one just changes
	if (s.mode != AcdHeight::Fill && (fill != k_None || fixed == k_None)) {
		s.mode = AcdHeight::Fixed;
		s.height = AcdUnscale(std::max(height, minimum), metrics.dpi);
		return;
	}

	// a trade between this one and the one below
	size_t j = s.mode == AcdHeight::Fill && fill != k_None ? fill : fixed;
	auto& other = sections[j];
	int a = heightOf(index), b = heightOf(j), total = a + b;
	int otherMinimum = AcdScale(other.minHeight, metrics.dpi);
	if (total <= 0 || total - otherMinimum < minimum)
		return;
	height = std::clamp(height, minimum, total - otherMinimum);
	if (s.mode == AcdHeight::Fill && other.mode == AcdHeight::Fill) {
		// their weights become their heights, in the units of their weights so far
		double perPixel = (s.weight + other.weight) / total;
		s.weight = (float)(height * perPixel);
		other.weight = (float)((total - height) * perPixel);
		return;
	}
	// the one below becomes fixed at what's left; a fill section here gets the room it leaves (or gives)
	other.mode = AcdHeight::Fixed;
	other.height = AcdUnscale(total - height, metrics.dpi);
	if (s.mode != AcdHeight::Fill) {
		s.mode = AcdHeight::Fixed;
		s.height = AcdUnscale(height, metrics.dpi);
	}
}

// ---- Reordering ----------------------------------------------------------------------------

size_t AcdDropIndex(const AcdLayout& layout, int y) {
	for (size_t i = 0; i < layout.items.size(); i++) {
		const auto& header = layout.items[i].header;
		if (y < (header.top + header.bottom) / 2)
			return i;
	}
	return layout.items.size();
}

size_t AcdMove(std::vector<AcdSection>& sections, size_t from, size_t before) {
	if (from >= sections.size())
		return from;
	before = std::min(before, sections.size());
	if (before == from || before == from + 1)
		return from;
	auto section = sections[from];
	sections.erase(sections.begin() + from);
	if (before > from)
		before--;
	sections.insert(sections.begin() + before, section);
	return before;
}

// ---- Saved state ---------------------------------------------------------------------------

std::wstring AcdSaveState(const std::vector<AcdSection>& sections) {
	std::wstring text = L"1";
	wchar_t entry[64];
	for (const auto& s : sections) {
		double size = s.mode == AcdHeight::Fixed ? s.height : s.mode == AcdHeight::Fill ? s.weight : 0;
		swprintf_s(entry, L";%u,%d,%g", s.id, s.expanded ? 1 : 0, size);
		text += entry;
	}
	return text;
}

bool AcdLoadState(std::vector<AcdSection>& sections, std::wstring_view text) {
	struct Entry {
		UINT   id;
		bool   expanded;
		double size;
	};
	std::vector<Entry> entries;
	size_t start = 0;
	bool first = true;
	while (start <= text.size()) {
		size_t end = text.find(L';', start);
		if (end == std::wstring_view::npos)
			end = text.size();
		std::wstring part(text.substr(start, end - start));
		start = end + 1;
		if (first) {
			if (part != L"1")
				return false;
			first = false;
			continue;
		}
		if (part.empty())
			continue;
		unsigned id = 0;
		int expanded = 0;
		double size = 0;
		wchar_t extra = 0;
		if (swscanf_s(part.c_str(), L"%u,%d,%lf%c", &id, &expanded, &size, &extra, 1) != 3 || (expanded != 0 && expanded != 1) ||
			!std::isfinite(size) || size < 0)
			return false;
		entries.push_back({ id, expanded == 1, size });
	}
	if (first)
		return false;

	std::vector<AcdSection> ordered;
	ordered.reserve(sections.size());
	std::vector<bool> placed(sections.size());
	for (const auto& e : entries) {
		auto it = std::find_if(sections.begin(), sections.end(), [&](const AcdSection& s) { return s.id == e.id; });
		size_t i = it - sections.begin();
		if (it == sections.end() || placed[i])
			continue;
		placed[i] = true;
		auto s = *it;
		s.expanded = e.expanded || !s.collapsible;
		s.progress = s.expanded ? 1 : 0;
		if (s.mode == AcdHeight::Fixed && e.size >= 1)
			s.height = (int)std::lround(e.size);
		else if (s.mode == AcdHeight::Fill && e.size > 0)
			s.weight = (float)e.size;
		ordered.push_back(s);
	}
	for (size_t i = 0; i < sections.size(); i++) {
		if (!placed[i])
			ordered.push_back(sections[i]);
	}
	sections = std::move(ordered);
	return true;
}
