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
		const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
		const RECT& c = parts.Content;
		const auto active = m_Group->ActivePane();
		m_Host.EnsureContent(active);
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
	NotifyStructure();
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

	const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
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
			// (the chrome takes the focus itself only when asked to; otherwise it belongs to the content)
			if (!m_ChromeFocusWanted && pane->hWnd && ::IsWindowVisible(pane->hWnd))
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

	HFONT old = dc.SelectFont(Font());
	for (auto pane : m_Group->Panes()) {
		SIZE size{};
		dc.GetTextExtent(pane->Title.c_str(), (int)pane->Title.size(), &size);
		const bool closable = m_Group->IsDocument() && Has(pane->Caps, PaneCaps::CanClose);
		strip.Specs.push_back({ size.cx, pane->Icon != nullptr, closable, pane->Modified && !closable });
	}
	dc.SelectFont(old);

	// a new active tab takes the scrolling back from the user
	const int active = m_Group->ActiveIndex();
	if (active != m_LastActive) {
		m_LastActive = active;
		m_ScrollLocked = false;
	}
	strip.Layout = LayoutTabStrip(strip.Specs, parts.Tabs, Metrics(), m_First, m_ScrollLocked ? -1 : active);
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
	const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, Metrics()), dc.m_hDC);
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
	return ComputeCaptionButtons(parts.Caption, CloseButtonVisible(), PinVisible(), MenuVisible(), Metrics());
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
		const CaptionButtons b = ButtonsFor(ComputeGroupParts(*m_Group, rc, Metrics()));
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
	const auto& metrics = Metrics();
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
	pen.CreatePen(PS_SOLID, std::max(1, Dpi() / 96), hot ? theme.ButtonGlyphHot : idle);
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
	pen.CreatePen(PS_SOLID, std::max(1, Dpi() / 96), hot ? theme.ButtonGlyphHot : idle);
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
	const auto& metrics = Metrics();
	RECT rc;
	GetClientRect(&rc);
	dc.FillSolidRect(&rc, theme.GroupBack);
	dc.SetBkMode(TRANSPARENT);
	if (!m_Group)
		return;

	const GroupParts parts = ComputeGroupParts(*m_Group, rc, metrics);
	const auto active = m_Group->ActivePane();
	const bool activeGroup = m_Host.IsActive(m_Group);
	dc.SelectFont(Font());

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
		if (active) {
			const std::wstring title = CaptionText(*active);
			dc.DrawText(title.c_str(), (int)title.size(), &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
		}
	}

	if (parts.HasTabs) {
		dc.FillSolidRect(&parts.Tabs, theme.TabStripBack);
		const bool stripAtBottom = m_Group->TabsAtBottom && !m_Group->IsDocument();
		const int accent = std::max(2, Dpi() * 2 / 96);
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

			// the close button shows on the selected tab and on the one under the mouse; a modified document shows a dot
			// instead, which turns into the button when the mouse is on it
			if (spec.Closable && !IsRectEmpty(&strip.Layout.Close[k])) {
				const bool overButton = hot && m_HotTabClose;
				if (pane->Modified && !overButton)
					DrawModifiedDot(dc.m_hDC, strip.Layout.Close[k], selected ? theme.TabActiveText : theme.ButtonGlyph);
				else if (selected || hot)
					DrawCloseGlyph(dc.m_hDC, strip.Layout.Close[k], overButton, theme.ButtonGlyph);
			}
			else if (spec.Marked && !IsRectEmpty(&strip.Layout.Mark[k])) {
				DrawModifiedDot(dc.m_hDC, strip.Layout.Mark[k], selected ? theme.TabActiveText : theme.ButtonGlyph);
			}
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

	// where the keyboard is
	if (HasChromeFocus()) {
		RECT focus = FocusRect();
		if (!IsRectEmpty(&focus)) {
			InflateRect(&focus, -2, -2);
			::DrawFocusRect(dc, &focus);
		}
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
		const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
		InvalidateRect(&parts.Tabs, FALSE);
	}
}

