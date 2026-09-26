#include "DockLayout.h"
#include "DockSerializer.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace WTLDock {

namespace {

constexpr int DefaultToolLength = 250;

template<typename F>
void VisitGroups(DockNode& node, F&& fn) {
	if (auto split = node.AsSplit()) {
		for (auto& child : split->Children())
			VisitGroups(*child, fn);
	}
	else {
		fn(*node.AsGroup());
	}
}

bool IsAncestor(const DockNode* ancestor, const DockNode* node) {
	for (; node; node = node->Parent())
		if (node == ancestor)
			return true;
	return false;
}

}

DockLayout::DockLayout() {
	m_Root = std::make_unique<DockSplit>(Axis::Horizontal);
	auto docs = std::make_unique<DockGroup>(PaneKind::Document);
	docs->Size = SizeSpec::Star();
	m_Root->m_Children.push_back(std::move(docs));
	Reindex();
}

DockLayout::~DockLayout() = default;

//
// panes
//

DockPane* DockLayout::AddPane(const PaneDesc& desc) {
	if (desc.Id.empty() || FindPane(desc.Id))
		return nullptr;
	m_Panes.push_back(std::unique_ptr<DockPane>(new DockPane(desc)));
	auto pane = m_Panes.back().get();
	if (m_Dpi != 96) {
		pane->PreferredSize = { ::MulDiv(desc.PreferredSize.cx, m_Dpi, 96), ::MulDiv(desc.PreferredSize.cy, m_Dpi, 96) };
		pane->MinSize = { ::MulDiv(desc.MinSize.cx, m_Dpi, 96), ::MulDiv(desc.MinSize.cy, m_Dpi, 96) };
	}
	return pane;
}

void DockLayout::SetDpi(int dpi) {
	if (dpi <= 0 || dpi == m_Dpi)
		return;
	const int from = m_Dpi;
	auto scale = [&](int v) { return ::MulDiv(v, dpi, from); };
	auto scaleNodes = [&](DockSplit& root) {
		std::function<void(DockNode&)> visit = [&](DockNode& n) {
			if (!n.Size.IsStar())
				n.Size.Value = std::max(1.0, std::round(n.Size.Value * dpi / from));
			if (auto split = n.AsSplit())
				for (auto& c : split->Children())
					visit(*c);
		};
		visit(root);
	};
	scaleNodes(*m_Root);
	ForEachGroup([&](DockGroup& g) {
		if (g.m_Where != GroupLocation::Float)
			g.AutoHideLength = scale(g.AutoHideLength);
		});
	for (auto& p : m_Panes) {
		p->PreferredSize = { scale(p->PreferredSize.cx), scale(p->PreferredSize.cy) };
		p->MinSize = { scale(p->MinSize.cx), scale(p->MinSize.cy) };
	}
	m_Dpi = dpi;
	Commit();
}

bool DockLayout::SetFloatDpi(DockFloat* window, int dpi) {
	if (dpi < 48 || dpi > 960 || std::none_of(m_Floats.begin(), m_Floats.end(), [&](auto& f) { return f.get() == window; }))
		return false;
	if (window->m_Dpi == dpi)
		return true;
	const int from = window->m_Dpi;
	std::function<void(DockNode&)> visit = [&](DockNode& n) {
		if (!n.Size.IsStar())
			n.Size.Value = std::max(1.0, std::round(n.Size.Value * dpi / from));
		if (auto split = n.AsSplit())
			for (auto& c : split->Children())
				visit(*c);
		};
	visit(*window->m_Root);
	window->m_Dpi = dpi;
	Commit();
	return true;
}

bool DockLayout::RemovePane(DockPane* pane) {
	if (!Owns(pane))
		return false;
	if (pane->m_Group) {
		DetachPane(pane);
		Commit();
	}
	if (m_ActiveDocument == pane)
		m_ActiveDocument = nullptr;
	m_Panes.erase(std::find_if(m_Panes.begin(), m_Panes.end(), [&](auto& p) { return p.get() == pane; }));
	return true;
}

DockPane* DockLayout::FindPane(std::wstring_view id) const {
	for (auto& p : m_Panes)
		if (p->m_Id == id)
			return p.get();
	return nullptr;
}

bool DockLayout::Owns(const DockPane* pane) const {
	return pane && std::any_of(m_Panes.begin(), m_Panes.end(), [&](auto& p) { return p.get() == pane; });
}

