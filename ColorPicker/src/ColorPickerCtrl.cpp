#include "ColorPicker.h"
#include "ColorPickerDraw.h"
#include "ColorPickerGradient.h"
#include "ColorPickerMagnifier.h"
#include "ColorPickerModel.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>

using CpDraw::IsEmpty;

// ---- Construction ------------------------------------------------------------------

CColorPickerCtrl::CColorPickerCtrl() : m_Grad(std::make_unique<CpGradientWindow>(this)), m_Custom(CPK_CUSTOMCOLORS, CLR_NONE) {
	int count;
	auto palette = CpDefaultPalette(&count);
	m_Palette.assign(palette, palette + count);
}

CColorPickerCtrl::~CColorPickerCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CColorPickerCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	// a container: Tab goes through the gradients and the fields
	ModifyStyleEx(0, WS_EX_CONTROLPARENT);
	ModifyStyle(0, WS_CLIPCHILDREN);
	UpdateFont();
	m_Grad->Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS);
	CreateFields();
	m_hToolTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	Layout();
	UpdateFields(-1);
	return 0;
}

LRESULT CColorPickerCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	EndEyedropper(true);
	// popups made with a child as their parent are owned by the top-level window: they'd outlive the control
	if (m_hToolTip)
		::DestroyWindow(m_hToolTip);
	m_hToolTip = nullptr;
	m_ToolIds.clear();
	if (m_Magnifier && m_Magnifier->IsWindow())
		m_Magnifier->DestroyWindow();
	for (auto& edit : m_Edits)
		edit = nullptr;   // they go with the control
	bHandled = FALSE;
	return 0;
}

HWND CColorPickerCtrl::GetGradientWindow() const {
	return m_Grad->m_hWnd;
}

LRESULT CColorPickerCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Grad->IsWindow())
		m_Grad->SetFocus();
	return 0;
}

LRESULT CColorPickerCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through back buffers
}

LRESULT CColorPickerCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	Invalidate();
	return 0;
}

