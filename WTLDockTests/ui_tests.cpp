// Tests for the docking window (CDockHost): real windows, created off-screen. After every operation the windows
// are compared with what the layout model says they should look like.

#include <atlbase.h>
#include <atlapp.h>

CAppModule _Module;

#include <atlwin.h>
#include <atlgdi.h>

#include <map>
#include <random>
#include <set>

#include "Harness.h"
#include "WTLDockUI.h"

using namespace WTLDock;

namespace {

void Pump() {
	MSG msg;
	while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
}

RECT RectIn(HWND window, HWND parent) {
	RECT rc;
	::GetWindowRect(window, &rc);
	::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
	return rc;
}

std::wstring ClassOf(HWND window) {
	wchar_t name[64]{};
	::GetClassNameW(window, name, _countof(name));
	return name;
}

struct Fixture {
	explicit Fixture(int width = 1000, int height = 600) {
		Frame = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"dock test frame", WS_POPUP | WS_VISIBLE,
			-12000, 0, width, height, nullptr, nullptr, nullptr, nullptr);
		::ShowWindow(Frame, SW_SHOWNA);
		RECT rc{ 0, 0, width, height };
		Host.Create(Frame, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
		Host.SetKeepFloatsOnScreen(false);
		Pump();
	}

	~Fixture() {
		if (Host.IsWindow())
			Host.DestroyWindow();
		::DestroyWindow(Frame);
		Pump();
	}

	DockPane* Add(const wchar_t* id, PaneKind kind, DockSide side = DockSide::Left, int cx = 250, int cy = 250) {
		PaneDesc d;
		d.Id = d.Title = id;
		d.Kind = kind;
		d.DefaultSide = side;
		d.PreferredSize = { cx, cy };
		d.hWnd = ::CreateWindowExW(0, L"STATIC", id, WS_CHILD, 0, 0, 10, 10, Host, nullptr, nullptr, nullptr);
		return Host.Layout().AddPane(d);
	}

	void AddStandard() {
		Sol = Add(L"Sol", PaneKind::Tool, DockSide::Left, 250);
		Props = Add(L"Props", PaneKind::Tool, DockSide::Right, 200);
		Output = Add(L"Output", PaneKind::Tool, DockSide::Bottom, 300, 150);
		A = Add(L"a.cpp", PaneKind::Document);
		B = Add(L"b.cpp", PaneKind::Document);
		auto& l = Host.Layout();
		l.Show(Sol);
		l.Show(Props);
		l.Show(Output);
		l.Show(A);
		l.Show(B);
	}

	HWND Frame{};
	CDockHost Host;
	DockPane *Sol{}, *Props{}, *Output{}, *A{}, *B{};
};

// The tab strip of a group, worked out the way the group window does it.
TabStrip StripOf(Fixture& f, DockGroup* group) {
	HWND gw = f.Host.GroupWindow(group);
	RECT client;
	::GetClientRect(gw, &client);
	const auto& metrics = f.Host.Metrics();
	const auto parts = ComputeGroupParts(*group, client, metrics);
	CClientDC dc(gw);
	dc.SelectFont(f.Host.Font());
	std::vector<TabSpec> specs;
	for (auto p : group->Panes()) {
		SIZE size{};
		dc.GetTextExtent(p->Title.c_str(), (int)p->Title.size(), &size);
		specs.push_back({ size.cx, p->Icon != nullptr, group->IsDocument() && Has(p->Caps, PaneCaps::CanClose) });
	}
	return LayoutTabStrip(specs, parts.Tabs, metrics, f.Host.GetTabState(group).First, group->ActiveIndex());
}

LPARAM Pt(int x, int y) {
	return MAKELPARAM(x, y);
}

LPARAM Center(const RECT& r) {
	return Pt((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

int GuideWindows();

// The floating windows (frames) owned by the fixture's top-level window.
std::vector<HWND> FramesOf(Fixture& f) {
	struct Context {
		HWND Owner;
		std::vector<HWND> Frames;
	} context{ f.Frame, {} };
	::EnumWindows([](HWND w, LPARAM lp) -> BOOL {
		auto c = reinterpret_cast<Context*>(lp);
		if (ClassOf(w) == L"WTLDock_Float" && ::GetWindow(w, GW_OWNER) == c->Owner)
			c->Frames.push_back(w);
		return TRUE;
	}, reinterpret_cast<LPARAM>(&context));
	return context.Frames;
}

// A rectangle for a floating window that is nowhere near a screen.
RECT OffScreen(int left, int top, int width, int height) {
	return { -12000 + left, top, -12000 + left + width, top + height };
}

// Compares every window with the model.
void Verify(Fixture& f, int line) {
	Pump();
	if (!f.Host.IsDragging())
		Check(GuideWindows() == 0, "no guide windows outside a drag", line);
	auto& layout = f.Host.Layout();
	std::set<HWND> expectedGroups;

	// the floating windows: one frame per float, owned by the top-level window, where the layout says
	std::set<HWND> expectedFrames;
	for (auto& fl : layout.Floats()) {
		HWND frame = f.Host.FloatWindow(fl->Id());
		Check(frame != nullptr, "every floating tree has a frame", line);
		if (!frame)
			continue;
		expectedFrames.insert(frame);
		Check(::IsWindowVisible(frame) != FALSE, "frames are visible", line);
		Check(::GetWindow(frame, GW_OWNER) == f.Frame, "frames are owned by the top-level window", line);
		RECT actual;
		::GetWindowRect(frame, &actual);
		const RECT expected = fl->Rect();
		Check(EqualRect(&actual, &expected) != FALSE, "the frame is where the layout says", line);
	}
	for (HWND frame : FramesOf(f))
		Check(expectedFrames.contains(frame), "no stale frames", line);

	layout.ForEachGroup([&](DockGroup& g) {
		if (g.Location() == GroupLocation::AutoHide)
			return;
		HWND surface = g.Location() == GroupLocation::Main ? f.Host.m_hWnd : f.Host.FloatWindow(g.Float()->Id());
		HWND w = f.Host.GroupWindow(&g);
		Check(w != nullptr, "every shown group has a window", line);
		if (!w || !surface)
			return;
		expectedGroups.insert(w);
		Check(::GetParent(w) == surface, "group windows are children of their surface", line);
		Check(::IsWindowVisible(w) != FALSE, "group windows are visible", line);
		const RECT actual = RectIn(w, surface);
		Check(EqualRect(&actual, &g.Rect) != FALSE, "group window rectangle equals the model's", line);
	});

	for (HWND c = ::GetWindow(f.Host, GW_CHILD); c; c = ::GetWindow(c, GW_HWNDNEXT)) {
		if (ClassOf(c) == L"WTLDock_Group")
			Check(expectedGroups.contains(c), "no stale group windows", line);
		else
			Check(!::IsWindowVisible(c), "content parked in the host is hidden", line);
	}
	for (HWND frame : expectedFrames) {
		for (HWND c = ::GetWindow(frame, GW_CHILD); c; c = ::GetWindow(c, GW_HWNDNEXT))
			Check(ClassOf(c) == L"WTLDock_Group" && expectedGroups.contains(c), "frames hold only live group windows", line);
	}

	for (auto& p : layout.Panes()) {
		if (!p->hWnd)
			continue;
		Check(::IsWindow(p->hWnd) != FALSE, "content windows are never destroyed", line);
		auto g = p->Group();
		const bool shown = g && g->Location() != GroupLocation::AutoHide;
		if (!shown || g->ActivePane() != p.get()) {
			Check(!::IsWindowVisible(p->hWnd), "content of a pane that is not on show is hidden", line);
			continue;
		}
		HWND gw = f.Host.GroupWindow(g);
		Check(::IsWindowVisible(p->hWnd) != FALSE, "content of the active pane is visible", line);
		Check(::GetParent(p->hWnd) == gw, "content is a child of its group window", line);
		RECT client;
		::GetClientRect(gw, &client);
		const RECT expected = ComputeGroupParts(*g, client, f.Host.Metrics()).Content;
		const RECT actual = RectIn(p->hWnd, gw);
		Check(EqualRect(&actual, &expected) != FALSE, "content fills the content area of its group", line);
	}
}

#define VERIFY(f) Verify((f), __LINE__)

// ---- a window that records what it is sent ---------------------------------------

struct Sink {
	static inline UINT LastMessage = 0;
	static inline WPARAM LastWParam = 0;
	static inline int Count = 0;

	static LRESULT CALLBACK Proc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
		if (msg == WM_COMMAND || msg == WM_NOTIFY || msg == WM_CTLCOLORSTATIC) {
			LastMessage = msg;
			LastWParam = wp;
			Count++;
			return 77;
		}
		return ::DefWindowProc(hWnd, msg, wp, lp);
	}

	static HWND Create() {
		static bool registered = [] {
			WNDCLASSW wc{};
			wc.lpfnWndProc = Proc;
			wc.hInstance = ::GetModuleHandle(nullptr);
			wc.lpszClassName = L"WTLDockTestSink";
			return ::RegisterClassW(&wc) != 0;
		}();
		(void)registered;
		return ::CreateWindowExW(0, L"WTLDockTestSink", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, ::GetModuleHandle(nullptr), nullptr);
	}
};

// ---- tests -------------------------------------------------------------------------

TEST(Host_PlacesContentInItsGroups) {
	Fixture f;
	f.AddStandard();
	VERIFY(f);
	CHECK(f.Host.GroupWindow(f.Sol->Group()) != nullptr);
	CHECK(f.Host.GroupWindow(f.Sol->Group()) != f.Host.GroupWindow(f.Props->Group()));
	CHECK(f.Host.GroupWindow(f.A->Group()) == f.Host.GroupWindow(f.B->Group()));
	// b.cpp was shown last, so it is the visible document
	CHECK(::IsWindowVisible(f.B->hWnd) && !::IsWindowVisible(f.A->hWnd));
	// 1000 - 2 splitters (5) - 250 - 200 columns
	CHECK(Width(f.A->Group()->Rect) == 1000 - 10 - 450);
}

TEST(Host_HiddenPanesKeepTheirWindows) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.Layout().Hide(f.Sol));
	VERIFY(f);
	CHECK(::IsWindow(f.Sol->hWnd) && !::IsWindowVisible(f.Sol->hWnd));
	CHECK(f.Host.Layout().Show(f.Sol));
	VERIFY(f);
	CHECK(::IsWindowVisible(f.Sol->hWnd) != FALSE);
}

TEST(Host_TabSwitchingShowsOnlyTheActiveContent) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab));
	VERIFY(f);
	CHECK(::IsWindowVisible(f.Props->hWnd) && !::IsWindowVisible(f.Sol->hWnd));
	CHECK(l.Activate(f.Sol));
	VERIFY(f);
	CHECK(::IsWindowVisible(f.Sol->hWnd) && !::IsWindowVisible(f.Props->hWnd));
}

