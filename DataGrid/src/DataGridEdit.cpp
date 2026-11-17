#include "DataGridData.h"

#include <oleauto.h>

#include <algorithm>

#pragma comment(lib, "oleaut32.lib")

// Editing cells (DGVS_EDITABLE): the editors over a cell (an edit box, a drop-down list for enums, a date picker),
// check boxes toggled in place, pasting, clearing, and undo. Every change by the user goes through Validate and Apply.

namespace {
	constexpr size_t MaxPasteCells = 1'000'000;

	std::wstring WindowText(HWND hwnd) {
		std::wstring text(::GetWindowTextLengthW(hwnd), L'\0');
		::GetWindowTextW(hwnd, text.data(), (int)text.size() + 1);
		return text;
	}

	std::wstring ClipboardText(HWND owner) {
		std::wstring text;
		if (!::OpenClipboard(owner))
			return text;
		if (HANDLE h = ::GetClipboardData(CF_UNICODETEXT)) {
			if (auto p = (const wchar_t*)::GlobalLock(h)) {
				text.assign(p, wcsnlen(p, ::GlobalSize(h) / sizeof(wchar_t)));
				::GlobalUnlock(h);
			}
		}
		::CloseClipboard();
		return text;
	}

	// the user's date and time format, for the date picker
	std::wstring DateTimeFormat() {
		wchar_t date[80] = L"", time[80] = L"";
		::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, date, _countof(date));
		::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTTIME, time, _countof(time));
		// the picker's format takes literal text in quotes: the locale's time format already uses them that way
		return std::wstring(date) + L" " + time;
	}
}

bool DgParseUserDate(const std::wstring& text, int64_t& ticks) {
	DATE date;
	SYSTEMTIME st;
	FILETIME ft;
	if (FAILED(::VarDateFromStr(text.c_str(), LOCALE_USER_DEFAULT, 0, &date)) || !::VariantTimeToSystemTime(date, &st)
		|| !::SystemTimeToFileTime(&st, &ft))
		return false;
	ticks = DgTicksOf(ft);
	return true;
}

// ---- Where cells are -----------------------------------------------------------------------------------

bool CDataGridCtrl::CellRect(LONGLONG viewRow, int displayCol, RECT& rc) const {
	auto& d = *m_Data;
	auto& L = d.layout;
	if (displayCol < 0 || displayCol >= (int)L.cols.size() || viewRow < d.topRow)
		return false;
	int64_t offset = viewRow - d.topRow;
	if (offset > d.clientHeight / L.rowHeight + 1)
		return false;
	int y = L.header + (int)offset * L.rowHeight;
	RECT cell{ L.Left(displayCol, d.scrollX), y, L.Right(displayCol, d.scrollX), y + L.rowHeight };
	RECT area{ displayCol < L.frozenCount ? L.rowHeader : L.ScrollLeft(), L.header,
		displayCol < L.frozenCount ? L.ScrollLeft() : d.clientWidth, d.clientHeight };
	return ::IntersectRect(&rc, &cell, &area) != FALSE;
}

RECT CDataGridCtrl::CheckRect(const RECT& cell, int align) const {
	auto& d = *m_Data;
	int pad = Scale(6);
	int box = std::min(Scale(13), d.layout.rowHeight - Scale(6));
	int x = align == DgAlign::Left ? cell.left + pad : align == DgAlign::Right ? cell.right - pad - box : (cell.left + cell.right - box) / 2;
	int y = (cell.top + cell.bottom - box) / 2;
	return { x, y, x + box, y + box };
}

// ---- Opening and closing an editor ------------------------------------------------------------------

bool CDataGridCtrl::IsEditing() const {
	return m_Data->edit.active;
}

bool CDataGridCtrl::CanEdit(LONGLONG row, int id) const {
	auto& d = *m_Data;
	auto column = d.columns.Find(id);
	return m_hWnd && (GetStyle() & DGVS_EDITABLE) && column && !(column->flags & DgColFlag::ReadOnly) && row >= 0
		&& row < d.DataRowCount();
}

BOOL CDataGridCtrl::BeginEdit(LONGLONG row, int id) {
	return StartEdit(row, id, nullptr);
}

