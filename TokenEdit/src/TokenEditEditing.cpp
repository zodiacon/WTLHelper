#include "TokenEditData.h"

#include <commctrl.h>

#include <algorithm>

// ---- Editing a token ------------------------------------------------------------------------------------
//
// The token comes out of the list and its text goes into the typing area at its place, selected. What's typed then
// becomes tokens as any typing does, the first of them taking the old token's image, colors, tip and lParam (TKEA_EDITED),
// all one undo step from before it came out. Escape puts it back as it was; the edit left empty removes it.

BOOL CTokenEditCtrl::EditToken(UINT id) {
	int i = m_Data->tokens.Find(id);
	return i >= 0 && StartEdit(i, false);
}

bool CTokenEditCtrl::IsEditingToken() const {
	return m_Data->editing;
}

bool CTokenEditCtrl::StartEdit(int index, bool user) {
	auto& d = *m_Data;
	if (!IsWindow() || ReadOnly() || !m_Edit.IsWindow() || index < 0 || index >= (int)d.tokens.Size())
		return false;
	if (user && (GetStyle() & TKES_NOEDITING))
		return false;
	if (d.tokens[index].locked)
		return false;
	const UINT id = d.tokens[index].id;
	// what's typed first becomes tokens (an edit already going on ends so)
	if (!EditText().empty()) {
		Tokenize(TKEA_TYPED, true);
		if (!IsWindow() || !EditText().empty())
			return false;   // refused: the user fixes that first
	}
	if (d.editing)
		EndTokenEdit(false);
	index = d.tokens.Find(id);
	if (index < 0)
		return false;
	if (user) {
		auto& t = d.tokens[index];
		NMTKETOKEN nm{};
		nm.id = t.id;
		nm.index = index;
		nm.pszText = t.text.c_str();
		nm.pszTip = t.tip.c_str();
		nm.iImage = t.image;
		nm.crBack = t.back;
		nm.crText = t.fore;
		nm.flags = (t.invalid ? TKEF_INVALID : 0) | (t.locked ? TKEF_LOCKED : 0) | (t.selected ? TKEF_SELECTED : 0);
		nm.lParam = t.lParam;
		if (FireNotify(&nm.hdr, TKEN_TOKENEDITING) || !IsWindow())
			return false;
		index = d.tokens.Find(id);
		if (index < 0)
			return false;
	}
	CloseSuggestions();
	CancelDrag();
	d.tokens.SelectAll(false);
	d.editBefore = d.tokens.Save();
	d.edited = d.tokens[index];
	d.edited.selected = false;
	d.editing = true;
	d.editStale = false;
	d.tokens.Remove(index);
	d.editAt = index < (int)d.tokens.Size() ? index : -1;
	d.focus = -1;
	d.hot = d.pressed = {};
	Changed(true);
	m_Edit.SetFocus();
	SetTyped(d.edited.text, true);
	ScrollToShow(d.layout.edit);
	NotifySelection();
	return true;
}

void CTokenEditCtrl::EndTokenEdit(bool cancel) {
	auto& d = *m_Data;
	if (!d.editing)
		return;
	d.editing = false;
	if (cancel) {
		// back where it was, as it was; nothing changed
		const size_t at = d.editAt < 0 ? d.tokens.Size() : std::min((size_t)d.editAt, d.tokens.Size());
		d.tokens.Insert(at, d.edited);
		if (d.editAt >= 0)
			d.editAt = (int)at + 1;
		SetTyped({}, false);
		Changed(true);
		return;
	}
	// emptied: it's gone
	NMTKETOKEN nm{};
	nm.id = d.edited.id;
	nm.index = d.editAt < 0 ? (int)d.tokens.Size() : d.editAt;
	nm.pszText = d.edited.text.c_str();
	nm.pszTip = d.edited.tip.c_str();
	nm.iImage = d.edited.image;
	nm.crBack = d.edited.back;
	nm.crText = d.edited.fore;
	nm.flags = (d.edited.invalid ? TKEF_INVALID : 0) | (d.edited.locked ? TKEF_LOCKED : 0);
	nm.lParam = d.edited.lParam;
	if (!d.editStale)
		d.history.Push(d.editBefore, d.tokens.Save());
	FireNotify(&nm.hdr, TKEN_TOKENREMOVED);
	if (!IsWindow())
		return;
	Changed(true);
	NotifyChanged();
}

// ---- Dragging ---------------------------------------------------------------------------------------------

void CTokenEditCtrl::CancelDrag() {
	auto& d = *m_Data;
	if (d.drag == TkeData::Drag::None)
		return;
	d.drag = TkeData::Drag::None;   // first: releasing the capture comes back here
	d.dropIndex = -1;
	d.pendingOnly = -1;
	if (IsWindow()) {
		if (GetCapture() == m_hWnd)
			ReleaseCapture();
		Invalidate(FALSE);
	}
}