TEST(Host_MovingAPaneReparentsItsContent) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	HWND before = f.Host.GroupWindow(f.Props->Group());
	CHECK(l.DockTo(f.Props, f.A->Group(), DockPosition::Bottom));
	VERIFY(f);
	CHECK(f.Host.GroupWindow(f.Props->Group()) != before);
	CHECK(::GetParent(f.Props->hWnd) == f.Host.GroupWindow(f.Props->Group()));
	CHECK(l.DockToEdge(f.Props, DockSide::Left));
	VERIFY(f);
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab));
	VERIFY(f);
}

TEST(Host_AutoHiddenPanesAreNotShown) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	VERIFY(f);
	CHECK(!::IsWindowVisible(f.Sol->hWnd));
	CHECK(l.Unhide(f.Sol->Group()));
	VERIFY(f);
	CHECK(::IsWindowVisible(f.Sol->hWnd) != FALSE);
}

TEST(Host_ResizingTheHostRelayoutsEverything) {
	Fixture f;
	f.AddStandard();
	::SetWindowPos(f.Frame, nullptr, 0, 0, 1400, 900, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(f.Host, nullptr, 0, 0, 1400, 900, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	VERIFY(f);
	CHECK(Width(f.Sol->Group()->Rect) == 250);
	CHECK(Height(f.Output->Group()->Rect) == 150);
	CHECK(Width(f.A->Group()->Rect) == 1400 - 10 - 450);
	RECT client;
	f.Host.GetClientRect(&client);
	CHECK(Width(client) == 1400 && Height(client) == 900);
}

TEST(Host_DraggingASplitterResizesTheGroups) {
	Fixture f;
	f.AddStandard();
	auto splitters = DockLayout::Splitters(f.Host.Layout().Root());
	SplitterHit hit{};
	for (auto& s : splitters)
		if (s.Split->GetAxis() == Axis::Horizontal && s.Split->Children()[s.Index].get() == f.Sol->Group())
			hit = s;
	CHECK(hit.Split != nullptr);
	const int x = (hit.Rect.left + hit.Rect.right) / 2, y = (hit.Rect.top + hit.Rect.bottom) / 2;

	::SendMessage(f.Host, WM_LBUTTONDOWN, MK_LBUTTON, Pt(x, y));
	for (int dx : { 10, 30, 60 })
		::SendMessage(f.Host, WM_MOUSEMOVE, MK_LBUTTON, Pt(x + dx, y));
	::SendMessage(f.Host, WM_LBUTTONUP, 0, Pt(x + 60, y));
	VERIFY(f);
	CHECK(Width(f.Sol->Group()->Rect) == 310);
	CHECK(Width(f.Props->Group()->Rect) == 200);

	// moves after the button went up do nothing
	::SendMessage(f.Host, WM_MOUSEMOVE, 0, Pt(x + 200, y));
	CHECK(Width(f.Sol->Group()->Rect) == 310);
}

TEST(Host_CloseButtonHidesTheActivePane) {
	Fixture f;
	f.AddStandard();
	HWND gw = f.Host.GroupWindow(f.Sol->Group());
	RECT client;
	::GetClientRect(gw, &client);
	const RECT parts = ComputeGroupParts(*f.Sol->Group(), client, f.Host.Metrics()).Caption;
	const RECT button = CloseButtonRect(parts, f.Host.Metrics());
	const int x = (button.left + button.right) / 2, y = (button.top + button.bottom) / 2;

	// pressing and releasing elsewhere does nothing
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(x, y));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(x - 100, y));
	CHECK(f.Sol->State() == PaneState::Docked);

	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(x, y));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(x, y));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Hidden);
	CHECK(!::IsWindowVisible(f.Sol->hWnd));

	// a pane without the capability has no button
	f.Props->Caps = PaneCaps::None;
	gw = f.Host.GroupWindow(f.Props->Group());
	::GetClientRect(gw, &client);
	const RECT b2 = CloseButtonRect(ComputeGroupParts(*f.Props->Group(), client, f.Host.Metrics()).Caption, f.Host.Metrics());
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt((b2.left + b2.right) / 2, (b2.top + b2.bottom) / 2));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt((b2.left + b2.right) / 2, (b2.top + b2.bottom) / 2));
	CHECK(f.Props->State() == PaneState::Docked);
}

TEST(Host_ClickingATabActivatesThePane) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);
	CHECK(f.Sol->Group()->ActivePane() == f.Props);

	HWND gw = f.Host.GroupWindow(f.Sol->Group());
	RECT client;
	::GetClientRect(gw, &client);
	const auto tabs = StripOf(f, f.Sol->Group()).Tabs;
	CHECK(tabs.size() == 2);

	// the first tab is Sol
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt((tabs[0].left + tabs[0].right) / 2, (tabs[0].top + tabs[0].bottom) / 2));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt((tabs[0].left + tabs[0].right) / 2, (tabs[0].top + tabs[0].bottom) / 2));
	VERIFY(f);
	CHECK(f.Sol->Group()->ActivePane() == f.Sol);
	CHECK(f.Host.ActivePane() == f.Sol);
	CHECK(f.Host.IsActive(f.Sol->Group()));

	f.Host.ActivatePane(f.Props);
	VERIFY(f);
	CHECK(f.Sol->Group()->ActivePane() == f.Props && f.Host.ActivePane() == f.Props);
}

TEST(Host_ForwardsWhatContentSaysToItsParent) {
	Fixture f;
	f.AddStandard();
	HWND sink = Sink::Create();
	f.Host.SetNotifyTarget(sink);
	HWND parent = ::GetParent(f.Sol->hWnd);
	CHECK(parent == f.Host.GroupWindow(f.Sol->Group()));

	Sink::Count = 0;
	CHECK(::SendMessage(parent, WM_COMMAND, MAKEWPARAM(1234, BN_CLICKED), (LPARAM)f.Sol->hWnd) == 77);
	CHECK(Sink::LastMessage == WM_COMMAND && LOWORD(Sink::LastWParam) == 1234);
	NMHDR nm{ f.Sol->hWnd, 5, 1 };
	CHECK(::SendMessage(parent, WM_NOTIFY, 5, (LPARAM)&nm) == 77);
	CHECK(::SendMessage(parent, WM_CTLCOLORSTATIC, 0, (LPARAM)f.Sol->hWnd) == 77);
	CHECK(Sink::Count == 3);

	f.Host.SetNotifyTarget(nullptr);	// falls back to the host's parent
	CHECK(f.Host.NotifyTarget() == f.Frame);
	::DestroyWindow(sink);
}

