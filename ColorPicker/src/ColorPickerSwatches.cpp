#include "ColorPicker.h"
#include "ColorPickerDraw.h"
#include "ColorPickerGradient.h"
#include "ColorPickerMagnifier.h"
#include "ColorPickerModel.h"

#include <commctrl.h>

#include <algorithm>

// The swatch groups under the fields (palette, custom, recent), the eyedropper button beside the swatch, and
// the eyedropper itself: a low-level mouse hook while it's on, since the mouse has to be followed over other
// apps' windows, where capturing it doesn't reach without a button held down.

using namespace CpDraw;

namespace {
	const UINT GroupParts[CpGroupCount] = { CPKHT_PALETTE, CPKHT_CUSTOM, CPKHT_RECENT };

	int GroupOfPart(UINT part) {
		for (int g = 0; g < CpGroupCount; g++) {
			if (GroupParts[g] == part)
				return g;
		}
		return -1;
	}

	// the one control picking (the hook has no data of its own)
	CColorPickerCtrl* g_Picker;
	HHOOK g_PickHook;
}

// ---- Groups ----------------------------------------------------------------------------------

bool CColorPickerCtrl::HasGroups() const {
	return (GetStyle() & CPKS_NOPALETTE) == 0;
}

int CColorPickerCtrl::GroupCount(int group) const {
	switch (group) {
		case CpPalette: return (int)m_Palette.size();
		case CpCustom: return (int)m_Custom.size();
		case CpRecent: return (int)m_Recent.size();
	}
	return 0;
}

COLORREF CColorPickerCtrl::GroupColor(int group, int index) const {
	if (index < 0 || index >= GroupCount(group))
		return CLR_NONE;
	switch (group) {
		case CpPalette: return m_Palette[index];
		case CpCustom: return m_Custom[index];
		default: return m_Recent[index];
	}
}

RECT CColorPickerCtrl::SwatchCellRect(int group, int index) const {
	CpPanel p;
	std::copy(std::begin(m_Groups), std::end(m_Groups), p.group);
	p.cell = m_Cell;
	return CpSwatchCell(p, (CpGroup)group, index, m_CellGap);
}

void CColorPickerCtrl::SetPalette(const COLORREF* colors, int count) {
	if (!colors) {
		colors = CpDefaultPalette(&count);
	}
	count = std::clamp(count, 0, CPK_MAXPALETTE);
	m_Palette.assign(colors, colors + count);
	for (auto& c : m_Palette)
		c &= 0xFFFFFF;
	if (IsWindow()) {
		Layout();   // the rows may change
		Invalidate();
	}
}

int CColorPickerCtrl::GetPalette(COLORREF* colors, int max) const {
	if (colors)
		std::copy_n(m_Palette.begin(), std::min(max, (int)m_Palette.size()), colors);
	return (int)m_Palette.size();
}

void CColorPickerCtrl::SetCustomColors(const COLORREF* colors, int count) {
	for (int i = 0; i < CPK_CUSTOMCOLORS; i++)
		m_Custom[i] = colors && i < count && colors[i] != CLR_NONE ? colors[i] & 0xFFFFFF : CLR_NONE;
	if (IsWindow())
		InvalidateRect(&m_Groups[CpCustom], FALSE);
}

int CColorPickerCtrl::GetCustomColors(COLORREF* colors, int max) const {
	if (colors)
		std::copy_n(m_Custom.begin(), std::min(max, CPK_CUSTOMCOLORS), colors);
	return CPK_CUSTOMCOLORS;
}

void CColorPickerCtrl::AddRecentColor(COLORREF color) {
	CpAddRecent(m_Recent, color, CPK_MAXRECENT);
	if (IsWindow())
		InvalidateRect(&m_Groups[CpRecent], FALSE);
}

