// Accessibility (MSAA, which UI Automation clients see through its proxy): the grid is an outline, each
// row an outline item with the name, the value as shown, the description and the state (selected,
// expanded, read-only, checked for a set flag or a true Bool).

#include "PropertyGrid.h"
#include "PropertyGridModel.h"

#include <oleacc.h>

#pragma comment(lib, "oleacc.lib")

class PgAccessible : public IAccessible {
public:
	PgAccessible(CPropertyGridCtrl* grid, IAccessible* proxy) : m_Grid(grid), m_Proxy(proxy) {}

	// the grid is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_Grid = nullptr;
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

	// IAccessible
	STDMETHODIMP get_accParent(IDispatch** parent) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		*count = (long)m_Grid->m_Rows.size();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		return Row(child) >= 0 ? S_FALSE : E_INVALIDARG;   // rows are simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self) {
			// the window's own name (its label, in a dialog), else a generic one
			if (SUCCEEDED(m_Proxy->get_accName(child, name)) && *name)
				return S_OK;
			*name = ::SysAllocString(L"Properties");
			return S_OK;
		}
		if (row < 0)
			return E_INVALIDARG;
		*name = ::SysAllocString(m_Grid->RowName(row).c_str());
		return S_OK;
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row < 0)
			return E_INVALIDARG;
		*value = ::SysAllocString(m_Grid->RowValue(row).c_str());
		return S_OK;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* description) override {
		*description = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self)
			return m_Proxy->get_accDescription(child, description);
		if (row < 0)
			return E_INVALIDARG;
		auto& r = m_Grid->m_Rows[row];
		auto text = r.flag >= 0 ? L"A flag of " + r.node->name + L"." : r.node->description;
		if (text.empty())
			return S_FALSE;
		*description = ::SysAllocString(text.c_str());
		return S_OK;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row < Self)
			return E_INVALIDARG;
		role->vt = VT_I4;
		role->lVal = row == Self ? ROLE_SYSTEM_OUTLINE : ROLE_SYSTEM_OUTLINEITEM;
		return S_OK;
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self)
			return m_Proxy->get_accState(child, state);
		if (row < 0)
			return E_INVALIDARG;
		auto& g = *m_Grid;
		auto& r = g.m_Rows[row];
		auto node = r.node;
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;
		if (row == g.SelectedRow()) {
			s |= STATE_SYSTEM_SELECTED;
			if (::GetFocus() == g.m_hWnd)
				s |= STATE_SYSTEM_FOCUSED;
		}
		if (r.flag < 0 && (node->IsCategory() || g.HasVisibleChildren(node)))
			s |= node->expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
		if (!node->IsCategory() && g.IsReadOnly(node))
			s |= STATE_SYSTEM_READONLY;
		if (row < g.m_TopRow || row >= g.m_TopRow + g.GetPageRows())
			s |= STATE_SYSTEM_OFFSCREEN;
		// check boxes: a flag row, a Bool
		auto& v = node->value.value;
		if (r.flag >= 0 && v.type == PRGVT_INT && PgIsFlagSet(v.llVal, node->enumItems[r.flag].value))
			s |= STATE_SYSTEM_CHECKED;
		else if (r.flag < 0 && v.type == PRGVT_BOOL && v.boolVal)
			s |= STATE_SYSTEM_CHECKED;
		state->vt = VT_I4;
		state->lVal = s;
		return S_OK;
	}

	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Grid ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Grid ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
		*shortcut = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		return Row(child) == Self ? m_Proxy->get_accKeyboardShortcut(child, shortcut) : S_FALSE;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		::VariantInit(focus);
		if (::GetFocus() != m_Grid->m_hWnd)
			return S_FALSE;
		int row = m_Grid->SelectedRow();
		focus->vt = VT_I4;
		focus->lVal = row >= 0 ? row + 1 : CHILDID_SELF;
		return S_OK;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		::VariantInit(selection);
		int row = m_Grid->SelectedRow();
		if (row < 0)
			return S_FALSE;
		selection->vt = VT_I4;
		selection->lVal = row + 1;
		return S_OK;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row < 0)
			return E_INVALIDARG;
		auto text = DefaultAction(row);
		if (!text)
			return S_FALSE;
		*action = ::SysAllocString(text);
		return S_OK;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row < 0)
			return row == Self ? S_FALSE : E_INVALIDARG;
		if (flags & SELFLAG_TAKEFOCUS)
			m_Grid->SetFocus();
		if (flags & (SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION))
			m_Grid->SelectRow(row);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row == Self)
			return m_Proxy->accLocation(left, top, width, height, child);
		if (row < 0)
			return E_INVALIDARG;
		auto grid = m_Grid->GetGridRect();
		RECT rc{ grid.left, grid.top + (row - m_Grid->m_TopRow) * m_Grid->m_RowHeight, grid.right, 0 };
		rc.bottom = rc.top + m_Grid->m_RowHeight;
		::MapWindowPoints(m_Grid->m_hWnd, nullptr, (POINT*)&rc, 2);
		*left = rc.left;
		*top = rc.top;
		*width = rc.right - rc.left;
		*height = rc.bottom - rc.top;
		return S_OK;
	}

	STDMETHODIMP accNavigate(long direction, VARIANT start, VARIANT* end) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		::VariantInit(end);
		int row = Row(start);
		int count = (int)m_Grid->m_Rows.size();
		int target = -1;
		if (row == Self) {
			if (direction == NAVDIR_FIRSTCHILD)
				target = 0;
			else if (direction == NAVDIR_LASTCHILD)
				target = count - 1;
			else
				return m_Proxy->accNavigate(direction, start, end);   // siblings of the window
		}
		else if (row >= 0) {
			if (direction == NAVDIR_NEXT || direction == NAVDIR_DOWN)
				target = row + 1;
			else if (direction == NAVDIR_PREVIOUS || direction == NAVDIR_UP)
				target = row - 1;
		}
		else
			return E_INVALIDARG;
		if (target < 0 || target >= count)
			return S_FALSE;
		end->vt = VT_I4;
		end->lVal = target + 1;
		return S_OK;
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		::VariantInit(child);
		POINT pt{ x, y };
		m_Grid->ScreenToClient(&pt);
		RECT rc;
		m_Grid->GetClientRect(&rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		UINT flags;
		int row = m_Grid->HitRow(pt, &flags);
		child->vt = VT_I4;
		child->lVal = row >= 0 ? row + 1 : CHILDID_SELF;
		return S_OK;
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Grid)
			return RPC_E_DISCONNECTED;
		int row = Row(child);
		if (row < 0)
			return row == Self ? S_FALSE : E_INVALIDARG;
		if (!DefaultAction(row))
			return DISP_E_MEMBERNOTFOUND;
		auto& g = *m_Grid;
		auto r = g.m_Rows[row];
		if (r.flag >= 0)
			g.ToggleFlag(r.node, r.flag);
		else if (!r.node->IsCategory() && PgEditType(*r.node) == PRGVT_BOOL)
			g.ToggleBool(r.node);
		else if (g.CanEdit(r.node) && !g.IsReadOnly(r.node))
			g.BeginEdit(r.node);
		else
			g.SetExpanded(r.node, !r.node->expanded, true);
		return S_OK;
	}

	STDMETHODIMP put_accName(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP put_accValue(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}

private:
	static constexpr int Self = -1;
	static constexpr int Invalid = -2;

	// the row a child id names: Self for the grid, Invalid for no row
	int Row(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return Self;
		int row = child.lVal - 1;
		return row >= 0 && row < (int)m_Grid->m_Rows.size() ? row : Invalid;
	}

	const wchar_t* DefaultAction(int row) const {
		auto& g = *m_Grid;
		auto& r = g.m_Rows[row];
		if (r.flag >= 0 || (!r.node->IsCategory() && PgEditType(*r.node) == PRGVT_BOOL))
			return g.IsReadOnly(r.node) ? nullptr : L"Toggle";
		if (g.CanEdit(r.node) && !g.IsReadOnly(r.node))
			return L"Edit";
		if (r.node->IsCategory() || g.HasVisibleChildren(r.node))
			return r.node->expanded ? L"Collapse" : L"Expand";
		return nullptr;
	}

	CPropertyGridCtrl*         m_Grid;
	ATL::CComPtr<IAccessible>  m_Proxy;   // the system's, for the window itself
	LONG                       m_Refs = 1;
};

