#include "DataGridData.h"

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// the model mirrors the public values
static_assert(DgType::Text == DGVVT_TEXT && DgType::DateTime == DGVVT_DATETIME && DgType::Bool == DGVVT_BOOL);
static_assert(DgColType::Integer == DGVCT_INTEGER && DgColType::Progress == DGVCT_PROGRESS && DgColType::Link == DGVCT_LINK);
static_assert(DgAlign::Right == DGVA_RIGHT && DgAlign::Center == DGVA_CENTER);
static_assert(DgColFlag::Hidden == DGVCF_HIDDEN && DgColFlag::GroupDigits == DGVCF_GROUPDIGITS);
static_assert(DgHitFlag::Cell == DGVHT_CELL && DgHitFlag::HeaderDivider == DGVHT_HEADERDIVIDER && DgHitFlag::Empty == DGVHT_EMPTY);

// ---- Helpers -----------------------------------------------------------------------

namespace {
	int64_t TicksOf(const FILETIME& ft) {
		return (int64_t)(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
	}

	FILETIME FileTimeOf(int64_t ticks) {
		return { (DWORD)(uint64_t)ticks, (DWORD)((uint64_t)ticks >> 32) };
	}

	DgValue ValueOf(const DGVVALUE& v) {
		switch (v.type) {
			case DGVVT_TEXT:     return DgValue::Text(v.pszText ? v.pszText : L"");
			case DGVVT_INT:      return DgValue::Int(v.llVal);
			case DGVVT_FLOAT:    return DgValue::Float(v.dblVal);
			case DGVVT_BOOL:     return DgValue::Bool(v.bVal != FALSE);
			case DGVVT_DATETIME: return DgValue::DateTime(TicksOf(v.ftVal));
		}
		return {};
	}

	DGVVALUE PublicOf(const DgValue& v, LPCWSTR text) {
		switch (v.type) {
			case DgType::Text:     return DgvText(text);
			case DgType::Int:      return DgvInt(v.i);
			case DgType::Float:    return DgvFloat(v.d);
			case DgType::Bool:     return DgvBool(v.i != 0);
			case DgType::DateTime: return DgvDateTime(FileTimeOf(v.i));
		}
		return DgvEmpty();
	}

	void Apply(DgColumn& c, const DGVCOLUMN& d) {
		if (d.mask & DGVCM_TITLE)
			c.title = d.pszTitle ? d.pszTitle : L"";
		if (d.mask & DGVCM_TYPE)
			c.type = std::clamp(d.type, (int)DGVCT_TEXT, (int)DGVCT_LINK);
		if (d.mask & DGVCM_WIDTH)
			c.width = std::max(0, d.cxWidth);
		if (d.mask & DGVCM_MINWIDTH)
			c.minWidth = std::max(0, d.cxMin);
		if (d.mask & DGVCM_FILL)
			c.fill = std::max(0, d.fill);
		if (d.mask & DGVCM_ALIGN)
			c.align = d.align;
		if (d.mask & DGVCM_DECIMALS)
			c.decimals = std::clamp(d.decimals, 0, 15);
		if (d.mask & DGVCM_FLAGS)
			c.flags = d.flags;
		if (d.mask & DGVCM_ENUMITEMS)
			c.enumItems = DgSplitEnumItems(d.pszEnumItems);
		if (d.mask & DGVCM_PARAM)
			c.param = d.lParam;
	}
}

std::wstring DgCellText(const DgCell& cell, const DgColumn& column, const DgNumberFormat& nf) {
	if (cell.value.type != DgType::DateTime)
		return DgFormat(cell.value, column, nf);
	FILETIME ft = FileTimeOf(cell.value.i);
	SYSTEMTIME st;
	if (!::FileTimeToSystemTime(&ft, &st))
		return DgFormatDateTime(cell.value.i);
	wchar_t date[80]{}, time[80]{};
	::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, _countof(date), nullptr);
	::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, _countof(time));
	return std::wstring(date) + L" " + time;
}

