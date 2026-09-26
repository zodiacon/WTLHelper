#include "DockAccessible.h"
#include <algorithm>

#pragma comment(lib, "oleacc.lib")

namespace WTLDock {

namespace {

HRESULT ReturnString(const std::wstring& text, BSTR* out) {
	if (!out)
		return E_POINTER;
	*out = nullptr;
	if (text.empty())
		return S_FALSE;
	*out = ::SysAllocStringLen(text.c_str(), (UINT)text.size());
	return *out ? S_OK : E_OUTOFMEMORY;
}

void Empty(VARIANT* v) {
	if (v)
		::VariantInit(v);
}

VARIANT Self() {
	VARIANT v;
	::VariantInit(&v);
	v.vt = VT_I4;
	v.lVal = CHILDID_SELF;
	return v;
}

CComPtr<IAccessible> AccessibleOf(HWND window) {
	CComPtr<IAccessible> acc;
	if (FAILED(::AccessibleObjectFromWindow(window, OBJID_WINDOW, IID_IAccessible, (void**)&acc)))
		acc.Release();
	return acc;
}

// the type information of IAccessible, for IDispatch (kept for the life of the process)
ITypeInfo* AccessibleTypeInfo() {
	static ITypeInfo* info = [] {
		ITypeInfo* result = nullptr;
		CComPtr<ITypeLib> lib;
		if (SUCCEEDED(::LoadRegTypeLib(LIBID_Accessibility, 1, 1, 0, &lib)))
			lib->GetTypeInfoOfGuid(IID_IAccessible, &result);
		return result;
	}();
	return info;
}

}

DockAccessible::DockAccessible(IDockAccessibleOwner* owner, HWND window) : m_Owner(owner), m_Window(window) {
	::CreateStdAccessibleObject(window, OBJID_CLIENT, IID_IAccessible, (void**)&m_Std);
}

DockAccessible* DockAccessible::Create(IDockAccessibleOwner* owner) {
	return owner ? new DockAccessible(owner, owner->AccWindow()) : nullptr;
}

void DockAccessible::Detach() {
	m_Owner = nullptr;
}

bool DockAccessible::Alive() const {
	return m_Owner && m_Window && ::IsWindow(m_Window);
}

DockAccessible::Snapshot DockAccessible::Take() const {
	Snapshot s;
	if (Alive()) {
		s.Elements = m_Owner->AccElements();
		s.Windows = m_Owner->AccChildWindows();
	}
	return s;
}

DockAccessible::Kind DockAccessible::Resolve(const VARIANT& child, const Snapshot& s, size_t& index) {
	if (child.vt != VT_I4)
		return Kind::Invalid;
	const LONG id = child.lVal;
	if (id == CHILDID_SELF)
		return Kind::Self;
	if (id < 0)
		return Kind::Invalid;
	if ((size_t)id <= s.Elements.size()) {
		index = (size_t)id - 1;
		return Kind::Element;
	}
	if ((size_t)id <= s.Elements.size() + s.Windows.size()) {
		index = (size_t)id - 1 - s.Elements.size();
		return Kind::Window;
	}
	return Kind::Invalid;
}

HRESULT DockAccessible::ChildVariant(const Snapshot& s, size_t child, VARIANT* out) {
	Empty(out);
	if (child < s.Elements.size()) {
		out->vt = VT_I4;
		out->lVal = (LONG)child + 1;
		return S_OK;
	}
	child -= s.Elements.size();
	if (child >= s.Windows.size())
		return S_FALSE;
	auto acc = AccessibleOf(s.Windows[child]);
	if (!acc)
		return S_FALSE;
	out->vt = VT_DISPATCH;
	out->pdispVal = acc.Detach();
	return S_OK;
}

//
// IUnknown and IDispatch
//

STDMETHODIMP DockAccessible::QueryInterface(REFIID riid, void** ppv) {
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

STDMETHODIMP_(ULONG) DockAccessible::AddRef() {
	return (ULONG)::InterlockedIncrement(&m_Ref);
}

STDMETHODIMP_(ULONG) DockAccessible::Release() {
	const LONG count = ::InterlockedDecrement(&m_Ref);
	if (count == 0)
		delete this;
	return (ULONG)count;
}

STDMETHODIMP DockAccessible::GetTypeInfoCount(UINT* count) {
	if (!count)
		return E_POINTER;
	*count = AccessibleTypeInfo() ? 1 : 0;
	return S_OK;
}

STDMETHODIMP DockAccessible::GetTypeInfo(UINT index, LCID, ITypeInfo** info) {
	if (!info)
		return E_POINTER;
	*info = nullptr;
	if (index != 0 || !AccessibleTypeInfo())
		return DISP_E_BADINDEX;
	*info = AccessibleTypeInfo();
	(*info)->AddRef();
	return S_OK;
}

STDMETHODIMP DockAccessible::GetIDsOfNames(REFIID, LPOLESTR* names, UINT count, LCID, DISPID* ids) {
	auto info = AccessibleTypeInfo();
	return info ? ::DispGetIDsOfNames(info, names, count, ids) : E_NOTIMPL;
}

STDMETHODIMP DockAccessible::Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* params, VARIANT* result, EXCEPINFO* excep, UINT* argErr) {
	auto info = AccessibleTypeInfo();
	return info ? ::DispInvoke(static_cast<IAccessible*>(this), info, id, flags, params, result, excep, argErr) : E_NOTIMPL;
}

//
// IAccessible
//

STDMETHODIMP DockAccessible::get_accParent(IDispatch** parent) {
	if (!Alive() || !m_Std)
		return E_FAIL;
	return m_Std->get_accParent(parent);
}

STDMETHODIMP DockAccessible::get_accChildCount(long* count) {
	if (!count)
		return E_POINTER;
	*count = 0;
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	*count = (long)(s.Elements.size() + s.Windows.size());
	return S_OK;
}

STDMETHODIMP DockAccessible::get_accChild(VARIANT child, IDispatch** disp) {
	if (!disp)
		return E_POINTER;
	*disp = nullptr;
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	switch (Resolve(child, s, index)) {
		case Kind::Window: {
			auto acc = AccessibleOf(s.Windows[index]);
			if (!acc)
				return S_FALSE;
			*disp = acc.Detach();
			return S_OK;
		}
		case Kind::Element:
			return S_FALSE;			// a child that is no object of its own
		default:
			return E_INVALIDARG;
	}
}

STDMETHODIMP DockAccessible::get_accName(VARIANT child, BSTR* name) {
	if (!name)
		return E_POINTER;
	*name = nullptr;
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	switch (Resolve(child, s, index)) {
		case Kind::Self:
			return ReturnString(m_Owner->AccName(), name);
		case Kind::Element:
			return ReturnString(s.Elements[index].Name, name);
		case Kind::Window:
			if (auto acc = AccessibleOf(s.Windows[index]))
				return acc->get_accName(Self(), name);
			return S_FALSE;
		default:
			return E_INVALIDARG;
	}
}

STDMETHODIMP DockAccessible::get_accValue(VARIANT, BSTR* value) {
	if (value)
		*value = nullptr;
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accDescription(VARIANT, BSTR* description) {
	if (description)
		*description = nullptr;
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accRole(VARIANT child, VARIANT* role) {
	if (!role)
		return E_POINTER;
	Empty(role);
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	switch (Resolve(child, s, index)) {
		case Kind::Self:
			role->vt = VT_I4;
			role->lVal = m_Owner->AccRole();
			return S_OK;
		case Kind::Element:
			role->vt = VT_I4;
			role->lVal = s.Elements[index].Role;
			return S_OK;
		case Kind::Window:
			if (auto acc = AccessibleOf(s.Windows[index]))
				return acc->get_accRole(Self(), role);
			return S_FALSE;
		default:
			return E_INVALIDARG;
	}
}

STDMETHODIMP DockAccessible::get_accState(VARIANT child, VARIANT* state) {
	if (!state)
		return E_POINTER;
	Empty(state);
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	switch (Resolve(child, s, index)) {
		case Kind::Self:
			return m_Std ? m_Std->get_accState(Self(), state) : E_FAIL;
		case Kind::Element:
			state->vt = VT_I4;
			state->lVal = s.Elements[index].State;
			return S_OK;
		case Kind::Window:
			if (auto acc = AccessibleOf(s.Windows[index]))
				return acc->get_accState(Self(), state);
			return S_FALSE;
		default:
			return E_INVALIDARG;
	}
}

STDMETHODIMP DockAccessible::get_accHelp(VARIANT, BSTR* help) {
	if (help)
		*help = nullptr;
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accHelpTopic(BSTR* helpFile, VARIANT, long* topic) {
	if (helpFile)
		*helpFile = nullptr;
	if (topic)
		*topic = 0;
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accKeyboardShortcut(VARIANT, BSTR* shortcut) {
	if (shortcut)
		*shortcut = nullptr;
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accFocus(VARIANT* child) {
	if (!child)
		return E_POINTER;
	Empty(child);
	if (!Alive())
		return E_FAIL;
	HWND focus = ::GetFocus();
	if (!focus || !(focus == m_Window || ::IsChild(m_Window, focus)))
		return S_FALSE;
	if (focus == m_Window) {
		child->vt = VT_I4;
		child->lVal = CHILDID_SELF;
		return S_OK;
	}
	const Snapshot s = Take();
	for (HWND w : s.Windows) {
		if (w == focus || ::IsChild(w, focus)) {
			if (auto acc = AccessibleOf(w)) {
				child->vt = VT_DISPATCH;
				child->pdispVal = acc.Detach();
				return S_OK;
			}
		}
	}
	child->vt = VT_I4;
	child->lVal = CHILDID_SELF;
	return S_OK;
}

STDMETHODIMP DockAccessible::get_accSelection(VARIANT* children) {
	if (!children)
		return E_POINTER;
	Empty(children);
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	for (size_t i = 0; i < s.Elements.size(); i++) {
		if (s.Elements[i].State & STATE_SYSTEM_SELECTED) {
			children->vt = VT_I4;
			children->lVal = (LONG)i + 1;
			return S_OK;
		}
	}
	return S_FALSE;
}

STDMETHODIMP DockAccessible::get_accDefaultAction(VARIANT child, BSTR* action) {
	if (!action)
		return E_POINTER;
	*action = nullptr;
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	if (Resolve(child, s, index) == Kind::Element)
		return ReturnString(s.Elements[index].Action, action);
	return S_FALSE;
}

STDMETHODIMP DockAccessible::accSelect(long flags, VARIANT child) {
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	switch (Resolve(child, s, index)) {
		case Kind::Self:
			if (flags & SELFLAG_TAKEFOCUS) {
				::SetFocus(m_Window);
				return S_OK;
			}
			return S_FALSE;
		case Kind::Element: {
			auto select = s.Elements[index].Select;
			if ((flags & (SELFLAG_TAKESELECTION | SELFLAG_TAKEFOCUS | SELFLAG_ADDSELECTION)) && select) {
				select();
				return S_OK;
			}
			return S_FALSE;
		}
		default:
			return E_INVALIDARG;
	}
}

STDMETHODIMP DockAccessible::accLocation(long* left, long* top, long* width, long* height, VARIANT child) {
	if (!left || !top || !width || !height)
		return E_POINTER;
	*left = *top = *width = *height = 0;
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	RECT rc{};
	switch (Resolve(child, s, index)) {
		case Kind::Self:
			// the client area of the window
			{
				RECT client;
				::GetClientRect(m_Window, &client);
				POINT origin{ 0, 0 };
				::ClientToScreen(m_Window, &origin);
				rc = { origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
			}
			break;
		case Kind::Element:
			rc = s.Elements[index].Screen;
			break;
		case Kind::Window:
			::GetWindowRect(s.Windows[index], &rc);
			break;
		default:
			return E_INVALIDARG;
	}
	*left = rc.left;
	*top = rc.top;
	*width = rc.right - rc.left;
	*height = rc.bottom - rc.top;
	return S_OK;
}

STDMETHODIMP DockAccessible::accNavigate(long direction, VARIANT start, VARIANT* end) {
	if (!end)
		return E_POINTER;
	Empty(end);
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	const size_t count = s.Elements.size() + s.Windows.size();
	size_t index = 0;
	Kind kind = Resolve(start, s, index);
	if (kind == Kind::Invalid)
		return E_INVALIDARG;
	// where a child is among all of them (elements first)
	const size_t position = kind == Kind::Element ? index : kind == Kind::Window ? s.Elements.size() + index : 0;

	if (kind == Kind::Self) {
		if (direction == NAVDIR_FIRSTCHILD)
			return count ? ChildVariant(s, 0, end) : S_FALSE;
		if (direction == NAVDIR_LASTCHILD)
			return count ? ChildVariant(s, count - 1, end) : S_FALSE;
		// siblings of the window itself are for the standard object to say
		return m_Std ? m_Std->accNavigate(direction, start, end) : S_FALSE;
	}

	switch (direction) {
		case NAVDIR_NEXT:
			return position + 1 < count ? ChildVariant(s, position + 1, end) : S_FALSE;
		case NAVDIR_PREVIOUS:
			return position > 0 ? ChildVariant(s, position - 1, end) : S_FALSE;
		default:
			return S_FALSE;
	}
}

STDMETHODIMP DockAccessible::accHitTest(long left, long top, VARIANT* child) {
	if (!child)
		return E_POINTER;
	Empty(child);
	if (!Alive())
		return E_FAIL;
	const POINT pt{ left, top };
	RECT window;
	::GetWindowRect(m_Window, &window);
	if (!PtInRect(&window, pt))
		return S_FALSE;

	const Snapshot s = Take();
	// a child window covers what the chrome would be under it
	for (HWND w : s.Windows) {
		RECT rc;
		::GetWindowRect(w, &rc);
		if (::IsWindowVisible(w) && PtInRect(&rc, pt)) {
			if (auto acc = AccessibleOf(w)) {
				child->vt = VT_DISPATCH;
				child->pdispVal = acc.Detach();
				return S_OK;
			}
		}
	}
	// the last one wins: buttons lie on the caption, close buttons on their tabs
	for (size_t i = s.Elements.size(); i-- > 0;) {
		if (!IsRectEmpty(&s.Elements[i].Screen) && PtInRect(&s.Elements[i].Screen, pt)) {
			child->vt = VT_I4;
			child->lVal = (LONG)i + 1;
			return S_OK;
		}
	}
	child->vt = VT_I4;
	child->lVal = CHILDID_SELF;
	return S_OK;
}

STDMETHODIMP DockAccessible::accDoDefaultAction(VARIANT child) {
	if (!Alive())
		return E_FAIL;
	const Snapshot s = Take();
	size_t index = 0;
	if (Resolve(child, s, index) != Kind::Element)
		return S_FALSE;
	auto invoke = s.Elements[index].Invoke;
	if (!invoke)
		return S_FALSE;
	invoke();		// (this can end the window, and with it the owner: nothing is touched afterwards)
	return S_OK;
}

STDMETHODIMP DockAccessible::put_accName(VARIANT, BSTR) {
	return DISP_E_MEMBERNOTFOUND;
}

STDMETHODIMP DockAccessible::put_accValue(VARIANT, BSTR) {
	return DISP_E_MEMBERNOTFOUND;
}

}
