#include "RibbonBarModel.h"

#include <algorithm>
#include <climits>
#include <cwctype>

// ---- Metrics ---------------------------------------------------------------------------------------

RibMetrics RibMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	RibMetrics m;
	m.rowHeight = s(rowHeight);
	m.largeIcon = s(largeIcon);
	m.smallIcon = s(smallIcon);
	m.pad = s(pad);
	m.gap = s(gap);
	m.arrow = s(arrow);
	m.groupPad = s(groupPad);
	m.columnGap = s(columnGap);
	m.labelHeight = s(labelHeight);
	m.launcher = s(launcher);
	m.separator = s(separator);
	return m;
}

// ---- Items ------------------------------------------------------------------------------------------

RibSize RibItem::Largest() const {
	if (kind == RibKind::Window || kind == RibKind::Separator)
		return std::max(size, RibSize::Small);    // never large
	return size;
}

RibSize RibItem::Smallest() const {
	auto s = minSize;
	if (s == RibSize::Icon && kind != RibKind::Window && !HasImage())
		s = RibSize::Small;                        // nothing to show as an icon
	if (kind == RibKind::Separator)
		s = RibSize::Small;
	return std::max(s, Largest());
}

int RibItem::Rows(RibSize s) const {
	if (kind == RibKind::Separator)
		return 3;
	if (kind == RibKind::Window)
		return std::clamp(rows, 1, 3);
	return s == RibSize::Large ? 3 : 1;
}

// ---- Measuring ----------------------------------------------------------------------------------------

void RibSplitLabel(std::wstring_view text, const RibMeasure& measure, int extra2, std::wstring& line1, std::wstring& line2) {
	line1.assign(text);
	line2.clear();
	int best = INT_MAX;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != L' ')
			continue;
		auto a = text.substr(0, i);
		auto b = text.substr(i + 1);
		while (!a.empty() && a.back() == L' ')
			a.remove_suffix(1);
		while (!b.empty() && b.front() == L' ')
			b.remove_prefix(1);
		if (a.empty() || b.empty())
			continue;
		int cost = std::max(measure(a), measure(b) + extra2);
		if (cost < best) {
			best = cost;
			line1.assign(a);
			line2.assign(b);
		}
	}
}

void RibMeasureItem(RibItem& item, const RibMetrics& m, const RibMeasure& measure) {
	item.line1.clear();
	item.line2.clear();
	if (item.kind == RibKind::Separator) {
		item.width[0] = item.width[1] = item.width[2] = m.separator;
		return;
	}
	int text = item.text.empty() ? 0 : measure(item.text);
	if (item.kind == RibKind::Window) {
		int labeled = (text ? text + m.gap : 0) + item.windowWidth;
		item.width[(int)RibSize::Large] = item.width[(int)RibSize::Small] = labeled;
		item.width[(int)RibSize::Icon] = item.windowWidth;
		return;
	}
	int arrow = item.HasArrow() ? m.arrow : 0;
	int icon = item.HasImage() ? m.smallIcon : 0;
	int arrowPart = arrow ? m.gap + arrow : 0;

	// large: the image above, the label on two lines with the arrow after the second (or alone on it)
	RibSplitLabel(item.text, measure, arrowPart, item.line1, item.line2);
	int w1 = item.line1.empty() ? 0 : measure(item.line1);
	int w2 = item.line2.empty() ? arrow : measure(item.line2) + arrowPart;
	item.width[(int)RibSize::Large] = 2 * m.pad + std::max({ m.largeIcon, w1, w2 });

	item.width[(int)RibSize::Small] = 2 * m.pad + icon + (text ? (icon ? m.gap : 0) + text : 0) + arrowPart;
	item.width[(int)RibSize::Icon] = 2 * m.pad + icon + arrowPart;
}

void RibMeasureGroup(RibGroup& group, const RibMetrics& m, const RibMeasure& measure) {
	for (auto& item : group.items)
		RibMeasureItem(item, m, measure);
	group.labelWidth = group.text.empty() ? 0 : measure(group.text);
	if (group.flags & RibGroupLauncher)
		group.labelWidth += m.gap + m.launcher;
	int arrowPart = m.gap + m.arrow;
	RibSplitLabel(group.text, measure, arrowPart, group.line1, group.line2);
	int w1 = group.line1.empty() ? 0 : measure(group.line1);
	int w2 = group.line2.empty() ? m.arrow : measure(group.line2) + arrowPart;
	group.collapsedWidth = 2 * m.groupPad + 2 * m.pad + std::max({ m.largeIcon, w1, w2 });
	RibGroupSteps(group, m);
}