LRESULT CDockGroupWnd::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	m_Host.HideTip();
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
			const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
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
		const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, Metrics()), dc.m_hDC);
		if (pressed < (int)strip.Specs.size() && strip.Specs[pressed].Closable)
			m_Host.ClosePane(PaneAt(pressed));
	}
	return 0;
}

LRESULT CDockGroupWnd::OnRButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	m_Host.HideTip();
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
		if (std::abs(pt.x - m_CaptionStart.x) >= ::GetSystemMetricsForDpi(SM_CXDRAG, Dpi()) ||
			std::abs(pt.y - m_CaptionStart.y) >= ::GetSystemMetricsForDpi(SM_CYDRAG, Dpi())) {
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
		const GroupParts parts = ComputeGroupParts(*m_Group, bounds, Metrics());
		const int band = std::max(::GetSystemMetricsForDpi(SM_CYDRAG, Dpi()), Metrics().TabHeight / 2);
		if (pt.y < parts.Tabs.top - band || pt.y >= parts.Tabs.bottom + band || pt.x < bounds.left - band || pt.x >= bounds.right + band) {
			if (BeginDockDrag(m_DragPane, false, pt))
				return 0;
		}

		if (!m_Dragging) {
			const int threshold = ::GetSystemMetricsForDpi(SM_CXDRAG, Dpi());
			m_Dragging = std::abs(pt.x - m_DragStart.x) >= threshold;
		}
		if (m_Dragging) {
			// swap with a neighbour when the mouse passes its centre
			RECT rc;
			GetClientRect(&rc);
			CClientDC dc(m_hWnd);
			const auto strip = LayoutStrip(ComputeGroupParts(*m_Group, rc, Metrics()), dc.m_hDC);
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
	UpdateTip(hit);

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
	m_Host.CancelTip(m_hWnd);
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
	const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
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

//
// accessibility
//

LRESULT CDockGroupWnd::OnGetObject(UINT, WPARAM wp, LPARAM lp, BOOL& handled) {
	if ((LONG)lp != OBJID_CLIENT) {
		handled = FALSE;
		return 0;
	}
	if (!m_Acc)
		m_Acc = DockAccessible::Create(this);
	if (!m_Acc) {
		handled = FALSE;
		return 0;
	}
	return ::LresultFromObject(IID_IAccessible, wp, m_Acc);
}

CDockGroupWnd::~CDockGroupWnd() {
	if (m_Acc) {
		m_Acc->Detach();
		m_Acc->Release();
	}
}

std::wstring CDockGroupWnd::AccName() const {
	if (!m_Group)
		return {};
	if (m_Group->IsDocument())
		return L"Documents";
	auto pane = m_Group->ActivePane();
	return pane ? pane->Title : std::wstring();
}

LONG CDockGroupWnd::AccRole() const {
	return ROLE_SYSTEM_GROUPING;
}

std::vector<HWND> CDockGroupWnd::AccChildWindows() const {
	std::vector<HWND> windows;
	if (m_Group) {
		auto pane = m_Group->ActivePane();
		if (pane && pane->hWnd && ::IsWindow(pane->hWnd) && ::GetParent(pane->hWnd) == m_hWnd)
			windows.push_back(pane->hWnd);
	}
	return windows;
}

std::vector<AccElement> CDockGroupWnd::AccElements() const {
	std::vector<AccElement> list;
	if (!m_Group || !m_hWnd)
		return list;

	// laying the strip out keeps the scroll position, like drawing does
	auto self = const_cast<CDockGroupWnd*>(this);
	CDockHost* host = &m_Host;
	RECT rc;
	GetClientRect(&rc);
	POINT origin{ 0, 0 };
	::ClientToScreen(m_hWnd, &origin);
	auto toScreen = [&](RECT r) {
		OffsetRect(&r, origin.x, origin.y);
		return r;
	};
	const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
	DockPane* active = m_Group->ActivePane();

	auto button = [&](const wchar_t* name, LONG role, const RECT& where, const wchar_t* action, Button which) {
		AccElement e;
		e.Name = name;
		e.Role = role;
		e.Screen = toScreen(where);
		e.Action = action;
		e.Invoke = [self, which] { self->RunButton(which); };
		e.Key = which == Button::Menu ? L"btn:menu" : which == Button::Pin ? L"btn:pin" : L"btn:close";
		list.push_back(std::move(e));
	};

	if (parts.HasCaption) {
		AccElement caption;
		caption.Name = active ? active->Title : std::wstring();
		if (active)
			caption.Description = active->Modified ? L"Modified" : L"";
		caption.Role = ROLE_SYSTEM_TITLEBAR;
		caption.Screen = toScreen(parts.Caption);
		list.push_back(std::move(caption));

		const CaptionButtons b = ButtonsFor(parts);
		if (b.HasMenu)
			button(L"Window Position", ROLE_SYSTEM_BUTTONMENU, b.Menu, L"Open", Button::Menu);
		if (b.HasPin)
			button(m_Group->Location() == GroupLocation::AutoHide ? L"Dock" : L"Auto Hide", ROLE_SYSTEM_PUSHBUTTON, b.Pin, L"Press", Button::Pin);
		if (b.HasClose)
			button(L"Close", ROLE_SYSTEM_PUSHBUTTON, b.Close, L"Press", Button::Close);
	}

	if (parts.HasTabs) {
		CClientDC dc(m_hWnd);
		const Strip strip = self->LayoutStrip(parts, dc.m_hDC);
		const int first = strip.Layout.First;
		const int visible = (int)strip.Layout.Tabs.size();
		const DockPane* focused = m_Host.ActivePane();
		const auto& panes = m_Group->Panes();
		for (int i = 0; i < (int)panes.size(); i++) {
			DockPane* pane = panes[i];
			const bool shown = i >= first && i < first + visible;

			AccElement tab;
			tab.Name = pane->Title;
			tab.Description = pane->Modified ? (pane->Tooltip.empty() ? std::wstring(L"Modified") : pane->Tooltip + L" (modified)") : pane->Tooltip;
			tab.Role = ROLE_SYSTEM_PAGETAB;
			tab.State = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
			if (pane == active)
				tab.State |= STATE_SYSTEM_SELECTED;
			if (pane == focused)
				tab.State |= STATE_SYSTEM_FOCUSED;
			if (shown)
				tab.Screen = toScreen(strip.Layout.Tabs[i - first]);
			else
				tab.State |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
			tab.Action = L"Switch";
			tab.Invoke = tab.Select = [host, pane] { host->ActivatePane(pane); };
			tab.Key = L"tab:" + pane->Id();
			list.push_back(std::move(tab));

			if (strip.Specs[i].Closable) {
				AccElement close;
				close.Name = L"Close " + pane->Title;
				close.Role = ROLE_SYSTEM_PUSHBUTTON;
				if (shown)
					close.Screen = toScreen(strip.Layout.Close[i - first]);
				else
					close.State = STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
				close.Action = L"Press";
				close.Invoke = [host, pane] { host->ClosePane(pane); };
				close.Key = L"close:" + pane->Id();
				list.push_back(std::move(close));
			}
		}
		if (strip.Layout.Overflow) {
			AccElement more;
			more.Name = L"Tab list";
			more.Role = ROLE_SYSTEM_BUTTONDROPDOWN;
			more.Screen = toScreen(strip.Layout.OverflowButton);
			more.Action = L"Open";
			const RECT where = strip.Layout.OverflowButton;
			const bool bottom = m_Group->TabsAtBottom && !m_Group->IsDocument();
			more.Invoke = [self, where, bottom] { self->ShowOverflowMenu(where, bottom); };
			more.Key = L"overflow";
			list.push_back(std::move(more));
		}
	}
	return list;
}

// Tells the clients that the children (tabs, buttons) are not the ones they knew.
void CDockGroupWnd::NotifyStructure() {
	if (!m_hWnd || !m_Group)
		return;
	std::wstring signature = std::to_wstring(m_Group->ActiveIndex()) + L"|" + std::to_wstring((int)m_Group->Location());
	for (auto pane : m_Group->Panes())
		signature += L"|" + pane->Title;
	if (signature != m_AccSignature) {
		const bool first = m_AccSignature.empty();
		m_AccSignature = std::move(signature);
		if (!first)
			::NotifyWinEvent(EVENT_OBJECT_REORDER, m_hWnd, OBJID_CLIENT, CHILDID_SELF);
	}
}

//
// modified marks and tooltips
//

std::wstring CDockGroupWnd::CaptionText(const DockPane& pane) {
	return pane.Modified ? pane.Title + L" \u25CF" : pane.Title;
}

void CDockGroupWnd::DrawModifiedDot(CDCHandle dc, const RECT& slot, COLORREF color) const {
	const int size = MarkSize(Metrics());
	const int cx = (slot.left + slot.right) / 2, cy = (slot.top + slot.bottom) / 2;
	CBrush brush;
	brush.CreateSolidBrush(color);
	CPen pen;
	pen.CreatePen(PS_SOLID, 1, color);
	HBRUSH oldBrush = dc.SelectBrush(brush);
	HPEN oldPen = dc.SelectPen(pen);
	dc.Ellipse(cx - size / 2, cy - size / 2, cx - size / 2 + size, cy - size / 2 + size);
	dc.SelectPen(oldPen);
	dc.SelectBrush(oldBrush);
}

// What the mouse rests on, in words: what a button does, the full title of what is cut off, what the application says
// about a tab.
bool CDockGroupWnd::TipFor(const Hit& hit, RECT& target, std::wstring& text) {
	if (!m_Group || !m_hWnd)
		return false;
	RECT rc;
	GetClientRect(&rc);
	const GroupParts parts = ComputeGroupParts(*m_Group, rc, Metrics());
	POINT origin{ 0, 0 };
	ClientToScreen(&origin);
	auto toScreen = [&](RECT r) {
		OffsetRect(&r, origin.x, origin.y);
		return r;
	};

	switch (hit.Type) {
		case Hit::Kind::CaptionClose:
		case Hit::Kind::CaptionPin:
		case Hit::Kind::CaptionMenu: {
			const CaptionButtons b = ButtonsFor(parts);
			if (hit.Type == Hit::Kind::CaptionClose) {
				target = toScreen(b.Close);
				text = L"Close";
			}
			else if (hit.Type == Hit::Kind::CaptionPin) {
				target = toScreen(b.Pin);
				text = m_Group->Location() == GroupLocation::AutoHide ? L"Dock" : L"Auto Hide";
			}
			else {
				target = toScreen(b.Menu);
				text = L"Window Position";
			}
			return true;
		}

		case Hit::Kind::Caption: {
			DockPane* pane = m_Group->ActivePane();
			if (!pane)
				return false;
			text = pane->Tooltip;
			if (text.empty()) {
				// the title, if the caption has no room for all of it
				CClientDC dc(m_hWnd);
				HFONT old = dc.SelectFont(Font());
				const std::wstring title = CaptionText(*pane);
				SIZE size{};
				dc.GetTextExtent(title.c_str(), (int)title.size(), &size);
				dc.SelectFont(old);
				const CaptionButtons b = ButtonsFor(parts);
				if (size.cx <= b.TextRight - (parts.Caption.left + Metrics().TextPadding))
					return false;
				text = pane->Title;
			}
			RECT area = parts.Caption;
			area.right = ButtonsFor(parts).TextRight;
			target = toScreen(area);
			return true;
		}

		case Hit::Kind::Tab:
		case Hit::Kind::TabClose: {
			DockPane* pane = PaneAt(hit.Tab);
			if (!pane)
				return false;
			CClientDC dc(m_hWnd);
			const Strip strip = LayoutStrip(parts, dc.m_hDC);
			const int k = hit.Tab - strip.Layout.First;
			if (k < 0 || k >= (int)strip.Layout.Tabs.size())
				return false;
			if (hit.Type == Hit::Kind::TabClose) {
				target = toScreen(strip.Layout.Close[k]);
				text = L"Close";
				return true;
			}
			text = pane->Tooltip;
			if (text.empty()) {
				// a tab that is cut off (the last one that fits) shows its title
				const RECT room = TabTextRect(strip.Layout.Tabs[k], strip.Specs[hit.Tab], Metrics());
				if (strip.Specs[hit.Tab].TextWidth <= Width(room))
					return false;
				text = pane->Title;
			}
			target = toScreen(strip.Layout.Tabs[k]);
			return true;
		}

		case Hit::Kind::Overflow: {
			CClientDC dc(m_hWnd);
			const Strip strip = LayoutStrip(parts, dc.m_hDC);
			target = toScreen(strip.Layout.OverflowButton);
			text = L"Show open tabs";
			return true;
		}

		default:
			return false;
	}
}

void CDockGroupWnd::UpdateTip(const Hit& hit) {
	RECT target{};
	std::wstring text;
	if (TipFor(hit, target, text))
		m_Host.RequestTip(m_hWnd, target, text, Dpi());
	else
		m_Host.CancelTip(m_hWnd);
}

//
// keyboard focus in the chrome
//

// The items the keyboard visits, in the order of the elements: caption buttons, then tabs (each followed by its close
// button) and the tab list button.
std::vector<AccElement> CDockGroupWnd::FocusItems() const {
	std::vector<AccElement> items;
	for (auto& e : AccElements())
		if (!e.Key.empty())
			items.push_back(std::move(e));
	return items;
}

bool CDockGroupWnd::FocusChrome(DockPane* pane) {
	if (!m_hWnd || !m_Group || m_Group->Panes().empty())
		return false;
	if (!pane || pane->Group() != m_Group)
		pane = m_Group->ActivePane();
	if (!pane)
		return false;
	m_ChromeFocusWanted = true;
	m_FocusKey = L"tab:" + pane->Id();
	// a group with a single pane and no tab strip has the caption's buttons only: start on the first of them
	const auto items = FocusItems();
	if (std::none_of(items.begin(), items.end(), [&](auto& e) { return e.Key == m_FocusKey; }))
		m_FocusKey = items.empty() ? std::wstring() : items.front().Key;
	if (m_FocusKey.empty()) {
		m_ChromeFocusWanted = false;
		return false;
	}
	::SetFocus(m_hWnd);
	m_ChromeFocusWanted = false;
	m_ChromeFocus = ::GetFocus() == m_hWnd;
	EnsureFocusVisible();
	Invalidate(FALSE);
	return m_ChromeFocus;
}

bool CDockGroupWnd::HasChromeFocus() const {
	return m_ChromeFocus && m_hWnd && ::GetFocus() == m_hWnd;
}

std::wstring CDockGroupWnd::FocusName() const {
	if (!HasChromeFocus())
		return {};
	for (auto& e : FocusItems())
		if (e.Key == m_FocusKey)
			return e.Name;
	return {};
}

// a tab that is scrolled out of the strip is scrolled in
void CDockGroupWnd::EnsureFocusVisible() {
	if (!m_Group || (m_FocusKey.rfind(L"tab:", 0) != 0 && m_FocusKey.rfind(L"close:", 0) != 0))
		return;
	const std::wstring id = m_FocusKey.substr(m_FocusKey.find(L':') + 1);
	const auto& panes = m_Group->Panes();
	int index = -1;
	for (int i = 0; i < (int)panes.size(); i++)
		if (panes[i]->Id() == id)
			index = i;
	if (index < 0)
		return;
	RECT rc;
	GetClientRect(&rc);
	CClientDC dc(m_hWnd);
	const auto parts = ComputeGroupParts(*m_Group, rc, Metrics());
	const auto strip = LayoutStrip(parts, dc.m_hDC);
	if (index < strip.Layout.First || index >= strip.Layout.First + (int)strip.Layout.Tabs.size()) {
		m_First = index;
		m_ScrollLocked = true;
	}
}

void CDockGroupWnd::MoveFocus(int step) {
	const auto items = FocusItems();
	if (items.empty())
		return;
	int at = -1;
	for (int i = 0; i < (int)items.size(); i++)
		if (items[i].Key == m_FocusKey)
			at = i;
	at = at < 0 ? 0 : std::clamp(at + step, 0, (int)items.size() - 1);
	m_FocusKey = items[at].Key;
	EnsureFocusVisible();
	Invalidate(FALSE);
	if (m_Host.IsTipVisible())
		m_Host.HideTip();
	::NotifyWinEvent(EVENT_OBJECT_FOCUS, m_hWnd, OBJID_CLIENT, at + 1);
}

RECT CDockGroupWnd::FocusRect() const {
	for (auto& e : FocusItems()) {
		if (e.Key == m_FocusKey && !IsRectEmpty(&e.Screen)) {
			RECT rc = e.Screen;
			::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<POINT*>(&rc), 2);
			return rc;
		}
	}
	return {};
}

LRESULT CDockGroupWnd::OnKillFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_ChromeFocus) {
		m_ChromeFocus = false;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CDockGroupWnd::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL& handled) {
	if (!HasChromeFocus()) {
		handled = FALSE;
		return 0;
	}
	return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
}