// ---- Construction ------------------------------------------------------------------

CDataGridCtrl::CDataGridCtrl() : m_Data(std::make_unique<DgData>()) {
}

CDataGridCtrl::~CDataGridCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hBold)
		::DeleteObject(m_hBold);
	if (m_hUnderline)
		::DeleteObject(m_hUnderline);
}

LRESULT CDataGridCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_VSCROLL | WS_HSCROLL);
	BOOL handled;
	OnSettingChange(0, 0, 0, handled);   // the number format
	UpdateFonts();
	UpdateMetrics();
	Relayout();
	return 0;
}

LRESULT CDataGridCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Data->relayingOut)
		m_Data->sizedAgain = true;   // a scroll bar came or went
	else
		Relayout();
	return 0;
}

LRESULT CDataGridCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM, BOOL&) {
	if (wParam == GWL_STYLE) {
		UpdateMetrics();
		Relayout();
	}
	return 0;
}

LRESULT CDataGridCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

void CDataGridCtrl::SetOwner(HWND owner) {
	m_Owner = owner;
}

HWND CDataGridCtrl::Owner() const {
	return m_Owner ? m_Owner : GetParent().m_hWnd;
}

LRESULT CDataGridCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(Owner(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

// ---- Metrics and fonts -------------------------------------------------------------

int CDataGridCtrl::Scale(int dips) const {
	return DgScale(dips, (int)m_Dpi);
}

int CDataGridCtrl::Unscale(int pixels) const {
	return DgUnscale(pixels, (int)m_Dpi);
}

void CDataGridCtrl::UpdateFonts() {
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	if (m_hBold)
		::DeleteObject(m_hBold);
	if (m_hUnderline)
		::DeleteObject(m_hUnderline);
	LOGFONTW bold = lf;
	bold.lfWeight = FW_SEMIBOLD;
	m_hBold = ::CreateFontIndirectW(&bold);
	LOGFONTW underline = lf;
	underline.lfUnderline = TRUE;
	m_hUnderline = ::CreateFontIndirectW(&underline);
}

void CDataGridCtrl::UpdateMetrics() {
	auto& d = *m_Data;
	CClientDC dc(m_hWnd);
	HFONT old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	SIZE digit{};
	dc.GetTextExtent(L"0", 1, &digit);
	dc.SelectFont(old);
	d.textHeight = tm.tmHeight;
	d.digitWidth = std::max(1, (int)digit.cx);
}

LRESULT CDataGridCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_OwnFont = wParam == 0;
	m_hFont = (HFONT)wParam;
	UpdateFonts();
	UpdateMetrics();
	Relayout();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CDataGridCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CDataGridCtrl::OnSettingChange(UINT, WPARAM, LPARAM, BOOL&) {
	auto& nf = m_Data->numberFormat;
	wchar_t buffer[16];
	if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, buffer, _countof(buffer)))
		nf.thousands = buffer;
	if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, buffer, _countof(buffer)))
		nf.decimal = buffer;
	if (m_hWnd)
		Invalidate();
	return 0;
}

LRESULT CDataGridCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&) {
	Invalidate();   // the colors are read as it paints
	return 0;
}

LRESULT CDataGridCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	UpdateMetrics();
	Relayout();
	bHandled = FALSE;
	return 0;
}

// ---- Layout and scrolling --------------------------------------------------------------

void CDataGridCtrl::Relayout() {
	auto& d = *m_Data;
	if (!m_hWnd)
		return;
	d.relayingOut = true;
	for (int pass = 0; pass < 3; pass++) {   // a scroll bar showing or hiding changes the client area
		d.sizedAgain = false;
		CRect rc;
		GetClientRect(&rc);
		d.clientWidth = rc.Width();
		d.clientHeight = rc.Height();

		DWORD style = GetStyle();
		DgLayoutParams p;
		p.clientWidth = d.clientWidth;
		p.dpi = (int)m_Dpi;
		p.rowHeight = d.rowHeightDips ? Scale(d.rowHeightDips) : d.textHeight + Scale(8);
		p.header = (style & DGVS_NOHEADER) ? 0 : d.textHeight + Scale(10);
		if (style & DGVS_ROWHEADER) {
			int digits = (int)std::to_wstring(std::max<int64_t>(1, d.RowCount())).size();
			p.rowHeader = std::max(2, digits) * d.digitWidth + Scale(16);
		}
		p.frozen = d.frozen;
		d.layout = DgLayoutColumns(d.columns, p);
		ClampScroll();
		UpdateScrollBars();
		if (!d.sizedAgain)
			break;
	}
	d.relayingOut = false;
	Invalidate();
}