void RibGroupSteps(RibGroup& group, const RibMetrics& m) {
	group.steps.clear();
	auto& items = group.items;
	int n = (int)items.size();
	bool any = false;
	RibStep step;
	step.sizes.resize(n);
	for (int i = 0; i < n; i++) {
		step.sizes[i] = items[i].Largest();
		any = any || (!items[i].Hidden() && items[i].kind != RibKind::Separator);
	}
	if (!any)
		return;    // nothing to show
	RibPlaceItems(group, step.sizes, m, &step.width);
	group.steps.push_back(step);

	// large to small, then small to icons: each step takes the rightmost run of up to three (a column of small items)
	for (auto from : { RibSize::Large, RibSize::Small }) {
		auto to = (RibSize)((int)from + 1);
		auto canShrink = [&](int i) {
			return !items[i].Hidden() && step.sizes[i] == from && items[i].Smallest() >= to;
		};
		for (;;) {
			int last = n - 1;
			while (last >= 0 && !canShrink(last))
				last--;
			if (last < 0)
				break;
			int taken = 0;
			for (int i = last; i >= 0 && taken < 3; i--) {
				if (items[i].Hidden())
					continue;
				if (!canShrink(i))
					break;
				step.sizes[i] = to;
				taken++;
			}
			RibPlaceItems(group, step.sizes, m, &step.width);
			if (step.width < group.steps.back().width)
				group.steps.push_back(step);
		}
	}
	if (!(group.flags & RibGroupNoCollapse) && group.collapsedWidth < group.steps.back().width) {
		step.collapsed = true;
		step.width = group.collapsedWidth;
		group.steps.push_back(step);
	}
}

// ---- Layout --------------------------------------------------------------------------------------------

std::vector<RibItemPlace> RibPlaceItems(const RibGroup& group, const std::vector<RibSize>& sizes, const RibMetrics& m, int* width) {
	std::vector<RibItemPlace> places(group.items.size());
	int x = 0, column = 0, used = 0;    // the column's x, its width so far, its rows taken
	for (size_t i = 0; i < group.items.size(); i++) {
		auto& item = group.items[i];
		places[i].size = sizes[i];
		if (item.Hidden())
			continue;
		int rows = item.Rows(sizes[i]);
		int w = item.width[(int)sizes[i]];
		if (used > 0 && used + rows > 3) {
			x += column + m.columnGap;
			column = used = 0;
		}
		places[i].rc = { x, used * m.rowHeight, x + w, (used + rows) * m.rowHeight };
		column = std::max(column, w);
		used += rows;
		if (used >= 3) {
			x += column + m.columnGap;
			column = used = 0;
		}
	}
	int content = used > 0 ? x + column : std::max(0, x - m.columnGap);
	int inner = std::max(content, group.labelWidth);
	int dx = m.groupPad + (inner - content) / 2;    // the items centered under a wider label
	for (auto& p : places)
		if (p.rc.right > p.rc.left)
			::OffsetRect(&p.rc, dx, m.groupPad);
	if (width)
		*width = inner + 2 * m.groupPad;
	return places;
}

