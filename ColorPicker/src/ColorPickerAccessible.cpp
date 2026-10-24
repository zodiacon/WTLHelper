// Accessibility (MSAA, which UI Automation clients see through its proxy). The gradients are a group of three
// sliders (saturation and brightness, hue, alpha) valued as their numbers; the swatches a list of colors named
// by their hex value; the fields keep their edit controls' objects, named for screen readers since their labels
// are drawn; the color button is a drop-down button valued as its color. Every object wraps the system's one for
// its window and passes on what it doesn't answer itself.

#include "ColorPicker.h"
#include "ColorPickerGradient.h"
#include "ColorPickerModel.h"

#include <oleacc.h>

#include <cmath>

#pragma comment(lib, "oleacc.lib")

// ---- The base: the system's object for the window, passed through -----------------------------

class CpAccessible : public IAccessible {
public:
	CpAccessible(HWND hWnd, IAccessible* proxy) : m_hWnd(hWnd), m_Proxy(proxy) {}
	virtual ~CpAccessible() = default;

	// the window is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_Connected = false;
	}

	// the system's object for a window's client area, which these wrap
	static ATL::CComPtr<IAccessible> StdObjectFor(HWND hWnd) {
		ATL::CComPtr<IAccessible> proxy;
		::CreateStdAccessibleObject(hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy));
		return proxy;
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

	// IAccessible: the system's answers
#define CP_PASS(call) return m_Connected ? m_Proxy->call : RPC_E_DISCONNECTED
	STDMETHODIMP get_accParent(IDispatch** parent) override { CP_PASS(get_accParent(parent)); }
	STDMETHODIMP get_accChildCount(long* count) override { CP_PASS(get_accChildCount(count)); }
	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override { CP_PASS(get_accChild(child, dispatch)); }
	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override { CP_PASS(get_accName(child, name)); }
	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override { CP_PASS(get_accValue(child, value)); }
	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override { CP_PASS(get_accDescription(child, text)); }
	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override { CP_PASS(get_accRole(child, role)); }
	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override { CP_PASS(get_accState(child, state)); }
	STDMETHODIMP get_accHelp(VARIANT child, BSTR* help) override { CP_PASS(get_accHelp(child, help)); }
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT child, long* topic) override { CP_PASS(get_accHelpTopic(file, child, topic)); }
	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override { CP_PASS(get_accKeyboardShortcut(child, key)); }
	STDMETHODIMP get_accFocus(VARIANT* focus) override { CP_PASS(get_accFocus(focus)); }
	STDMETHODIMP get_accSelection(VARIANT* selection) override { CP_PASS(get_accSelection(selection)); }
	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override { CP_PASS(get_accDefaultAction(child, action)); }
	STDMETHODIMP accSelect(long flags, VARIANT child) override { CP_PASS(accSelect(flags, child)); }
	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override { CP_PASS(accLocation(x, y, cx, cy, child)); }
	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override { CP_PASS(accNavigate(dir, start, end)); }
	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override { CP_PASS(accHitTest(x, y, child)); }
	STDMETHODIMP accDoDefaultAction(VARIANT child) override { CP_PASS(accDoDefaultAction(child)); }
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
#undef CP_PASS

