// The month view: the month's weeks as rows sharing the control's height under the weekdays' names, a cell a day with
// its number at the top and its items in lanes (bars over days, lines in a day: ClvLayoutMonth), and "+N more" where a
// day has more than its cell holds. The days' window is hidden meanwhile (items' windows with it: their captions are
// shown). "+N more" opens ClvMorePopup over the day's cell: the day's items, to select, open, or drag out to another
// day (the control's drag from then on).

#include "CalendarViewData.h"

#include <dwmapi.h>
#include <oleacc.h>

#include <algorithm>

using namespace ClvDraw;

namespace {
	std::wstring DateText(int64_t day, DWORD flags, LPCWSTR picture = nullptr) {
		auto st = ClvToSystemTime(ClvTimeOf(day));
		wchar_t text[80];
		if (!::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, flags, &st, picture, text, _countof(text), nullptr))
			return {};
		return text;
	}

	ClvTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return ClvFromSystemTime(st);
	}
}

bool CCalendarViewCtrl::IsMonth() const {
	return m_Data->view == CLVV_MONTH;
}

// ---- Layout ------------------------------------------------------------------------------------------------

void CCalendarViewCtrl::LayoutMonth(const RECT& client) {
	auto& d = *m_Data;
	d.header = { 0, d.navbar.bottom, client.right, d.navbar.bottom + d.m.headerHeight };
	d.strip = { 0, d.header.bottom, client.right, d.header.bottom };   // none
	const ClvColumns columns{ 0, std::max(0, (int)client.right), 7 };
	d.layout = ClvDayLayout{};
	d.layout.range = d.range;
	d.layout.columns = columns;
	d.layout.axis = d.axis;
	d.month = ClvLayoutMonth(d.store, d.monthFirst, d.range, columns, d.header.bottom, std::max(d.header.bottom, client.bottom), d.m);
	d.daysTop = client.bottom;
	d.accOrder = ClvItemOrder(d.month.boxes, d.store);
	if (m_Days->IsWindowVisible())
		m_Days->ShowWindow(SW_HIDE);
	PlaceWindows();   // hidden: no day piece has room for them
	Invalidate(FALSE);
}

const ClvBox* CCalendarViewCtrl::HitBox(const ClvHit& hit, bool strip) const {
	auto& d = *m_Data;
	if (hit.box < 0)
		return nullptr;
	if (IsMonth())
		return &d.month.boxes[hit.box];
	return &(strip ? d.layout.strip : d.layout.timed)[hit.box];
}

const ClvBox* CCalendarViewCtrl::BoxOf(ULONGLONG id) const {
	auto& d = *m_Data;
	if (IsMonth())
		return d.month.Find(id);
	if (auto box = d.layout.FindStrip(id))
		return box;
	for (auto& box : d.layout.timed)
		if (box.id == id)
			return &box;
	return nullptr;
}

RECT CCalendarViewCtrl::BoxRect(const ClvBox& box, bool strip) const {
	auto& d = *m_Data;
	RECT rc = box.rc;
	if (!IsMonth())
		::OffsetRect(&rc, 0, strip ? d.strip.top + d.m.stripPad - d.stripScroll : -d.scrollY);
	return rc;
}

// ---- Painting ----------------------------------------------------------------------------------------------

