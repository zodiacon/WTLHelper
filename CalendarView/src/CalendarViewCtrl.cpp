#include "CalendarViewData.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

static_assert(CLVC_WINDOW == (int)ClvContent::Window);

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	ClvTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return ClvFromSystemTime(st);
	}

	constexpr UINT_PTR NowTimer = 1, DragTimer = 2;
	constexpr int MinSlotDips = 10, MaxSlotDips = 120;
}

// ---- Construction ------------------------------------------------------------------

CCalendarViewCtrl::CCalendarViewCtrl() : m_Data(std::make_unique<ClvData>()), m_Days(std::make_unique<ClvDaysWindow>(this)), m_Edit(this, 2) {
	m_Data->range = { ClvDayOf(Now()), 1 };
}

CCalendarViewCtrl::~CCalendarViewCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hBold)
		::DeleteObject(m_hBold);
	if (m_hStaticBrush)
		::DeleteObject(m_hStaticBrush);
}

LRESULT CCalendarViewCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	d.isVirtual = (GetStyle() & CLVS_VIRTUAL) != 0;
	ModifyStyleEx(0, WS_EX_CONTROLPARENT);
	// a container too: Tab goes into the items' windows
	m_Days->Create(m_hWnd, rcDefault, L"Days", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT);
	SetTimer(NowTimer, 30 * 1000);
	// the title edited in place (F2), hidden until then
	m_Edit.SubclassWindow(::CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr));
	// a tool on the strip and one on the days, each moved onto the item under the mouse
	m_hTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	if (m_hTip) {
		for (HWND window : { m_hWnd, m_Days->m_hWnd }) {
			TTTOOLINFOW ti{ sizeof(ti) };
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = window;
			ti.uId = 1;
			ti.lpszText = LPSTR_TEXTCALLBACKW;
			::SendMessage(m_hTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		}
		::SendMessage(m_hTip, TTM_SETMAXTIPWIDTH, 0, Scale(400));
	}
	UpdateScrollBarTheme();
	UpdateFonts();
	RangeChanged(false);
	return 0;
}

LRESULT CCalendarViewCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	KillTimer(NowTimer);
	DisconnectAccessible();
	m_Picker.reset();
	if (m_hTip)
		::DestroyWindow(m_hTip);
	m_hTip = nullptr;
	bHandled = FALSE;
	return 0;
}

LRESULT CCalendarViewCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CCalendarViewCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateLayout();
	return 0;
}

LRESULT CCalendarViewCtrl::OnFocus(UINT uMsg, WPARAM, LPARAM, BOOL&) {
	Invalidate(FALSE);   // the selection's color
	if (m_Days->IsWindow())
		m_Days->Invalidate(FALSE);
	if (uMsg == WM_KILLFOCUS)
		ClosePicker();
	if (uMsg == WM_SETFOCUS)
		NotifyAccessible(EVENT_OBJECT_FOCUS, m_Data->selected ? AccChildId(m_Data->selected) : AccSlotsId());
	return 0;
}

LRESULT CCalendarViewCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == DragTimer) {
		DragScroll();
		return 0;
	}
	if (wParam != NowTimer) {
		bHandled = FALSE;
		return 0;
	}
	// the line at now moves, and today may be another day
	Invalidate(FALSE);
	m_Days->Invalidate(FALSE);
	return 0;
}

HWND CCalendarViewCtrl::GetDaysWindow() const {
	return m_Days->m_hWnd;
}

LRESULT CCalendarViewCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

LRESULT CCalendarViewCtrl::NotifyItem(UINT code, ULONGLONG id) const {
	NMCLVITEM nm{};
	nm.id = id;
	if (auto item = m_Data->store.Find(id)) {
		nm.hwnd = item->hwnd;
		nm.lParam = item->lParam;
	}
	return FireNotify(&nm.hdr, code);
}

