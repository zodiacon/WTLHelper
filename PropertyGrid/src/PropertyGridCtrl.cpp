#include "PropertyGrid.h"
#include "PropertyGridModel.h"

#include <algorithm>
#include <cmath>

// ---- Helpers -----------------------------------------------------------------------

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static bool HasShownChildren(const PgNode* node) {
	return std::any_of(node->children.begin(), node->children.end(), [](auto& c) { return (c->flags & PRGF_HIDDEN) == 0; });
}

// ---- Construction ------------------------------------------------------------------

CPropertyGridCtrl::CPropertyGridCtrl() : m_Model(std::make_unique<PgModel>()) {
}

CPropertyGridCtrl::~CPropertyGridCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

PgNode* CPropertyGridCtrl::Node(HPRGITEM hItem) const {
	return hItem;
}

LRESULT CPropertyGridCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	Rebuild();
	return 0;
}

LRESULT CPropertyGridCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Drag = Drag::None;
	bHandled = FALSE;
	return 0;
}

// ---- Metrics and fonts -------------------------------------------------------------

int CPropertyGridCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CPropertyGridCtrl::UpdateFonts() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	lf.lfWeight = FW_BOLD;
	if (!m_BoldFont.IsNull())
		m_BoldFont.DeleteObject();
	m_BoldFont.CreateFontIndirect(&lf);
}

void CPropertyGridCtrl::UpdateMetrics() {
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	dc.SelectFont(old);

	m_RowHeight = tm.tmHeight + Scale(4);
	m_Margin = Scale(16);
	m_Indent = Scale(14);
	m_Pad = Scale(4);
	m_SplitterGrip = Scale(3);
	m_DescSplitterHeight = Scale(5);
}

LRESULT CPropertyGridCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	UpdateMetrics();
	UpdateScrollBar();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CPropertyGridCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CPropertyGridCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
	// a font set with WM_SETFONT is the app's to replace for the new DPI
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	Layout();
	return 0;
}

LRESULT CPropertyGridCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS && m_OwnFont) {
		UpdateFonts();
		UpdateMetrics();
		Layout();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CPropertyGridCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

// ---- Layout -------------------------------------------------------------------------

RECT CPropertyGridCtrl::GetDescRect() const {
	RECT rc;
	GetClientRect(&rc);
	if (!m_ShowDesc)
		return { 0, rc.bottom, rc.right, rc.bottom };
	// keep a couple of rows for the grid
	int height = std::max(0, std::min(Scale(m_DescHeightDips), (int)rc.bottom - 2 * m_RowHeight - m_DescSplitterHeight));
	return { 0, rc.bottom - height, rc.right, rc.bottom };
}

RECT CPropertyGridCtrl::GetGridRect() const {
	RECT rc;
	GetClientRect(&rc);
	if (m_ShowDesc)
		rc.bottom = std::max(rc.top, GetDescRect().top - m_DescSplitterHeight);
	return rc;
}

int CPropertyGridCtrl::GetSplitX() const {
	auto grid = GetGridRect();
	int width = grid.right - m_Margin;
	int x = m_Margin + (int)(width * m_SplitFraction);
	return std::max(m_Margin + Scale(20), std::min(x, (int)grid.right - Scale(20)));
}

int CPropertyGridCtrl::GetPageRows() const {
	auto grid = GetGridRect();
	return std::max(1, (int)(grid.bottom - grid.top) / m_RowHeight);
}

void CPropertyGridCtrl::UpdateScrollBar() {
	if (!IsWindow())
		return;
	int page = GetPageRows();
	m_TopRow = std::max(0, std::min(m_TopRow, (int)m_Rows.size() - page));
	SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
	si.nMax = (int)m_Rows.size() - 1;
	si.nPage = page;
	si.nPos = m_TopRow;
	SetScrollInfo(SB_VERT, &si, TRUE);
}

void CPropertyGridCtrl::Layout() {
	UpdateScrollBar();
	Invalidate();
}

void CPropertyGridCtrl::ScrollTo(int topRow) {
	int old = m_TopRow;
	m_TopRow = topRow;
	UpdateScrollBar();
	if (m_TopRow != old)
		Invalidate();
}

LRESULT CPropertyGridCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	Layout();
	return 0;
}

// ---- Rows ---------------------------------------------------------------------------