protected:
	static constexpr long Invalid = -1;

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

	static HRESULT Location(HWND hWnd, RECT rc, long* x, long* y, long* cx, long* cy) {
		::MapWindowPoints(hWnd, nullptr, (POINT*)&rc, 2);
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}

	// a window's name as the system gives it (a dialog's label before it), else a fallback
	static HRESULT WindowName(HWND hWnd, BSTR* name, LPCWSTR fallback) {
		*name = nullptr;
		ATL::CComPtr<IAccessible> outer;
		if (SUCCEEDED(::AccessibleObjectFromWindow(hWnd, OBJID_WINDOW, IID_PPV_ARGS(&outer)))) {
			VARIANT self{};
			self.vt = VT_I4;
			self.lVal = CHILDID_SELF;
			if (SUCCEEDED(outer->get_accName(self, name)) && *name && ::SysStringLen(*name))
				return S_OK;
			::SysFreeString(*name);
			*name = nullptr;
		}
		return Text(name, fallback);
	}

	// the next or previous of count children for accNavigate
	static HRESULT Navigate(long dir, long id, long count, VARIANT* end, IAccessible* proxy, VARIANT start) {
		::VariantInit(end);
		long target = 0;
		if (id == CHILDID_SELF) {
			if (dir == NAVDIR_FIRSTCHILD)
				target = 1;
			else if (dir == NAVDIR_LASTCHILD)
				target = count;
			else
				return proxy->accNavigate(dir, start, end);   // the window's siblings
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

	HWND                      m_hWnd;
	ATL::CComPtr<IAccessible> m_Proxy;
	bool                      m_Connected = true;
	LONG                      m_Refs = 1;
};

// ---- The gradients: three sliders ------------------------------------------------------------------

class CpGradAccessible : public CpAccessible {
public:
	CpGradAccessible(CColorPickerCtrl* c, IAccessible* proxy) : CpAccessible(c->m_Grad->m_hWnd, proxy), m_Ctrl(c) {}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		*count = Count();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		switch (Id(child, Count())) {
			case CHILDID_SELF: return WindowName(m_Ctrl->m_hWnd, name, L"Color");
			case CPKHT_SV: return Text(name, L"Saturation and brightness");
			case CPKHT_HUE: return Text(name, L"Hue");
			case CPKHT_ALPHA: return Text(name, L"Alpha");
		}
		return E_INVALIDARG;
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		auto hsv = m_Ctrl->GetHsv();
		wchar_t text[64];
		switch (Id(child, Count())) {
			case CHILDID_SELF:
			{
				auto hex = CpFormatHex(m_Ctrl->GetColor());
				if (m_Ctrl->GetAlpha() == 255 || !m_Ctrl->HasAlpha())
					return Text(value, hex.c_str());
				swprintf_s(text, L"%s, alpha %d", hex.c_str(), m_Ctrl->GetAlpha());
				return Text(value, text);
			}
			case CPKHT_SV:
				swprintf_s(text, L"Saturation %d%%, brightness %d%%", (int)std::lround(hsv.s * 100), (int)std::lround(hsv.v * 100));
				return Text(value, text);
			case CPKHT_HUE:
				swprintf_s(text, L"%d degrees", (int)std::lround(hsv.h));
				return Text(value, text);
			case CPKHT_ALPHA:
				swprintf_s(text, L"%d", m_Ctrl->GetAlpha());
				return Text(value, text);
		}
		return E_INVALIDARG;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == CHILDID_SELF)
			return m_Proxy->get_accDescription(child, text);
		*text = nullptr;
		return id == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_GROUPING : ROLE_SYSTEM_SLIDER);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == CHILDID_SELF)
			return m_Proxy->get_accState(child, state);
		if (id == Invalid)
			return E_INVALIDARG;
		long s = STATE_SYSTEM_FOCUSABLE;
		if (::GetFocus() == m_hWnd && (UINT)id == m_Ctrl->m_Focus)
			s |= STATE_SYSTEM_FOCUSED;
		return Long(state, s);
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		::VariantInit(focus);
		if (::GetFocus() != m_hWnd)
			return S_FALSE;
		return Long(focus, (long)m_Ctrl->m_Focus);
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Location(m_hWnd, m_Ctrl->PartRect((UINT)id), x, y, cx, cy);
	}

	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(start, Count());
		if (id == Invalid)
			return E_INVALIDARG;
		return Navigate(dir, id, Count(), end, m_Proxy, start);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		::VariantInit(child);
		POINT pt{ x, y };
		::ScreenToClient(m_hWnd, &pt);
		RECT rc;
		::GetClientRect(m_hWnd, &rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		UINT part = m_Ctrl->HitTest(pt);
		return Long(child, part >= CPKHT_SV && part <= CPKHT_ALPHA ? (long)part : CHILDID_SELF);
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == Invalid)
			return E_INVALIDARG;
		if (flags & SELFLAG_TAKEFOCUS) {
			::SetFocus(m_hWnd);
			if (id != CHILDID_SELF)
				m_Ctrl->SetFocusPart((UINT)id);
		}
		return S_OK;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT, BSTR* action) override {
		*action = nullptr;
		return m_Connected ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP accDoDefaultAction(VARIANT) override {
		return m_Connected ? S_FALSE : RPC_E_DISCONNECTED;
	}

private:
	long Count() const {
		return m_Ctrl->HasAlpha() ? 3 : 2;
	}

	CColorPickerCtrl* m_Ctrl;
};

// ---- The swatches: a list of colors -------------------------------------------------------------------

