#include "BreadcrumbData.h"

#include <commctrl.h>

#include <algorithm>

using namespace BrcDraw;

namespace {
	int CopyText(const std::wstring& s, LPWSTR text, int cch) {
		if (text && cch > 0) {
			size_t n = std::min(s.size(), (size_t)cch - 1);
			s.copy(text, n);
			text[n] = 0;
		}
		return (int)s.size() + 1;
	}

	std::wstring Trim(std::wstring s) {
		while (!s.empty() && iswspace(s.back()))
			s.pop_back();
		size_t i = 0;
		while (i < s.size() && iswspace(s[i]))
			i++;
		return s.substr(i);
	}
}

// ---- Editing ---------------------------------------------------------------------------------------

BOOL CBreadcrumbCtrl::BeginEdit() {
	auto& d = *m_Data;
	if (d.editing) {
		m_Edit.SetFocus();
		return TRUE;
	}
	if (!IsWindow() || !m_Edit.IsWindow() || (GetStyle() & BRCS_NOEDIT))
		return FALSE;
	NMHDR nm{};
	if (FireNotify(&nm, BRCN_BEGINEDIT) || !IsWindow())
		return FALSE;
	CloseDropDown();
	HWND focus = ::GetFocus();
	d.editFocusBefore = focus == m_hWnd || focus == m_Edit.m_hWnd ? nullptr : focus;
	d.editing = true;
	d.haveCandidates = false;
	d.hot = d.pressed = {};
	std::wstring text = d.editText;
	if (text.empty()) {
		text = PathTo(-1);
		if (text.empty() && !d.segments.empty())
			text = d.segments.back().text;   // "This PC"
	}
	EnsureLayout();
	PlaceEdit();
	SetText(text, true);
	m_Edit.ShowWindow(SW_SHOW);
	m_Edit.SetFocus();
	Invalidate(FALSE);
	return TRUE;
}

void CBreadcrumbCtrl::EndEdit() {
	EndEditing(true);
}

void CBreadcrumbCtrl::EndEditing(bool restoreFocus) {
	auto& d = *m_Data;
	if (!d.editing)
		return;
	d.editing = false;
	if (d.list && d.list->IsOpen())
		d.list->Close();
	HWND before = d.editFocusBefore;
	d.editFocusBefore = nullptr;
	if (restoreFocus && ::GetFocus() == m_Edit.m_hWnd) {
		if (before && ::IsWindow(before))
			::SetFocus(before);
		else
			SetFocus();   // the control, rather than a hidden window
	}
	m_Edit.ShowWindow(SW_HIDE);
	d.candidates.clear();
	d.haveCandidates = false;
	Invalidate(FALSE);
	NMHDR nm{};
	FireNotify(&nm, BRCN_ENDEDIT);
}

bool CBreadcrumbCtrl::IsEditing() const {
	return m_Data->editing;
}

void CBreadcrumbCtrl::SetEditText(LPCWSTR text) {
	auto& d = *m_Data;
	d.editText = text ? text : L"";
	if (d.editing)
		SetText(d.editText, true);
}

HWND CBreadcrumbCtrl::GetEditControl() const {
	return m_Edit.m_hWnd;
}

