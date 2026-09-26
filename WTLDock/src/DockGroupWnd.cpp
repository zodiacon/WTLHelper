#include "DockGroupWnd.h"
#include <algorithm>

namespace WTLDock {

namespace {

constexpr UINT_PTR SessionTimer = 1;

}

//
// content
//

void CDockGroupWnd::Relayout() {
	if (!m_hWnd)
		return;
	if (m_Group) {
		RECT rc;
		GetClientRect(&rc);
		const GroupParts parts = ComputeGroupParts(*m_Group, rc, m_Host.Metrics());
		const RECT& c = parts.Content;
		const auto active = m_Group->ActivePane();
		for (auto pane : m_Group->Panes()) {
			if (!pane->hWnd || !::IsWindow(pane->hWnd))
				continue;
			CDockHost::Adopt(pane->hWnd, m_hWnd);
			if (pane == active)
				::SetWindowPos(pane->hWnd, nullptr, c.left, c.top, Width(c), Height(c), SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
			else
				::ShowWindow(pane->hWnd, SW_HIDE);
		}
	}
	Invalidate(FALSE);
}

void CDockGroupWnd::Retire() {
	m_Group = nullptr;
	EndInteraction();
	if (!m_hWnd)
		return;
	ShowWindow(SW_HIDE);
	// whatever content is still in here goes back to the host (which parks or re-parents it)
	for (HWND child = GetWindow(GW_CHILD); child;) {
		HWND next = ::GetWindow(child, GW_HWNDNEXT);
		CDockHost::Adopt(child, m_Host.m_hWnd);
		::ShowWindow(child, SW_HIDE);
		child = next;
	}
	// and the window itself leaves its frame, which may be about to go
	CDockHost::Adopt(m_hWnd, m_Host.m_hWnd);
}

void CDockGroupWnd::EndInteraction() {
	m_HotButton = m_PressButton = Button::None;
	m_HotTabClose = m_HotOverflow = false;
	m_HotTab = m_PressTabClose = m_MiddleTab = -1;
	m_DragPane = nullptr;
	m_Dragging = false;
	m_CaptionPending = false;
	if (m_Session) {
		m_Session = false;
		if (m_hWnd)
			KillTimer(SessionTimer);
	}
}

//
// dragging out of the group
//

bool CDockGroupWnd::BeginDockDrag(DockPane* pane, bool wholeGroup, POINT client) {
	POINT screen = client;
	ClientToScreen(&screen);
	if (!m_Host.BeginDrag(pane, wholeGroup, screen))
		return false;
	m_DragPane = nullptr;
	m_Dragging = false;
	m_CaptionPending = false;
	m_Session = true;
	m_LastScreen = screen;
	SetTimer(SessionTimer, 30);
	return true;
}

void CDockGroupWnd::EndDockDrag(bool commit) {
	EndInteraction();
	if (::GetCapture() == m_hWnd)
		ReleaseCapture();
	m_Host.EndDrag(commit);		// this may retire the window
}

LRESULT CDockGroupWnd::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	if (id != SessionTimer) {
		handled = FALSE;
		return 0;
	}
	if (!m_Session) {
		KillTimer(SessionTimer);
		return 0;
	}
	// the mouse may stand still while Ctrl goes down or up; and Escape cancels
	if (!m_Host.IsDragging() || (::GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
		EndDockDrag(false);
		return 0;
	}
	m_Host.UpdateDrag(m_LastScreen, (::GetKeyState(VK_CONTROL) & 0x8000) != 0);
	return 0;
}

void CDockGroupWnd::DropInfo(std::vector<RECT>& zones, std::vector<RECT>& tabs, int& firstTab) {
	firstTab = 0;
	if (!m_Group || !m_hWnd)
		return;
	RECT rc;
	GetClientRect(&rc);
	POINT origin{ 0, 0 };
	ClientToScreen(&origin);
	auto toScreen = [&](RECT r) {
		OffsetRect(&r, origin.x, origin.y);
		return r;
	};

	const GroupParts parts = ComputeGroupParts(*m_Group, rc, m_Host.Metrics());
	if (parts.HasCaption)
		zones.push_back(toScreen(parts.Caption));
	if (parts.HasTabs) {
		zones.push_back(toScreen(parts.Tabs));
		CClientDC dc(m_hWnd);
		const Strip strip = LayoutStrip(parts, dc.m_hDC);
		for (auto& tab : strip.Layout.Tabs)
			tabs.push_back(toScreen(tab));
		firstTab = strip.Layout.First;
	}
}

LRESULT CDockGroupWnd::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	if (!m_Host.m_Syncing)		// during a sync the host calls Relayout when all windows have been placed
		Relayout();
	return 0;
}

LRESULT CDockGroupWnd::OnForward(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
	HWND target = m_Host.NotifyTarget();
	if (!target) {
		handled = FALSE;
		return 0;
	}
	return ::SendMessage(target, msg, wp, lp);
}

LRESULT CDockGroupWnd::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Group) {
		if (auto pane = m_Group->ActivePane()) {
			m_Host.SetActivePane(pane);
			if (pane->hWnd && ::IsWindowVisible(pane->hWnd))
				::SetFocus(pane->hWnd);
		}
	}
	return 0;
}