//
// structure
//

DockGroup* DockLayout::PrimaryDocumentGroup() const {
	DockGroup* first = nullptr;
	DockGroup* nonEmpty = nullptr;
	VisitGroups(*m_Root, [&](DockGroup& g) {
		if (!g.IsDocument())
			return;
		if (!first)
			first = &g;
		if (!nonEmpty && !g.m_Panes.empty())
			nonEmpty = &g;
		});
	return nonEmpty ? nonEmpty : first;
}

std::vector<DockGroup*> DockLayout::DocumentGroups() const {
	std::vector<DockGroup*> groups;
	VisitGroups(*m_Root, [&](DockGroup& g) {
		if (g.IsDocument())
			groups.push_back(&g);
		});
	return groups;
}

DockGroup* DockLayout::ActiveDocumentGroup() const {
	if (m_ActiveDocument && m_ActiveDocument->m_Group && m_ActiveDocument->m_Group->m_Where != GroupLocation::AutoHide)
		return m_ActiveDocument->m_Group;
	return PrimaryDocumentGroup();
}

void DockLayout::NoteActive(const DockPane* pane) {
	if (pane && pane->Kind() == PaneKind::Document && Owns(pane) && pane->m_Group)
		m_ActiveDocument = pane;
}

void DockLayout::ForEachGroup(const std::function<void(DockGroup&)>& fn) const {
	VisitGroups(*m_Root, fn);
	for (auto& bar : m_AutoHide)
		for (auto& g : bar)
			fn(*g);
	for (auto& f : m_Floats)
		VisitGroups(*f->m_Root, fn);
}

//
// normalization and indexing
//

void DockLayout::Commit() {
	Normalize();
	Reindex();
	m_Version++;
	if (m_OnChanged)
		m_OnChanged();
}

void DockLayout::Normalize() {
	NormalizeRoot(*m_Root, PrimaryDocumentGroup());
	for (auto& bar : m_AutoHide)
		std::erase_if(bar, [](auto& g) { return g->m_Panes.empty(); });
	for (auto& f : m_Floats)
		NormalizeRoot(*f->m_Root, nullptr);
	std::erase_if(m_Floats, [](auto& f) { return f->m_Root->m_Children.empty(); });
}

void DockLayout::NormalizeRoot(DockSplit& root, DockGroup* keep) {
	NormalizeSplit(root, keep);
	// a root with a single split child takes over that child
	while (root.m_Children.size() == 1 && root.m_Children[0]->IsSplit()) {
		auto child = std::move(root.m_Children[0]);
		auto* inner = child->AsSplit();
		root.m_Axis = inner->m_Axis;
		auto kids = std::move(inner->m_Children);
		root.m_Children = std::move(kids);
		for (auto& k : root.m_Children)
			k->m_Parent = &root;
	}
}

void DockLayout::NormalizeSplit(DockSplit& split, DockGroup* keep) {
	auto& kids = split.m_Children;
	for (size_t i = 0; i < kids.size();) {
		if (auto inner = kids[i]->AsSplit()) {
			NormalizeSplit(*inner, keep);
			if (inner->m_Children.empty()) {
				kids.erase(kids.begin() + i);
				continue;
			}
			if (inner->m_Children.size() == 1) {
				// a split with a single child is replaced by that child, which inherits its space
				auto only = std::move(inner->m_Children[0]);
				only->Size = inner->Size;
				only->m_Parent = &split;
				kids[i] = std::move(only);
			}
		}
		else if (kids[i]->AsGroup()->m_Panes.empty() && kids[i].get() != keep) {
			kids.erase(kids.begin() + i);
			continue;
		}
		i++;
	}

	// every split needs a child that absorbs size changes
	int stars = 0;
	for (auto& k : kids) {
		if (k->Size.IsStar() && !(k->Size.Value > 0))
			k->Size.Value = 1;
		stars += k->Size.IsStar();
	}
	if (stars == 0 && !kids.empty())
		kids.back()->Size = SizeSpec::Star();
	if (stars == 1) {
		// a lone star weight is meaningless; don't let leftovers of earlier splits accumulate
		for (auto& k : kids)
			if (k->Size.IsStar())
				k->Size.Value = 1;
	}
}

