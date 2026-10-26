// Going between days: the navigation bar (CLVS_NAVBAR: Today, back, forward, and the days shown, which opens the date
// chooser), the keys' Navigate, and the date chooser itself: a month of days in a popup that doesn't take the
// activation. The control keeps the focus and passes the popup its keys; the popup has the mouse while it's open, so a
// press anywhere else closes it (and is eaten). Picking a day shows it, from its week's start in a week's view.

#include "CalendarViewData.h"

#include <dwmapi.h>
#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

using namespace ClvDraw;

namespace {
	std::wstring DateText(int64_t day, DWORD flags, LPCWSTR picture = nullptr) {
		auto st = ClvToSystemTime(ClvTimeOf(day));
		wchar_t text[80];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}

	// "October 19" as the locale says a month's day (DATE_MONTHDAY gives nothing with GetDateFormatEx)
	std::wstring MonthDay(int64_t day) {
		wchar_t picture[64] = L"";
		if (!::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SMONTHDAY, picture, _countof(picture)) || !*picture)
			wcscpy_s(picture, L"MMMM d");
		return DateText(day, 0, picture);
	}

	// a chevron centered on (cx, cy) pointing left, right or down
	enum class Dir { Left, Right, Down };

	void Chevron(WTL::CDCHandle dc, int cx, int cy, int size, Dir dir, COLORREF color) {
		const int h = size / 2, q = size / 4;
		POINT pts[3];
		switch (dir) {
			case Dir::Left: pts[0] = { cx + q, cy - h }; pts[1] = { cx - q, cy }; pts[2] = { cx + q + 1, cy + h + 1 }; break;
			case Dir::Right: pts[0] = { cx - q, cy - h }; pts[1] = { cx + q, cy }; pts[2] = { cx - q - 1, cy + h + 1 }; break;
			case Dir::Down: pts[0] = { cx - h, cy - q }; pts[1] = { cx, cy + q }; pts[2] = { cx + h + 1, cy - q - 1 }; break;
		}
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, size / 6), color);
		auto old = dc.SelectPen(pen);
		dc.Polyline(pts, 3);
		dc.SelectPen(old);
	}

	// a button's face: hot or pressed shaded (in high contrast framed in the highlight color)
	void Face(WTL::CDCHandle dc, const RECT& rc, const CLVCOLORS& c, bool hot, bool pressed, bool outlined, int radius) {
		if (HighContrast) {
			if (hot || pressed || outlined)
				RoundFill(dc, rc, c.crHeaderBack, hot || pressed ? ::GetSysColor(COLOR_HIGHLIGHT) : c.crText, radius);
			return;
		}
		const COLORREF back = c.crHeaderBack;
		if (pressed || hot || outlined)
			RoundFill(dc, rc, pressed ? Blend(back, c.crText, 16) : hot ? Blend(back, c.crText, 9) : back,
				outlined ? Blend(back, c.crText, 28) : pressed ? Blend(back, c.crText, 16) : Blend(back, c.crText, 9), radius);
	}
}

// ---- Going between days -----------------------------------------------------------------------------

void CCalendarViewCtrl::SetFirstDayOfWeek(int day) {
	m_Data->firstWeekday = day < 0 ? -1 : day % 7;
}

int CCalendarViewCtrl::GetFirstDayOfWeek() const {
	if (m_Data->firstWeekday >= 0)
		return m_Data->firstWeekday;
	DWORD value = 0;
	if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, (LPWSTR)&value, 2))
		return (int)(value + 1) % 7;   // 0 Monday there
	return 0;
}

void CCalendarViewCtrl::Navigate(int steps) {
	auto& d = *m_Data;
	if (!steps)
		return;
	EndRename(true);
	if (IsMonth()) {
		d.monthFirst = ClvAddMonths(d.monthFirst, steps);
		d.range = ClvMonthRange(d.monthFirst, GetFirstDayOfWeek());
	}
	else
		d.range.first += (int64_t)steps * ClvViewStep(d.range.days);
	d.cursor = -1;
	RangeChanged(true);
	Invalidate(FALSE);
}

