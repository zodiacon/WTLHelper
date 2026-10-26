// Accessibility (MSAA, which UI Automation clients see through its proxy). The control is a list whose children are
// the items shown, in the order Tab goes through them (the strip's, then the days' by start): each a list item named by
// its title, described by its time; selected and focused as the control's selection is; Open (CLVN_ITEMACTIVATE) its
// default action. While slots are selected a "selected time" item follows, named by the time, its default action New
// Item (CLVN_CREATEITEM); it has the focus when the control has it. The days' window's own object is the last child,
// so the items' windows (its children) are reached through it. Actions are posted, so the app's handlers don't run
// inside the client's call. The object wraps the system's one for the window and passes on what it doesn't answer; its
// name is never taken from the window's own OBJID_WINDOW object, which would ask this one again.

#include "CalendarViewData.h"

#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

class ClvAccessible : public IAccessible {
public:
	ClvAccessible(CCalendarViewCtrl* ctrl, IAccessible* proxy) : m_Ctrl(ctrl), m_Proxy(proxy) {}
	virtual ~ClvAccessible() = default;

	// the window is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_Ctrl = nullptr;
	}

	// IUnknown
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

	// IDispatch: not needed by clients of IAccessible
	STDMETHODIMP GetTypeInfoCount(UINT* count) override {
		*count = 0;
		return S_OK;
	}
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
		return E_NOTIMPL;
	}

	// ---- IAccessible -----------------------------------------------------------------------------

	STDMETHODIMP get_accParent(IDispatch** parent) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		*count = Count();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (child.vt == VT_I4 && child.lVal == Count())
			return DaysObject(dispatch);
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (auto days = Days(child))
			return days->get_accName(Self(), name);
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->get_accName(child, name);   // the window's text, or the label before it in a dialog
		if (IsSlots(id))
			return Text(name, m_Ctrl->AccSlots().c_str());
		return Text(name, Item(id).title.c_str());
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		if (IsSlots(id))
			return Text(text, L"Selected time");
		return Text(text, m_Ctrl->AccDescription(Item(id).id).c_str());
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (auto days = Days(child))
			return days->get_accRole(Self(), role);
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (auto days = Days(child))
			return days->get_accState(Self(), state);
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		auto& d = *m_Ctrl->m_Data;
		const bool focused = ::GetFocus() == m_Ctrl->m_hWnd;
		const bool readOnly = (m_Ctrl->GetStyle() & CLVS_READONLY) != 0;
		if (id == CHILDID_SELF)
			return Long(state, STATE_SYSTEM_FOCUSABLE | (focused ? STATE_SYSTEM_FOCUSED : 0) | (readOnly ? STATE_SYSTEM_READONLY : 0));
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (IsSlots(id))
			return Long(state, s | STATE_SYSTEM_SELECTED | (focused && !d.selected ? STATE_SYSTEM_FOCUSED : 0));
		auto& item = Item(id);
		if (item.id == d.selected)
			s |= STATE_SYSTEM_SELECTED | (focused ? STATE_SYSTEM_FOCUSED : 0);
		if (item.readOnly || readOnly)
			s |= STATE_SYSTEM_READONLY;
		RECT rc;
		if (!Location(id, &rc))
			s |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
		return Long(state, s);
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		return Text(action, IsSlots(id) ? L"New Item" : L"Open");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		// later, as the user's: the app's handlers mustn't run inside the client's call
		if (IsSlots(id))
			::PostMessage(m_Ctrl->m_hWnd, CCalendarViewCtrl::WM_CLV_ACCACTION, 2, 0);
		else {
			m_Ctrl->m_Data->accItem = Item(id).id;
			::PostMessage(m_Ctrl->m_hWnd, CCalendarViewCtrl::WM_CLV_ACCACTION, 0, 0);
		}
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (::GetFocus() != m_Ctrl->m_hWnd)
			return S_FALSE;
		auto& d = *m_Ctrl->m_Data;
		return Long(focus, d.selected ? m_Ctrl->AccChildId(d.selected) : m_Ctrl->AccSlotsId());
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto& d = *m_Ctrl->m_Data;
		long id = d.selected ? m_Ctrl->AccChildId(d.selected) : m_Ctrl->AccSlotsId();
		return id != CHILDID_SELF ? Long(selection, id) : S_FALSE;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		if (flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION | SELFLAG_EXTENDSELECTION))
			return E_INVALIDARG;   // one item is selected at a time
		if (flags & SELFLAG_TAKEFOCUS)
			::SetFocus(m_Ctrl->m_hWnd);
		if ((flags & SELFLAG_TAKESELECTION) && !IsSlots(id)) {
			m_Ctrl->m_Data->accItem = Item(id).id;
			::PostMessage(m_Ctrl->m_hWnd, CCalendarViewCtrl::WM_CLV_ACCACTION, 1, 0);
		}
		return S_OK;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (auto days = Days(child))
			return days->accLocation(x, y, cx, cy, Self());
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		RECT rc{};
		Location(id, &rc);   // scrolled out of view: where it would be
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}

	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		::VariantInit(end);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		const long count = Count();
		long id = start.vt == VT_I4 && start.lVal == count ? count : Id(start);
		if (id == Invalid)
			return E_INVALIDARG;
		long target = 0;
		if (id == CHILDID_SELF) {
			if (dir == NAVDIR_FIRSTCHILD)
				target = 1;
			else if (dir == NAVDIR_LASTCHILD)
				target = count;
			else
				return m_Proxy->accNavigate(dir, start, end);   // the window's siblings
		}
		else if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN)
			target = id + 1;
		else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP)
			target = id - 1;
		if (target < 1 || target > count)
			return S_FALSE;
		if (target == count) {
			// the days' window: an object of its own
			IDispatch* dispatch = nullptr;
			if (FAILED(DaysObject(&dispatch)) || !dispatch)
				return S_FALSE;
			end->vt = VT_DISPATCH;
			end->pdispVal = dispatch;
			return S_OK;
		}
		return Long(end, target);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto& d = *m_Ctrl->m_Data;
		POINT pt{ x, y };
		::ScreenToClient(m_Ctrl->m_hWnd, &pt);
		RECT client;
		::GetClientRect(m_Ctrl->m_hWnd, &client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		// the title being edited: its own object
		HWND under = ::ChildWindowFromPointEx(m_Ctrl->m_hWnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (under && under != m_Ctrl->m_hWnd && under != m_Ctrl->m_Days->m_hWnd)
			return m_Proxy->accHitTest(x, y, child);
		if (under == m_Ctrl->m_Days->m_hWnd) {
			// an item's window: the days' window's object has it
			POINT inDays{ x, y };
			::ScreenToClient(under, &inDays);
			HWND window = ::ChildWindowFromPointEx(under, inDays, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
			if (window && window != under) {
				IDispatch* dispatch = nullptr;
				if (SUCCEEDED(DaysObject(&dispatch)) && dispatch) {
					child->vt = VT_DISPATCH;
					child->pdispVal = dispatch;
					return S_OK;
				}
			}
		}
		bool strip;
		auto hit = m_Ctrl->HitAt(pt, &strip);
		if (hit.box >= 0)
			return Long(child, m_Ctrl->AccChildId(hit.Id(d.layout, strip)));
		return Long(child, CHILDID_SELF);
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override {
		*key = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Ctrl ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Ctrl ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2;

	long Items() const {
		return (long)m_Ctrl->m_Data->accOrder.size();
	}

	// the items, the selected time (while there is one), the days' window
	long Count() const {
		return Items() + (m_Ctrl->m_Data->slots ? 1 : 0) + 1;
	}

	bool IsSlots(long id) const {
		return m_Ctrl->m_Data->slots && id == Items() + 1;
	}

	// a simple element's child id (an item's, the selected time's), CHILDID_SELF, or Invalid
	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal < Count() ? child.lVal : Invalid;
	}

	const ClvItem& Item(long id) const {
		auto& d = *m_Ctrl->m_Data;
		return d.store.Items()[d.store.Index(d.accOrder[id - 1])];
	}

	// the days' window's object, when child is its id (a client asking it as a simple element)
	ATL::CComPtr<IAccessible> Days(const VARIANT& child) const {
		ATL::CComPtr<IAccessible> days;
		if (child.vt == VT_I4 && child.lVal == Count())
			::AccessibleObjectFromWindow(m_Ctrl->m_Days->m_hWnd, (DWORD)OBJID_WINDOW, IID_PPV_ARGS(&days));
		return days;
	}

	static VARIANT Self() {
		VARIANT v;
		v.vt = VT_I4;
		v.lVal = CHILDID_SELF;
		return v;
	}

	HRESULT DaysObject(IDispatch** dispatch) const {
		return ::AccessibleObjectFromWindow(m_Ctrl->m_Days->m_hWnd, (DWORD)OBJID_WINDOW, IID_IDispatch, (void**)dispatch);
	}

	// on the screen; false if it's out of view
	bool Location(long id, RECT* out) const {
		auto& d = *m_Ctrl->m_Data;
		HWND days = m_Ctrl->m_Days->m_hWnd;
		RECT view{}, rc{};
		HWND window = m_Ctrl->m_hWnd;
		if (IsSlots(id)) {
			// the selected time's first day
			const int col = (int)std::clamp<int64_t>(ClvDayOf(d.slot.start) - d.range.first, 0, d.range.days - 1);
			if (d.slot.allDay) {
				rc = { d.layout.columns.Left(col), d.strip.top, d.layout.columns.Right(col), d.strip.bottom };
				view = d.strip;
			}
			else {
				const ClvTime dayStart = ClvTimeOf(d.range.first + col);
				const int from = (int)std::max<ClvTime>(0, d.slot.start - dayStart), to = (int)std::min<ClvTime>(ClvDay, d.slot.end - dayStart);
				rc = { d.layout.columns.Left(col), d.axis.Y(from) - d.scrollY, d.layout.columns.Right(col), d.axis.Y(to) - d.scrollY };
				window = days;
				::GetClientRect(days, &view);
			}
		}
		else {
			const auto& item = Item(id);
			if (auto box = d.layout.FindStrip(item.id)) {
				rc = box->rc;
				::OffsetRect(&rc, 0, d.strip.top + d.m.stripPad - d.stripScroll);
				view = { d.strip.left, d.strip.top + d.m.stripPad, d.strip.right, d.strip.bottom - d.m.stripPad };
			}
			else {
				for (auto& piece : d.layout.timed)
					if (piece.id == item.id) {
						rc = piece.rc;
						break;
					}
				::OffsetRect(&rc, 0, -d.scrollY);
				window = days;
				::GetClientRect(days, &view);
			}
		}
		RECT shown;
		const bool visible = ::IntersectRect(&shown, &rc, &view) != FALSE;
		::MapWindowPoints(window, nullptr, (POINT*)&rc, 2);
		*out = rc;
		return visible;
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

	CCalendarViewCtrl*        m_Ctrl;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

// ---- What the items say ------------------------------------------------------------------------------

std::wstring CCalendarViewCtrl::AccDescription(ULONGLONG id) const {
	auto item = m_Data->store.Find(id);
	if (!item)
		return {};
	auto s = ClvWhen(*item, true);
	if (item->readOnly)
		s += L", read only";
	return s;
}

std::wstring CCalendarViewCtrl::AccSlots() const {
	auto& d = *m_Data;
	if (!d.slots)
		return {};
	ClvItem span;
	span.start = d.slot.start;
	span.end = d.slot.end;
	span.allDay = d.slot.allDay;
	return ClvWhen(span, true);
}

long CCalendarViewCtrl::AccChildId(ULONGLONG id) const {
	auto& order = m_Data->accOrder;
	auto it = id ? std::find(order.begin(), order.end(), id) : order.end();
	return it == order.end() ? CHILDID_SELF : (long)(it - order.begin()) + 1;
}

long CCalendarViewCtrl::AccSlotsId() const {
	return m_Data->slots ? (long)m_Data->accOrder.size() + 1 : CHILDID_SELF;
}

// ---- The object --------------------------------------------------------------------------------------

LRESULT CCalendarViewCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	auto& d = *m_Data;
	if (!d.acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		d.acc = new ClvAccessible(this, proxy);   // the window's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(d.acc));
}

void CCalendarViewCtrl::NotifyAccessible(DWORD event, long child) {
	// no client has asked yet: nobody to tell
	if (m_Data->acc && IsWindow())
		::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, child);
}

void CCalendarViewCtrl::DisconnectAccessible() {
	auto& d = *m_Data;
	if (d.acc) {
		d.acc->Disconnect();
		d.acc->Release();
		d.acc = nullptr;
	}
}

LRESULT CCalendarViewCtrl::OnAccAction(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (wParam == 2) {
		CreateItem();
		return 0;
	}
	const ULONGLONG id = d.accItem;
	if (!d.store.Find(id))
		return 0;
	if (wParam == 1) {
		// selected, as a click would
		SetSelected(id, true);
		EnsureVisible(id);
	}
	else
		NotifyItem(CLVN_ITEMACTIVATE, id);
	return 0;
}