void CColorPickerCtrl::SetRecentColors(const COLORREF* colors, int count) {
	m_Recent.clear();
	// in order, the first the latest
	for (int i = std::min(count, CPK_MAXRECENT) - 1; colors && i >= 0; i--)
		CpAddRecent(m_Recent, colors[i], CPK_MAXRECENT);
	if (IsWindow())
		InvalidateRect(&m_Groups[CpRecent], FALSE);
}

int CColorPickerCtrl::GetRecentColors(COLORREF* colors, int max) const {
	if (colors)
		std::copy_n(m_Recent.begin(), std::min(max, (int)m_Recent.size()), colors);
	return (int)m_Recent.size();
}

void CColorPickerCtrl::PickSwatch(UINT part, int index) {
	COLORREF color = GroupColor(GroupOfPart(part), index);
	if (color == CLR_NONE)
		return;
	auto c = CpUpdateHsv({ m_Hsv.h, m_Hsv.s, m_Hsv.v }, color);
	Hsv n{ c.h, c.s, c.v };
	if (SameState(n, m_Alpha))
		return;
	SetState(n, m_Alpha);
	Notify(CPKN_COLORCHANGING, part);
	Notify(CPKN_COLORCHANGED, part);
}

void CColorPickerCtrl::SaveCustom(int index, COLORREF color) {
	if (index < 0 || index >= CPK_CUSTOMCOLORS || m_Custom[index] == color)
		return;
	m_Custom[index] = color;
	auto rc = SwatchCellRect(CpCustom, index);
	::InflateRect(&rc, 2, 2);
	InvalidateRect(&rc, FALSE);
	NMHDR nm{ m_hWnd, (UINT_PTR)GetDlgCtrlID(), CPKN_CUSTOMCHANGED };
	::SendMessage(GetParent(), WM_NOTIFY, nm.idFrom, (LPARAM)&nm);
}

void CColorPickerCtrl::CustomMenu(int index, POINT ptScreen) {
	enum { IdSave = 1, IdClear };
	HMENU menu = ::CreatePopupMenu();
	::AppendMenuW(menu, MF_STRING, IdSave, L"&Save the Color Here");
	::AppendMenuW(menu, MF_STRING | (m_Custom[index] == CLR_NONE ? MF_GRAYED : 0), IdClear, L"&Clear");
	UINT cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, 0, m_hWnd, nullptr);
	::DestroyMenu(menu);
	if (cmd == IdSave)
		SaveCustom(index, GetColor());
	else if (cmd == IdClear)
		SaveCustom(index, CLR_NONE);
}

// ---- Mouse -----------------------------------------------------------------------------------

void CColorPickerCtrl::SetHot(UINT part, int index) {
	if (part == m_HotPart && index == m_HotIndex)
		return;
	auto invalidate = [&](UINT p, int i) {
		RECT rc{};
		if (p == CPKHT_EYEDROPPER)
			rc = m_Eyedropper;
		else if (int g = GroupOfPart(p); g >= 0 && i >= 0)
			rc = SwatchCellRect(g, i);
		::InflateRect(&rc, 2, 2);
		InvalidateRect(&rc, FALSE);
	};
	invalidate(m_HotPart, m_HotIndex);
	m_HotPart = part;
	m_HotIndex = index;
	invalidate(part, index);
	if (part != CPKHT_NOWHERE && !m_TrackingLeave) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
}

LRESULT CColorPickerCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingLeave = false;
	SetHot(CPKHT_NOWHERE, -1);
	return 0;
}

LRESULT CColorPickerCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	int index;
	if (!m_Picking && HitTest(pt, &index) == CPKHT_CUSTOM) {
		ClientToScreen(&pt);
		CustomMenu(index, pt);
	}
	return 0;
}