bool CDataGridCtrl::StartEdit(LONGLONG dataRow, int id, const wchar_t* typed) {
	auto& d = *m_Data;
	if (d.edit.active && !EndEdit(true))
		return false;
	auto column = d.columns.Find(id);
	int64_t view = d.rows.ToView(dataRow);
	int col = d.layout.IndexOf(id);
	if (!CanEdit(dataRow, id) || !column || column->type == DgColType::Bool || view < 0 || col < 0)
		return false;
	if (d.caretRow != dataRow || d.caretId != id)
		Select(SelOp::Reset, view, col, true);
	EnsureVisible(dataRow, id);

	NMDGVEDIT nm{};
	nm.iRow = dataRow;
	nm.idColumn = id;
	if (FireNotify(&nm.hdr, DGVN_BEGINEDIT))
		return false;

	RECT rc;
	if (!CellRect(view, col, rc))
		return false;
	DgCell old;
	Fetch(dataRow, id, old);

	HWND editor = nullptr;
	auto kind = DgData::Edit::Kind::Text;
	switch (column->type) {
		case DgColType::Enum: {
			kind = DgData::Edit::Kind::List;
			editor = ::CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST, rc.left, rc.top,
				rc.right - rc.left, Scale(240), m_hWnd, nullptr, nullptr, nullptr);
			::SendMessage(editor, WM_SETFONT, (WPARAM)m_hFont, 0);
			for (auto& item : column->enumItems)
				::SendMessage(editor, CB_ADDSTRING, 0, (LPARAM)item.c_str());
			int selected = old.value.type == DgType::Int ? (int)old.value.i : -1;
			if (typed && *typed) {   // the first item starting with what was typed
				for (size_t i = 0; i < column->enumItems.size(); i++)
					if (!column->enumItems[i].empty() && towlower(column->enumItems[i][0]) == towlower(*typed)) {
						selected = (int)i;
						break;
					}
			}
			::SendMessage(editor, CB_SETCURSEL, selected, 0);
			// the closed box as tall as the cell
			::SendMessage(editor, CB_SETITEMHEIGHT, (WPARAM)-1, std::max(1, (int)(rc.bottom - rc.top) - Scale(6)));
			break;
		}
		case DgColType::DateTime: {
			kind = DgData::Edit::Kind::Date;
			editor = ::CreateWindowExW(0, DATETIMEPICK_CLASSW, nullptr, WS_CHILD | DTS_SHOWNONE, rc.left, rc.top,
				rc.right - rc.left, rc.bottom - rc.top, m_hWnd, nullptr, nullptr, nullptr);
			::SendMessage(editor, WM_SETFONT, (WPARAM)m_hFont, 0);
			auto format = DateTimeFormat();
			::SendMessage(editor, DTM_SETFORMATW, 0, (LPARAM)format.c_str());
			SYSTEMTIME st{};
			FILETIME ft = DgFileTimeOf(old.value.i);
			if (old.value.type == DgType::DateTime && ::FileTimeToSystemTime(&ft, &st)) {
				::SendMessage(editor, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
			}
			else {
				::GetLocalTime(&st);   // empty: today, unchecked
				::SendMessage(editor, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
				::SendMessage(editor, DTM_SETSYSTEMTIME, GDT_NONE, 0);
			}
			break;
		}
		default: {
			int align = DgEffectiveAlign(*column);
			DWORD style = WS_CHILD | ES_AUTOHSCROLL | (align == DgAlign::Right ? ES_RIGHT : align == DgAlign::Center ? ES_CENTER : ES_LEFT);
			auto text = typed ? std::wstring(typed) : DgEditText(old.value, *column, d.numberFormat);
			// as tall as the text, in the middle of the cell, so it lines up with the other cells' text
			int h = std::min((int)(rc.bottom - rc.top) - 2, d.textHeight + Scale(4));
			int y = (rc.top + rc.bottom - h) / 2;
			editor = ::CreateWindowExW(0, WC_EDITW, text.c_str(), style, rc.left + 1, y, rc.right - rc.left - 2, h, m_hWnd,
				nullptr, nullptr, nullptr);
			::SendMessage(editor, WM_SETFONT, (WPARAM)m_hFont, 0);
			int margin = Scale(6) - 1;
			::SendMessage(editor, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(margin, margin));
			::SendMessage(editor, EM_SETSEL, text.size(), text.size());
			break;
		}
	}
	if (!editor)
		return false;
	d.edit.active = true;
	d.edit.row = dataRow;
	d.edit.id = id;
	d.edit.kind = kind;
	d.edit.typed = typed != nullptr && kind == DgData::Edit::Kind::Text;
	d.edit.dirty = typed != nullptr && kind != DgData::Edit::Kind::Date;   // typing replaced the value
	m_Editor.SubclassWindow(editor);
	::ShowWindow(editor, SW_SHOW);
	::SetFocus(editor);
	InvalidateRow(view);
	return true;
}

BOOL CDataGridCtrl::EndEdit(bool commit) {
	auto& d = *m_Data;
	if (!d.edit.active)
		return TRUE;
	auto column = d.columns.Find(d.edit.id);
	// untouched, nothing is written: a value an editor can't hold exactly (a date's fraction of a second, a float's
	// digits past what's shown) stays as it was
	if (commit && column && d.edit.dirty) {
		DgParsed parsed;
		std::wstring text;
		HWND editor = m_Editor.m_hWnd;
		switch (d.edit.kind) {
			case DgData::Edit::Kind::Text:
				text = WindowText(editor);
				parsed = DgParseValue(text, *column, d.numberFormat, DgParseUserDate);
				break;
			case DgData::Edit::Kind::List: {
				int index = (int)::SendMessage(editor, CB_GETCURSEL, 0, 0);
				parsed.ok = true;
				if (index >= 0) {
					parsed.value = DgValue::Int(index);
					text = column->enumItems[(size_t)index];
				}
				break;
			}
			case DgData::Edit::Kind::Date: {
				SYSTEMTIME st{};
				FILETIME ft;
				parsed.ok = true;
				if (::SendMessage(editor, DTM_GETSYSTEMTIME, 0, (LPARAM)&st) == GDT_VALID && ::SystemTimeToFileTime(&st, &ft)) {
					parsed.value = DgValue::DateTime(DgTicksOf(ft));
					text = WindowText(editor);
				}
				break;
			}
		}
		int64_t view = d.rows.ToView(d.edit.row);
		int col = d.layout.IndexOf(d.edit.id);
		if (!parsed.ok) {
			ShowError(parsed.error, view, col);
			return FALSE;
		}
		DgCell old;
		Fetch(d.edit.row, d.edit.id, old);
		DgCell cell{ parsed.value, old.image };
		std::wstring error;
		if (!Validate(d.edit.row, d.edit.id, cell, text, DGVCR_EDIT, error)) {
			ShowError(error, view, col);
			return FALSE;
		}
		if (!(cell.value == old.value))
			Apply({ DgCellChange{ d.edit.row, d.edit.id, old, cell } }, DGVCR_EDIT, true);
	}
	// closed first, so the focus moving off it doesn't end it again
	d.edit.active = false;
	HideError();
	HWND editor = m_Editor.UnsubclassWindow();
	if (::GetFocus() == editor || ::IsChild(editor, ::GetFocus()))
		SetFocus();
	::DestroyWindow(editor);
	NMDGVEDIT nm{};
	nm.iRow = d.edit.row;
	nm.idColumn = d.edit.id;
	nm.fCanceled = !commit;
	FireNotify(&nm.hdr, DGVN_ENDEDIT);
	InvalidateRow(d.rows.ToView(d.edit.row));
	return TRUE;
}

bool CDataGridCtrl::CommitOrCancel() {
	if (!m_Data->edit.active)
		return true;
	return EndEdit(true) != FALSE;
}

void CDataGridCtrl::PositionEditor() {
	auto& d = *m_Data;
	if (!d.edit.active || !m_Editor.m_hWnd)
		return;
	RECT rc;
	int64_t view = d.rows.ToView(d.edit.row);
	if (view < 0 || !CellRect(view, d.layout.IndexOf(d.edit.id), rc)) {   // its cell went out of view
		if (!EndEdit(true))
			EndEdit(false);
		return;
	}
	RECT now;
	m_Editor.GetWindowRect(&now);
	ScreenToClient(&now);
	int h = now.bottom - now.top;
	int y = d.edit.kind == DgData::Edit::Kind::Text ? (rc.top + rc.bottom - h) / 2 : rc.top;
	int x = d.edit.kind == DgData::Edit::Kind::Text ? rc.left + 1 : rc.left;
	int w = d.edit.kind == DgData::Edit::Kind::Text ? rc.right - rc.left - 2 : rc.right - rc.left;
	m_Editor.SetWindowPos(nullptr, x, y, w, d.edit.kind == DgData::Edit::Kind::Date ? rc.bottom - rc.top : h,
		SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---- The editor's messages ------------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnEditorKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	HWND editor = m_Editor.m_hWnd;
	if (d.edit.kind == DgData::Edit::Kind::List && ::SendMessage(editor, CB_GETDROPPEDSTATE, 0, 0)) {
		if (wParam == VK_ESCAPE) {   // the list closes; the editor stays
			bHandled = FALSE;
			return 0;
		}
		if (wParam == VK_RETURN)
			::SendMessage(editor, CB_SHOWDROPDOWN, FALSE, 0);
	}
	if (d.edit.kind == DgData::Edit::Kind::Date && ::SendMessage(editor, DTM_GETMONTHCAL, 0, 0)) {
		bHandled = FALSE;   // the calendar is open: its keys
		return 0;
	}
	if (wParam == VK_F2 && d.edit.kind == DgData::Edit::Kind::Text) {   // between moving on with the arrows and in the text
		d.edit.typed = !d.edit.typed;
		return 0;
	}
	auto action = DgEditKey((unsigned)wParam, shift, d.edit.typed);
	if (action == DgEditAction::None) {
		bHandled = FALSE;
		return 0;
	}
	if (action == DgEditAction::Cancel) {
		EndEdit(false);
		return 0;
	}
	if (!EndEdit(true))
		return 0;
	DgKey key;
	switch (action) {
		case DgEditAction::CommitDown:  key = DgKey::Down; break;
		case DgEditAction::CommitUp:    key = DgKey::Up; break;
		case DgEditAction::CommitRight: key = DgKey::Right; break;
		case DgEditAction::CommitLeft:  key = DgKey::Left; break;
		default: return 0;
	}
	auto pos = DgNavigate(d.CaretPos(), key, false, d.RowCount(), d.ColCount(), 1);
	Select(SelOp::Reset, pos.row, pos.col, true);
	return 0;
}

LRESULT CDataGridCtrl::OnEditorChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == L'\r' || wParam == L'\t' || wParam == 27)
		return 0;   // handled as keys: no beep
	bHandled = FALSE;
	return 0;
}