TEST(Host_PaintsTheChromeInTheTheme) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().Activate(f.A);		// a.cpp is the first tab of the documents
	f.Host.ActivatePane(f.Sol);
	Pump();
	const DockTheme theme = f.Host.Theme();

	auto capture = [&] {
		RECT rc;
		f.Host.GetClientRect(&rc);
		CClientDC screen(nullptr);
		CDC dc;
		dc.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
		HBITMAP old = dc.SelectBitmap(bmp);
		::PrintWindow(f.Host, dc, PW_CLIENTONLY);
		auto sample = [&](int x, int y) { return dc.GetPixel(x, y); };
		struct Result { COLORREF SolCaption, PropsCaption, Splitter, DocTabAccent; };
		const RECT sol = f.Sol->Group()->Rect, props = f.Props->Group()->Rect, doc = f.A->Group()->Rect;
		Result r{
			sample(sol.left + 2, sol.top + 8),
			sample(props.left + 2, props.top + 8),
			sample(sol.right + 2, 100),
			sample(doc.left + 3, doc.top + 1),
		};
		dc.SelectBitmap(old);
		return r;
	};

	auto r = capture();
	CHECK(r.SolCaption == theme.CaptionActiveBack);
	CHECK(r.PropsCaption == theme.CaptionInactiveBack);
	CHECK(r.Splitter == theme.Splitter);
	CHECK(r.DocTabAccent == theme.TabActiveAccent);

	f.Host.ActivatePane(f.Props);
	Pump();
	r = capture();
	CHECK(r.SolCaption == theme.CaptionInactiveBack);
	CHECK(r.PropsCaption == theme.CaptionActiveBack);

	f.Host.SetTheme(DockTheme::Dark());
	Pump();
	const DockTheme dark = DockTheme::Dark();
	r = capture();
	CHECK(r.PropsCaption == dark.CaptionActiveBack);
	CHECK(r.SolCaption == dark.CaptionInactiveBack);
	CHECK(r.Splitter == dark.Splitter);
}

// ---- tabs ---------------------------------------------------------------------------

struct Docs {
	DockPane *A, *B, *C;
};

Docs AddDocs(Fixture& f) {
	Docs d{ f.Add(L"a.cpp", PaneKind::Document), f.Add(L"b.cpp", PaneKind::Document), f.Add(L"c.cpp", PaneKind::Document) };
	f.Host.Layout().Show(d.A);
	f.Host.Layout().Show(d.B);
	f.Host.Layout().Show(d.C);
	return d;
}

void Click(HWND window, LPARAM pt, UINT down = WM_LBUTTONDOWN, UINT up = WM_LBUTTONUP) {
	::SendMessage(window, down, down == WM_LBUTTONDOWN ? MK_LBUTTON : MK_MBUTTON, pt);
	::SendMessage(window, up, 0, pt);
}

TEST(Host_DocumentTabsHaveCloseButtons) {
	Fixture f;
	Docs d = AddDocs(f);
	auto strip = StripOf(f, d.A->Group());
	CHECK(strip.Tabs.size() == 3 && !IsRectEmpty(&strip.Close[0]) && !IsRectEmpty(&strip.Close[2]));

	// closing a tab that is not the active one leaves the active tab alone
	Click(f.Host.GroupWindow(d.A->Group()), Center(strip.Close[0]));
	VERIFY(f);
	CHECK(d.A->State() == PaneState::Hidden && d.B->State() == PaneState::Document);
	CHECK(d.C->Group()->ActivePane() == d.C);

	// a press that is released somewhere else is not a click
	strip = StripOf(f, d.B->Group());
	HWND gw = f.Host.GroupWindow(d.B->Group());
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.Close[0]));
	::SendMessage(gw, WM_LBUTTONUP, 0, Center(strip.Tabs[1]));
	CHECK(d.B->State() == PaneState::Document);

	Click(gw, Center(strip.Close[0]));
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Hidden && d.C->State() == PaneState::Document);
}

TEST(Host_MiddleClickClosesADocumentTab) {
	Fixture f;
	Docs d = AddDocs(f);
	const auto strip = StripOf(f, d.A->Group());
	Click(f.Host.GroupWindow(d.A->Group()), Center(strip.Tabs[1]), WM_MBUTTONDOWN, WM_MBUTTONUP);
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Hidden && d.A->State() == PaneState::Document && d.C->State() == PaneState::Document);
}

TEST(Host_ToolTabsHaveNoCloseButtons) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);
	const auto strip = StripOf(f, f.Sol->Group());
	CHECK(strip.Tabs.size() == 2 && IsRectEmpty(&strip.Close[0]));
	Click(f.Host.GroupWindow(f.Sol->Group()), Center(strip.Tabs[0]), WM_MBUTTONDOWN, WM_MBUTTONUP);
	CHECK(f.Sol->State() == PaneState::Docked && f.Props->State() == PaneState::Docked);
}

TEST(Host_ClosingCanBeVetoed) {
	Fixture f;
	Docs d = AddDocs(f);
	int closing = 0;
	std::vector<DockPane*> closed;
	f.Host.OnPaneClosing = [&](DockPane* p) { closing++; return p != d.B; };
	f.Host.OnPaneClosed = [&](DockPane* p) { closed.push_back(p); };

	CHECK(!f.Host.ClosePane(d.B));
	CHECK(d.B->State() == PaneState::Document && closing == 1 && closed.empty());
	CHECK(f.Host.ClosePane(d.A));
	CHECK(closed.size() == 1 && closed[0] == d.A);

	// the same goes for the buttons
	const auto strip = StripOf(f, d.B->Group());
	Click(f.Host.GroupWindow(d.B->Group()), Center(strip.Close[0]));
	CHECK(d.B->State() == PaneState::Document && closing == 3);

	// and a pane that cannot be closed is not even asked
	d.C->Caps = PaneCaps::None;
	CHECK(!f.Host.ClosePane(d.C) && closing == 3);
}

TEST(Host_DraggingATabReordersTheGroup) {
	Fixture f;
	Docs d = AddDocs(f);
	DockGroup* g = d.A->Group();
	HWND gw = f.Host.GroupWindow(g);
	auto order = [&] {
		std::wstring s;
		for (auto p : d.A->Group()->Panes())
			s += p->Id() + L" ";
		return s;
	};
	CHECK_STR(order(), L"a.cpp b.cpp c.cpp ");

	// a plain click activates but does not move anything
	Click(gw, Center(StripOf(f, g).Tabs[0]));
	CHECK_STR(order(), L"a.cpp b.cpp c.cpp ");
	CHECK(d.A->Group()->ActivePane() == d.A);

	// drag a.cpp to the right, past the centres of b.cpp and then c.cpp
	auto strip = StripOf(f, g);
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.Tabs[0]));
	const int y = (strip.Tabs[0].top + strip.Tabs[0].bottom) / 2;
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt((strip.Tabs[1].left + strip.Tabs[1].right) / 2 + 3, y));
	CHECK_STR(order(), L"b.cpp a.cpp c.cpp ");
	strip = StripOf(f, g);
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt((strip.Tabs[2].left + strip.Tabs[2].right) / 2 + 3, y));
	CHECK_STR(order(), L"b.cpp c.cpp a.cpp ");
	VERIFY(f);

	// and back to the left
	strip = StripOf(f, g);
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt((strip.Tabs[1].left + strip.Tabs[1].right) / 2 - 3, y));
	CHECK_STR(order(), L"b.cpp a.cpp c.cpp ");
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(0, y));
	CHECK(d.A->Group()->ActivePane() == d.A);

	// after the button went up the mouse does not drag any more
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(900, y));
	CHECK_STR(order(), L"b.cpp a.cpp c.cpp ");
	VERIFY(f);
}

