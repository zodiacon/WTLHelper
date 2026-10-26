#include "TokenEditData.h"

#include <commctrl.h>

#include <algorithm>

using namespace TkeDraw;

namespace {
	int CopyText(const std::wstring& s, LPWSTR text, int cch) {
		if (text && cch > 0) {
			size_t n = std::min(s.size(), (size_t)cch - 1);
			s.copy(text, n);
			text[n] = 0;
		}
		return (int)s.size() + 1;
	}

	std::wstring TrimLeft(std::wstring s) {
		size_t i = 0;
		while (i < s.size() && (s[i] == L' ' || s[i] == L'\t'))
			i++;
		return s.substr(i);
	}

	std::wstring ClipboardText(HWND owner) {
		std::wstring text;
		if (!::IsClipboardFormatAvailable(CF_UNICODETEXT) || !::OpenClipboard(owner))
			return text;
		if (HANDLE h = ::GetClipboardData(CF_UNICODETEXT)) {
			if (auto p = (const wchar_t*)::GlobalLock(h)) {
				text = p;
				::GlobalUnlock(h);
			}
		}
		::CloseClipboard();
		return text;
	}
}

// ---- The typing area ---------------------------------------------------------------------------

std::wstring CTokenEditCtrl::EditText() const {
	if (!m_Edit.IsWindow())
		return {};
	int len = m_Edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	m_Edit.GetWindowText(text.data(), len + 1);
	text.resize(len);
	return text;
}

void CTokenEditCtrl::SetTyped(const std::wstring& text, bool selectAll) {
	auto& d = *m_Data;
	d.settingText = true;
	m_Edit.SetWindowText(text.c_str());
	d.settingText = false;
	if (selectAll)
		m_Edit.SetSel(0, -1);
	else
		CaretToEnd();
	Changed(false);              // its width
}

void CTokenEditCtrl::CaretToEnd() {
	// (EM_SETSEL with -1 only takes the selection away: the caret stays)
	const int length = m_Edit.GetWindowTextLength();
	m_Edit.SetSel(length, length);
}

int CTokenEditCtrl::GetEditText(LPWSTR text, int cch) const {
	return CopyText(EditText(), text, cch);
}

void CTokenEditCtrl::SetEditText(LPCWSTR text) {
	if (m_Edit.IsWindow())
		SetTyped(text ? text : L"", false);
}

HWND CTokenEditCtrl::GetEditControl() const {
	return m_Edit.m_hWnd;
}

BOOL CTokenEditCtrl::CommitText() {
	if (!m_Edit.IsWindow() || ReadOnly() || EditText().empty())
		return FALSE;
	const size_t count = m_Data->tokens.Size();
	Tokenize(TKEA_COMMIT, true);
	return m_Data->tokens.Size() > count;
}

int CTokenEditCtrl::EditWidth() const {
	auto text = EditText();
	int width = 0;
	if (!text.empty() && m_Edit.IsWindow()) {
		WTL::CClientDC dc(m_Edit);
		auto old = dc.SelectFont(m_hFont);
		SIZE size{};
		dc.GetTextExtent(text.c_str(), (int)text.size(), &size);
		dc.SelectFont(old);
		width = size.cx;
	}
	return width + Scale(6);   // the caret, and a little room before the text scrolls
}

void CTokenEditCtrl::PlaceEdit() {
	auto& d = *m_Data;
	if (!m_Edit.IsWindow())
		return;
	RECT r = d.layout.edit;
	::OffsetRect(&r, 0, -d.scroll);
	const int height = m_TextHeight;
	const int top = (int)(r.top + r.bottom - height) / 2;
	m_Edit.SetWindowPos(nullptr, r.left + Scale(2), top, std::max(0, (int)(r.right - r.left) - Scale(2)), height,
		SWP_NOZORDER | SWP_NOACTIVATE);
}

void CTokenEditCtrl::FocusEdit() {
	if (ReadOnly() || !m_Edit.IsWindow())
		SetFocus();
	else
		m_Edit.SetFocus();
}

void CTokenEditCtrl::SetEditAt(int index) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.tokens.Size())
		index = -1;
	if (index == d.editAt)
		return;
	d.editAt = index;
	Changed(false);
}

// ---- Tokenizing ---------------------------------------------------------------------------------