LRESULT CColorPickerCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	// Shift+F10 or the menu key: the custom slot under the mouse, if any
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	bHandled = FALSE;
	if (pt.x == -1 && pt.y == -1 && m_HotPart == CPKHT_CUSTOM && m_HotIndex >= 0) {
		auto rc = SwatchCellRect(CpCustom, m_HotIndex);
		pt = { rc.left, rc.bottom };
		ClientToScreen(&pt);
		CustomMenu(m_HotIndex, pt);
		bHandled = TRUE;
	}
	return 0;
}

LRESULT CColorPickerCtrl::OnSetCursor(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// over the control and its children while picking: the cross
	if (m_Picking) {
		::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
		return TRUE;
	}
	bHandled = FALSE;
	return 0;
}

// ---- Tooltips ------------------------------------------------------------------------------

void CColorPickerCtrl::UpdateToolTips() {
	if (!m_hToolTip)
		return;
	for (auto id : m_ToolIds) {
		TOOLINFOW ti{ sizeof(ti), 0, m_hWnd, id };
		::SendMessage(m_hToolTip, TTM_DELTOOLW, 0, (LPARAM)&ti);
	}
	// one tool per cell (id: group * 100 + index), and one for the eyedropper button (id 1000)
	auto add = [&](UINT_PTR id, const RECT& rc) {
		TOOLINFOW ti{ sizeof(ti), TTF_SUBCLASS, m_hWnd, id };
		ti.rect = rc;
		ti.lpszText = LPSTR_TEXTCALLBACKW;
		::SendMessage(m_hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	};
	static_assert(CPK_MAXPALETTE < 100);
	std::vector<UINT_PTR> ids;
	if (!IsEmpty(m_Eyedropper))
		ids.push_back(1000);
	for (int g = 0; g < CpGroupCount; g++) {
		if (IsEmpty(m_Groups[g]))
			continue;
		int count = g == CpRecent ? CPK_MAXRECENT : GroupCount(g);
		for (int i = 0; i < count; i++)
			ids.push_back(g * 100 + i);
	}
	for (auto id : ids)
		add(id, id == 1000 ? m_Eyedropper : SwatchCellRect((int)(id / 100), (int)(id % 100)));
	m_ToolIds = std::move(ids);
}

LRESULT CColorPickerCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom != m_hToolTip || hdr->code != TTN_GETDISPINFOW) {
		bHandled = FALSE;
		return 0;
	}
	auto di = (NMTTDISPINFOW*)lParam;
	UINT_PTR id = hdr->idFrom;
	if (id == 1000) {
		m_TipText = L"Pick a color from the screen";
	}
	else {
		int group = (int)(id / 100), index = (int)(id % 100);
		COLORREF color = GroupColor(group, index);
		if (color != CLR_NONE)
			m_TipText = CpFormatHex(color).c_str();
		else if (group == CpCustom)
			m_TipText = L"Click to save the color here";
		else
			m_TipText.Empty();
		if (group == CpCustom && color != CLR_NONE)
			m_TipText += L" (Ctrl+click to replace)";
	}
	di->lpszText = (LPWSTR)(LPCWSTR)m_TipText;
	return 0;
}

// ---- Painting ------------------------------------------------------------------------------

void CColorPickerCtrl::PaintSwatches(HDC hdc, const CPKCOLORS& c) {
	COLORREF current = GetColor();
	bool currentShown = false;   // one cell marked as the color, the first that is
	for (int g = 0; g < CpGroupCount; g++) {
		if (IsEmpty(m_Groups[g]))
			continue;
		int slots = g == CpRecent ? CPK_MAXRECENT : GroupCount(g);
		for (int i = 0; i < slots; i++) {
			RECT rc = SwatchCellRect(g, i);
			COLORREF color = GroupColor(g, i);
			bool hot = m_HotPart == GroupParts[g] && m_HotIndex == i;
			if (color == CLR_NONE) {
				// an empty slot: a faint outline (custom slots also take a click)
				FillSolid(hdc, rc, c.crBack);
				Frame(hdc, rc, hot ? c.crText : Blend(c.crBack, c.crBorder, g == CpCustom ? 100 : 55));
				continue;
			}
			FillSolid(hdc, rc, color);
			Frame(hdc, rc, c.crBorder);
			if (hot || (!currentShown && color == current)) {
				// a dark and a light frame: seen on any color and background
				RECT outer = rc;
				::InflateRect(&outer, 1, 1);
				Frame(hdc, outer, hot ? c.crText : RGB(0, 0, 0));
				Frame(hdc, rc, RGB(255, 255, 255));
				currentShown |= color == current;
			}
		}
	}
}