//
// tab strip
//

CDockGroupWnd::Strip CDockGroupWnd::LayoutStrip(const GroupParts& parts, CDCHandle dc) {
	Strip strip;
	if (!m_Group || !parts.HasTabs)
		return strip;

	HFONT old = dc.SelectFont(m_Host.Font());
	for (auto pane : m_Group->Panes()) {
		SIZE size{};
		dc.GetTextExtent(pane->Title.c_str(), (int)pane->Title.size(), &size);
		strip.Specs.push_back({ size.cx, pane->Icon != nullptr, m_Group->IsDocument() && Has(pane->Caps, PaneCaps::CanClose) });
	}
	dc.SelectFont(old);

	// a new active tab takes the scrolling back from the user
	const int active = m_Group->ActiveIndex();
	if (active != m_LastActive) {
		m_LastActive = active;
		m_ScrollLocked = false;
	}
	strip.Layout = LayoutTabStrip(strip.Specs, parts.Tabs, m_Host.Metrics(), m_First, m_ScrollLocked ? -1 : active);
	m_First = strip.Layout.First;
	return strip;
}

TabStripState CDockGroupWnd::State() {
	TabStripState state{};
	if (!m_Group || !m_hWnd)
		return state;
	RECT rc;
	GetClientRect(&rc);
	CClientDC dc(m_hWnd);
	const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, m_Host.Metrics()), dc.m_hDC);
	state.First = strip.Layout.First;
	state.Visible = (int)strip.Layout.Tabs.size();
	state.Overflow = strip.Layout.Overflow;
	return state;
}

DockPane* CDockGroupWnd::PaneAt(int tab) const {
	if (!m_Group || tab < 0 || tab >= (int)m_Group->Panes().size())
		return nullptr;
	return m_Group->Panes()[tab];
}

bool CDockGroupWnd::CloseButtonVisible() const {
	if (!m_Group || m_Group->IsDocument())
		return false;
	auto pane = m_Group->ActivePane();
	return pane && Has(pane->Caps, PaneCaps::CanClose);
}

// pinned: auto-hide it; while it is out of an auto-hide bar: dock it
bool CDockGroupWnd::PinVisible() const {
	if (!m_Group || m_Group->IsDocument())
		return false;
	if (m_Group->Location() == GroupLocation::AutoHide)
		return true;
	if (m_Group->Location() != GroupLocation::Main)
		return false;
	const auto& panes = m_Group->Panes();
	return !panes.empty() && std::all_of(panes.begin(), panes.end(), [](auto p) { return Has(p->Caps, PaneCaps::CanAutoHide); });
}

