#pragma once

// Markdown view: shows a Markdown document (CommonMark with GitHub's extensions: tables, task lists, strikethrough,
// autolinks, alerts, footnotes, emoji shortcodes, and the raw HTML readmes use for centered logos, badges and
// <details>), drawn with Direct2D and DirectWrite. A viewer only: the app gives it the text. The API follows the
// common controls: C-style structures and WM_NOTIFY codes (MDVN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>

#include <memory>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_MARKDOWNVIEW[] = L"WC_MarkdownView";

// ---- Styles (the window's low word) ------------------------------------------

#define MDVS_CENTERED      0x0001   // the text column at most MDVSTYLE::maxWidth wide, centered

// ---- Parse flags ---------------------------------------------------------------

#define MDVF_NOHTML        0x0001   // raw HTML is shown as text
#define MDVF_NOTABLES      0x0002
#define MDVF_HARDBREAKS    0x0004   // every line break is a hard one (chat style)
#define MDVF_NOEMOJI       0x0008   // :shortcodes: stay text

// ---- Style ----------------------------------------------------------------------

#define MDVSM_FONT           0x0001
#define MDVSM_CODEFONT       0x0002
#define MDVSM_FONTSIZE       0x0004
#define MDVSM_HEADINGSCALE   0x0008
#define MDVSM_SPACING        0x0010
#define MDVSM_MAXWIDTH       0x0020
#define MDVSM_PADDING        0x0040
#define MDVSM_ALL            0x007F

struct MDVSTYLE {
	UINT  mask;                    // MDVSM_*: the members to set (SetTextStyle) or that were filled (GetTextStyle)
	WCHAR szFont[LF_FACESIZE];     // empty: Segoe UI
	WCHAR szCodeFont[LF_FACESIZE]; // empty: Cascadia Mono if installed, else Consolas
	float fontSize;                // DIPs (16)
	float headingScale[6];         // h1..h6, of fontSize
	float spacing;                 // between blocks, in ems (1)
	int   maxWidth;                // DIPs, with MDVS_CENTERED (980)
	int   padding;                 // DIPs around the text (24)
};

// ---- Colors ---------------------------------------------------------------------

enum MDVALERT {
	MDVA_NOTE,
	MDVA_TIP,
	MDVA_IMPORTANT,
	MDVA_WARNING,
	MDVA_CAUTION,
	MDVA_COUNT
};

struct MDVCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crMuted;              // quotes, the footnotes' numbers
	COLORREF crHeading;
	COLORREF crLink;
	COLORREF crBorder;             // the rules, the lines under h1 and h2, the quotes' bars
	COLORREF crCodeBack;           // code blocks and spans
	COLORREF crCodeText;
	COLORREF crMarkBack;           // <mark>
	COLORREF crAlert[MDVA_COUNT];
	COLORREF crTableBorder;
	COLORREF crTableHeaderBack;
	COLORREF crTableStripeBack;
	COLORREF crSelBack;
	COLORREF crSelText;
};

// ---- Headings (a table of contents) --------------------------------------------

struct MDVHEADING {
	int     level;                 // 1..6
	LPCWSTR pszText;               // valid until the text changes
	LPCWSTR pszAnchor;             // GitHub's: "getting-started", for ScrollToAnchor
	size_t  sourceOffset;
};

// ---- Notifications -------------------------------------------------------------

#define MDVN_FIRST          (0U - 3400U)   // outside the common controls ranges
#define MDVN_SCROLLED       (MDVN_FIRST - 0)   // NMMDVSCROLL; the view scrolled

struct NMMDVSCROLL {
	NMHDR  hdr;
	int    heading;                // the heading at or above the top of the view (GetHeading), -1 if none
	size_t topOffset;              // GetTopOffset
};

// ---- Control class --------------------------------------------------------------------------

struct MdvState;

class CMarkdownViewCtrl : public ATL::CWindowImpl<CMarkdownViewCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_MarkdownView", CS_DBLCLKS, -1)

	CMarkdownViewCtrl();
	~CMarkdownViewCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CMarkdownViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Content ----
	void SetText(LPCWSTR pszMarkdown, size_t cch = (size_t)-1);
	BOOL LoadFile(LPCWSTR pszPath);            // UTF-8 (with a BOM or not), UTF-16 with a BOM, else the ANSI code page; sets the base path
	CString GetText() const;
	void SetParseFlags(UINT mdvf);             // parses the text again
	UINT GetParseFlags() const;
	void SetBasePath(LPCWSTR pszDir);          // for relative images and links
	CString GetBasePath() const;

	// ---- Look ----
	void SetTextStyle(const MDVSTYLE& style);
	void GetTextStyle(MDVSTYLE& style) const;
	void SetColors(const MDVCOLORS* colors);   // null: back to the system's (and the dark mode's)
	MDVCOLORS GetColors() const;
	void SetZoom(int percent);                 // 25..400; Ctrl+wheel too
	int GetZoom() const;

	// ---- Navigation ----
	int GetHeadingCount() const;
	BOOL GetHeading(int index, MDVHEADING& heading) const;
	BOOL ScrollToAnchor(LPCWSTR pszAnchor);    // "#name" or "name": a heading, a footnote, an <a name>
	BOOL ScrollToHeading(int index);
	void ScrollToOffset(size_t sourceOffset);  // the block with the offset at the top
	size_t GetTopOffset() const;               // the source offset of the block at the top, to come back to it
	int GetTopHeading() const;                 // the heading at or above the top of the view, -1 if none

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnVScroll(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	// layout (MarkdownViewCtrl.cpp)
	void Reparse();
	void Relayout();                           // a new flow: the zoom, the style, details opened or closed
	void UpdateMetrics();
	float ColumnLeft() const;
	float ColumnWidth() const;
	float ViewHeight() const;                  // DIPs
	void MeasureVisible();
	void ScrollTo(float y, bool notify = true);
	void ScrollToBox(int box);
	void UpdateScrollBar();
	int BoxAtPoint(POINT pt) const;            // client pixels
	void ToggleDetails(int block);

	// painting (MarkdownViewPaint.cpp)
	void Paint();
	bool CreateTarget();
	void DiscardTarget();
	void DiscardLayouts();
	void UpdateFormats();
	bool Measure(size_t box);                  // makes the box's text layout; false if it has none
	void PaintBox(size_t box, float x, float y);
	void PaintMarker(size_t box, float x, float y);
	void UpdateScrollBarTheme();
	bool IsDark() const;

	std::unique_ptr<MdvState> m_State;
};
