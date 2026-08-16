#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlmisc.h>
#include <atlgdi.h>
#include <atltheme.h>

#include <uxtheme.h>
#include <vector>
#include <string>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_TREELISTVIEW[] = L"WC_TreeListView";

// ---- Item handle ------------------------------------------------------------
//
// HTLITEM is an opaque pointer to an internal node.  Callers only hold and
// compare handles; they never dereference TlvNode directly.

struct TlvNode;
using HTLITEM = TlvNode*;

constexpr HTLITEM TLV_ROOT = nullptr;             // parent arg: top-level item
#define           TLV_LAST  ((HTLITEM)(ULONG_PTR)-1) // insertAfter: append
#define           TLV_FIRST ((HTLITEM)(ULONG_PTR)-2) // insertAfter: prepend

// ---- Notification codes -----------------------------------------------------

// ---- Search flags -----------------------------------------------------------

#define TLVFS_MATCHCASE  0x0001   // case-sensitive (default: case-insensitive)
#define TLVFS_CONTAINS   0x0002   // substring match (default: prefix)

// ---- Sort callback ----------------------------------------------------------

typedef int (CALLBACK* PFNTLVCOMPARE)(HTLITEM hItem1, HTLITEM hItem2, LPARAM lParam);

// ---- Notification codes -----------------------------------------------------

#define TLVN_FIRST          (NM_FIRST - 400)
#define TLVN_SELCHANGED     (TLVN_FIRST - 0)   // NMTLVITEMCHANGE
#define TLVN_ITEMEXPANDING  (TLVN_FIRST - 1)   // NMTLVEXPAND
#define TLVN_ITEMEXPANDED   (TLVN_FIRST - 2)   // NMTLVEXPAND
#define TLVN_ACTIVATE       (TLVN_FIRST - 3)   // NMTLVACTIVATE
#define TLVN_BEGINLABELEDIT (TLVN_FIRST - 4)   // NMTLVLABELEDIT; non-zero = veto
#define TLVN_ENDLABELEDIT   (TLVN_FIRST - 5)   // NMTLVLABELEDIT; non-zero = accept text
#define TLVN_ITEMCHECK      (TLVN_FIRST - 6)   // NMTLVITEMCHECK; informational
#define TLVN_GETDISPINFO    (TLVN_FIRST - 7)   // NMTLVDISPINFO; fill pszText
#define TLVN_SORT           (TLVN_FIRST - 8)   // NMTLVSORT; sortable column clicked

// ---- Notification structures ------------------------------------------------

struct NMTLVCUSTOMDRAW {
	NMCUSTOMDRAW nmcd;
	COLORREF     clrText;
	COLORREF     clrTextBk;
	int          iSubItem;
	HTLITEM      hItem;
};

struct NMTLVITEMCHANGE {
	NMHDR   hdr;
	HTLITEM hItemOld;   // item that lost selection (or nullptr)
	HTLITEM hItemNew;   // item that gained selection (or nullptr)
};

struct NMTLVEXPAND {
	NMHDR   hdr;
	HTLITEM hItem;
	bool    bExpanded;  // true = expanded, false = collapsed
};

struct NMTLVACTIVATE {
	NMHDR   hdr;
	HTLITEM hItem;
	POINT   ptAction;   // client coords of click, or {-1,-1} for keyboard
};

struct NMTLVLABELEDIT {
	NMHDR   hdr;
	HTLITEM hItem;
	LPWSTR  pszText;    // nullptr on BEGINLABELEDIT; proposed text on ENDLABELEDIT
};

struct NMTLVITEMCHECK {
	NMHDR   hdr;
	HTLITEM hItem;
	bool    bChecked;
};

struct NMTLVDISPINFO {
	NMHDR   hdr;
	HTLITEM hItem;
	int     iSubItem;
	LPWSTR  pszText;    // write the text here
	int     cchTextMax; // size of the buffer at pszText
};

struct NMTLVSORT {
	NMHDR hdr;
	int   iSubItem;   // column that was clicked
	int   iDirection; // 1 = ascending, -1 = descending
};

// ---- Internal node ----------------------------------------------------------
//
// Defined here so that CTreeListViewCtrl can hold m_Root inline (sentinel).
// Users must treat HTLITEM as fully opaque.