// the drop-down menu is there if it has something to offer
bool CDockGroupWnd::MenuVisible() const {
	if (!m_Group || m_Group->IsDocument())
		return false;
	auto pane = m_Group->ActivePane();
	if (!pane)
		return false;
	return m_Host.CanExecute(DockCommand::Close, pane) || m_Host.CanExecute(DockCommand::Float, pane) ||
		m_Host.CanExecute(DockCommand::Dock, pane) || m_Host.CanExecute(DockCommand::AutoHide, pane);
}

CaptionButtons CDockGroupWnd::ButtonsFor(const GroupParts& parts) const {
	return ComputeCaptionButtons(parts.Caption, CloseButtonVisible(), PinVisible(), MenuVisible(), m_Host.Metrics());
}

CDockGroupWnd::Button CDockGroupWnd::ButtonOf(Hit::Kind kind) {
	switch (kind) {
		case Hit::Kind::CaptionClose: return Button::Close;
		case Hit::Kind::CaptionPin: return Button::Pin;
		case Hit::Kind::CaptionMenu: return Button::Menu;
		default: return Button::None;
	}
}

void CDockGroupWnd::RunButton(Button button) {
	if (!m_Group)
		return;
	DockPane* pane = m_Group->ActivePane();
	if (!pane)
		return;
	if (button == Button::Pin) {
		// (the window may be retired by this; touch nothing afterwards)
		m_Host.Execute(m_Group->Location() == GroupLocation::AutoHide ? DockCommand::Dock : DockCommand::AutoHide, pane);
	}
	else if (button == Button::Menu) {
		RECT rc;
		GetClientRect(&rc);
		const CaptionButtons b = ButtonsFor(ComputeGroupParts(*m_Group, rc, m_Host.Metrics()));
		POINT screen{ b.Menu.left, b.Menu.bottom };
		ClientToScreen(&screen);
		m_Host.ShowPaneMenu(pane, screen);
	}
}

CDockGroupWnd::Hit CDockGroupWnd::Locate(POINT pt) {
	Hit hit;
	if (!m_Group)
		return hit;
	RECT rc;
	GetClientRect(&rc);
	const auto& metrics = m_Host.Metrics();
	const GroupParts parts = ComputeGroupParts(*m_Group, rc, metrics);

	if (parts.HasCaption && PtInRect(&parts.Caption, pt)) {
		const CaptionButtons b = ButtonsFor(parts);
		hit.Type = b.HasClose && PtInRect(&b.Close, pt) ? Hit::Kind::CaptionClose :
			b.HasPin && PtInRect(&b.Pin, pt) ? Hit::Kind::CaptionPin :
			b.HasMenu && PtInRect(&b.Menu, pt) ? Hit::Kind::CaptionMenu : Hit::Kind::Caption;
		return hit;
	}
	if (parts.HasTabs && PtInRect(&parts.Tabs, pt)) {
		CClientDC dc(m_hWnd);
		const auto strip = LayoutStrip(parts, dc.m_hDC);
		if (strip.Layout.Overflow && PtInRect(&strip.Layout.OverflowButton, pt)) {
			hit.Type = Hit::Kind::Overflow;
			return hit;
		}
		for (size_t k = 0; k < strip.Layout.Tabs.size(); k++) {
			if (PtInRect(&strip.Layout.Tabs[k], pt)) {
				hit.Tab = strip.Layout.First + (int)k;
				hit.Type = PtInRect(&strip.Layout.Close[k], pt) ? Hit::Kind::TabClose : Hit::Kind::Tab;
				return hit;
			}
		}
	}
	return hit;
}

//
// painting
//

void CDockGroupWnd::DrawCloseGlyph(CDCHandle dc, const RECT& button, bool hot, COLORREF idle) const {
	const auto& theme = m_Host.Theme();
	if (hot)
		dc.FillSolidRect(&button, theme.ButtonHotBack);

	const int inset = Width(button) / 4 + 1;
	CPen pen;
	pen.CreatePen(PS_SOLID, std::max(1, m_Host.Dpi() / 96), hot ? theme.ButtonGlyphHot : idle);
	HPEN old = dc.SelectPen(pen);
	dc.MoveTo(button.left + inset, button.top + inset);
	dc.LineTo(button.right - inset, button.bottom - inset);
	dc.MoveTo(button.right - inset - 1, button.top + inset);
	dc.LineTo(button.left + inset - 1, button.bottom - inset);
	dc.SelectPen(old);
}

