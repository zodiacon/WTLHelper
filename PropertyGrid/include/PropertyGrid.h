#pragma once

// Property grid: a categorized name/value editor in the style of the Visual Studio
// properties window. The API follows the common controls: an opaque HPRGITEM handle,
// C-style structures and WM_NOTIFY codes (PRGN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <climits>
#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_PROPERTYGRID[] = L"WC_PropertyGrid";

// ---- Item handle ------------------------------------------------------------
//
// HPRGITEM is an opaque pointer to an internal node. Callers only hold and compare
// handles; a handle stays valid until its item (or an ancestor) is deleted.

struct PgNode;
using HPRGITEM = PgNode*;

constexpr HPRGITEM PRG_ROOT = nullptr;                // parent arg: top-level item
#define           PRG_LAST  ((HPRGITEM)(ULONG_PTR)-1) // insertAfter: append
#define           PRG_FIRST ((HPRGITEM)(ULONG_PTR)-2) // insertAfter: prepend

// ---- Property kinds (select the editor and the formatting) ------------------
//
// Each kind holds one value type (PRGVT_*); an item may also hold PRGVT_EMPTY (shown blank).

enum : UINT {
	PRGK_CATEGORY,   // group header, top level only, no value
	PRGK_STRING,     // PRGVT_STRING
	PRGK_INTEGER,    // PRGVT_INT; minVal/maxVal; PRGF_HEX shows hex
	PRGK_REAL,       // PRGVT_REAL; precision
	PRGK_BOOL,       // PRGVT_BOOL; drawn as a check box
	PRGK_ENUM,       // PRGVT_INT, one of the enum items
	PRGK_FLAGS,      // PRGVT_INT, an OR of the enum items; expands into a check box per item
	PRGK_COLOR,      // PRGVT_COLOR; swatch + #RRGGBB; "..." opens ChooseColor
	PRGK_FONT,       // PRGVT_FONT; "..." opens ChooseFont
	PRGK_PATH,       // PRGVT_STRING; "..." opens a file dialog (a folder one with PRGF_FOLDER)
	PRGK_DATETIME,   // PRGVT_DATETIME
	PRGK_CUSTOM,     // any value type, edited as text; with PRGF_BUTTON the app supplies a dialog
};

// ---- Values -------------------------------------------------------------------

enum : UINT {
	PRGVT_EMPTY,
	PRGVT_STRING,    // pszVal
	PRGVT_INT,       // llVal
	PRGVT_REAL,      // dblVal
	PRGVT_BOOL,      // boolVal
	PRGVT_COLOR,     // clrVal
	PRGVT_FONT,      // lfVal; lfHeight in 96-DPI units (-12 = 9pt)
	PRGVT_DATETIME,  // ftVal, UTC
};

struct PRGVALUE {
	UINT type;      // PRGVT_*
	union {
		LPCWSTR  pszVal;
		LONGLONG llVal;
		double   dblVal;
		BOOL     boolVal;
		COLORREF clrVal;
		FILETIME ftVal;
		LOGFONTW lfVal;
	};
};

inline PRGVALUE PrgMakeString(LPCWSTR text)       { PRGVALUE v{}; v.type = PRGVT_STRING; v.pszVal = text; return v; }
inline PRGVALUE PrgMakeInt(LONGLONG value)        { PRGVALUE v{}; v.type = PRGVT_INT; v.llVal = value; return v; }
inline PRGVALUE PrgMakeReal(double value)         { PRGVALUE v{}; v.type = PRGVT_REAL; v.dblVal = value; return v; }
inline PRGVALUE PrgMakeBool(bool value)           { PRGVALUE v{}; v.type = PRGVT_BOOL; v.boolVal = value; return v; }
inline PRGVALUE PrgMakeColor(COLORREF value)      { PRGVALUE v{}; v.type = PRGVT_COLOR; v.clrVal = value; return v; }
inline PRGVALUE PrgMakeFont(const LOGFONTW& lf)   { PRGVALUE v{}; v.type = PRGVT_FONT; v.lfVal = lf; return v; }
inline PRGVALUE PrgMakeDateTime(const FILETIME& ft) { PRGVALUE v{}; v.type = PRGVT_DATETIME; v.ftVal = ft; return v; }

// Enum and Flags kinds: the named values. Flags shows the names of the set items, joined by " | ".
struct PRGENUMITEM {
	LPCWSTR  pszName;
	LONGLONG value;
};

