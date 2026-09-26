#include "DockNavigatorWnd.h"
#include <algorithm>

namespace WTLDock {

namespace {

constexpr UINT_PTR TimerWatch = 1;

const wchar_t* const ColumnTitles[] = { L"Active Files", L"Active Tool Windows" };

}

void CDockNavigatorWnd::ComputeLayout() {
	const auto& nav = m_Host.m_Nav;
	const int counts[2] = { (int)nav.Items(DockNavigator::Column::Documents).size(), (int)nav.Items(DockNavigator::Column::Tools).size() };
	m_Layout = ComputeNavigatorLayout(counts, m_First, (int)nav.CurrentColumn(), nav.Row(), m_Host.Metrics());
	m_First[0] = m_Layout.First[0];
	m_First[1] = m_Layout.First[1];
}

void CDockNavigatorWnd::Open(HWND owner, bool watchControlKey) {
	if (!m_hWnd) {
		RECT rc{ 0, 0, 10, 10 };
		Create(owner, rc, L"", 0, 0);
	}
	if (!m_hWnd)
		return;
	m_WatchControl = watchControlKey;
	m_First[0] = m_First[1] = 0;
	m_LastMouse = { -1, -1 };
	ComputeLayout();

	// centred over the window that owns the docking area
	RECT area;
	::GetWindowRect(owner, &area);
	const RECT outer{ 0, 0, m_Layout.Size.cx, m_Layout.Size.cy };
	RECT rc = outer;
	AdjustWindowRectExForDpi(&rc, WS_POPUP, FALSE, WS_EX_TOOLWINDOW, m_Host.Dpi());
	const int w = Width(rc), h = Height(rc);
	const int x = (area.left + area.right - w) / 2, y = (area.top + area.bottom - h) / 2;
	SetWindowPos(HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	Invalidate(FALSE);
	if (m_WatchControl)
		SetTimer(TimerWatch, 30);
}

void CDockNavigatorWnd::Close() {
	if (!m_hWnd)
		return;
	KillTimer(TimerWatch);
	ShowWindow(SW_HIDE);
}

void CDockNavigatorWnd::Refresh() {
	if (!m_hWnd)
		return;
	const NavigatorLayout before = m_Layout;
	ComputeLayout();
	if (m_Layout.Size.cx != before.Size.cx || m_Layout.Size.cy != before.Size.cy) {
		RECT rc{ 0, 0, m_Layout.Size.cx, m_Layout.Size.cy };
		AdjustWindowRectExForDpi(&rc, WS_POPUP, FALSE, WS_EX_TOOLWINDOW, m_Host.Dpi());
		SetWindowPos(nullptr, 0, 0, Width(rc), Height(rc), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	Invalidate(FALSE);
}

RECT CDockNavigatorWnd::RowRect(DockNavigator::Column column, int row) const {
	const int c = (int)column;
	const int i = row - m_Layout.First[c];
	if (i < 0 || i >= (int)m_Layout.Rows[c].size())
		return {};
	return m_Layout.Rows[c][i];
}

bool CDockNavigatorWnd::RowAt(POINT pt, DockNavigator::Column& column, int& row) const {
	for (int c = 0; c < 2; c++) {
		for (size_t i = 0; i < m_Layout.Rows[c].size(); i++) {
			if (PtInRect(&m_Layout.Rows[c][i], pt)) {
				column = (DockNavigator::Column)c;
				row = m_Layout.First[c] + (int)i;
				return true;
			}
		}
	}
	return false;
}

LRESULT CDockNavigatorWnd::OnMouseMove(UINT, WPARAM, LPARAM lp, BOOL&) {
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	// a window that opens under a resting mouse must not take the selection away from the keyboard
	if (pt.x == m_LastMouse.x && pt.y == m_LastMouse.y)
		return 0;
	const bool first = m_LastMouse.x < 0;
	m_LastMouse = pt;
	DockNavigator::Column column;
	int row;
	if (!first && RowAt(pt, column, row) && m_Host.m_Nav.Select(column, row))
		Refresh();
	return 0;
}

LRESULT CDockNavigatorWnd::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	DockNavigator::Column column;
	int row;
	if (RowAt({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }, column, row) && m_Host.m_Nav.Select(column, row))
		m_Host.CommitNavigator();
	return 0;
}

LRESULT CDockNavigatorWnd::OnMouseWheel(UINT, WPARAM wp, LPARAM, BOOL&) {
	m_Host.NavigatorMove(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1);
	return 0;
}

LRESULT CDockNavigatorWnd::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	if (id != TimerWatch) {
		handled = FALSE;
		return 0;
	}
	// the key events normally do this; the timer is for when they do not arrive (the focus went elsewhere)
	if (m_WatchControl && !(::GetAsyncKeyState(VK_CONTROL) & 0x8000))
		m_Host.CommitNavigator();
	return 0;
}

std::wstring CDockNavigatorWnd::Describe(const DockPane& pane) {
	static const wchar_t* const sides[] = { L"left", L"right", L"top", L"bottom" };
	std::wstring text = pane.Title + L" - ";
	auto group = pane.Group();
	switch (pane.State()) {
		case PaneState::Docked:
			text += L"docked";
			if (group && group->Side())
				text += std::wstring(L" ") + sides[(int)*group->Side()];
			break;
		case PaneState::AutoHide:
			text += L"auto-hidden";
			if (group && group->Side())
				text += std::wstring(L" ") + sides[(int)*group->Side()];
			break;
		case PaneState::Floating:
			text += L"floating";
			break;
		default:
			text += L"document";
			break;
	}
	return text;
}

LRESULT CDockNavigatorWnd::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		Draw((HDC)wp);
		return 0;
	}
	CPaintDC dc(m_hWnd);
	CMemoryDC mem(dc, dc.m_ps.rcPaint);
	Draw(mem);
	return 0;
}

