// Accessibility (MSAA, which UI Automation clients see through its proxy). The ribbon's window is a tool bar whose
// children are, in order, the quick access buttons and the bar's menu button, the tabs (page tabs, the selected one
// selected), the minimize button, then the selected tab's commands: buttons, check and radio buttons, menu and split
// buttons, folded groups (menu buttons) and dialog launchers. A popup (a folded group, a minimized ribbon's tab) is a
// tool bar of its own with its commands. Hosted windows are windows of their own, with their own objects. Each
// command's keyboard shortcut is its key tips ("Alt, H, B"). The object wraps the system's one for the window and
// passes on what it doesn't answer itself.

#include "RibbonBarData.h"

#include <oleacc.h>

#pragma comment(lib, "oleacc.lib")

using Kind = RibKeyTarget::Kind;

class RibAccessible : public IAccessible {
public:
	RibAccessible(CRibbonBarCtrl* ctrl, HWND hwnd, IAccessible* proxy) : m_Ctrl(ctrl), m_Hwnd(hwnd), m_Proxy(proxy) {}
	virtual ~RibAccessible() = default;

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
		*count = (long)Kids().size();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Text(name, m_Ctrl->AccWindowName(m_Hwnd).c_str());
		return Text(name, m_Ctrl->AccName(kids[id - 1]).c_str());
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		auto description = m_Ctrl->AccDescription(kids[id - 1]);
		return description.empty() ? S_FALSE : Text(text, description.c_str());
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(role, ROLE_SYSTEM_TOOLBAR);
		auto& t = kids[id - 1];
		switch (t.kind) {
			case Kind::Tab: return Long(role, ROLE_SYSTEM_PAGETAB);
			case Kind::Group: case Kind::Customize: return Long(role, ROLE_SYSTEM_BUTTONMENU);
			case Kind::Launcher: case Kind::Minimize: return Long(role, ROLE_SYSTEM_PUSHBUTTON);
			default: break;
		}
		auto item = m_Ctrl->AccItem(t);
		if (!item)
			return Long(role, ROLE_SYSTEM_PUSHBUTTON);
		switch (item->kind) {
			case RibKind::Radio: return Long(role, ROLE_SYSTEM_RADIOBUTTON);
			case RibKind::DropDown: return Long(role, ROLE_SYSTEM_BUTTONMENU);
			case RibKind::Split: return Long(role, t.kind == Kind::Qat ? ROLE_SYSTEM_PUSHBUTTON : ROLE_SYSTEM_SPLITBUTTON);
			default: return Long(role, ROLE_SYSTEM_PUSHBUTTON);
		}
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return Long(state, ::GetFocus() == m_Hwnd ? STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_FOCUSED : 0);
		return Long(state, m_Ctrl->AccState(kids[id - 1]));
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		auto& t = kids[id - 1];
		auto item = m_Ctrl->AccItem(t);
		if (item && (item->state & RibStateDisabled))
			return S_FALSE;
		LPCWSTR text = L"Press";
		if (t.kind == Kind::Tab)
			text = L"Switch";
		else if (t.kind == Kind::Group || t.kind == Kind::Customize || (item && item->kind == RibKind::DropDown))
			text = L"Open";
		else if (item && item->kind == RibKind::Toggle)
			text = (item->state & RibStateChecked) ? L"Uncheck" : L"Check";
		else if (item && item->kind == RibKind::Radio)
			text = L"Check";
		return Text(action, text);
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		auto item = m_Ctrl->AccItem(kids[id - 1]);
		if (item && (item->state & RibStateDisabled))
			return E_FAIL;
		// later, as the user's click: a menu's modal loop mustn't run inside the client's call
		m_Ctrl->PostAccAction(kids[id - 1]);
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = m_Ctrl->AccFocus(m_Hwnd);
		if (id == CHILDID_SELF && ::GetFocus() != m_Hwnd)
			return S_FALSE;
		return Long(focus, id);
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		// the selected tab
		auto kids = Kids();
		for (size_t k = 0; k < kids.size(); k++)
			if (kids[k].kind == Kind::Tab && (m_Ctrl->AccState(kids[k]) & STATE_SYSTEM_SELECTED))
				return Long(selection, (long)k + 1);
		return S_FALSE;
	}

	STDMETHODIMP accSelect(long, VARIANT) override {
		return m_Ctrl ? DISP_E_MEMBERNOTFOUND : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		RECT rc = m_Ctrl->TargetRect(kids[id - 1]);
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
		long count = (long)Kids().size();
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
		// a hosted window under it: its own object
		HWND under = ::ChildWindowFromPointEx(m_Hwnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (under && under != m_Hwnd)
			return m_Proxy->accHitTest(x, y, child);
		auto kids = Kids();
		for (size_t k = 0; k < kids.size(); k++) {
			RECT rc = m_Ctrl->TargetRect(kids[k]);
			if (::PtInRect(&rc, pt))
				return Long(child, (long)k + 1);
		}
		return Long(child, CHILDID_SELF);
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* key) override {
		*key = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		auto kids = Kids();
		long id = Id(child, (long)kids.size());
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		auto keys = m_Ctrl->AccShortcut(kids[id - 1]);
		return keys.empty() ? S_FALSE : Text(key, keys.c_str());
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
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2;

	std::vector<RibKeyTarget> Kids() const {
		return m_Ctrl->AccChildren(m_Hwnd);
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

	CRibbonBarCtrl*           m_Ctrl;
	HWND                      m_Hwnd;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

// ---- The control's side -------------------------------------------------------------------------

LRESULT CRibbonBarCtrl::AccObject(HWND hwnd, RibAccessible*& slot, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!slot) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(hwnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		slot = new RibAccessible(this, hwnd, proxy);   // the window's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(slot));
}

void CRibbonBarCtrl::DisconnectAccessible(RibAccessible*& slot) {
	if (slot) {
		slot->Disconnect();
		slot->Release();
		slot = nullptr;
	}
}

LRESULT CRibbonBarCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	return AccObject(m_hWnd, m_Data->acc, wParam, lParam, bHandled);
}

LRESULT RibPopup::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	return m_Ribbon->AccObject(m_hWnd, acc, wParam, lParam, bHandled);
}

LRESULT RibPopup::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Ribbon->DisconnectAccessible(acc);
	bHandled = FALSE;
	return 0;
}

