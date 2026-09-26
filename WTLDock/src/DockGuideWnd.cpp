#include "DockGuideWnd.h"
#include <algorithm>

namespace WTLDock {

void CDockGuideWnd::Configure(HWND owner, Look look, const DropTarget& target) {
	if (!m_hWnd) {
		RECT rc{ 0, 0, 1, 1 };
		Create(owner, rc, L"", 0, 0);
	}
	if (!m_hWnd)
		return;
	if (look != m_Look || !m_Target.SameTargetAs(target) || m_Target.Type == DropTarget::Kind::None) {
		m_Look = look;
		m_Target = target;
		const BYTE alpha = look == Look::Marker ? 235 : look == Look::Preview ? 90 : 70;
		::SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
		Invalidate(FALSE);
	}
}

bool CDockGuideWnd::Place(const RECT& rect, bool hot, bool raise) {
	if (!m_hWnd)
		return false;
	const bool moved = !EqualRect(&rect, &m_Rect) || !m_Shown;
	if (moved) {
		m_Rect = rect;
		SetWindowPos(HWND_TOPMOST, rect.left, rect.top, Width(rect), Height(rect), SWP_NOACTIVATE | SWP_SHOWWINDOW);
		m_Shown = true;
		Invalidate(FALSE);
	}
	else if (raise) {
		SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (hot != m_Hot) {
		m_Hot = hot;
		Invalidate(FALSE);
	}
	return moved;
}

void CDockGuideWnd::Hide() {
	if (m_hWnd && m_Shown)
		ShowWindow(SW_HIDE);
	m_Shown = false;
	m_Rect = {};
}

LRESULT CDockGuideWnd::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		Draw((HDC)wp);
		return 0;
	}
	CPaintDC dc(m_hWnd);
	Draw(dc.m_hDC);
	return 0;
}

void CDockGuideWnd::Draw(HDC hdc) {
	CDCHandle dc(hdc);
	RECT rc;
	GetClientRect(&rc);

	auto frame = [&](const RECT& r, COLORREF color, int thickness) {
		CBrush brush;
		brush.CreateSolidBrush(color);
		for (int i = 0; i < thickness; i++) {
			RECT inner = r;
			InflateRect(&inner, -i, -i);
			dc.FrameRect(&inner, brush);
		}
	};

	if (m_Look != Look::Marker) {
		dc.FillSolidRect(&rc, m_Look == Look::Preview ? m_Theme.PreviewFill : m_Theme.GuideBack);
		frame(rc, m_Theme.PreviewBorder, 2);
		return;
	}

	dc.FillSolidRect(&rc, m_Theme.GuideBack);
	frame(rc, m_Hot ? m_Theme.GuideHot : m_Theme.GuideBorder, 2);

	// a little window with the part that the drop would take highlighted
	RECT box = rc;
	InflateRect(&box, -Width(rc) / 4, -Height(rc) / 4);
	const COLORREF fill = m_Hot ? m_Theme.GuideHot : m_Theme.GuideGlyph;
	RECT part = box;
	DockPosition where = DockPosition::Tab;
	if (m_Target.Type == DropTarget::Kind::Side)
		where = m_Target.Position;
	else if (m_Target.Type == DropTarget::Kind::Edge)
		where = m_Target.Edge == DockSide::Left ? DockPosition::Left : m_Target.Edge == DockSide::Right ? DockPosition::Right :
			m_Target.Edge == DockSide::Top ? DockPosition::Top : DockPosition::Bottom;

	switch (where) {
		case DockPosition::Left: part.right = box.left + Width(box) / 2; break;
		case DockPosition::Right: part.left = box.right - Width(box) / 2; break;
		case DockPosition::Top: part.bottom = box.top + Height(box) / 2; break;
		case DockPosition::Bottom: part.top = box.bottom - Height(box) / 2; break;
		default: part.bottom = box.top + std::max(3, Height(box) / 3); break;		// a tab: the header strip
	}
	dc.FillSolidRect(&part, fill);
	frame(box, m_Hot ? m_Theme.GuideHot : m_Theme.GuideGlyph, 1);
}

}
