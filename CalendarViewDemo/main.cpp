// CalendarViewDemo: a week of meetings in a calendar view. The View menu shows a day, three days, the work week or the
// whole week, goes back and forth and to today, changes the slots and the zoom (Ctrl+wheel zooms too), and switches
// dark mode and a larger font. Items adds an item over the slots selected (click a slot, or a day in the strip for an
// all-day one) and deletes the one selected. Data > Virtual Mode makes the control again with CLVS_VIRTUAL: then the
// items come from a generator that knows every day, asked for the days shown, so going back and forth goes on forever.
// This week has items that are windows: a build's progress bar, a meeting's Join button, an agenda list, read-only
// notes (on the item's color: WM_CTLCOLORSTATIC) and one too short for its window; their notifications and commands
// come here through the control. "Burndown" is a custom item drawn here (CDDS_ITEMPOSTPAINT), and View > Dim Past
// Items changes the colors of the items that are over (CDDS_ITEMPREPAINT). The log at the bottom shows the
// notifications.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "CalendarView.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <algorithm>
#include <memory>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

enum : UINT {
	IDC_CALENDAR = 100, IDC_LOG, IDC_PROGRESS = 200, IDC_JOIN, IDC_AGENDA, IDC_NOTES, IDC_SHORT,
	CMD_EXIT = 1000, CMD_DAY, CMD_THREEDAYS, CMD_WORKWEEK, CMD_WEEK, CMD_PREV, CMD_NEXT, CMD_TODAY, CMD_SLOT15,
	CMD_SLOT30, CMD_SLOT60, CMD_ZOOMIN, CMD_ZOOMOUT, CMD_DARKMODE, CMD_LARGEFONT, CMD_CLEARLOG, CMD_NOW, CMD_ADD,
	CMD_DELETE, CMD_VIRTUAL, CMD_REFRESH, CMD_DIMPAST,
};

static std::unique_ptr<CCalendarViewCtrl> g_calendar;   // made again for virtual mode: a new control's state
static HWND g_main, g_log;
static HMENU g_menu;
static HFONT g_font, g_largeFont;
static ULONGLONG g_nextId = 1000;
static SYSTEMTIME g_monday;   // this week's, at 0:00
static bool g_virtual;
static int g_fetched, g_released;
static HWND g_progress;
constexpr UINT_PTR ProgressTimer = 1;

// ---- Helpers ------------------------------------------------------------------------------------

static void Log(const wchar_t* format, ...) {
	wchar_t text[1024];
	va_list args;
	va_start(args, format);
	::_vsnwprintf_s(text, _TRUNCATE, format, args);
	va_end(args);
	int i = (int)::SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)text);
	::SendMessageW(g_log, LB_SETCURSEL, i, 0);
}

static int Scale(int dips) {
	return ::MulDiv(dips, (int)::GetDpiForWindow(g_main), 96);
}

static bool Toggle(UINT id) {
	bool on = !(::GetMenuState(g_menu, id, MF_BYCOMMAND) & MF_CHECKED);
	::CheckMenuItem(g_menu, id, on ? MF_CHECKED : MF_UNCHECKED);
	return on;
}

static ULONGLONG Ticks(const SYSTEMTIME& st) {
	FILETIME ft;
	::SystemTimeToFileTime(&st, &ft);
	return ULARGE_INTEGER{ ft.dwLowDateTime, ft.dwHighDateTime }.QuadPart;
}

static SYSTEMTIME FromTicks(ULONGLONG ticks) {
	FILETIME ft{ (DWORD)ticks, (DWORD)(ticks >> 32) };
	SYSTEMTIME st;
	::FileTimeToSystemTime(&ft, &st);
	return st;
}

constexpr ULONGLONG Minute = 60ULL * 10000000ULL, DayTicks = 24 * 60 * Minute;

// a day after this week's Monday (negative: before), at h:mm
static SYSTEMTIME At(int day, int h = 0, int m = 0) {
	return FromTicks(Ticks(g_monday) + (LONGLONG)day * DayTicks + (ULONGLONG)(h * 60 + m) * Minute);
}

