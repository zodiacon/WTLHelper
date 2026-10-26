// Editing: a press selects an item or slots; dragging moves an item (between the days and the strip too), resizes it by
// its ends, or selects more slots. The item changes as the mouse moves, so its window and the items beside it follow;
// the items as they were are kept, to go back to when the app vetoes the change (CLVN_ITEMCHANGING), the drag is
// cancelled (Escape, the capture lost) or the user undoes it. The mouse is the control's while a drag goes on (the
// days' window's presses too), so its points are the control's client coords, the days below daysTop.

#include "CalendarViewData.h"

#include <algorithm>

namespace {
	constexpr UINT_PTR DragTimer = 2;

	// the item's fields from a notification's, whole days for an all-day item
	void SpanFrom(ClvItem& item, const SYSTEMTIME& start, const SYSTEMTIME& end, BOOL allDay) {
		item.start = ClvFromSystemTime(start);
		item.end = std::max(item.start, ClvFromSystemTime(end));
		item.allDay = allDay != FALSE;
		if (item.allDay) {
			item.start = ClvDayOf(item.start) * ClvDay;
			item.end = std::max(ClvDayOf(item.end + ClvDay - 1), ClvDayOf(item.start) + 1) * ClvDay;
		}
	}
}

ClvHit CCalendarViewCtrl::HitAt(POINT pt, bool* strip) const {
	auto& d = *m_Data;
	*strip = pt.y < d.daysTop;
	return *strip ? HitStrip(pt) : HitDays({ pt.x, pt.y - d.daysTop });
}

LPCWSTR CCalendarViewCtrl::CursorAt(POINT pt) const {
	auto& d = *m_Data;
	switch (d.drag) {
		case ClvDrag::Move: return IDC_SIZEALL;
		case ClvDrag::Start: case ClvDrag::End: return IDC_SIZENS;
		case ClvDrag::StripStart: case ClvDrag::StripEnd: return IDC_SIZEWE;
		case ClvDrag::Select: return nullptr;
		default: break;
	}
	if (GetStyle() & CLVS_READONLY)
		return nullptr;
	bool strip;
	auto hit = HitAt(pt, &strip);
	if (hit.box < 0 || d.store.Items()[(strip ? d.layout.strip : d.layout.timed)[hit.box].item].readOnly)
		return nullptr;
	switch (hit.part) {
		case ClvPart::Start: case ClvPart::End: return strip ? IDC_SIZEWE : IDC_SIZENS;
		case ClvPart::Caption: return IDC_SIZEALL;   // a window item's handle
		default: return nullptr;
	}
}

// ---- Pressing ---------------------------------------------------------------------------------------

void CCalendarViewCtrl::Press(POINT pt, bool dbl) {
	auto& d = *m_Data;
	EndRename(true);
	if (::GetFocus() != m_hWnd)
		SetFocus();   // the keys are the control's
	if (NavPress(pt))
		return;
	bool strip;
	auto hit = HitAt(pt, &strip);
	if (hit.box >= 0) {
		const auto& box = (strip ? d.layout.strip : d.layout.timed)[hit.box];
		const auto& item = d.store.Items()[box.item];
		const ULONGLONG id = item.id;
		SetSelected(id, true);
		d.cursor = std::max(item.start, d.range.Start());
		if (dbl) {
			NotifyItem(CLVN_ITEMACTIVATE, id);
			return;
		}
		if ((GetStyle() & CLVS_READONLY) || item.readOnly)
			return;
		ClvDrag kind = ClvDrag::None;
		switch (hit.part) {
			case ClvPart::Start: kind = strip ? ClvDrag::StripStart : ClvDrag::Start; break;
			case ClvPart::End: kind = strip ? ClvDrag::StripEnd : ClvDrag::End; break;
			case ClvPart::Item: case ClvPart::Caption: case ClvPart::Body: kind = ClvDrag::Move; break;
			default: break;
		}
		if (kind == ClvDrag::None)
			return;
		d.drag = ClvDrag::Pending;
		d.pending = kind;
		d.dragId = id;
		d.dragOriginal = ClvSpanOf(item);
		d.dragDown = d.dragMouse = pt;
		d.before = d.store.Save();
		d.grabDay = d.range.first + std::max(0, hit.day);
		d.grab = strip ? ClvTimeOf(d.grabDay) : d.layout.TimeAt(std::max(0, hit.day), hit.minute);
		SetCapture();
		return;
	}
	if (hit.part != ClvPart::Slot)
		return;
	if (dbl) {
		CreateItem();   // over the slots the first click selected
		return;
	}
	// the slot pressed; a drag selects more (notified when the button comes up)
	const int64_t day = d.range.first + hit.day;
	d.anchorStrip = strip;
	d.anchor = strip ? ClvTimeOf(day) : d.layout.TimeAt(hit.day, hit.minute);
	auto span = strip ? ClvDayRange(day, day) : ClvSlotRange(d.anchor, d.anchor, d.axis.slotMinutes);
	SelectSlots(span.start, span.end, strip, false);
	d.cursor = span.start;
	d.drag = ClvDrag::Pending;
	d.pending = ClvDrag::Select;
	d.dragId = 0;
	d.dragDown = d.dragMouse = pt;
	SetCapture();
}