std::vector<std::wstring> CTokenEditCtrl::AddTyped(std::vector<TkeToken> tokens, UINT source) {
	auto& d = *m_Data;
	std::vector<std::wstring> refused;
	auto before = d.tokens.Save();
	bool added = false;
	for (auto& preset : tokens) {
		const std::wstring& piece = preset.text;
		if (d.maxTokens && (int)d.tokens.Size() >= d.maxTokens) {
			refused.push_back(piece);
			continue;
		}
		if (Duplicate(piece))
			continue;   // there already: it just goes
		NMTKETOKEN nm{};
		nm.index = d.editAt < 0 ? (int)d.tokens.Size() : d.editAt;
		nm.source = source;
		nm.pszText = piece.c_str();
		nm.pszTip = preset.tip.empty() ? nullptr : preset.tip.c_str();
		nm.iImage = preset.image;
		nm.crBack = preset.back;
		nm.crText = preset.fore;
		nm.flags = (preset.invalid ? TKEF_INVALID : 0) | (preset.locked ? TKEF_LOCKED : 0);
		nm.lParam = preset.lParam;
		if (FireNotify(&nm.hdr, TKEN_TOKENADDING) || !nm.pszText || !*nm.pszText) {
			if (!IsWindow())
				return {};
			refused.push_back(piece);
			continue;
		}
		if (!IsWindow())
			return {};
		TkeToken t;
		t.text = nm.pszText;   // the app's, perhaps
		if (Duplicate(t.text))
			continue;
		t.tip = nm.pszTip ? nm.pszTip : L"";
		t.image = nm.iImage;
		t.back = nm.crBack;
		t.fore = nm.crText;
		t.invalid = (nm.flags & TKEF_INVALID) != 0;
		t.locked = (nm.flags & TKEF_LOCKED) != 0;
		t.lParam = nm.lParam;
		const int at = d.editAt < 0 ? (int)d.tokens.Size() : std::min(d.editAt, (int)d.tokens.Size());
		d.tokens.Insert(at, std::move(t));
		if (d.editAt >= 0)
			d.editAt = at + 1;
		added = true;

		NMTKETOKEN done{};
		auto& token = d.tokens[at];
		done.id = token.id;
		done.index = at;
		done.source = source;
		done.pszText = token.text.c_str();
		done.pszTip = token.tip.c_str();
		done.iImage = token.image;
		done.crBack = token.back;
		done.crText = token.fore;
		done.flags = (token.invalid ? TKEF_INVALID : 0) | (token.locked ? TKEF_LOCKED : 0);
		done.lParam = token.lParam;
		FireNotify(&done.hdr, TKEN_TOKENADDED);
		if (!IsWindow())
			return {};
	}
	if (added) {
		d.history.Push(std::move(before), d.tokens.Save());
		Changed(true);
		NotifyChanged();
	}
	return refused;
}

void CTokenEditCtrl::Tokenize(UINT source, bool final) {
	auto& d = *m_Data;
	if (d.tokenizing || ReadOnly())
		return;
	auto text = EditText();
	std::wstring rest;
	auto pieces = final ? TkeSplit(text, d.separators) : TkeSplit(text, d.separators, &rest);
	if (pieces.empty()) {
		if (final && !text.empty() && text.find_first_not_of(L" \t") == std::wstring::npos)
			SetTyped({}, false);   // only spaces
		else if (!final && rest != text)
			SetTyped(rest, false); // only separators typed
		return;
	}
	std::vector<TkeToken> tokens(pieces.size());
	for (size_t i = 0; i < pieces.size(); i++)
		tokens[i].text = std::move(pieces[i]);
	CloseSuggestions();
	d.tokenizing = true;
	auto refused = AddTyped(std::move(tokens), source);
	d.tokenizing = false;
	if (!IsWindow())
		return;
	if (refused.empty()) {
		SetTyped(TrimLeft(rest), false);
	}
	else {
		// back to be fixed, the rest still being typed after them
		const std::wstring joiner = std::wstring(1, d.separators[0]) + L" ";
		auto back = TkeJoin(refused, d.separators, joiner);
		const int length = (int)back.size();
		rest = TrimLeft(rest);
		if (!rest.empty())
			back += joiner + rest;
		SetTyped(back, false);
		m_Edit.SetSel(0, length);
	}
	ScrollToShow(d.layout.edit);
}

// ---- The edit's messages ---------------------------------------------------------------------------