LRESULT CDataGridCtrl::OnEditorGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTALLKEYS | m_Editor.DefWindowProc();
}

LRESULT CDataGridCtrl::OnEditorKillFocus(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bHandled = FALSE;
	auto& d = *m_Data;
	HWND to = (HWND)wParam;
	if (!d.edit.active || to == m_Editor.m_hWnd || ::IsChild(m_Editor.m_hWnd, to))
		return 0;
	if (d.edit.kind == DgData::Edit::Kind::Date && ::SendMessage(m_Editor.m_hWnd, DTM_GETMONTHCAL, 0, 0))
		return 0;   // its calendar dropped down
	PostMessage(WM_DGV_ENDEDIT);   // after the focus has settled
	return 0;
}

LRESULT CDataGridCtrl::OnEditorCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.edit.active && (HWND)lParam == m_Editor.m_hWnd && (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE))
		d.edit.dirty = true;
	if (m_FilterEdit.m_hWnd && (HWND)lParam == m_FilterEdit.m_hWnd && HIWORD(wParam) == EN_CHANGE)
		SetTimer(DgData::FilterTimer, 300);   // filters once the typing pauses
	return 0;
}

LRESULT CDataGridCtrl::OnEditorNotify(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	auto nm = (NMHDR*)lParam;
	if (d.edit.active && nm->hwndFrom == m_Editor.m_hWnd && nm->code == DTN_DATETIMECHANGE)
		d.edit.dirty = true;
	return 0;
}

