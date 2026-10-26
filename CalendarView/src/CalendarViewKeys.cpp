// The keys: the arrows move the slots selected (a cursor slot; Shift extends from the anchor), past the days shown to
// the next ones; Tab goes along the items; Alt+arrows move the selected item (Alt+Shift+Up/Down: its end) through the
// same veto and undo as a drag; Enter opens an item or asks for one over the slots; F2 renames; Delete removes. The
// title edited in place, and the tooltips.

#include "CalendarViewData.h"

#include <commctrl.h>

#include <algorithm>

namespace {
	std::wstring DateText(ClvTime t, LPCWSTR picture) {
		auto st = ClvToSystemTime(t);
		wchar_t text[80];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}

	std::wstring TimeOnly(ClvTime t) {
		auto st = ClvToSystemTime(t);
		wchar_t text[40];
		if (!::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, text, _countof(text)))
			return {};
		return text;
	}

	// an item's time as a tooltip says it
	std::wstring When(const ClvItem& item) {
		const LPCWSTR day = L"ddd MMM d";
		if (item.allDay) {
			const int64_t first = ClvFirstDay(item), last = ClvLastDay(item);
			auto text = DateText(ClvTimeOf(first), day);
			if (last > first)
				text += L" \x2013 " + DateText(ClvTimeOf(last), day);
			return text;
		}
		if (ClvDayOf(item.start) == ClvLastDay(item))
			return DateText(item.start, day) + L", " + TimeOnly(item.start) + L" \x2013 " + TimeOnly(item.end);
		return DateText(item.start, day) + L" " + TimeOnly(item.start) + L" \x2013 " + DateText(item.end, day) + L" " + TimeOnly(item.end);
	}
}

LRESULT CCalendarViewCtrl::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& d = *m_Data;
	const bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0, alt = ::GetKeyState(VK_MENU) < 0;
	const bool arrow = wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN;
	if (uMsg == WM_SYSKEYDOWN && !(alt && arrow)) {
		bHandled = FALSE;   // Alt with anything else: the menu's
		return 0;
	}
	if (wParam == VK_ESCAPE && d.drag != ClvDrag::None) {
		CancelDrag();
		return 0;
	}
	if (d.drag == ClvDrag::None && KeyDown((UINT)wParam, ctrl, alt, shift))
		return 0;
	bHandled = FALSE;
	return 0;
}

bool CCalendarViewCtrl::KeyDown(UINT vk, bool ctrl, bool alt, bool shift) {
	auto& d = *m_Data;
	const bool editable = !(GetStyle() & CLVS_READONLY);
	const bool arrow = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN;
	const int days = vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : 0, slots = vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : 0;
	if (ctrl) {
		if (vk == 'Z' && !shift && editable)
			Undo();
		else if ((vk == 'Y' || (vk == 'Z' && shift)) && editable)
			Redo();
		else
			return false;
		return true;
	}
	if (arrow) {
		if (alt) {
			if (editable)
				Nudge(days, slots, shift && slots != 0);
		}
		else
			MoveSlots(days, slots, shift);
		return true;
	}
	switch (vk) {
		case VK_TAB: {
			const ULONGLONG next = ClvNextItem(ClvTabOrder(d.layout, d.store), d.selected, !shift);
			if (!next)
				return false;   // on to the dialog's next control
			SetSelected(next, true);
			EnsureVisible(next);
			if (auto item = d.store.Find(next))
				d.cursor = std::max(item->start, d.range.Start());
			return true;
		}
		case VK_RETURN:
			if (d.selected)
				NotifyItem(CLVN_ITEMACTIVATE, d.selected);
			else
				CreateItem();
			return true;
		case VK_F2:
			if (d.selected)
				EditTitle(d.selected);
			return true;
		case VK_DELETE:
			DeleteSelected();
			return true;
		case VK_PRIOR:
		case VK_NEXT: {
			RECT rc;
			m_Days->GetClientRect(&rc);
			ScrollDays(d.scrollY + (vk == VK_NEXT ? 1 : -1) * std::max(d.axis.slotHeight, (int)rc.bottom - d.axis.slotHeight));
			return true;
		}
	}
	return false;
}

LRESULT CCalendarViewCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	LRESULT code = DLGC_WANTARROWS;
	if (d.drag != ClvDrag::None)
		code |= DLGC_WANTALLKEYS;
	// Tab while there's an item to go to, else the dialog's
	if (ClvNextItem(ClvTabOrder(d.layout, d.store), d.selected, ::GetKeyState(VK_SHIFT) >= 0))
		code |= DLGC_WANTTAB;
	return code;
}

