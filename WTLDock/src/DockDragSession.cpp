#include "DockDragSession.h"
#include "DockGroupWnd.h"
#include "DockFloatFrame.h"

namespace WTLDock {

DockDragSession::DockDragSession(CDockHost& host, DockPane* pane, bool wholeGroup, POINT start, bool external) :
	m_Host(host), m_Pane(pane), m_Whole(wholeGroup), m_External(external), m_Version(host.m_Layout.Version()) {

	if (auto group = pane->Group()) {
		if (HWND window = host.GroupWindow(group)) {
			RECT rc;
			::GetWindowRect(window, &rc);
			m_Grab = { std::clamp<LONG>(start.x - rc.left, 0, Width(rc)), std::clamp<LONG>(start.y - rc.top, 0, Height(rc)) };
		}
		if (external && group->Float())
			m_IgnoredFrame = host.FloatWindow(group->Float()->Id());
	}
}

DockDragSession::~DockDragSession() {
	HideAll();
	for (auto& w : m_Markers)
		if (w->IsWindow())
			w->DestroyWindow();
	if (m_Preview && m_Preview->IsWindow())
		m_Preview->DestroyWindow();
}

void DockDragSession::HideAll() {
	for (auto& w : m_Markers)
		w->Hide();
	if (m_Preview)
		m_Preview->Hide();
}

int DockDragSession::VisibleMarkers() const {
	int count = 0;
	for (auto& w : m_Markers)
		count += w->IsShown();
	return count;
}

DropContext DockDragSession::BuildContext(POINT cursor, bool suppressDocking) const {
	DropContext c;
	c.Layout = &m_Host.m_Layout;
	c.Pane = m_Pane;
	c.WholeGroup = m_Whole;
	c.Metrics = m_Host.m_Metrics;
	c.DockingAllowed = !suppressDocking;

	const DockGroup* source = m_Pane->Group();

	// the group under the cursor (a group that is dragged as a whole cannot be dropped on itself)
	const DockGroup* hover = m_Host.GroupAtScreen(cursor, m_IgnoredFrame);
	if (hover && m_Whole && hover == source)
		hover = nullptr;
	if (hover) {
		if (auto it = m_Host.m_Groups.find(hover); it != m_Host.m_Groups.end()) {
			c.Hover = hover;
			::GetWindowRect(it->second->m_hWnd, &c.HoverRect);
			it->second->DropInfo(c.HoverZones, c.HoverTabs, c.HoverFirstTab);
		}
	}

	// the docking area of the main window
	if (m_Host.IsWindowVisible() && !::IsIconic(::GetAncestor(m_Host.m_hWnd, GA_ROOT))) {
		POINT origin{ 0, 0 };
		m_Host.ClientToScreen(&origin);
		c.MainBounds = m_Host.m_Layout.Root().Rect;
		OffsetRect(&c.MainBounds, origin.x, origin.y);
	}

	// a floating window the size of the group, held where it was grabbed
	if (!m_External && source) {
		const bool canFloat = m_Whole ? m_Host.m_Layout.CanFloatGroup(*source) : m_Host.m_Layout.CanFloatPane(m_Pane);
		if (canFloat) {
			SIZE size = m_Pane->PreferredSize;
			if (HWND window = m_Host.GroupWindow(source)) {
				RECT rc;
				::GetWindowRect(window, &rc);
				size = { Width(rc), Height(rc) };
			}
			size.cx = std::max<LONG>(size.cx, m_Host.m_Metrics.MinGroupSize.cx);
			size.cy = std::max<LONG>(size.cy, m_Host.m_Metrics.MinGroupSize.cy);
			const RECT outer = m_Host.OuterRectForClient({ 0, 0, size.cx, size.cy }, m_Host.m_Dpi);
			const int left = cursor.x - std::min<LONG>(m_Grab.x, Width(outer) / 2);
			const int top = cursor.y - std::min<LONG>(m_Grab.y, m_Host.m_Metrics.CaptionHeight);
			c.Ghost = { left, top, left + Width(outer), top + Height(outer) };
		}
	}
	return c;
}

void DockDragSession::Update(POINT cursor, bool suppressDocking) {
	const DropContext context = BuildContext(cursor, suppressDocking);
	const std::vector<Guide> guides = BuildGuides(context);
	m_Target = PickTarget(context, guides, cursor);

	const HWND owner = ::GetAncestor(m_Host.m_hWnd, GA_ROOT);
	const DockTheme& theme = m_Host.m_Theme;

	// the preview goes underneath the markers
	if (!m_Preview)
		m_Preview = std::make_unique<CDockGuideWnd>(theme);
	bool previewMoved = false;
	if (m_Target.Type != DropTarget::Kind::None) {
		m_Preview->Configure(owner, m_Target.Type == DropTarget::Kind::Float ? CDockGuideWnd::Look::Ghost : CDockGuideWnd::Look::Preview, m_Target);
		previewMoved = m_Preview->Place(m_Target.Preview, false);
	}
	else {
		m_Preview->Hide();
	}

	while (m_Markers.size() < guides.size())
		m_Markers.push_back(std::make_unique<CDockGuideWnd>(theme));
	for (size_t i = 0; i < m_Markers.size(); i++) {
		if (i >= guides.size()) {
			m_Markers[i]->Hide();
			continue;
		}
		m_Markers[i]->Configure(owner, CDockGuideWnd::Look::Marker, guides[i].Target);
		const bool hot = m_Target.Type != DropTarget::Kind::None && m_Target.Type != DropTarget::Kind::Float &&
			PtInRect(&guides[i].Rect, cursor) && guides[i].Target.SameTargetAs(m_Target);
		m_Markers[i]->Place(guides[i].Rect, hot, previewMoved);
	}
}

bool DockDragSession::Commit() {
	if (Stale())
		return false;
	return ApplyDrop(m_Host.m_Layout, m_Pane, m_Whole, m_Target);
}

}
