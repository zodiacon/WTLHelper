#pragma once

// DataGrid: a grid of typed columns over rows that are either kept by the control (the store, the default) or asked
// of the app as they're painted (virtual mode: SetVirtual, SetRowCount, DGVN_GETDISPINFO), for millions of rows. The
// header is drawn by the control: columns are resized by their dividers (a double-click fits the column to its title
// and the rows in view), dragged to another place, hidden, given a share of the spare width (fill) and frozen at the
// left, where they don't scroll. A column's type formats its values (numbers, dates, enums) and draws them (check
// boxes, progress bars, an image and text, links). The caret is a cell, moved with the keys as in Excel. Rows scroll
// whole, so any number of them fit the scroll bar. The API follows the common controls: C-style structures and
// WM_NOTIFY codes (DGVN_*) sent to the parent. Columns are known by IDs the control gives them, which don't change
// when they move; rows by their index in the data, which stays the same however they're shown. Cells can be edited,
// pasted and undone; the store's rows filtered, sorted and grouped (a worker thread rebuilds big ones); cells found
// (Ctrl+F), colored by conditional formats or the app's custom draw, and rows added in a new row at the end. The
// layout, sort, groups and filters save as a line of text; screen readers see a table of rows of cells (MSAA).

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>
#include <atltypes.h>
#include <commctrl.h>

#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_DATAGRID[] = L"WC_DataGrid";

// ---- Styles --------------------------------------------------------------------

#define DGVS_NOHEADER          0x0001   // no column header
#define DGVS_ROWHEADER         0x0002   // a column of row numbers at the left
#define DGVS_NOHORZLINES       0x0004   // no lines between rows
#define DGVS_NOVERTLINES       0x0008   // no lines between columns
#define DGVS_NOCOLUMNRESIZE    0x0010   // the user can't resize columns
#define DGVS_NOCOLUMNREORDER   0x0020   // the user can't drag columns to another place
#define DGVS_NOCARETROW        0x0040   // the caret's row isn't shaded
#define DGVS_NOSORT            0x0080   // a click on a title doesn't sort (DGVN_COLUMNCLICK is still sent)
#define DGVS_EDITABLE          0x0100   // the user can edit cells (F2, typing, a double-click), paste, clear and undo
#define DGVS_FILTERROW         0x0200   // a row under the titles to type each column's filter in
#define DGVS_HEADERMENU        0x0400   // a button on each title for a menu: sort, group, filter by its values
#define DGVS_NEWROW            0x0800   // with DGVS_EDITABLE: an empty row after the last, where typing adds a row
#define DGVS_NOFINDBOX         0x1000   // Ctrl+F doesn't open the find box (Find and F3 still work)
#define DGVS_USERROWS          0x2000   // with DGVS_EDITABLE, the store: Ctrl+Shift++ inserts a row, Delete on whole rows deletes them
#define DGVS_NOTOOLTIPS        0x4000   // no tooltip with the whole text of a cell or a title that's cut off

// The new row's placeholder (DGVS_NEWROW) where a data row is given: the caret, hit testing, GetDataRow, editing.
#define DGV_NEWROW             (-2)

// ---- Values ---------------------------------------------------------------------

#define DGVVT_EMPTY      0
#define DGVVT_TEXT       1
#define DGVVT_INT        2
#define DGVVT_FLOAT      3
#define DGVVT_BOOL       4
#define DGVVT_DATETIME   5   // a FILETIME, in local time (it's shown as it is)

struct DGVVALUE {
	int type;               // DGVVT_*
	union {
		LONGLONG llVal;
		double dblVal;
		BOOL bVal;
		FILETIME ftVal;
	};
	LPCWSTR pszText;        // DGVVT_TEXT; given out by the control: valid until the cell changes
};

inline DGVVALUE DgvEmpty() {
	return DGVVALUE{};
}
inline DGVVALUE DgvText(LPCWSTR text) {
	DGVVALUE v{};
	v.type = DGVVT_TEXT;
	v.pszText = text;
	return v;
}
inline DGVVALUE DgvInt(LONGLONG value) {
	DGVVALUE v{};
	v.type = DGVVT_INT;
	v.llVal = value;
	return v;
}
inline DGVVALUE DgvFloat(double value) {
	DGVVALUE v{};
	v.type = DGVVT_FLOAT;
	v.dblVal = value;
	return v;
}
inline DGVVALUE DgvBool(bool value) {
	DGVVALUE v{};
	v.type = DGVVT_BOOL;
	v.bVal = value;
	return v;
}
inline DGVVALUE DgvDateTime(const FILETIME& value) {
	DGVVALUE v{};
	v.type = DGVVT_DATETIME;
	v.ftVal = value;
	return v;
}

// ---- Columns ----------------------------------------------------------------------

#define DGVCT_TEXT        0   // any value, as text
#define DGVCT_INTEGER     1   // whole numbers, at the right
#define DGVCT_FLOAT       2   // numbers with DGVCOLUMN::decimals digits after the point, at the right
#define DGVCT_BOOL        3   // a check box
#define DGVCT_DATETIME    4   // the user's short date and time
#define DGVCT_ENUM        5   // an integer naming one of DGVCOLUMN::pszEnumItems
#define DGVCT_PROGRESS    6   // a bar: an integer 0-100 or a float 0-1
#define DGVCT_IMAGETEXT   7   // the cell's image (from SetImageList) before its text
#define DGVCT_LINK        8   // text drawn as a link; a click sends DGVN_LINKCLICK

