// Unit tests for the WTLDock layout model.
//
// Deliberately dependency-free (same approach as GraphControlTests): a console exe that returns the number
// of failed checks. Nothing here creates a window; the model is pure data.

#include <random>

#include "Harness.h"

using namespace WTLDock;

// ---- Fixture -------------------------------------------------------------------

struct Panes {
	DockPane* Sol;		// tool, left, 250 wide
	DockPane* Props;	// tool, right, 200 wide
	DockPane* Output;	// tool, bottom, 150 high
	DockPane* Ext;		// tool, right
	DockPane* Pinned;	// tool, left, no capabilities
	DockPane* A;		// documents
	DockPane* B;
	DockPane* C;
};

static DockPane* AddTool(DockLayout& l, const wchar_t* id, DockSide side, int cx, int cy, PaneCaps caps = PaneCaps::All) {
	PaneDesc d;
	d.Id = d.Title = id;
	d.DefaultSide = side;
	d.PreferredSize = { cx, cy };
	d.Caps = caps;
	return l.AddPane(d);
}

static DockPane* AddDoc(DockLayout& l, const wchar_t* id) {
	PaneDesc d;
	d.Id = d.Title = id;
	d.Kind = PaneKind::Document;
	return l.AddPane(d);
}

static Panes Register(DockLayout& l) {
	Panes p{};
	p.Sol = AddTool(l, L"Sol", DockSide::Left, 250, 300);
	p.Props = AddTool(l, L"Props", DockSide::Right, 200, 300);
	p.Output = AddTool(l, L"Output", DockSide::Bottom, 300, 150);
	p.Ext = AddTool(l, L"Ext", DockSide::Right, 180, 300);
	p.Pinned = AddTool(l, L"Pinned", DockSide::Left, 100, 100, PaneCaps::None);
	p.A = AddDoc(l, L"a.cpp");
	p.B = AddDoc(l, L"b.cpp");
	p.C = AddDoc(l, L"c.cpp");
	return p;
}

// Sol | documents | Props, with Output underneath everything; a.cpp is open.
static Panes BuildStandard(DockLayout& l) {
	Panes p = Register(l);
	l.Show(p.Sol);
	l.Show(p.Props);
	l.Show(p.Output);
	l.Show(p.A);
	return p;
}

static const RECT Client{ 0, 0, 1000, 600 };

// ---- Basics ------------------------------------------------------------------

TEST(New_LayoutHasAnEmptyDocumentArea) {
	DockLayout l;
	CHECK_DUMP(l, L"main: H(D[])");
	CHECK_VALID(l);
	l.Arrange(Client);
	CHECK_RECT(l.Root().Rect, 0, 0, 1000, 600);
	CHECK_RECT(l.PrimaryDocumentGroup()->Rect, 0, 0, 1000, 600);
}

TEST(AddPane_RejectsEmptyAndDuplicateIds) {
	DockLayout l;
	CHECK(AddTool(l, L"x", DockSide::Left, 100, 100));
	CHECK(!AddTool(l, L"x", DockSide::Left, 100, 100));
	CHECK(!AddTool(l, L"", DockSide::Left, 100, 100));
	CHECK(l.FindPane(L"x") != nullptr);
	CHECK(l.FindPane(L"y") == nullptr);
	CHECK(l.Panes().size() == 1);
}

TEST(Show_PlacesPanesAtTheirDefaultEdges) {
	DockLayout l;
	Panes p = Register(l);
	CHECK(p.Sol->State() == PaneState::Hidden);
	CHECK_DUMP(l, L"main: H(D[])\nhidden: Sol Props Output Ext Pinned a.cpp b.cpp c.cpp");

	CHECK(l.Show(p.Sol));
	CHECK_DUMP(l, L"main: H(T[Sol]@250 D[])\nhidden: Props Output Ext Pinned a.cpp b.cpp c.cpp");
	CHECK(l.Show(p.Props));
	CHECK(l.Show(p.Output));
	CHECK(l.Show(p.A));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK_VALID(l);

	CHECK(p.Sol->State() == PaneState::Docked);
	CHECK(p.A->State() == PaneState::Document);
	CHECK(p.Sol->Group()->Side() == DockSide::Left);
	CHECK(p.Props->Group()->Side() == DockSide::Right);
	CHECK(p.Output->Group()->Side() == DockSide::Bottom);
	CHECK(!p.A->Group()->Side().has_value());
}

TEST(Show_OnAPlacedPaneJustActivatesIt) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(p.Props->Group()->ActivePane() == p.Props);
	CHECK(l.Show(p.Sol));
	CHECK(p.Sol->Group()->ActivePane() == p.Sol);
	CHECK_DUMP(l, L"main: V(H(T[>Sol,Props]@250 D[a.cpp]) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
}

// ---- Arrange -----------------------------------------------------------------

TEST(Arrange_StandardLayoutGeometry) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	// root: V, 594 usable rows -> 444 above the 150 high Output; inner: 988 usable columns
	CHECK_RECT(p.Sol->Group()->Rect, 0, 0, 250, 444);
	CHECK_RECT(p.A->Group()->Rect, 256, 0, 794, 444);
	CHECK_RECT(p.Props->Group()->Rect, 800, 0, 1000, 444);
	CHECK_RECT(p.Output->Group()->Rect, 0, 450, 1000, 600);
}

TEST(Arrange_PxNodesKeepTheirSizeWhenTheWindowGrows) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange({ 0, 0, 1200, 700 });
	CHECK(Width(p.Sol->Group()->Rect) == 250);
	CHECK(Width(p.Props->Group()->Rect) == 200);
	CHECK(Height(p.Output->Group()->Rect) == 150);
	CHECK(Width(p.A->Group()->Rect) == 1200 - 12 - 450);
	CHECK(Height(p.A->Group()->Rect) == 700 - 6 - 150);
}

TEST(Arrange_ShrinksPxNodesBeforeStarNodesGoBelowTheirMinimum) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange({ 0, 0, 300, 300 });
	// 288 usable columns; the document keeps its 80 minimum, Props gives up 120, Sol the remaining 122
	CHECK(Width(p.A->Group()->Rect) == 80);
	CHECK(Width(p.Props->Group()->Rect) == 80);
	CHECK(Width(p.Sol->Group()->Rect) == 128);
}

TEST(Arrange_SqueezesEverythingWhenTheMinimumsDoNotFit) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange({ 0, 0, 200, 300 });
	int total = Width(p.Sol->Group()->Rect) + Width(p.A->Group()->Rect) + Width(p.Props->Group()->Rect);
	CHECK(total == 200 - 12);
	CHECK(p.Props->Group()->Rect.right <= 200);
	CHECK(Width(p.A->Group()->Rect) > 0);
}

TEST(Arrange_PaneMinSizeRaisesTheGroupMinimum) {
	DockLayout l;
	Panes p = BuildStandard(l);
	p.A->MinSize = { 400, 100 };
	l.Arrange({ 0, 0, 700, 600 });
	CHECK(Width(p.A->Group()->Rect) == 400);
	CHECK(l.MinLength(*p.A->Group(), Axis::Horizontal) == 400);
	CHECK(l.MinLength(l.Root(), Axis::Horizontal) == 80 + 400 + 80 + 12);
}

TEST(Arrange_AutoHideBarsTakeSpaceFromTheClientArea) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.AutoHide(p.Sol->Group()));
	l.Arrange(Client);
	CHECK_RECT(l.AutoHideBarRect(DockSide::Left), 0, 0, 24, 600);
	CHECK(IsRectEmpty(&l.AutoHideBarRect(DockSide::Right)));
	CHECK(l.Root().Rect.left == 24);
	CHECK(l.PrimaryDocumentGroup()->Rect.left == 24);
	CHECK(l.Root().Rect.right == 1000);
}

TEST(Splitters_ListedBetweenSiblings) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto hits = DockLayout::Splitters(l.Root());
	CHECK(hits.size() == 3);
	int horizontal = 0;
	for (auto& h : hits)
		if (h.Split->GetAxis() == Axis::Horizontal)
			horizontal++;
	CHECK(horizontal == 2);
	CHECK(DockLayout::GroupAt(l.Root(), { 100, 100 }) == p.Sol->Group());
	CHECK(DockLayout::GroupAt(l.Root(), { 500, 500 }) == p.Output->Group());
	CHECK(DockLayout::GroupAt(l.Root(), { 253, 100 }) == nullptr);	// on a splitter
}

// ---- Resizing ----------------------------------------------------------------

static SplitterHit FindSplitter(DockLayout& l, DockPane* left) {
	for (auto& h : DockLayout::Splitters(l.Root()))
		if (h.Split->Children()[h.Index].get() == left->Group())
			return h;
	return {};
}

