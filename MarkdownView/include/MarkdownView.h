#pragma once

// Markdown view: shows a Markdown document (CommonMark with GitHub's extensions: tables, task lists, strikethrough,
// autolinks, alerts, footnotes, emoji shortcodes, and the raw HTML readmes use for centered logos, badges and
// <details>), drawn with Direct2D and DirectWrite. A viewer only: the app gives it the text. The API follows the
// common controls: C-style structures and WM_NOTIFY codes (MDVN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>

#include <cstdint>
#include <memory>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_MARKDOWNVIEW[] = L"WC_MarkdownView";

// ---- Styles (the window's low word) ------------------------------------------

#define MDVS_CENTERED      0x0001   // the text column at most MDVSTYLE::maxWidth wide, centered
#define MDVS_TASKCLICK     0x0002   // task list boxes can be clicked (MDVN_TASKTOGGLE)
#define MDVS_NOLINKTIPS    0x0004   // no tooltip with a link's address
#define MDVS_NOSELECTION   0x0008   // the text can't be selected

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

enum MDVSYNTAX {               // the built-in highlighting's colors
	MDVSYN_KEYWORD,
	MDVSYN_TYPE,               // also a shell variable
	MDVSYN_STRING,
	MDVSYN_COMMENT,
	MDVSYN_NUMBER,
	MDVSYN_PREPROCESSOR,
	MDVSYN_TAG,
	MDVSYN_ATTRIBUTE,
	MDVSYN_COUNT
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
	COLORREF crSelBack;            // behind selected text (which keeps its colors)
	COLORREF crSelBackInactive;    // the same while the control doesn't have the focus
	COLORREF crSyntax[MDVSYN_COUNT];
	COLORREF crFindBack;           // HighlightAll's matches
};

// ---- Code highlighting -------------------------------------------------------------
//
// An app's highlighter colors a code block: it fills up to maxSpans spans and returns how many it has (more than
// maxSpans: it's called again with room for them all), or -1 for the built-in highlighting (C, C++, C#, Java,
// JavaScript, TypeScript, JSON, Python, PowerShell, shell, Rust, Go, SQL, CSS, XML/HTML, YAML).

#define MDVSPF_BOLD     0x0001
#define MDVSPF_ITALIC   0x0002

struct MDVSPAN {
	UINT     start;            // in the code's characters
	UINT     length;
	COLORREF color;
	UINT     flags;            // MDVSPF_*
};

typedef int (CALLBACK* MDVHIGHLIGHTPROC)(LPCWSTR pszLang, LPCWSTR pszCode, UINT cch, MDVSPAN* spans, int maxSpans, LPARAM lParam);

// ---- Headings (a table of contents) --------------------------------------------

struct MDVHEADING {
	int     level;                 // 1..6
	LPCWSTR pszText;               // valid until the text changes
	LPCWSTR pszAnchor;             // GitHub's: "getting-started", for ScrollToAnchor
	size_t  sourceOffset;
};

// ---- Finding and hit testing -------------------------------------------------------

#define MDVFF_MATCHCASE    0x0001
#define MDVFF_WHOLEWORD    0x0002
#define MDVFF_UP           0x0004   // Find: backwards

#define MDVHT_NOWHERE      0
#define MDVHT_TEXT         1
#define MDVHT_LINK         2        // pszUrl, pszTitle
#define MDVHT_IMAGE        3        // pszUrl (the image's source), pszTitle (its alternative text)
#define MDVHT_TASK         4        // a task list's box
#define MDVHT_CODECOPY     5        // a code block's copy button
#define MDVHT_SUMMARY      6        // a <details>' summary

struct MDVHITTEST {
	POINT   pt;                    // in: client coordinates
	UINT    part;                  // out: MDVHT_*
	size_t  sourceOffset;          // the source offset of the character there (MDVHT_TEXT, MDVHT_LINK, MDVHT_TASK: its mark)
	LPCWSTR pszUrl;                // valid until the text changes
	LPCWSTR pszTitle;
};

// ---- Notifications -------------------------------------------------------------

#define MDVN_FIRST          (0U - 3400U)   // outside the common controls ranges
#define MDVN_SCROLLED       (MDVN_FIRST - 0)   // NMMDVSCROLL; the view scrolled
#define MDVN_GETIMAGE       (MDVN_FIRST - 1)   // NMMDVIMAGE; an image that isn't a local file or a data: URL
#define MDVN_LINKCLICK      (MDVN_FIRST - 2)   // NMMDVLINK; non-zero: the app handled it (else a #link scrolls; nothing else is opened)
#define MDVN_LINKHOT        (MDVN_FIRST - 3)   // NMMDVLINK; the link under the mouse changed (pszUrl null: none)
#define MDVN_SELCHANGED     (MDVN_FIRST - 4)   // NMHDR; the user changed the selection
#define MDVN_CONTEXTMENU    (MDVN_FIRST - 5)   // NMMDVCONTEXTMENU
#define MDVN_TASKTOGGLE     (MDVN_FIRST - 6)   // NMMDVTASK; MDVS_TASKCLICK: a box clicked; non-zero stops it, else the text changes

