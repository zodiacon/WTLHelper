#pragma once

// Breadcrumb bar: the path to a location as a row of segments ("This PC > C: > Users > pavel"), as in Explorer's
// address bar. Clicking a segment goes there; the chevron after it drops down a list of what's under it (the app adds
// those when BRCN_GETCHILDREN asks), and picking one goes there. When the bar is too narrow the leftmost segments fold
// into the chevron before the first one, whose list then shows them. The app owns the hierarchy: the control only
// says where the user wants to go (BRCN_NAVIGATE), and the app shows it with SetPath. The location's icon is at the
// left, and the app's buttons (Refresh) at the box's right. The API follows the common controls: C-style structures
// and WM_NOTIFY codes (BRCN_*) sent to the parent.

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

constexpr wchar_t WC_BREADCRUMB[] = L"WC_Breadcrumb";

// ---- Styles --------------------------------------------------------------------

#define BRCS_NONAV           0x0001   // no back and forward buttons (and their recent locations arrow)
#define BRCS_NOUP            0x0002   // no up button
#define BRCS_NOICON          0x0004   // no location icon
#define BRCS_NOROOTCHEVRON   0x0008   // no chevron before the first segment, unless segments fold into it
#define BRCS_NOHISTORY       0x0010   // no history arrow at the box's right

// ---- Segments and children ----------------------------------------------------------

#define BRCIF_NOCHILDREN     0x0001   // nothing under it: no chevron

struct BRCITEM {
	LPCWSTR pszText = nullptr;
	LPCWSTR pszName = nullptr;        // its part of the path text; nullptr: the text, L"": none ("This PC")
	int     iImage = -1;              // in the image list
	UINT    flags = 0;                // BRCIF_*
	LPARAM  lParam = 0;
};

// ---- Buttons ---------------------------------------------------------------------
//
// The app's buttons at the right end of the box. A click sends BRCN_BUTTONCLICK, then posts WM_COMMAND with the id to
// the owner, as a menu does.

#define BRCBS_DISABLED       0x0001
#define BRCBS_CHECKED        0x0002

// the control's own drawings, for a button without an image
#define BRCGLYPH_NONE        0
#define BRCGLYPH_REFRESH     1
#define BRCGLYPH_GO          2        // an arrow to the right
#define BRCGLYPH_CLOSE       3        // a cross
#define BRCGLYPH_SEARCH      4        // a magnifying glass

struct BRCBUTTON {
	UINT    id = 0;                   // its command; unique among the buttons
	int     iImage = -1;              // in the image list; -1: the glyph
	UINT    glyph = BRCGLYPH_NONE;
	LPCWSTR pszTip = nullptr;
	UINT    state = 0;                // BRCBS_*
};

// ---- Colors ----------------------------------------------------------------------

struct BRCCOLORS {
	COLORREF crBack;                  // around the box: the nav buttons
	COLORREF crBox;
	COLORREF crBorder;                // the box's frame
	COLORREF crHotBorder;             // the box's frame under the mouse
	COLORREF crText;
	COLORREF crDimText;               // disabled buttons
	COLORREF crHotBack;               // a segment or a button under the mouse
	COLORREF crPressedBack;           // pressed, or its list open
	COLORREF crListBack;              // the drop-down lists
	COLORREF crListHotBack;           // a list's row under the mouse or the keys
};

// ---- Notifications -------------------------------------------------------------

#define BRCN_FIRST           (0U - 3150U)   // outside the common controls ranges
#define BRCN_NAVIGATE        (BRCN_FIRST - 0)   // NMBRCNAVIGATE; the user wants to go somewhere: the app calls SetPath
#define BRCN_GETCHILDREN     (BRCN_FIRST - 1)   // NMBRCCHILDREN; a list drops down: AddChild what's under the segment
#define BRCN_BUTTONCLICK     (BRCN_FIRST - 2)   // NMBRCBUTTON; non-zero: done, no WM_COMMAND
#define BRCN_HEIGHTCHANGED   (BRCN_FIRST - 3)   // NMHDR; GetIdealHeight changed (DPI, font): lay the window out again

// where a BRCN_NAVIGATE comes from
#define BRCNS_SEGMENT        0        // a segment clicked, or picked from the folded ones
#define BRCNS_CHILD          1        // picked from a segment's list (index: that segment's, -1 the root chevron's)

struct NMBRCNAVIGATE {
	NMHDR   hdr;
	UINT    source;                   // BRCNS_*
	int     index;                    // the segment's
	LPCWSTR pszPath;                  // the path text there: the segments' names up to it (and the child's)
	LPCWSTR pszText;                  // the segment's or the child's text
	LPARAM  lParam;                   // the segment's or the child's
};