static LONGLONG DayIndex(const SYSTEMTIME& st) {   // days from this week's Monday (before it: negative)
	const LONGLONG diff = (LONGLONG)Ticks(st) - (LONGLONG)Ticks(g_monday), day = (LONGLONG)DayTicks;
	return diff >= 0 ? diff / day : -((-diff + day - 1) / day);
}

static void ShowDate(const SYSTEMTIME& st, wchar_t* text, size_t size) {
	swprintf_s(text, size, L"%u/%u %02u:%02u", st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// ---- The items --------------------------------------------------------------------------------------------

static const COLORREF Green = RGB(0x3A, 0x9E, 0x4F), Orange = RGB(0xE0, 0x8A, 0x2E), Purple = RGB(0x8E, 0x5B, 0xC7);

static void Add(ULONGLONG id, LPCWSTR title, const SYSTEMTIME& start, const SYSTEMTIME& end, UINT flags = 0, COLORREF color = CLR_INVALID) {
	g_calendar->AddItem(id, title, start, end, flags, color);
}

// this week's items, the control's own
static void Build() {
	for (int day = 0; day < 5; day++)
		Add(1 + day, L"Stand-up", At(day, 9, 30), At(day, 9, 45));
	Add(10, L"Design review", At(0, 10), At(0, 11, 30), 0, Purple);
	Add(11, L"Interview", At(0, 11), At(0, 12));
	Add(12, L"1:1 with Dana", At(0, 11), At(0, 11, 30), 0, Green);
	Add(13, L"Lunch", At(1, 12, 30), At(1, 13, 30), 0, Orange);
	Add(14, L"Planning", At(1, 14), At(1, 16));
	Add(15, L"Customer call", At(2, 8), At(2, 8, 30));
	Add(16, L"Workshop", At(2, 13), At(2, 17), 0, Purple);
	Add(17, L"Coffee", At(2, 15), At(2, 15, 20), 0, Orange);
	Add(18, L"Late deploy", At(3, 22), At(4, 1, 30));
	Add(19, L"Retro", At(4, 15), At(4, 16), 0, Green);
	Add(20, L"Conference", At(1), At(3), CLVF_ALLDAY, Purple);
	Add(21, L"Team offsite", At(4), At(5), CLVF_ALLDAY, Green);
	Add(22, L"On call", At(0, 9), At(2, 9), 0, Orange);   // two days: the strip's
	Add(23, L"Release week", At(0), At(5), CLVF_ALLDAY);

	// items that are windows: made the days' window's children, or adopted from elsewhere
	HWND days = g_calendar->GetDaysWindow();
	auto window = [&](ULONGLONG id, LPCWSTR title, const SYSTEMTIME& start, const SYSTEMTIME& end, HWND hwnd, COLORREF color = CLR_INVALID) {
		::SendMessageW(hwnd, WM_SETFONT, (WPARAM)(g_largeFont ? g_largeFont : g_font), FALSE);
		CLVITEM item;
		item.id = id;
		item.pszTitle = title;
		item.start = start;
		item.end = end;
		item.color = color;
		item.content = CLVC_WINDOW;
		item.hwnd = hwnd;
		g_calendar->AddItem(&item);
	};
	g_progress = ::CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD, 0, 0, 0, 0, days, (HMENU)IDC_PROGRESS, nullptr, nullptr);
	::SendMessageW(g_progress, PBM_SETSTEP, 2, 0);
	window(30, L"Build #1234", At(2, 9, 45), At(2, 11, 30), g_progress, Green);
	window(31, L"Daily sync", At(3, 10), At(3, 11, 30), ::CreateWindowExW(0, WC_BUTTONW, L"Join meeting",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, days, (HMENU)IDC_JOIN, nullptr, nullptr));
	HWND agenda = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		0, 0, 0, 0, days, (HMENU)IDC_AGENDA, nullptr, nullptr);
	for (auto line : { L"1. Last sprint", L"2. Demo", L"3. Risks", L"4. Next steps", L"5. Questions", L"6. Wrap-up" })
		::SendMessageW(agenda, LB_ADDSTRING, 0, (LPARAM)line);
	window(32, L"Agenda", At(3, 13), At(3, 15, 30), agenda, Purple);
	// made a child of the main window: the control adopts it
	window(33, L"Notes", At(4, 10), At(4, 12), ::CreateWindowExW(0, WC_EDITW,
		L"Ship the release notes.\r\nAsk Dana about the offsite.\r\nBook the room for Monday.",
		WS_CHILD | ES_MULTILINE | ES_READONLY, 0, 0, 0, 0, g_main, (HMENU)IDC_NOTES, nullptr, nullptr), Orange);
	window(34, L"Too short for its window", At(4, 13), At(4, 13, 15), ::CreateWindowExW(0, WC_BUTTONW, L"Hidden",
		WS_CHILD, 0, 0, 0, 0, days, (HMENU)IDC_SHORT, nullptr, nullptr));

	// a custom item: the control draws its background, the demo a chart on it
	CLVITEM burndown;
	burndown.id = 40;
	burndown.pszTitle = L"Burndown";
	burndown.start = At(0, 13);
	burndown.end = At(0, 15, 30);
	burndown.content = CLVC_CUSTOM;
	g_calendar->AddItem(&burndown);
}

