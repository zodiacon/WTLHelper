#include "DockLayout.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace WTLDock {

namespace {

// Splits 'avail' pixels among children. Px children ask for their size, Star children share the rest by weight,
// nobody goes below its minimum unless even the minimums do not fit (then everything is squeezed proportionally).
std::vector<int> ComputeLengths(const std::vector<SizeSpec>& specs, const std::vector<int>& mins, int avail) {
	const int n = (int)specs.size();
	std::vector<int> lens(n, 0);

	const int sumMin = std::accumulate(mins.begin(), mins.end(), 0);
	if (avail <= sumMin) {
		int used = 0;
		for (int i = 0; i < n; i++) {
			lens[i] = sumMin > 0 ? (int)((long long)avail * mins[i] / sumMin) : avail / n;
			used += lens[i];
		}
		lens[n - 1] += avail - used;
		return lens;
	}

	std::vector<int> stars;
	for (int i = 0; i < n; i++) {
		if (specs[i].IsStar())
			stars.push_back(i);
		else
			lens[i] = std::max(mins[i], (int)std::lround(specs[i].Value));
	}

	auto pixelTotal = [&] {
		int total = 0;
		for (int i = 0; i < n; i++)
			if (!specs[i].IsStar())
				total += lens[i];
		return total;
		};

	int starMins = 0;
	for (int i : stars)
		starMins += mins[i];

	// make room for the star children by taking from the Px children, last first
	int deficit = starMins - (avail - pixelTotal());
	for (int i = n - 1; i >= 0 && deficit > 0; i--) {
		if (specs[i].IsStar())
			continue;
		int cut = std::min(deficit, lens[i] - mins[i]);
		lens[i] -= cut;
		deficit -= cut;
	}

	int remaining = avail - pixelTotal();
	if (stars.empty()) {
		lens[n - 1] += remaining;
		return lens;
	}

	// proportional shares; a child whose share is below its minimum gets the minimum and leaves the pool
	auto weight = [&](int i) { return std::max(specs[i].Value, 1e-6); };
	std::vector<int> pool = stars;
	for (;;) {
		double totalWeight = 0;
		for (int i : pool)
			totalWeight += weight(i);
		bool clamped = false;
		for (auto it = pool.begin(); it != pool.end(); ++it) {
			if (remaining * weight(*it) / totalWeight < mins[*it]) {
				lens[*it] = mins[*it];
				remaining -= mins[*it];
				pool.erase(it);
				clamped = true;
				break;
			}
		}
		if (clamped && !pool.empty())
			continue;
		if (!pool.empty()) {
			int used = 0;
			for (int i : pool) {
				lens[i] = (int)(remaining * weight(i) / totalWeight);
				used += lens[i];
			}
			lens[pool.back()] += remaining - used;
		}
		break;
	}
	return lens;
}

}

int DockLayout::MinLength(const DockNode& node, Axis axis) const {
	if (auto group = node.AsGroup()) {
		int min = Along(m_Metrics.MinGroupSize, axis);
		for (auto p : group->Panes())
			min = std::max(min, Along(p->MinSize, axis));
		return min;
	}

	auto split = node.AsSplit();
	int result = 0;
	for (auto& c : split->Children()) {
		int m = MinLength(*c, axis);
		result = split->GetAxis() == axis ? result + m : std::max(result, m);
	}
	if (split->GetAxis() == axis && !split->Children().empty())
		result += m_Metrics.SplitterThickness * ((int)split->Children().size() - 1);
	return result;
}

void DockLayout::Arrange(const RECT& client) {
	const int thickness = m_Metrics.AutoHideBarThickness;
	RECT r = client;
	for (auto& bar : m_BarRects)
		bar = {};

	// top and bottom bars span the full width, left and right bars what is between them
	auto& top = m_BarRects[(int)DockSide::Top];
	auto& bottom = m_BarRects[(int)DockSide::Bottom];
	auto& left = m_BarRects[(int)DockSide::Left];
	auto& right = m_BarRects[(int)DockSide::Right];
	if (!m_AutoHide[(int)DockSide::Top].empty()) {
		top = { r.left, r.top, r.right, std::min(r.top + thickness, r.bottom) };
		r.top = top.bottom;
	}
	if (!m_AutoHide[(int)DockSide::Bottom].empty()) {
		bottom = { r.left, std::max(r.bottom - thickness, r.top), r.right, r.bottom };
		r.bottom = bottom.top;
	}
	if (!m_AutoHide[(int)DockSide::Left].empty()) {
		left = { r.left, r.top, std::min(r.left + thickness, r.right), r.bottom };
		r.left = left.right;
	}
	if (!m_AutoHide[(int)DockSide::Right].empty()) {
		right = { std::max(r.right - thickness, r.left), r.top, r.right, r.bottom };
		r.right = right.left;
	}

	m_Root->Rect = r;
	ArrangeSplit(*m_Root);
}

void DockLayout::Arrange(DockFloat& window, const RECT& client) {
	window.m_Root->Rect = client;
	ArrangeSplit(*window.m_Root);
}

void DockLayout::ArrangeSplit(DockSplit& split) {
	auto& kids = split.m_Children;
	const int n = (int)kids.size();
	if (n == 0)
		return;

	const Axis axis = split.m_Axis;
	const bool horizontal = axis == Axis::Horizontal;
	const int gap = m_Metrics.SplitterThickness;
	const int avail = std::max(0, Length(split.Rect, axis) - gap * (n - 1));

	std::vector<SizeSpec> specs;
	std::vector<int> mins;
	for (auto& c : kids) {
		specs.push_back(c->Size);
		mins.push_back(MinLength(*c, axis));
	}
	const auto lens = ComputeLengths(specs, mins, avail);

	int pos = horizontal ? split.Rect.left : split.Rect.top;
	for (int i = 0; i < n; i++) {
		RECT r = split.Rect;
		if (horizontal) {
			r.left = pos;
			r.right = pos + lens[i];
		}
		else {
			r.top = pos;
			r.bottom = pos + lens[i];
		}
		kids[i]->Rect = r;
		pos += lens[i] + gap;
		if (auto inner = kids[i]->AsSplit())
			ArrangeSplit(*inner);
	}
}

std::vector<SplitterHit> DockLayout::Splitters(DockSplit& root) {
	std::vector<SplitterHit> result;
	std::function<void(DockSplit&)> collect = [&](DockSplit& s) {
		auto& kids = s.m_Children;
		for (int i = 0; i + 1 < (int)kids.size(); i++) {
			RECT r = s.Rect;
			if (s.m_Axis == Axis::Horizontal) {
				r.left = kids[i]->Rect.right;
				r.right = kids[i + 1]->Rect.left;
			}
			else {
				r.top = kids[i]->Rect.bottom;
				r.bottom = kids[i + 1]->Rect.top;
			}
			result.push_back({ &s, i, r });
		}
		for (auto& c : kids)
			if (auto inner = c->AsSplit())
				collect(*inner);
		};
	collect(root);
	return result;
}

DockGroup* DockLayout::GroupAt(DockSplit& root, POINT pt) {
	for (auto& c : root.m_Children) {
		if (!PtInRect(&c->Rect, pt))
			continue;
		if (auto inner = c->AsSplit())
			return GroupAt(*inner, pt);
		return c->AsGroup();
	}
	return nullptr;
}

}
