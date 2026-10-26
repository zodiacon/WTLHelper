#pragma once

// Calendar view: a day view of one or more days side by side, as in Outlook's calendar. A time ruler at the left, the
// days' names on top, under them a strip of the all-day items (and those a day or longer), and the days' time below,
// scrolled and zoomed (Ctrl+wheel), with the working hours apart and a line at the time now. Items are drawn by the
// control, by the app (custom draw), or are the app's own windows placed in the days, under a caption the control
// draws. Overlapping items go side by side. The items are the control's own (AddItem), or in virtual mode
// (CLVS_VIRTUAL) the app's: the control asks for those of the days it shows (CLVN_GETITEMS) and lets go of the ones
// that leave them (CLVN_RELEASEITEM). The API follows the common controls: C-style structures and WM_NOTIFY codes
// (CLVN_*) sent to the parent. Times are local SYSTEMTIMEs (seconds ignored).

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <memory>
#include <string>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_CALENDARVIEW[] = L"WC_CalendarView";

// ---- Styles --------------------------------------------------------------------

#define CLVS_NONOW           0x0001   // no line at the time now
#define CLVS_READONLY        0x0002   // the user changes no items
#define CLVS_VIRTUAL         0x0004   // the app has the items: CLVN_GETITEMS asks for those of the days shown (set at creation)

// ---- Views ----------------------------------------------------------------------

#define CLVV_DAY             0        // day columns (SetDayCount)
#define CLVV_MONTH           1        // a month's weeks (not yet)

// ---- Items ----------------------------------------------------------------------

// CLVITEM::flags
#define CLVF_ALLDAY          0x0001   // whole days: start and end are the first day and the day after the last
#define CLVF_READONLY        0x0002   // the user doesn't move or resize it

// CLVITEM::content
#define CLVC_DEFAULT         0        // the control draws it
#define CLVC_CUSTOM          1        // the app draws it (custom draw)
#define CLVC_WINDOW          2        // hwnd shows it, under a caption the control draws

// CLVITEM::mask: the fields SetItem changes
#define CLVIF_TIME           0x0001   // start, end
#define CLVIF_TITLE          0x0002
#define CLVIF_TIP            0x0004
#define CLVIF_COLOR          0x0008
#define CLVIF_FLAGS          0x0010
#define CLVIF_CONTENT        0x0020   // content, hwnd
#define CLVIF_PARAM          0x0040
#define CLVIF_ALL            0x007F

struct CLVITEM {
	UINT    mask = CLVIF_ALL;         // SetItem's
	ULONGLONG id = 0;                 // unique, not 0
	SYSTEMTIME start{};
	SYSTEMTIME end{};                 // not before start; an all-day item's: the day after its last
	UINT    flags = 0;                // CLVF_*
	LPCWSTR pszTitle = nullptr;
	LPCWSTR pszTip = nullptr;         // the tooltip's text; nullptr: the title and the time
	COLORREF color = CLR_INVALID;     // CLR_INVALID: the control's
	UINT    content = CLVC_DEFAULT;   // CLVC_*
	HWND    hwnd = nullptr;           // CLVC_WINDOW's
	LPARAM  lParam = 0;
};

// ---- Colors ----------------------------------------------------------------------

struct CLVCOLORS {
	COLORREF crBack;                  // working time
	COLORREF crText;
	COLORREF crHeaderBack;            // the days' names, the ruler and the strip
	COLORREF crHeaderText;
	COLORREF crGrid;                  // the hours' and days' lines
	COLORREF crNonWork;               // the time off
	COLORREF crToday;                 // today's name and the line at now
	COLORREF crItem;                  // an item's color when it has none
	COLORREF crSelection;             // the slots selected
};

// ---- Notifications -------------------------------------------------------------

#define CLVN_FIRST           (0U - 3350U)   // outside the common controls ranges
#define CLVN_SELCHANGED      (CLVN_FIRST - 0)   // NMCLVITEM; the user selected an item (id 0: none, slots maybe)
#define CLVN_ITEMACTIVATE    (CLVN_FIRST - 1)   // NMCLVITEM; an item double-clicked
#define CLVN_RANGECHANGED    (CLVN_FIRST - 2)   // NMCLVRANGE; the days shown changed
#define CLVN_GETITEMS        (CLVN_FIRST - 3)   // NMCLVRANGE; virtual: AddItem the items over the days, now
#define CLVN_RELEASEITEM     (CLVN_FIRST - 4)   // NMCLVITEM; virtual: an item left the days shown (its window is the app's again)
#define CLVN_SLOTCHANGED     (CLVN_FIRST - 5)   // NMCLVRANGE; the user selected slots (GetSelectedSlots)

struct NMCLVITEM {
	NMHDR   hdr;
	ULONGLONG id;
	HWND    hwnd;                     // CLVC_WINDOW's
	LPARAM  lParam;
};

struct NMCLVRANGE {
	NMHDR   hdr;
	SYSTEMTIME start;                 // [start, end)
	SYSTEMTIME end;
	BOOL    allDay;                   // CLVN_SLOTCHANGED: the strip's days
};