void CCalendarViewCtrl::MoveSlots(int days, int slots, bool extend) {
	auto& d = *m_Data;
	const int slot = d.axis.slotMinutes;
	if (d.cursor < 0 || !d.range.Contains(ClvDayOf(d.cursor))) {
		// from the slots selected, else the item's start, else the working day's start
		auto item = d.selected ? d.store.Find(d.selected) : nullptr;
		d.cursor = d.slots ? d.slot.start : item ? std::max(item->start, d.range.Start()) : d.layout.TimeAt(0, d.work.start);
	}
	if (d.slots && d.slot.allDay && slots == 0 && !extend) {
		// along the strip's days
		const int64_t day = ClvDayOf(d.cursor) + days;
		if (!d.range.Contains(day)) {
			d.range.first += days;
			RangeChanged(true);
		}
		auto span = ClvDayRange(day, day);
		d.cursor = span.start;
		SelectSlots(span.start, span.end, true, true);
		return;
	}
	const ClvTime cursor = ClvMoveSlot(d.cursor, days, slots, slot);
	if (!extend || !d.slots || d.slot.allDay)
		d.anchor = cursor;
	d.anchorStrip = false;
	// past the days shown: the next ones
	const int64_t day = ClvDayOf(cursor);
	if (!d.range.Contains(day)) {
		d.range.first += day < d.range.first ? day - d.range.first : day - d.range.Last();
		RangeChanged(true);
	}
	d.cursor = cursor;
	auto span = ClvSlotRange(d.anchor, cursor, slot);
	SelectSlots(span.start, span.end, false, true);
	RECT rc;
	m_Days->GetClientRect(&rc);
	const int minute = ClvMinuteOf(cursor);
	ScrollDays(ClvScrollToShow(d.scrollY, d.axis.Y(minute), d.axis.Y(minute + slot), rc.bottom, d.axis.Height()));
}

void CCalendarViewCtrl::Nudge(int days, int slots, bool end) {
	auto& d = *m_Data;
	auto item = d.selected ? d.store.Find(d.selected) : nullptr;
	if (!item || item->readOnly)
		return;
	const ULONGLONG id = item->id;
	const int slot = d.axis.slotMinutes;
	const bool strip = ClvInStrip(*item);
	const ClvSpan o = ClvSpanOf(*item);
	ClvSpan r = o;
	if (end) {
		r = strip ? ClvDragStripEnd(o, ClvLastDay(*item) + slots) : ClvDragEnd(o, o.end + (ClvTime)slots * slot, slot, slot);
	}
	else {
		if (strip && slots != 0)
			return;   // whole days only
		const ClvTime delta = (ClvTime)days * ClvDay + (ClvTime)slots * slot;
		r = { o.start + delta, o.end + delta, o.allDay };
	}
	if (r.start == o.start && r.end == o.end)
		return;
	d.before = d.store.Save();
	item->start = r.start;
	item->end = r.end;
	if (!d.range.Contains(ClvFirstDay(*item)) && !d.range.Contains(ClvLastDay(*item))) {
		d.range.first += days;   // it went past the days shown: they follow
		RangeChanged(true);
	}
	Changed();
	if (Apply(id, end ? CLVCH_END : CLVCH_MOVE))
		EnsureVisible(id);
}

// ---- The title edited in place ---------------------------------------------------------------------------

BOOL CCalendarViewCtrl::EditTitle(ULONGLONG id) {
	auto& d = *m_Data;
	if ((GetStyle() & CLVS_READONLY) || !m_Edit.IsWindow() || !d.store.Find(id))
		return FALSE;
	EndRename(true);
	EnsureVisible(id);
	auto item = d.store.Find(id);
	if (!item)
		return FALSE;
	// its piece: the strip's, or its first day's shown
	RECT rc{};
	if (auto box = d.layout.FindStrip(id)) {
		rc = box->rc;
		::OffsetRect(&rc, 0, d.strip.top + d.m.stripPad - d.stripScroll);
	}
	else {
		const int day = (int)(std::max(ClvFirstDay(*item), d.range.first) - d.range.first);
		auto timed = d.layout.FindTimed(id, day);
		if (!timed)
			return FALSE;
		rc = timed->rc;
		::OffsetRect(&rc, 0, d.daysTop - d.scrollY);
	}
	// over the title's line
	TEXTMETRICW tm{};
	{
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hBold);
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
	}
	RECT edit{ rc.left + 1 + Scale(4) + Scale(2), rc.top + 1, rc.right - 1, std::min((int)rc.bottom - 1, (int)(rc.top + 1 + tm.tmHeight + Scale(4))) };
	if (edit.right <= edit.left || edit.bottom <= edit.top)
		return FALSE;
	d.renaming = id;
	m_Edit.SetFont(m_hBold);
	m_Edit.SetWindowText(item->title.c_str());
	m_Edit.SetWindowPos(HWND_TOP, &edit, SWP_SHOWWINDOW);
	m_Edit.SetSel(0, -1);
	m_Edit.SetFocus();
	return TRUE;
}