struct TlvNode {
	std::vector<std::wstring> cols;
	int      image       = -1;
	LPARAM   data        = 0;
	bool     expanded     = true;
	bool     hasChildren  = false;  // show glyph even when childCount == 0 (lazy load)
	bool     checked      = false;
	ULONGLONG callbackCols = 0;    // bitmask: bit N set → col N text supplied via TLVN_GETDISPINFO
	int      level       = 0;
	int      childCount  = 0;

	TlvNode* parent = nullptr;
	TlvNode* firstChild = nullptr;
	TlvNode* lastChild = nullptr;
	TlvNode* next = nullptr;   // next sibling
	TlvNode* prev = nullptr;   // prev sibling
};

// ---- Control class ----------------------------------------------------------

class CTreeListViewCtrl : public ATL::CWindowImpl<CTreeListViewCtrl> {
public:
	DECLARE_WND_CLASS(L"WC_TreeListView")

	BEGIN_MSG_MAP(CTreeListViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeChanged)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		NOTIFY_HANDLER(IDC_LIST, LVN_GETDISPINFOW, OnGetDispInfo)
		NOTIFY_HANDLER(IDC_LIST, NM_CUSTOMDRAW, OnCustomDraw)
		NOTIFY_HANDLER(IDC_LIST, NM_CLICK, OnClick)
		NOTIFY_HANDLER(IDC_LIST, NM_RCLICK, OnRClick)
		NOTIFY_HANDLER(IDC_LIST, NM_DBLCLK, OnDblClk)
		NOTIFY_HANDLER(IDC_LIST, NM_SETFOCUS, OnFocusChange)
		NOTIFY_HANDLER(IDC_LIST, NM_KILLFOCUS, OnFocusChange)
		NOTIFY_HANDLER(IDC_LIST, LVN_ITEMCHANGED, OnItemChanged)
		NOTIFY_HANDLER(IDC_LIST, LVN_COLUMNCLICK, OnColumnClick)
		NOTIFY_HANDLER(IDC_LIST, LVN_KEYDOWN, OnKeyDown)
		NOTIFY_HANDLER(IDC_LIST, LVN_BEGINLABELEDITW, OnBeginLabelEdit)
		NOTIFY_HANDLER(IDC_LIST, LVN_ENDLABELEDITW, OnEndLabelEdit)
	END_MSG_MAP()

	// ---- Column API ---------------------------------------------------------

	int  InsertColumn(int col, LPCWSTR title, int width, int fmt = LVCFMT_LEFT);
	BOOL DeleteColumn(int col);
	int  GetColumnWidth(int col) const;
	BOOL SetColumnWidth(int col, int width);
	int  GetColumnCount() const;
	CHeaderCtrl GetHeader() const;

	// ---- Item API -----------------------------------------------------------

	HTLITEM InsertItem(HTLITEM hParent, HTLITEM hInsertAfter, LPCWSTR text, int image = -1, LPARAM data = 0);
	BOOL    DeleteItem(HTLITEM hItem);
	void    DeleteAllItems();

	void    SetItemText(HTLITEM hItem, int col, LPCWSTR text);
	ATL::CString GetItemText(HTLITEM hItem, int col) const;
	void    SetItemData(HTLITEM hItem, LPARAM data);
	LPARAM  GetItemData(HTLITEM hItem) const;
	void    SetItemImage(HTLITEM hItem, int image);
	int     GetItemImage(HTLITEM hItem) const;
	bool    IsTextCallback(HTLITEM hItem, int col) const;

	// ---- Callback mode ----------------------------------------------------------
	// When enabled, TLVN_GETDISPINFO fires for every item/column, overriding
	// per-item LPSTR_TEXTCALLBACK settings.  Equivalent to LVS_OWNERDATA text
	// callbacks, but with HTLITEM instead of an index.

	void    SetCallbackMode(bool enable);
	bool    GetCallbackMode() const;

	// ---- Tree navigation ----------------------------------------------------