// While it's sent, the app calls AddChild for each item under the segment (none: the list doesn't open). The child that
// is the next segment is shown in bold.
struct NMBRCCHILDREN {
	NMHDR   hdr;
	int     index;                    // the segment's; -1: the root's (the chevron before the first segment)
	LPCWSTR pszPath;                  // the path text there
	LPARAM  lParam;                   // the segment's
};

struct NMBRCBUTTON {
	NMHDR   hdr;
	UINT    id;
	RECT    rcButton;                 // screen coords: a menu of the app's can drop down from it
};

// ---- Control class --------------------------------------------------------------------------

struct BrcData;
struct BrcHit;
class BrcDropList;

class CBreadcrumbCtrl : public ATL::CWindowImpl<CBreadcrumbCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_Breadcrumb", CS_DBLCLKS, -1)

	CBreadcrumbCtrl();
	~CBreadcrumbCtrl();
	friend class BrcDropList;

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CBreadcrumbCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
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
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- The path ----------------------------------------------------------------------------------

	BOOL    SetPath(const BRCITEM* items, int count);             // closes a list that's open; no notification
	int     GetItemCount() const;
	int     GetItemText(int index, LPWSTR text, int cch) const;   // the length with the terminator; text may be nullptr
	LPARAM  GetItemParam(int index) const;                        // 0 if out of range
	// The path text: the segments' names joined with the first separator, up to count segments (-1: all). The
	// length with the terminator; text may be nullptr.
	int     GetPathText(LPWSTR text, int cch, int count = -1) const;
	void    SetSeparators(LPCWSTR separators);                    // default "\\"

	// While BRCN_GETCHILDREN is sent: one item under the segment. FALSE at any other time.
	BOOL    AddChild(const BRCITEM* item);
	BOOL    AddChild(LPCWSTR text, int image = -1, LPARAM lParam = 0, LPCWSTR name = nullptr);
	void    CloseDropDown();

	// ---- Buttons -------------------------------------------------------------------------------------

	BOOL    AddButton(const BRCBUTTON* button, int insertAt = -1);    // FALSE if the id is taken
	BOOL    RemoveButton(UINT id);
	BOOL    SetButtonState(UINT id, UINT state);                  // BRCBS_*
	UINT    GetButtonState(UINT id) const;                        // (UINT)-1 if there's no such button

	// ---- Appearance and size -------------------------------------------------------------------------
	//
	// The image list holds small icons (16x16 at 96 DPI); its size sets the icons' (the app gives one for the DPI on
	// WM_DPICHANGED). The font is the message font at the window's DPI; WM_SETFONT replaces it.

	void    SetImageList(HIMAGELIST images);                      // the control doesn't destroy it
	void    SetColors(const BRCCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	BRCCOLORS GetColors() const;
	int     GetIdealHeight() const;
	void    SetOwner(HWND owner);                                 // gets WM_COMMAND; default (nullptr): the parent

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
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
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Changed(bool measure);                                // the layout (and the text widths) to be found again
	void    EnsureLayout();
	void    NotifyHeight();
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	HWND    Owner() const;
	BrcHit  HitTest(POINT pt);
	void    SetHot(const BrcHit& hit);
	void    Navigate(UINT source, int index, int child);          // child: in the open list's children, or -1
	void    ClickButton(int index);
	std::wstring PathTo(int count) const;

	// the drop-down lists (BreadcrumbList.cpp)
	void    OpenDropDown(const BrcHit& from, bool pressed);       // pressed: the button is down (released on a row picks it)
	void    DropPicked(int tag);
	void    DropClosed();
	bool    DropMouse(POINT pt);                                  // the mouse over the bar while a list is open: TRUE if it opened another

	// painting (BreadcrumbPaint.cpp)
	void    Paint(WTL::CDCHandle dc);
	void    PaintNav(WTL::CDCHandle dc, const BRCCOLORS& c);
	void    PaintSegments(WTL::CDCHandle dc, const BRCCOLORS& c);
	void    PaintButtons(WTL::CDCHandle dc, const BRCCOLORS& c);
	void    PaintButtonBack(WTL::CDCHandle dc, const RECT& rc, const BrcHit& part, const BRCCOLORS& c);

	std::unique_ptr<BrcData> m_Data;
	HIMAGELIST m_Images = nullptr;
	HWND    m_Owner = nullptr;

	BRCCOLORS m_Colors{};
	bool    m_SystemColors = true;

	HFONT   m_hFont = nullptr;
	HFONT   m_hBold = nullptr;                                    // the lists' current child
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_TextHeight = 16;
	int     m_LastHeight = 0;                                     // the height last told (BRCN_HEIGHTCHANGED)
};