void CCalendarViewCtrl::PaintMonth(WTL::CDCHandle dc, const CLVCOLORS& c, bool notify) {
	auto& d = *m_Data;
	const auto& l = d.month;
	const auto& cols = l.columns;
	RECT client;
	GetClientRect(&client);
	const int pad = Scale(6);

	// the weekdays' names, whole when they fit
	dc.SelectFont(m_hFont);
	dc.SetTextColor(c.crHeaderText);
	for (int col = 0; col < 7; col++) {
		const int weekday = ClvWeekday(d.range.first + col);
		RECT cell{ cols.Left(col), d.header.top, cols.Right(col), d.header.bottom };
		wchar_t name[40] = L"";
		::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDAYNAME1 + (weekday + 6) % 7, name, _countof(name));
		SIZE size{};
		dc.GetTextExtent(name, -1, &size);
		if (size.cx > cell.right - cell.left - 2 * pad)
			::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SABBREVDAYNAME1 + (weekday + 6) % 7, name, _countof(name));
		RECT text{ cell.left + pad, cell.top, cell.right - pad, cell.bottom };
		dc.DrawText(name, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (col > 0) {
			RECT line{ cell.left, cell.top + pad, cell.left + 1, cell.bottom };
			FillSolid(dc, line, c.crGrid);
		}
	}

	// the days: the month's on the background, the others' shaded; the selected ones; their numbers, today's marked
	const int64_t today = ClvDayOf(Now());
	for (int i = 0; i < d.range.days; i++) {
		const int64_t day = d.range.first + i;
		const RECT cell = l.Cell(i);
		const bool inMonth = ClvMonthStart(day) == d.monthFirst;
		const bool selected = d.slots && d.slot.allDay && ClvTimeOf(day) >= d.slot.start && ClvTimeOf(day) < d.slot.end;
		FillSolid(dc, cell, selected ? c.crSelection : inMonth || HighContrast ? c.crBack : c.crNonWork);
		const bool isToday = day == today;
		if (isToday) {
			RECT bar{ cell.left + 1, cell.top + 1, cell.right, cell.top + 1 + Scale(3) };
			FillSolid(dc, bar, c.crToday);
		}
		RECT number{ cell.left + pad, cell.top + Scale(2), cell.right - pad, cell.top + d.m.dayNumber };
		auto text = ClvDateOf(day).day == 1 || i == 0 ? DateText(day, 0, L"MMM d") : DateText(day, 0, L"d");
		dc.SelectFont(isToday ? m_hBold : m_hFont);
		dc.SetTextColor(isToday ? c.crToday : inMonth || HighContrast ? c.crText : Blend(c.crText, c.crBack, 45));
		dc.DrawText(text.c_str(), (int)text.size(), &number, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	dc.SelectFont(m_hFont);
	for (size_t w = 0; w < l.rowTops.size(); w++) {
		RECT line{ 0, l.rowTops[w], client.right, l.rowTops[w] + 1 };
		FillSolid(dc, line, c.crGrid);
	}
	for (int col = 1; col < 7; col++) {
		RECT line{ cols.Left(col), d.header.bottom, cols.Left(col) + 1, client.bottom };
		FillSolid(dc, line, c.crGrid);
	}

	// the items, the selected one on top; then "+N more"
	for (auto& box : l.boxes)
		if (box.id != d.selected)
			PaintBox(dc, c, box, 0, true, notify);
	for (auto& box : l.boxes)
		if (box.id == d.selected)
			PaintBox(dc, c, box, 0, true, notify);
	dc.SetTextColor(HighContrast ? c.crText : c.crItem);
	for (auto& more : l.more) {
		wchar_t text[32];
		swprintf_s(text, L"+%d more", more.count);
		RECT rc = more.rc;
		rc.left += Scale(4);
		dc.DrawText(text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
}

// ---- "+N more" ---------------------------------------------------------------------------------------------

void CCalendarViewCtrl::ShowMore(int day) {
	auto& d = *m_Data;
	if (!m_More)
		m_More = std::make_unique<ClvMorePopup>(this);
	RECT cell = d.month.Cell(day);
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&cell, 2);
	m_More->Show(cell, d.range.first + day);
}

void CCalendarViewCtrl::CloseMore() {
	if (m_More && m_More->IsOpen())
		m_More->Close();
}

void CCalendarViewCtrl::DragFromMore(ULONGLONG id, LONGLONG day, POINT screen) {
	auto& d = *m_Data;
	auto item = d.store.Find(id);
	if (!item || item->readOnly || (GetStyle() & CLVS_READONLY))
		return;
	POINT pt = screen;
	ScreenToClient(&pt);
	d.dragId = id;
	d.dragOriginal = ClvSpanOf(*item);
	d.before = d.store.Save();
	d.grabDay = day;
	d.grab = ClvTimeOf(day);
	d.dragDown = d.dragMouse = pt;
	d.pending = ClvDrag::Move;
	d.drag = ClvDrag::Move;
	SetCapture();
	DragTo(pt);
}

// Accessibility: a list named by the day, its children the day's items (named by their titles, described by their
// times), the keys' row selected and focused (reported as it moves), Open their default action (posted).
class ClvMoreAccessible : public IAccessible {
public:
	ClvMoreAccessible(ClvMorePopup* popup, IAccessible* proxy) : m_Popup(popup), m_Proxy(proxy) {}
	virtual ~ClvMoreAccessible() = default;

	void Disconnect() {
		m_Popup = nullptr;
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
		return m_Popup ? m_Proxy->get_accParent(parent) : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		*count = Count();
		return S_OK;
	}
	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;
	}
	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Text(name, DateText(m_Popup->m_Day, DATE_LONGDATE).c_str());
		auto item = Item(id);
		return item ? Text(name, item->title.c_str()) : S_FALSE;
	}
	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		auto item = id == CHILDID_SELF ? nullptr : Item(id);
		return item ? Text(text, ClvWhen(*item, true).c_str()) : S_FALSE;
	}
	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM);
	}
	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(state, 0);
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (id - 1 == m_Popup->m_Sel)
			s |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED;
		return Long(state, s);
	}
	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return id == CHILDID_SELF ? S_FALSE : Text(action, L"Open");
	}
	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		::PostMessage(m_Popup->m_hWnd, ClvMorePopup::WM_MORE_ACCACTION, id - 1, 0);
		return S_OK;
	}
	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		return m_Popup->m_Sel >= 0 ? Long(focus, m_Popup->m_Sel + 1) : S_FALSE;
	}
	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		return get_accFocus(selection);
	}
	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF || (flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION | SELFLAG_EXTENDSELECTION)))
			return E_INVALIDARG;
		if (flags & SELFLAG_TAKESELECTION)
			m_Popup->SetSel(id - 1);
		return S_OK;
	}
	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		RECT rc = m_Popup->RowRect(id - 1);
		::MapWindowPoints(m_Popup->m_hWnd, nullptr, (POINT*)&rc, 2);
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}
	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		::VariantInit(end);
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		long id = Id(start);
		if (id == Invalid)
			return E_INVALIDARG;
		long target = 0;
		if (id == CHILDID_SELF) {
			if (dir == NAVDIR_FIRSTCHILD)
				target = 1;
			else if (dir == NAVDIR_LASTCHILD)
				target = Count();
			else
				return m_Proxy->accNavigate(dir, start, end);
		}
		else if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN)
			target = id + 1;
		else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP)
			target = id - 1;
		if (target < 1 || target > Count())
			return S_FALSE;
		return Long(end, target);
	}
	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_Popup->m_hWnd, &pt);
		const int row = m_Popup->RowAt(pt);
		return Long(child, row >= 0 ? row + 1 : CHILDID_SELF);
	}
	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override {
		*key = nullptr;
		if (!m_Popup)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Popup ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Popup ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2;

	long Count() const {
		return (long)m_Popup->m_Items.size();
	}

	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= Count() ? child.lVal : Invalid;
	}

	const ClvItem* Item(long id) const {
		return m_Popup->m_Owner->m_Data->store.Find(m_Popup->m_Items[id - 1]);
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

	ClvMorePopup*             m_Popup;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

ClvMorePopup::~ClvMorePopup() {
	if (IsWindow())
		DestroyWindow();
}

LRESULT ClvMorePopup::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	if (m_Acc) {
		m_Acc->Disconnect();
		m_Acc->Release();
		m_Acc = nullptr;
	}
	m_Open = false;
	bHandled = FALSE;
	return 0;
}