// ---- Dragging ---------------------------------------------------------------------------------------

void CCalendarViewCtrl::DragTo(POINT pt) {
	auto& d = *m_Data;
	d.dragMouse = pt;
	if (d.drag == ClvDrag::Pending) {
		// a click until the mouse moves far enough
		if (std::abs(pt.x - d.dragDown.x) < ::GetSystemMetrics(SM_CXDRAG) && std::abs(pt.y - d.dragDown.y) < ::GetSystemMetrics(SM_CYDRAG))
			return;
		d.drag = d.pending;
		if (d.drag != ClvDrag::Select)
			SetComposited(true);   // the items' windows move with every step: painted off screen meanwhile
	}

	// where the mouse is: a column (held to the days), and its time or day
	const auto& cols = d.layout.columns;
	const int x = std::clamp((int)pt.x, cols.left, cols.left + std::max(1, cols.width) - 1);
	const int col = std::max(0, cols.At(x));
	const bool inStrip = pt.y < d.daysTop;
	const int minute = std::min((int)ClvDay - 1, d.axis.Minute(pt.y - d.daysTop + d.scrollY));
	const ClvTime at = d.layout.TimeAt(col, minute);
	const int64_t day = d.range.first + col;
	const int slot = d.axis.slotMinutes;

	if (d.drag == ClvDrag::Select) {
		auto span = d.anchorStrip ? ClvDayRange(ClvDayOf(d.anchor), day) : ClvSlotRange(d.anchor, at, slot);
		if (span.start != d.slot.start || span.end != d.slot.end) {
			d.slot = span;
			d.cursor = d.anchorStrip ? ClvTimeOf(day) : ClvSlotRange(at, at, slot).start;
			Invalidate(FALSE);
			m_Days->Invalidate(FALSE);
		}
	}
	else if (d.drag != ClvDrag::None) {
		auto item = d.store.Find(d.dragId);
		if (!item) {
			CancelDrag();
			return;
		}
		const ClvSpan& o = d.dragOriginal;
		ClvSpan r = o;
		switch (d.drag) {
			case ClvDrag::Move:
				if (inStrip)
					r = ClvDragMoveStrip(o, d.grabDay, day);
				else {
					// from the strip into the days: an hour at the slot under the mouse
					ClvSpan from = o;
					from.allDay = o.allDay || o.end - o.start >= ClvDay;
					r = ClvDragMove(from, d.grab, at, slot, 60);
				}
				break;
			case ClvDrag::Start: r = ClvDragStart(o, at, slot, slot); break;
			case ClvDrag::End: r = ClvDragEnd(o, at, slot, slot); break;
			case ClvDrag::StripStart: r = ClvDragStripStart(o, day); break;
			case ClvDrag::StripEnd: r = ClvDragStripEnd(o, day); break;
			default: break;
		}
		if (r.start != item->start || r.end != item->end || r.allDay != item->allDay) {
			item->start = r.start;
			item->end = r.end;
			item->allDay = r.allDay;
			Changed();
		}
	}

	// past the days' top or bottom: they scroll
	RECT rc;
	GetClientRect(&rc);
	const int band = Scale(16);
	const bool scrolls = d.drag == ClvDrag::Move || d.drag == ClvDrag::Start || d.drag == ClvDrag::End
		|| (d.drag == ClvDrag::Select && !d.anchorStrip);
	if (scrolls && !inStrip && (pt.y < d.daysTop + band || pt.y >= rc.bottom - band))
		SetTimer(DragTimer, 50);
	else
		KillTimer(DragTimer);
}

