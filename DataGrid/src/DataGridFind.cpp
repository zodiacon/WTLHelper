#include "DataGridData.h"

#include <algorithm>

// Finding cells by their shown text (DgFindCell over the rows in view), the find box (Ctrl+F) and the cells it shades.

namespace {
	constexpr DWORD TypingBudget = 150;   // ms a keystroke in the find box may look for

	std::wstring WindowText(HWND hwnd) {
		std::wstring text(::GetWindowTextLengthW(hwnd), L'\0');
		::GetWindowTextW(hwnd, text.data(), (int)text.size() + 1);
		return text;
	}
}

BOOL CDataGridCtrl::Find(LPCWSTR text, UINT flags) {
	auto& d = *m_Data;
	if (!text || !*text) {   // the last text again, as F3 (the flags as given)
		if (d.findText.empty())
			return FALSE;
		auto last = d.findText;
		return FindFrom(last, flags, false, false) > 0;
	}
	return FindFrom(text, flags, false, false) > 0;
}

int CDataGridCtrl::FindFrom(const std::wstring& text, UINT flags, bool inclusive, bool beep, DWORD budget) {
	auto& d = *m_Data;
	d.findText = text;
	d.findFlags = flags & ~DGVFF_BACKWARD;   // F3 goes on forward, Shift+F3 back
	if (text.empty() || !CommitOrCancel())
		return 0;
	int n = d.ColCount();
	int64_t rows = d.RowCount();
	if (n == 0 || rows == 0)
		return 0;
	DgPos start{};
	if (d.HasCaret() && d.rows.ToView(d.caretRow) >= 0)
		start = d.CaretPos();
	else
		inclusive = true;   // no caret: from the first cell
	int first = 0, last = n - 1;
	if (flags & DGVFF_COLUMN)
		first = last = start.col;

	auto found = [&](int64_t viewRow, int col) {
		Select(SelOp::Reset, viewRow, col, true);
		return 1;
	};
	if (d.virtualMode) {   // the app may know better than asking for every cell
		NMDGVFIND nm{};
		nm.pszText = text.c_str();
		nm.flags = flags;
		nm.iStartRow = d.HasCaret() ? d.caretRow : -1;
		nm.idStartColumn = d.caretId;
		nm.fInclusive = inclusive;
		nm.iRow = -1;
		if (FireNotify(&nm.hdr, DGVN_FINDCELL)) {
			int64_t view = d.rows.ToView(nm.iRow);
			int col = d.layout.IndexOf(nm.idColumn);
			if (view >= 0 && col >= 0)
				return found(view, col);
			if (beep)
				::MessageBeep(MB_OK);
			return 0;
		}
	}
	// the columns' objects once, not per cell
	std::vector<const DgColumn*> columns;
	for (auto& c : d.layout.cols)
		columns.push_back(d.columns.Find(c.id));
	ULONGLONG deadline = budget ? ::GetTickCount64() + budget : 0;
	bool late = false;
	DgPos at;
	bool ok = DgFindCell(start, inclusive, flags, rows, first, last, d.virtualMode ? DGV_VIRTUALFINDROWS : INT64_MAX,
		[&](int64_t r, int c) {
			int64_t data = d.rows.ToData(r);
			auto column = columns[(size_t)c];
			DgCell cell;
			if (data < 0 || !column || !Fetch(data, column->id, cell, false) || cell.value.type == DgType::Empty)
				return false;   // the cells looked at aren't kept: they'd fill the virtual grid's cache
			return DgTextMatches(DgCellText(cell, *column, d.numberFormat), text, flags);
		}, at, nullptr,
		[&] { return late = late || (deadline && ::GetTickCount64() > deadline); });
	if (!ok) {
		if (late)
			return -1;
		if (beep)
			::MessageBeep(MB_OK);
		return 0;
	}
	return found(at.row, at.col);
}