TEST(ResizeSplitter_MovesTheBoundary) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto hit = FindSplitter(l, p.Sol);
	CHECK(hit.Split != nullptr);
	CHECK(l.ResizeSplitter(hit.Split, hit.Index, 50));
	l.Arrange(Client);
	CHECK(Width(p.Sol->Group()->Rect) == 300);
	CHECK(Width(p.A->Group()->Rect) == 488);
	CHECK(Width(p.Props->Group()->Rect) == 200);
	CHECK_VALID(l);
}

TEST(ResizeSplitter_ClampsToMinimumSizes) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto hit = FindSplitter(l, p.Sol);
	CHECK(l.ResizeSplitter(hit.Split, hit.Index, -1000));
	l.Arrange(Client);
	CHECK(Width(p.Sol->Group()->Rect) == 80);
	hit = FindSplitter(l, p.Sol);
	CHECK(l.ResizeSplitter(hit.Split, hit.Index, 5000));
	l.Arrange(Client);
	CHECK(Width(p.A->Group()->Rect) == 80);
	CHECK(Width(p.Sol->Group()->Rect) == 250 + 538 - 80);
	CHECK(!l.ResizeSplitter(hit.Split, hit.Index, 5000));	// already at the limit
}

TEST(ResizeSplitter_BetweenStarSiblingsKeepsTheOtherOnesStill) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.DockTo(p.Ext, p.A->Group(), DockPosition::Right));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp]*0.5 T[Ext]*0.5 T[Props]@200) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
	l.Arrange(Client);
	// 1000 - 3 splitters - 450 = 532 shared by the two star children
	CHECK(Width(p.A->Group()->Rect) == 266);
	CHECK(Width(p.Ext->Group()->Rect) == 266);

	auto hit = FindSplitter(l, p.A);
	CHECK(l.ResizeSplitter(hit.Split, hit.Index, 66));
	l.Arrange(Client);
	CHECK(std::abs(Width(p.A->Group()->Rect) - 332) <= 1);
	CHECK(std::abs(Width(p.Ext->Group()->Rect) - 200) <= 1);
	CHECK(Width(p.Sol->Group()->Rect) == 250);
	CHECK(Width(p.Props->Group()->Rect) == 200);

	// the proportions survive a resize of the window
	l.Arrange({ 0, 0, 1988, 600 });
	CHECK(Width(p.A->Group()->Rect) > Width(p.Ext->Group()->Rect));
	CHECK_VALID(l);
}

TEST(ResizeSplitter_RejectsBadArguments) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(!l.ResizeSplitter(nullptr, 0, 10));
	CHECK(!l.ResizeSplitter(&l.Root(), 5, 10));
	CHECK(!l.ResizeSplitter(&l.Root(), 0, 0));
	DockLayout empty;
	CHECK(!empty.ResizeSplitter(&empty.Root(), 0, 10));	// a single child has no splitter
}

// ---- Docking -----------------------------------------------------------------

TEST(DockTo_TabMergesPanesIntoOneGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab));
	CHECK_DUMP(l, L"main: V(H(T[Sol,>Props]@250 D[a.cpp]) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(p.Props->Group() == p.Sol->Group());
	CHECK_VALID(l);

	// at a specific position
	CHECK(l.Show(p.Ext));
	CHECK(l.DockTo(p.Ext, p.Sol->Group(), DockPosition::Tab, 0));
	CHECK_DUMP(l, L"main: V(H(T[>Ext,Sol,Props]@250 D[a.cpp]) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
}

TEST(DockTo_TabOntoTheOwnGroupReorders) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab, 0));
	CHECK(p.Sol->Group()->Panes()[0] == p.Props);
	CHECK(l.ReorderTab(p.Props, 1));
	CHECK(p.Sol->Group()->Panes()[1] == p.Props);
	CHECK(p.Sol->Group()->ActivePane() == p.Props);
	CHECK_VALID(l);
}

TEST(DockTo_SplitAcrossTheParentAxisWrapsTheTarget) {
	DockLayout l;
	Panes p = BuildStandard(l);
	// Output moves above the document area: its old (bottom) group disappears and the root turns into a single H split
	CHECK(l.DockTo(p.Output, p.A->Group(), DockPosition::Bottom));
	CHECK_DUMP(l, L"main: H(T[Sol]@250 V(D[a.cpp] T[Output]) T[Props]@200)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK_VALID(l);
	CHECK(p.Output->Group()->Side() == DockSide::Bottom);
	l.Arrange(Client);
	CHECK_RECT(p.Output->Group()->Rect, 256, 300 + 3, 794, 600);
	CHECK_RECT(p.A->Group()->Rect, 256, 0, 794, 300 - 3);
}

TEST(DockTo_SplitAlongTheParentAxisSharesThePxTargetsSpace) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	CHECK(l.DockTo(p.Ext, p.Sol->Group(), DockPosition::Right));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@122 T[Ext]@122 D[a.cpp] T[Props]@200) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
	l.Arrange(Client);	// the split uses the target's current size
	CHECK(l.DockTo(p.Pinned, p.Sol->Group(), DockPosition::Left));
	CHECK_DUMP(l, L"main: V(H(T[Pinned]@58 T[Sol]@58 T[Ext]@122 D[a.cpp] T[Props]@200) T[Output]@150)\nhidden: b.cpp c.cpp");
	CHECK_VALID(l);
}

TEST(DockTo_ToolGroupBesideTheDocumentGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.DockTo(p.Ext, p.A->Group(), DockPosition::Top));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 V(T[Ext] D[a.cpp]) T[Props]@200) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
	CHECK(p.Ext->Group()->Side() == DockSide::Top);
}

TEST(DockTo_SplittingAPaneOffItsOwnGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(!l.DockTo(p.Output, p.Output->Group(), DockPosition::Left));		// would leave nothing behind
	CHECK(l.DockTo(p.Props, p.Sol->Group(), DockPosition::Bottom));
	CHECK_DUMP(l, L"main: V(H(V(T[Sol] T[Props])@250 D[a.cpp]) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK_VALID(l);
}

TEST(DockToEdge_UsesTheOuterEdgeOfTheRoot) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.DockToEdge(p.Ext, DockSide::Top));
	CHECK_DUMP(l, L"main: V(T[Ext]@300 H(T[Sol]@250 D[a.cpp] T[Props]@200) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
	CHECK(l.DockToEdge(p.Pinned, DockSide::Left));
	CHECK_DUMP(l, L"main: H(T[Pinned]@100 V(T[Ext]@300 H(T[Sol]@250 D[a.cpp] T[Props]@200) T[Output]@150))\nhidden: b.cpp c.cpp");
	CHECK_VALID(l);
}

TEST(DockTo_DocumentRules) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.B);
	CHECK(!l.DockTo(p.Sol, p.A->Group(), DockPosition::Tab));				// a tool pane is not a document
	CHECK(!l.DockTo(p.B, p.Sol->Group(), DockPosition::Tab));				// a document is not a tool pane
	CHECK(!l.DockTo(p.B, p.Sol->Group(), DockPosition::Right));
	CHECK(!l.DockToEdge(p.B, DockSide::Left));
	CHECK(!l.Float(p.B, { 0, 0, 100, 100 }));
	CHECK(!l.FloatGroup(p.A->Group(), { 0, 0, 100, 100 }));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp,>b.cpp] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned c.cpp");
}

TEST(Documents_TabAndSplit) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.B);
	l.Show(p.C);
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp,b.cpp,>c.cpp] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned");
	CHECK(l.DockTo(p.C, p.A->Group(), DockPosition::Right));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp,>b.cpp]*0.5 D[c.cpp]*0.5 T[Props]@200) T[Output]@150)\nhidden: Ext Pinned");
	CHECK_VALID(l);
	CHECK(!p.C->Group()->Side().has_value());
	CHECK(p.Sol->Group()->Side() == DockSide::Left);
	CHECK(p.Props->Group()->Side() == DockSide::Right);

	// the second document group goes away when it is emptied
	CHECK(l.Hide(p.C));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp,>b.cpp] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned c.cpp");
}

TEST(Documents_TheLastDocumentGroupIsNeverRemoved) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.B);
	CHECK(l.Hide(p.A));
	CHECK(l.Hide(p.B));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned a.cpp b.cpp c.cpp");
	CHECK_VALID(l);
	CHECK(l.Show(p.B));
	CHECK(p.B->State() == PaneState::Document);
}

// ---- Hide / Show ---------------------------------------------------------------

TEST(Hide_CollapsesEmptyGroupsAndSplits) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.Hide(p.Output));
	CHECK_DUMP(l, L"main: H(T[Sol]@250 D[a.cpp] T[Props]@200)\nhidden: Output Ext Pinned b.cpp c.cpp");
	CHECK(l.Hide(p.Sol));
	CHECK(l.Hide(p.Props));
	CHECK_DUMP(l, L"main: H(D[a.cpp])\nhidden: Sol Props Output Ext Pinned b.cpp c.cpp");
	CHECK(!l.Hide(p.Sol));	// already hidden
	CHECK_VALID(l);
}