LRESULT CCalendarViewCtrl::NotifyRange(UINT code, LONGLONG start, LONGLONG end, bool allDay) const {
	NMCLVRANGE nm{};
	nm.start = ClvToSystemTime(start);
	nm.end = ClvToSystemTime(end);
	nm.allDay = allDay;
	return FireNotify(&nm.hdr, code);
}

void CCalendarViewCtrl::Release(const ClvItem& item) {
	NMCLVITEM nm{};
	nm.id = item.id;
	nm.hwnd = item.hwnd;
	nm.lParam = item.lParam;
	FireNotify(&nm.hdr, CLVN_RELEASEITEM);
}

// ---- Fonts and metrics ----------------------------------------------------------------

int CCalendarViewCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CCalendarViewCtrl::UpdateFonts() {
	auto& d = *m_Data;
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	if (m_hBold)
		::DeleteObject(m_hBold);
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	lf.lfWeight = FW_SEMIBOLD;
	m_hBold = ::CreateFontIndirectW(&lf);
	int text = 16, ruler = 0;
	if (IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		text = tm.tmHeight;
		ruler = RulerWidth(dc.m_hDC);
		dc.SelectFont(old);
	}
	auto m = ClvMetrics{}.Scaled(m_Dpi);
	m.rulerWidth = std::max(m.rulerWidth, ruler);
	m.headerHeight = std::max(m.headerHeight, text + Scale(10));
	m.stripRow = std::max(m.stripRow, text + Scale(6));
	m.minItem = std::max(m.minItem, text + Scale(4));
	m.caption = std::max(m.caption, text + Scale(4));
	d.m = m;
	d.axis.slotHeight = Scale(d.slotDips);
	Changed();
}

LRESULT CCalendarViewCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CCalendarViewCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CCalendarViewCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		// the same time at the top: the zoom is in DIPs
		auto& d = *m_Data;
		const int minute = d.axis.Minute(d.scrollY);
		m_Dpi = dpi;
		UpdateFonts();
		ScrollDays(d.axis.Y(minute));
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CCalendarViewCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS)   // the message font
		UpdateFonts();
	bHandled = FALSE;
	return 0;
}

LRESULT CCalendarViewCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UpdateScrollBarTheme();
	Invalidate();
	if (m_Days->IsWindow())
		m_Days->Invalidate();
	bHandled = FALSE;
	return 0;
}

void CCalendarViewCtrl::SetColors(const CLVCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow()) {
		UpdateScrollBarTheme();
		Invalidate();
		m_Days->Invalidate();
	}
}

