#include "ColorPicker.h"
#include "ColorPickerDraw.h"
#include "ColorPickerModel.h"

using namespace CpDraw;

// ---- The drop-down ----------------------------------------------------------------------------
//
// A popup holding a picker under the button. It takes the activation (the fields need the keyboard) while
// keeping its owner's frame looking active, as a menu does; losing the activation closes it, keeping the color.
// The button runs a message loop while it's open, so Tab, Enter and Escape work through IsDialogMessage.

class CpColorPopup : public ATL::CWindowImpl<CpColorPopup, ATL::CWindow, ATL::CWinTraits<WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW>> {
public:
	DECLARE_WND_CLASS_EX(L"CpColorPopup", CS_DROPSHADOW, -1)

	explicit CpColorPopup(CColorButtonCtrl* owner) : m_Owner(owner) {}

	CColorPickerCtrl m_Picker;

	BEGIN_MSG_MAP(CpColorPopup)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
	END_MSG_MAP()

private:
	LRESULT OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
		if (m_Picker.IsWindow())
			m_Picker.SetWindowPos(nullptr, 1, 1, std::max(0, LOWORD(lParam) - 2), std::max(0, HIWORD(lParam) - 2), SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}

	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		// the border; the picker covers the rest
		WTL::CPaintDC dc(m_hWnd);
		RECT rc;
		GetClientRect(&rc);
		Frame(dc, rc, m_Owner->GetColors().crBorder);
		return 0;
	}

	LRESULT OnNotify(UINT, WPARAM, LPARAM lParam, BOOL&) {
		auto nm = (NMCOLORPICKER*)lParam;
		if (nm->hdr.hwndFrom == m_Picker.m_hWnd)
			m_Owner->PickerChanged(nm);
		return 0;
	}

	LRESULT OnCommand(UINT, WPARAM wParam, LPARAM, BOOL&) {
		// Enter and Escape, from IsDialogMessage
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			m_Owner->PostMessage(CColorButtonCtrl::WM_CPB_CLOSE, LOWORD(wParam) == IDCANCEL);
		return 0;
	}

	LRESULT OnActivate(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
		HWND owner = GetWindow(GW_OWNER);
		if (LOWORD(wParam) != WA_INACTIVE) {
			::SendMessage(owner, WM_NCACTIVATE, TRUE, 0);   // the owner's frame stays active-looking
			return 0;
		}
		if ((HWND)lParam != owner)
			::SendMessage(owner, WM_NCACTIVATE, FALSE, 0);
		// a click on the button closes it: that click mustn't open it again
		POINT pt;
		::GetCursorPos(&pt);
		if (::WindowFromPoint(pt) == m_Owner->m_hWnd && (::GetKeyState(VK_LBUTTON) & 0x8000))
			m_Owner->m_IgnoreClick = true;
		m_Owner->PostMessage(CColorButtonCtrl::WM_CPB_CLOSE, FALSE);
		return 0;
	}

	CColorButtonCtrl* m_Owner;
};

// ---- The button ----------------------------------------------------------------------------------

CColorButtonCtrl::CColorButtonCtrl() : m_Custom(CPK_CUSTOMCOLORS, CLR_NONE) {
}

CColorButtonCtrl::~CColorButtonCtrl() = default;

LRESULT CColorButtonCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	return 0;
}

LRESULT CColorButtonCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	CloseUp(true);
	bHandled = FALSE;
	return 0;
}

void CColorButtonCtrl::SetColor(COLORREF color) {
	m_Color = color & 0xFFFFFF;
	if (IsWindow())
		Invalidate();
}

COLORREF CColorButtonCtrl::GetColor() const {
	return m_Color;
}

void CColorButtonCtrl::SetAlpha(BYTE alpha) {
	m_Alpha = alpha;
	if (IsWindow())
		Invalidate();
}

BYTE CColorButtonCtrl::GetAlpha() const {
	return m_Alpha;
}

void CColorButtonCtrl::SetPickerStyle(DWORD style) {
	m_PickerStyle = style;
}

DWORD CColorButtonCtrl::GetPickerStyle() const {
	return m_PickerStyle;
}

void CColorButtonCtrl::SetPalette(const COLORREF* colors, int count) {
	if (colors)
		m_Palette.assign(colors, colors + std::clamp(count, 0, CPK_MAXPALETTE));
	else
		m_Palette.clear();
}

void CColorButtonCtrl::SetCustomColors(const COLORREF* colors, int count) {
	for (int i = 0; i < CPK_CUSTOMCOLORS; i++)
		m_Custom[i] = colors && i < count && colors[i] != CLR_NONE ? colors[i] & 0xFFFFFF : CLR_NONE;
}

int CColorButtonCtrl::GetCustomColors(COLORREF* colors, int max) const {
	if (colors)
		std::copy_n(m_Custom.begin(), std::min(max, CPK_CUSTOMCOLORS), colors);
	return CPK_CUSTOMCOLORS;
}