void CTokenEditCtrl::Drop() {
	auto& d = *m_Data;
	if (d.dropIndex < 0 || !d.tokens.SelectedCount())
		return;
	std::vector<UINT> order;
	order.reserve(d.tokens.Size());
	for (auto& t : d.tokens.Tokens())
		order.push_back(t.id);
	auto before = d.tokens.Save();
	// the typing area keeps its place by the token after it
	const UINT editBeforeId = d.editAt >= 0 && d.editAt < (int)d.tokens.Size() ? d.tokens[d.editAt].id : 0;
	const size_t first = d.tokens.MoveSelected(d.dropIndex);
	bool moved = false;
	for (size_t i = 0; i < order.size(); i++)
		if (d.tokens[i].id != order[i])
			moved = true;
	if (!moved)
		return;   // dropped onto itself
	if (editBeforeId)
		d.editAt = d.tokens.Find(editBeforeId);
	d.focus = (int)first;
	d.history.Push(std::move(before), d.tokens.Save());
	Changed(false);
	ScrollToShow(d.layout.tokens[first].rc);
	NMHDR nm{};
	FireNotify(&nm, TKEN_TOKENSMOVED);
	if (IsWindow())
		NotifyChanged();
}

// ---- Undo -----------------------------------------------------------------------------------------------

bool CTokenEditCtrl::CanUndo() const {
	return m_Data->history.CanUndo();
}

bool CTokenEditCtrl::CanRedo() const {
	return m_Data->history.CanRedo();
}

void CTokenEditCtrl::ClearUndo() {
	m_Data->history.Clear();
}

BOOL CTokenEditCtrl::Undo() {
	auto& d = *m_Data;
	EndTokenEdit(true);   // an edit going on is put back first
	if (!d.history.Undo(d.tokens))
		return FALSE;
	AfterRestore();
	return TRUE;
}

BOOL CTokenEditCtrl::Redo() {
	auto& d = *m_Data;
	EndTokenEdit(true);
	if (!d.history.Redo(d.tokens))
		return FALSE;
	AfterRestore();
	return TRUE;
}

void CTokenEditCtrl::AfterRestore() {
	auto& d = *m_Data;
	CloseSuggestions();
	CancelDrag();
	d.tokens.SelectAll(false);   // the snapshots' selections are history
	d.editAt = d.focus = -1;
	d.hot = d.pressed = {};
	Changed(true);
	NotifyChanged();
	if (IsWindow())
		NotifySelection();
}

// ---- Tooltips ---------------------------------------------------------------------------------------------

void CTokenEditCtrl::SetCloseTip(LPCWSTR text) {
	m_Data->closeTip = text ? text : L"";
	UpdateTip();
}

void CTokenEditCtrl::UpdateTip() {
	auto& d = *m_Data;
	if (!m_hTip)
		return;
	// a close button says what it does; a token shows its tip, or its text when that's cut
	std::wstring text;
	RECT rc{};
	if (d.drag == TkeData::Drag::None && d.hot.index >= 0 && d.hot.index < (int)d.tokens.Size()) {
		auto& t = d.tokens[d.hot.index];
		auto& box = d.layout.tokens[d.hot.index];
		if (d.hot.part == TkePart::Close) {
			text = d.closeTip.empty() ? L"Remove" : d.closeTip;
			rc = box.close;
		}
		else if (d.hot.part == TkePart::Token) {
			text = !t.tip.empty() ? t.tip : box.cut ? t.text : L"";
			rc = box.rc;
			if (!::IsRectEmpty(&box.close))
				rc.right = box.close.left;   // the close button has its own
		}
		::OffsetRect(&rc, 0, -d.scroll);
	}
	if (text.empty())
		rc = {};
	if (text == m_TipText && ::EqualRect(&rc, &d.tipRect))
		return;
	m_TipText = text;
	d.tipRect = rc;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	ti.rect = rc;
	::SendMessage(m_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	::SendMessage(m_hTip, TTM_POP, 0, 0);
	if (!text.empty()) {
		// the mouse move that made it hot reached the tooltip before the tool moved there
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		MSG msg{ m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y) };
		::SendMessage(m_hTip, TTM_RELAYEVENT, 0, (LPARAM)&msg);
	}
}

LRESULT CTokenEditCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom == m_hTip && hdr->code == TTN_GETDISPINFOW) {
		((NMTTDISPINFOW*)hdr)->lpszText = m_TipText.data();
		return 0;
	}
	bHandled = FALSE;
	return 0;
}
