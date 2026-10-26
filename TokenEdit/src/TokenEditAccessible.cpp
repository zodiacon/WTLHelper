// Accessibility (MSAA, which UI Automation clients see through its proxy). The box is a list whose children are the
// tokens, in order, each a list item named by its text and described by its tip (and "invalid"), selected and focused
// as the control's selection and keys are; the last child is the typing area, the Edit's own object. A token's default
// action edits it. The suggestions' popup is a list of its rows, the hot one selected and reported focused as it
// moves while the edit keeps the real focus (an active descendant); a row's default action picks it. Actions are
// posted, so the app's handlers don't run inside the client's call. Each object wraps the system's one for its window
// and passes on what it doesn't answer; a name is never taken from the window's own OBJID_WINDOW object, which would
// ask this one again.

#include "TokenEditData.h"

#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

namespace {
	enum : WPARAM { AccEdit, AccSelect, AccAddSelection, AccRemoveSelection, AccPick };

	const CLSID AccPropServices = { 0xb5f8350b, 0x0548, 0x48b1, { 0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7 } };
	const MSAAPROPID AccName = { 0x608d3df8, 0x8128, 0x4aa7, { 0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91 } };

	HRESULT Text(BSTR* out, const std::wstring& text) {
		*out = ::SysAllocString(text.c_str());
		return *out ? S_OK : E_OUTOFMEMORY;
	}

	HRESULT Long(VARIANT* out, long value) {
		out->vt = VT_I4;
		out->lVal = value;
		return S_OK;
	}

	HRESULT Location(HWND hwnd, RECT rc, long* x, long* y, long* cx, long* cy) {
		::MapWindowPoints(hwnd, nullptr, (POINT*)&rc, 2);
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}
}

// ---- What both objects share ------------------------------------------------------------------------

class TkeAccBase : public IAccessible {
public:
	explicit TkeAccBase(IAccessible* proxy) : m_Proxy(proxy) {
	}
	virtual ~TkeAccBase() = default;

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

	STDMETHODIMP get_accParent(IDispatch** parent) override {
		if (!Connected())
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}
	STDMETHODIMP get_accChildCount(long* count) override {
		if (!Connected())
			return RPC_E_DISCONNECTED;
		*count = Count();
		return S_OK;
	}
	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override {
		*key = nullptr;
		if (!Connected())
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return Connected() ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return Connected() ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!Connected())
			return RPC_E_DISCONNECTED;
		return Id(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP put_accValue(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}

	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		::VariantInit(end);
		if (!Connected())
			return RPC_E_DISCONNECTED;
		const long count = Count();
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

protected:
	static constexpr long Invalid = -2;

	virtual bool Connected() const = 0;
	virtual long Count() const = 0;

	// a child id of 1..count, CHILDID_SELF, or Invalid
	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= Count() ? child.lVal : Invalid;
	}

	ATL::CComPtr<IAccessible> m_Proxy;
	LONG m_Refs = 1;
};

// ---- The box ----------------------------------------------------------------------------------------------

class TkeBoxAccessible : public TkeAccBase {
public:
	TkeBoxAccessible(CTokenEditCtrl* ctrl, IAccessible* proxy) : TkeAccBase(proxy), m_Ctrl(ctrl) {
	}