std::optional<DockSide> DockLayout::ComputeSide(const DockGroup& group, const DockGroup* primary) {
	if (!primary)
		return {};
	const DockNode* node = &group;
	while (auto parent = node->Parent()) {
		if (IsAncestor(parent, primary)) {
			const DockNode* other = primary;
			while (other->Parent() != parent)
				other = other->Parent();
			bool before = parent->IndexOf(node) < parent->IndexOf(other);
			if (parent->GetAxis() == Axis::Horizontal)
				return before ? DockSide::Left : DockSide::Right;
			return before ? DockSide::Top : DockSide::Bottom;
		}
		node = parent;
	}
	return {};
}

void DockLayout::Reindex() {
	for (auto& p : m_Panes) {
		p->m_Group = nullptr;
		p->m_State = PaneState::Hidden;
	}

	std::function<void(DockSplit&)> setParents = [&](DockSplit& s) {
		for (auto& c : s.m_Children) {
			c->m_Parent = &s;
			if (auto inner = c->AsSplit())
				setParents(*inner);
		}
		};
	m_Root->m_Parent = nullptr;
	setParents(*m_Root);
	for (auto& f : m_Floats) {
		f->m_Root->m_Parent = nullptr;
		setParents(*f->m_Root);
	}

	auto place = [](DockGroup& g, GroupLocation where, std::optional<DockSide> side, DockFloat* fl) {
		g.m_Where = where;
		g.m_Side = side;
		g.m_Float = fl;
		PaneState state = where == GroupLocation::AutoHide ? PaneState::AutoHide :
			where == GroupLocation::Float ? PaneState::Floating :
			g.IsDocument() ? PaneState::Document : PaneState::Docked;
		for (auto p : g.m_Panes) {
			p->m_Group = &g;
			p->m_State = state;
		}
		};

	auto primary = PrimaryDocumentGroup();
	VisitGroups(*m_Root, [&](DockGroup& g) {
		place(g, GroupLocation::Main, g.IsDocument() ? std::nullopt : ComputeSide(g, primary), nullptr);
		});
	for (int side = 0; side < SideCount; side++) {
		for (auto& g : m_AutoHide[side]) {
			g->m_Parent = nullptr;
			place(*g, GroupLocation::AutoHide, (DockSide)side, nullptr);
		}
	}
	for (auto& f : m_Floats)
		VisitGroups(*f->m_Root, [&](DockGroup& g) { place(g, GroupLocation::Float, std::nullopt, f.get()); });
}

//
// placement helpers
//

void DockLayout::RecordPlacement(DockPane* pane) {
	auto g = pane->m_Group;
	if (!g)
		return;
	switch (pane->m_State) {
		case PaneState::Document:
			pane->m_LastState = PaneState::Document;
			break;
		case PaneState::Docked:
			pane->m_LastState = PaneState::Docked;
			if (g->m_Side)
				pane->m_LastSide = *g->m_Side;
			if (Width(g->Rect) > 0 && Height(g->Rect) > 0)
				pane->PreferredSize = { Width(g->Rect), Height(g->Rect) };
			break;
		case PaneState::AutoHide:
			pane->m_LastState = PaneState::AutoHide;
			pane->m_LastSide = *g->m_Side;
			if (g->AutoHideLength > 0) {
				if (AxisOf(pane->m_LastSide) == Axis::Horizontal)
					pane->PreferredSize.cx = g->AutoHideLength;
				else
					pane->PreferredSize.cy = g->AutoHideLength;
			}
			break;
		case PaneState::Floating:
			// a closed document comes back among the documents; where it floated is only remembered for floating it again
			if (pane->Kind() == PaneKind::Tool)
				pane->m_LastState = PaneState::Floating;
			pane->m_LastFloatRect = g->m_Float->Rect();
			break;
		default:
			break;
	}
}

void DockLayout::DetachPane(DockPane* pane) {
	RecordPlacement(pane);
	pane->m_Group->RemovePane(pane);
	pane->m_Group = nullptr;
}

std::unique_ptr<DockGroup> DockLayout::NewGroup(DockPane* pane) const {
	auto g = std::make_unique<DockGroup>(pane->Kind());
	g->AddPane(pane, 0);
	g->Size = SizeSpec::Star();
	return g;
}