struct NMMDVSCROLL {
	NMHDR  hdr;
	int    heading;                // the heading at or above the top of the view (GetHeading), -1 if none
	size_t topOffset;              // GetTopOffset
};

struct NMMDVLINK {
	NMHDR   hdr;
	LPCWSTR pszUrl;
	LPCWSTR pszTitle;
	size_t  sourceOffset;          // the link's text in the source
};

struct NMMDVCONTEXTMENU {
	NMHDR       hdr;
	POINT       pt;                // screen coordinates; from the keyboard, the selection's or the view's corner
	MDVHITTEST  hit;               // what's there
	BOOL        hasSelection;
};

struct NMMDVTASK {
	NMHDR  hdr;
	size_t sourceOffset;           // the character between [ and ], which becomes 'x' or ' '
	BOOL   checked;                // the state it's going to
};

// The app gives the image now (pStream: the control releases it; hBitmap: the control copies it, the app keeps
// it), or later with SetImage, or never (it stays a frame). Asked once per source while the text is the same.
struct NMMDVIMAGE {
	NMHDR    hdr;
	LPCWSTR  pszUrl;
	IStream* pStream;          // out: PNG, JPEG, GIF, BMP, TIFF, ICO, WebP (with its codec) or SVG
	HBITMAP  hBitmap;          // out
};

// ---- Control class --------------------------------------------------------------------------

struct MdvState;
struct MdvText;

class CMarkdownViewCtrl : public ATL::CWindowImpl<CMarkdownViewCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_MarkdownView", CS_DBLCLKS, -1)

	CMarkdownViewCtrl();
	~CMarkdownViewCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");
	enum : UINT { WM_MDV_IMAGES = WM_USER + 100 };   // posted by the worker thread: images are decoded
	enum : UINT_PTR { CopiedTimer = 1, ScrollTimer = 2 };

	BEGIN_MSG_MAP(CMarkdownViewCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_COPY, OnCopy)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_MDV_IMAGES, OnImages)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseHWheel)
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
	void SetImage(LPCWSTR pszUrl, IStream* pStream);   // an image MDVN_GETIMAGE didn't give (null: it failed)
	void SetHighlighter(MDVHIGHLIGHTPROC proc, LPARAM lParam);   // null: the built-in highlighting

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

	// ---- Selection ----
	BOOL HasSelection() const;
	void SelectAll();
	void ClearSelection();
	void GetSelection(size_t& anchorOffset, size_t& caretOffset) const;   // source offsets; equal: none
	void SetSelection(size_t anchorOffset, size_t caretOffset);         // the texts at the source offsets
	CString GetSelectedText() const;           // plain text: list markers, tabs between cells
	CString GetSelectedHtml() const;
	CString GetSelectedMarkdown() const;       // the source, from the start of the selection's first line
	BOOL Copy();                               // plain text and HTML (CF_HTML); Ctrl+C

	// ---- Finding ----
	BOOL Find(LPCWSTR pszText, UINT flags);    // MDVFF_*: selects the next match from the selection (going round) and shows it
	int HighlightAll(LPCWSTR pszText, UINT flags);   // shades every match (their number; at most 100,000); null: none
	BOOL HitTest(MDVHITTEST& ht) const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnImages(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseHWheel(UINT, WPARAM, LPARAM, BOOL&);
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
	bool BoxRect(size_t box, float rc[4]) const;   // client DIPs: left, top, right, bottom
	float SidewaysRoom(size_t box) const;      // how far the box's content scrolls sideways (0: it fits)
	bool SidewaysBar(size_t box, float track[4], float thumb[4]) const;
	void ScrollSideways(size_t box, float offset);
	bool CopyButton(size_t box, float rc[4]) const;
	void CopyCode(int block);
	void ImagesChanged(const std::vector<uint64_t>& keys);

	// selection, links and finding (MarkdownViewSelection.cpp)
	void UpdateLeaves();
	// the text nearest a client point: the place between characters, whether the point is on the text, the character
	bool PositionAt(POINT pt, int& leaf, size_t& pos, bool* inside = nullptr, size_t* hit = nullptr) const;
	const MdvText* LeafText(int leaf, float& x, float& y) const;   // a text leaf's layout and where it's drawn (client DIPs)
	bool PressText(POINT pt, bool shift, bool doubleClick);
	void DragSelection(POINT pt);
	void EndSelection(POINT pt);
	void AutoScroll();
	void UpdateHotLink(POINT pt);
	bool LinkAt(POINT pt, int& leaf, int& link, float rc[4]) const;
	bool TaskAt(POINT pt, int& item) const;
	void ClickLink(int leaf, int link);
	void ToggleTask(int item);
	void ShowPosition(int leaf, size_t pos);
	void SelectionChanged(bool notify);

	// painting (MarkdownViewPaint.cpp)
	void Paint();
	bool CreateTarget();
	void DiscardTarget();
	void DiscardLayouts();
	void UpdateFormats();
	bool Measure(size_t box);                  // makes the box's text layout; false if it has none
	bool MeasureTable(size_t box);
	void PaintBox(size_t box, float x, float y);
	void PaintMarker(size_t box, float x, float y);
	void UpdateScrollBarTheme();
	bool IsDark() const;

	std::unique_ptr<MdvState> m_State;
};
