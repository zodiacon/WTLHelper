// Accessibility (MSAA, which UI Automation clients see through its proxy): the grid is a table whose children are its
// rows in view (a group's row too, expanded or collapsed, and the new row's placeholder), each a row whose children are
// its cells, named by their shown text and described by their column's title. Rows and cells are objects of their own
// (DgAccessible, made as they're asked for), so a client can walk down to a cell. The caret's cell is the focused
// element: EVENT_OBJECT_FOCUS names it by a child ID of its own (CaretChild) that the table resolves to it. A cell's
// value can be set (an edit, through DGVN_VALIDATE), and its default action toggles a check box, opens the editor or
// activates it.

#include "DataGridData.h"

#include <oleacc.h>

#include <algorithm>
#include <climits>
#include <memory>

#pragma comment(lib, "oleacc.lib")

struct DgAccLink {
	CDataGridCtrl* grid = nullptr;          // null once the grid is gone
	ATL::CComPtr<IAccessible> proxy;        // the system's, for the window itself
};

class DgAccessible : public IAccessible {
public:
	enum class Kind { Table, Row, Cell };
	static constexpr long CaretChild = LONG_MAX;   // the table's child ID for the caret's cell (focus events)

	DgAccessible(std::shared_ptr<DgAccLink> link, Kind kind, int64_t row = -1, int col = -1)
		: m_Link(std::move(link)), m_Kind(kind), m_Row(row), m_Col(col) {}

