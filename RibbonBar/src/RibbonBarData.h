#pragma once

// The control's state, shared by its source files.

#include "RibbonBar.h"
#include "RibbonBarModel.h"

#include <vector>

inline bool Same(const RibHit& a, const RibHit& b) {
	return a.part == b.part && a.group == b.group && a.item == b.item;
}

struct RibData {
	std::vector<RibTab> tabs;
	std::vector<RibContext> contexts;
	RibMetrics m;                     // at the DPI, the image lists' sizes
	int  selected = -1;               // in tabs

	// the layout: measured when the sizes or the font change, placed when the width or the tab does
	bool measure = true;
	bool layout = true;
	std::vector<int> tabWidths;       // by tab
	std::vector<int> visible;         // the tabs shown, in order
	std::vector<RECT> tabRects;       // by visible
	std::vector<RibGroupPlace> places;    // the selected tab's groups
	int  total = 0;                   // their width
	RECT panel{};                     // the groups' strip, client coords
	int  originX = 0;                 // the groups' x = 0

	// the mouse
	int    hotTab = -1;               // in visible
	RibHit hot;
	RibHit pressed;

	RibTab* Selected() {
		return selected >= 0 && selected < (int)tabs.size() ? &tabs[selected] : nullptr;
	}
	int FindTab(UINT id) const {
		for (int i = 0; i < (int)tabs.size(); i++)
			if (tabs[i].id == id)
				return i;
		return -1;
	}
	RibGroup* FindGroup(UINT id, int* tab = nullptr) {
		for (int t = 0; t < (int)tabs.size(); t++)
			for (auto& g : tabs[t].groups)
				if (g.id == id) {
					if (tab)
						*tab = t;
					return &g;
				}
		return nullptr;
	}
	// f(tab, group, item index, item) for every item with the id
	template<typename F>
	void ForEachItem(UINT id, F&& f) {
		for (int t = 0; t < (int)tabs.size(); t++)
			for (int g = 0; g < (int)tabs[t].groups.size(); g++) {
				auto& items = tabs[t].groups[g].items;
				for (int i = 0; i < (int)items.size(); i++)
					if (items[i].id == id && items[i].kind != RibKind::Separator)
						f(t, g, i, items[i]);
			}
	}
};

namespace RibDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}
}
