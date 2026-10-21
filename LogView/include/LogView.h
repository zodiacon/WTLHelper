#pragma once

// Log view: a read-only, line-based text viewer for logs and traces with millions of lines. Lines are kept
// by the control (appended from any thread, with an optional limit that drops the oldest) or supplied by
// the app on demand (virtual mode). Each line has a level and a style that pick its colors. The API follows
// the common controls: C-style structures and WM_NOTIFY codes (LGVN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_LOGVIEW[] = L"WC_LogView";

constexpr size_t LGV_NOLINE = (size_t)-1;

// ---- Levels -------------------------------------------------------------------
//
// A line's level (0..31) picks its colors when the line has no style of its own, and (later) filters by a
// mask of 1 << level. The predefined levels have default styles; the app may use any of 0..31.

enum : BYTE {
	LGVL_NONE,
	LGVL_TRACE,      // dimmed
	LGVL_DEBUG,      // dimmed
	LGVL_INFO,
	LGVL_WARNING,    // amber, with a gutter marker
	LGVL_ERROR,      // red, with a gutter marker
	LGVL_CRITICAL,   // white on red, with a gutter marker
	LGVL_MAX = 31,
};

// ---- Styles -------------------------------------------------------------------
//
// Style 0 on a line means "the style of its level". Styles 1..255 are the app's (SetStyle).

#define LGVSF_BOLD     0x0001
#define LGVSF_ITALIC   0x0002
#define LGVSF_DIM      0x0004   // the text color blended halfway into the background

struct LGVSTYLE {
	COLORREF crText   = CLR_DEFAULT;   // CLR_DEFAULT: the view's text color
	COLORREF crBack   = CLR_DEFAULT;   // CLR_DEFAULT: the view's background
	COLORREF crMarker = CLR_NONE;      // a bar in the gutter; CLR_NONE: none
	UINT     flags    = 0;             // LGVSF_*
};

// ---- Lines ---------------------------------------------------------------------

struct LGVLINE {
	LPCWSTR pszText = nullptr;
	int     cchText = -1;              // -1: null-terminated
	BYTE    level = LGVL_NONE;
	BYTE    style = 0;
	LPARAM  lParam = 0;
};

struct LGVLINEINFO {
	ULONGLONG number;                  // as shown in the gutter: 1-based, counting lines dropped by the limit
	BYTE      level;
	BYTE      style;
	LPARAM    lParam;
	size_t    length;                  // characters
};

// ---- Positions ----------------------------------------------------------------
//
// A place between characters: column is a character index in the line (a tab is one character), 0..length.

struct LGVPOS {
	size_t line;
	size_t column;
};

// ---- Filter and rules ----------------------------------------------------------
//
// A filter shows only the lines whose text matches (or, with LGVFF_EXCLUDE, doesn't) and whose level is in the
// mask. A rule styles the lines matching it in place of their level's style (when the line has no style of its
// own; the level's gutter marker stays unless the rule's style has one), or with LGVRF_MATCHONLY just the text it
// matches. The first whole-line rule that matches wins; match-only rules all apply, later ones over earlier.

#define LGVFF_MATCHCASE   0x0001
#define LGVFF_REGEX       0x0002   // ECMAScript (std::wregex)
#define LGVFF_EXCLUDE     0x0004   // filter: the lines that don't match the text
#define LGVFF_WHOLEWORD   0x0008   // not next to a letter, digit or '_'
#define LGVRF_MATCHONLY   0x0100   // rule: style the matched text only (its colors; bold and italic don't apply)

#define LGV_ALLLEVELS     0xFFFFFFFF

struct LGVFILTER {
	LPCWSTR pszText = nullptr;         // nullptr or "": any text
	UINT    flags = 0;                 // LGVFF_*
	DWORD   levelMask = LGV_ALLLEVELS; // 1 << level
};

struct LGVSEARCH {
	LPCWSTR pszText = nullptr;         // nullptr or "": no search
	UINT    flags = 0;                 // LGVFF_MATCHCASE, LGVFF_REGEX, LGVFF_WHOLEWORD
};

struct LGVRULE {
	LPCWSTR pszText = nullptr;
	UINT    flags = 0;                 // LGVFF_MATCHCASE, LGVFF_REGEX, LGVRF_MATCHONLY
	BYTE    style = 0;                 // 1..255 (SetStyle)
};

// ---- Colors ----------------------------------------------------------------------

