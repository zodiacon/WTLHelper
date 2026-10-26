#pragma once

// Calendar view: a day view of one or more days side by side, as in Outlook's calendar. A time ruler at the left, the
// days' names on top, under them a strip of the all-day items (and those a day or longer), and the days' time below,
// scrolled and zoomed (Ctrl+wheel), with the working hours apart and a line at the time now. Items are drawn by the
// control, by the app (custom draw), or are the app's own windows placed in the days, under a caption the control
// draws: it makes the window a child of its days' window (GetDaysWindow), places it under the caption, hides it when
// its item is too short, in the strip or not shown, and passes the window's notifications, commands and colors on to its
// own parent. Overlapping items go side by side. The user drags items to move them (between the days and the strip
// too) and their ends to resize them, drags over slots to select them and double-clicks (or presses Enter) to ask the
// app for an item there; each change is the app's to allow, and Ctrl+Z undoes it (the control's own items). The keys:
// the arrows move the slots selected (Shift: more of them), Tab goes along the items, Alt+arrows move the selected item
// (Alt+Shift+Up/Down: its end), F2 renames it, Delete removes it. Tooltips tell an item's time. In high contrast it
// draws with the system colors only (over the app's and the items' own). Accessibility: a list of the items (MSAA).
// Ctrl+Page Up and Ctrl+Page Down go to the days before and after (a week in a week's view), Ctrl+Home to today, and
// Ctrl+G or F4 opens a month to pick a date from; CLVS_NAVBAR adds a bar above the days with Today, back and forward,
// and the days shown, which opens it too. The items are the control's own (AddItem), or in virtual mode
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
#define CLVS_NAVBAR          0x0008   // a bar above the days' names: Today, back, forward, and the days shown (a click picks a date)

// ---- Views ----------------------------------------------------------------------

#define CLVV_DAY             0        // day columns (SetDayCount)
#define CLVV_MONTH           1        // a month's weeks (not yet)

// ---- Items ----------------------------------------------------------------------

// CLVITEM::flags
#define CLVF_ALLDAY          0x0001   // whole days: start and end are the first day and the day after the last
#define CLVF_READONLY        0x0002   // the user doesn't move or resize it

// CLVITEM::content
#define CLVC_DEFAULT         0        // the control draws it
#define CLVC_CUSTOM          1        // the control draws its background, the app what's on it (CDDS_ITEMPOSTPAINT)
#define CLVC_WINDOW          2        // hwnd shows it, under a caption the control draws; hwnd stays the app's (hidden
                                      // once the item is removed or released: the app destroys it)

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
#define CLVN_CUSTOMDRAW      (CLVN_FIRST - 6)   // NMCLVCUSTOMDRAW; see below
#define CLVN_ITEMCHANGING    (CLVN_FIRST - 7)   // NMCLVCHANGE; the user moved or resized an item: non-zero vetoes (the app may change the values)
#define CLVN_ITEMCHANGED     (CLVN_FIRST - 8)   // NMCLVCHANGE
#define CLVN_CREATEITEM      (CLVN_FIRST - 9)   // NMCLVCREATE; the user asks for an item over the slots selected
#define CLVN_TITLECHANGING   (CLVN_FIRST - 10)  // NMCLVTITLE; the user renamed an item (F2): non-zero vetoes
#define CLVN_TITLECHANGED    (CLVN_FIRST - 11)  // NMCLVTITLE
#define CLVN_ITEMDELETING    (CLVN_FIRST - 12)  // NMCLVITEM; the user deletes an item (Delete): non-zero vetoes
#define CLVN_ITEMDELETED     (CLVN_FIRST - 13)  // NMCLVITEM; removed (a window item's window hidden: undo may bring it back)
#define CLVN_RESTORED        (CLVN_FIRST - 14)  // NMHDR; undo or redo changed the items: read them again

struct NMCLVITEM {
	NMHDR   hdr;
	ULONGLONG id;
	HWND    hwnd;                     // CLVC_WINDOW's
	LPARAM  lParam;
};

// what the user changed
#define CLVCH_MOVE           0        // the start and the end (into or out of the strip: allDay too)
#define CLVCH_START          1
#define CLVCH_END            2

struct NMCLVCHANGE {
	NMHDR   hdr;
	ULONGLONG id;
	UINT    change;                   // CLVCH_*
	SYSTEMTIME start;                 // the new values
	SYSTEMTIME end;
	BOOL    allDay;
	LPARAM  lParam;
};

struct NMCLVCREATE {
	NMHDR   hdr;
	SYSTEMTIME start;                 // the slots selected, [start, end)
	SYSTEMTIME end;
	BOOL    allDay;
	ULONGLONG id;                     // set by the app to the item it added: selected and renamed in place
};

