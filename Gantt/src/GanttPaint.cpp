#include "GanttData.h"

#include <algorithm>
#include <cmath>

using namespace GntDraw;

// ---- Helpers ----------------------------------------------------------------------------------------

void GntDraw::RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
	WTL::CBrush brush;
	brush.CreateSolidBrush(fill);
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, 1, border);
	auto oldBrush = dc.SelectBrush(brush);
	auto oldPen = dc.SelectPen(pen);
	dc.RoundRect(&rc, { radius, radius });
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

void GntDraw::Polygon(WTL::CDCHandle dc, const POINT* points, int count, COLORREF color) {
	WTL::CBrush brush;
	brush.CreateSolidBrush(color);
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, 1, color);
	auto oldBrush = dc.SelectBrush(brush);
	auto oldPen = dc.SelectPen(pen);
	dc.Polygon(points, count);
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

namespace {
	GntTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return GntFromSystemTime(st);
	}

	std::wstring Date(const SYSTEMTIME& st, LPCWSTR picture, DWORD flags = 0) {
		wchar_t text[80];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}

	// a chevron centered on (cx, cy): right, or down
	void Chevron(WTL::CDCHandle dc, int cx, int cy, int size, bool down, COLORREF color) {
		const int h = size / 2, q = size / 4;
		POINT pts[3];
		if (down) {
			pts[0] = { cx - h, cy - q };
			pts[1] = { cx, cy + q };
			pts[2] = { cx + h, cy - q };
		}
		else {
			pts[0] = { cx - q, cy - h };
			pts[1] = { cx + q, cy };
			pts[2] = { cx - q, cy + h };
		}
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, 1, color);
		auto old = dc.SelectPen(pen);
		dc.Polyline(pts, 3);
		dc.SelectPen(old);
	}
}

// ---- Colors and texts ---------------------------------------------------------------------------------

GNTCOLORS CGanttCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	GNTCOLORS c;
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	const bool dark = IsDark(window);
	const COLORREF accent = dark ? RGB(0x4C, 0xA0, 0xE8) : RGB(0x2B, 0x7C, 0xD3);
	c.crBack = window;
	c.crText = text;
	c.crHeaderBack = Blend(window, text, dark ? 8 : 4);
	c.crHeaderText = text;
	c.crGrid = Blend(window, text, dark ? 16 : 12);
	c.crNonWork = Blend(window, text, dark ? 6 : 5);
	c.crSelBack = Blend(window, highlight, dark ? 30 : 16);
	c.crBar = Blend(window, accent, dark ? 55 : 42);
	c.crBarDone = accent;
	c.crBarBorder = Blend(accent, text, dark ? 10 : 20);
	c.crSummary = Blend(window, text, dark ? 72 : 78);
	c.crMilestone = Blend(accent, text, 35);
	c.crLink = Blend(window, text, 55);
	c.crToday = dark ? RGB(0xFF, 0x6B, 0x6B) : RGB(0xD1, 0x34, 0x38);
	return c;
}

std::wstring CGanttCtrl::CellText(int task, UINT column) const {
	auto& d = *m_Data;
	auto& t = d.project.Tasks()[task];
	wchar_t text[64];
	switch (column) {
		case GNTCOL_NAME:
			return t.name;
		case GNTCOL_START:
			return Date(GntToSystemTime(t.start), nullptr, DATE_SHORTDATE);
		case GNTCOL_FINISH: {
			// a finish at midnight is the day before's end
			GntTime f = t.finish;
			if (f > t.start && f % GntDay == 0)
				f--;
			return Date(GntToSystemTime(f), nullptr, DATE_SHORTDATE);
		}
		case GNTCOL_DURATION: {
			const int day = std::max(1, d.project.calendar.MinutesPerDay());
			if (t.duration % day == 0)
				swprintf_s(text, L"%lld d", t.duration / day);
			else if (t.duration < day)
				swprintf_s(text, L"%g h", std::round(t.duration / 6.0) / 10.0);
			else
				swprintf_s(text, L"%g d", std::round(t.duration * 10.0 / day) / 10.0);
			return text;
		}
		case GNTCOL_PROGRESS:
			swprintf_s(text, L"%d%%", t.progress);
			return text;
	}
	return {};
}

