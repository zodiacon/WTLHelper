#pragma once

// Log view: a read-only, line-based text viewer for logs and traces with millions of lines. Lines are kept
// by the control (appended from any thread, with an optional limit that drops the oldest) or supplied by
// the app on demand (virtual mode). Each line has a level and a style that pick its colors. The API follows
// the common controls: C-style structures and WM_NOTIFY codes (LGVN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_LOGVIEW[] = L"WC_LogView";

constexpr size_t LGV_NOLINE = (size_t)-1;

// ---- Levels -------------------------------------------------------------------
//
// A line's level (0..31) picks its colors when the line has no style of its own, and (later) filters by a
// mask of 1 << level. The predefined levels have default styles; the app may use any of 0..31.

enum : BYTE {
	LGVL_NONE,
	LGVL_TRACE,      // dimmed
	LGVL_DEBUG,      // dimmed
	LGVL_INFO,
	LGVL_WARNING,    // amber, with a gutter marker
	LGVL_ERROR,      // red, with a gutter marker
	LGVL_CRITICAL,   // white on red, with a gutter marker
	LGVL_MAX = 31,
};

// ---- Styles -------------------------------------------------------------------
//
// Style 0 on a line means "the style of its level". Styles 1..255 are the app's (SetStyle).

#define LGVSF_BOLD     0x0001
#define LGVSF_ITALIC   0x0002
#define LGVSF_DIM      0x0004   // the text color blended halfway into the background

struct LGVSTYLE {
	COLORREF crText   = CLR_DEFAULT;   // CLR_DEFAULT: the view's text color
	COLORREF crBack   = CLR_DEFAULT;   // CLR_DEFAULT: the view's background
	COLORREF crMarker = CLR_NONE;      // a bar in the gutter; CLR_NONE: none
	UINT     flags    = 0;             // LGVSF_*
};

// ---- Lines ---------------------------------------------------------------------

struct LGVLINE {
	LPCWSTR pszText = nullptr;
	int     cchText = -1;              // -1: null-terminated
	BYTE    level = LGVL_NONE;
	BYTE    style = 0;
	LPARAM  lParam = 0;
};

struct LGVLINEINFO {
	ULONGLONG number;                  // as shown in the gutter: 1-based, counting lines dropped by the limit
	BYTE      level;
	BYTE      style;
	LPARAM    lParam;
	size_t    length;                  // characters
};

// ---- Colors ----------------------------------------------------------------------

struct LGVCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crGutterBack;
	COLORREF crGutterText;
	COLORREF crSelBack;
	COLORREF crSelText;
	COLORREF crSelBackInactive;
	COLORREF crSelTextInactive;
};

// ---- Hit testing ----------------------------------------------------------------

#define LGVHT_NOWHERE  0
#define LGVHT_GUTTER   1
#define LGVHT_TEXT     2   // on a line, right of the gutter (also past the end of its text)
#define LGVHT_BELOW    3   // under the last line

// ---- Notifications -------------------------------------------------------------

#define LGVN_FIRST          (0U - 2850U)   // outside the common controls ranges
#define LGVN_GETDISPINFO    (LGVN_FIRST - 0)   // NMLGVDISPINFO; virtual mode: a line is needed
#define LGVN_CONTEXTMENU    (LGVN_FIRST - 1)   // NMLGVLINE; pt in screen coords, line may be LGV_NOLINE
#define LGVN_ACTIVATE       (LGVN_FIRST - 2)   // NMLGVLINE; a double click or Enter on a line
#define LGVN_FOLLOWCHANGED  (LGVN_FIRST - 3)   // NMHDR; the user's scrolling turned following the tail on or off

struct NMLGVLINE {
	NMHDR  hdr;
	size_t line;
	POINT  pt;
};

// Virtual mode: the parent points pszText at the line's text (cchText -1 if null-terminated), which has to
// last until the notification returns, and may set level, style and lParam (zero on entry). It's sent while
// painting, so the handler must not change the view.
struct NMLGVDISPINFO {
	NMHDR   hdr;
	size_t  line;
	LPCWSTR pszText;
	int     cchText;
	BYTE    level;
	BYTE    style;
	LPARAM  lParam;
};

// ---- Control class --------------------------------------------------------------------------