void CDataGridCtrl::ClampScroll() {
	auto& d = *m_Data;
	d.scrollX = std::clamp(d.scrollX, 0, DgMaxScrollX(d.layout, d.clientWidth));
	d.topRow = std::clamp<int64_t>(d.topRow, 0, DgMaxTopRow(d.RowCount(), DgFullRows(d.layout, d.clientHeight)));
}

void CDataGridCtrl::UpdateScrollBars() {
	auto& d = *m_Data;
	int64_t rows = d.RowCount();
	int64_t full = DgFullRows(d.layout, d.clientHeight);
	int64_t maxTop = DgMaxTopRow(rows, full);

	SCROLLINFO si{ sizeof(si), SIF_ALL };
	if (maxTop <= DgScrollMap::MaxPos) {
		si.nMax = (int)std::max<int64_t>(0, rows - 1);
		si.nPage = (UINT)std::min<int64_t>(full, rows);
	}
	else {   // scaled: the thumb's size can't be the true share
		si.nPage = (UINT)std::max<int64_t>(1, (int64_t)((double)full / rows * DgScrollMap::MaxPos));
		si.nMax = DgScrollMap::Range(maxTop) + (int)si.nPage - 1;
	}
	si.nPos = DgScrollMap::ToPos(d.topRow, maxTop);
	SetScrollInfo(SB_VERT, &si, TRUE);

	SCROLLINFO hi{ sizeof(hi), SIF_ALL };
	int view = std::max(0, d.clientWidth - d.layout.ScrollLeft());
	hi.nMax = std::max(0, d.layout.totalWidth - d.layout.frozenWidth - 1);
	hi.nPage = (UINT)view;
	hi.nPos = d.scrollX;
	SetScrollInfo(SB_HORZ, &hi, TRUE);
}

void CDataGridCtrl::ScrollRows(LONGLONG top) {
	auto& d = *m_Data;
	int64_t old = d.topRow;
	d.topRow = top;
	ClampScroll();
	if (d.topRow != old) {
		UpdateScrollBars();
		Invalidate();
	}
}

void CDataGridCtrl::ScrollX(int x) {
	auto& d = *m_Data;
	int old = d.scrollX;
	d.scrollX = x;
	ClampScroll();
	if (d.scrollX != old) {
		UpdateScrollBars();
		Invalidate();
	}
}

LRESULT CDataGridCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	int64_t full = DgFullRows(d.layout, d.clientHeight);
	int64_t maxTop = DgMaxTopRow(d.RowCount(), full);
	int64_t top = d.topRow;
	switch (LOWORD(wParam)) {
		case SB_LINEUP:   top--; break;
		case SB_LINEDOWN: top++; break;
		case SB_PAGEUP:   top -= full; break;
		case SB_PAGEDOWN: top += full; break;
		case SB_TOP:      top = 0; break;
		case SB_BOTTOM:   top = maxTop; break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_VERT, &si);
			top = DgScrollMap::FromPos(si.nTrackPos, maxTop);
			break;
		}
		default:
			return 0;
	}
	ScrollRows(top);
	return 0;
}

