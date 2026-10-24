#pragma once

// Diff view: compares two texts line by line and shows them side by side (a change's lines next to each other,
// padded where one side has fewer) or unified (a change's old lines, then its new ones), with the lines' own
// numbers, the changed words of changed lines marked, and runs of unchanged lines folded to a few lines of
// context. The control computes the diff itself; the app gives it the two texts. The API follows the common
// controls: C-style structures and WM_NOTIFY codes (DFVN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_DIFFVIEW[] = L"WC_DiffView";

constexpr size_t DFV_NOLINE = (size_t)-1;

// ---- Layout and options --------------------------------------------------------

enum DFVLAYOUT {
	DFVL_SIDEBYSIDE,     // the left text in the left pane, the right text in the right pane
	DFVL_UNIFIED,        // one pane: each change's left lines (-), then its right lines (+)
};

#define DFVF_IGNORECASE          0x0001
#define DFVF_IGNORESPACECHANGE   0x0002   // a run of whitespace equals any other; leading and trailing don't count
#define DFVF_IGNORESPACE         0x0004   // whitespace doesn't count (wins over DFVF_IGNORESPACECHANGE)
#define DFVF_NOINTRALINE         0x0008   // don't mark the changed words of changed lines

#define DFV_ALLCONTEXT           (-1)     // SetContextLines: show every unchanged line

// ---- Information -----------------------------------------------------------------

struct DFVINFO {
	size_t leftLines;
	size_t rightLines;
	size_t changes;            // runs of changed lines
	size_t deleted;            // left lines in changes
	size_t inserted;           // right lines in changes
};

struct DFVCHANGE {             // a run of changed lines: deleted from the left, inserted on the right
	size_t leftLine;           // 0-based; where the lines would be when the count is 0
	size_t leftCount;
	size_t rightLine;
	size_t rightCount;
};

// ---- Colors ----------------------------------------------------------------------

struct DFVCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crGutterBack;
	COLORREF crGutterText;
	COLORREF crDeletedBack;    // a changed line on the left (or a '-' line)
	COLORREF crDeletedMark;    // its changed words, and the gutter's marker
	COLORREF crInsertedBack;   // a changed line on the right (or a '+' line)
	COLORREF crInsertedMark;
	COLORREF crFiller;         // where a side has no line (side by side); hatched over crBack
	COLORREF crFoldBack;       // a row of folded unchanged lines
	COLORREF crFoldText;
	COLORREF crTitleBack;      // the titles above the panes
	COLORREF crTitleText;
	COLORREF crSelBack;
	COLORREF crSelText;
	COLORREF crSelBackInactive;
	COLORREF crSelTextInactive;
};

// ---- Positions ----------------------------------------------------------------
//
// A place between characters of a line of one side: column is a character index in the line (a tab is one
// character), 0..length. Side by side a selection is in one pane: both its ends are on the same side. Unified, an
// unchanged line is on the left.

struct DFVPOS {
	BOOL   right;
	size_t line;
	size_t column;
};

// ---- Hit testing ----------------------------------------------------------------

#define DFVHT_NOWHERE   0
#define DFVHT_TITLE     1
#define DFVHT_GUTTER    2   // pos: the line, column 0
#define DFVHT_TEXT      3   // pos: the character boundary nearest to the point (also past the end of the line)
#define DFVHT_FILLER    4   // where the side has no line; pos: the nearest line of the side
#define DFVHT_FOLD      5   // pos: the first hidden line (of the left side, unified)
#define DFVHT_OVERVIEW  6   // the strip beside the scroll bar; row: the row there
#define DFVHT_BELOW     7   // under the last row

struct DFVHITTEST {
	POINT  pt;                 // in: client coords
	UINT   part;               // out: DFVHT_*
	size_t row;                // the row, if on one
	DFVPOS pos;
};

// ---- Notifications -------------------------------------------------------------

#define DFVN_FIRST          (0U - 3000U)   // outside the common controls ranges
#define DFVN_SELCHANGED     (DFVN_FIRST - 0)   // NMHDR; the user moved the caret or changed the selection
#define DFVN_CONTEXTMENU    (DFVN_FIRST - 1)   // NMDFVCONTEXTMENU
#define DFVN_FOLDEXPANDED   (DFVN_FIRST - 2)   // NMHDR; the user opened a fold (a click, or Enter next to it)

struct NMDFVCONTEXTMENU {
	NMHDR  hdr;
	POINT  pt;                 // screen coords; from the keyboard, below the caret
	UINT   part;               // DFVHT_* where it was asked for (the caret's: DFVHT_TEXT)
	DFVPOS pos;
};

// ---- Control class --------------------------------------------------------------------------

struct DfDocument;
struct DfSpan;