void ClvMorePopup::Show(const RECT& cell, int64_t day) {
	auto& o = *m_Owner;
	auto& d = *o.m_Data;
	if (!IsWindow()) {
		HWND root = ::GetAncestor(o.m_hWnd, GA_ROOT);
		Create(root, rcDefault, nullptr, WS_POPUP | WS_CLIPSIBLINGS, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
		DWORD corner = 3;   // DWMWCP_ROUNDSMALL: Windows 11 rounds the corners and draws the border
		m_Border = SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner))) ? 0 : 1;
	}
	m_Day = day;
	m_Items.clear();
	for (int index : d.store.InRange(ClvTimeOf(day), ClvTimeOf(day + 1)))
		m_Items.push_back(d.store.Items()[index].id);
	m_Hot = m_Down = -1;
	auto it = std::find(m_Items.begin(), m_Items.end(), d.selected);
	m_Sel = it == m_Items.end() ? -1 : (int)(it - m_Items.begin());
	m_Pad = o.Scale(6);
	m_Row = d.m.stripRow + o.Scale(2);
	m_Title = d.m.stripRow + o.Scale(4);

	const int width = std::max((int)(cell.right - cell.left) + o.Scale(32), o.Scale(220)) + 2 * m_Border;
	int height = 2 * m_Pad + m_Title + (int)m_Items.size() * m_Row + 2 * m_Border;
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&cell, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	height = std::min(height, (int)(work.bottom - work.top));
	int x = (cell.left + cell.right - width) / 2, y = cell.top - o.Scale(4);
	x = std::max((int)work.left, std::min(x, (int)work.right - width));
	y = std::max((int)work.top, std::min(y, (int)work.bottom - height));
	SetWindowPos(HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_Open = true;
	SetCapture();
	Invalidate(FALSE);
	::NotifyWinEvent(EVENT_OBJECT_REORDER, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
	if (m_Sel >= 0)
		::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, m_Sel + 1);
}