// the custom item's chart: its title, then bars falling to the end of the sprint
static void DrawBurndown(NMCLVCUSTOMDRAW* cd) {
	HDC dc = cd->nmcd.hdc;
	RECT rc = cd->rcContent;
	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, cd->crText);
	RECT title = rc;
	::DrawTextW(dc, L"Burndown", -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
	::DrawTextW(dc, L"Burndown", -1, &title, DT_SINGLELINE | DT_NOPREFIX);
	rc.top = title.bottom + Scale(4);
	if (rc.bottom - rc.top < Scale(8))
		return;
	static const int left[] = { 100, 92, 85, 70, 66, 52, 40, 31, 18, 9 };
	const int n = _countof(left), w = (rc.right - rc.left) / n;
	HBRUSH brush = ::CreateSolidBrush(cd->crColor);
	for (int i = 0; i < n && w > 1; i++) {
		RECT bar{ rc.left + i * w, rc.bottom - (rc.bottom - rc.top) * left[i] / 100, rc.left + (i + 1) * w - 1, rc.bottom };
		::FillRect(dc, &bar, brush);
	}
	::DeleteObject(brush);
}

static bool IsOver(ULONGLONG id) {
	CLVITEM item;
	if (!g_calendar->GetItem(id, &item))
		return false;
	SYSTEMTIME now;
	::GetLocalTime(&now);
	return Ticks(item.end) <= Ticks(now);
}

// virtual mode: every day's items, made up from the day (the same each time it's asked for)
static void Generate(const SYSTEMTIME& start, const SYSTEMTIME& end) {
	const LONGLONG first = DayIndex(start), last = DayIndex(FromTicks(Ticks(end) - 1));
	for (LONGLONG day = first; day <= last; day++) {
		const ULONGLONG base = (ULONGLONG)(day + 100000) * 16;   // ids by day
		const int weekday = (int)(((day % 7) + 7) % 7);           // 0 Monday
		if (weekday < 5) {
			Add(base + 1, L"Stand-up", At((int)day, 9, 30), At((int)day, 9, 45));
			unsigned seed = (unsigned)(day * 2654435761u);
			for (int k = 0; k < 3; k++) {
				seed = seed * 1103515245 + 12345;
				const int hour = 10 + (seed >> 16) % 7, length = 30 * (1 + (seed >> 8) % 4);
				static const wchar_t* const titles[] = { L"Review", L"Sync", L"Interview", L"Planning", L"Focus time" };
				static const COLORREF colors[] = { CLR_INVALID, Green, Orange, Purple };
				wchar_t title[40];
				swprintf_s(title, L"%s %lld", titles[(seed >> 4) % 5], day);
				Add(base + 2 + k, title, At((int)day, hour, (seed >> 12) % 2 * 30), At((int)day, hour, (seed >> 12) % 2 * 30 + length),
					0, colors[(seed >> 20) % 4]);
			}
		}
		else {
			// over Saturday and Sunday: asked for either (the control keeps one, by id)
			const LONGLONG saturday = day - (weekday - 5);
			Add((ULONGLONG)(saturday + 100000) * 16 + 8, L"Weekend", At((int)saturday), At((int)saturday + 2), CLVF_ALLDAY, Green);
		}
	}
}

