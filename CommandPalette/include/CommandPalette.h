#pragma once

// Command palette: a search box over a list of the app's commands. Typing filters the list by fuzzy matching (the
// typed letters found in order, best at word starts), the matched letters are highlighted, and Enter or a click runs
// the command (WM_COMMAND to the owner). With nothing typed the recently used commands come first. The commands are
// added one by one or read from a menu. The API follows the common controls: C-style structures and WM_NOTIFY codes
// (CMDPN_*) sent to the parent.

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

constexpr wchar_t WC_COMMANDPALETTE[] = L"WC_CommandPalette";

// ---- Styles --------------------------------------------------------------------

#define CMDPS_HIDEDISABLED   0x0001   // disabled commands aren't listed (else they are, dimmed, and don't run)
#define CMDPS_NORECENT       0x0002   // no "recently used" first with nothing typed (the list keeps its order)
#define CMDPS_NOHELP         0x0004   // '?' doesn't list the providers

// ---- Commands -----------------------------------------------------------------

#define CMDPIS_DISABLED      0x0001
#define CMDPIS_CHECKED       0x0002   // a check mark before the name (when it has no image)
#define CMDPIS_HIDDEN        0x0004   // not listed

struct CMDPITEM {
	UINT    id = 0;                   // sent with WM_COMMAND; unique (adding an id again replaces that command)
	LPCWSTR pszName = nullptr;
	LPCWSTR pszCategory = nullptr;    // shown before the name ("File: Open"), and matched with it; nullptr: none
	LPCWSTR pszShortcut = nullptr;    // shown at the right; nullptr: none
	int     iImage = -1;              // in the image list (SetImageList); -1: none
	UINT    state = 0;                // CMDPIS_*
	LPARAM  lParam = 0;
};

// ---- Providers ------------------------------------------------------------------
//
// Besides the commands the palette can list the app's items, picked by the text's prefix: '@' symbols, ':' a line
// number, ... The app says how many items a provider has (SetProviderItemCount), and the control asks for each one
// with CMDPN_GETITEM when it needs it, keeping what it gets (InvalidateProvider forgets it). The text after the prefix
// is sent with CMDPN_QUERY when it changes, then matched like the commands; a long list is gone through in slices on a
// timer, showing the results as they come. A provider with CMDPPF_NOFILTER isn't matched: it sets its items for the
// text in CMDPN_QUERY (a line number: one item), and they're listed as they are. Picking an item sends CMDPN_EXECUTE
// with the provider and the index (no WM_COMMAND). '?' lists the providers (and the commands); picking one types its
// prefix.

#define CMDPPF_NOFILTER      0x0001

struct NMCMDPGETITEM;

// Gives an item as CMDPN_GETITEM would (hdr isn't set), called directly: a long list's items come much faster than
// through a notification each, which every hook on the thread (and a popup's forwarding) slows down.
typedef void (CALLBACK* CMDPGETITEMPROC)(void* context, NMCMDPGETITEM* item);

struct CMDPPROVIDER {
	LPCWSTR pszPrefix = nullptr;      // not empty, not '?' (unless CMDPS_NOHELP)
	LPCWSTR pszName = nullptr;        // in the '?' list
	LPCWSTR pszPlaceholder = nullptr; // shown in the list while nothing follows the prefix and there are no items
	UINT    flags = 0;                // CMDPPF_*
	LPARAM  lParam = 0;
	CMDPGETITEMPROC pfnGetItem = nullptr;   // nullptr: CMDPN_GETITEM
	void*   pvContext = nullptr;      // for pfnGetItem
};

// ---- Colors ----------------------------------------------------------------------

struct CMDPCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crDimText;               // categories, shortcuts, disabled commands, the box's placeholder
	COLORREF crMatch;                 // the matched letters
	COLORREF crSelBack;               // the selected row
	COLORREF crSelText;
	COLORREF crHotBack;               // the row under the mouse
	COLORREF crBoxBack;               // the search box
	COLORREF crBoxText;
	COLORREF crBoxBorder;
	COLORREF crBoxFocusBorder;
};

// ---- Notifications -------------------------------------------------------------

