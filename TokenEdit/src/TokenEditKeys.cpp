#include "TokenEditData.h"

#include <algorithm>

// ---- Going among the tokens -------------------------------------------------------------------------
//
// With tokens selected the control has the focus, and d.focus is the token the keys are on; the typing area sits
// before tokens[editAt] (after the last when editAt is -1), and the arrows go into it and out of it as if it were one
// more item in the flow.

void CTokenEditCtrl::FocusToken(int index, bool extend) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.tokens.Size())
		return;
	CloseSuggestions();
	d.tokens.Select(index, extend ? TkeSelect::Extend : TkeSelect::Only);
	d.focus = index;
	if (::GetFocus() != m_hWnd)
		SetFocus();
	EnsureLayout();
	ScrollToShow(d.layout.tokens[index].rc);
	Invalidate(FALSE);
	NotifySelection();
}

void CTokenEditCtrl::FocusEditAt(bool start) {
	auto& d = *m_Data;
	if (ReadOnly() || !m_Edit.IsWindow())
		return;
	m_Edit.SetFocus();   // the tokens are deselected (OnEditSetFocus)
	if (start)
		m_Edit.SetSel(0, 0);
	else
		CaretToEnd();
	ScrollToShow(d.layout.edit);
}

bool CTokenEditCtrl::KeyDown(UINT vk) {
	auto& d = *m_Data;
	const int count = (int)d.tokens.Size();
	const bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0;
	const bool readOnly = ReadOnly();
	const int editPos = d.editAt < 0 ? count : d.editAt;
	int focus = d.focus;
	if (focus < 0 || focus >= count)
		focus = d.tokens.Anchor() >= 0 && d.tokens.Anchor() < count ? d.tokens.Anchor() : count - 1;

	// the clipboard first: Shift+Delete is cut, not delete
	if ((ctrl && vk == 'C') || (ctrl && vk == VK_INSERT)) {
		Copy();
		return true;
	}
	if ((ctrl && vk == 'X') || (shift && vk == VK_DELETE)) {
		Cut();
		return true;
	}
	if ((ctrl && vk == 'V') || (shift && vk == VK_INSERT)) {
		Paste();
		return true;
	}
	if (ctrl && (vk == 'Z' || vk == 'Y')) {
		if (vk == 'Y' || shift)
			Redo();
		else
			Undo();
		return true;
	}
	if (d.drag != TkeData::Drag::None) {
		if (vk == VK_ESCAPE)
			CancelDrag();
		return true;   // the other keys wait for the drop
	}

	switch (vk) {
		case VK_F2:
			if (focus >= 0)
				StartEdit(focus, true);
			return true;

		case VK_LEFT:
			if (focus < 0)
				return true;
			if (!shift && editPos == focus && !readOnly)
				FocusEditAt(false);   // the typing area is just before it
			else if (focus > 0)
				FocusToken(focus - 1, shift);
			return true;

		case VK_RIGHT:
			if (focus < 0)
				return true;
			if (!shift && editPos == focus + 1 && !readOnly)
				FocusEditAt(true);
			else if (focus + 1 < count)
				FocusToken(focus + 1, shift);
			return true;

		case VK_HOME:
			FocusToken(0, shift);
			return true;

		case VK_END:
			if (!shift && !readOnly)
				FocusEditAt(false);
			else
				FocusToken(count - 1, shift);
			return true;

		case VK_UP:
		case VK_DOWN: {
			EnsureLayout();
			int next = TkeLineNeighbor(d.layout, focus, vk == VK_UP ? -1 : 1);
			if (next == count && !shift)
				FocusEditAt(false);
			else if (next >= 0 && next < count)
				FocusToken(next, shift);
			return true;
		}

		case VK_SPACE:
			if (ctrl && focus >= 0) {
				d.tokens.Select(focus, TkeSelect::Toggle);
				Invalidate(FALSE);
				NotifySelection();
			}
			return true;

		case VK_DELETE:
		case VK_BACK:
			if (!readOnly && d.tokens.SelectedCount()) {
				RemoveByUser(d.tokens.Selection());
				if (IsWindow())
					FocusEditAt(false);
			}
			return true;

		case VK_ESCAPE:
			if (d.tokens.SelectedCount()) {
				d.tokens.SelectAll(false);
				Invalidate(FALSE);
				NotifySelection();
				FocusEdit();
				return true;
			}
			return false;

		case 'A':
			if (ctrl) {
				d.tokens.SelectAll();
				Invalidate(FALSE);
				NotifySelection();
				return true;
			}
			return false;
	}
	return false;
}