void CDockGroupWnd::DrawPinGlyph(CDCHandle dc, const RECT& button, bool pinned, bool hot, COLORREF idle) const {
	const auto& theme = m_Host.Theme();
	if (hot)
		dc.FillSolidRect(&button, theme.ButtonHotBack);

	// a push pin: upright when the window is docked ("pinned"), lying on its side when it is out of an auto-hide bar
	const int s = std::max(3, Width(button) / 2 - 3);
	const int cx = (button.left + button.right) / 2, cy = (button.top + button.bottom) / 2;
	CPen pen;
	pen.CreatePen(PS_SOLID, std::max(1, m_Host.Dpi() / 96), hot ? theme.ButtonGlyphHot : idle);
	HPEN old = dc.SelectPen(pen);
	auto line = [&](int x0, int y0, int x1, int y1) {
		if (pinned) {
			dc.MoveTo(cx + x0, cy + y0);
			dc.LineTo(cx + x1, cy + y1);
		}
		else {
			// the same drawing turned by 90 degrees
			dc.MoveTo(cx - y0, cy + x0);
			dc.LineTo(cx - y1, cy + x1);
		}
	};
	line(-s / 2, -s, s / 2, -s);				// the head
	line(-s / 2, -s, -s / 2, 0);
	line(s / 2, -s, s / 2, 0);
	line(-s, 0, s, 0);							// the base
	line(0, 0, 0, s);							// the needle
	dc.SelectPen(old);
}

void CDockGroupWnd::DrawMenuGlyph(CDCHandle dc, const RECT& button, bool hot, COLORREF idle) const {
	const auto& theme = m_Host.Theme();
	if (hot)
		dc.FillSolidRect(&button, theme.ButtonHotBack);
	const COLORREF color = hot ? theme.ButtonGlyphHot : idle;
	CPen pen;
	pen.CreatePen(PS_SOLID, 1, color);
	CBrush brush;
	brush.CreateSolidBrush(color);
	HPEN oldPen = dc.SelectPen(pen);
	HBRUSH oldBrush = dc.SelectBrush(brush);
	const int cx = (button.left + button.right) / 2, cy = (button.top + button.bottom) / 2, h = std::max(2, Width(button) / 6);
	const POINT triangle[] = { { cx - h * 2, cy - h }, { cx + h * 2, cy - h }, { cx, cy + h } };
	dc.Polygon(triangle, 3);
	dc.SelectPen(oldPen);
	dc.SelectBrush(oldBrush);
}

LRESULT CDockGroupWnd::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CDockGroupWnd::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		RECT client;
		GetClientRect(&client);
		Draw((HDC)wp, client);
		return 0;
	}
	CPaintDC paintDC(m_hWnd);
	Draw(paintDC.m_hDC, paintDC.m_ps.rcPaint);
	return 0;
}

