// Accessibility (MSAA, which UI Automation clients see through its proxy): the list window is a list, each result
// row a list item named by its label ("File: Open"), described by its shortcut, its default action running it. The
// keyboard stays in the search box, so the selected row is reported as focused while the box has the focus (as an
// active descendant): moving the selection sends focus and selection events for it, and a screen reader reads the
// row. Events are sent only once a client has asked for the object. The box and the list have no labels of their
// own: they're named through IAccPropServices.

#include "CommandPalette.h"
#include "CommandPaletteList.h"
#include "CommandPaletteModel.h"

#include <oleacc.h>

#include <algorithm>
#include <climits>

#pragma comment(lib, "oleacc.lib")

// oleacc.h declares these but no library defines them (and the ATL headers include it before initguid.h could)
static const CLSID AccPropServices = { 0xb5f8350b, 0x0548, 0x48b1, { 0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7 } };
static const MSAAPROPID AccName = { 0x608d3df8, 0x8128, 0x4aa7, { 0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91 } };

class CmpAccessible : public IAccessible {
public:
	CmpAccessible(CCommandPaletteCtrl* palette, IAccessible* proxy) : m_Palette(palette), m_Proxy(proxy) {}

	// the palette is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_Palette = nullptr;
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
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		*count = (long)std::min<size_t>(m_Palette->Results().size(), LONG_MAX - 1);
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		return row >= 0 ? S_FALSE : E_INVALIDARG;   // rows are simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->get_accName(child, name);   // the name given through IAccPropServices
		if (row == Invalid)
			return E_INVALIDARG;
		*name = ::SysAllocString(m_Palette->RowItem(row).label.c_str());
		return S_OK;
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		return Row(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* description) override {
		*description = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self) {
			wchar_t text[64];
			auto count = m_Palette->Results().size();
			swprintf_s(text, count == 1 ? L"%zu result" : L"%zu results", count);
			*description = ::SysAllocString(text);
			return S_OK;
		}
		if (row == Invalid)
			return E_INVALIDARG;
		// the shortcut (or a provider's detail), and whether it was used lately
		auto& item = m_Palette->RowItem(row);
		std::wstring text = item.shortcut;
		if (m_Palette->Results()[row].recent >= 0)
			text += text.empty() ? L"Recently used" : L", recently used";
		if (text.empty())
			return S_FALSE;
		*description = ::SysAllocString(text.c_str());
		return S_OK;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Invalid)
			return E_INVALIDARG;
		role->vt = VT_I4;
		role->lVal = row == Self ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM;
		return S_OK;
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->get_accState(child, state);
		if (row == Invalid)
			return E_INVALIDARG;
		auto& p = *m_Palette;
		auto& item = p.RowItem(row);
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_READONLY;
		if (item.state & CMDPIS_DISABLED)
			s |= STATE_SYSTEM_UNAVAILABLE;
		if (item.state & CMDPIS_CHECKED)
			s |= STATE_SYSTEM_CHECKED;
		if (row == p.m_Selected) {
			s |= STATE_SYSTEM_SELECTED;
			if (::GetFocus() == p.m_Box.m_hWnd)
				s |= STATE_SYSTEM_FOCUSED;   // the box's active descendant
		}
		if (row < p.m_Top || row >= p.m_Top + p.PageRows())
			s |= STATE_SYSTEM_OFFSCREEN;
		state->vt = VT_I4;
		state->lVal = s;
		return S_OK;
	}

	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Palette ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Palette ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
		*shortcut = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->get_accKeyboardShortcut(child, shortcut);
		if (row == Invalid)
			return E_INVALIDARG;
		// a command's shortcut is the app's key for it
		auto& item = m_Palette->RowItem(row);
		if (m_Palette->m_Mode != 0 || item.shortcut.empty())
			return S_FALSE;
		*shortcut = ::SysAllocString(item.shortcut.c_str());
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		::VariantInit(focus);
		if (::GetFocus() != m_Palette->m_Box.m_hWnd || m_Palette->m_Selected < 0)
			return S_FALSE;
		focus->vt = VT_I4;
		focus->lVal = m_Palette->m_Selected + 1;
		return S_OK;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		::VariantInit(selection);
		if (m_Palette->m_Selected < 0)
			return S_FALSE;
		selection->vt = VT_I4;
		selection->lVal = m_Palette->m_Selected + 1;
		return S_OK;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		*action = ::SysAllocString(m_Palette->m_Mode < 0 ? L"Choose" : L"Run");
		return S_OK;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		if (flags & SELFLAG_TAKEFOCUS)
			m_Palette->m_Box.SetFocus();
		if (flags & (SELFLAG_TAKESELECTION | SELFLAG_TAKEFOCUS | SELFLAG_ADDSELECTION))
			m_Palette->Select(row, true);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->accLocation(left, top, width, height, child);
		if (row == Invalid)
			return E_INVALIDARG;
		auto& p = *m_Palette;
		RECT rc;
		p.m_List->GetClientRect(&rc);
		RECT item{ 0, (row - p.m_Top) * p.m_RowHeight, rc.right, 0 };
		item.bottom = item.top + p.m_RowHeight;
		::MapWindowPoints(p.m_List->m_hWnd, nullptr, (POINT*)&item, 2);
		*left = item.left;
		*top = item.top;
		*width = item.right - item.left;
		*height = item.bottom - item.top;
		return S_OK;
	}

	STDMETHODIMP accNavigate(long direction, VARIANT start, VARIANT* end) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		::VariantInit(end);
		auto row = Row(start);
		int count = (int)std::min<size_t>(m_Palette->Results().size(), LONG_MAX - 1);
		int target = -1;
		if (row == Self) {
			if (direction == NAVDIR_FIRSTCHILD)
				target = 0;
			else if (direction == NAVDIR_LASTCHILD)
				target = count - 1;
			else
				return m_Proxy->accNavigate(direction, start, end);   // the window's siblings
		}
		else if (row != Invalid) {
			if (direction == NAVDIR_NEXT || direction == NAVDIR_DOWN)
				target = row + 1;
			else if (direction == NAVDIR_PREVIOUS || direction == NAVDIR_UP)
				target = row - 1;
		}
		else {
			return E_INVALIDARG;
		}
		if (target < 0 || target >= count)
			return S_FALSE;
		end->vt = VT_I4;
		end->lVal = target + 1;
		return S_OK;
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		::VariantInit(child);
		POINT pt{ x, y };
		m_Palette->m_List->ScreenToClient(&pt);
		RECT rc;
		m_Palette->m_List->GetClientRect(&rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		int row = m_Palette->RowAt(pt);
		child->vt = VT_I4;
		child->lVal = row >= 0 ? row + 1 : CHILDID_SELF;
		return S_OK;
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Palette)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		m_Palette->Execute(row);   // as the user's click
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

	// the row a child id names: Self for the list, Invalid for no row
	int Row(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return Self;
		if (child.lVal < 1 || (size_t)child.lVal > m_Palette->Results().size())
			return Invalid;
		return (int)child.lVal - 1;
	}

	CCommandPaletteCtrl*       m_Palette;
	ATL::CComPtr<IAccessible>  m_Proxy;   // the system's, for the list window itself
	LONG                       m_Refs = 1;
};