class CpSwatchAccessible : public CpAccessible {
public:
	CpSwatchAccessible(CColorPickerCtrl* c, IAccessible* proxy) : CpAccessible(c->m_Swatches->m_hWnd, proxy), m_Ctrl(c) {}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		*count = Count();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return Text(name, L"Swatches");
		if (id == Invalid)
			return E_INVALIDARG;
		COLORREF color = m_Ctrl->GroupColor(group, index);
		return Text(name, color == CLR_NONE ? L"Empty" : CpFormatHex(color).c_str());
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		return Id(child, Count()) == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return m_Proxy->get_accDescription(child, text);
		if (id == Invalid)
			return E_INVALIDARG;
		static const wchar_t* const groups[] = { L"Palette color", L"Custom color", L"Recent color" };
		wchar_t buffer[64];
		swprintf_s(buffer, L"%s %d of %d", groups[group], index + 1, m_Ctrl->GroupCount(group));
		return Text(text, buffer);
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(child, Count());
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return m_Proxy->get_accState(child, state);
		if (id == Invalid)
			return E_INVALIDARG;
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;
		if (::GetFocus() == m_hWnd && group == m_Ctrl->m_SwGroup && index == m_Ctrl->m_SwIndex)
			s |= STATE_SYSTEM_FOCUSED;
		if (m_Ctrl->GroupColor(group, index) == m_Ctrl->GetColor())
			s |= STATE_SYSTEM_SELECTED;   // the color picked
		return Long(state, s);
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		::VariantInit(focus);
		int child = m_Ctrl->SwatchChild(m_Ctrl->m_SwGroup, m_Ctrl->m_SwIndex);
		if (::GetFocus() != m_hWnd || child == 0)
			return S_FALSE;
		return Long(focus, child);
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		return m_Connected ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return S_FALSE;
		if (id == Invalid)
			return E_INVALIDARG;
		bool empty = m_Ctrl->GroupColor(group, index) == CLR_NONE;
		return Text(action, empty ? L"Save the color here" : L"Pick");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return S_FALSE;
		if (id == Invalid)
			return E_INVALIDARG;
		m_Ctrl->ActivateSwatch(group, index, false);
		return S_OK;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == Invalid)
			return E_INVALIDARG;
		if (flags & SELFLAG_TAKEFOCUS) {
			::SetFocus(m_hWnd);
			if (id != CHILDID_SELF)
				m_Ctrl->FocusSwatch(group, index, true);
		}
		return S_OK;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		int group, index;
		long id = Cell(child, &group, &index);
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Location(m_Ctrl->m_hWnd, m_Ctrl->SwatchCellRect(group, index), x, y, cx, cy);   // control coords
	}

	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		long id = Id(start, Count());
		if (id == Invalid)
			return E_INVALIDARG;
		return Navigate(dir, id, Count(), end, m_Proxy, start);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		::VariantInit(child);
		POINT pt{ x, y };
		RECT rc;
		::GetWindowRect(m_hWnd, &rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		::ScreenToClient(m_Ctrl->m_hWnd, &pt);
		int index;
		UINT part = m_Ctrl->HitTest(pt, &index);
		int id = part >= CPKHT_PALETTE && part <= CPKHT_RECENT ? m_Ctrl->SwatchChild(part - CPKHT_PALETTE, index) : 0;
		return Long(child, id ? id : CHILDID_SELF);
	}

private:
	long Count() const {
		int counts[3];
		m_Ctrl->SwatchCounts(counts);
		return counts[0] + counts[1] + counts[2];
	}

	// a child id and its cell
	long Cell(const VARIANT& child, int* pGroup, int* pIndex) const {
		long id = Id(child, Count());
		if (id != Invalid && id != CHILDID_SELF && !m_Ctrl->SwatchOfChild(id, pGroup, pIndex))
			return Invalid;
		return id;
	}

	CColorPickerCtrl* m_Ctrl;
};

// ---- A field: the edit control's object, named ----------------------------------------------------

class CpEditAccessible : public CpAccessible {
public:
	CpEditAccessible(HWND hWnd, IAccessible* proxy, LPCWSTR name) : CpAccessible(hWnd, proxy), m_Name(name) {}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (child.vt == VT_I4 && child.lVal == CHILDID_SELF)
			return Text(name, m_Name);
		return m_Proxy->get_accName(child, name);
	}

private:
	LPCWSTR m_Name;
};

// ---- The color button: a drop-down button valued as its color ---------------------------------------