std::unique_ptr<DockGroup> DockLayout::ReleaseGroup(DockGroup* group) {
	if (group->m_Where == GroupLocation::AutoHide) {
		auto& bar = m_AutoHide[(int)*group->m_Side];
		auto it = std::find_if(bar.begin(), bar.end(), [&](auto& g) { return g.get() == group; });
		auto result = std::move(*it);
		bar.erase(it);
		return result;
	}
	auto parent = group->m_Parent;
	int index = parent->IndexOf(group);
	std::unique_ptr<DockNode> node = std::move(parent->m_Children[index]);
	parent->m_Children.erase(parent->m_Children.begin() + index);
	node->m_Parent = nullptr;
	return std::unique_ptr<DockGroup>(static_cast<DockGroup*>(node.release()));
}

int DockLayout::DefaultLength(const DockGroup& group, DockSide side) const {
	Axis axis = AxisOf(side);
	int length = Length(group.Rect, axis);
	// a group that comes from a floating window was measured at the DPI of that window
	if (length > 0 && group.m_Where == GroupLocation::Float && group.m_Float && group.m_Float->m_Dpi != m_Dpi)
		length = std::max(1, ::MulDiv(length, m_Dpi, group.m_Float->m_Dpi));
	if (length <= 0 && group.AutoHideLength > 0)
		length = group.AutoHideLength;
	if (length <= 0 && group.ActivePane())
		length = Along(group.ActivePane()->PreferredSize, axis);
	return length > 0 ? length : DefaultToolLength;
}

int DockLayout::LengthWhenDocked(const DockGroup& group, DockSide side) const {
	if (group.m_Where != GroupLocation::Float || group.m_Panes.empty())
		return 0;
	return DefaultLength(group, side);
}

int DockLayout::LengthBeside(const DockGroup& moving, const DockGroup& target, DockPosition pos) const {
	if (pos == DockPosition::Tab || target.m_Where == GroupLocation::AutoHide)
		return 0;
	// (moving within one floating window is no coming from elsewhere)
	if (moving.m_Where == GroupLocation::Float && target.m_Where == GroupLocation::Float && moving.m_Float == target.m_Float)
		return 0;
	const DockSide side = ToSide(pos);
	int length = LengthWhenDocked(moving, side);
	if (length <= 0)
		return 0;
	// the target keeps at least its minimum
	const Axis axis = AxisOf(side);
	const int room = Length(target.Rect, axis);
	if (room > 0) {
		const int dpi = NodeDpi(target);
		const int keep = MinLengthAt(target, axis, dpi) + ScaleTo(m_Metrics.SplitterThickness, dpi);
		length = std::min(length, std::max(1, room - keep));
	}
	return std::max(1, length);
}

void DockLayout::InsertBeside(DockNode* target, std::unique_ptr<DockNode> node, DockPosition pos, int length) {
	const DockSide side = ToSide(pos);
	const Axis axis = AxisOf(side);
	auto parent = target->m_Parent;
	const int index = parent->IndexOf(target);

	if (parent->m_Axis == axis) {
		// share the target's space with the new node
		if (length > 0) {
			// the node comes with the size it had; the target gives it up
			node->Size = SizeSpec::Px(length);
			if (!target->Size.IsStar()) {
				const double current = Length(target->Rect, axis) > 0 ? Length(target->Rect, axis) : target->Size.Value;
				target->Size = SizeSpec::Px(std::max(1.0, current - m_Metrics.SplitterThickness - length));
			}
		}
		else if (target->Size.IsStar()) {
			double weight = target->Size.Value / 2;
			target->Size = node->Size = SizeSpec::Star(weight);
		}
		else {
			double current = Length(target->Rect, axis) > 0 ? Length(target->Rect, axis) : target->Size.Value;
			int newLength = std::max(1, (int)((current - m_Metrics.SplitterThickness) / 2));
			node->Size = SizeSpec::Px(newLength);
			target->Size = SizeSpec::Px(std::max(1.0, current - m_Metrics.SplitterThickness - newLength));
		}
		node->m_Parent = parent;
		parent->m_Children.insert(parent->m_Children.begin() + (IsBefore(side) ? index : index + 1), std::move(node));
		return;
	}

	// different axis: replace the target with a new split holding the target and the new node
	auto wrapper = std::make_unique<DockSplit>(axis);
	wrapper->Size = target->Size;
	wrapper->m_Parent = parent;
	auto targetOwner = std::move(parent->m_Children[index]);
	targetOwner->Size = node->Size = SizeSpec::Star();
	if (length > 0)
		node->Size = SizeSpec::Px(length);
	targetOwner->m_Parent = node->m_Parent = wrapper.get();
	if (IsBefore(side)) {
		wrapper->m_Children.push_back(std::move(node));
		wrapper->m_Children.push_back(std::move(targetOwner));
	}
	else {
		wrapper->m_Children.push_back(std::move(targetOwner));
		wrapper->m_Children.push_back(std::move(node));
	}
	parent->m_Children[index] = std::move(wrapper);
}