void CPropertyGridCtrl::Rebuild() {
	if (m_UpdateCount > 0) {
		m_RebuildPending = true;
		return;
	}
	m_RebuildPending = false;
	m_Model->BuildRows(m_View, m_Rows);

	if (m_Selected && RowOf(m_Selected) < 0) {
		// hidden by a collapse or a view change: the nearest visible ancestor takes over
		auto node = m_Selected->parent;
		while (node && node != &m_Model->root && RowOf(node) < 0)
			node = node->parent;
		SelectRow(node && node != &m_Model->root ? RowOf(node) : -1);
	}
	if (IsWindow())
		Layout();
}

int CPropertyGridCtrl::RowOf(const PgNode* node) const {
	if (!node)
		return -1;
	auto it = std::find_if(m_Rows.begin(), m_Rows.end(), [=](auto& r) { return r.node == node; });
	return it == m_Rows.end() ? -1 : (int)(it - m_Rows.begin());
}

bool CPropertyGridCtrl::HasVisibleChildren(const PgNode* node) const {
	return node && HasShownChildren(node);
}

void CPropertyGridCtrl::ExpandAncestors(PgNode* node) {
	for (auto p = node ? node->parent : nullptr; p && p != &m_Model->root; p = p->parent)
		p->expanded = true;
}

bool CPropertyGridCtrl::SetExpanded(PgNode* node, bool expand, bool notify) {
	if (!node || node->expanded == expand)
		return false;
	node->expanded = expand;
	Rebuild();
	if (notify) {
		NMPRGEXPAND nm{};
		nm.hItem = node;
		nm.expanded = expand;
		FireNotify(&nm.hdr, PRGN_ITEMEXPANDED);
	}
	return true;
}

void CPropertyGridCtrl::SelectRow(int row, bool notify) {
	auto node = row >= 0 && row < (int)m_Rows.size() ? m_Rows[row].node : nullptr;
	if (node == m_Selected)
		return;
	auto old = m_Selected;
	m_Selected = node;
	if (node)
		EnsureVisible(node);
	if (IsWindow())
		Invalidate();
	if (notify) {
		NMPRGSELCHANGE nm{};
		nm.hItemOld = old;
		nm.hItemNew = node;
		FireNotify(&nm.hdr, PRGN_SELCHANGED);
	}
}

// ---- Painting -----------------------------------------------------------------------

PRGCOLORS CPropertyGridCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read on every paint: the WTLHelper dark mode hooks GetSysColor
	PRGCOLORS c;
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
	c.crCategoryBack = ::GetSysColor(COLOR_3DFACE);
	c.crCategoryText = ::GetSysColor(COLOR_WINDOWTEXT);
	c.crGridLine = ::GetSysColor(COLOR_3DFACE);
	c.crSelBack = ::GetSysColor(COLOR_HIGHLIGHT);
	c.crSelText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	c.crSelBackInactive = Blend(c.crBack, c.crSelBack, 30);
	c.crSelTextInactive = c.crText;
	c.crDisabledText = ::GetSysColor(COLOR_GRAYTEXT);
	c.crDescBack = ::GetSysColor(COLOR_3DFACE);
	c.crDescText = ::GetSysColor(COLOR_WINDOWTEXT);
	return c;
}