void CBreadcrumbCtrl::PlaceEdit() {
	if (!m_Edit.IsWindow())
		return;
	const RECT& r = m_Data->layout.edit;
	const int height = m_TextHeight + Scale(2);
	const int top = (int)(r.top + r.bottom - height) / 2;
	const int left = (int)r.left + Scale(4);
	m_Edit.SetWindowPos(nullptr, left, top, std::max(0, (int)r.right - left - Scale(2)), height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring CBreadcrumbCtrl::EditText() const {
	if (!m_Edit.IsWindow())
		return {};
	int len = m_Edit.GetWindowTextLength();
	std::wstring text(len + 1, L'\0');
	m_Edit.GetWindowText(text.data(), len + 1);
	text.resize(len);
	return text;
}

void CBreadcrumbCtrl::SetText(const std::wstring& text, bool selectAll) {
	auto& d = *m_Data;
	d.settingText = true;
	m_Edit.SetWindowText(text.c_str());
	d.settingText = false;
	if (selectAll)
		m_Edit.SetSel(0, -1);
	else
		m_Edit.SetSel((int)text.size(), (int)text.size());   // the caret at the end (-1 would only deselect)
}

// ---- Suggestions and history ----------------------------------------------------------------

BOOL CBreadcrumbCtrl::AddSuggestion(LPCWSTR path) {
	auto& d = *m_Data;
	if (!d.collectingSuggestions || !path || !*path)
		return FALSE;
	d.collectingSuggestions->emplace_back(path);
	return TRUE;
}

void CBreadcrumbCtrl::Suggest(bool force) {
	auto& d = *m_Data;
	if (!d.editing)
		return;
	auto text = EditText();
	auto closeList = [&] {
		if (d.list && d.list->IsOpen())
			d.list->Close();
	};
	if (text.empty() && !force) {
		closeList();
		return;
	}
	std::wstring parent, leaf;
	BrcSplitLast(text, d.separators, parent, leaf);
	if (!d.haveCandidates || !BrcSamePath(parent, d.candidatesParent)) {
		// the parent's items, asked once while it's the same
		d.candidates.clear();
		NMBRCSUGGEST nm{};
		nm.pszText = text.c_str();
		nm.pszParent = parent.c_str();
		d.collectingSuggestions = &d.candidates;
		FireNotify(&nm.hdr, BRCN_GETSUGGESTIONS);
		d.collectingSuggestions = nullptr;
		if (!IsWindow() || !d.editing)
			return;
		d.candidatesParent = parent;
		d.haveCandidates = true;
	}
	auto texts = BrcSuggest(text, d.candidates, d.history.Paths(), 100);
	std::erase(texts, text);   // just what's typed suggests nothing
	if (texts.empty())
		closeList();
	else
		ShowTextList(texts, (int)BrcList::Suggestions);
}

void CBreadcrumbCtrl::ShowTextList(const std::vector<std::wstring>& texts, int kind) {
	auto& d = *m_Data;
	d.texts = texts;
	std::vector<BrcRow> rows;
	rows.reserve(texts.size());
	for (int i = 0; i < (int)texts.size(); i++)
		rows.push_back({ texts[i], -1, false, false, BrcTagText + i });
	EnsureList();
	if (d.list->IsOpen() && d.listKind == BrcList::Children)
		d.list->Close();
	d.listKind = (BrcList)kind;
	d.opened = d.listKind == BrcList::History ? BrcHit{ BrcPart::History } : BrcHit{};
	// as wide as the box, under it
	RECT under = d.layout.box;
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&under, 2);
	d.list->Show(std::move(rows), under, false, false, (int)(under.right - under.left));
	Invalidate(FALSE);
}

BOOL CBreadcrumbCtrl::ShowHistory() {
	auto& d = *m_Data;
	if (d.history.Paths().empty() || (!d.editing && !BeginEdit()))
		return FALSE;
	ShowTextList(d.history.Paths(), (int)BrcList::History);
	return TRUE;
}

void CBreadcrumbCtrl::AddToHistory(LPCWSTR path) {
	if (path)
		m_Data->history.Add(path);
}

int CBreadcrumbCtrl::GetHistory(LPWSTR text, int cch) const {
	return CopyText(m_Data->history.Save(), text, cch);
}

void CBreadcrumbCtrl::SetHistory(LPCWSTR text) {
	m_Data->history.Load(text ? text : L"");
}

void CBreadcrumbCtrl::Commit() {
	auto& d = *m_Data;
	if (!d.editing)
		return;
	auto text = Trim(EditText());
	if (text.empty()) {
		EndEditing(true);
		return;
	}
	if (d.list && d.list->IsOpen())
		d.list->Close();
	NMBRCNAVIGATE nm{};
	nm.source = BRCNS_TYPED;
	nm.index = -1;
	nm.pszPath = text.c_str();
	nm.pszText = text.c_str();
	d.committing = true;   // a message box of the app's takes the focus: editing goes on
	LRESULT rejected = FireNotify(&nm.hdr, BRCN_NAVIGATE);
	d.committing = false;
	if (!IsWindow() || !d.editing)
		return;
	if (rejected) {
		m_Edit.SetFocus();
		m_Edit.SetSel(0, -1);
		return;
	}
	d.history.Add(text);
	EndEditing(true);
}

// ---- The edit box's messages -----------------------------------------------------------------------

LRESULT CBreadcrumbCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if ((HWND)lParam != m_Edit.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	if (HIWORD(wParam) == EN_CHANGE && d.editing && !d.settingText)
		Suggest(false);
	return 0;
}

LRESULT CBreadcrumbCtrl::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)lParam != m_Edit.m_hWnd) {
		bHandled = FALSE;
		return 0;
	}
	auto c = GetColors();
	if (m_EditBrushColor != c.crBox) {
		if (!m_EditBrush.IsNull())
			m_EditBrush.DeleteObject();
		m_EditBrush.CreateSolidBrush(c.crBox);
		m_EditBrushColor = c.crBox;
	}
	WTL::CDCHandle dc((HDC)wParam);
	dc.SetTextColor(c.crText);
	dc.SetBkColor(c.crBox);
	return (LRESULT)m_EditBrush.m_hBrush;
}

