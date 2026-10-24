// Accessibility (MSAA, which UI Automation clients see through its proxy): the control is a list of its rows. A row
// is named by its text (a changed pair side by side: both lines), described by its line numbers and what happened
// to it; a fold is named by the lines it hides, and its default action opens it. The caret's row is the focused and
// selected item. Events (focus, selection, the rows rebuilt) are sent only once a client has asked for the object.

#include "DiffView.h"
#include "DiffViewModel.h"

#include <oleacc.h>

#include <algorithm>
#include <climits>
#include <string>

#pragma comment(lib, "oleacc.lib")

class DfAccessible : public IAccessible {
public:
	DfAccessible(CDiffViewCtrl* view, IAccessible* proxy) : m_View(view), m_Proxy(proxy) {}

	// the view is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_View = nullptr;
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
		if (!m_View)
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		*count = (long)std::min<size_t>(Rows().size(), LONG_MAX - 1);
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		return row != Invalid && row != Self ? S_FALSE : E_INVALIDARG;   // rows are simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self) {
			// the standard object's name (the control's label, in a dialog); never the window's own OBJID_WINDOW
			// object, which would ask this one again
			if (SUCCEEDED(m_Proxy->get_accName(child, name)) && *name && **name)
				return S_OK;
			::SysFreeString(*name);
			*name = ::SysAllocString(L"Differences");
			return S_OK;
		}
		if (row == Invalid)
			return E_INVALIDARG;
		*name = ::SysAllocString(RowName(row).c_str());
		return S_OK;
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		return Row(child) == Invalid ? E_INVALIDARG : S_FALSE;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* description) override {
		*description = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self) {
			DFVINFO info{};
			m_View->GetInfo(&info);
			wchar_t text[160];
			if (info.changes == 0)
				swprintf_s(text, L"No differences, %zu lines", info.leftLines);
			else
				swprintf_s(text, L"%zu changes: %zu lines deleted, %zu inserted", info.changes, info.deleted, info.inserted);
			*description = ::SysAllocString(text);
			return S_OK;
		}
		if (row == Invalid)
			return E_INVALIDARG;
		*description = ::SysAllocString(RowDescription(row).c_str());
		return S_OK;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Invalid)
			return E_INVALIDARG;
		role->vt = VT_I4;
		role->lVal = row == Self ? ROLE_SYSTEM_LIST : ROLE_SYSTEM_LISTITEM;
		return S_OK;
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->get_accState(child, state);
		if (row == Invalid)
			return E_INVALIDARG;
		auto& v = *m_View;
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_READONLY;
		if (Rows()[row].kind == DfRowKind::Fold)
			s |= STATE_SYSTEM_COLLAPSED;
		if ((int)row == v.CaretRow()) {
			s |= STATE_SYSTEM_SELECTED;
			if (::GetFocus() == v.m_hWnd)
				s |= STATE_SYSTEM_FOCUSED;
		}
		if (row < v.m_TopRow || row >= v.m_TopRow + v.PageRows())
			s |= STATE_SYSTEM_OFFSCREEN;
		state->vt = VT_I4;
		state->lVal = s;
		return S_OK;
	}

	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_View ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_View ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
		*shortcut = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		return Row(child) == Self ? m_Proxy->get_accKeyboardShortcut(child, shortcut) : S_FALSE;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		::VariantInit(focus);
		if (::GetFocus() != m_View->m_hWnd)
			return S_FALSE;
		focus->vt = VT_I4;
		focus->lVal = CaretChild();
		return S_OK;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		::VariantInit(selection);
		// the caret's row stands for the selection (which may span many rows)
		long child = CaretChild();
		if (child == CHILDID_SELF)
			return S_FALSE;
		selection->vt = VT_I4;
		selection->lVal = child;
		return S_OK;
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		*action = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		if (Rows()[row].kind != DfRowKind::Fold)
			return S_FALSE;
		*action = ::SysAllocString(L"Expand");
		return S_OK;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		auto& v = *m_View;
		if (flags & SELFLAG_TAKEFOCUS)
			v.SetFocus();
		// the caret to the row's line (side by side, on the caret's side if it has one there)
		DfPos pos;
		bool side = v.m_Caret.right != FALSE;
		if (!v.m_Doc->LineOfRow(row, side, pos) && !v.m_Doc->LineOfRow(row, !side, pos))
			return S_FALSE;
		DFVPOS at{ pos.right ? TRUE : FALSE, pos.line, 0 };
		if (flags & SELFLAG_TAKESELECTION)
			v.SetSelection(at, { at.right, at.line, v.m_Doc->Lines(pos.right)[pos.line].size() });
		else if (flags & (SELFLAG_TAKEFOCUS | SELFLAG_ADDSELECTION))
			v.MoveCaret(at, false, true);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return m_Proxy->accLocation(left, top, width, height, child);
		if (row == Invalid)
			return E_INVALIDARG;
		auto& v = *m_View;
		auto rc = v.GetRowsRect();
		long long y = rc.top + ((long long)row - (long long)v.m_TopRow) * v.m_RowHeight;
		RECT item{ rc.left, (int)std::clamp(y, -(long long)INT_MAX / 2, (long long)INT_MAX / 2), rc.right, 0 };
		item.bottom = item.top + v.m_RowHeight;
		::MapWindowPoints(v.m_hWnd, nullptr, (POINT*)&item, 2);
		*left = item.left;
		*top = item.top;
		*width = item.right - item.left;
		*height = item.bottom - item.top;
		return S_OK;
	}

	STDMETHODIMP accNavigate(long direction, VARIANT start, VARIANT* end) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		::VariantInit(end);
		auto row = Row(start);
		size_t count = std::min<size_t>(Rows().size(), LONG_MAX - 1);
		long long target = -1;
		if (row == Self) {
			if (direction == NAVDIR_FIRSTCHILD)
				target = 0;
			else if (direction == NAVDIR_LASTCHILD)
				target = (long long)count - 1;
			else
				return m_Proxy->accNavigate(direction, start, end);   // siblings of the window
		}
		else if (row != Invalid) {
			if (direction == NAVDIR_NEXT || direction == NAVDIR_DOWN)
				target = (long long)row + 1;
			else if (direction == NAVDIR_PREVIOUS || direction == NAVDIR_UP)
				target = (long long)row - 1;
		}
		else {
			return E_INVALIDARG;
		}
		if (target < 0 || target >= (long long)count)
			return S_FALSE;
		end->vt = VT_I4;
		end->lVal = (long)target + 1;
		return S_OK;
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		::VariantInit(child);
		DFVHITTEST hit{ { x, y } };
		m_View->ScreenToClient(&hit.pt);
		RECT rc;
		m_View->GetClientRect(&rc);
		if (!::PtInRect(&rc, hit.pt))
			return S_FALSE;
		m_View->HitTest(&hit);
		child->vt = VT_I4;
		bool onRow = hit.part != DFVHT_NOWHERE && hit.part != DFVHT_TITLE && hit.part != DFVHT_OVERVIEW && hit.part != DFVHT_BELOW && hit.part != DFVHT_MERGE;
		child->lVal = onRow ? Child(hit.row) : CHILDID_SELF;
		return S_OK;
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self)
			return S_FALSE;
		if (row == Invalid)
			return E_INVALIDARG;
		if (Rows()[row].kind != DfRowKind::Fold)
			return S_FALSE;
		m_View->ExpandFold(row, true);   // as the user's click
		return S_OK;
	}

	STDMETHODIMP put_accName(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}
	STDMETHODIMP put_accValue(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}