void CColorPickerCtrl::PaintEyedropperButton(HDC hdc, const CPKCOLORS& c) {
	RECT rc = m_Eyedropper;
	if (IsEmpty(rc))
		return;
	bool hot = m_HotPart == CPKHT_EYEDROPPER;
	FillSolid(hdc, rc, m_Picking ? Blend(c.crFieldBack, c.crText, 25) : hot ? Blend(c.crFieldBack, c.crText, 12) : c.crFieldBack);
	Frame(hdc, rc, m_Picking ? ::GetSysColor(COLOR_HIGHLIGHT) : c.crBorder);

	// the glyph: a cross hair, a ring with ticks
	int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
	int r = std::max(3, (int)std::min(rc.right - rc.left, rc.bottom - rc.top) / 2 - Scale(5));
	COLORREF ink = Blend(c.crFieldBack, c.crText, 80);
	HPEN pen = ::CreatePen(PS_SOLID, std::max(1, Scale(1)), ink);
	auto oldPen = ::SelectObject(hdc, pen);
	auto oldBrush = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
	int ring = r * 2 / 3;
	::Ellipse(hdc, cx - ring, cy - ring, cx + ring + 1, cy + ring + 1);
	::MoveToEx(hdc, cx - r, cy, nullptr);
	::LineTo(hdc, cx - ring / 2, cy);
	::MoveToEx(hdc, cx + ring / 2 + 1, cy, nullptr);
	::LineTo(hdc, cx + r + 1, cy);
	::MoveToEx(hdc, cx, cy - r, nullptr);
	::LineTo(hdc, cx, cy - ring / 2);
	::MoveToEx(hdc, cx, cy + ring / 2 + 1, nullptr);
	::LineTo(hdc, cx, cy + r + 1);
	::SelectObject(hdc, oldBrush);
	::SelectObject(hdc, oldPen);
	::DeleteObject(pen);
}

// ---- Eyedropper ----------------------------------------------------------------------------

bool CColorPickerCtrl::IsEyedropperActive() const {
	return m_Picking;
}

void CColorPickerCtrl::StartEyedropper() {
	if (m_Picking || !IsWindow() || m_Drag != CPKHT_NOWHERE || g_Picker)
		return;
	EndSpin();
	// the focus here: Escape cancels (and a field typed in commits first)
	m_Grad->SetFocus();
	if (!m_Magnifier)
		m_Magnifier = std::make_unique<CpMagnifier>();
	if (!m_Magnifier->IsWindow())
		m_Magnifier->Create(m_hWnd, rcDefault);
	g_PickHook = ::SetWindowsHookExW(WH_MOUSE_LL, PickHookProc, ::GetModuleHandleW(nullptr), 0);
	if (!g_PickHook)
		return;
	g_Picker = this;
	m_Picking = true;
	m_PickStartHsv = m_Hsv;
	m_PickStartAlpha = m_Alpha;
	m_PickPressed = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0;   // started by pressing the button
	m_PickSwallowUp = 0;
	m_PickMovePosted = false;
	::GetCursorPos(&m_PickPoint);
	::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
	InvalidateRect(&m_Eyedropper, FALSE);
	SampleScreen(m_PickPoint);
}

void CColorPickerCtrl::CancelEyedropper() {
	EndEyedropper(true);
}

