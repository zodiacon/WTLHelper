// Accessibility (MSAA, which UI Automation clients see through its proxy). One IAccessible class asks its window
// (NtfAccSource) about its children. The info bar is a grouping named "Notifications" whose children are, for each
// row shown, the message (an alert, named by its title and described by its text) and then its links, checkbox,
// buttons and close button; then the "N more" strip (a link). A toast is an alert itself, its parts its children. A new
// message raises EVENT_SYSTEM_ALERT, which screen readers announce. Each object wraps the system's one for its window
// and passes on what it doesn't answer itself; a name is never taken from the window's own OBJID_WINDOW object, which
// would ask this one again.

#include "NotifyData.h"

#include <oleacc.h>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

using namespace NtfDraw;

class NtfAccessible : public IAccessible {
public:
	NtfAccessible(NtfAccSource* source, HWND hwnd, IAccessible* proxy) : m_Source(source), m_Hwnd(hwnd), m_Proxy(proxy) {}
	virtual ~NtfAccessible() = default;

	// the window is going: calls from clients still holding this fail from now on
	void Disconnect() {
		m_Source = nullptr;
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
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		return m_Proxy->get_accParent(parent);
	}
	STDMETHODIMP get_accChildCount(long* count) override {
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		*count = m_Source->AccCount();
		return S_OK;
	}
	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		*dispatch = nullptr;
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id != Invalid && id != CHILDID_SELF ? S_FALSE : E_INVALIDARG;   // simple elements
	}
	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		return Str(child, name, [&](long id) { return m_Source->AccName(id); });
	}
	STDMETHODIMP get_accDescription(VARIANT child, BSTR* text) override {
		return Str(child, text, [&](long id) { return m_Source->AccDescription(id); });
	}
	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		return Str(child, action, [&](long id) { return m_Source->AccAction(id); });
	}
	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id == Invalid ? E_INVALIDARG : Long(role, m_Source->AccRole(id));
	}
	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		return id == Invalid ? E_INVALIDARG : Long(state, m_Source->AccState(id));
	}
	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (m_Source->AccAction(id).empty())
			return DISP_E_MEMBERNOTFOUND;
		m_Source->AccDoAction(id);
		return S_OK;
	}
	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		::VariantInit(focus);
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		int id = m_Source->AccFocus();
		return id < 0 ? S_FALSE : Long(focus, id);
	}
	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		::VariantInit(selection);
		return m_Source ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP accSelect(long, VARIANT) override {
		return m_Source ? DISP_E_MEMBERNOTFOUND : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP accLocation(long* x, long* y, long* cx, long* cy, VARIANT child) override {
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		if (id == CHILDID_SELF)
			return m_Proxy->accLocation(x, y, cx, cy, child);
		RECT rc = m_Source->AccRect(id);
		::MapWindowPoints(m_Hwnd, nullptr, (POINT*)&rc, 2);
		*x = rc.left;
		*y = rc.top;
		*cx = rc.right - rc.left;
		*cy = rc.bottom - rc.top;
		return S_OK;
	}
	STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* end) override {
		::VariantInit(end);
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long count = m_Source->AccCount();
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
	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		::VariantInit(child);
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		POINT pt{ x, y };
		::ScreenToClient(m_Hwnd, &pt);
		RECT client;
		::GetClientRect(m_Hwnd, &client);
		if (!::PtInRect(&client, pt))
			return S_FALSE;
		return Long(child, m_Source->AccHitTest(pt));
	}
	STDMETHODIMP get_accValue(VARIANT, BSTR* value) override {
		*value = nullptr;
		return m_Source ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* key) override {
		*key = nullptr;
		return m_Source ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return m_Source ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return m_Source ? S_FALSE : RPC_E_DISCONNECTED;
	}
	STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
	STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
	static constexpr long Invalid = -2;

	// a child id of 1..count, CHILDID_SELF, or Invalid
	long Id(const VARIANT& child) const {
		if (child.vt != VT_I4)
			return Invalid;
		if (child.lVal == CHILDID_SELF)
			return CHILDID_SELF;
		return child.lVal >= 1 && child.lVal <= m_Source->AccCount() ? child.lVal : Invalid;
	}

	template<typename F>
	HRESULT Str(const VARIANT& child, BSTR* out, F&& get) {
		*out = nullptr;
		if (!m_Source)
			return RPC_E_DISCONNECTED;
		long id = Id(child);
		if (id == Invalid)
			return E_INVALIDARG;
		auto s = get(id);
		if (s.empty())
			return S_FALSE;
		*out = ::SysAllocString(s.c_str());
		return *out ? S_OK : E_OUTOFMEMORY;
	}

	static HRESULT Long(VARIANT* out, long value) {
		out->vt = VT_I4;
		out->lVal = value;
		return S_OK;
	}

	NtfAccSource*             m_Source;
	HWND                      m_Hwnd;
	ATL::CComPtr<IAccessible> m_Proxy;
	LONG                      m_Refs = 1;
};

