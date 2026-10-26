#include "CalendarViewData.h"

#include <algorithm>

using namespace ClvDraw;

bool ClvDraw::HighContrast = false;

// ---- Helpers ----------------------------------------------------------------------------------------

void ClvDraw::RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
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

namespace {
	// the custom draw notification's answer, with the DC as it was afterwards, clipped to the item meanwhile
	LRESULT CustomDraw(const CCalendarViewCtrl* ctrl, NMCLVCUSTOMDRAW& cd, DWORD stage) {
		cd.nmcd.dwDrawStage = stage;
		cd.nmcd.hdr.hwndFrom = ctrl->m_hWnd;
		cd.nmcd.hdr.idFrom = ::GetDlgCtrlID(ctrl->m_hWnd);
		cd.nmcd.hdr.code = CLVN_CUSTOMDRAW;
		const int saved = ::SaveDC(cd.nmcd.hdc);
		::IntersectClipRect(cd.nmcd.hdc, cd.nmcd.rc.left, cd.nmcd.rc.top, cd.nmcd.rc.right, cd.nmcd.rc.bottom);
		const LRESULT result = ::SendMessage(ctrl->GetParent(), WM_NOTIFY, cd.nmcd.hdr.idFrom, (LPARAM)&cd);
		::RestoreDC(cd.nmcd.hdc, saved);
		return result;
	}

	ClvTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return ClvFromSystemTime(st);
	}

	std::wstring Date(const SYSTEMTIME& st, LPCWSTR picture) {
		wchar_t text[80];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}

	std::wstring Time(const SYSTEMTIME& st, DWORD flags) {
		wchar_t text[40];
		if (!::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &st, nullptr, text, _countof(text)))
			return {};
		return text;
	}

	// an hour's label in the ruler: "9 AM", or "09:00" where the clock has 24 hours
	std::wstring HourLabel(int hour) {
		SYSTEMTIME st{};
		st.wYear = 2000;
		st.wMonth = st.wDay = 1;
		st.wHour = (WORD)hour;
		auto text = Time(st, TIME_NOMINUTESORSECONDS);
		if (std::none_of(text.begin(), text.end(), [](wchar_t ch) { return ::IsCharAlphaW(ch); }))
			text = Time(st, TIME_NOSECONDS);
		return text;
	}

	// a chevron centered on (cx, cy), up or down
	void Chevron(WTL::CDCHandle dc, int cx, int cy, int size, bool down, COLORREF color) {
		const int h = size / 2, q = size / 4;
		POINT pts[3] = { { cx - h, cy + (down ? -q : q) }, { cx, cy + (down ? q : -q) }, { cx + h + 1, cy + (down ? -q - 1 : q + 1) } };
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, size / 6), color);
		auto old = dc.SelectPen(pen);
		dc.Polyline(pts, 3);
		dc.SelectPen(old);
	}

	// the time off: shaded, or in high contrast hatched in the gray text color over the background
	void FillOff(WTL::CDCHandle dc, const RECT& rc, const CLVCOLORS& c) {
		FillSolid(dc, rc, c.crNonWork);
		if (!HighContrast || rc.bottom <= rc.top)
			return;
		WTL::CBrush hatch;
		hatch.CreateHatchBrush(HS_BDIAGONAL, ::GetSysColor(COLOR_GRAYTEXT));
		const int mode = dc.SetBkMode(TRANSPARENT);
		dc.FillRect(&rc, hatch);
		dc.SetBkMode(mode);
	}

	void HLine(WTL::CDCHandle dc, int x1, int x2, int y, COLORREF color) {
		RECT rc{ x1, y, x2, y + 1 };
		FillSolid(dc, rc, color);
	}

	void VLine(WTL::CDCHandle dc, int x, int y1, int y2, COLORREF color) {
		RECT rc{ x, y1, x + 1, y2 };
		FillSolid(dc, rc, color);
	}
}

// ---- Colors and texts ---------------------------------------------------------------------------------

