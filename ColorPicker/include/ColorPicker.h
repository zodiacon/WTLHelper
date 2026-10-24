#pragma once

// Color picker: a saturation/value area, a hue bar and an optional alpha bar, with a panel on the right: the new
// color over the original one, and fields for H, S, V, R, G, B, alpha and hex. The color is kept as HSV, so the
// hue doesn't jump when the color goes gray or black. The API follows the common controls: C-style structures
// and WM_NOTIFY codes (CPKN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlgdi.h>

#include <cstdint>
#include <memory>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_COLORPICKER[] = L"WC_ColorPicker";

// ---- Styles ------------------------------------------------------------------

#define CPKS_NOALPHA   0x0001   // no alpha bar or field (the alpha stays as set)
#define CPKS_NOFIELDS  0x0002   // no fields
#define CPKS_NOSWATCH  0x0004   // no new/original swatch (and no eyedropper button)
#define CPKS_NOPALETTE 0x0008   // no palette, custom or recent colors
#define CPKS_NOEYEDROPPER 0x0010   // no eyedropper button (StartEyedropper still works)

// ---- Swatches ------------------------------------------------------------------
//
// Under the fields, in rows of 12: the palette (up to 4 rows), the custom colors (12 slots) and the recent ones
// (up to 12, the latest first). They're RGB colors: picking one keeps the alpha.

#define CPK_MAXPALETTE    48
#define CPK_CUSTOMCOLORS  12
#define CPK_MAXRECENT     12

// ---- Colors -----------------------------------------------------------------------

struct CPKHSV {
	double h;      // 0..360
	double s;      // 0..1
	double v;      // 0..1
};

struct CPKCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crFieldBack;      // the fields' background
	COLORREF crBorder;         // around the parts and the fields
	COLORREF crCheck1;         // the checkerboard under transparent colors
	COLORREF crCheck2;
};

// ---- Hit testing ----------------------------------------------------------------

#define CPKHT_NOWHERE   0
#define CPKHT_SV        1   // the saturation/value area
#define CPKHT_HUE       2
#define CPKHT_ALPHA     3
#define CPKHT_FIELD     4   // a field (its frame, spin buttons included)
#define CPKHT_NEWCOLOR  5   // the swatch's new color
#define CPKHT_OLDCOLOR  6   // the swatch's original color (clicking it goes back to it)
#define CPKHT_EYEDROPPER 7  // the eyedropper button (and the color picked with it)
#define CPKHT_PALETTE   8   // a palette color (the index: HitTest's *pIndex)
#define CPKHT_CUSTOM    9   // a custom color slot
#define CPKHT_RECENT    10  // a recent color

// ---- Notifications -------------------------------------------------------------
//
// While the user drags, types or presses keys, every change sends CPKN_COLORCHANGING; the end of the change
// (the mouse button released, a key, or a field that was typed in losing the focus or getting Enter) sends
// CPKN_COLORCHANGED. Escape during a drag goes back to the color it started with and sends CPKN_COLORCHANGING
// with that color, and no CPKN_COLORCHANGED; so do Escape and a right click with the eyedropper, which
// otherwise changes the color as it moves and sends CPKN_COLORCHANGED on the click that picks. Setting the color
// with the API sends neither.

#define CPKN_FIRST          (0U - 2900U)   // outside the common controls ranges
#define CPKN_COLORCHANGING  (CPKN_FIRST - 0)   // NMCOLORPICKER
#define CPKN_COLORCHANGED   (CPKN_FIRST - 1)   // NMCOLORPICKER
#define CPKN_CUSTOMCHANGED  (CPKN_FIRST - 2)   // NMHDR; the user saved or cleared a custom color
#define CPKN_DROPDOWN       (CPKN_FIRST - 3)   // NMHDR; a color button is about to open its picker
#define CPKN_CLOSEUP        (CPKN_FIRST - 4)   // NMHDR; a color button's picker closed