void CColorPickerCtrl::EndEyedropper(bool cancel) {
	if (!m_Picking)
		return;
	m_Picking = false;
	if (g_PickHook)
		::UnhookWindowsHookEx(g_PickHook);
	g_PickHook = nullptr;
	g_Picker = nullptr;
	if (m_Magnifier && m_Magnifier->IsWindow())
		m_Magnifier->ShowWindow(SW_HIDE);
	InvalidateRect(&m_Eyedropper, FALSE);
	if (SameState(m_PickStartHsv, m_PickStartAlpha))
		return;
	if (cancel) {
		SetState(m_PickStartHsv, m_PickStartAlpha);
		Notify(CPKN_COLORCHANGING, CPKHT_EYEDROPPER);
	}
	else {
		Notify(CPKN_COLORCHANGED, CPKHT_EYEDROPPER);
	}
}

void CColorPickerCtrl::SampleScreen(POINT ptScreen) {
	m_Magnifier->Show(ptScreen, m_Dpi, GetColors(), m_hFont);
	auto c = CpUpdateHsv({ m_Hsv.h, m_Hsv.s, m_Hsv.v }, m_Magnifier->GetColor());
	Hsv n{ c.h, c.s, c.v };
	if (SameState(n, m_Alpha))
		return;
	SetState(n, m_Alpha);
	Notify(CPKN_COLORCHANGING, CPKHT_EYEDROPPER);
}

LRESULT CColorPickerCtrl::OnPick(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (!m_Picking)
		return 0;
	switch (wParam) {
		case PickMove:
			m_PickMovePosted = false;
			SampleScreen(m_PickPoint);
			break;
		case PickCommit:
			SampleScreen(m_PickPoint);
			EndEyedropper(false);
			break;
		case PickCancel:
			EndEyedropper(true);
			break;
	}
	return 0;
}

// Runs on the control's thread (the one that set it), between its messages: it only notes the mouse and posts.
LRESULT CALLBACK CColorPickerCtrl::PickHookProc(int code, WPARAM wParam, LPARAM lParam) {
	auto self = g_Picker;
	if (code != HC_ACTION || !self)
		return ::CallNextHookEx(nullptr, code, wParam, lParam);
	auto ms = (const MSLLHOOKSTRUCT*)lParam;
	self->m_PickPoint = ms->pt;
	auto post = [&](WPARAM what) { self->PostMessage(WM_CPK_PICK, what); };
	switch (wParam) {
		case WM_MOUSEMOVE:
			if (!self->m_PickMovePosted) {
				self->m_PickMovePosted = true;
				post(PickMove);
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
			// the click that picks (or cancels) is ours, and so is its button-up
			self->m_PickSwallowUp = wParam == WM_LBUTTONDOWN ? WM_LBUTTONUP : WM_RBUTTONUP;
			post(wParam == WM_LBUTTONDOWN ? PickCommit : PickCancel);
			return 1;

		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
			if (self->m_PickSwallowUp == wParam) {
				self->m_PickSwallowUp = 0;
				return 1;
			}
			if (wParam == WM_LBUTTONUP && self->m_PickPressed) {
				// the press on the button ends: released away from it, it's a drag that picks there
				self->m_PickPressed = false;
				RECT button = self->m_Eyedropper;
				self->ClientToScreen(&button);
				if (!::PtInRect(&button, ms->pt)) {
					post(PickCommit);
					return 1;
				}
			}
			break;
	}
	return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

// ---- Magnifier ------------------------------------------------------------------------------

CpMagnifier::~CpMagnifier() {
	if (m_hBits)
		::DeleteObject(m_hBits);
}

LRESULT CpMagnifier::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	::SetLayeredWindowAttributes(m_hWnd, 0, 255, LWA_ALPHA);
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = Pixels;
	bmi.bmiHeader.biHeight = -Pixels;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	m_hBits = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	m_Bits = (uint32_t*)bits;
	return 0;
}

LRESULT CpMagnifier::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CpMagnifier::OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) {
	return HTTRANSPARENT;
}

