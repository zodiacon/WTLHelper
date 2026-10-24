#include "Accordion.h"
#include "AccordionModel.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>

#pragma comment(lib, "comctl32.lib")

// a section's side the model doesn't need: what the header shows, and the body window
struct AcdNode {
	struct Button {
		UINT         Id;
		int          Image;
		std::wstring Text;
	};

	std::wstring Text;
	std::wstring Info;
	int          Image = -1;
	HWND         Body = nullptr;
	LPARAM       Data = 0;
	std::vector<Button> Buttons;
	bool         Clipped = false;   // a window region shows part of the body (while it opens or closes)
};

// The headers' keyboard focus: a child window with no size, first among the children, so the headers are one tab
// stop before the bodies (the control is a WS_EX_CONTROLPARENT container, which the dialog manager tabs into
// rather than to). Its messages are handled in the control's ALT_MSG_MAP(1).
class AcdFocusWindow : public ATL::CWindowImpl<AcdFocusWindow> {
public:
	DECLARE_WND_CLASS_EX(L"AcdHeaderFocus", 0, -1)

	explicit AcdFocusWindow(CAccordionCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(AcdFocusWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 1)
	END_MSG_MAP()

private:
	CAccordionCtrl* m_Owner;
};

// The header being dragged to another place: a layered popup over the control, a little transparent, that the
// mouse goes through.
class AcdDragImage : public ATL::CWindowImpl<AcdDragImage> {
public:
	DECLARE_WND_CLASS_EX(L"AcdDragImage", 0, -1)

	BEGIN_MSG_MAP(AcdDragImage)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
	END_MSG_MAP()

private:
	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) { return HTTRANSPARENT; }
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) { return MA_NOACTIVATE; }
};

namespace {

constexpr UINT_PTR k_AnimationTimer = 1;
constexpr double   k_AnimationMs = 150;
constexpr UINT_PTR k_TipTool = 1;

} // namespace

namespace {

bool IsDark(COLORREF c) {
	return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 < 128 * 1000;
}

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
	::SetBkColor(hdc, color);
	::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

// the colors that follow the system (and WTLHelper's dark mode, which hooks COLOR_WINDOW, COLOR_3DFACE and
// COLOR_WINDOWTEXT; read each time)
ACDCOLORS SystemColors() {
	ACDCOLORS c;
	HIGHCONTRASTW hc{ sizeof(hc) };
	if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
		c.crBack = c.crHeaderBack = ::GetSysColor(COLOR_WINDOW);
		c.crHeaderText = c.crInfoText = c.crBorder = ::GetSysColor(COLOR_WINDOWTEXT);
		return c;
	}
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	COLORREF text = ::GetSysColor(COLOR_WINDOWTEXT);
	bool dark = IsDark(c.crBack);
	c.crHeaderBack = Blend(c.crBack, text, dark ? 8 : 5);
	c.crHeaderText = text;
	c.crInfoText = Blend(text, c.crBack, 40);
	c.crBorder = Blend(c.crBack, text, dark ? 22 : 18);
	return c;
}

// a chevron: pointing down (open) or right (closed), arms `size` pixels long and `thickness` thick, centered in rc
void Chevron(HDC hdc, const RECT& rc, bool open, int size, int thickness, COLORREF color) {
	int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
	for (int i = 0; i < size; i++) {
		if (open) {
			// rows from the top: the arms come together at the bottom
			int y = cy - size / 2 + i;
			FillSolid(hdc, { cx - size + i, y, cx - size + i + thickness, y + 1 }, color);
			FillSolid(hdc, { cx + size - i - thickness, y, cx + size - i, y + 1 }, color);
		}
		else {
			int x = cx - size / 2 + i;
			FillSolid(hdc, { x, cy - size + i, x + 1, cy - size + i + thickness }, color);
			FillSolid(hdc, { x, cy + size - i - thickness, x + 1, cy + size - i }, color);
		}
	}
}

} // namespace

CAccordionCtrl::CAccordionCtrl() : m_Layout(std::make_unique<AcdLayout>()), m_FocusWindow(std::make_unique<AcdFocusWindow>(this)),
	m_DragLayout(std::make_unique<AcdLayout>()) {
}

CAccordionCtrl::~CAccordionCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hTitleFont)
		::DeleteObject(m_hTitleFont);
	if (m_hBackBrush)
		::DeleteObject(m_hBackBrush);
}

int CAccordionCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

int CAccordionCtrl::IndexOf(HACDSECTION hSection) const {
	for (size_t i = 0; i < m_Nodes.size(); i++) {
		if (m_Nodes[i].get() == hSection)
			return (int)i;
	}
	return -1;
}

// ---- Window lifetime --------------------------------------------------------------------

LRESULT CAccordionCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	// Tab goes into the bodies; painting leaves them alone
	ModifyStyleEx(0, WS_EX_CONTROLPARENT);
	ModifyStyle(0, WS_CLIPCHILDREN);
	// a tab stop itself only through the headers' focus window
	ModifyStyle(WS_TABSTOP, 0);
	RECT none{};
	m_FocusWindow->Create(m_hWnd, none, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP);
	m_hToolTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
	ti.hwnd = m_hWnd;
	ti.uId = k_TipTool;
	ti.lpszText = LPSTR_TEXTCALLBACKW;
	::SendMessage(m_hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	UpdateFont();
	Layout();
	return 0;
}

LRESULT CAccordionCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	KillTimer(k_AnimationTimer);
	HideDragImage();
	// a popup made with a child as its parent is owned by the top-level window: it'd outlive the control
	if (m_hToolTip)
		::DestroyWindow(m_hToolTip);
	m_hToolTip = nullptr;
	bHandled = FALSE;
	return 0;
}

LRESULT CAccordionCtrl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	// the control given the focus: the headers take it
	if (m_FocusWindow->IsWindow())
		m_FocusWindow->SetFocus();
	return 0;
}

LRESULT CAccordionCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	return 0;
}

LRESULT CAccordionCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFont();
	m_FitWidth = -1;
	Layout();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CAccordionCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS) {
		UpdateFont();
		Layout();
	}
	Invalidate();   // high contrast, among others
	bHandled = FALSE;
	return 0;
}

LRESULT CAccordionCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CAccordionCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto ss = (const STYLESTRUCT*)lParam;
	if (wParam == GWL_STYLE && (ss->styleNew & ACDS_SINGLEEXPAND) && !(ss->styleOld & ACDS_SINGLEEXPAND)) {
		// one open section: the first that is stays
		for (size_t i = 0; i < m_Sections.size(); i++) {
			if (m_Sections[i].collapsible && m_Sections[i].expanded) {
				Changed(AcdSetExpanded(m_Sections, i, true, true));
				break;
			}
		}
	}
	bHandled = FALSE;
	return 0;
}

// ---- Font -----------------------------------------------------------------------------------