void CDataGridCtrl::SetHighlight(LPCWSTR text, UINT flags) {
	auto& d = *m_Data;
	d.highlight = text ? text : L"";
	d.highlightFlags = flags;
	if (m_hWnd)
		Invalidate();
}

// ---- The find box -----------------------------------------------------------------------------------------------

bool CDataGridCtrl::IsFindBoxShown() const {
	return m_FindEdit.m_hWnd != nullptr;
}

void CDataGridCtrl::ShowFindBox(bool show) {
	auto& d = *m_Data;
	if (!show) {
		CloseFindBox();
		return;
	}
	if (!CommitOrCancel())
		return;
	if (!m_FindEdit.m_hWnd) {
		HWND edit = ::CreateWindowExW(0, WC_EDITW, d.findText.c_str(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd,
			nullptr, nullptr, nullptr);
		if (!edit)
			return;
		::SendMessage(edit, WM_SETFONT, (WPARAM)m_hFont, 0);
		int margin = Scale(4);
		::SendMessage(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(margin, margin));
		::SendMessage(edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Find");
		m_FindEdit.SubclassWindow(edit);
		PositionFindBox();
		::ShowWindow(edit, SW_SHOW);
		d.findMiss = false;
		SetHighlight(d.findText.c_str(), d.findFlags);
	}
	m_FindEdit.SendMessage(EM_SETSEL, 0, -1);
	m_FindEdit.SetFocus();
}

void CDataGridCtrl::CloseFindBox() {
	auto& d = *m_Data;
	if (!m_FindEdit.m_hWnd)
		return;
	HWND edit = m_FindEdit.UnsubclassWindow();
	if (::GetFocus() == edit)
		SetFocus();
	::DestroyWindow(edit);
	d.findMiss = false;
	SetHighlight(nullptr);
}

void CDataGridCtrl::PositionFindBox() {
	auto& d = *m_Data;
	if (!m_FindEdit.m_hWnd)
		return;
	int w = std::min(Scale(220), std::max(Scale(80), d.clientWidth - Scale(16)));
	int h = d.textHeight + Scale(8);
	m_FindEdit.SetWindowPos(nullptr, d.clientWidth - w - Scale(8), d.layout.header + Scale(6), w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CDataGridCtrl::OnFindChanged() {
	auto& d = *m_Data;
	auto text = WindowText(m_FindEdit.m_hWnd);
	SetHighlight(text.c_str(), 0);
	// from the caret's cell, so typing more stays on it while it still matches; for a short while only, so the typing
	// keeps up (Enter looks through all of it)
	d.findMiss = !text.empty() && FindFrom(text, 0, true, false, TypingBudget) == 0;
	d.findText = text;
	m_FindEdit.Invalidate();
}

LRESULT CDataGridCtrl::OnFindKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	switch (wParam) {
		case VK_RETURN:
		case VK_F3: {
			auto text = WindowText(m_FindEdit.m_hWnd);
			d.findMiss = !text.empty() && FindFrom(text, shift ? DGVFF_BACKWARD : 0, false, true) == 0;
			m_FindEdit.Invalidate();
			return 0;
		}
		case VK_ESCAPE:
			CloseFindBox();
			return 0;
		case VK_TAB:   // back to the cells, the box staying
			SetFocus();
			return 0;
	}
	bHandled = FALSE;
	return 0;
}

bool CDataGridCtrl::FindKey(WPARAM key, bool ctrl, bool shift) {
	auto& d = *m_Data;
	bool box = !(GetStyle() & DGVS_NOFINDBOX);
	if (key == 'F' && ctrl && !shift && box) {
		ShowFindBox(true);
		return true;
	}
	if (key == VK_F3 && !ctrl) {
		if (d.findText.empty()) {
			if (box)
				ShowFindBox(true);
			return true;
		}
		FindFrom(d.findText, d.findFlags | (shift ? DGVFF_BACKWARD : 0), false, true);
		return true;
	}
	if (key == VK_ESCAPE && m_FindEdit.m_hWnd) {
		CloseFindBox();
		return true;
	}
	return false;
}