void CCalendarViewCtrl::DragScroll() {
	auto& d = *m_Data;
	RECT rc;
	GetClientRect(&rc);
	const int band = Scale(16), step = std::max(2, d.axis.slotHeight / 3);
	const POINT pt = d.dragMouse;
	if (d.drag == ClvDrag::None || pt.y < d.daysTop) {
		KillTimer(DragTimer);
		return;
	}
	if (pt.y < d.daysTop + band)
		ScrollDays(d.scrollY - step);
	else if (pt.y >= rc.bottom - band)
		ScrollDays(d.scrollY + step);
	DragTo(pt);
}

void CCalendarViewCtrl::EndDrag(POINT pt) {
	auto& d = *m_Data;
	DragTo(pt);
	const auto kind = d.drag;
	d.drag = ClvDrag::None;   // before the capture goes: losing it cancels a drag
	KillTimer(DragTimer);
	SetComposited(false);
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	if (kind == ClvDrag::Select || (kind == ClvDrag::Pending && d.pending == ClvDrag::Select)) {
		if (d.slots)
			NotifyRange(CLVN_SLOTCHANGED, d.slot.start, d.slot.end, d.slot.allDay);
		return;
	}
	if (kind == ClvDrag::Pending || kind == ClvDrag::None)
		return;   // a click
	auto item = d.store.Find(d.dragId);
	const auto& o = d.dragOriginal;
	if (!item || (item->start == o.start && item->end == o.end && item->allDay == o.allDay))
		return;   // where it was
	if (Apply(d.dragId, kind == ClvDrag::Move ? CLVCH_MOVE : kind == ClvDrag::Start || kind == ClvDrag::StripStart ? CLVCH_START : CLVCH_END))
		EnsureVisible(d.dragId);   // a row of the strip may be scrolled away
}

void CCalendarViewCtrl::CancelDrag() {
	auto& d = *m_Data;
	const bool changed = d.drag != ClvDrag::None && d.drag != ClvDrag::Pending && d.drag != ClvDrag::Select;
	if (d.drag == ClvDrag::None)
		return;
	d.drag = ClvDrag::None;
	if (!IsWindow())
		return;
	KillTimer(DragTimer);
	SetComposited(false);
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	if (changed) {
		d.store.Restore(d.before);
		Changed();
	}
	Invalidate(FALSE);
}

void CCalendarViewCtrl::SetComposited(bool on) {
	if (!m_Days->IsWindow())
		return;
	const bool now = (m_Days->GetExStyle() & WS_EX_COMPOSITED) != 0;
	if (on != now)
		m_Days->ModifyStyleEx(on ? 0 : WS_EX_COMPOSITED, on ? WS_EX_COMPOSITED : 0);
}

LRESULT CCalendarViewCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	const POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	if (m_Data->drag != ClvDrag::None)
		DragTo(pt);
	else {
		NavHot(pt);
		UpdateTip(m_hWnd, pt);
	}
	return 0;
}

LRESULT CCalendarViewCtrl::OnDaysMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	const POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	if (m_Data->drag == ClvDrag::None)
		UpdateTip(m_Days->m_hWnd, pt);
	return 0;
}

LRESULT CCalendarViewCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_Data->navPressed >= 0) {
		NavRelease({ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) });
		return 0;
	}
	if (m_Data->drag != ClvDrag::None)
		EndDrag({ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) });
	return 0;
}

LRESULT CCalendarViewCtrl::OnDaysLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	// the capture is the control's, so this comes only when it was taken meanwhile
	if (m_Data->drag != ClvDrag::None)
		EndDrag({ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) + m_Data->daysTop });
	return 0;
}

LRESULT CCalendarViewCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Data->navPressed >= 0) {
		m_Data->navPressed = -1;
		InvalidateRect(&m_Data->navbar, FALSE);
	}
	if (m_Data->drag != ClvDrag::None)
		CancelDrag();   // the capture taken from us
	return 0;
}

LRESULT CCalendarViewCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	LPCWSTR cursor = (HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT ? CursorAt(pt) : nullptr;
	if (!cursor) {
		bHandled = FALSE;
		return 0;
	}
	::SetCursor(::LoadCursor(nullptr, cursor));
	return TRUE;
}