void CAccordionCtrl::UpdateFont() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	// the titles in the same font, semibold
	if (m_hTitleFont)
		::DeleteObject(m_hTitleFont);
	LOGFONTW lf{};
	::GetObjectW(m_hFont ? m_hFont : (HFONT)::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
	lf.lfWeight = FW_SEMIBOLD;
	m_hTitleFont = ::CreateFontIndirectW(&lf);

	// the header: the text with room around it, and at least an icon's height
	HDC hdc = ::GetDC(m_hWnd);
	auto old = ::SelectObject(hdc, m_hTitleFont);
	TEXTMETRICW tm{};
	::GetTextMetricsW(hdc, &tm);
	::SelectObject(hdc, old);
	::ReleaseDC(m_hWnd, hdc);
	int icon = 0;
	if (m_hImageList) {
		int cx, cy;
		::ImageList_GetIconSize(m_hImageList, &cx, &cy);
		icon = cy + Scale(6);
	}
	m_HeaderHeight = std::max({ (int)tm.tmHeight + Scale(12), Scale(26), icon });
}

LRESULT CAccordionCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
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

LRESULT CAccordionCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

int CAccordionCtrl::GetHeaderHeight() const {
	return m_HeaderHeight;
}

// ---- Layout ---------------------------------------------------------------------------------

void CAccordionCtrl::AskFitHeights(int width, bool all) {
	if (!all && width == m_FitWidth)
		return;
	m_FitWidth = width;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		auto& s = m_Sections[i];
		if (s.mode != AcdHeight::Fit)
			continue;
		NMACDFITHEIGHT nm{};
		nm.section.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), ACDN_GETFITHEIGHT };
		nm.section.hSection = m_Nodes[i].get();
		nm.section.id = s.id;
		nm.section.lParam = m_Nodes[i]->Data;
		nm.width = width;
		nm.height = s.fitHeight;
		::SendMessage(GetParent(), WM_NOTIFY, nm.section.hdr.idFrom, (LPARAM)&nm);
		s.fitHeight = std::max(0, nm.height);
	}
}

void CAccordionCtrl::Layout() {
	if (!IsWindow() || m_InLayout)
		return;
	m_InLayout = true;
	AcdMetrics metrics{ m_HeaderHeight, 0, m_Dpi };
	// The scroll bar coming or going changes the width (and the fit heights with it): twice at most, as a width
	// only shows the bar if the content is taller, which a narrower width doesn't undo.
	for (int pass = 0; pass < 2; pass++) {
		RECT rc;
		GetClientRect(&rc);
		AskFitHeights(rc.right, false);
		*m_Layout = AcdLayoutSections(m_Sections, rc.right, rc.bottom, 0, metrics);
		m_ScrollPos = AcdClampScroll(m_ScrollPos, *m_Layout);
		SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
		si.nMax = std::max(0, m_Layout->contentHeight - 1);
		si.nPage = rc.bottom;
		si.nPos = m_ScrollPos;
		SetScrollInfo(SB_VERT, &si, TRUE);
		RECT after;
		GetClientRect(&after);
		if (after.right == rc.right)
			break;
	}
	RECT rc;
	GetClientRect(&rc);
	*m_Layout = AcdLayoutSections(m_Sections, rc.right, rc.bottom, m_ScrollPos, metrics);
	PositionBodies();
	Invalidate(FALSE);
	m_InLayout = false;
}