TEST(Show_RestoresTheLastPositionAndSize) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto hit = FindSplitter(l, p.Sol);
	l.ResizeSplitter(hit.Split, hit.Index, 60);
	l.Arrange(Client);
	CHECK(Width(p.Sol->Group()->Rect) == 310);

	CHECK(l.Hide(p.Sol));
	CHECK(l.DockToEdge(p.Ext, DockSide::Left));
	CHECK(l.Show(p.Sol));	// last seen at the left: back to the left edge, 310 wide
	CHECK_DUMP(l, L"main: H(T[Sol]@310 T[Ext]@180 V(H(D[a.cpp] T[Props]@200) T[Output]@150))\nhidden: Pinned b.cpp c.cpp");
}

TEST(Show_RestoresDocumentsAndFloatsAndAutoHide) {
	DockLayout l;
	Panes p = BuildStandard(l);
	RECT rc{ 50, 60, 350, 400 };
	CHECK(l.Float(p.Props, rc));
	CHECK(l.AutoHide(p.Sol->Group()));
	CHECK(l.Hide(p.Props));
	CHECK(l.Hide(p.Sol));
	CHECK(l.Hide(p.A));
	CHECK(l.Show(p.Props));
	CHECK(p.Props->State() == PaneState::Floating);
	CHECK(l.Floats()[0]->Rect().left == 50 && l.Floats()[0]->Rect().bottom == 400);
	CHECK(l.Show(p.Sol));
	CHECK(p.Sol->State() == PaneState::AutoHide);
	CHECK(l.Show(p.A));
	CHECK(p.A->State() == PaneState::Document);
	CHECK_VALID(l);
}

TEST(Activate_SwitchesTabs) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(p.Sol->Group()->ActivePane() == p.Props);
	CHECK(l.Activate(p.Sol));
	CHECK(p.Sol->Group()->ActivePane() == p.Sol);
	CHECK(!l.Activate(p.Ext));	// hidden
}

TEST(RemovePane_HidesAndUnregisters) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.RemovePane(p.Sol));
	CHECK(l.FindPane(L"Sol") == nullptr);
	CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props]@200) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK_VALID(l);
}

// ---- Floating ------------------------------------------------------------------

TEST(Float_MovesAPaneIntoItsOwnWindow) {
	DockLayout l;
	Panes p = BuildStandard(l);
	RECT rc{ 100, 120, 400, 420 };
	CHECK(l.Float(p.Props, rc));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp]) T[Output]@150)\nfloat: (100,120,400,420) H(T[Props])\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(p.Props->State() == PaneState::Floating);
	CHECK(p.Props->Group()->Location() == GroupLocation::Float);
	CHECK(!p.Props->Group()->Side().has_value());
	CHECK_VALID(l);

	// floating again only moves the window
	int id = l.Floats()[0]->Id();
	RECT rc2{ 10, 10, 300, 300 };
	CHECK(l.Float(p.Props, rc2));
	CHECK(l.Floats().size() == 1 && l.Floats()[0]->Id() == id);
	CHECK(l.Floats()[0]->Rect().right == 300);
}

TEST(Float_OnePaneOutOfATabGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(l.Float(p.Props, { 0, 0, 200, 200 }));
	CHECK_DUMP(l, L"main: V(H(T[Sol]@250 D[a.cpp]) T[Output]@150)\nfloat: (0,0,200,200) H(T[Props])\nhidden: Ext Pinned b.cpp c.cpp");
}

TEST(Float_WholeGroupKeepsItsTabs) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	CHECK(l.FloatGroup(p.Sol->Group(), { 0, 0, 200, 200 }));
	CHECK_DUMP(l, L"main: V(D[a.cpp] T[Output]@150)\nfloat: (0,0,200,200) H(T[Sol,>Props])\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK_VALID(l);
}

TEST(Float_RespectsCapabilities) {
	DockLayout l;
	Panes p = Register(l);
	l.Show(p.Pinned);
	CHECK(!l.Float(p.Pinned, { 0, 0, 100, 100 }));
	CHECK(!l.Float(p.Sol, { 0, 0, 0, 0 }));	// empty rectangle
	CHECK(l.Floats().empty());
}

TEST(Float_HiddenPaneCanBeFloatedDirectly) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.Float(p.Ext, { 0, 0, 200, 200 }));
	CHECK(p.Ext->State() == PaneState::Floating);
	CHECK_VALID(l);
}

TEST(Float_DockInsideAFloatingWindow) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Float(p.Props, { 0, 0, 400, 400 });
	auto window = l.Floats()[0].get();
	CHECK(l.DockToEdge(p.Output, DockSide::Bottom, window));
	CHECK_DUMP(l, L"main: H(T[Sol]@250 D[a.cpp])\nfloat: (0,0,400,400) V(T[Props] T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(l.DockTo(p.Sol, p.Props->Group(), DockPosition::Right));
	CHECK(l.Floats().size() == 1);
	CHECK_VALID(l);

	l.Arrange(*l.Floats()[0], { 0, 0, 400, 400 });
	CHECK_RECT(p.Output->Group()->Rect, 0, 250, 400, 400);
	CHECK(Width(p.Props->Group()->Rect) + Width(p.Sol->Group()->Rect) + 6 == 400);
}

TEST(Float_TheWindowGoesAwayWhenItsLastPaneLeaves) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Float(p.Props, { 0, 0, 400, 400 });
	l.Float(p.Output, { 10, 10, 300, 300 });
	CHECK(l.Floats().size() == 2);
	CHECK(l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab));
	CHECK(l.Floats().size() == 1);
	CHECK(l.Hide(p.Output));
	CHECK(l.Floats().empty());
	CHECK_VALID(l);
}

TEST(Float_SetRect) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Float(p.Props, { 0, 0, 400, 400 });
	CHECK(l.SetFloatRect(l.Floats()[0].get(), { 5, 6, 405, 406 }));
	CHECK(l.Floats()[0]->Rect().top == 6);
	CHECK(!l.SetFloatRect(l.Floats()[0].get(), { 0, 0, 0, 0 }));
}

// ---- Group moves ---------------------------------------------------------------

TEST(MoveGroupTo_TabMergesAllPanes) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Ext, p.Sol->Group(), DockPosition::Tab);		// a hidden pane can be docked directly
	CHECK(l.MoveGroupTo(p.Sol->Group(), p.Props->Group(), DockPosition::Tab));
	CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props,Sol,>Ext]@200) T[Output]@150)\nhidden: Pinned b.cpp c.cpp");
	CHECK_VALID(l);
}

TEST(MoveGroupTo_SplitMovesTheGroupNode) {
	DockLayout l;
	Panes p = BuildStandard(l);
	CHECK(l.MoveGroupTo(p.Output->Group(), p.Sol->Group(), DockPosition::Bottom));
	CHECK_DUMP(l, L"main: H(V(T[Sol] T[Output])@250 D[a.cpp] T[Props]@200)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(!l.MoveGroupTo(p.Sol->Group(), p.Sol->Group(), DockPosition::Left));
	CHECK(!l.MoveGroupTo(p.Sol->Group(), p.A->Group(), DockPosition::Tab));	// tool group into the documents
	CHECK(!l.MoveGroupTo(p.A->Group(), p.Sol->Group(), DockPosition::Right));	// documents next to a tool group
}

TEST(MoveGroupToEdge_KeepsTabsAndLength) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	l.Arrange(Client);
	CHECK(l.MoveGroupToEdge(p.Sol->Group(), DockSide::Right));
	CHECK_DUMP(l, L"main: H(V(D[a.cpp] T[Output]@150) T[Sol,>Props]@250)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(!l.MoveGroupToEdge(p.A->Group(), DockSide::Left));
	CHECK_VALID(l);
}

// ---- Auto-hide -----------------------------------------------------------------

TEST(AutoHide_CollapsesAGroupIntoTheBar) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	CHECK(l.AutoHide(p.Sol->Group()));
	CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props]@200) T[Output]@150)\nautohide left: T[Sol]@250\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(p.Sol->State() == PaneState::AutoHide);
	CHECK(p.Sol->Group()->Side() == DockSide::Left);
	CHECK(l.AutoHide(p.Output->Group()));
	CHECK(l.AutoHideGroups(DockSide::Bottom).size() == 1);
	CHECK(l.AutoHideGroups(DockSide::Bottom)[0]->AutoHideLength == 150);
	CHECK_VALID(l);
}

TEST(AutoHide_Unhide_DocksAtTheEdgeOfTheSameSide) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	l.AutoHide(p.Sol->Group());
	CHECK(l.Unhide(p.Sol->Group()));
	CHECK_DUMP(l, L"main: H(T[Sol]@250 V(H(D[a.cpp] T[Props]@200) T[Output]@150))\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(p.Sol->State() == PaneState::Docked);
	CHECK(p.Sol->Group()->Side() == DockSide::Left);
	CHECK(!l.Unhide(p.Sol->Group()));	// not in a bar any more
	CHECK_VALID(l);
}

