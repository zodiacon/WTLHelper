#include "ColorPicker.h"
#include "ColorPickerModel.h"

#include <algorithm>
#include <cmath>

// ---- Helpers -----------------------------------------------------------------------

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

static bool IsDarkColor(COLORREF c) {
	return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 < 128 * 1000;
}

static bool IsEmpty(const RECT& rc) {
	return rc.right <= rc.left || rc.bottom <= rc.top;
}

// ---- Construction ------------------------------------------------------------------

CColorPickerCtrl::CColorPickerCtrl() = default;
CColorPickerCtrl::~CColorPickerCtrl() = default;

LRESULT CColorPickerCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	Layout();
	return 0;
}

LRESULT CColorPickerCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through the back buffer
}

LRESULT CColorPickerCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	Invalidate();
	return 0;
}

LRESULT CColorPickerCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == GWL_STYLE) {
		Layout();
		if (m_Focus == CPKHT_ALPHA && !HasAlpha())
			m_Focus = CPKHT_HUE;
		Invalidate();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	Layout();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_AlphaDirty = true;   // the checkerboard
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnUpdateUIState(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();   // focus cues shown or hidden
	bHandled = FALSE;
	return 0;
}

// ---- Layout ----------------------------------------------------------------------------

int CColorPickerCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

bool CColorPickerCtrl::HasAlpha() const {
	return (GetStyle() & CPKS_NOALPHA) == 0;
}

void CColorPickerCtrl::Layout() {
	if (!IsWindow())
		return;
	m_Pad = Scale(8);
	m_BarWidth = Scale(20);
	m_Gap = Scale(12);
	RECT rc;
	GetClientRect(&rc);
	auto l = CpComputeLayout(rc.right, rc.bottom, { m_Pad, m_BarWidth, m_Gap }, HasAlpha());
	m_Sv = l.sv;
	m_Hue = l.hue;
	m_AlphaBar = l.alpha;
	m_SvDirty = m_HueDirty = m_AlphaDirty = true;
}

RECT CColorPickerCtrl::PartRect(UINT part) const {
	switch (part) {
		case CPKHT_SV: return m_Sv;
		case CPKHT_HUE: return m_Hue;
		case CPKHT_ALPHA: return HasAlpha() ? m_AlphaBar : RECT{};
	}
	return {};
}

BOOL CColorPickerCtrl::GetPartRect(UINT part, RECT* prc) const {
	if (!prc)
		return FALSE;
	*prc = PartRect(part);
	return !IsEmpty(*prc);
}

UINT CColorPickerCtrl::HitTest(POINT pt) const {
	// a little around each part grabs it too: the markers stick out
	int slack = Scale(4);
	for (UINT part : { CPKHT_SV, CPKHT_HUE, CPKHT_ALPHA }) {
		auto rc = PartRect(part);
		if (IsEmpty(rc))
			continue;
		::InflateRect(&rc, slack, slack);
		if (::PtInRect(&rc, pt))
			return part;
	}
	return CPKHT_NOWHERE;
}

UINT CColorPickerCtrl::NextPart(UINT part, bool forward) const {
	UINT last = HasAlpha() ? CPKHT_ALPHA : CPKHT_HUE;
	if (forward)
		return part < last ? part + 1 : CPKHT_NOWHERE;
	return part > CPKHT_SV ? part - 1 : CPKHT_NOWHERE;
}

void CColorPickerCtrl::SetFocusPart(UINT part) {
	if (IsEmpty(PartRect(part)) || part == m_Focus)
		return;
	m_Focus = part;
	Invalidate();
}

UINT CColorPickerCtrl::GetFocusPart() const {
	return m_Focus;
}

// ---- Color ---------------------------------------------------------------------------

void CColorPickerCtrl::SetState(const Hsv& hsv, BYTE alpha) {
	auto c = CpNormalize({ hsv.h, hsv.s, hsv.v });
	Hsv n{ c.h, c.s, c.v };
	if (n.h != m_Hsv.h)
		m_SvDirty = true;
	if (CpHsvToRgb(c) != GetColor())
		m_AlphaDirty = true;
	if (n.h == m_Hsv.h && n.s == m_Hsv.s && n.v == m_Hsv.v && alpha == m_Alpha)
		return;
	m_Hsv = n;
	m_Alpha = alpha;
	if (IsWindow())
		Invalidate();
}

void CColorPickerCtrl::SetColor(COLORREF color) {
	auto c = CpUpdateHsv({ m_Hsv.h, m_Hsv.s, m_Hsv.v }, color & 0xFFFFFF);
	SetState({ c.h, c.s, c.v }, m_Alpha);
}

COLORREF CColorPickerCtrl::GetColor() const {
	return CpHsvToRgb({ m_Hsv.h, m_Hsv.s, m_Hsv.v });
}

void CColorPickerCtrl::SetAlpha(BYTE alpha) {
	SetState(m_Hsv, alpha);
}

BYTE CColorPickerCtrl::GetAlpha() const {
	return m_Alpha;
}

void CColorPickerCtrl::SetHsv(const CPKHSV& hsv) {
	SetState({ hsv.h, hsv.s, hsv.v }, m_Alpha);
}

CPKHSV CColorPickerCtrl::GetHsv() const {
	return { m_Hsv.h, m_Hsv.s, m_Hsv.v };
}

void CColorPickerCtrl::Notify(UINT code, UINT part) {
	NMCOLORPICKER nm{};
	nm.hdr.hwndFrom = m_hWnd;
	nm.hdr.idFrom = GetDlgCtrlID();
	nm.hdr.code = code;
	nm.color = GetColor();
	nm.alpha = m_Alpha;
	nm.hsv = GetHsv();
	nm.part = part;
	::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

// ---- Mouse -------------------------------------------------------------------------------

void CColorPickerCtrl::ApplyPoint(POINT pt) {
	Hsv hsv = m_Hsv;
	BYTE alpha = m_Alpha;
	switch (m_Drag) {
		case CPKHT_SV:
			CpSvFromPoint(m_Sv, pt, &hsv.s, &hsv.v);
			break;
		case CPKHT_HUE:
			hsv.h = CpHueFromBar(CpBarFromY(m_Hue, pt.y));
			break;
		case CPKHT_ALPHA:
			alpha = CpAlphaFromBar(CpBarFromY(m_AlphaBar, pt.y));
			break;
		default:
			return;
	}
	auto before = m_Hsv;
	auto beforeAlpha = m_Alpha;
	SetState(hsv, alpha);
	if (before.h != m_Hsv.h || before.s != m_Hsv.s || before.v != m_Hsv.v || beforeAlpha != m_Alpha) {
		UpdateWindow();   // the marker follows the mouse even when the parent is slow to repaint
		Notify(CPKN_COLORCHANGING, m_Drag);
	}
}

LRESULT CColorPickerCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (::GetFocus() != m_hWnd)
		SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	UINT part = HitTest(pt);
	if (part == CPKHT_NOWHERE || m_Drag != CPKHT_NOWHERE)
		return 0;
	SetFocusPart(part);
	m_DragStartHsv = m_Hsv;
	m_DragStartAlpha = m_Alpha;
	m_Drag = part;
	SetCapture();
	ApplyPoint(pt);
	return 0;
}

LRESULT CColorPickerCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != CPKHT_NOWHERE)
		ApplyPoint({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	return 0;
}

LRESULT CColorPickerCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	EndDrag(false);
	return 0;
}

