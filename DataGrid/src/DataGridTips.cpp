#include "DataGridData.h"

#include <algorithm>

// The tooltip with the whole text of a cell or a title that's cut off ("..."), shown right over the text, as a list
// view's (DGVS_NOTOOLTIPS: none). One tool covers the cell under the mouse; the tooltip subclasses the grid for the
// mouse (TTF_SUBCLASS) and lets clicks through (TTF_TRANSPARENT).

void CDataGridCtrl::UpdateTip(POINT pt) {
	auto& d = *m_Data;
	auto& L = d.layout;
	if ((GetStyle() & DGVS_NOTOOLTIPS) || d.drag != DgData::Drag::None) {
		HideTip();
		return;
	}
	DGVHITTEST hit{ pt };
	int col = -1;
	Hit(pt, hit, col);
	DgData::TipKey key;
	std::wstring text;
	RECT cell{}, room{};   // the cell, and the room its text has
	bool bold = false;
	if ((hit.flags & DGVHT_HEADER) && col >= 0) {
		auto column = d.columns.Find(L.cols[col].id);
		if (!column)
			return HideTip();
		key = { -1, col };
		text = column->title;
		bold = true;
		cell = { L.Left(col, d.scrollX), 0, L.Right(col, d.scrollX), L.titles };
		room = { cell.left + Scale(6), 0, cell.right - Scale(6), L.titles };
		// what's beside the title: the menu's button or the funnel, the sort's arrow and number
		if ((GetStyle() & DGVS_HEADERMENU) || HasFilter(column->id))
			room.right -= Scale(20);
		auto k = std::find_if(d.sortKeys.begin(), d.sortKeys.end(), [&](auto& s) { return s.id == column->id; });
		if (k != d.sortKeys.end())
			room.right -= Scale(11) + (d.sortKeys.size() > 1 ? d.digitWidth * 2 + Scale(3) : 0);
	}
	else if ((hit.flags & DGVHT_CELL) && hit.iRow >= 0 && col >= 0) {
		auto column = d.columns.Find(L.cols[col].id);
		if (!column || column->type == DgColType::Bool || column->type == DgColType::Progress)
			return HideTip();   // drawn, not text
		if (d.edit.active && d.edit.row == hit.iRow && d.edit.id == column->id)
			return HideTip();   // its editor is over it
		DgCell value;
		if (!Fetch(hit.iRow, column->id, value) || value.value.type == DgType::Empty)
			return HideTip();
		key = { hit.iViewRow, col };
		text = DgCellText(value, *column, d.numberFormat);
		int y = L.header + (int)(hit.iViewRow - d.topRow) * L.rowHeight;
		cell = { L.Left(col, d.scrollX), y, L.Right(col, d.scrollX), y + L.rowHeight };
		room = { cell.left + Scale(6), y, cell.right - Scale(6), y + L.rowHeight };
		if (column->type == DgColType::ImageText && d.images && value.image >= 0) {
			int cx = 0, cy = 0;
			::ImageList_GetIconSize(d.images, &cx, &cy);
			room.left += cx + Scale(4);
		}
		if (!d.rules.empty()) {   // a rule's bold is wider
			auto look = DgResolveLook(d.rules, column->id,
				[&](int id) -> const DgValue* {
					static thread_local DgCell held;
					return Fetch(hit.iRow, id, held) ? &held.value : nullptr;
				},
				[&](int id) { return (const DgColumn*)d.columns.Find(id); }, d.numberFormat, DgValueText);
			bold = look.bold;
		}
	}
	else {
		return HideTip();
	}
	if (key == d.tipKey)
		return;   // the same, shown or waiting
	HideTip();
	d.tipKey = key;

	// cut off? (the text as drawn: one line)
	CClientDC dc(m_hWnd);
	HFONT old = dc.SelectFont(bold ? m_hBold : m_hFont);
	SIZE size{};
	dc.GetTextExtent(text.c_str(), (int)text.size(), &size);
	dc.SelectFont(old);
	if (size.cx <= room.right - room.left)
		return;   // all of it shows

	// the part of the cell in view (frozen columns cover the scrolled ones)
	RECT area{ col < L.frozenCount ? L.rowHeader : L.ScrollLeft(), 0, col < L.frozenCount ? L.ScrollLeft() : d.clientWidth, d.clientHeight };
	RECT visible;
	if (!::IntersectRect(&visible, &cell, &area))
		return;
	if (!d.tip) {
		d.tip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0,
			m_hWnd, nullptr, nullptr, nullptr);
		TTTOOLINFOW ti{ sizeof(ti) };
		ti.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
		ti.hwnd = m_hWnd;
		ti.uId = 1;
		ti.lpszText = LPSTR_TEXTCALLBACKW;
		::SendMessage(d.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	}
	::SendMessage(d.tip, TTM_SETMAXTIPWIDTH, 0, Scale(600));   // longer: wrapped
	::SendMessage(d.tip, WM_SETFONT, (WPARAM)(bold ? m_hBold : m_hFont), FALSE);
	d.tipText = std::move(text);
	d.tipBold = bold;
	int top = (cell.top + cell.bottom - d.textHeight) / 2;   // the text's line, as drawn
	d.tipRect = { std::max(room.left, visible.left), top, room.right, top + d.textHeight };
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	ti.rect = visible;
	::SendMessage(d.tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

void CDataGridCtrl::HideTip() {
	auto& d = *m_Data;
	d.tipKey = {};
	if (!d.tip)
		return;
	::SendMessage(d.tip, TTM_POP, 0, 0);
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;   // an empty rect: nothing to show until the mouse finds a cut-off text
	::SendMessage(d.tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

LRESULT CDataGridCtrl::TipNotify(NMHDR* nm) {
	auto& d = *m_Data;
	switch (nm->code) {
		case TTN_GETDISPINFOW:
			((NMTTDISPINFOW*)nm)->lpszText = d.tipText.data();
			return 0;
		case TTN_SHOW: {   // over the text, its letters on the cell's
			RECT rc = d.tipRect;
			::MapWindowPoints(m_hWnd, nullptr, (POINT*)&rc, 2);
			::SendMessage(d.tip, TTM_ADJUSTRECT, TRUE, (LPARAM)&rc);
			::SetWindowPos(d.tip, nullptr, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			return TRUE;
		}
	}
	return 0;
}