TEST(AutoHide_Rules) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.Pinned);
	CHECK(!l.AutoHide(p.A->Group()));										// documents
	CHECK(!l.AutoHide(p.Pinned->Group()));									// no CanAutoHide
	l.Float(p.Props, { 0, 0, 300, 300 });
	CHECK(!l.AutoHide(p.Props->Group()));									// floating
	CHECK(l.AutoHide(p.Sol->Group()));
	CHECK(!l.AutoHide(p.Sol->Group()));										// already auto-hidden
	CHECK_VALID(l);
}

TEST(AutoHide_APaneCanBeDockedBackIntoAGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.AutoHide(p.Sol->Group());
	CHECK(l.DockTo(p.Sol, p.Props->Group(), DockPosition::Tab));
	CHECK(l.AutoHideGroups(DockSide::Left).empty());
	CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props,>Sol]@200) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(!l.DockTo(p.Ext, p.Ext->Group(), DockPosition::Left));	// hidden pane, no group
	CHECK_VALID(l);
}

// ---- Persistence ---------------------------------------------------------------

static void BuildBusy(DockLayout& l, Panes& p) {
	p = BuildStandard(l);
	l.Show(p.B);
	l.Show(p.C);
	l.DockTo(p.C, p.A->Group(), DockPosition::Right);
	l.DockTo(p.Ext, p.Sol->Group(), DockPosition::Tab);
	l.Show(p.Ext);
	l.DockTo(p.Ext, p.Sol->Group(), DockPosition::Tab);
	l.Arrange(Client);
	l.Float(p.Props, { 40, 50, 340, 450 });
	l.Show(p.Pinned);
	l.Hide(p.Pinned);
	l.AutoHide(p.Output->Group());
}

TEST(Save_LoadRoundTrips) {
	DockLayout l;
	Panes p;
	BuildBusy(l, p);
	CHECK_VALID(l);
	std::string text = l.Save();

	DockLayout copy;
	Register(copy);
	std::wstring error;
	CHECK(copy.Load(text, {}, &error));
	CHECK_STR(error, L"");
	CHECK_STR(copy.Dump(), l.Dump());
	CHECK(copy.Save() == text);
	CHECK_VALID(copy);

	// and the hidden pane still remembers where it was
	CHECK(copy.Show(copy.FindPane(L"Pinned")));
	CHECK(l.Show(p.Pinned));
	CHECK_STR(copy.Dump(), l.Dump());
}

TEST(Load_RestoresRememberedPlacementOfHiddenPanes) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Float(p.Props, { 40, 50, 340, 450 });
	l.Hide(p.Props);
	std::string text = l.Save();

	DockLayout copy;
	Register(copy);
	CHECK(copy.Load(text));
	auto props = copy.FindPane(L"Props");
	CHECK(props->State() == PaneState::Hidden);
	CHECK(copy.Show(props));
	CHECK(props->State() == PaneState::Floating);
	CHECK(copy.Floats()[0]->Rect().left == 40 && copy.Floats()[0]->Rect().bottom == 450);
}

TEST(Load_DropsUnknownPanesAndHidesUnlistedOnes) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.Ext);
	std::string text = l.Save();

	DockLayout copy;		// knows Sol, Props, a.cpp, b.cpp but not Output or Ext; and knows Pinned, which the file lacks
	AddTool(copy, L"Sol", DockSide::Left, 250, 300);
	AddTool(copy, L"Props", DockSide::Right, 200, 300);
	AddTool(copy, L"Pinned", DockSide::Left, 100, 100);
	AddDoc(copy, L"a.cpp");
	CHECK(copy.Load(text));
	CHECK_DUMP(copy, L"main: H(T[Sol]@250 D[a.cpp] T[Props]@200)\nhidden: Pinned");
	CHECK_VALID(copy);
}

TEST(Load_FactoryCreatesMissingPanes) {
	DockLayout l;
	Panes p = BuildStandard(l);
	std::string text = l.Save();

	DockLayout copy;
	std::vector<std::wstring> requested;
	auto factory = [&](DockLayout& layout, const std::wstring& id) -> DockPane* {
		requested.push_back(id);
		if (id == L"Props")
			return nullptr;	// refuse this one
		return id == L"a.cpp" ? AddDoc(layout, id.c_str()) : AddTool(layout, id.c_str(), DockSide::Left, 250, 250);
	};
	CHECK(copy.Load(text, factory));
	CHECK(requested.size() == 4);
	CHECK_DUMP(copy, L"main: V(H(T[Sol]@250 D[a.cpp]) T[Output]@150)");
	CHECK_VALID(copy);
}

TEST(Load_DropsPanesOfTheWrongKindAndDuplicates) {
	DockLayout copy;
	Panes p = Register(copy);
	const char* text = R"({
		"version": 1,
		"main": { "type": "split", "axis": "h", "children": [
			{ "type": "group", "kind": "tool", "panes": ["Sol", "a.cpp", "Sol"], "size": { "px": 250 } },
			{ "type": "group", "kind": "document", "panes": ["a.cpp", "Props", "b.cpp"], "size": { "star": 1 } },
			{ "type": "group", "kind": "tool", "panes": ["Sol", "ghost"], "size": { "px": 100 } }
		] }
	})";
	CHECK(copy.Load(text));
	CHECK_DUMP(copy, L"main: H(T[Sol]@250 D[>a.cpp,b.cpp])\nhidden: Props Output Ext Pinned c.cpp");
	CHECK_VALID(copy);
}

TEST(Load_DocumentGroupsInFloatsAreDropped) {
	DockLayout copy;
	Panes p = Register(copy);
	const char* text = R"({
		"version": 1,
		"main": { "type": "split", "axis": "h", "children": [
			{ "type": "group", "kind": "document", "panes": ["a.cpp"] } ] },
		"autoHide": { "left": [ { "type": "group", "kind": "document", "panes": ["b.cpp"] }, { "type": "group", "kind": "tool", "panes": ["Sol"], "hideLength": 210 } ] },
		"floats": [ { "rect": [1, 2, 301, 302], "root": { "type": "split", "axis": "v", "children": [
			{ "type": "group", "kind": "document", "panes": ["c.cpp"] },
			{ "type": "group", "kind": "tool", "panes": ["Props"] } ] } } ]
	})";
	CHECK(copy.Load(text));
	CHECK_DUMP(copy, L"main: H(D[a.cpp])\nautohide left: T[Sol]@210\nfloat: (1,2,301,302) V(T[Props])\nhidden: Output Ext Pinned b.cpp c.cpp");
}

TEST(Load_RejectsBadInputAndLeavesTheLayoutAlone) {
	DockLayout l;
	Panes p = BuildStandard(l);
	const std::wstring before = l.Dump();
	const uint64_t version = l.Version();

	const char* bad[] = {
		"",
		"not json",
		"[]",
		"{}",
		R"({"version": 2, "main": {}})",
		R"({"version": 1})",
		R"({"version": 1, "main": {"type": "group", "kind": "tool", "panes": []}})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": []}})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "tool", "panes": ["Sol"]}]}})",
		R"({"version": 1, "main": {"type": "split", "axis": "x", "children": []}})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": [], "size": {"px": -5}}]}})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": [1]}]}})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": []}]}, "floats": [{"rect": [0,0,0,0], "root": {"type": "split", "axis": "h", "children": []}}]})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": []}]}, "floats": 5})",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": []}]}} trailing)",
		R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": ["a.cpp\)",
	};
	for (auto text : bad) {
		std::wstring error;
		CHECK(!l.Load(text, {}, &error));
		CHECK(!error.empty());
	}

	// nesting bomb: must be rejected, not crash
	std::string deep(100000, '[');
	CHECK(!l.Load(deep));
	std::string deepNodes = R"({"version": 1, "main": )";
	for (int i = 0; i < 200; i++)
		deepNodes += R"({"type": "split", "axis": "h", "children": [)";
	CHECK(!l.Load(deepNodes));

	CHECK_STR(l.Dump(), before);
	CHECK(l.Version() == version);
	CHECK_VALID(l);
}

TEST(Load_JsonEscapesAndUnicode) {
	DockLayout l;
	PaneDesc d;
	d.Id = L"Ünïcöde ✓ \U0001F600 \"q\" \\ /";
	d.Title = L"t";
	auto pane = l.AddPane(d);
	l.Show(pane);
	std::string text = l.Save();

	DockLayout copy;
	d.Title = L"t";
	copy.AddPane(d);
	CHECK(copy.Load(text));
	CHECK(copy.FindPane(d.Id)->State() == PaneState::Docked);

	// escapes written by hand, including a surrogate pair
	DockLayout hand;
	d.Id = L"a\tb\U0001F600";
	hand.AddPane(d);
	const char* json = R"({"version": 1, "main": {"type": "split", "axis": "h", "children": [
		{"type": "group", "kind": "document", "panes": []},
		{"type": "group", "kind": "tool", "panes": ["a\tb😀"]}]}})";
	CHECK(hand.Load(json));
	CHECK(hand.FindPane(d.Id)->State() == PaneState::Docked);
	CHECK(!hand.Load(R"({"version": 1, "main": "\ud83d"})"));		// unpaired surrogate
}