LRESULT CCalendarViewCtrl::OnDaysSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);   // the control's: the days are below daysTop
	LPCWSTR cursor = (HWND)wParam == m_Days->m_hWnd && LOWORD(lParam) == HTCLIENT ? CursorAt(pt) : nullptr;
	if (!cursor) {
		bHandled = FALSE;
		return 0;
	}
	::SetCursor(::LoadCursor(nullptr, cursor));
	return TRUE;
}

// ---- Changes ---------------------------------------------------------------------------------------------

bool CCalendarViewCtrl::Apply(ULONGLONG id, UINT change) {
	auto& d = *m_Data;
	auto item = d.store.Find(id);
	if (!item)
		return false;
	NMCLVCHANGE nm{};
	nm.id = id;
	nm.change = change;
	nm.start = ClvToSystemTime(item->start);
	nm.end = ClvToSystemTime(item->end);
	nm.allDay = item->allDay;
	nm.lParam = item->lParam;
	if (FireNotify(&nm.hdr, CLVN_ITEMCHANGING)) {
		d.store.Restore(d.before);   // vetoed
		Changed();
		return false;
	}
	// as the app left the values
	if (!(item = d.store.Find(id)))
		return false;
	SpanFrom(*item, nm.start, nm.end, nm.allDay);
	Step();
	Changed();
	NotifyAccessible(EVENT_OBJECT_DESCRIPTIONCHANGE, AccChildId(id));
	nm.start = ClvToSystemTime(item->start);
	nm.end = ClvToSystemTime(item->end);
	nm.allDay = item->allDay;
	FireNotify(&nm.hdr, CLVN_ITEMCHANGED);
	return true;
}

void CCalendarViewCtrl::Step() {
	auto& d = *m_Data;
	if (!d.isVirtual)   // the app's items: its undo
		d.history.Push(d.before, d.store.Save());
}

void CCalendarViewCtrl::CreateItem() {
	auto& d = *m_Data;
	if (!d.slots || (GetStyle() & CLVS_READONLY))
		return;
	NMCLVCREATE nm{};
	nm.start = ClvToSystemTime(d.slot.start);
	nm.end = ClvToSystemTime(d.slot.end);
	nm.allDay = d.slot.allDay;
	d.before = d.store.Save();
	d.creating = true;
	FireNotify(&nm.hdr, CLVN_CREATEITEM);
	d.creating = false;
	if (!nm.id || !d.store.Find(nm.id))
		return;
	Step();
	SetSelected(nm.id, true);
	EditTitle(nm.id);
}

void CCalendarViewCtrl::DeleteSelected() {
	auto& d = *m_Data;
	auto item = d.selected ? d.store.Find(d.selected) : nullptr;
	if (!item || item->readOnly || (GetStyle() & CLVS_READONLY))
		return;
	NMCLVITEM nm{};
	nm.id = item->id;
	nm.hwnd = item->hwnd;
	nm.lParam = item->lParam;
	if (FireNotify(&nm.hdr, CLVN_ITEMDELETING))
		return;   // vetoed
	d.before = d.store.Save();
	d.store.Remove(nm.id);
	Step();
	Changed();
	FireNotify(&nm.hdr, CLVN_ITEMDELETED);
}

// ---- Undo ---------------------------------------------------------------------------------------------

BOOL CCalendarViewCtrl::Undo() {
	auto& d = *m_Data;
	CancelDrag();
	EndRename(false);
	if (!d.history.Undo(d.store))
		return FALSE;
	Restored();
	return TRUE;
}

BOOL CCalendarViewCtrl::Redo() {
	auto& d = *m_Data;
	CancelDrag();
	EndRename(false);
	if (!d.history.Redo(d.store))
		return FALSE;
	Restored();
	return TRUE;
}

bool CCalendarViewCtrl::CanUndo() const {
	return m_Data->history.CanUndo();
}

bool CCalendarViewCtrl::CanRedo() const {
	return m_Data->history.CanRedo();
}

void CCalendarViewCtrl::ClearUndo() {
	m_Data->history.Clear();
}

void CCalendarViewCtrl::Restored() {
	Changed();
	NMHDR nm{};
	FireNotify(&nm, CLVN_RESTORED);
}