	HTLITEM GetRootItem() const;
	HTLITEM GetChildItem(HTLITEM hItem) const;
	HTLITEM GetNextSiblingItem(HTLITEM hItem) const;
	HTLITEM GetPrevSiblingItem(HTLITEM hItem) const;
	HTLITEM GetParentItem(HTLITEM hItem) const;
	HTLITEM GetNextItem(HTLITEM hItem, UINT flag) const;  // TVGN_* flags
	int     GetItemLevel(HTLITEM hItem) const;
	bool    HasChildren(HTLITEM hItem) const;
	void    SetHasChildren(HTLITEM hItem, bool has);  // lazy-load: show glyph without real children

	// ---- Expand / collapse --------------------------------------------------

	void    Expand(HTLITEM hItem, bool expand = true);  // returns without action if vetoed
	void    Toggle(HTLITEM hItem);
	bool    IsExpanded(HTLITEM hItem) const;
	void    ExpandAll(HTLITEM hItem = TLV_ROOT);        // bulk — no per-item notifications
	void    CollapseAll(HTLITEM hItem = TLV_ROOT);

	// ---- Tree lines ---------------------------------------------------------

	void    SetTreeLines(bool enable);
	bool    GetTreeLines() const;

	// ---- View mode ----------------------------------------------------------

	void    SetViewMode(bool treeMode);
	bool    IsTreeMode() const;

	// ---- Selection ----------------------------------------------------------

	HTLITEM GetSelectedItem() const;                     // first selected item
	HTLITEM GetNextSelectedItem(HTLITEM hPrev) const;    // pass nullptr for first; multi-select enumeration
	void    SelectItem(HTLITEM hItem);
	void    SelectAll();
	void    DeselectAll();
	void    EnsureVisible(HTLITEM hItem);

	// ---- Multi-selection ----------------------------------------------------

	void    SetMultiSelect(bool enable);
	bool    IsMultiSelect() const;

	// ---- Data query ---------------------------------------------------------

	int     GetItemCount() const;          // total nodes in the tree
	int     GetVisibleItemCount() const;   // nodes currently in the flat visible list
	HTLITEM FindItem(HTLITEM hStart, int col, LPCWSTR text, UINT flags = 0) const;
	bool    GetItemRect(HTLITEM hItem, RECT* prc) const;  // bounds in ListView client coords
	HTLITEM HitTest(POINT pt, int* pCol = nullptr) const; // pt in TLV client coords

	// ---- Sort ---------------------------------------------------------------

	void    SortChildren(HTLITEM hParent, PFNTLVCOMPARE pfn, LPARAM lParam, bool recurse = false);

	// Sortable columns: clicking the header toggles the sort arrow and fires
	// TLVN_SORT.  Non-sortable column clicks relay LVN_COLUMNCLICK as before.
	bool    SetColumnSortable(int col, bool sortable);
	bool    GetColumnSortable(int col) const;
	int     GetSortColumn() const;          // -1 if no column is sorted
	int     GetSortDirection() const;       // 1 = asc, -1 = desc, 0 = none
	void    SetSortColumn(int col, int dir); // programmatic arrow update, no notification

	// ---- Indent -------------------------------------------------------------

	void SetIndent(int width);
	int  GetIndent() const;

	// ---- Batch update -------------------------------------------------------

	void BeginUpdate();   // suppress redraws and RebuildVisibleList during bulk inserts
	void EndUpdate();

	// ---- Rendering ----------------------------------------------------------

	COLORREF SetBkColor(COLORREF clr);
	COLORREF GetBkColor() const;
	COLORREF SetTextColor(COLORREF clr);
	COLORREF GetTextColor() const;
	HFONT    SetFont(HFONT hFont, bool redraw = true);
	HFONT    GetFont() const;
	void     SetColumnSortArrow(int col, int dir);   // dir: 1=asc, -1=desc, 0=none
	void     SetAlternateRowColor(COLORREF clr);     // CLR_NONE to disable
	COLORREF GetAlternateRowColor() const;

	// ---- Editing ------------------------------------------------------------

	void    SetEditLabels(bool enable);
	bool    GetEditLabels() const;
	void    EditLabel(HTLITEM hItem);  // programmatically begin in-place edit

	// ---- Checkboxes ---------------------------------------------------------