TEST(Host_OverflowKeepsTheActiveTabInView) {
	Fixture f(420, 500);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 8; i++) {
		std::wstring id = L"a-rather-long-document-name-" + std::to_wstring(i) + L".cpp";
		docs.push_back(f.Add(id.c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	DockGroup* g = docs[0]->Group();
	HWND gw = f.Host.GroupWindow(g);

	auto state = f.Host.GetTabState(g);
	CHECK(state.Overflow && state.Visible >= 1 && state.Visible < 8);
	CHECK(state.First + state.Visible == 8);			// the last document is active

	f.Host.ActivatePane(docs[0]);
	state = f.Host.GetTabState(g);
	CHECK(state.First == 0);
	f.Host.ActivatePane(docs[4]);
	state = f.Host.GetTabState(g);
	CHECK(state.First <= 4 && 4 < state.First + state.Visible);
	VERIFY(f);

	// the wheel scrolls without changing the active tab
	f.Host.ActivatePane(docs[0]);
	const auto strip = StripOf(f, g);
	POINT pt{ strip.Tabs[0].left + 5, strip.Tabs[0].top + 5 };
	::ClientToScreen(gw, &pt);
	::SendMessage(gw, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
	CHECK(f.Host.GetTabState(g).First == 1);
	CHECK(docs[0]->Group()->ActivePane() == docs[0]);
	::SendMessage(gw, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
	CHECK(f.Host.GetTabState(g).First == 0);

	// with room for everything there is no overflow
	::SetWindowPos(f.Frame, nullptr, 0, 0, 4000, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	::SetWindowPos(f.Host, nullptr, 0, 0, 4000, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	CHECK(!f.Host.GetTabState(g).Overflow && f.Host.GetTabState(g).Visible == 8);
	VERIFY(f);
}

TEST(Host_CommandsCloseTabsInBulk) {
	Fixture f;
	DockPane* sol = f.Add(L"Sol", PaneKind::Tool, DockSide::Left, 250);
	f.Host.Layout().Show(sol);
	Docs d = AddDocs(f);
	d.C->Caps = PaneCaps::None;				// cannot be closed

	CHECK(f.Host.CanExecute(DockCommand::CloseOthers, d.B));
	CHECK(!f.Host.CanExecute(DockCommand::Close, d.C));
	CHECK(!f.Host.CanExecute(DockCommand::Close, nullptr));
	CHECK(f.Host.CanExecute(DockCommand::AutoHide, sol) && !f.Host.CanExecute(DockCommand::AutoHide, d.A));

	// c.cpp refuses to close, so only a.cpp goes
	CHECK(f.Host.Execute(DockCommand::CloseOthers, d.B));
	VERIFY(f);
	CHECK(d.A->State() == PaneState::Hidden && d.B->State() == PaneState::Document && d.C->State() == PaneState::Document);

	CHECK(f.Host.Execute(DockCommand::CloseAll, d.B));
	CHECK(d.B->State() == PaneState::Hidden && d.C->State() == PaneState::Document);
	CHECK(!f.Host.CanExecute(DockCommand::CloseAll, d.C) && !f.Host.CanExecute(DockCommand::CloseOthers, d.C));
	CHECK(!f.Host.Execute(DockCommand::CloseAll, d.C));

	CHECK(f.Host.Execute(DockCommand::AutoHide, sol));
	CHECK(sol->State() == PaneState::AutoHide);
	VERIFY(f);
}

TEST(Host_ClosingTheActivePaneActivatesANeighbour) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.B);
	CHECK(f.Host.ActivePane() == d.B);
	CHECK(f.Host.ClosePane(d.B));
	CHECK(f.Host.ActivePane() != nullptr && f.Host.ActivePane() != d.B);
	CHECK(f.Host.ActivePane()->Group() == d.C->Group());
	CHECK(f.Host.IsActive(d.C->Group()));
	VERIFY(f);
}

TEST(Host_PanesCanBeRemovedWhileClosing) {
	Fixture f;
	Docs d = AddDocs(f);
	auto& l = f.Host.Layout();
	f.Host.OnPaneClosed = [&](DockPane* p) {
		::DestroyWindow(p->hWnd);
		l.RemovePane(p);
	};
	f.Host.ActivatePane(d.B);
	CHECK(f.Host.ClosePane(d.B));
	CHECK(l.FindPane(L"b.cpp") == nullptr);
	CHECK(f.Host.ActivePane() != nullptr);
	VERIFY(f);

	// removing the active pane behind the host's back does not leave it with a dangling pointer either
	f.Host.ActivatePane(d.A);
	CHECK(l.RemovePane(d.A));
	Pump();		// focus moves to a neighbour (the host follows it) or nowhere
	CHECK(f.Host.ActivePane() == nullptr || f.Host.ActivePane()->Id() != L"a.cpp");
	VERIFY(f);
}

COLORREF PixelOf(Fixture& f, int x, int y) {
	RECT rc;
	f.Host.GetClientRect(&rc);
	CClientDC screen(nullptr);
	CDC dc;
	dc.CreateCompatibleDC(screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
	HBITMAP old = dc.SelectBitmap(bmp);
	::PrintWindow(f.Host, dc, PW_CLIENTONLY);
	const COLORREF color = dc.GetPixel(x, y);
	dc.SelectBitmap(old);
	return color;
}

TEST(Host_HoveringATabHighlightsItAndShowsItsCloseButton) {
	Fixture f;
	Docs d = AddDocs(f);					// c.cpp is active
	const DockTheme theme = f.Host.Theme();
	DockGroup* g = d.A->Group();
	HWND gw = f.Host.GroupWindow(g);
	const auto strip = StripOf(f, g);
	const RECT groupRect = g->Rect;

	// a spot in the tab that is neither text, icon nor close button: its left padding
	const int x = groupRect.left + strip.Tabs[0].left + 2, y = groupRect.top + strip.Tabs[0].top + 12;
	CHECK(PixelOf(f, x, y) == theme.TabInactiveBack);

	::SendMessage(gw, WM_MOUSEMOVE, 0, Center(strip.Tabs[0]));
	CHECK(PixelOf(f, x, y) == theme.TabHotBack);
	// the close button appears on the hot tab: some pixel in it differs from the tab's background
	const RECT close = strip.Close[0];
	bool glyph = false;
	for (int i = close.left; i < close.right && !glyph; i++)
		for (int j = close.top; j < close.bottom && !glyph; j++)
			glyph = PixelOf(f, groupRect.left + i, groupRect.top + j) != theme.TabHotBack;
	CHECK(glyph);

	// moving to the other tab moves the highlight; leaving clears it
	::SendMessage(gw, WM_MOUSEMOVE, 0, Center(strip.Tabs[1]));
	CHECK(PixelOf(f, x, y) == theme.TabInactiveBack);
	::SendMessage(gw, WM_MOUSELEAVE, 0, 0);
	CHECK(PixelOf(f, groupRect.left + strip.Tabs[1].left + 2, y) == theme.TabInactiveBack);

	// the active tab is never "hot": it keeps its own colour
	::SendMessage(gw, WM_MOUSEMOVE, 0, Center(strip.Tabs[2]));
	CHECK(PixelOf(f, groupRect.left + strip.Tabs[2].left + 2, y) == theme.TabActiveBack);
}

TEST(Host_TabIconsAreDrawnAndCountedInTheTabWidth) {
	Fixture f;
	Docs d = AddDocs(f);
	const int plain = Width(StripOf(f, d.A->Group()).Tabs[0]);
	HICON icon = ::LoadIcon(nullptr, IDI_APPLICATION);
	for (auto p : { d.A, d.B, d.C })
		p->Icon = icon;
	f.Host.Sync();
	CHECK(Width(StripOf(f, d.A->Group()).Tabs[0]) == plain + f.Host.Metrics().IconSize + f.Host.Metrics().TabIconGap);
	::RedrawWindow(f.Host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	VERIFY(f);
}

// ---- floating windows ---------------------------------------------------------------

std::wstring TextOf(HWND window) {
	wchar_t text[256]{};
	::GetWindowTextW(window, text, _countof(text));
	return text;
}

RECT ScreenRect(HWND window) {
	RECT rc;
	::GetWindowRect(window, &rc);
	return rc;
}

TEST(Host_FloatingPanesGetAFrameOfTheirOwn) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	const RECT rc = OffScreen(100, 50, 300, 300);
	CHECK(l.Float(f.Props, rc));
	VERIFY(f);

	CHECK(FramesOf(f).size() == 1);
	HWND frame = f.Host.FloatWindow(l.Floats()[0]->Id());
	const RECT actual = ScreenRect(frame);
	CHECK(EqualRect(&actual, &rc) != FALSE);
	CHECK(TextOf(frame) == L"Props");								// the title follows the pane
	CHECK(::IsWindowVisible(f.Props->hWnd) != FALSE);
	CHECK(::GetParent(::GetParent(f.Props->hWnd)) == frame);		// content -> group window -> frame
	CHECK(f.Host.GroupWindow(f.Sol->Group()) != nullptr);			// the main tree is untouched

	// docking the pane elsewhere takes the frame with it
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab));
	VERIFY(f);
	CHECK(FramesOf(f).empty());
	CHECK(!::IsWindow(frame));
}

TEST(Host_AFloatingTreeCanHoldSeveralGroups) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Props, OffScreen(0, 0, 400, 400)));
	CHECK(l.DockToEdge(f.Output, DockSide::Bottom, l.Floats()[0].get()));
	VERIFY(f);
	CHECK(FramesOf(f).size() == 1);
	CHECK(::GetParent(f.Host.GroupWindow(f.Output->Group())) == f.Host.FloatWindow(l.Floats()[0]->Id()));
	CHECK(::GetParent(f.Host.GroupWindow(f.Props->Group())) == ::GetParent(f.Host.GroupWindow(f.Output->Group())));

	// a splitter of the floating tree can be dragged inside the frame
	auto& fl = *l.Floats()[0];
	HWND frame = f.Host.FloatWindow(fl.Id());
	auto splitters = DockLayout::Splitters(fl.Root());
	CHECK(splitters.size() == 1);
	const RECT sp = splitters[0].Rect;
	const int x = (sp.left + sp.right) / 2, y = (sp.top + sp.bottom) / 2;
	const int before = Height(f.Output->Group()->Rect);
	::SendMessage(frame, WM_LBUTTONDOWN, MK_LBUTTON, Pt(x, y));
	::SendMessage(frame, WM_MOUSEMOVE, MK_LBUTTON, Pt(x, y - 30));
	::SendMessage(frame, WM_LBUTTONUP, 0, Pt(x, y - 30));
	VERIFY(f);
	CHECK(Height(f.Output->Group()->Rect) == before + 30);
}

