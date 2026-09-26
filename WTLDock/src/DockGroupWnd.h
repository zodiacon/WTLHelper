#pragma once

#include "DockHost.h"
#include "DockAccessible.h"

namespace WTLDock {

// The window of one group: draws the caption and the tab strip and hosts the active pane's content window.
// Created and owned by CDockHost; not for direct use.
class CDockGroupWnd : public ATL::CWindowImpl<CDockGroupWnd>, private IDockAccessibleOwner {
public:
	DECLARE_WND_CLASS_EX(L"WTLDock_Group", CS_DBLCLKS, 0)

	explicit CDockGroupWnd(CDockHost& host) : m_Host(host) {
	}
	~CDockGroupWnd();

	DockGroup* Group() const {
		return m_Group;
	}
	// Points the window at a group (nodes are recreated by layout operations, so this is refreshed on every sync).
	void Attach(DockGroup* group) {
		m_Group = group;
	}
	// Positions the content windows and repaints.
	void Relayout();
	// Takes the keyboard focus into the chrome (the tab of 'pane', else of the active pane): arrow keys then visit tabs
	// and buttons. False if there is nothing to visit.
	bool FocusChrome(DockPane* pane);
	bool HasChromeFocus() const;
	// the name of the item the keyboard is on ("" without chrome focus)
	std::wstring FocusName() const;
	// Detaches the window from its group and moves its content windows out; it is destroyed later.
	void Retire();
	TabStripState State();
	// the caption and tab strip (where a dropped pane becomes a tab) and the visible tabs, in screen coordinates
	void DropInfo(std::vector<RECT>& zones, std::vector<RECT>& tabs, int& firstTab);

	void OnFinalMessage(HWND) override {
		delete this;
	}

	BEGIN_MSG_MAP(CDockGroupWnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_MBUTTONDOWN, OnMButtonDown)
		MESSAGE_HANDLER(WM_MBUTTONUP, OnMButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		// what content windows say to their parent goes to the frame
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnForward)
		MESSAGE_HANDLER(WM_HSCROLL, OnForward)
		MESSAGE_HANDLER(WM_VSCROLL, OnForward)
		MESSAGE_HANDLER(WM_COMPAREITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_DRAWITEM, WM_CHARTOITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnForward)
	END_MSG_MAP()

private:
	enum class Button { None, Close, Pin, Menu };

	struct Strip {
		TabStrip Layout;
		std::vector<TabSpec> Specs;
	};
	struct Hit {
		enum class Kind { None, Caption, CaptionClose, CaptionPin, CaptionMenu, Tab, TabClose, Overflow } Type{ Kind::None };
		int Tab{ -1 };		// index among all tabs of the group
	};

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);

	std::vector<AccElement> FocusItems() const;
	void EnsureFocusVisible();
	void MoveFocus(int step);
	RECT FocusRect() const;

	// IDockAccessibleOwner
	HWND AccWindow() const override {
		return m_hWnd;
	}
	std::wstring AccName() const override;
	LONG AccRole() const override;
	std::vector<AccElement> AccElements() const override;
	std::vector<HWND> AccChildWindows() const override;
	void NotifyStructure();

	// A group in a floating window is at the DPI of that window's monitor, whatever the main window's is.
	int Dpi() const {
		return m_Host.GroupDpi(m_Group);
	}
	const DockMetrics& Metrics() const {
		return m_Host.MetricsFor(Dpi());
	}
	HFONT Font() const {
		return m_Host.FontFor(Dpi());
	}
	HFONT ItalicFont() const {
		return m_Host.ItalicFontFor(Dpi());
	}

	// tooltips
	void UpdateTip(const Hit& hit);
	bool TipFor(const Hit& hit, RECT& targetScreen, std::wstring& text);
	static std::wstring CaptionText(const DockPane& pane);
	void DrawModifiedDot(CDCHandle dc, const RECT& slot, COLORREF color) const;

	std::vector<TabSpec> MeasureSpecs(CDCHandle dc) const;
	GroupParts Parts(const RECT& client) const;
	Strip LayoutStrip(const GroupParts& parts, CDCHandle dc);
	Hit Locate(POINT pt);
	DockPane* PaneAt(int tab) const;
	bool CloseButtonVisible() const;
	bool PinVisible() const;
	bool MenuVisible() const;
	CaptionButtons ButtonsFor(const GroupParts& parts) const;
	static Button ButtonOf(Hit::Kind kind);
	void RunButton(Button button);
	void DrawCloseGlyph(CDCHandle dc, const RECT& button, bool hot, COLORREF idle) const;
	void DrawPinGlyph(CDCHandle dc, const RECT& button, bool pinned, bool hot, COLORREF idle) const;
	void DrawMenuGlyph(CDCHandle dc, const RECT& button, bool hot, COLORREF idle) const;
	void Draw(HDC hdc, RECT clip);
	void ShowOverflowMenu(const RECT& button, bool stripAtBottom);
	void SetHot(int tab, bool close, bool overflow);
	void EndInteraction();
	bool BeginDockDrag(DockPane* pane, bool wholeGroup, POINT client);
	void EndDockDrag(bool commit);

	CDockHost& m_Host;
	DockGroup* m_Group{};
	DockAccessible* m_Acc{};
	std::wstring m_AccSignature;
	bool m_ChromeFocus{};			// the keyboard is on the tabs and buttons, not in the content
	bool m_ChromeFocusWanted{};		// set while we take the focus ourselves (WM_SETFOCUS must not pass it on)
	std::wstring m_FocusKey;		// the item that has it (AccElement::Key)

	// tab strip state
	int m_First{};				// first visible tab
	bool m_ScrollLocked{};		// the user scrolled with the wheel: don't force the active tab into view
	int m_LastActive{ -1 };

	// hover and press state
	Button m_HotButton{};		// the caption button under the mouse
	Button m_PressButton{};		// and the one that is pressed
	int m_HotTab{ -1 };
	bool m_HotTabClose{};
	bool m_HotOverflow{};
	bool m_Tracking{};
	int m_PressTabClose{ -1 };
	int m_MiddleTab{ -1 };

	// dragging out of the group: the host shows the guides
	bool m_Session{};
	bool m_CaptionPending{};	// the caption is pressed; dragging it drags the group
	POINT m_CaptionStart{};
	POINT m_LastScreen{};

	// dragging a tab to a new position
	DockPane* m_DragPane{};
	POINT m_DragStart{};
	bool m_Dragging{};
};

}