#define DGVA_DEFAULT      0   // by the type: numbers at the right, check boxes and bars centered, the rest at the left
#define DGVA_LEFT         1
#define DGVA_CENTER       2
#define DGVA_RIGHT        3

#define DGVCF_HIDDEN      0x0001   // not shown
#define DGVCF_NORESIZE    0x0002   // the user can't resize it
#define DGVCF_NOREORDER   0x0004   // the user can't drag it
#define DGVCF_GROUPDIGITS 0x0008   // numbers with the user's thousands separator
#define DGVCF_NOSORT      0x0010   // a click on its title doesn't sort
#define DGVCF_READONLY    0x0020   // its cells can't be edited (DGVS_EDITABLE)
#define DGVCF_NOHIDE      0x0040   // the user can't hide it (the columns' menu)

#define DGVCM_TITLE       0x0001
#define DGVCM_TYPE        0x0002
#define DGVCM_WIDTH       0x0004
#define DGVCM_MINWIDTH    0x0008
#define DGVCM_FILL        0x0010
#define DGVCM_ALIGN       0x0020
#define DGVCM_DECIMALS    0x0040
#define DGVCM_FLAGS       0x0080
#define DGVCM_ENUMITEMS   0x0100
#define DGVCM_PARAM       0x0200
#define DGVCM_AGGREGATE   0x0400
#define DGVCM_ALL         0x07FF

struct DGVCOLUMN {
	UINT mask;               // DGVCM_*: the members that count (InsertColumn: the rest take their defaults)
	LPCWSTR pszTitle;
	int type;                // DGVCT_*
	int cxWidth;             // DIPs (default 100); a fill column's is what it got last
	int cxMin;               // DIPs (default 24): the narrowest the user can make it, and a fill column gets
	int fill;                // > 0: the column takes this share of the width the fixed ones leave
	int align;               // DGVA_*
	int decimals;            // DGVCT_FLOAT (default 2)
	UINT flags;              // DGVCF_*
	LPCWSTR pszEnumItems;    // DGVCT_ENUM: the names, each ending with a 0, the list with another ("Low\0High\0")
	LPARAM lParam;
	int aggregate;           // DGVAGG_*: what it shows on group rows
};

#define DGVAGG_NONE       0
#define DGVAGG_SUM        1   // numbers
#define DGVAGG_AVG        2
#define DGVAGG_MIN        3   // numbers or dates
#define DGVAGG_MAX        4
#define DGVAGG_COUNT      5   // cells that aren't empty

// ---- Filters ------------------------------------------------------------------------

// The filter row parses what's typed: ">100", ">=", "<", "<=" and "a..b" on numbers and dates are ranges (a date with
// no time is its whole day), "=x" the cells equal to x (case ignored), anything else "contains".
#define DGVFK_CONTAINS    0   // pszText anywhere in the cell's shown text, case ignored
#define DGVFK_RANGE       1   // from minValue to maxValue, both included (DGVVT_EMPTY: open); numbers or dates
#define DGVFK_VALUES      2   // one of ppszValues (shown texts), or empty when fBlanks

struct DGVFILTER {
	int idColumn;
	int kind;                // DGVFK_*
	LPCWSTR pszText;         // contains: the text; else what the user typed (the filter row shows it), if anything:
	                         // a value filter with a text ignores case

	DGVVALUE minValue;
	DGVVALUE maxValue;
	const LPCWSTR* ppszValues;
	int cValues;
	BOOL fBlanks;
};

// ---- Hit testing ------------------------------------------------------------------

#define DGVHT_NOWHERE        0x0000
#define DGVHT_CELL           0x0001
#define DGVHT_HEADER         0x0002   // a column's title
#define DGVHT_HEADERDIVIDER  0x0004   // the right edge of idColumn's title, where it's resized
#define DGVHT_ROWHEADER      0x0008   // a row's number
#define DGVHT_CORNER         0x0010   // above the row numbers
#define DGVHT_EMPTY          0x0020   // past the last row or column
#define DGVHT_FILTERROW      0x0040   // a column's cell in the filter row
#define DGVHT_GROUP          0x0080   // a group's row (iRow -1, iViewRow says which)
#define DGVHT_HEADERBUTTON   0x0100   // the menu button on a title (DGVS_HEADERMENU)

struct DGVHITTEST {
	POINT pt;               // in: client coordinates
	UINT flags;             // out: DGVHT_*
	LONGLONG iRow;          // -1 if none
	int idColumn;           // 0 if none
	LONGLONG iViewRow;      // the row in view (-1 if none): a group's row has no data row
};

// ---- Colors ------------------------------------------------------------------------

struct DGVCOLORS {          // CLR_DEFAULT: the system's (which WTLHelper's dark mode changes)
	COLORREF crBack;
	COLORREF crText;
	COLORREF crGrid;
	COLORREF crHeaderBack;
	COLORREF crHeaderText;
	COLORREF crCaretRow;     // the caret's row's shading
	COLORREF crCaret;        // the frame around the caret's cell
	COLORREF crLink;
	COLORREF crSelection;    // the selected cells' background (lighter when the control hasn't the focus)
};