// ---- The window ---------------------------------------------------------------------------------------

static void Layout() {
	if (!g_calendar || !g_calendar->IsWindow())
		return;   // SetMenu in WM_CREATE sizes the window before the calendar is made
	RECT rc;
	::GetClientRect(g_main, &rc);
	const int log = Scale(110);
	const int bottom = std::max(0, (int)rc.bottom - log);
	g_calendar->SetWindowPos(nullptr, 0, 0, rc.right, bottom, SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(g_log, nullptr, 0, bottom, rc.right, rc.bottom - bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void UpdateFonts() {
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
	if (g_font)
		::DeleteObject(g_font);
	g_font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	::SendMessageW(g_log, WM_SETFONT, (WPARAM)g_font, TRUE);
	if (g_largeFont) {
		::DeleteObject(g_largeFont);
		ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
		g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
		g_calendar->SetFont(g_largeFont);
	}
}

static void ShowDays(int days) {
	for (UINT id : { CMD_DAY, CMD_THREEDAYS, CMD_WORKWEEK, CMD_WEEK })
		::CheckMenuItem(g_menu, id, MF_UNCHECKED);
	::CheckMenuItem(g_menu, days == 1 ? CMD_DAY : days == 3 ? CMD_THREEDAYS : days == 5 ? CMD_WORKWEEK : CMD_WEEK, MF_CHECKED);
	SYSTEMTIME first;
	g_calendar->GetRange(&first, nullptr);
	if (days == 5 || days == 7) {
		// the week holding the first day shown
		const LONGLONG day = DayIndex(first);
		first = At((int)(day - ((day % 7) + 7) % 7));
	}
	g_calendar->SetDayCount(days);
	g_calendar->SetDate(first);
}

static void CreateCalendar() {
	if (g_calendar && g_calendar->IsWindow())
		g_calendar->DestroyWindow();
	g_calendar = std::make_unique<CCalendarViewCtrl>();
	g_fetched = g_released = 0;
	g_progress = nullptr;
	g_calendar->Create(g_main, nullptr, L"Calendar", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP |
		(g_virtual ? CLVS_VIRTUAL : 0), 0, IDC_CALENDAR);
	::SetWindowPos(*g_calendar, g_log, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	if (g_largeFont)
		g_calendar->SetFont(g_largeFont);
	g_calendar->SetDayCount(5);
	g_calendar->SetDate(At(0));
	if (!g_virtual)
		Build();
	Layout();
	ShowDays(5);
}

static HMENU BuildMenu() {
	HMENU file = ::CreatePopupMenu();
	::AppendMenuW(file, MF_STRING, CMD_EXIT, L"E&xit");
	HMENU view = ::CreatePopupMenu();
	::AppendMenuW(view, MF_STRING, CMD_DAY, L"&Day\tCtrl+1");
	::AppendMenuW(view, MF_STRING, CMD_THREEDAYS, L"&Three Days\tCtrl+3");
	::AppendMenuW(view, MF_STRING, CMD_WORKWEEK, L"&Work Week\tCtrl+5");
	::AppendMenuW(view, MF_STRING, CMD_WEEK, L"W&eek\tCtrl+7");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_PREV, L"&Previous\tAlt+Left");
	::AppendMenuW(view, MF_STRING, CMD_NEXT, L"&Next\tAlt+Right");
	::AppendMenuW(view, MF_STRING, CMD_TODAY, L"T&oday\tCtrl+T");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_SLOT15, L"&15 Minute Slots");
	::AppendMenuW(view, MF_STRING | MF_CHECKED, CMD_SLOT30, L"&30 Minute Slots");
	::AppendMenuW(view, MF_STRING, CMD_SLOT60, L"&60 Minute Slots");
	::AppendMenuW(view, MF_STRING, CMD_ZOOMIN, L"Zoom &In");
	::AppendMenuW(view, MF_STRING, CMD_ZOOMOUT, L"Zoom O&ut");
	::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(view, MF_STRING, CMD_NOW, L"No Line at &Now");
	::AppendMenuW(view, MF_STRING, CMD_DIMPAST, L"Di&m Past Items (Custom Draw)");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(view, MF_STRING, CMD_DARKMODE, L"Dar&k Mode");
#endif
	::AppendMenuW(view, MF_STRING, CMD_LARGEFONT, L"&Larger Font");
	::AppendMenuW(view, MF_STRING, CMD_CLEARLOG, L"Clear Lo&g");
	HMENU items = ::CreatePopupMenu();
	::AppendMenuW(items, MF_STRING, CMD_ADD, L"&New Item in the Selected Slots\tCtrl+N");
	::AppendMenuW(items, MF_STRING, CMD_DELETE, L"&Delete the Selected Item");
	HMENU data = ::CreatePopupMenu();
	::AppendMenuW(data, MF_STRING, CMD_VIRTUAL, L"&Virtual Mode");
	::AppendMenuW(data, MF_STRING, CMD_REFRESH, L"&Refresh (Virtual)");
	HMENU menu = ::CreateMenu();
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"&File");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"&View");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)items, L"&Items");
	::AppendMenuW(menu, MF_POPUP, (UINT_PTR)data, L"&Data");
	return menu;
}