void CAccordionCtrl::PositionBodies() {
	// The bodies follow the headers' focus window in the z-order, top to bottom: the tab order. One opening or
	// closing is at its full height with a region showing its part, so its content doesn't reflow every frame.
	struct Clip {
		HWND Body;
		int  Width, Height;   // Height -1: no region
	};
	std::vector<Clip> clips;
	auto defer = ::BeginDeferWindowPos((int)m_Nodes.size() + 1);
	HWND after = HWND_TOP;
	if (m_FocusWindow->IsWindow()) {
		RECT focus = (size_t)m_Focus < m_Layout->items.size() ? m_Layout->items[m_Focus].header : RECT{};
		if (defer)
			defer = ::DeferWindowPos(defer, *m_FocusWindow, HWND_TOP, focus.left, focus.top, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
		after = *m_FocusWindow;
	}
	for (size_t i = 0; i < m_Nodes.size() && i < m_Layout->items.size(); i++) {
		auto& node = *m_Nodes[i];
		HWND body = node.Body;
		if (!body || !::IsWindow(body))
			continue;
		const auto& item = m_Layout->items[i];
		RECT rc = item.body;
		int visible = rc.bottom - rc.top;
		bool partial = visible > 0 && item.fullHeight > visible && m_Sections[i].progress > 0 && m_Sections[i].progress < 1;
		if (partial) {
			rc.bottom = rc.top + item.fullHeight;
			clips.push_back({ body, rc.right - rc.left, visible });
			node.Clipped = true;
		}
		else if (node.Clipped) {
			clips.push_back({ body, 0, -1 });
			node.Clipped = false;
		}
		UINT flags = SWP_NOACTIVATE | (visible > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
		if (defer)
			defer = ::DeferWindowPos(defer, body, after, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, flags);
		else
			::SetWindowPos(body, after, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, flags);
		after = body;
	}
	if (defer)
		::EndDeferWindowPos(defer);
	for (const auto& clip : clips)
		::SetWindowRgn(clip.Body, clip.Height < 0 ? nullptr : ::CreateRectRgn(0, 0, clip.Width, clip.Height), TRUE);
}

// ---- Scrolling ------------------------------------------------------------------------------

int CAccordionCtrl::GetScrollPos() const {
	return m_ScrollPos;
}

void CAccordionCtrl::SetScroll(int pos) {
	pos = AcdClampScroll(pos, *m_Layout);
	if (pos == m_ScrollPos)
		return;
	m_ScrollPos = pos;
	SetScrollPos(SB_VERT, pos, TRUE);
	RECT rc;
	GetClientRect(&rc);
	*m_Layout = AcdLayoutSections(m_Sections, rc.right, rc.bottom, m_ScrollPos, AcdMetrics{ m_HeaderHeight, 0, m_Dpi });
	PositionBodies();
	Invalidate(FALSE);
}

LRESULT CAccordionCtrl::OnVScroll(UINT msg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (lParam)   // a body's scroll bar or trackbar
		return OnForward(msg, wParam, lParam, bHandled);
	RECT rc;
	GetClientRect(&rc);
	int pos = m_ScrollPos;
	switch (LOWORD(wParam)) {
		case SB_TOP:        pos = 0; break;
		case SB_BOTTOM:     pos = m_Layout->scrollMax; break;
		case SB_LINEUP:     pos -= m_HeaderHeight; break;
		case SB_LINEDOWN:   pos += m_HeaderHeight; break;
		case SB_PAGEUP:     pos -= std::max<int>(rc.bottom - m_HeaderHeight, m_HeaderHeight); break;
		case SB_PAGEDOWN:   pos += std::max<int>(rc.bottom - m_HeaderHeight, m_HeaderHeight); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION:
		{
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_VERT, &si);
			pos = si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	SetScroll(pos);
	return 0;
}

LRESULT CAccordionCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (m_Layout->scrollMax == 0) {
		bHandled = FALSE;   // on to the parent
		return 0;
	}
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	int step = lines == WHEEL_PAGESCROLL ? m_Layout->scrollMax : (int)lines * Scale(20);
	SetScroll(m_ScrollPos - ::MulDiv(delta, step, WHEEL_DELTA));
	return 0;
}

void CAccordionCtrl::EnsureVisible(HACDSECTION hSection) {
	int i = IndexOf(hSection);
	if (i < 0 || (size_t)i >= m_Layout->items.size())
		return;
	RECT rc;
	GetClientRect(&rc);
	const auto& item = m_Layout->items[i];
	if (item.header.top < 0) {
		SetScroll(m_ScrollPos + item.header.top);
	}
	else {
		int bottom = std::max(item.header.bottom, item.body.bottom);
		if (bottom > rc.bottom)
			SetScroll(m_ScrollPos + std::min<int>(bottom - rc.bottom, item.header.top));   // the header stays in view
	}
}

// ---- The bodies' messages ---------------------------------------------------------------------

LRESULT CAccordionCtrl::OnForward(UINT msg, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (msg == WM_NOTIFY) {
		auto hdr = (NMHDR*)lParam;
		if (hdr->hwndFrom == m_hToolTip) {
			if (hdr->code == TTN_GETDISPINFOW && m_Hot >= 0 && m_HotButton >= 0 && (size_t)m_Hot < m_Nodes.size()) {
				auto& buttons = m_Nodes[m_Hot]->Buttons;
				if ((size_t)m_HotButton < buttons.size())
					((NMTTDISPINFOW*)lParam)->lpszText = buttons[m_HotButton].Text.data();
			}
			return 0;
		}
	}
	return ::SendMessage(GetParent(), msg, wParam, lParam);
}

LRESULT CAccordionCtrl::OnCtlColor(UINT msg, WPARAM wParam, LPARAM lParam, BOOL&) {
	// The parent colors writable edits, list boxes and the rest (WTLHelper's dark mode does it there). A static or
	// a read-only edit is on the accordion's background: a parent passing it to DefWindowProc would give it a
	// dialog's gray, and there's no telling that from a color it chose.
	if (msg != WM_CTLCOLORSTATIC)
		return ::SendMessage(GetParent(), msg, wParam, lParam);
	auto c = GetColors();
	if (!m_hBackBrush || m_BackBrushColor != c.crBack) {
		if (m_hBackBrush)
			::DeleteObject(m_hBackBrush);
		m_hBackBrush = ::CreateSolidBrush(c.crBack);
		m_BackBrushColor = c.crBack;
	}
	HDC hdc = (HDC)wParam;
	::SetBkColor(hdc, c.crBack);
	::SetTextColor(hdc, m_SystemColors ? ::GetSysColor(COLOR_WINDOWTEXT) : (IsDark(c.crBack) ? RGB(255, 255, 255) : RGB(0, 0, 0)));
	return (LRESULT)m_hBackBrush;
}

// ---- Painting -------------------------------------------------------------------------------

LRESULT CAccordionCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CAccordionCtrl::OnPaint(UINT msg, WPARAM wParam, LPARAM, BOOL&) {
	PAINTSTRUCT ps{};
	HDC hdc;
	if (msg == WM_PRINTCLIENT) {
		hdc = (HDC)wParam;
		GetClientRect(&ps.rcPaint);
	}
	else {
		hdc = BeginPaint(&ps);
	}
	{
		WTL::CMemoryDC mdc(hdc, ps.rcPaint);
		auto c = GetColors();
		FillSolid(mdc, ps.rcPaint, c.crBack);
		for (size_t i = 0; i < m_Layout->items.size(); i++) {
			RECT clip;
			if (::IntersectRect(&clip, &m_Layout->items[i].header, &ps.rcPaint))
				PaintHeader(mdc, i, c);
		}
		if (int y = DropLineY(); y != INT_MIN) {
			RECT rc;
			GetClientRect(&rc);
			int thick = std::max(3, Scale(3));
			y = std::clamp(y - thick / 2, 0, (int)rc.bottom - thick);
			FillSolid(mdc, { rc.left, y, rc.right, y + thick }, ::GetSysColor(COLOR_HIGHLIGHT));
		}
	}
	if (msg != WM_PRINTCLIENT)
		EndPaint(&ps);
	return 0;
}

void CAccordionCtrl::PaintHeader(HDC hdc, size_t index, const ACDCOLORS& c) {
	const auto& node = *m_Nodes[index];
	const auto& section = m_Sections[index];
	RECT rc = m_Layout->items[index].header;
	bool hot = (int)index == m_Hot && m_HotButton < 0 && section.collapsible;
	bool lifted = m_Drag == Drag::Reorder && index == m_DragIndex;   // its image is with the mouse
	FillSolid(hdc, rc, lifted ? Blend(c.crHeaderBack, c.crHeaderText, 16) : hot ? Blend(c.crHeaderBack, c.crHeaderText, 6) : c.crHeaderBack);
	// a line above (but on the first) and below
	if (index > 0)
		FillSolid(hdc, { rc.left, rc.top, rc.right, rc.top + 1 }, c.crBorder);
	FillSolid(hdc, { rc.left, rc.bottom - 1, rc.right, rc.bottom }, c.crBorder);

	int pad = Scale(8), x = rc.left + pad, right = rc.right - pad;
	if (section.collapsible) {
		int box = Scale(12);
		Chevron(hdc, { x, rc.top, x + box, rc.bottom }, section.expanded, std::max(3, Scale(4)), std::max(1, Scale(3) / 2),
			c.crHeaderText);
		x += box + Scale(6);
	}
	if (m_hImageList && node.Image >= 0) {
		int cx, cy;
		::ImageList_GetIconSize(m_hImageList, &cx, &cy);
		::ImageList_Draw(m_hImageList, node.Image, hdc, x, (rc.top + rc.bottom - cy) / 2, ILD_TRANSPARENT);
		x += cx + Scale(6);
	}
	::SetBkMode(hdc, TRANSPARENT);
	auto old = ::SelectObject(hdc, m_hFont);
	// the buttons, at the right
	auto buttons = ButtonRects(index);
	for (size_t b = 0; b < buttons.size(); b++) {
		const RECT& br = buttons[b];
		if (br.right <= br.left)
			continue;
		right = std::min<int>(right, br.left - Scale(4));
		if ((int)index == m_Hot && (int)b == m_HotButton)
			FillSolid(hdc, br, Blend(c.crHeaderBack, c.crHeaderText, m_Pressed == (int)b ? 24 : 12));
		const auto& button = node.Buttons[b];
		if (m_hImageList && button.Image >= 0) {
			int cx, cy;
			::ImageList_GetIconSize(m_hImageList, &cx, &cy);
			::ImageList_Draw(m_hImageList, button.Image, hdc, (br.left + br.right - cx) / 2, (br.top + br.bottom - cy) / 2, ILD_TRANSPARENT);
		}
		else {
			RECT text = br;
			::SetTextColor(hdc, c.crHeaderText);
			::DrawTextW(hdc, button.Text.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
		}
	}
	if (!node.Info.empty()) {
		RECT measure{};
		::DrawTextW(hdc, node.Info.c_str(), -1, &measure, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
		// the title gets at least half; the info text gives way
		int width = std::min<int>(measure.right, std::max(0, (right - x) / 2));
		RECT info{ right - width, rc.top, right, rc.bottom };
		::SetTextColor(hdc, c.crInfoText);
		::DrawTextW(hdc, node.Info.c_str(), -1, &info, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX | DT_END_ELLIPSIS);
		right = info.left - pad;
	}
	::SelectObject(hdc, m_hTitleFont);
	RECT title{ x, rc.top, right, rc.bottom };
	::SetTextColor(hdc, c.crHeaderText);
	::DrawTextW(hdc, node.Text.c_str(), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	::SelectObject(hdc, old);

	// the focus cue, unless the keyboard hasn't been used (UISF_HIDEFOCUS)
	if ((int)index == m_Focus && HeadersFocused() && !(m_FocusWindow->SendMessage(WM_QUERYUISTATE) & UISF_HIDEFOCUS)) {
		RECT focus = rc;
		::InflateRect(&focus, -Scale(2), -Scale(2));
		::SetTextColor(hdc, c.crHeaderText);
		::SetBkColor(hdc, c.crHeaderBack);
		::DrawFocusRect(hdc, &focus);
	}
}

std::vector<RECT> CAccordionCtrl::ButtonRects(size_t index) const {
	if (index >= m_Nodes.size() || index >= m_Layout->items.size() || m_Nodes[index]->Buttons.empty())
		return {};
	const RECT& rc = m_Layout->items[index].header;
	// room for the chevron, the icon and some of the title before them
	int left = rc.left + Scale(8) + (m_Sections[index].collapsible ? Scale(18) : 0) + Scale(48);
	if (m_hImageList && m_Nodes[index]->Image >= 0) {
		int cx, cy;
		::ImageList_GetIconSize(m_hImageList, &cx, &cy);
		left += cx + Scale(6);
	}
	return AcdButtonRects(rc, (int)m_Nodes[index]->Buttons.size(), m_HeaderHeight - Scale(8), Scale(2), rc.right - Scale(4), left);
}

int CAccordionCtrl::ButtonAt(size_t index, POINT pt) const {
	auto rects = ButtonRects(index);
	for (size_t b = 0; b < rects.size(); b++) {
		if (::PtInRect(&rects[b], pt))
			return (int)b;
	}
	return -1;
}

void CAccordionCtrl::InvalidateHeader(int index) {
	if (index >= 0 && (size_t)index < m_Layout->items.size())
		InvalidateRect(&m_Layout->items[index].header, FALSE);
}

// ---- Mouse ----------------------------------------------------------------------------------

int CAccordionCtrl::EdgeSize() const {
	return (GetStyle() & ACDS_NORESIZE) ? -1 : Scale(4);
}

LRESULT CAccordionCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	if (m_Drag != Drag::None)
		return 0;
	auto hit = AcdHitTest(*m_Layout, m_Sections, pt, EdgeSize());
	if (hit.part == AcdPart::ResizeEdge) {
		// the edge (a body covers its own bottom, so it's the top of the header below): resizing; Escape goes
		// back, so the keys come to the headers meanwhile
		m_Drag = Drag::Resize;
		m_DragIndex = hit.index;
		m_DragStart = pt;
		const RECT& body = m_Layout->items[hit.index].body;
		m_DragHeight = body.bottom - body.top;
		*m_DragLayout = *m_Layout;
		m_DragSaved = m_Sections;
		if (!HeadersFocused())
			m_FocusWindow->SetFocus();
		SetCapture();
		UpdateComposited();   // the bodies resize with every move: painted together, they don't flicker
		return 0;
	}
	if (hit.part != AcdPart::Header)
		return 0;
	FocusHeader(hit.index);
	int button = ButtonAt(hit.index, pt);
	if (button >= 0) {
		// clicked when it's let go over it
		SetHot((int)hit.index, button);
		m_Pressed = button;
		SetCapture();
		InvalidateHeader(m_Hot);
		return 0;
	}
	// a click when it's let go (toggling), or a move once it's dragged
	m_Drag = Drag::Pending;
	m_DragIndex = hit.index;
	m_DragStart = pt;
	m_DragGrab = pt.y - m_Layout->items[hit.index].header.top;
	SetCapture();
	return 0;
}

LRESULT CAccordionCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != Drag::None) {
		Drag drag = m_Drag;
		size_t index = m_DragIndex;
		EndDrag(false);
		if (drag == Drag::Pending && index < m_Sections.size() && m_Sections[index].collapsible)
			UserToggle(index, !m_Sections[index].expanded);
		return 0;
	}
	if (m_Pressed < 0)
		return 0;
	int pressed = m_Pressed, index = m_Hot;
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	bool over = index >= 0 && ButtonAt(index, pt) == pressed;
	m_Pressed = -1;
	ReleaseCapture();
	InvalidateHeader(index);
	if (over) {
		NMACDBUTTON nm{};
		nm.idButton = m_Nodes[index]->Buttons[pressed].Id;
		nm.rcButton = ButtonRects(index)[pressed];
		Notify(nm.section, ACDN_BUTTONCLICK, index);
	}
	return 0;
}

LRESULT CAccordionCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	// the capture taken away (another window, Alt+Tab): the drag is called off
	if (m_Drag != Drag::None)
		EndDrag(true);
	if (m_Pressed >= 0) {
		m_Pressed = -1;
		InvalidateHeader(m_Hot);
	}
	return 0;
}

LRESULT CAccordionCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (!m_Tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_Tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	switch (m_Drag) {
		case Drag::Pending:
			// far enough (and there's somewhere to go): a reorder
			if (!(GetStyle() & ACDS_NOREORDER) && m_Sections.size() > 1 && std::abs(pt.y - m_DragStart.y) > ::GetSystemMetricsForDpi(SM_CYDRAG, m_Dpi)) {
				SetHot(-1, -1);
				ShowDragImage();   // before it's drawn lifted: the image is the header as it looks
				m_Drag = Drag::Reorder;
				m_DropIndex = AcdDropIndex(*m_Layout, pt.y);
				MoveDragImage(pt);
				Invalidate(FALSE);
			}
			return 0;

		case Drag::Reorder:
		{
			// past the top or the bottom: scrolls that way
			RECT rc;
			GetClientRect(&rc);
			if (pt.y < 0)
				SetScroll(m_ScrollPos + pt.y);
			else if (pt.y > rc.bottom)
				SetScroll(m_ScrollPos + pt.y - rc.bottom);
			MoveDragImage(pt);
			size_t drop = AcdDropIndex(*m_Layout, pt.y);
			if (drop != m_DropIndex) {
				m_DropIndex = drop;
				Invalidate(FALSE);
			}
			return 0;
		}

		case Drag::Resize:
			AcdResize(m_Sections, m_DragIndex, m_DragHeight + pt.y - m_DragStart.y, *m_DragLayout, AcdMetrics{ m_HeaderHeight, 0, m_Dpi });
			Layout();
			return 0;

		default:
			break;
	}
	if (m_Pressed >= 0) {
		// the pressed button shows pressed only while the mouse is over it
		bool over = ButtonAt(m_Hot, pt) == m_Pressed;
		if (over != (m_HotButton == m_Pressed)) {
			m_HotButton = over ? m_Pressed : -1;
			InvalidateHeader(m_Hot);
		}
		return 0;
	}
	auto hit = AcdHitTest(*m_Layout, m_Sections, pt, -1);
	if (hit.part == AcdPart::Header)
		SetHot((int)hit.index, ButtonAt(hit.index, pt));
	else
		SetHot(-1, -1);
	return 0;
}

LRESULT CAccordionCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tracking = false;
	if (m_Pressed < 0)
		SetHot(-1, -1);
	return 0;
}