TEST(ChangeHandler_FiresOncePerOperation) {
	DockLayout l;
	Panes p = Register(l);
	int calls = 0;
	l.SetChangeHandler([&] { calls++; });
	uint64_t v = l.Version();
	l.Show(p.Sol);
	CHECK(calls == 1 && l.Version() == v + 1);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);	// hidden pane docked directly: one change
	CHECK(calls == 2);
	l.Hide(p.Ext);											// nothing to do
	l.DockTo(p.Sol, nullptr, DockPosition::Tab);
	CHECK(calls == 2 && l.Version() == v + 2);
}

// ---- DPI -----------------------------------------------------------------------

TEST(Dpi_AddPaneScalesTheDescription) {
	DockLayout l;
	l.SetDpi(192);
	auto p = AddTool(l, L"x", DockSide::Left, 250, 300);
	CHECK(p->PreferredSize.cx == 500 && p->PreferredSize.cy == 600);
	CHECK(p->MinSize.cx == 160 && p->MinSize.cy == 120);
}

TEST(Dpi_SetDpiScalesEveryPixelSize) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	l.AutoHide(p.Sol->Group());
	const std::wstring before = l.Dump();

	int changes = 0;
	l.SetChangeHandler([&] { changes++; });
	l.SetDpi(144);
	CHECK(l.Dpi() == 144 && changes == 1);
	CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props]@300) T[Output]@225)\nautohide left: T[Sol]@375\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(p.Props->PreferredSize.cx == 300 && p.Ext->MinSize.cx == 120);

	l.SetDpi(144);		// no change
	CHECK(changes == 1);
	l.SetDpi(0);
	CHECK(l.Dpi() == 144);

	l.SetDpi(96);
	CHECK_STR(l.Dump(), before);
	CHECK_VALID(l);
}

TEST(Dpi_LoadScalesAFileSavedAtAnotherDpi) {
	DockLayout l;
	BuildStandard(l);
	std::string text = l.Save();		// 96 DPI

	DockLayout hi;
	hi.SetDpi(192);
	Register(hi);
	CHECK(hi.Load(text));
	CHECK_DUMP(hi, L"main: V(H(T[Sol]@500 D[a.cpp] T[Props]@400) T[Output]@300)\nhidden: Ext Pinned b.cpp c.cpp");
	CHECK(hi.FindPane(L"Ext")->PreferredSize.cx == 360);

	// the file remembers its DPI, so it can travel back
	DockLayout back;
	Register(back);
	CHECK(back.Load(hi.Save()));
	CHECK_STR(back.Dump(), l.Dump());

	CHECK(!back.Load(R"({"version": 1, "dpi": 5, "main": {"type": "split", "axis": "h", "children": [{"type": "group", "kind": "document", "panes": []}]}})"));
}

// ---- Chrome geometry -------------------------------------------------------------

TEST(Geometry_ToolGroupWithASinglePaneHasACaptionOnly) {
	DockLayout l;
	Panes p = BuildStandard(l);
	const auto parts = ComputeGroupParts(*p.Sol->Group(), { 0, 0, 250, 400 }, DockMetrics::ForDpi(96));
	CHECK(parts.HasCaption && !parts.HasTabs);
	CHECK_RECT(parts.Caption, 0, 0, 250, 22);
	CHECK_RECT(parts.Content, 0, 22, 250, 400);
}

TEST(Geometry_ToolGroupTabsGoAtTheBottomOrBelowTheCaption) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	const auto m = DockMetrics::ForDpi(96);

	auto parts = ComputeGroupParts(*p.Sol->Group(), { 0, 0, 250, 400 }, m);
	CHECK(parts.HasCaption && parts.HasTabs);
	CHECK_RECT(parts.Tabs, 0, 376, 250, 400);
	CHECK_RECT(parts.Content, 0, 22, 250, 376);

	p.Sol->Group()->TabsAtBottom = false;
	parts = ComputeGroupParts(*p.Sol->Group(), { 0, 0, 250, 400 }, m);
	CHECK_RECT(parts.Tabs, 0, 22, 250, 46);
	CHECK_RECT(parts.Content, 0, 46, 250, 400);
}

TEST(Geometry_DocumentGroupsHaveTabsButNoCaption) {
	DockLayout l;
	Panes p = BuildStandard(l);
	const auto parts = ComputeGroupParts(*p.A->Group(), { 10, 20, 510, 320 }, DockMetrics::ForDpi(96));
	CHECK(!parts.HasCaption && parts.HasTabs);
	CHECK_RECT(parts.Tabs, 10, 20, 510, 44);
	CHECK_RECT(parts.Content, 10, 44, 510, 320);
}

TEST(Geometry_TheOnlyGroupOfAFloatHasNoCaption) {
	DockLayout l;
	Panes p = BuildStandard(l);
	const auto m = DockMetrics::ForDpi(96);
	CHECK(l.Float(p.Props, { 0, 0, 300, 300 }));
	auto parts = ComputeGroupParts(*p.Props->Group(), { 0, 0, 280, 260 }, m);
	CHECK(!parts.HasCaption && !parts.HasTabs);
	CHECK_RECT(parts.Content, 0, 0, 280, 260);

	// with tabs: just the strip
	CHECK(l.DockTo(p.Sol, p.Props->Group(), DockPosition::Tab));
	parts = ComputeGroupParts(*p.Props->Group(), { 0, 0, 280, 260 }, m);
	CHECK(!parts.HasCaption && parts.HasTabs);
	CHECK_RECT(parts.Tabs, 0, 236, 280, 260);

	// two groups in the window: each keeps its caption
	CHECK(l.DockToEdge(p.Output, DockSide::Bottom, l.Floats()[0].get()));
	parts = ComputeGroupParts(*p.Output->Group(), { 0, 0, 280, 100 }, m);
	CHECK(parts.HasCaption);
	parts = ComputeGroupParts(*p.Props->Group(), { 0, 0, 280, 100 }, m);
	CHECK(parts.HasCaption);
}

TEST(Geometry_TinyGroupsNeverGoNegative) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
	for (int h : { 0, 5, 30, 60 }) {
		const auto parts = ComputeGroupParts(*p.Sol->Group(), { 0, 0, 100, h }, DockMetrics::ForDpi(96));
		CHECK(Height(parts.Caption) >= 0 && Height(parts.Tabs) >= 0 && Height(parts.Content) >= 0);
		CHECK(Height(parts.Caption) + Height(parts.Tabs) + Height(parts.Content) <= h);
	}
}

// ---- Tab strip -----------------------------------------------------------------

static std::vector<TabSpec> Specs(int count, int textWidth = 50, bool icon = false, bool closable = false) {
	return std::vector<TabSpec>(count, TabSpec{ textWidth, icon, closable });
}

TEST(TabStrip_TabsFollowEachOtherWhenTheyAllFit) {
	const auto m = DockMetrics::ForDpi(96);
	const auto strip = LayoutTabStrip(Specs(3), { 0, 0, 400, 24 }, m, 0, 0);
	CHECK(!strip.Overflow && strip.First == 0 && strip.Tabs.size() == 3);
	CHECK_RECT(strip.Tabs[0], 0, 0, 70, 24);
	CHECK_RECT(strip.Tabs[1], 71, 0, 141, 24);
	CHECK_RECT(strip.Tabs[2], 142, 0, 212, 24);
	CHECK(IsRectEmpty(&strip.Close[0]));
	CHECK(IsRectEmpty(&strip.OverflowButton));
	CHECK(LayoutTabStrip({}, { 0, 0, 100, 24 }, m, 0, -1).Tabs.empty());
}

TEST(TabStrip_IconsAndCloseButtonsMakeTabsWider) {
	const auto m = DockMetrics::ForDpi(96);
	const TabSpec spec{ 50, true, true };
	CHECK(TabWidth(spec, m) == 10 + 20 + 50 + 25);
	const auto strip = LayoutTabStrip({ spec }, { 0, 0, 400, 24 }, m, 0, 0);
	CHECK_RECT(strip.Tabs[0], 0, 0, 105, 24);
	CHECK_RECT(strip.Close[0], 84, 4, 100, 20);
	CHECK_RECT(TabIconRect(strip.Tabs[0], m), 10, 4, 26, 20);
	CHECK_RECT(TabTextRect(strip.Tabs[0], spec, m), 30, 0, 80, 24);
}

TEST(TabStrip_OverflowShowsAWholeRunAndAButton) {
	const auto m = DockMetrics::ForDpi(96);
	const RECT area{ 0, 0, 250, 24 };
	const auto strip = LayoutTabStrip(Specs(5), area, m, 0, 0);		// 5 * 70 + 4 = 354 > 250
	CHECK(strip.Overflow && strip.First == 0 && strip.Tabs.size() == 3);
	CHECK_RECT(strip.OverflowButton, 226, 0, 250, 24);
	CHECK(strip.Tabs.back().right <= strip.OverflowButton.left);
}