LRESULT CColorPickerCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	EndDrag(false);   // taken away (another window, a menu): what the drag did stays
	return 0;
}

void CColorPickerCtrl::EndDrag(bool cancel) {
	UINT part = m_Drag;
	if (part == CPKHT_NOWHERE)
		return;
	m_Drag = CPKHT_NOWHERE;   // first: releasing the capture comes back through OnCaptureChanged
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	bool changed = m_Hsv.h != m_DragStartHsv.h || m_Hsv.s != m_DragStartHsv.s || m_Hsv.v != m_DragStartHsv.v ||
		m_Alpha != m_DragStartAlpha;
	if (!changed)
		return;
	if (cancel) {
		SetState(m_DragStartHsv, m_DragStartAlpha);
		Notify(CPKN_COLORCHANGING, part);
	}
	else {
		Notify(CPKN_COLORCHANGED, part);
	}
}

// ---- Keyboard --------------------------------------------------------------------------

LRESULT CColorPickerCtrl::OnFocus(UINT uMsg, WPARAM, LPARAM, BOOL&) {
	// tabbing in starts at the first part, Shift+Tab at the last
	if (uMsg == WM_SETFOCUS && ::GetKeyState(VK_TAB) < 0)
		m_Focus = ::GetKeyState(VK_SHIFT) < 0 ? (HasAlpha() ? CPKHT_ALPHA : CPKHT_HUE) : CPKHT_SV;
	Invalidate();
	return 0;
}

