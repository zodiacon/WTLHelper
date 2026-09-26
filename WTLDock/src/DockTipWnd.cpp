#include "DockTipWnd.h"
#include <algorithm>

namespace WTLDock {

namespace {

constexpr int MaxWidthAt96 = 420;

}

void CDockTipWnd::Show(HWND owner, const std::wstring& text, const RECT& target, int dpi) {
	if (!m_hWnd) {
		RECT rc{ 0, 0, 10, 10 };
		Create(owner, rc, L"", 0, 0);
	}
	if (!m_hWnd)
		return;
	m_Text = text;
	m_Dpi = dpi;

	const DockMetrics& metrics = m_Host.MetricsFor(dpi);
	const int pad = metrics.TextPadding;
	CClientDC dc(nullptr);
	HFONT old = dc.SelectFont(m_Host.FontFor(dpi));
	RECT measure{ 0, 0, ::MulDiv(MaxWidthAt96, dpi, 96), 0 };
	dc.DrawText(m_Text.c_str(), (int)m_Text.size(), &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
	dc.SelectFont(old);
	const int width = Width(measure) + 2 * pad + 2, height = Height(measure) + pad + 2;

	// below the target, or above it if the monitor ends there; and inside the monitor sideways
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST), &mi);
	const int gap = ::MulDiv(4, dpi, 96);
	int x = target.left, y = target.bottom + gap;
	if (y + height > mi.rcWork.bottom)
		y = target.top - gap - height;
	x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - width));
	y = std::max<int>(mi.rcWork.top, y);

	SetWindowPos(HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_Shown = true;
	Invalidate(FALSE);
	UpdateWindow();
}

void CDockTipWnd::Hide() {
	if (m_hWnd && m_Shown)
		ShowWindow(SW_HIDE);
	m_Shown = false;
}

RECT CDockTipWnd::Rect() const {
	RECT rc{};
	if (m_hWnd)
		::GetWindowRect(m_hWnd, &rc);
	return rc;
}

LRESULT CDockTipWnd::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		Draw((HDC)wp);
		return 0;
	}
	CPaintDC dc(m_hWnd);
	Draw(dc.m_hDC);
	return 0;
}

void CDockTipWnd::Draw(HDC hdc) {
	CDCHandle dc(hdc);
	const DockTheme& theme = m_Host.Theme();
	const DockMetrics& metrics = m_Host.MetricsFor(m_Dpi);

	RECT client;
	GetClientRect(&client);
	dc.FillSolidRect(&client, theme.GuideBack);
	CBrush border;
	border.CreateSolidBrush(theme.GuideBorder);
	dc.FrameRect(&client, border);

	HFONT old = dc.SelectFont(m_Host.FontFor(m_Dpi));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(theme.TabInactiveText);
	RECT text = client;
	InflateRect(&text, -1, -1);
	text.left += metrics.TextPadding;
	text.right -= metrics.TextPadding;
	text.top += metrics.TextPadding / 2;
	dc.DrawText(m_Text.c_str(), (int)m_Text.size(), &text, DT_WORDBREAK | DT_NOPREFIX);
	dc.SelectFont(old);
}

}