TEST(Host_MovingOrSizingTheFrameUpdatesTheLayout) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Props, OffScreen(100, 50, 300, 300)));
	HWND frame = f.Host.FloatWindow(l.Floats()[0]->Id());

	RECT moved = OffScreen(300, 120, 420, 340);
	::SetWindowPos(frame, nullptr, moved.left, moved.top, Width(moved), Height(moved), SWP_NOZORDER | SWP_NOACTIVATE);
	VERIFY(f);
	const RECT model = l.Floats()[0]->Rect();
	CHECK(EqualRect(&model, &moved) != FALSE);
	RECT client;
	::GetClientRect(frame, &client);
	CHECK(Width(f.Props->Group()->Rect) == Width(client) && Height(f.Props->Group()->Rect) == Height(client));

	// and the other way round
	const RECT again = OffScreen(50, 10, 350, 260);
	CHECK(l.SetFloatRect(l.Floats()[0].get(), again));
	VERIFY(f);
	const RECT actual = ScreenRect(frame);
	CHECK(EqualRect(&actual, &again) != FALSE);
}

TEST(Host_TheFrameCannotBeSmallerThanItsContent) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Props, OffScreen(0, 0, 300, 300)));
	CHECK(l.DockToEdge(f.Output, DockSide::Bottom, l.Floats()[0].get()));
	HWND frame = f.Host.FloatWindow(l.Floats()[0]->Id());

	MINMAXINFO info{};
	::SendMessage(frame, WM_GETMINMAXINFO, 0, (LPARAM)&info);
	RECT client;
	::GetClientRect(frame, &client);
	RECT window = ScreenRect(frame);
	const int nonClientX = Width(window) - Width(client), nonClientY = Height(window) - Height(client);
	const SIZE min = { l.Metrics().MinGroupSize.cx, 2 * l.Metrics().MinGroupSize.cy + l.Metrics().SplitterThickness };
	CHECK(info.ptMinTrackSize.x >= min.cx + nonClientX);
	CHECK(info.ptMinTrackSize.y >= min.cy + nonClientY);
}

TEST(Host_ClosingTheFrameClosesItsPanes) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Output, f.Props->Group(), DockPosition::Tab);		// Props + Output in one group
	CHECK(l.FloatGroup(f.Props->Group(), OffScreen(0, 0, 300, 300)));
	HWND frame = f.Host.FloatWindow(l.Floats()[0]->Id());

	// a pane that refuses keeps the window
	f.Host.OnPaneClosing = [&](DockPane* p) { return p != f.Output; };
	::SendMessage(frame, WM_CLOSE, 0, 0);
	VERIFY(f);
	CHECK(f.Props->State() == PaneState::Hidden && f.Output->State() == PaneState::Floating);
	CHECK(FramesOf(f).size() == 1 && ::IsWindow(frame));

	f.Host.OnPaneClosing = nullptr;
	::SendMessage(f.Host.FloatWindow(l.Floats()[0]->Id()), WM_CLOSE, 0, 0);
	VERIFY(f);
	CHECK(f.Output->State() == PaneState::Hidden && FramesOf(f).empty());
}

TEST(Host_DoubleClickingACaptionFloatsAndDocks) {
	Fixture f;
	f.AddStandard();
	const RECT before = f.Sol->Group()->Rect;

	HWND gw = f.Host.GroupWindow(f.Sol->Group());
	RECT client;
	::GetClientRect(gw, &client);
	const RECT caption = ComputeGroupParts(*f.Sol->Group(), client, f.Host.Metrics()).Caption;
	::SendMessage(gw, WM_LBUTTONDBLCLK, MK_LBUTTON, Pt(caption.left + 12, (caption.top + caption.bottom) / 2));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Floating && FramesOf(f).size() == 1);
	// the window opens with the size of the group
	CHECK(Width(f.Sol->Group()->Rect) == Width(before) && Height(f.Sol->Group()->Rect) == Height(before));
	CHECK(Width(f.A->Group()->Rect) > Width(before));		// and the documents got the room

	// the window has a title bar, so the group has no caption of its own; a double click on the title bar docks it again
	gw = f.Host.GroupWindow(f.Sol->Group());
	::GetClientRect(gw, &client);
	CHECK(!ComputeGroupParts(*f.Sol->Group(), client, f.Host.Metrics()).HasCaption);
	::SendMessage(f.Host.FloatWindow(f.Sol->Group()->Float()->Id()), WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Docked && FramesOf(f).empty());
	CHECK(f.Sol->Group()->Side() == DockSide::Left);
	CHECK(Width(f.Sol->Group()->Rect) == Width(before));

	// documents do not float
	HWND docWindow = f.Host.GroupWindow(f.A->Group());
	::SendMessage(docWindow, WM_LBUTTONDBLCLK, MK_LBUTTON, Pt(20, 10));
	CHECK(f.A->State() == PaneState::Document && FramesOf(f).empty());
}

TEST(Host_DoubleClickingATabFloatsThatPane) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);
	const auto strip = StripOf(f, f.Sol->Group());
	::SendMessage(f.Host.GroupWindow(f.Sol->Group()), WM_LBUTTONDBLCLK, MK_LBUTTON, Center(strip.Tabs[0]));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Floating && f.Props->State() == PaneState::Docked);
}

TEST(Host_DoubleClickingTheTitleBarDocksTheWindow) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Output, OffScreen(0, 0, 500, 250)));
	CHECK(l.DockToEdge(f.Props, DockSide::Right, l.Floats()[0].get()));
	VERIFY(f);
	HWND frame = f.Host.FloatWindow(l.Floats()[0]->Id());
	::SendMessage(frame, WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
	VERIFY(f);
	CHECK(FramesOf(f).empty());
	CHECK(f.Output->State() == PaneState::Docked && f.Props->State() == PaneState::Docked);
	CHECK(f.Output->Group()->Side() == DockSide::Bottom && f.Props->Group()->Side() == DockSide::Right);
}

TEST(Host_FloatAndDockCommands) {
	Fixture f;
	f.AddStandard();
	f.Props->Caps = PaneCaps::CanClose;					// may not float
	CHECK(f.Host.CanExecute(DockCommand::Float, f.Sol));
	CHECK(!f.Host.CanExecute(DockCommand::Float, f.Props));
	CHECK(!f.Host.CanExecute(DockCommand::Float, f.A));	// documents stay put
	CHECK(!f.Host.CanExecute(DockCommand::Dock, f.Sol));

	f.Host.Layout().DockTo(f.Output, f.Sol->Group(), DockPosition::Tab);
	CHECK(f.Host.Execute(DockCommand::Float, f.Output));	// just this pane leaves the group
	VERIFY(f);
	CHECK(f.Output->State() == PaneState::Floating && f.Sol->State() == PaneState::Docked);
	CHECK(!f.Host.CanExecute(DockCommand::Float, f.Output) && f.Host.CanExecute(DockCommand::Dock, f.Output));
	CHECK(!f.Host.CanExecute(DockCommand::AutoHide, f.Output));

	CHECK(f.Host.Execute(DockCommand::Dock, f.Output));
	VERIFY(f);
	// it was in the tab group on the left when it floated, so that is the edge it returns to
	CHECK(f.Output->State() == PaneState::Docked && f.Output->Group()->Side() == DockSide::Left);
	CHECK(!f.Host.FloatPane(f.A));
	CHECK(!f.Host.DockFloating(f.Sol));
	CHECK(f.Host.ToggleFloat(f.Sol) && f.Sol->State() == PaneState::Floating);
	CHECK(f.Host.ToggleFloat(f.Sol) && f.Sol->State() == PaneState::Docked);
}

