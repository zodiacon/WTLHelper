#pragma once

// Accordion: a vertical stack of sections, each a header that opens and closes the body under it. A body is a
// window the app gives the section (a list, a property grid, a dialog), which the control places and shows. Any
// number of sections can be open at once, or one (ACDS_SINGLEEXPAND). A body is a fixed height, a share of the
// space the others leave (fill), or the height its content asks for (fit); when the open sections don't fit,
// the control scrolls. The API follows the common controls: C-style structures, section handles, and WM_NOTIFY
// codes (ACDN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>
#include <atlctrls.h>

#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_ACCORDION[] = L"WC_Accordion";

// ---- Styles ------------------------------------------------------------------

#define ACDS_SINGLEEXPAND  0x0001   // opening a section closes the others
#define ACDS_NOANIMATION   0x0002   // sections open and close at once (they do anyway with animations off in Windows)
#define ACDS_ALWAYSANIMATE 0x0004   // animate even with animations off in Windows (ACDS_NOANIMATION still wins)
#define ACDS_NORESIZE      0x0008   // no dragging a body's bottom edge to resize it
#define ACDS_NOREORDER     0x0010   // no dragging headers to reorder the sections

// ---- Sections --------------------------------------------------------------------

struct AcdNode;
typedef AcdNode* HACDSECTION;

struct AcdSection;   // AccordionModel.h
struct AcdLayout;

#define ACD_FIRST  ((HACDSECTION)(ULONG_PTR)-2)   // InsertSection: at the top
#define ACD_LAST   ((HACDSECTION)(ULONG_PTR)-1)   // InsertSection: at the bottom

// how a body's height is set
#define ACDH_FIXED  0   // height DIPs
#define ACDH_FILL   1   // a share (weight) of the space the other sections leave
#define ACDH_FIT    2   // its content's: asked with ACDN_GETFITHEIGHT

// section state
#define ACDSS_EXPANDED    0x0001
#define ACDSS_NOCOLLAPSE  0x0002   // always open, with no chevron

struct ACDSECTION {
	UINT    id;          // the app's; kept in the saved state, so it should be unique (0: the control picks one)
	LPCWSTR pszText;     // the title
	LPCWSTR pszInfo;     // right-aligned in the header ("12 items"); may be nullptr
	int     iImage;      // in the image list; -1 for none
	HWND    hwndBody;    // the body: a child of the accordion (it's made one); may be nullptr
	UINT    heightMode;  // ACDH_*
	int     height;      // ACDH_FIXED: DIPs (0: 120)
	float   weight;      // ACDH_FILL: its share among the open fill sections (0: 1)
	int     minHeight;   // DIPs: the body's least
	UINT    state;       // ACDSS_*
	LPARAM  lParam;
};

// a button in a section's header, left of its info text
struct ACDBUTTON {
	UINT    id;          // the app's, in ACDN_BUTTONCLICK
	int     iImage;      // in the image list; -1: the text instead
	LPCWSTR pszText;     // shown without an image (a short "+" or "..."), and the tooltip either way
};

// ---- Colors -----------------------------------------------------------------------

struct ACDCOLORS {
	COLORREF crBack;          // behind the sections (where a body has no window, or there's no section)
	COLORREF crHeaderBack;
	COLORREF crHeaderText;
	COLORREF crInfoText;      // the header's right-aligned text
	COLORREF crBorder;        // the line under each header
};

// ---- Notifications -------------------------------------------------------------