	void Disconnect() {
		m_Link->grid = nullptr;
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

// a child named by its ID answers for itself
#define DG_FORWARD(child, call)                                       \
	{                                                                 \
		ATL::CComPtr<DgAccessible> other;                             \
		HRESULT hr_ = Resolve(child, other);                          \
		if (FAILED(hr_))                                              \
			return hr_;                                               \
		if (other)                                                    \
			return other->call;                                       \
	}

	// IAccessible
	STDMETHODIMP get_accParent(IDispatch** parent) override {
		if (!parent)
			return E_POINTER;
		*parent = nullptr;
		if (!Alive())
			return RPC_E_DISCONNECTED;
		switch (m_Kind) {
			case Kind::Table:
				return m_Link->proxy->get_accParent(parent);
			case Kind::Row:
				return Table()->QueryInterface(IID_IDispatch, (void**)parent);
			case Kind::Cell:
				*parent = Make(Kind::Row, m_Row);
				return S_OK;
		}
		return E_FAIL;
	}

	STDMETHODIMP get_accChildCount(long* count) override {
		if (!count)
			return E_POINTER;
		*count = 0;
		if (!Alive())
			return RPC_E_DISCONNECTED;
		if (m_Kind == Kind::Table)
			*count = (long)std::min<int64_t>(D().RowCount(), LONG_MAX - 1);
		else if (m_Kind == Kind::Row && D().rows.GroupOf(m_Row) < 0)
			*count = D().ColCount();
		return S_OK;
	}

	STDMETHODIMP get_accChild(VARIANT child, IDispatch** dispatch) override {
		if (!dispatch)
			return E_POINTER;
		*dispatch = nullptr;
		if (!Alive())
			return RPC_E_DISCONNECTED;
		if (child.vt != VT_I4 || child.lVal == CHILDID_SELF)
			return E_INVALIDARG;
		*dispatch = MakeChild(child.lVal);
		return *dispatch ? S_OK : E_INVALIDARG;
	}

	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override {
		if (!name)
			return E_POINTER;
		*name = nullptr;
		DG_FORWARD(child, get_accName(Self(), name));
		auto& d = D();
		switch (m_Kind) {
			case Kind::Table: {
				// the control's own name (its text or label, in a dialog) from the system's client object, else a
				// generic one. Not the window object's: with no text, it asks the client for the name, that's this
				// again, without end.
				if (SUCCEEDED(m_Link->proxy->get_accName(Self(), name)) && *name && **name)
					return S_OK;
				::SysFreeString(*name);
				*name = ::SysAllocString(L"Data grid");
				return S_OK;
			}
			case Kind::Row: {
				std::wstring text;
				if (int64_t g = d.rows.GroupOf(m_Row); g >= 0 && g < (int64_t)d.view.groups.size()) {
					auto& group = d.view.groups[(size_t)g];
					auto column = d.columns.Find(group.id);
					text = (column ? column->title + L": " : L"")
						+ (group.value.type == DgType::Empty ? L"(Blank)" : column ? DgValueText(group.value, *column, d.numberFormat) : L"")
						+ L", " + std::to_wstring(group.count) + (group.count == 1 ? L" row" : L" rows");
				}
				else if (d.rows.IsNewRow(m_Row)) {
					text = L"New row";
				}
				else {
					text = L"Row " + std::to_wstring(m_Row + 1);
				}
				*name = ::SysAllocString(text.c_str());
				return S_OK;
			}
			case Kind::Cell:
				*name = ::SysAllocString(CellText().c_str());
				return S_OK;
		}
		return E_FAIL;
	}

	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override {
		if (!value)
			return E_POINTER;
		*value = nullptr;
		DG_FORWARD(child, get_accValue(Self(), value));
		if (m_Kind != Kind::Cell)
			return S_FALSE;
		*value = ::SysAllocString(CellText().c_str());
		return S_OK;
	}

	STDMETHODIMP get_accDescription(VARIANT child, BSTR* description) override {
		if (!description)
			return E_POINTER;
		*description = nullptr;
		DG_FORWARD(child, get_accDescription(Self(), description));
		if (m_Kind == Kind::Table)
			return m_Link->proxy->get_accDescription(Self(), description);
		if (m_Kind != Kind::Cell)
			return S_FALSE;
		auto column = D().columns.Find(ColumnId());
		if (!column)
			return S_FALSE;
		*description = ::SysAllocString(column->title.c_str());
		return S_OK;
	}

	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
		if (!role)
			return E_POINTER;
		::VariantInit(role);
		DG_FORWARD(child, get_accRole(Self(), role));
		role->vt = VT_I4;
		role->lVal = m_Kind == Kind::Table ? ROLE_SYSTEM_TABLE : m_Kind == Kind::Row ? ROLE_SYSTEM_ROW : ROLE_SYSTEM_CELL;
		return S_OK;
	}

	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
		if (!state)
			return E_POINTER;
		::VariantInit(state);
		DG_FORWARD(child, get_accState(Self(), state));
		if (m_Kind == Kind::Table)
			return m_Link->proxy->get_accState(Self(), state);
		auto& d = D();
		long s = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;
		if (m_Row < d.topRow || m_Row >= d.topRow + DgFullRows(d.layout, d.clientHeight))
			s |= STATE_SYSTEM_OFFSCREEN;
		if (m_Kind == Kind::Row) {
			if (int64_t g = d.rows.GroupOf(m_Row); g >= 0 && g < (int64_t)d.view.groups.size())
				s |= d.collapsed.count(d.view.groups[(size_t)g].key) ? STATE_SYSTEM_COLLAPSED : STATE_SYSTEM_EXPANDED;
			if (d.selection.RowTouched(m_Row))
				s |= STATE_SYSTEM_SELECTED;
		}
		else {
			if (d.selection.Contains(m_Row, m_Col))
				s |= STATE_SYSTEM_SELECTED;
			if (IsCaret() && ::GetFocus() == G()->m_hWnd)
				s |= STATE_SYSTEM_FOCUSED;
			int64_t data = d.rows.ToData(m_Row);
			if (!G()->CanEditCell(data, ColumnId()))
				s |= STATE_SYSTEM_READONLY;
			auto column = d.columns.Find(ColumnId());
			DgCell cell;
			if (column && column->type == DgColType::Bool && G()->Fetch(data, column->id, cell) && DgChecked(cell.value))
				s |= STATE_SYSTEM_CHECKED;
			if (d.rows.ToData(m_Row) < 0 && !d.rows.IsNewRow(m_Row))
				s |= STATE_SYSTEM_UNAVAILABLE;
		}
		state->vt = VT_I4;
		state->lVal = s;
		return S_OK;
	}

	STDMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
		*help = nullptr;
		return Alive() ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override {
		*file = nullptr;
		*topic = 0;
		return Alive() ? S_FALSE : RPC_E_DISCONNECTED;
	}

	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
		if (!shortcut)
			return E_POINTER;
		*shortcut = nullptr;
		DG_FORWARD(child, get_accKeyboardShortcut(Self(), shortcut));
		return m_Kind == Kind::Table ? m_Link->proxy->get_accKeyboardShortcut(Self(), shortcut) : S_FALSE;
	}

	STDMETHODIMP get_accFocus(VARIANT* focus) override {
		if (!focus)
			return E_POINTER;
		::VariantInit(focus);
		if (!Alive())
			return RPC_E_DISCONNECTED;
		auto& d = D();
		if (::GetFocus() != G()->m_hWnd || !d.HasCaret())
			return S_FALSE;
		auto caret = d.CaretPos();
		if (m_Kind == Kind::Cell) {
			if (!IsCaret())
				return S_FALSE;
			focus->vt = VT_I4;
			focus->lVal = CHILDID_SELF;
			return S_OK;
		}
		if (m_Kind == Kind::Row && caret.row != m_Row)
			return S_FALSE;
		focus->vt = VT_DISPATCH;
		focus->pdispVal = Make(Kind::Cell, caret.row, caret.col);
		return S_OK;
	}

	STDMETHODIMP get_accSelection(VARIANT* selection) override {
		if (!selection)
			return E_POINTER;
		::VariantInit(selection);
		return Alive() ? S_FALSE : RPC_E_DISCONNECTED;   // the states say which are selected
	}

	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
		if (!action)
			return E_POINTER;
		*action = nullptr;
		DG_FORWARD(child, get_accDefaultAction(Self(), action));
		auto& d = D();
		const wchar_t* text = nullptr;
		if (m_Kind == Kind::Row) {
			if (int64_t g = d.rows.GroupOf(m_Row); g >= 0 && g < (int64_t)d.view.groups.size())
				text = d.collapsed.count(d.view.groups[(size_t)g].key) ? L"Expand" : L"Collapse";
		}
		else if (m_Kind == Kind::Cell) {
			auto column = d.columns.Find(ColumnId());
			int64_t data = d.rows.ToData(m_Row);
			if (column && column->type == DgColType::Bool && G()->CanEditCell(data, column->id))
				text = L"Toggle";
			else if (column && G()->CanEditCell(data, column->id))
				text = L"Edit";
			else if (data >= 0)
				text = L"Activate";
		}
		if (!text)
			return S_FALSE;
		*action = ::SysAllocString(text);
		return S_OK;
	}

	STDMETHODIMP accSelect(long flags, VARIANT child) override {
		DG_FORWARD(child, accSelect(flags, Self()));
		if (m_Kind == Kind::Table)
			return S_FALSE;
		auto& d = D();
		auto* g = G();
		if (flags & SELFLAG_TAKEFOCUS)
			g->SetFocus();
		if (d.rows.GroupOf(m_Row) >= 0)
			return S_FALSE;   // the caret isn't on groups' rows
		int col = m_Kind == Kind::Cell ? m_Col : std::max(0, d.CaretPos().col);
		using Op = CDataGridCtrl::SelOp;
		Op op = flags & SELFLAG_EXTENDSELECTION ? Op::Extend : flags & SELFLAG_ADDSELECTION ? Op::Add : Op::Reset;
		if (flags & (SELFLAG_TAKESELECTION | SELFLAG_ADDSELECTION | SELFLAG_EXTENDSELECTION | SELFLAG_TAKEFOCUS))
			g->Select(op, m_Row, col, true, m_Kind == Kind::Row);
		return S_OK;
	}

	STDMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
		if (!left || !top || !width || !height)
			return E_POINTER;
		DG_FORWARD(child, accLocation(left, top, width, height, Self()));
		if (m_Kind == Kind::Table)
			return m_Link->proxy->accLocation(left, top, width, height, Self());
		auto& d = D();
		auto& L = d.layout;
		long long y = L.header + (m_Row - d.topRow) * (long long)L.rowHeight;
		y = std::clamp(y, (long long)INT_MIN / 2, (long long)INT_MAX / 2);
		RECT rc{ 0, (int)y, d.clientWidth, (int)y + L.rowHeight };
		if (m_Kind == Kind::Cell) {
			rc.left = L.Left(m_Col, d.scrollX);
			rc.right = L.Right(m_Col, d.scrollX);
		}
		::MapWindowPoints(G()->m_hWnd, nullptr, (POINT*)&rc, 2);
		*left = rc.left;
		*top = rc.top;
		*width = rc.right - rc.left;
		*height = rc.bottom - rc.top;
		return S_OK;
	}

	STDMETHODIMP accNavigate(long direction, VARIANT start, VARIANT* end) override {
		if (!end)
			return E_POINTER;
		::VariantInit(end);
		DG_FORWARD(start, accNavigate(direction, Self(), end));
		auto& d = D();
		int64_t rows = std::min<int64_t>(d.RowCount(), LONG_MAX - 1);
		int cols = d.ColCount();
		IDispatch* to = nullptr;
		switch (m_Kind) {
			case Kind::Table:
				if (direction == NAVDIR_FIRSTCHILD && rows > 0)
					to = Make(Kind::Row, 0);
				else if (direction == NAVDIR_LASTCHILD && rows > 0)
					to = Make(Kind::Row, rows - 1);
				else if (direction != NAVDIR_FIRSTCHILD && direction != NAVDIR_LASTCHILD)
					return m_Link->proxy->accNavigate(direction, Self(), end);   // the window's siblings
				break;
			case Kind::Row: {
				bool cells = d.rows.GroupOf(m_Row) < 0 && cols > 0;
				if ((direction == NAVDIR_NEXT || direction == NAVDIR_DOWN) && m_Row + 1 < rows)
					to = Make(Kind::Row, m_Row + 1);
				else if ((direction == NAVDIR_PREVIOUS || direction == NAVDIR_UP) && m_Row > 0)
					to = Make(Kind::Row, m_Row - 1);
				else if (direction == NAVDIR_FIRSTCHILD && cells)
					to = Make(Kind::Cell, m_Row, 0);
				else if (direction == NAVDIR_LASTCHILD && cells)
					to = Make(Kind::Cell, m_Row, cols - 1);
				break;
			}
			case Kind::Cell:
				if ((direction == NAVDIR_NEXT || direction == NAVDIR_RIGHT) && m_Col + 1 < cols)
					to = Make(Kind::Cell, m_Row, m_Col + 1);
				else if ((direction == NAVDIR_PREVIOUS || direction == NAVDIR_LEFT) && m_Col > 0)
					to = Make(Kind::Cell, m_Row, m_Col - 1);
				else if (direction == NAVDIR_UP || direction == NAVDIR_DOWN) {   // the same column, past groups' rows
					int step = direction == NAVDIR_UP ? -1 : 1;
					for (int64_t r = m_Row + step; r >= 0 && r < rows; r += step)
						if (d.rows.GroupOf(r) < 0) {
							to = Make(Kind::Cell, r, m_Col);
							break;
						}
				}
				break;
		}
		if (!to)
			return S_FALSE;
		end->vt = VT_DISPATCH;
		end->pdispVal = to;
		return S_OK;
	}

	STDMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
		if (!child)
			return E_POINTER;
		::VariantInit(child);
		if (!Alive())
			return RPC_E_DISCONNECTED;
		auto* g = G();
		POINT pt{ x, y };
		g->ScreenToClient(&pt);
		RECT rc;
		g->GetClientRect(&rc);
		if (!::PtInRect(&rc, pt))
			return S_FALSE;
		DGVHITTEST hit{ pt };
		int col = -1;
		g->Hit(pt, hit, col);
		IDispatch* to = nullptr;
		if (m_Kind == Kind::Table && hit.iViewRow >= 0 && hit.iViewRow < LONG_MAX - 1)
			to = Make(Kind::Row, hit.iViewRow);
		else if (m_Kind == Kind::Row && hit.iViewRow == m_Row && col >= 0 && D().rows.GroupOf(m_Row) < 0)
			to = Make(Kind::Cell, m_Row, col);
		if (to) {
			child->vt = VT_DISPATCH;
			child->pdispVal = to;
		}
		else {
			child->vt = VT_I4;
			child->lVal = CHILDID_SELF;
		}
		return S_OK;
	}

	STDMETHODIMP accDoDefaultAction(VARIANT child) override {
		DG_FORWARD(child, accDoDefaultAction(Self()));
		auto& d = D();
		auto* g = G();
		if (m_Kind == Kind::Row) {
			if (d.rows.GroupOf(m_Row) < 0)
				return S_FALSE;
			g->ToggleGroup(m_Row, true);
			return S_OK;
		}
		if (m_Kind != Kind::Cell)
			return S_FALSE;
		auto column = d.columns.Find(ColumnId());
		int64_t data = d.rows.ToData(m_Row);
		if (!column)
			return E_FAIL;
		if (column->type == DgColType::Bool && g->CanEditCell(data, column->id)) {
			g->ToggleChecks(m_Row, m_Col, false);
			return S_OK;
		}
		if (g->CanEditCell(data, column->id))
			return g->StartEdit(data, column->id, nullptr) ? S_OK : E_FAIL;
		if (data < 0)
			return S_FALSE;
		DGVHITTEST hit{};
		hit.flags = DGVHT_CELL;
		hit.iRow = data;
		hit.idColumn = column->id;
		g->NotifyCell(DGVN_ACTIVATE, hit, POINT{});
		return S_OK;
	}

	STDMETHODIMP put_accName(VARIANT, BSTR) override {
		return E_NOTIMPL;
	}

	// a cell's value, as if typed in its editor (the new row's adds a row)
	STDMETHODIMP put_accValue(VARIANT child, BSTR value) override {
		DG_FORWARD(child, put_accValue(Self(), value));
		if (m_Kind != Kind::Cell)
			return E_NOTIMPL;
		auto& d = D();
		auto* g = G();
		auto column = d.columns.Find(ColumnId());
		int64_t data = d.rows.ToData(m_Row);
		if (!column || !g->CanEditCell(data, column->id) || !g->CommitOrCancel())
			return E_ACCESSDENIED;
		std::wstring text = value ? value : L"";
		auto parsed = DgParseValue(text, *column, d.numberFormat, DgParseUserDate);
		if (!parsed.ok)
			return E_INVALIDARG;
		DgCell old;
		bool adding = data == DgRowMap::NewRow;
		if (!adding)
			g->Fetch(data, column->id, old);
		DgCell cell{ parsed.value, old.image };
		std::wstring error;
		if (!g->Validate(adding ? d.DataRowCount() : data, column->id, cell, text, DGVCR_EDIT, error))
			return E_INVALIDARG;
		if (adding) {
			if (cell.value.type == DgType::Empty)
				return S_OK;
			return g->AddRows(1, column->id, {}, { DgCellChange{ 0, column->id, old, cell } }, DGVCR_EDIT) >= 0 ? S_OK : E_ACCESSDENIED;
		}
		if (cell.value == old.value)
			return S_OK;
		g->Apply({ DgCellChange{ data, column->id, old, cell } }, DGVCR_EDIT, true);
		return S_OK;
	}