std::vector<int> RibFitTab(const std::vector<RibGroup>& groups, int width, const RibMetrics&, int* total) {
	int n = (int)groups.size();
	std::vector<int> steps(n, -1);
	int sum = 0, shown = 0;
	std::vector<int> priorities;
	for (int g = 0; g < n; g++) {
		if (!groups[g].Visible())
			continue;
		steps[g] = 0;
		sum += groups[g].steps[0].width;
		shown++;
		priorities.push_back(groups[g].priority);
	}
	sum += std::max(0, shown - 1);    // the lines between them
	std::sort(priorities.begin(), priorities.end());
	priorities.erase(std::unique(priorities.begin(), priorities.end()), priorities.end());

	auto go = [&](int g, int step) {
		sum += groups[g].steps[step].width - groups[g].steps[steps[g]].width;
		steps[g] = step;
	};
	// a group's steps before it collapses
	auto open = [&](int g) {
		auto& s = groups[g].steps;
		return s.back().collapsed ? s.size() - 1 : s.size();
	};
	for (int p : priorities) {
		if (sum <= width)
			break;
		// rounds of a step each, from the right; then the groups collapse, from the right: none collapses while
		// another of its priority can still shrink
		size_t rounds = 0;
		for (int g = 0; g < n; g++)
			if (steps[g] >= 0 && groups[g].priority == p)
				rounds = std::max(rounds, open(g));
		for (size_t round = 1; round < rounds && sum > width; round++)
			for (int g = n - 1; g >= 0 && sum > width; g--)
				if (steps[g] >= 0 && groups[g].priority == p && open(g) > round)
					go(g, (int)round);
		for (int g = n - 1; g >= 0 && sum > width; g--)
			if (steps[g] >= 0 && groups[g].priority == p && groups[g].steps.back().collapsed)
				go(g, (int)groups[g].steps.size() - 1);
	}
	if (total)
		*total = sum;
	return steps;
}

std::vector<RibGroupPlace> RibLayoutTab(const std::vector<RibGroup>& groups, int width, const RibMetrics& m, int* total) {
	auto steps = RibFitTab(groups, width, m);
	std::vector<RibGroupPlace> places(groups.size());
	int x = 0;
	bool any = false;
	for (size_t g = 0; g < groups.size(); g++) {
		auto& place = places[g];
		place.x = x;
		place.step = steps[g];
		if (steps[g] < 0)
			continue;
		auto& group = groups[g];
		auto& step = group.steps[steps[g]];
		place.width = step.width;
		place.collapsed = step.collapsed;
		if (step.collapsed)
			place.items.resize(group.items.size());
		else {
			place.items = RibPlaceItems(group, step.sizes, m);
			int top = m.groupPad + m.ItemsHeight();
			place.label = { m.groupPad, top, step.width - m.groupPad, top + m.labelHeight };
			if (group.flags & RibGroupLauncher) {
				int y = top + (m.labelHeight - m.launcher) / 2;
				place.launcher = { step.width - m.groupPad - m.launcher, y, step.width - m.groupPad, y + m.launcher };
				place.label.right = place.launcher.left - m.gap;
			}
		}
		x += step.width + 1;
		any = true;
	}
	if (total)
		*total = any ? x - 1 : 0;
	return places;
}

// ---- Hit testing ---------------------------------------------------------------------------------------------

void RibSplitRects(const RECT& rc, RibSize size, const RibMetrics& m, RECT& main, RECT& arrow) {
	main = arrow = rc;
	if (size == RibSize::Large) {
		main.bottom = std::min(rc.bottom, rc.top + 2 * m.pad + m.largeIcon);
		arrow.top = main.bottom;
	}
	else {
		arrow.left = std::max(rc.left, rc.right - (m.arrow + m.pad + m.gap / 2));
		main.right = arrow.left;
	}
}

RECT RibCollapsedRect(int width, const RibMetrics& m) {
	return { m.groupPad, m.groupPad, width - m.groupPad, m.BodyHeight() - m.groupPad };
}

RibHit RibHitTest(const std::vector<RibGroup>& groups, const std::vector<RibGroupPlace>& places, POINT pt, const RibMetrics& m) {
	RibHit hit;
	for (size_t g = 0; g < places.size() && g < groups.size(); g++) {
		auto& place = places[g];
		if (place.width <= 0 || pt.x < place.x || pt.x >= place.x + place.width)
			continue;
		hit.group = (int)g;
		POINT p{ pt.x - place.x, pt.y };
		if (place.collapsed) {
			auto rc = RibCollapsedRect(place.width, m);
			if (::PtInRect(&rc, p))
				hit.part = RibPart::Group;
			return hit;
		}
		if (::PtInRect(&place.launcher, p)) {
			hit.part = RibPart::Launcher;
			return hit;
		}
		auto& items = groups[g].items;
		for (size_t i = 0; i < items.size() && i < place.items.size(); i++) {
			auto& item = items[i];
			if (item.Hidden() || item.kind == RibKind::Separator || item.kind == RibKind::Window)
				continue;
			auto& rc = place.items[i].rc;
			if (!::PtInRect(&rc, p))
				continue;
			hit.item = (int)i;
			hit.part = RibPart::Item;
			if (item.kind == RibKind::Split) {
				RECT main, arrow;
				RibSplitRects(rc, place.items[i].size, m, main, arrow);
				if (::PtInRect(&arrow, p))
					hit.part = RibPart::Arrow;
			}
			return hit;
		}
		return hit;
	}
	return hit;
}