TEST(TabStrip_TheActiveTabIsKeptInView) {
	const auto m = DockMetrics::ForDpi(96);
	const RECT area{ 0, 0, 250, 24 };

	auto strip = LayoutTabStrip(Specs(5), area, m, 0, 4);
	CHECK(strip.First == 2 && strip.Tabs.size() == 3);		// 2, 3, 4

	strip = LayoutTabStrip(Specs(5), area, m, 3, 1);			// scrolled past it: come back
	CHECK(strip.First == 1);

	strip = LayoutTabStrip(Specs(5), area, m, 4, -1);			// no room is wasted after the last tab
	CHECK(strip.First == 2);

	strip = LayoutTabStrip(Specs(5), area, m, 1, -1);			// free scrolling is respected
	CHECK(strip.First == 1);
}

TEST(TabStrip_ATabWiderThanTheStripIsCutOff) {
	const auto m = DockMetrics::ForDpi(96);
	const auto strip = LayoutTabStrip(Specs(1, 400, false, true), { 0, 0, 100, 24 }, m, 0, 0);
	CHECK(strip.Overflow && strip.Tabs.size() == 1);
	CHECK_RECT(strip.Tabs[0], 0, 0, 76, 24);
	CHECK(IsRectEmpty(&strip.Close[0]));
}

TEST(Geometry_CloseButtonSitsInTheCaption) {
	const auto m = DockMetrics::ForDpi(96);
	CHECK_RECT(CloseButtonRect({ 0, 0, 250, 22 }, m), 230, 3, 246, 19);
	const auto small = CloseButtonRect({ 0, 0, 250, 10 }, m);
	CHECK(Height(small) == 10 && small.right == 246);
}

TEST(Metrics_ScaleWithTheDpi) {
	const auto m = DockMetrics::ForDpi(192);
	CHECK(m.CaptionHeight == 44 && m.TabHeight == 48 && m.SplitterThickness == 10);
	const auto lm = m.ToLayoutMetrics();
	CHECK(lm.SplitterThickness == 10 && lm.AutoHideBarThickness == 48 && lm.MinGroupSize.cx == 160);
	CHECK(DockTheme::Light().GroupBack != DockTheme::Dark().GroupBack);
}

// ---- Drag and drop ----------------------------------------------------------------

// A drag context for a layout that has been arranged: "screen" coordinates are just the model's.
static DropContext MakeDrop(DockLayout& l, DockPane* pane, bool whole, DockGroup* hover) {
	const auto m = DockMetrics::ForDpi(96);
	DropContext c;
	c.Layout = &l;
	c.Pane = pane;
	c.WholeGroup = whole;
	c.Metrics = m;
	c.MainBounds = l.Root().Rect;
	c.Ghost = { 300, 300, 600, 600 };
	if (hover) {
		c.Hover = hover;
		c.HoverRect = hover->Rect;
		const RECT local{ 0, 0, Width(hover->Rect), Height(hover->Rect) };
		const auto parts = ComputeGroupParts(*hover, local, m);
		auto shifted = [&](RECT r) {
			OffsetRect(&r, hover->Rect.left, hover->Rect.top);
			return r;
		};
		if (parts.HasCaption)
			c.HoverZones.push_back(shifted(parts.Caption));
		if (parts.HasTabs) {
			c.HoverZones.push_back(shifted(parts.Tabs));
			std::vector<TabSpec> specs(hover->Panes().size(), TabSpec{ 50, false, false });
			for (auto& tab : LayoutTabStrip(specs, parts.Tabs, m, 0, 0).Tabs)
				c.HoverTabs.push_back(shifted(tab));
		}
	}
	return c;
}

static POINT CenterPoint(const RECT& r) {
	return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

static int CountKind(const std::vector<Guide>& guides, DropTarget::Kind kind) {
	int n = 0;
	for (auto& g : guides)
		n += g.Target.Type == kind;
	return n;
}

TEST(Drop_ATabOverAnotherToolGroupIsOfferedTheWholeCompassAndTheEdges) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	const auto c = MakeDrop(l, p.Sol, false, p.Props->Group());
	const auto guides = BuildGuides(c);
	CHECK(guides.size() == 9);
	CHECK(CountKind(guides, DropTarget::Kind::Tab) == 1 && CountKind(guides, DropTarget::Kind::Side) == 4 && CountKind(guides, DropTarget::Kind::Edge) == 4);

	// the compass: the centre is the middle of the group, the arms are one step (a marker and a gap) away
	const POINT centre = CenterPoint(p.Props->Group()->Rect);
	CHECK(guides[0].Target.Type == DropTarget::Kind::Tab);
	CHECK_RECT(guides[0].Rect, centre.x - 16, centre.y - 16, centre.x + 16, centre.y + 16);
	CHECK(guides[1].Target.Position == DockPosition::Left);
	CHECK_RECT(guides[1].Rect, centre.x - 36 - 16, centre.y - 16, centre.x - 36 + 16, centre.y + 16);
	CHECK(guides[4].Target.Position == DockPosition::Bottom);
	CHECK_RECT(guides[4].Rect, centre.x - 16, centre.y + 36 - 16, centre.x + 16, centre.y + 36 + 16);
}

TEST(Drop_ToolPanesCannotBeTabsOfTheDocuments) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	const auto guides = BuildGuides(MakeDrop(l, p.Sol, false, p.A->Group()));
	CHECK(CountKind(guides, DropTarget::Kind::Tab) == 0);
	CHECK(CountKind(guides, DropTarget::Kind::Side) == 4);		// but they can go beside them
	CHECK(CountKind(guides, DropTarget::Kind::Edge) == 4);
}

TEST(Drop_DocumentsOnlyGoToOtherDocumentGroupsAndNeverToAnEdge) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.B);
	CHECK(l.DockTo(p.B, p.A->Group(), DockPosition::Right));
	l.Arrange(Client);

	auto guides = BuildGuides(MakeDrop(l, p.A, false, p.B->Group()));
	CHECK(guides.size() == 5);
	CHECK(CountKind(guides, DropTarget::Kind::Tab) == 1 && CountKind(guides, DropTarget::Kind::Edge) == 0);
	CHECK(BuildGuides(MakeDrop(l, p.A, false, p.Sol->Group())).empty());		// not into a tool group
	CHECK(BuildGuides(MakeDrop(l, p.A, false, nullptr)).empty());				// and no edge guides for a document
}

TEST(Drop_AGroupIsNotOfferedItselfButASingleTabMayLeaveItsGroup) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	// dragged as a whole over itself: nothing to do there, only the edges are left
	auto guides = BuildGuides(MakeDrop(l, p.Props, true, p.Props->Group()));
	CHECK(guides.size() == 4 && CountKind(guides, DropTarget::Kind::Edge) == 4);
	// the only tab of a group over its own group: same
	guides = BuildGuides(MakeDrop(l, p.Props, false, p.Props->Group()));
	CHECK(guides.size() == 4);

	// one of two tabs over its own group: it can be split off, but "as a tab" would change nothing
	l.DockTo(p.Output, p.Props->Group(), DockPosition::Tab);
	l.Arrange(Client);
	guides = BuildGuides(MakeDrop(l, p.Output, false, p.Props->Group()));
	CHECK(CountKind(guides, DropTarget::Kind::Tab) == 0 && CountKind(guides, DropTarget::Kind::Side) == 4);
}

TEST(Drop_PickingATargetGivesItsPreview) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto c = MakeDrop(l, p.Sol, false, p.Props->Group());
	const auto guides = BuildGuides(c);
	const RECT hover = p.Props->Group()->Rect;

	auto pick = [&](const Guide& g) { return PickTarget(c, guides, CenterPoint(g.Rect)); };
	auto tab = pick(guides[0]);
	CHECK(tab.Type == DropTarget::Kind::Tab && tab.Group == p.Props->Group());
	CHECK(EqualRect(&tab.Preview, &hover) != FALSE);

	auto left = pick(guides[1]);
	CHECK(left.Type == DropTarget::Kind::Side && left.Position == DockPosition::Left);
	CHECK_RECT(left.Preview, hover.left, hover.top, hover.left + Width(hover) / 2, hover.bottom);
	auto bottom = pick(guides[4]);
	CHECK_RECT(bottom.Preview, hover.left, hover.bottom - Height(hover) / 2, hover.right, hover.bottom);

	// an edge: a strip as wide (or high) as the pane likes to be, along that side of the whole docking area
	auto edge = pick(guides[5]);
	CHECK(edge.Type == DropTarget::Kind::Edge && edge.Edge == DockSide::Left);
	CHECK_RECT(edge.Preview, 0, 0, 250, 600);
	auto bottomEdge = pick(guides[8]);
	CHECK(bottomEdge.Edge == DockSide::Bottom);
	CHECK_RECT(bottomEdge.Preview, 0, 300, 1000, 600);
}

