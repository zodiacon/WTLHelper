// The keys (selecting, expanding, moving a task a unit, renaming it in place, deleting a link), selecting links with the
// mouse, and the tooltips. Changes made with the keys go through the same notifications and undo as drags.

#include "GanttData.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>

using namespace GntDraw;

namespace {
	const wchar_t* const LinkKinds[] = { L"Finish to start", L"Start to start", L"Finish to finish", L"Start to finish" };

	std::wstring DateText(GntTime t, LPCWSTR picture) {
		auto st = GntToSystemTime(t);
		wchar_t text[64];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}
}

// ---- Selecting ---------------------------------------------------------------------------------------

void CGanttCtrl::SelectRow(int row) {
	auto& d = *m_Data;
	if (d.rows.empty())
		return;
	row = std::clamp(row, 0, (int)d.rows.size() - 1);
	const UINT id = d.project.Tasks()[d.rows[row]].id;
	d.selLinkFrom = d.selLinkTo = 0;
	SetSelected(id, true);
	EnsureVisible(id);
}

BOOL CGanttCtrl::GetSelectedLink(UINT* from, UINT* to) const {
	auto& d = *m_Data;
	if (!d.selLinkFrom)
		return FALSE;
	if (from)
		*from = d.selLinkFrom;
	if (to)
		*to = d.selLinkTo;
	return TRUE;
}

bool CGanttCtrl::KeyDown(UINT vk, bool alt, bool shift) {
	auto& d = *m_Data;
	const int task = d.selected ? d.project.Index(d.selected) : -1;
	auto it = std::find(d.rows.begin(), d.rows.end(), task);
	const int row = it == d.rows.end() ? -1 : (int)(it - d.rows.begin());
	const int page = std::max(1, (int)(d.chart.bottom - d.chart.top) / d.m.rowHeight - 1);
	auto t = task >= 0 ? d.project.Find(d.selected) : nullptr;
	switch (vk) {
		case VK_UP: SelectRow(row < 0 ? 0 : row - 1); return true;
		case VK_DOWN: SelectRow(row + 1); return true;
		case VK_HOME: SelectRow(0); return true;
		case VK_END: SelectRow((int)d.rows.size() - 1); return true;
		case VK_PRIOR: SelectRow(row - page); return true;
		case VK_NEXT: SelectRow(row + page); return true;
		case VK_LEFT:
		case VK_RIGHT: {
			const int dir = vk == VK_LEFT ? -1 : 1;
			if (alt) {
				Nudge(shift, dir);
				return true;
			}
			if (!t)
				return true;
			if (t->summary && t->expanded == (dir < 0)) {
				// collapsed or expanded
				t->expanded = dir > 0;
				Changed();
				NotifyTask(GNTN_EXPANDED, d.selected);
			}
			else if (dir < 0 && t->parent)
				SelectRow((int)(std::find(d.rows.begin(), d.rows.end(), d.project.Index(t->parent)) - d.rows.begin()));
			else if (dir > 0 && t->summary)
				SelectRow(row + 1);   // its first child
			return true;
		}
		case VK_F2:
			if (d.selected)
				EditName(d.selected);
			return true;
		case VK_RETURN:
			if (d.selected)
				NotifyTask(GNTN_DBLCLK, d.selected);
			return true;
		case VK_DELETE:
			RemoveSelectedLink();
			return true;
	}
	return false;
}

// ---- Moving a task a unit ----------------------------------------------------------------------------

void CGanttCtrl::Nudge(bool finish, int dir) {
	auto& d = *m_Data;
	auto t = d.selected ? d.project.Find(d.selected) : nullptr;
	if (!t || t->summary || (GetStyle() & GNTS_READONLY) || d.drag != GntDrag::None)
		return;
	const auto& cal = d.project.calendar;
	const bool hours = d.lower == GntUnit::Hour;
	d.before = d.project.Save();
	if (finish) {
		if (t->Milestone())
			return;
		const int64_t unit = hours ? 60 : std::max(1, cal.MinutesPerDay());
		t->duration = std::max<int64_t>(unit, t->duration + dir * unit);
	}
	else if (hours) {
		// an hour, onto working time the way it went
		GntTime s = t->start + dir * 60;
		for (int k = 0; k < 10000 && cal.NextWorkStart(s) != s; k++)
			s = dir > 0 ? cal.NextWorkStart(s) : s - 60;
		t->start = s;
	}
	else {
		// a working day, at the same time of day
		int64_t day = GntDayOf(t->start) + dir;
		for (int k = 0; k < 3700 && !cal.IsWorkingDay(day); k++)
			day += dir;
		t->start = cal.NextWorkStart(GntTimeOf(day, (int)(t->start - GntDayOf(t->start) * GntDay)));
	}
	d.project.Recompute();
	Apply(d.selected, finish ? GNTC_FINISH : GNTC_MOVE);
	EnsureVisible(d.selected);
}