LRESULT NtfAccObject(NtfAccessible*& slot, NtfAccSource* source, HWND hwnd, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!slot) {
		ATL::CComPtr<IAccessible> proxy;
		if (FAILED(::CreateStdAccessibleObject(hwnd, OBJID_CLIENT, IID_PPV_ARGS(&proxy))) || !proxy)
			return 0;
		slot = new NtfAccessible(source, hwnd, proxy);   // the window's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(slot));
}

void NtfAccDisconnect(NtfAccessible*& slot) {
	if (slot) {
		slot->Disconnect();
		slot->Release();
		slot = nullptr;
	}
}

// ---- A message's parts -------------------------------------------------------------------------------

std::wstring NtfAcc::Name(const NtfItem& item, const NtfHit& part, const std::wstring& close) {
	auto& m = item.m;
	switch (part.part) {
		case NtfPart::None: {
			// the message: its title, else its text
			if (!m.title.empty())
				return m.title;
			return NtfPlainText(m.runs);
		}
		case NtfPart::Link:
			return part.index >= 0 && part.index < (int)item.layout.textRuns.size() ? item.layout.textRuns[part.index].text : std::wstring();
		case NtfPart::Check: return m.checkText;
		case NtfPart::Button: return part.index >= 0 && part.index < (int)m.buttons.size() ? m.buttons[part.index].text : std::wstring();
		case NtfPart::Close: return close;
		default: return {};
	}
}

std::wstring NtfAcc::Description(const NtfItem& item, const NtfHit& part) {
	if (part.part != NtfPart::None)
		return {};
	auto& m = item.m;
	// the text (when the title named it), the progress, the details shown
	std::wstring text = m.title.empty() ? std::wstring() : NtfPlainText(m.runs);
	if (m.progressKind == NtfProgress::Value)
		text += (text.empty() ? L"" : L" ") + std::to_wstring(m.progress) + L"%";
	if (m.expanded && !m.details.empty())
		text += (text.empty() ? L"" : L"\n") + m.details;
	return text;
}

long NtfAcc::Role(const NtfHit& part) {
	switch (part.part) {
		case NtfPart::None: return ROLE_SYSTEM_ALERT;
		case NtfPart::Link: case NtfPart::More: return ROLE_SYSTEM_LINK;
		case NtfPart::Check: return ROLE_SYSTEM_CHECKBUTTON;
		default: return ROLE_SYSTEM_PUSHBUTTON;
	}
}

long NtfAcc::State(const NtfItem& item, const NtfHit& part) {
	switch (part.part) {
		case NtfPart::None: return STATE_SYSTEM_READONLY;
		case NtfPart::Link: return STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_LINKED;
		case NtfPart::Check: return STATE_SYSTEM_FOCUSABLE | (item.m.checked ? STATE_SYSTEM_CHECKED : 0);
		default: return STATE_SYSTEM_FOCUSABLE;
	}
}

std::wstring NtfAcc::Action(const NtfItem& item, const NtfHit& part) {
	switch (part.part) {
		case NtfPart::Link: case NtfPart::More: return L"Jump";
		case NtfPart::Check: return item.m.checked ? L"Uncheck" : L"Check";
		case NtfPart::Button: case NtfPart::Close: return L"Press";
		default: return {};
	}
}

// ---- The info bar --------------------------------------------------------------------------------------

class NtfBarAcc : public NtfAccSource {
public:
	explicit NtfBarAcc(CInfoBarCtrl* bar) : m_Bar(bar) {
	}

