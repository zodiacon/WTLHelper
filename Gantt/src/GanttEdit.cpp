// Editing: dragging a bar (moving it, its ends, its progress) and from its link handle to another bar. The task changes
// as the mouse moves, so its summaries and the grid's columns follow; the project as it was is kept, to go back to when
// the app vetoes the change (GNTN_TASKCHANGING), the drag is cancelled (Escape, the capture lost) or the user undoes it.
// Markers: labeled lines at times, with a tag at the top of the chart.

#include "GanttData.h"

#include <algorithm>
#include <cmath>

using namespace GntDraw;

namespace {
	GntUnit SnapUnit(GntUnit lower) {
		return lower == GntUnit::Hour ? GntUnit::Hour : GntUnit::Day;   // by the hour when the header shows hours
	}

	std::wstring ShortDate(GntTime t) {
		auto st = GntToSystemTime(t);
		wchar_t text[64];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"ddd MMM d", text, _countof(text), nullptr))
			return {};
		return text;
	}
}

// ---- Dragging ---------------------------------------------------------------------------------------

bool CGanttCtrl::BeginDrag(const GntHitInfo& hit, POINT pt) {
	auto& d = *m_Data;
	if ((GetStyle() & GNTS_READONLY) || hit.area != GntArea::Chart || hit.part == GntPart::None || hit.task < 0)
		return false;
	auto& t = d.project.Tasks()[hit.task];
	if (t.summary)
		return false;   // its dates are its children's
	d.drag = GntDrag::Pending;
	d.dragPart = hit.part;
	d.dragId = t.id;
	d.dragDown = d.dragMouse = pt;
	d.dragOriginal = t;
	d.before = d.project.Save();
	d.linkTarget = 0;
	SetCapture();
	return true;
}

void CGanttCtrl::DragTo(POINT pt) {
	auto& d = *m_Data;
	if (d.drag == GntDrag::Pending) {
		// a click until the mouse moves far enough
		if (std::abs(pt.x - d.dragDown.x) < ::GetSystemMetrics(SM_CXDRAG) && std::abs(pt.y - d.dragDown.y) < ::GetSystemMetrics(SM_CYDRAG))
			return;
		switch (d.dragPart) {
			case GntPart::Start: d.drag = GntDrag::Start; break;
			case GntPart::Finish: d.drag = GntDrag::Finish; break;
			case GntPart::Progress: d.drag = GntDrag::Progress; break;
			case GntPart::Link: d.drag = GntDrag::Link; break;
			default: d.drag = GntDrag::Move; break;
		}
	}
	d.dragMouse = pt;
	auto t = d.project.Find(d.dragId);
	if (!t) {
		CancelDrag();
		return;
	}
	const auto& o = d.dragOriginal;
	const auto& cal = d.project.calendar;
	const int dx = pt.x - d.dragDown.x;
	const GntUnit snap = SnapUnit(d.lower);
	switch (d.drag) {
		case GntDrag::Move:
			t->start = GntDragStart(o.start, dx, d.scale, snap, cal);
			break;
		case GntDrag::Start: {
			// the finish stays: at least a unit's work before it
			const int64_t least = snap == GntUnit::Hour ? 60 : std::max(1, cal.MinutesPerDay());
			GntTime start = GntDragStart(o.start, dx, d.scale, snap, cal);
			if (cal.WorkBetween(start, o.finish) < least)
				start = cal.AddWork(o.finish, -least);
			t->start = start;
			t->duration = cal.WorkBetween(start, o.finish);
			break;
		}
		case GntDrag::Finish:
			t->duration = GntDragDuration(o.start, o.finish, dx, d.scale, snap, cal);
			break;
		case GntDrag::Progress: {
			auto bar = GntLayoutBar(o, 0, d.scale, d.m);
			t->progress = GntDragProgress(bar, pt.x - d.chart.left);
			break;
		}
		case GntDrag::Link: {
			// a task it can be linked to
			auto hit = HitTest(pt);
			UINT target = hit.area == GntArea::Chart && hit.task >= 0 ? d.project.Tasks()[hit.task].id : 0;
			d.linkTarget = target && d.project.CanLink(d.dragId, target) ? target : 0;
			Invalidate(FALSE);
			return;
		}
		default:
			return;
	}
	d.project.Recompute();
	Invalidate(FALSE);
}

void CGanttCtrl::EndDrag(POINT pt) {
	auto& d = *m_Data;
	const auto kind = d.drag;
	DragTo(pt);
	d.drag = GntDrag::None;   // before the capture goes: losing it cancels a drag
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate(FALSE);
	if (kind == GntDrag::Pending || kind == GntDrag::None)
		return;   // a click
	if (kind == GntDrag::Link) {
		if (d.linkTarget)
			LinkTo(d.dragId, d.linkTarget);
		return;
	}
	auto t = d.project.Find(d.dragId);
	const auto& o = d.dragOriginal;
	if (!t || (t->start == o.start && t->duration == o.duration && t->progress == o.progress))
		return;   // where it was
	Apply(d.dragId, kind == GntDrag::Move ? GNTC_MOVE : kind == GntDrag::Start ? GNTC_START : kind == GntDrag::Finish ? GNTC_FINISH : GNTC_PROGRESS);
}