void CCalendarViewCtrl::PickDate(LONGLONG day) {
	auto& d = *m_Data;
	EndRename(true);
	if (IsMonth()) {
		if (ClvMonthStart(day) != d.monthFirst) {
			d.monthFirst = ClvMonthStart(day);
			d.range = ClvMonthRange(day, GetFirstDayOfWeek());
			d.cursor = -1;
			RangeChanged(true);
		}
		Invalidate(FALSE);
		return;
	}
	const int64_t first = ClvFirstShown(day, d.range.days, GetFirstDayOfWeek());
	if (first != d.range.first) {
		d.range.first = first;
		d.cursor = -1;
		RangeChanged(true);
	}
	Invalidate(FALSE);
}

bool CCalendarViewCtrl::PickerOpen() const {
	return m_Picker && m_Picker->IsOpen();
}

void CCalendarViewCtrl::ClosePicker() {
	if (PickerOpen())
		m_Picker->Close();
}

void CCalendarViewCtrl::ShowDatePicker() {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	if (PickerOpen()) {
		ClosePicker();
		return;
	}
	EndRename(true);
	CancelDrag();
	if (!m_Picker)
		m_Picker = std::make_unique<ClvPicker>(this);
	// under the bar's title, else under the days' names
	RECT under = !::IsRectEmpty(&d.navbar) ? d.nav[ClvNavTitle] : RECT{ d.layout.columns.left, d.header.top, d.layout.columns.left + 1, d.header.bottom };
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&under, 2);
	m_Picker->Show(under, IsMonth() ? d.monthFirst : d.range.first);
	InvalidateRect(&d.navbar, FALSE);
}

// ---- The bar --------------------------------------------------------------------------------------------

std::wstring CCalendarViewCtrl::RangeTitle() const {
	auto& d = *m_Data;
	if (IsMonth())
		return DateText(d.monthFirst, DATE_YEARMONTH);
	if (d.range.days == 1)
		return DateText(d.range.first, DATE_LONGDATE);
	const int64_t first = d.range.first, last = d.range.Last();
	const auto a = ClvDateOf(first), b = ClvDateOf(last);
	const std::wstring year = DateText(last, 0, L"yyyy");
	if (a.year != b.year)
		return MonthDay(first) + L", " + DateText(first, 0, L"yyyy") + L" \x2013 " + MonthDay(last) + L", " + year;
	return MonthDay(first) + L" \x2013 " + MonthDay(last) + L", " + year;
}

std::wstring CCalendarViewCtrl::NavName(int part) const {
	const int step = ClvViewStep(m_Data->range.days);
	const std::wstring span = IsMonth() ? L"month" : step == 7 ? L"week" : step == 1 ? L"day" : std::to_wstring(step) + L" days";
	switch (part) {
		case ClvNavToday: return L"Today";
		case ClvNavPrev: return L"Previous " + span;
		case ClvNavNext: return L"Next " + span;
		case ClvNavTitle: return RangeTitle();
	}
	return {};
}

std::wstring CCalendarViewCtrl::NavTip(int part) const {
	switch (part) {
		case ClvNavToday: return NavName(part) + L" (Ctrl+Home)";
		case ClvNavPrev: return NavName(part) + L" (Ctrl+Page Up)";
		case ClvNavNext: return NavName(part) + L" (Ctrl+Page Down)";
		case ClvNavTitle: return L"Choose a date (Ctrl+G)";
	}
	return {};
}

void CCalendarViewCtrl::LayoutNavBar(int width) {
	auto& d = *m_Data;
	for (auto& rc : d.nav)
		rc = {};
	if (!(GetStyle() & CLVS_NAVBAR)) {
		d.navbar = {};
		return;
	}
	const int height = d.m.headerHeight + Scale(10);
	d.navbar = { 0, 0, width, height };
	const int pad = Scale(5), gap = Scale(6);
	const int top = pad, bottom = height - pad, button = bottom - top;
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	SIZE today{};
	dc.GetTextExtent(L"Today", 5, &today);
	int x = Scale(8);
	d.nav[ClvNavToday] = { x, top, x + today.cx + Scale(20), bottom };
	x = d.nav[ClvNavToday].right + gap;
	d.nav[ClvNavPrev] = { x, top, x + button, bottom };
	x += button;
	d.nav[ClvNavNext] = { x, top, x + button, bottom };
	x += button + gap;
	dc.SelectFont(m_hBold);
	auto title = RangeTitle();
	SIZE size{};
	dc.GetTextExtent(title.c_str(), (int)title.size(), &size);
	dc.SelectFont(old);
	const int right = std::min(width - Scale(8), x + (int)size.cx + Scale(16) + Scale(16));
	d.nav[ClvNavTitle] = { x, top, std::max(x, right), bottom };
}

