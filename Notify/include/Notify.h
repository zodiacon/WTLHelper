#pragma once

// Notifications that don't stop the user: an info bar (CInfoBarCtrl), a strip docked in a window with a row for each
// message, the newest on top, as Visual Studio's and Office's bars; and toasts (CToastHost), small popups in a corner of
// a window that go away by themselves. A message has a severity (its icon and colors), a title, a text that may have
// links ('<a id="x">here</a>'), buttons, a close button, and optionally details the user can show, a progress bar and
// a "don't show again" checkbox. The API follows the common controls: C-style structures and WM_NOTIFY codes (NTFN_*)
// sent to the parent.

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

constexpr wchar_t WC_INFOBAR[] = L"WC_InfoBar";

// ---- Styles --------------------------------------------------------------------

#define NTFS_NOANIMATION     0x0001   // rows appear and go at once
#define NTFS_ALWAYSANIMATE   0x0002   // even when Windows' client area animations are off

// ---- Messages -------------------------------------------------------------------

#define NTFSEV_INFO          0
#define NTFSEV_SUCCESS       1
#define NTFSEV_WARNING       2
#define NTFSEV_ERROR         3

#define NTFF_NOCLOSE         0x0001   // no close button: the app removes it
#define NTFF_CHECKED         0x0002   // the checkbox starts checked
#define NTFF_EXPANDED        0x0004   // the details start shown

#define NTFP_NONE            0
#define NTFP_VALUE           1        // value: 0 to 100
#define NTFP_INDETERMINATE   2        // something's going on

struct NTFBUTTON {
	UINT    id = 0;                   // NTFN_BUTTONCLICK's
	LPCWSTR pszText = nullptr;
};

struct NTFMESSAGE {
	UINT    id = 0;                   // 0: one is made (AddMessage returns it); a message with the id is replaced
	UINT    severity = NTFSEV_INFO;   // NTFSEV_*
	LPCWSTR pszTitle = nullptr;       // in bold, before the text (a toast's: above it)
	LPCWSTR pszText = nullptr;        // links: <a id="x">text</a> (NTFN_LINKCLICK); &lt; &gt; &amp; &quot;
	const NTFBUTTON* pButtons = nullptr;
	int     cButtons = 0;
	UINT    flags = 0;                // NTFF_*
	LPCWSTR pszCheck = nullptr;       // a checkbox's text ("Don't show again"); its state goes with NTFN_CLOSED
	LPCWSTR pszDetails = nullptr;     // shown under the text when the user asks
	UINT    progress = NTFP_NONE;     // NTFP_*
	int     value = 0;                // NTFP_VALUE's
	int     iImage = -1;              // in the image list; -1: the severity's icon
	UINT    timeout = 0;              // a toast's, in ms; 0: it stays
	LPARAM  lParam = 0;
};

// ---- Colors ----------------------------------------------------------------------

struct NTFCOLORS {
	COLORREF crBack[4];               // by severity
	COLORREF crIcon[4];               // by severity
	COLORREF crText;
	COLORREF crDimText;               // the details
	COLORREF crLink;
	COLORREF crBorder;                // between rows, under the bar, around toasts
	COLORREF crButton;
	COLORREF crButtonHot;
	COLORREF crButtonPressed;
	COLORREF crButtonBorder;
	COLORREF crAccent;                // the progress bar, a checked box
};

// ---- Strings ---------------------------------------------------------------------
//
// The controls' own words (SetString replaces them: another language). %d is a number.

#define NTFSTR_SHOWDETAILS   0        // "Show details"
#define NTFSTR_HIDEDETAILS   1        // "Hide details"
#define NTFSTR_MORE          2        // "%d more": the info bar's rows past its maximum
#define NTFSTR_FEWER         3        // "Show fewer"
#define NTFSTR_CLOSE         4        // "Close": the close button's tooltip and name
#define NTFSTR_COUNT         5

// ---- Notifications -------------------------------------------------------------

