#pragma once

// The control's state, shared by its source files.

#include "RibbonBar.h"
#include "RibbonBarModel.h"

#include <memory>
#include <vector>

class RibAccessible;

inline bool Same(const RibHit& a, const RibHit& b) {
	return a.part == b.part && a.group == b.group && a.item == b.item;
}

// Where a tab's groups are drawn and clicked: the ribbon's strip under the tabs, or a popup (a folded group opened,
// or a tab opened while the ribbon is minimized).
struct RibSurface {
	HWND   hwnd = nullptr;
	int    tab = -1;                  // the tab shown (an index)
	int    group = -1;                // a folded group's popup: that group alone, at its widest
	std::vector<RibGroupPlace> places;    // by the tab's groups; width 0: not on this surface
	RECT   panel{};                   // the groups' strip, client coords
	int    originX = 0;               // the groups' x = 0, from the panel's left
	int    total = 0;                 // the groups' width
	int    scroll = 0;                // when they don't fit
	RibHit hot;
	RibHit pressed;
	RibHit tip;                       // under the mouse for the tooltip (a disabled item too; an arrow is its item)
	int    opened = -1;               // the folded group whose popup is open
	bool   tracking = false;          // TrackMouseEvent
	bool   menu = false;              // an item's menu is open: it stays pressed and hot

	int Avail() const {
		return std::max(0, (int)(panel.right - panel.left) - 2 * originX);
	}
	int MaxScroll() const {
		return std::max(0, total - Avail());
	}
	int GroupX(int g) const {         // client coords
		return panel.left + originX + places[g].x - scroll;
	}
};

// A popup showing a surface. It takes the activation (a hosted window in it can have the focus) and closes when the
// activation goes anywhere but another of the ribbon's popups, on Escape, or when an item runs.
class RibPopup : public ATL::CWindowImpl<RibPopup, ATL::CWindow, ATL::CWinTraits<WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW>> {
public:
	DECLARE_WND_CLASS_EX(L"RibPopup", CS_DROPSHADOW | CS_DBLCLKS, -1)

	explicit RibPopup(CRibbonBarCtrl* ribbon) : m_Ribbon(ribbon) {}

	BEGIN_MSG_MAP(RibPopup)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_RANGE_HANDLER(WM_MOUSEMOVE, WM_LBUTTONDBLCLK, OnMouse)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouse)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnMouse)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYUP, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSCHAR, OnKeyDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnForward)
		MESSAGE_HANDLER(WM_DRAWITEM, OnForward)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnForward)
		MESSAGE_HANDLER(WM_COMPAREITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnForward)
	END_MSG_MAP()

	RibSurface surface;
	HWND   opener = nullptr;          // the surface's window it opened from
	bool   hidden = false;            // closed: destroyed on the ribbon's WM_RIB_CLOSEPOPUPS
	RibAccessible* acc = nullptr;     // made when a client asks (RibbonBarAccessible.cpp)

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouse(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnActivate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);

	CRibbonBarCtrl* m_Ribbon;
};

// The keyboard: key tips (Alt) and moving between the commands with the arrow keys.
enum class RibKeyMode : uint8_t { Off, Tips, Navigate };

// What a key tip or the keyboard focus can be on.
struct RibKeyTarget {
	enum class Kind : uint8_t { Tab, Qat, Item, Group, Launcher, Customize, Minimize };   // the last two: MSAA's
	Kind kind = Kind::Item;
	int  index = -1;                  // Tab: in visible; Qat: in qat
	int  group = -1;                  // Item, Group (folded), Launcher
	int  item = -1;
	HWND hwnd = nullptr;              // the surface's window (the ribbon's for the tab row)
	std::wstring tip;
};

inline bool SameTarget(const RibKeyTarget& a, const RibKeyTarget& b) {
	return a.kind == b.kind && a.hwnd == b.hwnd && a.index == b.index && a.group == b.group && a.item == b.item;
}