TEST(Drop_TheCaptionAndTheTabStripMeanAsATab) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.DockTo(p.Ext, p.Props->Group(), DockPosition::Tab);		// Props + Ext, so there is a tab strip
	l.Arrange(Client);
	const auto c = MakeDrop(l, p.Sol, false, p.Props->Group());
	const auto guides = BuildGuides(c);
	CHECK(c.HoverZones.size() == 2 && c.HoverTabs.size() == 2);

	// in the caption: as the last tab
	auto t = PickTarget(c, guides, CenterPoint(c.HoverZones[0]));
	CHECK(t.Type == DropTarget::Kind::Tab && t.TabIndex == -1);
	// in the strip: at the position of the cursor among the tabs (first tab is at 0; the centres decide)
	const RECT tab0 = c.HoverTabs[0], tab1 = c.HoverTabs[1];
	const int y = (tab0.top + tab0.bottom) / 2;
	CHECK(PickTarget(c, guides, { tab0.left + 2, y }).TabIndex == 0);
	CHECK(PickTarget(c, guides, { (tab0.left + tab0.right) / 2 + 2, y }).TabIndex == 1);
	CHECK(PickTarget(c, guides, { (tab1.left + tab1.right) / 2 + 2, y }).TabIndex == 2);
	CHECK(PickTarget(c, guides, { tab1.right + 1, y }).TabIndex == 2);
}

TEST(Drop_AnywhereElseFloatsWhereTheGhostIs) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Arrange(Client);
	auto c = MakeDrop(l, p.Sol, false, p.Props->Group());
	auto guides = BuildGuides(c);

	auto t = PickTarget(c, guides, { 5000, 5000 });
	CHECK(t.Type == DropTarget::Kind::Float && EqualRect(&t.Preview, &c.Ghost) != FALSE);
	c.Ghost = {};
	CHECK(PickTarget(c, guides, { 5000, 5000 }).Type == DropTarget::Kind::None);

	// Ctrl: no docking at all
	c.Ghost = { 300, 300, 600, 600 };
	c.DockingAllowed = false;
	CHECK(BuildGuides(c).empty());
	CHECK(PickTarget(c, {}, CenterPoint(guides[0].Rect)).Type == DropTarget::Kind::Float);
}

TEST(Drop_ApplyingADropDocksOrFloats) {
	auto drop = [](DockLayout& l, DockPane* pane, bool whole, DockGroup* hover, size_t guide) {
		l.Arrange(Client);
		auto c = MakeDrop(l, pane, whole, hover);
		auto guides = BuildGuides(c);
		auto target = PickTarget(c, guides, CenterPoint(guides[guide].Rect));
		return ApplyDrop(l, pane, whole, target);
	};

	{	// a tab onto another group, as a tab
		DockLayout l;
		Panes p = BuildStandard(l);
		CHECK(drop(l, p.Sol, false, p.Props->Group(), 0));
		CHECK_DUMP(l, L"main: V(H(D[a.cpp] T[Props,>Sol]@200) T[Output]@150)\nhidden: Ext Pinned b.cpp c.cpp");
		CHECK_VALID(l);
	}
	{	// a tab beside a group
		DockLayout l;
		Panes p = BuildStandard(l);
		CHECK(drop(l, p.Sol, false, p.Props->Group(), 3));		// Top
		CHECK(p.Sol->Group()->Side() == DockSide::Right && p.Props->Group()->Parent() == p.Sol->Group()->Parent());
		CHECK_VALID(l);
	}
	{	// a whole group at an edge
		DockLayout l;
		Panes p = BuildStandard(l);
		l.DockTo(p.Props, p.Sol->Group(), DockPosition::Tab);
		CHECK(drop(l, p.Sol, true, p.Output->Group(), 8));		// the bottom edge
		CHECK(p.Sol->Group() == p.Props->Group() && p.Sol->Group()->Side() == DockSide::Bottom);
		CHECK_VALID(l);
	}
	{	// a document as a tab of another document group, at the position of the cursor
		DockLayout l;
		Panes p = BuildStandard(l);
		l.Show(p.B);
		l.DockTo(p.B, p.A->Group(), DockPosition::Right);
		CHECK(drop(l, p.A, false, p.B->Group(), 0));
		CHECK(p.A->Group() == p.B->Group());
		CHECK_VALID(l);
	}
	{	// nothing to do
		DockLayout l;
		Panes p = BuildStandard(l);
		DropTarget none;
		CHECK(!ApplyDrop(l, p.Sol, false, none));
		CHECK(!ApplyDrop(l, p.Ext, false, DropTarget{ DropTarget::Kind::Edge }));	// hidden pane: not placed
	}
	{	// floating
		DockLayout l;
		Panes p = BuildStandard(l);
		DropTarget t;
		t.Type = DropTarget::Kind::Float;
		t.Preview = { 10, 20, 310, 320 };
		CHECK(ApplyDrop(l, p.Sol, false, t));
		CHECK(p.Sol->State() == PaneState::Floating && l.Floats()[0]->Rect().right == 310);
		CHECK(ApplyDrop(l, p.Props, true, t));
		CHECK(l.Floats().size() == 2);
		DockPane* doc = p.A;
		CHECK(!ApplyDrop(l, doc, false, t));		// documents do not float
	}
}

TEST(Model_CanQueriesMatchTheOperations) {
	DockLayout l;
	Panes p = BuildStandard(l);
	l.Show(p.Ext);
	CHECK(l.CanFloatPane(p.Sol) && !l.CanFloatPane(p.A));
	p.Sol->Caps = PaneCaps::None;
	CHECK(!l.CanFloatPane(p.Sol) && !l.Float(p.Sol, { 0, 0, 100, 100 }));
	CHECK(l.CanDockToEdge(p.Props) && !l.CanDockToEdge(p.A) && !l.CanDockToEdge(nullptr));
	CHECK(l.CanMoveGroupToEdge(p.Props->Group()) && !l.CanMoveGroupToEdge(p.A->Group()));

	CHECK(l.CanMoveGroupTo(p.Props->Group(), p.Sol->Group(), DockPosition::Tab));
	CHECK(!l.CanMoveGroupTo(p.Props->Group(), p.Props->Group(), DockPosition::Tab));
	CHECK(!l.CanMoveGroupTo(p.Props->Group(), p.A->Group(), DockPosition::Tab));		// tool group into the documents
	CHECK(l.CanMoveGroupTo(p.Props->Group(), p.A->Group(), DockPosition::Left));		// but beside them
	CHECK(!l.CanMoveGroupTo(p.A->Group(), p.Props->Group(), DockPosition::Left));		// documents stay among documents
	CHECK(!l.CanMoveGroupTo(nullptr, p.A->Group(), DockPosition::Left));
	l.AutoHide(p.Ext->Group());
	CHECK(!l.CanMoveGroupTo(p.Props->Group(), p.Ext->Group(), DockPosition::Tab));		// nothing docks to a group in a bar
}

// ---- Randomized ----------------------------------------------------------------

static void CheckGeometry(const DockNode& n, int line) {
	auto split = n.AsSplit();
	if (!split)
		return;
	for (auto& c : split->Children()) {
		g_checks++;
		if (c->Rect.left < n.Rect.left || c->Rect.right > n.Rect.right || c->Rect.top < n.Rect.top || c->Rect.bottom > n.Rect.bottom ||
			Width(c->Rect) < 0 || Height(c->Rect) < 0) {
			g_failures++;
			wprintf(L"  FAIL  %s:%d  child rectangle escapes its parent\n", g_currentTest, line);
		}
		CheckGeometry(*c, line);
	}
}