LRESULT CBreadcrumbCtrl::OnEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	const bool listOpen = d.list && d.list->IsOpen() && d.listKind != BrcList::Children;
	auto hotText = [&]() -> const std::wstring* {
		int tag = listOpen ? d.list->HotTag() : -1;
		return tag >= BrcTagText && tag - BrcTagText < (int)d.texts.size() ? &d.texts[tag - BrcTagText] : nullptr;
	};
	switch (wParam) {
		case VK_RETURN:
			Commit();
			return 0;
		case VK_ESCAPE:
			if (listOpen)
				d.list->Close();
			else
				EndEditing(true);
			return 0;
		case VK_F4:
			if (listOpen && d.listKind == BrcList::History)
				d.list->Close();
			else
				ShowHistory();
			return 0;
		case VK_UP:
		case VK_DOWN:
		case VK_PRIOR:
		case VK_NEXT:
			if (!listOpen) {
				if (wParam == VK_DOWN)
					Suggest(true);
				return 0;
			}
			// the row picked with the keys goes into the box
			d.list->MoveHot((UINT)wParam);
			if (auto text = hotText())
				SetText(*text, false);
			return 0;
		case VK_TAB:
			if (listOpen) {
				// completes: the row picked with the keys, else as far as the suggestions agree
				auto typed = EditText();
				auto text = hotText();
				auto done = text ? *text : BrcCommonPrefix(typed, d.texts);
				if (done != typed) {
					SetText(done, false);
					Suggest(false);
				}
			}
			return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	switch (wParam) {
		case L'\r':
		case L'\n':
		case L'\t':
		case 27:
			return 0;   // handled as keys: no beep
		case 0x7F: {
			// Ctrl+Backspace: the word (a path part) before the caret
			DWORD start = 0, end = 0;
			m_Edit.SendMessage(EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
			auto text = EditText();
			auto stop = [&](wchar_t ch) {
				return iswspace(ch) || d.separators.find(ch) != std::wstring::npos;
			};
			size_t i = std::min((size_t)start, text.size());
			while (i > 0 && stop(text[i - 1]))
				i--;
			while (i > 0 && !stop(text[i - 1]))
				i--;
			m_Edit.SetSel((int)i, (int)end);
			m_Edit.ReplaceSel(L"", TRUE);
			return 0;
		}
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CBreadcrumbCtrl::OnEditGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	// all the keys: Enter, Escape and Tab (completing) in a dialog too
	return m_Edit.DefWindowProc(uMsg, wParam, lParam) | DLGC_WANTALLKEYS;
}

LRESULT CBreadcrumbCtrl::OnEditKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	if (d.editing && !d.committing)
		EndEditing(false);   // the focus went somewhere: it stays there
	bHandled = FALSE;
	return 0;
}
