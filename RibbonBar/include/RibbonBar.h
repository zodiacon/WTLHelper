#pragma once

// Ribbon bar: a row of tabs, each a strip of labeled groups of commands: large buttons (an image over a label), small
// ones (an image beside it) and icons, toggles, radio items, drop-down and split buttons, and the app's own windows (a
// combo box). As the window narrows the groups shrink, the rightmost first: large items become small, small ones
// icons, and at last a group folds into one button that opens it in a popup. A click posts WM_COMMAND with the item's
// id to the owner, as a menu does; the app sets the items' states through the API, or when RIBN_UPDATEUI asks before
// they're painted. The ribbon can be minimized to its tabs (a tab then opens as a popup), and contextual tabs show only
// while the app says. A quick access bar at the left of the tabs holds the commands the user adds (right-click an
// item). Alt shows key tips (the app passes its messages through PreTranslateMessage), and the arrow keys move between
// the commands. GetState/SetState save and restore the tab, minimizing and the quick access bar. The API follows the
// common controls: C-style structures and WM_NOTIFY codes (RIBN_*) sent to the parent.

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

constexpr wchar_t WC_RIBBONBAR[] = L"WC_RibbonBar";

// ---- Styles --------------------------------------------------------------------

#define RIBS_NOMINIMIZE      0x0001   // no minimize button; double-clicking a tab doesn't minimize
#define RIBS_NOQAT           0x0002   // no quick access bar

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
	// RIBIK_WINDOW: a child window, made the ribbon's (or a popup's while one shows its group), shown and hidden with
	// its group and disabled with the item; its notifications go on to the ribbon's parent. pszText is a label before
	// it, dropped when the group shrinks.
	HWND    hwnd = nullptr;
	int     cxWindow = 0;             // its width, in DIPs
	int     cRows = 1;                // its height in rows (1 to 3); a combo box is centered in its rows
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
	int     context = 0;              // 0: always shown; else its contextual set's id (AddContext)
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
#define RIBN_HEIGHTCHANGED   (RIBN_FIRST - 3)   // NMHDR; GetIdealHeight changed (DPI, font, minimized): lay the window out again
#define RIBN_DROPDOWN        (RIBN_FIRST - 4)   // NMRIBDROPDOWN; a drop-down or a split button's arrow pressed
#define RIBN_QATCHANGED      (RIBN_FIRST - 5)   // NMHDR; the user added or removed a quick access command
#define RIBN_CONTEXTMENU     (RIBN_FIRST - 6)   // NMRIBCONTEXTMENU; the ribbon's menu is about to open

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

// Before the item's menu opens: the app may fill it, or set another (it's the app's). A non-zero return: the app
// showed its own UI (at rcItem), no menu. A command picked from the menu is posted as WM_COMMAND to the owner; the
// owner gets the menu's messages (WM_INITMENUPOPUP, WM_MENUSELECT) while it's open.
struct NMRIBDROPDOWN {
	NMHDR  hdr;
	UINT   id;
	HMENU  hMenu;
	RECT   rcItem;                    // screen coords
	LPARAM lParam;
};

// The ribbon's own menus: right-click (or the Apps key while navigating), and the quick access bar's button. The app may
// change, remove or add items (its own are posted as WM_COMMAND to the owner); a non-zero return: the app showed its own
// menu (at pt), not this one.
#define RIBCM_RIBBON         0        // the tab row, a group's label
#define RIBCM_ITEM           1        // an item: id
#define RIBCM_QAT            2        // a quick access button: id
#define RIBCM_CUSTOMIZE      3        // the quick access bar's button: its commands, checked

// the ribbon's commands in those menus
#define RIBCMD_ADDTOQAT      0xFE00
#define RIBCMD_REMOVEFROMQAT 0xFE01
#define RIBCMD_MINIMIZE      0xFE02
#define RIBCMD_QATFIRST      0xFE10   // RIBCM_CUSTOMIZE: the bar's commands, in order (unchecking one removes it)

struct NMRIBCONTEXTMENU {
	NMHDR  hdr;
	UINT   where;                     // RIBCM_*
	UINT   id;                        // the item's
	HMENU  hMenu;                     // the ribbon's: destroyed after
	POINT  pt;                        // screen coords
};

// ---- Control class --------------------------------------------------------------------------

struct RibData;
struct RibSurface;
struct RibHit;
struct RibKeyTarget;
class RibPopup;
class RibTipWindow;
class RibAccessible;
struct RibItem;