void CDockGroupWnd::Draw(HDC hdc, RECT clip) {
	CMemoryDC dc(hdc, clip);

	const auto& theme = m_Host.Theme();
	const auto& metrics = m_Host.Metrics();
	RECT rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, theme.GroupBack);
	dc.SetBkMode(TRANSPARENT);
	if (!m_Group)
		return;

	const GroupParts parts = ComputeGroupParts(*m_Group, rc, metrics);
	const auto active = m_Group->ActivePane();
	const bool activeGroup = m_Host.IsActive(m_Group);
	dc.SelectFont(m_Host.Font());

	if (parts.HasCaption) {
		dc.FillSolidRect(&parts.Caption, activeGroup ? theme.CaptionActiveBack : theme.CaptionInactiveBack);
		dc.SetTextColor(activeGroup ? theme.CaptionActiveText : theme.CaptionInactiveText);

		RECT text = parts.Caption;
		text.left += metrics.TextPadding;
		const CaptionButtons buttons = ButtonsFor(parts);
		const COLORREF idle = activeGroup ? theme.CaptionActiveText : theme.CaptionInactiveText;
		text.right = buttons.TextRight;
		if (buttons.HasClose)
			DrawCloseGlyph(dc.m_hDC, buttons.Close, m_HotButton == Button::Close, idle);
		if (buttons.HasPin)
			DrawPinGlyph(dc.m_hDC, buttons.Pin, m_Group->Location() != GroupLocation::AutoHide, m_HotButton == Button::Pin, idle);
		if (buttons.HasMenu)
			DrawMenuGlyph(dc.m_hDC, buttons.Menu, m_HotButton == Button::Menu, idle);
		if (active)
			dc.DrawText(active->Title.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
	}

	if (parts.HasTabs) {
		dc.FillSolidRect(&parts.Tabs, theme.TabStripBack);
		const bool stripAtBottom = m_Group->TabsAtBottom && !m_Group->IsDocument();
		const int accent = std::max(2, m_Host.Dpi() * 2 / 96);
		const Strip strip = LayoutStrip(parts, dc.m_hDC);
		const auto& panes = m_Group->Panes();

		for (size_t k = 0; k < strip.Layout.Tabs.size(); k++) {
			const int index = strip.Layout.First + (int)k;
			const RECT& tab = strip.Layout.Tabs[k];
			const TabSpec& spec = strip.Specs[index];
			DockPane* pane = panes[index];
			const bool selected = pane == active;
			const bool hot = index == m_HotTab;

			dc.FillSolidRect(&tab, selected ? theme.TabActiveBack : hot ? theme.TabHotBack : theme.TabInactiveBack);
			if (spec.HasIcon) {
				const RECT icon = TabIconRect(tab, metrics);
				::DrawIconEx(dc, icon.left, icon.top, pane->Icon, metrics.IconSize, metrics.IconSize, 0, nullptr, DI_NORMAL);
			}
			dc.SetTextColor(selected ? theme.TabActiveText : theme.TabInactiveText);
			RECT text = TabTextRect(tab, spec, metrics);
			dc.DrawText(pane->Title.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);

			// the close button shows on the selected tab and on the one under the mouse
			if (spec.Closable && !IsRectEmpty(&strip.Layout.Close[k]) && (selected || hot))
				DrawCloseGlyph(dc.m_hDC, strip.Layout.Close[k], hot && m_HotTabClose, theme.ButtonGlyph);
			if (selected) {
				RECT line{ tab.left, tab.top, tab.right, tab.top + accent };
				dc.FillSolidRect(&line, theme.TabActiveAccent);
			}
		}

		if (strip.Layout.Overflow) {
			const RECT& area = strip.Layout.OverflowButton;
			const int size = std::min(metrics.ButtonSize, Height(area));
			const RECT button{ area.left + (Width(area) - size) / 2, area.top + (Height(area) - size) / 2,
				area.left + (Width(area) - size) / 2 + size, area.top + (Height(area) - size) / 2 + size };
			if (m_HotOverflow)
				dc.FillSolidRect(&button, theme.ButtonHotBack);
			const COLORREF glyph = m_HotOverflow ? theme.ButtonGlyphHot : theme.ButtonGlyph;
			CPen pen;
			pen.CreatePen(PS_SOLID, 1, glyph);
			CBrush brush;
			brush.CreateSolidBrush(glyph);
			HPEN oldPen = dc.SelectPen(pen);
			HBRUSH oldBrush = dc.SelectBrush(brush);
			const int cx = (button.left + button.right) / 2, cy = (button.top + button.bottom) / 2, h = std::max(2, size / 6);
			const POINT triangle[] = { { cx - h * 2, cy - h }, { cx + h * 2, cy - h }, { cx, cy + h } };
			dc.Polygon(triangle, 3);
			dc.SelectPen(oldPen);
			dc.SelectBrush(oldBrush);
		}

		// a hairline between the strip and the content
		RECT edge = parts.Tabs;
		if (stripAtBottom)
			edge.bottom = edge.top + 1;
		else
			edge.top = edge.bottom - 1;
		dc.FillSolidRect(&edge, theme.Border);
	}
}

