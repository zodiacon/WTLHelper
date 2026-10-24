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
#define CPKS_NOSWATCH  0x0004   // no new/original swatch

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

// ---- Notifications -------------------------------------------------------------
//
// While the user drags, types or presses keys, every change sends CPKN_COLORCHANGING; the end of the change
// (the mouse button released, a key, or a field that was typed in losing the focus or getting Enter) sends
// CPKN_COLORCHANGED. Escape during a drag goes back to the color it started with and sends CPKN_COLORCHANGING
// with that color, and no CPKN_COLORCHANGED. Setting the color with the API sends neither.

#define CPKN_FIRST          (0U - 2900U)   // outside the common controls ranges
#define CPKN_COLORCHANGING  (CPKN_FIRST - 0)   // NMCOLORPICKER
#define CPKN_COLORCHANGED   (CPKN_FIRST - 1)   // NMCOLORPICKER

struct NMCOLORPICKER {
	NMHDR    hdr;
	COLORREF color;
	BYTE     alpha;
	CPKHSV   hsv;
	UINT     part;             // CPKHT_*: what the user changed
};

// ---- Control class --------------------------------------------------------------------------

class CpGradientWindow;

class CColorPickerCtrl : public ATL::CWindowImpl<CColorPickerCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_ColorPicker", 0, -1)

	CColorPickerCtrl();
	~CColorPickerCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

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
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
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

	UINT      HitTest(POINT pt) const;            // client coords; CPKHT_*
	BOOL      GetPartRect(UINT part, RECT* prc) const;   // client coords; FALSE if not shown (or CPKHT_FIELD)
	void      SetFocusPart(UINT part);            // CPKHT_SV, CPKHT_HUE or CPKHT_ALPHA
	UINT      GetFocusPart() const;
	HWND      GetGradientWindow() const;          // the child that shows the area and the bars

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
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
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
	static LRESULT CALLBACK EditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

	// painting the gradients
	void    PaintGradients(HDC hdc);
	bool    EnsureBuffer(int width, int height);

	std::unique_ptr<CpGradientWindow> m_Grad;

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
