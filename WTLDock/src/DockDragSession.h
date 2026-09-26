#pragma once

#include "DockHost.h"
#include "DockDrop.h"
#include "DockGuideWnd.h"

#include <memory>

namespace WTLDock {

// One drag of a pane or group over the docking windows: shows the guides and the preview, and on release carries out
// the drop. Created and owned by CDockHost; not for direct use.
class DockDragSession {
public:
	// 'external': the dragged group has a window of its own that the user is moving (a floating window's title bar),
	// so there is no ghost and that window is not a drop target
	DockDragSession(CDockHost& host, DockPane* pane, bool wholeGroup, POINT start, bool external);
	~DockDragSession();

	void Update(POINT cursor, bool suppressDocking);
	// Carries out the drop on the target of the last Update; true if the layout changed.
	bool Commit();
	void HideAll();
	// the layout changed behind the session's back: its targets are gone
	bool Stale() const {
		return m_Host.m_Layout.Version() != m_Version;
	}

	const DropTarget& Target() const {
		return m_Target;
	}
	int VisibleMarkers() const;

private:
	DropContext BuildContext(POINT cursor, bool suppressDocking) const;

	CDockHost& m_Host;
	DockPane* m_Pane;
	bool m_Whole;
	bool m_External;
	uint64_t m_Version;
	POINT m_Grab{};					// where in the group's window the drag started
	HWND m_IgnoredFrame{};
	DropTarget m_Target;
	std::vector<std::unique_ptr<CDockGuideWnd>> m_Markers;
	std::unique_ptr<CDockGuideWnd> m_Preview;
};

}