#define NTFN_FIRST           (0U - 3200U)   // outside the common controls ranges
#define NTFN_BUTTONCLICK     (NTFN_FIRST - 0)   // NMNTFBUTTON; then the message closes (NTFN_CLOSED), unless non-zero
#define NTFN_LINKCLICK       (NTFN_FIRST - 1)   // NMNTFLINK
#define NTFN_CLOSED          (NTFN_FIRST - 2)   // NMNTFCLOSED; the user closed it (the API's removing sends nothing)
#define NTFN_HEIGHTCHANGED   (NTFN_FIRST - 3)   // NMHDR; GetIdealHeight changed (a row came or went, wrapping, DPI): lay out again

// why a message closed
#define NTFR_CLOSEBUTTON     0
#define NTFR_BUTTON          1        // after NTFN_BUTTONCLICK
#define NTFR_TIMEOUT         2        // a toast's time ran out

struct NMNTFBUTTON {
	NMHDR   hdr;
	UINT    id;                       // the message's
	UINT    button;                   // the button's
	BOOL    checked;                  // the checkbox
	LPARAM  lParam;
};

struct NMNTFLINK {
	NMHDR   hdr;
	UINT    id;
	LPCWSTR pszLink;                  // the link's id
	LPARAM  lParam;
};

struct NMNTFCLOSED {
	NMHDR   hdr;
	UINT    id;
	UINT    reason;                   // NTFR_*
	BOOL    checked;                  // the checkbox ("don't show again")
	LPARAM  lParam;
};

// ---- Info bar ---------------------------------------------------------------------------------
//
// The keys (the app makes it a tab stop: WS_TABSTOP): Tab and the arrow keys go along the rows' links, checkboxes,
// buttons and close buttons (Tab past the last goes on to the next control); Enter or Space presses, Delete closes the
// row, Ctrl+C copies its message.

struct NtfBarData;
struct NtfHit;

class CInfoBarCtrl : public ATL::CWindowImpl<CInfoBarCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_InfoBar", 0, -1)

	CInfoBarCtrl();
	~CInfoBarCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CInfoBarCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Messages ----------------------------------------------------------------------------------
	//
	// The newest is on top; it slides in, and a message closed slides out. Past SetMaxRows the rest fold into a strip
	// ("2 more") that shows them all. The control's height follows (GetIdealHeight, NTFN_HEIGHTCHANGED).

	UINT    AddMessage(const NTFMESSAGE* message);                // its id; 0 if it can't be added
	UINT    AddMessage(UINT severity, LPCWSTR title, LPCWSTR text, UINT id = 0);
	BOOL    RemoveMessage(UINT id);                               // no NTFN_CLOSED
	void    RemoveAll();
	bool    HasMessage(UINT id) const;                            // shown (one going isn't)
	int     GetMessageCount() const;
	BOOL    SetMessageText(UINT id, LPCWSTR text);
	BOOL    SetProgress(UINT id, UINT kind, int value = 0);       // NTFP_*
	BOOL    SetChecked(UINT id, bool checked);
	bool    GetChecked(UINT id) const;
	void    SetMaxRows(int rows);                                 // 0: all; default 3

	// ---- Appearance and size -------------------------------------------------------------------------
	//
	// The font is the message font at the window's DPI; WM_SETFONT replaces it. The image list has small icons for
	// messages that don't use their severity's.

	int     GetIdealHeight() const;                               // the rows (while they slide: as far as they are)
	void    SetImageList(HIMAGELIST images);                      // the control doesn't destroy it
	void    SetColors(const NTFCOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	NTFCOLORS GetColors() const;
	void    SetString(UINT which, LPCWSTR text);                  // NTFSTR_*; nullptr: the default

private:
	enum : UINT_PTR { AnimationTimer = 1 };

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    Changed();                                            // the rows to be laid out again
	void    EnsureLayout();
	void    LayoutItems();                                        // each row for the width (and the stack)
	void    Animate();                                            // a frame: heights, NTFN_HEIGHTCHANGED, the timer
	bool    Animating() const;
	bool    AnimationsOn() const;
	void    NotifyHeight();
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	NtfHit  HitTest(POINT pt);
	void    SetHot(const NtfHit& hit);
	void    Click(const NtfHit& hit);
	void    Close(int item, UINT reason);                         // the user's: NTFN_CLOSED, then it slides out
	int     Find(UINT id) const;                                  // the item; -1 if none (or going)
	std::vector<NtfHit> FocusOrder();                             // the parts the keys go along
	int     FocusIndex(const std::vector<NtfHit>& order) const;   // the keys' part in it (the first if it's gone); -1 if none
	std::wstring String(UINT which, int number = 0) const;

	std::unique_ptr<NtfBarData> m_Data;
	HIMAGELIST m_Images = nullptr;
	NTFCOLORS m_Colors{};
	bool    m_SystemColors = true;
	HFONT   m_hFont = nullptr;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_LastHeight = 0;                                     // told (NTFN_HEIGHTCHANGED)
};