// ---- Selection ---------------------------------------------------------------------

#define DGVSM_CELLS          0   // rectangles of cells (Shift extends, Ctrl adds another)
#define DGVSM_ROWS           1   // whole rows (Ctrl+click toggles one)
#define DGVSM_SINGLE         2   // the caret's cell only

// Rows in view (as sorted; GetDataRow maps them) and visible columns (GetVisibleColumn), both ends included. A
// selection is kept in these terms, so sorting, moving or hiding columns puts it back on the caret's cell.
struct DGVRANGE {
	LONGLONG iTopRow;
	LONGLONG iBottomRow;
	int iLeftColumn;
	int iRightColumn;
};

#define DGVCOPY_TITLES       0x0001   // the columns' titles in a first row

// ---- Sorting ------------------------------------------------------------------------

struct DGVSORTKEY {
	int idColumn;
	BOOL fDescending;
};

// ---- Notifications ----------------------------------------------------------------

#define DGVN_FIRST           (0U - 3500U)   // outside the common controls ranges
#define DGVN_GETDISPINFO     (DGVN_FIRST - 0)   // NMDGVDISPINFO; virtual mode: the app fills in the cell's value
#define DGVN_CARETCHANGED    (DGVN_FIRST - 1)   // NMDGVCARET
#define DGVN_COLUMNCLICK     (DGVN_FIRST - 2)   // NMDGVCOLUMN; a title was clicked: non-zero stops it sorting
#define DGVN_COLUMNRESIZED   (DGVN_FIRST - 3)   // NMDGVCOLUMN; the user resized a column (cxWidth: its new width)
#define DGVN_COLUMNMOVED     (DGVN_FIRST - 4)   // NMDGVCOLUMN; the user dragged a column (iOldIndex to iDisplayIndex)
#define DGVN_ACTIVATE        (DGVN_FIRST - 5)   // NMDGVCELL; a double-click or Enter on a cell
#define DGVN_CONTEXTMENU     (DGVN_FIRST - 6)   // NMDGVCELL; a right-click or the menu key (ptScreen: where to show it)
#define DGVN_LINKCLICK       (DGVN_FIRST - 7)   // NMDGVCELL; a DGVCT_LINK cell's text was clicked
#define DGVN_SELCHANGED      (DGVN_FIRST - 8)   // NMHDR; the user changed the selection
#define DGVN_SORTCHANGING    (DGVN_FIRST - 9)   // NMDGVSORT; the user is sorting: non-zero stops it
#define DGVN_SORTCHANGED     (DGVN_FIRST - 10)  // NMDGVSORT; virtual mode: the app sorts its rows and calls RedrawRows

struct NMDGVSORT {
	NMHDR hdr;
	int cKeys;               // 0: unsorted
	const DGVSORTKEY* pKeys; // the first is the main one
};

// Filtering and grouping: the store's rows are filtered, sorted and grouped together, on a worker thread past
// 100,000 rows (the last rows in view stay until DGVN_VIEWREADY). Virtual mode keeps and shows the filters (the app
// filters its rows on DGVN_FILTERCHANGED) and doesn't group.
#define DGVN_FILTERCHANGING  (DGVN_FIRST - 16)  // NMDGVFILTER; the user is filtering: non-zero stops it
#define DGVN_FILTERCHANGED   (DGVN_FIRST - 17)  // NMDGVFILTER
#define DGVN_GROUPCHANGED    (DGVN_FIRST - 18)  // NMHDR; the user grouped by other columns
#define DGVN_GROUPEXPANDED   (DGVN_FIRST - 19)  // NMDGVGROUP; the user opened or closed a group
#define DGVN_VIEWREADY       (DGVN_FIRST - 20)  // NMHDR; the rows rebuilt on the worker thread are shown

struct NMDGVFILTER {
	NMHDR hdr;
	int idColumn;            // 0: all of them (cleared)
	const DGVFILTER* pFilter;   // nullptr: cleared
};

struct NMDGVGROUP {
	NMHDR hdr;
	LONGLONG iViewRow;
	int idColumn;
	BOOL fExpanded;
};

// Editing (DGVS_EDITABLE): the user's changes to cells go through DGVN_VALIDATE (each cell), are written (the store,
// or DGVN_SETCELL in virtual mode) and announced with DGVN_CELLSCHANGED (each change, with its reason).
#define DGVN_BEGINEDIT       (DGVN_FIRST - 11)  // NMDGVEDIT; an editor opens: non-zero stops it
#define DGVN_ENDEDIT         (DGVN_FIRST - 12)  // NMDGVEDIT; it closed (fCanceled: nothing was written)
#define DGVN_VALIDATE        (DGVN_FIRST - 13)  // NMDGVVALIDATE; non-zero rejects the value (szError says why)
#define DGVN_SETCELL         (DGVN_FIRST - 14)  // NMDGVSETCELL; virtual mode: the app keeps the cell's new value
#define DGVN_CELLSCHANGED    (DGVN_FIRST - 15)  // NMDGVCHANGE; the user changed cells