LRESULT CDataGridCtrl::OnEndEditPosted(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	HWND focus = ::GetFocus();
	if (!d.edit.active || focus == m_Editor.m_hWnd || ::IsChild(m_Editor.m_hWnd, focus))
		return 0;
	if (!EndEdit(true)) {   // the focus went elsewhere with a value that can't be kept
		::MessageBeep(MB_ICONWARNING);
		EndEdit(false);
	}
	return 0;
}

LRESULT CDataGridCtrl::OnCtlColor(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	UpdateColors();
	if (d.editBrushColor != d.colors.back) {
		if (d.editBrush)
			::DeleteObject(d.editBrush);
		d.editBrush = ::CreateSolidBrush(d.colors.back);
		d.editBrushColor = d.colors.back;
	}
	::SetTextColor((HDC)wParam, d.colors.text);
	::SetBkColor((HDC)wParam, d.colors.back);
	return (LRESULT)d.editBrush;
}

// ---- Keys and typing on the grid --------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnChar(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.edit.active || wParam < L' ' || wParam == 127 || !(GetStyle() & DGVS_EDITABLE) || d.caretRow < 0)
		return 0;
	bool ctrl = ::GetKeyState(VK_CONTROL) < 0, alt = ::GetKeyState(VK_MENU) < 0;
	if (ctrl && !alt)   // a shortcut (Ctrl+Alt is AltGr: a character)
		return 0;
	if (wParam == L' ' && (ctrl || ::GetKeyState(VK_SHIFT) < 0))
		return 0;   // Shift+Space and Ctrl+Space select
	auto column = d.columns.Find(d.caretId);
	if (!column || column->type == DgColType::Bool)
		return 0;   // Space toggles those (WM_KEYDOWN)
	wchar_t typed[2] = { (wchar_t)wParam, 0 };
	if (!StartEdit(d.caretRow, d.caretId, typed))
		::MessageBeep(MB_OK);
	return 0;
}

