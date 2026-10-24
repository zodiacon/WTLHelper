// Accessibility (MSAA, which UI Automation clients see through its proxy). The headers are on the headers' focus
// window: a group named for the accordion, its children each header (an outline button, expanded or collapsed,
// named by its title, described by its info text) followed by the header's buttons (push buttons). The bodies are
// windows of their own, with their own objects. The object wraps the system's one for the focus window and
// passes on what it doesn't answer itself.

#include "Accordion.h"
#include "AccordionModel.h"
#include "AccordionNode.h"

#include <oleacc.h>

#pragma comment(lib, "oleacc.lib")

class AcdAccessible : public IAccessible {
public:
	AcdAccessible(CAccordionCtrl* ctrl, IAccessible* proxy) : m_Ctrl(ctrl), m_Proxy(proxy) {}
	virtual ~AcdAccessible() = default;

	// the control is going: calls from clients still holding this fail from now on
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
		*count = (long)Children().size();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return ControlName(name);
		auto [section, button] = kids[id - 1];
		const auto& node = *m_Ctrl->m_Nodes[section];
		return Text(name, button < 0 ? node.Text.c_str() : node.Buttons[button].Name().c_str());
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF || kids[id - 1].second >= 0)
			return S_FALSE;
		const auto& info = m_Ctrl->m_Nodes[kids[id - 1].first]->Info;
		return info.empty() ? S_FALSE : Text(text, info.c_str());
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(role, ROLE_SYSTEM_GROUPING);
		return Long(role, kids[id - 1].second < 0 ? ROLE_SYSTEM_OUTLINEBUTTON : ROLE_SYSTEM_PUSHBUTTON);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(state, m_Ctrl->HeadersFocused() ? STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_FOCUSED : STATE_SYSTEM_FOCUSABLE);
		auto [section, button] = kids[id - 1];
		long value = 0;
		RECT rc = PartRect(section, button), client;
		m_Ctrl->GetClientRect(&client);
		RECT visible;
		if (!::IntersectRect(&visible, &rc, &client))
			value |= STATE_SYSTEM_OFFSCREEN | (rc.right <= rc.left ? STATE_SYSTEM_INVISIBLE : 0);
		if (button < 0) {
			const auto& s = m_Ctrl->m_Sections[section];
			value |= STATE_SYSTEM_FOCUSABLE;
			if (m_Ctrl->HeadersFocused() && m_Ctrl->m_Focus == (int)section)
				value |= STATE_SYSTEM_FOCUSED;
			if (s.collapsible)
				value |= s.expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
		}
		else if ((int)section == m_Ctrl->m_Hot && button == m_Ctrl->m_HotButton) {
			value |= STATE_SYSTEM_HOTTRACKED;
		}
		return Long(state, value);
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		auto [section, button] = kids[id - 1];
		if (button >= 0)
			return Text(action, L"Press");
		const auto& s = m_Ctrl->m_Sections[section];
		if (!s.collapsible)
			return S_FALSE;
		return Text(action, s.expanded ? L"Collapse" : L"Expand");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		auto [section, button] = kids[id - 1];
		// as the user would: with the notifications
		if (button >= 0) {
			NMACDBUTTON nm{};
			nm.idButton = m_Ctrl->m_Nodes[section]->Buttons[button].Id;
			nm.rcButton = PartRect(section, button);
			m_Ctrl->Notify(nm.section, ACDN_BUTTONCLICK, section);
			return S_OK;
		}
		if (!m_Ctrl->m_Sections[section].collapsible)
			return DISP_E_MEMBERNOTFOUND;
		m_Ctrl->UserToggle(section, !m_Ctrl->m_Sections[section].expanded);
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (!m_Ctrl->HeadersFocused() || m_Ctrl->m_Nodes.empty())
			return S_FALSE;
		return Long(focus, m_Ctrl->HeaderChildId(m_Ctrl->m_Focus));
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		return m_Ctrl ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid || id == CHILDID_SELF || kids[id - 1].second >= 0 || flags != SELFLAG_TAKEFOCUS)
			return E_INVALIDARG;
		m_Ctrl->FocusHeader(kids[id - 1].first);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Children();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		RECT rc;
		if (id == CHILDID_SELF)
			m_Ctrl->GetClientRect(&rc);   // the focus window has no size: the control's area
		else
			rc = PartRect(kids[id - 1].first, kids[id - 1].second);
		::MapWindowPoints(m_Ctrl->m_hWnd, nullptr, (POINT*)&rc, 2);
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
		long count = (long)Children().size();
		long id = Id(start, count);
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
		else if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN || dir == NAVDIR_RIGHT) {
			target = id + 1;
		}
		else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP || dir == NAVDIR_LEFT) {
			target = id - 1;
		}
		if (target < 1 || target > count)
			return S_FALSE;
		return Long(end, target);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_Ctrl->m_hWnd, &pt);
		RECT client;
		m_Ctrl->GetClientRect(&client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		auto kids = Children();
		// a button before its header, which it's in
		for (size_t k = kids.size(); k-- > 0;) {
			RECT rc = PartRect(kids[k].first, kids[k].second);
			if (::PtInRect(&rc, pt))
				return Long(child, (long)k + 1);
		}
		return Long(child, CHILDID_SELF);
	}

	STDMETHODIMP get_accValue(VARIANT, BSTR* value) override {
		*value = nullptr;
		return m_Ctrl ? S_FALSE : RPC_E_DISCONNECTED;
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
	STDMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* key) override {
		*key = nullptr;
		return m_Ctrl ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2;

	// the children in order: each section's header (button -1), then its buttons
	std::vector<std::pair<size_t, int>> Children() const {
		std::vector<std::pair<size_t, int>> kids;
		for (size_t i = 0; i < m_Ctrl->m_Nodes.size(); i++) {
			kids.emplace_back(i, -1);
			for (int b = 0; b < (int)m_Ctrl->m_Nodes[i]->Buttons.size(); b++)
				kids.emplace_back(i, b);
		}
		return kids;
	}

	RECT PartRect(size_t section, int button) const {
		if (button >= 0) {
			auto rects = m_Ctrl->ButtonRects(section);
			return (size_t)button < rects.size() ? rects[button] : RECT{};
		}
		return section < m_Ctrl->m_Layout->items.size() ? m_Ctrl->m_Layout->items[section].header : RECT{};
	}

	// a child id of 1..count, CHILDID_SELF, or Invalid
	static long Id(const VARIANT& child, long count) {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= count ? child.lVal : Invalid;
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

	// The accordion's name as the system gives it (a dialog's label before it), else "Sections". Asked of the
	// accordion's window, never the focus window: that one's name would come from this object again.
	HRESULT ControlName(BSTR* name) const {
		*name = nullptr;
		ATL::CComPtr<IAccessible> outer;
		if (SUCCEEDED(::AccessibleObjectFromWindow(m_Ctrl->m_hWnd, OBJID_WINDOW, IID_PPV_ARGS(&outer)))) {
			VARIANT self{};
			self.vt = VT_I4;
			self.lVal = CHILDID_SELF;
			if (SUCCEEDED(outer->get_accName(self, name)) && *name && ::SysStringLen(*name))
				return S_OK;
			::SysFreeString(*name);
			*name = nullptr;
		}
		return Text(name, L"Sections");
	}

	CAccordionCtrl*           m_Ctrl;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

// ---- The control's side -------------------------------------------------------------------------

LRESULT CAccordionCtrl::OnHeaderGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(GetHeaderFocusWindow(), OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		m_Acc = new AcdAccessible(this, proxy);   // the control's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Acc));
}

long CAccordionCtrl::HeaderChildId(int index) const {
	// the headers before it, and their buttons, come first
	long id = 0;
	for (int i = 0; i <= index && (size_t)i < m_Nodes.size(); i++)
		id += 1 + (i < index ? (long)m_Nodes[i]->Buttons.size() : 0);
	return id;
}

void CAccordionCtrl::NotifyAccessible(DWORD event, long child) {
	// no client has asked yet: nobody to tell
	if (m_Acc && ::IsWindow(GetHeaderFocusWindow()))
		::NotifyWinEvent(event, GetHeaderFocusWindow(), OBJID_CLIENT, child);
}

void CAccordionCtrl::DisconnectAccessible() {
	if (m_Acc) {
		m_Acc->Disconnect();
		m_Acc->Release();
		m_Acc = nullptr;
	}
}