	int AccCount() override {
		return (int)Children().size();
	}
	std::wstring AccName(int child) override {
		if (child == 0)
			return m_Bar->String(NTFSTR_NOTIFICATIONS);
		auto hit = Child(child);
		if (hit.part == NtfPart::More) {
			auto& d = *m_Bar->m_Data;
			return d.hidden > 0 ? m_Bar->String(NTFSTR_MORE, d.hidden) : m_Bar->String(NTFSTR_FEWER);
		}
		return NtfAcc::Name(Item(hit), hit, m_Bar->String(NTFSTR_CLOSE));
	}
	std::wstring AccDescription(int child) override {
		auto hit = Child(child);
		return child == 0 || hit.part == NtfPart::More ? std::wstring() : NtfAcc::Description(Item(hit), hit);
	}
	long AccRole(int child) override {
		return child == 0 ? ROLE_SYSTEM_GROUPING : NtfAcc::Role(Child(child));
	}
	long AccState(int child) override {
		auto& d = *m_Bar->m_Data;
		if (child == 0)
			return STATE_SYSTEM_FOCUSABLE | (::GetFocus() == m_Bar->m_hWnd ? STATE_SYSTEM_FOCUSED : 0);
		auto hit = Child(child);
		long s = hit.part == NtfPart::More ? STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_LINKED : NtfAcc::State(Item(hit), hit);
		if (d.hot == hit)
			s |= STATE_SYSTEM_HOTTRACKED;
		if (d.pressed == hit && d.hot == hit)
			s |= STATE_SYSTEM_PRESSED;
		if (hit.part != NtfPart::None && ::GetFocus() == m_Bar->m_hWnd) {
			auto order = m_Bar->FocusOrder();
			int i = m_Bar->FocusIndex(order);
			if (i >= 0 && order[i] == hit)
				s |= STATE_SYSTEM_FOCUSED;
		}
		return s;
	}
	std::wstring AccAction(int child) override {
		if (child == 0)
			return {};
		auto hit = Child(child);
		return hit.part == NtfPart::More ? L"Jump" : NtfAcc::Action(Item(hit), hit);
	}
	void AccDoAction(int child) override {
		auto hit = Child(child);
		const UINT id = hit.item >= 0 ? m_Bar->m_Data->items[hit.item].m.id : 0;
		::PostMessage(m_Bar->m_hWnd, WM_NTF_ACCACTION, id, MAKELPARAM((WORD)hit.part, (WORD)(hit.index + 1)));
	}
	RECT AccRect(int child) override {
		auto& d = *m_Bar->m_Data;
		auto hit = Child(child);
		if (hit.part == NtfPart::More)
			return d.strip;
		for (size_t r = 0; r < d.rowItems.size(); r++)
			if (d.rowItems[r] == hit.item) {
				auto& item = d.items[hit.item];
				const int y = d.rowTops[r] + d.rowHeights[r] - item.layout.height;
				RECT row{ 0, d.rowTops[r], d.width, d.rowTops[r] + d.rowHeights[r] };
				if (hit.part == NtfPart::None)
					return row;
				RECT rc{};
				for (auto& one : PartRects(item, hit, d.m))
					::UnionRect(&rc, &rc, &one);
				::OffsetRect(&rc, 0, y);
				return rc;
			}
		return {};
	}
	int AccFocus() override {
		if (::GetFocus() != m_Bar->m_hWnd)
			return -1;
		auto order = m_Bar->FocusOrder();
		int i = m_Bar->FocusIndex(order);
		return i < 0 ? 0 : Id(order[i]);
	}
	int AccHitTest(POINT pt) override {
		auto hit = m_Bar->HitTest(pt);
		if (hit.part == NtfPart::None && hit.item < 0)
			return 0;
		return Id(hit);   // a row's background: its message
	}

	// the bar's children: each row's message and its parts, then the strip
	std::vector<NtfHit> Children() const {
		auto& d = *m_Bar->m_Data;
		m_Bar->EnsureLayout();
		std::vector<NtfHit> kids;
		for (int item : d.rowItems) {
			if (d.items[item].going)
				continue;
			kids.push_back({ item, NtfPart::None });
			for (auto part : FocusParts(d.items[item])) {
				part.item = item;
				kids.push_back(part);
			}
		}
		if (!::IsRectEmpty(&d.strip))
			kids.push_back({ -1, NtfPart::More });
		return kids;
	}
	int Id(const NtfHit& hit) const {
		auto kids = Children();
		auto it = std::find(kids.begin(), kids.end(), hit);
		return it == kids.end() ? 0 : (int)(it - kids.begin()) + 1;
	}

private:
	NtfHit Child(int child) const {
		auto kids = Children();
		return child >= 1 && child <= (int)kids.size() ? kids[child - 1] : NtfHit{};
	}
	const NtfItem& Item(const NtfHit& hit) const {
		return m_Bar->m_Data->items[hit.item];
	}

	CInfoBarCtrl* m_Bar;
};

LRESULT CInfoBarCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if (!m_AccSource)
		m_AccSource = std::make_unique<NtfBarAcc>(this);
	return NtfAccObject(d.acc, m_AccSource.get(), m_hWnd, wParam, lParam, bHandled);
}

LRESULT CInfoBarCtrl::OnAccAction(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	NtfHit hit{ -1, (NtfPart)LOWORD(lParam), (int)HIWORD(lParam) - 1 };
	if (hit.part != NtfPart::More) {
		hit.item = Find((UINT)wParam);
		if (hit.item < 0)
			return 0;
	}
	if (hit.part != NtfPart::None) {
		m_Data->focus = hit;   // as the keys would have
		Click(hit);
	}
	return 0;
}

void CInfoBarCtrl::NotifyAccessible(DWORD event, const NtfHit& part) {
	// an alert goes out always (screen readers listen for them); the rest once a client asked
	if (!IsWindow() || (event != EVENT_SYSTEM_ALERT && !m_Data->acc))
		return;
	NtfBarAcc acc(this);
	long child = part.part == NtfPart::None && part.item < 0 ? CHILDID_SELF : acc.Id(part);
	::NotifyWinEvent(event, m_hWnd, OBJID_CLIENT, child ? child : CHILDID_SELF);
}