LRESULT CAccordionCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT) {
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		if (m_Drag == Drag::Resize || (m_Drag == Drag::None && AcdHitTest(*m_Layout, m_Sections, pt, EdgeSize()).part == AcdPart::ResizeEdge)) {
			::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
			return TRUE;
		}
	}
	bHandled = FALSE;
	return 0;
}

// cancel: a resize goes back to where it started, a reorder doesn't happen
void CAccordionCtrl::EndDrag(bool cancel) {
	Drag drag = m_Drag;
	size_t index = m_DragIndex;
	m_Drag = Drag::None;   // first: releasing the capture comes back through OnCaptureChanged
	if (::GetCapture() == m_hWnd)
		ReleaseCapture();
	HideDragImage();
	UpdateComposited();
	switch (drag) {
		case Drag::Reorder:
		{
			Invalidate(FALSE);   // the line
			if (cancel || index >= m_Sections.size())
				break;
			size_t to = MoveAt(index, m_DropIndex);
			if (to != index) {
				m_Focus = (int)to;
				Layout();
				NMACDSECTION nm{};
				Notify(nm, ACDN_SECTIONMOVED, to);
			}
			break;
		}

		case Drag::Resize:
		{
			bool changed = false;
			for (size_t i = 0; i < m_Sections.size() && i < m_DragSaved.size(); i++) {
				const auto& a = m_Sections[i];
				const auto& b = m_DragSaved[i];
				changed |= a.mode != b.mode || a.height != b.height || a.weight != b.weight;
			}
			if (cancel) {
				m_Sections = m_DragSaved;
				Layout();
			}
			else if (changed && index < m_Sections.size()) {
				NMACDSECTION nm{};
				Notify(nm, ACDN_SECTIONRESIZED, index);
			}
			m_DragSaved.clear();
			break;
		}

		default:
			break;
	}
}