class LgStore;
class LgBatch;
class LgTextWindow;

class CLogViewCtrl : public ATL::CWindowImpl<CLogViewCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_LogView", 0, -1)

	CLogViewCtrl();
	~CLogViewCtrl();
	friend class LgTextWindow;

	enum : UINT { WM_LGV_FLUSH = WM_USER + 100 };   // posted to itself: appended lines to take in

	BEGIN_MSG_MAP(CLogViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_LGV_FLUSH, OnFlush)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the text area, a child window
		MESSAGE_HANDLER(WM_PAINT, OnTextPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnTextPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnTextSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnTextFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnTextFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnTextGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnTextKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnTextLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnTextLButtonDblClk)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnTextRButtonDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnTextContextMenu)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnTextMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnTextMouseWheel)
		MESSAGE_HANDLER(WM_VSCROLL, OnTextVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnTextHScroll)
	END_MSG_MAP()

	// ---- Lines ----------------------------------------------------------------------------
	//
	// AppendLine/AppendLines/AppendText may be called from any thread; everything else from the window's.
	// On the window's thread a line is in the view (GetLineCount) when the call returns. From other threads
	// lines are queued and taken in together by a posted message, so a busy writer costs one repaint per
	// batch; lines appended before the window exists come in when it's created.

	void    AppendLine(LPCWSTR text, BYTE level = LGVL_NONE, BYTE style = 0, LPARAM lParam = 0);
	void    AppendLines(const LGVLINE* lines, int count);
	// splits at "\r\n", "\n" and "\r"; a final line break doesn't start another line. cch -1: null-terminated.
	void    AppendText(LPCWSTR text, int cch = -1, BYTE level = LGVL_NONE, BYTE style = 0);
	void    Clear();                                  // all lines, queued ones too; leaves virtual mode
	void    Flush();                                  // takes in lines queued by other threads now
	size_t  GetLineCount() const;
	ATL::CString GetLineText(size_t line) const;
	BOOL    GetLineInfo(size_t line, LGVLINEINFO* pInfo) const;
	BOOL    SetLineStyle(size_t line, BYTE level, BYTE style);   // not in virtual mode
	// 0: no limit. Past it the oldest lines are dropped; the gutter's numbers keep counting.
	void    SetLineLimit(size_t maxLines);
	size_t  GetLineLimit() const;

	// ---- Virtual mode -------------------------------------------------------------------
	//
	// The parent owns the lines and supplies them with LGVN_GETDISPINFO. Setting the count enters virtual
	// mode (lines the control held are dropped, appending is ignored); Clear leaves it.

	void    SetVirtualLineCount(size_t count);
	bool    IsVirtual() const;
	void    InvalidateLines(size_t first, size_t count);  // repaints them (their text is asked for again)

	// ---- View --------------------------------------------------------------------------

	// following the tail keeps the last line in view as lines come in. Scrolling up turns it off, scrolling
	// to the end (Ctrl+End, or the scroll bar to the bottom) back on; either sends LGVN_FOLLOWCHANGED.
	void    SetFollowTail(bool follow);               // no notification
	bool    IsFollowingTail() const;
	void    EnsureVisible(size_t line);
	void    SetTopLine(size_t line);
	size_t  GetTopLine() const;
	int     GetVisibleLineCount() const;              // whole lines that fit
	size_t  HitTest(POINT pt, UINT* pPart = nullptr) const;   // client coords; LGVHT_* in *pPart
	BOOL    GetLineRect(size_t line, RECT* prc) const;        // client coords; FALSE if not in view
	void    ShowLineNumbers(bool show);
	bool    AreLineNumbersShown() const;
	void    SetTabSize(int columns);                  // 1..16, default 4
	int     GetTabSize() const;
	void    BeginUpdate();                            // nests; suppresses painting and scroll bar updates
	void    EndUpdate();
	HWND    GetTextWindow() const;                    // the child that shows the lines (scroll bars, focus)

	// ---- Appearance -------------------------------------------------------------------
	//
	// The default font is Consolas, 10 points at the window's DPI; WM_SETFONT replaces it (the app then
	// replaces it for a new DPI). Bold and italic come from the same face.

	void    SetColors(const LGVCOLORS* colors);       // nullptr: system colors (follows WTLHelper dark mode)
	LGVCOLORS GetColors() const;                      // the colors in effect
	BOOL    SetStyle(BYTE index, const LGVSTYLE* style);        // 1..255; nullptr resets it
	BOOL    GetStyle(BYTE index, LGVSTYLE* style) const;
	BOOL    SetLevelStyle(BYTE level, const LGVSTYLE* style);   // 0..31; nullptr: back to the default
	BOOL    GetLevelStyle(BYTE level, LGVSTYLE* style) const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFlush(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnTextPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextHScroll(UINT, WPARAM, LPARAM, BOOL&);

	// lines
	struct LineData {                                 // a line as drawn: owned, or from the parent
		LPCWSTR text;
		size_t  length;
		BYTE    level;
		BYTE    style;
		LPARAM  lParam;
	};
	bool    OnWindowThread() const;
	void    Enqueue(const LGVLINE* lines, int count, LPCWSTR text, int cch, BYTE level, BYTE style);
	void    PostFlush();
	void    LinesAdded();                             // after the store changed: scroll range, follow, repaint
	bool    GetLine(size_t line, LineData& data) const;
	ULONGLONG LineNumber(size_t line) const;
	size_t  MaxColumns() const;

	// layout and scrolling
	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Layout();
	RECT    GetTextRect() const;                      // the text area's client rect
	int     GutterWidth() const;
	int     GetPageLines() const;
	size_t  MaxTopLine() const;
	int     MaxLeft() const;
	void    UpdateScrollBars();
	void    ScrollTo(size_t topLine, int left, bool byUser);
	void    SetFollow(bool follow, bool notify);

	// painting
	void    Paint(WTL::CDCHandle dc, const RECT& rcPaint);
	void    PaintLine(WTL::CDCHandle dc, size_t line, const RECT& rc, const LGVCOLORS& c, int gutter);
	LGVSTYLE EffectiveStyle(BYTE level, BYTE style) const;
	HFONT   FontFor(UINT flags) const;

	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    NotifyLine(UINT code, size_t line, POINT ptScreen);

	std::unique_ptr<LgStore>      m_Store;
	std::unique_ptr<LgTextWindow> m_Text;
	bool                          m_Virtual = false;
	size_t                        m_VirtualCount = 0;
	std::atomic<DWORD>            m_ThreadId = 0;    // the window's, once created; read by appending threads

	// lines from other threads
	SRWLOCK                       m_PendingLock = SRWLOCK_INIT;
	std::unique_ptr<LgBatch>      m_Pending;
	std::atomic<bool>             m_FlushPosted = false;
	bool                          m_LinesChanged = false;   // appended on the window's thread, not yet laid out

	// view state
	size_t                        m_TopLine = 0;
	int                           m_Left = 0;        // horizontal scroll, pixels
	bool                          m_Follow = true;
	bool                          m_LineNumbers = true;
	int                           m_TabSize = 4;
	int                           m_UpdateCount = 0;
	int                           m_WheelRemainder = 0;
	int                           m_HWheelRemainder = 0;

	// metrics (pixels at the current DPI)
	UINT                          m_Dpi = 96;
	int                           m_LineHeight = 16;
	int                           m_TextOffset = 0;  // text top in a line
	int                           m_CharWidth = 8;   // average; exact for a fixed-pitch font
	int                           m_DigitWidth = 8;
	bool                          m_FixedPitch = true;
	int                           m_Pad = 4;
	int                           m_MarkerWidth = 3;

	// fonts, colors, styles
	HFONT                         m_hFont = nullptr; // in effect (owned if m_OwnFont)
	bool                          m_OwnFont = true;
	WTL::CFont                    m_Fonts[3];        // bold, italic, bold italic
	bool                          m_SystemColors = true;
	LGVCOLORS                     m_Colors{};
	LGVSTYLE                      m_Styles[256];     // [0] unused
	LGVSTYLE                      m_LevelStyles[32];
	std::wstring                  m_DrawBuffer;      // a line with its tabs expanded
	mutable std::wstring          m_VirtualText;     // the last line the parent supplied
	size_t                        m_SeenColumns = 0; // virtual mode: the widest line painted so far
};