#undef DG_FORWARD

private:
	CDataGridCtrl* G() const {
		return m_Link->grid;
	}
	DgData& D() const {
		return *m_Link->grid->m_Data;
	}
	static VARIANT Self() {
		VARIANT v;
		v.vt = VT_I4;
		v.lVal = CHILDID_SELF;
		return v;
	}

	// the grid's still there and so is this element's row (and column)
	bool Alive() const {
		if (!G())
			return false;
		auto& d = D();
		if (m_Kind == Kind::Table)
			return true;
		if (m_Row < 0 || m_Row >= d.RowCount())
			return false;
		return m_Kind == Kind::Row || (m_Col >= 0 && m_Col < d.ColCount());
	}

	int ColumnId() const {
		auto& cols = D().layout.cols;
		return m_Col >= 0 && m_Col < (int)cols.size() ? cols[(size_t)m_Col].id : 0;
	}

	bool IsCaret() const {
		auto& d = D();
		if (!d.HasCaret() || d.rows.ToView(d.caretRow) != m_Row)
			return false;
		return d.layout.IndexOf(d.caretId) == m_Col;
	}

	std::wstring CellText() const {
		auto& d = D();
		auto column = d.columns.Find(ColumnId());
		DgCell cell;
		if (!column || !G()->Fetch(d.rows.ToData(m_Row), column->id, cell))
			return {};
		return DgCellText(cell, *column, d.numberFormat);
	}

	IAccessible* Table() const {
		return G()->m_Accessible;
	}

	IDispatch* Make(Kind kind, int64_t row, int col = -1) const {
		return static_cast<IAccessible*>(new DgAccessible(m_Link, kind, row, col));   // its reference is the caller's
	}

	// a child's object by its ID: the table's rows (and the caret's cell), a row's cells; nullptr if none
	IDispatch* MakeChild(long id) const {
		auto& d = D();
		if (m_Kind == Kind::Table) {
			if (id == CaretChild) {
				if (!d.HasCaret() || d.rows.ToView(d.caretRow) < 0 || d.layout.IndexOf(d.caretId) < 0)
					return nullptr;
				auto caret = d.CaretPos();
				return Make(Kind::Cell, caret.row, caret.col);
			}
			if (id >= 1 && id - 1 < std::min<int64_t>(d.RowCount(), LONG_MAX - 1))
				return Make(Kind::Row, id - 1);
			return nullptr;
		}
		if (m_Kind == Kind::Row && d.rows.GroupOf(m_Row) < 0 && id >= 1 && id <= d.ColCount())
			return Make(Kind::Cell, m_Row, id - 1);
		return nullptr;
	}

	HRESULT Resolve(const VARIANT& child, ATL::CComPtr<DgAccessible>& other) const {
		if (!Alive())
			return RPC_E_DISCONNECTED;
		if (child.vt != VT_I4)
			return E_INVALIDARG;
		if (child.lVal == CHILDID_SELF)
			return S_OK;
		IDispatch* made = MakeChild(child.lVal);
		if (!made)
			return E_INVALIDARG;
		other.Attach(static_cast<DgAccessible*>(static_cast<IAccessible*>(made)));
		return S_OK;
	}

	std::shared_ptr<DgAccLink> m_Link;
	Kind m_Kind;
	int64_t m_Row;      // a row in view
	int m_Col;          // an index into the visible columns
	LONG m_Refs = 1;
};