// ---- Tabs -----------------------------------------------------------------------------------------------------

std::vector<int> RibVisibleTabs(const std::vector<RibTab>& tabs, const std::vector<RibContext>& contexts) {
	std::vector<int> order;
	for (int i = 0; i < (int)tabs.size(); i++)
		if (!tabs[i].hidden && tabs[i].context == 0)
			order.push_back(i);
	for (auto& c : contexts) {
		if (!c.shown)
			continue;
		for (int i = 0; i < (int)tabs.size(); i++)
			if (!tabs[i].hidden && tabs[i].context == c.id)
				order.push_back(i);
	}
	return order;
}

// ---- Key tips ------------------------------------------------------------------------------------------------------

static std::wstring Upper(std::wstring s) {
	for (auto& c : s)
		c = (wchar_t)::towupper(c);
	return s;
}

std::vector<std::wstring> RibAssignKeyTips(const std::vector<std::wstring>& labels, const std::vector<std::wstring>& fixed) {
	size_t n = labels.size();
	std::vector<std::wstring> tips(n);
	std::vector<std::wstring> used;
	auto conflicts = [&](const std::wstring& t) {
		for (auto& u : used)
			if (t.starts_with(u) || u.starts_with(t))
				return true;
		return false;
	};
	auto take = [&](size_t i, std::wstring t) {
		if (conflicts(t))
			return false;
		used.push_back(t);
		tips[i] = std::move(t);
		return true;
	};
	for (size_t i = 0; i < n; i++)
		if (i < fixed.size() && !fixed[i].empty()) {
			tips[i] = Upper(fixed[i]);
			used.push_back(tips[i]);
		}
	// the characters a label offers, word starts first
	auto letters = [&](size_t i, bool starts) {
		std::wstring s;
		auto& text = labels[i];
		for (size_t k = 0; k < text.size(); k++) {
			if (!::iswalnum(text[k]))
				continue;
			bool start = k == 0 || !::iswalnum(text[k - 1]);
			if (!starts || start)
				s += (wchar_t)::towupper(text[k]);
		}
		return s;
	};
	for (int pass = 0; pass < 3; pass++) {
		for (size_t i = 0; i < n; i++) {
			if (!tips[i].empty())
				continue;
			std::wstring chars = pass == 0 ? letters(i, true) : pass == 1 ? letters(i, false) : L"123456789";
			for (auto c : chars)
				if (take(i, std::wstring(1, c)))
					break;
		}
	}
	const std::wstring digits = L"123456789", alphabet = L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	for (size_t i = 0; i < n; i++) {
		if (!tips[i].empty())
			continue;
		auto own = letters(i, true) + letters(i, false);
		auto firsts = own + alphabet;
		auto seconds = own + digits + alphabet;
		bool done = false;
		for (size_t a = 0; a < firsts.size() && !done; a++)
			for (size_t b = 0; b < seconds.size() && !done; b++)
				done = take(i, std::wstring{ firsts[a], seconds[b] });
	}
	return tips;
}

std::wstring RibQatKeyTip(int index) {
	if (index < 0)
		return {};
	if (index < 9)
		return std::wstring(1, (wchar_t)(L'1' + index));
	if (index < 18)
		return std::wstring{ L'0', (wchar_t)(L'9' - (index - 9)) };
	if (index < 18 + 26)
		return std::wstring{ L'0', (wchar_t)(L'A' + (index - 18)) };
	return {};
}

int RibMatchKeyTip(const std::vector<std::wstring>& tips, std::wstring_view typed, int* prefixCount) {
	auto t = Upper(std::wstring(typed));
	int found = -1, count = 0;
	for (int i = 0; i < (int)tips.size(); i++) {
		if (tips[i].empty() || !tips[i].starts_with(t))
			continue;
		count++;
		if (tips[i] == t)
			found = i;
	}
	if (prefixCount)
		*prefixCount = count;
	return found;
}