#define ACDN_FIRST          (0U - 2950U)   // outside the common controls ranges
#define ACDN_GETFITHEIGHT   (ACDN_FIRST - 0)   // NMACDFITHEIGHT: an ACDH_FIT section's body height
// The user opening or closing a section (a click, a key): ACDN_EXPANDING first, which a non-zero return vetoes,
// then ACDN_EXPANDED for it and for each section that closed with it (ACDS_SINGLEEXPAND). The API sends neither.
#define ACDN_EXPANDING      (ACDN_FIRST - 1)   // NMACDEXPAND
#define ACDN_EXPANDED       (ACDN_FIRST - 2)   // NMACDEXPAND
#define ACDN_BUTTONCLICK    (ACDN_FIRST - 3)   // NMACDBUTTON: a header button
// a right click or the menu key on a header, or on no section (hSection nullptr); a non-zero return: handled
#define ACDN_CONTEXTMENU    (ACDN_FIRST - 4)   // NMACDCONTEXTMENU
// the user dragged a body's edge (when the drag ends) or a header to another place; the API sends neither
#define ACDN_SECTIONRESIZED (ACDN_FIRST - 5)   // NMACDSECTION
#define ACDN_SECTIONMOVED   (ACDN_FIRST - 6)   // NMACDSECTION

struct NMACDSECTION {
	NMHDR       hdr;
	HACDSECTION hSection;
	UINT        id;
	LPARAM      lParam;
};

struct NMACDEXPAND {
	NMACDSECTION section;
	BOOL         expand;     // opening (TRUE) or closing
};

struct NMACDBUTTON {
	NMACDSECTION section;
	UINT         idButton;
	RECT         rcButton;   // client coords: a menu can drop down from it
};

struct NMACDCONTEXTMENU {
	NMACDSECTION section;    // hSection nullptr: not on a section
	POINT        ptScreen;   // where the menu goes (the header's corner for the menu key)
};

struct NMACDFITHEIGHT {
	NMACDSECTION section;
	int          width;      // the body's, in pixels
	int          height;     // set it: pixels (it comes in as the last height given)
};

// ---- The control ------------------------------------------------------------------

class AcdFocusWindow;
class AcdDragImage;

