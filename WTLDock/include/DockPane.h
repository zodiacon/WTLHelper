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
	// A preview document: its tab is drawn in italics and the next one that is opened as a preview replaces it
	// (CDockHost::ShowPreview); editing it, pinning it or double clicking its tab makes it a normal one. Not saved.
	bool Preview{};
	// A stripe in this colour under the tab (a project's colour, say); CLR_INVALID: none.
	COLORREF TabColor{ CLR_INVALID };
	HWND hWnd{};
	HICON Icon{};
	PaneCaps Caps{ PaneCaps::All };
	SIZE PreferredSize{ 250, 250 };
	SIZE MinSize{ 80, 60 };

	// A pinned document keeps its tab at the left of its group, before all the unpinned ones (DockLayout::SetPinned).
	// Saved with the layout.
	bool Pinned() const {
		return m_Pinned;
	}

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
	bool m_Pinned{};
	PaneState m_State{ PaneState::Hidden };
	DockGroup* m_Group{};
	DockSide m_LastSide;
	PaneState m_LastState;
	RECT m_LastFloatRect{};
};

}