// ---- Control class --------------------------------------------------------------------------

struct ClvData;
struct ClvHit;
struct ClvItem;
class ClvDaysWindow;

class CCalendarViewCtrl : public ATL::CWindowImpl<CCalendarViewCtrl> {
	friend class ClvDaysWindow;
public:
	DECLARE_WND_CLASS_EX(L"WC_CalendarView", CS_DBLCLKS, -1)

	CCalendarViewCtrl();
	~CCalendarViewCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CCalendarViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the days' window: the ruler and the days' time, scrolled
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnDaysPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnDaysPaint)
		MESSAGE_HANDLER(WM_VSCROLL, OnDaysVScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnDaysLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnDaysLButtonDblClk)
	END_MSG_MAP()

	// ---- Items -----------------------------------------------------------------------------------------
	//
	// In virtual mode the items added are kept while they're over the days shown; add them while CLVN_GETITEMS is
	// sent, or later as the app's data changes.

	BOOL    AddItem(const CLVITEM* item);
	BOOL    AddItem(ULONGLONG id, LPCWSTR title, const SYSTEMTIME& start, const SYSTEMTIME& end, UINT flags = 0, COLORREF color = CLR_INVALID);
	BOOL    SetItem(const CLVITEM* item);                         // the fields in mask, by id
	BOOL    GetItem(ULONGLONG id, CLVITEM* item) const;           // the strings valid until the item changes
	BOOL    RemoveItem(ULONGLONG id);
	void    RemoveAll();
	int     GetItemCount() const;
	// Virtual: lets go of every item (CLVN_RELEASEITEM) and asks for the days shown again (CLVN_GETITEMS).
	void    RefreshItems();

	BOOL    Select(ULONGLONG id);                                 // no notification; 0: none
	ULONGLONG GetSelected() const;
	BOOL    GetSelectedSlots(SYSTEMTIME* start, SYSTEMTIME* end, BOOL* allDay) const;   // FALSE if none are
	void    EnsureVisible(ULONGLONG id);                          // its days shown and its time scrolled into view

	// ---- The days shown ------------------------------------------------------------------------------

	BOOL    SetView(UINT view);                                   // CLVV_*; FALSE: not there (yet)
	UINT    GetView() const;
	void    SetDate(const SYSTEMTIME& first);                     // the first day shown
	void    SetDayCount(int days);                                // 1 to 14
	void    GetRange(SYSTEMTIME* first, int* days) const;
	void    GoToday();                                            // today shown (the first day, or in the week shown) and now in view
	void    ScrollToTime(int minute);                             // at the top
	void    SetSlotMinutes(int minutes);                          // 5, 6, 10, 15, 20, 30 or 60
	int     GetSlotMinutes() const;
	void    SetZoom(int slotHeightDips);                          // a slot's height; Ctrl+wheel zooms too
	int     GetZoom() const;
	void    SetWorkWeek(BYTE days, int startMinute, int endMinute);   // a bit by weekday, Sunday's the lowest; [start, end)
	void    SetColors(const CLVCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	CLVCOLORS GetColors() const;

	HWND    GetDaysWindow() const;                                // the child with the days' time (the parent of items' windows)

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    Changed();                                            // the items changed: layout, repaint
	void    UpdateLayout();                                       // the columns, the boxes, the days' window, the scroll bar
	void    RangeChanged(bool notify);                            // the days shown changed: virtual items, layout, notification
	void    ScrollDays(int y);
	void    ScrollStrip(int y);
	void    ZoomAt(int slotHeight, int y);                        // y: a days' window y that stays at its time
	ClvHit  HitStrip(POINT pt) const;                             // pt: the control's client coords
	ClvHit  HitDays(POINT pt) const;                              // pt: the days' window's
	void    SetSelected(ULONGLONG id, bool notify);
	void    SelectSlots(LONGLONG start, LONGLONG end, bool allDay, bool notify);
	void    ClearSlots();
	void    Release(const ClvItem& item);                         // a virtual item let go of: CLVN_RELEASEITEM
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	LRESULT NotifyItem(UINT code, ULONGLONG id) const;
	LRESULT NotifyRange(UINT code, LONGLONG start, LONGLONG end, bool allDay) const;

	// painting (CalendarViewPaint.cpp)
	void    Paint(WTL::CDCHandle dc);                             // the days' names and the strip
	void    PaintDays(WTL::CDCHandle dc, const RECT& rcPaint);    // the ruler and the days' time
	void    PaintItem(WTL::CDCHandle dc, const CLVCOLORS& c, int box, bool strip);
	std::wstring DayName(LONGLONG day, int width, WTL::CDCHandle dc) const;
	int     RulerWidth(WTL::CDCHandle dc) const;                 // the widest hour's label and its pads
	std::wstring TimeText(LONGLONG start, LONGLONG end, bool allDay) const;

	std::unique_ptr<ClvData> m_Data;
	std::unique_ptr<ClvDaysWindow> m_Days;
	CLVCOLORS m_Colors{};
	bool    m_SystemColors = true;
	HFONT   m_hFont = nullptr;
	HFONT   m_hBold = nullptr;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
};