TEST(Host_AFloatReturnsToWhereItWasLast) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	const RECT rc = OffScreen(400, 200, 320, 280);
	CHECK(f.Host.FloatPane(f.Props, &rc));
	VERIFY(f);
	CHECK(f.Host.DockFloating(f.Props));
	CHECK(f.Host.FloatPane(f.Props));				// again, without a rectangle
	VERIFY(f);
	const RECT model = l.Floats()[0]->Rect();
	CHECK(EqualRect(&model, &rc) != FALSE);
}

TEST(Host_FloatsAreBroughtBackOntoAScreen) {
	Fixture f;
	f.AddStandard();
	f.Host.SetKeepFloatsOnScreen(true);
	auto& l = f.Host.Layout();
	const RECT lost{ -30000, -30000, -29700, -29700 };
	CHECK(l.Float(f.Props, lost));
	Pump();
	const RECT model = l.Floats()[0]->Rect();
	const RECT strip{ model.left, model.top, model.right, model.top + 40 };
	CHECK(::MonitorFromRect(&strip, MONITOR_DEFAULTTONULL) != nullptr);
	CHECK(Width(model) == 300 && Height(model) == 300);
	const RECT actual = ScreenRect(f.Host.FloatWindow(l.Floats()[0]->Id()));
	CHECK(EqualRect(&actual, &model) != FALSE);

	// a window that is reachable stays where it is
	f.Host.Layout().Hide(f.Props);
	Pump();
	f.Host.SetKeepFloatsOnScreen(false);
}

TEST(Host_SavedFloatsComeBack) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	const RECT rc = OffScreen(200, 100, 350, 320);
	CHECK(l.Float(f.Props, rc));
	CHECK(l.DockToEdge(f.Output, DockSide::Bottom, l.Floats()[0].get()));
	const std::string text = l.Save();
	const std::wstring dump = l.Dump();

	CHECK(l.Hide(f.Props) && l.Hide(f.Output));
	VERIFY(f);
	CHECK(FramesOf(f).empty());

	CHECK(l.Load(text));
	VERIFY(f);
	CHECK_STR(l.Dump(), dump);
	CHECK(FramesOf(f).size() == 1);
	const RECT model = l.Floats()[0]->Rect();
	CHECK(EqualRect(&model, &rc) != FALSE);
}

TEST(Host_FloatingWindowsFollowTheTheme) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().Float(f.Props, OffScreen(0, 0, 300, 300));
	HWND frame = f.Host.FloatWindow(f.Host.Layout().Floats()[0]->Id());

	// the client area of the frame shows the workspace colour in the splitter gaps and the theme's caption in the group
	f.Host.Layout().DockToEdge(f.Output, DockSide::Bottom, f.Host.Layout().Floats()[0].get());
	Pump();
	f.Host.SetTheme(DockTheme::Dark());
	Pump();
	RECT rc;
	::GetClientRect(frame, &rc);
	CClientDC screen(nullptr);
	CDC dc;
	dc.CreateCompatibleDC(screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
	HBITMAP old = dc.SelectBitmap(bmp);
	::PrintWindow(frame, dc, PW_CLIENTONLY);
	auto splitter = DockLayout::Splitters(f.Host.Layout().Floats()[0]->Root())[0].Rect;
	CHECK(dc.GetPixel(splitter.left + 2, (splitter.top + splitter.bottom) / 2) == DockTheme::Dark().Splitter);
	dc.SelectBitmap(old);
	VERIFY(f);
}

// ---- drag and drop ---------------------------------------------------------------------

// how many guide windows (markers, previews) exist, shown or not
int GuideWindows() {
	int count = 0;
	::EnumWindows([](HWND w, LPARAM lp) -> BOOL {
		if (ClassOf(w) == L"WTLDock_Guide")
			(*reinterpret_cast<int*>(lp))++;
		return TRUE;
	}, reinterpret_cast<LPARAM>(&count));
	return count;
}

POINT ScreenCenterOf(Fixture& f, DockGroup* group) {
	const RECT rc = ScreenRect(f.Host.GroupWindow(group));
	return { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
}

// the compass arm of a group: a marker (32) and a gap (4) from the centre
POINT ArmOf(Fixture& f, DockGroup* group, DockPosition pos) {
	POINT c = ScreenCenterOf(f, group);
	switch (pos) {
		case DockPosition::Left: c.x -= 36; break;
		case DockPosition::Right: c.x += 36; break;
		case DockPosition::Top: c.y -= 36; break;
		case DockPosition::Bottom: c.y += 36; break;
		default: break;
	}
	return c;
}

// the guide at an edge of the docking area of the main window
POINT EdgeGuide(Fixture& f, DockSide side) {
	RECT b = f.Host.Layout().Root().Rect;
	POINT origin{ 0, 0 };
	::ClientToScreen(f.Host, &origin);
	OffsetRect(&b, origin.x, origin.y);
	switch (side) {
		case DockSide::Left: return { b.left + 32, (b.top + b.bottom) / 2 };
		case DockSide::Right: return { b.right - 32, (b.top + b.bottom) / 2 };
		case DockSide::Top: return { (b.left + b.right) / 2, b.top + 32 };
		default: return { (b.left + b.right) / 2, b.bottom - 32 };
	}
}

const POINT NowhereOnScreen{ -9000, 300 };

TEST(Host_DraggingShowsGuidesAndAPreview) {
	Fixture f;
	f.AddStandard();
	const std::wstring before = f.Host.Layout().Dump();
	const POINT over = ScreenCenterOf(f, f.Props->Group());

	CHECK(f.Host.BeginDrag(f.Sol, false, over));
	CHECK(f.Host.IsDragging() && f.Host.VisibleGuides() == 9);		// the compass and four edges
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab && f.Host.CurrentDropTarget().Group == f.Props->Group());
	CHECK(GuideWindows() == 10);									// and the preview
	CHECK(!f.Host.BeginDrag(f.Output, false, over));				// one drag at a time

	// away from every group: just the edges, and a floating window's outline
	f.Host.UpdateDrag(NowhereOnScreen);
	CHECK(f.Host.VisibleGuides() == 4 && f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float);

	CHECK(!f.Host.EndDrag(false));
	CHECK(!f.Host.IsDragging() && GuideWindows() == 0 && f.Host.VisibleGuides() == 0);
	CHECK_STR(f.Host.Layout().Dump(), before);
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::None);
	VERIFY(f);
}

TEST(Host_DroppingOnACompassArmSplitsTheGroup) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Props->Group())));
	f.Host.UpdateDrag(ArmOf(f, f.Props->Group(), DockPosition::Left));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Side && f.Host.CurrentDropTarget().Position == DockPosition::Left);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	auto parent = f.Props->Group()->Parent();
	CHECK(parent->IndexOf(f.Sol->Group()) + 1 == parent->IndexOf(f.Props->Group()));
	CHECK(f.Sol->State() == PaneState::Docked && GuideWindows() == 0);
}

TEST(Host_DroppingOnTheCentreMakesATab) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Props->Group())));
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->Group() == f.Props->Group() && f.Props->Group()->Panes().size() == 2 && f.Props->Group()->ActivePane() == f.Sol);
}

TEST(Host_DroppingOnAnEdgeGuideDocksAtTheEdge) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Output, false, ScreenCenterOf(f, f.A->Group())));
	f.Host.UpdateDrag(EdgeGuide(f, DockSide::Left));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Edge && f.Host.CurrentDropTarget().Edge == DockSide::Left);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Output->Group()->Side() == DockSide::Left && f.Host.Layout().Root().Children()[0].get() == f.Output->Group());
}

