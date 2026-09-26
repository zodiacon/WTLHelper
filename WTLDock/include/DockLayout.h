#pragma once

#include "DockNode.h"
#include <functional>
#include <string>
#include <string_view>

namespace WTLDock {

class DockLayout;

struct SplitterHit {
	DockSplit* Split;
	int Index;		// the splitter sits between Split->Children()[Index] and [Index + 1]
	RECT Rect;
};

// Called by DockLayout::Load for a pane id that is not registered. Should register the pane
// (DockLayout::AddPane) and return it, or return null to drop the pane from the layout.
using PaneFactory = std::function<DockPane*(DockLayout&, const std::wstring& id)>;

struct LoadOptions {
	// makes the panes that the file mentions and that are not registered
	PaneFactory Factory;
	// panes that are registered but that the file does not know at all (they are new since it was saved) are shown
	// at their default place instead of staying hidden
	bool ShowNewPanes{ false };
	// a file saved with an older DockLayout::AppVersion than this is refused (0: any is fine)
	int MinAppVersion{ 0 };
};

//
// The docking layout model. Pure data: it owns panes and the layout tree, knows nothing about windows.
//
//  main root ─ DockSplit ─┬─ DockGroup (tool)          docked panes, tabbed
//                         ├─ DockSplit ─┬─ DockGroup (document)   the document area (always exists)
//                         │             └─ DockGroup (tool)
//                         └─ ...
//  auto-hide bars[4]       groups collapsed to the edge of the main window
//  floats[]                floating windows, each with its own root split
//
// Every mutating operation ends by normalizing the tree (empty groups and single-child splits are removed),
// re-indexing parent/pane pointers, bumping Version() and invoking the change handler. Node pointers obtained
// before an operation must not be used after it; DockPane pointers stay valid until RemovePane.
//
class DockLayout final {
public:
	DockLayout();
	~DockLayout();
	DockLayout(const DockLayout&) = delete;
	DockLayout& operator=(const DockLayout&) = delete;

	//
	// DPI. All pixel sizes in the layout (fixed-size nodes, pane preferred/minimum sizes, auto-hide lengths) are
	// device pixels at this DPI. PaneDesc sizes are given at 96 DPI and scaled by AddPane.
	//
	int Dpi() const {
		return m_Dpi;
	}
	// Scales every pixel size of the main window from the current DPI to the new one. Floating windows have a DPI of
	// their own (they may be on another monitor) and are left alone.
	void SetDpi(int dpi);
	// A floating window moved to a monitor with another DPI: the pixel sizes of its tree are scaled to it. (Minimum
	// sizes and splitters are always worked out at the DPI of the tree they are in.)
	bool SetFloatDpi(DockFloat* window, int dpi);

	//
	// panes
	//
	// Registers a pane. It starts hidden; call Show() to place it. Returns null if the id is empty or in use.
	DockPane* AddPane(const PaneDesc& desc);
	// Hides and unregisters the pane; the pointer is invalid afterwards.
	bool RemovePane(DockPane* pane);
	DockPane* FindPane(std::wstring_view id) const;
	const std::vector<std::unique_ptr<DockPane>>& Panes() const {
		return m_Panes;
	}

	//
	// structure (read only)
	//
	DockSplit& Root() const {
		return *m_Root;
	}
	// the group that anchors the document area
	DockGroup* PrimaryDocumentGroup() const;
	// the document groups of the main window, in the order of the tree (left to right, top to bottom)
	std::vector<DockGroup*> DocumentGroups() const;
	// Where Show() puts a document: the group of the document that was active last (in the main window or in a
	// floating one), or else the primary group.
	DockGroup* ActiveDocumentGroup() const;
	// Remembers that a pane has been made active by the user (the layout can't tell: it is what has the focus).
	// Only documents matter; nothing changes visibly.
	void NoteActive(const DockPane* pane);
	const std::vector<std::unique_ptr<DockGroup>>& AutoHideGroups(DockSide side) const {
		return m_AutoHide[(int)side];
	}
	const std::vector<std::unique_ptr<DockFloat>>& Floats() const {
		return m_Floats;
	}
	void ForEachGroup(const std::function<void(DockGroup&)>& fn) const;