std::wstring CGanttCtrl::UnitLabel(int unit, LONGLONG start, int width) const {
	auto st = GntToSystemTime(start);
	switch ((GntUnit)unit) {
		case GntUnit::Hour: {
			wchar_t text[40];
			if (!::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOMINUTESORSECONDS, &st, nullptr, text, _countof(text)))
				return {};
			return text;
		}
		case GntUnit::Day: return Date(st, width >= Scale(70) ? L"ddd d" : L"d");
		case GntUnit::Week: return Date(st, width >= Scale(130) ? L"MMM d, yyyy" : L"MMM d");
		case GntUnit::Month: {
			if (width >= Scale(110))
				return Date(st, L"MMMM yyyy");
			auto name = Date(st, L"MMM");
			return width >= Scale(36) ? name : name.substr(0, 1);
		}
		case GntUnit::Quarter: {
			wchar_t text[40];
			if (width >= Scale(70))
				swprintf_s(text, L"Q%d %d", (st.wMonth - 1) / 3 + 1, st.wYear);
			else
				swprintf_s(text, L"Q%d", (st.wMonth - 1) / 3 + 1);
			return text;
		}
		default: return Date(st, L"yyyy");
	}
}

// ---- Painting ---------------------------------------------------------------------------------------

LRESULT CGanttCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		Paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC);
	return 0;
}

void CGanttCtrl::Paint(WTL::CDCHandle dc) {
	auto& d = *m_Data;
	auto c = GetColors();
	RECT rc;
	GetClientRect(&rc);
	FillSolid(dc, rc, c.crBack);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);
	PaintChart(dc, c);
	PaintGrid(dc, c);
	PaintHeader(dc, c);
	FillSolid(dc, d.splitter, c.crHeaderBack);
	const int x = (d.splitter.left + d.splitter.right) / 2;
	FillSolid(dc, { x, d.splitter.top, x + 1, d.splitter.bottom }, c.crGrid);
	dc.SelectFont(oldFont);
}