// ---- The grid's side ------------------------------------------------------------------------

LRESULT CPropertyGridCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Accessible) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy)))) {
			bHandled = FALSE;
			return 0;
		}
		m_Accessible = new PgAccessible(this, proxy);   // the grid's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Accessible));
}

void CPropertyGridCtrl::DisconnectAccessible() {
	if (!m_Accessible)
		return;
	m_Accessible->Disconnect();
	m_Accessible->Release();
	m_Accessible = nullptr;
}

void CPropertyGridCtrl::NotifyAccessible(DWORD event, int row) {
	if (IsWindow())
		::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, row < 0 ? CHILDID_SELF : row + 1);
}

std::wstring CPropertyGridCtrl::RowName(int row) const {
	auto& r = m_Rows[row];
	return r.flag >= 0 ? r.node->enumItems[r.flag].name : r.node->name;
}

std::wstring CPropertyGridCtrl::RowValue(int row) const {
	auto& r = m_Rows[row];
	auto node = r.node;
	if (node->IsCategory())
		return {};
	QueryValue(node);
	auto& v = node->value.value;
	if (r.flag >= 0)
		return v.type == PRGVT_INT && PgIsFlagSet(v.llVal, node->enumItems[r.flag].value) ? L"True" : L"False";
	if (node->flags & PRGF_PASSWORD)
		return {};   // not read out
	return PgFormatValue(*node);
}