void CAccordionCtrl::ShowDragImage() {
	if (m_DragIndex >= m_Layout->items.size())
		return;
	const RECT header = m_Layout->items[m_DragIndex].header;
	int w = header.right - header.left, h = header.bottom - header.top;
	if (w <= 0 || h <= 0)
		return;
	if (!m_DragImage)
		m_DragImage = std::make_unique<AcdDragImage>();
	if (!m_DragImage->IsWindow()) {
		RECT none{};
		m_DragImage->Create(m_hWnd, none, nullptr, WS_POPUP,
			WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
	}
	// the header drawn into a bitmap, the popup showing it at a constant alpha
	HDC screen = ::GetDC(nullptr);
	HDC mem = ::CreateCompatibleDC(screen);
	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (bitmap) {
		auto old = ::SelectObject(mem, bitmap);
		::SetViewportOrgEx(mem, -header.left, -header.top, nullptr);
		PaintHeader(mem, m_DragIndex, GetColors());
		::SetViewportOrgEx(mem, 0, 0, nullptr);
		// a frame, so it stands out from the headers it passes over
		FillSolid(mem, { 0, 0, w, 1 }, GetColors().crBorder);
		POINT src{}, dst{ header.left, header.top };
		ClientToScreen(&dst);
		SIZE size{ w, h };
		BLENDFUNCTION blend{ AC_SRC_OVER, 0, 210, 0 };
		::UpdateLayeredWindow(*m_DragImage, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
		::SelectObject(mem, old);
		::DeleteObject(bitmap);
	}
	::DeleteDC(mem);
	::ReleaseDC(nullptr, screen);
	m_DragImage->ShowWindow(SW_SHOWNOACTIVATE);
}

// the image where the mouse has the header, kept over the control
void CAccordionCtrl::MoveDragImage(POINT pt) {
	if (!m_DragImage || !m_DragImage->IsWindow() || m_DragIndex >= m_Layout->items.size())
		return;
	RECT rc;
	GetClientRect(&rc);
	const RECT& header = m_Layout->items[m_DragIndex].header;
	int h = header.bottom - header.top;
	POINT dst{ header.left, std::clamp<int>(pt.y - m_DragGrab, 0, std::max<int>(0, rc.bottom - h)) };
	ClientToScreen(&dst);
	m_DragImage->SetWindowPos(HWND_TOPMOST, dst.x, dst.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void CAccordionCtrl::HideDragImage() {
	if (m_DragImage && m_DragImage->IsWindow())
		m_DragImage->DestroyWindow();
}

// Bodies resizing with every mouse move or animation frame each erase and paint on their own, which flickers;
// composited, the control and its children are painted together off screen. Only meanwhile: composited, any
// body's repaint is the whole control's.
void CAccordionCtrl::UpdateComposited() {
	bool on = m_Drag == Drag::Resize || m_Animating;
	if (on == m_Composited || !IsWindow())
		return;
	m_Composited = on;
	if (on) {
		m_AppComposited = (GetExStyle() & WS_EX_COMPOSITED) != 0;
		if (m_AppComposited)
			return;
		ModifyStyleEx(0, WS_EX_COMPOSITED);
	}
	else {
		if (m_AppComposited)
			return;
		ModifyStyleEx(WS_EX_COMPOSITED, 0);
	}
	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

int CAccordionCtrl::DropLineY() const {
	if (m_Drag != Drag::Reorder || m_DropIndex == m_DragIndex || m_DropIndex == m_DragIndex + 1)
		return INT_MIN;   // where it is already
	const auto& items = m_Layout->items;
	if (m_DropIndex < items.size())
		return items[m_DropIndex].header.top;
	if (items.empty())
		return INT_MIN;
	return std::max(items.back().header.bottom, items.back().body.bottom);
}

size_t CAccordionCtrl::MoveAt(size_t from, size_t before) {
	size_t to = AcdMove(m_Sections, from, before);
	if (to != from) {
		auto node = std::move(m_Nodes[from]);
		m_Nodes.erase(m_Nodes.begin() + from);
		m_Nodes.insert(m_Nodes.begin() + to, std::move(node));
	}
	return to;
}

void CAccordionCtrl::SetHot(int index, int button) {
	if (index == m_Hot && button == m_HotButton)
		return;
	InvalidateHeader(m_Hot);
	bool newButton = index != m_Hot || button != m_HotButton;
	m_Hot = index;
	m_HotButton = button;
	InvalidateHeader(m_Hot);
	if (newButton)
		UpdateToolTip();
}

// the tooltip's tool is the hot button (an empty rectangle without one)
void CAccordionCtrl::UpdateToolTip() {
	if (!m_hToolTip)
		return;
	::SendMessage(m_hToolTip, TTM_POP, 0, 0);
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = k_TipTool;
	if (m_Hot >= 0 && m_HotButton >= 0) {
		auto rects = ButtonRects(m_Hot);
		if ((size_t)m_HotButton < rects.size())
			ti.rect = rects[m_HotButton];
	}
	::SendMessage(m_hToolTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

LRESULT CAccordionCtrl::OnContextMenu(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// a body's goes on to the parent
	if ((HWND)wParam != m_hWnd && (HWND)wParam != m_FocusWindow->m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	int index = -1;
	if (pt.x == -1 && pt.y == -1) {
		// the menu key: the focused header, at its bottom left
		if ((size_t)m_Focus < m_Layout->items.size()) {
			index = m_Focus;
			const RECT& header = m_Layout->items[index].header;
			pt = { header.left + Scale(8), header.bottom };
		}
		else {
			pt = {};
		}
		ClientToScreen(&pt);
	}
	else {
		POINT client = pt;
		ScreenToClient(&client);
		auto hit = AcdHitTest(*m_Layout, m_Sections, client, -1);
		if (hit.part == AcdPart::Header)
			index = (int)hit.index;
	}
	ContextMenu(index, pt);
	return 0;
}

bool CAccordionCtrl::ContextMenu(int index, POINT ptScreen) {
	NMACDCONTEXTMENU nm{};
	nm.ptScreen = ptScreen;
	if (index >= 0) {
		Notify(nm.section, ACDN_CONTEXTMENU, index);
		return m_LastNotifyResult != 0;
	}
	nm.section.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), ACDN_CONTEXTMENU };
	return ::SendMessage(GetParent(), WM_NOTIFY, nm.section.hdr.idFrom, (LPARAM)&nm) != 0;
}

// ---- Notifications and changes ------------------------------------------------------------------

void CAccordionCtrl::Notify(NMACDSECTION& nm, UINT code, size_t index) {
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), code };
	nm.hSection = m_Nodes[index].get();
	nm.id = m_Sections[index].id;
	nm.lParam = m_Nodes[index]->Data;
	m_LastNotifyResult = ::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
}

void CAccordionCtrl::UserToggle(size_t index, bool expand) {
	if (index >= m_Sections.size() || m_Sections[index].expanded == expand || (!expand && !m_Sections[index].collapsible))
		return;
	HACDSECTION handle = m_Nodes[index].get();
	NMACDEXPAND nm{};
	nm.expand = expand;
	Notify(nm.section, ACDN_EXPANDING, index);
	if (m_LastNotifyResult)
		return;
	// the parent may have changed the sections
	int i = IndexOf(handle);
	if (i < 0)
		return;
	auto changed = AcdSetExpanded(m_Sections, i, expand, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
	std::vector<HACDSECTION> handles;
	for (auto c : changed)
		handles.push_back(m_Nodes[c].get());
	Changed(changed);
	for (auto h : handles) {
		int k = IndexOf(h);
		if (k < 0)
			continue;
		NMACDEXPAND done{};
		done.expand = m_Sections[k].expanded;
		Notify(done.section, ACDN_EXPANDED, k);
	}
}

void CAccordionCtrl::Changed(const std::vector<size_t>& changed) {
	// the focus in a body that closes goes to its header
	HWND focus = ::GetFocus();
	for (auto i : changed) {
		HWND body = m_Nodes[i]->Body;
		if (!m_Sections[i].expanded && body && focus && (focus == body || ::IsChild(body, focus)))
			FocusHeader(i);
	}
	if (Animates())
		StartAnimation();
	else
		AcdSnap(m_Sections);
	Layout();
}

bool CAccordionCtrl::Animates() const {
	DWORD style = GetStyle();
	if ((style & ACDS_NOANIMATION) || !IsWindowVisible())
		return false;
	if (style & ACDS_ALWAYSANIMATE)
		return true;
	BOOL on = TRUE;
	::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
	return on != FALSE;
}

void CAccordionCtrl::StartAnimation() {
	m_AnimationTick = ::GetTickCount64();
	m_Animating = true;
	UpdateComposited();
	SetTimer(k_AnimationTimer, 10);
}

LRESULT CAccordionCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != k_AnimationTimer) {
		bHandled = FALSE;
		return 0;
	}
	ULONGLONG now = ::GetTickCount64();
	bool moving = AcdAnimate(m_Sections, (double)(now - m_AnimationTick), k_AnimationMs);
	m_AnimationTick = now;
	Layout();
	if (!moving) {
		KillTimer(k_AnimationTimer);
		m_Animating = false;
		UpdateComposited();
	}
	return 0;
}

// ---- The keyboard -----------------------------------------------------------------------------

bool CAccordionCtrl::HeadersFocused() const {
	return m_FocusWindow->IsWindow() && ::GetFocus() == m_FocusWindow->m_hWnd;
}

void CAccordionCtrl::FocusHeader(size_t index) {
	if (index >= m_Nodes.size())
		return;
	InvalidateHeader(m_Focus);
	m_Focus = (int)index;
	InvalidateHeader(m_Focus);
	EnsureVisible(m_Nodes[index].get());
	if (!HeadersFocused())
		m_FocusWindow->SetFocus();
}

HACDSECTION CAccordionCtrl::GetFocusSection() const {
	return (size_t)m_Focus < m_Nodes.size() ? m_Nodes[m_Focus].get() : nullptr;
}

void CAccordionCtrl::SetFocusSection(HACDSECTION hSection) {
	int i = IndexOf(hSection);
	if (i < 0)
		return;
	InvalidateHeader(m_Focus);
	m_Focus = i;
	InvalidateHeader(m_Focus);
	EnsureVisible(hSection);
}

HWND CAccordionCtrl::GetHeaderFocusWindow() const {
	return m_FocusWindow->m_hWnd;
}

LRESULT CAccordionCtrl::OnHeaderFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (!m_Nodes.empty())
		m_Focus = std::clamp(m_Focus, 0, (int)m_Nodes.size() - 1);
	InvalidateHeader(m_Focus);
	return 0;
}

LRESULT CAccordionCtrl::OnHeaderUpdateUIState(UINT msg, WPARAM wParam, LPARAM lParam, BOOL&) {
	LRESULT result = m_FocusWindow->DefWindowProc(msg, wParam, lParam);
	InvalidateHeader(m_Focus);   // the focus cue shown or hidden
	return result;
}

LRESULT CAccordionCtrl::OnHeaderGetDlgCode(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Drag != Drag::None)
		return DLGC_WANTALLKEYS;   // Escape
	LRESULT code = DLGC_WANTARROWS | DLGC_WANTCHARS;
	// Enter toggles here rather than pressing the dialog's default button
	auto msg = (const MSG*)lParam;
	if (msg && msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
		code |= DLGC_WANTMESSAGE;
	return code;
}

LRESULT CAccordionCtrl::OnHeaderKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (m_Drag != Drag::None) {
		if (wParam == VK_ESCAPE)
			EndDrag(true);
		return 0;
	}
	int n = (int)m_Nodes.size();
	if (n == 0) {
		bHandled = FALSE;
		return 0;
	}
	int focus = std::clamp(m_Focus, 0, n - 1);
	bool collapsible = m_Sections[focus].collapsible;
	switch (wParam) {
		case VK_UP:     FocusHeader(std::max(0, focus - 1)); break;
		case VK_DOWN:   FocusHeader(std::min(n - 1, focus + 1)); break;
		case VK_HOME:   FocusHeader(0); break;
		case VK_END:    FocusHeader(n - 1); break;
		case VK_SPACE:
		case VK_RETURN:
			if (collapsible)
				UserToggle(focus, !m_Sections[focus].expanded);
			break;
		case VK_RIGHT:
		case VK_ADD:
		case VK_OEM_PLUS:
			UserToggle(focus, true);
			break;
		case VK_LEFT:
		case VK_SUBTRACT:
		case VK_OEM_MINUS:
			UserToggle(focus, false);
			break;
		default:
			bHandled = FALSE;
			break;
	}
	return 0;
}

LRESULT CAccordionCtrl::OnHeaderChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// the keys handled on WM_KEYDOWN: no beep
	if (wParam == L' ' || wParam == L'+' || wParam == L'-' || wParam == L'\r' || wParam == L'=')
		return 0;
	bHandled = FALSE;
	return 0;
}

