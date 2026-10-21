// In-place editing: the editor windows (edit, combo box, date picker), committing and validating values.

#include "PropertyGrid.h"
#include "PropertyGridModel.h"
#include "PropertyGridCheckList.h"

#include <algorithm>
#include <commdlg.h>
#include <shobjidl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

static constexpr UINT_PTR EditorSubclassId = 1;

static std::wstring WindowText(HWND hWnd) {
	std::wstring text(::GetWindowTextLengthW(hWnd), L'\0');
	::GetWindowTextW(hWnd, text.data(), (int)text.size() + 1);
	return text;
}

// ---- State ------------------------------------------------------------------------

bool CPropertyGridCtrl::IsReadOnly(const PgNode* node) const {
	return m_ReadOnly || (node->flags & PRGF_READONLY);
}

bool CPropertyGridCtrl::CanEdit(const PgNode* node) const {
	// read-only values get an editor too, to select and copy the text; check boxes never do
	return node && !node->IsCategory() && PgEditType(*node) != PRGVT_BOOL;
}

bool CPropertyGridCtrl::IsEditing() const {
	return m_Editing != nullptr;
}

HPRGITEM CPropertyGridCtrl::GetEditItem() const {
	return m_Editing;
}

HWND CPropertyGridCtrl::GetEditorWindow() const {
	return m_hEditor;
}

void CPropertyGridCtrl::SetReadOnly(bool readOnly) {
	if (readOnly && m_Editing)
		EndEditCore(false, false);
	m_ReadOnly = readOnly;
	if (IsWindow())
		Invalidate();
}

bool CPropertyGridCtrl::IsReadOnly() const {
	return m_ReadOnly;
}

// ---- Geometry ---------------------------------------------------------------------

RECT CPropertyGridCtrl::GetValueRect(int row) const {
	auto grid = GetGridRect();
	int top = grid.top + (row - m_TopRow) * m_RowHeight;
	return { GetSplitX() + 1, top, grid.right, top + m_RowHeight - 1 };
}

int CPropertyGridCtrl::ValueTextLeft(const PgNode* node, const RECT& rcValue) const {
	// matches PaintRow: a check box or a color swatch comes before the text
	int left = rcValue.left + m_Pad;
	int boxSize = std::min(m_RowHeight - 2 * m_Pad, Scale(13));
	auto type = node->value.value.type;
	if (type == PRGVT_BOOL)
		left += boxSize + m_Pad;
	else if (type == PRGVT_COLOR)
		left += boxSize * 3 / 2 + m_Pad;
	return left;
}

RECT CPropertyGridCtrl::GetCheckBoxRect(int row) const {
	auto rc = GetValueRect(row);
	int boxSize = std::min(m_RowHeight - 2 * m_Pad, Scale(13));
	int top = rc.top + (rc.bottom - rc.top - boxSize) / 2;
	return { rc.left + m_Pad, top, rc.left + m_Pad + boxSize, top + boxSize };
}

// ---- Beginning and ending -------------------------------------------------------------

BOOL CPropertyGridCtrl::BeginEdit(HPRGITEM hItem) {
	auto node = hItem ? Node(hItem) : m_Selected;
	if (!node || !IsWindow())
		return FALSE;
	if (node == m_Editing)
		return TRUE;
	if (m_Editing && !EndEditCore(true, false))
		return FALSE;
	if ((node != m_Selected || m_SelFlag >= 0) && !SelectItem(node))
		return FALSE;
	if (!CanEdit(node))
		return FALSE;
	NMPRGITEM nm{};
	nm.hItem = node;
	nm.lParam = node->lParam;
	if (FireNotify(&nm.hdr, PRGN_BEGINEDIT) || RowOf(node) < 0)
		return FALSE;
	EnsureVisible(node);
	return CreateEditor(node);
}

BOOL CPropertyGridCtrl::EndEdit(bool commit) {
	return EndEditCore(commit, false);
}

bool CPropertyGridCtrl::EndEditCore(bool commit, bool focusLost) {
	if (!m_Editing)
		return true;
	if (m_Ending)
		return false;
	m_Ending = true;
	std::wstring error;
	bool ok = !commit || CommitEditor(error);
	m_Ending = false;
	if (!ok) {
		if (!focusLost) {
			ShowError(error);
			return false;
		}
		// the user went elsewhere: the bad input is dropped
		::MessageBeep(MB_ICONWARNING);
	}
	DestroyEditor();
	return ok;
}