void CColorButtonCtrl::SetRecentColors(const COLORREF* colors, int count) {
	m_Recent.clear();
	for (int i = std::min(count, CPK_MAXRECENT) - 1; colors && i >= 0; i--)
		CpAddRecent(m_Recent, colors[i], CPK_MAXRECENT);
}

int CColorButtonCtrl::GetRecentColors(COLORREF* colors, int max) const {
	if (colors)
		std::copy_n(m_Recent.begin(), std::min(max, (int)m_Recent.size()), colors);
	return (int)m_Recent.size();
}

void CColorButtonCtrl::SetColors(const CPKCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

CPKCOLORS CColorButtonCtrl::GetColors() const {
	return m_SystemColors ? SystemColors() : m_Colors;
}

void CColorButtonCtrl::Notify(UINT code, UINT part) {
	NMCOLORPICKER nm{};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom = GetDlgCtrlID();
	nm.hdr.code = code;
	nm.color = m_Color;
	nm.alpha = m_Alpha;
	auto hsv = CpRgbToHsv(m_Color);
	nm.hsv = { hsv.h, hsv.s, hsv.v };
	nm.part = part;
	::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

// ---- Opening and closing -------------------------------------------------------------------

bool CColorButtonCtrl::IsDroppedDown() const {
	return m_Dropped;
}

void CColorButtonCtrl::DropDown() {
	if (m_Dropped || !IsWindow() || !IsWindowEnabled())
		return;
	Notify(CPKN_DROPDOWN, CPKHT_NOWHERE);

	m_Popup = std::make_unique<CpColorPopup>(this);
	if (!m_Popup->Create(::GetAncestor(m_hWnd, GA_ROOT), rcDefault)) {
		m_Popup.reset();
		return;
	}
	auto& picker = m_Popup->m_Picker;
	picker.Create(m_Popup->m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | m_PickerStyle, 0, 1);
	if (!m_SystemColors)
		picker.SetColors(&m_Colors);
	if (!m_Palette.empty())
		picker.SetPalette(m_Palette.data(), (int)m_Palette.size());
	picker.SetCustomColors(m_Custom.data(), (int)m_Custom.size());
	picker.SetRecentColors(m_Recent.data(), (int)m_Recent.size());
	picker.SetColor(m_Color);
	picker.SetAlpha(m_Alpha);
	picker.SetOriginalColor(m_Color, m_Alpha);

	// under the button (or above it), in its monitor's work area
	auto ideal = picker.GetIdealSize();
	SIZE size{ ideal.cx + 2, ideal.cy + 2 };
	RECT anchor;
	GetWindowRect(&anchor);
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfoW(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
	auto at = CpPlaceDropDown(anchor, size, mi.rcWork);

	m_StartColor = m_Color;
	m_StartAlpha = m_Alpha;
	m_Changed = false;
	m_Dropped = true;
	Invalidate();
	m_Popup->SetWindowPos(HWND_TOP, at.x, at.y, size.cx, size.cy, SWP_SHOWWINDOW);
	picker.SetFocus();

	// until it closes; a WM_QUIT goes back to the app's loop
	MSG msg;
	while (m_Dropped) {
		if (!::GetMessageW(&msg, nullptr, 0, 0)) {
			CloseUp(false);
			::PostQuitMessage((int)msg.wParam);
			break;
		}
		if (m_Popup && m_Popup->IsWindow() && ::IsDialogMessageW(m_Popup->m_hWnd, &msg))
			continue;
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}
}

void CColorButtonCtrl::CloseUp(bool cancel) {
	if (!m_Dropped)
		return;
	m_Dropped = false;
	if (m_Popup && m_Popup->m_Picker.IsWindow()) {
		m_Popup->m_Picker.CancelEyedropper();
		m_Popup->m_Picker.GetCustomColors(m_Custom.data(), CPK_CUSTOMCOLORS);
	}
	if (m_Popup && m_Popup->IsWindow())
		m_Popup->DestroyWindow();
	m_Popup.reset();
	if (!IsWindow())
		return;
	Invalidate();
	Notify(CPKN_CLOSEUP, CPKHT_NOWHERE);

	bool differs = m_Color != m_StartColor || m_Alpha != m_StartAlpha;
	if (cancel) {
		if (differs) {
			m_Color = m_StartColor;
			m_Alpha = m_StartAlpha;
			Invalidate();
			Notify(CPKN_COLORCHANGING, CPKHT_NOWHERE);
		}
	}
	else if (differs) {
		CpAddRecent(m_Recent, m_Color, CPK_MAXRECENT);
		Notify(CPKN_COLORCHANGED, CPKHT_NOWHERE);
	}
}

void CColorButtonCtrl::PickerChanged(const NMCOLORPICKER* nm) {
	if (nm->hdr.code == CPKN_CUSTOMCHANGED) {
		m_Popup->m_Picker.GetCustomColors(m_Custom.data(), CPK_CUSTOMCOLORS);
		Notify(CPKN_CUSTOMCHANGED, CPKHT_CUSTOM);
		return;
	}
	if (nm->hdr.code != CPKN_COLORCHANGING && nm->hdr.code != CPKN_COLORCHANGED)
		return;
	if (nm->color != m_Color || nm->alpha != m_Alpha) {
		m_Color = nm->color;
		m_Alpha = nm->alpha;
		m_Changed = true;
		Invalidate();
		UpdateWindow();
		Notify(CPKN_COLORCHANGING, nm->part);
	}
	// a swatch picked: done, as a drop-down palette does
	if (nm->hdr.code == CPKN_COLORCHANGED && nm->part >= CPKHT_PALETTE && nm->part <= CPKHT_RECENT)
		PostMessage(WM_CPB_CLOSE, FALSE);
}

LRESULT CColorButtonCtrl::OnClose(UINT, WPARAM wParam, LPARAM, BOOL&) {
	CloseUp(wParam != FALSE);
	return 0;
}

// ---- Input ---------------------------------------------------------------------------------

LRESULT CColorButtonCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return 0;   // Space and F4 come anyway; Enter and Escape are the dialog's
}

LRESULT CColorButtonCtrl::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool open = uMsg == WM_SYSKEYDOWN ? wParam == VK_DOWN || wParam == VK_UP : wParam == VK_SPACE || wParam == VK_F4;
	if (!open) {
		bHandled = FALSE;
		return 0;
	}
	DropDown();
	return 0;
}

LRESULT CColorButtonCtrl::OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_IgnoreClick) {
		m_IgnoreClick = false;   // it closed the picker
		return 0;
	}
	SetFocus();
	DropDown();
	return 0;
}