// ---- Sections -------------------------------------------------------------------------------

HACDSECTION CAccordionCtrl::InsertSection(const ACDSECTION* section, HACDSECTION insertAfter) {
	if (!section)
		return nullptr;
	size_t index;
	if (insertAfter == ACD_FIRST) {
		index = 0;
	}
	else if (insertAfter == ACD_LAST || !insertAfter) {
		index = m_Nodes.size();
	}
	else {
		int after = IndexOf(insertAfter);
		if (after < 0)
			return nullptr;
		index = after + 1;
	}

	auto node = std::make_unique<AcdNode>();
	node->Text = section->pszText ? section->pszText : L"";
	node->Info = section->pszInfo ? section->pszInfo : L"";
	node->Image = section->iImage;
	node->Data = section->lParam;

	AcdSection s;
	s.id = section->id;
	if (s.id == 0 || FindSection(s.id)) {
		// one that isn't taken (the saved state finds sections by id)
		while (FindSection(m_NextId))
			m_NextId++;
		s.id = m_NextId++;
	}
	s.mode = section->heightMode == ACDH_FILL ? AcdHeight::Fill : section->heightMode == ACDH_FIT ? AcdHeight::Fit : AcdHeight::Fixed;
	s.height = section->height > 0 ? section->height : 120;
	s.weight = section->weight > 0 ? section->weight : 1;
	s.minHeight = std::max(0, section->minHeight);
	s.collapsible = !(section->state & ACDSS_NOCOLLAPSE);
	s.expanded = (section->state & ACDSS_EXPANDED) || !s.collapsible;

	HACDSECTION handle = node.get();
	m_Nodes.insert(m_Nodes.begin() + index, std::move(node));
	m_Sections.insert(m_Sections.begin() + index, s);
	SetHot(-1, -1);
	if ((int)index <= m_Focus && m_Nodes.size() > 1)
		m_Focus++;
	// a new section is there at once; the ones it closes (ACDS_SINGLEEXPAND) go as usual
	m_Sections[index].progress = s.expanded ? 1 : 0;
	if (s.expanded) {
		auto changed = AcdSetExpanded(m_Sections, index, true, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
		if (!changed.empty())
			Changed(changed);
	}
	if (s.mode == AcdHeight::Fit)
		m_FitWidth = -1;
	SetSectionWindow(handle, section->hwndBody);
	Layout();
	return handle;
}

BOOL CAccordionCtrl::DeleteSection(HACDSECTION hSection) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	HWND focus = ::GetFocus();
	bool hadFocus = false;
	if (HWND body = m_Nodes[i]->Body; body && ::IsWindow(body)) {
		hadFocus = focus && (focus == body || ::IsChild(body, focus));
		if (m_Nodes[i]->Clipped)
			::SetWindowRgn(body, nullptr, FALSE);
		::ShowWindow(body, SW_HIDE);
	}
	m_Nodes.erase(m_Nodes.begin() + i);
	m_Sections.erase(m_Sections.begin() + i);
	SetHot(-1, -1);
	if (m_Focus > i || m_Focus >= (int)m_Nodes.size())
		m_Focus = std::max(0, m_Focus - 1);
	Layout();
	if (hadFocus)
		m_FocusWindow->SetFocus();
	return TRUE;
}

