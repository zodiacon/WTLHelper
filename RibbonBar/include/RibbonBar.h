#pragma once

// Ribbon bar: a row of tabs, each a strip of labeled groups of commands: large buttons (an image over a label), small
// ones (an image beside it) and icons, toggles, radio items, drop-down and split buttons. As the window narrows the
// groups shrink, the rightmost first: large items become small, small ones icons, and at last a group folds into one
// button. A click posts WM_COMMAND with the item's id to the owner, as a menu does; the app sets the items' states
// through the API, or when RIBN_UPDATEUI asks before they're painted. The API follows the common controls: C-style
// structures and WM_NOTIFY codes (RIBN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <memory>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_RIBBONBAR[] = L"WC_RibbonBar";

// ---- Items --------------------------------------------------------------------

#define RIBIK_BUTTON         0
#define RIBIK_TOGGLE         1        // checked and unchecked by a click
#define RIBIK_RADIO          2        // checks itself, unchecks the tab's others with its radio number
#define RIBIK_DROPDOWN       3        // the whole button opens its menu
#define RIBIK_SPLIT          4        // runs its command; its arrow part opens the menu
#define RIBIK_WINDOW         5        // the app's window, hosted in the group
#define RIBIK_SEPARATOR      6

// the sizes an item is shown at, largest first
#define RIBSZ_LARGE          0        // the image over the label
#define RIBSZ_SMALL          1        // the image beside the label
#define RIBSZ_ICON           2        // the image alone

#define RIBIS_DISABLED       0x0001
#define RIBIS_CHECKED        0x0002
#define RIBIS_HIDDEN         0x0004

// An item's id is its command (WM_COMMAND's). Several items may share one (the same command on two tabs): the state
// API changes them all.
struct RIBITEM {
	UINT    id = 0;
	UINT    kind = RIBIK_BUTTON;      // RIBIK_*
	LPCWSTR pszText = nullptr;
	LPCWSTR pszTip = nullptr;         // the tooltip's description, under the label
	LPCWSTR pszKeyTip = nullptr;      // nullptr: one is chosen from the label
	int     iImage = -1;              // in the large image list
	int     iSmallImage = -1;         // in the small one
	UINT    state = 0;                // RIBIS_*
	int     radio = 0;                // RIBIK_RADIO: the items of a tab with the same number are one group
	UINT    size = RIBSZ_LARGE;       // the largest it's shown at
	UINT    minSize = RIBSZ_ICON;     // the smallest it goes down to (an item without an image stops at small)
	HMENU   hMenu = nullptr;          // RIBIK_DROPDOWN, RIBIK_SPLIT: its commands; the app keeps it
	HWND    hwnd = nullptr;           // RIBIK_WINDOW: made a child of the ribbon
	int     cxWindow = 0;             // RIBIK_WINDOW: its width, in DIPs (the label, pszText, goes before it)
	int     cRows = 1;                // RIBIK_WINDOW: its height in rows (1 to 3)
	LPARAM  lParam = 0;
};

// ---- Groups and tabs -------------------------------------------------------------

#define RIBGF_NOCOLLAPSE     0x0001   // never folds into one button
#define RIBGF_LAUNCHER       0x0002   // a dialog launcher beside the label: WM_COMMAND with uLauncherId

struct RIBGROUP {
	UINT    id = 0;                   // unique among the groups
	LPCWSTR pszText = nullptr;
	LPCWSTR pszKeyTip = nullptr;      // its folded button's
	int     iImage = -1;              // the folded button's, in the large image list (-1: its first item's)
	UINT    flags = 0;                // RIBGF_*
	int     priority = 0;             // lower shrinks first, all the way, before any group of a higher one starts
	UINT    uLauncherId = 0;
};

struct RIBTAB {
	UINT    id = 0;                   // unique among the tabs
	LPCWSTR pszText = nullptr;
	LPCWSTR pszKeyTip = nullptr;
};

// ---- Colors ----------------------------------------------------------------------

struct RIBCOLORS {
	COLORREF crBack;                  // the tab row
	COLORREF crTabText;               // the tabs' names (the hot tab's background is a blend of the two)
	COLORREF crPanel;                 // the selected tab's groups
	COLORREF crText;
	COLORREF crDimText;               // group labels, disabled items
	COLORREF crLine;                  // between the groups, under the panel
	COLORREF crHotBack;               // an item under the mouse
	COLORREF crPressedBack;
	COLORREF crCheckedBack;           // toggles and radio items checked
	COLORREF crCheckedBorder;
	COLORREF crAccent;                // the selected tab's text and its mark
};

// ---- Notifications -------------------------------------------------------------

#define RIBN_FIRST           (0U - 3100U)   // outside the common controls ranges
#define RIBN_EXECUTE         (RIBN_FIRST - 0)   // NMRIBITEM; an item clicked (a toggle already flipped): non-zero = done, no WM_COMMAND
#define RIBN_TABCHANGED      (RIBN_FIRST - 1)   // NMRIBTAB; the user selected a tab
#define RIBN_UPDATEUI        (RIBN_FIRST - 2)   // NMRIBTAB; the tab's items are about to be painted: set their states now
#define RIBN_HEIGHTCHANGED   (RIBN_FIRST - 3)   // NMHDR; GetIdealHeight changed (DPI, font): lay the window out again