void CPropertyGridCtrl::SetColors(const PRGCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

LRESULT CPropertyGridCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CPropertyGridCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		GetClientRect(&rc);
		Paint((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC, dc.m_ps.rcPaint);
	return 0;
}

void CPropertyGridCtrl::Paint(WTL::CDCHandle dc, const RECT& rcPaint) {
	auto c = GetColors();
	auto grid = GetGridRect();
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	FillSolid(dc, grid, c.crBack);
	RECT gutter{ 0, grid.top, m_Margin, grid.bottom };
	FillSolid(dc, gutter, c.crCategoryBack);

	RECT rcRow{ 0, grid.top, grid.right, grid.top + m_RowHeight };
	for (int row = m_TopRow; row < (int)m_Rows.size() && rcRow.top < grid.bottom; row++) {
		RECT dummy;
		if (::IntersectRect(&dummy, &rcRow, &rcPaint))
			PaintRow(dc, row, rcRow, c);
		::OffsetRect(&rcRow, 0, m_RowHeight);
	}

	if (m_ShowDesc) {
		auto desc = GetDescRect();
		RECT band{ 0, grid.bottom, grid.right, desc.top };
		FillSolid(dc, band, c.crCategoryBack);
		PaintDescription(dc, desc, c);
	}
	dc.SelectFont(oldFont);
}

int CPropertyGridCtrl::GlyphCenter(const PgRow& row) const {
	// top-level rows use the left margin; deeper ones the indent slot left of the name
	return row.level == 0 ? m_Margin / 2 : m_Margin + (row.level - 1) * m_Indent + m_Indent / 2;
}

int CPropertyGridCtrl::TextLeft(const PgRow& row) const {
	return m_Margin + row.level * m_Indent + m_Pad;
}

void CPropertyGridCtrl::PaintRow(WTL::CDCHandle dc, int row, const RECT& rc, const PRGCOLORS& c) {
	auto& r = m_Rows[row];
	auto node = r.node;
	bool selected = node == m_Selected;
	bool focused = ::GetFocus() == m_hWnd;
	auto selBack = focused ? c.crSelBack : c.crSelBackInactive;
	auto selText = focused ? c.crSelText : c.crSelTextInactive;
	const UINT textFormat = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;

	if (node->IsCategory()) {
		FillSolid(dc, rc, c.crCategoryBack);
		RECT rcText{ TextLeft(r), rc.top, rc.right - m_Pad, rc.bottom };
		if (selected) {
			RECT rcSel{ m_Margin, rc.top, rc.right, rc.bottom };
			FillSolid(dc, rcSel, selBack);
		}
		DrawGlyph(dc, GlyphCenter(r), rc, node->expanded, c.crCategoryText);
		auto old = dc.SelectFont(m_BoldFont);
		dc.SetTextColor(selected ? selText : c.crCategoryText);
		dc.DrawText(node->name.c_str(), (int)node->name.size(), &rcText, textFormat);
		dc.SelectFont(old);
		return;
	}

	int splitX = GetSplitX();
	RECT rcName{ m_Margin, rc.top, splitX, rc.bottom - 1 };
	if (selected)
		FillSolid(dc, rcName, selBack);
	if (HasVisibleChildren(node))
		DrawGlyph(dc, GlyphCenter(r), rc, node->expanded, selected && r.level > 0 ? selText : c.crText);
	RECT rcText{ TextLeft(r), rc.top, splitX - m_Pad, rc.bottom - 1 };
	dc.SetTextColor(selected ? selText : c.crText);
	dc.DrawText(node->name.c_str(), (int)node->name.size(), &rcText, textFormat);

	// value
	auto& value = node->value.value;
	bool modified = node->defValue.value.type != PRGVT_EMPTY && !PgValuesEqual(value, node->defValue.value);
	auto textColor = (node->flags & PRGF_READONLY) ? c.crDisabledText : c.crText;
	RECT rcValue{ splitX + 1 + m_Pad, rc.top, rc.right - m_Pad, rc.bottom - 1 };
	int boxSize = std::min((int)(rc.bottom - rc.top) - 2 * m_Pad, Scale(13));
	int boxTop = rc.top + (rc.bottom - 1 - rc.top - boxSize) / 2;
	if (value.type == PRGVT_BOOL) {
		RECT rcBox{ rcValue.left, boxTop, rcValue.left + boxSize, boxTop + boxSize };
		DrawCheckBox(dc, rcBox, value.boolVal != FALSE, textColor, c.crBack);
		rcValue.left = rcBox.right + m_Pad;
	}
	else if (value.type == PRGVT_COLOR) {
		RECT rcSwatch{ rcValue.left, boxTop, rcValue.left + boxSize * 3 / 2, boxTop + boxSize };
		FillSolid(dc, rcSwatch, value.clrVal);
		WTL::CBrush frame;
		frame.CreateSolidBrush(c.crText);
		dc.FrameRect(&rcSwatch, frame);
		rcValue.left = rcSwatch.right + m_Pad;
	}
	auto text = PgFormatValue(*node);
	auto old = modified ? dc.SelectFont(m_BoldFont) : nullptr;
	dc.SetTextColor(textColor);
	dc.DrawText(text.c_str(), (int)text.size(), &rcValue, textFormat);
	if (modified)
		dc.SelectFont(old);

	// grid lines
	RECT line{ m_Margin, rc.bottom - 1, rc.right, rc.bottom };
	FillSolid(dc, line, c.crGridLine);
	RECT split{ splitX, rc.top, splitX + 1, rc.bottom };
	FillSolid(dc, split, c.crGridLine);
}

void CPropertyGridCtrl::DrawGlyph(WTL::CDCHandle dc, int x, const RECT& rcRow, bool expanded, COLORREF color) {
	int cy = (rcRow.top + rcRow.bottom) / 2;
	int half = std::max(2, Scale(4));
	int quarter = std::max(1, Scale(2));
	POINT pts[3];
	if (expanded) {
		// pointing down
		pts[0] = { x - half, cy - quarter };
		pts[1] = { x + half, cy - quarter };
		pts[2] = { x, cy + quarter + 1 };
	}
	else {
		// pointing right
		pts[0] = { x - quarter, cy - half };
		pts[1] = { x + quarter + 1, cy };
		pts[2] = { x - quarter, cy + half };
	}
	WTL::CBrush brush;
	brush.CreateSolidBrush(color);
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, 1, color);
	auto oldBrush = dc.SelectBrush(brush);
	auto oldPen = dc.SelectPen(pen);
	dc.Polygon(pts, 3);
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

void CPropertyGridCtrl::DrawCheckBox(WTL::CDCHandle dc, const RECT& rc, bool checked, COLORREF color, COLORREF back) {
	// drawn by hand rather than themed, so it follows the grid's colors (dark included)
	FillSolid(dc, rc, back);
	WTL::CBrush frame;
	frame.CreateSolidBrush(color);
	dc.FrameRect(&rc, frame);
	if (!checked)
		return;
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	POINT pts[3] = {
		{ rc.left + w * 2 / 10, rc.top + h * 5 / 10 },
		{ rc.left + w * 4 / 10, rc.top + h * 7 / 10 },
		{ rc.left + w * 8 / 10, rc.top + h * 3 / 10 },
	};
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, std::max(1, Scale(2)), color);
	auto old = dc.SelectPen(pen);
	dc.Polyline(pts, 3);
	dc.SelectPen(old);
}