void CAccordionCtrl::DeleteAllSections() {
	for (auto& node : m_Nodes) {
		if (node->Body && ::IsWindow(node->Body)) {
			if (node->Clipped)
				::SetWindowRgn(node->Body, nullptr, FALSE);
			::ShowWindow(node->Body, SW_HIDE);
		}
	}
	m_Nodes.clear();
	m_Sections.clear();
	m_ScrollPos = 0;
	m_Focus = 0;
	SetHot(-1, -1);
	Layout();
}

BOOL CAccordionCtrl::MoveSection(HACDSECTION hSection, HACDSECTION insertAfter) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	size_t before;
	if (insertAfter == ACD_FIRST) {
		before = 0;
	}
	else if (insertAfter == ACD_LAST || !insertAfter) {
		before = m_Nodes.size();
	}
	else {
		int after = IndexOf(insertAfter);
		if (after < 0)
			return FALSE;
		before = after + 1;
	}
	HACDSECTION focus = GetFocusSection();
	MoveAt(i, before);
	if (focus)
		m_Focus = IndexOf(focus);
	SetHot(-1, -1);
	Layout();
	return TRUE;
}

int CAccordionCtrl::GetState(LPWSTR text, int cch) const {
	auto state = AcdSaveState(m_Sections);
	if (text && cch > 0)
		wcsncpy_s(text, cch, state.c_str(), _TRUNCATE);
	return (int)state.size() + 1;
}

BOOL CAccordionCtrl::SetState(LPCWSTR text) {
	if (!text)
		return FALSE;
	auto sections = m_Sections;
	if (!AcdLoadState(sections, text))
		return FALSE;
	// the nodes in the sections' new order (the ids are unique)
	std::vector<std::unique_ptr<AcdNode>> nodes;
	nodes.reserve(m_Nodes.size());
	for (const auto& s : sections) {
		for (size_t i = 0; i < m_Sections.size(); i++) {
			if (m_Sections[i].id == s.id && m_Nodes[i]) {
				nodes.push_back(std::move(m_Nodes[i]));
				break;
			}
		}
	}
	HACDSECTION focus = GetFocusSection();
	m_Nodes = std::move(nodes);
	m_Sections = std::move(sections);
	KillTimer(k_AnimationTimer);
	m_Animating = false;
	UpdateComposited();
	AcdSnap(m_Sections);
	if (focus)
		m_Focus = std::max(0, IndexOf(focus));
	SetHot(-1, -1);
	m_FitWidth = -1;
	Layout();
	// the focus in a body that closed goes to its header
	HWND hasFocus = ::GetFocus();
	for (size_t i = 0; i < m_Nodes.size(); i++) {
		HWND body = m_Nodes[i]->Body;
		if (!m_Sections[i].expanded && body && hasFocus && (hasFocus == body || ::IsChild(body, hasFocus)))
			FocusHeader(i);
	}
	return TRUE;
}

int CAccordionCtrl::GetSectionCount() const {
	return (int)m_Nodes.size();
}

HACDSECTION CAccordionCtrl::GetSection(int index) const {
	return index >= 0 && (size_t)index < m_Nodes.size() ? m_Nodes[index].get() : nullptr;
}