int CCalendarViewCtrl::NavPartAt(POINT pt) const {
	auto& d = *m_Data;
	if (!::PtInRect(&d.navbar, pt))
		return -1;
	for (int i = 0; i < ClvNavCount; i++)
		if (::PtInRect(&d.nav[i], pt))
			return i;
	return -1;
}

bool CCalendarViewCtrl::NavPress(POINT pt) {
	auto& d = *m_Data;
	if (!::PtInRect(&d.navbar, pt))
		return false;
	const int part = NavPartAt(pt);
	if (part == ClvNavTitle) {
		ShowDatePicker();   // opens on the press, as a drop-down does
		return true;
	}
	if (part >= 0) {
		d.navPressed = part;
		SetCapture();
		InvalidateRect(&d.navbar, FALSE);
	}
	return true;
}

void CCalendarViewCtrl::NavRelease(POINT pt) {
	auto& d = *m_Data;
	const int part = d.navPressed;
	d.navPressed = -1;
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	InvalidateRect(&d.navbar, FALSE);
	if (part >= 0 && NavPartAt(pt) == part)
		NavAction(part);
}

void CCalendarViewCtrl::NavHot(POINT pt) {
	auto& d = *m_Data;
	const int hot = NavPartAt(pt);
	if (hot != d.navHot) {
		d.navHot = hot;
		InvalidateRect(&d.navbar, FALSE);
	}
	if (hot >= 0 && !d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
}

void CCalendarViewCtrl::NavAction(int part) {
	switch (part) {
		case ClvNavToday: GoToday(); break;
		case ClvNavPrev: Navigate(-1); break;
		case ClvNavNext: Navigate(1); break;
		case ClvNavTitle: ShowDatePicker(); break;
	}
}

LRESULT CCalendarViewCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	d.tracking = false;
	if (d.navHot >= 0) {
		d.navHot = -1;
		InvalidateRect(&d.navbar, FALSE);
	}
	return 0;
}

LRESULT CCalendarViewCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == GWL_STYLE)
		UpdateLayout();   // the bar came or went
	bHandled = FALSE;
	return 0;
}

