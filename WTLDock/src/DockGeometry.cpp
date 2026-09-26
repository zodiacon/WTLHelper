#include "DockGeometry.h"
#include <algorithm>

namespace WTLDock {

DockTheme DockTheme::Light() {
	DockTheme t{};
	t.IsDark = false;
	t.Workspace = RGB(238, 238, 242);
	t.Splitter = RGB(238, 238, 242);
	t.SplitterDragging = RGB(0, 122, 204);
	t.GroupBack = RGB(255, 255, 255);
	t.Border = RGB(204, 206, 219);

	t.CaptionActiveBack = RGB(0, 122, 204);
	t.CaptionActiveText = RGB(255, 255, 255);
	t.CaptionInactiveBack = RGB(204, 206, 219);
	t.CaptionInactiveText = RGB(30, 30, 30);
	t.ButtonHotBack = RGB(28, 151, 234);
	t.ButtonGlyph = RGB(30, 30, 30);
	t.ButtonGlyphHot = RGB(255, 255, 255);

	t.TabStripBack = RGB(238, 238, 242);
	t.TabActiveBack = RGB(255, 255, 255);
	t.TabActiveText = RGB(0, 122, 204);
	t.TabActiveAccent = RGB(0, 122, 204);
	t.TabInactiveBack = RGB(238, 238, 242);
	t.TabInactiveText = RGB(30, 30, 30);
	t.TabHotBack = RGB(201, 222, 245);

	t.GuideBack = RGB(246, 246, 250);
	t.GuideBorder = RGB(150, 155, 175);
	t.GuideGlyph = RGB(120, 125, 145);
	t.GuideHot = RGB(0, 122, 204);
	t.PreviewFill = RGB(0, 122, 204);
	t.PreviewBorder = RGB(0, 90, 160);

	t.BarBack = RGB(238, 238, 242);
	t.BarItemBack = RGB(255, 255, 255);
	t.BarItemText = RGB(30, 30, 30);
	return t;
}

DockTheme DockTheme::Dark() {
	DockTheme t{};
	t.IsDark = true;
	t.Workspace = RGB(37, 37, 38);
	t.Splitter = RGB(37, 37, 38);
	t.SplitterDragging = RGB(0, 122, 204);
	t.GroupBack = RGB(30, 30, 30);
	t.Border = RGB(63, 63, 70);

	t.CaptionActiveBack = RGB(0, 122, 204);
	t.CaptionActiveText = RGB(255, 255, 255);
	t.CaptionInactiveBack = RGB(63, 63, 70);
	t.CaptionInactiveText = RGB(241, 241, 241);
	t.ButtonHotBack = RGB(28, 151, 234);
	t.ButtonGlyph = RGB(241, 241, 241);
	t.ButtonGlyphHot = RGB(255, 255, 255);

	t.TabStripBack = RGB(45, 45, 48);
	t.TabActiveBack = RGB(30, 30, 30);
	t.TabActiveText = RGB(255, 255, 255);
	t.TabActiveAccent = RGB(0, 122, 204);
	t.TabInactiveBack = RGB(45, 45, 48);
	t.TabInactiveText = RGB(153, 153, 153);
	t.TabHotBack = RGB(62, 62, 64);

	t.GuideBack = RGB(45, 45, 48);
	t.GuideBorder = RGB(100, 100, 110);
	t.GuideGlyph = RGB(170, 170, 180);
	t.GuideHot = RGB(0, 122, 204);
	t.PreviewFill = RGB(0, 122, 204);
	t.PreviewBorder = RGB(28, 151, 234);

	t.BarBack = RGB(45, 45, 48);
	t.BarItemBack = RGB(63, 63, 70);
	t.BarItemText = RGB(241, 241, 241);
	return t;
}

DockMetrics DockMetrics::ForDpi(int dpi) {
	auto s = [&](int v) { return ::MulDiv(v, dpi, 96); };
	DockMetrics m{};
	m.CaptionHeight = s(22);
	m.TabHeight = s(24);
	m.TabPadding = s(10);
	m.TabGap = s(1);
	m.TabIconGap = s(4);
	m.TabCloseSize = s(16);
	m.IconSize = s(16);
	m.ButtonSize = s(16);
	m.ButtonMargin = s(4);
	m.TextPadding = s(6);
	m.SplitterThickness = s(5);
	m.AutoHideBarThickness = s(24);
	m.MinGroupSize = { s(80), s(60) };
	return m;
}

LayoutMetrics DockMetrics::ToLayoutMetrics() const {
	LayoutMetrics m;
	m.SplitterThickness = SplitterThickness;
	m.AutoHideBarThickness = AutoHideBarThickness;
	m.MinGroupSize = MinGroupSize;
	return m;
}

namespace {

// A floating window with a single group already has a title bar; the group needs no caption of its own.
bool IsSoleGroupOfFloat(const DockGroup& group) {
	if (group.Location() != GroupLocation::Float || !group.Float())
		return false;
	auto& children = group.Float()->Root().Children();
	return children.size() == 1 && children[0]->IsGroup();
}

}

GroupParts ComputeGroupParts(const DockGroup& group, const RECT& client, const DockMetrics& m) {
	GroupParts parts;
	const RECT rc = client;
	parts.Caption = parts.Tabs = { rc.left, rc.top, rc.right, rc.top };

	if (group.IsDocument()) {
		parts.HasTabs = true;
		parts.Tabs.bottom = std::min(rc.top + m.TabHeight, rc.bottom);
		parts.Content = { rc.left, parts.Tabs.bottom, rc.right, rc.bottom };
		return parts;
	}

	parts.HasCaption = !IsSoleGroupOfFloat(group);
	if (parts.HasCaption)
		parts.Caption.bottom = std::min(rc.top + m.CaptionHeight, rc.bottom);
	int top = parts.Caption.bottom, bottom = rc.bottom;
	parts.HasTabs = group.Panes().size() > 1;
	if (parts.HasTabs) {
		if (group.TabsAtBottom) {
			parts.Tabs = { rc.left, std::max(bottom - m.TabHeight, top), rc.right, bottom };
			bottom = parts.Tabs.top;
		}
		else {
			parts.Tabs = { rc.left, top, rc.right, std::min(top + m.TabHeight, bottom) };
			top = parts.Tabs.bottom;
		}
	}
	parts.Content = { rc.left, top, rc.right, bottom };
	return parts;
}

RECT CloseButtonRect(const RECT& caption, const DockMetrics& m) {
	const int size = std::min(m.ButtonSize, Height(caption));
	const int top = caption.top + (Height(caption) - size) / 2;
	return { caption.right - m.ButtonMargin - size, top, caption.right - m.ButtonMargin, top + size };
}

//
// tab strip
//

int TabWidth(const TabSpec& tab, const DockMetrics& m) {
	return m.TabPadding + (tab.HasIcon ? m.IconSize + m.TabIconGap : 0) + tab.TextWidth +
		(tab.Closable ? m.TabIconGap + m.TabCloseSize + m.TabPadding / 2 : m.TabPadding);
}

RECT TabIconRect(const RECT& tab, const DockMetrics& m) {
	const int top = tab.top + (Height(tab) - m.IconSize) / 2;
	return { tab.left + m.TabPadding, top, tab.left + m.TabPadding + m.IconSize, top + m.IconSize };
}

RECT TabTextRect(const RECT& tab, const TabSpec& spec, const DockMetrics& m) {
	RECT r = tab;
	r.left += m.TabPadding + (spec.HasIcon ? m.IconSize + m.TabIconGap : 0);
	r.right -= spec.Closable ? m.TabIconGap + m.TabCloseSize + m.TabPadding / 2 : m.TabPadding;
	if (r.right < r.left)
		r.right = r.left;
	return r;
}

TabStrip LayoutTabStrip(const std::vector<TabSpec>& tabs, const RECT& strip, const DockMetrics& m, int first, int active) {
	TabStrip result;
	const int n = (int)tabs.size();
	if (n == 0)
		return result;

	std::vector<int> widths;
	int total = 0;
	for (int i = 0; i < n; i++) {
		widths.push_back(TabWidth(tabs[i], m));
		total += widths.back() + (i ? m.TabGap : 0);
	}

	int avail = Width(strip);
	int firstIndex = 0, count = n;
	if (total > avail) {
		result.Overflow = true;
		const int buttonWidth = m.ButtonSize + 2 * m.ButtonMargin;
		avail = std::max(0, avail - buttonWidth);
		result.OverflowButton = { strip.right - buttonWidth, strip.top, strip.right, strip.bottom };

		// how many whole tabs fit when starting at f (at least one, even if it has to be cut off)
		auto fits = [&](int f) {
			int used = 0, c = 0;
			for (int i = f; i < n; i++) {
				const int need = widths[i] + (c ? m.TabGap : 0);
				if (used + need > avail)
					break;
				used += need;
				c++;
			}
			return std::max(c, 1);
		};

		firstIndex = std::clamp(first, 0, n - 1);
		if (active >= 0 && active < n) {
			if (active < firstIndex)
				firstIndex = active;
			while (firstIndex < active && firstIndex + fits(firstIndex) - 1 < active)
				firstIndex++;
		}
		while (firstIndex > 0 && firstIndex - 1 + fits(firstIndex - 1) - 1 >= n - 1)
			firstIndex--;
		count = std::min(fits(firstIndex), n - firstIndex);
	}

	result.First = firstIndex;
	const int limit = result.Overflow ? strip.right - (m.ButtonSize + 2 * m.ButtonMargin) : strip.right;
	int x = strip.left;
	for (int i = firstIndex; i < firstIndex + count; i++) {
		RECT r{ x, strip.top, std::min(x + widths[i], (int)limit), strip.bottom };
		result.Tabs.push_back(r);
		RECT close{};
		if (tabs[i].Closable) {
			const int top = r.top + (Height(r) - m.TabCloseSize) / 2;
			close = { x + widths[i] - m.TabPadding / 2 - m.TabCloseSize, top, x + widths[i] - m.TabPadding / 2, top + m.TabCloseSize };
			if (close.right > r.right)
				close = {};		// cut off
		}
		result.Close.push_back(close);
		x += widths[i] + m.TabGap;
	}
	return result;
}

}
