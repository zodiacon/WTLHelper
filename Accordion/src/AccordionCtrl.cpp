#include "Accordion.h"
#include "AccordionModel.h"

#include <windowsx.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// a section's side the model doesn't need: what the header shows, and the body window
struct AcdNode {
	std::wstring Text;
	std::wstring Info;
	int          Image = -1;
	HWND         Body = nullptr;
	LPARAM       Data = 0;
};

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

CAccordionCtrl::CAccordionCtrl() : m_Layout(std::make_unique<AcdLayout>()) {
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
	UpdateFont();
	Layout();
	return 0;
}

LRESULT CAccordionCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	bHandled = FALSE;
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
				AcdSetExpanded(m_Sections, i, true, true);
				break;
			}
		}
		AcdSnap(m_Sections);
		Layout();
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
	auto defer = ::BeginDeferWindowPos((int)m_Nodes.size());
	for (size_t i = 0; i < m_Nodes.size() && i < m_Layout->items.size(); i++) {
		HWND body = m_Nodes[i]->Body;
		if (!body || !::IsWindow(body))
			continue;
		const RECT& rc = m_Layout->items[i].body;
		bool show = rc.bottom > rc.top;
		UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | (show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
		if (defer)
			defer = ::DeferWindowPos(defer, body, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, flags);
		else
			::SetWindowPos(body, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, flags);
	}
	if (defer)
		::EndDeferWindowPos(defer);
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
	}
	if (msg != WM_PRINTCLIENT)
		EndPaint(&ps);
	return 0;
}

void CAccordionCtrl::PaintHeader(HDC hdc, size_t index, const ACDCOLORS& c) {
	const auto& node = *m_Nodes[index];
	const auto& section = m_Sections[index];
	RECT rc = m_Layout->items[index].header;
	FillSolid(hdc, rc, c.crHeaderBack);
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
}

// ---- Mouse ----------------------------------------------------------------------------------

LRESULT CAccordionCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	auto hit = AcdHitTest(*m_Layout, m_Sections, pt, -1);
	if (hit.part == AcdPart::Header && m_Sections[hit.index].collapsible)
		Toggle(hit.index);
	return 0;
}

void CAccordionCtrl::Toggle(size_t index) {
	AcdSetExpanded(m_Sections, index, !m_Sections[index].expanded, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
	AcdSnap(m_Sections);
	Layout();
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
	if (s.expanded)
		AcdSetExpanded(m_Sections, index, true, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
	AcdSnap(m_Sections);
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
	if (HWND body = m_Nodes[i]->Body; body && ::IsWindow(body))
		::ShowWindow(body, SW_HIDE);
	m_Nodes.erase(m_Nodes.begin() + i);
	m_Sections.erase(m_Sections.begin() + i);
	Layout();
	return TRUE;
}

void CAccordionCtrl::DeleteAllSections() {
	for (auto& node : m_Nodes) {
		if (node->Body && ::IsWindow(node->Body))
			::ShowWindow(node->Body, SW_HIDE);
	}
	m_Nodes.clear();
	m_Sections.clear();
	m_ScrollPos = 0;
	Layout();
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
	if (!collapsible)
		m_Sections[i].expanded = true;
	AcdSnap(m_Sections);
	Layout();
	return TRUE;
}

BOOL CAccordionCtrl::Expand(HACDSECTION hSection, bool expand) {
	int i = IndexOf(hSection);
	if (i < 0)
		return FALSE;
	AcdSetExpanded(m_Sections, i, expand, (GetStyle() & ACDS_SINGLEEXPAND) != 0);
	AcdSnap(m_Sections);
	Layout();
	return TRUE;
}

bool CAccordionCtrl::IsExpanded(HACDSECTION hSection) const {
	int i = IndexOf(hSection);
	return i >= 0 && m_Sections[i].expanded;
}

void CAccordionCtrl::ExpandAll(bool expand) {
	bool single = (GetStyle() & ACDS_SINGLEEXPAND) != 0;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		if (!m_Sections[i].collapsible)
			continue;
		AcdSetExpanded(m_Sections, i, expand, single);
		if (expand && single)
			break;
	}
	AcdSnap(m_Sections);
	Layout();
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