void CCalendarViewCtrl::PaintNavBar(WTL::CDCHandle dc, const CLVCOLORS& c) {
	auto& d = *m_Data;
	if (::IsRectEmpty(&d.navbar))
		return;
	FillSolid(dc, d.navbar, c.crHeaderBack);
	RECT line{ d.navbar.left, d.navbar.bottom - 1, d.navbar.right, d.navbar.bottom };
	FillSolid(dc, line, c.crGrid);
	const int radius = Scale(6);
	auto face = [&](int part, bool outlined) {
		Face(dc, d.nav[part], c, d.navHot == part, d.navPressed == part || (part == ClvNavTitle && PickerOpen()), outlined, radius);
	};
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(c.crHeaderText);

	face(ClvNavToday, true);
	auto old = dc.SelectFont(m_hFont);
	RECT rc = d.nav[ClvNavToday];
	dc.DrawText(L"Today", 5, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

	const int size = Scale(10);
	for (int part : { (int)ClvNavPrev, (int)ClvNavNext }) {
		face(part, false);
		const RECT& r = d.nav[part];
		Chevron(dc, (r.left + r.right) / 2, (r.top + r.bottom) / 2, size, part == ClvNavPrev ? Dir::Left : Dir::Right, c.crHeaderText);
	}

	face(ClvNavTitle, false);
	rc = d.nav[ClvNavTitle];
	rc.left += Scale(8);
	rc.right -= Scale(8) + Scale(14);
	dc.SelectFont(m_hBold);
	auto title = RangeTitle();
	dc.DrawText(title.c_str(), (int)title.size(), &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	Chevron(dc, d.nav[ClvNavTitle].right - Scale(8) - Scale(7), (rc.top + rc.bottom) / 2, Scale(8), Dir::Down, c.crHeaderText);
	dc.SelectFont(old);
}

// ---- The date chooser -----------------------------------------------------------------------------------

// Accessibility: a list of the month's days (6 weeks of them), each a list item named by its date, the focus day
// selected and focused, Pick its default action; then the previous and next months' buttons. The focus day's moves are
// reported always (a reader asks only after an event); actions are posted.
class ClvPickerAccessible : public IAccessible {
public:
	ClvPickerAccessible(ClvPicker* picker, IAccessible* proxy) : m_Picker(picker), m_Proxy(proxy) {}
	virtual ~ClvPickerAccessible() = default;

	void Disconnect() {
		m_Picker = nullptr;
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
			*ppv = static_cast<IAccessible*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override {
		return ::InterlockedIncrement(&m_Refs);
	}
	STDMETHODIMP_(ULONG) Release() override {
		auto refs = ::InterlockedDecrement(&m_Refs);
		if (refs == 0)
			delete this;
		return refs;
	}
	STDMETHODIMP GetTypeInfoCount(UINT* count) override {
		*count = 0;
		return S_OK;
	}
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }

	STDMETHODIMP get_accParent(IDispatch** parent) override {
		return m_Picker ? m_Proxy->get_accParent(parent) : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		*count = Count;
		return S_OK;
	}
	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;
	}
	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Text(name, DateText(m_Picker->m_Layout.month, DATE_YEARMONTH).c_str());
		if (id == PrevId)
			return Text(name, L"Previous month");
		if (id == NextId)
			return Text(name, L"Next month");
		return Text(name, m_Picker->DayName(Day(id)).c_str());
	}
	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id >= 1 && id <= Days) {
			auto& d = *m_Picker->m_Owner->m_Data;
			const int64_t day = Day(id);
			std::wstring s;
			if (day == ClvDayOf(Now()))
				s = L"Today";
			if (d.range.Contains(day))
				s += s.empty() ? L"Shown" : L", shown";
			if (!s.empty())
				return Text(text, s.c_str());
		}
		return S_FALSE;
	}
	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : id > Days ? ROLE_SYSTEM_PUSHBUTTON : ROLE_SYSTEM_LISTITEM);
	}
	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF || id > Days)
			return Long(state, 0);
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (Day(id) == m_Picker->m_Focus)
			s |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED;
		return Long(state, s);
	}
	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		return Text(action, id > Days ? L"Press" : L"Pick");
	}
	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		::PostMessage(m_Picker->m_hWnd, ClvPicker::WM_PICKER_ACCACTION, id, 0);
		return S_OK;
	}
	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		return Long(focus, (long)(m_Picker->m_Focus - m_Picker->m_Layout.first) + 1);
	}
	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		return get_accFocus(selection);
	}
	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id < 1 || id > Days || (flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION | SELFLAG_EXTENDSELECTION)))
			return E_INVALIDARG;
		if (flags & SELFLAG_TAKESELECTION)
			m_Picker->SetFocusDay(Day(id));
		return S_OK;
	}
	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		auto& l = m_Picker->m_Layout;
		RECT rc = id == PrevId ? l.prev : id == NextId ? l.next : l.DayRect(id - 1);
		::OffsetRect(&rc, m_Picker->m_Border, m_Picker->m_Border);
		::MapWindowPoints(m_Picker->m_hWnd, nullptr, (POINT*)&rc, 2);
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}
	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		::VariantInit(end);
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		long id = Id(start);
		if (id == Invalid)
			return E_INVALIDARG;
		long target = 0;
		if (id == CHILDID_SELF) {
			if (dir == NAVDIR_FIRSTCHILD)
				target = 1;
			else if (dir == NAVDIR_LASTCHILD)
				target = Count;
			else
				return m_Proxy->accNavigate(dir, start, end);
		}
		else if (dir == NAVDIR_NEXT)
			target = id + 1;
		else if (dir == NAVDIR_PREVIOUS)
			target = id - 1;
		else if (id <= Days && (dir == NAVDIR_DOWN || dir == NAVDIR_UP))
			target = id + (dir == NAVDIR_DOWN ? 7 : -7);
		else if (id <= Days && (dir == NAVDIR_RIGHT || dir == NAVDIR_LEFT))
			target = id + (dir == NAVDIR_RIGHT ? 1 : -1);
		if (target < 1 || target > Count)
			return S_FALSE;
		return Long(end, target);
	}
	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_Picker->m_hWnd, &pt);
		pt.x -= m_Picker->m_Border;
		pt.y -= m_Picker->m_Border;
		int64_t day = 0;
		switch (m_Picker->m_Layout.Hit(pt, &day)) {
			case ClvMiniPart::Prev: return Long(child, PrevId);
			case ClvMiniPart::Next: return Long(child, NextId);
			case ClvMiniPart::Day: return Long(child, (long)(day - m_Picker->m_Layout.first) + 1);
			default: return Long(child, CHILDID_SELF);
		}
	}
	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override {
		*key = nullptr;
		if (!m_Picker)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Picker ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Picker ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2, Days = 42, PrevId = 43, NextId = 44, Count = 44;

	static ClvTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return ClvFromSystemTime(st);
	}

	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= Count ? child.lVal : Invalid;
	}

	int64_t Day(long id) const {
		return m_Picker->m_Layout.first + id - 1;
	}

	static HRESULT Text(BSTR* out, LPCWSTR text) {
		*out = ::SysAllocString(text);
		return *out ? S_OK : E_OUTOFMEMORY;
	}

	static HRESULT Long(VARIANT* out, long value) {
		out->vt = VT_I4;
		out->lVal = value;
		return S_OK;
	}

	ClvPicker*                m_Picker;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