LRESULT CTokenEditCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if ((HWND)lParam != m_Edit.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	if (HIWORD(wParam) == EN_CHANGE && !d.settingText) {
		if (TkeHasSeparator(EditText(), d.separators))
			Tokenize(TKEA_TYPED, false);
		else
			Changed(false);   // its width
		if (IsWindow())
			ScrollToShow(d.layout.edit);
		if (IsWindow() && !d.tokenizing)
			UpdateSuggestions(false);
	}
	return 0;
}

LRESULT CTokenEditCtrl::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)lParam != m_Edit.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	auto c = PaintColors();
	if (m_EditBrushColor != c.crBack) {
		if (!m_EditBrush.IsNull())
			m_EditBrush.DeleteObject();
		m_EditBrush.CreateSolidBrush(c.crBack);
		m_EditBrushColor = c.crBack;
	}
	WTL::CDCHandle dc((HDC)wParam);
	dc.SetTextColor(c.crText);
	dc.SetBkColor(c.crBack);
	return (LRESULT)m_EditBrush.m_hBrush;
}

LRESULT CTokenEditCtrl::OnEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (SuggestKey((UINT)wParam) || EditKey((UINT)wParam))
		return 0;
	switch (wParam) {
		case VK_RETURN:
			if (!EditText().empty()) {
				Tokenize(TKEA_TYPED, true);
				return 0;
			}
			break;

		case VK_BACK: {
			// at the start, the token before is selected (a second Backspace removes it)
			int start = 0, end = 0;
			m_Edit.GetSel(start, end);
			const int before = (d.editAt < 0 ? (int)d.tokens.Size() : d.editAt) - 1;
			if (start == 0 && end == 0 && before >= 0) {
				FocusToken(before, false);
				return 0;
			}
			break;
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == L'\r' || wParam == L'\n' || wParam == L'\t' || wParam == 27)
		return 0;   // Enter made a token, Tab picked a suggestion, Escape closed them (or they aren't the edit's): no beep
	if (wParam == 1 && EditText().empty())
		return 0;   // Ctrl+A selected the tokens
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnEditGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	LRESULT code = m_Edit.DefWindowProc(uMsg, wParam, lParam);
	// Enter with something typed makes a token rather than pressing the dialog's default button
	// and while suggestions are shown, Tab, Escape and Enter are theirs
	auto msg = (const MSG*)lParam;
	if (msg && msg->message == WM_KEYDOWN) {
		if (msg->wParam == VK_RETURN && !EditText().empty())
			code |= DLGC_WANTALLKEYS;
		if ((msg->wParam == VK_TAB || msg->wParam == VK_ESCAPE || msg->wParam == VK_RETURN) && IsSuggesting())
			code |= DLGC_WANTALLKEYS;
	}
	return code;
}

LRESULT CTokenEditCtrl::OnEditSetFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.tokens.SelectedCount()) {
		d.tokens.SelectAll(false);
		NotifySelection();
	}
	Invalidate(FALSE);   // the focus line
	bHandled = FALSE;
	return 0;
}

LRESULT CTokenEditCtrl::OnEditKillFocus(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	bHandled = FALSE;
	// to the control (tokens selected), what's typed waits there; leaving the control, it becomes a token
	CloseSuggestions();
	if ((HWND)wParam != m_hWnd)
		LeaveTyping();
	Invalidate(FALSE);
	return 0;
}

void CTokenEditCtrl::LeaveTyping() {
	auto& d = *m_Data;
	if (!d.tokenizing && !EditText().empty())
		Tokenize(TKEA_FOCUS, true);
	if (IsWindow() && EditText().empty())
		SetEditAt(-1);   // the caret's place among the tokens is forgotten
}

LRESULT CTokenEditCtrl::OnEditPaste(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (ReadOnly())
		return 0;
	auto clip = ClipboardText(m_hWnd);
	if (!TkeHasSeparator(clip, d.separators)) {
		bHandled = FALSE;   // one piece: the edit pastes it
		return 0;
	}
	// a single-line edit would keep only the first line: the text is put together here, then split
	int start = 0, end = 0;
	m_Edit.GetSel(start, end);
	auto text = EditText();
	start = std::clamp(start, 0, (int)text.size());
	end = std::clamp(end, start, (int)text.size());
	// pasted at the end, all of it becomes tokens; in the middle of what's typed, what follows the last separator
	// is still being typed
	const bool atEnd = end == (int)text.size();
	text = text.substr(0, start) + clip + text.substr(end);
	SetTyped(text, false);
	Tokenize(TKEA_PASTED, atEnd);
	return 0;
}
