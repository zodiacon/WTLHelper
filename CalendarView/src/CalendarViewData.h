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

// Pending: pressed, not moved yet; Start, End: a timed item's ends; StripStart, StripEnd: a strip item's; Select: slots
enum class ClvDrag : uint8_t { None, Pending, Move, Start, End, StripStart, StripEnd, Select };

class ClvAccessible;

// the navigation bar's parts, as they come
enum ClvNav { ClvNavToday, ClvNavPrev, ClvNavNext, ClvNavTitle, ClvNavCount };

// The date chooser: a month of days in a popup that doesn't take the activation (the control keeps the focus and passes
// it the keys). It has the mouse while it's open: a press outside it closes it, and is eaten.
class ClvPickerAccessible;

class ClvPicker : public ATL::CWindowImpl<ClvPicker> {
public:
	DECLARE_WND_CLASS_EX(L"ClvPicker", CS_DROPSHADOW, -1)

	explicit ClvPicker(CCalendarViewCtrl* owner) : m_Owner(owner) {}
	~ClvPicker();

	enum : UINT { WM_PICKER_ACCACTION = WM_USER + 101 };

	BEGIN_MSG_MAP(ClvPicker)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_PICKER_ACCACTION, OnAccAction)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	END_MSG_MAP()

	void Show(const RECT& under, int64_t day);   // under: screen coords
	void Close();
	bool IsOpen() const {
		return m_Open;
	}
	bool Key(UINT vk);                            // the control's keys while it's open
	void Wheel(int delta);

private:
	friend class ClvPickerAccessible;

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAccAction(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);

	void Relayout(int64_t day);                   // the month day is in
	void SetFocusDay(int64_t day);                // moves the month with it
	void Paint(WTL::CDCHandle dc);
	void NotifyFocus();
	std::wstring DayName(int64_t day) const;      // spoken

	CCalendarViewCtrl* m_Owner;
	ClvMiniLayout m_Layout;
	int     m_Border = 1;                         // 0 when DWM draws it
	bool    m_Open = false;
	bool    m_Closing = false;
	bool    m_Keys = false;                       // the keys were used: the focus day framed
	int64_t m_Focus = 0;
	int64_t m_Hot = INT64_MIN;
	ClvMiniPart m_HotPart = ClvMiniPart::None;
	ClvPickerAccessible* m_Acc = nullptr;
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

	// the layout's top of the days' window (the control's client coords)
	int  daysTop = 0;

	// the navigation bar (CLVS_NAVBAR): its parts by ClvNav* (empty: no bar)
	RECT navbar{};
	RECT nav[4]{};
	int  navHot = -1, navPressed = -1;
	bool tracking = false;
	int  firstWeekday = -1;            // -1: the locale's

	// a drag: the item changes as it goes, the items as they were kept to go back to
	ClvDrag drag = ClvDrag::None;
	ClvDrag pending = ClvDrag::None;   // what a press becomes once the mouse moves
	ULONGLONG dragId = 0;
	ClvSpan dragOriginal;
	POINT dragDown{}, dragMouse{};     // the control's client coords
	ClvTime grab = 0;                  // the time under the mouse at the press
	int64_t grabDay = 0;               // and its day
	ClvTime anchor = 0;                // a slot selection's first slot (a day's start in the strip)
	bool anchorStrip = false;
	ClvItemStore::State before;
	ClvHistory history;
	bool creating = false;             // CLVN_CREATEITEM is out: its AddItem is an undo step, not an API change

	ClvTime cursor = -1;               // the keys' slot (-1: none yet)
	ULONGLONG renaming = 0;            // the item whose title is being edited
	std::wstring tipText;              // the tooltip's, on tipRect of tipWindow
	RECT tipRect{};
	HWND tipWindow = nullptr;

	std::wstring titleBuffer, tipBuffer;   // GetItem's
	ClvAccessible* acc = nullptr;      // once a client has asked
	std::vector<uint64_t> accOrder;    // the items as the accessible list's children, in the tab order
	ULONGLONG accItem = 0;             // the item a posted action is for
	std::vector<std::pair<HWND, RECT>> placed;   // the items' windows as last placed (empty: hidden)
};

// an item's time as a tooltip says it, or spoken (an accessibility client's description: no abbreviations)
std::wstring ClvWhen(const ClvItem& item, bool spoken);

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

	extern bool HighContrast;   // set as a paint starts

	// a color between two; in high contrast the second (no shades between the system's colors)
	inline COLORREF Shade(COLORREF a, COLORREF b, int percentB) {
		return HighContrast ? b : Blend(a, b, percentB);
	}

	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);
}