//
// mouse
//

void CDockGroupWnd::SetHot(int tab, bool close, bool overflow) {
	if (tab == m_HotTab && close == m_HotTabClose && overflow == m_HotOverflow)
		return;
	m_HotTab = tab;
	m_HotTabClose = close;
	m_HotOverflow = overflow;
	if (m_Group) {
		RECT rc;
		GetClientRect(&rc);
		const GroupParts parts = ComputeGroupParts(*m_Group, rc, m_Host.Metrics());
		InvalidateRect(&parts.Tabs, FALSE);
	}
}

LRESULT CDockGroupWnd::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (!m_Group)
		return 0;
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	const Hit hit = Locate(pt);

	switch (hit.Type) {
		case Hit::Kind::CaptionClose:
		case Hit::Kind::CaptionPin:
		case Hit::Kind::CaptionMenu:
			m_PressButton = ButtonOf(hit.Type);
			SetCapture();
			Invalidate(FALSE);
			break;

		case Hit::Kind::Caption:
			m_Host.ActivatePane(m_Group->ActivePane());
			if (m_Group) {
				// dragging the caption takes the whole group along
				m_CaptionPending = true;
				m_CaptionStart = pt;
				SetCapture();
			}
			break;

		case Hit::Kind::TabClose:
			m_PressTabClose = hit.Tab;
			SetCapture();
			break;

		case Hit::Kind::Tab: {
			DockPane* pane = PaneAt(hit.Tab);
			m_Host.ActivatePane(pane);
			// this may take the window with it if the layout reacts to the activation, so start the drag last
			if (m_Group && pane) {
				m_DragPane = pane;
				m_DragStart = pt;
				m_Dragging = false;
				SetCapture();
			}
			break;
		}

		case Hit::Kind::Overflow: {
			RECT rc;
			GetClientRect(&rc);
			const GroupParts parts = ComputeGroupParts(*m_Group, rc, m_Host.Metrics());
			CClientDC dc(m_hWnd);
			const auto strip = LayoutStrip(parts, dc.m_hDC);
			ShowOverflowMenu(strip.Layout.OverflowButton, m_Group->TabsAtBottom && !m_Group->IsDocument());
			break;
		}

		default:
			break;
	}
	return 0;
}

LRESULT CDockGroupWnd::OnLButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Session) {
		EndDockDrag(true);
		return 0;
	}
	if (!m_Group)
		return 0;
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	const Hit hit = Locate(pt);

	// what was pressed and where the button went up decide whether it counts as a click
	DockPane* toClose = nullptr;
	Button clicked = Button::None;
	if (m_PressButton != Button::None && ButtonOf(hit.Type) == m_PressButton)
		clicked = m_PressButton;
	if (clicked == Button::Close)
		toClose = m_Group->ActivePane();
	else if (m_PressTabClose >= 0 && hit.Type == Hit::Kind::TabClose && hit.Tab == m_PressTabClose)
		toClose = PaneAt(hit.Tab);

	const bool captured = m_PressButton != Button::None || m_PressTabClose >= 0 || m_DragPane || m_CaptionPending;
	EndInteraction();
	if (captured)
		ReleaseCapture();
	if (toClose)
		m_Host.ClosePane(toClose);		// the window may be retired by this; touch nothing afterwards
	else if (clicked == Button::Pin || clicked == Button::Menu)
		RunButton(clicked);
	return 0;
}

