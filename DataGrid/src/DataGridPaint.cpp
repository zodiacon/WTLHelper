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

	enum class Glyph { Down, Right, Funnel };

	// a small filled shape in the middle of box
	void PaintGlyph(HDC dc, Glyph glyph, const RECT& box, COLORREF color) {
		int s = std::max(4, (int)std::min(box.right - box.left, box.bottom - box.top) / 2);   // its size
		int cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
		HBRUSH brush = ::CreateSolidBrush(color);
		HPEN pen = ::CreatePen(PS_SOLID, 1, color);
		HGDIOBJ oldBrush = ::SelectObject(dc, brush), oldPen = ::SelectObject(dc, pen);
		switch (glyph) {
			case Glyph::Down: {
				POINT p[3] = { { cx - s / 2, cy - s / 4 }, { cx + s / 2, cy - s / 4 }, { cx, cy + s / 4 } };
				::Polygon(dc, p, 3);
				break;
			}
			case Glyph::Right: {
				POINT p[3] = { { cx - s / 4, cy - s / 2 }, { cx - s / 4, cy + s / 2 }, { cx + s / 4, cy } };
				::Polygon(dc, p, 3);
				break;
			}
			case Glyph::Funnel: {   // a cone over a stem
				POINT p[6] = { { cx - s / 2, cy - s / 2 }, { cx + s / 2, cy - s / 2 }, { cx + s / 8, cy },
					{ cx + s / 8, cy + s / 2 }, { cx - s / 8, cy + s / 2 - s / 6 }, { cx - s / 8, cy } };
				::Polygon(dc, p, 6);
				break;
			}
		}
		::SelectObject(dc, oldBrush);
		::SelectObject(dc, oldPen);
		::DeleteObject(brush);
		::DeleteObject(pen);
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
	int caretCol = d.HasCaret() ? L.IndexOf(d.caretId) : -1;
	int64_t caretView = d.rows.ToView(d.caretRow);
	// a selection of the caret's cell alone isn't shaded: the caret's frame shows it, as in Excel
	auto& ranges = d.selection.Ranges();
	bool onlyCaret = ranges.size() == 1 && ranges[0].top == ranges[0].bottom && ranges[0].left == ranges[0].right
		&& d.selection.Mode() != DgSelMode::Rows;
	// custom draw: the app asks for the rows' and the cells' notifications
	NMDGVCUSTOMDRAW cd{};
	cd.nmcd.hdc = dc;
	cd.nmcd.rc = RECT{ 0, 0, w, h };
	cd.nmcd.dwDrawStage = CDDS_PREPAINT;
	cd.iRow = -1;
	cd.clrText = c.text;
	cd.clrTextBk = c.back;
	LRESULT drawFlags = FireNotify(&cd.nmcd.hdr, NM_CUSTOMDRAW);
	bool notifyRows = (drawFlags & CDRF_NOTIFYITEMDRAW) != 0;
	bool looks = !d.rules.empty() && !c.highContrast;   // high contrast keeps the system's colors
	COLORREF highlight = c.highContrast ? ::GetSysColor(COLOR_HIGHLIGHT) : Blend(c.back, RGB(255, 190, 0), 45);
	COLORREF selectionBack = d.focused ? c.selection : c.selectionInactive;

	for (int64_t r = first; r < last; r++) {
		int y = L.header + (int)(r - first) * L.rowHeight;
		if (y >= clip.bottom || y + L.rowHeight <= clip.top)
			continue;
		int64_t dataRow = d.rows.ToData(r);
		if (int64_t g = d.rows.GroupOf(r); g >= 0 && g < (int64_t)d.view.groups.size()) {
			PaintGroupRow(dc, d.view.groups[(size_t)g], y);
			continue;
		}
		bool newRow = dataRow == DgRowMap::NewRow;
		bool caretRow = r == caretView && !(style & DGVS_NOCARETROW);
		COLORREF rowBack = caretRow ? c.caretRow : c.back;
		COLORREF rowText = c.text;
		bool notifyCells = false;
		if (notifyRows && !newRow) {
			cd.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
			cd.nmcd.dwItemSpec = (DWORD_PTR)r;
			cd.nmcd.rc = RECT{ L.rowHeader, y, contentRight, y + L.rowHeight };
			cd.nmcd.uItemState = d.selection.RowTouched(r) ? CDIS_SELECTED : 0;
			cd.iRow = dataRow;
			cd.idColumn = 0;
			cd.clrText = rowText;
			cd.clrTextBk = rowBack;
			notifyCells = (FireNotify(&cd.nmcd.hdr, NM_CUSTOMDRAW) & CDRF_NOTIFYSUBITEMDRAW) != 0;
			rowText = cd.clrText;
			rowBack = cd.clrTextBk;
		}
		if (rowBack != c.back)
			Fill(dc, RECT{ L.rowHeader, y, contentRight, y + L.rowHeight }, rowBack);

		// the values the rules test, fetched once for the row
		std::vector<std::pair<int, DgCell>> ruleValues;
		if (looks && !newRow)
			for (auto& rule : d.rules)
				if (std::none_of(ruleValues.begin(), ruleValues.end(), [&](auto& v) { return v.first == rule.column; })) {
					DgCell v;
					Fetch(dataRow, rule.column, v);
					ruleValues.emplace_back(rule.column, std::move(v));
				}

		for (int i : order) {
			int x = L.Left(i, d.scrollX), right = x + L.cols[i].width;
			RECT cellClip{ i < L.frozenCount ? L.rowHeader : L.ScrollLeft(), y, i < L.frozenCount ? L.ScrollLeft() : w, y + L.rowHeight };
			RECT cell{ x, y, right, y + L.rowHeight };
			RECT visible;
			if (!::IntersectRect(&visible, &cell, &cellClip) || visible.left >= clip.right || visible.right <= clip.left)
				continue;
			bool selected = !onlyCaret && d.selection.Contains(r, i);
			auto column = d.columns.Find(L.cols[i].id);
			DgCell value;
			bool has = column && Fetch(dataRow, column->id, value);
			bool editing = d.edit.active && dataRow == d.edit.row && column && column->id == d.edit.id;

			// its colors: the row's, a rule's, the find's shading, the app's; the selection blends over them
			COLORREF back = rowBack, text = rowText;
			bool bold = false;
			if (looks && has) {
				auto look = DgResolveLook(d.rules, column->id,
					[&](int id) -> const DgValue* {
						auto v = std::find_if(ruleValues.begin(), ruleValues.end(), [&](auto& p) { return p.first == id; });
						return v != ruleValues.end() ? &v->second.value : nullptr;
					},
					[&](int id) { return (const DgColumn*)d.columns.Find(id); }, d.numberFormat, DgValueText);
				if (look.back != DgNoColor)
					back = look.back;
				if (look.text != DgNoColor)
					text = look.text;
				bold = look.bold;
			}
			if (has && !d.highlight.empty() && value.value.type != DgType::Empty
				&& DgTextMatches(DgCellText(value, *column, d.numberFormat), d.highlight, d.highlightFlags))
				back = highlight;
			if (selected) {
				back = back == rowBack && rowBack == (caretRow ? c.caretRow : c.back) ? selectionBack : Blend(back, selectionBack, 55);
				if (text == c.text)
					text = c.selectionText;
			}
			LRESULT cellFlags = CDRF_DODEFAULT;
			if (notifyCells && has) {
				cd.nmcd.dwDrawStage = CDDS_ITEMPREPAINT | CDDS_SUBITEM;
				cd.nmcd.rc = cell;
				cd.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (r == caretView && i == caretCol ? CDIS_FOCUS : 0);
				cd.idColumn = column->id;
				cd.clrText = text;
				cd.clrTextBk = back;
				dc.SaveDC();
				dc.IntersectClipRect(&visible);
				dc.SelectFont(bold ? m_hBold : m_hFont);
				cellFlags = FireNotify(&cd.nmcd.hdr, NM_CUSTOMDRAW);
				if (!(cellFlags & CDRF_NEWFONT))
					dc.RestoreDC(-1);
				text = cd.clrText;
				back = cd.clrTextBk;
			}
			// frozen cells cover the scrolled ones under them
			if (back != rowBack || i < L.frozenCount)
				Fill(dc, visible, back);
			if (!has || editing || (cellFlags & CDRF_SKIPDEFAULT)) {   // nothing to draw, its editor over it, or the app drew it
				if (cellFlags & CDRF_NEWFONT)
					dc.RestoreDC(-1);
				if (newRow && column && column->type == DgColType::Bool && CanEditCell(dataRow, column->id)) {   // a box to tick
					dc.SaveDC();
					dc.IntersectClipRect(&visible);
					Frame(dc, CheckRect(cell, DgEffectiveAlign(*column)), 1, Blend(c.back, c.text, 60));
					dc.RestoreDC(-1);
				}
				if (!(style & DGVS_NOVERTLINES))
					VLine(dc, right - 1, y, y + L.rowHeight, c.grid);
				continue;
			}
			if (!(cellFlags & CDRF_NEWFONT)) {
				dc.SaveDC();
				dc.IntersectClipRect(&visible);
				if (bold)
					dc.SelectFont(m_hBold);
			}
			RECT inner{ cell.left + pad, cell.top, cell.right - pad, cell.bottom - 1 };
			int align = DgEffectiveAlign(*column);
			switch (column->type) {
				case DgColType::Bool: {
					if (value.value.type == DgType::Empty && !CanEdit(dataRow, column->id))   // editable: an empty box to click
						break;
					RECT rb = CheckRect(cell, align);
					int bx = rb.left, by = rb.top, box = rb.right - rb.left;
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
					bool link = column->type == DgColType::Link && text == (selected ? c.selectionText : c.text);   // not a rule's color
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
			if (cellFlags & CDRF_NOTIFYPOSTPAINT) {
				cd.nmcd.dwDrawStage = CDDS_ITEMPOSTPAINT | CDDS_SUBITEM;
				FireNotify(&cd.nmcd.hdr, NM_CUSTOMDRAW);
			}
			if (!(style & DGVS_NOVERTLINES))
				VLine(dc, right - 1, y, y + L.rowHeight, c.grid);
		}
		if (!(style & DGVS_NOHORZLINES))
			HLine(dc, L.rowHeader, contentRight, y + L.rowHeight - 1, c.grid);
		// the new row's placeholder: a hint across it, unless it's being typed in
		if (newRow && !(d.edit.active && d.edit.row == DgRowMap::NewRow)) {
			static const wchar_t hint[] = L"Type here to add a row";
			int left = n > 0 ? std::max(L.Left(0, d.scrollX), L.rowHeader) : L.rowHeader;
			RECT rc{ left + pad, y, std::max(left + pad, contentRight - pad), y + L.rowHeight - 1 };
			dc.SetTextColor(Blend(c.back, c.text, 45));
			dc.DrawText(hint, (int)wcslen(hint), &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		}

		// the row's number (the new row's a star)
		if (L.rowHeader > 0) {
			RECT rh{ 0, y, L.rowHeader, y + L.rowHeight };
			Fill(dc, rh, r == caretView ? c.headerPressed : d.selection.RowTouched(r) ? c.headerHot : c.headerBack);
			auto number = newRow ? std::wstring(L"*") : std::to_wstring(r + 1);
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
		Fill(dc, RECT{ 0, 0, w, L.titles }, c.headerBack);
		for (int i : order) {
			int x = L.Left(i, d.scrollX), right = x + L.cols[i].width;
			RECT cell{ x, 0, right, L.titles };
			RECT area{ i < L.frozenCount ? L.rowHeader : L.ScrollLeft(), 0, i < L.frozenCount ? L.ScrollLeft() : w, L.titles };
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
			RECT text{ x + pad, 0, right - pad, L.titles };
			// the menu's button (a funnel when it's filtered), or a funnel alone without the menu
			bool filtered = HasFilter(column->id);
			if ((style & DGVS_HEADERMENU) || filtered) {
				int bw = Scale(18);
				RECT button{ right - bw, Scale(3), right - Scale(2), L.titles - Scale(3) };
				if ((style & DGVS_HEADERMENU) && d.hotCol == i)
					Fill(dc, button, c.headerPressed);
				COLORREF glyph = filtered ? c.caret : Blend(c.headerBack, c.headerText, 60);
				if (filtered || ((style & DGVS_HEADERMENU) && d.hotCol == i))
					PaintGlyph(dc, filtered ? Glyph::Funnel : Glyph::Down, button, glyph);
				text.right = button.left - Scale(2);
			}
			// sorted by it: an arrow at the right, with its place among the keys when there are several
			auto key = std::find_if(d.sortKeys.begin(), d.sortKeys.end(), [&](auto& k) { return k.id == column->id; });
			if (key != d.sortKeys.end()) {
				int aw = Scale(8), hgt = Scale(4);
				int ax = text.right - aw, ay = (L.titles - hgt) / 2;
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
					RECT nr{ text.right - d.digitWidth * 2, 0, text.right, L.titles };
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
			VLine(dc, right - 1, Scale(4), L.titles - Scale(4), c.grid);
		}
		if (L.rowHeader > 0) {
			Fill(dc, RECT{ 0, 0, L.rowHeader, L.titles }, c.headerBack);
			VLine(dc, L.rowHeader - 1, 0, L.titles, c.grid);
		}
		if (L.frozenCount > 0)
			VLine(dc, L.ScrollLeft() - 1, 0, L.titles, Blend(c.back, c.text, 45));
		HLine(dc, 0, w, L.titles - 1, Blend(c.back, c.text, 25));

		// the filter row: each column's filter, or "Filter..." where the mouse is
		if (L.header > L.titles) {
			dc.SelectFont(m_hFont);
			Fill(dc, RECT{ 0, L.titles, w, L.header }, c.back);
			for (int i : order) {
				RECT visible;
				if (!FilterRect(i, visible))
					continue;
				int id = L.cols[i].id;
				if (id == d.filterEditId)
					continue;   // its edit box is over it
				int x = L.Left(i, d.scrollX), right = x + L.cols[i].width;
				auto summary = FilterSummary(id);
				bool placeholder = summary.empty() && d.hotFilterCol == i;
				if (placeholder)
					summary = L"Filter\x2026";
				dc.SaveDC();
				dc.IntersectClipRect(&visible);
				RECT text{ x + pad, L.titles, right - pad, L.header };
				dc.SetTextColor(placeholder ? Blend(c.back, c.text, 45) : c.caret);
				dc.DrawText(summary.c_str(), (int)summary.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
				dc.RestoreDC(-1);
				VLine(dc, right - 1, L.titles, L.header, c.grid);
			}
			if (L.rowHeader > 0) {
				Fill(dc, RECT{ 0, L.titles, L.rowHeader, L.header }, c.headerBack);
				VLine(dc, L.rowHeader - 1, L.titles, L.header, c.grid);
			}
			HLine(dc, 0, w, L.header - 1, Blend(c.back, c.text, 25));
			dc.SelectFont(m_hBold);
		}

		// a column being dragged: its title follows the mouse, a bar marks where it would go
		if (d.drag == DgData::Drag::Reorder && d.dragCol >= 0 && d.dragCol < n) {
			auto column = d.columns.Find(L.cols[d.dragCol].id);
			int left = d.dragX - d.dragOffset;
			RECT ghost{ left, 0, left + L.cols[d.dragCol].width, L.titles };
			Fill(dc, ghost, c.headerPressed);
			Frame(dc, ghost, 1, c.caret);
			if (column) {
				RECT text{ ghost.left + pad, 0, ghost.right - pad, L.titles };
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
	// rows being rebuilt on the worker thread: a note in the corner over the last ones
	if (d.job) {
		dc.SelectFont(m_hFont);
		std::wstring note = L"Working\x2026";
		SIZE size{};
		dc.GetTextExtent(note.c_str(), (int)note.size(), &size);
		RECT box{ w - size.cx - Scale(28), h - size.cy - Scale(18), w - Scale(8), h - Scale(8) };
		Fill(dc, box, c.headerBack);
		Frame(dc, box, 1, c.grid);
		dc.SetTextColor(c.headerText);
		dc.DrawText(note.c_str(), (int)note.size(), &box, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
	}
	dc.SelectFont(oldFont);
	if (drawFlags & CDRF_NOTIFYPOSTPAINT) {
		cd.nmcd.dwDrawStage = CDDS_POSTPAINT;
		cd.nmcd.rc = RECT{ 0, 0, w, h };
		cd.nmcd.dwItemSpec = 0;
		cd.nmcd.uItemState = 0;
		cd.iRow = -1;
		cd.idColumn = 0;
		FireNotify(&cd.nmcd.hdr, NM_CUSTOMDRAW);
	}

	// a virtual grid keeps the cells around the rows in view
	if (d.virtualMode) {
		int64_t full = DgFullRows(L, h);
		d.cache.Trim(first - full, last + full);
	}
}

void CDataGridCtrl::PaintGroupRow(HDC hdc, const DgGroup& group, int y) {
	auto& d = *m_Data;
	auto& c = d.colors;
	auto& L = d.layout;
	CDCHandle dc(hdc);
	int w = d.clientWidth, pad = Scale(6), h = L.rowHeight;
	Fill(dc, RECT{ L.rowHeader, y, w, y + h }, Blend(c.back, c.text, 7));
	if (L.rowHeader > 0) {
		Fill(dc, RECT{ 0, y, L.rowHeader, y + h }, c.headerBack);
		VLine(dc, L.rowHeader - 1, y, y + h, c.grid);
	}
	// open or closed, indented by its level
	bool open = !d.collapsed.count(group.key);
	int left = L.rowHeader + Scale(4) + group.level * Scale(16);
	RECT mark{ left, y, left + Scale(16), y + h };
	PaintGlyph(dc, open ? Glyph::Down : Glyph::Right, mark, Blend(c.back, c.text, 65));

	// the aggregates in their columns, the label up to the first of them
	auto column = d.columns.Find(group.id);
	int labelLeft = mark.right + Scale(2), labelRight = w;
	HFONT old = dc.SelectFont(m_hFont);
	for (auto& a : group.aggregates) {
		int i = L.IndexOf(a.id);
		auto aggColumn = d.columns.Find(a.id);
		if (i < 0 || !aggColumn)
			continue;
		auto text = DgAggregateText(a, *aggColumn, d.numberFormat, DgValueText);
		RECT cell{ L.Left(i, d.scrollX), y, L.Right(i, d.scrollX), y + h };
		RECT area{ i < L.frozenCount ? L.rowHeader : L.ScrollLeft(), y, i < L.frozenCount ? L.ScrollLeft() : w, y + h };
		RECT visible;
		if (text.empty() || !::IntersectRect(&visible, &cell, &area))
			continue;
		if (visible.left > labelLeft + Scale(60))
			labelRight = std::min(labelRight, (int)visible.left - pad);
		dc.SaveDC();
		dc.IntersectClipRect(&visible);
		RECT inner{ cell.left + pad, y, cell.right - pad, y + h - 1 };
		dc.SetTextColor(Blend(c.back, c.text, 80));
		dc.DrawText(text.c_str(), (int)text.size(), &inner, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | AlignFlags(DgEffectiveAlign(*aggColumn)));
		dc.RestoreDC(-1);
	}
	std::wstring value = group.value.type == DgType::Empty ? L"(Blank)" : column ? DgValueText(group.value, *column, d.numberFormat) : L"";
	std::wstring label = (column ? column->title + L": " : L"") + value + L"    " + DgFormatInt(group.count, true, d.numberFormat)
		+ (group.count == 1 ? L" row" : L" rows");
	dc.SelectFont(m_hBold);
	dc.SetTextColor(c.text);
	RECT text{ labelLeft, y, labelRight, y + h - 1 };
	dc.DrawText(label.c_str(), (int)label.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	dc.SelectFont(old);
	HLine(dc, L.rowHeader, w, y + h - 1, c.grid);
}