LRESULT CColorPickerCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == GWL_STYLE) {
		if (m_Focus == CPKHT_ALPHA && !HasAlpha())
			m_Focus = CPKHT_HUE;
		Layout();
		Invalidate();
		if (m_Grad->IsWindow())
			m_Grad->Invalidate();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFont();
	Layout();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {
		UpdateFont();
		Layout();
		Invalidate();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_AlphaDirty = true;   // the checkerboard
	Invalidate();
	if (m_Grad->IsWindow())
		m_Grad->Invalidate();
	for (auto edit : m_Edits) {
		if (edit)
			::InvalidateRect(edit, nullptr, TRUE);
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CColorPickerCtrl::OnGradUpdateUIState(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Grad->Invalidate();   // focus cues shown or hidden
	bHandled = FALSE;
	return 0;
}

// ---- Fonts and layout ------------------------------------------------------------------

int CColorPickerCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

bool CColorPickerCtrl::HasAlpha() const {
	return (GetStyle() & CPKS_NOALPHA) == 0;
}

bool CColorPickerCtrl::HasFields() const {
	return (GetStyle() & CPKS_NOFIELDS) == 0;
}

bool CColorPickerCtrl::HasSwatch() const {
	return (GetStyle() & CPKS_NOSWATCH) == 0;
}

void CColorPickerCtrl::UpdateFont() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	for (auto edit : m_Edits) {
		if (edit)
			::SendMessage(edit, WM_SETFONT, (WPARAM)m_hFont, FALSE);
	}
}

LRESULT CColorPickerCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFont();
	Layout();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CColorPickerCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

void CColorPickerCtrl::Layout() {
	if (!IsWindow())
		return;
	m_Pad = Scale(8);
	m_BarWidth = Scale(20);
	m_Gap = Scale(12);
	RECT rc;
	GetClientRect(&rc);

	// the panel on the right, then the gradients take the rest
	PositionFields();
	int gradWidth = rc.right;
	if (HasFields() || HasSwatch() || HasGroups()) {
		int left = rc.right;
		for (auto& r : m_Labels)
			if (!IsEmpty(r))
				left = std::min(left, (int)r.left);
		for (auto& r : m_Groups)
			if (!IsEmpty(r))
				left = std::min(left, (int)r.left);
		if (!IsEmpty(m_Swatch))
			left = std::min(left, (int)m_Swatch.left);
		gradWidth = std::max(0, left - m_Gap + m_Pad);
	}
	if (m_Grad->IsWindow())
		m_Grad->SetWindowPos(nullptr, 0, 0, gradWidth, rc.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	auto l = CpComputeLayout(gradWidth, rc.bottom, { m_Pad, m_BarWidth, m_Gap }, HasAlpha());
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
		case CPKHT_NEWCOLOR:
		case CPKHT_OLDCOLOR:
		{
			RECT inner = m_Swatch, n, o;
			::InflateRect(&inner, -1, -1);
			CpSwatchRects(inner, m_HasOriginal, &n, &o);
			return IsEmpty(m_Swatch) ? RECT{} : part == CPKHT_NEWCOLOR ? n : o;
		}
	}
	return {};
}

BOOL CColorPickerCtrl::GetPartRect(UINT part, RECT* prc) const {
	if (!prc)
		return FALSE;
	*prc = PartRect(part);
	return !IsEmpty(*prc);
}

UINT CColorPickerCtrl::HitTest(POINT pt, int* pIndex) const {
	int index = -1;
	if (!pIndex)
		pIndex = &index;
	*pIndex = -1;
	// a little around each gradient grabs it too: the markers stick out
	int slack = Scale(4);
	RECT grad{};
	if (m_Grad->IsWindow())
		m_Grad->GetClientRect(&grad);
	for (UINT part : { CPKHT_SV, CPKHT_HUE, CPKHT_ALPHA }) {
		auto rc = PartRect(part);
		if (IsEmpty(rc))
			continue;
		::InflateRect(&rc, slack, slack);
		::IntersectRect(&rc, &rc, &grad);
		if (::PtInRect(&rc, pt))
			return part;
	}
	for (UINT part : { CPKHT_NEWCOLOR, CPKHT_OLDCOLOR }) {
		auto rc = PartRect(part);
		if (::PtInRect(&rc, pt))
			return part;
	}
	for (auto& rc : m_Frames) {
		if (::PtInRect(&rc, pt))
			return CPKHT_FIELD;
	}
	if (::PtInRect(&m_Eyedropper, pt))
		return CPKHT_EYEDROPPER;
	CpPanel p;
	std::copy(std::begin(m_Groups), std::end(m_Groups), p.group);
	p.cell = m_Cell;
	static const UINT parts[] = { CPKHT_PALETTE, CPKHT_CUSTOM, CPKHT_RECENT };
	for (int g = 0; g < CpGroupCount; g++) {
		int i = CpSwatchAt(p, (CpGroup)g, g == CpRecent ? CPK_MAXRECENT : GroupCount(g), m_CellGap, pt);
		if (i >= 0) {
			*pIndex = i;
			return parts[g];
		}
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
	if (part < CPKHT_SV || part > CPKHT_ALPHA || IsEmpty(PartRect(part)) || part == m_Focus)
		return;
	m_Focus = part;
	if (m_Grad->IsWindow())
		m_Grad->Invalidate();
}

UINT CColorPickerCtrl::GetFocusPart() const {
	return m_Focus;
}

// ---- Color ---------------------------------------------------------------------------

bool CColorPickerCtrl::SameState(const Hsv& hsv, BYTE alpha) const {
	return hsv.h == m_Hsv.h && hsv.s == m_Hsv.s && hsv.v == m_Hsv.v && alpha == m_Alpha;
}

void CColorPickerCtrl::SetState(const Hsv& hsv, BYTE alpha, int skipField) {
	auto c = CpNormalize({ hsv.h, hsv.s, hsv.v });
	Hsv n{ c.h, c.s, c.v };
	if (SameState(n, alpha))
		return;
	if (n.h != m_Hsv.h)
		m_SvDirty = true;
	if (CpHsvToRgb(c) != GetColor())
		m_AlphaDirty = true;
	m_Hsv = n;
	m_Alpha = alpha;
	if (!IsWindow())
		return;
	m_Grad->Invalidate();
	auto swatch = PartRect(CPKHT_NEWCOLOR);
	InvalidateRect(&swatch);
	for (auto& rc : m_Groups) {
		// the cell marked as the color
		RECT around = rc;
		::InflateRect(&around, 2, 2);
		InvalidateRect(&around, FALSE);
	}
	UpdateFields(skipField);
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

void CColorPickerCtrl::SetOriginalColor(COLORREF color, BYTE alpha) {
	m_HasOriginal = color != CLR_NONE;
	m_Original = color & 0xFFFFFF;
	m_OriginalAlpha = alpha;
	if (IsWindow())
		InvalidateRect(&m_Swatch);
}

COLORREF CColorPickerCtrl::GetOriginalColor(BYTE* pAlpha) const {
	if (pAlpha)
		*pAlpha = m_OriginalAlpha;
	return m_HasOriginal ? m_Original : CLR_NONE;
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

// ---- Mouse on the gradients ---------------------------------------------------------------

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
	if (SameState(hsv, alpha))
		return;
	SetState(hsv, alpha);
	m_Grad->UpdateWindow();   // the marker follows the mouse even when the parent is slow to repaint
	Notify(CPKN_COLORCHANGING, m_Drag);
}

LRESULT CColorPickerCtrl::OnGradLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (::GetFocus() != m_Grad->m_hWnd)
		m_Grad->SetFocus();   // a field typed in commits first
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	UINT part = HitTest(pt);
	if (part < CPKHT_SV || part > CPKHT_ALPHA || m_Drag != CPKHT_NOWHERE)
		return 0;
	SetFocusPart(part);
	m_DragStartHsv = m_Hsv;
	m_DragStartAlpha = m_Alpha;
	m_Drag = part;
	m_Grad->SetCapture();
	ApplyPoint(pt);
	return 0;
}

LRESULT CColorPickerCtrl::OnGradMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != CPKHT_NOWHERE)
		ApplyPoint({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	return 0;
}

LRESULT CColorPickerCtrl::OnGradLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	EndDrag(false);
	return 0;
}

LRESULT CColorPickerCtrl::OnGradCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	EndDrag(false);   // taken away (another window, a menu): what the drag did stays
	return 0;
}

void CColorPickerCtrl::EndDrag(bool cancel) {
	UINT part = m_Drag;
	if (part == CPKHT_NOWHERE)
		return;
	m_Drag = CPKHT_NOWHERE;   // first: releasing the capture comes back through OnGradCaptureChanged
	if (::GetCapture() == m_Grad->m_hWnd)
		::ReleaseCapture();
	if (SameState(m_DragStartHsv, m_DragStartAlpha))
		return;
	if (cancel) {
		SetState(m_DragStartHsv, m_DragStartAlpha);
		Notify(CPKN_COLORCHANGING, part);
	}
	else {
		Notify(CPKN_COLORCHANGED, part);
	}
}

// ---- Keyboard on the gradients --------------------------------------------------------------

LRESULT CColorPickerCtrl::OnGradFocus(UINT uMsg, WPARAM, LPARAM, BOOL&) {
	// tabbing in starts at the first part, Shift+Tab at the last
	if (uMsg == WM_SETFOCUS && ::GetKeyState(VK_TAB) < 0)
		m_Focus = ::GetKeyState(VK_SHIFT) < 0 ? (HasAlpha() ? CPKHT_ALPHA : CPKHT_HUE) : CPKHT_SV;
	// the eyedropper keeps the focus here for Escape; the focus going (another app) ends it
	if (uMsg == WM_KILLFOCUS && m_Picking)
		CancelEyedropper();
	m_Grad->Invalidate();
	return 0;
}

LRESULT CColorPickerCtrl::OnGradGetDlgCode(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != CPKHT_NOWHERE || m_Picking)
		return DLGC_WANTALLKEYS;   // Escape cancels the drag or the eyedropper
	LRESULT code = DLGC_WANTARROWS;
	// Tab moves between the parts, and leaves from the first or last one
	auto msg = (const MSG*)lParam;
	if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_TAB && ::GetKeyState(VK_CONTROL) >= 0 &&
		NextPart(m_Focus, ::GetKeyState(VK_SHIFT) >= 0) != CPKHT_NOWHERE)
		code |= DLGC_WANTTAB;
	return code;
}

LRESULT CColorPickerCtrl::OnGradKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	if (m_Picking) {
		if (wParam == VK_ESCAPE)
			CancelEyedropper();
		return 0;
	}
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
	if (!SameState(hsv, (BYTE)alpha)) {
		SetState(hsv, (BYTE)alpha);
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
	if (IsWindow()) {
		BOOL handled;
		OnColorsChanged(0, 0, 0, handled);
	}
	else {
		m_AlphaDirty = true;
	}
}

CPKCOLORS CColorPickerCtrl::GetColors() const {
	return m_SystemColors ? CpDraw::SystemColors() : m_Colors;
}

// ---- Painting the gradients ------------------------------------------------------------------

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

LRESULT CColorPickerCtrl::OnGradPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		PaintGradients((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_Grad->m_hWnd);
	PaintGradients(dc);
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

void CColorPickerCtrl::PaintGradients(HDC hdc) {
	RECT client;
	m_Grad->GetClientRect(&client);
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
	bool cues = (m_Grad->SendMessage(WM_QUERYUISTATE) & UISF_HIDEFOCUS) == 0;
	if (::GetFocus() == m_Grad->m_hWnd && cues) {
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
