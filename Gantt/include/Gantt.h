#pragma once

// Gantt chart: a project's tasks as a tree with columns (name, start, finish, duration, % complete) at the left, and as
// bars on a calendar at the right: summary tasks over their children, milestones as diamonds, the part done, the
// dependencies between tasks as arrows, non-working time shaded and today marked. Durations are working time on the
// control's calendar (working days and hours, holidays). The header's time units follow the zoom (Ctrl+wheel). The API
// follows the common controls: C-style structures and WM_NOTIFY codes (GNTN_*) sent to the parent. Times are local
// SYSTEMTIMEs; durations are working minutes (GetMinutesPerDay: a working day's).

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_GANTT[] = L"WC_Gantt";

// ---- Styles --------------------------------------------------------------------

#define GNTS_NOTODAY         0x0001   // no line at today

// ---- Tasks ----------------------------------------------------------------------

#define GNTF_COLLAPSED       0x0001   // a summary task's children hidden

// GNTTASK::mask: the fields SetTask changes
#define GNTTF_NAME           0x0001
#define GNTTF_START          0x0002
#define GNTTF_DURATION       0x0004
#define GNTTF_PROGRESS       0x0008
#define GNTTF_COLOR          0x0010
#define GNTTF_PARAM          0x0020
#define GNTTF_ALL            0x003F

struct GNTTASK {
	UINT    mask = GNTTF_ALL;         // SetTask's
	UINT    id = 0;                   // unique, not 0
	UINT    parent = 0;               // AddTask's: the summary task it's under; 0: at the top
	LPCWSTR pszName = nullptr;
	SYSTEMTIME start{};               // a summary task's are its children's
	LONGLONG duration = 0;            // working minutes; 0: a milestone
	int     progress = 0;             // 0 to 100
	COLORREF color = CLR_INVALID;     // the bar's; CLR_INVALID: the control's
	UINT    flags = 0;                // GNTF_*
	LPARAM  lParam = 0;
	// GetTask's
	SYSTEMTIME finish{};
	BOOL    summary = FALSE;
};

// a dependency: the second task can't start (or finish) before the first starts or finishes
#define GNTL_FINISHSTART     0
#define GNTL_STARTSTART      1
#define GNTL_FINISHFINISH    2
#define GNTL_STARTFINISH     3

// the grid's columns
#define GNTCOL_NAME          0
#define GNTCOL_START         1
#define GNTCOL_FINISH        2
#define GNTCOL_DURATION      3
#define GNTCOL_PROGRESS      4

// ---- Colors ----------------------------------------------------------------------

struct GNTCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crHeaderBack;
	COLORREF crHeaderText;
	COLORREF crGrid;                  // the lines between rows and cells
	COLORREF crNonWork;               // non-working time
	COLORREF crSelBack;               // the selected row
	COLORREF crBar;
	COLORREF crBarDone;
	COLORREF crBarBorder;
	COLORREF crSummary;
	COLORREF crMilestone;
	COLORREF crLink;
	COLORREF crToday;
};

// ---- Notifications -------------------------------------------------------------

#define GNTN_FIRST           (0U - 3250U)   // outside the common controls ranges
#define GNTN_SELCHANGED      (GNTN_FIRST - 0)   // NMGANTTTASK; the user selected a task (id 0: none)
#define GNTN_DBLCLK          (GNTN_FIRST - 1)   // NMGANTTTASK; a task's row or bar double-clicked
#define GNTN_EXPANDED        (GNTN_FIRST - 2)   // NMGANTTTASK; the user expanded or collapsed a summary task

struct NMGANTTTASK {
	NMHDR   hdr;
	UINT    id;
	LPARAM  lParam;
};

// ---- Control class --------------------------------------------------------------------------

struct GntData;
struct GntHitInfo;

class CGanttCtrl : public ATL::CWindowImpl<CGanttCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_Gantt", CS_DBLCLKS, -1)

	CGanttCtrl();
	~CGanttCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CGanttCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Tasks -----------------------------------------------------------------------------------------

	BOOL    AddTask(const GNTTASK* task, UINT after = 0);         // after a task of its parent (with its children), else last
	BOOL    AddTask(UINT id, LPCWSTR name, const SYSTEMTIME& start, LONGLONG duration, UINT parent = 0);
	BOOL    SetTask(const GNTTASK* task);                         // the fields in mask, by id
	BOOL    GetTask(UINT id, GNTTASK* task) const;                // pszName valid until the task changes
	BOOL    RemoveTask(UINT id);                                  // its children and links too
	void    RemoveAll();
	int     GetTaskCount() const;
	BOOL    Expand(UINT id, bool expand = true);
	BOOL    AddLink(UINT from, UINT to, UINT type = GNTL_FINISHSTART, LONGLONG lag = 0);   // FALSE: a cycle, a summary task
	BOOL    RemoveLink(UINT from, UINT to);
	// Moves each task that starts before its links allow, later (never earlier): the tasks after them by links follow.
	void    Reschedule();

	BOOL    Select(UINT id);                                      // no notification; 0: none
	UINT    GetSelected() const;
	void    EnsureVisible(UINT id);                               // its row and its bar scrolled into view

	// ---- The calendar ----------------------------------------------------------------------------------
	//
	// Default: Monday to Friday, 8:00 to 17:00. Changing it moves the finishes (durations are working time).

	void    SetWorkDays(BYTE days);                               // a bit by weekday, Sunday's the lowest
	void    SetWorkHours(int startMinute, int endMinute);         // of a working day: [start, end)
	BOOL    AddHoliday(const SYSTEMTIME& day);
	BOOL    RemoveHoliday(const SYSTEMTIME& day);
	int     GetMinutesPerDay() const;                             // a working day's

	// ---- The view --------------------------------------------------------------------------------------

	void    SetZoom(double minutesPerPixel);                      // Ctrl+wheel zooms too; default 45 (32 pixels a day)
	double  GetZoom() const;
	void    ScrollToTime(const SYSTEMTIME& t);                    // at the chart's left
	void    SetColumns(const UINT* columns, const int* widthsDips, int count);   // GNTCOL_*, in order
	void    SetGridWidth(int dips);                               // the grid's, left of the splitter (the user drags it)
	void    SetColors(const GNTCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	GNTCOLORS GetColors() const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    Changed();                                            // the tasks changed: rows, range, scroll bars, repaint
	void    UpdateLayout();                                       // the panes, the range, the scroll bars
	void    UpdateScrollBars();
	void    ScrollTo(int y, LONGLONG origin);                     // the rows' pixels, and the time at the chart's left; clamped
	void    ZoomAt(double minutesPerPixel, int x);                // x: a chart client x that stays at its time
	GntHitInfo HitTest(POINT pt);
	void    SetSelected(UINT id, bool notify);
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	LRESULT NotifyTask(UINT code, UINT id) const;

	// painting (GanttPaint.cpp)
	void    Paint(WTL::CDCHandle dc);
	void    PaintGrid(WTL::CDCHandle dc, const GNTCOLORS& c);
	void    PaintHeader(WTL::CDCHandle dc, const GNTCOLORS& c);
	void    PaintChart(WTL::CDCHandle dc, const GNTCOLORS& c);
	std::wstring CellText(int task, UINT column) const;
	std::wstring UnitLabel(int unit, LONGLONG start, int width) const;

	std::unique_ptr<GntData> m_Data;
	GNTCOLORS m_Colors{};
	bool    m_SystemColors = true;
	HFONT   m_hFont = nullptr;
	HFONT   m_hBold = nullptr;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
};
