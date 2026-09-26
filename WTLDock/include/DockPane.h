#pragma once

#include "DockTypes.h"
#include <string>

namespace WTLDock {

class DockGroup;
class DockFloat;
class DockLayout;
struct DockSerializer;

struct PaneDesc {
	std::wstring Id;		// stable and unique; used to persist the layout
	std::wstring Title;
	std::wstring Tooltip;	// shown when the mouse rests on its tab or caption (a document's full path, say); empty: none
	HWND hWnd{};			// content window (not owned)
	HICON Icon{};			// not owned
	PaneKind Kind{ PaneKind::Tool };
	PaneCaps Caps{ PaneCaps::All };
	DockSide DefaultSide{ DockSide::Left };
	SIZE PreferredSize{ 250, 250 };	// at 96 DPI
	SIZE MinSize{ 80, 60 };			// at 96 DPI
};

// A dockable pane. Created by DockLayout::AddPane and owned by the layout; the pointer stays valid until RemovePane.
class DockPane final {
public:
	DockPane(const DockPane&) = delete;
	DockPane& operator=(const DockPane&) = delete;

	const std::wstring& Id() const {
		return m_Id;
	}
	PaneKind Kind() const {
		return m_Kind;
	}

	std::wstring Title;
	std::wstring Tooltip;
	// Unsaved changes: the tab shows a dot (in place of the close button), the caption and the window switcher a mark
	// after the title. Set it and call CDockHost::RefreshPane (nothing else in the layout changes).
	bool Modified{};
	HWND hWnd{};
	HICON Icon{};
	PaneCaps Caps{ PaneCaps::All };
	SIZE PreferredSize{ 250, 250 };
	SIZE MinSize{ 80, 60 };

	// placement, maintained by DockLayout
	PaneState State() const {
		return m_State;
	}
	DockGroup* Group() const {
		return m_Group;
	}

	// where the pane goes on Show() after it has been hidden
	PaneState LastState() const {
		return m_LastState;
	}
	DockSide LastSide() const {
		return m_LastSide;
	}
	const RECT& LastFloatRect() const {
		return m_LastFloatRect;
	}

private:
	friend class DockLayout;
	friend struct DockSerializer;

	explicit DockPane(const PaneDesc& desc) :
		Title(desc.Title), Tooltip(desc.Tooltip), hWnd(desc.hWnd), Icon(desc.Icon), Caps(desc.Caps),
		PreferredSize(desc.PreferredSize), MinSize(desc.MinSize),
		m_Id(desc.Id), m_Kind(desc.Kind), m_LastSide(desc.DefaultSide),
		m_LastState(desc.Kind == PaneKind::Document ? PaneState::Document : PaneState::Docked) {
	}

	std::wstring m_Id;
	PaneKind m_Kind;
	PaneState m_State{ PaneState::Hidden };
	DockGroup* m_Group{};
	DockSide m_LastSide;
	PaneState m_LastState;
	RECT m_LastFloatRect{};
};

}