class CpButtonAccessible : public CpAccessible {
public:
	CpButtonAccessible(CColorButtonCtrl* b, IAccessible* proxy) : CpAccessible(b->m_hWnd, proxy), m_Button(b) {}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (Id(child, 0) != CHILDID_SELF)
			return E_INVALIDARG;
		// the system's (a dialog's label before it); not the window object's, which would ask this one again
		*name = nullptr;
		if (SUCCEEDED(m_Proxy->get_accName(child, name)) && *name && ::SysStringLen(*name))
			return S_OK;
		::SysFreeString(*name);
		return Text(name, L"Color");
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (Id(child, 0) != CHILDID_SELF)
			return E_INVALIDARG;
		auto hex = CpFormatHex(m_Button->GetColor());
		if (m_Button->GetAlpha() == 255)
			return Text(value, hex.c_str());
		wchar_t text[64];
		swprintf_s(text, L"%s, alpha %d", hex.c_str(), m_Button->GetAlpha());
		return Text(value, text);
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (Id(child, 0) != CHILDID_SELF)
			return E_INVALIDARG;
		return Long(role, ROLE_SYSTEM_BUTTONDROPDOWN);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		HRESULT hr = m_Proxy->get_accState(child, state);
		if (FAILED(hr) || state->vt != VT_I4)
			return hr;
		state->lVal |= STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_HASPOPUP |
			(m_Button->IsDroppedDown() ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED);
		return S_OK;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (Id(child, 0) != CHILDID_SELF)
			return E_INVALIDARG;
		return Text(action, m_Button->IsDroppedDown() ? L"Close" : L"Open");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Connected)
			return RPC_E_DISCONNECTED;
		if (Id(child, 0) != CHILDID_SELF)
			return E_INVALIDARG;
		// posted: opening runs a message loop until the picker closes, which mustn't hold up the caller
		if (m_Button->IsDroppedDown())
			m_Button->PostMessage(CColorButtonCtrl::WM_CPB_CLOSE, FALSE);
		else
			m_Button->PostMessage(CColorButtonCtrl::WM_CPB_OPEN);
		return S_OK;
	}

private:
	CColorButtonCtrl* m_Button;
};

// ---- The windows' side --------------------------------------------------------------------------

template<typename T, typename Make>
static LRESULT Serve(CpAccessible*& acc, HWND hWnd, WPARAM wParam, Make&& make) {
	if (!acc) {
		auto proxy = CpAccessible::StdObjectFor(hWnd);
		if (!proxy)
			return 0;
		acc = make(proxy.p);   // the window's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(acc));
}

LRESULT CColorPickerCtrl::OnGradGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	return Serve<CpGradAccessible>(m_GradAcc, m_Grad->m_hWnd, wParam, [&](IAccessible* proxy) {
		return new CpGradAccessible(this, proxy);
	});
}

LRESULT CColorPickerCtrl::OnSwGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	return Serve<CpSwatchAccessible>(m_SwAcc, m_Swatches->m_hWnd, wParam, [&](IAccessible* proxy) {
		return new CpSwatchAccessible(this, proxy);
	});
}

LRESULT CColorPickerCtrl::EditGetObject(CColorPickerCtrl* self, int field, HWND hWnd, WPARAM wParam, LPARAM) {
	static const wchar_t* const names[] = { L"Hue", L"Saturation", L"Brightness", L"Alpha", L"Red", L"Green", L"Blue", L"Hex" };
	return Serve<CpEditAccessible>(self->m_EditAcc[field], hWnd, wParam, [&](IAccessible* proxy) {
		return new CpEditAccessible(hWnd, proxy, names[field]);
	});
}

void CColorPickerCtrl::NotifyAccessible(DWORD event, HWND hWnd, long child) {
	// no client has asked yet: nobody to tell
	if (!hWnd || !::IsWindow(hWnd))
		return;
	if ((hWnd == m_Grad->m_hWnd && !m_GradAcc) || (hWnd == m_Swatches->m_hWnd && !m_SwAcc))
		return;
	::NotifyWinEvent(event, hWnd, OBJID_CLIENT, child);
}

void CColorPickerCtrl::DisconnectAccessible() {
	auto cut = [](CpAccessible*& acc) {
		if (acc) {
			acc->Disconnect();
			acc->Release();
			acc = nullptr;
		}
	};
	cut(m_GradAcc);
	cut(m_SwAcc);
	for (auto& acc : m_EditAcc)
		cut(acc);
}

LRESULT CColorButtonCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	return Serve<CpButtonAccessible>(m_Acc, m_hWnd, wParam, [&](IAccessible* proxy) {
		return new CpButtonAccessible(this, proxy);
	});
}

void CColorButtonCtrl::NotifyAccessible(DWORD event) {
	if (m_Acc && IsWindow())
		::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
}

void CColorButtonCtrl::DisconnectAccessible() {
	if (m_Acc) {
		m_Acc->Disconnect();
		m_Acc->Release();
		m_Acc = nullptr;
	}
}