LRESULT CColorButtonCtrl::OnMouseMove(UINT, WPARAM, LPARAM, BOOL&) {
	m_IgnoreClick = false;
	if (!m_Hot) {
		m_Hot = true;
		Invalidate();
	}
	if (!m_TrackingLeave) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
	return 0;
}

LRESULT CColorButtonCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingLeave = false;
	m_Hot = false;
	m_IgnoreClick = false;
	Invalidate();
	return 0;
}

LRESULT CColorButtonCtrl::OnFocus(UINT, WPARAM, LPARAM, BOOL&) {
	Invalidate();
	return 0;
}

LRESULT CColorButtonCtrl::OnRepaint(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CColorButtonCtrl::OnUpdateUIState(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

// ---- Painting -----------------------------------------------------------------------------------

LRESULT CColorButtonCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CColorButtonCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		Paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	RECT rc;
	GetClientRect(&rc);
	WTL::CMemoryDC mdc(dc, rc);
	Paint(mdc);
	return 0;
}

void CColorButtonCtrl::Paint(HDC hdc) {
	auto c = GetColors();
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	auto scale = [=](int dips) { return ::MulDiv(dips, dpi, 96); };
	RECT rc;
	GetClientRect(&rc);
	bool enabled = IsWindowEnabled() != FALSE, focused = ::GetFocus() == m_hWnd;

	FillSolid(hdc, rc, c.crBack);
	RECT face = rc;
	FillSolid(hdc, face, m_Hot && enabled ? Blend(c.crFieldBack, c.crText, 8) : c.crFieldBack);
	COLORREF border = !enabled ? Blend(c.crBack, c.crBorder, 50) : (m_Dropped || focused) ? ::GetSysColor(COLOR_HIGHLIGHT) :
		m_Hot ? Blend(c.crBorder, c.crText, 40) : c.crBorder;
	Frame(hdc, face, border);

	// the color, and the arrow on the right
	int pad = scale(4), arrowWidth = scale(16);
	RECT swatch{ rc.left + pad, rc.top + pad, rc.right - arrowWidth, rc.bottom - pad };
	if (!IsEmpty(swatch)) {
		RECT inner = swatch;
		::InflateRect(&inner, -1, -1);
		DrawPixels(hdc, inner, [&](const CpPixels& px) {
			CpRenderSwatch(px, m_Color, m_Alpha, scale(5), c.crCheck1, c.crCheck2);
			if (!enabled) {
				auto back = CpPixel(c.crFieldBack);
				for (int i = 0; i < px.width * px.height; i++)
					px.bits[i] = CpBlend(px.bits[i], back, 0.6);
			}
		});
		Frame(hdc, swatch, enabled ? c.crBorder : Blend(c.crBack, c.crBorder, 50));
	}
	RECT arrow{ rc.right - arrowWidth, rc.top, rc.right - scale(1), rc.bottom };
	Arrow(hdc, arrow, false, std::max(5, scale(7)) | 1, enabled ? Blend(c.crFieldBack, c.crText, 80) : Blend(c.crFieldBack, c.crText, 35));

	bool cues = (SendMessage(WM_QUERYUISTATE) & UISF_HIDEFOCUS) == 0;
	if (focused && cues && !m_Dropped) {
		RECT fr = swatch;
		::InflateRect(&fr, scale(2), scale(2));
		::SetTextColor(hdc, c.crText);
		::SetBkColor(hdc, c.crFieldBack);
		::DrawFocusRect(hdc, &fr);
	}
}