struct NMCLVTITLE {
	NMHDR   hdr;
	ULONGLONG id;
	LPCWSTR pszTitle;                 // the new title
	LPARAM  lParam;
};

struct NMCLVRANGE {
	NMHDR   hdr;
	SYSTEMTIME start;                 // [start, end)
	SYSTEMTIME end;
	BOOL    allDay;                   // CLVN_SLOTCHANGED: the strip's days
};

// Custom draw, as the common controls': CDDS_PREPAINT once a paint (return CDRF_NOTIFYITEMDRAW for every item's);
// CDDS_ITEMPREPAINT for each item asked for, and always for CLVC_CUSTOM items: return CDRF_SKIPDEFAULT to draw it all,
// or change crFill and crText, and CDRF_NOTIFYPOSTPAINT for CDDS_ITEMPOSTPAINT after the control's drawing (always sent
// for CLVC_CUSTOM items: draw in rcContent). The DC is clipped to the item, and its font is the control's.
struct NMCLVCUSTOMDRAW {
	NMCUSTOMDRAW nmcd;                // rc: the item's piece; uItemState: CDIS_SELECTED, CDIS_FOCUS; lItemlParam: its lParam
	ULONGLONG id;
	RECT    rcContent;                // inside its color bar and pads (a window item's: its caption's)
	COLORREF crFill;                  // the colors it's drawn in (ITEMPREPAINT: the app may change them)
	COLORREF crText;
	COLORREF crColor;                 // the item's color (its bar)
	BOOL    strip;                    // in the all-day strip
	BOOL    continuesBefore;          // the item goes on before the piece (an earlier day), or after it
	BOOL    continuesAfter;
};

// ---- Control class --------------------------------------------------------------------------

struct ClvData;
struct ClvHit;
struct ClvItem;
class ClvDaysWindow;