bool CPropertyGridCtrl::CreateEditor(PgNode* node) {
	bool readOnly = IsReadOnly(node);
	auto type = PgEditType(*node);
	auto& value = node->value.value;
	m_Editing = node;
	m_EditInitialText.clear();
	m_EditInitialIndex = -1;

	if (node->kind == PRGK_ENUM && !readOnly) {
		bool editable = (node->flags & PRGF_EDITABLE) != 0;
		m_hEditor = ::CreateWindowExW(0, WC_COMBOBOXW, L"",
			WS_CHILD | WS_VSCROLL | CBS_AUTOHSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST),
			0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		if (m_hEditor) {
			::SendMessage(m_hEditor, WM_SETFONT, (WPARAM)m_hFont, 0);
			for (size_t i = 0; i < node->enumItems.size(); i++) {
				auto& e = node->enumItems[i];
				auto index = (int)::SendMessage(m_hEditor, CB_ADDSTRING, 0, (LPARAM)e.name.c_str());
				::SendMessage(m_hEditor, CB_SETITEMDATA, index, i);
				if (m_EditInitialIndex < 0 && value.type == PRGVT_INT && e.value == value.llVal)
					m_EditInitialIndex = index;
			}
			::SendMessage(m_hEditor, CB_SETCURSEL, m_EditInitialIndex, 0);
			::SendMessage(m_hEditor, CB_SETITEMHEIGHT, (WPARAM)-1, std::max(m_TextHeight, m_RowHeight - Scale(7)));
			if (editable) {
				COMBOBOXINFO cbi{ sizeof(cbi) };
				::GetComboBoxInfo(m_hEditor, &cbi);
				m_hEditorEdit = cbi.hwndItem;
				m_EditInitialText = PgEditText(*node);   // a number not among the items shows as such
				::SetWindowTextW(m_hEditor, m_EditInitialText.c_str());
			}
			m_Editor = Editor::Combo;
		}
	}
	else if (type == PRGVT_DATETIME && !readOnly) {
		bool dateOnly = (node->flags & PRGF_DATEONLY) != 0;
		m_hEditor = ::CreateWindowExW(0, DATETIMEPICK_CLASSW, L"", WS_CHILD | DTS_SHORTDATEFORMAT,
			0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		if (m_hEditor) {
			::SendMessage(m_hEditor, WM_SETFONT, (WPARAM)m_hFont, 0);
			if (!dateOnly) {
				// the locale's short date and time, as the grid shows them
				wchar_t date[80], time[80];
				if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, date, _countof(date))
					&& ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, time, _countof(time))) {
					auto format = std::wstring(date) + L" " + time;
					DateTime_SetFormat(m_hEditor, format.c_str());
				}
			}
			SYSTEMTIME local;
			SYSTEMTIME utc;
			if (value.type != PRGVT_DATETIME || !::FileTimeToSystemTime(&value.ftVal, &utc)
				|| !::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
				::GetLocalTime(&local);
			DateTime_SetSystemtime(m_hEditor, GDT_VALID, &local);
			// as the picker reads it back (no milliseconds): unchanged means no commit
			DateTime_GetSystemtime(m_hEditor, &m_EditInitialTime);
			m_Editor = Editor::Date;
		}
	}
	else {
		DWORD style = WS_CHILD | ES_AUTOHSCROLL;
		if (readOnly || type == PRGVT_FONT)
			style |= ES_READONLY;   // a font comes from its dialog
		if ((node->flags & PRGF_PASSWORD) && type == PRGVT_STRING)
			style |= ES_PASSWORD;
		m_hEditor = ::CreateWindowExW(0, WC_EDITW, L"", style, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		if (m_hEditor) {
			::SendMessage(m_hEditor, WM_SETFONT, (WPARAM)m_hFont, 0);
			::SendMessage(m_hEditor, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
			m_EditInitialText = PgEditText(*node);   // set once the edit has its size (below)
			if (node->kind == PRGK_INTEGER && !readOnly && (node->minVal != LLONG_MIN || node->maxVal != LLONG_MAX)) {
				// not a buddy: the arrows step the value in UDN_DELTAPOS, so any 64-bit range works
				m_hSpin = ::CreateWindowExW(0, UPDOWN_CLASSW, nullptr, WS_CHILD, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
				::SendMessage(m_hSpin, UDM_SETRANGE32, 0, 100);
				::SendMessage(m_hSpin, UDM_SETPOS32, 0, 50);
			}
			m_Editor = Editor::Edit;
		}
	}

	if (!m_hEditor) {
		m_Editing = nullptr;
		m_Editor = Editor::None;
		return false;
	}
	::SetWindowSubclass(m_hEditor, EditorSubclassProc, EditorSubclassId, (DWORD_PTR)this);
	if (m_hEditorEdit)
		::SetWindowSubclass(m_hEditorEdit, EditorSubclassProc, EditorSubclassId, (DWORD_PTR)this);
	PositionEditor();
	if (m_Editor == Editor::Edit) {
		// with no width yet, selecting all would scroll the text to its end, and it would stay so
		::SetWindowTextW(m_hEditor, m_EditInitialText.c_str());
		::SendMessage(m_hEditor, EM_SETSEL, 0, -1);
	}
	::SetFocus(m_hEditor);
	Invalidate();   // the value's text moves to the editor
	return true;
}

void CPropertyGridCtrl::DestroyEditor() {
	CloseCheckList(false);
	HideError();
	HWND editor = m_hEditor;
	HWND spin = m_hSpin;
	m_hEditor = m_hEditorEdit = m_hSpin = nullptr;
	m_Editing = nullptr;
	m_Editor = Editor::None;
	if (!editor)
		return;
	auto focus = ::GetFocus();
	if (focus == editor || ::IsChild(editor, focus))
		SetFocus();   // before the editor goes, or the focus goes nowhere
	::DestroyWindow(editor);
	if (spin)
		::DestroyWindow(spin);
	Invalidate();
}

void CPropertyGridCtrl::PositionEditor() {
	if (!m_hEditor)
		return;
	CloseCheckList(false);   // it would be left behind
	int row = RowOf(m_Editing);
	auto grid = GetGridRect();
	auto rc = GetValueRect(row);
	if (row < m_TopRow || rc.bottom > grid.bottom) {
		// scrolled out of view: kept (with the focus), just not shown
		::ShowWindow(m_hEditor, SW_HIDE);
		if (m_hSpin)
			::ShowWindow(m_hSpin, SW_HIDE);
		return;
	}
	const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW;
	int cellRight = GetButton(m_Editing) != Button::None ? GetButtonRect(row).left - 1 : rc.right;
	switch (m_Editor) {
		case Editor::Edit: {
			int left = ValueTextLeft(m_Editing, rc);
			int right = cellRight - m_Pad;
			if (m_hSpin) {
				int width = ::GetSystemMetricsForDpi(SM_CXVSCROLL, m_Dpi);
				::SetWindowPos(m_hSpin, nullptr, cellRight - width, rc.top, width, rc.bottom - rc.top, flags);
				right = cellRight - width - 1;
			}
			int top = rc.top + (rc.bottom - rc.top - m_TextHeight) / 2;
			::SetWindowPos(m_hEditor, nullptr, left, top, std::max(0, right - left), m_TextHeight, flags);
			break;
		}
		case Editor::Combo: {
			// the height given is the list's; the selection field's comes from the item height
			::SetWindowPos(m_hEditor, nullptr, rc.left, rc.top, cellRight - rc.left, m_RowHeight * 12, flags);
			RECT field;
			::GetWindowRect(m_hEditor, &field);
			int top = rc.top + (rc.bottom - rc.top - (field.bottom - field.top)) / 2;
			::SetWindowPos(m_hEditor, nullptr, rc.left, top, 0, 0, SWP_NOSIZE | flags);
			break;
		}
		case Editor::Date:
			::SetWindowPos(m_hEditor, nullptr, rc.left, rc.top, cellRight - rc.left, rc.bottom - rc.top, flags);
			break;
	}
}

// ---- Committing ---------------------------------------------------------------------

bool CPropertyGridCtrl::CommitEditor(std::wstring& error) {
	auto node = m_Editing;
	if (IsReadOnly(node))
		return true;
	PgStoredValue value;
	switch (m_Editor) {
		case Editor::Edit: {
			auto text = WindowText(m_hEditor);
			if (text == m_EditInitialText)
				return true;   // untouched: the displayed (maybe rounded) text isn't written back
			if (!PgParseValue(*node, text, value, error))
				return false;
			break;
		}
		case Editor::Combo:
			if (m_hEditorEdit) {
				auto text = WindowText(m_hEditor);
				if (text == m_EditInitialText)
					return true;
				if (!PgParseValue(*node, text, value, error))
					return false;
			}
			else {
				auto index = (int)::SendMessage(m_hEditor, CB_GETCURSEL, 0, 0);
				if (index < 0 || index == m_EditInitialIndex)
					return true;
				auto item = (size_t)::SendMessage(m_hEditor, CB_GETITEMDATA, index, 0);
				if (item >= node->enumItems.size())
					return true;
				value.Assign(PrgMakeInt(node->enumItems[item].value));
			}
			break;
		case Editor::Date: {
			SYSTEMTIME local, utc;
			FILETIME ft;
			if (DateTime_GetSystemtime(m_hEditor, &local) != GDT_VALID || memcmp(&local, &m_EditInitialTime, sizeof(local)) == 0)
				return true;
			if (!::TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !::SystemTimeToFileTime(&utc, &ft)) {
				error = L"The date isn't valid.";
				return false;
			}
			value.Assign(PrgMakeDateTime(ft));
			break;
		}
		default:
			return true;
	}
	return CommitValue(node, value.value, error);
}

bool CPropertyGridCtrl::CommitValue(PgNode* node, const PRGVALUE& value, std::wstring& error) {
	if (PgValuesEqual(value, node->value.value))
		return true;
	NMPRGVALUE nm{};
	nm.hItem = node;
	nm.lParam = node->lParam;
	nm.oldValue = node->value.value;
	nm.newValue = value;
	if (FireNotify(&nm.hdr, PRGN_VALUECHANGING)) {
		nm.szError[_countof(nm.szError) - 1] = 0;
		error = nm.szError[0] ? nm.szError : L"The value was rejected.";
		return false;
	}
	if (!PgValueFits(node->kind, nm.newValue)) {
		error = L"The value doesn't fit the property.";
		return false;
	}
	PgStoredValue old;
	old.Assign(node->value.value);
	node->value.Assign(nm.newValue);
	Invalidate();
	nm.oldValue = old.value;
	nm.newValue = node->value.value;
	nm.szError[0] = 0;
	FireNotify(&nm.hdr, PRGN_VALUECHANGED);
	return true;
}

void CPropertyGridCtrl::ToggleBool(PgNode* node) {
	if (IsReadOnly(node)) {
		::MessageBeep(MB_OK);
		return;
	}
	auto& v = node->value.value;
	bool current = v.type == PRGVT_BOOL && v.boolVal;
	std::wstring error;
	if (!CommitValue(node, PrgMakeBool(!current), error))
		ShowError(error);
}

void CPropertyGridCtrl::CycleEnum(PgNode* node) {
	if (node->enumItems.empty())
		return;
	if (IsReadOnly(node)) {
		::MessageBeep(MB_OK);
		return;
	}
	auto& v = node->value.value;
	auto it = std::find_if(node->enumItems.begin(), node->enumItems.end(), [&](auto& e) {
		return v.type == PRGVT_INT && e.value == v.llVal;
		});
	size_t next = it == node->enumItems.end() ? 0 : (it - node->enumItems.begin() + 1) % node->enumItems.size();
	std::wstring error;
	if (!CommitValue(node, PrgMakeInt(node->enumItems[next].value), error))
		ShowError(error);
}

void CPropertyGridCtrl::StepInteger(int delta) {
	if (m_Editor != Editor::Edit || !m_Editing)
		return;
	auto node = m_Editing;
	PgStoredValue parsed;
	std::wstring error;
	LONGLONG v = PgParseValue(*node, WindowText(m_hEditor), parsed, error) ? parsed.value.llVal
		: node->value.value.type == PRGVT_INT ? node->value.value.llVal : 0;
	if (delta > 0)
		v = v >= node->maxVal ? node->maxVal : v + 1;
	else
		v = v <= node->minVal ? node->minVal : v - 1;
	v = std::clamp(v, node->minVal, node->maxVal);
	auto text = PgFormatValue(*node, PrgMakeInt(v));
	::SetWindowTextW(m_hEditor, text.c_str());
	::SendMessage(m_hEditor, EM_SETSEL, 0, -1);
}

void CPropertyGridCtrl::MoveToProperty(int direction, bool edit) {
	int row = SelectedRow();
	for (int r = row + direction; r >= 0 && r < (int)m_Rows.size(); r += direction) {
		auto node = m_Rows[r].node;
		if (node->IsCategory() || (edit && (m_Rows[r].flag >= 0 || !CanEdit(node))))
			continue;
		SelectRow(r);
		if (edit)
			BeginEdit(node);
		return;
	}
}

// ---- Error tip ------------------------------------------------------------------------

void CPropertyGridCtrl::ShowError(const std::wstring& text) {
	::MessageBeep(MB_ICONWARNING);
	RECT rc;
	if (m_hEditor && ::IsWindowVisible(m_hEditor))
		::GetWindowRect(m_hEditor, &rc);
	else {
		int row = SelectedRow();
		if (row < m_TopRow)
			return;
		rc = GetValueRect(row);
		::MapWindowPoints(m_hWnd, nullptr, (POINT*)&rc, 2);
	}
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	if (!m_hErrorTip) {
		m_hErrorTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_BALLOON | TTS_NOPREFIX | TTS_ALWAYSTIP,
			0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		ti.lpszText = const_cast<LPWSTR>(L"");
		::SendMessage(m_hErrorTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
	}
	::SendMessage(m_hErrorTip, TTM_SETMAXTIPWIDTH, 0, Scale(320));
	ti.lpszText = const_cast<LPWSTR>(text.c_str());
	::SendMessage(m_hErrorTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	::SendMessage(m_hErrorTip, TTM_SETTITLEW, TTI_ERROR, (LPARAM)L"Invalid value");
	::SendMessage(m_hErrorTip, TTM_TRACKPOSITION, 0, MAKELPARAM(rc.left + Scale(12), rc.bottom));
	::SendMessage(m_hErrorTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}

void CPropertyGridCtrl::HideError() {
	if (!m_hErrorTip)
		return;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	::SendMessage(m_hErrorTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

// ---- Editor messages ----------------------------------------------------------------------

LRESULT CALLBACK CPropertyGridCtrl::EditorSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	auto self = (CPropertyGridCtrl*)data;
	switch (msg) {
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, EditorSubclassProc, id);
			break;
		case WM_GETDLGCODE:
			// Enter, Escape and Tab are the grid's, also in a dialog
			return ::DefSubclassProc(hWnd, msg, wParam, lParam) | DLGC_WANTALLKEYS;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			self->HideError();
			if (self->HandleEditorKey(hWnd, msg, wParam))
				return 0;   // the editor may be gone by now
			break;
		case WM_CHAR:
			if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB)
				return 0;   // handled on WM_KEYDOWN; a single-line edit would beep
			if (wParam == L' ' && self->IsCheckListOpen())
				return 0;   // toggled a flag on WM_KEYDOWN
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

bool CPropertyGridCtrl::HandleEditorKey(HWND, UINT msg, WPARAM key) {
	if (IsCheckListOpen()) {
		// the check list has no focus of its own: the edit's keys drive it
		switch (key) {
			case VK_UP:
			case VK_DOWN:
				if (msg == WM_SYSKEYDOWN)
					CloseCheckList(false);   // Alt+Up, Alt+Down
				else
					m_CheckList->MoveCurrent(key == VK_UP ? -1 : 1);
				return true;
			case VK_SPACE:
				m_CheckList->ToggleCurrent();
				return true;
			case VK_F4:
				CloseCheckList(false);
				return true;
			case VK_ESCAPE:
				CloseCheckList(true);   // the text as it was before the list opened
				return true;
			case VK_SHIFT:
			case VK_CONTROL:
			case VK_MENU:
				return false;
			default:
				CloseCheckList(false);   // Enter and Tab then commit as usual; typing would make it stale
				break;
		}
	}
	bool hasButton = m_Editor == Editor::Edit && GetButton(m_Editing) != Button::None;
	if (msg == WM_SYSKEYDOWN) {
		if (key == VK_DOWN && hasButton) {
			PressButton(m_Editing);
			return true;
		}
		return false;
	}
	if (key == VK_F4 && hasButton) {
		PressButton(m_Editing);
		return true;
	}
	bool dropped = (m_Editor == Editor::Combo && ::SendMessage(m_hEditor, CB_GETDROPPEDSTATE, 0, 0))
		|| (m_Editor == Editor::Date && DateTime_GetMonthCal(m_hEditor));
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	switch (key) {
		case VK_RETURN:
			if (dropped)
				return false;   // picks the item and closes the list
			EndEditCore(true, false);
			return true;
		case VK_ESCAPE:
			if (dropped)
				return false;
			EndEditCore(false, false);
			return true;
		case VK_TAB:
			if (EndEditCore(true, false))
				MoveToProperty(shift ? -1 : 1, true);
			return true;
		case VK_UP:
		case VK_DOWN:
			if (m_Editor != Editor::Edit)
				return false;   // the combo box and the date picker use them
			if (m_hSpin)
				StepInteger(key == VK_UP ? 1 : -1);
			else if (EndEditCore(true, false))
				MoveToProperty(key == VK_UP ? -1 : 1, false);
			return true;
	}
	return false;
}

LRESULT CPropertyGridCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (!m_hEditor || (HWND)lParam != m_hEditor) {
		bHandled = FALSE;
		return 0;
	}
	switch (HIWORD(wParam)) {
		case EN_KILLFOCUS:
		case CBN_KILLFOCUS:
			// checked once the focus has settled: it may have moved within the editor
			PostMessage(WM_PRG_CHECKFOCUS);
			break;
		case EN_CHANGE:
		case CBN_EDITCHANGE:
			HideError();
			break;
		case CBN_CLOSEUP:
			// the selection notifications come in no fixed order around this one
			PostMessage(WM_PRG_COMBOCLOSED);
			break;
	}
	return 0;
}

LRESULT CPropertyGridCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (m_hSpin && hdr->hwndFrom == m_hSpin && hdr->code == UDN_DELTAPOS) {
		StepInteger(((NMUPDOWN*)hdr)->iDelta > 0 ? 1 : -1);
		::SetFocus(m_hEditor);
		return 1;   // the spin's own position stays put
	}
	if (m_hEditor && hdr->hwndFrom == m_hEditor) {
		if (hdr->code == NM_KILLFOCUS)
			PostMessage(WM_PRG_CHECKFOCUS);
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CPropertyGridCtrl::OnCheckFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (!m_hEditor || m_Ending)
		return 0;
	auto focus = ::GetFocus();
	if (focus == m_hEditor || ::IsChild(m_hEditor, focus) || (m_hSpin && focus == m_hSpin))
		return 0;
	if (m_Editor == Editor::Combo && ::SendMessage(m_hEditor, CB_GETDROPPEDSTATE, 0, 0))
		return 0;
	if (m_Editor == Editor::Date && DateTime_GetMonthCal(m_hEditor))
		return 0;
	EndEditCore(true, true);
	return 0;
}

LRESULT CPropertyGridCtrl::OnComboClosed(UINT, WPARAM, LPARAM, BOOL&) {
	// a pick from the list commits at once; closing it unchanged keeps editing
	if (m_Editor == Editor::Combo && (int)::SendMessage(m_hEditor, CB_GETCURSEL, 0, 0) != m_EditInitialIndex)
		EndEditCore(true, false);
	return 0;
}

LRESULT CPropertyGridCtrl::OnCtlColor(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto c = GetColors();
	auto dc = (HDC)wParam;
	::SetTextColor(dc, m_Editing && IsReadOnly(m_Editing) ? c.crDisabledText : c.crText);
	::SetBkColor(dc, c.crBack);
	if (m_EditorBrushColor != c.crBack) {
		if (!m_EditorBrush.IsNull())
			m_EditorBrush.DeleteObject();
		m_EditorBrush.CreateSolidBrush(c.crBack);
		m_EditorBrushColor = c.crBack;
	}
	return (LRESULT)m_EditorBrush.m_hBrush;
}

// ---- Buttons and dialogs -------------------------------------------------------------------

CPropertyGridCtrl::Button CPropertyGridCtrl::GetButton(const PgNode* node) const {
	if (!node || node->IsCategory() || IsReadOnly(node))
		return Button::None;
	if (node->flags & PRGF_BUTTON)
		return Button::Ellipsis;
	switch (node->kind) {
		case PRGK_COLOR:
		case PRGK_FONT:
		case PRGK_PATH:
			return Button::Ellipsis;
		case PRGK_FLAGS:
			return PgHasSubRows(node) ? Button::DropDown : Button::None;
	}
	return Button::None;
}

RECT CPropertyGridCtrl::GetButtonRect(int row) const {
	auto rc = GetValueRect(row);
	int width = std::min(m_RowHeight, Scale(22));
	return { rc.right - width, rc.top, rc.right, rc.bottom };
}

void CPropertyGridCtrl::PressButton(PgNode* node) {
	auto button = GetButton(node);
	if (button == Button::DropDown) {
		if (IsCheckListOpen()) {
			CloseCheckList(false);
			return;
		}
		if (m_Editing != node && !BeginEdit(node))
			return;
		if (m_Editor == Editor::Edit) {
			::SetFocus(m_hEditor);
			OpenCheckList();
		}
		return;
	}
	if (button != Button::Ellipsis)
		return;
	if (m_Editing && !EndEditCore(true, false))
		return;   // the typed text first; it's bad
	NMPRGITEM nm{};
	nm.hItem = node;
	nm.lParam = node->lParam;
	int row = RowOf(node);
	if (row >= 0) {
		auto rc = GetButtonRect(row);
		nm.pt = { rc.left, rc.bottom };
		ClientToScreen(&nm.pt);
	}
	if (FireNotify(&nm.hdr, PRGN_BUTTONCLICK))
		return;   // the parent's own dialog
	PgStoredValue value;
	if (!RunDialog(node, value))
		return;
	std::wstring error;
	if (!CommitValue(node, value.value, error))
		ShowError(error);
}

static bool PickPath(HWND owner, const PgNode& node, std::wstring& path) {
	// the dialogs are COM objects, and the thread may not have COM yet
	HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool ok = false;
	{
		bool folder = (node.flags & PRGF_FOLDER) != 0;
		bool save = (node.flags & PRGF_SAVEFILE) && !folder;
		ATL::CComPtr<IFileDialog> dlg;
		if (SUCCEEDED(dlg.CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog))) {
			FILEOPENDIALOGOPTIONS options = 0;
			dlg->GetOptions(&options);
			dlg->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
			dlg->SetTitle(node.name.c_str());

			// the filter: name and pattern pairs, '\0' separated
			std::vector<std::wstring> parts;
			for (size_t start = 0; start < node.filter.size();) {
				auto end = node.filter.find(L'\0', start);
				if (end == node.filter.npos)
					end = node.filter.size();
				parts.push_back(node.filter.substr(start, end - start));
				start = end + 1;
			}
			std::vector<COMDLG_FILTERSPEC> specs;
			specs.reserve(parts.size() / 2);
			for (size_t i = 0; i + 1 < parts.size(); i += 2)
				specs.push_back({ parts[i].c_str(), parts[i + 1].c_str() });
			if (!folder && !specs.empty())
				dlg->SetFileTypes((UINT)specs.size(), specs.data());

			// start at the current path
			auto& current = node.value.value;
			std::wstring currentPath = current.type == PRGVT_STRING && current.pszVal ? current.pszVal : L"";
			auto slash = currentPath.find_last_of(L"\\/");
			std::wstring dir = folder ? currentPath : slash == currentPath.npos ? L"" : currentPath.substr(0, slash);
			if (!folder)
				dlg->SetFileName(slash == currentPath.npos ? currentPath.c_str() : currentPath.c_str() + slash + 1);
			ATL::CComPtr<IShellItem> start;
			if (!dir.empty() && SUCCEEDED(::SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&start))))
				dlg->SetFolder(start);

			ATL::CComPtr<IShellItem> result;
			PWSTR name = nullptr;
			if (SUCCEEDED(dlg->Show(owner)) && SUCCEEDED(dlg->GetResult(&result))
				&& SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
				path = name;
				::CoTaskMemFree(name);
				ok = true;
			}
		}
	}
	if (SUCCEEDED(init))
		::CoUninitialize();
	return ok;
}

bool CPropertyGridCtrl::RunDialog(PgNode* node, PgStoredValue& value) {
	auto owner = ::GetAncestor(m_hWnd, GA_ROOT);
	auto& current = node->value.value;
	switch (node->kind) {
		case PRGK_COLOR: {
			CHOOSECOLORW cc{ sizeof(cc) };
			cc.hwndOwner = owner;
			cc.rgbResult = current.type == PRGVT_COLOR ? current.clrVal : RGB(0, 0, 0);
			cc.lpCustColors = m_CustomColors;
			cc.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR;
			if (!::ChooseColorW(&cc))
				return false;
			value.Assign(PrgMakeColor(cc.rgbResult));
			return true;
		}
		case PRGK_FONT: {
			LOGFONTW lf{};
			if (current.type == PRGVT_FONT)
				lf = current.lfVal;
			else {
				::GetObjectW(m_hFont, sizeof(lf), &lf);
				lf.lfHeight = ::MulDiv(lf.lfHeight, 96, m_Dpi);
			}
			// the dialog works in the screen DC's units, the value in 96-DPI ones
			HDC screen = ::GetDC(nullptr);
			int dpi = ::GetDeviceCaps(screen, LOGPIXELSY);
			::ReleaseDC(nullptr, screen);
			lf.lfHeight = ::MulDiv(lf.lfHeight, dpi, 96);
			CHOOSEFONTW cf{ sizeof(cf) };
			cf.hwndOwner = owner;
			cf.lpLogFont = &lf;
			cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOVERTFONTS;
			if (!::ChooseFontW(&cf))
				return false;
			lf.lfHeight = ::MulDiv(lf.lfHeight, 96, dpi);
			value.Assign(PrgMakeFont(lf));
			return true;
		}
		case PRGK_PATH: {
			std::wstring path;
			if (!PickPath(owner, *node, path))
				return false;
			value.Assign(PrgMakeString(path.c_str()));
			return true;
		}
	}
	return false;   // PRGF_BUTTON on another kind: the parent's to handle
}

// ---- Flags -------------------------------------------------------------------------------

void CPropertyGridCtrl::ToggleFlag(PgNode* node, int flag) {
	if (IsReadOnly(node)) {
		::MessageBeep(MB_OK);
		return;
	}
	auto& v = node->value.value;
	LONGLONG mask = v.type == PRGVT_INT ? v.llVal : 0;
	auto bits = node->enumItems[flag].value;
	std::wstring error;
	if (!CommitValue(node, PrgMakeInt(PgSetFlag(mask, bits, !PgIsFlagSet(mask, bits))), error))
		ShowError(error);
}

bool CPropertyGridCtrl::IsCheckListOpen() const {
	return m_CheckList && m_CheckList->IsOpen();
}

void CPropertyGridCtrl::OpenCheckList() {
	if (m_Editor != Editor::Edit || !m_Editing)
		return;
	// from the text as typed so far, if it parses
	auto text = WindowText(m_hEditor);
	PgStoredValue parsed;
	std::wstring error;
	auto& v = m_Editing->value.value;
	LONGLONG mask = PgParseValue(*m_Editing, text, parsed, error) ? parsed.value.llVal : v.type == PRGVT_INT ? v.llVal : 0;
	m_CheckListInitialText = text;
	if (!m_CheckList)
		m_CheckList = std::make_unique<PgCheckList>(*this);
	auto rc = GetValueRect(RowOf(m_Editing));
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&rc, 2);
	m_CheckList->Open(m_Editing, mask, rc);
}

void CPropertyGridCtrl::CloseCheckList(bool restoreText) {
	if (!IsCheckListOpen())
		return;
	m_CheckList->Close();
	if (restoreText && m_Editor == Editor::Edit) {
		::SetWindowTextW(m_hEditor, m_CheckListInitialText.c_str());
		::SendMessage(m_hEditor, EM_SETSEL, 0, -1);
	}
}

void CPropertyGridCtrl::OnCheckListChanged(LONGLONG mask) {
	// the edit shows the flags as they're checked; Enter (or leaving) commits them as typed text would be
	if (m_Editor != Editor::Edit || !m_Editing)
		return;
	auto text = PgFormatValue(*m_Editing, PrgMakeInt(mask));
	::SetWindowTextW(m_hEditor, text.c_str());
	::SendMessage(m_hEditor, EM_SETSEL, 0, -1);
}
