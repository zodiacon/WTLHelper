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

CaptionButtons ComputeCaptionButtons(const RECT& caption, bool close, bool pin, bool menu, const DockMetrics& m) {
	CaptionButtons b;
	const int size = std::min(m.ButtonSize, Height(caption));
	const int top = caption.top + (Height(caption) - size) / 2;
	const int gap = m.TabGap * 2;
	int right = caption.right - m.ButtonMargin;

	auto place = [&](RECT& slot, bool& has) {
		slot = { right - size, top, right, top + size };
		has = true;
		right -= size + gap;
	};
	if (close)
		place(b.Close, b.HasClose);
	if (pin)
		place(b.Pin, b.HasPin);
	if (menu)
		place(b.Menu, b.HasMenu);
	b.TextRight = (close || pin || menu) ? right + gap - m.ButtonMargin : caption.right - m.TextPadding;
	return b;
}

RECT CloseButtonRect(const RECT& caption, const DockMetrics& m) {
	const int size = std::min(m.ButtonSize, Height(caption));
	const int top = caption.top + (Height(caption) - size) / 2;
	return { caption.right - m.ButtonMargin - size, top, caption.right - m.ButtonMargin, top + size };
}

//
// tab strip
//

int MarkSize(const DockMetrics& m) {
	return std::max(4, m.TabCloseSize / 2);
}

// what the tab needs to the right of its text
static int TabTail(const TabSpec& tab, const DockMetrics& m) {
	if (tab.Closable)
		return m.TabIconGap + m.TabCloseSize + m.TabPadding / 2;
	if (tab.Marked)
		return m.TabIconGap + MarkSize(m) + m.TabPadding;
	return m.TabPadding;
}

int TabWidth(const TabSpec& tab, const DockMetrics& m) {
	return m.TabPadding + (tab.HasIcon ? m.IconSize + m.TabIconGap : 0) + tab.TextWidth + TabTail(tab, m);
}

RECT TabIconRect(const RECT& tab, const DockMetrics& m) {
	const int top = tab.top + (Height(tab) - m.IconSize) / 2;
	return { tab.left + m.TabPadding, top, tab.left + m.TabPadding + m.IconSize, top + m.IconSize };
}

RECT TabTextRect(const RECT& tab, const TabSpec& spec, const DockMetrics& m) {
	RECT r = tab;
	r.left += m.TabPadding + (spec.HasIcon ? m.IconSize + m.TabIconGap : 0);
	r.right -= TabTail(spec, m);
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
		RECT mark{};
		if (tabs[i].Marked && !tabs[i].Closable) {
			const int size = MarkSize(m);
			const int top = r.top + (Height(r) - size) / 2;
			mark = { x + widths[i] - m.TabPadding - size, top, x + widths[i] - m.TabPadding, top + size };
			if (mark.right > r.right)
				mark = {};		// cut off
		}
		result.Mark.push_back(mark);
		x += widths[i] + m.TabGap;
	}
	return result;
}

bool KeepRectOnScreen(RECT& rect, int reachable) {
	const RECT strip{ rect.left, rect.top, rect.right, rect.top + reachable };
	if (::MonitorFromRect(&strip, MONITOR_DEFAULTTONULL))
		return false;

	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	const int w = std::min(Width(rect), Width(work)), h = std::min(Height(rect), Height(work));
	rect = { work.left + 40, work.top + 40, work.left + 40 + w, work.top + 40 + h };
	if (rect.right > work.right)
		OffsetRect(&rect, work.right - rect.right, 0);
	if (rect.bottom > work.bottom)
		OffsetRect(&rect, 0, work.bottom - rect.bottom);
	return true;
}

NavigatorLayout ComputeNavigatorLayout(const int counts[2], const int first[2], int selectedColumn, int selectedRow, const DockMetrics& metrics) {
	NavigatorLayout layout;
	const int columnWidth = ::MulDiv(240, metrics.IconSize, 16);		// the icon size follows the DPI
	const int rowHeight = metrics.TabHeight + metrics.TabGap * 2;
	const int headerHeight = metrics.CaptionHeight;
	const int margin = metrics.TextPadding * 2;
	const int rows = std::max(1, std::min(NavigatorMaxRows, std::max(counts[0], counts[1])));

	const int top = margin;
	const int bodyTop = top + headerHeight;
	for (int c = 0; c < 2; c++) {
		const int left = margin + c * (columnWidth + margin);
		layout.Header[c] = { left, top, left + columnWidth, top + headerHeight };
		layout.Column[c] = { left, bodyTop, left + columnWidth, bodyTop + rows * rowHeight };

		int f = std::clamp(first[c], 0, std::max(0, counts[c] - rows));
		if (c == selectedColumn && selectedRow >= 0) {
			if (selectedRow < f)
				f = selectedRow;
			else if (selectedRow >= f + rows)
				f = selectedRow - rows + 1;
		}
		layout.First[c] = f;
		const int visible = std::max(0, std::min(rows, counts[c] - f));
		for (int i = 0; i < visible; i++)
			layout.Rows[c].push_back({ left, bodyTop + i * rowHeight, left + columnWidth, bodyTop + (i + 1) * rowHeight });
	}

	const int width = margin * 3 + columnWidth * 2;
	const int footerTop = bodyTop + rows * rowHeight + margin / 2;
	layout.Footer = { margin, footerTop, width - margin, footerTop + headerHeight };
	layout.Size = { width, layout.Footer.bottom + margin };
	return layout;
}

}