void CPropertyGridCtrl::PaintDescription(WTL::CDCHandle dc, const RECT& rc, const PRGCOLORS& c) {
	FillSolid(dc, rc, c.crDescBack);
	if (!m_Selected)
		return;
	RECT rcText = rc;
	::InflateRect(&rcText, -m_Pad * 2, -m_Pad);
	dc.SetTextColor(c.crDescText);
	auto old = dc.SelectFont(m_BoldFont);
	RECT rcName = rcText;
	int height = dc.DrawText(m_Selected->name.c_str(), (int)m_Selected->name.size(), &rcName,
		DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	dc.SelectFont(m_hFont);
	rcText.top += height + m_Pad;
	if (rcText.top < rcText.bottom)
		dc.DrawText(m_Selected->description.c_str(), (int)m_Selected->description.size(), &rcText,
			DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX | DT_EDITCONTROL);
	dc.SelectFont(old);
}

// ---- Hit testing ----------------------------------------------------------------------

HPRGITEM CPropertyGridCtrl::HitTest(POINT pt, UINT* pFlags) const {
	UINT flags = PRGHT_NOWHERE;
	PgNode* node = nullptr;
	auto grid = GetGridRect();
	if (m_ShowDesc && pt.y >= grid.bottom) {
		auto desc = GetDescRect();
		flags = pt.y >= desc.top ? PRGHT_DESCRIPTION : PRGHT_DESCSPLITTER;
	}
	else if (::PtInRect(&grid, pt)) {
		int row = m_TopRow + (pt.y - grid.top) / m_RowHeight;
		if (row < (int)m_Rows.size()) {
			auto& r = m_Rows[row];
			node = r.node;
			if (node->IsCategory())
				flags = pt.x < m_Margin ? PRGHT_GLYPH : PRGHT_NAME;
			else {
				int splitX = GetSplitX();
				if (std::abs(pt.x - splitX) <= m_SplitterGrip)
					flags = PRGHT_SPLITTER;
				else if (HasVisibleChildren(node) && std::abs(pt.x - GlyphCenter(r)) <= std::max(m_Indent, m_Margin) / 2)
					flags = PRGHT_GLYPH;
				else
					flags = pt.x < splitX ? PRGHT_NAME : PRGHT_VALUE;
			}
		}
	}
	if (pFlags)
		*pFlags = flags;
	return node;
}

// ---- Mouse ----------------------------------------------------------------------------

LRESULT CPropertyGridCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	UINT flags;
	auto node = HitTest(pt, &flags);
	switch (flags) {
		case PRGHT_DESCSPLITTER:
			m_Drag = Drag::Description;
			m_DragOffset = pt.y - GetDescRect().top;
			SetCapture();
			break;
		case PRGHT_SPLITTER:
			m_Drag = Drag::Splitter;
			m_DragOffset = pt.x - GetSplitX();
			SetCapture();
			break;
		case PRGHT_GLYPH:
			SelectRow(RowOf(node));
			SetExpanded(node, !node->expanded, true);
			break;
		case PRGHT_NAME:
		case PRGHT_VALUE:
			SelectRow(RowOf(node));
			break;
	}
	return 0;
}

