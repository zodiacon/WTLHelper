#include "PropertyGridCheckList.h"

#include <algorithm>

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

void PgCheckList::Open(const PgNode* node, LONGLONG mask, const RECT& rcCell) {
	m_Items.clear();
	for (auto& e : node->enumItems)
		if (e.value != 0)
			m_Items.push_back(e);
	m_Mask = mask;
	m_Current = 0;
	if (m_Items.empty())
		return;
	if (!m_hWnd) {
		RECT rc{};
		Create(m_Grid.m_hWnd, rc, nullptr, WS_POPUP | WS_BORDER, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
		if (!m_hWnd)
			return;
	}
	int border = ::GetSystemMetricsForDpi(SM_CYBORDER, m_Grid.m_Dpi);
	int height = (int)m_Items.size() * m_Grid.m_RowHeight + 2 * border;
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&rcCell, MONITOR_DEFAULTTONEAREST), &mi);
	auto& work = mi.rcWork;
	height = std::min(height, (int)(work.bottom - work.top));
	int y = rcCell.bottom;
	if (y + height > work.bottom)
		y = std::max((int)work.top, (int)rcCell.top - height);
	SetWindowPos(HWND_TOPMOST, rcCell.left, y, rcCell.right - rcCell.left, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	Invalidate();
	SetCapture();
}

void PgCheckList::Close() {
	if (!IsOpen() || m_Closing)
		return;
	m_Closing = true;
	if (::GetCapture() == m_hWnd)
		ReleaseCapture();
	ShowWindow(SW_HIDE);
	m_Closing = false;
}

bool PgCheckList::IsOpen() const {
	return m_hWnd && IsWindowVisible();
}

void PgCheckList::MoveCurrent(int delta) {
	if (m_Items.empty())
		return;
	m_Current = std::clamp(m_Current + delta, 0, (int)m_Items.size() - 1);
	Invalidate();
}

void PgCheckList::ToggleCurrent() {
	Toggle(m_Current);
}

void PgCheckList::Toggle(int index) {
	if (index < 0 || index >= (int)m_Items.size())
		return;
	auto value = m_Items[index].value;
	m_Mask = PgSetFlag(m_Mask, value, !PgIsFlagSet(m_Mask, value));
	m_Current = index;
	Invalidate();
	m_Grid.OnCheckListChanged(m_Mask);
}

int PgCheckList::ItemAt(POINT pt) const {
	RECT rc;
	GetClientRect(&rc);
	if (!::PtInRect(&rc, pt))
		return -1;
	int index = pt.y / m_Grid.m_RowHeight;
	return index < (int)m_Items.size() ? index : -1;
}

LRESULT PgCheckList::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT PgCheckList::OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
	return MA_NOACTIVATE;   // the grid's window stays active, its edit keeps the focus
}

LRESULT PgCheckList::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	RECT rc;
	GetClientRect(&rc);
	if (!::PtInRect(&rc, pt))
		m_Grid.CloseCheckList(false);   // captured: a click anywhere else closes the list (and goes no further)
	else
		Toggle(ItemAt(pt));
	return 0;
}

LRESULT PgCheckList::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	int index = ItemAt(pt);
	if (index >= 0 && index != m_Current) {
		m_Current = index;
		Invalidate();
	}
	return 0;
}

LRESULT PgCheckList::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	// switching away (Alt+Tab and the like) takes the capture: the list goes too
	if (!m_Closing && IsWindowVisible())
		m_Grid.CloseCheckList(false);
	return 0;
}

LRESULT PgCheckList::OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
	WTL::CPaintDC dc(m_hWnd);
	RECT rc;
	GetClientRect(&rc);
	WTL::CMemoryDC mdc(dc, rc);
	auto c = m_Grid.GetColors();
	FillSolid(mdc.m_hDC, rc, c.crBack);
	auto oldFont = mdc.SelectFont(m_Grid.m_hFont);
	mdc.SetBkMode(TRANSPARENT);
	int height = m_Grid.m_RowHeight;
	int pad = m_Grid.m_Pad;
	int box = std::min(height - 2 * pad, m_Grid.Scale(13));
	for (int i = 0; i < (int)m_Items.size(); i++) {
		RECT row{ 0, i * height, rc.right, (i + 1) * height };
		bool current = i == m_Current;
		auto back = current ? c.crSelBack : c.crBack;
		auto text = current ? c.crSelText : c.crText;
		if (current)
			FillSolid(mdc.m_hDC, row, back);
		RECT rcBox{ pad, row.top + (height - box) / 2, pad + box, row.top + (height - box) / 2 + box };
		m_Grid.DrawCheckBox(mdc.m_hDC, rcBox, PgIsFlagSet(m_Mask, m_Items[i].value), text, back);
		RECT rcText{ rcBox.right + pad, row.top, rc.right - pad, row.bottom };
		mdc.SetTextColor(text);
		mdc.DrawText(m_Items[i].name.c_str(), (int)m_Items[i].name.size(), &rcText,
			DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	mdc.SelectFont(oldFont);
	return 0;
}