// ---- Toasts ---------------------------------------------------------------------------------------
//
// Popups in a corner of a window's client area: the newest in the corner, the older ones stacked away from it, those
// that don't fit waiting their turn. They fade in and out, follow the window when it moves or is resized, and go away
// when their time (NTFMESSAGE::timeout) runs out; the mouse over any of them stops the clocks. They don't take the
// activation. The notifications are the info bar's, sent to the window (hwndFrom: the toast's popup, idFrom: the id
// given to Attach); a click on a toast's body is NTFN_BUTTONCLICK with the button 0 (it closes, unless non-zero). The
// host isn't a window: call Detach before the window is destroyed (its WM_DESTROY). The keys: Focus (an app's
// shortcut) gives them to the newest toast, whose links and buttons Tab goes along; Up and Down go to the next toast,
// Enter presses, Delete closes, Ctrl+C copies, Escape gives the keys back. A toast with the keys stops the clocks too.

#define NTFC_TOPLEFT         0
#define NTFC_TOPRIGHT        1
#define NTFC_BOTTOMLEFT      2
#define NTFC_BOTTOMRIGHT     3        // the default

struct NtfToastData;

class CToastHost {
public:
	CToastHost();
	~CToastHost();
	CToastHost(const CToastHost&) = delete;
	CToastHost& operator=(const CToastHost&) = delete;

	BOOL    Attach(HWND window, UINT id = 0);                     // the window the toasts are shown in, and tell
	void    Detach();                                             // the toasts close at once (no notification)
	HWND    GetWindow() const;

	UINT    Show(const NTFMESSAGE* message);                      // its id; a toast with the id is replaced; 0 if it can't
	UINT    Show(UINT severity, LPCWSTR title, LPCWSTR text, UINT timeout = 5000, UINT id = 0);
	BOOL    Close(UINT id);                                       // fades out; no NTFN_CLOSED
	BOOL    Focus();                                              // the keys to the newest toast; FALSE if none shows
	void    CloseAll();
	bool    IsShown(UINT id) const;                               // shown or waiting (one going isn't)
	int     GetCount() const;
	BOOL    SetMessageText(UINT id, LPCWSTR text);
	BOOL    SetProgress(UINT id, UINT kind, int value = 0);       // NTFP_*
	bool    GetChecked(UINT id) const;

	void    SetCorner(UINT corner);                               // NTFC_*
	void    SetMaxShown(int count);                               // 0 (the default): as many as fit
	void    SetWidth(int dips);                                   // default 360
	void    SetAnimation(int on);                                 // 1 on, 0 off, -1 (the default) as Windows says
	void    SetImageList(HIMAGELIST images);
	void    SetColors(const NTFCOLORS* colors);                   // nullptr: from the system colors
	NTFCOLORS GetColors() const;
	void    SetString(UINT which, LPCWSTR text);                  // NTFSTR_*

private:
	friend class NtfToastWnd;
	friend struct NtfToastData;
	std::unique_ptr<NtfToastData> m_Data;
};