#define CMDPN_FIRST          (0U - 3050U)   // outside the common controls ranges
#define CMDPN_EXECUTE        (CMDPN_FIRST - 0)   // NMCMDPITEM; a command is picked: non-zero return = done, no WM_COMMAND
#define CMDPN_SELCHANGED     (CMDPN_FIRST - 1)   // NMCMDPITEM; the selected row changed (id 0: none)
#define CMDPN_CANCEL         (CMDPN_FIRST - 2)   // NMHDR; Escape in the box
#define CMDPN_TEXTCHANGED    (CMDPN_FIRST - 3)   // NMHDR; the user changed the search text
#define CMDPN_CLOSED         (CMDPN_FIRST - 4)   // NMHDR; the popup closed (from CCommandPalettePopup only)
#define CMDPN_GETITEM        (CMDPN_FIRST - 5)   // NMCMDPGETITEM; a provider's item is needed
#define CMDPN_QUERY          (CMDPN_FIRST - 6)   // NMCMDPQUERY; the text after a provider's prefix changed
#define CMDPN_RESULTSCHANGED (CMDPN_FIRST - 7)   // NMHDR; the rows listed changed (filtered again, or more found)

struct NMCMDPITEM {
	NMHDR  hdr;
	UINT   id;                        // a command's; 0 for a provider's item
	LPARAM lParam;
	int    provider;                  // 0: a command
	size_t index;                     // the provider's item
};

// The app points the strings at the item's (they're copied when it returns) and may set the image and lParam. Also
// what a provider's pfnGetItem fills in.
struct NMCMDPGETITEM {
	NMHDR   hdr;
	int     provider;
	size_t  index;
	LPCWSTR pszName;                  // nullptr or "": the item isn't listed
	LPCWSTR pszCategory;              // shown dimmed before the name, and matched with it
	LPCWSTR pszShortcut;              // shown at the right (a detail: a file's folder, a symbol's kind)
	int     iImage;                   // -1 on entry
	LPARAM  lParam;
};

struct NMCMDPQUERY {
	NMHDR   hdr;
	int     provider;
	LPCWSTR pszText;                  // the text after the prefix
};

// ---- Control class --------------------------------------------------------------------------

struct CmpItem;
struct CmpProvider;
class CmpRecent;
class CmpSearch;
class CmpListWindow;