LRESULT CDataGridCtrl::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	int x = d.scrollX;
	int page = std::max(1, d.clientWidth - d.layout.ScrollLeft());
	switch (LOWORD(wParam)) {
		case SB_LINELEFT:  x -= Scale(20); break;
		case SB_LINERIGHT: x += Scale(20); break;
		case SB_PAGELEFT:  x -= page; break;
		case SB_PAGERIGHT: x += page; break;
		case SB_LEFT:      x = 0; break;
		case SB_RIGHT:     x = INT_MAX / 2; break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_HORZ, &si);
			x = si.nTrackPos;
			break;
		}
		default:
			return 0;
	}
	ScrollX(x);
	return 0;
}

LRESULT CDataGridCtrl::OnMouseWheel(UINT msg, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	bool horizontal = msg == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT);
	if (horizontal) {
		if (msg == WM_MOUSEWHEEL)
			delta = -delta;   // Shift+wheel down scrolls right
		m_HWheelRemainder += delta;
		int steps = m_HWheelRemainder / WHEEL_DELTA * WHEEL_DELTA;
		if (steps == 0 && std::abs(m_HWheelRemainder) < WHEEL_DELTA) {
			// a precise touchpad: move by its share
			ScrollX(d.scrollX + ::MulDiv(m_HWheelRemainder, Scale(60), WHEEL_DELTA));
			m_HWheelRemainder = 0;
			return 0;
		}
		m_HWheelRemainder -= steps;
		ScrollX(d.scrollX + steps / WHEEL_DELTA * Scale(60));
		return 0;
	}
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	int64_t full = DgFullRows(d.layout, d.clientHeight);
	m_WheelRemainder += delta;
	int notches = m_WheelRemainder / WHEEL_DELTA;
	if (notches == 0)
		return 0;
	m_WheelRemainder -= notches * WHEEL_DELTA;
	int64_t rows = lines == WHEEL_PAGESCROLL ? full : (int64_t)lines;
	ScrollRows(d.topRow - notches * rows);
	return 0;
}

// ---- Columns ------------------------------------------------------------------------------------

int CDataGridCtrl::InsertColumn(const DGVCOLUMN& column, int displayIndex) {
	DgColumn c;
	Apply(c, column);
	int id = m_Data->columns.Insert(std::move(c), displayIndex);
	if (m_Data->caretId == 0)
		m_Data->caretId = id;
	Relayout();
	return id;
}

BOOL CDataGridCtrl::DeleteColumn(int id) {
	auto& d = *m_Data;
	auto c = d.columns.Find(id);
	if (!c)
		return FALSE;
	d.store.ClearSlot(c->slot);
	d.columns.Erase(id);
	d.cache.Clear();
	if (d.caretId == id) {
		auto visible = d.columns.Visible();
		d.caretId = visible.empty() ? 0 : visible.front()->id;
	}
	Relayout();
	return TRUE;
}

BOOL CDataGridCtrl::SetColumn(int id, const DGVCOLUMN& column) {
	auto c = m_Data->columns.Find(id);
	if (!c)
		return FALSE;
	Apply(*c, column);
	Relayout();
	return TRUE;
}

BOOL CDataGridCtrl::GetColumn(int id, DGVCOLUMN* column) const {
	auto c = m_Data->columns.Find(id);
	if (!c || !column)
		return FALSE;
	UINT mask = column->mask;
	if (mask & DGVCM_TITLE)
		column->pszTitle = c->title.c_str();
	if (mask & DGVCM_TYPE)
		column->type = c->type;
	if (mask & DGVCM_WIDTH)
		column->cxWidth = GetColumnWidth(id);
	if (mask & DGVCM_MINWIDTH)
		column->cxMin = c->minWidth;
	if (mask & DGVCM_FILL)
		column->fill = c->fill;
	if (mask & DGVCM_ALIGN)
		column->align = c->align;
	if (mask & DGVCM_DECIMALS)
		column->decimals = c->decimals;
	if (mask & DGVCM_FLAGS)
		column->flags = c->flags;
	if (mask & DGVCM_ENUMITEMS)
		column->pszEnumItems = nullptr;   // not given back
	if (mask & DGVCM_PARAM)
		column->lParam = c->param;
	return TRUE;
}