bool CTokenEditCtrl::EditKey(UINT vk) {
	auto& d = *m_Data;
	const int count = (int)d.tokens.Size();
	const int editPos = d.editAt < 0 ? count : d.editAt;
	const bool ctrl = ::GetKeyState(VK_CONTROL) < 0;
	int start = 0, end = 0;
	m_Edit.GetSel(start, end);
	const int length = m_Edit.GetWindowTextLength();

	switch (vk) {
		case VK_ESCAPE:
			// an edited token back as it was
			if (d.editing) {
				EndTokenEdit(true);
				return true;
			}
			return false;

		case 'Z':
		case 'Y':
			// with nothing typed, the tokens' undo (else the edit's own)
			if (ctrl && length == 0 && !d.editing) {
				if (vk == 'Y' || ::GetKeyState(VK_SHIFT) < 0)
					Redo();
				else
					Undo();
				return true;
			}
			return false;

		case VK_LEFT:
			// out of the text's start onto the token before
			if (start == 0 && end == 0 && editPos > 0) {
				FocusToken(editPos - 1, false);
				return true;
			}
			return false;

		case VK_RIGHT:
			if (start == length && end == length && editPos < count) {
				FocusToken(editPos, false);
				return true;
			}
			return false;

		case VK_UP:
		case VK_DOWN: {
			EnsureLayout();
			int next = TkeLineNeighbor(d.layout, count, vk == VK_UP ? -1 : 1);
			if (next >= 0 && next < count)
				FocusToken(next, false);
			return true;   // a single-line edit does nothing with them anyway
		}

		case VK_SPACE:
			if (ctrl) {
				ShowSuggestions(TRUE);
				return true;
			}
			return false;

		case 'A':
			// nothing typed: all the tokens
			if (ctrl && length == 0 && count > 0) {
				d.tokens.SelectAll();
				d.focus = count - 1;
				SetFocus();
				Invalidate(FALSE);
				NotifySelection();
				return true;
			}
			return false;
	}
	return false;
}

// ---- Clipboard ---------------------------------------------------------------------------------------

std::wstring CTokenEditCtrl::SelectedText() const {
	auto& d = *m_Data;
	std::vector<std::wstring> texts;
	for (auto& t : d.tokens.Tokens())
		if (t.selected)
			texts.push_back(t.text);
	return TkeJoin(texts, d.separators, std::wstring(1, d.separators[0]) + L" ");
}

bool CTokenEditCtrl::SetClipboardText(const std::wstring& text) {
	if (!::OpenClipboard(m_hWnd))
		return false;
	::EmptyClipboard();
	bool done = false;
	const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
	if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		if (auto p = ::GlobalLock(h)) {
			memcpy(p, text.c_str(), bytes);
			::GlobalUnlock(h);
			done = ::SetClipboardData(CF_UNICODETEXT, h) != nullptr;
		}
		if (!done)
			::GlobalFree(h);
	}
	::CloseClipboard();
	return done;
}

BOOL CTokenEditCtrl::Copy() {
	if (!m_Data->tokens.SelectedCount())
		return FALSE;
	return SetClipboardText(SelectedText());
}

BOOL CTokenEditCtrl::Cut() {
	auto& d = *m_Data;
	if (ReadOnly() || !d.tokens.SelectedCount() || !Copy())
		return FALSE;
	RemoveByUser(d.tokens.Selection());
	if (IsWindow() && ::GetFocus() == m_hWnd)
		FocusEditAt(false);
	return TRUE;
}

BOOL CTokenEditCtrl::Paste() {
	if (ReadOnly() || !m_Edit.IsWindow() || !::IsClipboardFormatAvailable(CF_UNICODETEXT))
		return FALSE;
	auto& d = *m_Data;
	// with a token selected, the text goes after it
	if (::GetFocus() == m_hWnd && d.focus >= 0 && d.focus < (int)d.tokens.Size())
		SetEditAt(d.focus + 1);
	FocusEditAt(false);
	m_Edit.SendMessage(WM_PASTE);   // split as typed (OnEditPaste)
	return TRUE;
}

LRESULT CTokenEditCtrl::OnCopy(UINT, WPARAM, LPARAM, BOOL&) {
	Copy();
	return 0;
}

LRESULT CTokenEditCtrl::OnCut(UINT, WPARAM, LPARAM, BOOL&) {
	Cut();
	return 0;
}

LRESULT CTokenEditCtrl::OnPaste(UINT, WPARAM, LPARAM, BOOL&) {
	Paste();
	return 0;
}