bool CGanttCtrl::Apply(UINT id, UINT change) {
	auto& d = *m_Data;
	auto t = d.project.Find(id);
	if (!t)
		return false;
	NMGANTTCHANGE nm{};
	nm.id = id;
	nm.change = change;
	nm.start = GntToSystemTime(t->start);
	nm.duration = t->duration;
	nm.progress = t->progress;
	nm.lParam = t->lParam;
	if (FireNotify(&nm.hdr, GNTN_TASKCHANGING)) {
		d.project.Restore(d.before);   // vetoed
		Changed();
		return false;
	}
	// as the app left the values
	t = d.project.Find(id);
	if (!t)
		return false;
	t->start = GntFromSystemTime(nm.start);
	t->duration = std::max<LONGLONG>(0, nm.duration);
	t->progress = std::clamp(nm.progress, 0, 100);
	d.project.Recompute();
	Commit(id, change);
	return true;
}

void CGanttCtrl::CancelDrag() {
	auto& d = *m_Data;
	const bool changed = d.drag != GntDrag::None && d.drag != GntDrag::Pending && d.drag != GntDrag::Link;
	d.drag = GntDrag::None;
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	if (changed) {
		d.project.Restore(d.before);
		Changed();
	}
	if (IsWindow())
		Invalidate(FALSE);
}

void CGanttCtrl::Commit(UINT id, UINT change) {
	auto& d = *m_Data;
	if (!(GetStyle() & GNTS_NOAUTOSCHEDULE))
		d.project.PushSuccessors(id);
	d.history.Push(d.before, d.project.Save());
	Changed();
	auto t = d.project.Find(id);
	if (!t)
		return;
	NMGANTTCHANGE nm{};
	nm.id = id;
	nm.change = change;
	nm.start = GntToSystemTime(t->start);
	nm.duration = t->duration;
	nm.progress = t->progress;
	nm.lParam = t->lParam;
	FireNotify(&nm.hdr, GNTN_TASKCHANGED);
}

void CGanttCtrl::LinkTo(UINT from, UINT to) {
	auto& d = *m_Data;
	NMGANTTLINK nm{};
	nm.from = from;
	nm.to = to;
	nm.type = GNTL_FINISHSTART;
	if (FireNotify(&nm.hdr, GNTN_LINKADDING))
		return;   // vetoed
	if (!d.project.AddLink({ from, to, (GntLinkType)std::min(nm.type, (UINT)GNTL_STARTFINISH), nm.lag }))
		return;
	if (!(GetStyle() & GNTS_NOAUTOSCHEDULE))
		d.project.PushSuccessors(from);
	d.history.Push(d.before, d.project.Save());
	Changed();
	FireNotify(&nm.hdr, GNTN_LINKADDED);
}

// ---- Undo -----------------------------------------------------------------------------------------

BOOL CGanttCtrl::Undo() {
	auto& d = *m_Data;
	if (d.drag != GntDrag::None || !d.history.Undo(d.project))
		return FALSE;
	Changed();
	NMHDR nm{};
	FireNotify(&nm, GNTN_RESTORED);
	return TRUE;
}

BOOL CGanttCtrl::Redo() {
	auto& d = *m_Data;
	if (d.drag != GntDrag::None || !d.history.Redo(d.project))
		return FALSE;
	Changed();
	NMHDR nm{};
	FireNotify(&nm, GNTN_RESTORED);
	return TRUE;
}

bool CGanttCtrl::CanUndo() const {
	return m_Data->history.CanUndo();
}

bool CGanttCtrl::CanRedo() const {
	return m_Data->history.CanRedo();
}

void CGanttCtrl::ClearUndo() {
	m_Data->history.Clear();
}

// ---- Markers ---------------------------------------------------------------------------------------

BOOL CGanttCtrl::AddMarker(UINT id, const SYSTEMTIME& time, LPCWSTR label, COLORREF color) {
	auto& m = m_Data->markers;
	GntMarker marker{ id, GntFromSystemTime(time), label ? label : L"", color };
	auto it = std::find_if(m.begin(), m.end(), [&](auto& x) { return x.id == id; });
	if (it != m.end())
		*it = std::move(marker);
	else
		m.push_back(std::move(marker));
	if (IsWindow())
		Invalidate(FALSE);
	return TRUE;
}

BOOL CGanttCtrl::RemoveMarker(UINT id) {
	auto& m = m_Data->markers;
	auto it = std::find_if(m.begin(), m.end(), [&](auto& x) { return x.id == id; });
	if (it == m.end())
		return FALSE;
	m.erase(it);
	Invalidate(FALSE);
	return TRUE;
}