void CCalendarViewCtrl::UpdateScrollBarTheme() {
	// the dark Explorer theme makes the scroll bar dark; set only when it changes
	if (!m_Days->IsWindow())
		return;
	const int dark = ClvDraw::IsDark(GetColors().crBack) ? 1 : 0;
	if (dark == m_DarkScrollBars)
		return;
	m_DarkScrollBars = dark;
	::SetWindowTheme(m_Days->m_hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	m_Days->SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// ---- Layout and scrolling ---------------------------------------------------------------------------

void CCalendarViewCtrl::Changed() {
	auto& d = *m_Data;
	if (d.selected && !d.store.Find(d.selected))
		d.selected = 0;
	UpdateLayout();
}

void CCalendarViewCtrl::UpdateLayout() {
	auto& d = *m_Data;
	if (!IsWindow() || !m_Days->IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	// the days' window always has its scroll bar (SIF_DISABLENOSCROLL): its client is that much narrower
	const int width = std::max(0, (int)rc.right - ::GetSystemMetricsForDpi(SM_CXVSCROLL, m_Dpi));
	const ClvColumns columns{ d.m.rulerWidth, std::max(0, width - d.m.rulerWidth), d.range.days };
	d.layout = ClvLayoutDay(d.store, d.range, columns, d.axis, d.m);
	d.accOrder = ClvTabOrder(d.layout, d.store);
	NotifyAccessible(EVENT_OBJECT_REORDER, CHILDID_SELF);

	LayoutNavBar(rc.right);
	d.header = { 0, d.navbar.bottom, rc.right, d.navbar.bottom + d.m.headerHeight };
	d.strip = { 0, d.header.bottom, rc.right, d.header.bottom + d.layout.StripView(d.m) };
	const int top = std::min((int)rc.bottom, (int)d.strip.bottom);
	d.daysTop = top;
	m_Days->SetWindowPos(nullptr, 0, top, rc.right, std::max(0, (int)rc.bottom - top), SWP_NOZORDER | SWP_NOACTIVATE);

	if (!d.scrolled && rc.bottom > top) {
		// the working day from its start, an hour before it shown
		d.scrolled = true;
		d.scrollY = d.axis.Y(std::max(0, d.work.start - 60));
	}
	ScrollStrip(d.stripScroll);
	ScrollDays(d.scrollY);
	Invalidate(FALSE);
}

void CCalendarViewCtrl::ScrollDays(int y) {
	auto& d = *m_Data;
	EndRename(true);   // its place moved
	RECT rc;
	m_Days->GetClientRect(&rc);
	d.scrollY = ClvClampScroll(y, d.axis.Height(), rc.bottom);
	SCROLLINFO si{ sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL };
	si.nMax = std::max(0, d.axis.Height() - 1);
	si.nPage = std::max(0, (int)rc.bottom);
	si.nPos = d.scrollY;
	m_Days->SetScrollInfo(SB_VERT, &si);
	m_Days->Invalidate(FALSE);
	PlaceWindows();
}

void CCalendarViewCtrl::ScrollStrip(int y) {
	auto& d = *m_Data;
	EndRename(true);
	const int view = d.layout.StripView(d.m) - 2 * d.m.stripPad;
	d.stripScroll = ClvClampScroll(y, d.layout.StripHeight(d.m), view);
	Invalidate(FALSE);
}

void CCalendarViewCtrl::ZoomAt(int slotHeight, int y) {
	auto& d = *m_Data;
	slotHeight = std::clamp(slotHeight, Scale(MinSlotDips), Scale(MaxSlotDips));
	if (slotHeight == d.axis.slotHeight)
		return;
	const int minute = d.axis.Minute(d.scrollY + y);
	d.axis.slotHeight = slotHeight;
	d.slotDips = ::MulDiv(slotHeight, 96, m_Dpi);
	UpdateLayout();
	ScrollDays(d.axis.Y(minute) - y);
}

LRESULT CCalendarViewCtrl::OnDaysVScroll(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (lParam)
		return OnForward(uMsg, wParam, lParam, bHandled);   // an item's window's scroll bar
	auto& d = *m_Data;
	SCROLLINFO si{ sizeof(si), SIF_ALL };
	m_Days->GetScrollInfo(SB_VERT, &si);
	int y = d.scrollY;
	switch (LOWORD(wParam)) {
		case SB_LINEUP: y -= d.axis.slotHeight; break;
		case SB_LINEDOWN: y += d.axis.slotHeight; break;
		case SB_PAGEUP: y -= (int)si.nPage; break;
		case SB_PAGEDOWN: y += (int)si.nPage; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = INT_MAX / 2; break;
	}
	ScrollDays(y);
	return 0;
}

LRESULT CCalendarViewCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	if (PickerOpen()) {
		m_Picker->Wheel(delta);   // its months
		return 0;
	}
	POINT pt = PointOf(lParam);   // screen coords, whichever window has it
	POINT client = pt;
	ScreenToClient(&client);
	if (::PtInRect(&d.strip, client)) {
		if (d.layout.stripRows > d.m.stripMaxRows)
			ScrollStrip(d.stripScroll - delta * d.m.stripRow / WHEEL_DELTA);
		return 0;
	}
	POINT days = pt;
	m_Days->ScreenToClient(&days);
	if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
		// zoom around the mouse (its time stays under it)
		RECT rc;
		m_Days->GetClientRect(&rc);
		const int y = ::PtInRect(&rc, days) ? days.y : rc.bottom / 2;
		const int steps = delta / WHEEL_DELTA != 0 ? delta / WHEEL_DELTA : (delta > 0 ? 1 : -1);
		ZoomAt(ClvZoom(d.axis.slotHeight, steps, Scale(MinSlotDips), Scale(MaxSlotDips)), y);
		return 0;
	}
	ScrollDays(d.scrollY - delta * 3 * d.axis.slotHeight / WHEEL_DELTA);
	if (d.drag != ClvDrag::None)
		DragTo(d.dragMouse);   // the time under the mouse moved
	return 0;
}

// ---- The days shown --------------------------------------------------------------------------------

void CCalendarViewCtrl::RangeChanged(bool notify) {
	auto& d = *m_Data;
	CancelDrag();
	if (d.isVirtual && IsWindow()) {
		// let go of the items that left the days, and ask for the days not asked for yet
		for (auto& item : d.store.Keep(d.range.Start(), d.range.End()))
			Release(item);
		auto missing = ClvMissing(d.range, d.cached);
		d.cached = d.range;
		d.fetching = true;
		for (auto& r : missing)
			NotifyRange(CLVN_GETITEMS, r.Start(), r.End(), false);
		d.fetching = false;
	}
	Changed();
	if (notify)
		NotifyRange(CLVN_RANGECHANGED, d.range.Start(), d.range.End(), false);
}

BOOL CCalendarViewCtrl::SetView(UINT view) {
	if (view != CLVV_DAY)
		return FALSE;   // the month view is to come
	m_Data->view = view;
	return TRUE;
}

UINT CCalendarViewCtrl::GetView() const {
	return m_Data->view;
}

void CCalendarViewCtrl::SetDate(const SYSTEMTIME& first) {
	auto& d = *m_Data;
	const int64_t day = ClvDayOf(ClvFromSystemTime(first));
	if (day == d.range.first)
		return;
	d.range.first = day;
	RangeChanged(true);
}

void CCalendarViewCtrl::SetDayCount(int days) {
	auto& d = *m_Data;
	days = std::clamp(days, 1, 14);
	if (days == d.range.days)
		return;
	d.range.days = days;
	RangeChanged(true);
}

void CCalendarViewCtrl::GetRange(SYSTEMTIME* first, int* days) const {
	if (first)
		*first = ClvToSystemTime(m_Data->range.Start());
	if (days)
		*days = m_Data->range.days;
}

void CCalendarViewCtrl::GoToday() {
	auto& d = *m_Data;
	const ClvTime now = Now();
	const int64_t today = ClvDayOf(now);
	if (!d.range.Contains(today)) {
		// a week shown: the week holding today (a work week from its Monday)
		d.range.first = ClvFirstShown(today, d.range.days, GetFirstDayOfWeek());
		RangeChanged(true);
	}
	ScrollToTime(std::max(0, ClvMinuteOf(now) - 60));
}

void CCalendarViewCtrl::ScrollToTime(int minute) {
	auto& d = *m_Data;
	d.scrolled = true;
	if (IsWindow())
		ScrollDays(d.axis.Y(std::clamp(minute, 0, (int)ClvDay)));
	else
		d.scrollY = d.axis.Y(std::clamp(minute, 0, (int)ClvDay));
}

void CCalendarViewCtrl::SetSlotMinutes(int minutes) {
	auto& d = *m_Data;
	static const int allowed[] = { 5, 6, 10, 15, 20, 30, 60 };
	if (std::find(std::begin(allowed), std::end(allowed), minutes) == std::end(allowed) || minutes == d.axis.slotMinutes)
		return;
	const int minute = d.axis.Minute(d.scrollY);
	d.axis.slotMinutes = minutes;
	if (d.slots && !d.slot.allDay)
		ClearSlots();   // not on the new slots
	UpdateLayout();
	ScrollDays(d.axis.Y(minute));
}

int CCalendarViewCtrl::GetSlotMinutes() const {
	return m_Data->axis.slotMinutes;
}

void CCalendarViewCtrl::SetZoom(int slotHeightDips) {
	auto& d = *m_Data;
	d.slotDips = std::clamp(slotHeightDips, MinSlotDips, MaxSlotDips);
	if (IsWindow())
		ZoomAt(Scale(d.slotDips), 0);
}

int CCalendarViewCtrl::GetZoom() const {
	return m_Data->slotDips;
}

void CCalendarViewCtrl::SetWorkWeek(BYTE days, int startMinute, int endMinute) {
	auto& w = m_Data->work;
	w.days = days & 0x7F;
	w.start = std::clamp(startMinute, 0, (int)ClvDay - 1);
	w.end = std::clamp(endMinute, w.start + 1, (int)ClvDay);
	if (IsWindow()) {
		Invalidate(FALSE);
		m_Days->Invalidate(FALSE);
	}
}

// ---- Items -----------------------------------------------------------------------------------------

namespace {
	// the item's fields in mask from the API's
	void ApplyFields(ClvItem& t, const CLVITEM& item, UINT mask) {
		if (mask & CLVIF_TIME) {
			t.start = ClvFromSystemTime(item.start);
			t.end = ClvFromSystemTime(item.end);
		}
		if (mask & CLVIF_TITLE)
			t.title = item.pszTitle ? item.pszTitle : L"";
		if (mask & CLVIF_TIP)
			t.tip = item.pszTip ? item.pszTip : L"";
		if (mask & CLVIF_COLOR)
			t.color = item.color;
		if (mask & CLVIF_FLAGS) {
			t.allDay = (item.flags & CLVF_ALLDAY) != 0;
			t.readOnly = (item.flags & CLVF_READONLY) != 0;
		}
		if (mask & CLVIF_CONTENT) {
			t.content = (ClvContent)std::min(item.content, (UINT)CLVC_WINDOW);
			t.hwnd = t.content == ClvContent::Window ? item.hwnd : nullptr;
		}
		if (mask & CLVIF_PARAM)
			t.lParam = item.lParam;
		if (t.allDay) {
			// whole days, at least one
			t.start = ClvDayOf(t.start) * ClvDay;
			t.end = std::max(ClvDayOf(t.end + ClvDay - 1), ClvDayOf(t.start) + 1) * ClvDay;
		}
	}
}

BOOL CCalendarViewCtrl::AddItem(const CLVITEM* item) {
	auto& d = *m_Data;
	if (!item)
		return FALSE;
	ClvItem t;
	t.id = item->id;
	ApplyFields(t, *item, CLVIF_ALL);
	if (!d.creating) {
		CancelDrag();
		d.history.Clear();   // an undo would take this back too
	}
	if (!d.store.Add(t))
		return FALSE;
	if (!d.fetching)
		Changed();
	return TRUE;
}

BOOL CCalendarViewCtrl::AddItem(ULONGLONG id, LPCWSTR title, const SYSTEMTIME& start, const SYSTEMTIME& end, UINT flags, COLORREF color) {
	CLVITEM item;
	item.id = id;
	item.pszTitle = title;
	item.start = start;
	item.end = end;
	item.flags = flags;
	item.color = color;
	return AddItem(&item);
}

BOOL CCalendarViewCtrl::SetItem(const CLVITEM* item) {
	auto& d = *m_Data;
	auto p = item ? d.store.Find(item->id) : nullptr;
	if (!p)
		return FALSE;
	CancelDrag();
	d.history.Clear();   // an undo would take this back too
	ClvItem t = *p;
	ApplyFields(t, *item, item->mask);
	if (!d.store.Set(t))
		return FALSE;
	Changed();
	return TRUE;
}

BOOL CCalendarViewCtrl::GetItem(ULONGLONG id, CLVITEM* item) const {
	auto& d = *m_Data;
	auto t = d.store.Find(id);
	if (!t || !item)
		return FALSE;
	d.titleBuffer = t->title;
	d.tipBuffer = t->tip;
	item->mask = CLVIF_ALL;
	item->id = t->id;
	item->start = ClvToSystemTime(t->start);
	item->end = ClvToSystemTime(t->end);
	item->flags = (t->allDay ? CLVF_ALLDAY : 0) | (t->readOnly ? CLVF_READONLY : 0);
	item->pszTitle = d.titleBuffer.c_str();
	item->pszTip = d.tipBuffer.c_str();
	item->color = t->color;
	item->content = (UINT)t->content;
	item->hwnd = t->hwnd;
	item->lParam = t->lParam;
	return TRUE;
}

BOOL CCalendarViewCtrl::RemoveItem(ULONGLONG id) {
	CancelDrag();
	m_Data->history.Clear();   // an undo would take this back too
	if (!m_Data->store.Remove(id))
		return FALSE;
	Changed();
	return TRUE;
}

void CCalendarViewCtrl::RemoveAll() {
	CancelDrag();
	m_Data->history.Clear();   // an undo would take this back too
	m_Data->store.Clear();
	Changed();
}

int CCalendarViewCtrl::GetItemCount() const {
	return (int)m_Data->store.Items().size();
}

void CCalendarViewCtrl::RefreshItems() {
	auto& d = *m_Data;
	if (!d.isVirtual)
		return;
	CancelDrag();
	auto items = d.store.Save();
	d.store.Clear();
	for (auto& item : items)
		Release(item);
	d.cached = { 0, 0 };
	RangeChanged(false);
}

// ---- Selection ----------------------------------------------------------------------------------------

BOOL CCalendarViewCtrl::Select(ULONGLONG id) {
	if (id && !m_Data->store.Find(id))
		return FALSE;
	SetSelected(id, false);
	return TRUE;
}

ULONGLONG CCalendarViewCtrl::GetSelected() const {
	return m_Data->selected;
}

void CCalendarViewCtrl::SetSelected(ULONGLONG id, bool notify) {
	auto& d = *m_Data;
	if (id)
		ClearSlots();
	if (d.selected == id)
		return;
	const ULONGLONG old = d.selected;
	d.selected = id;
	Invalidate(FALSE);
	m_Days->Invalidate(FALSE);
	RedrawWindowOf(old);
	RedrawWindowOf(id);
	if (const long child = AccChildId(id); child != CHILDID_SELF) {
		NotifyAccessible(EVENT_OBJECT_SELECTION, child);
		if (::GetFocus() == m_hWnd)
			NotifyAccessible(EVENT_OBJECT_FOCUS, child);
	}
	if (notify)
		NotifyItem(CLVN_SELCHANGED, id);
}

void CCalendarViewCtrl::SelectSlots(LONGLONG start, LONGLONG end, bool allDay, bool notify) {
	auto& d = *m_Data;
	SetSelected(0, notify);
	const bool had = d.slots;
	d.slots = true;
	d.slot = { start, end, allDay };
	Invalidate(FALSE);
	m_Days->Invalidate(FALSE);
	if (!had)
		NotifyAccessible(EVENT_OBJECT_REORDER, CHILDID_SELF);   // the selected time's child came
	NotifyAccessible(EVENT_OBJECT_NAMECHANGE, AccSlotsId());
	if (::GetFocus() == m_hWnd)
		NotifyAccessible(EVENT_OBJECT_FOCUS, AccSlotsId());
	if (notify)
		NotifyRange(CLVN_SLOTCHANGED, start, end, allDay);
}

void CCalendarViewCtrl::ClearSlots() {
	auto& d = *m_Data;
	if (!d.slots)
		return;
	d.slots = false;
	NotifyAccessible(EVENT_OBJECT_REORDER, CHILDID_SELF);   // the selected time's child went
	Invalidate(FALSE);
	m_Days->Invalidate(FALSE);
}

BOOL CCalendarViewCtrl::GetSelectedSlots(SYSTEMTIME* start, SYSTEMTIME* end, BOOL* allDay) const {
	auto& d = *m_Data;
	if (!d.slots)
		return FALSE;
	if (start)
		*start = ClvToSystemTime(d.slot.start);
	if (end)
		*end = ClvToSystemTime(d.slot.end);
	if (allDay)
		*allDay = d.slot.allDay;
	return TRUE;
}

void CCalendarViewCtrl::EnsureVisible(ULONGLONG id) {
	auto& d = *m_Data;
	auto item = d.store.Find(id);
	if (!item)
		return;
	if (!d.range.Contains(ClvFirstDay(*item)) && !d.range.Contains(ClvLastDay(*item))) {
		d.range.first = ClvFirstDay(*item);
		RangeChanged(true);
		if (!(item = d.store.Find(id)))
			return;   // a virtual app didn't give it back
	}
	if (ClvInStrip(*item)) {
		if (auto box = d.layout.FindStrip(id))
			ScrollStrip(ClvScrollToShow(d.stripScroll, box->row * d.m.stripRow, (box->row + 1) * d.m.stripRow, d.layout.StripView(d.m) - 2 * d.m.stripPad,
				d.layout.StripHeight(d.m)));
		return;
	}
	const int day = (int)(std::max(ClvFirstDay(*item), d.range.first) - d.range.first);
	if (auto box = d.layout.FindTimed(id, day)) {
		RECT rc;
		m_Days->GetClientRect(&rc);
		ScrollDays(ClvScrollToShow(d.scrollY, box->rc.top, box->rc.bottom, rc.bottom, d.axis.Height()));
	}
}

// ---- Mouse --------------------------------------------------------------------------------------------

ClvHit CCalendarViewCtrl::HitStrip(POINT pt) const {
	auto& d = *m_Data;
	if (!::PtInRect(&d.strip, pt))
		return {};
	return ClvHitStrip(d.layout, { pt.x, pt.y - d.strip.top - d.m.stripPad + d.stripScroll }, d.m, d.selected);
}

ClvHit CCalendarViewCtrl::HitDays(POINT pt) const {
	auto& d = *m_Data;
	return ClvHitDays(d.layout, { pt.x, pt.y + d.scrollY }, d.m, d.selected);
}

LRESULT CCalendarViewCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	Press(PointOf(lParam), false);
	return 0;
}