TEST(Random_OperationsKeepTheInvariants) {
	std::mt19937 rng(20240925);
	auto pick = [&](size_t n) { return (size_t)(rng() % n); };

	DockLayout l;
	Register(l);
	std::vector<DockPane*> panes;
	for (auto& x : l.Panes())
		panes.push_back(x.get());
	// a few more, to get deeper trees
	for (int i = 0; i < 4; i++) {
		std::wstring id = L"extra" + std::to_wstring(i);
		panes.push_back(AddTool(l, id.c_str(), (DockSide)(i % 4), 150 + i * 20, 120 + i * 10));
	}
	std::vector<std::wstring> ids;
	for (auto x : panes)
		ids.push_back(x->Id());

	int succeeded = 0, saved = 0;
	const RECT big{ 0, 0, 2400, 1800 };
	for (int step = 0; step < 4000; step++) {
		std::vector<DockGroup*> groups;
		l.ForEachGroup([&](DockGroup& g) { groups.push_back(&g); });
		auto anyPane = [&] { return panes[pick(panes.size())]; };
		auto anyGroup = [&] { return groups[pick(groups.size())]; };
		auto anyPosition = [&] { return (DockPosition)pick(5); };
		auto anySide = [&] { return (DockSide)pick(4); };
		auto anyWindow = [&]() -> DockFloat* { return l.Floats().empty() || pick(2) ? nullptr : l.Floats()[pick(l.Floats().size())].get(); };
		RECT rc{ (LONG)pick(200), (LONG)pick(200), 250 + (LONG)pick(300), 250 + (LONG)pick(300) };

		bool ok = false;
		switch (pick(14)) {
			case 0: ok = l.Show(anyPane()); break;
			case 1: ok = l.Hide(anyPane()); break;
			case 2: ok = l.DockTo(anyPane(), anyGroup(), anyPosition(), (int)pick(4) - 1); break;
			case 3: ok = l.DockToEdge(anyPane(), anySide(), anyWindow()); break;
			case 4: ok = l.Float(anyPane(), rc); break;
			case 5: ok = l.FloatGroup(anyGroup(), rc); break;
			case 6: ok = l.AutoHide(anyGroup()); break;
			case 7: ok = l.Unhide(anyGroup()); break;
			case 8: ok = l.MoveGroupTo(anyGroup(), anyGroup(), anyPosition()); break;
			case 9: ok = l.MoveGroupToEdge(anyGroup(), anySide(), anyWindow()); break;
			case 10: ok = l.ReorderTab(anyPane(), (int)pick(4)); break;
			case 11: ok = l.Activate(anyPane()); break;
			case 12: {
				l.Arrange(big);
				auto hits = DockLayout::Splitters(l.Root());
				if (!hits.empty()) {
					auto& h = hits[pick(hits.size())];
					ok = l.ResizeSplitter(h.Split, h.Index, (int)pick(600) - 300);
				}
				break;
			}
			case 13:
				if (!l.Floats().empty())
					ok = l.SetFloatRect(l.Floats()[pick(l.Floats().size())].get(), rc);
				break;
		}
		succeeded += ok;

		std::wstring error;
		if (!l.Validate(&error)) {
			g_checks++;
			g_failures++;
			wprintf(L"  FAIL  step %d: %s\n        %s\n", step, error.c_str(), l.Dump().c_str());
			return;
		}

		l.Arrange(big);
		CheckGeometry(l.Root(), __LINE__);
		for (auto& f : l.Floats()) {
			l.Arrange(*f, { 0, 0, 800, 600 });
			CheckGeometry(f->Root(), __LINE__);
		}

		if (step % 40 == 0) {
			// a save/load round trip must reproduce the layout exactly
			std::string text = l.Save();
			DockLayout copy;
			for (auto x : panes) {
				PaneDesc d;
				d.Id = x->Id();
				d.Kind = x->Kind();
				d.Caps = x->Caps;
				copy.AddPane(d);
			}
			std::wstring loadError;
			g_checks++;
			if (!copy.Load(text, {}, &loadError)) {
				g_failures++;
				wprintf(L"  FAIL  step %d: reload failed: %s\n%S\n", step, loadError.c_str(), text.c_str());
				return;
			}
			CHECK_STR(copy.Dump(), l.Dump());
			CHECK(copy.Save() == text);
			CHECK_VALID(copy);
			saved++;
		}
	}
	wprintf(L"        (%d of 4000 operations applied, %d round trips)\n", succeeded, saved);
	CHECK(succeeded > 1000);
}

int wmain() {
	wprintf(L"WTLDock layout model tests\n\n");

	Run_New_LayoutHasAnEmptyDocumentArea();
	Run_AddPane_RejectsEmptyAndDuplicateIds();
	Run_Show_PlacesPanesAtTheirDefaultEdges();
	Run_Show_OnAPlacedPaneJustActivatesIt();

	Run_Arrange_StandardLayoutGeometry();
	Run_Arrange_PxNodesKeepTheirSizeWhenTheWindowGrows();
	Run_Arrange_ShrinksPxNodesBeforeStarNodesGoBelowTheirMinimum();
	Run_Arrange_SqueezesEverythingWhenTheMinimumsDoNotFit();
	Run_Arrange_PaneMinSizeRaisesTheGroupMinimum();
	Run_Arrange_AutoHideBarsTakeSpaceFromTheClientArea();
	Run_Splitters_ListedBetweenSiblings();

	Run_ResizeSplitter_MovesTheBoundary();
	Run_ResizeSplitter_ClampsToMinimumSizes();
	Run_ResizeSplitter_BetweenStarSiblingsKeepsTheOtherOnesStill();
	Run_ResizeSplitter_RejectsBadArguments();

	Run_DockTo_TabMergesPanesIntoOneGroup();
	Run_DockTo_TabOntoTheOwnGroupReorders();
	Run_DockTo_SplitAcrossTheParentAxisWrapsTheTarget();
	Run_DockTo_SplitAlongTheParentAxisSharesThePxTargetsSpace();
	Run_DockTo_ToolGroupBesideTheDocumentGroup();
	Run_DockTo_SplittingAPaneOffItsOwnGroup();
	Run_DockToEdge_UsesTheOuterEdgeOfTheRoot();
	Run_DockTo_DocumentRules();
	Run_Documents_TabAndSplit();
	Run_Documents_TheLastDocumentGroupIsNeverRemoved();

	Run_Hide_CollapsesEmptyGroupsAndSplits();
	Run_Show_RestoresTheLastPositionAndSize();
	Run_Show_RestoresDocumentsAndFloatsAndAutoHide();
	Run_Activate_SwitchesTabs();
	Run_RemovePane_HidesAndUnregisters();

	Run_Float_MovesAPaneIntoItsOwnWindow();
	Run_Float_OnePaneOutOfATabGroup();
	Run_Float_WholeGroupKeepsItsTabs();
	Run_Float_RespectsCapabilities();
	Run_Float_HiddenPaneCanBeFloatedDirectly();
	Run_Float_DockInsideAFloatingWindow();
	Run_Float_TheWindowGoesAwayWhenItsLastPaneLeaves();
	Run_Float_SetRect();

	Run_MoveGroupTo_TabMergesAllPanes();
	Run_MoveGroupTo_SplitMovesTheGroupNode();
	Run_MoveGroupToEdge_KeepsTabsAndLength();

	Run_AutoHide_CollapsesAGroupIntoTheBar();
	Run_AutoHide_Unhide_DocksAtTheEdgeOfTheSameSide();
	Run_AutoHide_Rules();
	Run_AutoHide_APaneCanBeDockedBackIntoAGroup();

	Run_Save_LoadRoundTrips();
	Run_Load_RestoresRememberedPlacementOfHiddenPanes();
	Run_Load_DropsUnknownPanesAndHidesUnlistedOnes();
	Run_Load_FactoryCreatesMissingPanes();
	Run_Load_DropsPanesOfTheWrongKindAndDuplicates();
	Run_Load_DocumentGroupsInFloatsAreDropped();
	Run_Load_RejectsBadInputAndLeavesTheLayoutAlone();
	Run_Load_JsonEscapesAndUnicode();
	Run_ChangeHandler_FiresOncePerOperation();

	Run_Dpi_AddPaneScalesTheDescription();
	Run_Dpi_SetDpiScalesEveryPixelSize();
	Run_Dpi_LoadScalesAFileSavedAtAnotherDpi();

	Run_Geometry_ToolGroupWithASinglePaneHasACaptionOnly();
	Run_Geometry_ToolGroupTabsGoAtTheBottomOrBelowTheCaption();
	Run_Geometry_DocumentGroupsHaveTabsButNoCaption();
	Run_Geometry_TheOnlyGroupOfAFloatHasNoCaption();
	Run_Geometry_TinyGroupsNeverGoNegative();
	Run_TabStrip_TabsFollowEachOtherWhenTheyAllFit();
	Run_TabStrip_IconsAndCloseButtonsMakeTabsWider();
	Run_TabStrip_OverflowShowsAWholeRunAndAButton();
	Run_TabStrip_TheActiveTabIsKeptInView();
	Run_TabStrip_ATabWiderThanTheStripIsCutOff();
	Run_Geometry_CloseButtonSitsInTheCaption();
	Run_Metrics_ScaleWithTheDpi();

	Run_Drop_ATabOverAnotherToolGroupIsOfferedTheWholeCompassAndTheEdges();
	Run_Drop_ToolPanesCannotBeTabsOfTheDocuments();
	Run_Drop_DocumentsOnlyGoToOtherDocumentGroupsAndNeverToAnEdge();
	Run_Drop_AGroupIsNotOfferedItselfButASingleTabMayLeaveItsGroup();
	Run_Drop_PickingATargetGivesItsPreview();
	Run_Drop_TheCaptionAndTheTabStripMeanAsATab();
	Run_Drop_AnywhereElseFloatsWhereTheGhostIs();
	Run_Drop_ApplyingADropDocksOrFloats();
	Run_Model_CanQueriesMatchTheOperations();

	Run_Random_OperationsKeepTheInvariants();

	RunUiTests();

	wprintf(L"\n%d checks, %d failed\n", g_checks, g_failures);
	return g_failures;
}