COLORREF CpMagnifier::GetColor() const {
	return m_Bits ? CpColor(m_Bits[(Pixels / 2) * Pixels + Pixels / 2]) : 0;
}

void CpMagnifier::Show(POINT pt, UINT dpi, const CPKCOLORS& colors, HFONT font) {
	m_Dpi = dpi;
	m_Colors = colors;
	m_hFont = font;
	if (!m_hBits)
		return;
	HDC screen = ::GetDC(nullptr);
	HDC mem = ::CreateCompatibleDC(screen);
	auto old = ::SelectObject(mem, m_hBits);
	::BitBlt(mem, 0, 0, Pixels, Pixels, screen, pt.x - Pixels / 2, pt.y - Pixels / 2, SRCCOPY);
	::SelectObject(mem, old);
	::DeleteDC(mem);
	::ReleaseDC(nullptr, screen);
	::GdiFlush();

	// placed beside the mouse, in its monitor's work area
	TEXTMETRICW tm{};
	HDC dc = ::GetDC(m_hWnd);
	auto oldFont = ::SelectObject(dc, font);
	::GetTextMetricsW(dc, &tm);
	::SelectObject(dc, oldFont);
	::ReleaseDC(m_hWnd, dc);
	int zoom = ::MulDiv(9, dpi, 96);
	SIZE size{ Pixels * zoom + 2, Pixels * zoom + 2 + tm.tmHeight + ::MulDiv(6, dpi, 96) };
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfoW(::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
	auto at = CpPlaceMagnifier(pt, size, mi.rcWork, ::MulDiv(20, dpi, 96));
	SetWindowPos(HWND_TOPMOST, at.x, at.y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	Invalidate();
	UpdateWindow();
}

LRESULT CpMagnifier::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC pdc(m_hWnd);
	RECT client;
	GetClientRect(&client);
	WTL::CMemoryDC dc(pdc, client);
	int zoom = ::MulDiv(9, m_Dpi, 96);
	int grid = Pixels * zoom;

	::SetBkColor(dc, m_Colors.crBack);
	::ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &client, nullptr, 0, nullptr);
	if (m_Bits) {
		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = Pixels;
		bmi.bmiHeader.biHeight = -Pixels;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		::SetStretchBltMode(dc, COLORONCOLOR);
		::StretchDIBits(dc, 1, 1, grid, grid, 0, 0, Pixels, Pixels, m_Bits, &bmi, DIB_RGB_COLORS, SRCCOPY);
	}
	// the pixel under the mouse: a dark and a light frame
	int c0 = 1 + (Pixels / 2) * zoom;
	RECT center{ c0 - 1, c0 - 1, c0 + zoom + 1, c0 + zoom + 1 };
	auto frame = [&](const RECT& rc, COLORREF color) {
		::SetBkColor(dc, color);
		RECT edges[4] = { { rc.left, rc.top, rc.right, rc.top + 1 }, { rc.left, rc.bottom - 1, rc.right, rc.bottom },
			{ rc.left, rc.top, rc.left + 1, rc.bottom }, { rc.right - 1, rc.top, rc.right, rc.bottom } };
		for (auto& e : edges)
			::ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &e, nullptr, 0, nullptr);
	};
	frame(center, RGB(0, 0, 0));
	::InflateRect(&center, -1, -1);
	frame(center, RGB(255, 255, 255));
	frame(client, m_Colors.crBorder);

	auto text = CpFormatHex(GetColor());
	RECT rcText{ 1, 1 + grid, client.right - 1, client.bottom - 1 };
	auto oldFont = ::SelectObject(dc, m_hFont);
	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, m_Colors.crText);
	::DrawTextW(dc, text.c_str(), -1, &rcText, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	::SelectObject(dc, oldFont);
	return 0;
}