// ---- The grid's side ------------------------------------------------------------------------------------------

LRESULT CDataGridCtrl::OnGetObject(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((LONG)lParam != OBJID_CLIENT) {
		bHandled = FALSE;
		return 0;
	}
	if (!m_Accessible) {
		auto link = std::make_shared<DgAccLink>();
		if (FAILED(::CreateStdAccessibleObject(m_hWnd, OBJID_CLIENT, IID_PPV_ARGS(&link->proxy)))) {
			bHandled = FALSE;
			return 0;
		}
		link->grid = this;
		m_Accessible = new DgAccessible(link, DgAccessible::Kind::Table);   // the grid's reference
	}
	return ::LresultFromObject(IID_IAccessible, wParam, static_cast<IAccessible*>(m_Accessible));
}

void CDataGridCtrl::DisconnectAccessible() {
	if (!m_Accessible)
		return;
	m_Accessible->Disconnect();   // every row and cell made shares the link
	m_Accessible->Release();
	m_Accessible = nullptr;
}

void CDataGridCtrl::NotifyCaretAccessible() {
	auto& d = *m_Data;
	if (!m_Accessible || ::GetFocus() != m_hWnd || !d.HasCaret())
		return;   // no client has asked yet, or the focus is elsewhere (an editor's own)
	::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, DgAccessible::CaretChild);
}