ClvPicker::~ClvPicker() {
	if (IsWindow())
		DestroyWindow();
}

LRESULT ClvPicker::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	if (m_Acc) {
		m_Acc->Disconnect();
		m_Acc->Release();
		m_Acc = nullptr;
	}
	m_Open = false;
	bHandled = FALSE;
	return 0;
}

void ClvPicker::Relayout(int64_t day) {
	auto& o = *m_Owner;
	m_Layout = ClvLayoutMini(day, o.GetFirstDayOfWeek(), ClvMiniMetrics{}.Scaled(o.m_Dpi));
	if (IsWindow()) {
		Invalidate(FALSE);
		::NotifyWinEvent(EVENT_OBJECT_REORDER, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
		::NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
	}
}

void ClvPicker::Show(const RECT& under, int64_t day) {
	auto& o = *m_Owner;
	if (!IsWindow()) {
		HWND root = ::GetAncestor(o.m_hWnd, GA_ROOT);
		Create(root, rcDefault, nullptr, WS_POPUP | WS_CLIPSIBLINGS, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
		// Windows 11 rounds the corners and draws the border
		DWORD corner = 3;   // DWMWCP_ROUNDSMALL
		m_Border = SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner))) ? 0 : 1;
	}
	m_Focus = day;
	m_Hot = INT64_MIN;
	m_HotPart = ClvMiniPart::None;
	m_Keys = false;
	Relayout(day);
	const int width = m_Layout.size.cx + 2 * m_Border, height = m_Layout.size.cy + 2 * m_Border;

	// under the anchor, or above it if there's no room
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&under, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	int x = under.left, y = under.bottom + o.Scale(2);
	if (y + height > work.bottom && under.top - o.Scale(2) - height >= work.top)
		y = under.top - o.Scale(2) - height;
	x = std::max((int)work.left, std::min(x, (int)work.right - width));
	SetWindowPos(HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_Open = true;
	SetCapture();
	Invalidate(FALSE);
	NotifyFocus();
}

void ClvPicker::Close() {
	if (!m_Open || m_Closing)
		return;
	m_Closing = true;
	m_Open = false;
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	ShowWindow(SW_HIDE);
	m_Closing = false;
	auto& d = *m_Owner->m_Data;
	m_Owner->InvalidateRect(&d.navbar, FALSE);
}

void ClvPicker::SetFocusDay(int64_t day) {
	const int64_t month = m_Layout.month;
	m_Focus = day;
	if (day < m_Layout.first || day >= m_Layout.first + 42 || ClvMonthStart(day) != month)
		Relayout(day);
	Invalidate(FALSE);
	NotifyFocus();
}