void ClvMorePopup::Close() {
	if (!m_Open || m_Closing)
		return;
	m_Closing = true;
	m_Open = false;
	m_Down = -1;
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	ShowWindow(SW_HIDE);
	m_Closing = false;
}

RECT ClvMorePopup::RowRect(int row) const {
	RECT client;
	GetClientRect(&client);
	const int top = m_Border + m_Pad + m_Title + row * m_Row;
	return { m_Border + m_Pad, top, client.right - m_Border - m_Pad, top + m_Row };
}

int ClvMorePopup::RowAt(POINT pt) const {
	for (int row = 0; row < (int)m_Items.size(); row++) {
		RECT rc = RowRect(row);
		if (::PtInRect(&rc, pt))
			return row;
	}
	return -1;
}

void ClvMorePopup::SetSel(int row) {
	if (row < 0 || row >= (int)m_Items.size())
		return;
	m_Sel = row;
	m_Owner->SetSelected(m_Items[row], true);
	Invalidate(FALSE);
	// always: a reader asks for the object only after an event
	::NotifyWinEvent(EVENT_OBJECT_SELECTION, m_hWnd, OBJID_CLIENT, row + 1);
	::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, row + 1);
}

bool ClvMorePopup::Key(UINT vk) {
	switch (vk) {
		case VK_ESCAPE: Close(); return true;
		case VK_UP: SetSel(m_Sel < 0 ? (int)m_Items.size() - 1 : std::max(0, m_Sel - 1)); return true;
		case VK_DOWN: SetSel(m_Sel < 0 ? 0 : std::min((int)m_Items.size() - 1, m_Sel + 1)); return true;
		case VK_HOME: SetSel(0); return true;
		case VK_END: SetSel((int)m_Items.size() - 1); return true;
		case VK_RETURN:
			if (m_Sel >= 0) {
				const auto id = m_Items[m_Sel];
				Close();
				m_Owner->NotifyItem(CLVN_ITEMACTIVATE, id);
			}
			return true;
		case VK_TAB:
			Close();
			return false;   // on to the control
	}
	return true;   // nothing else while it's open
}