bool CDataGridCtrl::EditKey(WPARAM key, bool ctrl, bool shift) {
	auto& d = *m_Data;
	if (!(GetStyle() & DGVS_EDITABLE))
		return false;
	switch (key) {
		case VK_F2:
			if (ctrl || shift || d.caretRow < 0)
				return false;
			if (!StartEdit(d.caretRow, d.caretId, nullptr))
				::MessageBeep(MB_OK);
			return true;
		case VK_DELETE:
			if (ctrl)
				return false;
			if (shift)
				Cut();
			else
				ClearSelectedCells();
			return true;
		case VK_INSERT:
			if (!shift || ctrl)
				return false;
			Paste();
			return true;
		case 'V':
			if (!ctrl || shift)
				return false;
			Paste();
			return true;
		case 'X':
			if (!ctrl || shift)
				return false;
			Cut();
			return true;
		case 'Z':
			if (!ctrl)
				return false;
			if (shift)
				Redo();
			else
				Undo();
			return true;
		case 'Y':
			if (!ctrl || shift)
				return false;
			Redo();
			return true;
		case VK_SPACE: {
			if (ctrl || shift || d.caretRow < 0)
				return false;
			auto column = d.columns.Find(d.caretId);
			if (!column || column->type != DgColType::Bool)
				return false;
			auto caret = d.CaretPos();
			ToggleChecks(caret.row, caret.col, true);
			return true;
		}
	}
	return false;
}

// ---- Changing cells --------------------------------------------------------------------------------------

bool CDataGridCtrl::Validate(LONGLONG dataRow, int id, DgCell& cell, const std::wstring& text, UINT reason, std::wstring& error) {
	NMDGVVALIDATE nm{};
	nm.iRow = dataRow;
	nm.idColumn = id;
	nm.reason = reason;
	std::wstring valueText = cell.value.s;
	nm.value = DgToPublic(cell.value, valueText.c_str());
	nm.pszText = text.c_str();
	if (FireNotify(&nm.hdr, DGVN_VALIDATE)) {
		error = nm.szError[0] ? nm.szError : L"This value isn't allowed here.";
		return false;
	}
	cell.value = DgFromPublic(nm.value);   // the app may have changed it
	return true;
}

void CDataGridCtrl::WriteCell(LONGLONG dataRow, int id, const DgCell& cell) {
	auto& d = *m_Data;
	auto column = d.columns.Find(id);
	if (!column)
		return;
	if (d.virtualMode) {
		NMDGVSETCELL nm{};
		nm.iRow = dataRow;
		nm.idColumn = id;
		nm.value = DgToPublic(cell.value, cell.value.s.c_str());
		nm.iImage = cell.image;
		FireNotify(&nm.hdr, DGVN_SETCELL);
		d.cache.Drop(dataRow, dataRow);
	}
	else {
		d.store.Set(dataRow, column->slot, cell);
	}
	InvalidateRow(d.rows.ToView(dataRow));
}