int CDataGridCtrl::GetColumnCount() const {
	return m_Data->columns.Count();
}

int CDataGridCtrl::GetColumnOrder(int* ids, int count) const {
	auto& order = m_Data->columns.Order();
	if (ids)
		for (int i = 0; i < count && i < (int)order.size(); i++)
			ids[i] = order[i];
	return (int)order.size();
}

BOOL CDataGridCtrl::SetColumnOrder(const int* ids, int count) {
	if (!ids || count < 0 || !m_Data->columns.SetOrder(std::vector<int>(ids, ids + count)))
		return FALSE;
	Relayout();
	return TRUE;
}

int CDataGridCtrl::GetColumnWidth(int id) const {
	auto& d = *m_Data;
	auto c = d.columns.Find(id);
	if (!c)
		return 0;
	int index = d.layout.IndexOf(id);
	return index >= 0 ? Unscale(d.layout.cols[index].width) : c->width;
}

BOOL CDataGridCtrl::SetColumnWidth(int id, int width) {
	auto c = m_Data->columns.Find(id);
	if (!c)
		return FALSE;
	c->width = std::max(0, width);
	c->fill = 0;
	Relayout();
	return TRUE;
}

BOOL CDataGridCtrl::ShowColumn(int id, bool show) {
	auto c = m_Data->columns.Find(id);
	if (!c)
		return FALSE;
	if (show)
		c->flags &= ~DgColFlag::Hidden;
	else
		c->flags |= DgColFlag::Hidden;
	Relayout();
	if (!show && m_Data->caretId == id) {
		auto visible = m_Data->columns.Visible();
		m_Data->caretId = visible.empty() ? 0 : visible.front()->id;
	}
	return TRUE;
}

BOOL CDataGridCtrl::AutoFitColumn(int id) {
	int index = m_Data->layout.IndexOf(id);
	auto c = m_Data->columns.Find(id);
	if (index < 0 || !c)
		return FALSE;
	c->width = Unscale(MeasureColumn(index));
	c->fill = 0;
	Relayout();
	return TRUE;
}

void CDataGridCtrl::SetFrozenColumns(int count) {
	m_Data->frozen = std::max(0, count);
	Relayout();
}

int CDataGridCtrl::GetFrozenColumns() const {
	return m_Data->frozen;
}

// ---- Rows -----------------------------------------------------------------------------------------

void CDataGridCtrl::SetVirtual(bool virtualMode) {
	auto& d = *m_Data;
	d.virtualMode = virtualMode;
	d.store.Clear();
	d.cache.Clear();
	d.virtualCount = 0;
	d.rows.Reset(0);
	d.caretRow = -1;
	d.topRow = 0;
	Relayout();
}

bool CDataGridCtrl::IsVirtual() const {
	return m_Data->virtualMode;
}

BOOL CDataGridCtrl::SetRowCount(LONGLONG count) {
	auto& d = *m_Data;
	if (count < 0)
		return FALSE;
	if (d.virtualMode) {
		d.virtualCount = count;
		d.cache.Drop(count, INT64_MAX);
	}
	else {
		d.store.Resize(count);
	}
	d.rows.Reset(count);
	if (d.caretRow >= count)
		d.caretRow = count - 1;
	Relayout();
	return TRUE;
}

LONGLONG CDataGridCtrl::GetRowCount() const {
	return m_Data->DataRowCount();
}

LONGLONG CDataGridCtrl::InsertRows(LONGLONG at, LONGLONG count) {
	auto& d = *m_Data;
	if (d.virtualMode || count < 0)
		return -1;
	int64_t first = d.store.Insert(at, count);
	d.rows.Reset(d.store.Count());
	if (d.caretRow >= first)
		d.caretRow += count;
	Relayout();
	return first;
}

BOOL CDataGridCtrl::DeleteRows(LONGLONG at, LONGLONG count) {
	auto& d = *m_Data;
	if (d.virtualMode || !d.store.Erase(at, count))
		return FALSE;
	d.rows.Reset(d.store.Count());
	if (d.caretRow >= at + count)
		d.caretRow -= count;
	else if (d.caretRow >= at)
		d.caretRow = std::min<int64_t>(at, d.store.Count() - 1);
	Relayout();
	return TRUE;
}