	void    SetCheckBoxes(bool enable);
	bool    GetCheckBoxes() const;
	bool    GetCheckState(HTLITEM hItem) const;
	void    SetCheckState(HTLITEM hItem, bool checked);

	// ---- Image list ---------------------------------------------------------

	CImageList SetImageList(HIMAGELIST himl, int type = LVSIL_SMALL);
	CImageList GetImageList(int type = LVSIL_SMALL) const;

	// ---- Extended LV styles -------------------------------------------------

	DWORD GetExtendedStyle() const;
	void  SetExtendedStyle(DWORD dwExStyle);

	// ---- Direct ListView access for advanced use ----------------------------

	CListViewCtrl& GetListCtrl();

private:
	enum { IDC_LIST = 1 };

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDispInfo(int, LPNMHDR, BOOL&);
	LRESULT OnCustomDraw(int, LPNMHDR, BOOL&);
	LRESULT OnClick(int, LPNMHDR, BOOL&);
	LRESULT OnRClick(int, LPNMHDR, BOOL&);
	LRESULT OnDblClk(int, LPNMHDR, BOOL&);
	LRESULT OnFocusChange(int, LPNMHDR, BOOL&);
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
	LRESULT OnBeginLabelEdit(int, LPNMHDR, BOOL&);
	LRESULT OnEndLabelEdit(int, LPNMHDR, BOOL&);

	static LRESULT CALLBACK ListSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
	static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
	LRESULT OnColumnClick(int, LPNMHDR, BOOL&);
	LRESULT OnKeyDown(int, LPNMHDR, BOOL&);

	void    RebuildVisibleList();
	void    AppendVisible(TlvNode* node);
	void    ExpandRecursive(TlvNode* node, bool expand);
	void    ExpandToItem(TlvNode* node);
	int     VisibleIndex(HTLITEM hItem) const;
	int     CountNodes(const TlvNode* node) const;
	void    SortNode(TlvNode* parent, PFNTLVCOMPARE pfn, LPARAM lParam, bool recurse);
	void    LinkAfter(TlvNode* parent, TlvNode* node, TlvNode* after);
	void    Unlink(TlvNode* node);
	void    DeleteTree(TlvNode* node);
	LRESULT FireNotify(NMHDR* pnm);

	LRESULT RelayCustomDraw(NMLVCUSTOMDRAW* plvcd);
	void    DrawColumn0(NMLVCUSTOMDRAW* pcd);
	void    DrawGlyphManual(WTL::CDCHandle dc, const RECT& r, bool expanded);
	void    DrawTreeLines(WTL::CDCHandle dc, const RECT& rcCol0, TlvNode* node);
	void    DrawCheckBox(WTL::CDCHandle dc, const RECT& r, bool checked);

	CListViewCtrl         m_List;
	TlvNode               m_Root;             // sentinel; children are top-level items
	std::vector<TlvNode*> m_Visible;          // flat ordered list of visible nodes
	int                   m_ColCount    = 0;
	bool                  m_TreeMode    = true;
	bool                  m_TreeLines   = true;
	bool                  m_MultiSelect = false;
	bool                  m_InSelChange = false;
	bool                  m_EditLabels    = false;
	bool                  m_CheckBoxes    = false;
	bool                  m_CallbackMode  = false;
	ULONGLONG             m_SortableCols  = 0;
	int                   m_SortCol       = -1;   // column bearing the sort arrow
	int                   m_SortDir       = 0;    // 1=asc, -1=desc
	int                   m_EditTextLeft  = 0;   // desired left edge during label edit
	int                   m_EditRight     = 0;   // right edge of col 0 during label edit
	WTL::CTheme           m_Theme;             // TREEVIEW parts (expand/collapse glyph)
	WTL::CTheme           m_ThemeBtn;          // BUTTON parts (checkbox)
	int                   m_IndentWidth = 16;       // DPI-scaled via GetSystemMetricsForDpi
	COLORREF              m_ClrText     = CLR_DEFAULT;  // cached from CDDS_ITEMPREPAINT
	COLORREF              m_ClrTextBk   = CLR_DEFAULT;
	COLORREF              m_AltRowColor  = CLR_NONE; // CLR_NONE = disabled
	int                   m_UpdateCount  = 0;
};