LRESULT CPropertyGridCtrl::OnLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	UINT flags;
	auto node = HitTest(pt, &flags);
	if ((flags == PRGHT_NAME || flags == PRGHT_VALUE) && HasVisibleChildren(node))
		SetExpanded(node, !node->expanded, true);
	else if (flags == PRGHT_GLYPH)
		SetExpanded(node, !node->expanded, true);   // the second click of a fast double toggle
	return 0;
}

LRESULT CPropertyGridCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Drag != Drag::None)
		ReleaseCapture();
	return 0;
}

LRESULT CPropertyGridCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Drag = Drag::None;
	return 0;
}

LRESULT CPropertyGridCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	if (m_Drag == Drag::Splitter) {
		auto grid = GetGridRect();
		int width = grid.right - m_Margin;
		if (width > 0)
			SetSplitterPos((double)(pt.x - m_DragOffset - m_Margin) / width);
	}
	else if (m_Drag == Drag::Description) {
		RECT rc;
		GetClientRect(&rc);
		int height = rc.bottom - (pt.y - m_DragOffset);
		SetDescriptionHeight(::MulDiv(height, 96, m_Dpi));
	}
	return 0;
}

LRESULT CPropertyGridCtrl::OnSetCursor(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	if (LOWORD(lParam) != HTCLIENT) {
		bHandled = FALSE;
		return 0;
	}
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	UINT flags;
	HitTest(pt, &flags);
	LPCWSTR cursor = IDC_ARROW;
	if (flags == PRGHT_SPLITTER || m_Drag == Drag::Splitter)
		cursor = IDC_SIZEWE;
	else if (flags == PRGHT_DESCSPLITTER || m_Drag == Drag::Description)
		cursor = IDC_SIZENS;
	::SetCursor(::LoadCursor(nullptr, cursor));
	return TRUE;
}

LRESULT CPropertyGridCtrl::OnRButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	UINT flags;
	auto node = HitTest(pt, &flags);
	if (node)
		SelectRow(RowOf(node));
	return 0;
}

LRESULT CPropertyGridCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	NotifyContextMenu(pt, pt.x == -1 && pt.y == -1);
	return 0;
}

void CPropertyGridCtrl::NotifyContextMenu(POINT ptScreen, bool fromKeyboard) {
	NMPRGITEM nm{};
	if (fromKeyboard) {
		// below the selected row's name, or the grid's corner
		nm.hItem = m_Selected;
		RECT rc;
		if (m_Selected && GetItemRect(m_Selected, &rc))
			nm.pt = { TextLeft(m_Rows[RowOf(m_Selected)]), rc.bottom };
		else {
			auto grid = GetGridRect();
			nm.pt = { grid.left + m_Margin, grid.top };
		}
		ClientToScreen(&nm.pt);
	}
	else {
		POINT pt = ptScreen;
		ScreenToClient(&pt);
		UINT flags;
		nm.hItem = HitTest(pt, &flags);
		nm.pt = ptScreen;
	}
	nm.lParam = nm.hItem ? nm.hItem->lParam : 0;
	FireNotify(&nm.hdr, PRGN_CONTEXTMENU);
}

LRESULT CPropertyGridCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	if (lines == 0)
		return 0;
	if (lines == WHEEL_PAGESCROLL)
		lines = GetPageRows();
	m_WheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
	int rows = m_WheelRemainder * (int)lines / WHEEL_DELTA;
	if (rows) {
		m_WheelRemainder -= rows * WHEEL_DELTA / (int)lines;
		ScrollTo(m_TopRow - rows);
	}
	return 0;
}

LRESULT CPropertyGridCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	int page = GetPageRows();
	int top = m_TopRow;
	switch (LOWORD(wParam)) {
		case SB_LINEUP:   top--; break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP:   top -= page; break;
		case SB_PAGEDOWN: top += page; break;
		case SB_TOP:      top = 0; break;
		case SB_BOTTOM:   top = (int)m_Rows.size(); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_VERT, &si);
			top = si.nTrackPos;
			break;
		}
	}
	ScrollTo(top);
	return 0;
}

// ---- Keyboard -------------------------------------------------------------------------

LRESULT CPropertyGridCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

LRESULT CPropertyGridCtrl::OnFocus(UINT, WPARAM, LPARAM, BOOL&) {
	Invalidate();
	return 0;
}