void DockLayout::InsertAtEdge(DockSplit& root, std::unique_ptr<DockNode> node, DockSide side, int length) {
	const Axis axis = AxisOf(side);
	if (root.m_Axis != axis) {
		if (root.m_Children.size() <= 1) {
			root.m_Axis = axis;
		}
		else {
			auto inner = std::make_unique<DockSplit>(root.m_Axis);
			inner->m_Children = std::move(root.m_Children);
			root.m_Children.clear();
			for (auto& c : inner->m_Children)
				c->m_Parent = inner.get();
			inner->Size = SizeSpec::Star();
			inner->m_Parent = &root;
			root.m_Axis = axis;
			root.m_Children.push_back(std::move(inner));
		}
	}
	node->Size = SizeSpec::Px(length);
	node->m_Parent = &root;
	if (IsBefore(side))
		root.m_Children.insert(root.m_Children.begin(), std::move(node));
	else
		root.m_Children.push_back(std::move(node));
}

void DockLayout::AddFloat(std::unique_ptr<DockGroup> group, const RECT& rect) {
	std::unique_ptr<DockFloat> window(new DockFloat(++m_NextFloatId, rect, m_Dpi));
	group->Size = SizeSpec::Star();
	group->m_Parent = window->m_Root.get();
	window->m_Root->m_Children.push_back(std::move(group));
	m_Floats.push_back(std::move(window));
}

//
// operations
//

bool DockLayout::Show(DockPane* pane) {
	if (!Owns(pane))
		return false;
	if (pane->m_Group)
		return Activate(pane);

	switch (pane->m_LastState) {
		case PaneState::Document:
			if (auto docs = ActiveDocumentGroup())
				return DockTo(pane, docs, DockPosition::Tab);
			return false;

		case PaneState::AutoHide:
			if (Has(pane->Caps, PaneCaps::CanAutoHide)) {
				auto g = NewGroup(pane);
				g->AutoHideLength = Along(pane->PreferredSize, AxisOf(pane->m_LastSide));
				if (g->AutoHideLength <= 0)
					g->AutoHideLength = DefaultToolLength;
				m_AutoHide[(int)pane->m_LastSide].push_back(std::move(g));
				Commit();
				return true;
			}
			break;

		case PaneState::Floating:
			if (Has(pane->Caps, PaneCaps::CanFloat)) {
				RECT rc = pane->m_LastFloatRect;
				if (IsRectEmpty(&rc))
					rc = { 100, 100, 100 + pane->PreferredSize.cx, 100 + pane->PreferredSize.cy };
				return Float(pane, rc);
			}
			break;

		default:
			break;
	}
	return DockToEdge(pane, pane->m_LastSide);
}

bool DockLayout::Hide(DockPane* pane) {
	if (!Owns(pane) || !pane->m_Group)
		return false;
	DetachPane(pane);
	Commit();
	return true;
}

bool DockLayout::Activate(DockPane* pane) {
	if (!Owns(pane) || !pane->m_Group)
		return false;
	NoteActive(pane);
	auto g = pane->m_Group;
	int index = (int)(std::find(g->m_Panes.begin(), g->m_Panes.end(), pane) - g->m_Panes.begin());
	if (g->m_Active != index) {
		g->m_Active = index;
		Commit();
	}
	return true;
}

bool DockLayout::ReorderTab(DockPane* pane, int index) {
	if (!Owns(pane) || !pane->m_Group)
		return false;
	auto g = pane->m_Group;
	index = std::clamp(index, 0, (int)g->m_Panes.size() - 1);
	auto it = std::find(g->m_Panes.begin(), g->m_Panes.end(), pane);
	if (it - g->m_Panes.begin() == index)
		return true;
	g->m_Panes.erase(it);
	g->m_Panes.insert(g->m_Panes.begin() + index, pane);
	g->m_Active = index;
	Commit();
	return true;
}