TEST(Host_DroppingOnTheTabStripInsertsAtTheCursor) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);		// Sol, Props
	const auto strip = StripOf(f, f.Sol->Group());
	const RECT group = ScreenRect(f.Host.GroupWindow(f.Sol->Group()));
	const POINT firstTab{ group.left + strip.Tabs[0].left + 2, group.top + (strip.Tabs[0].top + strip.Tabs[0].bottom) / 2 };

	CHECK(f.Host.BeginDrag(f.Output, false, ScreenCenterOf(f, f.A->Group())));
	f.Host.UpdateDrag(firstTab);
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab && f.Host.CurrentDropTarget().TabIndex == 0);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->Group()->Panes().size() == 3 && f.Sol->Group()->Panes()[0] == f.Output);

	// the caption: as the last tab
	f.Host.Layout().Hide(f.Props);
	f.Host.Layout().Show(f.Props);
	CHECK(f.Host.BeginDrag(f.Props, false, ScreenCenterOf(f, f.A->Group())));
	const RECT sol = ScreenRect(f.Host.GroupWindow(f.Sol->Group()));
	f.Host.UpdateDrag({ sol.left + 20, sol.top + 8 });
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab && f.Host.CurrentDropTarget().TabIndex == -1);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->Group()->Panes().back() == f.Props);
}

TEST(Host_DroppingAwayFromTheGuidesFloatsThePane) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Sol->Group())));
	f.Host.UpdateDrag(NowhereOnScreen);
	const RECT ghost = f.Host.CurrentDropTarget().Preview;
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float && !IsRectEmpty(&ghost));
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Floating && FramesOf(f).size() == 1);
	const RECT model = f.Host.Layout().Floats()[0]->Rect();
	CHECK(EqualRect(&model, &ghost) != FALSE);
	// the window is held by the cursor: it is where the drag ended, not where the group was
	CHECK(model.left <= NowhereOnScreen.x && NowhereOnScreen.x < model.right);
}

TEST(Host_ControlKeepsAPaneFromDocking) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Props->Group())));
	CHECK(f.Host.VisibleGuides() == 9);
	f.Host.UpdateDrag(ScreenCenterOf(f, f.Props->Group()), true);
	CHECK(f.Host.VisibleGuides() == 0 && f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float);
	f.Host.UpdateDrag(ScreenCenterOf(f, f.Props->Group()), false);
	CHECK(f.Host.VisibleGuides() == 9 && f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab);
	f.Host.EndDrag(false);
	VERIFY(f);
}

TEST(Host_ALayoutChangeCancelsTheDrag) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Props->Group())));
	CHECK(f.Host.Layout().Hide(f.Output));
	CHECK(!f.Host.IsDragging() && GuideWindows() == 0);
	CHECK(!f.Host.EndDrag(true));
	VERIFY(f);
}

TEST(Host_DocumentsOnlyDockAmongDocuments) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.DockTo(f.B, f.A->Group(), DockPosition::Right));			// two document groups
	VERIFY(f);

	CHECK(f.Host.BeginDrag(f.A, false, ScreenCenterOf(f, f.B->Group())));
	CHECK(f.Host.VisibleGuides() == 5);									// a compass, no edges
	f.Host.UpdateDrag(ScreenCenterOf(f, f.Sol->Group()));
	CHECK(f.Host.VisibleGuides() == 0 && f.Host.CurrentDropTarget().Type == DropTarget::Kind::None);	// not among tool windows, not floating
	f.Host.UpdateDrag(NowhereOnScreen);
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::None);
	CHECK(!f.Host.EndDrag(true));
	CHECK(f.A->State() == PaneState::Document);

	// onto the other group, as a tab; and below it, as a new group
	CHECK(f.Host.BeginDrag(f.A, false, ScreenCenterOf(f, f.B->Group())));
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.A->Group() == f.B->Group());
	CHECK(f.Host.BeginDrag(f.A, false, ScreenCenterOf(f, f.B->Group())));
	f.Host.UpdateDrag(ArmOf(f, f.B->Group(), DockPosition::Bottom));
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.A->Group() != f.B->Group() && f.A->Group()->IsDocument());
}

TEST(Host_DraggingATabOutOfItsGroupDocksItElsewhere) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Output, f.Sol->Group(), DockPosition::Tab);					// Sol, Output
	DockGroup* group = f.Sol->Group();
	HWND gw = f.Host.GroupWindow(group);
	const auto strip = StripOf(f, group);

	// press the Output tab, pull it up out of the strip: the guides appear
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.Tabs[1]));
	CHECK(!f.Host.IsDragging());
	POINT out{ (strip.Tabs[1].left + strip.Tabs[1].right) / 2, strip.Tabs[1].top - 120 };
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(out.x, out.y));
	CHECK(f.Host.IsDragging());

	// onto the left arm of the Properties group
	POINT arm = ArmOf(f, f.Props->Group(), DockPosition::Left);
	::ScreenToClient(gw, &arm);
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(arm.x, arm.y));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Side && f.Host.CurrentDropTarget().Group == f.Props->Group());
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(arm.x, arm.y));
	VERIFY(f);
	CHECK(!f.Host.IsDragging() && GuideWindows() == 0);
	CHECK(f.Output->Group() != f.Sol->Group() && f.Output->Group()->Parent() == f.Props->Group()->Parent());
	CHECK(f.Sol->Group()->Panes().size() == 1);
}

TEST(Host_DraggingACaptionTakesTheWholeGroup) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Output, f.Props->Group(), DockPosition::Tab);				// Props + Output, both go
	DockGroup* group = f.Props->Group();
	HWND gw = f.Host.GroupWindow(group);
	RECT client;
	::GetClientRect(gw, &client);
	const RECT caption = ComputeGroupParts(*group, client, f.Host.Metrics()).Caption;
	const POINT press{ caption.left + 20, (caption.top + caption.bottom) / 2 };

	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(press.x, press.y));
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(press.x + 1, press.y));		// not far enough yet
	CHECK(!f.Host.IsDragging());
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(press.x + 20, press.y + 20));
	CHECK(f.Host.IsDragging());

	POINT edge = EdgeGuide(f, DockSide::Left);
	::ScreenToClient(gw, &edge);
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(edge.x, edge.y));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Edge);
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(edge.x, edge.y));
	VERIFY(f);
	CHECK(f.Props->Group() == f.Output->Group() && f.Props->Group()->Side() == DockSide::Left);
	CHECK(f.Props->Group()->Panes().size() == 2);

	// a caption press that ends without a drag is just a click
	HWND again = f.Host.GroupWindow(f.Props->Group());
	::GetClientRect(again, &client);
	const RECT c2 = ComputeGroupParts(*f.Props->Group(), client, f.Host.Metrics()).Caption;
	::SendMessage(again, WM_LBUTTONDOWN, MK_LBUTTON, Pt(c2.left + 20, c2.top + 5));
	::SendMessage(again, WM_LBUTTONUP, 0, Pt(c2.left + 20, c2.top + 5));
	CHECK(!f.Host.IsDragging());
	CHECK(f.Host.IsActive(f.Props->Group()));
}

TEST(Host_LosingTheMouseCaptureCancelsTheDrag) {
	Fixture f;
	f.AddStandard();
	const std::wstring before = f.Host.Layout().Dump();
	DockGroup* group = f.Sol->Group();
	HWND gw = f.Host.GroupWindow(group);
	RECT client;
	::GetClientRect(gw, &client);
	const RECT caption = ComputeGroupParts(*group, client, f.Host.Metrics()).Caption;
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(caption.left + 20, caption.top + 5));
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(caption.left + 60, caption.top + 60));
	CHECK(f.Host.IsDragging());
	::SendMessage(gw, WM_CAPTURECHANGED, 0, 0);
	CHECK(!f.Host.IsDragging() && GuideWindows() == 0);
	CHECK_STR(f.Host.Layout().Dump(), before);
	// and the window is not stuck in the drag
	::SendMessage(gw, WM_MOUSEMOVE, MK_LBUTTON, Pt(caption.left + 100, caption.top + 100));
	CHECK(!f.Host.IsDragging());
	VERIFY(f);
}

TEST(Host_DragsBetweenFloatingAndMainWindows) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Output, OffScreen(0, 100, 400, 300)));
	CHECK(l.Float(f.Props, OffScreen(500, 100, 400, 300)));
	VERIFY(f);

	// a pane of the main window over a group in a frame: offered the compass of that group
	CHECK(f.Host.BeginDrag(f.Sol, false, ScreenCenterOf(f, f.Output->Group())));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab && f.Host.CurrentDropTarget().Group == f.Output->Group());
	CHECK(f.Host.VisibleGuides() == 9);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->Group() == f.Output->Group() && f.Sol->State() == PaneState::Floating);

	// a pane of a frame back to the main window's edge; its frame disappears with the last pane
	CHECK(f.Host.BeginDrag(f.Props, true, ScreenCenterOf(f, f.Props->Group())));
	f.Host.UpdateDrag(EdgeGuide(f, DockSide::Right));
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Props->State() == PaneState::Docked && f.Props->Group()->Side() == DockSide::Right && FramesOf(f).size() == 1);
}