LRESULT CDockGroupWnd::OnLButtonDblClk(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (!m_Group)
		return 0;
	// a double click on the caption or on a tab of a tool window floats it, or docks it again (the first click has
	// already activated the pane)
	const Hit hit = Locate({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
	DockPane* pane = nullptr;
	if (hit.Type == Hit::Kind::Caption)
		pane = m_Group->ActivePane();
	else if (hit.Type == Hit::Kind::Tab)
		pane = PaneAt(hit.Tab);
	if (pane && pane->Kind() == PaneKind::Tool)
		m_Host.ToggleFloat(pane);		// the window may be retired by this; touch nothing afterwards
	return 0;
}

LRESULT CDockGroupWnd::OnMButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	const Hit hit = Locate({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
	m_MiddleTab = hit.Type == Hit::Kind::Tab || hit.Type == Hit::Kind::TabClose ? hit.Tab : -1;
	return 0;
}

LRESULT CDockGroupWnd::OnMButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	const Hit hit = Locate({ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) });
	const int pressed = m_MiddleTab;
	m_MiddleTab = -1;
	if (pressed >= 0 && pressed == hit.Tab) {
		// only tabs that have a close button close this way
		RECT rc;
		GetClientRect(&rc);
		CClientDC dc(m_hWnd);
		const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, m_Host.Metrics()), dc.m_hDC);
		if (pressed < (int)strip.Specs.size() && strip.Specs[pressed].Closable)
			m_Host.ClosePane(PaneAt(pressed));
	}
	return 0;
}

LRESULT CDockGroupWnd::OnRButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (!m_Group)
		return 0;
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	const Hit hit = Locate(pt);

	DockPane* pane = nullptr;
	if (hit.Type == Hit::Kind::Tab || hit.Type == Hit::Kind::TabClose)
		pane = PaneAt(hit.Tab);
	else if (hit.Type == Hit::Kind::Caption || ButtonOf(hit.Type) != Button::None)
		pane = m_Group->ActivePane();
	if (!pane)
		return 0;

	POINT screen = pt;
	ClientToScreen(&screen);
	m_Host.ActivatePane(pane);
	m_Host.ShowPaneMenu(pane, screen);
	return 0;
}

LRESULT CDockGroupWnd::OnMouseMove(UINT, WPARAM wp, LPARAM lp, BOOL&) {
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

	if (m_Session) {
		if (!m_Host.IsDragging()) {
			EndDockDrag(false);		// the drag was cancelled behind our back
			return 0;
		}
		m_LastScreen = pt;
		ClientToScreen(&m_LastScreen);
		m_Host.UpdateDrag(m_LastScreen, (::GetKeyState(VK_CONTROL) & 0x8000) != 0);
		return 0;
	}
	if (!m_Group)
		return 0;

	if (m_CaptionPending) {
		if (!(wp & MK_LBUTTON)) {
			m_CaptionPending = false;
			return 0;
		}
		if (std::abs(pt.x - m_CaptionStart.x) >= ::GetSystemMetricsForDpi(SM_CXDRAG, m_Host.Dpi()) ||
			std::abs(pt.y - m_CaptionStart.y) >= ::GetSystemMetricsForDpi(SM_CYDRAG, m_Host.Dpi())) {
			DockPane* pane = m_Group->ActivePane();
			if (!pane || !BeginDockDrag(pane, true, pt))
				m_CaptionPending = false;		// nothing to drag (e.g. the group is in an auto-hide bar)
		}
		return 0;
	}

	if (m_DragPane) {
		if (!(wp & MK_LBUTTON)) {
			EndInteraction();
			return 0;
		}

		// away from the tab strip the drag is no longer about the order of the tabs but about where the pane goes
		RECT bounds;
		GetClientRect(&bounds);
		const GroupParts parts = ComputeGroupParts(*m_Group, bounds, m_Host.Metrics());
		const int band = std::max(::GetSystemMetricsForDpi(SM_CYDRAG, m_Host.Dpi()), m_Host.Metrics().TabHeight / 2);
		if (pt.y < parts.Tabs.top - band || pt.y >= parts.Tabs.bottom + band || pt.x < bounds.left - band || pt.x >= bounds.right + band) {
			if (BeginDockDrag(m_DragPane, false, pt))
				return 0;
		}

		if (!m_Dragging) {
			const int threshold = ::GetSystemMetricsForDpi(SM_CXDRAG, m_Host.Dpi());
			m_Dragging = std::abs(pt.x - m_DragStart.x) >= threshold;
		}
		if (m_Dragging) {
			// swap with a neighbour when the mouse passes its centre
			RECT rc;
			GetClientRect(&rc);
			CClientDC dc(m_hWnd);
			const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, m_Host.Metrics()), dc.m_hDC);
			const auto& panes = m_Group->Panes();
			const int index = (int)(std::find(panes.begin(), panes.end(), m_DragPane) - panes.begin());
			auto center = [&](int tab) {
				const int k = tab - strip.Layout.First;
				return k >= 0 && k < (int)strip.Layout.Tabs.size() ? (strip.Layout.Tabs[k].left + strip.Layout.Tabs[k].right) / 2 : -1;
			};
			const int next = center(index + 1), previous = center(index - 1);
			if (next >= 0 && pt.x > next)
				m_Host.Layout().ReorderTab(m_DragPane, index + 1);
			else if (previous >= 0 && pt.x < previous)
				m_Host.Layout().ReorderTab(m_DragPane, index - 1);
		}
		return 0;
	}

	Hit hit = Locate(pt);
	const bool onTab = hit.Type == Hit::Kind::Tab || hit.Type == Hit::Kind::TabClose;
	SetHot(onTab ? hit.Tab : -1, hit.Type == Hit::Kind::TabClose, hit.Type == Hit::Kind::Overflow);

	const Button hotButton = ButtonOf(hit.Type);
	if (hotButton != m_HotButton) {
		m_HotButton = hotButton;
		Invalidate(FALSE);
	}
	if (!m_Tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_Tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	return 0;
}