#define DGVCR_EDIT           0   // why cells changed: an editor
#define DGVCR_PASTE          1
#define DGVCR_CLEAR          2   // Delete, or Cut
#define DGVCR_TOGGLE         3   // a check box
#define DGVCR_UNDO           4
#define DGVCR_REDO           5
#define DGVCR_INSERTROWS     6   // the user inserted a row (DGVS_USERROWS)
#define DGVCR_DELETEROWS     7   // the user deleted rows

struct NMDGVEDIT {
	NMHDR hdr;
	LONGLONG iRow;
	int idColumn;
	BOOL fCanceled;          // DGVN_ENDEDIT
};

struct NMDGVVALIDATE {
	NMHDR hdr;
	LONGLONG iRow;
	int idColumn;
	UINT reason;             // DGVCR_EDIT, DGVCR_PASTE, DGVCR_CLEAR or DGVCR_TOGGLE
	DGVVALUE value;          // in, parsed from the text; out: the app may change it (its text is copied)
	LPCWSTR pszText;         // what was typed or pasted
	WCHAR szError[256];      // out, when rejected
};

struct NMDGVSETCELL {
	NMHDR hdr;
	LONGLONG iRow;
	int idColumn;
	DGVVALUE value;
	int iImage;
};

struct NMDGVCHANGE {
	NMHDR hdr;
	UINT reason;             // DGVCR_*
	int cCells;
	LONGLONG iFirstRow;      // the first cell changed
	int idFirstColumn;
};

struct NMDGVDISPINFO {
	NMHDR hdr;
	LONGLONG iRow;           // the data row
	int idColumn;
	DGVVALUE value;          // out; the text is copied when the notification returns
	int iImage;              // out (-1: none); DGVCT_IMAGETEXT
};

struct NMDGVCARET {
	NMHDR hdr;
	LONGLONG iRow;
	int idColumn;
	LONGLONG iOldRow;        // -1: there was no caret
	int idOldColumn;
};

struct NMDGVCOLUMN {
	NMHDR hdr;
	int idColumn;
	int iDisplayIndex;       // among all the columns, hidden ones too
	int iOldIndex;           // DGVN_COLUMNMOVED
	int cxWidth;             // DIPs
};

struct NMDGVCELL {
	NMHDR hdr;
	UINT flags;              // DGVHT_* where it happened (the keyboard's: DGVHT_CELL)
	LONGLONG iRow;           // -1 if none
	int idColumn;            // 0 if none
	POINT ptScreen;
};

// The new row (DGVS_NEWROW): a value typed in its placeholder (or a check box clicked there, or text pasted past the
// last row) adds rows at the end (the store's; virtual mode: the count goes up, then DGVN_SETCELL), then writes the
// values (DGVN_VALIDATE first, with the rows' indexes to be), as one step to undo. Ctrl+Shift++ (DGVS_USERROWS,
// InsertRowAtCaret) inserting an empty row above the caret's sends DGVN_ROWADDING (idColumn 0), then DGVN_ROWSCHANGED.
#define DGVN_ROWADDING       (DGVN_FIRST - 21)  // NMDGVROWADD; non-zero stops it (nothing is written)
#define DGVN_ROWADDED        (DGVN_FIRST - 22)  // NMDGVROWADD; the rows are there, their values not yet written

struct NMDGVROWADD {
	NMHDR hdr;
	LONGLONG iRow;           // the (first) new row's index in the data
	int idColumn;            // the cell typed in (0: none)
	int cRows;               // how many
};

// The user's rows (DGVS_USERROWS): deleting the selected whole rows (Delete, DeleteSelectedRows), and every change of
// rows the user made or undid. The store only.
#define DGVN_ROWSDELETING    (DGVN_FIRST - 24)  // NMDGVROWS; non-zero stops it
#define DGVN_ROWSCHANGED     (DGVN_FIRST - 25)  // NMDGVROWS; rows inserted or deleted (reason: the step's, or undo, redo)

struct NMDGVROWS {
	NMHDR hdr;
	UINT reason;             // DGVCR_*
	int cInserted;           // their indexes after the change, ascending
	const LONGLONG* piInserted;
	int cDeleted;            // their indexes before it, ascending
	const LONGLONG* piDeleted;
};

// The columns' menu (DGVS_HEADERMENU's Columns, ShowColumnMenu): the user showed or hid a column.
#define DGVN_COLUMNSHOWN     (DGVN_FIRST - 26)  // NMDGVCOLUMN
#define DGVN_COLUMNHIDDEN    (DGVN_FIRST - 27)  // NMDGVCOLUMN

// ---- Finding ------------------------------------------------------------------------------------

#define DGVFF_MATCHCASE      0x0001
#define DGVFF_WHOLECELL      0x0002   // the cell's whole shown text, not a part of it
#define DGVFF_BACKWARD       0x0004   // left, then up
#define DGVFF_COLUMN         0x0008   // in the caret's column only
#define DGVFF_NOWRAP         0x0010   // not past the last (first) cell

// Virtual mode: the control finds by asking for the cells (DGVN_GETDISPINFO, about a second for 10,000 rows of ten
// columns), at most DGV_VIRTUALFINDROWS rows from the caret; the app may find the cell itself, faster or further:
// non-zero with iRow and idColumn set (iRow -1: not found). Typing in the find box looks for 150 ms at most each time.
#define DGVN_FINDCELL        (DGVN_FIRST - 23)  // NMDGVFIND
#define DGV_VIRTUALFINDROWS  10000