class CCommandPaletteCtrl : public ATL::CWindowImpl<CCommandPaletteCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_CommandPalette", 0, -1)

	CCommandPaletteCtrl();
	~CCommandPaletteCtrl();
	friend class CmpListWindow;

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CCommandPaletteCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the search box
		MESSAGE_HANDLER(WM_KEYDOWN, OnBoxKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnBoxChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnBoxGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnBoxFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnBoxFocus)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnListWheel)
	ALT_MSG_MAP(2)   // the list, a child window
		MESSAGE_HANDLER(WM_PAINT, OnListPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnListPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnListSize)
		MESSAGE_HANDLER(WM_VSCROLL, OnListVScroll)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnListWheel)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnListMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnListMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnListLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnListLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnListMouseActivate)
	END_MSG_MAP()

	// ---- Commands -----------------------------------------------------------------------

	BOOL    AddCommand(const CMDPITEM* item);         // an id already there is replaced
	BOOL    AddCommand(UINT id, LPCWSTR name, LPCWSTR category = nullptr, LPCWSTR shortcut = nullptr, int image = -1);
	BOOL    RemoveCommand(UINT id);
	void    ClearCommands();                          // menus added with AddFromMenu too
	int     GetCommandCount() const;
	BOOL    SetCommandState(UINT id, UINT state);     // CMDPIS_*
	UINT    GetCommandState(UINT id) const;           // (UINT)-1 if there's no such command
	// a menu's commands (and its submenus', their path as the category, after the given one); separators and items
	// with no id or a submenu are skipped, the names lose their & marks, the text after a tab is the shortcut, and
	// the items' grayed and checked states are kept. The menu is read again by RefreshMenus (it must outlive that).
	int     AddFromMenu(HMENU menu, LPCWSTR category = nullptr);   // the number of commands added
	// The menus' commands as they are now. With a window, it's first sent WM_INITMENUPOPUP for each submenu (as when
	// the menu opens), so an app that sets the items' states then (WTL's CUpdateUI) has them current.
	void    RefreshMenus(HWND initMenus = nullptr);
	void    SetImageList(HIMAGELIST images);          // small icons; the control doesn't destroy it

	// ---- Providers -----------------------------------------------------------------------------

	int     AddProvider(const CMDPPROVIDER* provider);   // its id (> 0); 0 if the prefix is empty or taken
	BOOL    RemoveProvider(int id);
	BOOL    SetProviderItemCount(int id, size_t count);  // its items are asked for again as they're needed
	BOOL    InvalidateProvider(int id);               // the same items, asked for again
	int     GetActiveProvider() const;                // the one the text picks: 0 for the commands, -1 for '?'
	bool    IsSearching() const;                      // going through a long list
	HIMAGELIST GetImageList() const;

	// ---- Search and selection --------------------------------------------------------------

	void    SetSearchText(LPCWSTR text);              // the list follows; no notification
	ATL::CString GetSearchText() const;
	void    SetPlaceholder(LPCWSTR text);             // shown in the empty box; default "Type a command"
	int     GetResultCount() const;                   // the rows listed now
	UINT    GetResultId(int row) const;               // 0 if out of range
	int     GetSelectedRow() const;                   // -1 if none
	void    SetSelectedRow(int row);                  // scrolled into view; no notification
	UINT    GetSelectedId() const;                    // 0 if none
	BOOL    ExecuteSelected();                        // as Enter; FALSE if none or disabled
	void    SetOwner(HWND owner);                     // gets WM_COMMAND; default (nullptr): the parent
	HWND    GetSearchBox() const;

	// ---- Recently used -------------------------------------------------------------------------
	//
	// A command run goes to the front; with nothing typed the list starts with them, the latest first. Saved as
	// text ("12;7;301").

	ATL::CString GetRecent() const;
	BOOL    SetRecent(LPCWSTR text);                  // FALSE (and none) if it isn't such a list
	void    ClearRecent();
	void    SetRecentLimit(int count);                // default 50

	// ---- Appearance and size ----------------------------------------------------------------
	//
	// The font is the message font at the window's DPI; WM_SETFONT replaces it (the app then replaces it for a new
	// DPI).

	void    SetColors(const CMDPCOLORS* colors);      // nullptr: from the system colors (follows WTLHelper dark mode)
	CMDPCOLORS GetColors() const;
	int     GetIdealHeight(int rows) const;           // the height that shows the box and that many rows

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColorEdit(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnBoxKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnBoxChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnBoxGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnBoxFocus(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnListPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListMouseActivate(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Layout();
	RECT    GetBoxRect() const;                       // the search box's frame, client coords
	void    Filter();                                 // the results for the search text, the first selected
	void    Search(DWORD ms);                         // goes on through the list for up to ms; then the order and the view
	const CmpItem* ItemOf(size_t index);              // the active list's (a provider's is asked for if it's needed)
	const CmpItem& RowItem(int row) const;            // the item a result row shows (asked for already)
	std::vector<struct CmpResult>& Results() const;
	CmpProvider* FindProvider(int id) const;
	void    BuildHelp();                              // the '?' list
	LRESULT NotifyItem(UINT code, int row);           // NMCMDPITEM for a row
	void    UpdateScrollBar();
	void    UpdateScrollBarTheme();
	void    ScrollTo(int top);
	void    EnsureVisible(int row);
	void    Select(int row, bool notify);
	int     RowAt(POINT pt) const;                    // in the list's coords; -1 if none
	int     PageRows() const;
	void    PaintRow(WTL::CDCHandle dc, int row, const RECT& rc, const CMDPCOLORS& c);
	void    Execute(int row);
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    ReadMenu(HMENU menu, std::vector<std::wstring>& path, int source);
	static void InitMenus(HWND window, HMENU menu);
	int     FindItem(UINT id) const;

	struct MenuSource {
		HMENU menu;
		std::wstring category;
	};

	std::vector<CmpItem> m_Items;
	std::unique_ptr<CmpSearch> m_Search;              // through the active list: its results are the rows
	std::vector<std::unique_ptr<CmpProvider>> m_Providers;
	std::vector<CmpItem> m_Help;                      // the '?' list
	int     m_Mode = 0;                               // the active list: 0 the commands, -1 '?', else a provider's id
	int     m_NextProvider = 1;
	int     m_QueryProvider = 0;                      // the last CMDPN_QUERY's
	std::wstring m_QueryText;
	bool    m_InQuery = false;                        // in CMDPN_QUERY: the app may set the count, which doesn't filter
	bool    m_UserSelected = false;                   // the user moved the selection while the search goes on
	std::unique_ptr<CmpRecent> m_Recent;
	std::vector<MenuSource> m_Menus;
	ATL::CContainedWindowT<WTL::CEdit> m_Box;
	std::unique_ptr<CmpListWindow> m_List;
	HIMAGELIST m_Images = nullptr;
	HWND    m_Owner = nullptr;
	ATL::CString m_Placeholder = L"Type a command";
	bool    m_SettingText = false;                    // SetSearchText: no CMDPN_TEXTCHANGED

	CMDPCOLORS m_Colors{};
	bool    m_SystemColors = true;
	int     m_DarkScrollBar = -1;
	WTL::CBrush m_BoxBrush;
	COLORREF m_BoxBrushColor = CLR_INVALID;

	HFONT   m_hFont = nullptr;
	bool    m_OwnFont = true;
	WTL::CFont m_BoldFont;
	UINT    m_Dpi = 96;
	int     m_TextHeight = 16;
	int     m_RowHeight = 24;
	int     m_BoxHeight = 28;
	int     m_Pad = 6;
	int     m_IconSize = 16;

	int     m_Selected = -1;
	int     m_Hot = -1;
	int     m_Pressed = -1;
	int     m_Top = 0;                                // the first row in view
	int     m_WheelRemainder = 0;
	bool    m_TrackingMouse = false;
};

// ---- The popup ------------------------------------------------------------------------------
//
// A command palette in a popup over the owner window, at the top of its client area, as tall as the results need (up
// to a number of rows). It closes on Escape, when a command is picked, or when it loses the activation, and sends
// CMDPN_CLOSED; the palette's notifications go on to the owner (CMDPN_EXECUTE's answer counts). Closing only hides
// it: the commands and the recently used stay for the next Show. Showing reads the palette's menus again.

class CCommandPalettePopup : public ATL::CWindowImpl<CCommandPalettePopup, ATL::CWindow, ATL::CWinTraits<WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW>> {
public:
	DECLARE_WND_CLASS_EX(L"CmpPopup", CS_DROPSHADOW, -1)

	BEGIN_MSG_MAP(CCommandPalettePopup)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(CCommandPaletteCtrl::ThemeChangedMessage, OnThemeChanged)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnThemeChanged)
	END_MSG_MAP()

	// owner: gets the notifications and WM_COMMAND (its top-level window owns the popup); id: the palette's, in them
	BOOL    Create(HWND owner, UINT id = 1);
	CCommandPaletteCtrl& Palette() {
		return m_Palette;
	}
	// text: the search text to start with (selected, so typing replaces it); nullptr: the last one
	BOOL    Show(LPCWSTR text = L"");
	void    Close();                                  // as Escape
	bool    IsOpen() const;
	void    SetMaxRows(int rows);                     // default 12
	void    SetWidth(int dips);                       // the most it takes (less in a narrow owner); default 600

private:
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnActivate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnThemeChanged(UINT, WPARAM, LPARAM, BOOL&);

	RECT    TargetRect() const;                       // where it goes, screen coords, sized to the results
	void    UpdateFrame();                            // the window's corners, border and dark caption colors

	CCommandPaletteCtrl m_Palette;
	HWND    m_Owner = nullptr;
	int     m_MaxRows = 12;
	int     m_Width = 600;
	int     m_Border = 1;                             // drawn by the popup (0 when Windows draws it)
	bool    m_Closing = false;
};
