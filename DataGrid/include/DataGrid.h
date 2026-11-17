#pragma once

// DataGrid: a grid of typed columns over rows that are either kept by the control (the store, the default) or asked
// of the app as they're painted (virtual mode: SetVirtual, SetRowCount, DGVN_GETDISPINFO), for millions of rows. The
// header is drawn by the control: columns are resized by their dividers (a double-click fits the column to its title
// and the rows in view), dragged to another place, hidden, given a share of the spare width (fill) and frozen at the
// left, where they don't scroll. A column's type formats its values (numbers, dates, enums) and draws them (check
// boxes, progress bars, an image and text, links). The caret is a cell, moved with the keys as in Excel. Rows scroll
// whole, so any number of them fit the scroll bar. The API follows the common controls: C-style structures and
// WM_NOTIFY codes (DGVN_*) sent to the parent. Columns are known by IDs the control gives them, which don't change
// when they move; rows by their index in the data, which stays the same however they're shown.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>
#include <atltypes.h>
#include <commctrl.h>

#include <memory>
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
#define DGVCM_ALL         0x03FF

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
};

// ---- Hit testing ------------------------------------------------------------------

#define DGVHT_NOWHERE        0x0000
#define DGVHT_CELL           0x0001
#define DGVHT_HEADER         0x0002   // a column's title
#define DGVHT_HEADERDIVIDER  0x0004   // the right edge of idColumn's title, where it's resized
#define DGVHT_ROWHEADER      0x0008   // a row's number
#define DGVHT_CORNER         0x0010   // above the row numbers
#define DGVHT_EMPTY          0x0020   // past the last row or column

struct DGVHITTEST {
	POINT pt;               // in: client coordinates
	UINT flags;             // out: DGVHT_*
	LONGLONG iRow;          // -1 if none
	int idColumn;           // 0 if none
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

// ---- Control class --------------------------------------------------------------------------

struct DgData;
struct DgCell;
struct DgSortKey;

class CDataGridCtrl : public ATL::CWindowImpl<CDataGridCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_DataGrid", CS_DBLCLKS, -1)

	CDataGridCtrl();
	~CDataGridCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CDataGridCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
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
	END_MSG_MAP()

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

	// ---- Looks -----------------------------------------------------------------------------------------

	void    SetColors(const DGVCOLORS* colors);            // nullptr: the system's
	void    GetColors(DGVCOLORS* colors) const;            // as set (CLR_DEFAULT for the system's)
	void    SetRowHeight(int height);                      // DIPs; 0: from the font
	int     GetRowHeight() const;                          // DIPs
	void    SetImageList(HIMAGELIST images);               // not owned
	void    SetOwner(HWND owner);                          // gets the notifications (default: the parent)

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
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
	bool    Fetch(LONGLONG dataRow, int id, DgCell& cell);   // from the store, the cache or the app
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

	// DataGridPaint.cpp
	void    Paint(HDC hdc, const RECT& clip);
	void    UpdateColors();

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
};
