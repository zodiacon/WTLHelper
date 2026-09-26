#pragma once

#include "DockLayout.h"
#include "DockGeometry.h"
#include "DockTheme.h"
#include "DockStore.h"

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace WTLDock {

class CDockGroupWnd;
class CDockFloatFrame;
class SplitterTracker;
class DockDragSession;
struct DropTarget;

// Things the UI does to a pane (also reachable from the tab and caption context menu).
enum class DockCommand {
	Close,			// the pane
	CloseOthers,	// the other panes of its group
	CloseAll,		// all panes of its group
	AutoHide,		// its group
	Float,			// the pane, into a window of its own
	Dock,			// the group of a floating or auto-hidden pane, docked (at its edge of the window)
};

// A snapshot of a group's tab strip.
struct TabStripState {
	int First;		// index of the first visible tab
	int Visible;	// number of visible tabs
	bool Overflow;	// not every tab fits (a drop-down button is shown)
};

//
// The docking window. Make it the client window of a frame, register panes with Layout().AddPane (PaneDesc::hWnd is
// the content window) and place them with Layout().Show / DockTo / ...
//
//   m_hWndClient = m_Dock.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
//   auto view = ...Create(m_Dock, ...);			// content windows are child windows; any parent will do, the host adopts them
//   PaneDesc d; d.Id = L"tree"; d.Title = L"Tree"; d.hWnd = view; d.DefaultSide = DockSide::Left;
//   m_Dock.Layout().Show(m_Dock.Layout().AddPane(d));
//
// The host keeps one child window (CDockGroupWnd) per group and mirrors the layout onto them after every change.
// Groups of floating windows live in frames (CDockFloatFrame): owned tool windows with a native title bar, one per
// floating tree of the layout. Content windows of panes that are hidden or auto-hidden are hidden.
// Messages that content windows send to their parent (WM_COMMAND, WM_NOTIFY, WM_CTLCOLOR*, ...) are forwarded to the
// notify target, which is the host's parent (the frame) unless changed.
//
// The content windows are destroyed together with the host.
//
class CDockHost : public ATL::CWindowImpl<CDockHost> {
public:
	DECLARE_WND_CLASS_EX(L"WTLDock_Host", CS_DBLCLKS, 0)

	CDockHost();
	~CDockHost();

	DockLayout& Layout() {
		return m_Layout;
	}
	const DockTheme& Theme() const {
		return m_Theme;
	}
	void SetTheme(const DockTheme& theme);
	const DockMetrics& Metrics() const {
		return m_Metrics;
	}
	int Dpi() const {
		return m_Dpi;
	}
	HFONT Font() const {
		return m_Font;
	}
	HFONT BoldFont() const {
		return m_BoldFont;
	}

	// the pane that has (or last had) the keyboard focus
	DockPane* ActivePane() const {
		return m_ActiveId.empty() ? nullptr : m_Layout.FindPane(m_ActiveId);
	}
	// Makes the pane the visible tab of its group and moves the focus into it.
	void ActivatePane(DockPane* pane);
	bool IsActive(const DockGroup* group) const;

	HWND NotifyTarget() const {
		return m_NotifyTarget ? m_NotifyTarget : ::GetParent(m_hWnd);
	}
	void SetNotifyTarget(HWND hWnd) {
		m_NotifyTarget = hWnd;
	}

	// Closing. ClosePane is what the close buttons do: it needs PaneCaps::CanClose, asks OnPaneClosing (return false
	// to keep the pane, e.g. after "save changes?") and hides the pane, activating a neighbour. The window of a
	// closed pane is kept; destroy it yourself, typically together with RemovePane in OnPaneClosed.
	bool ClosePane(DockPane* pane);
	bool CanExecute(DockCommand command, const DockPane* pane) const;
	bool Execute(DockCommand command, DockPane* pane);
	// Pops up the context menu of a tab or caption (screen coordinates).
	void ShowPaneMenu(DockPane* pane, POINT screen);

	//
	// Floating. The layout stores a floating window's rectangle (outer, in screen coordinates); the host makes a
	// frame window for it and keeps the two in step: moving or sizing the frame updates the layout and the other way round.
	//
	// Floats a docked tool pane (with its tab group if it is alone in it). Without a rectangle the pane goes where it
	// floated last, or else next to where it was docked.
	bool FloatPane(DockPane* pane, const RECT* screenRect = nullptr);
	// Docks the group of a floating pane where the pane was docked before.
	bool DockFloating(DockPane* pane);
	// FloatPane for a docked pane, DockFloating for a floating one (what a double click on a caption does).
	bool ToggleFloat(DockPane* pane);
	// Docks every group of a floating window.
	bool DockFloatWindow(int floatId);
	// Closes the panes of a floating window (what its close button does). True if the window is gone.
	bool CloseFloatWindow(int floatId);
	// The frame window of a floating tree (DockFloat::Id), or null.
	HWND FloatWindow(int floatId) const;
	// A floating window that would be out of reach (its monitor is gone) is moved onto a screen. Default on.
	void SetKeepFloatsOnScreen(bool keep) {
		m_KeepOnScreen = keep;
	}

	//
	// Dragging. The group windows start a drag when a tab or a caption is dragged away; the same calls let an
	// application drag a pane itself. While a drag is on, guides and a preview are shown; EndDrag(true) docks the pane
	// where the guides say (or floats it, if the cursor was on none), EndDrag(false) cancels. A change to the layout
	// during a drag cancels it. Ctrl (suppressDocking) leaves only floating.
	//
	bool BeginDrag(DockPane* pane, bool wholeGroup, POINT screen, bool externalWindow = false);
	void UpdateDrag(POINT screen, bool suppressDocking = false);
	// True if the layout changed.
	bool EndDrag(bool commit);
	bool IsDragging() const {
		return m_Drag != nullptr;
	}
	// what would happen if the drag ended now (Type None if there is no drag)
	const DropTarget& CurrentDropTarget() const;
	// the number of drop target markers on show
	int VisibleGuides() const;

	//
	// Auto-hide. A group in an auto-hide bar shows one item per pane; clicking an item (or resting the mouse on it)
	// slides the group out over the documents as a flyout. It stays while it has the focus and goes when the focus
	// moves to another pane, when something else is clicked, when the pin is pressed (that docks the group again) or,
	// if it was only hovered, when the mouse has left it.
	//
	bool ShowFlyout(DockPane* pane, bool activate = true);
	void HideFlyout();
	// the pane whose group is out (the one shown), or null
	DockPane* FlyoutPane() const;
	// where the flyout stands once it is all the way out, in client coordinates of the host
	RECT FlyoutRect() const;
	// the item of a pane on its auto-hide bar, in client coordinates of the host
	bool GetBarItemRect(const DockPane* pane, RECT& rect) const;
	// milliseconds: the slide (0 for none), the wait before a hovered item opens (0 for at once), and the wait
	// after the mouse has left a hovered flyout before it closes
	void SetFlyoutTiming(int animationMs, int hoverDelayMs, int leaveDelayMs) {
		m_AnimMs = animationMs;
		m_HoverMs = hoverDelayMs;
		m_LeaveMs = leaveDelayMs;
	}

	//
	// Persistence. The state of the docking area is the layout, the pane that has the focus and, if asked for, where
	// the top-level window is (restored onto a screen that exists). A layout alone is DockLayout::Save / Load.
	// Panes that a saved state mentions and that are not registered are made by the options' factory or, without
	// one, by SetPaneFactory.
	//
	std::string SaveState(bool includeWindowPlacement = true) const;
	bool LoadState(std::string_view text, const LoadOptions& options = {}, std::wstring* error = nullptr, bool restoreWindowPlacement = true);
	bool SaveStateToFile(const std::wstring& path, bool includeWindowPlacement = true) const;
	bool LoadStateFromFile(const std::wstring& path, const LoadOptions& options = {}, std::wstring* error = nullptr, bool restoreWindowPlacement = true);

	void SetPaneFactory(PaneFactory factory) {
		m_PaneFactory = std::move(factory);
	}
	// Creates the content window of a pane the first time the pane is on show without one, as a child of 'parent'
	// (the host). This is how panes that are created on demand, or made from a saved layout, get their windows.
	void SetContentFactory(std::function<HWND(DockPane&, HWND parent)> factory) {
		m_ContentFactory = std::move(factory);
	}

	// The default arrangement, to go back to (Window > Reset Window Layout): call CaptureDefaultLayout once the
	// application has set its panes up.
	void CaptureDefaultLayout();
	bool HasDefaultLayout() const {
		return !m_DefaultLayout.empty();
	}
	bool ResetLayout(std::wstring* error = nullptr);

	// Named layouts, kept in Layouts() (which the application can save to a file and load again).
	DockLayoutStore& Layouts() {
		return m_Store;
	}
	bool SaveLayoutAs(const std::wstring& name);
	bool ApplyLayout(const std::wstring& name, std::wstring* error = nullptr);

	//
	// Menus. A "View" menu lists panes: FillPaneMenu appends an item for every pane of a kind (checked if it is
	// showing) with the ids firstId + index in Layout().Panes(); HandlePaneCommand shows or activates the pane behind
	// such an id. The same for the named layouts.
	//
	int FillPaneMenu(HMENU menu, UINT firstId, PaneKind kind = PaneKind::Tool) const;
	bool HandlePaneCommand(UINT id, UINT firstId);
	// Brings a pane into view: shows it if it is hidden, slides it out if it is auto-hidden, else activates it.
	bool ShowPane(DockPane* pane);
	int FillLayoutMenu(HMENU menu, UINT firstId) const;
	bool HandleLayoutCommand(UINT id, UINT firstId);

	// Documents. Closing honours PaneCaps::CanClose and OnPaneClosing; returns how many were closed.
	int CloseAllDocuments(bool exceptActive = false);
	// the next (or previous) tab of the document group that has the focus, wrapping around
	bool ActivateNextDocument(bool forward = true);

	// the window of a group (in the main tree, a floating one or the flyout), or null
	HWND GroupWindow(const DockGroup* group) const;
	TabStripState GetTabState(const DockGroup* group) const;
	// Brings the windows in line with the layout. Called automatically whenever the layout changes; call it
	// yourself after changing something the layout cannot see (a pane's hWnd, title or icon).
	void Sync();

	std::function<void()> OnLayoutChanged;
	std::function<void()> OnActivePaneChanged;
	std::function<bool(DockPane*)> OnPaneClosing;
	std::function<void(DockPane*)> OnPaneClosed;
	// lets the application append items to the context menu; commands with ids outside the range reserved by the
	// framework (0xD000 - 0xD0FF) are posted to the notify target as WM_COMMAND
	std::function<void(DockPane*, HMENU)> OnBuildPaneMenu;

	static constexpr UINT WM_REAP = WM_APP + 0x10;

	BEGIN_MSG_MAP(CDockHost)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_REAP, OnReap)
	END_MSG_MAP()

private:
	friend class CDockGroupWnd;
	friend class CDockFloatFrame;
	friend class DockDragSession;

	struct BarItem {
		DockGroup* Group;
		DockPane* Pane;
		RECT Rect;
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnReap(UINT, WPARAM, LPARAM, BOOL&);

	void UpdateDpi();
	void CreateFonts();
	void Retire(CDockGroupWnd* window);
	void RetireFrame(CDockFloatFrame* frame);
	// makes 'child' a child window of 'parent', converting a popup window if necessary
	static void Adopt(HWND child, HWND parent);
	void SetActivePane(DockPane* pane);
	void EnsureContent(DockPane* pane);
	DockPane* PaneFromWindow(HWND hWnd) const;
	void OnFocusChanged(HWND hWnd);
	static void CALLBACK FocusEventProc(HWINEVENTHOOK hook, DWORD event, HWND hWnd, LONG idObject, LONG idChild, DWORD thread, DWORD time);

	// floating windows (called by the frames)
	DockFloat* FindFloat(int id) const;
	DockSplit* FloatRoot(int id) const;
	SIZE FloatMinClientSize(int id) const;
	void OnFrameMoved(CDockFloatFrame* frame);
	void ForgetFrame(CDockFloatFrame* frame);
	RECT DefaultFloatRect(const DockPane* pane) const;
	RECT OuterRectForClient(const RECT& client, int dpi) const;
	void EnsureOnScreen(RECT& rect) const;
	std::wstring FloatTitle(const DockFloat& window) const;
	// the group under a point of the screen, looking through 'ignore'; null if something else is in the way
	const DockGroup* GroupAtScreen(POINT pt, HWND ignore) const;
	bool BeginFrameMove(int floatId);

	std::vector<BarItem> BarItems(DockSide side, CDCHandle dc) const;
	bool BarItemAt(POINT pt, BarItem& item) const;
	void UpdateBarHover(POINT pt);
	// the flyout
	DockGroup* ResolveFlyout();
	void ClearFlyout();
	RECT FlyoutRectAt(const DockGroup& group, double progress) const;
	void PositionFlyout();
	void StartFlyoutAnimation(bool opening);
	void FinishFlyoutClose();
	void OnFlyoutFocus(HWND hWnd);
	void DrawBars(CDCHandle dc);
	void Draw(HDC hdc, RECT clip);

	DockLayout m_Layout;
	DockTheme m_Theme;
	DockMetrics m_Metrics;
	int m_Dpi{ 96 };
	CFont m_Font, m_BoldFont, m_VerticalFont;

	std::map<const DockGroup*, CDockGroupWnd*> m_Groups;
	std::vector<CDockGroupWnd*> m_Retired;
	std::map<int, CDockFloatFrame*> m_Frames;
	std::vector<CDockFloatFrame*> m_RetiredFrames;
	std::wstring m_ActiveId;
	HWND m_NotifyTarget{};
	HWINEVENTHOOK m_FocusHook{};
	bool m_Syncing{};
	bool m_KeepOnScreen{ true };
	PaneFactory m_PaneFactory;
	std::function<HWND(DockPane&, HWND)> m_ContentFactory;
	std::string m_DefaultLayout;
	DockLayoutStore m_Store;

	// the flyout of an auto-hidden group: identified by a pane of the group, since nodes come and go
	std::wstring m_FlyoutId;
	HWND m_FlyoutWindow{};			// its window, as of the last sync
	bool m_FlyoutFocused{};			// the user has been in it: it does not close by itself
	bool m_FlyoutClosing{};
	double m_FlyoutProgress{ 1 };	// 0: all inside the bar, 1: all the way out
	double m_AnimFrom{}, m_AnimTarget{};
	ULONGLONG m_AnimStart{};
	ULONGLONG m_LeaveSince{};
	HWND m_ReturnFocus{};
	std::wstring m_HoverId;			// the bar item the mouse rests on
	bool m_TrackingMouse{};
	int m_AnimMs{ 120 }, m_HoverMs{ 300 }, m_LeaveMs{ 500 };

	std::unique_ptr<SplitterTracker> m_Splitters;
	std::unique_ptr<DockDragSession> m_Drag;
};

}