void CGanttCtrl::PaintHeader(WTL::CDCHandle dc, const GNTCOLORS& c) {
	auto& d = *m_Data;
	static const wchar_t* const Titles[] = { L"Task name", L"Start", L"Finish", L"Duration", L"% Done" };
	const int pad = Scale(6);

	// the grid's: the columns' titles
	FillSolid(dc, d.gridHeader, c.crHeaderBack);
	dc.SetTextColor(c.crHeaderText);
	int x = d.gridHeader.left;
	for (auto& col : d.columns) {
		const int w = Scale(col.dips);
		RECT r{ x + pad, d.gridHeader.top, x + w - pad, d.gridHeader.bottom };
		dc.DrawText(Titles[col.id], -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		x += w;
		FillSolid(dc, { x - 1, d.gridHeader.top + Scale(4), x, d.gridHeader.bottom - Scale(4) }, c.crGrid);
		if (x >= d.gridHeader.right)
			break;
	}
	FillSolid(dc, { d.gridHeader.left, d.gridHeader.bottom - 1, d.gridHeader.right, d.gridHeader.bottom }, c.crGrid);

	// the chart's: two tiers of the scale's units
	const RECT& h = d.chartHeader;
	FillSolid(dc, h, c.crHeaderBack);
	const int saved = dc.SaveDC();
	dc.IntersectClipRect(&h);
	const GntTime from = d.scale.origin, to = d.scale.T(h.right - h.left);
	auto tier = [&](GntUnit unit, int top, bool upperTier) {
		for (auto& cell : GntCells(unit, from, to + 1)) {
			const int x1 = h.left + (int)std::lround(d.scale.X(cell.start)), x2 = h.left + (int)std::lround(d.scale.X(cell.end));
			FillSolid(dc, { x1, top, x1 + 1, top + d.tier }, c.crGrid);
			auto label = UnitLabel((int)unit, cell.start, x2 - x1);
			// an upper cell's label stays in view while the cell does (not squeezed into its last pixels)
			RECT r{ upperTier ? std::max(x1, (int)h.left) + pad : x1, top, upperTier ? x2 - pad : x2, top + d.tier };
			if (upperTier && r.right - r.left < Scale(40))
				continue;
			dc.DrawText(label.c_str(), (int)label.size(), &r,
				(upperTier ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
	};
	if (d.upper != d.lower) {
		tier(d.upper, h.top, true);
		FillSolid(dc, { h.left, h.top + d.tier - 1, h.right, h.top + d.tier }, c.crGrid);
		tier(d.lower, h.top + d.tier, false);
	}
	else
		tier(d.lower, h.top, true);
	FillSolid(dc, { h.left, h.bottom - 1, h.right, h.bottom }, c.crGrid);
	dc.RestoreDC(saved);
}

void CGanttCtrl::PaintGrid(WTL::CDCHandle dc, const GNTCOLORS& c) {
	auto& d = *m_Data;
	const RECT& g = d.grid;
	const int saved = dc.SaveDC();
	dc.IntersectClipRect(&g);
	const int rh = d.m.rowHeight, pad = Scale(6);
	const int first = d.scrollY / rh, last = std::min((int)d.rows.size(), (d.scrollY + (int)(g.bottom - g.top)) / rh + 1);
	for (int r = first; r < last; r++) {
		const int task = d.rows[r];
		auto& t = d.project.Tasks()[task];
		const int y = g.top + r * rh - d.scrollY;
		if (t.id == d.selected)
			FillSolid(dc, { g.left, y, g.right, y + rh }, c.crSelBack);
		int x = g.left;
		for (auto& col : d.columns) {
			const int w = Scale(col.dips);
			RECT cell{ x + pad, y, x + w - pad, y + rh };
			if (col.id == GNTCOL_NAME) {
				// indented by its depth, a summary's expander before it
				const int ex = x + Scale(2) + t.depth * d.indent;
				if (t.summary)
					Chevron(dc, ex + d.indent / 2, y + rh / 2, Scale(8), t.expanded, c.crText);
				cell.left = ex + d.indent;
			}
			dc.SelectFont(t.summary && col.id == GNTCOL_NAME ? m_hBold : m_hFont);
			dc.SetTextColor(c.crText);
			auto text = CellText(task, col.id);
			const UINT align = col.id == GNTCOL_DURATION || col.id == GNTCOL_PROGRESS ? DT_RIGHT : DT_LEFT;
			dc.DrawText(text.c_str(), (int)text.size(), &cell, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			x += w;
			FillSolid(dc, { x - 1, y, x, y + rh }, Blend(c.crBack, c.crGrid, 50));
		}
		FillSolid(dc, { g.left, y + rh - 1, g.right, y + rh }, Blend(c.crBack, c.crGrid, 60));
	}
	dc.SelectFont(m_hFont);
	dc.RestoreDC(saved);
}

void CGanttCtrl::PaintChart(WTL::CDCHandle dc, const GNTCOLORS& c) {
	auto& d = *m_Data;
	const RECT& ch = d.chart;
	const int saved = dc.SaveDC();
	dc.IntersectClipRect(&ch);
	const auto& cal = d.project.calendar;
	const GntTime from = d.scale.origin, to = d.scale.T(ch.right - ch.left);
	auto X = [&](GntTime t) {
		return ch.left + (int)std::lround(d.scale.X(t));
	};

	// non-working time: the days off, and (by the hour) the hours off
	if (GntDay / d.scale.minutesPerPixel >= Scale(5))   // narrower: stripes
		for (int64_t day = GntDayOf(from), last = GntDayOf(to); day <= last; day++) {
			const GntTime t = GntTimeOf(day);
			if (!cal.IsWorkingDay(day))
				FillSolid(dc, { X(t), ch.top, X(t + GntDay), ch.bottom }, c.crNonWork);
			else if (d.lower == GntUnit::Hour) {
				FillSolid(dc, { X(t), ch.top, X(t + cal.dayStart), ch.bottom }, c.crNonWork);
				FillSolid(dc, { X(t + cal.dayEnd), ch.top, X(t + GntDay), ch.bottom }, c.crNonWork);
			}
		}
	// the lower tier's cells
	const COLORREF line = Blend(c.crBack, c.crGrid, 60);
	for (auto& cell : GntCells(d.lower, from, to + 1))
		if (X(cell.end) - X(cell.start) >= Scale(10))
			FillSolid(dc, { X(cell.start), ch.top, X(cell.start) + 1, ch.bottom }, line);

	const int rh = d.m.rowHeight;
	const int first = d.scrollY / rh, last = std::min((int)d.rows.size(), (d.scrollY + (int)(ch.bottom - ch.top)) / rh + 1);
	auto bar = [&](int row) {
		auto b = GntLayoutBar(d.project.Tasks()[d.rows[row]], row, d.scale, d.m);
		const int dy = ch.top - d.scrollY;
		::OffsetRect(&b.rc, ch.left, dy);
		::OffsetRect(&b.done, ch.left, dy);
		return b;
	};
	for (int r = first; r < last; r++)
		if (d.project.Tasks()[d.rows[r]].id == d.selected) {
			const int y = ch.top + r * rh - d.scrollY;
			FillSolid(dc, { ch.left, y, ch.right, y + rh }, c.crSelBack);
		}

	if (!(GetStyle() & GNTS_NOTODAY)) {
		const int x = X(Now());
		FillSolid(dc, { x, ch.top, x + std::max(1, Scale(2)), ch.bottom }, c.crToday);
	}

	// the links, under the bars
	{
		std::vector<int> rowOf(d.project.Tasks().size(), -1);
		for (int r = 0; r < (int)d.rows.size(); r++)
			rowOf[d.rows[r]] = r;
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, Scale(1)), c.crLink);
		auto oldPen = dc.SelectPen(pen);
		const int arrow = Scale(4);
		WTL::CPen hotPen, selPen;
		hotPen.CreatePen(PS_SOLID, std::max(1, Scale(1)), c.crText);
		selPen.CreatePen(PS_SOLID, std::max(2, Scale(2)), c.crBarDone);
		for (int i = 0; i < (int)d.project.Links().size(); i++) {
			auto& l = d.project.Links()[i];
			const int a = d.project.Index(l.from), b = d.project.Index(l.to);
			if (a < 0 || b < 0 || rowOf[a] < 0 || rowOf[b] < 0)
				continue;
			const int ra = rowOf[a], rb = rowOf[b];
			if (std::max(ra, rb) < first || std::min(ra, rb) >= last)
				continue;
			auto points = GntRouteLink(bar(ra), bar(rb), l.type, d.m);
			// the line stops at the arrow's base; the arrow points along the last step
			POINT end = points.back(), before = points[points.size() - 2];
			const int dx = end.x > before.x ? 1 : end.x < before.x ? -1 : 0, dy = end.y > before.y ? 1 : end.y < before.y ? -1 : 0;
			points.back() = { end.x - dx * arrow, end.y - dy * arrow };
			// the link clicked (Delete removes it) and the one under the mouse stand out
			const bool selected = l.from == d.selLinkFrom && l.to == d.selLinkTo, hot = i == d.hotLink;
			if (selected || hot)
				dc.SelectPen(selected ? selPen : hotPen);
			dc.Polyline(points.data(), (int)points.size());
			if (selected || hot)
				dc.SelectPen(pen);
			POINT head[3] = { end, { end.x - dx * arrow - dy * arrow, end.y - dy * arrow - dx * arrow },
				{ end.x - dx * arrow + dy * arrow, end.y - dy * arrow + dx * arrow } };
			Polygon(dc, head, 3, selected ? c.crBarDone : hot ? c.crText : c.crLink);
		}
		dc.SelectPen(oldPen);
	}

	// the bars, with their names after them
	dc.SetTextColor(c.crText);
	for (int r = first; r < last; r++) {
		auto& t = d.project.Tasks()[d.rows[r]];
		auto b = bar(r);
		if (b.rc.right < ch.left - Scale(400) || b.rc.left > ch.right)
			continue;
		const bool hot = d.hot.area == GntArea::Chart && d.hot.task == d.rows[r] && d.hot.part != GntPart::None;
		if (b.milestone) {
			const int cx = (b.rc.left + b.rc.right) / 2, cy = (b.rc.top + b.rc.bottom) / 2, h = (b.rc.right - b.rc.left) / 2;
			POINT diamond[4] = { { cx, cy - h }, { cx + h, cy }, { cx, cy + h }, { cx - h, cy } };
			Polygon(dc, diamond, 4, hot ? c.crText : c.crMilestone);
		}
		else if (b.summary) {
			FillSolid(dc, b.rc, c.crSummary);
			// the ends point down at the children
			const int cap = Scale(5);
			POINT left[3] = { { b.rc.left, b.rc.bottom }, { b.rc.left + cap, b.rc.bottom }, { b.rc.left, b.rc.bottom + cap } };
			POINT right[3] = { { b.rc.right, b.rc.bottom }, { b.rc.right - cap, b.rc.bottom }, { b.rc.right, b.rc.bottom + cap } };
			Polygon(dc, left, 3, c.crSummary);
			Polygon(dc, right, 3, c.crSummary);
		}
		else {
			const bool own = t.color != CLR_INVALID;
			const COLORREF fill = own ? t.color : c.crBar;
			RoundFill(dc, b.rc, fill, hot ? c.crText : own ? Blend(t.color, c.crText, 30) : c.crBarBorder, Scale(4));
			if (b.done.right > b.rc.left + 1) {
				RECT done{ b.rc.left + 1, b.rc.top + 1, std::min(b.done.right, b.rc.right - 1), b.rc.bottom - 1 };
				FillSolid(dc, done, own ? Blend(t.color, c.crText, 35) : c.crBarDone);
			}
		}
		RECT label{ b.rc.right + Scale(6), b.rc.top - rh, ch.right + Scale(400), b.rc.bottom + rh };
		// the link handle on the bar under the mouse: drag from it to another bar
		if (d.hot.area == GntArea::Chart && d.hot.task == d.rows[r] && !b.summary && !(GetStyle() & GNTS_READONLY)
			&& d.drag == GntDrag::None) {
			RECT handle = GntLinkHandle(b, d.m);
			RoundFill(dc, handle, d.hot.part == GntPart::Link ? c.crBarDone : c.crBack, c.crBarBorder, (int)(handle.right - handle.left));
			label.left = handle.right + Scale(4);
		}
		dc.SelectFont(t.summary ? m_hBold : m_hFont);
		dc.DrawText(t.name.c_str(), (int)t.name.size(), &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	dc.SelectFont(m_hFont);
	PaintMarkers(dc, c);
	PaintEditing(dc, c);
	dc.RestoreDC(saved);
}