struct NMRIBITEM {
	NMHDR  hdr;
	UINT   id;
	UINT   kind;
	UINT   state;                     // after the click
	LPARAM lParam;
};

struct NMRIBTAB {
	NMHDR  hdr;
	UINT   id;
};

// ---- Control class --------------------------------------------------------------------------

struct RibData;

class CRibbonBarCtrl : public ATL::CWindowImpl<CRibbonBarCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_RibbonBar", CS_DBLCLKS, -1)

	CRibbonBarCtrl();
	~CRibbonBarCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CRibbonBarCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Tabs, groups, items -------------------------------------------------------------------

	BOOL    AddTab(const RIBTAB* tab, int insertAt = -1);         // FALSE if the id is taken
	BOOL    AddTab(UINT id, LPCWSTR text);
	BOOL    RemoveTab(UINT id);                                   // its groups and items too
	int     GetTabCount() const;
	UINT    GetTabId(int index) const;                            // 0 if out of range
	BOOL    SelectTab(UINT id);                                   // no notification
	UINT    GetSelectedTab() const;                               // 0 if none

	BOOL    AddGroup(UINT tabId, const RIBGROUP* group);          // at the tab's end; FALSE if the id is taken
	BOOL    AddGroup(UINT tabId, UINT id, LPCWSTR text, UINT flags = 0);
	BOOL    RemoveGroup(UINT id);

	BOOL    AddItem(UINT groupId, const RIBITEM* item);           // at the group's end
	BOOL    AddButton(UINT groupId, UINT id, LPCWSTR text, int image, UINT size = RIBSZ_LARGE, UINT kind = RIBIK_BUTTON);
	BOOL    AddSeparator(UINT groupId);
	BOOL    RemoveItem(UINT id);                                  // every item with the id

	BOOL    SetItemState(UINT id, UINT state, UINT mask = (UINT)-1);    // RIBIS_*; every item with the id
	UINT    GetItemState(UINT id) const;                          // (UINT)-1 if there's no such item
	BOOL    EnableItem(UINT id, bool enable = true);
	BOOL    CheckItem(UINT id, bool check = true);                // a radio item: its group's others unchecked
	BOOL    SetItemText(UINT id, LPCWSTR text);
	// Sends RIBN_UPDATEUI for the selected tab now (it's also sent before the items are painted): an app's idle
	// handler can call it, so states that follow the app's (a selection, the clipboard) show without a repaint first.
	void    UpdateUI();

	// ---- Appearance and size -------------------------------------------------------------------------
	//
	// Images: a large list (32x32 at 96 DPI) and a small one (16x16); their sizes set the items' (the app gives lists
	// for the DPI, WM_DPICHANGED). The font is the message font at the window's DPI; WM_SETFONT replaces it.

	void    SetImageLists(HIMAGELIST large, HIMAGELIST smallImages);   // the control doesn't destroy them
	void    SetColors(const RIBCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	RIBCOLORS GetColors() const;
	int     GetIdealHeight() const;                               // the tab row and the groups
	void    SetOwner(HWND owner);                                 // gets WM_COMMAND; default (nullptr): the parent

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Changed(bool measure);                                // the layout (and the sizes) to be found again
	void    EnsureLayout();
	void    NotifyHeight();
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    SendUpdateUI(bool painting);                          // painting: what the app changes isn't repainted again
	void    Execute(int group, int item);
	void    SetHot(int tab, int group, int item, int part);
	void    UserSelectTab(int visible);                           // a tab in the row, by its place: notified

	// painting (RibbonBarPaint.cpp)
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintTabs(WTL::CDCHandle dc, const RIBCOLORS& c);
	void    PaintGroup(WTL::CDCHandle dc, int group, int x, const RIBCOLORS& c);
	void    PaintItem(WTL::CDCHandle dc, int group, int item, const RECT& rc, const RIBCOLORS& c);
	void    PaintImage(WTL::CDCHandle dc, int image, bool large, int x, int y, bool disabled);

	std::unique_ptr<RibData> m_Data;
	HIMAGELIST m_Large = nullptr;
	HIMAGELIST m_Small = nullptr;
	HWND    m_Owner = nullptr;

	RIBCOLORS m_Colors{};
	bool    m_SystemColors = true;

	HFONT   m_hFont = nullptr;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_TextHeight = 16;
	int     m_TabHeight = 28;
	int     m_LastHeight = 0;                                     // the height last told (RIBN_HEIGHTCHANGED)
	bool    m_InUpdate = false;                                   // in RIBN_UPDATEUI
	bool    m_NoInvalidate = false;                               // in RIBN_UPDATEUI sent while painting
	bool    m_TrackingMouse = false;
};