CLVCOLORS CCalendarViewCtrl::GetColors() const {
	if (IsHighContrast()) {
		// the system's colors, over the app's: the theme's pairs, nothing between them
		CLVCOLORS c;
		c.crBack = c.crNonWork = c.crItem = ::GetSysColor(COLOR_WINDOW);
		c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
		c.crHeaderBack = ::GetSysColor(COLOR_BTNFACE);
		c.crHeaderText = ::GetSysColor(COLOR_BTNTEXT);
		c.crGrid = ::GetSysColor(COLOR_GRAYTEXT);
		c.crToday = ::GetSysColor(COLOR_HOTLIGHT);
		c.crSelection = ::GetSysColor(COLOR_HIGHLIGHT);
		return c;
	}
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	const COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	const bool dark = IsDark(window);
	const COLORREF accent = dark ? RGB(0x4C, 0xA0, 0xE8) : RGB(0x2B, 0x7C, 0xD3);
	CLVCOLORS c;
	c.crBack = window;
	c.crText = text;
	c.crHeaderBack = Blend(window, text, dark ? 7 : 4);
	c.crHeaderText = text;
	c.crGrid = Blend(window, text, dark ? 20 : 15);
	c.crNonWork = dark ? Blend(window, RGB(0, 0, 0), 30) : Blend(window, text, 5);
	c.crToday = dark ? RGB(0xFF, 0x6B, 0x6B) : RGB(0xD1, 0x34, 0x38);
	c.crItem = accent;
	c.crSelection = Blend(window, highlight, dark ? 35 : 22);
	return c;
}