bool DockLayout::CanFloatGroup(const DockGroup& group) const {
	if (group.m_Panes.empty())
		return false;
	// the main window keeps a document group
	if (group.IsDocument() && group.m_Where == GroupLocation::Main && DocumentGroups().size() < 2)
		return false;
	return std::all_of(group.m_Panes.begin(), group.m_Panes.end(), [](auto p) { return Has(p->Caps, PaneCaps::CanFloat); });
}

bool DockLayout::Float(DockPane* pane, const RECT& rect) {
	if (IsRectEmpty(&rect) || !CanFloatPane(pane))
		return false;
	if (pane->m_Group && pane->m_Group->m_Panes.size() == 1 && CanFloatGroup(*pane->m_Group))
		return FloatGroup(pane->m_Group, rect);
	if (pane->m_Group)
		DetachPane(pane);
	AddFloat(NewGroup(pane), rect);
	if (pane->Kind() == PaneKind::Document)
		m_ActiveDocument = pane;
	Commit();
	return true;
}

bool DockLayout::FloatGroup(DockGroup* group, const RECT& rect) {
	if (!group || IsRectEmpty(&rect) || !CanFloatGroup(*group))
		return false;
	if (group->m_Where == GroupLocation::Float && group->m_Float->m_Root->m_Children.size() == 1) {
		group->m_Float->m_Rect = rect;
		Commit();
		return true;
	}
	for (auto p : group->m_Panes)
		RecordPlacement(p);
	AddFloat(ReleaseGroup(group), rect);
	Commit();
	return true;
}

bool DockLayout::SetFloatRect(DockFloat* window, const RECT& rect) {
	if (!window || IsRectEmpty(&rect))
		return false;
	window->m_Rect = rect;
	Commit();
	return true;
}

bool DockLayout::CanDockTo(const DockPane* pane, const DockGroup* target, DockPosition pos) const {
	if (!Owns(pane) || !target || target->m_Where == GroupLocation::AutoHide)
		return false;
	if (pane->Kind() == PaneKind::Document) {
		if (!target->IsDocument())
			return false;
	}
	else if (pos == DockPosition::Tab && target->IsDocument()) {
		return false;
	}
	// splitting a group off itself only makes sense if something stays behind
	if (pane->m_Group == target && pos != DockPosition::Tab && target->m_Panes.size() < 2)
		return false;
	return true;
}

bool DockLayout::DockTo(DockPane* pane, DockGroup* target, DockPosition pos, int tabIndex) {
	if (!CanDockTo(pane, target, pos))
		return false;
	if (pane->m_Group == target && pos == DockPosition::Tab)
		return ReorderTab(pane, tabIndex < 0 ? (int)target->m_Panes.size() - 1 : tabIndex);

	// a pane that comes out of a floating window takes the size of that window
	const int length = pane->m_Group && pos != DockPosition::Tab ? LengthBeside(*pane->m_Group, *target, pos) : 0;
	if (pane->m_Group)
		DetachPane(pane);
	if (pos == DockPosition::Tab)
		target->AddPane(pane, tabIndex);
	else
		InsertBeside(target, NewGroup(pane), pos, length);
	if (pane->Kind() == PaneKind::Document)
		m_ActiveDocument = pane;
	Commit();
	return true;
}

bool DockLayout::DockToEdge(DockPane* pane, DockSide side, DockFloat* window) {
	if (!CanDockToEdge(pane))
		return false;
	if (window && std::none_of(m_Floats.begin(), m_Floats.end(), [&](auto& f) { return f.get() == window; }))
		return false;

	int length = Along(pane->PreferredSize, AxisOf(side));
	if (pane->m_Group) {
		// a pane that comes out of a floating window takes the size of that window
		if (const int floated = LengthWhenDocked(*pane->m_Group, side); floated > 0)
			length = floated;
		DetachPane(pane);
	}
	InsertAtEdge(window ? *window->m_Root : *m_Root, NewGroup(pane), side, length > 0 ? length : DefaultToolLength);
	Commit();
	return true;
}