void CRibbonBarCtrl::NotifyAccessible(HWND hwnd, DWORD event, long child) {
	// no client has asked yet: nobody to tell
	auto& d = *m_Data;
	bool asked = hwnd == m_hWnd ? d.acc != nullptr : false;
	for (auto& p : d.popups)
		if (p->m_hWnd == hwnd)
			asked = p->acc != nullptr;
	if (asked && ::IsWindow(hwnd))
		::NotifyWinEvent(event, hwnd, OBJID_CLIENT, child);
}

std::vector<RibKeyTarget> CRibbonBarCtrl::AccChildren(HWND hwnd) {
	auto& d = *m_Data;
	EnsureLayout();
	std::vector<RibKeyTarget> kids;
	if (hwnd == m_hWnd) {
		// in the row's order: the quick access bar, the tabs, the minimize button
		std::vector<RibKeyTarget> row;
		AddRowTargets(row, nullptr, nullptr);
		for (auto& t : row)
			if (t.kind == Kind::Qat)
				kids.push_back(t);
		if (!::IsRectEmpty(&d.qatCustomize)) {
			RibKeyTarget t;
			t.kind = Kind::Customize;
			t.hwnd = m_hWnd;
			kids.push_back(t);
		}
		for (auto& t : row)
			if (t.kind == Kind::Tab)
				kids.push_back(t);
		if (!::IsRectEmpty(&d.minimizeButton)) {
			RibKeyTarget t;
			t.kind = Kind::Minimize;
			t.hwnd = m_hWnd;
			kids.push_back(t);
		}
		if (!m_Minimized)
			AddSurfaceTargets(kids, d.main, nullptr, nullptr);
	}
	else if (auto s = SurfaceOf(hwnd))
		AddSurfaceTargets(kids, *s, nullptr, nullptr);
	// hosted windows have their own objects
	kids.erase(std::remove_if(kids.begin(), kids.end(), [&](const RibKeyTarget& t) {
		auto item = AccItem(t);
		return t.kind == Kind::Item && item && item->kind == RibKind::Window;
	}), kids.end());
	return kids;
}

long CRibbonBarCtrl::AccChildId(const RibKeyTarget& t) {
	auto kids = AccChildren(t.hwnd);
	for (size_t k = 0; k < kids.size(); k++)
		if (SameTarget(kids[k], t))
			return (long)k + 1;
	return CHILDID_SELF;
}

long CRibbonBarCtrl::AccFocus(HWND hwnd) {
	auto& d = *m_Data;
	if (d.keyMode == RibKeyMode::Navigate && d.focus >= 0 && d.focus < (int)d.targets.size() && d.targets[d.focus].hwnd == hwnd)
		return AccChildId(d.targets[d.focus]);
	return CHILDID_SELF;
}