class CAccordionCtrl : public ATL::CWindowImpl<CAccordionCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_Accordion", CS_DBLCLKS, -1)

	CAccordionCtrl();
	~CAccordionCtrl();

	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CAccordionCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnForward)
		// the bodies' messages go to the parent, as if the bodies were its children
		MESSAGE_HANDLER(WM_COMMAND, OnForward)
		MESSAGE_HANDLER(WM_NOTIFY, OnForward)
		MESSAGE_HANDLER(WM_DRAWITEM, OnForward)
		MESSAGE_HANDLER(WM_MEASUREITEM, OnForward)
		MESSAGE_HANDLER(WM_COMPAREITEM, OnForward)
		MESSAGE_HANDLER(WM_DELETEITEM, OnForward)
		MESSAGE_HANDLER(WM_VKEYTOITEM, OnForward)
		MESSAGE_HANDLER(WM_CHARTOITEM, OnForward)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnCtlColor)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the headers' focus: a child window with no size, first in the tab order
		MESSAGE_HANDLER(WM_SETFOCUS, OnHeaderFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnHeaderFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnHeaderGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnHeaderKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnHeaderChar)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_UPDATEUISTATE, OnHeaderUpdateUIState)
	END_MSG_MAP()

	// ---- Sections ------------------------------------------------------------------

	HACDSECTION InsertSection(const ACDSECTION* section, HACDSECTION insertAfter = ACD_LAST);
	BOOL        DeleteSection(HACDSECTION hSection);   // its window is hidden, not destroyed
	BOOL        MoveSection(HACDSECTION hSection, HACDSECTION insertAfter);   // ACD_FIRST, ACD_LAST or a section
	void        DeleteAllSections();
	int         GetSectionCount() const;
	HACDSECTION GetSection(int index) const;           // top to bottom; nullptr if out of range
	int         GetSectionIndex(HACDSECTION hSection) const;   // -1 if not a section
	HACDSECTION FindSection(UINT id) const;
	// the section as it is; the strings stay valid until the section's text changes or it's deleted
	BOOL        GetSectionInfo(HACDSECTION hSection, ACDSECTION* section) const;

	BOOL        SetSectionText(HACDSECTION hSection, LPCWSTR text);
	BOOL        SetSectionInfoText(HACDSECTION hSection, LPCWSTR text);
	BOOL        SetSectionImage(HACDSECTION hSection, int image);
	BOOL        SetSectionWindow(HACDSECTION hSection, HWND hwndBody);   // the old one is hidden
	HWND        GetSectionWindow(HACDSECTION hSection) const;
	BOOL        SetSectionHeight(HACDSECTION hSection, UINT heightMode, int height = 0, float weight = 0);
	BOOL        SetSectionMinHeight(HACDSECTION hSection, int minHeight);
	BOOL        SetSectionData(HACDSECTION hSection, LPARAM lParam);
	LPARAM      GetSectionData(HACDSECTION hSection) const;
	BOOL        SetSectionCollapsible(HACDSECTION hSection, bool collapsible);   // not collapsible: opened
	// the header's buttons, right to left before the info text (count 0: none); the strings are copied
	BOOL        SetSectionButtons(HACDSECTION hSection, const ACDBUTTON* buttons, int count);

	// Opening and closing from the API sends no notifications (and animates, like the user's). With
	// ACDS_SINGLEEXPAND, opening one closes the others.
	BOOL        Expand(HACDSECTION hSection, bool expand = true);
	bool        IsExpanded(HACDSECTION hSection) const;
	void        ExpandAll(bool expand);               // with ACDS_SINGLEEXPAND, expanding opens the first one only
	void        UpdateFitHeight(HACDSECTION hSection = nullptr);   // ask again (nullptr: every fit section)

	// header and body, client coords (the body as it shows: empty when closed)
	BOOL        GetSectionRect(HACDSECTION hSection, RECT* pHeader, RECT* pBody) const;
	void        EnsureVisible(HACDSECTION hSection);  // scrolls its header (and as much of its body) into view
	int         GetScrollPos() const;

	// the header with the keyboard focus (when the headers have it); nullptr with no sections
	HACDSECTION GetFocusSection() const;
	void        SetFocusSection(HACDSECTION hSection);   // moves the focus cue; doesn't take the focus
	HWND        GetHeaderFocusWindow() const;            // the child that takes the focus for the headers

	// ---- Saved state -------------------------------------------------------------------
	//
	// The sections' order, which are open, and the sizes the user dragged them to, as text to keep in the app's
	// settings. Sections are matched by id; ones the text doesn't name keep their place after the others.
	// SetState doesn't animate or notify, and returns FALSE (changing nothing) for text it can't read.

	int         GetState(LPWSTR text, int cch) const;   // the length with the terminator; text may be nullptr
	BOOL        SetState(LPCWSTR text);

	// ---- Looks ---------------------------------------------------------------------

	HIMAGELIST  SetImageList(HIMAGELIST hImageList);   // the header icons; not destroyed by the control
	HIMAGELIST  GetImageList() const;
	void        SetColors(const ACDCOLORS* colors);   // nullptr: system colors (follows WTLHelper dark mode)
	ACDCOLORS   GetColors() const;                    // the colors in effect
	int         GetHeaderHeight() const;              // pixels, from the font

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHeaderFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHeaderGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHeaderKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHeaderChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnHeaderUpdateUIState(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	int     IndexOf(HACDSECTION hSection) const;   // -1 if not a section
	void    UpdateFont();
	void    Layout();                               // the sections, the scroll bar and the bodies
	void    AskFitHeights(int width, bool all);
	void    SetScroll(int pos);
	void    PositionBodies();
	void    PaintHeader(HDC hdc, size_t index, const ACDCOLORS& c);
	std::vector<RECT> ButtonRects(size_t index) const;   // client coords; empty rects for buttons with no room
	int     ButtonAt(size_t index, POINT pt) const;     // -1 if none
	void    InvalidateHeader(int index);
	void    SetHot(int index, int button);
	void    UpdateToolTip();
	void    UserToggle(size_t index, bool expand);      // with the notifications
	void    Changed(const std::vector<size_t>& changed);  // animates, or snaps; the focus leaves bodies that close
	bool    Animates() const;
	void    StartAnimation();
	void    FocusHeader(size_t index);                  // the focus cue there, and the focus to the headers
	void    Notify(NMACDSECTION& nm, UINT code, size_t index);   // the parent's answer in m_LastNotifyResult
	bool    ContextMenu(int index, POINT ptScreen);
	bool    HeadersFocused() const;
	int     EdgeSize() const;                           // the resize edge's reach (-1: none, with ACDS_NORESIZE)
	size_t  MoveAt(size_t from, size_t before);         // the section and its node; returns the new index
	void    EndDrag(bool cancel);
	int     DropLineY() const;                          // where the reorder line goes; INT_MIN: nowhere
	void    ShowDragImage();                            // the dragged header, following the mouse
	void    MoveDragImage(POINT pt);
	void    HideDragImage();
	void    UpdateComposited();                         // WS_EX_COMPOSITED while bodies resize (a drag, an animation)

	// top to bottom: the sections' text and windows, and the model's side of them (AccordionModel.h), in the same order
	std::vector<std::unique_ptr<AcdNode>> m_Nodes;
	std::vector<AcdSection> m_Sections;
	UINT        m_NextId = 1;
	UINT        m_Dpi = 96;
	HFONT       m_hFont = nullptr;
	HFONT       m_hTitleFont = nullptr;            // the font, semibold
	bool        m_OwnFont = true;
	HIMAGELIST  m_hImageList = nullptr;
	ACDCOLORS   m_Colors{};
	bool        m_SystemColors = true;
	int         m_HeaderHeight = 24;
	HBRUSH      m_hBackBrush = nullptr;            // crBack, for the bodies the parent doesn't color
	COLORREF    m_BackBrushColor = CLR_INVALID;
	int         m_ScrollPos = 0;
	int         m_FitWidth = -1;                   // the width the fit heights were asked for
	bool        m_InLayout = false;
	std::unique_ptr<AcdLayout> m_Layout;              // as last laid out
	std::unique_ptr<AcdFocusWindow> m_FocusWindow;
	int         m_Focus = 0;                       // the header with the focus cue
	int         m_Hot = -1;                        // the header under the mouse, and its button there (-1: none)
	int         m_HotButton = -1;
	int         m_Pressed = -1;                    // the button the mouse went down on (in m_Hot's header)
	bool        m_Tracking = false;                // TrackMouseEvent for WM_MOUSELEAVE
	HWND        m_hToolTip = nullptr;              // the header buttons' tooltips
	ULONGLONG   m_AnimationTick = 0;               // the last animation step
	LRESULT     m_LastNotifyResult = 0;
	// a drag: a header pressed (a click, or a reorder once it moves), a header being moved, an edge being dragged
	enum class Drag { None, Pending, Reorder, Resize };
	Drag        m_Drag = Drag::None;
	size_t      m_DragIndex = 0;
	POINT       m_DragStart{};
	int         m_DragHeight = 0;                  // resize: the body's height when it started
	size_t      m_DropIndex = 0;                   // reorder: where it goes (AcdDropIndex)
	std::unique_ptr<AcdLayout> m_DragLayout;       // resize: the layout it started on
	std::vector<AcdSection> m_DragSaved;           // resize: the sections before it, for Escape
	int         m_DragGrab = 0;                    // reorder: where the header was taken, from its top
	std::unique_ptr<AcdDragImage> m_DragImage;     // reorder: the header following the mouse
	bool        m_Animating = false;
	bool        m_Composited = false;              // WS_EX_COMPOSITED is on for a resize or an animation
	bool        m_AppComposited = false;           // the app had it on already
};