private:
	static constexpr size_t Self = (size_t)-1;
	static constexpr size_t Invalid = (size_t)-2;

	const std::vector<DfRow>& Rows() const {
		return m_View->m_Doc->rows;
	}

	// the row a child id names: Self for the list, Invalid for no row
	size_t Row(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return Self;
		if (child.lVal < 1)
			return Invalid;
		size_t row = (size_t)child.lVal - 1;
		return row < Rows().size() ? row : Invalid;
	}

	static long Child(size_t row) {
		return row < LONG_MAX - 1 ? (long)row + 1 : CHILDID_SELF;
	}

	long CaretChild() const {
		int row = m_View->CaretRow();
		return row >= 0 ? Child((size_t)row) : CHILDID_SELF;
	}

	// a line's text without its indentation (or trailing spaces), which a screen reader would read out
	std::wstring Text(bool right, int line) const {
		auto& lines = m_View->m_Doc->Lines(right);
		if (line < 0 || (size_t)line >= lines.size())
			return {};
		auto text = lines[line];
		auto first = text.find_first_not_of(L" \t");
		if (first == std::wstring_view::npos)
			return {};
		return std::wstring(text.substr(first, text.find_last_not_of(L" \t") - first + 1));
	}

	std::wstring RowName(size_t index) const {
		auto& row = Rows()[index];
		if (row.kind == DfRowKind::Fold)
			return std::to_wstring(row.hidden) + L" unchanged lines";
		if (row.kind == DfRowKind::Changed && row.left >= 0 && row.right >= 0)
			return Text(false, row.left) + L"  \x2192  " + Text(true, row.right);
		return row.left >= 0 ? Text(false, row.left) : Text(true, row.right);
	}

	std::wstring RowDescription(size_t index) const {
		auto& row = Rows()[index];
		auto number = [](int line) { return std::to_wstring(line + 1); };
		switch (row.kind) {
			case DfRowKind::Fold:
				return L"Lines " + number(row.left) + L" to " + number(row.left + row.hidden - 1) + L" on the left, " +
					number(row.right) + L" to " + number(row.right + row.hidden - 1) + L" on the right, folded";
			case DfRowKind::Equal:
				return row.left == row.right ? L"Line " + number(row.left) + L", unchanged" :
					L"Left line " + number(row.left) + L", right line " + number(row.right) + L", unchanged";
			default:
				if (row.left >= 0 && row.right >= 0)
					return L"Left line " + number(row.left) + L" changed into right line " + number(row.right);
				if (row.left >= 0)
					return L"Left line " + number(row.left) + L", deleted";
				return L"Right line " + number(row.right) + L", inserted";
		}
	}

	CDiffViewCtrl*             m_View;
	ATL::CComPtr<IAccessible>  m_Proxy;   // the system's, for the window itself
	LONG                       m_Refs = 1;
};

// ---- The view's side ------------------------------------------------------------------------

LRESULT CDiffViewCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
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
		m_Accessible = new DfAccessible(this, proxy);   // the view's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Accessible));
}

void CDiffViewCtrl::DisconnectAccessible() {
	if (!m_Accessible)
		return;
	m_Accessible->Disconnect();
	m_Accessible->Release();
	m_Accessible = nullptr;
}

void CDiffViewCtrl::NotifyCaretAccessible() {
	if (!m_Accessible || !IsWindow())
		return;   // no client has asked yet: nobody to tell
	int row = CaretRow();
	if (row < 0 || row >= LONG_MAX - 1)
		return;
	if (::GetFocus() == m_hWnd)
		::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, (LONG)row + 1);
	::NotifyWinEvent(EVENT_OBJECT_SELECTION, m_hWnd, OBJID_CLIENT, (LONG)row + 1);
}

void CDiffViewCtrl::NotifyRowsAccessible() {
	if (m_Accessible && IsWindow())
		::NotifyWinEvent(EVENT_OBJECT_REORDER, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
}