static void Slots(UINT id, int minutes) {
	for (UINT cmd : { CMD_SLOT15, CMD_SLOT30, CMD_SLOT60 })
		::CheckMenuItem(g_menu, cmd, cmd == id ? MF_CHECKED : MF_UNCHECKED);
	g_calendar->SetSlotMinutes(minutes);
}

static void OnCommand(UINT id) {
	switch (id) {
		case CMD_EXIT: ::DestroyWindow(g_main); break;
		case CMD_DAY: ShowDays(1); break;
		case CMD_THREEDAYS: ShowDays(3); break;
		case CMD_WORKWEEK: ShowDays(5); break;
		case CMD_WEEK: ShowDays(7); break;
		case CMD_PREV:
		case CMD_NEXT: {
			SYSTEMTIME first;
			int days;
			g_calendar->GetRange(&first, &days);
			const int step = days == 5 ? 7 : days;
			g_calendar->SetDate(At((int)DayIndex(first) + (id == CMD_NEXT ? step : -step)));
			break;
		}
		case CMD_TODAY: g_calendar->GoToday(); break;
		case CMD_SLOT15: Slots(id, 15); break;
		case CMD_SLOT30: Slots(id, 30); break;
		case CMD_SLOT60: Slots(id, 60); break;
		case CMD_ZOOMIN: g_calendar->SetZoom(g_calendar->GetZoom() * 5 / 4 + 1); break;
		case CMD_ZOOMOUT: g_calendar->SetZoom(g_calendar->GetZoom() * 4 / 5); break;
		case CMD_NOW:
			if (Toggle(id))
				g_calendar->ModifyStyle(0, CLVS_NONOW);
			else
				g_calendar->ModifyStyle(CLVS_NONOW, 0);
			g_calendar->RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
			break;
#ifdef DEMO_WTLHELPER
		case CMD_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, g_main);
			::CheckMenuItem(g_menu, id, WTLHelper::IsDarkMode() ? MF_CHECKED : MF_UNCHECKED);
			break;
#endif
		case CMD_LARGEFONT:
			if (Toggle(id)) {
				NONCLIENTMETRICSW ncm{ sizeof(ncm) };
				::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(g_main));
				ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
				g_largeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
				g_calendar->SetFont(g_largeFont);
			}
			else {
				g_calendar->SetFont(nullptr);   // the default again
				::DeleteObject(g_largeFont);
				g_largeFont = nullptr;
			}
			break;
		case CMD_CLEARLOG: ::SendMessageW(g_log, LB_RESETCONTENT, 0, 0); break;
		case CMD_ADD: {
			SYSTEMTIME start, end;
			BOOL allDay;
			if (!g_calendar->GetSelectedSlots(&start, &end, &allDay)) {
				Log(L"select slots first");
				break;
			}
			wchar_t title[40];
			swprintf_s(title, L"New item %llu", g_nextId);
			if (g_calendar->AddItem(g_nextId, title, start, end, allDay ? CLVF_ALLDAY : 0)) {
				g_calendar->Select(g_nextId);
				Log(L"added %llu", g_nextId);
			}
			g_nextId++;
			break;
		}
		case CMD_DELETE:
			if (ULONGLONG sel = g_calendar->GetSelected()) {
				CLVITEM item;
				g_calendar->GetItem(sel, &item);
				if (g_calendar->RemoveItem(sel)) {
					Log(L"removed %llu", sel);
					if (item.hwnd) {
						// the control hid it: it's the app's to destroy
						if (item.hwnd == g_progress)
							g_progress = nullptr;
						::DestroyWindow(item.hwnd);
					}
				}
			}
			break;
		case CMD_DIMPAST:
			Toggle(id);
			g_calendar->RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
			break;
		case CMD_VIRTUAL:
			g_virtual = Toggle(id);
			CreateCalendar();
			Log(g_virtual ? L"virtual mode" : L"the control's own items");
			break;
		case CMD_REFRESH:
			g_calendar->RefreshItems();
			break;
	}
}