RibItem* CRibbonBarCtrl::AccItem(const RibKeyTarget& t) {
	auto& d = *m_Data;
	if (t.kind == Kind::Qat)
		return t.index >= 0 && t.index < (int)d.qat.size() ? d.FindItem(d.qat[t.index]) : nullptr;
	if (t.kind != Kind::Item)
		return nullptr;
	auto s = SurfaceOf(t.hwnd);
	if (!s || s->tab < 0 || s->tab >= (int)d.tabs.size() || t.group < 0 || t.group >= (int)d.tabs[s->tab].groups.size())
		return nullptr;
	auto& items = d.tabs[s->tab].groups[t.group].items;
	return t.item >= 0 && t.item < (int)items.size() ? &items[t.item] : nullptr;
}

std::wstring CRibbonBarCtrl::AccWindowName(HWND hwnd) {
	auto& d = *m_Data;
	if (hwnd == m_hWnd) {
		// the window's text (an app may name it), else "Ribbon"
		wchar_t text[128] = L"";
		::GetWindowTextW(m_hWnd, text, (int)std::size(text));
		return *text ? text : L"Ribbon";
	}
	auto s = SurfaceOf(hwnd);
	if (!s || s->tab < 0 || s->tab >= (int)d.tabs.size())
		return {};
	auto& tab = d.tabs[s->tab];
	return s->group >= 0 && s->group < (int)tab.groups.size() ? tab.groups[s->group].text : tab.text;
}

std::wstring CRibbonBarCtrl::AccName(const RibKeyTarget& t) {
	auto& d = *m_Data;
	switch (t.kind) {
		case Kind::Tab:
			return t.index >= 0 && t.index < (int)d.visible.size() ? d.tabs[d.visible[t.index]].text : std::wstring();
		case Kind::Customize:
			return L"Customize Quick Access Toolbar";
		case Kind::Minimize:
			return m_Minimized ? L"Expand the Ribbon" : L"Minimize the Ribbon";
		case Kind::Group:
		case Kind::Launcher: {
			auto s = SurfaceOf(t.hwnd);
			if (!s || s->tab < 0 || t.group < 0 || t.group >= (int)d.tabs[s->tab].groups.size())
				return {};
			auto& text = d.tabs[s->tab].groups[t.group].text;
			return t.kind == Kind::Group ? text : text + L" Options";
		}
		default:
			break;
	}
	auto item = AccItem(t);
	if (!item)
		return {};
	return item->text.empty() ? item->tip : item->text;
}

std::wstring CRibbonBarCtrl::AccDescription(const RibKeyTarget& t) {
	// the tooltip's description, else the group a command is in
	auto& d = *m_Data;
	auto item = AccItem(t);
	if (item && !item->tip.empty() && !item->text.empty())
		return item->tip;
	if (t.kind == Kind::Item)
		if (auto s = SurfaceOf(t.hwnd))
			return d.tabs[s->tab].groups[t.group].text;
	return {};
}

long CRibbonBarCtrl::AccState(const RibKeyTarget& t) {
	auto& d = *m_Data;
	long state = 0;
	bool focused = d.keyMode == RibKeyMode::Navigate && d.focus >= 0 && d.focus < (int)d.targets.size() && SameTarget(d.targets[d.focus], t);
	if (d.keyMode == RibKeyMode::Navigate)
		state |= STATE_SYSTEM_FOCUSABLE;
	if (focused)
		state |= STATE_SYSTEM_FOCUSED;
	auto s = SurfaceOf(t.hwnd);
	switch (t.kind) {
		case Kind::Tab:
			state |= STATE_SYSTEM_SELECTABLE;
			if (t.index >= 0 && t.index < (int)d.visible.size() && d.visible[t.index] == d.selected)
				state |= STATE_SYSTEM_SELECTED;
			if (d.hotTab == t.index)
				state |= STATE_SYSTEM_HOTTRACKED;
			return state;
		case Kind::Customize:
			return state | STATE_SYSTEM_HASPOPUP | (d.hotQat == RibData::Customize ? STATE_SYSTEM_HOTTRACKED : 0);
		case Kind::Minimize:
			return state | (d.hotTab == RibData::MinimizeButton ? STATE_SYSTEM_HOTTRACKED : 0);
		case Kind::Group:
			state |= STATE_SYSTEM_HASPOPUP;
			if (s && s->opened == t.group)
				state |= STATE_SYSTEM_EXPANDED;
			if (s && s->hot.part == RibPart::Group && s->hot.group == t.group)
				state |= STATE_SYSTEM_HOTTRACKED;
			return state;
		case Kind::Launcher:
			if (s && s->hot.part == RibPart::Launcher && s->hot.group == t.group)
				state |= STATE_SYSTEM_HOTTRACKED;
			return state;
		default:
			break;
	}
	auto item = AccItem(t);
	if (!item)
		return state;
	if (item->state & RibStateDisabled)
		state |= STATE_SYSTEM_UNAVAILABLE;
	if ((item->state & RibStateChecked) && (item->kind == RibKind::Toggle || item->kind == RibKind::Radio))
		state |= STATE_SYSTEM_CHECKED;
	if (item->HasArrow())
		state |= STATE_SYSTEM_HASPOPUP;
	if (t.kind == Kind::Qat) {
		if (d.hotQat == t.index)
			state |= STATE_SYSTEM_HOTTRACKED;
		if (d.pressedQat == t.index)
			state |= STATE_SYSTEM_PRESSED;
	}
	else if (s && s->hot.group == t.group && s->hot.item == t.item) {
		state |= STATE_SYSTEM_HOTTRACKED;
		if (Same(s->pressed, s->hot))
			state |= STATE_SYSTEM_PRESSED;
	}
	return state;
}

