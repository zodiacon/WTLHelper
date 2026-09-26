#pragma once

#include "DockPane.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace WTLDock {

class DockSplit;
class DockGroup;

// A node of the layout tree: either a split (children arranged along an axis) or a group (a set of tabbed panes).
// Nodes are only modified through DockLayout. Pointers to nodes are invalidated by any layout operation
// (nodes get created, merged and removed); pointers to panes are not.
class DockNode {
public:
	enum class Type { Split, Group };

	virtual ~DockNode() = default;
	DockNode(const DockNode&) = delete;
	DockNode& operator=(const DockNode&) = delete;

	Type GetType() const {
		return m_Type;
	}
	bool IsSplit() const {
		return m_Type == Type::Split;
	}
	bool IsGroup() const {
		return m_Type == Type::Group;
	}
	inline DockSplit* AsSplit();
	inline const DockSplit* AsSplit() const;
	inline DockGroup* AsGroup();
	inline const DockGroup* AsGroup() const;

	DockSplit* Parent() const {
		return m_Parent;
	}

	SizeSpec Size;	// within the parent split, along its axis
	RECT Rect{};	// result of the last DockLayout::Arrange

protected:
	explicit DockNode(Type type) : m_Type(type) {
	}

private:
	friend class DockLayout;
	friend struct DockSerializer;

	Type m_Type;
	DockSplit* m_Parent{};
};

class DockSplit final : public DockNode {
public:
	explicit DockSplit(Axis axis) : DockNode(Type::Split), m_Axis(axis) {
	}

	Axis GetAxis() const {
		return m_Axis;
	}
	const std::vector<std::unique_ptr<DockNode>>& Children() const {
		return m_Children;
	}
	int IndexOf(const DockNode* node) const {
		for (size_t i = 0; i < m_Children.size(); i++)
			if (m_Children[i].get() == node)
				return (int)i;
		return -1;
	}

private:
	friend class DockLayout;
	friend struct DockSerializer;

	Axis m_Axis;
	std::vector<std::unique_ptr<DockNode>> m_Children;
};

enum class GroupLocation { Main, AutoHide, Float };

class DockGroup final : public DockNode {
public:
	explicit DockGroup(PaneKind kind) : DockNode(Type::Group), TabsAtBottom(kind == PaneKind::Tool), m_Kind(kind) {
	}

	PaneKind Kind() const {
		return m_Kind;
	}
	bool IsDocument() const {
		return m_Kind == PaneKind::Document;
	}
	const std::vector<DockPane*>& Panes() const {
		return m_Panes;
	}
	int ActiveIndex() const {
		return m_Active;
	}
	DockPane* ActivePane() const {
		return m_Active >= 0 && m_Active < (int)m_Panes.size() ? m_Panes[m_Active] : nullptr;
	}

	GroupLocation Location() const {
		return m_Where;
	}
	// the side of the document area a docked tool group is on, or the side of the auto-hide bar it is in
	std::optional<DockSide> Side() const {
		return m_Side;
	}
	DockFloat* Float() const {
		return m_Float;
	}

	bool TabsAtBottom;
	int AutoHideLength{};	// length along the bar's axis when the group slides out / is restored

private:
	friend class DockLayout;
	friend struct DockSerializer;

	void AddPane(DockPane* pane, int index) {
		if (index < 0 || index > (int)m_Panes.size())
			index = (int)m_Panes.size();
		m_Panes.insert(m_Panes.begin() + index, pane);
		m_Active = index;
	}

	void RemovePane(DockPane* pane) {
		auto it = std::find(m_Panes.begin(), m_Panes.end(), pane);
		if (it == m_Panes.end())
			return;
		int index = (int)(it - m_Panes.begin());
		m_Panes.erase(it);
		if (m_Panes.empty())
			m_Active = 0;
		else if (index < m_Active)
			m_Active--;
		else if (m_Active >= (int)m_Panes.size())
			m_Active = (int)m_Panes.size() - 1;
	}

	PaneKind m_Kind;
	std::vector<DockPane*> m_Panes;
	int m_Active{};
	GroupLocation m_Where{ GroupLocation::Main };
	std::optional<DockSide> m_Side;
	DockFloat* m_Float{};
};

// A floating window: its own layout tree, positioned in screen coordinates.
class DockFloat final {
public:
	DockFloat(const DockFloat&) = delete;
	DockFloat& operator=(const DockFloat&) = delete;

	int Id() const {
		return m_Id;
	}
	const RECT& Rect() const {
		return m_Rect;
	}
	DockSplit& Root() const {
		return *m_Root;
	}
	// The DPI of the monitor the window is on: the pixel sizes in its tree are at this DPI (DockLayout::SetFloatDpi).
	int Dpi() const {
		return m_Dpi;
	}

private:
	friend class DockLayout;
	friend struct DockSerializer;

	DockFloat(int id, const RECT& rect, int dpi) : m_Id(id), m_Rect(rect), m_Dpi(dpi), m_Root(std::make_unique<DockSplit>(Axis::Horizontal)) {
	}

	int m_Id;
	RECT m_Rect;
	int m_Dpi;
	std::unique_ptr<DockSplit> m_Root;
};

inline DockSplit* DockNode::AsSplit() {
	return IsSplit() ? static_cast<DockSplit*>(this) : nullptr;
}
inline const DockSplit* DockNode::AsSplit() const {
	return IsSplit() ? static_cast<const DockSplit*>(this) : nullptr;
}
inline DockGroup* DockNode::AsGroup() {
	return IsGroup() ? static_cast<DockGroup*>(this) : nullptr;
}
inline const DockGroup* DockNode::AsGroup() const {
	return IsGroup() ? static_cast<const DockGroup*>(this) : nullptr;
}

}