LRESULT CDockGroupWnd::OnKeyDown(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	if (!HasChromeFocus() || !m_Group) {
		handled = FALSE;
		return 0;
	}
	const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	const auto items = FocusItems();
	const AccElement* current = nullptr;
	for (auto& e : items)
		if (e.Key == m_FocusKey)
			current = &e;

	// what a key press does to a tab or button that has the focus (the window may go away with it)
	auto paneOfFocus = [&]() -> DockPane* {
		if (m_FocusKey.rfind(L"tab:", 0) != 0 && m_FocusKey.rfind(L"close:", 0) != 0)
			return m_Group->ActivePane();
		return m_Host.Layout().FindPane(m_FocusKey.substr(m_FocusKey.find(L':') + 1));
	};

	switch (wp) {
		case VK_LEFT: case VK_UP:
			MoveFocus(-1);
			return 0;
		case VK_RIGHT: case VK_DOWN:
			MoveFocus(1);
			return 0;
		case VK_HOME:
			MoveFocus(-(int)items.size());
			return 0;
		case VK_END:
			MoveFocus((int)items.size());
			return 0;

		case VK_RETURN: case VK_SPACE:
			if (current && current->Invoke) {
				auto invoke = current->Invoke;
				invoke();		// this can end the window; touch nothing afterwards
			}
			return 0;

		case VK_DELETE:
			// (closes the tab that is on; the buttons have nothing to delete)
			if (m_FocusKey.rfind(L"tab:", 0) != 0 && m_FocusKey.rfind(L"close:", 0) != 0)
				return 0;
			if (auto pane = paneOfFocus()) {
				DockPane* target = pane;
				m_Host.ClosePane(target);
			}
			return 0;

		case VK_ESCAPE:
			// back to the content
			if (auto pane = m_Group->ActivePane())
				m_Host.ActivatePane(pane);
			return 0;

		case VK_TAB:
			m_Host.FocusNextChrome(m_Group->ActivePane(), !shift);
			return 0;

		case VK_APPS: case VK_F10:
			if (wp == VK_F10 && !shift)
				break;
			if (auto pane = paneOfFocus()) {
				RECT where = FocusRect();
				POINT screen{ where.left, where.bottom };
				ClientToScreen(&screen);
				m_Host.ShowPaneMenu(pane, screen);
			}
			return 0;
	}
	handled = FALSE;
	return 0;
}

}
