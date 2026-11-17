#include "DataGridData.h"

#include <algorithm>

// ---- Colors ----------------------------------------------------------------------------------

namespace {
	COLORREF Blend(COLORREF a, COLORREF b, int percentOfB) {
		auto mix = [&](int x, int y) { return (BYTE)((x * (100 - percentOfB) + y * percentOfB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	COLORREF Pick(COLORREF user, COLORREF system) {
		return user == CLR_DEFAULT ? system : user;
	}

	void Fill(HDC dc, const RECT& rc, COLORREF color) {
		::SetBkColor(dc, color);
		::ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	void HLine(HDC dc, int x1, int x2, int y, COLORREF color) {
		RECT rc{ x1, y, x2, y + 1 };
		Fill(dc, rc, color);
	}

	void VLine(HDC dc, int x, int y1, int y2, COLORREF color) {
		RECT rc{ x, y1, x + 1, y2 };
		Fill(dc, rc, color);
	}

	void Frame(HDC dc, const RECT& rc, int thickness, COLORREF color) {
		for (int i = 0; i < thickness; i++) {
			HLine(dc, rc.left + i, rc.right - i, rc.top + i, color);
			HLine(dc, rc.left + i, rc.right - i, rc.bottom - 1 - i, color);
			VLine(dc, rc.left + i, rc.top + i, rc.bottom - i, color);
			VLine(dc, rc.right - 1 - i, rc.top + i, rc.bottom - i, color);
		}
	}

	UINT AlignFlags(int align) {
		switch (align) {
			case DgAlign::Right:  return DT_RIGHT;
			case DgAlign::Center: return DT_CENTER;
		}
		return DT_LEFT;
	}
}

void CDataGridCtrl::UpdateColors() {
	auto& d = *m_Data;
	auto& c = d.colors;
	auto& u = d.userColors;
	HIGHCONTRASTW hc{ sizeof(hc) };
	c.highContrast = ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
	// read each time: WTLHelper's dark mode hooks GetSysColor
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT);
	COLORREF highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	if (c.highContrast) {
		c.back = window;
		c.text = text;
		c.grid = text;
		c.headerBack = ::GetSysColor(COLOR_BTNFACE);
		c.headerText = ::GetSysColor(COLOR_BTNTEXT);
		c.headerHot = c.headerBack;
		c.headerPressed = c.headerBack;
		c.caretRow = window;
		c.caret = highlight;
		c.caretInactive = text;
		c.link = ::GetSysColor(COLOR_HOTLIGHT);
		c.selection = c.selectionInactive = highlight;
		c.selectionText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
		return;
	}
	c.back = Pick(u.crBack, window);
	c.text = Pick(u.crText, text);
	c.grid = Pick(u.crGrid, Blend(c.back, c.text, 14));
	c.headerBack = Pick(u.crHeaderBack, Blend(c.back, c.text, 5));
	c.headerText = Pick(u.crHeaderText, c.text);
	c.headerHot = Blend(c.headerBack, highlight, 12);
	c.headerPressed = Blend(c.headerBack, highlight, 24);
	c.caretRow = Pick(u.crCaretRow, Blend(c.back, highlight, 10));
	c.caret = Pick(u.crCaret, highlight);
	c.caretInactive = Blend(c.back, c.text, 40);
	c.link = Pick(u.crLink, ::GetSysColor(COLOR_HOTLIGHT));
	c.selection = Pick(u.crSelection, Blend(c.back, highlight, 30));
	c.selectionInactive = Blend(c.back, c.selection, 55);
	c.selectionText = c.text;
}

// ---- Painting --------------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnPaint(UINT msg, WPARAM wParam, LPARAM, BOOL&) {
	CRect rc;
	GetClientRect(&rc);
	if (msg == WM_PRINTCLIENT) {
		Paint((HDC)wParam, rc);
		return 0;
	}
	CPaintDC dc(m_hWnd);
	if (rc.IsRectEmpty())
		return 0;
	CDC mem;
	mem.CreateCompatibleDC(dc);
	CBitmap bitmap;
	bitmap.CreateCompatibleBitmap(dc, rc.Width(), rc.Height());
	HBITMAP old = mem.SelectBitmap(bitmap);
	Paint(mem, dc.m_ps.rcPaint);
	dc.BitBlt(dc.m_ps.rcPaint.left, dc.m_ps.rcPaint.top, dc.m_ps.rcPaint.right - dc.m_ps.rcPaint.left,
		dc.m_ps.rcPaint.bottom - dc.m_ps.rcPaint.top, mem, dc.m_ps.rcPaint.left, dc.m_ps.rcPaint.top, SRCCOPY);
	mem.SelectBitmap(old);
	return 0;
}

void CDataGridCtrl::Paint(HDC hdc, const RECT& clip) {
	auto& d = *m_Data;
	UpdateColors();
	auto& c = d.colors;
	auto& L = d.layout;
	DWORD style = GetStyle();
	CDCHandle dc(hdc);
	int w = d.clientWidth, h = d.clientHeight;
	int pad = Scale(6);
	int n = (int)L.cols.size();
	int64_t rowCount = d.RowCount();
	int contentRight = std::min(w, n ? std::max(L.Right(n - 1, d.scrollX), L.ScrollLeft()) : L.rowHeader);

	Fill(dc, RECT{ 0, 0, w, h }, c.back);
	dc.SetBkMode(TRANSPARENT);
	HFONT oldFont = dc.SelectFont(m_hFont);

	// the columns in view: the scrolling ones, then the frozen ones over them
	std::vector<int> order;
	for (int i = L.frozenCount; i < n; i++)
		if (L.Right(i, d.scrollX) > L.ScrollLeft() && L.Left(i, d.scrollX) < w)
			order.push_back(i);
	for (int i = 0; i < L.frozenCount; i++)
		order.push_back(i);

	// ---- the rows
	int64_t first = d.topRow;
	int64_t last = std::min<int64_t>(rowCount, first + (h - L.header) / L.rowHeight + 2);
	int caretCol = d.caretRow >= 0 ? L.IndexOf(d.caretId) : -1;
	int64_t caretView = d.rows.ToView(d.caretRow);
	// a selection of the caret's cell alone isn't shaded: the caret's frame shows it, as in Excel
	auto& ranges = d.selection.Ranges();
	bool onlyCaret = ranges.size() == 1 && ranges[0].top == ranges[0].bottom && ranges[0].left == ranges[0].right
		&& d.selection.Mode() != DgSelMode::Rows;
	for (int64_t r = first; r < last; r++) {
		int y = L.header + (int)(r - first) * L.rowHeight;
		if (y >= clip.bottom || y + L.rowHeight <= clip.top)
			continue;
		int64_t dataRow = d.rows.ToData(r);
		bool caretRow = r == caretView && !(style & DGVS_NOCARETROW);
		if (caretRow)
			Fill(dc, RECT{ L.rowHeader, y, contentRight, y + L.rowHeight }, c.caretRow);

		for (int i : order) {
			int x = L.Left(i, d.scrollX), right = x + L.cols[i].width;
			RECT cellClip{ i < L.frozenCount ? L.rowHeader : L.ScrollLeft(), y, i < L.frozenCount ? L.ScrollLeft() : w, y + L.rowHeight };
			RECT cell{ x, y, right, y + L.rowHeight };
			RECT visible;
			if (!::IntersectRect(&visible, &cell, &cellClip) || visible.left >= clip.right || visible.right <= clip.left)
				continue;
			bool selected = !onlyCaret && d.selection.Contains(r, i);
			if (selected)
				Fill(dc, visible, d.focused ? c.selection : c.selectionInactive);
			else if (i < L.frozenCount)   // over the scrolled columns' parts under it
				Fill(dc, visible, caretRow ? c.caretRow : c.back);
			COLORREF text = selected ? c.selectionText : c.text;
			auto column = d.columns.Find(L.cols[i].id);
			DgCell value;
			if (!column || !Fetch(dataRow, column->id, value))
				continue;
			dc.SaveDC();
			dc.IntersectClipRect(&visible);
			RECT inner{ cell.left + pad, cell.top, cell.right - pad, cell.bottom - 1 };
			int align = DgEffectiveAlign(*column);
			switch (column->type) {
				case DgColType::Bool: {
					if (value.value.type == DgType::Empty)
						break;
					int box = std::min(Scale(13), L.rowHeight - Scale(6));
					int bx = align == DgAlign::Left ? inner.left : align == DgAlign::Right ? inner.right - box : (cell.left + cell.right - box) / 2;
					int by = (cell.top + cell.bottom - box) / 2;
					RECT rb{ bx, by, bx + box, by + box };
					Frame(dc, rb, 1, Blend(c.back, text, 60));
					if (DgChecked(value.value)) {
						CPen pen;
						pen.CreatePen(PS_SOLID, std::max(1, Scale(2)), text);
						HPEN oldPen = dc.SelectPen(pen);
						POINT pts[3] = { { bx + box / 5, by + box / 2 }, { bx + box * 2 / 5, by + box * 3 / 4 }, { bx + box * 4 / 5, by + box / 4 } };
						dc.Polyline(pts, 3);
						dc.SelectPen(oldPen);
					}
					break;
				}
				case DgColType::Progress: {
					double p = DgProgress(value.value);
					if (p < 0)
						break;
					RECT bar{ cell.left + Scale(4), cell.top + Scale(4), cell.right - Scale(4), cell.bottom - Scale(4) - 1 };
					Fill(dc, bar, Blend(c.back, c.text, 10));
					RECT done = bar;
					done.right = bar.left + (int)((bar.right - bar.left) * p + 0.5);
					Fill(dc, done, c.highContrast ? ::GetSysColor(COLOR_HIGHLIGHT) : Blend(c.back, ::GetSysColor(COLOR_HIGHLIGHT), 55));
					dc.SetTextColor(text);
					auto percent = DgCellText(value, *column, d.numberFormat);
					dc.DrawText(percent.c_str(), (int)percent.size(), &bar, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
					break;
				}
				default: {
					if (column->type == DgColType::ImageText && d.images && value.image >= 0) {
						int cx = 0, cy = 0;
						::ImageList_GetIconSize(d.images, &cx, &cy);
						::ImageList_Draw(d.images, value.image, dc, inner.left, (cell.top + cell.bottom - cy) / 2, ILD_TRANSPARENT);
						inner.left += cx + Scale(4);
					}
					auto s = DgCellText(value, *column, d.numberFormat);
					bool link = column->type == DgColType::Link;
					dc.SetTextColor(link && !(selected && c.highContrast) ? c.link : text);
					HFONT f = link ? dc.SelectFont(m_hUnderline) : nullptr;
					dc.DrawText(s.c_str(), (int)s.size(), &inner,
						DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | AlignFlags(align));
					if (f)
						dc.SelectFont(f);
					break;
				}
			}
			dc.RestoreDC(-1);
			if (!(style & DGVS_NOVERTLINES))
				VLine(dc, right - 1, y, y + L.rowHeight, c.grid);
		}
		if (!(style & DGVS_NOHORZLINES))
			HLine(dc, L.rowHeader, contentRight, y + L.rowHeight - 1, c.grid);

		// the row's number
		if (L.rowHeader > 0) {
			RECT rh{ 0, y, L.rowHeader, y + L.rowHeight };
			Fill(dc, rh, r == caretView ? c.headerPressed : d.selection.RowTouched(r) ? c.headerHot : c.headerBack);
			auto number = std::to_wstring(r + 1);
			RECT text{ 0, y, L.rowHeader - pad, y + L.rowHeight };
			dc.SetTextColor(c.headerText);
			dc.DrawText(number.c_str(), (int)number.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
			HLine(dc, 0, L.rowHeader, y + L.rowHeight - 1, c.grid);
			VLine(dc, L.rowHeader - 1, y, y + L.rowHeight, c.grid);
		}
	}

	// the edge of the frozen columns
	if (L.frozenCount > 0) {
		int y2 = L.header + (int)std::min<int64_t>(last - first, (h - L.header) / L.rowHeight + 1) * L.rowHeight;
		VLine(dc, L.ScrollLeft() - 1, L.header, std::min(h, y2), Blend(c.back, c.text, 45));
	}

	// the caret's cell
	if (caretView >= first && caretView < last && caretCol >= 0) {
		int y = L.header + (int)(caretView - first) * L.rowHeight;
		RECT cell{ L.Left(caretCol, d.scrollX), y, L.Right(caretCol, d.scrollX), y + L.rowHeight };
		RECT area{ caretCol < L.frozenCount ? L.rowHeader : L.ScrollLeft(), L.header, w, h };
		RECT visible;
		if (::IntersectRect(&visible, &cell, &area)) {
			dc.SaveDC();
			dc.IntersectClipRect(&area);
			cell.left -= 1;
			cell.top -= 1;
			Frame(dc, cell, d.focused ? Scale(2) : 1, d.focused ? c.caret : c.caretInactive);
			dc.RestoreDC(-1);
		}
	}

	// ---- the header
	if (L.header > 0 && clip.top < L.header) {
		dc.SelectFont(m_hBold);
		Fill(dc, RECT{ 0, 0, w, L.header }, c.headerBack);
		for (int i : order) {
			int x = L.Left(i, d.scrollX), right = x + L.cols[i].width;
			RECT cell{ x, 0, right, L.header };
			RECT area{ i < L.frozenCount ? L.rowHeader : L.ScrollLeft(), 0, i < L.frozenCount ? L.ScrollLeft() : w, L.header };
			RECT visible;
			if (!::IntersectRect(&visible, &cell, &area))
				continue;
			auto column = d.columns.Find(L.cols[i].id);
			if (!column)
				continue;
			bool pressed = d.drag == DgData::Drag::Press && d.dragCol == i;
			bool hot = (d.hotCol == i && d.drag == DgData::Drag::None)
				|| (d.selection.Mode() == DgSelMode::Cells && d.selection.ColTouched(i));
			Fill(dc, visible, pressed ? c.headerPressed : hot ? c.headerHot : c.headerBack);
			dc.SaveDC();
			dc.IntersectClipRect(&visible);
			RECT text{ x + pad, 0, right - pad, L.header };
			// sorted by it: an arrow at the right, with its place among the keys when there are several
			auto key = std::find_if(d.sortKeys.begin(), d.sortKeys.end(), [&](auto& k) { return k.id == column->id; });
			if (key != d.sortKeys.end()) {
				int aw = Scale(8), hgt = Scale(4);
				int ax = text.right - aw, ay = (L.header - hgt) / 2;
				COLORREF arrow = Blend(c.headerBack, c.headerText, 70);
				CBrush brush;
				brush.CreateSolidBrush(arrow);
				CPen pen;
				pen.CreatePen(PS_SOLID, 1, arrow);
				HBRUSH oldBrush = dc.SelectBrush(brush);
				HPEN oldPen = dc.SelectPen(pen);
				POINT up[3] = { { ax, ay + hgt }, { ax + aw, ay + hgt }, { ax + aw / 2, ay } };
				POINT down[3] = { { ax, ay }, { ax + aw, ay }, { ax + aw / 2, ay + hgt } };
				dc.Polygon(key->descending ? down : up, 3);
				dc.SelectBrush(oldBrush);
				dc.SelectPen(oldPen);
				text.right = ax - Scale(3);
				if (d.sortKeys.size() > 1) {
					auto number = std::to_wstring(key - d.sortKeys.begin() + 1);
					HFONT f = dc.SelectFont(m_hFont);
					dc.SetTextColor(arrow);
					RECT nr{ text.right - d.digitWidth * 2, 0, text.right, L.header };
					dc.DrawText(number.c_str(), (int)number.size(), &nr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
					dc.SelectFont(f);
					text.right = nr.right - d.digitWidth * (int)number.size() - Scale(3);
				}
			}
			dc.SetTextColor(c.headerText);
			int align = DgEffectiveAlign(*column);
			dc.DrawText(column->title.c_str(), (int)column->title.size(), &text,
				DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | AlignFlags(align == DgAlign::Center ? DgAlign::Center : align));
			dc.RestoreDC(-1);
			VLine(dc, right - 1, Scale(4), L.header - Scale(4), c.grid);
		}
		if (L.rowHeader > 0) {
			Fill(dc, RECT{ 0, 0, L.rowHeader, L.header }, c.headerBack);
			VLine(dc, L.rowHeader - 1, 0, L.header, c.grid);
		}
		if (L.frozenCount > 0)
			VLine(dc, L.ScrollLeft() - 1, 0, L.header, Blend(c.back, c.text, 45));
		HLine(dc, 0, w, L.header - 1, Blend(c.back, c.text, 25));

		// a column being dragged: its title follows the mouse, a bar marks where it would go
		if (d.drag == DgData::Drag::Reorder && d.dragCol >= 0 && d.dragCol < n) {
			auto column = d.columns.Find(L.cols[d.dragCol].id);
			int left = d.dragX - d.dragOffset;
			RECT ghost{ left, 0, left + L.cols[d.dragCol].width, L.header };
			Fill(dc, ghost, c.headerPressed);
			Frame(dc, ghost, 1, c.caret);
			if (column) {
				RECT text{ ghost.left + pad, 0, ghost.right - pad, L.header };
				dc.SetTextColor(c.headerText);
				dc.DrawText(column->title.c_str(), (int)column->title.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
			if (d.dropIndex >= 0) {
				int x = d.dropIndex < n ? L.Left(d.dropIndex, d.scrollX) : L.Right(n - 1, d.scrollX);
				if (d.dropIndex >= L.frozenCount && d.dropIndex < n)
					x = std::max(x, L.ScrollLeft());
				RECT bar{ x - Scale(1), 0, x + Scale(1) + 1, h };
				Fill(dc, bar, c.caret);
			}
		}
	}
	dc.SelectFont(oldFont);

	// a virtual grid keeps the cells around the rows in view
	if (d.virtualMode) {
		int64_t full = DgFullRows(L, h);
		d.cache.Trim(first - full, last + full);
	}
}