// ---- Links -------------------------------------------------------------------------------------------

std::vector<POINT> CGanttCtrl::LinkRoute(int link, bool* shown) {
	auto& d = *m_Data;
	*shown = false;
	auto& l = d.project.Links()[link];
	const int a = d.project.Index(l.from), b = d.project.Index(l.to);
	auto ra = std::find(d.rows.begin(), d.rows.end(), a), rb = std::find(d.rows.begin(), d.rows.end(), b);
	if (a < 0 || b < 0 || ra == d.rows.end() || rb == d.rows.end())
		return {};
	auto bar = [&](int task, int row) {
		auto g = GntLayoutBar(d.project.Tasks()[task], row, d.scale, d.m);
		::OffsetRect(&g.rc, d.chart.left, d.chart.top - d.scrollY);
		return g;
	};
	*shown = true;
	return GntRouteLink(bar(a, (int)(ra - d.rows.begin())), bar(b, (int)(rb - d.rows.begin())), l.type, d.m);
}

int CGanttCtrl::LinkAt(POINT pt) {
	auto& d = *m_Data;
	if (!::PtInRect(&d.chart, pt))
		return -1;
	for (int i = 0; i < (int)d.project.Links().size(); i++) {
		bool shown;
		auto route = LinkRoute(i, &shown);
		if (shown && GntLinkDistance(route, pt) <= Scale(3))
			return i;
	}
	return -1;
}

void CGanttCtrl::RemoveSelectedLink() {
	auto& d = *m_Data;
	if (!d.selLinkFrom || (GetStyle() & GNTS_READONLY))
		return;
	auto& links = d.project.Links();
	auto it = std::find_if(links.begin(), links.end(), [&](auto& l) { return l.from == d.selLinkFrom && l.to == d.selLinkTo; });
	if (it == links.end())
		return;
	NMGANTTLINK nm{};
	nm.from = it->from;
	nm.to = it->to;
	nm.type = (UINT)it->type;
	nm.lag = it->lag;
	if (FireNotify(&nm.hdr, GNTN_LINKREMOVING))
		return;   // vetoed
	d.before = d.project.Save();
	d.project.RemoveLink(nm.from, nm.to);
	d.history.Push(d.before, d.project.Save());
	d.selLinkFrom = d.selLinkTo = 0;
	d.hotLink = -1;
	Changed();
	FireNotify(&nm.hdr, GNTN_LINKREMOVED);
}

// ---- Renaming ----------------------------------------------------------------------------------------

BOOL CGanttCtrl::EditName(UINT id) {
	auto& d = *m_Data;
	if ((GetStyle() & GNTS_READONLY) || !m_Edit.IsWindow() || !d.project.Find(id))
		return FALSE;
	EndRename(true);
	EnsureVisible(id);
	const int task = d.project.Index(id);
	auto it = std::find(d.rows.begin(), d.rows.end(), task);
	if (it == d.rows.end())
		return FALSE;
	// the name's cell, after the expander
	const int row = (int)(it - d.rows.begin()), y = d.grid.top + row * d.m.rowHeight - d.scrollY;
	auto& t = d.project.Tasks()[task];
	int x = d.grid.left;
	RECT cell{};
	for (auto& col : d.columns) {
		const int w = Scale(col.dips);
		if (col.id == GNTCOL_NAME) {
			cell = { x + Scale(2) + (t.depth + 1) * d.indent, y + 1, std::min(x + w, (int)d.grid.right) - Scale(2), y + d.m.rowHeight - 1 };
			break;
		}
		x += w;
	}
	if (::IsRectEmpty(&cell))
		return FALSE;   // no name column
	d.renaming = id;
	m_Edit.SetFont(t.summary ? m_hBold : m_hFont);
	m_Edit.SetWindowText(t.name.c_str());
	m_Edit.SetWindowPos(HWND_TOP, &cell, SWP_SHOWWINDOW);
	m_Edit.SetSel(0, -1);
	m_Edit.SetFocus();
	return TRUE;
}

