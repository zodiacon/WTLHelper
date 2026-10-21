// Accessibility (MSAA, which UI Automation clients see through its proxy): the text area is a list, each line
// shown (a row) a list item named by its text, described by its number and level. The caret's line is the
// focused item; lines with selected text are selected. Lines coming in fire no events (a busy log would flood a
// screen reader): clients ask when they need to.

#include "LogView.h"
#include "LogViewModel.h"
#include "LogViewText.h"

#include <oleacc.h>

#include <algorithm>
#include <climits>
#include <iterator>

#pragma comment(lib, "oleacc.lib")

class LgAccessible : public IAccessible {
public:
	LgAccessible(CLogViewCtrl* view, IAccessible* proxy) : m_View(view), m_Proxy(proxy) {}

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
		*count = (long)std::min<size_t>(m_View->RowCount(), LONG_MAX - 1);
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		return Row(child) != Invalid && Row(child) != Self ? S_FALSE : E_INVALIDARG;   // rows are simple elements
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		*name = nullptr;
		if (!m_View)
			return RPC_E_DISCONNECTED;
		auto row = Row(child);
		if (row == Self) {
			// the control's own name (its label, in a dialog), else a generic one
			ATL::CComPtr<IAccessible> outer;
			if (SUCCEEDED(::AccessibleObjectFromWindow(m_View->m_hWnd, OBJID_WINDOW, IID_PPV_ARGS(&outer)))) {
				VARIANT self{};
				self.vt = VT_I4;
				self.lVal = CHILDID_SELF;
				if (SUCCEEDED(outer->get_accName(self, name)) && *name)
					return S_OK;
			}
			*name = ::SysAllocString(L"Log");
			return S_OK;
		}
		if (row == Invalid)
			return E_INVALIDARG;
		*name = ::SysAllocString(m_View->GetLineText(m_View->LineOfRow(row)));
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
		if (row == Self)
			return m_Proxy->get_accDescription(child, description);
		if (row == Invalid)
			return E_INVALIDARG;
		LGVLINEINFO info;
		if (!m_View->GetLineInfo(m_View->LineOfRow(row), &info))
			return S_FALSE;
		static const wchar_t* const levels[] = { nullptr, L"trace", L"debug", L"info", L"warning", L"error", L"critical" };
		wchar_t text[64];
		if (info.level < std::size(levels) && levels[info.level])
			swprintf_s(text, L"Line %llu, %s", info.number, levels[info.level]);
		else
			swprintf_s(text, L"Line %llu", info.number);
		*description = ::SysAllocString(text);
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
		size_t line = v.LineOfRow(row);
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_READONLY;
		size_t start, end;
		bool lineBreak;
		if (v.SelectionInLine(line, start, end, lineBreak))
			s |= STATE_SYSTEM_SELECTED;
		if (line == v.m_Caret.line && ::GetFocus() == v.m_Text->m_hWnd)
			s |= STATE_SYSTEM_FOCUSED;
		if (row < v.m_TopRow || row >= v.m_TopRow + v.GetPageLines())
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
		if (::GetFocus() != m_View->m_Text->m_hWnd)
			return S_FALSE;
		focus->vt = VT_I4;
		focus->lVal = CaretChild();
		return S_OK;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		if (!m_View)
			return RPC_E_DISCONNECTED;
		::VariantInit(selection);
		// the caret's line stands for the selection (which may span many lines)
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
		*action = ::SysAllocString(L"Activate");
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
		size_t line = v.LineOfRow(row);
		if (flags & SELFLAG_TAKEFOCUS)
			v.m_Text->SetFocus();
		// the whole line selected, or just the caret at its start
		if (flags & SELFLAG_TAKESELECTION)
			v.Select({ line, 0 }, { line, v.LineLength(line) }, true);
		else if (flags & SELFLAG_TAKEFOCUS)
			v.MoveCaret({ line, 0 }, false, true);
		v.EnsureCaretVisible(true);
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
		auto rc = v.GetTextRect();
		long long y = ((long long)row - (long long)v.m_TopRow) * v.m_LineHeight;
		RECT item{ rc.left, (int)std::clamp(y, -(long long)INT_MAX / 2, (long long)INT_MAX / 2), rc.right, 0 };
		item.bottom = item.top + v.m_LineHeight;
		::MapWindowPoints(v.m_Text->m_hWnd, nullptr, (POINT*)&item, 2);
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
		size_t count = std::min<size_t>(m_View->RowCount(), LONG_MAX - 1);
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
		POINT pt{ x, y };
		m_View->ScreenToClient(&pt);
		RECT rc;
		m_View->GetClientRect(&rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		size_t line = m_View->HitTest(pt);
		child->vt = VT_I4;
		child->lVal = line == LGV_NOLINE ? CHILDID_SELF : Child(m_View->RowOfLine(line));
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
		RECT rc{};
		size_t line = m_View->LineOfRow(row);
		POINT pt{};
		if (m_View->GetLineRect(line, &rc)) {
			pt = { rc.left, rc.bottom };
			m_View->ClientToScreen(&pt);
		}
		m_View->NotifyLine(LGVN_ACTIVATE, line, pt);
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

	// the row a child id names: Self for the list, Invalid for no row
	size_t Row(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return Self;
		if (child.lVal < 1)
			return Invalid;
		size_t row = (size_t)child.lVal - 1;
		return row < m_View->RowCount() ? row : Invalid;
	}

	static long Child(size_t row) {
		return row < LONG_MAX - 1 ? (long)row + 1 : CHILDID_SELF;
	}

	long CaretChild() const {
		auto& v = *m_View;
		return v.IsLineShown(v.m_Caret.line) ? Child(v.RowOfLine(v.m_Caret.line)) : CHILDID_SELF;
	}

	CLogViewCtrl*              m_View;
	ATL::CComPtr<IAccessible>  m_Proxy;   // the system's, for the text window itself
	LONG                       m_Refs = 1;
};

// ---- The view's side ------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnTextGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Accessible) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_Text->m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy)))) {
			bHandled = FALSE;
			return 0;
		}
		m_Accessible = new LgAccessible(this, proxy);   // the view's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Accessible));
}

void CLogViewCtrl::DisconnectAccessible() {
	if (!m_Accessible)
		return;
	m_Accessible->Disconnect();
	m_Accessible->Release();
	m_Accessible = nullptr;
}

void CLogViewCtrl::NotifyCaretAccessible() {
	if (!m_Accessible || !m_Text->IsWindow() || !IsLineShown(m_Caret.line))
		return;   // no client has asked yet: nobody to tell
	size_t row = RowOfLine(m_Caret.line);
	if (row >= LONG_MAX - 1)
		return;
	if (::GetFocus() == m_Text->m_hWnd)
		::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_Text->m_hWnd, OBJID_CLIENT, (LONG)row + 1);
	::NotifyWinEvent(EVENT_OBJECT_SELECTION, m_Text->m_hWnd, OBJID_CLIENT, (LONG)row + 1);
}
