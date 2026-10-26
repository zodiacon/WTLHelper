#pragma once

// The control's state, shared by its sources.

#include "CalendarView.h"
#include "CalendarViewModel.h"

// The days' time: the ruler and the day columns under the strip, scrolled. Its messages are the control's
// (ALT_MSG_MAP(1)); items' windows are its children, so it clips them under the days' names and the strip.
class ClvDaysWindow : public ATL::CWindowImpl<ClvDaysWindow> {
public:
	DECLARE_WND_CLASS_EX(L"ClvDays", CS_DBLCLKS, -1)

	explicit ClvDaysWindow(CCalendarViewCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(ClvDaysWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 1)
	END_MSG_MAP()

private:
	CCalendarViewCtrl* m_Owner;
};

struct ClvData {
	ClvItemStore store;
	ClvRange range;                // the days shown
	UINT view = CLVV_DAY;
	ClvWorkWeek work;
	ClvMetrics m;                  // scaled for the DPI and the font
	ClvAxis axis;                  // its slotHeight: slotDips scaled
	int  slotDips = 22;
	ClvDayLayout layout;

	// the layout (the control's client coords)
	RECT header{};                 // the days' names
	RECT strip{};                  // the all-day strip as shown, its pads too
	int  scrollY = 0;              // the days' pixels scrolled
	int  stripScroll = 0;          // the strip's rows' pixels scrolled
	bool scrolled = false;         // scrolled to the working day's start once there was a size

	ULONGLONG selected = 0;
	bool slots = false;            // slots selected: slot
	ClvSpan slot;

	// virtual mode: the days the items kept are over
	bool isVirtual = false;
	ClvRange cached{ 0, 0 };
	bool fetching = false;         // CLVN_GETITEMS is out: AddItem lays out once it's back

	std::wstring titleBuffer, tipBuffer;   // GetItem's
	std::vector<std::pair<HWND, RECT>> placed;   // the items' windows as last placed (empty: hidden)
};

namespace ClvDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	inline void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
		dc.SetBkColor(color);
		dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);
}