TEST(Host_RandomOperationsKeepWindowsAndModelInStep) {
	std::mt19937 rng(7);
	auto pick = [&](size_t n) { return (size_t)(rng() % n); };

	Fixture f;
	std::vector<DockPane*> panes;
	for (int i = 0; i < 6; i++) {
		std::wstring id = L"tool" + std::to_wstring(i);
		panes.push_back(f.Add(id.c_str(), PaneKind::Tool, (DockSide)(i % 4), 150 + i * 15, 130 + i * 10));
	}
	for (int i = 0; i < 4; i++) {
		std::wstring id = L"doc" + std::to_wstring(i);
		panes.push_back(f.Add(id.c_str(), PaneKind::Document));
	}
	auto& l = f.Host.Layout();
	int applied = 0;
	const int failuresBefore = g_failures;

	for (int step = 0; step < 600; step++) {
		std::vector<DockGroup*> groups;
		l.ForEachGroup([&](DockGroup& g) { groups.push_back(&g); });
		auto anyPane = [&] { return panes[pick(panes.size())]; };
		auto anyGroup = [&] { return groups[pick(groups.size())]; };
		const RECT rc = OffScreen(20, 20, 300, 280);

		bool ok = false;
		switch (pick(20)) {
			case 0: ok = l.Show(anyPane()); break;
			case 1: ok = l.Hide(anyPane()); break;
			case 2: ok = l.DockTo(anyPane(), anyGroup(), (DockPosition)pick(5)); break;
			case 3: ok = l.DockToEdge(anyPane(), (DockSide)pick(4)); break;
			case 4: ok = l.Float(anyPane(), rc); break;
			case 5: ok = l.AutoHide(anyGroup()); break;
			case 6: ok = l.Unhide(anyGroup()); break;
			case 7: ok = l.MoveGroupTo(anyGroup(), anyGroup(), (DockPosition)pick(5)); break;
			case 8: ok = l.MoveGroupToEdge(anyGroup(), (DockSide)pick(4)); break;
			case 9: ok = f.Host.Layout().Activate(anyPane()); break;
			case 10: {
				auto hits = DockLayout::Splitters(l.Root());
				if (!hits.empty()) {
					auto& h = hits[pick(hits.size())];
					ok = l.ResizeSplitter(h.Split, h.Index, (int)pick(300) - 150);
				}
				break;
			}
			case 11: ok = f.Host.ClosePane(anyPane()); break;
			case 12: ok = f.Host.Execute((DockCommand)pick(4), anyPane()); break;
			case 13: ok = l.ReorderTab(anyPane(), (int)pick(4)); break;
			case 14: f.Host.ActivatePane(anyPane()); break;
			case 15: ok = f.Host.ToggleFloat(anyPane()); break;
			case 16:
				if (!l.Floats().empty())
					ok = f.Host.DockFloatWindow(l.Floats()[pick(l.Floats().size())]->Id());
				break;
			case 17:
				if (!l.Floats().empty())
					ok = f.Host.CloseFloatWindow(l.Floats()[pick(l.Floats().size())]->Id());
				break;
			case 19: {
				// a whole drag: begin, wander over groups, compass arms and nowhere, end (or cancel)
				DockPane* pane = anyPane();
				if (!pane->Group() || pane->Group()->Location() == GroupLocation::AutoHide)
					break;
				auto somewhere = [&]() -> POINT {
					if (pick(4) == 0)
						return { -9000 + (int)pick(300), (int)pick(500) };
					HWND w = f.Host.GroupWindow(anyGroup());
					if (!w)
						return NowhereOnScreen;
					const RECT r = ScreenRect(w);
					const POINT offsets[] = { { 0, 0 }, { -36, 0 }, { 36, 0 }, { 0, -36 }, { 0, 36 }, { 7, 5 } };
					const POINT o = offsets[pick(6)];
					return { (r.left + r.right) / 2 + o.x, (r.top + r.bottom) / 2 + o.y };
				};
				if (f.Host.BeginDrag(pane, pick(2) != 0, somewhere())) {
					for (int i = 0; i < 3; i++)
						f.Host.UpdateDrag(somewhere(), pick(5) == 0);
					ok = f.Host.EndDrag(pick(4) != 0);
				}
				break;
			}
			case 18:
				if (!l.Floats().empty())
					ok = l.SetFloatRect(l.Floats()[pick(l.Floats().size())].get(), OffScreen((int)pick(500), (int)pick(300), 250 + (int)pick(300), 250 + (int)pick(200)));
				break;
		}
		applied += ok;
		Verify(f, __LINE__);
		if (g_failures != failuresBefore) {
			wprintf(L"  (stopped at step %d)\n%s\n", step, l.Dump().c_str());
			return;
		}
		if (step % 150 == 149) {
			::SetWindowPos(f.Host, nullptr, 0, 0, 800 + (int)pick(600), 500 + (int)pick(300), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			Verify(f, __LINE__);
		}
	}
	wprintf(L"        (%d of 600 operations applied)\n", applied);
	CHECK(applied > 200);
}

}

void RunUiTests() {
	_Module.Init(nullptr, ::GetModuleHandle(nullptr));
	wprintf(L"\nDocking window tests\n\n");

	Run_Host_PlacesContentInItsGroups();
	Run_Host_HiddenPanesKeepTheirWindows();
	Run_Host_TabSwitchingShowsOnlyTheActiveContent();
	Run_Host_MovingAPaneReparentsItsContent();
	Run_Host_AutoHiddenPanesAreNotShown();
	Run_Host_FloatingPanesGetAFrameOfTheirOwn();
	Run_Host_AFloatingTreeCanHoldSeveralGroups();
	Run_Host_MovingOrSizingTheFrameUpdatesTheLayout();
	Run_Host_TheFrameCannotBeSmallerThanItsContent();
	Run_Host_ClosingTheFrameClosesItsPanes();
	Run_Host_DoubleClickingACaptionFloatsAndDocks();
	Run_Host_DoubleClickingATabFloatsThatPane();
	Run_Host_DoubleClickingTheTitleBarDocksTheWindow();
	Run_Host_FloatAndDockCommands();
	Run_Host_AFloatReturnsToWhereItWasLast();
	Run_Host_FloatsAreBroughtBackOntoAScreen();
	Run_Host_SavedFloatsComeBack();
	Run_Host_FloatingWindowsFollowTheTheme();
	Run_Host_ResizingTheHostRelayoutsEverything();
	Run_Host_DraggingASplitterResizesTheGroups();
	Run_Host_CloseButtonHidesTheActivePane();
	Run_Host_ClickingATabActivatesThePane();
	Run_Host_ForwardsWhatContentSaysToItsParent();
	Run_Host_PaintsTheChromeInTheTheme();
	Run_Host_DocumentTabsHaveCloseButtons();
	Run_Host_MiddleClickClosesADocumentTab();
	Run_Host_ToolTabsHaveNoCloseButtons();
	Run_Host_ClosingCanBeVetoed();
	Run_Host_DraggingATabReordersTheGroup();
	Run_Host_OverflowKeepsTheActiveTabInView();
	Run_Host_CommandsCloseTabsInBulk();
	Run_Host_ClosingTheActivePaneActivatesANeighbour();
	Run_Host_PanesCanBeRemovedWhileClosing();
	Run_Host_HoveringATabHighlightsItAndShowsItsCloseButton();
	Run_Host_TabIconsAreDrawnAndCountedInTheTabWidth();
	Run_Host_DraggingShowsGuidesAndAPreview();
	Run_Host_DroppingOnACompassArmSplitsTheGroup();
	Run_Host_DroppingOnTheCentreMakesATab();
	Run_Host_DroppingOnAnEdgeGuideDocksAtTheEdge();
	Run_Host_DroppingOnTheTabStripInsertsAtTheCursor();
	Run_Host_DroppingAwayFromTheGuidesFloatsThePane();
	Run_Host_ControlKeepsAPaneFromDocking();
	Run_Host_ALayoutChangeCancelsTheDrag();
	Run_Host_DocumentsOnlyDockAmongDocuments();
	Run_Host_DraggingATabOutOfItsGroupDocksItElsewhere();
	Run_Host_DraggingACaptionTakesTheWholeGroup();
	Run_Host_LosingTheMouseCaptureCancelsTheDrag();
	Run_Host_DragsBetweenFloatingAndMainWindows();
	Run_Host_RandomOperationsKeepWindowsAndModelInStep();

	_Module.Term();
}