struct LGVCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crGutterBack;
	COLORREF crGutterText;
	COLORREF crSelBack;
	COLORREF crSelText;
	COLORREF crSelBackInactive;
	COLORREF crSelTextInactive;
	COLORREF crMatchBack;              // the search's matches
	COLORREF crMatchText;
};

// ---- Hit testing ----------------------------------------------------------------

#define LGVHT_NOWHERE  0
#define LGVHT_GUTTER   1
#define LGVHT_TEXT     2   // on a line, right of the gutter (also past the end of its text)
#define LGVHT_BELOW    3   // under the last line

// ---- Notifications -------------------------------------------------------------

#define LGVN_FIRST          (0U - 2850U)   // outside the common controls ranges
#define LGVN_GETDISPINFO    (LGVN_FIRST - 0)   // NMLGVDISPINFO; virtual mode: a line is needed
#define LGVN_CONTEXTMENU    (LGVN_FIRST - 1)   // NMLGVLINE; pt in screen coords, line may be LGV_NOLINE
#define LGVN_ACTIVATE       (LGVN_FIRST - 2)   // NMLGVLINE; a double click or Enter on a line
#define LGVN_FOLLOWCHANGED  (LGVN_FIRST - 3)   // NMHDR; the user's scrolling turned following the tail on or off
#define LGVN_SELCHANGED     (LGVN_FIRST - 4)   // NMHDR; the user moved the caret or changed the selection
#define LGVN_FILTERDONE     (LGVN_FIRST - 5)   // NMHDR; SetFilter's pass over the lines is complete
#define LGVN_SEARCHDONE     (LGVN_FIRST - 6)   // NMLGVSEARCH; FindNext (or F3) is done
#define LGVN_FILTERCHANGED  (LGVN_FIRST - 7)   // NMHDR; the user changed the filter in the toolbar
#define LGVN_SEARCHCHANGED  (LGVN_FIRST - 8)   // NMHDR; the user changed the search in the toolbar

struct NMLGVLINE {
	NMHDR  hdr;
	size_t line;
	POINT  pt;
};

struct NMLGVSEARCH {
	NMHDR  hdr;
	BOOL   found;                      // the match is selected
	BOOL   wrapped;                    // it went past the end (or the start, going back)
	size_t line;                       // the match's; LGV_NOLINE if none
};

// Virtual mode: the parent points pszText at the line's text (cchText -1 if null-terminated), which has to
// last until the notification returns, and may set level, style and lParam (zero on entry). It's sent while
// painting, so the handler must not change the view.
struct NMLGVDISPINFO {
	NMHDR   hdr;
	size_t  line;
	LPCWSTR pszText;
	int     cchText;
	BYTE    level;
	BYTE    style;
	LPARAM  lParam;
};

// ---- Control class --------------------------------------------------------------------------

class LgStore;
class LgBatch;
struct LgFilter;
class LgMatcher;
class LgRowMap;
struct LgRule;
class LgTextWindow;
class LgAccessible;