bool DockLayout::CanMoveGroupTo(const DockGroup* group, const DockGroup* target, DockPosition pos) const {
	if (!group || !target || group == target || group->m_Panes.empty() || target->m_Where == GroupLocation::AutoHide)
		return false;
	if (group->IsDocument() && !target->IsDocument())
		return false;
	// a document group that leaves the main window has to leave another one behind
	if (group->IsDocument() && group->m_Where == GroupLocation::Main && pos != DockPosition::Tab &&
		target->m_Where != GroupLocation::Main && DocumentGroups().size() < 2)
		return false;
	return pos != DockPosition::Tab || group->Kind() == target->Kind();
}

bool DockLayout::CanDockToEdge(const DockPane* pane) const {
	return Owns(pane) && pane->Kind() == PaneKind::Tool;
}

bool DockLayout::CanMoveGroupToEdge(const DockGroup* group) const {
	return group && !group->IsDocument() && !group->m_Panes.empty();
}

bool DockLayout::CanFloatPane(const DockPane* pane) const {
	return Owns(pane) && Has(pane->Caps, PaneCaps::CanFloat);
}

bool DockLayout::MoveGroupTo(DockGroup* group, DockGroup* target, DockPosition pos) {
	if (!CanMoveGroupTo(group, target, pos))
		return false;

	for (auto p : group->m_Panes)
		RecordPlacement(p);
	if (group->IsDocument() && group->ActivePane())
		m_ActiveDocument = group->ActivePane();

	if (pos == DockPosition::Tab) {
		auto panes = std::move(group->m_Panes);
		group->m_Panes.clear();
		int active = group->m_Active;
		for (auto p : panes)
			target->AddPane(p, -1);
		target->m_Active = (int)target->m_Panes.size() - (int)panes.size() + active;
	}
	else {
		const int length = LengthBeside(*group, *target, pos);
		InsertBeside(target, ReleaseGroup(group), pos, length);
	}
	Commit();
	return true;
}

bool DockLayout::MoveGroupToEdge(DockGroup* group, DockSide side, DockFloat* window) {
	if (!CanMoveGroupToEdge(group))
		return false;
	if (window && std::none_of(m_Floats.begin(), m_Floats.end(), [&](auto& f) { return f.get() == window; }))
		return false;

	for (auto p : group->m_Panes)
		RecordPlacement(p);
	int length = DefaultLength(*group, side);
	auto node = ReleaseGroup(group);
	InsertAtEdge(window ? *window->m_Root : *m_Root, std::move(node), side, length);
	Commit();
	return true;
}

bool DockLayout::AutoHide(DockGroup* group) {
	if (!group || group->m_Where != GroupLocation::Main || group->IsDocument() || !group->m_Side)
		return false;
	if (!std::all_of(group->m_Panes.begin(), group->m_Panes.end(), [](auto p) { return Has(p->Caps, PaneCaps::CanAutoHide); }))
		return false;

	const DockSide side = *group->m_Side;
	for (auto p : group->m_Panes)
		RecordPlacement(p);
	int length = DefaultLength(*group, side);
	auto node = ReleaseGroup(group);
	node->AutoHideLength = length;
	m_AutoHide[(int)side].push_back(std::move(node));
	Commit();
	return true;
}

bool DockLayout::Unhide(DockGroup* group) {
	if (!group || group->m_Where != GroupLocation::AutoHide)
		return false;
	const DockSide side = *group->m_Side;
	for (auto p : group->m_Panes)
		RecordPlacement(p);
	int length = group->AutoHideLength > 0 ? group->AutoHideLength : DefaultLength(*group, side);
	auto node = ReleaseGroup(group);
	InsertAtEdge(*m_Root, std::move(node), side, length);
	Commit();
	return true;
}

bool DockLayout::ResizeSplitter(DockSplit* split, int index, int delta) {
	if (!split || index < 0 || index + 1 >= (int)split->m_Children.size() || delta == 0)
		return false;
	const Axis axis = split->m_Axis;
	auto& a = *split->m_Children[index];
	auto& b = *split->m_Children[index + 1];
	const int la = Length(a.Rect, axis), lb = Length(b.Rect, axis);
	if (la + lb <= 0)
		return false;

	const int lowest = std::min(MinLength(a, axis), la + lb);
	const int highest = la + lb - MinLength(b, axis);
	const int newA = std::max(lowest, std::min(highest, la + delta));
	if (newA == la)
		return false;
	const int newB = la + lb - newA;

	// pixels per unit of star weight, so that untouched star siblings keep their size
	double starWeight = 0, starPixels = 0;
	for (auto& c : split->m_Children) {
		if (c->Size.IsStar()) {
			starWeight += c->Size.Value;
			starPixels += Length(c->Rect, axis);
		}
	}
	const double perPixel = starPixels > 0 ? starWeight / starPixels : 1;

	auto assign = [&](DockNode& n, int pixels) {
		n.Size = n.Size.IsStar() ? SizeSpec::Star(std::max(pixels * perPixel, 1e-6)) : SizeSpec::Px(pixels);
		};
	assign(a, newA);
	assign(b, newB);
	Commit();
	return true;
}