LRESULT CColorPickerCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != CPKHT_NOWHERE)
		return DLGC_WANTALLKEYS;   // Escape cancels the drag
	LRESULT code = DLGC_WANTARROWS;
	// Tab moves between the parts, and leaves the control from the first or last one
	auto msg = (const MSG*)lParam;
	if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_TAB && ::GetKeyState(VK_CONTROL) >= 0 &&
		NextPart(m_Focus, ::GetKeyState(VK_SHIFT) >= 0) != CPKHT_NOWHERE)
		code |= DLGC_WANTTAB;
	return code;
}

LRESULT CColorPickerCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	if (wParam == VK_ESCAPE && m_Drag != CPKHT_NOWHERE) {
		EndDrag(true);
		return 0;
	}
	if (m_Drag != CPKHT_NOWHERE)
		return 0;
	if (wParam == VK_TAB) {
		if (::GetKeyState(VK_CONTROL) < 0)
			return 0;
		UINT next = NextPart(m_Focus, !shift);
		if (next != CPKHT_NOWHERE)
			SetFocusPart(next);
		return 0;
	}
	if (!HandleKey(wParam, shift))
		bHandled = FALSE;
	return 0;
}

bool CColorPickerCtrl::HandleKey(WPARAM key, bool shift) {
	Hsv hsv = m_Hsv;
	int alpha = m_Alpha;
	switch (m_Focus) {
		case CPKHT_SV:
		{
			double step = shift ? 0.1 : 0.01;
			switch (key) {
				case VK_LEFT:  hsv.s -= step; break;
				case VK_RIGHT: hsv.s += step; break;
				case VK_UP:    hsv.v += step; break;
				case VK_DOWN:  hsv.v -= step; break;
				case VK_HOME:  hsv.s = 0; break;
				case VK_END:   hsv.s = 1; break;
				case VK_PRIOR: hsv.v = 1; break;
				case VK_NEXT:  hsv.v = 0; break;
				default: return false;
			}
			// steps land on whole percents, so a value reached by the mouse lines up again
			if (key == VK_LEFT || key == VK_RIGHT)
				hsv.s = std::round(hsv.s * 100) / 100;
			if (key == VK_UP || key == VK_DOWN)
				hsv.v = std::round(hsv.v * 100) / 100;
			hsv.s = std::clamp(hsv.s, 0.0, 1.0);
			hsv.v = std::clamp(hsv.v, 0.0, 1.0);
			break;
		}
		case CPKHT_HUE:
		{
			// the marker moves with the arrow: up is toward 0; no wrapping, so the marker doesn't jump
			double step = shift ? 10 : 1;
			switch (key) {
				case VK_UP: case VK_LEFT:    hsv.h = std::round(hsv.h - step); break;
				case VK_DOWN: case VK_RIGHT: hsv.h = std::round(hsv.h + step); break;
				case VK_PRIOR: hsv.h = std::round(hsv.h - 30); break;
				case VK_NEXT:  hsv.h = std::round(hsv.h + 30); break;
				case VK_HOME:  hsv.h = 0; break;
				case VK_END:   hsv.h = 360; break;
				default: return false;
			}
			hsv.h = std::clamp(hsv.h, 0.0, 360.0);
			break;
		}
		case CPKHT_ALPHA:
		{
			// opaque at the top: up is more opaque
			int step = shift ? 16 : 1;
			switch (key) {
				case VK_UP: case VK_RIGHT:  alpha += step; break;
				case VK_DOWN: case VK_LEFT: alpha -= step; break;
				case VK_PRIOR: alpha += 32; break;
				case VK_NEXT:  alpha -= 32; break;
				case VK_HOME:  alpha = 255; break;
				case VK_END:   alpha = 0; break;
				default: return false;
			}
			alpha = std::clamp(alpha, 0, 255);
			break;
		}
		default:
			return false;
	}
	auto before = m_Hsv;
	auto beforeAlpha = m_Alpha;
	SetState(hsv, (BYTE)alpha);
	if (before.h != m_Hsv.h || before.s != m_Hsv.s || before.v != m_Hsv.v || beforeAlpha != m_Alpha) {
		Notify(CPKN_COLORCHANGING, m_Focus);
		Notify(CPKN_COLORCHANGED, m_Focus);
	}
	return true;
}