LRESULT CPropertyGridCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	int count = (int)m_Rows.size();
	if (count == 0) {
		bHandled = FALSE;
		return 0;
	}
	int row = RowOf(m_Selected);
	int page = GetPageRows();
	auto node = m_Selected;
	switch (wParam) {
		case VK_UP:    SelectRow(row < 0 ? 0 : std::max(row - 1, 0)); break;
		case VK_DOWN:  SelectRow(std::min(row + 1, count - 1)); break;
		case VK_PRIOR: SelectRow(std::max(row - (page - 1), 0)); break;
		case VK_NEXT:  SelectRow(std::min(std::max(row, 0) + page - 1, count - 1)); break;
		case VK_HOME:  SelectRow(0); break;
		case VK_END:   SelectRow(count - 1); break;
		case VK_LEFT:
			if (node && node->expanded && HasVisibleChildren(node))
				SetExpanded(node, false, true);
			else if (node && RowOf(node->parent) >= 0)
				SelectRow(RowOf(node->parent));
			break;
		case VK_RIGHT:
			if (node && HasVisibleChildren(node)) {
				if (!node->expanded)
					SetExpanded(node, true, true);
				else
					SelectRow(row + 1);
			}
			break;
		case VK_ADD:
			if (node && HasVisibleChildren(node))
				SetExpanded(node, true, true);
			break;
		case VK_SUBTRACT:
			if (node && HasVisibleChildren(node))
				SetExpanded(node, false, true);
			break;
		case VK_MULTIPLY:
			if (node) {
				auto expand = [](auto& self, PgNode* n) -> void {
					n->expanded = true;
					for (auto& c : n->children)
						self(self, c.get());
					};
				expand(expand, node);
				Rebuild();
			}
			break;
		case VK_RETURN:
			if (node && HasVisibleChildren(node))
				SetExpanded(node, !node->expanded, true);
			break;
		default:
			bHandled = FALSE;
	}
	return 0;
}