LRESULT CCalendarViewCtrl::OnLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	Press(PointOf(lParam), true);
	return 0;
}

LRESULT CCalendarViewCtrl::OnDaysLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt = PointOf(lParam);
	Press({ pt.x, pt.y + m_Data->daysTop }, false);
	return 0;
}

LRESULT CCalendarViewCtrl::OnDaysLButtonDblClk(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt = PointOf(lParam);
	Press({ pt.x, pt.y + m_Data->daysTop }, true);
	return 0;
}

// ---- The items' windows -------------------------------------------------------------------------------

void CCalendarViewCtrl::PlaceWindows() {
	auto& d = *m_Data;
	if (!m_Days->IsWindow())
		return;
	// where each window goes: its first piece with room for it, inside the color bar and the border (else hidden)
	const int bar = 1 + Scale(4);
	std::vector<std::pair<HWND, RECT>> want;
	for (auto& item : d.store.Items()) {
		if (item.content != ClvContent::Window || !item.hwnd || !::IsWindow(item.hwnd))
			continue;
		RECT rc{};
		for (auto& box : d.layout.timed)
			if (box.id == item.id && !::IsRectEmpty(&box.body)) {
				rc = { box.body.left + bar, box.body.top - d.scrollY, box.body.right - 1, box.body.bottom - 1 - d.scrollY };
				break;
			}
		if (rc.right <= rc.left || rc.bottom <= rc.top)
			rc = {};
		want.push_back({ item.hwnd, rc });
	}
	auto find = [](const std::vector<std::pair<HWND, RECT>>& list, HWND hwnd) {
		auto it = std::find_if(list.begin(), list.end(), [&](auto& p) { return p.first == hwnd; });
		return it == list.end() ? nullptr : &*it;
	};

	HDWP dwp = ::BeginDeferWindowPos((int)(want.size() + d.placed.size()));
	auto defer = [&](HWND hwnd, const RECT* rc) {
		const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | (rc ? SWP_SHOWWINDOW : SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
		const int x = rc ? rc->left : 0, y = rc ? rc->top : 0, cx = rc ? rc->right - rc->left : 0, cy = rc ? rc->bottom - rc->top : 0;
		if (dwp)
			dwp = ::DeferWindowPos(dwp, hwnd, nullptr, x, y, cx, cy, flags);
		else
			::SetWindowPos(hwnd, nullptr, x, y, cx, cy, flags);
	};
	// the windows of items removed or let go of: hidden (the app's to destroy)
	for (auto& [hwnd, rc] : d.placed)
		if (!find(want, hwnd) && ::IsWindow(hwnd) && ::IsWindowVisible(hwnd) && ::GetParent(hwnd) == m_Days->m_hWnd)
			defer(hwnd, nullptr);
	for (auto& [hwnd, rc] : want) {
		if (::GetParent(hwnd) != m_Days->m_hWnd) {
			// made the days' window's child: it scrolls and clips with them
			if (dwp) {
				::EndDeferWindowPos(dwp);
				dwp = nullptr;
			}
			const LONG_PTR style = ::GetWindowLongPtr(hwnd, GWL_STYLE);
			if (!(style & WS_CHILD))
				::SetWindowLongPtr(hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
			::SetParent(hwnd, m_Days->m_hWnd);
			dwp = ::BeginDeferWindowPos((int)want.size());
		}
		auto before = find(d.placed, hwnd);
		const bool visible = ::IsWindowVisible(hwnd) != FALSE;
		if (::IsRectEmpty(&rc)) {
			if (visible)
				defer(hwnd, nullptr);
		}
		else if (!visible || !before || !::EqualRect(&before->second, &rc))
			defer(hwnd, &rc);
	}
	if (dwp)
		::EndDeferWindowPos(dwp);
	d.placed = std::move(want);
}

void CCalendarViewCtrl::RedrawWindowOf(ULONGLONG id) {
	auto item = id ? m_Data->store.Find(id) : nullptr;
	if (item && item->hwnd && ::IsWindow(item->hwnd))
		::RedrawWindow(item->hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

LRESULT CCalendarViewCtrl::OnDaysParentNotify(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	bHandled = FALSE;
	if (LOWORD(wParam) != WM_LBUTTONDOWN && LOWORD(wParam) != WM_RBUTTONDOWN)
		return 0;
	// a press in an item's window selects the item (the window keeps the press)
	const POINT pt = PointOf(lParam);
	for (auto& [hwnd, rc] : d.placed)
		if (::PtInRect(&rc, pt))
			for (auto& item : d.store.Items())
				if (item.hwnd == hwnd) {
					SetSelected(item.id, true);
					return 0;
				}
	return 0;
}

LRESULT CCalendarViewCtrl::OnForward(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (uMsg == WM_NOTIFY && ((NMHDR*)lParam)->hwndFrom == m_hTip)
		return OnNotify(uMsg, wParam, lParam, bHandled);   // the days' tool's text
	return ::SendMessage(GetParent(), uMsg, wParam, lParam);
}

LRESULT CCalendarViewCtrl::OnCtlColor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if (uMsg == WM_CTLCOLORSTATIC) {
		// a static (or a read-only edit) that is an item's window: on the item's fill
		for (auto& item : d.store.Items())
			if (item.hwnd == (HWND)lParam) {
				COLORREF fill, text;
				ItemColors(item, GetColors(), &fill, &text);
				if (fill != m_StaticColor || !m_hStaticBrush) {
					if (m_hStaticBrush)
						::DeleteObject(m_hStaticBrush);
					m_hStaticBrush = ::CreateSolidBrush(fill);
					m_StaticColor = fill;
				}
				::SetTextColor((HDC)wParam, text);
				::SetBkColor((HDC)wParam, fill);
				return (LRESULT)m_hStaticBrush;
			}
	}
	return OnForward(uMsg, wParam, lParam, bHandled);
}