	//
	// operations. All return false, leaving the layout untouched, if the request is not allowed.
	//
	// Places a hidden pane where it was last (or at its default side); activates a placed pane.
	bool Show(DockPane* pane);
	// Removes the pane from view, remembering where it was.
	bool Hide(DockPane* pane);
	// Makes the pane the active tab of its group.
	bool Activate(DockPane* pane);
	// Tabs cannot be moved across the boundary between the pinned and the unpinned ones: the pane goes as far as it can.
	bool ReorderTab(DockPane* pane, int index);
	// Pins a document (its tab goes to the end of the pinned tabs of its group) or unpins it (to the start of the
	// others). Documents only; a pane that is not placed remembers it for when it is.
	bool SetPinned(DockPane* pane, bool pinned);

	// floating (rect is in screen coordinates). A document floats into a window with a document group of its own that
	// other documents can be dropped on; a whole document group can float as long as another one stays in the main window.
	bool CanFloatPane(const DockPane* pane) const;
	bool CanFloatGroup(const DockGroup& group) const;
	bool Float(DockPane* pane, const RECT& rect);
	bool FloatGroup(DockGroup* group, const RECT& rect);
	bool SetFloatRect(DockFloat* window, const RECT& rect);

	// docking. Tab adds the pane to the target group (at tabIndex, or last); the other positions split the target.
	bool CanDockTo(const DockPane* pane, const DockGroup* target, DockPosition pos) const;
	bool DockTo(DockPane* pane, DockGroup* target, DockPosition pos, int tabIndex = -1);
	// docks at the outer edge of the main window, or of a floating window
	bool DockToEdge(DockPane* pane, DockSide side, DockFloat* window = nullptr);
	// How much room a group that floats takes when it is docked at a side of the main window or beside 'target': as
	// much as it had in its window (in the main window's pixels: a width for left and right, a height for top and
	// bottom), but never so much that 'target' is left below its minimum. 0 if the group is not in a floating window
	// (docking then shares the space as it always did).
	int LengthWhenDocked(const DockGroup& group, DockSide side) const;
	int LengthBeside(const DockGroup& moving, const DockGroup& target, DockPosition pos) const;
	// the same for a whole group (all its tabs move together)
	bool CanDockToEdge(const DockPane* pane) const;
	bool CanMoveGroupTo(const DockGroup* group, const DockGroup* target, DockPosition pos) const;
	bool CanMoveGroupToEdge(const DockGroup* group) const;
	bool MoveGroupTo(DockGroup* group, DockGroup* target, DockPosition pos);
	bool MoveGroupToEdge(DockGroup* group, DockSide side, DockFloat* window = nullptr);

	// auto-hide
	bool AutoHide(DockGroup* group);
	// Docks an auto-hidden group again where it was (see DockAnchor), else at the edge of its side.
	bool Unhide(DockGroup* group);
	// The same for a floating group: it goes back to its old tab group or its old place among the others, else to the
	// edge of the side it was last docked on.
	bool RedockGroup(DockGroup* group);

	// Moves the splitter between children[index] and children[index + 1] by delta pixels.
	// Uses the rectangles of the last Arrange, and clamps to the children's minimum sizes.
	bool ResizeSplitter(DockSplit* split, int index, int delta);

	//
	// geometry
	//
	const LayoutMetrics& Metrics() const {
		return m_Metrics;
	}
	void SetMetrics(const LayoutMetrics& metrics) {
		m_Metrics = metrics;
	}
	// Assigns Rect to every node of the main tree and to the auto-hide bars (which take space from the client area).
	void Arrange(const RECT& client);
	// Arranges a floating window's tree inside its client area.
	void Arrange(DockFloat& window, const RECT& client);
	const RECT& AutoHideBarRect(DockSide side) const {
		return m_BarRects[(int)side];
	}
	int MinLength(const DockNode& node, Axis axis) const;
	// the DPI the pixel sizes of the tree that holds the node are at
	int NodeDpi(const DockNode& node) const;