// ---- Items --------------------------------------------------------------------------------------------------------

bool RibCheckRadio(std::vector<RibGroup>& groups, int group, int item) {
	if (group < 0 || group >= (int)groups.size() || item < 0 || item >= (int)groups[group].items.size())
		return false;
	auto& chosen = groups[group].items[item];
	if (chosen.kind != RibKind::Radio)
		return false;
	int radio = chosen.radio;
	bool changed = false;
	for (auto& g : groups)
		for (auto& i : g.items) {
			if (i.kind != RibKind::Radio || i.radio != radio)
				continue;
			UINT state = &i == &chosen ? i.state | RibStateChecked : i.state & ~RibStateChecked;
			changed = changed || state != i.state;
			i.state = state;
		}
	return changed;
}

int RibNearest(const std::vector<RECT>& rects, int from, UINT vk) {
	if (from < 0 || from >= (int)rects.size())
		return -1;
	auto& f = rects[from];
	bool horizontal = vk == VK_LEFT || vk == VK_RIGHT;
	int best = -1;
	long long bestScore = LLONG_MAX;
	for (int i = 0; i < (int)rects.size(); i++) {
		auto& r = rects[i];
		if (i == from || r.right <= r.left || r.bottom <= r.top)
			continue;
		int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
		int along;    // the distance in the key's direction
		switch (vk) {
			case VK_LEFT: if (cx >= f.left) continue; along = f.left - r.right; break;
			case VK_RIGHT: if (cx <= f.right) continue; along = r.left - f.right; break;
			case VK_UP: if (cy >= f.top) continue; along = f.top - r.bottom; break;
			case VK_DOWN: if (cy <= f.bottom) continue; along = r.top - f.bottom; break;
			default: return -1;
		}
		along = std::max(along, 0);
		// across: how far it's off the line (0 when they overlap)
		int across = horizontal ? std::max({ 0, (int)(r.top - f.bottom), (int)(f.top - r.bottom) })
			: std::max({ 0, (int)(r.left - f.right), (int)(f.left - r.right) });
		long long score = across == 0 ? along : (1LL << 32) + 2LL * across + along;
		if (score < bestScore) {
			bestScore = score;
			best = i;
		}
	}
	return best;
}

// ---- State ------------------------------------------------------------------------------------------------------------

std::wstring RibSaveState(const RibState& state) {
	std::wstring s = L"tab=" + std::to_wstring(state.tab) + L";min=" + (state.minimized ? L"1" : L"0") + L";qat=";
	for (size_t i = 0; i < state.qat.size(); i++) {
		if (i)
			s += L',';
		s += std::to_wstring(state.qat[i]);
	}
	return s;
}

static bool ParseId(std::wstring_view s, UINT& value) {
	if (s.empty() || s.size() > 10)
		return false;
	unsigned long long v = 0;
	for (auto c : s) {
		if (c < L'0' || c > L'9')
			return false;
		v = v * 10 + (c - L'0');
	}
	if (v > UINT_MAX)
		return false;
	value = (UINT)v;
	return true;
}

bool RibLoadState(std::wstring_view text, RibState& state) {
	state = {};
	if (text.empty())
		return false;
	RibState s;
	while (!text.empty()) {
		auto end = text.find(L';');
		auto entry = text.substr(0, end);
		text = end == std::wstring_view::npos ? std::wstring_view{} : text.substr(end + 1);
		if (entry.empty())
			continue;
		auto eq = entry.find(L'=');
		if (eq == std::wstring_view::npos)
			return false;
		auto key = entry.substr(0, eq), value = entry.substr(eq + 1);
		if (key == L"tab") {
			if (!ParseId(value, s.tab))
				return false;
		}
		else if (key == L"min") {
			if (value != L"0" && value != L"1")
				return false;
			s.minimized = value == L"1";
		}
		else if (key == L"qat") {
			while (!value.empty()) {
				auto comma = value.find(L',');
				UINT id;
				if (!ParseId(value.substr(0, comma), id))
					return false;
				s.qat.push_back(id);
				value = comma == std::wstring_view::npos ? std::wstring_view{} : value.substr(comma + 1);
			}
		}
	}
	state = std::move(s);
	return true;
}