BOOL CDataGridCtrl::SetCell(LONGLONG row, int id, const DGVVALUE& value, int image) {
	auto& d = *m_Data;
	auto c = d.columns.Find(id);
	if (d.virtualMode || !c)
		return FALSE;
	if (!d.store.Set(row, c->slot, DgCell{ ValueOf(value), image }))
		return FALSE;
	InvalidateRow(d.rows.ToView(row));
	return TRUE;
}

bool CDataGridCtrl::Fetch(LONGLONG dataRow, int id, DgCell& cell) {
	auto& d = *m_Data;
	auto c = d.columns.Find(id);
	if (!c || dataRow < 0 || dataRow >= d.DataRowCount())
		return false;
	if (!d.virtualMode) {
		auto stored = d.store.Get(dataRow, c->slot);
		cell = stored ? *stored : DgCell{};
		return true;
	}
	if (auto cached = d.cache.Find(dataRow, id)) {
		cell = *cached;
		return true;
	}
	NMDGVDISPINFO info{};
	info.iRow = dataRow;
	info.idColumn = id;
	info.iImage = -1;
	FireNotify(&info.hdr, DGVN_GETDISPINFO);
	cell = DgCell{ ValueOf(info.value), info.iImage };
	d.cache.Put(dataRow, id, cell);
	return true;
}

BOOL CDataGridCtrl::GetCell(LONGLONG row, int id, DGVVALUE* value, int* image) {
	DgCell cell;
	if (!Fetch(row, id, cell))
		return FALSE;
	auto& d = *m_Data;
	d.cellText = cell.value.s;
	if (value)
		*value = PublicOf(cell.value, d.cellText.c_str());
	if (image)
		*image = cell.image;
	return TRUE;
}

int CDataGridCtrl::GetCellText(LONGLONG row, int id, LPWSTR text, int cch) {
	DgCell cell;
	auto c = m_Data->columns.Find(id);
	if (!c || !Fetch(row, id, cell))
		return 0;
	auto s = DgCellText(cell, *c, m_Data->numberFormat);
	if (text && cch > 0) {
		size_t n = std::min(s.size(), (size_t)cch - 1);
		s.copy(text, n);
		text[n] = 0;
	}
	return (int)s.size() + 1;
}

void CDataGridCtrl::DropCache() {
	m_Data->cache.Clear();
}

void CDataGridCtrl::RedrawRows(LONGLONG first, LONGLONG last) {
	auto& d = *m_Data;
	if (first < 0 && last < 0)
		d.cache.Clear();
	else
		d.cache.Drop(first, last);
	Invalidate();
}

void CDataGridCtrl::InvalidateRow(LONGLONG viewRow) {
	auto& d = *m_Data;
	if (!m_hWnd || viewRow < d.topRow)
		return;
	int64_t offset = viewRow - d.topRow;
	if (offset > d.clientHeight / d.layout.rowHeight + 1)
		return;
	int y = d.layout.header + (int)offset * d.layout.rowHeight;
	RECT rc{ 0, y, d.clientWidth, y + d.layout.rowHeight };
	InvalidateRect(&rc, FALSE);
}

// ---- Caret and scrolling --------------------------------------------------------------------------

BOOL CDataGridCtrl::SetCaret(LONGLONG row, int id, bool ensureVisible) {
	auto& d = *m_Data;
	int64_t view = d.rows.ToView(row);
	int col = d.layout.IndexOf(id);
	if (view < 0 || col < 0)
		return FALSE;
	MoveCaret(view, col, false);
	if (!ensureVisible)
		return TRUE;
	EnsureVisible(row, id);
	return TRUE;
}

BOOL CDataGridCtrl::GetCaret(LONGLONG* row, int* id) const {
	auto& d = *m_Data;
	if (d.caretRow < 0 || d.caretId == 0)
		return FALSE;
	if (row)
		*row = d.caretRow;
	if (id)
		*id = d.caretId;
	return TRUE;
}