void CCalendarViewCtrl::EndRename(bool commit) {
	auto& d = *m_Data;
	if (!d.renaming)
		return;
	const ULONGLONG id = d.renaming;
	d.renaming = 0;
	CString text;
	m_Edit.GetWindowText(text);
	if (::GetFocus() == m_Edit.m_hWnd)
		SetFocus();
	m_Edit.ShowWindow(SW_HIDE);
	auto item = d.store.Find(id);
	if (!commit || !item || item->title == (LPCWSTR)text)
		return;
	NMCLVTITLE nm{};
	nm.id = id;
	nm.pszTitle = text;
	nm.lParam = item->lParam;
	if (FireNotify(&nm.hdr, CLVN_TITLECHANGING))
		return;   // vetoed
	d.before = d.store.Save();
	if (auto r = d.store.Find(id))
		r->title = (LPCWSTR)text;
	Step();
	Invalidate(FALSE);
	m_Days->Invalidate(FALSE);
	FireNotify(&nm.hdr, CLVN_TITLECHANGED);
}

LRESULT CCalendarViewCtrl::OnEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
		EndRename(wParam == VK_RETURN);
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCalendarViewCtrl::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == L'\r' || wParam == 27 || wParam == L'\n')
		return 0;   // handled as keys: no beep
	bHandled = FALSE;
	return 0;
}

LRESULT CCalendarViewCtrl::OnEditGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	return m_Edit.DefWindowProc(uMsg, wParam, lParam) | DLGC_WANTALLKEYS;
}

LRESULT CCalendarViewCtrl::OnEditKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	EndRename(true);   // the focus went elsewhere: as a list view does, the title typed stays
	bHandled = FALSE;
	return 0;
}

// ---- Tooltips ----------------------------------------------------------------------------------------

void CCalendarViewCtrl::UpdateTip(HWND window, POINT pt) {
	auto& d = *m_Data;
	if (!m_hTip)
		return;
	std::wstring text;
	RECT rc{};
	if (d.drag == ClvDrag::None && !d.renaming) {
		const bool strip = window == m_hWnd;
		auto hit = strip ? HitStrip(pt) : HitDays(pt);
		if (hit.box >= 0) {
			const auto& box = (strip ? d.layout.strip : d.layout.timed)[hit.box];
			const auto& item = d.store.Items()[box.item];
			text = item.tip.empty() ? item.title + L"\n" + When(item) : item.tip;
			rc = box.rc;
			::OffsetRect(&rc, 0, strip ? d.strip.top + d.m.stripPad - d.stripScroll : -d.scrollY);
		}
	}
	if (text == d.tipText && ::EqualRect(&rc, &d.tipRect) && window == d.tipWindow)
		return;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.uId = 1;
	if (d.tipWindow && d.tipWindow != window) {
		// the other window's tool: nowhere
		ti.hwnd = d.tipWindow;
		::SendMessage(m_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	}
	d.tipText = text;
	d.tipRect = rc;
	d.tipWindow = window;
	ti.hwnd = window;
	ti.rect = rc;
	::SendMessage(m_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	::SendMessage(m_hTip, TTM_POP, 0, 0);
	if (!text.empty()) {
		// the mouse move that changed it reached the tooltip before the tool moved there
		MSG msg{ window, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y) };
		::SendMessage(m_hTip, TTM_RELAYEVENT, 0, (LPARAM)&msg);
	}
}

LRESULT CCalendarViewCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom == m_hTip && hdr->code == TTN_GETDISPINFOW) {
		((NMTTDISPINFOW*)hdr)->lpszText = m_Data->tipText.data();
		return 0;
	}
	bHandled = FALSE;
	return 0;
}