void ClvPicker::NotifyFocus() {
	// always: a reader asks for the object only after an event
	const long child = (long)(m_Focus - m_Layout.first) + 1;
	::NotifyWinEvent(EVENT_OBJECT_SELECTION, m_hWnd, OBJID_CLIENT, child);
	::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, child);
}

std::wstring ClvPicker::DayName(int64_t day) const {
	return DateText(day, DATE_LONGDATE);
}

bool ClvPicker::Key(UINT vk) {
	switch (vk) {
		case VK_ESCAPE: Close(); return true;
		case VK_RETURN: case VK_SPACE: {
			const int64_t day = m_Focus;
			Close();
			m_Owner->PickDate(day);
			return true;
		}
		case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
			m_Keys = true;
			SetFocusDay(m_Focus + (vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : vk == VK_UP ? -7 : 7));
			return true;
		case VK_PRIOR: case VK_NEXT:
			m_Keys = true;
			SetFocusDay(ClvAddMonths(m_Focus, vk == VK_NEXT ? 1 : -1));
			return true;
		case VK_HOME: case VK_END: {
			// the month's first or last day
			m_Keys = true;
			auto date = ClvDateOf(m_Layout.month);
			SetFocusDay(vk == VK_HOME ? m_Layout.month : m_Layout.month + ClvDaysInMonth(date.year, date.month) - 1);
			return true;
		}
		case VK_TAB:
			Close();
			return false;   // on to the control
	}
	return true;   // nothing else while it's open
}

void ClvPicker::Wheel(int delta) {
	SetFocusDay(ClvAddMonths(m_Focus, delta > 0 ? -1 : 1));
}

LRESULT ClvPicker::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (int)(short)LOWORD(lParam) - m_Border, (int)(short)HIWORD(lParam) - m_Border };
	int64_t day = INT64_MIN;
	auto part = m_Layout.Hit(pt, &day);
	if (part != ClvMiniPart::Day)
		day = INT64_MIN;
	if (part != m_HotPart || day != m_Hot) {
		m_HotPart = part;
		m_Hot = day;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT ClvPicker::OnLButtonDown(UINT uMsg, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	RECT client;
	GetClientRect(&client);
	if (!::PtInRect(&client, pt) || uMsg == WM_RBUTTONDOWN) {
		Close();   // a press elsewhere: eaten
		return 0;
	}
	pt.x -= m_Border;
	pt.y -= m_Border;
	int64_t day = 0;
	switch (m_Layout.Hit(pt, &day)) {
		case ClvMiniPart::Prev: SetFocusDay(ClvAddMonths(m_Focus, -1)); break;
		case ClvMiniPart::Next: SetFocusDay(ClvAddMonths(m_Focus, 1)); break;
		case ClvMiniPart::Title: SetFocusDay(ClvDayOf(ClvFromSystemTime([] { SYSTEMTIME st; ::GetLocalTime(&st); return st; }()))); break;
		case ClvMiniPart::Day:
			Close();
			m_Owner->PickDate(day);
			break;
		default: break;
	}
	return 0;
}

LRESULT ClvPicker::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	Close();   // the capture taken: another window has the mouse now
	return 0;
}

LRESULT ClvPicker::OnAccAction(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (wParam == 43)
		SetFocusDay(ClvAddMonths(m_Focus, -1));
	else if (wParam == 44)
		SetFocusDay(ClvAddMonths(m_Focus, 1));
	else if (wParam >= 1 && wParam <= 42) {
		const int64_t day = m_Layout.first + (int64_t)wParam - 1;
		Close();
		m_Owner->PickDate(day);
	}
	return 0;
}

LRESULT ClvPicker::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		m_Acc = new ClvPickerAccessible(this, proxy);
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Acc));
}

LRESULT ClvPicker::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC);
	return 0;
}