//
// diagnostics
//

bool DockLayout::Validate(std::wstring* error) const {
	auto fail = [&](std::wstring message) {
		if (error)
			*error = std::move(message);
		return false;
	};

	std::set<const DockPane*> seen;
	const DockGroup* primary = PrimaryDocumentGroup();
	if (!primary)
		return fail(L"no document group");

	std::function<bool(const DockSplit&, bool, GroupLocation, const DockFloat*)> checkSplit;
	auto checkGroup = [&](const DockGroup& g, GroupLocation where, const DockFloat* fl) {
		if (g.m_Where != where)
			return fail(L"group location mismatch");
		if (g.m_Float != fl)
			return fail(L"group float mismatch");
		if (g.m_Panes.empty() && &g != primary)
			return fail(L"empty group");
		if (g.m_Active < 0 || (g.m_Active > 0 && g.m_Active >= (int)g.m_Panes.size()))
			return fail(L"active index out of range");
		for (auto p : g.m_Panes) {
			if (!Owns(p))
				return fail(L"unregistered pane in a group");
			if (!seen.insert(p).second)
				return fail(L"pane placed twice: " + p->m_Id);
			if (p->m_Group != &g)
				return fail(L"pane group pointer mismatch: " + p->m_Id);
			if (p->Kind() != g.Kind())
				return fail(L"pane kind does not match its group: " + p->m_Id);
			PaneState expected = where == GroupLocation::AutoHide ? PaneState::AutoHide :
				where == GroupLocation::Float ? PaneState::Floating :
				g.IsDocument() ? PaneState::Document : PaneState::Docked;
			if (p->m_State != expected)
				return fail(L"pane state mismatch: " + p->m_Id);
		}
		return true;
	};
	checkSplit = [&](const DockSplit& s, bool root, GroupLocation where, const DockFloat* fl) {
		if (s.m_Children.empty())
			return fail(L"empty split");
		if (!root && s.m_Children.size() < 2)
			return fail(L"single-child split below the root");
		bool star = false;
		for (auto& c : s.m_Children) {
			if (c->m_Parent != &s)
				return fail(L"parent pointer mismatch");
			if (!(c->Size.Value > 0))
				return fail(L"non-positive size");
			star |= c->Size.IsStar();
			if (auto inner = c->AsSplit()) {
				if (!checkSplit(*inner, false, where, fl))
					return false;
			}
			else if (!checkGroup(*c->AsGroup(), where, fl)) {
				return false;
			}
		}
		return star || fail(L"split without a star child");
	};

	if (m_Root->m_Parent)
		return fail(L"root has a parent");
	if (!checkSplit(*m_Root, true, GroupLocation::Main, nullptr))
		return false;
	for (int side = 0; side < SideCount; side++) {
		for (auto& g : m_AutoHide[side]) {
			if (g->m_Parent || g->IsDocument() || g->m_Panes.empty() || g->m_Side != (DockSide)side)
				return fail(L"bad auto-hide group");
			if (!checkGroup(*g, GroupLocation::AutoHide, nullptr))
				return false;
		}
	}
	for (auto& f : m_Floats) {
		if (f->m_Root->m_Parent)
			return fail(L"float root has a parent");
		if (f->m_Dpi < 48 || f->m_Dpi > 960)
			return fail(L"float with a bad DPI");
		if (!checkSplit(*f->m_Root, true, GroupLocation::Float, f.get()))
			return false;
	}
	for (auto& p : m_Panes) {
		if (!p->m_Group && (p->m_State != PaneState::Hidden || seen.contains(p.get())))
			return fail(L"hidden pane inconsistent: " + p->m_Id);
		if (p->m_Group && !seen.contains(p.get()))
			return fail(L"pane refers to a group that is not in the layout: " + p->m_Id);
	}
	return true;
}

}