// ---- Item flags ------------------------------------------------------------------

#define PRGF_READONLY   0x0001   // value can't be edited (drawn gray)
#define PRGF_HEX        0x0002   // Integer, Enum, Flags: show numbers in hex
#define PRGF_FOLDER     0x0004   // Path: pick a folder
#define PRGF_SAVEFILE   0x0008   // Path: save dialog instead of open
#define PRGF_BUTTON     0x0010   // show a "..." button (PRGN_BUTTONCLICK) for any kind
#define PRGF_EDITABLE   0x0020   // Enum: free text allowed besides the items
#define PRGF_PASSWORD   0x0040   // String: masked
#define PRGF_HIDDEN     0x0080   // not shown
#define PRGF_COLLAPSED  0x0100   // starts collapsed (items with children start expanded otherwise; Flags always collapsed)
#define PRGF_DATEONLY   0x0200   // DateTime: no time part
#define PRGF_CALLBACK   0x0400   // the value comes from the parent (PRGN_GETDISPINFO) each time it's shown or used

// ---- Item description (InsertItem) ---------------------------------------------------

struct PRGITEMDESC {
	UINT              kind = PRGK_STRING;
	LPCWSTR           pszName = nullptr;
	LPCWSTR           pszDescription = nullptr;  // shown in the description pane
	PRGVALUE           value{};
	PRGVALUE           defValue{};                // PRGVT_EMPTY: no default; else a different value is drawn bold
	UINT              flags = 0;                 // PRGF_*
	LPARAM            lParam = 0;
	LONGLONG          minVal = LLONG_MIN;        // Integer
	LONGLONG          maxVal = LLONG_MAX;
	int               precision = -1;            // Real: digits after the point; -1 = shortest exact form
	const PRGENUMITEM* enumItems = nullptr;       // Enum, Flags (copied)
	int               enumCount = 0;
	LPCWSTR           pszFilter = nullptr;       // Path: OPENFILENAME filter, "Text\0*.txt\0\0"
};

// ---- Views --------------------------------------------------------------------------

enum : UINT {
	PRGV_CATEGORIZED,   // category headers, items in insertion order
	PRGV_ALPHABETICAL,  // no headers, top-level properties sorted by name
};

// ---- Hit testing ----------------------------------------------------------------------

enum : UINT {
	PRGHT_NOWHERE,
	PRGHT_GLYPH,        // expand/collapse glyph
	PRGHT_NAME,
	PRGHT_VALUE,
	PRGHT_SPLITTER,     // between the name and value columns
	PRGHT_DESCSPLITTER, // above the description pane
	PRGHT_DESCRIPTION,
	PRGHT_BUTTON,      // the selected item's "..." or drop-down button
	PRGHT_TOOLBAR,
};

// ---- Colors ---------------------------------------------------------------------------

struct PRGCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crCategoryBack;    // category rows and the left margin
	COLORREF crCategoryText;
	COLORREF crGridLine;
	COLORREF crSelBack;         // selected row, control focused
	COLORREF crSelText;
	COLORREF crSelBackInactive; // selected row, control not focused
	COLORREF crSelTextInactive;
	COLORREF crDisabledText;    // read-only values
	COLORREF crDescBack;
	COLORREF crDescText;
};

// ---- Notifications ----------------------------------------------------------------------

#define PRGN_FIRST          (0U - 2800U)   // outside the common controls ranges
#define PRGN_SELCHANGED     (PRGN_FIRST - 0)   // NMPRGSELCHANGE
#define PRGN_ITEMEXPANDED   (PRGN_FIRST - 1)   // NMPRGEXPAND; by the user (mouse, keyboard)
#define PRGN_CONTEXTMENU    (PRGN_FIRST - 2)   // NMPRGITEM; pt in screen coords, hItem may be nullptr
#define PRGN_BEGINEDIT      (PRGN_FIRST - 3)   // NMPRGITEM; nonzero = don't edit
#define PRGN_VALUECHANGING  (PRGN_FIRST - 4)   // NMPRGVALUE; nonzero = reject (with szError), or change newValue
#define PRGN_VALUECHANGED   (PRGN_FIRST - 5)   // NMPRGVALUE; the user changed a value
#define PRGN_BUTTONCLICK    (PRGN_FIRST - 6)   // NMPRGITEM; pt: the button's bottom left, screen coords;
                                               // nonzero = handled (else the kind's own dialog, if it has one)