static LRESULT OnNotify(NMHDR* hdr) {
	if (!g_calendar || hdr->hwndFrom != g_calendar->m_hWnd)
		return 0;
	wchar_t a[40], b[40];
	switch (hdr->code) {
		case CLVN_SELCHANGED: Log(L"CLVN_SELCHANGED %llu", ((NMCLVITEM*)hdr)->id); break;
		case CLVN_ITEMACTIVATE: Log(L"CLVN_ITEMACTIVATE %llu", ((NMCLVITEM*)hdr)->id); break;
		case CLVN_RANGECHANGED: {
			auto r = (NMCLVRANGE*)hdr;
			ShowDate(r->start, a, _countof(a));
			ShowDate(r->end, b, _countof(b));
			Log(L"CLVN_RANGECHANGED %s to %s", a, b);
			break;
		}
		case CLVN_SLOTCHANGED: {
			auto r = (NMCLVRANGE*)hdr;
			ShowDate(r->start, a, _countof(a));
			ShowDate(r->end, b, _countof(b));
			Log(L"CLVN_SLOTCHANGED %s to %s%s", a, b, r->allDay ? L" (all day)" : L"");
			break;
		}
		case CLVN_GETITEMS: {
			auto r = (NMCLVRANGE*)hdr;
			const int before = g_calendar->GetItemCount();
			Generate(r->start, r->end);
			g_fetched += g_calendar->GetItemCount() - before;
			ShowDate(r->start, a, _countof(a));
			ShowDate(r->end, b, _countof(b));
			Log(L"CLVN_GETITEMS %s to %s: %d added (%d in all, %d released)", a, b, g_calendar->GetItemCount() - before, g_fetched, g_released);
			break;
		}
		case CLVN_RELEASEITEM:
			g_released++;
			break;
		case CLVN_CUSTOMDRAW: {
			auto cd = (NMCLVCUSTOMDRAW*)hdr;
			const bool dim = ::GetMenuState(g_menu, CMD_DIMPAST, MF_BYCOMMAND) & MF_CHECKED;
			switch (cd->nmcd.dwDrawStage) {
				case CDDS_PREPAINT:
					return dim ? CDRF_NOTIFYITEMDRAW : CDRF_DODEFAULT;
				case CDDS_ITEMPREPAINT:
					if (dim && !(cd->nmcd.uItemState & CDIS_SELECTED) && IsOver(cd->id)) {
						cd->crFill = ::GetSysColor(COLOR_BTNFACE);
						cd->crText = ::GetSysColor(COLOR_GRAYTEXT);
					}
					return CDRF_DODEFAULT;
				case CDDS_ITEMPOSTPAINT:
					if (cd->id == 40)
						DrawBurndown(cd);
					return CDRF_DODEFAULT;
			}
			break;
		}
	}
	return 0;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE: {
			g_main = hwnd;
			g_menu = BuildMenu();
			::SetMenu(hwnd, g_menu);
			g_log = ::CreateWindowExW(0, WC_LISTBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | WS_BORDER,
				0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, nullptr, nullptr);
			// this week's Monday
			::GetLocalTime(&g_monday);
			g_monday.wHour = g_monday.wMinute = g_monday.wSecond = g_monday.wMilliseconds = 0;
			g_monday = FromTicks(Ticks(g_monday) - (ULONGLONG)((g_monday.wDayOfWeek + 6) % 7) * DayTicks);
			UpdateFonts();
			CreateCalendar();
			::SetTimer(hwnd, ProgressTimer, 200, nullptr);
			return 0;
		}
		case WM_SIZE:
			Layout();
			return 0;
		case WM_SETFOCUS:
			if (g_calendar)
				g_calendar->SetFocus();
			return 0;
		case WM_COMMAND:
			// the items' windows' commands come through the control
			if (lParam && LOWORD(wParam) == IDC_JOIN && HIWORD(wParam) == BN_CLICKED)
				Log(L"Join meeting clicked");
			else if (lParam && LOWORD(wParam) == IDC_AGENDA && HIWORD(wParam) == LBN_SELCHANGE)
				Log(L"agenda: item %d", (int)::SendMessageW((HWND)lParam, LB_GETCURSEL, 0, 0));
			else if (!lParam)
				OnCommand(LOWORD(wParam));
			return 0;
		case WM_TIMER:
			if (wParam == ProgressTimer && g_progress && ::IsWindow(g_progress))
				::SendMessageW(g_progress, PBM_STEPIT, 0, 0);   // wraps around at 100
			return 0;
		case WM_NOTIFY:
			return OnNotify((NMHDR*)lParam);
		case WM_DPICHANGED: {
			auto rc = (RECT*)lParam;
			UpdateFonts();
			::SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			Layout();
			return 0;
		}
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR cmdLine, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	::InitCommonControlsEx(&icc);
	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"CalendarViewDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HWND hwnd = ::CreateWindowExW(0, L"CalendarViewDemoWnd", L"Calendar View Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760, nullptr, nullptr, hInstance, cmdLine);
	::ShowWindow(hwnd, nCmdShow);

	ACCEL accels[] = {
		{ FCONTROL | FVIRTKEY, '1', CMD_DAY }, { FCONTROL | FVIRTKEY, '3', CMD_THREEDAYS }, { FCONTROL | FVIRTKEY, '5', CMD_WORKWEEK },
		{ FCONTROL | FVIRTKEY, '7', CMD_WEEK }, { FALT | FVIRTKEY, VK_LEFT, CMD_PREV }, { FALT | FVIRTKEY, VK_RIGHT, CMD_NEXT },
		{ FCONTROL | FVIRTKEY, 'T', CMD_TODAY }, { FCONTROL | FVIRTKEY, 'N', CMD_ADD },
	};
	HACCEL accel = ::CreateAcceleratorTableW(accels, _countof(accels));
	MSG msg;
	while (::GetMessageW(&msg, nullptr, 0, 0)) {
		if (::TranslateAcceleratorW(hwnd, accel, &msg))
			continue;
		::TranslateMessage(&msg);
		::DispatchMessageW(&msg);
	}
	::DestroyAcceleratorTable(accel);

	if (g_font)
		::DeleteObject(g_font);
	if (g_largeFont)
		::DeleteObject(g_largeFont);
	_Module.Term();
	return 0;
}