// ---- The palette's side ------------------------------------------------------------------------

LRESULT CCommandPaletteCtrl::OnListGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Accessible) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_List->m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy)))) {
			bHandled = FALSE;
			return 0;
		}
		m_Accessible = new CmpAccessible(this, proxy);   // the palette's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Accessible));
}

void CCommandPaletteCtrl::DisconnectAccessible() {
	if (!m_Accessible)
		return;
	m_Accessible->Disconnect();
	m_Accessible->Release();
	m_Accessible = nullptr;
}

void CCommandPaletteCtrl::NameAccessible() {
	// the box and the list have no labels to be named by
	ATL::CComPtr<IAccPropServices> props;
	if (FAILED(props.CoCreateInstance(AccPropServices)))
		return;   // COM isn't initialized on the thread: the names are the system's
	props->SetHwndPropStr(m_Box.m_hWnd, (DWORD)OBJID_CLIENT, CHILDID_SELF, AccName, L"Search commands");
	props->SetHwndPropStr(m_List->m_hWnd, (DWORD)OBJID_CLIENT, CHILDID_SELF, AccName, L"Commands");
}

void CCommandPaletteCtrl::NotifySelectionAccessible() {
	if (!m_Accessible || !m_List->IsWindow() || m_Selected < 0)
		return;   // no client has asked yet: nobody to tell
	LONG child = m_Selected + 1;
	if (::GetFocus() == m_Box.m_hWnd)
		::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_List->m_hWnd, OBJID_CLIENT, child);
	::NotifyWinEvent(EVENT_OBJECT_SELECTION, m_List->m_hWnd, OBJID_CLIENT, child);
}

void CCommandPaletteCtrl::NotifyResultsAccessible() {
	if (m_Accessible && m_List->IsWindow())
		::NotifyWinEvent(EVENT_OBJECT_REORDER, m_List->m_hWnd, OBJID_CLIENT, CHILDID_SELF);
}