struct NMCOLORPICKER {
	NMHDR    hdr;
	COLORREF color;
	BYTE     alpha;
	CPKHSV   hsv;
	UINT     part;             // CPKHT_*: what the user changed
};

// ---- Control class --------------------------------------------------------------------------

class CpGradientWindow;
class CpSwatchWindow;
class CpMagnifier;
class CpAccessible;

class CColorPickerCtrl : public ATL::CWindowImpl<CColorPickerCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_ColorPicker", 0, -1)

	CColorPickerCtrl();
	~CColorPickerCtrl();
	friend class CpGradAccessible;
	friend class CpSwatchAccessible;

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");
	// posted to itself by the eyedropper's mouse hook; wParam: PickMove, PickCommit or PickCancel
	enum : UINT { WM_CPK_PICK = WM_USER + 200 };
	enum : WPARAM { PickMove, PickCommit, PickCancel };

	BEGIN_MSG_MAP(CColorPickerCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_CPK_PICK, OnPick)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	ALT_MSG_MAP(1)   // the gradients, a child window
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnGradPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnGradPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnGradFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnGradFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGradGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnGradKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnGradLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnGradLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnGradMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnGradLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnGradCaptureChanged)
		MESSAGE_HANDLER(WM_UPDATEUISTATE, OnGradUpdateUIState)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnGradMouseWheel)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGradGetObject)
	ALT_MSG_MAP(2)   // the swatches, a child window (for the focus and the keys; the mouse goes through it)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnSwPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnSwPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSwFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnSwFocus)
		MESSAGE_HANDLER(WM_UPDATEUISTATE, OnSwUpdateUIState)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnSwGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnSwKeyDown)
		MESSAGE_HANDLER(WM_NCHITTEST, OnSwNcHitTest)
		MESSAGE_HANDLER(WM_GETOBJECT, OnSwGetObject)
	END_MSG_MAP()

	// ---- Color ---------------------------------------------------------------------------

	void      SetColor(COLORREF color);           // keeps the hue (and saturation) the color leaves undefined
	COLORREF  GetColor() const;
	void      SetAlpha(BYTE alpha);
	BYTE      GetAlpha() const;
	void      SetHsv(const CPKHSV& hsv);          // h wrapped into 0..360, s and v clamped
	CPKHSV    GetHsv() const;
	// shown under the new color in the swatch; clicking it goes back to it. CLR_NONE: none (the default).
	void      SetOriginalColor(COLORREF color, BYTE alpha = 255);
	COLORREF  GetOriginalColor(BYTE* pAlpha = nullptr) const;

	// ---- Parts -----------------------------------------------------------------------------
	//
	// The gradients are one tab stop, keys going to the focused part, which a click or Tab picks (Tab goes on
	// to the fields from the last part). The area: arrows move the marker (Shift: ten times as far), Home/End
	// to the left and right edges, Page Up/Down to the top and bottom. The bars: Up/Down (and Left/Right) a
	// step, Shift or Page Up/Down further, Home/End to the ends. A field: typing changes the color as soon as
	// the text is a value; Up/Down step it (Shift: by ten), as do its spin buttons and the mouse wheel; Enter
	// or leaving it puts the text right, Escape puts back the value it had.

	// client coords; CPKHT_*, with the swatch's index for CPKHT_PALETTE, CPKHT_CUSTOM and CPKHT_RECENT (else -1)
	UINT      HitTest(POINT pt, int* pIndex = nullptr) const;
	BOOL      GetPartRect(UINT part, RECT* prc) const;   // client coords; FALSE if not shown (or CPKHT_FIELD)
	void      SetFocusPart(UINT part);            // CPKHT_SV, CPKHT_HUE or CPKHT_ALPHA
	UINT      GetFocusPart() const;
	HWND      GetGradientWindow() const;          // the child that shows the area and the bars
	// the size that shows everything with a square area as tall as the panel (at least 200 DIPs)
	SIZE      GetIdealSize() const;

	// ---- Swatches --------------------------------------------------------------------------
	//
	// Clicking a palette or recent color, or a custom slot holding one, picks it. Clicking an empty custom slot
	// saves the color there, as Ctrl+click does on a full one; right-clicking a slot offers to save or clear
	// it. Either sends CPKN_CUSTOMCHANGED, for the app to keep them. Recent colors are the app's to add
	// (ColorPickerDialog adds the color picked).

	void      SetPalette(const COLORREF* colors, int count);   // nullptr: the default one (24 colors)
	int       GetPalette(COLORREF* colors, int max) const;     // the count; colors may be nullptr
	void      SetCustomColors(const COLORREF* colors, int count);   // CLR_NONE: an empty slot
	int       GetCustomColors(COLORREF* colors, int max) const;     // CPK_CUSTOMCOLORS slots
	void      AddRecentColor(COLORREF color);     // to the front, once
	void      SetRecentColors(const COLORREF* colors, int count);
	int       GetRecentColors(COLORREF* colors, int max) const;

	// ---- Eyedropper ------------------------------------------------------------------------
	//
	// Picks a color from anywhere on the screen: a magnifier follows the mouse and the color follows the pixel
	// under it. A click keeps it; Escape, a right click or losing the mouse goes back to the color before.

	void      StartEyedropper();
	void      CancelEyedropper();
	bool      IsEyedropperActive() const;

	// ---- Appearance ------------------------------------------------------------------------
	//
	// The fields use the message font at the window's DPI; WM_SETFONT replaces it (the app then replaces it
	// for a new DPI).

	void      SetColors(const CPKCOLORS* colors); // nullptr: system colors (follows WTLHelper dark mode)
	CPKCOLORS GetColors() const;                  // the colors in effect

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColorEdit(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPick(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnGradPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradUpdateUIState(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGradGetObject(UINT, WPARAM, LPARAM, BOOL&);

	LRESULT OnSwPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwUpdateUIState(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwNcHitTest(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSwGetObject(UINT, WPARAM, LPARAM, BOOL&);

	struct Hsv { double h, s, v; };               // CpHsv, which the header doesn't see
	enum { FieldCount = 8 };                      // CpFieldCount

	int     Scale(int dips) const;
	bool    HasAlpha() const;
	bool    HasFields() const;
	bool    HasSwatch() const;
	void    UpdateFont();
	void    Layout();
	RECT    PartRect(UINT part) const;            // empty if not shown
	UINT    NextPart(UINT part, bool forward) const;   // CPKHT_NOWHERE past the ends

	// changes
	void    SetState(const Hsv& hsv, BYTE alpha, int skipField = -1);   // repaints what changed, sets the fields
	bool    SameState(const Hsv& hsv, BYTE alpha) const;
	void    ApplyPoint(POINT pt);                 // a drag
	bool    HandleKey(WPARAM key, bool shift);
	void    EndDrag(bool cancel);
	void    Notify(UINT code, UINT part);

	// the panel: fields, spin buttons, swatch (ColorPickerFields.cpp)
	void    CreateFields();
	void    PositionFields();
	void    UpdateFields(int skipField);          // the fields' text from the color; skipField: -1 for none
	int     FieldOf(HWND hEdit) const;            // -1 if none
	bool    IsFieldTextValid(int field) const;
	ATL::CString FieldText(int field) const;      // the color's, as the field shows it
	void    FieldChanged(int field);              // typed in
	void    CommitField(int field);               // Enter, or the focus leaves it
	void    RevertField(int field);               // Escape: back to the color before the typing
	bool    IsFieldEdited(int field) const;       // typed in, or the text isn't the color's
	// CPKN_COLORCHANGING if the color changes; true if it changed (or typing before it did): the caller sends
	// CPKN_COLORCHANGED when the change is complete
	bool    StepField(int field, int delta);
	bool    SpinHitTest(POINT pt, int* pField, int* pDir) const;
	void    EndSpin();
	void    InvalidateField(int field);
	void    PaintPanel(HDC hdc, const RECT& rcPaint);

	// swatches and the eyedropper (ColorPickerSwatches.cpp)
	bool    HasGroups() const;
	int     GroupCount(int group) const;          // the cells there are (custom: all slots)
	COLORREF GroupColor(int group, int index) const;   // CLR_NONE for an empty custom slot
	RECT    SwatchCellRect(int group, int index) const;
	void    PickSwatch(UINT part, int index);
	void    SaveCustom(int index, COLORREF color); // CLR_NONE clears it
	void    CustomMenu(int index, POINT ptScreen);
	void    SetHot(UINT part, int index);
	void    UpdateToolTips();
	void    PaintSwatches(HDC hdc, const CPKCOLORS& c);
	void    PaintEyedropperButton(HDC hdc, const CPKCOLORS& c);
	void    SampleScreen(POINT ptScreen);         // the eyedropper's color, and the magnifier
	void    EndEyedropper(bool cancel);
	static LRESULT CALLBACK PickHookProc(int code, WPARAM wParam, LPARAM lParam);
	// the swatch window: the cells' bounds; keys move a focused cell (group, index)
	void    PositionSwatchWindow();
	void    InvalidateSwatches(const RECT* prc = nullptr);   // control coords; nullptr: all
	void    SwatchCounts(int* counts) const;      // [3]: the cells of each group that take the focus
	bool    IsSwatchCell(int group, int index) const;
	void    FocusSwatch(int group, int index, bool notify);
	void    ActivateSwatch(int group, int index, bool save);   // Enter: pick it, or save the color into a slot
	int     SwatchChild(int group, int index) const;   // the accessible child id (0 if none)
	bool    SwatchOfChild(long child, int* pGroup, int* pIndex) const;

	// accessibility (ColorPickerAccessible.cpp)
	void    NotifyAccessible(DWORD event, HWND hWnd, long child);   // only when a client has asked
	void    DisconnectAccessible();
	static LRESULT EditGetObject(CColorPickerCtrl* self, int field, HWND hWnd, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// painting the gradients
	void    PaintGradients(HDC hdc);
	bool    EnsureBuffer(int width, int height);

	std::unique_ptr<CpGradientWindow> m_Grad;
	std::unique_ptr<CpSwatchWindow> m_Swatches;
	int           m_SwGroup = 0;                  // the focused swatch
	int           m_SwIndex = 0;
	RECT          m_SwRect{};                     // the swatch window's, control coords
	int           m_GradWheel = 0;                // wheel remainder on the gradients
	CpAccessible* m_GradAcc = nullptr;            // made on the first WM_GETOBJECT; cut off in OnDestroy
	CpAccessible* m_SwAcc = nullptr;
	CpAccessible* m_EditAcc[8]{};

	Hsv           m_Hsv{ 0, 0, 1 };
	BYTE          m_Alpha = 255;
	bool          m_HasOriginal = false;
	COLORREF      m_Original = 0;
	BYTE          m_OriginalAlpha = 255;
	UINT          m_Focus = CPKHT_SV;
	UINT          m_Drag = CPKHT_NOWHERE;
	Hsv           m_DragStartHsv{};
	BYTE          m_DragStartAlpha = 255;

	UINT          m_Dpi = 96;
	int           m_Pad = 8, m_BarWidth = 20, m_Gap = 12;
	RECT          m_Sv{}, m_Hue{}, m_AlphaBar{};   // client coords of both windows: the child is at (0, 0)

	// the panel
	RECT          m_Swatch{};
	RECT          m_Labels[FieldCount]{};
	RECT          m_Frames[FieldCount]{};
	HWND          m_Edits[FieldCount]{};
	bool          m_Bad[FieldCount]{};            // the text isn't a value: a red frame
	int           m_Typing = -1;                  // the field typed in since the last CPKN_COLORCHANGED
	Hsv           m_TypingStartHsv{};             // the color before, for Escape
	BYTE          m_TypingStartAlpha = 255;
	bool          m_SettingText = false;          // the control sets a field's text: not the user's typing
	int           m_SpinField = -1;               // a spin button held down
	int           m_SpinDir = 0;
	bool          m_SpinPressed = false;          // the mouse is on it
	bool          m_SpinChanged = false;
	int           m_SpinWidth = 14;
	int           m_TextHeight = 16;
	int           m_WheelRemainder = 0;
	HFONT         m_hFont = nullptr;              // in effect (owned if m_OwnFont)
	bool          m_OwnFont = true;
	WTL::CBrush   m_FieldBrush;                   // WM_CTLCOLOREDIT
	COLORREF      m_FieldBrushColor = CLR_INVALID;

	// swatches
	enum { GroupCountMax = 3 };                   // CpGroupCount
	RECT          m_Eyedropper{};
	RECT          m_Groups[GroupCountMax]{};
	int           m_Cell = 0;
	int           m_CellGap = 3;
	std::vector<COLORREF> m_Palette;
	std::vector<COLORREF> m_Custom;               // CPK_CUSTOMCOLORS slots, CLR_NONE when empty
	std::vector<COLORREF> m_Recent;
	UINT          m_HotPart = CPKHT_NOWHERE;      // the swatch (or button) under the mouse
	int           m_HotIndex = -1;
	bool          m_TrackingLeave = false;
	HWND          m_hToolTip = nullptr;
	std::vector<UINT_PTR> m_ToolIds;              // the tooltip's tools, to remove on a new layout
	ATL::CString  m_TipText;

	// the eyedropper
	bool          m_Picking = false;
	Hsv           m_PickStartHsv{};
	BYTE          m_PickStartAlpha = 255;
	POINT         m_PickPoint{};                  // the mouse, screen coords (the hook's)
	bool          m_PickPressed = false;          // the button that started it is still down
	bool          m_PickMovePosted = false;
	UINT          m_PickSwallowUp = 0;            // the button-up to keep from other windows
	std::unique_ptr<CpMagnifier> m_Magnifier;

	// the gradients, drawn when they change (32-bit, top-down)
	std::vector<uint32_t> m_SvBits, m_HueBits, m_AlphaBits;
	bool          m_SvDirty = true, m_HueDirty = true, m_AlphaDirty = true;

	// the gradients' back buffer: a DIB section the size of the child
	WTL::CBitmap  m_Buffer;
	uint32_t*     m_BufferBits = nullptr;
	SIZE          m_BufferSize{};

	bool          m_SystemColors = true;
	CPKCOLORS     m_Colors{};
};

// ---- Dialog --------------------------------------------------------------------------------
//
// A modal dialog with a color picker, OK and Cancel; a replacement for ChooseColor that follows WTLHelper's dark
// mode. Resizable, at the owner's DPI, centered over it. The starting color shows as the original.

typedef void (CALLBACK* CPKPREVIEWPROC)(COLORREF color, BYTE alpha, LPARAM lParam);

struct CPKDIALOG {
	HWND            hwndOwner = nullptr;
	LPCWSTR         pszTitle = nullptr;        // nullptr: "Color"
	COLORREF        color = 0;                 // in: the starting color; out: the color picked
	BYTE            alpha = 255;               // in and out
	DWORD           pickerStyle = 0;           // CPKS_*: the parts shown
	COLORREF*       customColors = nullptr;    // CPK_CUSTOMCOLORS (CLR_NONE: empty); in and out, also on Cancel
	COLORREF*       recentColors = nullptr;    // room for CPK_MAXRECENT; in and out (OK adds the color picked)
	int             recentCount = 0;           // in and out
	const COLORREF* palette = nullptr;         // nullptr: the default one
	int             paletteCount = 0;
	const CPKCOLORS* colors = nullptr;         // nullptr: system colors
	// called as the color changes, for a live preview; on Cancel once more with the starting color
	CPKPREVIEWPROC  pfnPreview = nullptr;
	LPARAM          lParam = 0;
};

BOOL ColorPickerDialog(CPKDIALOG* pDialog);    // TRUE on OK

// ---- Color button ----------------------------------------------------------------------------
//
// A button showing a color (the left half opaque, the right half with its alpha) and a drop-down arrow. A click,
// Space, F4 or Alt+Down opens a color picker below it; the color changes as the user picks (CPKN_COLORCHANGING
// from the button), Enter or a click elsewhere keeps it (CPKN_COLORCHANGED, and it's added to the recent colors),
// Escape goes back. CPKN_DROPDOWN and CPKN_CLOSEUP tell when the picker opens and closes.

constexpr wchar_t WC_COLORBUTTON[] = L"WC_ColorButton";

class CpColorPopup;

class CColorButtonCtrl : public ATL::CWindowImpl<CColorButtonCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_ColorButton", CS_DBLCLKS, -1)

	CColorButtonCtrl();
	~CColorButtonCtrl();
	friend class CpColorPopup;
	friend class CpButtonAccessible;

	enum : UINT {
		WM_CPB_CLOSE = WM_USER + 210,   // posted to itself; wParam: TRUE to cancel
		WM_CPB_OPEN,                    // posted to itself (accessibility's default action)
	};

	BEGIN_MSG_MAP(CColorButtonCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_ENABLE, OnRepaint)
		MESSAGE_HANDLER(WM_UPDATEUISTATE, OnUpdateUIState)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_CPB_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_CPB_OPEN, OnOpen)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnRepaint)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnRepaint)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnRepaint)
		MESSAGE_HANDLER(CColorPickerCtrl::ThemeChangedMessage, OnRepaint)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnRepaint)
	END_MSG_MAP()

	void      SetColor(COLORREF color);
	COLORREF  GetColor() const;
	void      SetAlpha(BYTE alpha);
	BYTE      GetAlpha() const;
	void      SetPickerStyle(DWORD style);        // CPKS_*: the parts the picker shows (default: all)
	DWORD     GetPickerStyle() const;
	void      SetPalette(const COLORREF* colors, int count);   // as CColorPickerCtrl's
	void      SetCustomColors(const COLORREF* colors, int count);
	int       GetCustomColors(COLORREF* colors, int max) const;
	void      SetRecentColors(const COLORREF* colors, int count);
	int       GetRecentColors(COLORREF* colors, int max) const;
	void      SetColors(const CPKCOLORS* colors); // the button's and the picker's; nullptr: system colors
	CPKCOLORS GetColors() const;

	void      DropDown();
	void      CloseUp(bool cancel);
	bool      IsDroppedDown() const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRepaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnUpdateUIState(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOpen(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);

	void    Paint(HDC hdc);
	void    NotifyAccessible(DWORD event);        // only when a client has asked (ColorPickerAccessible.cpp)
	void    DisconnectAccessible();
	void    Notify(UINT code, UINT part);
	void    PickerChanged(const NMCOLORPICKER* nm);   // from the popup's picker

	COLORREF      m_Color = RGB(0, 0, 0);
	BYTE          m_Alpha = 255;
	DWORD         m_PickerStyle = 0;
	std::vector<COLORREF> m_Palette;              // empty: the picker's default
	std::vector<COLORREF> m_Custom;
	std::vector<COLORREF> m_Recent;
	bool          m_SystemColors = true;
	CPKCOLORS     m_Colors{};
	bool          m_Hot = false;
	bool          m_TrackingLeave = false;
	bool          m_IgnoreClick = false;          // the click that closed the picker: not one that opens it
	std::unique_ptr<CpColorPopup> m_Popup;
	bool          m_Dropped = false;
	COLORREF      m_StartColor = 0;               // when it opened, for Escape
	BYTE          m_StartAlpha = 255;
	bool          m_Changed = false;              // since it opened
	CpAccessible* m_Acc = nullptr;                // made on the first WM_GETOBJECT; cut off in OnDestroy
};