void CDockNavigatorWnd::Draw(HDC hdc) {
	CDCHandle dc(hdc);
	const DockTheme& theme = m_Host.Theme();
	const DockMetrics& metrics = m_Host.Metrics();
	const auto& nav = m_Host.m_Nav;

	RECT client;
	GetClientRect(&client);
	dc.FillSolidRect(&client, theme.GroupBack);
	CBrush border;
	border.CreateSolidBrush(theme.Border);
	dc.FrameRect(&client, border);

	dc.SetBkMode(TRANSPARENT);
	HFONT oldFont = dc.SelectFont(m_Host.BoldFont());
	for (int c = 0; c < 2; c++) {
		RECT header = m_Layout.Header[c];
		header.left += metrics.TextPadding;
		dc.SetTextColor(theme.TabInactiveText);
		dc.DrawText(ColumnTitles[c], -1, &header, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		RECT line{ m_Layout.Header[c].left, m_Layout.Header[c].bottom - 1, m_Layout.Header[c].right, m_Layout.Header[c].bottom };
		dc.FillSolidRect(&line, theme.Border);
	}

	dc.SelectFont(m_Host.Font());
	for (int c = 0; c < 2; c++) {
		const auto& items = nav.Items((DockNavigator::Column)c);
		for (size_t i = 0; i < m_Layout.Rows[c].size(); i++) {
			const int index = m_Layout.First[c] + (int)i;
			if (index >= (int)items.size())
				break;
			const DockPane* pane = items[index];
			const RECT row = m_Layout.Rows[c][i];
			const bool selected = (int)nav.CurrentColumn() == c && nav.Row() == index;
			dc.FillSolidRect(&row, selected ? theme.CaptionActiveBack : theme.GroupBack);

			RECT text = row;
			text.left += metrics.TextPadding;
			if (pane->Icon) {
				const int y = row.top + (Height(row) - metrics.IconSize) / 2;
				::DrawIconEx(dc, text.left, y, pane->Icon, metrics.IconSize, metrics.IconSize, 0, nullptr, DI_NORMAL);
				text.left += metrics.IconSize + metrics.TabIconGap;
			}
			dc.SetTextColor(selected ? theme.CaptionActiveText : theme.TabActiveText);
			dc.DrawText(pane->Title.c_str(), (int)pane->Title.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}

	if (auto selected = nav.Selected()) {
		RECT line{ m_Layout.Footer.left, m_Layout.Footer.top, m_Layout.Footer.right, m_Layout.Footer.top + 1 };
		dc.FillSolidRect(&line, theme.Border);
		RECT text = m_Layout.Footer;
		text.left += metrics.TextPadding;
		dc.SetTextColor(theme.TabInactiveText);
		const std::wstring description = Describe(*selected);
		dc.DrawText(description.c_str(), (int)description.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
	}
	dc.SelectFont(oldFont);
}

}