namespace {
	// a marker's tag, at the chart's top, from its line on
	RECT MarkerTag(HDC dc, const GntData& d, const GntMarker& marker, int scale) {
		const int x = d.chart.left + (int)std::lround(d.scale.X(marker.time));
		SIZE size{};
		::GetTextExtentPoint32W(dc, marker.label.c_str(), (int)marker.label.size(), &size);
		const int top = d.chart.top + scale;
		return { x, top, x + size.cx + 4 * scale, top + d.tier - 2 * scale };
	}
}

int CGanttCtrl::MarkerAt(POINT pt) const {
	auto& d = *m_Data;
	if (!::PtInRect(&d.chart, pt))
		return -1;
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	int found = -1;
	for (int i = (int)d.markers.size() - 1; i >= 0 && found < 0; i--) {
		RECT tag = MarkerTag(dc, d, d.markers[i], Scale(2));
		const int x = tag.left;
		if (::PtInRect(&tag, pt) || std::abs(pt.x - x) <= Scale(3))
			found = i;
	}
	dc.SelectFont(old);
	return found;
}

void CGanttCtrl::PaintMarkers(WTL::CDCHandle dc, const GNTCOLORS& c) {
	auto& d = *m_Data;
	const RECT& ch = d.chart;
	for (auto& marker : d.markers) {
		const COLORREF color = marker.color != CLR_INVALID ? marker.color : IsDark(c.crBack) ? RGB(0xB3, 0x8B, 0xF0) : RGB(0x6B, 0x3F, 0xC4);
		RECT tag = MarkerTag(dc, d, marker, Scale(2));
		if (tag.right < ch.left || tag.left > ch.right)
			continue;
		WTL::CPen pen;
		pen.CreatePen(PS_DOT, 1, color);
		auto old = dc.SelectPen(pen);
		dc.SetBkMode(TRANSPARENT);
		dc.MoveTo(tag.left, ch.top);
		dc.LineTo(tag.left, ch.bottom);
		dc.SelectPen(old);
		RoundFill(dc, tag, color, color, Scale(6));
		dc.SetTextColor(c.crBack);
		dc.DrawText(marker.label.c_str(), (int)marker.label.size(), &tag, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
}

// ---- The drag's look --------------------------------------------------------------------------------

void CGanttCtrl::PaintEditing(WTL::CDCHandle dc, const GNTCOLORS& c) {
	auto& d = *m_Data;
	if (d.drag == GntDrag::None || d.drag == GntDrag::Pending)
		return;
	const int task = d.project.Index(d.dragId);
	auto it = std::find(d.rows.begin(), d.rows.end(), task);
	if (task < 0 || it == d.rows.end())
		return;
	const int row = (int)(it - d.rows.begin());
	auto& t = d.project.Tasks()[task];
	auto bar = GntLayoutBar(t, row, d.scale, d.m);
	::OffsetRect(&bar.rc, d.chart.left, d.chart.top - d.scrollY);
	if (d.drag == GntDrag::Link) {
		// a line from the link handle to the mouse, the task it would link to framed
		RECT handle = GntLinkHandle(bar, d.m);
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, Scale(2)), c.crBarDone);
		auto old = dc.SelectPen(pen);
		dc.MoveTo((handle.left + handle.right) / 2, (handle.top + handle.bottom) / 2);
		dc.LineTo(d.dragMouse);
		if (d.linkTarget) {
			int target = d.project.Index(d.linkTarget);
			auto tr = std::find(d.rows.begin(), d.rows.end(), target);
			if (tr != d.rows.end()) {
				auto tb = GntLayoutBar(d.project.Tasks()[target], (int)(tr - d.rows.begin()), d.scale, d.m);
				::OffsetRect(&tb.rc, d.chart.left, d.chart.top - d.scrollY);
				::InflateRect(&tb.rc, Scale(2), Scale(2));
				auto oldBrush = dc.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
				dc.RoundRect(&tb.rc, { Scale(6), Scale(6) });
				dc.SelectBrush(oldBrush);
			}
		}
		dc.SelectPen(old);
		return;
	}
	// what it's becoming, above the bar
	std::wstring text;
	if (d.drag == GntDrag::Progress) {
		wchar_t buffer[16];
		swprintf_s(buffer, L"%d%%", t.progress);
		text = buffer;
	}
	else {
		GntTime f = t.finish;
		if (f > t.start && f % GntDay == 0)
			f--;
		text = t.Milestone() ? ShortDate(t.start) : ShortDate(t.start) + L" \x2013 " + ShortDate(f);
	}
	SIZE size{};
	dc.GetTextExtent(text.c_str(), (int)text.size(), &size);
	const int pad = Scale(4);
	RECT box{ bar.rc.left, bar.rc.top - size.cy - 2 * pad - Scale(2), bar.rc.left + size.cx + 2 * pad, bar.rc.top - Scale(2) };
	if (box.top < d.chart.top)
		::OffsetRect(&box, 0, bar.rc.bottom - box.top + Scale(2));   // no room above: under it
	RoundFill(dc, box, c.crHeaderBack, c.crGrid, Scale(6));
	dc.SetTextColor(c.crText);
	dc.DrawText(text.c_str(), (int)text.size(), &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