class CRibbonBarCtrl : public ATL::CWindowImpl<CRibbonBarCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_RibbonBar", CS_DBLCLKS, -1)

	CRibbonBarCtrl();
	~CRibbonBarCtrl();
	friend class RibPopup;
	friend class RibTipWindow;
	friend class RibAccessible;

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CRibbonBarCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYUP, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSCHAR, OnKeyDown)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_RIB_CLOSEPOPUPS, OnClosePopups)
		MESSAGE_HANDLER(WM_RIB_ACCACTION, OnAccAction)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		// the hosted windows' messages, for the parent
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnForward)
		MESSAGE_HANDLER(WM_DRAWITEM, OnForward)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnForward)
		MESSAGE_HANDLER(WM_COMPAREITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnForward)
	END_MSG_MAP()

	// ---- Tabs, groups, items -------------------------------------------------------------------

	BOOL    AddTab(const RIBTAB* tab, int insertAt = -1);         // FALSE if the id is taken
	BOOL    AddTab(UINT id, LPCWSTR text, int context = 0);
	BOOL    RemoveTab(UINT id);                                   // its groups and items too
	int     GetTabCount() const;
	UINT    GetTabId(int index) const;                            // 0 if out of range
	BOOL    SelectTab(UINT id);                                   // no notification; FALSE if it isn't shown
	UINT    GetSelectedTab() const;                               // 0 if none

	BOOL    AddGroup(UINT tabId, const RIBGROUP* group);          // at the tab's end; FALSE if the id is taken
	BOOL    AddGroup(UINT tabId, UINT id, LPCWSTR text, UINT flags = 0);
	BOOL    RemoveGroup(UINT id);

	BOOL    AddItem(UINT groupId, const RIBITEM* item);           // at the group's end
	BOOL    AddButton(UINT groupId, UINT id, LPCWSTR text, int image, UINT size = RIBSZ_LARGE, UINT kind = RIBIK_BUTTON);
	BOOL    AddSeparator(UINT groupId);
	BOOL    RemoveItem(UINT id);                                  // every item with the id (a window is hidden, not destroyed)

	BOOL    SetItemState(UINT id, UINT state, UINT mask = (UINT)-1);    // RIBIS_*; every item with the id
	UINT    GetItemState(UINT id) const;                          // (UINT)-1 if there's no such item
	BOOL    EnableItem(UINT id, bool enable = true);
	BOOL    CheckItem(UINT id, bool check = true);                // a radio item: its group's others unchecked
	BOOL    SetItemText(UINT id, LPCWSTR text);
	BOOL    SetItemMenu(UINT id, HMENU menu);
	// Sends RIBN_UPDATEUI for the selected tab now (it's also sent before the items are painted): an app's idle
	// handler can call it, so states that follow the app's (a selection, the clipboard) show without a repaint first.
	void    UpdateUI();

	// ---- Contextual tabs ------------------------------------------------------------------------------
	//
	// A set of tabs (RIBTAB::context) shown while the app says, after the others, marked in the set's color: "Picture
	// Tools" while a picture is selected. The selected tab hidden with its set: the first tab is selected.

	BOOL    AddContext(int id, LPCWSTR name, COLORREF color);     // id > 0
	BOOL    ShowContext(int id, bool show, bool select = false);  // select: its first tab
	bool    IsContextShown(int id) const;

	// ---- Minimized ----------------------------------------------------------------------------------------
	//
	// Only the tab row shows; clicking a tab opens its groups in a popup over the window. The user minimizes with the
	// button at the right of the tabs or by double-clicking a tab (unless RIBS_NOMINIMIZE).

	void    SetMinimized(bool minimized);                         // RIBN_HEIGHTCHANGED follows
	bool    IsMinimized() const;
	void    ClosePopups();                                        // a folded group's, a minimized tab's

	// ---- Quick access bar ------------------------------------------------------------------------------
	//
	// Small buttons at the left of the tab row for items the user picks (right-click: Add to Quick Access Toolbar), each
	// the first item with the id: its small image (else its text), its state. A drop-down opens its menu; a split button
	// runs its command. Windows can't be added. The user's changes send RIBN_QATCHANGED; the API's don't.

	BOOL    AddToQat(UINT id, int insertAt = -1);                 // FALSE if there's no such item, it's a window, or it's there
	BOOL    RemoveFromQat(UINT id);
	bool    IsInQat(UINT id) const;
	int     GetQatItems(UINT* ids, int count) const;              // the count; ids may be nullptr

	// ---- Keyboard ----------------------------------------------------------------------------------------
	//
	// Alt (pressed and released) or F10 shows key tips on the tabs and the quick access buttons; typing one goes into a
	// tab, whose items then show theirs (a folded group opens). Alt+a tab's key tip goes straight there. The arrow keys
	// and Tab move a focus between the tabs and the commands; Enter or Space runs it, Escape goes back. The ribbon takes
	// the focus meanwhile and gives it back after. The app's message loop passes its messages here first (WTL:
	// CMessageFilter::PreTranslateMessage), so Alt reaches the ribbon wherever the focus is; ShowKeyTips does it without.

	BOOL    PreTranslateMessage(MSG* pMsg);                       // TRUE: handled, don't dispatch it
	void    ShowKeyTips(bool show = true);
	bool    IsKeyboardActive() const;                             // key tips or the focus showing

	// ---- State ----------------------------------------------------------------------------------------------
	//
	// The selected tab, minimized, the quick access bar: as text ("tab=3;min=0;qat=101,102"), to keep between runs.
	// SetState goes after the items are added (quick access ids of no item are dropped); it doesn't notify, but
	// RIBN_HEIGHTCHANGED follows if it minimizes. FALSE (nothing changed) for text it can't read.

	int     GetState(LPWSTR text, int cch) const;                 // the length with the terminator; text may be nullptr
	BOOL    SetState(LPCWSTR text);

	// ---- Appearance and size -------------------------------------------------------------------------
	//
	// Images: a large list (32x32 at 96 DPI) and a small one (16x16); their sizes set the items' (the app gives lists
	// for the DPI, WM_DPICHANGED). The font is the message font at the window's DPI; WM_SETFONT replaces it.

	void    SetImageLists(HIMAGELIST large, HIMAGELIST smallImages);   // the control doesn't destroy them
	void    SetColors(const RIBCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	RIBCOLORS GetColors() const;
	int     GetIdealHeight() const;                               // the tab row and the groups (minimized: the tabs)
	void    SetOwner(HWND owner);                                 // gets WM_COMMAND; default (nullptr): the parent
	// High contrast: the system's colors (over the app's too), the hot and checked states framed in the highlight color.
	void    ForceHighContrast(int on);                            // for testing: 1 on, 0 off, -1 (the default) as Windows says
	bool    IsHighContrast() const;

private:
	enum : UINT { WM_RIB_CLOSEPOPUPS = WM_USER + 100, WM_RIB_ACCACTION };

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnClosePopups(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnAccAction(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Changed(bool measure);                                // the layout (and the sizes) to be found again
	void    EnsureLayout();
	void    LayoutSurface(RibSurface& s);                         // its groups' places, for its width
	void    PlaceWindows();                                       // the hosted windows, where their groups show
	void    NotifyHeight();
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	HWND    Owner() const;
	void    SendUpdateUI(bool painting);                          // painting: what the app changes isn't repainted again
	void    Execute(int tab, int group, int item);
	int     TabAt(POINT pt) const;                                // by its place in the row; -1 none, MinimizeButton
	void    SetHotTab(int tab);
	void    UserSelectTab(int visible);                           // a tab in the row, by its place: notified
	void    TabClicked(int visible);
	bool    JustClosed(HWND from, int tab, int group) const;      // that popup closed by this very click

	// a surface: the ribbon's groups, or a popup's
	LRESULT SurfaceMouse(RibSurface& s, UINT msg, WPARAM wParam, LPARAM lParam);
	RibHit  HitSurface(const RibSurface& s, POINT pt) const;
	RECT    ItemRect(const RibSurface& s, int group, int item) const;   // client coords of s
	void    DropDown(RibSurface& s, int group, int item);
	UINT    ItemMenu(UINT id, HMENU menu, LPARAM lParam, HWND hwnd, const RECT& rc);   // RIBN_DROPDOWN, the menu: the pick
	UINT    TrackMenu(HMENU menu, HWND hwnd, const RECT& rc, POINT screen);   // rc (client of hwnd): the button, or empty
	void    RunLauncher(const RibSurface& s, int group);

	// the quick access bar, the ribbon's menus, the state (RibbonBarQat.cpp)
	void    LayoutQat(int& x);                                    // from x in the tab row; x after it
	int     QatAt(POINT pt) const;                                // in qat, RibData::Customize, or -1
	void    SetHotQat(int index);
	void    InvalidateTabRow();
	void    QatMouseDown(int index);
	void    QatDropDown(int index);
	void    RunQat(int index);
	void    CustomizeMenu();
	void    ContextMenu(HWND hwnd, LPARAM lParam);
	void    RunMenu(HMENU menu, UINT where, UINT id, HWND hwnd, const RECT& rc, POINT screen);
	void    NotifyQat();

	// the keyboard (RibbonBarKeys.cpp)
	bool    KeyDown(UINT msg, UINT vk, LPARAM lParam);            // a key while the ribbon or a popup has the focus
	void    EnterKeyMode();
	HWND    ExitKeyMode(bool restore);                            // the window the focus was on
	void    RestoreFocus(HWND window);                            // unless a popup is open
	void    KeyLevel(HWND level);                                 // key tips: the tab row (nullptr) or a surface's
	void    NavLevel(HWND level);                                 // navigating: the tab row and the ribbon (nullptr), or a popup
	void    AddSurfaceTargets(std::vector<RibKeyTarget>& out, RibSurface& s, std::vector<std::wstring>* labels, std::vector<std::wstring>* fixed);
	void    AddRowTargets(std::vector<RibKeyTarget>& out, std::vector<std::wstring>* labels, std::vector<std::wstring>* fixed);
	std::vector<std::wstring> RowTips();
	void    ActivateTarget(int index);
	void    KeyBack();
	void    StartNavigating(UINT vk);                             // from the key tips (vk: an arrow key to move by, or 0)
	void    MoveFocus(UINT vk);
	void    SetFocusTarget(int index);
	int     FindTarget(int kind, int index, int group, HWND hwnd) const;
	RECT    TargetRect(const RibKeyTarget& t) const;              // client coords of its window
	RibSurface* SurfaceOf(HWND hwnd) const;
	bool    IsOurWindow(HWND hwnd) const;                         // the ribbon or an open popup
	void    InvalidateAll();
	void    ShowTips();                                           // the tip windows for the targets (none unless key tips)
	void    FilterTips();                                         // only those that start with what's typed
	void    DestroyTips();
	void    OpenGroupPopup(RibSurface& from, int group);
	void    OpenTabPopup();
	RibPopup* ShowPopup(std::unique_ptr<RibPopup> popup, HWND owner, RECT rc);   // rc: screen, kept on the monitor
	void    HidePopups(size_t from);                                // hidden now, destroyed on WM_RIB_CLOSEPOPUPS
	void    DestroyHidden();

	// painting (RibbonBarPaint.cpp)
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintTabs(WTL::CDCHandle dc, const RIBCOLORS& c);
	void    PaintSurface(WTL::CDCHandle dc, RibSurface& s, const RIBCOLORS& c);
	void    PaintGroup(WTL::CDCHandle dc, RibSurface& s, int group, int x, const RIBCOLORS& c);
	void    PaintItem(WTL::CDCHandle dc, RibSurface& s, int group, int item, const RECT& rc, const RIBCOLORS& c);
	void    PaintImage(WTL::CDCHandle dc, int image, bool large, int x, int y, bool disabled);
	void    PaintQat(WTL::CDCHandle dc, const RIBCOLORS& c);
	RIBCOLORS PaintColors();                                      // GetColors, and high contrast for the painting

	// MSAA (RibbonBarAccessible.cpp): the ribbon's window and each popup's
	LRESULT AccObject(HWND hwnd, RibAccessible*& slot, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
	void    DisconnectAccessible(RibAccessible*& slot);
	void    NotifyAccessible(HWND hwnd, DWORD event, long child);  // once a client has asked
	std::vector<RibKeyTarget> AccChildren(HWND hwnd);
	long    AccChildId(const RibKeyTarget& t);                    // CHILDID_SELF if it isn't one
	long    AccFocus(HWND hwnd);
	RibItem* AccItem(const RibKeyTarget& t);                      // an item's or a quick access button's
	std::wstring AccWindowName(HWND hwnd);
	std::wstring AccName(const RibKeyTarget& t);
	std::wstring AccDescription(const RibKeyTarget& t);
	long    AccState(const RibKeyTarget& t);
	std::wstring AccShortcut(const RibKeyTarget& t);
	void    PostAccAction(const RibKeyTarget& t);
	void    NotifyItemState(UINT id);

	// the tooltip: one tool for each surface's window, on what's hot there (RibbonBarQat.cpp)
	void    AddTipTool(HWND hwnd);
	void    RemoveTipTool(HWND hwnd);
	void    UpdateTip(HWND hwnd);                                 // the hot thing changed
	RibKeyTarget HotTarget(HWND hwnd);                            // hwnd nullptr: nothing hot
	bool    TipNotify(NMHDR* hdr);                                // TRUE: the tooltip's
	void    PaintKeys(WTL::CDCHandle dc, HWND hwnd, const RIBCOLORS& c);   // the keyboard's focus on hwnd
	void    PaintTip(WTL::CDCHandle dc, const RECT& rc, const std::wstring& text, bool disabled);

	std::unique_ptr<RibData> m_Data;
	HIMAGELIST m_Large = nullptr;
	HIMAGELIST m_Small = nullptr;
	HWND    m_Owner = nullptr;
	bool    m_Minimized = false;

	RIBCOLORS m_Colors{};
	bool    m_SystemColors = true;
	int     m_ForceHighContrast = -1;
	HWND    m_hTip = nullptr;
	std::wstring m_TipText;

	HFONT   m_hFont = nullptr;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_TextHeight = 16;
	int     m_TabHeight = 28;
	int     m_LastHeight = 0;                                     // the height last told (RIBN_HEIGHTCHANGED)
	bool    m_InUpdate = false;                                   // in RIBN_UPDATEUI
	bool    m_NoInvalidate = false;                               // in RIBN_UPDATEUI sent while painting
};