std::wstring CRibbonBarCtrl::AccShortcut(const RibKeyTarget& t) {
	// the key tips that reach it: "Alt, H, B" (the ribbon's own commands, while its tab is selected)
	auto& d = *m_Data;
	if (t.hwnd != m_hWnd || t.kind == Kind::Customize || t.kind == Kind::Minimize)
		return {};
	std::vector<RibKeyTarget> row;
	std::vector<std::wstring> labels, fixed;
	AddRowTargets(row, &labels, &fixed);
	auto rowTips = RibAssignKeyTips(labels, fixed);
	auto tipOf = [&](const std::vector<RibKeyTarget>& targets, const std::vector<std::wstring>& tips, const RibKeyTarget& x) {
		for (size_t k = 0; k < targets.size() && k < tips.size(); k++)
			if (SameTarget(targets[k], x))
				return tips[k];
		return std::wstring();
	};
	if (t.kind == Kind::Tab || t.kind == Kind::Qat) {
		auto tip = tipOf(row, rowTips, t);
		return tip.empty() ? tip : L"Alt, " + tip;
	}
	// the selected tab's, then its own among the tab's
	auto sel = std::find(d.visible.begin(), d.visible.end(), d.selected);
	if (sel == d.visible.end() || m_Minimized)
		return {};
	RibKeyTarget tab;
	tab.kind = Kind::Tab;
	tab.index = (int)(sel - d.visible.begin());
	tab.hwnd = m_hWnd;
	auto tabTip = tipOf(row, rowTips, tab);
	std::vector<RibKeyTarget> items;
	labels.clear();
	fixed.clear();
	AddSurfaceTargets(items, d.main, &labels, &fixed);
	auto tip = tipOf(items, RibAssignKeyTips(labels, fixed), t);
	if (tabTip.empty() || tip.empty())
		return {};
	return L"Alt, " + tabTip + L", " + tip;
}

void CRibbonBarCtrl::PostAccAction(const RibKeyTarget& t) {
	m_Data->accAction = t;
	PostMessage(WM_RIB_ACCACTION);
}

LRESULT CRibbonBarCtrl::OnAccAction(UINT, WPARAM, LPARAM, BOOL&) {
	// an MSAA client's default action, as the user's click
	auto& d = *m_Data;
	auto t = d.accAction;
	auto s = SurfaceOf(t.hwnd);
	EnsureLayout();
	switch (t.kind) {
		case Kind::Tab:
			if (t.index >= 0 && t.index < (int)d.visible.size())
				TabClicked(t.index);
			return 0;
		case Kind::Qat:
			RunQat(t.index);
			return 0;
		case Kind::Customize:
			CustomizeMenu();
			return 0;
		case Kind::Minimize:
			SetMinimized(!m_Minimized);
			return 0;
		case Kind::Group:
			if (s && t.group >= 0 && t.group < (int)s->places.size())
				OpenGroupPopup(*s, t.group);
			return 0;
		case Kind::Launcher:
			if (s)
				RunLauncher(*s, t.group);
			return 0;
		case Kind::Item:
			if (auto item = AccItem(t)) {
				if (item->HasArrow() && item->kind == RibKind::DropDown)
					DropDown(*s, t.group, t.item);
				else
					Execute(s->tab, t.group, t.item);
			}
			return 0;
	}
	return 0;
}