LRESULT CPropertyGridCtrl::FireNotify(NMHDR* pnm, UINT code) {
	if (!IsWindow())
		return 0;
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = GetDlgCtrlID();
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

// ---- Items ----------------------------------------------------------------------------

HPRGITEM CPropertyGridCtrl::AddCategory(LPCWSTR name, LPCWSTR description) {
	PRGITEMDESC desc;
	desc.kind = PRGK_CATEGORY;
	desc.pszName = name;
	desc.pszDescription = description;
	return InsertItem(PRG_ROOT, &desc);
}

HPRGITEM CPropertyGridCtrl::InsertItem(HPRGITEM hParent, const PRGITEMDESC* desc, HPRGITEM hInsertAfter) {
	if (!desc)
		return nullptr;
	auto node = m_Model->Insert(Node(hParent), *desc, hInsertAfter);
	if (node)
		Rebuild();
	return node;
}

BOOL CPropertyGridCtrl::DeleteItem(HPRGITEM hItem) {
	auto node = Node(hItem);
	if (!node || !node->parent)
		return FALSE;
	if (m_Selected == node || PgIsAncestor(node, m_Selected)) {
		// the row after the subtree, else the one before it
		int row = RowOf(node);
		int next = row + 1;
		while (next < (int)m_Rows.size() && PgIsAncestor(node, m_Rows[next].node))
			next++;
		SelectRow(row < 0 ? -1 : next < (int)m_Rows.size() ? next : row - 1);
	}
	std::erase_if(m_Rows, [=](auto& r) { return r.node == node || PgIsAncestor(node, r.node); });
	if (!m_Model->Delete(node))
		return FALSE;
	Rebuild();
	return TRUE;
}

void CPropertyGridCtrl::DeleteAllItems() {
	SelectRow(-1);
	m_Rows.clear();
	m_Model->Clear();
	m_TopRow = 0;
	Rebuild();
}

HPRGITEM CPropertyGridCtrl::FindItem(LPCWSTR path, HPRGITEM hStart) const {
	return m_Model->Find(path, Node(hStart));
}

int CPropertyGridCtrl::GetItemCount() const {
	return m_Model->Count();
}

// ---- Values ---------------------------------------------------------------------------

BOOL CPropertyGridCtrl::GetValue(HPRGITEM hItem, PRGVALUE* pValue) const {
	auto node = Node(hItem);
	if (!node || !pValue)
		return FALSE;
	*pValue = node->value.value;
	return TRUE;
}

BOOL CPropertyGridCtrl::SetValue(HPRGITEM hItem, const PRGVALUE* pValue) {
	auto node = Node(hItem);
	if (!node || !pValue || !PgValueFits(node->kind, *pValue))
		return FALSE;
	node->value.Assign(*pValue);
	if (IsWindow())
		Invalidate();
	return TRUE;
}

BOOL CPropertyGridCtrl::GetDefaultValue(HPRGITEM hItem, PRGVALUE* pValue) const {
	auto node = Node(hItem);
	if (!node || !pValue)
		return FALSE;
	*pValue = node->defValue.value;
	return TRUE;
}

BOOL CPropertyGridCtrl::SetDefaultValue(HPRGITEM hItem, const PRGVALUE* pValue) {
	auto node = Node(hItem);
	if (!node || !pValue || !PgValueFits(node->kind, *pValue))
		return FALSE;
	node->defValue.Assign(*pValue);
	if (IsWindow())
		Invalidate();
	return TRUE;
}

BOOL CPropertyGridCtrl::ResetValue(HPRGITEM hItem) {
	auto node = Node(hItem);
	if (!node || node->defValue.value.type == PRGVT_EMPTY)
		return FALSE;
	node->value.Assign(node->defValue.value);
	if (IsWindow())
		Invalidate();
	return TRUE;
}

bool CPropertyGridCtrl::IsModified(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node && node->defValue.value.type != PRGVT_EMPTY && !PgValuesEqual(node->value.value, node->defValue.value);
}

ATL::CString CPropertyGridCtrl::GetValueText(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? ATL::CString(PgFormatValue(*node).c_str()) : ATL::CString();
}

// ---- Attributes -----------------------------------------------------------------------

UINT CPropertyGridCtrl::GetItemKind(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? node->kind : PRGK_CATEGORY;
}

ATL::CString CPropertyGridCtrl::GetItemName(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? ATL::CString(node->name.c_str()) : ATL::CString();
}

BOOL CPropertyGridCtrl::SetItemName(HPRGITEM hItem, LPCWSTR name) {
	auto node = Node(hItem);
	if (!node || !name)
		return FALSE;
	node->name = name;
	Rebuild();   // the alphabetical order may change
	return TRUE;
}

ATL::CString CPropertyGridCtrl::GetDescription(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? ATL::CString(node->description.c_str()) : ATL::CString();
}

BOOL CPropertyGridCtrl::SetDescription(HPRGITEM hItem, LPCWSTR text) {
	auto node = Node(hItem);
	if (!node)
		return FALSE;
	node->description = text ? text : L"";
	if (IsWindow() && node == m_Selected)
		Invalidate();
	return TRUE;
}

UINT CPropertyGridCtrl::GetItemFlags(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? node->flags : 0;
}

BOOL CPropertyGridCtrl::SetItemFlags(HPRGITEM hItem, UINT flags, UINT mask) {
	auto node = Node(hItem);
	if (!node)
		return FALSE;
	node->flags = (node->flags & ~mask) | (flags & mask);
	Rebuild();   // PRGF_HIDDEN changes the rows
	return TRUE;
}

LPARAM CPropertyGridCtrl::GetItemData(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node ? node->lParam : 0;
}

BOOL CPropertyGridCtrl::SetItemData(HPRGITEM hItem, LPARAM data) {
	auto node = Node(hItem);
	if (!node)
		return FALSE;
	node->lParam = data;
	return TRUE;
}

BOOL CPropertyGridCtrl::SetEnumItems(HPRGITEM hItem, const PRGENUMITEM* items, int count) {
	auto node = Node(hItem);
	if (!node || count < 0 || (count > 0 && !items))
		return FALSE;
	node->enumItems.clear();
	node->enumItems.reserve(count);
	for (int i = 0; i < count; i++)
		node->enumItems.push_back({ items[i].pszName ? items[i].pszName : L"", items[i].value });
	if (IsWindow())
		Invalidate();
	return TRUE;
}

BOOL CPropertyGridCtrl::SetRange(HPRGITEM hItem, LONGLONG minVal, LONGLONG maxVal) {
	auto node = Node(hItem);
	if (!node || minVal > maxVal)
		return FALSE;
	node->minVal = minVal;
	node->maxVal = maxVal;
	return TRUE;
}

BOOL CPropertyGridCtrl::SetPrecision(HPRGITEM hItem, int precision) {
	auto node = Node(hItem);
	if (!node)
		return FALSE;
	node->precision = precision;
	if (IsWindow())
		Invalidate();
	return TRUE;
}

// ---- Navigation -----------------------------------------------------------------------

HPRGITEM CPropertyGridCtrl::GetChildItem(HPRGITEM hItem) const {
	auto node = hItem ? Node(hItem) : &m_Model->root;
	return node->children.empty() ? nullptr : node->children.front().get();
}

static HPRGITEM Sibling(HPRGITEM hItem, int offset) {
	if (!hItem || !hItem->parent)
		return nullptr;
	auto& siblings = hItem->parent->children;
	auto it = std::find_if(siblings.begin(), siblings.end(), [=](auto& c) { return c.get() == hItem; });
	auto index = (it - siblings.begin()) + offset;
	return index >= 0 && index < (ptrdiff_t)siblings.size() ? siblings[index].get() : nullptr;
}

HPRGITEM CPropertyGridCtrl::GetNextSiblingItem(HPRGITEM hItem) const {
	return Sibling(hItem, 1);
}

HPRGITEM CPropertyGridCtrl::GetPrevSiblingItem(HPRGITEM hItem) const {
	return Sibling(hItem, -1);
}

HPRGITEM CPropertyGridCtrl::GetParentItem(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node && node->parent != &m_Model->root ? node->parent : nullptr;
}

// ---- Selection and expansion ----------------------------------------------------------------

HPRGITEM CPropertyGridCtrl::GetSelectedItem() const {
	return m_Selected;
}

BOOL CPropertyGridCtrl::SelectItem(HPRGITEM hItem) {
	auto node = Node(hItem);
	if (!node) {
		SelectRow(-1);
		return TRUE;
	}
	if (RowOf(node) < 0) {
		ExpandAncestors(node);
		Rebuild();
	}
	int row = RowOf(node);
	if (row < 0)
		return FALSE;   // hidden, or a category in the alphabetical view
	SelectRow(row);
	return TRUE;
}

void CPropertyGridCtrl::EnsureVisible(HPRGITEM hItem) {
	int row = RowOf(Node(hItem));
	if (row < 0 || !IsWindow())
		return;
	int page = GetPageRows();
	if (row < m_TopRow)
		ScrollTo(row);
	else if (row >= m_TopRow + page)
		ScrollTo(row - page + 1);
}

BOOL CPropertyGridCtrl::Expand(HPRGITEM hItem, bool expand) {
	auto node = Node(hItem);
	if (!node)
		return FALSE;
	SetExpanded(node, expand, false);
	return TRUE;
}

bool CPropertyGridCtrl::IsExpanded(HPRGITEM hItem) const {
	auto node = Node(hItem);
	return node && node->expanded;
}

void CPropertyGridCtrl::ExpandAll(bool expand) {
	auto apply = [=](auto& self, PgNode* n) -> void {
		for (auto& c : n->children) {
			c->expanded = expand;
			self(self, c.get());
		}
		};
	apply(apply, &m_Model->root);
	Rebuild();
}

BOOL CPropertyGridCtrl::GetItemRect(HPRGITEM hItem, RECT* prc) const {
	int row = RowOf(Node(hItem));
	if (row < 0 || !prc || row < m_TopRow)
		return FALSE;
	auto grid = GetGridRect();
	int top = grid.top + (row - m_TopRow) * m_RowHeight;
	if (top >= grid.bottom)
		return FALSE;
	*prc = { grid.left, top, grid.right, top + m_RowHeight };
	return TRUE;
}

// ---- View -----------------------------------------------------------------------------

void CPropertyGridCtrl::SetView(UINT view) {
	if (view > PRGV_ALPHABETICAL || view == m_View)
		return;
	m_View = view;
	Rebuild();
	if (m_Selected)
		EnsureVisible(m_Selected);
}

UINT CPropertyGridCtrl::GetView() const {
	return m_View;
}

void CPropertyGridCtrl::ShowDescription(bool show) {
	m_ShowDesc = show;
	if (IsWindow())
		Layout();
}

bool CPropertyGridCtrl::IsDescriptionVisible() const {
	return m_ShowDesc;
}

void CPropertyGridCtrl::SetDescriptionHeight(int height) {
	m_DescHeightDips = std::max(24, height);
	if (IsWindow())
		Layout();
}

int CPropertyGridCtrl::GetDescriptionHeight() const {
	return m_DescHeightDips;
}

void CPropertyGridCtrl::SetSplitterPos(double fraction) {
	m_SplitFraction = std::clamp(fraction, 0.1, 0.9);
	if (IsWindow())
		Invalidate();
}

double CPropertyGridCtrl::GetSplitterPos() const {
	return m_SplitFraction;
}

void CPropertyGridCtrl::BeginUpdate() {
	if (m_UpdateCount++ == 0 && IsWindow())
		SetRedraw(FALSE);
}

void CPropertyGridCtrl::EndUpdate() {
	if (m_UpdateCount == 0 || --m_UpdateCount > 0)
		return;
	if (IsWindow())
		SetRedraw(TRUE);
	Rebuild();
	if (IsWindow())
		Invalidate();
}