struct NMDGVFIND {
	NMHDR hdr;
	LPCWSTR pszText;
	UINT flags;              // DGVFF_*
	LONGLONG iStartRow;      // the data row of the cell after (before) which to look (the caret's; -1: none)
	int idStartColumn;
	BOOL fInclusive;         // the start cell counts too (typing in the find box)
	LONGLONG iRow;           // out
	int idColumn;            // out
};

// ---- Conditional formats -------------------------------------------------------------------------

#define DGVCO_EQUAL          0
#define DGVCO_NOTEQUAL       1
#define DGVCO_LESS           2
#define DGVCO_LESSEQUAL      3
#define DGVCO_GREATER        4
#define DGVCO_GREATEREQUAL   5
#define DGVCO_BETWEEN        6   // value1 to value2, both included
#define DGVCO_CONTAINS       7   // value1's text in the cell's shown text, case ignored
#define DGVCO_EMPTY          8
#define DGVCO_NOTEMPTY       9

#define DGVFR_WHOLEROW       0x0001   // the row's cells take the looks, not only the tested column's
#define DGVFR_BOLD           0x0002

// Numbers compare with numbers, dates with dates, text with text (case ignored); text against another kind compares
// with what the cell shows (an enum's name). Earlier rules come first: the first that matches sets a color.
struct DGVFORMATRULE {
	int idColumn;            // the column whose value is tested
	int op;                  // DGVCO_*
	DGVVALUE value1;         // the text is copied
	DGVVALUE value2;         // DGVCO_BETWEEN's upper end
	UINT flags;              // DGVFR_*
	COLORREF crText;         // CLR_DEFAULT: as it is
	COLORREF crBack;
};

// ---- Custom draw -----------------------------------------------------------------------------------

// NM_CUSTOMDRAW, as a list view's: CDDS_PREPAINT (return CDRF_NOTIFYITEMDRAW for rows, CDRF_NOTIFYPOSTPAINT for
// CDDS_POSTPAINT at the end), then each data row in view CDDS_ITEMPREPAINT (dwItemSpec: the row in view; colors for its
// cells; CDRF_NOTIFYSUBITEMDRAW for its cells), each cell CDDS_ITEMPREPAINT | CDDS_SUBITEM (colors; CDRF_NEWFONT: the
// font selected in hdc; CDRF_SKIPDEFAULT: the app drew it, background and all; CDRF_NOTIFYPOSTPAINT: CDDS_ITEMPOSTPAINT
// | CDDS_SUBITEM after it's drawn). The groups' rows and the new row aren't sent. The colors come in as the control
// would draw them, conditional formats applied.
struct NMDGVCUSTOMDRAW {
	NMCUSTOMDRAW nmcd;       // rc: the row's or the cell's; uItemState: CDIS_SELECTED, CDIS_FOCUS (the caret's cell)
	LONGLONG iRow;           // the data row
	int idColumn;            // a cell's (0: the row)
	COLORREF clrText;        // in, out
	COLORREF clrTextBk;      // in, out: the background
};

// ---- Control class --------------------------------------------------------------------------

struct DgData;
struct DgCell;
struct DgSortKey;
struct DgCellChange;
struct DgEditStep;
struct DgViewResult;
struct DgFilter;
struct DgGroup;
class DgAccessible;

