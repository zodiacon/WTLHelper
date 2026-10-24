#pragma once

// Color picker: a saturation/value area, a hue bar and an optional alpha bar. The color is kept as HSV, so the
// hue doesn't jump when the color goes gray or black. The API follows the common controls: C-style structures
// and WM_NOTIFY codes (CPKN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>

#include <cstdint>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_COLORPICKER[] = L"WC_ColorPicker";

// ---- Styles ------------------------------------------------------------------

#define CPKS_NOALPHA   0x0001   // no alpha bar (the alpha stays as set)

// ---- Colors -----------------------------------------------------------------------

struct CPKHSV {
	double h;      // 0..360
	double s;      // 0..1
	double v;      // 0..1
};

struct CPKCOLORS {
	COLORREF crBack;
	COLORREF crText;
	COLORREF crBorder;         // around the parts
	COLORREF crCheck1;         // the checkerboard under transparent colors
	COLORREF crCheck2;
};

// ---- Hit testing ----------------------------------------------------------------

#define CPKHT_NOWHERE  0
#define CPKHT_SV       1   // the saturation/value area
#define CPKHT_HUE      2
#define CPKHT_ALPHA    3

// ---- Notifications -------------------------------------------------------------
//
// While the user drags or presses keys, every change sends CPKN_COLORCHANGING; the end of the change (the
// mouse button released, or a key) sends CPKN_COLORCHANGED. Escape during a drag goes back to the color it
// started with and sends CPKN_COLORCHANGING with that color, and no CPKN_COLORCHANGED. Setting the color with
// the API sends neither.

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

class CColorPickerCtrl : public ATL::CWindowImpl<CColorPickerCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_ColorPicker", 0, -1)

	CColorPickerCtrl();
	~CColorPickerCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CColorPickerCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_UPDATEUISTATE, OnUpdateUIState)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Color ---------------------------------------------------------------------------

	void      SetColor(COLORREF color);           // keeps the hue (and saturation) the color leaves undefined
	COLORREF  GetColor() const;
	void      SetAlpha(BYTE alpha);
	BYTE      GetAlpha() const;
	void      SetHsv(const CPKHSV& hsv);          // h wrapped into 0..360, s and v clamped
	CPKHSV    GetHsv() const;

	// ---- Parts -----------------------------------------------------------------------------
	//
	// Keys go to the focused part, which a click or Tab picks (Tab leaves the control from its last part).
	// The area: arrows move the marker (Shift: ten times as far), Home/End to the left and right edges,
	// Page Up/Down to the top and bottom. The bars: Up/Down (and Left/Right) a step, Shift or Page Up/Down
	// further, Home/End to the ends.

	UINT      HitTest(POINT pt) const;            // client coords; CPKHT_*
	BOOL      GetPartRect(UINT part, RECT* prc) const;   // the gradient, client coords; FALSE if not shown
	void      SetFocusPart(UINT part);
	UINT      GetFocusPart() const;

	// ---- Appearance ------------------------------------------------------------------------

	void      SetColors(const CPKCOLORS* colors); // nullptr: system colors (follows WTLHelper dark mode)
	CPKCOLORS GetColors() const;                  // the colors in effect

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnUpdateUIState(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	struct Hsv { double h, s, v; };               // CpHsv, which the header doesn't see

	int     Scale(int dips) const;
	bool    HasAlpha() const;
	void    Layout();
	RECT    PartRect(UINT part) const;            // the gradient; empty if not shown
	UINT    NextPart(UINT part, bool forward) const;   // CPKHT_NOWHERE past the ends

	// changes
	void    SetState(const Hsv& hsv, BYTE alpha);  // repaints what changed
	void    ApplyPoint(POINT pt);                 // a drag
	bool    HandleKey(WPARAM key, bool shift);
	void    EndDrag(bool cancel);
	void    Notify(UINT code, UINT part);

	// painting
	void    Paint(HDC hdc);
	bool    EnsureBuffer(int width, int height);

	Hsv           m_Hsv{ 0, 0, 1 };
	BYTE          m_Alpha = 255;
	UINT          m_Focus = CPKHT_SV;
	UINT          m_Drag = CPKHT_NOWHERE;
	Hsv           m_DragStartHsv{};
	BYTE          m_DragStartAlpha = 255;

	UINT          m_Dpi = 96;
	int           m_Pad = 8, m_BarWidth = 20, m_Gap = 12;
	RECT          m_Sv{}, m_Hue{}, m_AlphaBar{};

	// the gradients, drawn when they change (32-bit, top-down)
	std::vector<uint32_t> m_SvBits, m_HueBits, m_AlphaBits;
	bool          m_SvDirty = true, m_HueDirty = true, m_AlphaDirty = true;

	// the back buffer: a DIB section the size of the client area
	WTL::CBitmap  m_Buffer;
	uint32_t*     m_BufferBits = nullptr;
	SIZE          m_BufferSize{};

	bool          m_SystemColors = true;
	CPKCOLORS     m_Colors{};
};