void CGanttCtrl::EndRename(bool commit) {
	auto& d = *m_Data;
	if (!d.renaming)
		return;
	const UINT id = d.renaming;
	d.renaming = 0;
	CString text;
	m_Edit.GetWindowText(text);
	if (::GetFocus() == m_Edit.m_hWnd)
		SetFocus();
	m_Edit.ShowWindow(SW_HIDE);
	auto t = d.project.Find(id);
	if (!commit || !t || t->name == (LPCWSTR)text)
		return;
	NMGANTTRENAME nm{};
	nm.id = id;
	nm.pszName = text;
	nm.lParam = t->lParam;
	if (FireNotify(&nm.hdr, GNTN_RENAMING))
		return;   // vetoed
	d.before = d.project.Save();
	if (auto r = d.project.Find(id))
		r->name = (LPCWSTR)text;
	d.history.Push(d.before, d.project.Save());
	Invalidate(FALSE);
	NotifyAccessible(EVENT_OBJECT_NAMECHANGE, AccChildId(id));
	FireNotify(&nm.hdr, GNTN_RENAMED);
}

LRESULT CGanttCtrl::OnEditKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
		EndRename(wParam == VK_RETURN);
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CGanttCtrl::OnEditChar(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == L'\r' || wParam == 27 || wParam == L'\n')
		return 0;   // handled as keys: no beep
	bHandled = FALSE;
	return 0;
}

LRESULT CGanttCtrl::OnEditGetDlgCode(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	return m_Edit.DefWindowProc(uMsg, wParam, lParam) | DLGC_WANTALLKEYS;
}

LRESULT CGanttCtrl::OnEditKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	EndRename(true);   // the focus went elsewhere: as a list view does, the name typed stays
	bHandled = FALSE;
	return 0;
}

// ---- Tooltips ----------------------------------------------------------------------------------------

void CGanttCtrl::UpdateTip() {
	auto& d = *m_Data;
	if (!m_hTip)
		return;
	std::wstring text;
	RECT rc{};
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (d.drag == GntDrag::None && !d.renaming) {
		if (int m = MarkerAt(pt); m >= 0) {
			auto& marker = d.markers[m];
			text = marker.label + L"\n" + DateText(marker.time, L"ddd MMM d, yyyy");
			rc = { pt.x - Scale(4), d.chart.top, pt.x + Scale(4), d.chart.bottom };
		}
		else if (d.hot.area == GntArea::Chart && d.hot.task >= 0 && d.hot.part != GntPart::None && d.hot.part != GntPart::Link) {
			// a bar: its dates, its work, how much is done
			auto& t = d.project.Tasks()[d.hot.task];
			GntTime f = t.finish;
			if (f > t.start && f % GntDay == 0)
				f--;
			text = t.name + L"\n" + DateText(t.start, L"ddd MMM d");
			if (!t.Milestone())
				text += L" \x2013 " + DateText(f, L"ddd MMM d") + L" (" + CellText(d.hot.task, GNTCOL_DURATION) + L")\n"
					+ std::to_wstring(t.progress) + L"% done";
			auto bar = GntLayoutBar(t, d.hot.row, d.scale, d.m);
			rc = bar.rc;
			::OffsetRect(&rc, d.chart.left, d.chart.top - d.scrollY);
		}
		else if (d.hotLink >= 0 && d.hotLink < (int)d.project.Links().size()) {
			auto& l = d.project.Links()[d.hotLink];
			auto a = d.project.Find(l.from), b = d.project.Find(l.to);
			if (a && b) {
				text = a->name + L" \x2192 " + b->name + L"\n" + LinkKinds[(int)l.type];
				if (l.lag) {
					wchar_t lag[32];
					const int day = std::max(1, d.project.calendar.MinutesPerDay());
					swprintf_s(lag, L", %+g d", std::round(l.lag * 10.0 / day) / 10.0);
					text += lag;
				}
				rc = { pt.x - Scale(4), pt.y - Scale(4), pt.x + Scale(4), pt.y + Scale(4) };
			}
		}
	}
	if (text == d.tipText && ::EqualRect(&rc, &d.tipRect))
		return;
	d.tipText = text;
	d.tipRect = rc;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	ti.rect = rc;
	::SendMessage(m_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	::SendMessage(m_hTip, TTM_POP, 0, 0);
	if (!text.empty()) {
		// the mouse move that changed it reached the tooltip before the tool moved there
		MSG msg{ m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y) };
		::SendMessage(m_hTip, TTM_RELAYEVENT, 0, (LPARAM)&msg);
	}
}

LRESULT CGanttCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom == m_hTip && hdr->code == TTN_GETDISPINFOW) {
		((NMTTDISPINFOW*)hdr)->lpszText = m_Data->tipText.data();
		return 0;
	}
	bHandled = FALSE;
	return 0;
}