void ClvPicker::Paint(WTL::CDCHandle dc) {
	auto& o = *m_Owner;
	auto& d = *o.m_Data;
	HighContrast = o.IsHighContrast();
	const auto c = o.GetColors();
	RECT client;
	GetClientRect(&client);
	FillSolid(dc, client, c.crBack);
	if (m_Border) {
		WTL::CBrush frame;
		frame.CreateSolidBrush(c.crGrid);
		dc.FrameRect(&client, frame);
	}
	dc.OffsetViewportOrg(m_Border, m_Border);   // the memory DC's origin is the paint rectangle's
	dc.SetBkMode(TRANSPARENT);
	auto& l = m_Layout;
	const int radius = o.Scale(6);

	// the month between its buttons
	auto old = dc.SelectFont(o.m_hBold);
	dc.SetTextColor(c.crText);
	auto month = DateText(l.month, DATE_YEARMONTH);
	RECT rc = l.title;
	dc.DrawText(month.c_str(), (int)month.size(), &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	for (auto part : { ClvMiniPart::Prev, ClvMiniPart::Next }) {
		const RECT& r = part == ClvMiniPart::Prev ? l.prev : l.next;
		RECT face = r;
		::InflateRect(&face, -o.Scale(3), -o.Scale(3));
		if (m_HotPart == part)
			RoundFill(dc, face, Shade(c.crBack, c.crText, 9), HighContrast ? ::GetSysColor(COLOR_HIGHLIGHT) : Shade(c.crBack, c.crText, 9), radius);
		Chevron(dc, (r.left + r.right) / 2, (r.top + r.bottom) / 2, o.Scale(9), part == ClvMiniPart::Prev ? Dir::Left : Dir::Right, c.crText);
	}

	// the weekdays' initials
	dc.SelectFont(o.m_hFont);
	dc.SetTextColor(HighContrast ? c.crText : Blend(c.crText, c.crBack, 40));
	for (int col = 0; col < 7; col++) {
		wchar_t name[16] = L"";
		::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTESTDAYNAME1 + (l.Weekday(col) + 6) % 7, name, _countof(name));
		RECT cell{ l.weekdays.left + col * l.cell, l.weekdays.top, l.weekdays.left + (col + 1) * l.cell, l.weekdays.bottom };
		dc.DrawText(name, -1, &cell, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	}

	// the days: those shown in the view filled, today ringed, the mouse's shaded, the keys' framed
	SYSTEMTIME now;
	::GetLocalTime(&now);
	const int64_t today = ClvDayOf(ClvFromSystemTime(now));
	for (int i = 0; i < 42; i++) {
		const int64_t day = l.first + i;
		RECT cell = l.DayRect(i);
		RECT face = cell;
		::InflateRect(&face, -o.Scale(2), -o.Scale(2));
		const bool shown = o.IsMonth() ? ClvMonthStart(day) == d.monthFirst : d.range.Contains(day), inMonth = ClvMonthStart(day) == l.month;
		COLORREF text = inMonth ? c.crText : (HighContrast ? ::GetSysColor(COLOR_GRAYTEXT) : Blend(c.crText, c.crBack, 55));
		if (shown) {
			RoundFill(dc, face, c.crSelection, c.crSelection, radius);
			if (HighContrast)
				text = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
		}
		else if (day == m_Hot)
			RoundFill(dc, face, Shade(c.crBack, c.crText, 9), HighContrast ? ::GetSysColor(COLOR_HIGHLIGHT) : Shade(c.crBack, c.crText, 9), radius);
		if (day == today) {
			WTL::CPen pen;
			pen.CreatePen(PS_SOLID, std::max(2, o.Scale(2)), c.crToday);
			auto oldPen = dc.SelectPen(pen);
			auto oldBrush = dc.SelectStockBrush(NULL_BRUSH);
			dc.Ellipse(&face);
			dc.SelectBrush(oldBrush);
			dc.SelectPen(oldPen);
		}
		if (m_Keys && day == m_Focus) {
			WTL::CBrush frame;
			frame.CreateSolidBrush(c.crText);
			dc.FrameRect(&face, frame);
		}
		dc.SelectFont(day == today ? o.m_hBold : o.m_hFont);
		dc.SetTextColor(text);
		wchar_t number[8];
		swprintf_s(number, L"%d", ClvDateOf(day).day);
		dc.DrawText(number, -1, &cell, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	}
	dc.SelectFont(old);
	dc.OffsetViewportOrg(-m_Border, -m_Border);
}