class CLogViewCtrl : public ATL::CWindowImpl<CLogViewCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_LogView", 0, -1)

	CLogViewCtrl();
	~CLogViewCtrl();
	friend class LgTextWindow;
	friend class LgAccessible;

	enum : UINT { WM_LGV_FLUSH = WM_USER + 100 };   // posted to itself: appended lines to take in

	BEGIN_MSG_MAP(CLogViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_LGV_FLUSH, OnFlush)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColor)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnToolButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnToolButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnToolMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnToolMouseLeave)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the text area, a child window
		MESSAGE_HANDLER(WM_PAINT, OnTextPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnTextPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnTextSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnTextFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnTextFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnTextGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnTextKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnTextLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnTextLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnTextLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnTextMouseMove)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnTextCaptureChanged)
		MESSAGE_HANDLER(WM_TIMER, OnTextTimer)
		MESSAGE_HANDLER(WM_SETCURSOR, OnTextSetCursor)
		MESSAGE_HANDLER(WM_GETOBJECT, OnTextGetObject)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnTextRButtonDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTextContextMenu)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnTextMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnTextMouseWheel)
		MESSAGE_HANDLER(WM_VSCROLL, OnTextVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnTextHScroll)
	END_MSG_MAP()

	// ---- Lines ----------------------------------------------------------------------------
	//
	// AppendLine/AppendLines/AppendText may be called from any thread; everything else from the window's.
	// On the window's thread a line is in the view (GetLineCount) when the call returns. From other threads
	// lines are queued and taken in together by a posted message, so a busy writer costs one repaint per
	// batch; lines appended before the window exists come in when it's created.

	void    AppendLine(LPCWSTR text, BYTE level = LGVL_NONE, BYTE style = 0, LPARAM lParam = 0);
	void    AppendLines(const LGVLINE* lines, int count);
	// splits at "\r\n", "\n" and "\r"; a final line break doesn't start another line. cch -1: null-terminated.
	void    AppendText(LPCWSTR text, int cch = -1, BYTE level = LGVL_NONE, BYTE style = 0);
	void    Clear();                                  // all lines, queued ones too; leaves virtual mode
	void    Flush();                                  // takes in lines queued by other threads now
	size_t  GetLineCount() const;
	ATL::CString GetLineText(size_t line) const;
	BOOL    GetLineInfo(size_t line, LGVLINEINFO* pInfo) const;
	BOOL    SetLineStyle(size_t line, BYTE level, BYTE style);   // not in virtual mode
	// 0: no limit. Past it the oldest lines are dropped; the gutter's numbers keep counting.
	void    SetLineLimit(size_t maxLines);
	size_t  GetLineLimit() const;

	// ---- Virtual mode -------------------------------------------------------------------
	//
	// The parent owns the lines and supplies them with LGVN_GETDISPINFO. Setting the count enters virtual
	// mode (lines the control held are dropped, appending is ignored); Clear leaves it.

	void    SetVirtualLineCount(size_t count);
	bool    IsVirtual() const;
	void    InvalidateLines(size_t first, size_t count);  // repaints them (their text is asked for again)

	// ---- View --------------------------------------------------------------------------

	// following the tail keeps the last line in view as lines come in. Scrolling up turns it off, scrolling
	// to the end (Ctrl+End, or the scroll bar to the bottom) back on; either sends LGVN_FOLLOWCHANGED.
	void    SetFollowTail(bool follow);               // no notification
	bool    IsFollowingTail() const;
	void    EnsureVisible(size_t line);
	void    SetTopLine(size_t line);
	size_t  GetTopLine() const;
	int     GetVisibleLineCount() const;              // whole lines that fit
	// client coords; LGVHT_* in *pPart; *pColumn: the character boundary nearest to pt (LGVHT_TEXT only)
	size_t  HitTest(POINT pt, UINT* pPart = nullptr, size_t* pColumn = nullptr) const;
	BOOL    GetLineRect(size_t line, RECT* prc) const;        // client coords; FALSE if not in view
	void    ShowLineNumbers(bool show);
	bool    AreLineNumbersShown() const;
	void    SetTabSize(int columns);                  // 1..16, default 4
	int     GetTabSize() const;
	void    BeginUpdate();                            // nests; suppresses painting and scroll bar updates
	void    EndUpdate();
	HWND    GetTextWindow() const;                    // the child that shows the lines (scroll bars, focus)

	// ---- Filter and rules ---------------------------------------------------------------
	//
	// Filtering goes through the lines in slices on a timer, so even a regex over millions of lines leaves the
	// window responsive: lines that pass show up as it goes, and LGVN_FILTERDONE says when it has caught up.
	// Lines appended later are filtered as they come in. In virtual mode the parent is asked for each line
	// (LGVN_GETDISPINFO); after changing lines' text it sets the filter again to filter them anew. When the pass
	// is done the caret (moved to a line that's shown) is scrolled into view, unless the user scrolled meanwhile
	// or the view follows the tail. The gutter keeps the lines' own numbers. Line indexes in the rest of the API
	// stay those of all the lines.

	BOOL    SetFilter(const LGVFILTER* filter);       // nullptr: none. FALSE: a bad regex (the filter stays)
	BOOL    GetFilter(LGVFILTER* filter) const;       // pszText points into the control until the filter changes
	bool    IsFiltered() const;
	bool    IsFilterPending() const;                  // still going through the lines
	size_t  GetShownLineCount() const;                // the lines the filter shows (all of them with no filter)
	bool    IsLineShown(size_t line) const;
	int     AddRule(const LGVRULE* rule);             // the rule's index, -1 for a bad regex or no text
	BOOL    RemoveRule(int index);
	void    ClearRules();
	int     GetRuleCount() const;

	// ---- Search -----------------------------------------------------------------------
	//
	// The search text's matches are highlighted in the lines shown. FindNext goes from the caret (the end of the
	// selection, or its start going back) through the lines shown, around past the end, and selects the match;
	// like the filter it works in slices, so LGVN_SEARCHDONE comes right away or a little later. Keys: F3 and
	// Shift+F3 find the next and previous match, Ctrl+F3 searches for the selected text, Escape cancels.

	BOOL    SetSearch(const LGVSEARCH* search);       // nullptr or no text: none. FALSE: a bad regex
	BOOL    GetSearch(LGVSEARCH* search) const;       // pszText points into the control until the search changes
	BOOL    FindNext(bool forward = true);            // FALSE if there's no search text
	bool    IsSearching() const;
	void    CancelSearch();                           // no notification

	// ---- Toolbar ------------------------------------------------------------------------
	//
	// Optional, above the lines: a filter box and a search box, each with its options (match case, regex, and
	// exclude or whole word), buttons that show or hide the errors, warnings, info and debug lines (the filter's
	// level mask), and Follow. Ctrl+L goes to the filter box, Ctrl+F to the search box. In the search box Enter
	// or F3 finds the next match, Shift+Enter or Shift+F3 the previous one; Escape clears a box, or when it's
	// empty goes back to the lines. The toolbar sets the filter, search and following as the API does (and
	// LGVN_FILTERCHANGED, LGVN_SEARCHCHANGED, LGVN_FOLLOWCHANGED tell), and shows what the app sets with them.

	void    ShowToolbar(bool show);
	bool    IsToolbarVisible() const;

	// ---- Caret and selection ----------------------------------------------------------
	//
	// The selection runs from the anchor to the caret. Clicking places the caret, dragging or Shift selects;
	// the gutter selects whole lines. Keys: arrows, Home/End (smart Home), Page Up/Down, Ctrl+Home/End, with
	// Ctrl moving by words and Shift extending; Ctrl+Up/Down scroll without moving the caret; Ctrl+A selects
	// all; Ctrl+C or Ctrl+Insert copies (the caret's line when nothing is selected); Enter and a double click
	// send LGVN_ACTIVATE (a double click also selects the word). Positions follow their lines when the limit
	// drops older ones; a selection that's all dropped goes.

	LGVPOS  GetCaret() const;
	void    SetCaret(LGVPOS pos, bool extend = false);   // scrolls it into view; no notification
	void    GetSelection(LGVPOS* pStart, LGVPOS* pEnd) const;   // in order; equal if nothing is selected
	void    SetSelection(LGVPOS anchor, LGVPOS caret);   // the caret is scrolled into view; no notification
	void    SelectAll();
	bool    HasSelection() const;
	ATL::CString GetSelectedText() const;              // lines joined with CR LF
	BOOL    Copy();                                    // as Ctrl+C

	// ---- Appearance -------------------------------------------------------------------
	//
	// The default font is Consolas, 10 points at the window's DPI; WM_SETFONT replaces it (the app then
	// replaces it for a new DPI). Bold and italic come from the same face.

	void    SetColors(const LGVCOLORS* colors);       // nullptr: system colors (follows WTLHelper dark mode)
	LGVCOLORS GetColors() const;                      // the colors in effect
	BOOL    SetStyle(BYTE index, const LGVSTYLE* style);        // 1..255; nullptr resets it
	BOOL    GetStyle(BYTE index, LGVSTYLE* style) const;
	BOOL    SetLevelStyle(BYTE level, const LGVSTYLE* style);   // 0..31; nullptr: back to the default
	BOOL    GetLevelStyle(BYTE level, LGVSTYLE* style) const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFlush(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnToolButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnToolMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnToolMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnTextPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextHScroll(UINT, WPARAM, LPARAM, BOOL&);

	// lines
	struct LineData {                                 // a line as drawn: owned, or from the parent
		LPCWSTR text;
		size_t  length;
		BYTE    level;
		BYTE    style;
		LPARAM  lParam;
	};
	bool    OnWindowThread() const;
	void    Enqueue(const LGVLINE* lines, int count, LPCWSTR text, int cch, BYTE level, BYTE style);
	void    PostFlush();
	void    LinesAdded();                             // after the store changed: scroll range, follow, repaint
	bool    GetLine(size_t line, LineData& data) const;
	ULONGLONG LineNumber(size_t line) const;
	size_t  MaxColumns() const;

	// rows: the lines in view order, all of them or those the filter shows (LogViewFilter.cpp)
	ULONGLONG FirstSerial() const;                    // line 0's serial number
	size_t  RowCount() const;
	size_t  LineOfRow(size_t row) const;
	size_t  RowOfLine(size_t line) const;             // its row, or the next shown line's (RowCount() if none)
	bool    ScanStep(DWORD budgetMs);                 // filters the lines not seen yet; true when done
	void    StartScan();
	void    FilterDone();
	bool    SearchStep(DWORD budgetMs);               // true when done
	void    EndSearch(bool found, size_t line, size_t start, size_t end);
	void    StartFind(bool forward, bool byUser);

	// toolbar (LogViewToolbar.cpp)
	enum class Tool { None, FilterCase, FilterRegex, FilterExclude, SearchCase, SearchWord, SearchRegex,
		LevelError, LevelWarning, LevelInfo, LevelDebug, Follow, Count };
	int     ToolbarHeight() const;
	void    ToolLayout(RECT* tools, RECT* boxes) const;   // tools[Tool::Count], boxes[2] (filter, search)
	RECT    GetToolRect(Tool tool) const;             // the control's client coords
	RECT    GetBoxRect(bool search) const;            // a box's frame
	static DWORD LevelBits(Tool tool);                // a level button's levels (1 << level)
	Tool    ToolHitTest(POINT pt) const;
	bool    IsToolOn(Tool tool) const;
	void    CreateToolbar();
	void    PositionToolbar();
	void    PaintToolbar(WTL::CDCHandle dc, const LGVCOLORS& c);
	void    InvalidateToolbar();
	void    PressTool(Tool tool);
	void    ApplyToolbarFilter(bool notify);
	void    ApplyToolbarSearch(bool notify);
	void    SyncToolbar(bool filter, bool search);    // the boxes show the filter and search in effect
	void    FocusBox(bool search);
	static LRESULT CALLBACK BoxSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// layout and scrolling
	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Layout();
	RECT    GetTextRect() const;                      // the text area's client rect
	int     GutterWidth() const;
	int     GetPageLines() const;
	size_t  MaxTopRow() const;
	int     MaxLeft() const;
	void    UpdateScrollBars();
	void    ScrollTo(size_t topLine, int left, bool byUser);
	void    SetFollow(bool follow, bool notify);

	// caret and selection (LogViewSelection.cpp)
	enum class Drag { None, Text, Gutter };
	size_t  LineLength(size_t line) const;
	LGVPOS  ClampPos(LGVPOS pos) const;
	LGVPOS  LineStartOrNext(size_t line, bool next) const; // (line, 0), or with next the start of the line after
	int     IndexToX(size_t line, size_t index) const;   // from the text's start (no gutter, no scrolling)
	size_t  XToIndex(size_t line, int x) const;
	LGVPOS  PosFromPoint(POINT pt) const;             // text window coords; clamped to the lines
	int     TextX(std::wstring_view text, UINT flags, size_t index) const;   // IndexToX for a line's text
	size_t  TextIndexAt(std::wstring_view text, UINT flags, int x) const;  // XToIndex for a line's text
	void    Select(LGVPOS anchor, LGVPOS caret, bool notify, bool keepDesiredX = false);
	void    MoveCaret(LGVPOS pos, bool extend, bool notify, bool keepDesiredX = false);
	void    EnsureCaretVisible(bool byUser);
	void    CreateSystemCaret();                      // on focus, and again when the line height changes
	void    UpdateCaret();                            // the system caret's place, after scrolling or moving
	void    MovePositionsUp(size_t dropped);          // lines dropped by the limit
	bool    SelectionInLine(size_t line, size_t& start, size_t& end, bool& lineBreak) const;
	void    DragTo(POINT pt);
	void    HandleCaretKey(WPARAM key, bool ctrl, bool shift);
	LGVPOS  WordLeft(LGVPOS pos) const;
	LGVPOS  WordRight(LGVPOS pos) const;

	// accessibility (LogViewAccessible.cpp)
	void    DisconnectAccessible();
	void    NotifyCaretAccessible();                  // the caret's line is the focused item

	// painting
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintLine(WTL::CDCHandle dc, size_t line, const RECT& rc, const LGVCOLORS& c, int gutter);
	LGVSTYLE EffectiveStyle(BYTE level, BYTE style) const;
	HFONT   FontFor(UINT flags) const;

	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    NotifyLine(UINT code, size_t line, POINT ptScreen);

	std::unique_ptr<LgStore>      m_Store;
	std::unique_ptr<LgTextWindow> m_Text;
	bool                          m_Virtual = false;
	size_t                        m_VirtualCount = 0;
	std::atomic<DWORD>            m_ThreadId = 0;    // the window's, once created; read by appending threads

	// lines from other threads
	SRWLOCK                       m_PendingLock = SRWLOCK_INIT;
	std::unique_ptr<LgBatch>      m_Pending;
	std::atomic<bool>             m_FlushPosted = false;
	bool                          m_LinesChanged = false;   // appended on the window's thread, not yet laid out

	// view state
	size_t                        m_TopRow = 0;
	int                           m_Left = 0;        // horizontal scroll, pixels
	bool                          m_Follow = true;
	bool                          m_LineNumbers = true;
	int                           m_TabSize = 4;
	int                           m_UpdateCount = 0;
	int                           m_WheelRemainder = 0;
	int                           m_HWheelRemainder = 0;

	// filter and rules
	std::unique_ptr<LgFilter>     m_Filter;
	std::unique_ptr<LgRowMap>     m_Rows;
	std::vector<LgRule>           m_Rules;
	ULONGLONG                     m_ScanSerial = 0;  // the next line to filter
	bool                          m_FilterDonePending = false;
	bool                          m_CaretAfterFilter = false;   // scroll to the caret when the pass is done

	// search
	struct SearchState {
		bool      active = false;
		bool      forward = true;
		bool      byUser = false;   // F3: the selection change is notified
		bool      wrapped = false;
		bool      firstVisit = true;// on the start line, the first time
		ULONGLONG startSerial = 0;  // the line it started on, and the column
		size_t    startColumn = 0;
		ULONGLONG serial = 0;       // the line to look at next
		size_t    remaining = 0;    // lines left to look at
	};
	std::unique_ptr<LgMatcher>    m_Search;
	SearchState                   m_SearchState;

	// caret and selection
	LGVPOS                        m_Anchor{};
	LGVPOS                        m_Caret{};
	int                           m_DesiredX = -1;   // Up/Down keep to it; -1: the caret's own
	Drag                          m_Drag = Drag::None;
	size_t                        m_DragLine = 0;    // gutter drag: the line it started on
	POINT                         m_DragPoint{};     // the mouse, for scrolling while dragging outside
	bool                          m_HasCaret = false;
	bool                          m_CaretShown = false;

	// metrics (pixels at the current DPI)
	UINT                          m_Dpi = 96;
	int                           m_LineHeight = 16;
	int                           m_TextOffset = 0;  // text top in a line
	int                           m_CharWidth = 8;   // average; exact for a fixed-pitch font
	int                           m_DigitWidth = 8;
	bool                          m_FixedPitch = true;
	int                           m_Pad = 4;
	int                           m_MarkerWidth = 3;

	// fonts, colors, styles
	HFONT                         m_hFont = nullptr; // in effect (owned if m_OwnFont)
	bool                          m_OwnFont = true;
	WTL::CFont                    m_Fonts[3];        // bold, italic, bold italic
	bool                          m_SystemColors = true;
	LGVCOLORS                     m_Colors{};
	LGVSTYLE                      m_Styles[256];     // [0] unused
	LGVSTYLE                      m_LevelStyles[32];
	std::wstring                  m_DrawBuffer;      // a line with its tabs expanded
	mutable std::wstring          m_VirtualText;     // the last line the parent supplied

	// toolbar
	bool                          m_ShowToolbar = false;
	HWND                          m_hFilterBox = nullptr;
	HWND                          m_hSearchBox = nullptr;
	HWND                          m_hToolTip = nullptr;
	WTL::CFont                    m_UiFont;          // the toolbar's (the message font at the DPI)
	int                           m_UiTextHeight = 16;
	UINT                          m_FilterBoxFlags = 0;   // the boxes' options, kept when their regex is bad
	UINT                          m_SearchBoxFlags = 0;
	bool                          m_FilterBad = false;    // the box's regex doesn't compile: a red frame
	bool                          m_SearchBad = false;
	bool                          m_SettingBox = false;   // the toolbar sets a box's text: no EN_CHANGE round trip
	Tool                          m_HotTool = Tool::None;
	bool                          m_TrackingLeave = false;
	WTL::CBrush                   m_BoxBrush;        // WM_CTLCOLOREDIT
	LgAccessible*                 m_Accessible = nullptr;   // made on the first WM_GETOBJECT; cut off in OnDestroy
	COLORREF                      m_BoxBrushColor = CLR_INVALID;
	size_t                        m_SeenColumns = 0; // virtual mode: the widest line painted so far
};
