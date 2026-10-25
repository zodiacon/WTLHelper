// Accessibility (MSAA, which UI Automation clients see through its proxy). The control is an outline whose children
// are the rows shown (the collapsed summaries' tasks aren't), in order: each an outline item named by its task, its
// value its level ("1" at the top, as a tree view's), described by its dates, duration and progress, the links that
// come into it and its markers; selected and focused as the control's selection is. A summary's default action
// expands or collapses it, a task's is a double click (GNTN_DBLCLK). Default actions are posted, so the app's handlers
// don't run inside the client's call. The object wraps the system's one for the window and passes on what it doesn't
// answer; its name is never taken from the window's own OBJID_WINDOW object, which would ask this one again.

#include "GanttData.h"

#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

class GntAccessible : public IAccessible {
public:
	GntAccessible(CGanttCtrl* ctrl, IAccessible* proxy) : m_Ctrl(ctrl), m_Proxy(proxy) {}
	virtual ~GntAccessible() = default;

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
		if (id == CHILDID_SELF)
			return m_Proxy->get_accName(child, name);   // the window's text, or the label before it in a dialog
		return Text(name, Task(id).name.c_str());
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		*value = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		return Text(value, std::to_wstring(Task(id).depth + 1).c_str());   // the level, as a tree view's items have
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		*text = nullptr;
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return S_FALSE;
		return Text(text, m_Ctrl->AccDescription(Row(id)).c_str());
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		return Long(role, id == CHILDID_SELF ? ROLE_SYSTEM_OUTLINE : ROLE_SYSTEM_OUTLINEITEM);
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		const bool focused = ::GetFocus() == m_Ctrl->m_hWnd;
		if (id == CHILDID_SELF)
			return Long(state, STATE_SYSTEM_FOCUSABLE | (focused ? STATE_SYSTEM_FOCUSED : 0)
				| ((m_Ctrl->GetStyle() & GNTS_READONLY) ? STATE_SYSTEM_READONLY : 0));
		auto& d = *m_Ctrl->m_Data;
		auto& t = Task(id);
		long s = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
		if (t.id == d.selected)
			s |= STATE_SYSTEM_SELECTED | (focused ? STATE_SYSTEM_FOCUSED : 0);
		if (t.summary)
			s |= t.expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
		RECT rc = RowRect(id), view = d.grid;
		view.right = d.chart.right;
		if (!::IntersectRect(&rc, &rc, &view))
			s |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
		if (d.hot.row == id - 1 && (d.hot.area == GntArea::Grid || d.hot.area == GntArea::Chart))
			s |= STATE_SYSTEM_HOTTRACKED;
		return Long(state, s);
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
		auto& t = Task(id);
		return Text(action, t.summary ? (t.expanded ? L"Collapse" : L"Expand") : L"Double Click");
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		// later, as the user's: the app's handlers mustn't run inside the client's call
		::PostMessage(m_Ctrl->m_hWnd, CGanttCtrl::WM_GNT_ACCACTION, 0, Task(id).id);
		return S_OK;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		if (::GetFocus() != m_Ctrl->m_hWnd)
			return S_FALSE;
		return Long(focus, m_Ctrl->AccChildId(m_Ctrl->m_Data->selected));
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = m_Ctrl->AccChildId(m_Ctrl->m_Data->selected);
		return id != CHILDID_SELF ? Long(selection, id) : S_FALSE;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		if (!m_Ctrl)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid || id == CHILDID_SELF)
			return E_INVALIDARG;
		if (flags & (SELFLAG_ADDSELECTION | SELFLAG_REMOVESELECTION | SELFLAG_EXTENDSELECTION))
			return E_INVALIDARG;   // one task is selected at a time
		if (flags & SELFLAG_TAKEFOCUS)
			::SetFocus(m_Ctrl->m_hWnd);
		if (flags & SELFLAG_TAKESELECTION)
			::PostMessage(m_Ctrl->m_hWnd, CGanttCtrl::WM_GNT_ACCACTION, 1, Task(id).id);
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
		RECT rc = RowRect(id);
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
		else if (dir == NAVDIR_NEXT || dir == NAVDIR_DOWN)
			target = id + 1;
		else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_UP)
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
		::ScreenToClient(m_Ctrl->m_hWnd, &pt);
		RECT client;
		::GetClientRect(m_Ctrl->m_hWnd, &client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		// the name being edited: its own object
		HWND under = ::ChildWindowFromPointEx(m_Ctrl->m_hWnd, pt, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		if (under && under != m_Ctrl->m_hWnd)
			return m_Proxy->accHitTest(x, y, child);
		auto hit = m_Ctrl->HitTest(pt);
		return Long(child, hit.row >= 0 ? hit.row + 1 : CHILDID_SELF);
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

	long Count() const {
		return (long)m_Ctrl->m_Data->rows.size();
	}

	// a child id of 1..count, CHILDID_SELF, or Invalid
	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= Count() ? child.lVal : Invalid;
	}

	int Row(long id) const {
		return m_Ctrl->m_Data->rows[id - 1];   // the task's index
	}

	const GntTask& Task(long id) const {
		return m_Ctrl->m_Data->project.Tasks()[Row(id)];
	}

	// across the grid and the chart (scrolled out of view, outside them)
	RECT RowRect(long id) const {
		auto& d = *m_Ctrl->m_Data;
		const int y = d.grid.top + (int)(id - 1) * d.m.rowHeight - d.scrollY;
		return { d.grid.left, y, d.chart.right, y + d.m.rowHeight };
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

	CGanttCtrl*               m_Ctrl;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

// ---- What the items say ------------------------------------------------------------------------------

std::wstring CGanttCtrl::AccDescription(int task) const {
	auto& d = *m_Data;
	auto& t = d.project.Tasks()[task];
	std::wstring s;
	if (t.Milestone())
		s = L"Milestone, " + CellText(task, GNTCOL_START);
	else {
		s = (t.summary ? L"Summary, start " : L"Start ") + CellText(task, GNTCOL_START) + L", finish " + CellText(task, GNTCOL_FINISH)
			+ L", " + CellText(task, GNTCOL_DURATION) + L", " + CellText(task, GNTCOL_PROGRESS) + L" done";
	}
	// what it waits for
	static const wchar_t* const Kinds[] = { L"after", L"starts with", L"finishes with", L"finishes after the start of" };
	for (auto& l : d.project.Links())
		if (l.to == t.id)
			if (auto from = d.project.Find(l.from))
				s += L"; " + std::wstring(Kinds[(int)l.type]) + L" " + from->name;
	return s;
}

long CGanttCtrl::AccChildId(UINT id) const {
	auto& d = *m_Data;
	const int task = id ? d.project.Index(id) : -1;
	auto it = std::find(d.rows.begin(), d.rows.end(), task);
	return task < 0 || it == d.rows.end() ? CHILDID_SELF : (long)(it - d.rows.begin()) + 1;
}

// ---- The object --------------------------------------------------------------------------------------

LRESULT CGanttCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	auto& d = *m_Data;
	if (!d.acc) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		d.acc = new GntAccessible(this, proxy);   // the window's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(d.acc));
}

void CGanttCtrl::NotifyAccessible(DWORD event, long child) {
	// no client has asked yet: nobody to tell
	if (m_Data->acc && IsWindow())
		::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, child);
}

void CGanttCtrl::DisconnectAccessible() {
	auto& d = *m_Data;
	if (d.acc) {
		d.acc->Disconnect();
		d.acc->Release();
		d.acc = nullptr;
	}
}

LRESULT CGanttCtrl::OnAccAction(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	const UINT id = (UINT)lParam;
	auto t = d.project.Find(id);
	if (!t)
		return 0;
	if (wParam == 1) {
		// selected, as a click would
		SetSelected(id, true);
		EnsureVisible(id);
		return 0;
	}
	if (t->summary) {
		t->expanded = !t->expanded;
		Changed();
		NotifyTask(GNTN_EXPANDED, id);
		NotifyAccessible(EVENT_OBJECT_STATECHANGE, AccChildId(id));
	}
	else
		NotifyTask(GNTN_DBLCLK, id);
	return 0;
}