void CCalendarViewCtrl::ForceHighContrast(int on) {
	m_ForceHighContrast = on < 0 ? -1 : on ? 1 : 0;
	if (IsWindow()) {
		UpdateScrollBarTheme();
		RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

bool CCalendarViewCtrl::IsHighContrast() const {
	if (m_ForceHighContrast >= 0)
		return m_ForceHighContrast != 0;
	HIGHCONTRASTW hc{ sizeof(hc) };
	return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

std::wstring CCalendarViewCtrl::DayName(LONGLONG day, int width, WTL::CDCHandle dc) const {
	auto st = ClvToSystemTime(day * ClvDay);
	std::wstring text;
	for (LPCWSTR picture : { L"dddd, MMMM d", L"dddd d", L"ddd d", L"d" }) {
		text = Date(st, picture);
		SIZE size{};
		dc.GetTextExtent(text.c_str(), (int)text.size(), &size);
		if (size.cx <= width)
			break;
	}
	return text;
}

int CCalendarViewCtrl::RulerWidth(WTL::CDCHandle dc) const {
	int width = 0;
	for (int hour = 0; hour < 24; hour++) {
		auto label = HourLabel(hour);
		SIZE size{};
		dc.GetTextExtent(label.c_str(), (int)label.size(), &size);
		width = std::max(width, (int)size.cx);
	}
	return width + Scale(6) + Scale(8);   // the labels end 6 before the days, and start 8 in
}

std::wstring CCalendarViewCtrl::TimeText(LONGLONG start, LONGLONG end, bool allDay) const {
	if (allDay)
		return {};
	auto text = Time(ClvToSystemTime(start), TIME_NOSECONDS);
	if (end > start)
		text += L" \x2013 " + Time(ClvToSystemTime(end), TIME_NOSECONDS);
	return text;
}

bool CCalendarViewCtrl::Prepaint(WTL::CDCHandle dc, const RECT& rc) const {
	NMCLVCUSTOMDRAW cd{};
	cd.nmcd.hdc = dc;
	cd.nmcd.rc = rc;
	return (CustomDraw(this, cd, CDDS_PREPAINT) & CDRF_NOTIFYITEMDRAW) != 0;
}

// ---- The days' names and the strip ------------------------------------------------------------------

LRESULT CCalendarViewCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		Paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC);
	return 0;
}

void CCalendarViewCtrl::Paint(WTL::CDCHandle dc) {
	auto& d = *m_Data;
	HighContrast = IsHighContrast();
	const auto c = GetColors();
	const auto& cols = d.layout.columns;
	const int64_t today = ClvDayOf(Now());
	RECT rc;
	GetClientRect(&rc);
	FillSolid(dc, rc, c.crHeaderBack);
	PaintNavBar(dc, c);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	// the days' names
	const int pad = Scale(6);
	for (int i = 0; i < d.range.days; i++) {
		const int64_t day = d.range.first + i;
		RECT cell{ cols.Left(i), d.header.top, cols.Right(i), d.header.bottom };
		const bool isToday = day == today;
		dc.SelectFont(isToday ? m_hBold : m_hFont);
		auto name = DayName(day, cell.right - cell.left - 2 * pad, dc);
		RECT text{ cell.left + pad, cell.top, cell.right - pad, cell.bottom };
		dc.SetTextColor(isToday ? c.crToday : c.crHeaderText);
		dc.DrawText(name.c_str(), (int)name.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (isToday) {
			RECT bar{ cell.left + 1, cell.top, cell.right, cell.top + Scale(3) };
			FillSolid(dc, bar, c.crToday);
		}
		if (i > 0)
			VLine(dc, cell.left, cell.top + pad, cell.bottom, c.crGrid);
	}
	dc.SelectFont(m_hFont);

	// the strip: the days' backgrounds, the days selected, then the items over them
	RECT strip = d.strip;
	if (strip.bottom > strip.top) {
		const RECT area{ cols.left, strip.top, cols.left + cols.width, strip.bottom - 1 };
		FillSolid(dc, area, c.crBack);
		if (d.slots && d.slot.allDay) {
			for (int i = 0; i < d.range.days; i++) {
				const ClvTime t = d.range.Start() + (ClvTime)i * ClvDay;
				if (t >= d.slot.start && t < d.slot.end) {
					RECT sel{ cols.Left(i) + 1, area.top, cols.Right(i), area.bottom };
					FillSolid(dc, sel, c.crSelection);
				}
			}
		}
		for (int i = 0; i <= d.range.days; i++)
			VLine(dc, cols.Left(i), area.top, area.bottom, c.crGrid);
		HLine(dc, 0, rc.right, d.header.bottom - 1, c.crGrid);
		HLine(dc, 0, rc.right, strip.bottom - 1, c.crGrid);

		const bool notify = Prepaint(dc, rc);
		RECT rows{ area.left, strip.top + d.m.stripPad, area.right, strip.bottom - d.m.stripPad };
		dc.IntersectClipRect(&rows);
		int selected = -1;
		for (int i = 0; i < (int)d.layout.strip.size(); i++)
			if (d.layout.strip[i].id == d.selected)
				selected = i;
			else
				PaintItem(dc, c, i, true, notify);
		if (selected >= 0)
			PaintItem(dc, c, selected, true, notify);
		dc.SelectClipRgn(nullptr);

		// more rows than shown: chevrons in the ruler's corner where they are (the wheel scrolls them)
		if (d.layout.stripRows > d.m.stripMaxRows) {
			const int size = Scale(8), cx = cols.left / 2;
			const int view = d.layout.StripView(d.m) - 2 * d.m.stripPad;
			if (d.stripScroll > 0)
				Chevron(dc, cx, rows.top + d.m.stripRow / 2, size, false, c.crHeaderText);
			if (d.stripScroll + view < d.layout.StripHeight(d.m))
				Chevron(dc, cx, rows.bottom - d.m.stripRow / 2, size, true, c.crHeaderText);
		}
	}
	dc.SelectFont(oldFont);
}

// ---- The days' time -----------------------------------------------------------------------------------

LRESULT CCalendarViewCtrl::OnDaysPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		m_Days->GetClientRect(&rc);
		PaintDays((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_Days->m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	PaintDays(mdc.m_hDC, dc.m_ps.rcPaint);
	return 0;
}

void CCalendarViewCtrl::PaintDays(WTL::CDCHandle dc, const RECT& rcPaint) {
	auto& d = *m_Data;
	HighContrast = IsHighContrast();
	const auto c = GetColors();
	const auto& cols = d.layout.columns;
	const auto& axis = d.axis;
	RECT rc;
	m_Days->GetClientRect(&rc);
	const int top = d.scrollY;   // content y at the window's top
	auto Y = [&](int minute) { return axis.Y(minute) - top; };
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	// the ruler
	RECT ruler{ 0, rcPaint.top, cols.left, rcPaint.bottom };
	FillSolid(dc, ruler, c.crHeaderBack);
	RECT right{ cols.left + cols.width, rcPaint.top, std::max((int)rc.right, cols.left + cols.width), rcPaint.bottom };
	FillSolid(dc, right, c.crBack);

	// the days: their time off shaded, the slots selected
	for (int i = 0; i < d.range.days; i++) {
		const int64_t day = d.range.first + i;
		const int x1 = cols.Left(i), x2 = cols.Right(i);
		RECT all{ x1, Y(0), x2, Y((int)ClvDay) };
		if (!d.work.IsWorkingDay(day))
			FillOff(dc, all, c);
		else {
			FillSolid(dc, all, c.crBack);
			RECT before{ x1, Y(0), x2, Y(d.work.start) }, after{ x1, Y(d.work.end), x2, Y((int)ClvDay) };
			FillOff(dc, before, c);
			FillOff(dc, after, c);
		}
		if (d.slots && !d.slot.allDay) {
			const ClvTime dayStart = ClvTimeOf(day);
			const ClvTime s = std::max(d.slot.start, dayStart), e = std::min(d.slot.end, dayStart + ClvDay);
			if (s < e) {
				RECT sel{ x1 + 1, Y((int)(s - dayStart)), x2, Y((int)(e - dayStart)) };
				FillSolid(dc, sel, c.crSelection);
			}
		}
	}

	// the slots' and hours' lines, the hours' labels
	const COLORREF slotLine = Shade(c.crBack, c.crGrid, 45);
	const int first = std::max(0, axis.Minute(top + rcPaint.top) / axis.slotMinutes - 1);
	const int last = std::min((int)ClvDay / axis.slotMinutes, axis.Minute(top + rcPaint.bottom) / axis.slotMinutes + 1);
	dc.SetTextColor(c.crHeaderText);
	const int pad = Scale(6);
	for (int k = first; k <= last; k++) {
		const int minute = k * axis.slotMinutes;
		const int y = Y(minute);
		const bool hour = minute % 60 == 0;
		if (hour) {
			HLine(dc, cols.left - pad, cols.left + cols.width, y, c.crGrid);
			if (minute > 0 && minute < ClvDay) {
				auto label = HourLabel(minute / 60);
				RECT text{ 0, y + Scale(2), cols.left - pad, y + Scale(2) + axis.slotHeight * 2 };
				dc.DrawText(label.c_str(), (int)label.size(), &text, DT_SINGLELINE | DT_RIGHT | DT_TOP | DT_NOPREFIX);
			}
		}
		else
			HLine(dc, cols.left, cols.left + cols.width, y, slotLine);
	}
	for (int i = 0; i <= d.range.days; i++)
		VLine(dc, cols.Left(i), rcPaint.top, rcPaint.bottom, c.crGrid);

	// the items, the selected one on top
	const bool notify = Prepaint(dc, rc);
	int selected = -1;
	for (int i = 0; i < (int)d.layout.timed.size(); i++) {
		const auto& box = d.layout.timed[i];
		if (box.rc.bottom - top < rcPaint.top || box.rc.top - top > rcPaint.bottom)
			continue;
		if (box.id == d.selected)
			selected = i;
		else
			PaintItem(dc, c, i, false, notify);
	}
	if (selected >= 0)
		PaintItem(dc, c, selected, false, notify);

	// the line at now, across today
	const ClvTime now = Now();
	const int64_t today = ClvDayOf(now);
	if (!(GetStyle() & CLVS_NONOW) && d.range.Contains(today)) {
		const int i = (int)(today - d.range.first);
		const int y = Y(ClvMinuteOf(now));
		const int thick = std::max(2, Scale(2));
		RECT line{ cols.Left(i), y - thick / 2, cols.Right(i), y - thick / 2 + thick };
		FillSolid(dc, line, c.crToday);
		const int r = Scale(4);
		RECT dot{ cols.Left(i) - r, y - r, cols.Left(i) + r, y + r };
		WTL::CBrush brush;
		brush.CreateSolidBrush(c.crToday);
		auto oldBrush = dc.SelectBrush(brush);
		auto oldPen = dc.SelectStockPen(NULL_PEN);
		dc.Ellipse(&dot);
		dc.SelectBrush(oldBrush);
		dc.SelectPen(oldPen);
	}
	dc.SelectFont(oldFont);
}

// ---- Items --------------------------------------------------------------------------------------------

void CCalendarViewCtrl::ItemColors(const ClvItem& item, const CLVCOLORS& c, COLORREF* fill, COLORREF* text) const {
	if (IsHighContrast()) {
		// the items' own colors left out
		const bool selected = item.id == m_Data->selected;
		*fill = ::GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
		*text = ::GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
		return;
	}
	const COLORREF color = item.color != CLR_INVALID ? item.color : c.crItem;
	*fill = Blend(c.crBack, color, IsDark(c.crBack) ? 40 : 22);
	*text = c.crText;
	if (item.id == m_Data->selected) {
		*fill = ::GetFocus() == m_hWnd ? color : Blend(c.crBack, color, IsDark(c.crBack) ? 65 : 50);
		*text = IsDark(*fill) ? RGB(0xFF, 0xFF, 0xFF) : RGB(0, 0, 0);
	}
}


void CCalendarViewCtrl::PaintItem(WTL::CDCHandle dc, const CLVCOLORS& c, int index, bool strip, bool notify) {
	auto& d = *m_Data;
	const auto& box = strip ? d.layout.strip[index] : d.layout.timed[index];
	const auto& item = d.store.Items()[box.item];
	RECT rc = box.rc, caption = box.caption;
	const int dy = strip ? d.strip.top + d.m.stripPad - d.stripScroll : -d.scrollY;
	::OffsetRect(&rc, 0, dy);
	::OffsetRect(&caption, 0, dy);
	if (rc.right - rc.left < 2 || rc.bottom - rc.top < 2)
		return;

	const bool selected = item.id == d.selected;
	const COLORREF color = HighContrast ? c.crText : item.color != CLR_INVALID ? item.color : c.crItem;
	COLORREF fill, text;
	ItemColors(item, c, &fill, &text);
	const int bar = Scale(4);
	const bool window = item.content == ClvContent::Window;
	RECT inner{ rc.left + 1 + bar + Scale(4), rc.top + Scale(2), rc.right - Scale(4), rc.bottom - Scale(1) };
	if (window || strip)
		inner = { inner.left, window ? caption.top : rc.top, inner.right, window ? caption.bottom : rc.bottom };

	// the app's turn first: custom items always, the others when it asked
	NMCLVCUSTOMDRAW cd{};
	bool post = item.content == ClvContent::Custom;
	if (notify || post) {
		cd.nmcd.hdc = dc;
		cd.nmcd.rc = rc;
		cd.nmcd.uItemState = (selected ? CDIS_SELECTED : 0) | (selected && ::GetFocus() == m_hWnd ? CDIS_FOCUS : 0);
		cd.nmcd.lItemlParam = item.lParam;
		cd.nmcd.dwItemSpec = (DWORD_PTR)item.id;
		cd.id = item.id;
		cd.rcContent = inner;
		cd.crFill = fill;
		cd.crText = text;
		cd.crColor = color;
		cd.strip = strip;
		cd.continuesBefore = box.before;
		cd.continuesAfter = box.after;
		const LRESULT result = CustomDraw(this, cd, CDDS_ITEMPREPAINT);
		if (result & CDRF_SKIPDEFAULT)
			return;
		fill = cd.crFill;
		text = cd.crText;
		post = post || (result & CDRF_NOTIFYPOSTPAINT);
	}

	RoundFill(dc, rc, fill, HighContrast ? text : Blend(color, c.crBack, selected ? 0 : 25), Scale(6));
	// the color's bar at the left (none on a strip piece going on from before)
	if (!(strip && box.before)) {
		RECT side{ rc.left + 1, rc.top + 1, rc.left + 1 + bar, rc.bottom - 1 };
		FillSolid(dc, side, HighContrast ? text : color);
	}

	if (inner.right > inner.left && item.content != ClvContent::Custom) {
		dc.SetTextColor(text);
		TEXTMETRICW tm{};
		dc.SelectFont(m_hBold);
		dc.GetTextMetrics(&tm);
		const UINT format = DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
		if (strip || window)
			dc.DrawText(item.title.c_str(), (int)item.title.size(), &inner, format | DT_VCENTER);
		else {
			RECT line = inner;
			line.bottom = std::min(inner.bottom, inner.top + tm.tmHeight);
			dc.DrawText(item.title.c_str(), (int)item.title.size(), &line, format);
			// the time under the title, when there's room
			if (inner.bottom - line.bottom >= tm.tmHeight) {
				dc.SelectFont(m_hFont);
				auto time = TimeText(item.start, item.end, item.allDay);
				line.top = line.bottom;
				line.bottom = line.top + tm.tmHeight;
				dc.SetTextColor(selected || HighContrast ? text : Blend(c.crText, fill, 30));
				dc.DrawText(time.c_str(), (int)time.size(), &line, format);
			}
		}
		dc.SelectFont(m_hFont);
	}

	if (post) {
		cd.crFill = fill;
		cd.crText = text;
		dc.SetTextColor(text);
		CustomDraw(this, cd, CDDS_ITEMPOSTPAINT);
	}
}