	void Disconnect() {
		m_Ctrl = nullptr;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		if (id == EditId() && m_Ctrl->m_Edit.IsWindow())
			return ::AccessibleObjectFromWindow(m_Ctrl->m_Edit, OBJID_WINDOW, IID_IDispatch, (void**)dispatch);
		return S_FALSE;   // a token: a simple element
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == EditId())
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->get_accName(child, name);   // the window's text, or the label before it in a dialog
		return Text(name, Token(id).text);
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == EditId())
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		auto s = m_Ctrl->AccDescription(id - 1);
		return s.empty() ? S_FALSE : Text(text, s);
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == EditId())
			return Long(role, ROLE_SYSTEM_TEXT);
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		auto& d = *m_Ctrl->m_Data;
		const HWND focus = ::GetFocus();
		const bool focused = focus == m_Ctrl->m_hWnd;
		if (id == CHILDID_SELF)
			return Long(state, STATE_SYSTEM_FOCUSABLE | (focused || focus == m_Ctrl->m_Edit.m_hWnd ? STATE_SYSTEM_FOCUSED : 0)
				| (m_Ctrl->ReadOnly() ? STATE_SYSTEM_READONLY : 0) | (m_Ctrl->IsWindowEnabled() ? 0 : STATE_SYSTEM_UNAVAILABLE));
		if (id == EditId())
			return E_INVALIDARG;
		auto& t = Token(id);
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_MULTISELECTABLE | STATE_SYSTEM_EXTSELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (t.selected)
			s |= STATE_SYSTEM_SELECTED;
		if (focused && d.focus == id - 1)
			s |= STATE_SYSTEM_FOCUSED;
		if (t.locked || m_Ctrl->ReadOnly())
			s |= STATE_SYSTEM_READONLY;
		if (d.hot.index == id - 1)
			s |= STATE_SYSTEM_HOTTRACKED;
		RECT rc = TokenRect(id), client;
		m_Ctrl->GetClientRect(&client);
		if (!::IntersectRect(&rc, &rc, &client))
			s |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
		return Long(state, s);
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == EditId())
			return E_INVALIDARG;
		if (id == CHILDID_SELF || !CanEdit(id))
			return S_FALSE;
		return Text(action, L"Edit");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF || id == EditId() || !CanEdit(id))
			return E_INVALIDARG;
		::PostMessage(m_Ctrl->m_hWnd, CTokenEditCtrl::WM_TKE_ACCACTION, AccEdit, Token(id).id);
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		const HWND hwnd = ::GetFocus();
		auto& d = *m_Ctrl->m_Data;
		if (hwnd == m_Ctrl->m_Edit.m_hWnd)
			return Long(focus, EditId());
		if (hwnd != m_Ctrl->m_hWnd)
			return S_FALSE;
		return Long(focus, d.focus >= 0 && d.focus < (int)d.tokens.Size() ? d.focus + 1 : CHILDID_SELF);
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto& d = *m_Ctrl->m_Data;
		auto selected = d.tokens.Selection();
		if (selected.empty())
			return S_FALSE;
		// several: the one with the keys, else the first (an enumerator isn't worth it here)
		size_t one = selected[0];
		if (d.focus >= 0 && std::find(selected.begin(), selected.end(), (size_t)d.focus) != selected.end())
			one = d.focus;
		return Long(selection, (long)one + 1);
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF || id == EditId())
			return E_INVALIDARG;
		const UINT token = Token(id).id;
		const HWND hwnd = m_Ctrl->m_hWnd;
		if (flags & (SELFLAG_TAKESELECTION | SELFLAG_TAKEFOCUS))
			::PostMessage(hwnd, CTokenEditCtrl::WM_TKE_ACCACTION, AccSelect, token);
		else if (flags & (SELFLAG_ADDSELECTION | SELFLAG_EXTENDSELECTION))
			::PostMessage(hwnd, CTokenEditCtrl::WM_TKE_ACCACTION, AccAddSelection, token);
		else if (flags & SELFLAG_REMOVESELECTION)
			::PostMessage(hwnd, CTokenEditCtrl::WM_TKE_ACCACTION, AccRemoveSelection, token);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		if (id == EditId()) {
			RECT rc;
			m_Ctrl->m_Edit.GetWindowRect(&rc);
			*x = rc.left;
			*y = rc.top;
			*cx = rc.right - rc.left;
			*cy = rc.bottom - rc.top;
			return S_OK;
		}
		return Location(m_Ctrl->m_hWnd, TokenRect(id), x, y, cx, cy);
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
		HWND under = ::ChildWindowFromPointEx(m_Ctrl->m_hWnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (under && under == m_Ctrl->m_Edit.m_hWnd)
			return Long(child, EditId());
		auto hit = m_Ctrl->HitTest(pt);
		return Long(child, hit.index >= 0 ? hit.index + 1 : CHILDID_SELF);
	}

protected:
	bool Connected() const override {
		return m_Ctrl != nullptr;
	}

	long Count() const override {
		return (long)m_Ctrl->m_Data->tokens.Size() + 1;   // and the typing area
	}

private:
	long EditId() const {
		return Count();
	}

	const TkeToken& Token(long id) const {
		return m_Ctrl->m_Data->tokens[id - 1];
	}

	bool CanEdit(long id) const {
		return !m_Ctrl->ReadOnly() && !(m_Ctrl->GetStyle() & TKES_NOEDITING) && !Token(id).locked;
	}

	RECT TokenRect(long id) const {
		auto& d = *m_Ctrl->m_Data;
		m_Ctrl->EnsureLayout();
		RECT rc = id - 1 < (long)d.layout.tokens.size() ? d.layout.tokens[id - 1].rc : RECT{};
		::OffsetRect(&rc, 0, -d.scroll);
		return rc;
	}

	CTokenEditCtrl* m_Ctrl;
};

// ---- The suggestions -----------------------------------------------------------------------------------

class TkeListAccessible : public TkeAccBase {
public:
	TkeListAccessible(TkeSuggestList* list, IAccessible* proxy) : TkeAccBase(proxy), m_List(list) {
	}

