// Tooltips and accessibility (MSAA, which UI Automation clients see through its proxy). The bar's window is a tool bar
// named "Address: <the location>" whose children are, in order, the back, forward, recent locations and up buttons,
// the root chevron, each segment shown and its chevron (a push button and a drop-down button, both named by the
// segment), the history arrow and the app's buttons. While editing, the edit box is a window of its own. An open list
// (a drop-down) is a list of its rows; the row the keys select is reported focused and selected, as the control (or
// its edit box) keeps the focus. Each object wraps the system's one for its window and passes on what it doesn't answer
// itself; a name is never taken from the window's own OBJID_WINDOW object, which would ask this one again.

#include "BreadcrumbData.h"

#include <commctrl.h>
#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

namespace {
	const wchar_t* const DefaultStrings[BRCSTR_COUNT] = {
		L"Address", L"Back", L"Back to %s", L"Forward", L"Forward to %s", L"Up", L"Up to %s", L"Recent locations",
		L"Previous locations", L"More", L"Suggestions",
	};

	bool Drops(const BrcHit& part) {
		return part.part == BrcPart::Chevron || part.part == BrcPart::Overflow || part.part == BrcPart::Recent || part.part == BrcPart::History;
	}
}

class BrcAccessible : public IAccessible {
public:
	// list: the drop-down list's window, else the bar's
	BrcAccessible(CBreadcrumbCtrl* ctrl, HWND hwnd, IAccessible* proxy, bool list) : m_Ctrl(ctrl), m_Hwnd(hwnd), m_Proxy(proxy), m_List(list) {}
	virtual ~BrcAccessible() = default;

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
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (m_List)
			return Text(name, id == CHILDID_SELF ? ListName().c_str() : Row(id).text.c_str());
		if (id == CHILDID_SELF)
			return Text(name, (m_Ctrl->String(BRCSTR_ADDRESS) + L": " + Location()).c_str());
		return Text(name, m_Ctrl->PartName(Part(id)).c_str());
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		// the bar's value: the location, as edited
		return !m_List && id == CHILDID_SELF ? Text(value, Location().c_str()) : S_FALSE;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (m_List)
			return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : Row(id).separator ? ROLE_SYSTEM_SEPARATOR : ROLE_SYSTEM_LISTITEM);
		if (id == CHILDID_SELF)
			return Long(role, ROLE_SYSTEM_TOOLBAR);
		return Long(role, Drops(Part(id)) ? ROLE_SYSTEM_BUTTONDROPDOWN : ROLE_SYSTEM_PUSHBUTTON);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (m_List) {
			if (id == CHILDID_SELF)
				return Long(state, STATE_SYSTEM_FLOATING);
			if (Row(id).separator)
				return Long(state, STATE_SYSTEM_UNAVAILABLE);
			long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
			if (id - 1 == List().m_Hot)
				s |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED | STATE_SYSTEM_HOTTRACKED;
			return Long(state, s);
		}
		if (id == CHILDID_SELF)
			return Long(state, STATE_SYSTEM_FOCUSABLE | (::GetFocus() == m_Hwnd ? STATE_SYSTEM_FOCUSED : 0));
		return Long(state, PartState(Part(id)));
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
		if (m_List)
			return Row(id).separator ? S_FALSE : Text(action, L"Open");
		auto part = Part(id);
		if (!m_Ctrl->Enabled(part))
			return S_FALSE;
		if (Drops(part))
			return Text(action, m_Ctrl->m_Data->opened == part ? L"Close" : L"Open");
		return Text(action, L"Press");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		if (m_List) {
			if (Row(id).separator)
				return E_FAIL;
			::PostMessage(m_Ctrl->m_hWnd, CBreadcrumbCtrl::WM_BRC_ACCACTION, 1, id - 1);
			return S_OK;
		}
		auto part = Part(id);
		if (!m_Ctrl->Enabled(part))
			return E_FAIL;
		// later, as the user's click: the app's navigation mustn't run inside the client's call
		::PostMessage(m_Ctrl->m_hWnd, CBreadcrumbCtrl::WM_BRC_ACCACTION, 0, MAKELPARAM((WORD)part.part, (WORD)(part.index + 1)));
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (m_List)
			return List().m_Hot >= 0 ? Long(focus, List().m_Hot + 1) : S_FALSE;
		if (::GetFocus() != m_Hwnd)
			return S_FALSE;
		long id = m_Ctrl->AccChildId(m_Ctrl->FocusPart());
		return Long(focus, id);
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (m_List && List().m_Hot >= 0)
			return Long(selection, List().m_Hot + 1);
		return S_FALSE;
	}

	STDMETHODIMP accSelect(long, VARIANT) override {
		return m_Ctrl ? DISP_E_MEMBERNOTFOUND : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		RECT rc = m_List ? List().RowRect(id - 1) : m_Ctrl->PartRect(Part(id));
		::MapWindowPoints(m_Hwnd, nullptr, (POINT*)&rc, 2);
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
		long count = Count();
		long id = Id(start);
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
		else if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN || dir == NAVDIR_RIGHT)
			target = id + 1;
		else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP || dir == NAVDIR_LEFT)
			target = id - 1;
		if (target < 1 || target > count)
			return S_FALSE;
		return Long(end, target);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_Hwnd, &pt);
		RECT client;
		::GetClientRect(m_Hwnd, &client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		if (m_List) {
			int row = List().RowAt(pt);
			return Long(child, row >= 0 ? row + 1 : CHILDID_SELF);
		}
		// the edit box: its own object
		HWND under = ::ChildWindowFromPointEx(m_Hwnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (under && under != m_Hwnd)
			return m_Proxy->accHitTest(x, y, child);
		auto parts = m_Ctrl->AccParts();
		for (size_t k = 0; k < parts.size(); k++) {
			RECT rc = m_Ctrl->PartRect(parts[k]);
			if (::PtInRect(&rc, pt))
				return Long(child, (long)k + 1);
		}
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

	BrcDropList& List() const {
		return *m_Ctrl->m_Data->list;
	}

	long Count() const {
		if (m_List)
			return m_Ctrl->m_Data->list ? (long)List().m_Rows.size() : 0;
		return (long)m_Ctrl->AccParts().size();
	}

	// a child id of 1..count, CHILDID_SELF, or Invalid
	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= Count() ? child.lVal : Invalid;
	}

	const BrcRow& Row(long id) const {
		return List().m_Rows[id - 1];
	}

	BrcHit Part(long id) const {
		return m_Ctrl->AccParts()[id - 1];
	}

	// the path as the user would edit it
	std::wstring Location() const {
		auto& d = *m_Ctrl->m_Data;
		if (d.editing)
			return m_Ctrl->EditText();
		auto path = m_Ctrl->PathTo(-1);
		if (path.empty() && !d.segments.empty())
			path = d.segments.back().text;
		return path;
	}

	// the list by what dropped it
	std::wstring ListName() const {
		auto& d = *m_Ctrl->m_Data;
		switch (d.listKind) {
			case BrcList::Suggestions: return m_Ctrl->String(BRCSTR_SUGGESTIONS);
			case BrcList::History: return m_Ctrl->String(BRCSTR_HISTORY);
			default: return m_Ctrl->PartName(d.opened);
		}
	}

	long PartState(const BrcHit& part) const {
		auto& d = *m_Ctrl->m_Data;
		long s = STATE_SYSTEM_FOCUSABLE;
		if (!m_Ctrl->Enabled(part))
			s |= STATE_SYSTEM_UNAVAILABLE;
		if (d.hot == part)
			s |= STATE_SYSTEM_HOTTRACKED;
		if (d.pressed == part && d.hot == part)
			s |= STATE_SYSTEM_PRESSED;
		if (::GetFocus() == m_Hwnd && m_Ctrl->FocusPart() == part)
			s |= STATE_SYSTEM_FOCUSED;
		if (Drops(part))
			s |= STATE_SYSTEM_HASPOPUP | (d.opened == part ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED);
		if (part.part == BrcPart::Action && (d.buttons[part.index].state & BRCBS_CHECKED))
			s |= STATE_SYSTEM_CHECKED;
		return s;
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

	CBreadcrumbCtrl*          m_Ctrl;
	HWND                      m_Hwnd;
	ATL::CComPtr<IAccessible> m_Proxy;
	bool                      m_List;
	LONG                      m_Refs = 1;
};

// ---- Strings and names -----------------------------------------------------------------------------

void CBreadcrumbCtrl::SetString(UINT which, LPCWSTR text) {
	if (which < BRCSTR_COUNT)
		m_Data->strings[which] = text ? text : L"";
}

std::wstring CBreadcrumbCtrl::String(UINT which, const std::wstring& name) const {
	if (which >= BRCSTR_COUNT)
		return {};
	std::wstring s = m_Data->strings[which].empty() ? DefaultStrings[which] : m_Data->strings[which];
	if (auto i = s.find(L"%s"); i != std::wstring::npos)
		s.replace(i, 2, name);
	return s;
}

std::wstring CBreadcrumbCtrl::PartName(const BrcHit& part) const {
	auto& d = *m_Data;
	auto at = [&](int offset) {
		auto loc = d.backForward.At(offset);
		return loc ? (loc->text.empty() ? loc->path : loc->text) : std::wstring();
	};
	switch (part.part) {
		case BrcPart::Back: return d.backForward.CanGo(-1) ? String(BRCSTR_BACKTO, at(-1)) : String(BRCSTR_BACK);
		case BrcPart::Forward: return d.backForward.CanGo(1) ? String(BRCSTR_FORWARDTO, at(1)) : String(BRCSTR_FORWARD);
		case BrcPart::Up: return d.segments.size() > 1 ? String(BRCSTR_UPTO, d.segments[d.segments.size() - 2].text) : String(BRCSTR_UP);
		case BrcPart::Recent: return String(BRCSTR_RECENT);
		case BrcPart::History: return String(BRCSTR_HISTORY);
		case BrcPart::Overflow: return String(BRCSTR_MORE);
		case BrcPart::Text:
		case BrcPart::Chevron:
			return part.index >= 0 && part.index < (int)d.segments.size() ? d.segments[part.index].text : std::wstring();
		case BrcPart::Action:
			return part.index >= 0 && part.index < (int)d.buttons.size() ? d.buttons[part.index].tip : std::wstring();
		default:
			return {};
	}
}

std::vector<BrcHit> CBreadcrumbCtrl::AccParts() {
	EnsureLayout();
	auto& d = *m_Data;
	auto& l = d.layout;
	std::vector<BrcHit> parts;
	auto add = [&](BrcHit part) {
		RECT rc = PartRect(part);
		if (!::IsRectEmpty(&rc))
			parts.push_back(part);
	};
	add({ BrcPart::Back });
	add({ BrcPart::Forward });
	add({ BrcPart::Recent });
	add({ BrcPart::Up });
	if (!d.editing) {
		add({ BrcPart::Overflow });
		for (int i = l.firstVisible; i < (int)d.segments.size(); i++) {
			add({ BrcPart::Text, i });
			add({ BrcPart::Chevron, i });
		}
	}
	add({ BrcPart::History });
	for (int i = 0; i < (int)d.buttons.size(); i++)
		add({ BrcPart::Action, i });
	return parts;
}

long CBreadcrumbCtrl::AccChildId(const BrcHit& part) {
	auto parts = AccParts();
	auto it = std::find(parts.begin(), parts.end(), part);
	return it == parts.end() ? CHILDID_SELF : (long)(it - parts.begin()) + 1;
}

// ---- Tooltips ------------------------------------------------------------------------------------------

void CBreadcrumbCtrl::UpdateTip() {
	auto& d = *m_Data;
	if (!m_hTip)
		return;
	// what's under the mouse names where it goes; a segment only when its text is cut
	std::wstring text;
	RECT rc{};
	if (!(d.list && d.list->IsOpen()) && d.pressed.part == BrcPart::None) {
		switch (d.hot.part) {
			case BrcPart::Text:
				if (d.layout.truncated && d.hot.index == (int)d.segments.size() - 1)
					text = PartName(d.hot);
				break;
			case BrcPart::Chevron:
			case BrcPart::Empty:
			case BrcPart::Icon:
			case BrcPart::None:
				break;
			default:
				text = PartName(d.hot);
				break;
		}
		if (!text.empty())
			rc = PartRect(d.hot);
	}
	if (text == m_TipText && ::EqualRect(&rc, &d.tipRect))
		return;
	m_TipText = text;
	d.tipRect = rc;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	ti.rect = rc;
	::SendMessage(m_hTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	::SendMessage(m_hTip, TTM_POP, 0, 0);
	if (!text.empty()) {
		// the mouse move that made it hot reached the tooltip before the tool moved there
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		MSG msg{ m_hWnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y) };
		::SendMessage(m_hTip, TTM_RELAYEVENT, 0, (LPARAM)&msg);
	}
}

LRESULT CBreadcrumbCtrl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
	auto hdr = (NMHDR*)lParam;
	if (hdr->hwndFrom == m_hTip && hdr->code == TTN_GETDISPINFOW) {
		((NMTTDISPINFOW*)hdr)->lpszText = m_TipText.data();
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

// ---- Accessibility -------------------------------------------------------------------------------------

namespace {
	LRESULT AccObject(CBreadcrumbCtrl* ctrl, HWND hwnd, BrcAccessible*& slot, bool list, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
		if ((LONG)lParam != OBJID_CLIENT) {
			bHandled = FALSE;
			return 0;
		}
		if (!slot) {
			ATL::CComPtr<IAccessible> proxy;
			if (FAILED(::CreateStdAccessibleObject(hwnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
				return 0;
			slot = new BrcAccessible(ctrl, hwnd, proxy, list);   // the window's reference
		}
		return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(slot));
	}

	void Disconnect(BrcAccessible*& slot) {
		if (slot) {
			slot->Disconnect();
			slot->Release();
			slot = nullptr;
		}
	}
}

LRESULT CBreadcrumbCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	return AccObject(this, m_hWnd, m_Data->acc, false, wParam, lParam, bHandled);
}

LRESULT BrcDropList::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	return AccObject(m_Owner, m_hWnd, m_Acc, true, wParam, lParam, bHandled);
}

void BrcDropList::Disconnect() {
	::Disconnect(m_Acc);
}

void CBreadcrumbCtrl::NotifyAccessible(HWND hwnd, DWORD event, long child) {
	// no client has asked yet: nobody to tell
	auto& d = *m_Data;
	bool asked = hwnd == m_hWnd ? d.acc != nullptr : d.list && hwnd == d.list->m_hWnd && d.list->m_Acc != nullptr;
	if (asked && ::IsWindow(hwnd))
		::NotifyWinEvent(event, hwnd, OBJID_CLIENT, child);
}

LRESULT CBreadcrumbCtrl::OnAccAction(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (wParam == 1) {
		// a list's row: picked
		if (d.list && d.list->IsOpen() && d.list->Pickable((int)lParam))
			DropPicked(d.list->m_Rows[lParam].tag);
		return 0;
	}
	BrcHit part{ (BrcPart)LOWORD(lParam), (int)HIWORD(lParam) - 1 };
	if (!Enabled(part))
		return 0;
	SetKeyFocus(part);   // as the keys would have: there when a list closes
	if (Drops(part) && d.opened == part)
		CloseDropDown();
	else
		Activate(part);
	return 0;
}

void CBreadcrumbCtrl::DisconnectAccessible() {
	auto& d = *m_Data;
	::Disconnect(d.acc);
	if (d.list)
		d.list->Disconnect();
}