LRESULT ClvMorePopup::OnMouseMove(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	const POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	if ((wParam & MK_LBUTTON) && m_Down >= 0 && (std::abs(pt.x - m_DownPt.x) >= ::GetSystemMetrics(SM_CXDRAG)
		|| std::abs(pt.y - m_DownPt.y) >= ::GetSystemMetrics(SM_CYDRAG))) {
		// dragged out: the control's drag from here
		const auto id = m_Items[m_Down];
		const int64_t day = m_Day;
		POINT screen = pt;
		ClientToScreen(&screen);
		Close();
		m_Owner->DragFromMore(id, day, screen);
		return 0;
	}
	const int hot = RowAt(pt);
	if (hot != m_Hot) {
		m_Hot = hot;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT ClvMorePopup::OnLButtonDown(UINT uMsg, WPARAM, LPARAM lParam, BOOL&) {
	const POINT pt{ (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	RECT client;
	GetClientRect(&client);
	if (!::PtInRect(&client, pt) || uMsg == WM_RBUTTONDOWN) {
		Close();   // a press elsewhere: eaten
		return 0;
	}
	const int row = RowAt(pt);
	if (row < 0)
		return 0;
	SetSel(row);
	if (uMsg == WM_LBUTTONDBLCLK) {
		const auto id = m_Items[row];
		Close();
		m_Owner->NotifyItem(CLVN_ITEMACTIVATE, id);
		return 0;
	}
	m_Down = row;
	m_DownPt = pt;
	return 0;
}

LRESULT ClvMorePopup::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	m_Down = -1;
	return 0;
}

LRESULT ClvMorePopup::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	Close();   // the capture taken: another window has the mouse now
	return 0;
}

LRESULT ClvMorePopup::OnAccAction(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if ((size_t)wParam < m_Items.size()) {
		const auto id = m_Items[wParam];
		Close();
		m_Owner->NotifyItem(CLVN_ITEMACTIVATE, id);
	}
	return 0;
}

LRESULT ClvMorePopup::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		m_Acc = new ClvMoreAccessible(this, proxy);
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Acc));
}

LRESULT ClvMorePopup::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	Paint(mdc.m_hDC);
	return 0;
}

void ClvMorePopup::Paint(WTL::CDCHandle dc) {
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
	dc.SetBkMode(TRANSPARENT);

	// the day, then its items as the strip draws them, a timed one's time before its title
	auto old = dc.SelectFont(o.m_hBold);
	dc.SetTextColor(c.crText);
	RECT title{ m_Border + m_Pad + o.Scale(4), m_Border + m_Pad, client.right - m_Border - m_Pad, m_Border + m_Pad + m_Title };
	auto day = DateText(m_Day, DATE_LONGDATE);
	dc.DrawText(day.c_str(), (int)day.size(), &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	const int bar = o.Scale(4);
	for (int row = 0; row < (int)m_Items.size(); row++) {
		auto item = d.store.Find(m_Items[row]);
		if (!item)
			continue;
		RECT rc = RowRect(row);
		::InflateRect(&rc, 0, -d.m.itemPad);
		COLORREF fill, text;
		o.ItemColors(*item, c, &fill, &text);
		const COLORREF color = HighContrast ? text : item->color != CLR_INVALID ? item->color : c.crItem;
		const bool hot = row == m_Hot && item->id != d.selected;
		RoundFill(dc, rc, fill, HighContrast ? (hot ? ::GetSysColor(COLOR_HIGHLIGHT) : text) : hot ? Blend(color, c.crText, 30) : Blend(color, c.crBack, 25), o.Scale(6));
		RECT side{ rc.left + 1, rc.top + 1, rc.left + 1 + bar, rc.bottom - 1 };
		FillSolid(dc, side, HighContrast ? text : color);
		RECT inner{ side.right + o.Scale(4), rc.top, rc.right - o.Scale(4), rc.bottom };
		if (!item->allDay && ClvFirstDay(*item) == ClvLastDay(*item)) {
			auto time = o.TimeText(item->start, item->start, false);
			dc.SelectFont(o.m_hFont);
			dc.SetTextColor(text);
			SIZE size{};
			dc.GetTextExtent(time.c_str(), (int)time.size(), &size);
			dc.DrawText(time.c_str(), (int)time.size(), &inner, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
			inner.left += size.cx + o.Scale(6);
		}
		dc.SelectFont(o.m_hBold);
		dc.SetTextColor(text);
		if (inner.right > inner.left)
			dc.DrawText(item->title.c_str(), (int)item->title.size(), &inner, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	dc.SelectFont(old);
}