void CDataGridCtrl::Apply(std::vector<DgCellChange> changes, UINT reason, bool undoable, bool forward) {
	auto& d = *m_Data;
	if (changes.empty())
		return;
	if (forward) {
		for (auto& c : changes)
			WriteCell(c.row, c.id, c.after);
	}
	else {
		for (auto it = changes.rbegin(); it != changes.rend(); ++it)
			WriteCell(it->row, it->id, it->before);
	}
	NMDGVCHANGE nm{};
	nm.reason = reason;
	nm.cCells = (int)std::min<size_t>(changes.size(), INT_MAX);
	nm.iFirstRow = changes.front().row;
	nm.idFirstColumn = changes.front().id;
	if (undoable)
		d.undo.Push(DgEditStep{ std::move(changes), (int)reason });
	FireNotify(&nm.hdr, DGVN_CELLSCHANGED);
}

void CDataGridCtrl::ToggleChecks(LONGLONG viewRow, int displayCol, bool selection) {
	auto& d = *m_Data;
	int64_t dataRow = d.rows.ToData(viewRow);
	int id = displayCol >= 0 && displayCol < d.ColCount() ? d.layout.cols[displayCol].id : 0;
	if (!CanEdit(dataRow, id)) {
		::MessageBeep(MB_OK);
		return;
	}
	DgCell first;
	Fetch(dataRow, id, first);
	bool to = !DgChecked(first.value);   // every check box selected takes the clicked one's new state
	std::vector<std::pair<int64_t, int>> cells{ { viewRow, displayCol } };
	if (selection && d.selection.Contains(viewRow, displayCol)) {
		cells.clear();
		for (auto [top, bottom] : d.selection.Rows())
			for (int64_t r = top; r <= bottom && cells.size() < MaxPasteCells; r++)
				for (int c : d.selection.Cols())
					if (d.selection.Contains(r, c))
						cells.emplace_back(r, c);
	}
	std::vector<DgCellChange> changes;
	for (auto [r, c] : cells) {
		int cid = d.layout.cols[c].id;
		auto column = d.columns.Find(cid);
		int64_t row = d.rows.ToData(r);
		if (!column || column->type != DgColType::Bool || !CanEdit(row, cid))
			continue;
		DgCell old;
		Fetch(row, cid, old);
		DgCell cell{ DgValue::Bool(to), old.image };
		std::wstring error;
		if (DgChecked(old.value) == to && old.value.type != DgType::Empty)
			continue;
		if (!Validate(row, cid, cell, to ? L"true" : L"false", DGVCR_TOGGLE, error))
			continue;
		changes.push_back({ row, cid, old, cell });
	}
	Apply(std::move(changes), DGVCR_TOGGLE, true);
}

BOOL CDataGridCtrl::Paste() {
	auto& d = *m_Data;
	if (!(GetStyle() & DGVS_EDITABLE) || !CommitOrCancel())
		return FALSE;
	auto fields = DgParseTsv(ClipboardText(m_hWnd));
	auto targets = DgPasteTargets(fields, d.selection, d.RowCount(), d.ColCount(), MaxPasteCells);
	if (targets.empty())
		return FALSE;
	std::vector<DgCellChange> changes;
	int rejected = 0;
	for (auto& t : targets) {
		int64_t row = d.rows.ToData(t.row);
		int id = d.layout.cols[t.col].id;
		auto column = d.columns.Find(id);
		if (!column || !CanEdit(row, id)) {
			rejected++;
			continue;
		}
		auto parsed = DgParseValue(*t.text, *column, d.numberFormat, DgParseUserDate);
		DgCell old;
		Fetch(row, id, old);
		DgCell cell{ parsed.value, old.image };
		std::wstring error;
		if (!parsed.ok || !Validate(row, id, cell, *t.text, DGVCR_PASTE, error)) {
			rejected++;
			continue;
		}
		if (!(cell.value == old.value))
			changes.push_back({ row, id, old, cell });
	}
	Apply(std::move(changes), DGVCR_PASTE, true);
	if (rejected)
		::MessageBeep(MB_ICONWARNING);
	// what was pasted becomes the selection
	if (!(fields.size() == 1 && fields[0].size() == 1)) {
		DgPos from{ targets.front().row, targets.front().col }, to = from;
		for (auto& t : targets) {
			to.row = std::max(to.row, t.row);
			to.col = std::max(to.col, t.col);
		}
		d.selection.Reset(from, d.ColCount());
		d.selection.Extend(to, d.ColCount());
		Invalidate();
		FireSelChanged();
	}
	return TRUE;
}