class CDiffViewCtrl : public ATL::CWindowImpl<CDiffViewCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_DiffView", CS_DBLCLKS, -1)

	CDiffViewCtrl();
	~CDiffViewCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CDiffViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Texts ------------------------------------------------------------------------
	//
	// The texts are split at "\r\n", "\n" and "\r"; a final line break doesn't start another line, and a last
	// line without one differs from the same line with one. Setting either text compares them again (now, on the
	// calling thread) and keeps the view near the same lines. cch -1: null-terminated.

	void    SetTexts(LPCWSTR left, LPCWSTR right, int cchLeft = -1, int cchRight = -1);
	void    SetText(bool right, LPCWSTR text, int cch = -1);
	ATL::CString GetText(bool right) const;           // lines joined with CR LF
	size_t  GetLineCount(bool right) const;
	ATL::CString GetLineText(bool right, size_t line) const;
	void    Clear();                                  // both texts empty
	// shown above the panes (side by side) or as "left -> right" (unified); both nullptr or "": no titles
	void    SetTitles(LPCWSTR left, LPCWSTR right);

	// ---- The diff ----------------------------------------------------------------------

	void    SetOptions(UINT flags);                   // DFVF_*; compares again
	UINT    GetOptions() const;
	void    GetInfo(DFVINFO* pInfo) const;
	size_t  GetChangeCount() const;
	BOOL    GetChange(size_t index, DFVCHANGE* pChange) const;
	// the change holding a line of one side; -1 if the line is unchanged
	int     GetChangeOfLine(bool right, size_t line) const;
	// the diff in the unified format, with context lines around each change (names: the "---" and "+++" lines)
	ATL::CString GetUnifiedDiff(int context = 3, LPCWSTR leftName = L"a", LPCWSTR rightName = L"b") const;

	// ---- View --------------------------------------------------------------------------
	//
	// The view is in rows: a row shows a line of each side (side by side), one line (unified), or a fold of
	// unchanged lines.

	void    SetLayout(DFVLAYOUT layout);
	DFVLAYOUT GetLayout() const;
	void    SetContextLines(int lines);               // the unchanged lines kept next to a change; DFV_ALLCONTEXT
	int     GetContextLines() const;
	void    ShowLineNumbers(bool show);
	bool    AreLineNumbersShown() const;
	void    SetTabSize(int columns);                  // 1..16, default 4
	int     GetTabSize() const;
	size_t  GetRowCount() const;
	void    SetTopRow(size_t row);
	size_t  GetTopRow() const;
	int     GetVisibleRowCount() const;               // whole rows that fit
	void    EnsureLineVisible(bool right, size_t line);   // a folded line shows its fold
	void    EnsureChangeVisible(size_t index);        // as much of the change as fits, its start first
	void    ExpandAll();                              // opens every fold (until the context lines are set again)
	// the overview beside the scroll bar: every change's place in the whole, and the part in view
	void    ShowOverview(bool show);
	bool    IsOverviewShown() const;
	BOOL    HitTest(DFVHITTEST* pHit) const;

	// ---- Caret and selection ----------------------------------------------------------
	//
	// The selection runs from the anchor to the caret. Clicking places the caret, dragging or Shift selects, the
	// gutter selects whole lines, a double click a word; side by side, the selection stays in the pane it started
	// in. Keys: arrows, Home/End, Page Up/Down, Ctrl+Home/End, with Ctrl moving by words and Shift extending;
	// Ctrl+Up/Down scroll without moving the caret; F6 moves to the other pane; F7 and Shift+F7 go to the
	// next and previous change; Enter opens the fold next to the caret; Ctrl+A selects all (the
	// pane's text, side by side); Ctrl+C or Ctrl+Insert copies (the caret's line when nothing is selected). Setting
	// the texts keeps the positions' lines and columns, as far as they go.

	DFVPOS  GetCaret() const;
	void    SetCaret(DFVPOS pos, bool extend = false);   // scrolls it into view; no notification
	void    GetSelection(DFVPOS* pStart, DFVPOS* pEnd) const;   // in order; equal if nothing is selected
	void    SetSelection(DFVPOS anchor, DFVPOS caret);   // the caret is scrolled into view; no notification
	void    SelectAll();
	bool    HasSelection() const;
	ATL::CString GetSelectedText() const;             // lines joined with CR LF
	BOOL    Copy();                                   // as Ctrl+C
	int     GetCurrentChange() const;                 // the change the caret is in; -1 if none
	BOOL    GoToChange(size_t index);                 // the caret to the change's first line, scrolled into view
	BOOL    NextChange(bool forward = true);          // from the caret, as F7 and Shift+F7; FALSE if there's none

	// ---- Appearance -------------------------------------------------------------------
	//
	// The default font is Consolas, 10 points at the window's DPI; WM_SETFONT replaces it (the app then
	// replaces it for a new DPI).

	void    SetColors(const DFVCOLORS* colors);       // nullptr: from the system colors (follows WTLHelper dark mode)
	DFVCOLORS GetColors() const;                      // the colors in effect

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	struct Anchor {            // a line kept in view across a new diff or layout
		bool   right = false;
		size_t line = DFV_NOLINE;
	};

	struct Pane {              // where a pane's parts are, in client coords
		int  left = 0;         // the gutter's left edge
		int  text = 0;         // the text's left edge (the gutter's right)
		int  right = 0;
	};

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Recompute();                              // the diff and the rows, keeping the view near its lines
	void    RebuildRows();                            // the rows, keeping the view near its lines
	Anchor  GetAnchor() const;
	void    SetAnchor(const Anchor& anchor);
	void    UpdateScrollBars();
	void    UpdateScrollBarTheme();
	void    ScrollTo(size_t topRow, int left);
	RECT    GetRowsRect() const;                      // below the titles, left of the overview
	RECT    GetOverviewRect() const;
	int     OverviewWidth() const;
	void    PaintOverview(WTL::CDCHandle dc, const DFVCOLORS& c);
	void    ScrollToOverview(int y);                  // centers the row at y of the overview
	void    ExpandFold(size_t row, bool notify);
	void    BuildRows();                              // the document's rows for the layout and context
	int     TitleHeight() const;
	int     GutterWidth() const;
	int     PageRows() const;
	size_t  MaxTopRow() const;
	int     TextWidth() const;                        // a pane's text area
	int     MaxLeft() const;
	Pane    GetPane(int index) const;                 // side by side: 0 left, 1 right; unified: 0
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintTitles(WTL::CDCHandle dc, const DFVCOLORS& c);
	void    PaintRow(WTL::CDCHandle dc, size_t row, int y, const DFVCOLORS& c, int right);
	void    PaintText(WTL::CDCHandle dc, const RECT& rc, std::wstring_view line, const std::vector<DfSpan>* spans, COLORREF text, COLORREF markBack,
		size_t row, bool rightSide, const DFVCOLORS& c);
	void    PaintNumber(WTL::CDCHandle dc, int right, int y, int line, COLORREF color);
	void    FillHatch(WTL::CDCHandle dc, const RECT& rc, COLORREF back, COLORREF hatch);
	bool    SelectionIn(size_t row, bool rightSide, size_t length, size_t& from, size_t& to, bool& lineBreak) const;
	int     ColumnToX(WTL::CDCHandle dc, std::wstring_view line, size_t index) const;   // from the text's origin

	// caret and selection (DiffViewSelection.cpp)
	void    MoveCaret(DFVPOS pos, bool extend, bool notify);
	void    UpdateCaret();
	void    CreateSystemCaret();
	void    EnsureCaretVisible();
	void    ClampPositions();
	void    FireSelChanged();
	DFVPOS  PosFromPoint(POINT pt, bool rightSide, UINT* pPart) const;
	bool    PaneSideAt(int x) const;                  // side by side: the pane x is over (the center strip: the nearer)
	int     CaretRow() const;
	size_t  IndexAtX(bool rightSide, size_t line, int x) const;   // x from the text's origin
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    DragTo(POINT pt);
	void    MoveByKey(UINT vk, bool shift, bool ctrl);

	std::unique_ptr<DfDocument> m_Doc;
	DFVLAYOUT m_Layout = DFVL_SIDEBYSIDE;
	UINT    m_Options = 0;
	int     m_Context = DFV_ALLCONTEXT;
	ATL::CString m_Titles[2];

	DFVCOLORS m_Colors{};
	bool    m_SystemColors = true;
	int     m_DarkScrollBars = -1;                    // the theme set on the scroll bars: -1 none yet

	HFONT   m_hFont = nullptr;
	bool    m_OwnFont = true;
	WTL::CFont m_UiFont;                              // the titles'
	UINT    m_Dpi = 96;
	int     m_RowHeight = 16;
	int     m_TextOffset = 0;
	int     m_CharWidth = 8;
	int     m_DigitWidth = 8;
	bool    m_FixedPitch = true;
	int     m_Pad = 4;
	int     m_MarkerWidth = 3;
	int     m_UiTextHeight = 16;
	bool    m_LineNumbers = true;
	int     m_TabSize = 4;

	size_t  m_TopRow = 0;
	int     m_Left = 0;                               // pixels, both panes
	int     m_WheelRemainder = 0;
	int     m_HWheelRemainder = 0;
	std::wstring m_DrawBuffer;
	mutable std::wstring m_MeasureBuffer;

	DFVPOS  m_Anchor{};
	DFVPOS  m_Caret{};
	int     m_DesiredX = -1;                          // up and down keep the caret near this x (pixels), -1: none
	bool    m_HasCaret = false;
	bool    m_ShowOverview = true;
	enum class Drag { None, Select, Words, Lines, Overview } m_Drag = Drag::None;
	DFVPOS  m_DragStart{};                            // a word or line drag: the first word or line
	DFVPOS  m_DragEnd{};
	POINT   m_DragPoint{};
	int     m_SelRows[2] = { -1, -1 };                // unified, while painting: the selection's first and last rows
};