	static std::vector<SplitterHit> Splitters(DockSplit& root);
	static DockGroup* GroupAt(DockSplit& root, POINT pt);

	//
	// persistence and diagnostics
	//
	std::string Save() const;
	// Replaces the layout with the saved one. Panes are matched by id; ids that are not registered (and not created
	// by the factory) are dropped, registered panes missing from the text become hidden (or, with ShowNewPanes, are
	// shown if they are new). On failure the layout is left unchanged (except for panes the factory registered).
	bool Load(std::string_view text, const LoadOptions& options = {}, std::wstring* error = nullptr);

	// An application's own version of its pane arrangement: saved with the layout, so that a newer application can
	// refuse (LoadOptions::MinAppVersion) layouts that its panes no longer fit.
	int AppVersion() const {
		return m_AppVersion;
	}
	void SetAppVersion(int version) {
		m_AppVersion = version;
	}

	// A one-line-per-area description of the layout, e.g. "main: H(T[Tools]@250 D[a.cpp,b.cpp] T[Output]@200)".
	std::wstring Dump() const;
	// Checks the structural invariants; for tests and debug builds.
	bool Validate(std::wstring* error = nullptr) const;

	uint64_t Version() const {
		return m_Version;
	}
	void SetChangeHandler(std::function<void()> handler) {
		m_OnChanged = std::move(handler);
	}

private:
	friend struct DockSerializer;

	bool Owns(const DockPane* pane) const;
	void Commit();
	void Normalize();
	void NormalizeRoot(DockSplit& root, DockGroup* keep);
	void NormalizeSplit(DockSplit& split, DockGroup* keep);
	void MergeIntoParent(DockSplit& parent, size_t index);
	void Reindex();
	static void NormalizePins(DockGroup& group);
	static std::optional<DockSide> ComputeSide(const DockGroup& group, const DockGroup* primary);

	void RecordPlacement(DockPane* pane, bool wholeGroup = false);
	void RecordAnchor(DockPane* pane, bool wholeGroup);
	// what an anchor names: a group to join as a tab, or a part of the main layout to go beside (null: nothing usable)
	DockNode* ResolveAnchor(const DockAnchor& anchor, const DockGroup* exclude, DockGroup*& tabTarget) const;
	int CapBeside(const DockNode& target, DockPosition pos, int length) const;
	bool RestoreDocked(DockPane* pane);
	bool UnhideAtEdge(DockGroup* group);
	void DetachPane(DockPane* pane);
	std::unique_ptr<DockGroup> NewGroup(DockPane* pane) const;
	std::unique_ptr<DockGroup> ReleaseGroup(DockGroup* group);
	void InsertBeside(DockNode* target, std::unique_ptr<DockNode> node, DockPosition pos, int length = 0);
	void InsertAtEdge(DockSplit& root, std::unique_ptr<DockNode> node, DockSide side, int length);
	void AddFloat(std::unique_ptr<DockGroup> group, const RECT& rect);
	int DefaultLength(const DockGroup& group, DockSide side) const;
	void ArrangeSplit(DockSplit& split, int dpi);
	int MinLengthAt(const DockNode& node, Axis axis, int dpi) const;
	int ScaleTo(int value, int dpi) const {
		return dpi == m_Dpi ? value : ::MulDiv(value, dpi, m_Dpi);
	}

	std::unique_ptr<DockSplit> m_Root;
	std::vector<std::unique_ptr<DockGroup>> m_AutoHide[SideCount];
	std::vector<std::unique_ptr<DockFloat>> m_Floats;
	std::vector<std::unique_ptr<DockPane>> m_Panes;
	RECT m_BarRects[SideCount]{};
	LayoutMetrics m_Metrics;
	uint64_t m_Version{};
	int m_NextFloatId{};
	int m_Dpi{ 96 };
	int m_AppVersion{};
	const DockPane* m_ActiveDocument{};
	std::function<void()> m_OnChanged;
};

}