// A key tip: a small window of its own, over the hosted windows and past the surface's edges.
class RibTipWindow : public ATL::CWindowImpl<RibTipWindow, ATL::CWindow, ATL::CWinTraits<WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE>> {
public:
	DECLARE_WND_CLASS_EX(L"RibKeyTip", 0, -1)

	explicit RibTipWindow(CRibbonBarCtrl* ribbon) : m_Ribbon(ribbon) {}

	BEGIN_MSG_MAP(RibTipWindow)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
	END_MSG_MAP()

	std::wstring text;
	bool   disabled = false;

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) {
		return HTTRANSPARENT;    // clicks go to what's under it
	}
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);

	CRibbonBarCtrl* m_Ribbon;
};

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
	RECT minimizeButton{};            // empty with RIBS_NOMINIMIZE
	RibSurface main;                  // the ribbon's own (no groups while minimized)
	std::vector<std::unique_ptr<RibPopup>> popups;    // open, the last on top

	int  hotTab = -1;                 // in visible, or MinimizeButton

	// the quick access bar, at the left of the tab row: item ids (those with no item aren't shown)
	std::vector<UINT> qat;
	std::vector<RECT> qatRects;       // by qat; empty: not shown
	RECT qatCustomize{};              // its menu's button; empty with RIBS_NOQAT
	int  qatLine = 0;                 // the line between it and the tabs
	int  hotQat = -1;                 // in qat, or Customize
	int  pressedQat = -1;
	bool qatMenu = false;             // the hot one's menu is open

	// the keyboard (RibbonBarKeys.cpp)
	RibKeyMode keyMode = RibKeyMode::Off;
	HWND keyLevel = nullptr;          // the surface whose targets show (nullptr: the tab row; navigating, with the ribbon's)
	std::vector<RibKeyTarget> targets;
	std::vector<std::unique_ptr<RibTipWindow>> tips;   // by target, while the tips show
	std::wstring typed;               // the key tip's characters so far
	int  focus = -1;                  // navigating: in targets
	HWND restoreFocus = nullptr;      // where the focus was before
	bool altPending = false;          // Alt down, no other key since
	bool eatAltUp = false;            // Alt+key ran something: Alt's release isn't the system menu's

	// MSAA (RibbonBarAccessible.cpp)
	RibAccessible* acc = nullptr;     // the ribbon window's, made when a client asks
	RibKeyTarget accAction;           // a default action, run on WM_RIB_ACCACTION

	// the popup that closed last, as a click deactivated it: that click doesn't open it again
	HWND closedFrom = nullptr;
	int  closedTab = -1;
	int  closedGroup = -1;
	ULONGLONG closedTick = 0;

	static constexpr int MinimizeButton = -2;
	static constexpr int Customize = -2;   // hotQat

	// the first item with the id (its image, text and state stand for the others')
	RibItem* FindItem(UINT id, int* tab = nullptr, int* group = nullptr, int* index = nullptr) {
		for (int t = 0; t < (int)tabs.size(); t++)
			for (int g = 0; g < (int)tabs[t].groups.size(); g++) {
				auto& items = tabs[t].groups[g].items;
				for (int i = 0; i < (int)items.size(); i++)
					if (items[i].id == id && items[i].kind != RibKind::Separator) {
						if (tab)
							*tab = t;
						if (group)
							*group = g;
						if (index)
							*index = i;
						return &items[i];
					}
			}
		return nullptr;
	}
	// an item the quick access bar can show: a command, not a window
	bool QatAllowed(UINT id) {
		auto item = FindItem(id);
		return item && item->kind != RibKind::Window;
	}

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
	const RibContext* FindContext(int id) const {
		for (auto& c : contexts)
			if (c.id == id)
				return &c;
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
	// every surface, the ribbon's first
	template<typename F>
	void ForEachSurface(F&& f) {
		f(main);
		for (auto& p : popups)
			if (!p->hidden)
				f(p->surface);
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