class CCalendarViewCtrl : public ATL::CWindowImpl<CCalendarViewCtrl> {
	friend class ClvDaysWindow;
	friend class ClvAccessible;
	friend class ClvPicker;
	friend class ClvPickerAccessible;
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
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_CLV_ACCACTION, OnAccAction)
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
		MESSAGE_HANDLER(WM_PARENTNOTIFY, OnDaysParentNotify)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnDaysMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnDaysLButtonUp)
		MESSAGE_HANDLER(WM_SETCURSOR, OnDaysSetCursor)
		// the items' windows': on to the control's parent
		MESSAGE_HANDLER(WM_HSCROLL, OnForward)
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnForward)
		MESSAGE_HANDLER(WM_DRAWITEM, OnForward)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnForward)
		MESSAGE_HANDLER(WM_COMPAREITEM, OnForward)
		MESSAGE_HANDLER(WM_DELETEITEM, OnForward)
		MESSAGE_HANDLER(WM_VKEYTOITEM, OnForward)
		MESSAGE_HANDLER(WM_CHARTOITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnCtlColor)
	ALT_MSG_MAP(2)   // the title being edited (F2)
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnEditGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnEditKillFocus)
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

	BOOL    EditTitle(ULONGLONG id);                              // renames it in place, as F2 does

	// The user's changes go back and forth (Ctrl+Z, Ctrl+Y): the control's own items only (not in virtual mode). Undo
	// goes back to the items as they were, so the API's changes to the items clear what there is to undo.
	BOOL    Undo();                                               // CLVN_RESTORED follows
	BOOL    Redo();
	bool    CanUndo() const;
	bool    CanRedo() const;
	void    ClearUndo();

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
	void    Navigate(int steps);                                  // the days after (or before): a week in a week's view, else the days shown
	void    ShowDatePicker();                                     // a month to pick the date to show (as Ctrl+G does)
	void    SetFirstDayOfWeek(int day);                           // 0 Sunday to 6 Saturday; -1 (the default): the user's locale's
	int     GetFirstDayOfWeek() const;
	void    ScrollToTime(int minute);                             // at the top
	void    SetSlotMinutes(int minutes);                          // 5, 6, 10, 15, 20, 30 or 60
	int     GetSlotMinutes() const;
	void    SetZoom(int slotHeightDips);                          // a slot's height; Ctrl+wheel zooms too
	int     GetZoom() const;
	void    SetWorkWeek(BYTE days, int startMinute, int endMinute);   // a bit by weekday, Sunday's the lowest; [start, end)
	void    SetColors(const CLVCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	CLVCOLORS GetColors() const;                                  // in high contrast: the system's
	void    ForceHighContrast(int on);                            // for testing: 1 on, 0 off, -1 (the default) as Windows says
	bool    IsHighContrast() const;

	HWND    GetDaysWindow() const;                                // the child with the days' time (the parent of items' windows)

private:
	enum : UINT { WM_CLV_ACCACTION = WM_USER + 100 };              // an accessibility client's action, done later

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
	LRESULT OnDaysParentNotify(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDaysSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LPCWSTR CursorAt(POINT pt) const;                             // what a press at pt would do (control coords); nullptr: the arrow
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAccAction(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);

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
	void    UpdateScrollBarTheme();                               // the days' scroll bar dark on a dark background
	void    PlaceWindows();
	// editing (CalendarViewEdit.cpp)
	ClvHit  HitAt(POINT pt, bool* strip) const;                   // pt: the control's client coords, over the strip or the days
	void    Press(POINT pt, bool dbl);                            // the left button, anywhere (control coords)
	void    DragTo(POINT pt);
	void    EndDrag(POINT pt);
	void    CancelDrag();
	void    DragScroll();                                         // the drag's mouse past the days' top or bottom: scrolled
	bool    Apply(ULONGLONG id, UINT change);                     // the item changed (before kept): the app may veto, else it's a step
	void    Step();                                               // before and now: an undo step (the control's own items)
	void    CreateItem();                                         // CLVN_CREATEITEM over the slots selected
	void    DeleteSelected();
	void    Restored();
	void    SetComposited(bool on);
	// the keys (CalendarViewKeys.cpp)
	bool    KeyDown(UINT vk, bool ctrl, bool alt, bool shift);
	void    MoveSlots(int days, int slots, bool extend);
	void    Nudge(int days, int slots, bool end);                 // Alt+arrows: the selected item moved (or its end)
	void    EndRename(bool commit);
	void    UpdateTip(HWND window, POINT pt);
	// the navigation bar and the date chooser (CalendarViewNav.cpp)
	void    LayoutNavBar(int width);                              // its parts, from the top
	int     NavPartAt(POINT pt) const;                            // ClvNav*; -1 if none
	bool    NavPress(POINT pt);                                   // the button down on the bar: true if it was
	void    NavRelease(POINT pt);
	void    NavHot(POINT pt);
	void    NavAction(int part);
	std::wstring NavName(int part) const;                         // as an accessibility client hears it
	std::wstring NavTip(int part) const;
	std::wstring RangeTitle() const;                              // the days shown, as the bar says them
	void    PaintNavBar(WTL::CDCHandle dc, const CLVCOLORS& c);
	void    PickDate(LONGLONG day);                               // the date chooser's pick: its view shown
	void    ClosePicker();
	bool    PickerOpen() const;
	// accessibility (CalendarViewAccessible.cpp)
	long    AccChildId(ULONGLONG id) const;                       // its child's, 1..; CHILDID_SELF if it isn't shown
	long    AccSlotsId() const;                                   // the selected time's child; CHILDID_SELF if none
	long    AccNavCount() const;                                  // the bar's buttons, first among the children (0: no bar)
	std::wstring AccDescription(ULONGLONG id) const;
	std::wstring AccSlots() const;                                // the selected time, said
	void    NotifyAccessible(DWORD event, long child);            // once a client has asked
	void    DisconnectAccessible();                     // the tool on window (the control or the days'), pt its client's                                       // the items' windows under their captions, or hidden
	void    RedrawWindowOf(ULONGLONG id);                         // its window's background follows its item's
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
	void    PaintItem(WTL::CDCHandle dc, const CLVCOLORS& c, int box, bool strip, bool notify);
	bool    Prepaint(WTL::CDCHandle dc, const RECT& rc) const;   // CDDS_PREPAINT: whether the app wants each item's
	void    ItemColors(const ClvItem& item, const CLVCOLORS& c, COLORREF* fill, COLORREF* text) const;   // as it's drawn
	std::wstring DayName(LONGLONG day, int width, WTL::CDCHandle dc) const;
	int     RulerWidth(WTL::CDCHandle dc) const;                 // the widest hour's label and its pads
	std::wstring TimeText(LONGLONG start, LONGLONG end, bool allDay) const;

	std::unique_ptr<ClvData> m_Data;
	std::unique_ptr<ClvDaysWindow> m_Days;
	ATL::CContainedWindowT<WTL::CEdit> m_Edit;
	std::unique_ptr<class ClvPicker> m_Picker;
	HWND    m_hTip = nullptr;
	CLVCOLORS m_Colors{};
	bool    m_SystemColors = true;
	HFONT   m_hFont = nullptr;
	HFONT   m_hBold = nullptr;
	HBRUSH  m_hStaticBrush = nullptr;                             // WM_CTLCOLORSTATIC's: an item's fill
	COLORREF m_StaticColor = CLR_INVALID;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_DarkScrollBars = -1;                                // as last set: 0, 1 (-1: not yet)
	int     m_ForceHighContrast = -1;
};