// ---- Appearance ------------------------------------------------------------------------

void CColorPickerCtrl::SetColors(const CPKCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	m_AlphaDirty = true;
	if (IsWindow())
		Invalidate();
}

CPKCOLORS CColorPickerCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	CPKCOLORS c;
	c.crBack = ::GetSysColor(COLOR_3DFACE);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);   // WTLHelper maps it, not COLOR_BTNTEXT
	bool dark = IsDarkColor(c.crBack);
	c.crBorder = Blend(c.crBack, dark ? RGB(255, 255, 255) : RGB(0, 0, 0), dark ? 30 : 35);
	c.crCheck1 = dark ? RGB(0x5A, 0x5A, 0x5A) : RGB(0xFF, 0xFF, 0xFF);
	c.crCheck2 = dark ? RGB(0x3A, 0x3A, 0x3A) : RGB(0xCC, 0xCC, 0xCC);
	return c;
}

// ---- Painting ---------------------------------------------------------------------------

bool CColorPickerCtrl::EnsureBuffer(int width, int height) {
	if (!m_Buffer.IsNull() && m_BufferSize.cx == width && m_BufferSize.cy == height)
		return true;
	if (!m_Buffer.IsNull())
		m_Buffer.DeleteObject();
	m_BufferBits = nullptr;
	m_BufferSize = {};
	if (width <= 0 || height <= 0)
		return false;
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;   // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	m_Buffer.CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (m_Buffer.IsNull())
		return false;
	m_BufferBits = (uint32_t*)bits;
	m_BufferSize = { width, height };
	return true;
}

LRESULT CColorPickerCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		Paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	Paint(dc);
	return 0;
}

static void Render(std::vector<uint32_t>& bits, const RECT& rc, bool& dirty, auto&& draw) {
	CpPixels px{ nullptr, rc.right - rc.left, rc.bottom - rc.top };
	if (px.width <= 0 || px.height <= 0)
		return;
	if (dirty || bits.size() != (size_t)px.width * px.height) {
		bits.resize((size_t)px.width * px.height);
		px.bits = bits.data();
		draw(px);
		dirty = false;
	}
}