#define PRGN_VIEWCHANGED    (PRGN_FIRST - 7)   // NMHDR; by the toolbar's buttons
#define PRGN_FILTERCHANGED  (PRGN_FIRST - 8)   // NMHDR; typed in the toolbar's search box
#define PRGN_GETDISPINFO    (PRGN_FIRST - 9)   // NMPRGDISPINFO; a PRGF_CALLBACK item's value is needed

struct NMPRGITEM {
	NMHDR    hdr;
	HPRGITEM hItem;
	LPARAM   lParam;     // the item's data
	POINT    pt;
};

struct NMPRGSELCHANGE {
	NMHDR    hdr;
	HPRGITEM hItemOld;   // nullptr if none
	HPRGITEM hItemNew;   // nullptr if none
};

struct NMPRGEXPAND {
	NMHDR    hdr;
	HPRGITEM hItem;
	BOOL     expanded;
};

// Sent only for changes made by the user (editing, toggling), never for SetValue.
// PRGN_VALUECHANGING: the value is parsed and range checked. The parent may replace newValue (a string
// it points to has to last until the notification returns), or reject the change by returning nonzero,
// optionally with a message in szError; the editor then stays open.
struct NMPRGVALUE {
	NMHDR    hdr;
	HPRGITEM hItem;
	LPARAM   lParam;
	PRGVALUE oldValue;
	PRGVALUE newValue;
	WCHAR    szError[256];
};

// PRGF_CALLBACK: the parent fills value (which holds the last value it gave). A string it points to has to
// last until the notification returns; the grid keeps a copy. The grid asks while painting, so the handler
// must not change the grid's items. A change the user makes comes in PRGN_VALUECHANGING/CHANGED as usual;
// the parent stores it, or the next PRGN_GETDISPINFO brings the old value back.
struct NMPRGDISPINFO {
	NMHDR    hdr;
	HPRGITEM hItem;
	LPARAM   lParam;
	PRGVALUE value;
};

// ---- Control class --------------------------------------------------------------------------

struct PgModel;
struct PgRow;
class PgCheckList;
class PgAccessible;
struct PgStoredValue;