int CAccordionCtrl::GetSectionIndex(HACDSECTION hSection) const {
	return IndexOf(hSection);
}

HACDSECTION CAccordionCtrl::FindSection(UINT id) const {
	for (size_t i = 0; i < m_Sections.size(); i++) {
		if (m_Sections[i].id == id)
			return m_Nodes[i].get();
	}
	return nullptr;
}

BOOL CAccordionCtrl::GetSectionInfo(HACDSECTION hSection, ACDSECTION* section) const {
	int i = IndexOf(hSection);
	if (i < 0 || !section)
		return FALSE;
	const auto& node = *m_Nodes[i];
	const auto& s = m_Sections[i];
	section->id = s.id;
	section->pszText = node.Text.c_str();
	section->pszInfo = node.Info.c_str();
	section->iImage = node.Image;
	section->hwndBody = node.Body;
	section->heightMode = s.mode == AcdHeight::Fill ? ACDH_FILL : s.mode == AcdHeight::Fit ? ACDH_FIT : ACDH_FIXED;
	section->height = s.height;
	section->weight = s.weight;
	section->minHeight = s.minHeight;
	section->state = (s.expanded ? ACDSS_EXPANDED : 0) | (s.collapsible ? 0 : ACDSS_NOCOLLAPSE);
	section->lParam = node.Data;
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionText(HACDSECTION hSection, LPCWSTR text) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Nodes[i]->Text = text ? text : L"";
	if ((size_t)i < m_Layout->items.size())
		InvalidateRect(&m_Layout->items[i].header, FALSE);
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionInfoText(HACDSECTION hSection, LPCWSTR text) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Nodes[i]->Info = text ? text : L"";
	if ((size_t)i < m_Layout->items.size())
		InvalidateRect(&m_Layout->items[i].header, FALSE);
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionImage(HACDSECTION hSection, int image) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Nodes[i]->Image = image;
	if ((size_t)i < m_Layout->items.size())
		InvalidateRect(&m_Layout->items[i].header, FALSE);
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionWindow(HACDSECTION hSection, HWND hwndBody) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	auto& node = *m_Nodes[i];
	if (node.Body == hwndBody)
		return TRUE;
	if (node.Body && ::IsWindow(node.Body))
		::ShowWindow(node.Body, SW_HIDE);
	node.Body = hwndBody;
	if (hwndBody && ::IsWindow(hwndBody)) {
		::ShowWindow(hwndBody, SW_HIDE);   // until it's placed
		if (::GetParent(hwndBody) != m_hWnd) {
			::SetWindowLongPtrW(hwndBody, GWL_STYLE, (::GetWindowLongPtrW(hwndBody, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
			::SetParent(hwndBody, m_hWnd);
		}
	}
	if (m_Sections[i].mode == AcdHeight::Fit)
		m_FitWidth = -1;
	Layout();
	return TRUE;
}

HWND CAccordionCtrl::GetSectionWindow(HACDSECTION hSection) const {
	int i = IndexOf(hSection);
	return i < 0 ? nullptr : m_Nodes[i]->Body;
}

BOOL CAccordionCtrl::SetSectionHeight(HACDSECTION hSection, UINT heightMode, int height, float weight) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	auto& s = m_Sections[i];
	s.mode = heightMode == ACDH_FILL ? AcdHeight::Fill : heightMode == ACDH_FIT ? AcdHeight::Fit : AcdHeight::Fixed;
	if (height > 0)
		s.height = height;
	if (weight > 0)
		s.weight = weight;
	if (s.mode == AcdHeight::Fit)
		m_FitWidth = -1;
	Layout();
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionMinHeight(HACDSECTION hSection, int minHeight) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Sections[i].minHeight = std::max(0, minHeight);
	Layout();
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionData(HACDSECTION hSection, LPARAM lParam) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Nodes[i]->Data = lParam;
	return TRUE;
}

LPARAM CAccordionCtrl::GetSectionData(HACDSECTION hSection) const {
	int i = IndexOf(hSection);
	return i < 0 ? 0 : m_Nodes[i]->Data;
}

BOOL CAccordionCtrl::SetSectionCollapsible(HACDSECTION hSection, bool collapsible) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	m_Sections[i].collapsible = collapsible;
	if (!collapsible && !m_Sections[i].expanded) {
		m_Sections[i].expanded = true;
		Changed({ (size_t)i });
	}
	else {
		Layout();
	}
	return TRUE;
}

BOOL CAccordionCtrl::SetSectionButtons(HACDSECTION hSection, const ACDBUTTON* buttons, int count) {
	int i = IndexOf(hSection);
	if (i < 0 || (count > 0 && !buttons))
		return FALSE;
	auto& list = m_Nodes[i]->Buttons;
	list.clear();
	list.reserve(std::max(0, count));
	for (int b = 0; b < count; b++)
		list.push_back({ buttons[b].id, buttons[b].iImage, buttons[b].pszText ? buttons[b].pszText : L"" });
	if (i == m_Hot)
		SetHot(-1, -1);
	InvalidateHeader(i);
	return TRUE;
}

BOOL CAccordionCtrl::Expand(HACDSECTION hSection, bool expand) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	auto changed = AcdSetExpanded(m_Sections, i, expand, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
	if (!changed.empty())
		Changed(changed);
	return TRUE;
}

bool CAccordionCtrl::IsExpanded(HACDSECTION hSection) const {
	int i = IndexOf(hSection);
	return i >= 0 && m_Sections[i].expanded;
}

void CAccordionCtrl::ExpandAll(bool expand) {
	bool single = (GetStyle() & ACDS_SINGLEEXPAND) != 0;
	std::vector<size_t> changed;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		if (!m_Sections[i].collapsible)
			continue;
		for (auto c : AcdSetExpanded(m_Sections, i, expand, single)) {
			if (std::find(changed.begin(), changed.end(), c) == changed.end())
				changed.push_back(c);
		}
		if (expand && single)
			break;
	}
	if (!changed.empty())
		Changed(changed);
}

void CAccordionCtrl::UpdateFitHeight(HACDSECTION) {
	// one or all: asking is cheap, and the layout needs every height anyway
	m_FitWidth = -1;
	Layout();
}

BOOL CAccordionCtrl::GetSectionRect(HACDSECTION hSection, RECT* pHeader, RECT* pBody) const {
	int i = IndexOf(hSection);
	if (i < 0 || (size_t)i >= m_Layout->items.size())
		return FALSE;
	if (pHeader)
		*pHeader = m_Layout->items[i].header;
	if (pBody)
		*pBody = m_Layout->items[i].body;
	return TRUE;
}

// ---- Looks ----------------------------------------------------------------------------------

HIMAGELIST CAccordionCtrl::SetImageList(HIMAGELIST hImageList) {
	HIMAGELIST old = m_hImageList;
	m_hImageList = hImageList;
	if (IsWindow()) {
		UpdateFont();   // the header height
		Layout();
	}
	return old;
}

HIMAGELIST CAccordionCtrl::GetImageList() const {
	return m_hImageList;
}

void CAccordionCtrl::SetColors(const ACDCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

ACDCOLORS CAccordionCtrl::GetColors() const {
	return m_SystemColors ? SystemColors() : m_Colors;
}