void CDataGridCtrl::EnsureVisible(LONGLONG row, int id) {
	auto& d = *m_Data;
	int64_t view = d.rows.ToView(row);
	if (view >= 0)
		ScrollRows(DgTopToShow(d.topRow, DgFullRows(d.layout, d.clientHeight), view));
	int col = d.layout.IndexOf(id);
	if (col >= 0)
		ScrollX(DgScrollXToShow(d.layout, d.scrollX, d.clientWidth, col));
}

LONGLONG CDataGridCtrl::GetTopRow() const {
	return m_Data->rows.ToData(m_Data->topRow);
}

void CDataGridCtrl::SetTopRow(LONGLONG row) {
	int64_t view = m_Data->rows.ToView(row);
	if (view >= 0)
		ScrollRows(view);
}

int CDataGridCtrl::GetVisibleRowCount() const {
	return (int)std::min<int64_t>(INT_MAX, DgFullRows(m_Data->layout, m_Data->clientHeight));
}

void CDataGridCtrl::MoveCaret(LONGLONG viewRow, int displayCol, bool notify) {
	auto& d = *m_Data;
	if (displayCol < 0 || displayCol >= (int)d.layout.cols.size())
		return;
	int64_t dataRow = d.rows.ToData(viewRow);
	int id = d.layout.cols[displayCol].id;
	if (dataRow < 0 || (dataRow == d.caretRow && id == d.caretId))
		return;
	NMDGVCARET nm{};
	nm.iOldRow = d.caretRow;
	nm.idOldColumn = d.caretId;
	InvalidateRow(d.rows.ToView(d.caretRow));
	d.caretRow = dataRow;
	d.caretId = id;
	InvalidateRow(viewRow);
	if (notify) {
		nm.iRow = dataRow;
		nm.idColumn = id;
		FireNotify(&nm.hdr, DGVN_CARETCHANGED);
	}
}

// ---- Hit testing -------------------------------------------------------------------------------

void CDataGridCtrl::Hit(POINT pt, DGVHITTEST& hit, int& displayCol) const {
	auto& d = *m_Data;
	auto h = DgHitTest(d.layout, d.View(), pt.x, pt.y, Scale(4));
	hit.pt = pt;
	hit.flags = h.flags;
	hit.iRow = h.row >= 0 ? d.rows.ToData(h.row) : -1;
	hit.idColumn = h.col >= 0 ? d.layout.cols[h.col].id : 0;
	displayCol = h.col;
}

void CDataGridCtrl::HitTest(DGVHITTEST* hit) const {
	if (!hit)
		return;
	int col;
	Hit(hit->pt, *hit, col);
}

void CDataGridCtrl::NotifyCell(UINT code, const DGVHITTEST& hit, POINT screen) {
	NMDGVCELL nm{};
	nm.flags = hit.flags;
	nm.iRow = hit.iRow;
	nm.idColumn = hit.idColumn;
	nm.ptScreen = screen;
	FireNotify(&nm.hdr, code);
}

// ---- Looks -----------------------------------------------------------------------------------------

void CDataGridCtrl::SetColors(const DGVCOLORS* colors) {
	auto& d = *m_Data;
	d.userColors = colors ? *colors
		: DGVCOLORS{ CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT };
	if (m_hWnd)
		Invalidate();
}

void CDataGridCtrl::GetColors(DGVCOLORS* colors) const {
	if (colors)
		*colors = m_Data->userColors;
}

void CDataGridCtrl::SetRowHeight(int height) {
	m_Data->rowHeightDips = std::max(0, height);
	Relayout();
}

int CDataGridCtrl::GetRowHeight() const {
	return Unscale(m_Data->layout.rowHeight);
}

void CDataGridCtrl::SetImageList(HIMAGELIST images) {
	m_Data->images = images;
	if (m_hWnd)
		Invalidate();
}