class CPropertyGridCtrl : public ATL::CWindowImpl<CPropertyGridCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_PropertyGrid", CS_DBLCLKS, -1)

	CPropertyGridCtrl();
	~CPropertyGridCtrl();
	friend class PgCheckList;
	friend class PgAccessible;

	// posted to itself by the editing code
	enum : UINT { WM_PRG_CHECKFOCUS = WM_USER + 100, WM_PRG_COMBOCLOSED };

	BEGIN_MSG_MAP(CPropertyGridCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColor)
		MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColor)
		MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnCtlColor)
		MESSAGE_HANDLER(WM_PRG_CHECKFOCUS, OnCheckFocus)
		MESSAGE_HANDLER(WM_PRG_COMBOCLOSED, OnComboClosed)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Items ------------------------------------------------------------------------

	HPRGITEM AddCategory(LPCWSTR name, LPCWSTR description = nullptr);
	// hParent: PRG_ROOT, a category or a property (sub-property). Fails (nullptr) on a value type
	// that doesn't fit the kind, a category below the top level, or hInsertAfter not a child of hParent.
	HPRGITEM InsertItem(HPRGITEM hParent, const PRGITEMDESC* desc, HPRGITEM hInsertAfter = PRG_LAST);
	BOOL    DeleteItem(HPRGITEM hItem);
	void    DeleteAllItems();
	// path of names separated by '/' ("Appearance/Font"), case-insensitive, from hStart's children
	HPRGITEM FindItem(LPCWSTR path, HPRGITEM hStart = PRG_ROOT) const;
	int     GetItemCount() const;   // all items, categories included

	// ---- Values -----------------------------------------------------------------------

	// pValue->pszVal points into the control; valid until the item's value changes or the item is deleted
	BOOL    GetValue(HPRGITEM hItem, PRGVALUE* pValue) const;
	BOOL    SetValue(HPRGITEM hItem, const PRGVALUE* pValue);   // no notification
	BOOL    GetDefaultValue(HPRGITEM hItem, PRGVALUE* pValue) const;
	BOOL    SetDefaultValue(HPRGITEM hItem, const PRGVALUE* pValue);
	BOOL    ResetValue(HPRGITEM hItem);                        // back to the default, if there is one
	bool    IsModified(HPRGITEM hItem) const;                  // has a default and differs from it
	ATL::CString GetValueText(HPRGITEM hItem) const;           // as displayed
	void    RefreshValues();                                   // repaints: PRGF_CALLBACK items are asked again

	// ---- Attributes -------------------------------------------------------------------

	UINT    GetItemKind(HPRGITEM hItem) const;
	ATL::CString GetItemName(HPRGITEM hItem) const;
	BOOL    SetItemName(HPRGITEM hItem, LPCWSTR name);
	ATL::CString GetDescription(HPRGITEM hItem) const;
	BOOL    SetDescription(HPRGITEM hItem, LPCWSTR text);
	UINT    GetItemFlags(HPRGITEM hItem) const;
	BOOL    SetItemFlags(HPRGITEM hItem, UINT flags, UINT mask);
	LPARAM  GetItemData(HPRGITEM hItem) const;
	BOOL    SetItemData(HPRGITEM hItem, LPARAM data);
	BOOL    SetEnumItems(HPRGITEM hItem, const PRGENUMITEM* items, int count);
	BOOL    SetRange(HPRGITEM hItem, LONGLONG minVal, LONGLONG maxVal);
	BOOL    SetPrecision(HPRGITEM hItem, int precision);

	// ---- Navigation -------------------------------------------------------------------

	HPRGITEM GetChildItem(HPRGITEM hItem) const;        // PRG_ROOT: first top-level item
	HPRGITEM GetNextSiblingItem(HPRGITEM hItem) const;
	HPRGITEM GetPrevSiblingItem(HPRGITEM hItem) const;
	HPRGITEM GetParentItem(HPRGITEM hItem) const;       // nullptr for a top-level item

	// ---- Selection and expansion -----------------------------------------------------------

	HPRGITEM GetSelectedItem() const;
	BOOL    SelectItem(HPRGITEM hItem);                // nullptr clears; expands the ancestors; PRGN_SELCHANGED
	void    EnsureVisible(HPRGITEM hItem);
	BOOL    Expand(HPRGITEM hItem, bool expand = true);// no notification
	bool    IsExpanded(HPRGITEM hItem) const;
	void    ExpandAll(bool expand = true);
	BOOL    GetItemRect(HPRGITEM hItem, RECT* prc) const;   // client coords; FALSE if not visible
	HPRGITEM HitTest(POINT pt, UINT* pFlags = nullptr) const; // PRGHT_* in *pFlags

	// ---- Editing ----------------------------------------------------------------------
	//
	// The user edits a value by clicking it, pressing Enter or F2, or typing; F4 or Alt+Down opens a
	// drop-down. Enter commits, Escape cancels, Tab commits and moves to the next property. Bool values
	// toggle with a click on the check box, Space or a double click; a double click on an enum's name
	// selects the next value. The selected item's "..." button (or F4, Alt+Down) opens its dialog: color,
	// font, file or folder, or PRGN_BUTTONCLICK for PRGF_BUTTON; a Flags item's drop-down button opens a
	// check list, and its flags are also check boxes in rows below it.

	BOOL    BeginEdit(HPRGITEM hItem = nullptr);      // nullptr: the selected item; selects it
	BOOL    EndEdit(bool commit = true);              // FALSE if the new value was rejected (the editor stays)
	bool    IsEditing() const;
	HPRGITEM GetEditItem() const;
	HWND    GetEditorWindow() const;                  // the edit, combo box or date picker, while editing
	void    SetReadOnly(bool readOnly);               // the whole grid: values can be selected and copied only
	bool    IsReadOnly() const;

	// ---- View -------------------------------------------------------------------------

	void    SetView(UINT view);                       // PRGV_*
	UINT    GetView() const;
	void    ShowDescription(bool show);
	bool    IsDescriptionVisible() const;
	void    SetDescriptionHeight(int height);         // DIPs
	int     GetDescriptionHeight() const;
	void    SetSplitterPos(double fraction);          // name column share of the width, 0.1 .. 0.9
	double  GetSplitterPos() const;
	void    BeginUpdate();                            // nests; suppresses rebuilding and painting
	void    EndUpdate();
	// the built-in toolbar: Categorized and Alphabetical buttons and a search box (Ctrl+F) that sets the
	// filter. An app with its own toolbar leaves it hidden and calls SetView and SetFilter.
	void    ShowToolbar(bool show);
	bool    IsToolbarVisible() const;
	// shows only the items whose name or shown value contains the text (case-insensitive), with the items
	// above them; a category whose name contains it shows all of it. nullptr or "" shows everything.
	void    SetFilter(LPCWSTR text);                  // no notification
	ATL::CString GetFilter() const;

	// ---- Appearance -------------------------------------------------------------------

	void    SetColors(const PRGCOLORS* colors);        // nullptr: system colors (follows WTLHelper dark mode)
	PRGCOLORS GetColors() const;                       // the colors in effect

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCheckFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnComboClosed(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);

	// callback values, tooltips for cut text, accessibility
	void    QueryValue(PgNode* node) const;           // PRGF_CALLBACK: PRGN_GETDISPINFO into the node's value
	void    QueryAllValues();
	void    UpdateTextTip(POINT pt);
	void    HideTextTip();
	std::wstring RowName(int row) const;
	std::wstring RowValue(int row) const;
	void    NotifyAccessible(DWORD event, int row);   // row -1: the grid itself
	void    DisconnectAccessible();

	// editing
	enum class Editor { None, Edit, Combo, Date };
	bool    CanEdit(const PgNode* node) const;
	bool    IsReadOnly(const PgNode* node) const;
	bool    CreateEditor(PgNode* node);
	void    DestroyEditor();
	void    PositionEditor();
	RECT    GetValueRect(int row) const;              // the value cell, client coords
	int     ValueTextLeft(const PgNode* node, const RECT& rcValue) const;
	RECT    GetCheckBoxRect(int row) const;
	bool    CommitEditor(std::wstring& error);
	bool    CommitValue(PgNode* node, const PRGVALUE& value, std::wstring& error);
	bool    EndEditCore(bool commit, bool focusLost);
	void    ToggleBool(PgNode* node);
	void    CycleEnum(PgNode* node);
	void    StepInteger(int delta);
	void    MoveToProperty(int direction, bool edit); // Tab, Shift+Tab, Up/Down in an edit
	void    ShowError(const std::wstring& text);
	void    HideError();
	bool    HandleEditorKey(HWND hWnd, UINT msg, WPARAM wParam);
	static LRESULT CALLBACK EditorSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// buttons, dialogs, flags
	enum class Button { None, Ellipsis, DropDown };
	Button  GetButton(const PgNode* node) const;
	RECT    GetButtonRect(int row) const;
	void    PressButton(PgNode* node);                // the "..." or drop-down button, by mouse or key
	bool    RunDialog(PgNode* node, PgStoredValue& value);
	void    ToggleFlag(PgNode* node, int flag);
	void    OpenCheckList();
	void    CloseCheckList(bool restoreText);
	bool    IsCheckListOpen() const;
	void    OnCheckListChanged(LONGLONG mask);

	// toolbar
	int     ToolbarHeight() const;
	RECT    GetToolButtonRect(int index) const;      // 0: categorized, 1: alphabetical
	RECT    GetSearchRect() const;                    // the search box's frame
	void    CreateToolbar();
	void    PositionToolbar();
	void    PaintToolbar(WTL::CDCHandle dc, const PRGCOLORS& c);
	void    ApplyFilter(const std::wstring& filter, bool notify);
	void    OnSearchCommand(UINT code);
	static LRESULT CALLBACK SearchSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// layout
	int     Scale(int dips) const;
	void    UpdateMetrics();
	void    UpdateFonts();
	void    Layout();
	RECT    GetGridRect() const;
	RECT    GetDescRect() const;
	int     GetSplitX() const;
	int     GetPageRows() const;
	void    UpdateScrollBar();
	void    ScrollTo(int topRow);

	// rows
	void    Rebuild();
	int     RowOf(const PgNode* node) const;
	void    ExpandAncestors(PgNode* node);
	bool    SetExpanded(PgNode* node, bool expand, bool notify);
	void    SelectRow(int row, bool notify = true);
	int     SelectedRow() const;                      // the row of m_Selected (and m_SelFlag)
	void    EnsureRowVisible(int row);
	int     HitRow(POINT pt, UINT* pFlags) const;     // -1 if not on a row
	bool    HasVisibleChildren(const PgNode* node) const;

	// painting
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintRow(WTL::CDCHandle dc, int row, const RECT& rc, const PRGCOLORS& c);
	void    PaintDescription(WTL::CDCHandle dc, const RECT& rc, const PRGCOLORS& c);
	void    DrawGlyph(WTL::CDCHandle dc, int x, const RECT& rcRow, bool expanded, COLORREF color);
	void    DrawCheckBox(WTL::CDCHandle dc, const RECT& rc, bool checked, COLORREF color, COLORREF back);
	int     GlyphCenter(const PgRow& row) const;
	int     TextLeft(const PgRow& row) const;

	LRESULT FireNotify(NMHDR* pnm, UINT code);
	void    NotifyContextMenu(POINT ptScreen, bool fromKeyboard);
	PgNode* Node(HPRGITEM hItem) const;

	enum class Drag { None, Splitter, Description };

	std::unique_ptr<PgModel> m_Model;
	std::vector<PgRow>       m_Rows;
	PgNode*                  m_Selected = nullptr;
	int                      m_SelFlag = -1;         // >= 0: one of m_Selected's flag rows
	int                      m_TopRow = 0;
	UINT                     m_View = PRGV_CATEGORIZED;
	int                      m_UpdateCount = 0;
	bool                     m_RebuildPending = false;

	// layout state
	double                   m_SplitFraction = 0.45;
	bool                     m_ShowDesc = true;
	int                      m_DescHeightDips = 64;
	Drag                     m_Drag = Drag::None;
	int                      m_DragOffset = 0;
	int                      m_WheelRemainder = 0;

	// metrics (pixels at the current DPI)
	UINT                     m_Dpi = 96;
	int                      m_RowHeight = 20;
	int                      m_Margin = 16;      // left gutter
	int                      m_Indent = 12;      // per level
	int                      m_Pad = 4;
	int                      m_SplitterGrip = 3; // +/- around the column splitter
	int                      m_DescSplitterHeight = 5;
	int                      m_TextHeight = 16;

	// fonts and colors
	HFONT                    m_hFont = nullptr;      // in effect (owned if m_OwnFont)
	bool                     m_OwnFont = true;
	WTL::CFont               m_BoldFont;
	bool                     m_SystemColors = true;
	PRGCOLORS                m_Colors{};
	WTL::CBrush              m_EditorBrush;          // WM_CTLCOLOR* background
	COLORREF                 m_EditorBrushColor = CLR_INVALID;

	// editing
	bool                     m_ReadOnly = false;
	PgNode*                  m_Editing = nullptr;
	Editor                   m_Editor = Editor::None;
	HWND                     m_hEditor = nullptr;    // the edit, combo box or date picker
	HWND                     m_hEditorEdit = nullptr;// the edit inside an editable combo box
	HWND                     m_hSpin = nullptr;      // integer with a range
	HWND                     m_hErrorTip = nullptr;
	std::wstring             m_EditInitialText;
	int                      m_EditInitialIndex = -1;// combo box
	SYSTEMTIME               m_EditInitialTime{};    // date picker, as it first reads back
	bool                     m_Ending = false;       // EndEditCore running: focus changes it causes are ignored
	std::unique_ptr<PgCheckList> m_CheckList;        // a Flags item's drop-down

	// toolbar, filter, scroll bar
	bool                     m_ShowToolbar = false;
	HWND                     m_hSearch = nullptr;
	HWND                     m_hToolTip = nullptr;   // the toolbar buttons'
	bool                     m_SettingSearch = false;// the search box's text set by SetFilter: no EN_CHANGE round trip
	std::wstring             m_Filter;
	HWND                     m_hScroll = nullptr;    // a child, beside the grid only (not the toolbar or description)
	bool                     m_ScrollVisible = false;

	// the tooltip over a name or value cut short, and accessibility
	HWND                     m_hTextTip = nullptr;
	int                      m_TipRow = -1;
	UINT                     m_TipPart = 0;          // PRGHT_NAME or PRGHT_VALUE
	bool                     m_TrackingLeave = false;
	PgAccessible*            m_Accessible = nullptr; // made on the first WM_GETOBJECT; cut off from the grid in OnDestroy
	std::wstring             m_CheckListInitialText;
	COLORREF                 m_CustomColors[16];     // ChooseColor's, kept between uses
};