LRESULT CDockGroupWnd::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tracking = false;
	SetHot(-1, false, false);
	if (m_HotButton != Button::None) {
		m_HotButton = Button::None;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CDockGroupWnd::OnMouseWheel(UINT, WPARAM wp, LPARAM lp, BOOL& handled) {
	handled = FALSE;
	if (!m_Group)
		return 0;
	POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	ScreenToClient(&pt);
	RECT rc;
	GetClientRect(&rc);
	const GroupParts parts = ComputeGroupParts(*m_Group, rc, m_Host.Metrics());
	if (!parts.HasTabs || !PtInRect(&parts.Tabs, pt))
		return 0;

	CClientDC dc(m_hWnd);
	const auto strip = LayoutStrip(parts, dc.m_hDC);
	if (!strip.Layout.Overflow)
		return 0;
	const int last = (int)strip.Specs.size() - 1;
	m_First = std::clamp(m_First + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1), 0, last);
	m_ScrollLocked = true;
	InvalidateRect(&parts.Tabs, FALSE);
	handled = TRUE;
	return 0;
}

LRESULT CDockGroupWnd::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Session) {
		// the capture was taken from us (another window, a menu): the drag is off
		EndInteraction();
		m_Host.EndDrag(false);
		return 0;
	}
	const bool wasActive = m_PressButton != Button::None || m_PressTabClose >= 0 || m_DragPane || m_CaptionPending;
	m_CaptionPending = false;
	m_PressButton = Button::None;
	m_PressTabClose = -1;
	m_DragPane = nullptr;
	m_Dragging = false;
	if (wasActive)
		Invalidate(FALSE);
	return 0;
}

void CDockGroupWnd::ShowOverflowMenu(const RECT& button, bool stripAtBottom) {
	const std::vector<DockPane*> panes = m_Group->Panes();
	const DockPane* active = m_Group->ActivePane();

	CMenu menu;
	menu.CreatePopupMenu();
	for (size_t i = 0; i < panes.size(); i++)
		menu.AppendMenu(MF_STRING | (panes[i] == active ? MF_CHECKED : 0), (UINT_PTR)(i + 1), panes[i]->Title.c_str());

	POINT pt{ button.right, stripAtBottom ? button.top : button.bottom };
	ClientToScreen(&pt);
	const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | (stripAtBottom ? TPM_BOTTOMALIGN : 0);
	const UINT cmd = (UINT)menu.TrackPopupMenu(flags, pt.x, pt.y, m_hWnd);
	if (cmd >= 1 && cmd <= panes.size()) {
		m_ScrollLocked = false;
		m_Host.ActivatePane(panes[cmd - 1]);
	}
}

}