BOOL CDataGridCtrl::ClearSelectedCells() {
	auto& d = *m_Data;
	if (!(GetStyle() & DGVS_EDITABLE) || !CommitOrCancel())
		return FALSE;
	std::vector<DgCellChange> changes;
	size_t seen = 0;
	auto cols = d.selection.Cols();
	for (auto [top, bottom] : d.selection.Rows())
		for (int64_t r = top; r <= bottom && seen < MaxPasteCells; r++)
			for (int c : cols) {
				if (!d.selection.Contains(r, c) || ++seen > MaxPasteCells)
					continue;
				int64_t row = d.rows.ToData(r);
				int id = d.layout.cols[c].id;
				if (!CanEdit(row, id))
					continue;
				DgCell old;
				Fetch(row, id, old);
				if (old.value.type == DgType::Empty)
					continue;
				DgCell cell{ DgValue{}, old.image };
				std::wstring error;
				if (Validate(row, id, cell, L"", DGVCR_CLEAR, error))
					changes.push_back({ row, id, old, cell });
			}
	if (seen > MaxPasteCells)
		::MessageBeep(MB_ICONWARNING);
	Apply(std::move(changes), DGVCR_CLEAR, true);
	return TRUE;
}

BOOL CDataGridCtrl::Cut() {
	if (!(GetStyle() & DGVS_EDITABLE) || !CommitOrCancel() || !Copy())
		return FALSE;
	return ClearSelectedCells();
}

// ---- Undo ------------------------------------------------------------------------------------------------

BOOL CDataGridCtrl::Undo() {
	auto& d = *m_Data;
	if (!CommitOrCancel())
		return FALSE;
	auto step = d.undo.Undo();
	if (!step)
		return FALSE;
	Apply(step->changes, DGVCR_UNDO, false, false);
	auto& first = step->changes.front();
	int64_t view = d.rows.ToView(first.row);
	int col = d.layout.IndexOf(first.id);
	if (view >= 0 && col >= 0)
		Select(SelOp::Reset, view, col, true);
	return TRUE;
}

BOOL CDataGridCtrl::Redo() {
	auto& d = *m_Data;
	if (!CommitOrCancel())
		return FALSE;
	auto step = d.undo.Redo();
	if (!step)
		return FALSE;
	Apply(step->changes, DGVCR_REDO, false, true);
	auto& first = step->changes.front();
	int64_t view = d.rows.ToView(first.row);
	int col = d.layout.IndexOf(first.id);
	if (view >= 0 && col >= 0)
		Select(SelOp::Reset, view, col, true);
	return TRUE;
}

bool CDataGridCtrl::CanUndo() const {
	return m_Data->undo.CanUndo();
}

bool CDataGridCtrl::CanRedo() const {
	return m_Data->undo.CanRedo();
}

void CDataGridCtrl::ClearUndo() {
	m_Data->undo.Clear();
}

// ---- The error balloon ----------------------------------------------------------------------------------

void CDataGridCtrl::ShowError(const std::wstring& text, LONGLONG viewRow, int displayCol) {
	auto& d = *m_Data;
	::MessageBeep(MB_ICONWARNING);
	if (!d.errorTip) {
		d.errorTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_BALLOON | TTS_ALWAYSTIP,
			0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		TTTOOLINFOW ti{ sizeof(ti) };
		ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		ti.hwnd = m_hWnd;
		ti.uId = 1;
		ti.lpszText = const_cast<LPWSTR>(L"");
		::SendMessage(d.errorTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		::SendMessage(d.errorTip, TTM_SETMAXTIPWIDTH, 0, Scale(320));
	}
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	ti.lpszText = const_cast<LPWSTR>(text.c_str());
	::SendMessage(d.errorTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	RECT rc{};
	if (!CellRect(viewRow, displayCol, rc))
		GetClientRect(&rc);
	POINT pt{ rc.left + Scale(8), rc.bottom };
	ClientToScreen(&pt);
	::SendMessage(d.errorTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
	::SendMessage(d.errorTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
	SetTimer(DgData::ErrorTimer, 5000);
}

void CDataGridCtrl::HideError() {
	auto& d = *m_Data;
	if (!d.errorTip)
		return;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	::SendMessage(d.errorTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
	KillTimer(DgData::ErrorTimer);
}