void CColorPickerCtrl::Paint(HDC hdc) {
	RECT client;
	GetClientRect(&client);
	if (!EnsureBuffer(client.right, client.bottom))
		return;
	::GdiFlush();   // GDI's drawing on the buffer last time is done before writing its pixels
	CpPixels px{ m_BufferBits, m_BufferSize.cx, m_BufferSize.cy };
	auto c = GetColors();
	CpFillRect(px, client, c.crBack);
	int scale100 = ::MulDiv(100, m_Dpi, 96);
	auto s = [&](double dips) { return dips * scale100 / 100; };

	// the gradients, each in a frame
	auto rgb = GetColor();
	Render(m_SvBits, m_Sv, m_SvDirty, [&](const CpPixels& p) { CpRenderSv(p, m_Hsv.h); });
	Render(m_HueBits, m_Hue, m_HueDirty, [&](const CpPixels& p) { CpRenderHue(p); });
	if (HasAlpha())
		Render(m_AlphaBits, m_AlphaBar, m_AlphaDirty, [&](const CpPixels& p) {
			CpRenderAlpha(p, rgb, Scale(5), c.crCheck1, c.crCheck2);
		});
	auto blit = [&](const std::vector<uint32_t>& bits, const RECT& rc) {
		if (IsEmpty(rc) || bits.size() != (size_t)(rc.right - rc.left) * (rc.bottom - rc.top))
			return;
		RECT frame = rc;
		::InflateRect(&frame, 1, 1);
		CpFrameRect(px, frame, c.crBorder);
		CpCopy(px, rc.left, rc.top, { const_cast<uint32_t*>(bits.data()), rc.right - rc.left, rc.bottom - rc.top });
	};
	blit(m_SvBits, m_Sv);
	blit(m_HueBits, m_Hue);
	if (HasAlpha())
		blit(m_AlphaBits, m_AlphaBar);

	// the area's marker: a white ring between two thin dark ones, seen on any color
	if (!IsEmpty(m_Sv)) {
		auto pt = CpPointFromSv(m_Sv, m_Hsv.s, m_Hsv.v);
		double cx = pt.x + 0.5, cy = pt.y + 0.5, r = s(6);
		CpDrawRing(px, cx, cy, r, s(2), RGB(255, 255, 255));
		CpDrawRing(px, cx, cy, r + s(1.5), s(1), RGB(0, 0, 0));
		CpDrawRing(px, cx, cy, r - s(1.5), s(1), RGB(0, 0, 0));
	}

	// the bars' markers: a handle across the bar, dark, white, dark
	auto handle = [&](const RECT& bar, double t) {
		if (IsEmpty(bar))
			return;
		int y = CpYFromBar(bar, t);
		int out = Scale(3), half = Scale(3);
		RECT rc{ bar.left - out, y - half, bar.right + out, y + half + 1 };
		int white = std::max(1, Scale(2));
		CpFrameRect(px, rc, RGB(0, 0, 0));
		::InflateRect(&rc, -1, -1);
		CpFrameRect(px, rc, RGB(255, 255, 255), white);
		::InflateRect(&rc, -white, -white);
		CpFrameRect(px, rc, RGB(0, 0, 0));
	};
	handle(m_Hue, CpBarFromHue(m_Hsv.h));
	if (HasAlpha())
		handle(m_AlphaBar, CpBarFromAlpha(m_Alpha));

	WTL::CDC mdc;
	mdc.CreateCompatibleDC(hdc);
	auto old = mdc.SelectBitmap(m_Buffer);

	// the focused part
	bool cues = (SendMessage(WM_QUERYUISTATE) & UISF_HIDEFOCUS) == 0;
	if (::GetFocus() == m_hWnd && cues) {
		RECT rc = PartRect(m_Focus);
		if (!IsEmpty(rc)) {
			::InflateRect(&rc, Scale(4), Scale(4));
			mdc.SetTextColor(c.crText);
			mdc.SetBkColor(c.crBack);
			mdc.DrawFocusRect(&rc);
		}
	}

	::BitBlt(hdc, 0, 0, m_BufferSize.cx, m_BufferSize.cy, mdc, 0, 0, SRCCOPY);
	mdc.SelectBitmap(old);
}