class CDataGridCtrl : public ATL::CWindowImpl<CDataGridCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_DataGrid", CS_DBLCLKS, -1)

	CDataGridCtrl();
	~CDataGridCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CDataGridCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
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
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColor)
		MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColor)
		MESSAGE_HANDLER(WM_DGV_ENDEDIT, OnEndEditPosted)
		MESSAGE_HANDLER(WM_COMMAND, OnEditorCommand)
		MESSAGE_HANDLER(WM_NOTIFY, OnEditorNotify)
		MESSAGE_HANDLER(WM_DGV_VIEWDONE, OnViewDone)
		MESSAGE_HANDLER(WM_DGV_ENDFILTER, OnEndFilterPosted)
	ALT_MSG_MAP(1)   // the editor
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditorKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditorChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnEditorGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnEditorKillFocus)
	ALT_MSG_MAP(2)   // the filter row's edit box
		MESSAGE_HANDLER(WM_KEYDOWN, OnFilterKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditorChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnFilterGetDlgCode)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFilterKillFocus)
	ALT_MSG_MAP(3)   // the find box
		MESSAGE_HANDLER(WM_KEYDOWN, OnFindKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditorChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnFilterGetDlgCode)
	END_MSG_MAP()

	enum : UINT { WM_DGV_ENDEDIT = WM_USER + 100, WM_DGV_VIEWDONE, WM_DGV_ENDFILTER };

	// ---- Columns ------------------------------------------------------------------------------------

	int     InsertColumn(const DGVCOLUMN& column, int displayIndex = -1);   // its ID (> 0); -1: at the end
	BOOL    DeleteColumn(int id);
	BOOL    SetColumn(int id, const DGVCOLUMN& column);    // the members in mask
	BOOL    GetColumn(int id, DGVCOLUMN* column) const;    // the members in mask; pszTitle is the control's
	int     GetColumnCount() const;
	int     GetColumnOrder(int* ids, int count) const;     // all the IDs, as shown (hidden ones too); returns the count
	BOOL    SetColumnOrder(const int* ids, int count);     // all of them, each once
	int     GetColumnWidth(int id) const;                  // DIPs, as shown (a fill column's share)
	BOOL    SetColumnWidth(int id, int width);             // DIPs; a fill column becomes a fixed one
	BOOL    ShowColumn(int id, bool show);
	BOOL    AutoFitColumn(int id);                         // to its title and the rows in view
	BOOL    ShowColumnMenu(POINT ptScreen);                // every column, ticked if shown, to show or hide (not the last)
	void    SetFrozenColumns(int count);                   // the first count shown columns don't scroll
	int     GetFrozenColumns() const;

	// ---- Rows -----------------------------------------------------------------------------------------

	// Virtual mode: the control keeps no values but asks for them with DGVN_GETDISPINFO as it paints. Switching drops all
	// the rows.
	void    SetVirtual(bool virtualMode);
	bool    IsVirtual() const;
	BOOL    SetRowCount(LONGLONG count);                   // the store adds empty rows or drops the last ones
	LONGLONG GetRowCount() const;
	LONGLONG InsertRows(LONGLONG at, LONGLONG count = 1);  // the store: empty rows (-1: at the end); the first one's index
	BOOL    DeleteRows(LONGLONG at, LONGLONG count = 1);
	BOOL    SetCell(LONGLONG row, int id, const DGVVALUE& value, int image = -1);   // the store
	BOOL    GetCell(LONGLONG row, int id, DGVVALUE* value, int* image = nullptr);   // either; the text is the control's
	int     GetCellText(LONGLONG row, int id, LPWSTR text, int cch);   // as shown; the length with the terminator
	void    RedrawRows(LONGLONG first, LONGLONG last);     // virtual mode: the app's values changed (-1, -1: all)

	// ---- Caret and scrolling --------------------------------------------------------------------------

	BOOL    SetCaret(LONGLONG row, int id, bool ensureVisible = true);   // no DGVN_CARETCHANGED
	BOOL    GetCaret(LONGLONG* row, int* id) const;                       // FALSE if there are no rows or columns
	void    EnsureVisible(LONGLONG row, int id);
	LONGLONG GetTopRow() const;
	void    SetTopRow(LONGLONG row);
	int     GetVisibleRowCount() const;                    // the rows that fit whole
	void    HitTest(DGVHITTEST* hit) const;
	LONGLONG GetDataRow(LONGLONG viewRow) const;           // -1 if none
	LONGLONG GetViewRow(LONGLONG dataRow) const;           // -1 if not in view
	int     GetVisibleColumn(int index) const;             // the ID of the index-th column shown; 0 if none
	int     GetVisibleColumnIndex(int id) const;           // -1 if hidden or none

	// ---- Selection (the caret is the end a click or Shift moves) ----------------------------------------

	void    SetSelectionMode(int mode);                    // DGVSM_*; the selection goes back to the caret
	int     GetSelectionMode() const;
	void    SelectAll();
	void    ClearSelection();                              // only the caret's cell (DGVSM_ROWS: nothing)
	BOOL    SelectRange(const DGVRANGE& range, bool add = false);
	int     GetSelectedRanges(DGVRANGE* ranges, int count) const;   // returns how many there are
	bool    IsCellSelected(LONGLONG row, int id) const;    // a data row
	LONGLONG GetSelectedRowCount() const;                  // rows with any cell selected
	LONGLONG GetSelectedRows(LONGLONG* rows, LONGLONG count) const;   // their data rows, in view order; returns how many
	// The selected cells as tab-separated text and an HTML table (DGVCOPY_*); FALSE past a million cells.
	BOOL    Copy(UINT flags = 0);

	// ---- Sorting -----------------------------------------------------------------------------------------

	// The store's rows are sorted by the keys (stable; empty cells last); virtual mode only shows them, the app sorts.
	// Inserting or deleting rows sorts again; changing a cell doesn't (Resort does).
	BOOL    SetSortKeys(const DGVSORTKEY* keys, int count);
	int     GetSortKeys(DGVSORTKEY* keys, int count) const;   // returns how many there are
	void    Resort();

	// ---- Editing (DGVS_EDITABLE) -------------------------------------------------------------------------

	BOOL    BeginEdit(LONGLONG row, int id);               // the caret goes there; no editor for check boxes
	BOOL    EndEdit(bool commit = true);                   // FALSE: the value was rejected and the editor stays
	bool    IsEditing() const;
	bool    CanEdit(LONGLONG row, int id) const;
	BOOL    Paste();                                       // the clipboard's text into the selection
	BOOL    Cut();
	BOOL    ClearSelectedCells();
	LONGLONG InsertRowAtCaret();                           // the store: an empty row above the caret's (none: at the end), undoable; -1 if none
	BOOL    DeleteSelectedRows();                          // the store: the rows the selection touches, undoable
	BOOL    Undo();
	BOOL    Redo();
	bool    CanUndo() const;
	bool    CanRedo() const;
	void    ClearUndo();

	// ---- Filters and groups -----------------------------------------------------------------------------

	BOOL    SetFilter(const DGVFILTER& filter);             // replaces the column's; no notification
	BOOL    ClearFilter(int id = 0);                        // 0: all of them
	bool    HasFilter(int id) const;                        // 0: any
	LONGLONG GetFilteredRowCount() const;                  // the data rows that pass the filters
	BOOL    SetGroupColumns(const int* ids, int count);     // the store only; outermost first
	int     GetGroupColumns(int* ids, int count) const;     // returns how many there are
	bool    IsGroupRow(LONGLONG viewRow) const;
	BOOL    ExpandGroup(LONGLONG viewRow, bool expand);     // a group's row; no notification
	void    ExpandAllGroups(bool expand);
	LONGLONG GetViewRowCount() const;                      // the rows in view: the data rows that pass, and the groups'
	bool    IsBusy() const;                                // rows being rebuilt on the worker thread

	// ---- Finding -------------------------------------------------------------------------------------------

	// The next cell whose shown text matches (DGVFF_*), from the caret's: the caret goes there and it's selected.
	// FALSE if there's none (a virtual grid: in the rows looked at). text nullptr or empty: the last text looked for.
	BOOL    Find(LPCWSTR text, UINT flags = 0);
	// Ctrl+F: a box at the top right; typing finds from the caret and shades the cells that match, Enter and F3 find the
	// next, with Shift the one before, Esc closes it. F3 and Shift+F3 on the grid repeat the last find.
	void    ShowFindBox(bool show = true);
	bool    IsFindBoxShown() const;
	void    SetHighlight(LPCWSTR text, UINT flags = 0);   // the cells that match are shaded (nullptr or empty: none)

	// ---- Conditional formats -------------------------------------------------------------------------------

	int     AddFormatRule(const DGVFORMATRULE& rule, int index = -1);   // its ID (> 0); -1: last (the lowest priority)
	BOOL    DeleteFormatRule(int id);                      // 0: all
	int     GetFormatRules(int* ids, int count) const;     // in their order; returns how many there are

	// ---- The state --------------------------------------------------------------------------------------------

	// The columns' order, widths and hiding, the frozen columns, the sort, the groups and the filters as one line of
	// text, for the app to keep (columns by ID: insert them in the same order). Returns its length with the terminator.
	int     GetState(LPWSTR text, int cch) const;
	BOOL    SetState(LPCWSTR text);                        // columns it doesn't name keep their order, after the others

	// ---- Looks -----------------------------------------------------------------------------------------

	void    SetColors(const DGVCOLORS* colors);            // nullptr: the system's
	void    GetColors(DGVCOLORS* colors) const;            // as set (CLR_DEFAULT for the system's)
	void    SetRowHeight(int height);                      // DIPs; 0: from the font
	int     GetRowHeight() const;                          // DIPs
	void    SetImageList(HIMAGELIST images);               // not owned
	void    SetOwner(HWND owner);                          // gets the notifications (default: the parent)

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
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
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	// DataGridCtrl.cpp
	HWND    Owner() const;
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	int     Scale(int dips) const;
	int     Unscale(int pixels) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Relayout();                 // the columns' places and the scroll bars, then repaint
	void    UpdateScrollBars();
	void    ClampScroll();
	void    ScrollRows(LONGLONG top);
	void    ScrollX(int x);
	bool    Fetch(LONGLONG dataRow, int id, DgCell& cell, bool keep = true);   // from the store, the cache or the app (keep: in the cache)
	void    DropCache();
	void    MoveCaret(LONGLONG viewRow, int displayCol, bool notify);
	void    InvalidateRow(LONGLONG viewRow);
	void    Hit(POINT pt, DGVHITTEST& hit, int& displayCol) const;
	void    NotifyCell(UINT code, const DGVHITTEST& hit, POINT screen);

	// DataGridSelection.cpp
	enum class SelOp { Reset, Extend, Add, Toggle };
	void    Select(SelOp op, LONGLONG viewRow, int displayCol, bool user, bool wholeRows = false);   // the caret goes there too
	void    ResetSelection();                              // to the caret
	void    FireSelChanged();
	void    StartSelectDrag(bool rowHeader);
	void    SelectDragTo(POINT pt);
	void    EndSelectDrag();
	bool    SelectionKey(WPARAM key, bool ctrl, bool shift);   // Ctrl+A, Ctrl+C, Shift/Ctrl+Space
	void    Sort(std::vector<DgSortKey> keys, bool user);
	void    ApplyOrder();                                  // the row map from the sort keys
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);

	// DataGridEdit.cpp
	LRESULT OnChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEndEditPosted(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditorCommand(UINT, WPARAM, LPARAM, BOOL&);   // EN_CHANGE, CBN_SELCHANGE: dirty
	LRESULT OnEditorNotify(UINT, WPARAM, LPARAM, BOOL&);    // DTN_DATETIMECHANGE: dirty
	LRESULT OnEditorKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditorChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditorGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditorKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	bool    StartEdit(LONGLONG dataRow, int id, const wchar_t* typed);   // typed: what replaces the text
	bool    CommitOrCancel();                              // before the view changes: false if the value was rejected
	void    PositionEditor();
	bool    CellRect(LONGLONG viewRow, int displayCol, RECT& rc) const;   // the part in view; false if none
	RECT    CheckRect(const RECT& cell, int align) const;  // a check box's in its cell
	bool    OnCheckBox(LONGLONG viewRow, int displayCol, POINT pt) const;   // an editable check box under the point
	bool    EditKey(WPARAM key, bool ctrl, bool shift);    // F2, Delete, Ctrl+V/X/Z/Y, Space on a check box
	bool    Validate(LONGLONG dataRow, int id, DgCell& cell, const std::wstring& text, UINT reason, std::wstring& error);
	void    Apply(std::vector<DgCellChange> changes, UINT reason, bool undoable, bool forward = true);
	void    ApplyStep(const DgEditStep& step, UINT reason, bool undoable, bool forward);   // rows, then cells (back: the other way)
	void    ShowStep(const DgEditStep& step, bool forward);   // after undo or redo: the caret on what changed
	// count rows at the end, then the cells' changes: existing ones' rows as they are, added ones' rows 0 to count - 1
	// (their offsets); one undo step. The first new row, -1 if the app stopped it.
	LONGLONG AddRows(int count, int id, std::vector<DgCellChange> existing, std::vector<DgCellChange> added, UINT reason,
		bool caretToNew = true);
	void    WriteCell(LONGLONG dataRow, int id, const DgCell& cell);
	void    ToggleChecks(LONGLONG viewRow, int displayCol, bool selection);
	void    ShowError(const std::wstring& text, LONGLONG viewRow, int displayCol);
	void    HideError();

	// DataGridView.cpp
	LRESULT OnViewDone(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEndFilterPosted(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFilterKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFilterGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFilterKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	void    ShowView(DgViewResult view);             // the rows in view from a rebuilt view
	void    Reflatten(LONGLONG from = 0);                   // the entries again; a hidden caret goes to the first row shown from there
	bool    ChangeFilter(int id, const DgFilter* filter, bool user);   // nullptr: clear (id 0: all)
	bool    ChangeGroups(std::vector<int> ids, bool user);
	void    ToggleGroup(LONGLONG viewRow, bool user);
	void    ShowHeaderMenu(int displayCol);
	void    OpenFilterEdit(int displayCol);
	void    CloseFilterEdit(bool apply);
	void    PositionFilterEdit();
	bool    FilterRect(int displayCol, RECT& rc) const;
	std::wstring FilterSummary(int id) const;              // what the filter row shows for a column's filter
	void    ApplyFilterText(int id, const std::wstring& text);   // the filter row's: a "contains" filter, or none

	// DataGridFind.cpp
	LRESULT OnFindKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	bool    FindKey(WPARAM key, bool ctrl, bool shift);    // Ctrl+F, F3
	// inclusive: the caret's cell counts; budget: milliseconds to look for (0: no end). 1: found, 0: none, -1: out of time
	int     FindFrom(const std::wstring& text, UINT flags, bool inclusive, bool beep, DWORD budget = 0);
	void    PositionFindBox();
	void    CloseFindBox();
	void    OnFindChanged();                               // the find box's text

	// DataGridAccessible.cpp
	friend class DgAccessible;
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	void    DisconnectAccessible();
	void    NotifyCaretAccessible();                       // the caret's cell is the focused element

	// DataGridState.cpp (the state, the rules, the new row)
	bool    CanEditCell(LONGLONG row, int id) const;       // CanEdit, or a cell of the new row's placeholder

	// DataGridTips.cpp
	void    UpdateTip(POINT pt);                           // the tooltip for what's under the mouse
	void    HideTip();
	LRESULT TipNotify(NMHDR* nm);                          // TTN_GETDISPINFO, TTN_SHOW

	// DataGridView.cpp
	void    ToggleColumn(int id);                          // the columns' menu: show or hide it (DGVN_COLUMNSHOWN/HIDDEN)

	// DataGridPaint.cpp
	void    Paint(HDC hdc, const RECT& clip);
	void    UpdateColors();
	void    UpdateScrollBarTheme();                        // dark scroll bars on a dark background (DataGridCtrl.cpp)
	void    PaintGroupRow(HDC hdc, const DgGroup& group, int y);   // its open or closed mark, label and aggregates

	// DataGridHeader.cpp
	bool    HeaderMouseDown(POINT pt, const DGVHITTEST& hit, int displayCol);
	void    HeaderMouseMove(POINT pt);
	void    HeaderMouseUp(POINT pt);
	int     MeasureColumn(int displayIndex);   // pixels to fit the title and the rows in view

	std::unique_ptr<DgData> m_Data;
	HWND m_Owner = nullptr;
	HFONT m_hFont = nullptr;
	HFONT m_hBold = nullptr;      // the header's
	HFONT m_hUnderline = nullptr; // links'
	bool m_OwnFont = true;
	UINT m_Dpi = 96;
	int m_WheelRemainder = 0, m_HWheelRemainder = 0;
	int m_DarkScrollBars = -1;    // the theme last set: -1 none yet
	ATL::CContainedWindow m_Editor;   // the open editor (an edit box, a drop-down list or a date picker)
	ATL::CContainedWindow m_FilterEdit;   // the filter row's, while one of its cells is typed in
	ATL::CContainedWindow m_FindEdit;     // the find box's
	DgAccessible* m_Accessible = nullptr; // made on the first WM_GETOBJECT; cut off in OnDestroy
};