	void Disconnect() {
		m_List = nullptr;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Text(name, id == CHILDID_SELF ? L"Suggestions" : Item(id).text);
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF || Item(id).detail.empty())
			return S_FALSE;
		return Text(text, Item(id).detail);
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(state, m_List->IsOpen() ? 0 : STATE_SYSTEM_INVISIBLE);
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (id - 1 == m_List->m_Hot)
			s |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED;   // the edit keeps the real focus: this is where its keys go
		const RECT row = m_List->RowRect(id - 1);
		if (::IsRectEmpty(&row))
			s |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
		return Long(state, s);
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return id == CHILDID_SELF ? S_FALSE : Text(action, L"Pick");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		::PostMessage(m_List->m_Owner->m_hWnd, CTokenEditCtrl::WM_TKE_ACCACTION, AccPick, m_List->m_Rows[id - 1].index);
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_List)
			return RPC_E_DISCONNECTED;
		return m_List->m_Hot >= 0 ? Long(focus, m_List->m_Hot + 1) : S_FALSE;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		return get_accFocus(selection);
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		if (flags & (SELFLAG_TAKESELECTION | SELFLAG_TAKEFOCUS))
			m_List->SetHot(id - 1);   // no app code runs for it
		return S_OK;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_List)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		return Location(m_List->m_hWnd, m_List->RowRect(id - 1), x, y, cx, cy);
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_List)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_List->m_hWnd, &pt);
		RECT client;
		m_List->GetClientRect(&client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		int row = m_List->RowAt(pt);
		return Long(child, row >= 0 ? row + 1 : CHILDID_SELF);
	}

protected:
	bool Connected() const override {
		return m_List != nullptr;
	}

	long Count() const override {
		return (long)m_List->m_Rows.size();
	}

private:
	const TkeSuggestionItem& Item(long id) const {
		return m_List->m_Owner->m_Data->suggestions[m_List->m_Rows[id - 1].index];
	}

	TkeSuggestList* m_List;
};

// ---- The box's object ----------------------------------------------------------------------------------------

std::wstring CTokenEditCtrl::AccDescription(int index) const {
	auto& t = m_Data->tokens[index];
	std::wstring s = t.tip;
	if (t.invalid)
		s = s.empty() ? L"Invalid" : L"Invalid, " + s;
	return s;
}

LRESULT CTokenEditCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	auto& d = *m_Data;
	if (!d.acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		d.acc = new TkeBoxAccessible(this, proxy);   // the window's reference
		NameEdit();   // the label may have come after the control
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(d.acc));
}

void CTokenEditCtrl::NameEdit() {
	// the typing area has no label of its own: it's named as the box is (its label in a dialog, or its window text)
	if (!m_Edit.IsWindow())
		return;
	ATL::CComPtr<IAccessible> proxy;
	if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
		return;
	ATL::CComBSTR name;
	if (FAILED(proxy->get_accName(ATL::CComVariant(CHILDID_SELF), &name)) || !name || !name.Length())
		return;
	ATL::CComPtr<IAccPropServices> props;
	if (FAILED(props.CoCreateInstance(AccPropServices)))
		return;   // COM isn't initialized on the thread: the system's name (none)
	props->SetHwndPropStr(m_Edit, (DWORD)OBJID_CLIENT, CHILDID_SELF, AccName, name);
}

void CTokenEditCtrl::NotifyAccessible(DWORD event, long child) {
	// no client has asked yet: nobody to tell
	if (m_Data->acc && IsWindow())
		::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, child);
}

void CTokenEditCtrl::NotifyFocusToken() {
	auto& d = *m_Data;
	if (::GetFocus() == m_hWnd && d.focus >= 0 && d.focus < (int)d.tokens.Size())
		NotifyAccessible(EVENT_OBJECT_FOCUS, d.focus + 1);
}

void CTokenEditCtrl::DisconnectAccessible() {
	auto& d = *m_Data;
	if (d.acc) {
		d.acc->Disconnect();
		d.acc->Release();
		d.acc = nullptr;
	}
	if (d.list)
		d.list->Disconnect();
}

LRESULT CTokenEditCtrl::OnAccAction(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (wParam == AccPick) {
		if ((int)lParam >= 0 && (int)lParam < (int)d.suggestions.size())
			PickSuggestion((int)lParam);
		return 0;
	}
	const int i = d.tokens.Find((UINT)lParam);
	if (i < 0)
		return 0;
	switch (wParam) {
		case AccEdit:
			StartEdit(i, true);
			break;
		case AccSelect:
			FocusToken(i, false);
			break;
		case AccAddSelection:
		case AccRemoveSelection:
			d.tokens[i].selected = wParam == AccAddSelection;
			Invalidate(FALSE);
			NotifySelection();
			break;
	}
	return 0;
}

// ---- The suggestions' object ---------------------------------------------------------------------------------

LRESULT TkeSuggestList::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		m_Acc = new TkeListAccessible(this, proxy);
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Acc));
}

void TkeSuggestList::Disconnect() {
	if (m_Acc) {
		m_Acc->Disconnect();
		m_Acc->Release();
		m_Acc = nullptr;
	}
}
