// Tests for the docking window (CDockHost): real windows, created off-screen. After every operation the windows
// are compared with what the layout model says they should look like.

#include <atlbase.h>
#include <atlapp.h>

CAppModule _Module;

#include <atlwin.h>
#include <atlgdi.h>
#include <commctrl.h>

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
		Host.SetFlyoutTiming(0, 0, 100000);		// no slide, hover opens at once, nothing closes by itself
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
	const auto& metrics = f.Host.MetricsFor(f.Host.GroupDpi(group));
	const auto parts = ComputeGroupParts(*group, client, metrics, f.Host.GetTabState(group).Rows);
	CClientDC dc(gw);
	dc.SelectFont(f.Host.FontFor(f.Host.GroupDpi(group)));
	std::vector<TabSpec> specs;
	for (auto p : group->Panes()) {
		SIZE size{};
		dc.SelectFont(p->Preview ? f.Host.ItalicFontFor(f.Host.GroupDpi(group)) : f.Host.FontFor(f.Host.GroupDpi(group)));
		dc.GetTextExtent(p->Title.c_str(), (int)p->Title.size(), &size);
		const bool closable = group->IsDocument() && (Has(p->Caps, PaneCaps::CanClose) || p->Pinned());
		specs.push_back({ size.cx, p->Icon != nullptr, closable, p->Modified && !closable });
	}
	if (f.Host.MultiRowTabs())
		return LayoutTabRows(specs, parts.Tabs, metrics, group->ActiveIndex(), group->TabsAtBottom && !group->IsDocument());
	// (a tab that has been scrolled out of view is not forced back in: the user did that)
	const auto state = f.Host.GetTabState(group);
	int active = group->ActiveIndex();
	if (active < state.First || active >= state.First + state.Visible)
		active = -1;
	return LayoutTabStrip(specs, parts.Tabs, metrics, state.First, active);
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

	DockPane* flyout = f.Host.FlyoutPane();
	DockGroup* flyoutGroup = flyout ? flyout->Group() : nullptr;
	layout.ForEachGroup([&](DockGroup& g) {
		if (g.Location() == GroupLocation::AutoHide && &g != flyoutGroup)
			return;
		HWND surface = g.Location() == GroupLocation::Float ? f.Host.FloatWindow(g.Float()->Id()) : f.Host.m_hWnd;
		HWND w = f.Host.GroupWindow(&g);
		Check(w != nullptr, "every shown group has a window", line);
		if (!w || !surface)
			return;
		expectedGroups.insert(w);
		Check(::GetParent(w) == surface, "group windows are children of their surface", line);
		Check(::IsWindowVisible(w) != FALSE, "group windows are visible", line);
		const RECT actual = RectIn(w, surface);
		const RECT expected = &g == flyoutGroup ? f.Host.FlyoutRect() : g.Rect;
		Check(EqualRect(&actual, &expected) != FALSE, "group window rectangle equals the model's", line);
	});
	Check((flyout != nullptr) == (flyoutGroup != nullptr && f.Host.GroupWindow(flyoutGroup) != nullptr), "an open flyout has a window", line);

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
		const bool shown = g && (g->Location() != GroupLocation::AutoHide || g == flyoutGroup);
		if (!shown || g->ActivePane() != p.get()) {
			Check(!::IsWindowVisible(p->hWnd), "content of a pane that is not on show is hidden", line);
			continue;
		}
		HWND gw = f.Host.GroupWindow(g);
		Check(::IsWindowVisible(p->hWnd) != FALSE, "content of the active pane is visible", line);
		Check(::GetParent(p->hWnd) == gw, "content is a child of its group window", line);
		RECT client;
		::GetClientRect(gw, &client);
		const RECT expected = ComputeGroupParts(*g, client, f.Host.MetricsFor(f.Host.GroupDpi(g)), f.Host.GetTabState(g).Rows).Content;
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
	CHECK(f.Host.CanExecute(DockCommand::Float, f.A));	// documents float as well
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

TEST(Host_DocumentsDockOnlyAmongDocumentsOrFloat) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.DockTo(f.B, f.A->Group(), DockPosition::Right));			// two document groups
	VERIFY(f);

	CHECK(f.Host.BeginDrag(f.A, false, ScreenCenterOf(f, f.B->Group())));
	CHECK(f.Host.VisibleGuides() == 5);									// a compass, no edges
	f.Host.UpdateDrag(ScreenCenterOf(f, f.Sol->Group()));
	CHECK(f.Host.VisibleGuides() == 0 && f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float);	// not among tool windows: floating is all that is left
	f.Host.UpdateDrag(NowhereOnScreen);
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float);
	CHECK(!f.Host.EndDrag(false));
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

// ---- auto-hide bars and the flyout ---------------------------------------------------------

POINT HostToPoint(const RECT& r) {
	return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

// a click on the host window itself (bar items, splitters)
void ClickHost(Fixture& f, POINT pt) {
	::SendMessage(f.Host, WM_LBUTTONDOWN, MK_LBUTTON, Pt(pt.x, pt.y));
	::SendMessage(f.Host, WM_LBUTTONUP, 0, Pt(pt.x, pt.y));
}

// a pumped wait for something that a timer does
template<typename Condition>
bool WaitFor(Condition done, int milliseconds = 1500) {
	const ULONGLONG start = ::GetTickCount64();
	while (!done()) {
		Pump();
		if (::GetTickCount64() - start > (ULONGLONG)milliseconds)
			return done();
		::Sleep(10);
	}
	return true;
}

// the caption buttons of a group window in the layout they have when all three are shown
CaptionButtons ButtonsOf(Fixture& f, DockGroup* group) {
	RECT client;
	::GetClientRect(f.Host.GroupWindow(group), &client);
	const RECT caption = ComputeGroupParts(*group, client, f.Host.Metrics()).Caption;
	return ComputeCaptionButtons(caption, true, true, true, f.Host.Metrics());
}

TEST(Host_AutoHiddenPanesHaveBarItems) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);			// Sol, Props
	CHECK(l.AutoHide(f.Sol->Group()));
	CHECK(l.AutoHide(f.Output->Group()));
	VERIFY(f);

	RECT sol{}, props{}, output{};
	CHECK(f.Host.GetBarItemRect(f.Sol, sol) && f.Host.GetBarItemRect(f.Props, props) && f.Host.GetBarItemRect(f.Output, output));
	const RECT leftBar = l.AutoHideBarRect(DockSide::Left), bottomBar = l.AutoHideBarRect(DockSide::Bottom);
	RECT inside;
	CHECK(IntersectRect(&inside, &sol, &leftBar) && EqualRect(&inside, &sol));
	CHECK(IntersectRect(&inside, &props, &leftBar) && EqualRect(&inside, &props));
	CHECK(IntersectRect(&inside, &output, &bottomBar) && EqualRect(&inside, &output));
	// vertical bar: the items follow each other from the top, without overlap
	CHECK(sol.bottom <= props.top && Width(sol) == Width(leftBar));
	RECT none;
	CHECK(!f.Host.GetBarItemRect(f.A, none));						// not auto-hidden
	CHECK(f.Host.FlyoutPane() == nullptr);
}

TEST(Host_ClickingABarItemSlidesTheFlyoutOut) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	const RECT documents = f.A->Group()->Rect;

	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == f.Sol && f.Host.ActivePane() == f.Sol);
	CHECK(f.Sol->State() == PaneState::AutoHide);

	// it stands over the documents, next to the bar, as wide as the group was
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	CHECK(window != nullptr && ::IsWindowVisible(window) && ::IsWindowVisible(f.Sol->hWnd));
	const RECT out = f.Host.FlyoutRect();
	const RECT area = l.Root().Rect;
	CHECK(out.left == area.left && out.top == area.top && out.bottom == area.bottom && Width(out) == 250);
	const RECT actual = RectIn(window, f.Host);
	CHECK(EqualRect(&actual, &out) != FALSE);
	// the documents did not move
	const RECT docs = f.A->Group()->Rect;
	CHECK(EqualRect(&docs, &documents) != FALSE);
	CHECK(::GetWindow(window, GW_HWNDPREV) == nullptr);				// on top of its siblings

	// the item of the flyout that is out again: closes it
	ClickHost(f, HostToPoint(item));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == nullptr && f.Host.GroupWindow(f.Sol->Group()) == nullptr);
	CHECK(!::IsWindowVisible(f.Sol->hWnd));
	CHECK(f.Sol->State() == PaneState::AutoHide);
}

TEST(Host_AnotherItemSwitchesTheFlyout) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab);
	CHECK(l.AutoHide(f.Sol->Group()));
	CHECK(l.AutoHide(f.Output->Group()));

	RECT sol, props, output;
	CHECK(f.Host.GetBarItemRect(f.Sol, sol) && f.Host.GetBarItemRect(f.Props, props) && f.Host.GetBarItemRect(f.Output, output));
	ClickHost(f, HostToPoint(sol));
	CHECK(f.Host.FlyoutPane() == f.Sol);

	// the other tab of the same group: the same flyout, with the other pane in it
	ClickHost(f, HostToPoint(props));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == f.Props && f.Props->Group()->ActivePane() == f.Props);
	CHECK(::IsWindowVisible(f.Props->hWnd) && !::IsWindowVisible(f.Sol->hWnd));

	// another group: the first goes away, the second comes out of the bottom
	HWND before = f.Host.GroupWindow(f.Props->Group());
	ClickHost(f, HostToPoint(output));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == f.Output && f.Host.GroupWindow(f.Props->Group()) == nullptr && !::IsWindow(before));
	const RECT out = f.Host.FlyoutRect();
	CHECK(out.bottom == l.Root().Rect.bottom && Height(out) == 150);
}

TEST(Host_ClickingElsewhereClosesTheFlyout) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	CHECK(f.Host.FlyoutPane() == f.Sol);

	// a splitter of the main tree
	auto splitters = DockLayout::Splitters(l.Root());
	CHECK(!splitters.empty());
	ClickHost(f, HostToPoint(splitters[0].Rect));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == nullptr);
}

TEST(Host_ThePinDocksTheFlyoutAgainAndAutoHidesADockedGroup) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();

	// docked: the pin sends the group to its bar
	CaptionButtons buttons = ButtonsOf(f, f.Sol->Group());
	HWND gw = f.Host.GroupWindow(f.Sol->Group());
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::AutoHide);
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));

	// out: the pin docks it again, at the edge it came from
	ClickHost(f, HostToPoint(item));
	CHECK(f.Host.FlyoutPane() == f.Sol);
	buttons = ButtonsOf(f, f.Sol->Group());
	gw = f.Host.GroupWindow(f.Sol->Group());
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Docked && f.Sol->Group()->Side() == DockSide::Left);
	CHECK(f.Host.FlyoutPane() == nullptr && IsRectEmpty(&l.AutoHideBarRect(DockSide::Left)));

	// a pane that cannot be auto-hidden has no pin: pressing where it would be does nothing
	f.Props->Caps = PaneCaps::None;
	gw = f.Host.GroupWindow(f.Props->Group());
	buttons = ButtonsOf(f, f.Props->Group());
	::SendMessage(gw, WM_LBUTTONDOWN, MK_LBUTTON, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	::SendMessage(gw, WM_LBUTTONUP, 0, Pt(HostToPoint(buttons.Pin).x, HostToPoint(buttons.Pin).y));
	CHECK(f.Props->State() == PaneState::Docked);
}

TEST(Host_HoveringAnItemOpensAFlyoutThatClosesWhenTheMouseLeaves) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));

	::SendMessage(f.Host, WM_MOUSEMOVE, 0, Pt(HostToPoint(item).x, HostToPoint(item).y));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == f.Sol);
	CHECK(f.Host.ActivePane() != f.Sol);				// hovering does not take the focus

	// the (real) mouse is nowhere near: the flyout goes once the delay has passed
	f.Host.SetFlyoutTiming(0, 0, 50);
	CHECK(WaitFor([&] { return f.Host.FlyoutPane() == nullptr; }));
	VERIFY(f);

	// one that was clicked open does not go by itself
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	CHECK(!WaitFor([&] { return f.Host.FlyoutPane() == nullptr; }, 400));
	CHECK(f.Host.FlyoutPane() == f.Sol);
	f.Host.HideFlyout();
	VERIFY(f);
}

TEST(Host_TheFlyoutSlidesInAndOut) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	f.Host.SetFlyoutTiming(200, 0, 100000);
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));

	ClickHost(f, HostToPoint(item));
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	CHECK(window != nullptr);
	// it starts behind the bar...
	const RECT start = RectIn(window, f.Host);
	const RECT out = f.Host.FlyoutRect();
	CHECK(start.left < out.left);
	// ...and arrives
	CHECK(WaitFor([&] { const RECT r = RectIn(window, f.Host); return r.left == out.left; }));
	VERIFY(f);

	// closing slides it back, then it is gone
	f.Host.HideFlyout();
	CHECK(f.Host.FlyoutPane() != nullptr);				// still there while it moves
	CHECK(WaitFor([&] { return f.Host.FlyoutPane() == nullptr; }));
	VERIFY(f);
	CHECK(f.Host.GroupWindow(f.Sol->Group()) == nullptr);
	f.Host.SetFlyoutTiming(0, 0, 100000);
}

TEST(Host_FocusMovingToAnotherPaneClosesTheFlyout) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	CHECK(f.Host.FlyoutPane() == f.Sol);

	// moving the focus into a pane of the main window is what clicking into it does
	f.Host.ActivatePane(f.A);
	::SetFocus(f.A->hWnd);
	CHECK(WaitFor([&] { return f.Host.FlyoutPane() == nullptr; }, 1000));
	VERIFY(f);
	CHECK(f.Host.ActivePane() == f.A && f.Sol->State() == PaneState::AutoHide);

	// a flyout that was hovered open (never focused) closes on the focus moving too
	f.Host.SetFlyoutTiming(0, 0, 100000);
	::SendMessage(f.Host, WM_MOUSEMOVE, 0, Pt(HostToPoint(item).x, HostToPoint(item).y));
	CHECK(f.Host.FlyoutPane() == f.Sol);
	::SetFocus(f.B->hWnd);
	f.Host.ActivatePane(f.B);
	CHECK(WaitFor([&] { return f.Host.FlyoutPane() == nullptr; }, 1000));
	VERIFY(f);
}

TEST(Host_TheFlyoutFollowsChangesToTheLayout) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	RECT item;
	CHECK(f.Host.GetBarItemRect(f.Sol, item));

	// docking the group takes the flyout away
	ClickHost(f, HostToPoint(item));
	CHECK(l.Unhide(f.Sol->Group()));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == nullptr && f.Sol->State() == PaneState::Docked);

	// so does hiding the pane, or removing it
	CHECK(l.AutoHide(f.Sol->Group()));
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	CHECK(l.Hide(f.Sol));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == nullptr);

	CHECK(l.Show(f.Sol));											// comes back into the bar ("auto-hide" was its last state)
	CHECK(f.Sol->State() == PaneState::AutoHide);
	CHECK(f.Host.GetBarItemRect(f.Sol, item));
	ClickHost(f, HostToPoint(item));
	CHECK(f.Host.FlyoutPane() == f.Sol);
	const std::string text = l.Save();
	CHECK(l.Load(text));											// nodes are all new; the flyout is found again by its pane
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == f.Sol);
	CHECK(l.RemovePane(f.Sol));
	VERIFY(f);
	CHECK(f.Host.FlyoutPane() == nullptr);
}

TEST(Host_AutoHiddenPanesCanBeDockedFromTheirMenu) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.AutoHide(f.Sol->Group()));
	CHECK(f.Host.CanExecute(DockCommand::Dock, f.Sol));
	CHECK(!f.Host.CanExecute(DockCommand::AutoHide, f.Sol) && !f.Host.CanExecute(DockCommand::Float, f.Sol));
	CHECK(f.Host.Execute(DockCommand::Dock, f.Sol));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Docked && f.Sol->Group()->Side() == DockSide::Left);
}

// ---- Persistence, menus, factories ---------------------------------------------------

std::wstring TempPath(const wchar_t* name) {
	wchar_t dir[MAX_PATH];
	::GetTempPathW(_countof(dir), dir);
	return std::wstring(dir) + L"WTLDockUiTests." + std::to_wstring(::GetCurrentProcessId()) + L"." + name;
}

TEST(Host_StateRoundTripsWithTheActivePaneAndTheWindowPlacement) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.AutoHide(f.Props->Group());
	f.Host.ActivatePane(f.B);
	Pump();
	const auto dump = l.Dump();

	const std::string text = f.Host.SaveState();
	CHECK(text.find("\"activePane\"") != std::string::npos && text.find("\"window\"") != std::string::npos);
	CHECK(text.find("\"maximized\"") != std::string::npos && text.find("\"normal\"") != std::string::npos);
	CHECK(f.Host.SaveState(false).find("\"window\"") == std::string::npos);

	// the layout reads the state too: the additions are ignored by it
	DockLayout plain;
	for (auto& p : l.Panes()) {
		PaneDesc d;
		d.Id = p->Id();
		d.Kind = p->Kind();
		plain.AddPane(d);
	}
	CHECK(plain.Load(text));

	// scramble, then load: the arrangement and the active pane come back
	l.Show(f.Sol);
	l.Hide(f.Sol);
	l.Float(f.Output, OffScreen(100, 100, 300, 200));
	f.Host.ActivatePane(f.A);
	CHECK(f.Host.LoadState(text, {}, nullptr, false));
	VERIFY(f);
	CHECK_STR(l.Dump(), dump);
	CHECK(f.Host.ActivePane() == f.B);
	CHECK_VALID(l);
}

TEST(Host_StateGoesThroughAFile) {
	Fixture f;
	f.AddStandard();
	const auto path = TempPath(L"state.json");
	const auto dump = f.Host.Layout().Dump();
	CHECK(f.Host.SaveStateToFile(path, false));
	f.Host.Layout().Hide(f.Props);
	f.Host.Layout().Hide(f.Sol);
	CHECK(f.Host.LoadStateFromFile(path, {}, nullptr, false));
	CHECK_STR(f.Host.Layout().Dump(), dump);
	VERIFY(f);

	std::wstring error;
	CHECK(!f.Host.LoadStateFromFile(TempPath(L"missing.json"), {}, &error, false) && !error.empty());
	CHECK(!f.Host.LoadState("this is not json", {}, &error, false) && !error.empty());
	CHECK_STR(f.Host.Layout().Dump(), dump);				// failed loads change nothing
	::DeleteFileW(path.c_str());
}

TEST(Host_TheWindowPlacementComesBackOnAScreen) {
	Fixture f;
	f.AddStandard();
	// where the fixture window is: nowhere near a screen, and that is what is saved
	const std::string text = f.Host.SaveState();

	// loading without placement leaves the window alone
	RECT before, after;
	::GetWindowRect(f.Frame, &before);
	CHECK(f.Host.LoadState(text, {}, nullptr, false));
	::GetWindowRect(f.Frame, &after);
	CHECK(EqualRect(&before, &after) != FALSE);

	// with it, the window was saved out of reach, so it is moved onto a monitor (and sized as it was)
	CHECK(f.Host.LoadState(text));
	::GetWindowRect(f.Frame, &after);
	const RECT strip{ after.left, after.top, after.right, after.top + 40 };
	CHECK(::MonitorFromRect(&strip, MONITOR_DEFAULTTONULL) != nullptr);
	CHECK(Width(after) == Width(before) && Height(after) == Height(before));
	::SetWindowPos(f.Frame, nullptr, -12000, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	Pump();
}

TEST(Host_ThePaneFactoryMakesPanesTheFileMentions) {
	// a session with two documents that the next session has not registered
	std::string text;
	{
		Fixture f;
		f.AddStandard();
		text = f.Host.SaveState(false);
	}

	Fixture g;
	g.Sol = g.Add(L"Sol", PaneKind::Tool, DockSide::Left, 250);
	std::vector<std::wstring> asked;
	g.Host.SetPaneFactory([&](DockLayout& layout, const std::wstring& id) -> DockPane* {
		asked.push_back(id);
		if (id.find(L".cpp") == std::wstring::npos)
			return nullptr;
		PaneDesc d;
		d.Id = d.Title = id;
		d.Kind = PaneKind::Document;
		return layout.AddPane(d);
	});
	int created = 0;
	g.Host.SetContentFactory([&](DockPane& pane, HWND parent) -> HWND {
		created++;
		return ::CreateWindowExW(0, L"STATIC", pane.Title.c_str(), WS_CHILD, 0, 0, 10, 10, parent, nullptr, nullptr, nullptr);
	});
	CHECK(g.Host.LoadState(text, {}, nullptr, false));
	Pump();
	auto a = g.Host.Layout().FindPane(L"a.cpp");
	auto b = g.Host.Layout().FindPane(L"b.cpp");
	CHECK(a && b && a->State() == PaneState::Document && b->State() == PaneState::Document);
	CHECK(g.Host.Layout().FindPane(L"Props") == nullptr);	// the factory declined: the pane is dropped
	CHECK(!asked.empty());
	CHECK_VALID(g.Host.Layout());

	// content is made when a pane is first on show, and only then
	CHECK(created == 1);									// the active tab
	DockPane* shown = a->Group()->ActivePane();
	DockPane* other = shown == a ? b : a;
	CHECK(shown->hWnd != nullptr && other->hWnd == nullptr);
	g.Host.ActivatePane(other);
	CHECK(other->hWnd != nullptr && ::IsWindowVisible(other->hWnd) && created == 2);
	g.Host.ActivatePane(shown);
	g.Host.ActivatePane(other);
	CHECK(created == 2);									// not made twice
	VERIFY(g);
}

TEST(Host_TheDefaultLayoutCanBeRestored) {
	Fixture f;
	f.AddStandard();
	std::wstring error;
	CHECK(!f.Host.HasDefaultLayout() && !f.Host.ResetLayout(&error) && !error.empty());
	f.Host.CaptureDefaultLayout();
	CHECK(f.Host.HasDefaultLayout());
	const auto original = f.Host.Layout().Dump();

	auto& l = f.Host.Layout();
	l.AutoHide(f.Sol->Group());
	l.Float(f.Output, OffScreen(50, 50, 300, 200));
	l.Hide(f.Props);
	CHECK(l.Dump() != original);
	CHECK(f.Host.ResetLayout());
	CHECK_STR(l.Dump(), original);
	VERIFY(f);
	CHECK(FramesOf(f).empty());
}

TEST(Host_NamedLayoutsAreSavedAndApplied) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	const auto standard = l.Dump();
	CHECK(f.Host.SaveLayoutAs(L"Standard"));
	CHECK(!f.Host.SaveLayoutAs(L""));

	l.AutoHide(f.Output->Group());
	l.Hide(f.Props);
	const auto debug = l.Dump();
	CHECK(f.Host.SaveLayoutAs(L"Debug"));
	CHECK(f.Host.Layouts().Count() == 2);

	std::wstring error;
	CHECK(f.Host.ApplyLayout(L"Standard"));
	CHECK_STR(l.Dump(), standard);
	VERIFY(f);
	CHECK(f.Host.ApplyLayout(L"Debug"));
	CHECK_STR(l.Dump(), debug);
	VERIFY(f);
	CHECK(!f.Host.ApplyLayout(L"Nope", &error) && !error.empty());
	CHECK_STR(l.Dump(), debug);

	// the menu of them
	HMENU menu = ::CreatePopupMenu();
	CHECK(f.Host.FillLayoutMenu(menu, 5000) == 2);
	wchar_t text[64]{};
	::GetMenuStringW(menu, 1, text, _countof(text), MF_BYPOSITION);
	CHECK(std::wstring(text) == L"Debug" && ::GetMenuItemID(menu, 0) == 5000 && ::GetMenuItemID(menu, 1) == 5001);
	::DestroyMenu(menu);
	CHECK(f.Host.HandleLayoutCommand(5000, 5000));
	CHECK_STR(l.Dump(), standard);
	CHECK(!f.Host.HandleLayoutCommand(5002, 5000) && !f.Host.HandleLayoutCommand(4999, 5000));

	// and they survive a trip through a file
	const auto path = TempPath(L"layouts.json");
	CHECK(f.Host.Layouts().SaveToFile(path));
	f.Host.Layouts().Clear();
	CHECK(f.Host.Layouts().LoadFromFile(path) && f.Host.Layouts().Count() == 2);
	CHECK(f.Host.ApplyLayout(L"Debug"));
	CHECK_STR(l.Dump(), debug);
	::DeleteFileW(path.c_str());
}

TEST(Host_ThePaneMenuListsPanesAndShowsThem) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	l.Hide(f.Props);
	l.AutoHide(f.Output->Group());

	HMENU menu = ::CreatePopupMenu();
	CHECK(f.Host.FillPaneMenu(menu, 100) == 3);				// tools only
	CHECK(::GetMenuItemCount(menu) == 3);
	auto checked = [&](int position) { return (::GetMenuState(menu, position, MF_BYPOSITION) & MF_CHECKED) != 0; };
	CHECK(checked(0) && !checked(1) && checked(2));			// Sol showing, Props hidden, Output auto-hidden (still there)
	CHECK(::GetMenuItemID(menu, 0) == 100 && ::GetMenuItemID(menu, 1) == 101 && ::GetMenuItemID(menu, 2) == 102);
	::DestroyMenu(menu);

	HMENU documents = ::CreatePopupMenu();
	CHECK(f.Host.FillPaneMenu(documents, 200, PaneKind::Document) == 2 && ::GetMenuItemID(documents, 0) == 203);
	::DestroyMenu(documents);

	// a hidden pane is shown and active
	CHECK(f.Host.HandlePaneCommand(101, 100));
	CHECK(f.Props->State() == PaneState::Docked && f.Host.ActivePane() == f.Props);
	VERIFY(f);
	// an auto-hidden pane slides out
	CHECK(f.Host.HandlePaneCommand(102, 100));
	CHECK(f.Host.FlyoutPane() == f.Output);
	f.Host.HideFlyout();
	// a docked one is activated
	CHECK(f.Host.HandlePaneCommand(100, 100) && f.Host.ActivePane() == f.Sol);
	CHECK(!f.Host.HandlePaneCommand(99, 100) && !f.Host.HandlePaneCommand(500, 100));
	VERIFY(f);
}

TEST(Host_AllDocumentsCanBeClosedAtOnce) {
	Fixture f;
	Docs d = AddDocs(f);
	auto sol = f.Add(L"Sol", PaneKind::Tool);
	f.Host.Layout().Show(sol);
	f.Host.ActivatePane(d.B);
	Pump();

	// the active one is kept, and a veto keeps one more
	f.Host.OnPaneClosing = [&](DockPane* p) { return p != d.C; };
	CHECK(f.Host.CloseAllDocuments(true) == 1);
	CHECK(d.B->State() == PaneState::Document && d.C->State() == PaneState::Document);
	CHECK(d.A->State() == PaneState::Hidden);
	VERIFY(f);

	f.Host.OnPaneClosing = nullptr;
	CHECK(f.Host.CloseAllDocuments() == 2);
	CHECK(d.B->State() == PaneState::Hidden && d.C->State() == PaneState::Hidden);
	CHECK(sol->State() == PaneState::Docked);				// tools are none of its business
	CHECK(f.Host.CloseAllDocuments() == 0);
	VERIFY(f);
}

TEST(Host_TheNextDocumentWrapsAround) {
	Fixture f;
	Docs d = AddDocs(f);
	auto group = d.A->Group();
	CHECK(group->Panes().size() == 3);
	f.Host.ActivatePane(d.A);
	CHECK(f.Host.ActivateNextDocument() && group->ActivePane() == d.B);
	CHECK(f.Host.ActivateNextDocument() && group->ActivePane() == d.C);
	CHECK(f.Host.ActivateNextDocument() && group->ActivePane() == d.A);			// wrapped
	CHECK(f.Host.ActivateNextDocument(false) && group->ActivePane() == d.C);	// backwards wraps too
	CHECK(f.Host.ActivateNextDocument(false) && group->ActivePane() == d.B);
	VERIFY(f);

	f.Host.ClosePane(d.A);
	f.Host.ClosePane(d.C);
	CHECK(!f.Host.ActivateNextDocument());					// one tab: nothing to go to
	CHECK(group->ActivePane() == d.B);
}

// ---- Documents: floating and tab groups -------------------------------------------------

TEST(Host_DocumentsFloatIntoWindowsOfTheirOwn) {
	Fixture f;
	Docs d = AddDocs(f);
	CHECK(f.Host.CanExecute(DockCommand::Float, d.B) && !f.Host.CanExecute(DockCommand::Dock, d.B));

	const RECT rc = OffScreen(100, 100, 420, 320);
	CHECK(f.Host.FloatPane(d.B, &rc));
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Floating && FramesOf(f).size() == 1);
	CHECK(!f.Host.CanExecute(DockCommand::Float, d.B) && f.Host.CanExecute(DockCommand::Dock, d.B));

	// the document has its tab strip (and a close button) in the frame
	HWND window = f.Host.GroupWindow(d.B->Group());
	CHECK(window && ::GetParent(window) == FramesOf(f)[0]);
	const auto strip = StripOf(f, d.B->Group());
	CHECK(strip.Tabs.size() == 1 && !IsRectEmpty(&strip.Close[0]));
	CHECK(::IsWindowVisible(d.B->hWnd) != FALSE && ::GetParent(d.B->hWnd) == window);

	// the frame is titled after it, and closing the frame closes the document
	wchar_t title[64]{};
	::GetWindowTextW(FramesOf(f)[0], title, _countof(title));
	CHECK(std::wstring(title) == L"b.cpp");

	// back among the documents
	CHECK(f.Host.Execute(DockCommand::Dock, d.B));
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Document && d.B->Group() == d.A->Group() && FramesOf(f).empty());

	// toggling, and closing the window closes the pane
	CHECK(f.Host.ToggleFloat(d.C) && d.C->State() == PaneState::Floating);
	VERIFY(f);
	CHECK(f.Host.CloseFloatWindow(d.C->Group()->Float()->Id()));
	VERIFY(f);
	CHECK(d.C->State() == PaneState::Hidden && FramesOf(f).empty());
}

TEST(Host_ADocumentTabDraggedAwayFloatsAndCanBeDroppedBack) {
	Fixture f;
	Docs d = AddDocs(f);
	CHECK(f.Host.BeginDrag(d.B, false, ScreenCenterOf(f, d.A->Group())));
	f.Host.UpdateDrag(NowhereOnScreen);
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Float);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Floating && d.B->Group()->IsDocument());

	// dropping another document tab on the floating window makes a tab there
	CHECK(f.Host.BeginDrag(d.C, false, ScreenCenterOf(f, d.A->Group())));
	f.Host.UpdateDrag(ScreenCenterOf(f, d.B->Group()));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Tab && f.Host.CurrentDropTarget().Group == d.B->Group());
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(d.C->Group() == d.B->Group() && d.C->State() == PaneState::Floating);

	// and the whole window goes back with the Dock command on either tab
	CHECK(f.Host.Execute(DockCommand::Dock, d.C));
	VERIFY(f);
	CHECK(d.B->State() == PaneState::Document && d.C->State() == PaneState::Document && d.B->Group() == d.A->Group());
}

TEST(Host_TabGroupCommands) {
	Fixture f;
	Docs d = AddDocs(f);
	auto& l = f.Host.Layout();
	CHECK(!f.Host.CanExecute(DockCommand::MoveToNextGroup, d.A));			// there is only one group
	CHECK(f.Host.CanExecute(DockCommand::NewHorizontalGroup, d.B) && f.Host.CanExecute(DockCommand::NewVerticalGroup, d.B));

	CHECK(f.Host.Execute(DockCommand::NewVerticalGroup, d.B));
	VERIFY(f);
	CHECK(l.DocumentGroups().size() == 2 && d.B->Group() != d.A->Group());
	CHECK(d.B->Group() == l.DocumentGroups()[1]);							// beside, to the right
	CHECK(f.Host.CanExecute(DockCommand::MoveToNextGroup, d.A) && f.Host.CanExecute(DockCommand::MoveToPreviousGroup, d.A));

	CHECK(f.Host.Execute(DockCommand::NewHorizontalGroup, d.C));			// C was with A: it goes below
	VERIFY(f);
	CHECK(l.DocumentGroups().size() == 3);
	CHECK(!f.Host.CanExecute(DockCommand::NewHorizontalGroup, d.B));		// alone in its group

	// moving on wraps around, and a group emptied by it is gone
	CHECK(f.Host.Execute(DockCommand::MoveToNextGroup, d.B));
	VERIFY(f);
	CHECK(d.B->Group() == d.A->Group() && l.DocumentGroups().size() == 2);
	CHECK(f.Host.Execute(DockCommand::MoveToPreviousGroup, d.B));
	VERIFY(f);
	CHECK(d.B->Group() == d.C->Group() && l.DocumentGroups().size() == 2);
	CHECK_VALID(l);
}

TEST(Host_NewDocumentsOpenInTheGroupThatWasUsedLast) {
	Fixture f;
	Docs d = AddDocs(f);
	auto& l = f.Host.Layout();
	CHECK(f.Host.Execute(DockCommand::NewVerticalGroup, d.C));
	VERIFY(f);

	auto extra = f.Add(L"d.cpp", PaneKind::Document);
	f.Host.ActivatePane(d.A);
	CHECK(f.Host.ShowPane(extra));
	CHECK(extra->Group() == d.A->Group());
	f.Host.ActivatePane(d.C);
	auto more = f.Add(L"e.cpp", PaneKind::Document);
	CHECK(f.Host.ShowPane(more));
	CHECK(more->Group() == d.C->Group());
	VERIFY(f);
	CHECK(l.ActiveDocumentGroup() == d.C->Group());
}

// ---- Keyboard and the window switcher ---------------------------------------------------

// A key message for a window, as the message loop would hand it to PreTranslateMessage.
MSG KeyMessage(HWND window, UINT message, UINT vk) {
	MSG msg{};
	msg.hwnd = window;
	msg.message = message;
	msg.wParam = vk;
	return msg;
}

TEST(Host_TheMostRecentPanesComeFirstInTheSwitcher) {
	Fixture f;
	f.AddStandard();
	f.Host.ActivatePane(f.A);
	f.Host.ActivatePane(f.Sol);
	f.Host.ActivatePane(f.B);
	f.Host.ActivatePane(f.Props);
	Pump();

	CHECK(f.Host.ShowNavigator(true));
	CHECK(f.Host.IsNavigatorOpen() && f.Host.NavigatorWindow() && ::IsWindowVisible(f.Host.NavigatorWindow()));
	const auto& docs = f.Host.Navigator().Items(DockNavigator::Column::Documents);
	const auto& tools = f.Host.Navigator().Items(DockNavigator::Column::Tools);
	CHECK(docs.size() == 2 && docs[0] == f.B && docs[1] == f.A);
	CHECK(tools.size() == 3 && tools[0] == f.Props && tools[1] == f.Sol && tools[2] == f.Output);
	CHECK(f.Host.Navigator().Selected() == f.B);						// a tool window is active: the last document used
	f.Host.CancelNavigator();
	CHECK(!f.Host.IsNavigatorOpen() && !f.Host.NavigatorWindow());
}

TEST(Host_TheSwitcherMovesWithTheKeysAndGoesWhereControlIsReleased) {
	Fixture f;
	f.AddStandard();
	auto c = f.Add(L"c.cpp", PaneKind::Document);
	f.Host.Layout().Show(c);
	f.Host.ActivatePane(f.A);
	f.Host.ActivatePane(f.B);
	f.Host.ActivatePane(c);				// most recent first: c, b, a
	Pump();

	// Ctrl+Tab (as the shortcut handler sees it): opens on b.cpp, another Tab goes on to a.cpp
	CHECK(f.Host.HandleShortcut(VK_TAB, true, true, false, false));
	CHECK(f.Host.IsNavigatorOpen() && f.Host.Navigator().Selected() == f.B);
	MSG tab = KeyMessage(f.Frame, WM_KEYDOWN, VK_TAB);
	CHECK(f.Host.PreTranslateMessage(&tab));							// while it is open the keys are its
	CHECK(f.Host.Navigator().Selected() == f.A);
	CHECK(f.Host.HandleShortcut(VK_TAB, true, true, true, false));		// Ctrl+Shift+Tab: back
	CHECK(f.Host.Navigator().Selected() == f.B);
	CHECK(f.Host.HandleShortcut(VK_DOWN, true, false, false, false) && f.Host.Navigator().Selected() == f.A);
	CHECK(f.Host.HandleShortcut(VK_UP, true, false, false, false) && f.Host.Navigator().Selected() == f.B);

	// releasing Control goes to the selected pane
	MSG release = KeyMessage(f.Frame, WM_KEYUP, VK_CONTROL);
	CHECK(!f.Host.PreTranslateMessage(&release));						// the release is not swallowed
	CHECK(!f.Host.IsNavigatorOpen());
	CHECK(f.Host.ActivePane() == f.B);
	VERIFY(f);

	// backwards from the start: the least recently used file
	CHECK(f.Host.HandleShortcut(VK_TAB, true, true, true, false));
	CHECK(f.Host.Navigator().Selected() == f.A);
	f.Host.CancelNavigator();
}

TEST(Host_TheSwitcherReachesToolWindowsAndAutoHiddenOnes) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().AutoHide(f.Output->Group());
	f.Host.ActivatePane(f.A);
	Pump();

	CHECK(f.Host.ShowNavigator(true));
	CHECK(f.Host.HandleShortcut(VK_RIGHT, true, false, false, false));	// to the tool windows
	CHECK(f.Host.Navigator().CurrentColumn() == DockNavigator::Column::Tools);
	while (f.Host.Navigator().Selected() != f.Output)
		f.Host.NavigatorMove(1);
	CHECK(f.Host.HandleShortcut(VK_RETURN, true, false, false, false));
	CHECK(!f.Host.IsNavigatorOpen());
	CHECK(f.Host.FlyoutPane() == f.Output);								// an auto-hidden pane slides out
	f.Host.HideFlyout();

	// Escape gives up
	f.Host.ActivatePane(f.A);
	CHECK(f.Host.ShowNavigator(true));
	f.Host.NavigatorSwitchColumn();
	MSG escape = KeyMessage(f.Frame, WM_KEYDOWN, VK_ESCAPE);
	CHECK(f.Host.PreTranslateMessage(&escape));
	CHECK(!f.Host.IsNavigatorOpen() && f.Host.ActivePane() == f.A);
	CHECK(!f.Host.CommitNavigator());
}

TEST(Host_ClickingARowOfTheSwitcherGoesThere) {
	Fixture f;
	f.AddStandard();
	f.Host.ActivatePane(f.Sol);
	Pump();
	CHECK(f.Host.ShowNavigator(true));
	CDockHost& host = f.Host;
	HWND window = host.NavigatorWindow();
	CHECK(window != nullptr);

	// the rows are where the layout says, and pointing at one selects it
	RECT rc;
	::GetClientRect(window, &rc);
	CHECK(Width(rc) > 100 && Height(rc) > 60);
	const auto& tools = host.Navigator().Items(DockNavigator::Column::Tools);
	int row = -1;
	for (int i = 0; i < (int)tools.size(); i++)
		if (tools[i] == f.Output)
			row = i;
	CHECK(row >= 0);

	auto layoutRow = [&](DockNavigator::Column column, int index) {
		// the same computation the window does
		const int counts[2] = { (int)host.Navigator().Items(DockNavigator::Column::Documents).size(), (int)tools.size() };
		const int first[2] = { 0, 0 };
		auto layout = ComputeNavigatorLayout(counts, first, (int)host.Navigator().CurrentColumn(), host.Navigator().Row(), host.Metrics());
		return layout.Rows[(int)column][index];
	};
	const RECT target = layoutRow(DockNavigator::Column::Tools, row);
	::SendMessage(window, WM_MOUSEMOVE, 0, Pt(1, 1));						// the first move only tells where the mouse is
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(target));
	CHECK(host.Navigator().Selected() == f.Output);

	::SendMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, Center(target));
	CHECK(!host.IsNavigatorOpen() && host.ActivePane() == f.Output);
	VERIFY(f);
}

TEST(Host_TheSwitcherClosesWhenTheLayoutChanges) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.ShowNavigator(true));
	CHECK(f.Host.Layout().Hide(f.Props));
	CHECK(!f.Host.IsNavigatorOpen());
	CHECK(f.Host.ShowNavigator(true));
	f.Host.CancelNavigator();

	// destroying the host with the switcher open is fine (the fixture does it)
	CHECK(f.Host.ShowNavigator(false));
	CHECK(f.Host.IsNavigatorOpen());
}

TEST(Host_TheSwitcherIsDrawnInTheTheme) {
	Fixture f;
	f.AddStandard();
	f.Host.ActivatePane(f.A);
	Pump();
	CHECK(f.Host.ShowNavigator(true));
	HWND window = f.Host.NavigatorWindow();
	const DockTheme theme = f.Host.Theme();

	RECT rc;
	::GetClientRect(window, &rc);
	auto sampleAt = [&](int x, int y) {
		CClientDC screen(nullptr);
		CDC dc;
		dc.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
		HBITMAP old = dc.SelectBitmap(bmp);
		::PrintWindow(window, dc, PW_CLIENTONLY);
		const COLORREF color = dc.GetPixel(x, y);
		dc.SelectBitmap(old);
		return color;
	};

	const int counts[2] = { 2, 3 };
	const int first[2] = { 0, 0 };
	const auto layout = ComputeNavigatorLayout(counts, first, (int)f.Host.Navigator().CurrentColumn(), f.Host.Navigator().Row(), f.Host.Metrics());
	const RECT selected = layout.Rows[(int)f.Host.Navigator().CurrentColumn()][f.Host.Navigator().Row()];
	const RECT other = layout.Rows[1][2];
	CHECK(sampleAt(selected.right - 3, selected.top + 2) == theme.CaptionActiveBack);
	CHECK(sampleAt(other.right - 3, other.top + 2) == theme.GroupBack);

	f.Host.SetTheme(DockTheme::Dark());
	Pump();
	const DockTheme dark = DockTheme::Dark();
	CHECK(sampleAt(selected.right - 3, selected.top + 2) == dark.CaptionActiveBack);
	CHECK(sampleAt(other.right - 3, other.top + 2) == dark.GroupBack);
	f.Host.CancelNavigator();
}

TEST(Host_LongListsInTheSwitcherScroll) {
	Fixture f;
	std::vector<DockPane*> docs;
	for (int i = 0; i < 30; i++) {
		auto pane = f.Add((L"doc" + std::to_wstring(i)).c_str(), PaneKind::Document);
		f.Host.Layout().Show(pane);
		docs.push_back(pane);
	}
	Pump();
	CHECK(f.Host.ShowNavigator(true));
	for (int i = 0; i < 20; i++)
		f.Host.NavigatorMove(1);
	const int row = f.Host.Navigator().Row();
	auto window = f.Host.NavigatorWindow();
	CHECK(row == 20 && f.Host.Navigator().Selected() == docs[20]);
	RECT client;
	::GetClientRect(window, &client);
	CHECK(Height(client) > 0);
	f.Host.NavigatorMove(-30);												// all the way round
	CHECK(f.Host.Navigator().Row() == 20);
	CHECK(f.Host.CommitNavigator());
	CHECK(f.Host.ActivePane() == docs[20]);
	VERIFY(f);
}

TEST(Host_ShortcutsSwitchAndCloseTabs) {
	Fixture f;
	Docs d = AddDocs(f);
	auto group = d.A->Group();
	f.Host.ActivatePane(d.A);

	CHECK(f.Host.HandleShortcut(VK_F6, true, true, false, false) && group->ActivePane() == d.B);
	CHECK(f.Host.HandleShortcut(VK_F6, true, true, true, false) && group->ActivePane() == d.A);
	CHECK(!f.Host.HandleShortcut(VK_F6, false, true, false, false));		// releases are not shortcuts
	CHECK(!f.Host.HandleShortcut(VK_F6, true, false, false, false));		// F6 alone is nobody's

	// Ctrl+F4 closes the active document, and asks first like any other close
	int asked = 0;
	f.Host.OnPaneClosing = [&](DockPane*) { asked++; return true; };
	f.Host.ActivatePane(d.B);
	CHECK(f.Host.HandleShortcut(VK_F4, true, true, false, false));
	CHECK(asked == 1 && d.B->State() == PaneState::Hidden);
	VERIFY(f);
	d.C->Caps = PaneCaps::None;
	f.Host.ActivatePane(d.C);
	CHECK(!f.Host.HandleShortcut(VK_F4, true, true, false, false) && d.C->State() == PaneState::Document);

	// with the shortcuts off, none of it works
	f.Host.SetShortcutsEnabled(false);
	f.Host.ActivatePane(d.A);
	CHECK(!f.Host.HandleShortcut(VK_F6, true, true, false, false) && !f.Host.HandleShortcut(VK_TAB, true, true, false, false));
	CHECK(!f.Host.IsNavigatorOpen());
}

TEST(Host_ShortcutsMoveBetweenGroupsAndCloseToolWindows) {
	Fixture f;
	f.AddStandard();
	f.Host.ActivatePane(f.Sol);
	auto& l = f.Host.Layout();

	// the order: the main tree left to right (Sol, documents, Props, Output), then the floating windows
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, false, true) && f.Host.ActivePane() == f.B);
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, false, true) && f.Host.ActivePane() == f.Props);
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, false, true) && f.Host.ActivePane() == f.Output);
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, false, true) && f.Host.ActivePane() == f.Sol);		// wraps
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, true, true) && f.Host.ActivePane() == f.Output);	// Shift+Alt+F6

	// a floating window is part of the round, after the main window
	CHECK(l.Float(f.Output, OffScreen(50, 50, 300, 200)));
	f.Host.ActivatePane(f.Props);
	CHECK(f.Host.HandleShortcut(VK_F6, true, false, false, true) && f.Host.ActivePane() == f.Output);
	VERIFY(f);

	// Shift+Escape closes a tool window, but not a document
	CHECK(f.Host.HandleShortcut(VK_ESCAPE, true, false, true, false));
	CHECK(f.Output->State() == PaneState::Hidden);
	f.Host.ActivatePane(f.B);
	CHECK(!f.Host.HandleShortcut(VK_ESCAPE, true, false, true, false) && f.B->State() == PaneState::Document);
	VERIFY(f);
}

// Cancels the menu that the tests open, from inside its message loop.
void CALLBACK EndMenuTimer(HWND, UINT, UINT_PTR id, DWORD) {
	::KillTimer(nullptr, id);
	::EndMenu();
}

TEST(Host_AltMinusOpensTheMenuOfTheActivePane) {
	Fixture f;
	f.AddStandard();
	f.Host.ActivatePane(f.Sol);
	int built = 0;
	DockPane* menuPane = nullptr;
	f.Host.OnBuildPaneMenu = [&](DockPane* pane, HMENU) {
		built++;
		menuPane = pane;
	};
	::SetTimer(nullptr, 0, 80, EndMenuTimer);
	CHECK(f.Host.HandleShortcut(VK_OEM_MINUS, true, false, false, true));
	CHECK(built == 1 && menuPane == f.Sol);

	f.Host.OnBuildPaneMenu = nullptr;
	Pump();
}

TEST(Host_KeysForOtherWindowsAreLeftAlone) {
	Fixture f;
	f.AddStandard();
	HWND stranger = ::CreateWindowExW(0, L"STATIC", L"elsewhere", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, nullptr, nullptr);
	MSG msg = KeyMessage(stranger, WM_KEYDOWN, VK_F6);
	CHECK(!f.Host.PreTranslateMessage(&msg));
	msg = KeyMessage(f.Frame, WM_MOUSEMOVE, 0);
	CHECK(!f.Host.PreTranslateMessage(&msg));
	msg = KeyMessage(f.Host, WM_KEYDOWN, 'A');
	CHECK(!f.Host.PreTranslateMessage(&msg));
	CHECK(!f.Host.PreTranslateMessage(nullptr));

	// the navigator takes keys from anywhere while it is open
	f.Host.ShowNavigator(true);
	msg = KeyMessage(stranger, WM_KEYDOWN, VK_ESCAPE);
	CHECK(f.Host.PreTranslateMessage(&msg) && !f.Host.IsNavigatorOpen());
	::DestroyWindow(stranger);
}

// ---- Accessibility -----------------------------------------------------------------------

struct AccChild {
	LONG Id{};
	std::wstring Name;
	LONG Role{};
	LONG State{};
	RECT Where{};
	bool IsWindow{};
};

CComPtr<IAccessible> AccessibleOfClient(HWND window) {
	CComPtr<IAccessible> acc;
	if (FAILED(::AccessibleObjectFromWindow(window, (DWORD)OBJID_CLIENT, IID_IAccessible, (void**)&acc)))
		acc.Release();
	return acc;
}

VARIANT ChildId(LONG id) {
	VARIANT v;
	::VariantInit(&v);
	v.vt = VT_I4;
	v.lVal = id;
	return v;
}

std::wstring NameOf(IAccessible* acc, LONG id) {
	CComBSTR name;
	if (acc->get_accName(ChildId(id), &name) == S_OK && name)
		return std::wstring(name, name.Length());
	return {};
}

LONG RoleOf(IAccessible* acc, LONG id) {
	VARIANT role;
	::VariantInit(&role);
	if (acc->get_accRole(ChildId(id), &role) != S_OK || role.vt != VT_I4)
		return -1;
	return role.lVal;
}

LONG StateOf(IAccessible* acc, LONG id) {
	VARIANT state;
	::VariantInit(&state);
	if (acc->get_accState(ChildId(id), &state) != S_OK || state.vt != VT_I4)
		return -1;
	return state.lVal;
}

RECT LocationOf(IAccessible* acc, LONG id) {
	long l = 0, t = 0, w = 0, h = 0;
	acc->accLocation(&l, &t, &w, &h, ChildId(id));
	return { l, t, l + w, t + h };
}

std::vector<AccChild> ChildrenOf(IAccessible* acc) {
	std::vector<AccChild> list;
	long count = 0;
	acc->get_accChildCount(&count);
	for (long id = 1; id <= count; id++) {
		AccChild c;
		c.Id = id;
		c.Name = NameOf(acc, id);
		c.Role = RoleOf(acc, id);
		c.State = StateOf(acc, id);
		c.Where = LocationOf(acc, id);
		CComPtr<IDispatch> child;
		c.IsWindow = acc->get_accChild(ChildId(id), &child) == S_OK && child != nullptr;
		list.push_back(c);
	}
	return list;
}

const AccChild* FindChild(const std::vector<AccChild>& list, const wchar_t* name) {
	for (auto& c : list)
		if (c.Name == name)
			return &c;
	return nullptr;
}

TEST(Accessibility_AGroupExposesItsCaptionButtonsAndContent) {
	Fixture f;
	f.AddStandard();
	auto acc = AccessibleOfClient(f.Host.GroupWindow(f.Sol->Group()));
	CHECK(acc != nullptr);
	if (!acc)
		return;
	CHECK(NameOf(acc, CHILDID_SELF) == L"Sol" && RoleOf(acc, CHILDID_SELF) == ROLE_SYSTEM_GROUPING);

	const auto children = ChildrenOf(acc);
	const AccChild* title = FindChild(children, L"Sol");
	const AccChild* pin = FindChild(children, L"Auto Hide");
	const AccChild* close = FindChild(children, L"Close");
	CHECK(title && title->Role == ROLE_SYSTEM_TITLEBAR && pin && pin->Role == ROLE_SYSTEM_PUSHBUTTON && close && close->Role == ROLE_SYSTEM_PUSHBUTTON);
	CHECK(!children.empty() && children.back().IsWindow);							// the content comes last
	if (!title || !pin || !close)
		return;

	// their places are where they are drawn
	const auto parts = ComputeGroupParts(*f.Sol->Group(), [&] { RECT rc; ::GetClientRect(f.Host.GroupWindow(f.Sol->Group()), &rc); return rc; }(), f.Host.Metrics());
	RECT caption = parts.Caption;
	::MapWindowPoints(f.Host.GroupWindow(f.Sol->Group()), nullptr, reinterpret_cast<POINT*>(&caption), 2);
	CHECK(EqualRect(&title->Where, &caption) != FALSE);
	CHECK(close->Where.right <= caption.right && close->Where.left > pin->Where.right - 1 && pin->Where.left >= caption.left);

	// hit testing finds them
	VARIANT hit;
	::VariantInit(&hit);
	const POINT center{ (close->Where.left + close->Where.right) / 2, (close->Where.top + close->Where.bottom) / 2 };
	CHECK(acc->accHitTest(center.x, center.y, &hit) == S_OK && hit.vt == VT_I4 && hit.lVal == close->Id);
	::VariantClear(&hit);
	CHECK(acc->accHitTest(-30000, 0, &hit) == S_FALSE);

	// the default actions do what the buttons do
	CComBSTR action;
	CHECK(acc->get_accDefaultAction(ChildId(pin->Id), &action) == S_OK && std::wstring(action) == L"Press");
	CHECK(acc->accDoDefaultAction(ChildId(pin->Id)) == S_OK);
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::AutoHide);

	// the group window went with the pane; the old object answers with errors instead of crashing
	long count = 0;
	CHECK(FAILED(acc->get_accChildCount(&count)) || count >= 0);
}

TEST(Accessibility_TabsAreExposedAndCanBeSelectedAndClosed) {
	Fixture f;
	Docs d = AddDocs(f);
	auto group = d.A->Group();
	HWND window = f.Host.GroupWindow(group);
	auto acc = AccessibleOfClient(window);
	CHECK(acc != nullptr);
	if (!acc)
		return;
	f.Host.ActivatePane(d.B);

	auto children = ChildrenOf(acc);
	const AccChild* a = FindChild(children, L"a.cpp");
	const AccChild* b = FindChild(children, L"b.cpp");
	const AccChild* c = FindChild(children, L"c.cpp");
	CHECK(a && b && c && a->Role == ROLE_SYSTEM_PAGETAB);
	CHECK(FindChild(children, L"Close a.cpp") && FindChild(children, L"Close c.cpp"));
	CHECK(NameOf(acc, CHILDID_SELF) == L"Documents");
	if (!a || !b || !c)
		return;
	CHECK((b->State & STATE_SYSTEM_SELECTED) && !(a->State & STATE_SYSTEM_SELECTED) && !(c->State & STATE_SYSTEM_SELECTED));
	CHECK((a->State & STATE_SYSTEM_SELECTABLE) && (a->State & STATE_SYSTEM_FOCUSABLE));

	// the tab rectangles are the ones the strip has
	const auto strip = StripOf(f, group);
	RECT expected = strip.Tabs[1];
	::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&expected), 2);
	CHECK(EqualRect(&b->Where, &expected) != FALSE);

	// what is selected
	VARIANT selection;
	::VariantInit(&selection);
	CHECK(acc->get_accSelection(&selection) == S_OK && selection.vt == VT_I4 && selection.lVal == b->Id);

	// selecting and the default action activate the tab
	CHECK(acc->accSelect(SELFLAG_TAKESELECTION, ChildId(c->Id)) == S_OK);
	CHECK(group->ActivePane() == d.C);
	CComBSTR action;
	CHECK(acc->get_accDefaultAction(ChildId(a->Id), &action) == S_OK && std::wstring(action) == L"Switch");
	CHECK(acc->accDoDefaultAction(ChildId(a->Id)) == S_OK);
	CHECK(group->ActivePane() == d.A);
	VERIFY(f);

	// navigating among them
	children = ChildrenOf(acc);
	a = FindChild(children, L"a.cpp");
	VARIANT next;
	::VariantInit(&next);
	CHECK(acc->accNavigate(NAVDIR_NEXT, ChildId(a->Id), &next) == S_OK && next.vt == VT_I4 && NameOf(acc, next.lVal) == L"Close a.cpp");
	VARIANT first, last;
	::VariantInit(&first);
	::VariantInit(&last);
	CHECK(acc->accNavigate(NAVDIR_FIRSTCHILD, ChildId(CHILDID_SELF), &first) == S_OK && first.vt == VT_I4 && first.lVal == 1);
	CHECK(acc->accNavigate(NAVDIR_LASTCHILD, ChildId(CHILDID_SELF), &last) == S_OK && last.vt == VT_DISPATCH);
	::VariantClear(&last);
	CHECK(acc->accNavigate(NAVDIR_PREVIOUS, ChildId(1), &next) == S_FALSE);

	// the close button of a tab
	const AccChild* closeB = FindChild(children, L"Close b.cpp");
	CHECK(closeB != nullptr);
	if (closeB)
		CHECK(acc->accDoDefaultAction(ChildId(closeB->Id)) == S_OK && d.B->State() == PaneState::Hidden);
	VERIFY(f);
}

TEST(Accessibility_ATabScrolledOutOfTheStripIsOffscreen) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	Pump();
	auto group = docs[0]->Group();
	auto acc = AccessibleOfClient(f.Host.GroupWindow(group));
	CHECK(acc != nullptr);
	if (!acc)
		return;
	const auto state = f.Host.GetTabState(group);
	CHECK(state.Overflow && state.Visible < 25);

	const auto children = ChildrenOf(acc);
	int tabs = 0, offscreen = 0;
	for (auto& c : children) {
		if (c.Role == ROLE_SYSTEM_PAGETAB) {
			tabs++;
			offscreen += (c.State & STATE_SYSTEM_OFFSCREEN) != 0;
		}
	}
	CHECK(tabs == 25 && offscreen == 25 - state.Visible);
	CHECK(FindChild(children, L"Tab list") && FindChild(children, L"Tab list")->Role == ROLE_SYSTEM_BUTTONDROPDOWN);
}

TEST(Accessibility_TheHostListsTheGroupsAndTheAutoHideBars) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.Layout().AutoHide(f.Sol->Group()));
	Pump();
	auto acc = AccessibleOfClient(f.Host);
	CHECK(acc != nullptr);
	if (!acc)
		return;
	CHECK(NameOf(acc, CHILDID_SELF) == L"Docking area" && RoleOf(acc, CHILDID_SELF) == ROLE_SYSTEM_PANE);

	const auto children = ChildrenOf(acc);
	const AccChild* item = FindChild(children, L"Sol (auto hidden left)");
	CHECK(item != nullptr && item->Role == ROLE_SYSTEM_PUSHBUTTON);
	int windows = 0;
	for (auto& c : children)
		windows += c.IsWindow;
	CHECK(windows == 3);														// documents, Props, Output
	if (!item)
		return;

	RECT bar;
	CHECK(f.Host.GetBarItemRect(f.Sol, bar));
	::MapWindowPoints(f.Host, nullptr, reinterpret_cast<POINT*>(&bar), 2);
	CHECK(EqualRect(&item->Where, &bar) != FALSE);

	CHECK(acc->accDoDefaultAction(ChildId(item->Id)) == S_OK);
	CHECK(f.Host.FlyoutPane() == f.Sol);
	CHECK((StateOf(acc, item->Id) & STATE_SYSTEM_PRESSED) != 0);
	f.Host.HideFlyout();
}

TEST(Accessibility_TheObjectSurvivesItsWindow) {
	CComPtr<IAccessible> acc;
	{
		Fixture f;
		f.AddStandard();
		acc = AccessibleOfClient(f.Host.GroupWindow(f.Sol->Group()));
		CHECK(acc != nullptr);
	}
	// the window is gone: the object is not, and says so
	if (acc) {
		long count = 0;
		CComBSTR name;
		CHECK(FAILED(acc->get_accChildCount(&count)));
		CHECK(FAILED(acc->get_accName(ChildId(CHILDID_SELF), &name)));
		VARIANT hit;
		::VariantInit(&hit);
		CHECK(FAILED(acc->accHitTest(0, 0, &hit)));
		CHECK(FAILED(acc->accDoDefaultAction(ChildId(1))));
	}
}

TEST(Accessibility_TheObjectIsAutomationCallable) {
	Fixture f;
	f.AddStandard();
	auto acc = AccessibleOfClient(f.Host.GroupWindow(f.Sol->Group()));
	CHECK(acc != nullptr);
	if (!acc)
		return;
	CComQIPtr<IDispatch> dispatch(acc);
	CHECK(dispatch != nullptr);
	UINT infos = 0;
	CHECK(dispatch->GetTypeInfoCount(&infos) == S_OK);
	if (infos == 0)
		return;		// no type library on this machine: nothing more to say

	// what a scripting client does: look the property up by name and get it
	OLECHAR* names[] = { const_cast<OLECHAR*>(L"accName") };
	DISPID id = 0;
	CHECK(dispatch->GetIDsOfNames(IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id) == S_OK);
	VARIANTARG arg = ChildId(CHILDID_SELF);
	DISPPARAMS params{ &arg, nullptr, 1, 0 };
	CComVariant result;
	CHECK(dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &params, &result, nullptr, nullptr) == S_OK);
	CHECK(result.vt == VT_BSTR && std::wstring(result.bstrVal) == L"Sol");
}

TEST(Accessibility_AGroupTellsClientsWhenItsTabsChange) {
	Fixture f;
	Docs d = AddDocs(f);
	static int events = 0;
	events = 0;
	HWINEVENTHOOK hook = ::SetWinEventHook(EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER, nullptr,
		[](HWINEVENTHOOK, DWORD, HWND, LONG idObject, LONG, DWORD, DWORD) { events += idObject == OBJID_CLIENT; },
		::GetCurrentProcessId(), ::GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
	CHECK(hook != nullptr);
	Pump();
	events = 0;
	f.Host.Layout().Hide(d.C);
	Pump();
	::Sleep(20);
	Pump();
	const int afterHide = events;
	f.Host.Layout().Show(d.C);
	Pump();
	::Sleep(20);
	Pump();
	::UnhookWinEvent(hook);
	CHECK(afterHide >= 1 && events > afterHide);
}

// ---- Per-window DPI --------------------------------------------------------------------

TEST(Host_AFloatingWindowOnAnotherMonitorUsesItsOwnMetrics) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Sol, OffScreen(100, 100, 500, 400)));
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Bottom));			// two groups: both have a caption
	VERIFY(f);
	const int id = f.Sol->Group()->Float()->Id();
	const int hostDpi = f.Host.Dpi();
	CHECK(f.Host.FloatDpi(id) == f.Host.GroupDpi(f.Sol->Group()) && f.Host.FloatDpi(id) >= 96);

	HWND frame = f.Host.FloatWindow(id);
	RECT before;
	::GetWindowRect(frame, &before);
	MINMAXINFO info{};
	::SendMessage(frame, WM_GETMINMAXINFO, 0, (LPARAM)&info);
	const LONG minWidthBefore = info.ptMinTrackSize.x;

	// the window is dragged to a monitor that is twice as sharp
	const int sharp = f.Host.FloatDpi(id) * 2;
	RECT bigger = before;
	bigger.right = bigger.left + Width(before) * 2;
	bigger.bottom = bigger.top + Height(before) * 2;
	CHECK(f.Host.SetFloatDpi(id, sharp, &bigger));
	VERIFY(f);
	CHECK(f.Host.FloatDpi(id) == sharp && f.Host.GroupDpi(f.Sol->Group()) == sharp && f.Host.GroupDpi(f.Output->Group()) == hostDpi);

	RECT after;
	::GetWindowRect(frame, &after);
	CHECK(EqualRect(&after, &bigger) != FALSE);

	// its groups have the chrome of that DPI: a caption twice as high, and their content starts below it
	const DockMetrics sharpMetrics = DockMetrics::ForDpi(sharp);
	CHECK(&f.Host.MetricsFor(sharp) != &f.Host.Metrics() && f.Host.MetricsFor(sharp).CaptionHeight == sharpMetrics.CaptionHeight);
	CHECK(f.Host.MetricsFor(sharp).CaptionHeight > f.Host.Metrics().CaptionHeight);
	HWND solWindow = f.Host.GroupWindow(f.Sol->Group());
	const int contentTop = RectIn(f.Sol->hWnd, solWindow).top;
	CHECK(contentTop >= sharpMetrics.CaptionHeight && contentTop < sharpMetrics.CaptionHeight + 4);
	CHECK(f.Host.FontFor(sharp) != f.Host.Font());

	// while the main window's groups keep theirs
	HWND outputWindow = f.Host.GroupWindow(f.Output->Group());
	CHECK(RectIn(f.Output->hWnd, outputWindow).top < sharpMetrics.CaptionHeight);

	// the window cannot be made smaller than its content needs at that DPI
	::SendMessage(frame, WM_GETMINMAXINFO, 0, (LPARAM)&info);
	CHECK(info.ptMinTrackSize.x > minWidthBefore);

	// a real WM_DPICHANGED does the same, and takes the rectangle it is given
	RECT back = before;
	::SendMessage(frame, WM_DPICHANGED, MAKEWPARAM(hostDpi, hostDpi), (LPARAM)&back);
	VERIFY(f);
	CHECK(f.Host.FloatDpi(id) == hostDpi);
	::GetWindowRect(frame, &after);
	CHECK(EqualRect(&after, &before) != FALSE);

	// and docking it puts the panes in the main window with the main window's metrics
	CHECK(f.Host.SetFloatDpi(id, sharp));
	CHECK(f.Host.Execute(DockCommand::Dock, f.Sol));
	VERIFY(f);
	CHECK(f.Host.GroupDpi(f.Sol->Group()) == hostDpi);
}

TEST(Host_TheGroupsOfAFloatAtAnotherDpiDrawWithItsFonts) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Sol, OffScreen(100, 100, 500, 400)));
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Bottom));
	const int id = f.Sol->Group()->Float()->Id();
	CHECK(f.Host.SetFloatDpi(id, f.Host.Dpi() * 2));
	Pump();

	// the caption of a group is drawn in the colours of the theme and as high as the DPI says
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	RECT rc;
	::GetClientRect(window, &rc);
	CClientDC screen(nullptr);
	CDC dc;
	dc.CreateCompatibleDC(screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
	HBITMAP old = dc.SelectBitmap(bmp);
	::PrintWindow(window, dc, PW_CLIENTONLY);
	const DockMetrics metrics = DockMetrics::ForDpi(f.Host.Dpi() * 2);
	const DockTheme theme = f.Host.Theme();
	const int inside = metrics.CaptionHeight - 2;								// still in the caption: at the host's DPI this is below it
	const COLORREF captionColor = dc.GetPixel(2, inside);
	const COLORREF belowCaption = dc.GetPixel(2, metrics.CaptionHeight + 2);
	dc.SelectBitmap(old);
	CHECK(captionColor == theme.CaptionActiveBack || captionColor == theme.CaptionInactiveBack);
	CHECK(belowCaption != captionColor);
}

TEST(Host_ANewFloatTakesTheDpiOfItsMonitor) {
	Fixture f;
	f.AddStandard();
	// a float that is made has the DPI of the frame window that shows it, which is what Windows says it is
	CHECK(f.Host.Layout().Float(f.Sol, OffScreen(100, 100, 500, 400)));
	const int id = f.Sol->Group()->Float()->Id();
	HWND frame = f.Host.FloatWindow(id);
	CHECK(frame != nullptr);
	CHECK(f.Host.FloatDpi(id) == (int)::GetDpiForWindow(frame));
	VERIFY(f);
}

TEST(Host_SavedFloatsKeepTheirDpi) {
	std::string text;
	{
		Fixture f;
		f.AddStandard();
		CHECK(f.Host.Layout().Float(f.Sol, OffScreen(100, 100, 500, 400)));
		CHECK(f.Host.SetFloatDpi(f.Sol->Group()->Float()->Id(), 192));
		text = f.Host.SaveState(false);
	}
	Fixture g;
	g.AddStandard();
	CHECK(g.Host.LoadState(text, {}, nullptr, false));
	CHECK(g.Sol->State() == PaneState::Floating);
	// the frame is on a monitor of its own DPI, which the layout then follows
	const int id = g.Sol->Group()->Float()->Id();
	CHECK(g.Host.FloatDpi(id) == (int)::GetDpiForWindow(g.Host.FloatWindow(id)));
	VERIFY(g);
}

TEST(Host_MovingAFloatingWindowByItsTitleBarKeepsTheGuides) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.Layout().Float(f.Output, OffScreen(100, 100, 400, 300)));
	const int id = f.Output->Group()->Float()->Id();
	HWND frame = f.Host.FloatWindow(id);
	::SendMessage(frame, WM_NCLBUTTONDOWN, HTCAPTION, 0);
	::SendMessage(frame, WM_ENTERSIZEMOVE, 0, 0);
	CHECK(f.Host.IsDragging());

	// the window is moved (the move loop does this) and the drag goes on
	for (int i = 1; i <= 3; i++) {
		::SetWindowPos(frame, nullptr, -11800 + i * 30, 120 + i * 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		CHECK(f.Host.IsDragging());
		f.Host.UpdateDrag(ScreenCenterOf(f, f.Sol->Group()));
		CHECK(f.Host.IsDragging() && f.Host.VisibleGuides() > 0);
	}
	::SendMessage(frame, WM_EXITSIZEMOVE, 0, 0);
	CHECK(!f.Host.IsDragging());
}

// ---- Tooltips and modified marks ------------------------------------------------------

RECT ToScreen(HWND window, RECT rc) {
	::MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&rc), 2);
	return rc;
}

TEST(Host_TabsShowTheirTooltips) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.SetTipTiming(0, 0);
	d.A->Tooltip = L"C:\\Projects\\Demo\\a.cpp";
	HWND window = f.Host.GroupWindow(d.A->Group());
	const auto strip = StripOf(f, d.A->Group());

	CHECK(!f.Host.IsTipVisible());
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Tabs[0]));
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == L"C:\\Projects\\Demo\\a.cpp");
	const RECT tab = ToScreen(window, strip.Tabs[0]);
	const RECT tip = f.Host.TipRect();
	CHECK(tip.top >= tab.bottom && tip.left >= tab.left - 1 && Width(tip) > 40 && Height(tip) > 10);

	// a tab without one (and not cut off) has none
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Tabs[1]));
	CHECK(!f.Host.IsTipVisible());
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Tabs[0]));
	CHECK(f.Host.IsTipVisible());

	// it goes with the mouse, with a click and with a change to the layout
	::SendMessage(window, WM_MOUSELEAVE, 0, 0);
	CHECK(!f.Host.IsTipVisible());
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Tabs[0]));
	CHECK(f.Host.IsTipVisible());
	::SendMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.Tabs[0]));
	::SendMessage(window, WM_LBUTTONUP, 0, Center(strip.Tabs[0]));
	CHECK(!f.Host.IsTipVisible());
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Tabs[0]));
	CHECK(f.Host.IsTipVisible());
	CHECK(f.Host.Layout().Hide(d.C));
	CHECK(!f.Host.IsTipVisible());

	// and nothing shows when they are off
	f.Host.SetTipsEnabled(false);
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(StripOf(f, d.A->Group()).Tabs[0]));
	CHECK(!f.Host.IsTipVisible());
}

TEST(Host_TheCaptionButtonsSayWhatTheyDo) {
	Fixture f;
	f.AddStandard();
	f.Host.SetTipTiming(0, 0);
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	RECT client;
	::GetClientRect(window, &client);
	const auto buttons = ComputeCaptionButtons(ComputeGroupParts(*f.Sol->Group(), client, f.Host.Metrics()).Caption, true, true, true, f.Host.Metrics());

	::SendMessage(window, WM_MOUSEMOVE, 0, Center(buttons.Close));
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == L"Close");
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(buttons.Pin));
	CHECK(f.Host.TipText() == L"Auto Hide");
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(buttons.Menu));
	CHECK(f.Host.TipText() == L"Window Position");
	// the caption itself has nothing to say when its title fits
	::SendMessage(window, WM_MOUSEMOVE, 0, Pt(buttons.Menu.left - 60, buttons.Menu.top + 2));
	CHECK(!f.Host.IsTipVisible());

	// a tab of a document group has a close button, and the strip of many tabs a list button
	HWND docs = f.Host.GroupWindow(f.A->Group());
	const auto strip = StripOf(f, f.A->Group());
	::SendMessage(docs, WM_MOUSEMOVE, 0, Center(strip.Close[0]));
	CHECK(f.Host.TipText() == L"Close");
	::SendMessage(docs, WM_MOUSELEAVE, 0, 0);
}

TEST(Host_ATitleThatIsCutOffShowsInFull) {
	Fixture f(600, 400);
	f.AddStandard();
	f.Host.SetTipTiming(0, 0);
	f.Sol->Title = L"A solution explorer with a title much too long for its caption";
	f.Host.Sync();
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	RECT client;
	::GetClientRect(window, &client);
	const auto parts = ComputeGroupParts(*f.Sol->Group(), client, f.Host.Metrics());
	const auto buttons = ComputeCaptionButtons(parts.Caption, true, true, true, f.Host.Metrics());
	::SendMessage(window, WM_MOUSEMOVE, 0, Pt(parts.Caption.left + 10, parts.Caption.top + 4));
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == f.Sol->Title);
	(void)buttons;

	// the application's own text wins, and comes even when the title fits
	f.Sol->Title = L"Sol";
	f.Sol->Tooltip = L"The tool window of solutions";
	f.Host.Sync();
	::SendMessage(window, WM_MOUSEMOVE, 0, Pt(parts.Caption.left + 12, parts.Caption.top + 5));
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == L"The tool window of solutions");
}

TEST(Host_ATipWaitsForTheMouseToRest) {
	// (asked for directly: the mouse messages of a test are followed by a leave, since the real mouse is elsewhere)
	Fixture f;
	f.Host.SetTipTiming(60, 0);
	const RECT first{ 100, 100, 160, 120 }, second{ 170, 100, 230, 120 };
	const int dpi = f.Host.Dpi();

	f.Host.RequestTip(f.Host, first, L"first", dpi);
	CHECK(!f.Host.IsTipVisible());
	::Sleep(120);
	Pump();
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == L"first");
	// the next target while one is up: at once
	f.Host.RequestTip(f.Host, second, L"second", dpi);
	CHECK(f.Host.IsTipVisible() && f.Host.TipText() == L"second");
	// asking again for what is showing changes nothing, and someone else cannot cancel it
	f.Host.RequestTip(f.Host, second, L"second", dpi);
	f.Host.CancelTip(nullptr);
	CHECK(f.Host.IsTipVisible());

	// leaving before it comes cancels it
	f.Host.CancelTip(f.Host);
	CHECK(!f.Host.IsTipVisible());
	f.Host.RequestTip(f.Host, first, L"first", dpi);
	f.Host.CancelTip(f.Host);
	::Sleep(120);
	Pump();
	CHECK(!f.Host.IsTipVisible());

	// a tip goes by itself after its time
	f.Host.SetTipTiming(0, 60);
	f.Host.RequestTip(f.Host, first, L"first", dpi);
	CHECK(f.Host.IsTipVisible());
	::Sleep(150);
	Pump();
	CHECK(!f.Host.IsTipVisible());
}

TEST(Host_TheTipIsDrawnInTheThemeAndLetsTheMouseThrough) {
	Fixture f;
	Docs d = AddDocs(f);
	d.A->Tooltip = L"C:\\Projects\\Demo\\a.cpp";
	f.Host.SetTipTiming(0, 0);
	HWND window = f.Host.GroupWindow(d.A->Group());
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(StripOf(f, d.A->Group()).Tabs[0]));
	HWND tip = f.Host.TipWindow();
	CHECK(tip != nullptr);
	if (!tip)
		return;
	CHECK(::SendMessage(tip, WM_NCHITTEST, 0, 0) == HTTRANSPARENT);
	CHECK(::SendMessage(tip, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
	CHECK((::GetWindowLong(tip, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);

	auto corner = [&] {
		RECT rc;
		::GetClientRect(tip, &rc);
		CClientDC screen(nullptr);
		CDC dc;
		dc.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
		HBITMAP old = dc.SelectBitmap(bmp);
		::PrintWindow(tip, dc, PW_CLIENTONLY);
		const COLORREF border = dc.GetPixel(0, 0), inside = dc.GetPixel(3, 3);
		dc.SelectBitmap(old);
		return std::make_pair(border, inside);
	};
	auto light = corner();
	CHECK(light.first == f.Host.Theme().GuideBorder && light.second == f.Host.Theme().GuideBack);
	f.Host.SetTheme(DockTheme::Dark());
	auto dark = corner();
	CHECK(dark.first == DockTheme::Dark().GuideBorder && dark.second == DockTheme::Dark().GuideBack);
	f.Host.HideTip();
}

TEST(Host_ATipStaysOnTheMonitor) {
	// near the bottom of the screen it goes above what it describes
	Fixture f;
	Docs d = AddDocs(f);
	d.A->Tooltip = L"tooltip";
	f.Host.SetTipTiming(0, 0);
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
	RECT target{ mi.rcWork.right - 30, mi.rcWork.bottom - 20, mi.rcWork.right - 5, mi.rcWork.bottom - 2 };
	f.Host.RequestTip(f.Host, target, L"a tip at the corner of the screen", f.Host.Dpi());
	CHECK(f.Host.IsTipVisible());
	const RECT tip = f.Host.TipRect();
	CHECK(tip.right <= mi.rcWork.right && tip.bottom <= mi.rcWork.bottom && tip.left >= mi.rcWork.left);
	CHECK(tip.bottom <= target.top);
	f.Host.CancelTip(f.Host);
	CHECK(!f.Host.IsTipVisible());
}

TEST(Host_AModifiedDocumentShowsADotInPlaceOfItsCloseButton) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.A);
	Pump();
	HWND window = f.Host.GroupWindow(d.A->Group());
	const DockTheme theme = f.Host.Theme();

	auto sample = [&](RECT slot, int dx) {
		RECT rc;
		::GetClientRect(window, &rc);
		CClientDC screen(nullptr);
		CDC dc;
		dc.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
		HBITMAP old = dc.SelectBitmap(bmp);
		::PrintWindow(window, dc, PW_CLIENTONLY);
		const COLORREF c = dc.GetPixel((slot.left + slot.right) / 2 + dx, (slot.top + slot.bottom) / 2);
		dc.SelectBitmap(old);
		return c;
	};

	const auto before = StripOf(f, d.A->Group());
	const RECT closeB = before.Close[1];					// b.cpp: a tab that is neither selected nor under the mouse
	const int off = MarkSize(f.Host.Metrics()) / 2 - 1;
	CHECK(sample(closeB, off) == theme.TabInactiveBack);	// nothing drawn there

	d.B->Modified = true;
	f.Host.RefreshPane(d.B);
	Pump();
	const auto after = StripOf(f, d.A->Group());
	CHECK(EqualRect(&after.Tabs[1], &before.Tabs[1]) != FALSE);	// the tab does not change its size
	CHECK(sample(closeB, off) == theme.ButtonGlyph);		// the dot

	d.B->Modified = false;
	f.Host.RefreshPane(d.B);
	Pump();
	CHECK(sample(closeB, off) == theme.TabInactiveBack);
}

TEST(Host_AModifiedTabWithoutACloseButtonMakesRoomForItsMark) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.Layout().DockTo(f.Output, f.Sol->Group(), DockPosition::Tab));		// tool tabs have no close button
	Pump();
	const auto before = StripOf(f, f.Sol->Group());
	CHECK(before.Tabs.size() == 2 && IsRectEmpty(&before.Close[1]) && IsRectEmpty(&before.Mark[1]));

	f.Output->Modified = true;
	f.Host.RefreshPane(f.Output);
	Pump();
	const auto after = StripOf(f, f.Sol->Group());
	const int index = f.Sol->Group()->Panes()[0] == f.Output ? 0 : 1;
	CHECK(Width(after.Tabs[index]) == Width(before.Tabs[index]) + f.Host.Metrics().TabIconGap + MarkSize(f.Host.Metrics()));
	CHECK(!IsRectEmpty(&after.Mark[index]) && IsRectEmpty(&after.Mark[1 - index]));
	CHECK(after.Mark[index].left >= after.Tabs[index].left && after.Mark[index].right <= after.Tabs[index].right);

	// the caption and the floating window's title carry it too
	f.Host.ActivatePane(f.Output);
	CHECK(f.Host.Layout().Float(f.Output, OffScreen(50, 50, 300, 250)));
	Pump();
	wchar_t title[128]{};
	::GetWindowTextW(f.Host.FloatWindow(f.Output->Group()->Float()->Id()), title, _countof(title));
	CHECK(std::wstring(title) == L"Output \u25CF");
	f.Output->Modified = false;
	f.Host.RefreshPane(f.Output);
	::GetWindowTextW(f.Host.FloatWindow(f.Output->Group()->Float()->Id()), title, _countof(title));
	CHECK(std::wstring(title) == L"Output");
	f.Host.RefreshPane(nullptr);							// nothing to refresh is no harm
	VERIFY(f);
}

TEST(Accessibility_ModifiedAndTooltipReachScreenReaders) {
	Fixture f;
	Docs d = AddDocs(f);
	d.A->Tooltip = L"C:\\Projects\\a.cpp";
	d.B->Modified = true;
	auto acc = AccessibleOfClient(f.Host.GroupWindow(d.A->Group()));
	CHECK(acc != nullptr);
	if (!acc)
		return;
	const auto children = ChildrenOf(acc);
	const AccChild* a = FindChild(children, L"a.cpp");
	const AccChild* b = FindChild(children, L"b.cpp");
	const AccChild* c = FindChild(children, L"c.cpp");
	CHECK(a && b && c);
	if (!a || !b || !c)
		return;
	auto describe = [&](LONG id) {
		CComBSTR text;
		return acc->get_accDescription(ChildId(id), &text) == S_OK && text ? std::wstring(text) : std::wstring();
	};
	CHECK(describe(a->Id) == L"C:\\Projects\\a.cpp" && describe(b->Id) == L"Modified" && describe(c->Id).empty());
}

// ---- The Windows dialog ---------------------------------------------------------------------

// Runs 'step' inside the message loop of the modal dialog, as soon as it is on screen (a timer on the thread).
struct DialogDriver {
	static inline std::function<void(HWND)> Step;
	static inline UINT_PTR Timer = 0;
	static inline int Tries = 0;

	static void CALLBACK Tick(HWND, UINT, UINT_PTR, DWORD) {
		HWND dialog = ::FindWindowW(L"#32770", L"Windows");
		if ((!dialog || !::IsWindowVisible(dialog)) && ++Tries < 100)
			return;		// (not there yet, or not shown yet)
		if (dialog && !::IsWindowVisible(dialog))
			dialog = nullptr;
		::KillTimer(nullptr, Timer);
		Timer = 0;
		auto step = std::move(Step);
		Step = nullptr;
		if (dialog && step)
			step(dialog);
		if (dialog && ::IsWindow(dialog))
			::SendMessage(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);		// whatever the step did, do not hang
	}

	static void Start(std::function<void(HWND)> step) {
		Step = std::move(step);
		Tries = 0;
		Timer = ::SetTimer(nullptr, 0, 30, Tick);
	}
};

HWND Dlg(HWND dialog, int id) {
	return ::GetDlgItem(dialog, id);
}

void Press(HWND dialog, int id) {
	::SendMessage(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)Dlg(dialog, id));
}

int RowOf(HWND list, const wchar_t* name) {
	for (int i = 0, n = ListView_GetItemCount(list); i < n; i++) {
		wchar_t text[128]{};
		ListView_GetItemText(list, i, 0, text, _countof(text));
		if (wcscmp(text, name) == 0)
			return i;
	}
	return -1;
}

std::wstring CellOf(HWND list, int row, int column) {
	wchar_t text[128]{};
	ListView_GetItemText(list, row, column, text, _countof(text));
	return text;
}

void SelectOnly(HWND list, std::initializer_list<int> rows) {
	ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
	for (int row : rows)
		ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

TEST(Windows_TheListShowsTheDocumentsAndCanIncludeToolWindows) {
	Fixture f;
	f.AddStandard();
	auto c = f.Add(L"c.cpp", PaneKind::Document);
	f.Host.Layout().Show(c);
	f.Host.Layout().Float(f.Output, OffScreen(50, 50, 300, 200));
	f.B->Modified = true;
	f.Host.ActivatePane(f.A);

	int documents = -1, all = -1;
	std::wstring type, state, modified, floating;
	bool saveHidden = false;
	DialogDriver::Start([&](HWND dialog) {
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		documents = ListView_GetItemCount(list);
		const int row = RowOf(list, L"b.cpp");
		type = CellOf(list, row, 1);
		state = CellOf(list, row, 2);
		modified = CellOf(list, row, 3);
		saveHidden = !::IsWindowVisible(Dlg(dialog, WindowsDialogIds::Save));
		::SendMessage(Dlg(dialog, WindowsDialogIds::IncludeTools), BM_CLICK, 0, 0);
		all = ListView_GetItemCount(list);
		floating = CellOf(list, RowOf(list, L"Output"), 2);
	});
	CHECK(!f.Host.ShowWindowsDialog());
	CHECK(documents == 3 && all == 6);
	CHECK(type == L"Document" && state == L"Open" && modified == L"Yes");
	CHECK(floating == L"Floating");
	CHECK(saveHidden);												// there is no way to save unless the application gives one
	CHECK(f.Host.ActivePane() == f.A);								// closing the dialog changes nothing
}

TEST(Windows_ActivateGoesToTheSelectedWindow) {
	Fixture f;
	f.AddStandard();
	f.Host.Layout().AutoHide(f.Output->Group());
	f.Host.ActivatePane(f.A);

	DialogDriver::Start([&](HWND dialog) {
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		SelectOnly(list, { RowOf(list, L"b.cpp") });
		Press(dialog, WindowsDialogIds::Activate);
	});
	CHECK(f.Host.ShowWindowsDialog());
	CHECK(f.Host.ActivePane() == f.B && f.A->Group()->ActivePane() == f.B);

	// an auto-hidden tool window slides out
	DialogDriver::Start([&](HWND dialog) {
		::SendMessage(Dlg(dialog, WindowsDialogIds::IncludeTools), BM_CLICK, 0, 0);
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		SelectOnly(list, { RowOf(list, L"Output") });
		Press(dialog, WindowsDialogIds::Activate);
	});
	CHECK(f.Host.ShowWindowsDialog());
	CHECK(f.Host.FlyoutPane() == f.Output);
	f.Host.HideFlyout();
}

TEST(Windows_CloseWindowsClosesTheSelectedOnesAndHonoursAVeto) {
	Fixture f;
	f.AddStandard();
	auto c = f.Add(L"c.cpp", PaneKind::Document);
	f.Host.Layout().Show(c);
	f.Host.OnPaneClosing = [&](DockPane* p) { return p != f.B; };

	int before = 0, after = 0;
	DialogDriver::Start([&](HWND dialog) {
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		before = ListView_GetItemCount(list);
		SelectOnly(list, { RowOf(list, L"a.cpp"), RowOf(list, L"b.cpp") });
		Press(dialog, WindowsDialogIds::CloseWindows);
		after = ListView_GetItemCount(list);
	});
	CHECK(!f.Host.ShowWindowsDialog());
	CHECK(before == 3 && after == 2);
	CHECK(f.A->State() == PaneState::Hidden && f.B->State() == PaneState::Document && c->State() == PaneState::Document);
	VERIFY(f);
}

TEST(Windows_SaveIsForTheApplicationToDo) {
	Fixture f;
	f.AddStandard();
	f.A->Modified = true;
	f.B->Modified = true;
	std::vector<std::wstring> saved;
	f.Host.OnPaneSave = [&](DockPane* p) {
		saved.push_back(p->Title);
		return p != f.B;				// b.cpp cannot be saved
	};

	bool visible = false, enabledWithModified = false, enabledWithout = true;
	int stillModified = 0;
	DialogDriver::Start([&](HWND dialog) {
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		visible = ::IsWindowVisible(Dlg(dialog, WindowsDialogIds::Save)) != FALSE;
		SelectOnly(list, { RowOf(list, L"a.cpp") });
		enabledWithModified = ::IsWindowEnabled(Dlg(dialog, WindowsDialogIds::Save)) != FALSE;
		ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);	// both
		Press(dialog, WindowsDialogIds::Save);
		for (int i = 0, n = ListView_GetItemCount(list); i < n; i++)
			stillModified += CellOf(list, i, 3) == L"Yes";
		SelectOnly(list, { RowOf(list, L"a.cpp") });
		enabledWithout = ::IsWindowEnabled(Dlg(dialog, WindowsDialogIds::Save)) != FALSE;
	});
	f.Host.ShowWindowsDialog();
	CHECK(visible && enabledWithModified);
	CHECK(saved.size() == 2 && !f.A->Modified && f.B->Modified);
	CHECK(stillModified == 1);
	CHECK(!enabledWithout);												// a.cpp is saved now: nothing to save
}

TEST(Windows_ClickingAColumnSortsAndClickingAgainReverses) {
	Fixture f;
	f.AddStandard();
	auto c = f.Add(L"c.cpp", PaneKind::Document);
	f.Host.Layout().Show(c);
	f.B->Title = L"zeta.cpp";
	f.Host.Sync();

	std::wstring firstAscending, firstDescending;
	DialogDriver::Start([&](HWND dialog) {
		HWND list = Dlg(dialog, WindowsDialogIds::List);
		auto click = [&](int column) {
			NMLISTVIEW nm{};
			nm.hdr = { list, (UINT_PTR)WindowsDialogIds::List, LVN_COLUMNCLICK };
			nm.iSubItem = column;
			::SendMessage(dialog, WM_NOTIFY, WindowsDialogIds::List, (LPARAM)&nm);
		};
		click(0);														// the dialog starts sorted by name, so this reverses it
		firstDescending = CellOf(list, 0, 0);
		click(0);
		firstAscending = CellOf(list, 0, 0);
	});
	f.Host.ShowWindowsDialog();
	CHECK(firstDescending == L"zeta.cpp" && firstAscending == L"a.cpp");
}

TEST(Windows_TheDialogCanBeDrivenWithNoWindowsAtAll) {
	Fixture f;
	int count = -1;
	bool activateEnabled = true;
	DialogDriver::Start([&](HWND dialog) {
		count = ListView_GetItemCount(Dlg(dialog, WindowsDialogIds::List));
		activateEnabled = ::IsWindowEnabled(Dlg(dialog, WindowsDialogIds::Activate)) != FALSE;
	});
	CHECK(!f.Host.ShowWindowsDialog());
	CHECK(count == 0 && !activateEnabled);
}

// ---- Keyboard focus in the chrome ------------------------------------------------------------

void Key(HWND window, UINT vk) {
	::SendMessage(window, WM_KEYDOWN, vk, 0);
}

TEST(Host_TheKeyboardCanVisitTheTabsOfAGroup) {
	Fixture f;
	Docs d = AddDocs(f);
	HWND window = f.Host.GroupWindow(d.A->Group());
	f.Host.ActivatePane(d.B);
	Pump();

	CHECK(!f.Host.IsChromeFocused());
	CHECK(f.Host.FocusChrome());
	CHECK(f.Host.IsChromeFocused() && ::GetFocus() == window);
	CHECK(f.Host.ChromeFocusName() == L"b.cpp");						// the tab of the active pane

	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"Close b.cpp");					// its close button
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"c.cpp");
	Key(window, VK_LEFT);
	Key(window, VK_LEFT);
	CHECK(f.Host.ChromeFocusName() == L"b.cpp");
	Key(window, VK_HOME);
	CHECK(f.Host.ChromeFocusName() == L"a.cpp");
	Key(window, VK_LEFT);
	CHECK(f.Host.ChromeFocusName() == L"a.cpp");						// it stops at the ends
	Key(window, VK_END);
	CHECK(f.Host.ChromeFocusName() == L"Close c.cpp");
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"Close c.cpp");

	// the keyboard moving over tabs does not switch them; Enter does, and the focus goes into the content
	CHECK(d.A->Group()->ActivePane() == d.B);
	Key(window, VK_LEFT);
	CHECK(f.Host.ChromeFocusName() == L"c.cpp");
	Key(window, VK_RETURN);
	CHECK(d.A->Group()->ActivePane() == d.C && f.Host.ActivePane() == d.C);
	CHECK(!f.Host.IsChromeFocused());
	VERIFY(f);
}

TEST(Host_KeysDoWhatTheFocusedItemDoes) {
	Fixture f;
	Docs d = AddDocs(f);
	HWND window = f.Host.GroupWindow(d.A->Group());
	f.Host.ActivatePane(d.A);

	// a close button closes its tab
	CHECK(f.Host.FocusChrome(d.B));
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"Close b.cpp");
	Key(window, VK_SPACE);
	CHECK(d.B->State() == PaneState::Hidden);
	VERIFY(f);

	// Delete closes the tab that is on, and the ring stays on the group
	CHECK(f.Host.FocusChrome(d.C));
	window = f.Host.GroupWindow(d.A->Group());
	CHECK(f.Host.IsChromeFocused());
	Key(window, VK_DELETE);
	CHECK(d.C->State() == PaneState::Hidden);
	VERIFY(f);

	// Escape goes back to the content
	CHECK(f.Host.FocusChrome(d.A));
	window = f.Host.GroupWindow(d.A->Group());
	Key(window, VK_ESCAPE);
	CHECK(!f.Host.IsChromeFocused() && f.Host.ActivePane() == d.A);
}

TEST(Host_TheCaptionButtonsAreReachableFromTheKeyboard) {
	Fixture f;
	f.AddStandard();
	HWND window = f.Host.GroupWindow(f.Sol->Group());

	CHECK(f.Host.FocusChrome(f.Sol));
	CHECK(f.Host.ChromeFocusName() == L"Window Position");			// a lone tool window has no tabs: the buttons are all there is
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"Auto Hide");
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"Close");
	Key(window, VK_LEFT);
	Key(window, VK_RETURN);											// Auto Hide
	CHECK(f.Sol->State() == PaneState::AutoHide);
	VERIFY(f);
}

TEST(Host_TabMovesTheChromeFocusToTheNextGroup) {
	Fixture f;
	f.AddStandard();
	CHECK(f.Host.FocusChrome(f.Sol));
	HWND window = f.Host.GroupWindow(f.Sol->Group());
	Key(window, VK_TAB);
	CHECK(f.Host.IsChromeFocused() && ::GetFocus() == f.Host.GroupWindow(f.A->Group()));	// the documents follow the left tool window
	CHECK(f.Host.ChromeFocusName() == L"b.cpp");
	window = f.Host.GroupWindow(f.A->Group());
	Key(window, VK_TAB);
	CHECK(::GetFocus() == f.Host.GroupWindow(f.Props->Group()));
	CHECK(f.Host.FocusNextChrome(f.Props, false));					// and back
	CHECK(::GetFocus() == f.Host.GroupWindow(f.A->Group()));
	// with one group there is nowhere to go
	Fixture single;
	auto only = single.Add(L"only.cpp", PaneKind::Document);
	single.Host.Layout().Show(only);
	CHECK(single.Host.FocusChrome(only));
	CHECK(!single.Host.FocusNextChrome(only));
}

TEST(Host_TheShortcutTakesTheKeyboardToTheChrome) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.C);
	CHECK(f.Host.HandleShortcut(VK_F6, true, true, false, true));
	CHECK(f.Host.IsChromeFocused() && f.Host.ChromeFocusName() == L"c.cpp");
	f.Host.SetShortcutsEnabled(false);
	CHECK(f.Host.FocusChrome(d.A));
	f.Host.ActivatePane(d.A);
	CHECK(!f.Host.HandleShortcut(VK_F6, true, true, false, true));
}

TEST(Host_ATabOutOfViewIsScrolledInWhenTheKeyboardGoesThere) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.ActivatePane(docs[24]);
	Pump();
	auto group = docs[0]->Group();
	CHECK(f.Host.GetTabState(group).First > 0);

	CHECK(f.Host.FocusChrome(docs[0]));
	Pump();
	CHECK(f.Host.GetTabState(group).First == 0);
	HWND window = f.Host.GroupWindow(group);
	Key(window, VK_END);
	CHECK(f.Host.ChromeFocusName() == L"Tab list");						// the last item is the list button
	Key(window, VK_LEFT);
	CHECK(f.Host.ChromeFocusName() == L"Scroll tabs right");			// the arrows come between the tabs and the list button
	Key(window, VK_LEFT);
	CHECK(f.Host.ChromeFocusName() == L"Scroll tabs left");
	Key(window, VK_LEFT);
	Pump();
	const auto state = f.Host.GetTabState(group);
	CHECK(f.Host.ChromeFocusName() == L"Close document24.cpp");
	CHECK(state.First + state.Visible == 25);							// the last tab is in view
}

TEST(Host_TheFocusRingIsDrawnOnTheItemAndGoesWithTheFocus) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.A);
	HWND window = f.Host.GroupWindow(d.A->Group());
	Pump();

	auto capture = [&](const RECT& tab) {
		RECT rc;
		::GetClientRect(window, &rc);
		CClientDC screen(nullptr);
		CDC dc;
		dc.CreateCompatibleDC(screen);
		CBitmap bmp;
		bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
		HBITMAP old = dc.SelectBitmap(bmp);
		::PrintWindow(window, dc, PW_CLIENTONLY);
		std::vector<COLORREF> row;
		for (int x = tab.left + 4; x < tab.right - 4; x++)
			row.push_back(dc.GetPixel(x, tab.top + 2));		// the top edge of the ring
		dc.SelectBitmap(old);
		return row;
	};

	const RECT tab = StripOf(f, d.A->Group()).Tabs[1];		// b.cpp: not selected
	const auto plain = capture(tab);
	CHECK(f.Host.FocusChrome(d.B));
	Key(window, VK_LEFT);
	Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"b.cpp");
	Pump();
	const auto ringed = capture(tab);
	CHECK(plain != ringed);
	::SetFocus(d.A->hWnd);									// the focus goes to the content: the ring goes
	Pump();
	CHECK(!f.Host.IsChromeFocused());
	CHECK(capture(tab) == plain);
}

TEST(Host_ShiftF10OpensTheMenuOfTheTab) {
	Fixture f;
	Docs d = AddDocs(f);
	DockPane* menuPane = nullptr;
	f.Host.OnBuildPaneMenu = [&](DockPane* pane, HMENU) { menuPane = pane; };
	CHECK(f.Host.FocusChrome(d.B));
	HWND window = f.Host.GroupWindow(d.A->Group());
	::SetTimer(nullptr, 0, 80, EndMenuTimer);
	Key(window, VK_APPS);
	CHECK(menuPane == d.B);
	f.Host.OnBuildPaneMenu = nullptr;
	Pump();
}

// ---- A floating window keeps its size when it is docked ----------------------------------------

SIZE FloatClient(Fixture& f, DockPane* pane) {
	RECT rc;
	::GetClientRect(f.Host.FloatWindow(pane->Group()->Float()->Id()), &rc);
	return { Width(rc), Height(rc) };
}

TEST(Host_ADockedFloatHasTheWidthOrHeightItHadFloating) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();

	// the window was made wider than the tool window was, and docks back at its left
	CHECK(l.Float(f.Sol, OffScreen(100, 100, 560, 420)));
	SIZE size = FloatClient(f, f.Sol);
	CHECK(f.Host.Execute(DockCommand::Dock, f.Sol));
	VERIFY(f);
	CHECK(f.Sol->Group()->Side() == DockSide::Left && Width(f.Sol->Group()->Rect) == size.cx);

	// the one at the bottom keeps its height
	CHECK(l.Float(f.Output, OffScreen(100, 100, 700, 330)));
	size = FloatClient(f, f.Output);
	CHECK(f.Host.ToggleFloat(f.Output));
	VERIFY(f);
	CHECK(f.Output->Group()->Side() == DockSide::Bottom && Height(f.Output->Group()->Rect) == size.cy);
}

TEST(Host_DroppingAFloatOnAnEdgeOrBesideAGroupKeepsItsSize) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Sol, OffScreen(100, 100, 360, 420)));
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Tab));					// two tabs: a pane can be dragged out alone
	SIZE size = FloatClient(f, f.Sol);

	// a tab dragged to the right edge: the preview and the result are as wide as the window was
	CHECK(f.Host.BeginDrag(f.Props, false, ScreenCenterOf(f, f.Sol->Group())));
	f.Host.UpdateDrag(EdgeGuide(f, DockSide::Right));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Edge);
	CHECK(Width(f.Host.CurrentDropTarget().Preview) == size.cx);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Props->Group()->Side() == DockSide::Right && Width(f.Props->Group()->Rect) == size.cx);

	// the whole window dropped beside the documents (on the compass): the same
	size = FloatClient(f, f.Sol);
	CHECK(f.Host.BeginDrag(f.Sol, true, ScreenCenterOf(f, f.Sol->Group())));
	f.Host.UpdateDrag(ArmOf(f, f.Output->Group(), DockPosition::Left));
	CHECK(f.Host.CurrentDropTarget().Type == DropTarget::Kind::Side);
	CHECK(f.Host.EndDrag(true));
	VERIFY(f);
	CHECK(f.Sol->State() == PaneState::Docked && Width(f.Sol->Group()->Rect) == size.cx);
}

// ---- Pinned, preview and coloured tabs -----------------------------------------------------------

COLORREF PixelAt(HWND window, int x, int y) {
	RECT rc;
	::GetClientRect(window, &rc);
	CClientDC screen(nullptr);
	CDC dc;
	dc.CreateCompatibleDC(screen);
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(screen, Width(rc), Height(rc));
	HBITMAP old = dc.SelectBitmap(bmp);
	::PrintWindow(window, dc, PW_CLIENTONLY);
	const COLORREF c = dc.GetPixel(x, y);
	dc.SelectBitmap(old);
	return c;
}

TEST(Host_PinnedTabsMoveLeftAndSpareTheCloseAllCommands) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.C);
	CHECK(f.Host.CanExecute(DockCommand::PinTab, d.C) && !f.Host.CanExecute(DockCommand::UnpinTab, d.C));
	CHECK(f.Host.Execute(DockCommand::PinTab, d.C));
	CHECK(d.A->Group()->Panes()[0] == d.C);
	VERIFY(f);
	CHECK(!f.Host.CanExecute(DockCommand::PinTab, d.C) && f.Host.CanExecute(DockCommand::UnpinTab, d.C));

	// "close all tabs" and "close all but this" leave it
	CHECK(f.Host.Execute(DockCommand::CloseAll, d.A));
	CHECK(d.A->State() == PaneState::Hidden && d.B->State() == PaneState::Hidden && d.C->State() == PaneState::Document);
	auto e = f.Add(L"e.cpp", PaneKind::Document);
	f.Host.Layout().Show(e);
	f.Host.Layout().Show(d.A);
	CHECK(f.Host.Execute(DockCommand::CloseOthers, e));
	CHECK(d.A->State() == PaneState::Hidden && d.C->State() == PaneState::Document && e->State() == PaneState::Document);
	f.Host.Layout().Show(d.A);
	CHECK(f.Host.CloseAllDocuments() == 2 && d.C->State() == PaneState::Document);	// e.cpp and a.cpp: the pinned one stays
	// but it can be closed by name
	CHECK(f.Host.ClosePane(d.C));
	VERIFY(f);
}

TEST(Host_ThePinButtonOfATabUnpinsIt) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.Execute(DockCommand::PinTab, d.B);
	HWND window = f.Host.GroupWindow(d.A->Group());
	auto strip = StripOf(f, d.A->Group());
	CHECK(strip.Tabs.size() == 3 && !IsRectEmpty(&strip.Close[0]));			// the pinned tab, first, has the button

	// the tooltip and the accessible name say what it does
	f.Host.SetTipTiming(0, 0);
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.Close[0]));
	CHECK(f.Host.TipText() == L"Unpin");
	f.Host.HideTip();
	auto acc = AccessibleOfClient(window);
	CHECK(acc && FindChild(ChildrenOf(acc), L"Unpin b.cpp") && FindChild(ChildrenOf(acc), L"Close a.cpp"));

	// the pin glyph is drawn in the slot: something other than the tab's background
	const RECT slot = strip.Close[0];
	bool drawn = false;
	for (int x = slot.left; x < slot.right && !drawn; x++)
		for (int y = slot.top; y < slot.bottom && !drawn; y++)
			drawn = PixelAt(window, x, y) != f.Host.Theme().TabInactiveBack && PixelAt(window, x, y) != f.Host.Theme().TabActiveBack &&
				PixelAt(window, x, y) != f.Host.Theme().TabHotBack;
	CHECK(drawn);

	// a middle click does not close it; a click on the button unpins
	Click(window, Center(strip.Tabs[0]), WM_MBUTTONDOWN, WM_MBUTTONUP);
	CHECK(d.B->State() == PaneState::Document);
	Click(window, Center(strip.Close[0]));
	CHECK(!d.B->Pinned() && d.B->State() == PaneState::Document);
	VERIFY(f);
	strip = StripOf(f, d.A->Group());
	CHECK(!IsRectEmpty(&strip.Close[0]));
}

TEST(Host_APreviewReplacesThePreviousPreview) {
	Fixture f;
	Docs d = AddDocs(f);
	auto p1 = f.Add(L"p1.cpp", PaneKind::Document);
	auto p2 = f.Add(L"p2.cpp", PaneKind::Document);
	auto p3 = f.Add(L"p3.cpp", PaneKind::Document);
	std::vector<DockPane*> closed;
	f.Host.OnPaneClosed = [&](DockPane* p) { closed.push_back(p); };

	CHECK(f.Host.ShowPreview(p1));
	CHECK(p1->Preview && p1->State() == PaneState::Document && f.Host.ActivePane() == p1);
	CHECK(f.Host.ShowPreview(p2));
	CHECK(p1->State() == PaneState::Hidden && p2->State() == PaneState::Document && closed.size() == 1 && closed[0] == p1);
	CHECK(d.A->Group()->Panes().back() == p2);								// it is the last tab
	VERIFY(f);

	// a preview that has been edited is a document of its own and stays
	p2->Modified = true;
	f.Host.RefreshPane(p2);
	CHECK(!p2->Preview);
	CHECK(f.Host.ShowPreview(p3));
	CHECK(p2->State() == PaneState::Document && p3->Preview);

	// opening a document that is open already as a normal one only shows it
	CHECK(f.Host.ShowPreview(d.A));
	CHECK(!d.A->Preview && d.A->State() == PaneState::Document && f.Host.ActivePane() == d.A);

	// a double click on the tab of a preview, and pinning it, promote it
	HWND window = f.Host.GroupWindow(d.A->Group());
	const auto strip = StripOf(f, d.A->Group());
	int index = 0;
	for (int i = 0; i < (int)d.A->Group()->Panes().size(); i++)
		if (d.A->Group()->Panes()[i] == p3)
			index = i;
	::SendMessage(window, WM_LBUTTONDBLCLK, MK_LBUTTON, Center(strip.Tabs[index]));
	CHECK(!p3->Preview);
	auto p4 = f.Add(L"p4.cpp", PaneKind::Document);
	CHECK(f.Host.ShowPreview(p4));
	CHECK(p3->State() == PaneState::Document);								// promoted, so not replaced
	CHECK(f.Host.Execute(DockCommand::PinTab, p4));
	CHECK(!p4->Preview && p4->Pinned());
	CHECK(!f.Host.ShowPreview(nullptr) && !f.Host.ShowPreview(f.Add(L"tool", PaneKind::Tool)));
	VERIFY(f);
}

TEST(Host_APreviewTabIsSetApartFromTheOthers) {
	Fixture f;
	Docs d = AddDocs(f);
	auto p = f.Add(L"preview.cpp", PaneKind::Document);
	f.Host.ShowPreview(p);
	auto acc = AccessibleOfClient(f.Host.GroupWindow(d.A->Group()));
	CHECK(acc != nullptr);
	if (!acc)
		return;
	const auto children = ChildrenOf(acc);
	const AccChild* tab = FindChild(children, L"preview.cpp");
	CHECK(tab != nullptr);
	CComBSTR description;
	if (tab)
		CHECK(acc->get_accDescription(ChildId(tab->Id), &description) == S_OK && std::wstring(description) == L"Preview");
	CHECK(DockWindowList::StateText(*p) == L"Open, preview");
	f.Host.Execute(DockCommand::PinTab, d.A);
	CHECK(DockWindowList::StateText(*d.A) == L"Open, pinned");
}

TEST(Host_ATabWithAColourHasAStripeInIt) {
	Fixture f;
	Docs d = AddDocs(f);
	f.Host.ActivatePane(d.A);
	Pump();
	HWND window = f.Host.GroupWindow(d.A->Group());
	const RECT tab = StripOf(f, d.A->Group()).Tabs[1];					// b.cpp

	const int x = (tab.left + tab.right) / 2, y = tab.bottom - 2;
	CHECK(PixelAt(window, x, y) != RGB(200, 30, 60));
	d.B->TabColor = RGB(200, 30, 60);
	f.Host.RefreshPane(d.B);
	Pump();
	CHECK(PixelAt(window, x, y) == RGB(200, 30, 60));					// along the edge that faces the content
	CHECK(PixelAt(window, x, tab.top + 1) != RGB(200, 30, 60));
	d.B->TabColor = CLR_INVALID;
	f.Host.RefreshPane(d.B);
	Pump();
	CHECK(PixelAt(window, x, y) != RGB(200, 30, 60));
}

// ---- Tabs in several rows ---------------------------------------------------------------------

TEST(Host_TabsCanBeShownInSeveralRows) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.ActivatePane(docs[3]);
	Pump();
	auto group = docs[0]->Group();
	CHECK(f.Host.GetTabState(group).Overflow && f.Host.GetTabState(group).Rows == 1);

	f.Host.SetMultiRowTabs(true);
	Pump();
	auto state = f.Host.GetTabState(group);
	CHECK(f.Host.MultiRowTabs() && !state.Overflow && state.Rows > 1 && state.Visible == 25);
	VERIFY(f);

	// every tab has a place inside the strip, which is as high as the rows need; the active tab's row is the lowest
	const auto strip = StripOf(f, group);
	const DockMetrics& m = f.Host.Metrics();
	int lowest = 0, highest = 1 << 20;
	for (auto& tab : strip.Tabs) {
		lowest = std::max<int>(lowest, tab.top);
		highest = std::min<int>(highest, tab.top);
	}
	CHECK(strip.Tabs.size() == 25 && (lowest - highest) / m.TabHeight == state.Rows - 1);
	CHECK(strip.Tabs[3].top == lowest);
	RECT client;
	::GetClientRect(f.Host.GroupWindow(group), &client);
	CHECK(RectIn(docs[3]->hWnd, f.Host.GroupWindow(group)).top == state.Rows * m.TabHeight);

	// a click on a tab of another row activates it, and its row goes down next to the content
	HWND window = f.Host.GroupWindow(group);
	int other = -1;
	for (int i = 0; i < 25 && other < 0; i++)
		if (strip.Tabs[i].top == highest)
			other = i;
	CHECK(other >= 0);
	Click(window, Center(strip.Tabs[other]));
	CHECK(group->ActivePane() == docs[other]);
	VERIFY(f);
	CHECK(StripOf(f, group).Tabs[other].top == lowest);

	// all the tabs are reachable for accessibility and none is out of view
	auto acc = AccessibleOfClient(window);
	int offscreen = 0, tabs = 0;
	for (auto& c : ChildrenOf(acc)) {
		if (c.Role == ROLE_SYSTEM_PAGETAB) {
			tabs++;
			offscreen += (c.State & STATE_SYSTEM_OFFSCREEN) != 0;
		}
	}
	CHECK(tabs == 25 && offscreen == 0);

	// and back to one scrolling row
	f.Host.SetMultiRowTabs(false);
	Pump();
	CHECK(f.Host.GetTabState(group).Overflow && f.Host.GetTabState(group).Rows == 1);
	VERIFY(f);
}

TEST(Host_TheRowsFollowTheTabsAndTheWindow) {
	Fixture f(700, 400);
	Docs d = AddDocs(f);
	f.Host.SetMultiRowTabs(true);
	Pump();
	auto group = d.A->Group();
	CHECK(f.Host.GetTabState(group).Rows == 1);

	// more tabs than fit: another row; fewer again: back
	std::vector<DockPane*> more;
	for (int i = 0; i < 12; i++) {
		more.push_back(f.Add((L"a-longer-document-name" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(more.back());
	}
	Pump();
	const int rows = f.Host.GetTabState(group).Rows;
	CHECK(rows > 1);
	VERIFY(f);
	::SetWindowPos(f.Host, nullptr, 0, 0, 1900, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);		// wider: fewer rows
	Pump();
	CHECK(f.Host.GetTabState(group).Rows < rows);
	VERIFY(f);

	// a title that gets longer, and a mark that needs room, can add a row
	::SetWindowPos(f.Host, nullptr, 0, 0, 700, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	Pump();
	const int before = f.Host.GetTabState(group).Rows;
	for (auto p : more)
		p->Title += L"-with-a-much-longer-title";
	f.Host.RefreshPane(more[0]);
	Pump();
	CHECK(f.Host.GetTabState(group).Rows > before);
	VERIFY(f);

	// tool tabs at the bottom too: their row next to the content is the top one
	auto tools = f.Add(L"Solution Explorer", PaneKind::Tool, DockSide::Left, 260);
	f.Host.Layout().Show(tools);
	for (int i = 0; i < 8; i++) {
		auto t = f.Add((L"A tool window number " + std::to_wstring(i)).c_str(), PaneKind::Tool, DockSide::Left, 260);
		f.Host.Layout().DockTo(t, tools->Group(), DockPosition::Tab);
	}
	Pump();
	CHECK(f.Host.GetTabState(tools->Group()).Rows > 1);
	VERIFY(f);
}

TEST(Host_ATabDraggedToAnotherRowChangesPlace) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 20; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.SetMultiRowTabs(true);
	f.Host.ActivatePane(docs[0]);
	Pump();
	auto group = docs[0]->Group();
	HWND window = f.Host.GroupWindow(group);
	const auto strip = StripOf(f, group);

	// pick a tab in the top row and one in another row
	int from = -1, to = -1;
	int highest = 1 << 20;
	for (auto& t : strip.Tabs)
		highest = std::min<int>(highest, t.top);
	for (int i = 0; i < 20; i++) {
		if (strip.Tabs[i].top == highest && from < 0)
			from = i;
		if (strip.Tabs[i].top != highest && to < 0 && i > from && from >= 0)
			to = i;
	}
	CHECK(from >= 0 && to > from);
	if (from < 0 || to < 0)
		return;
	DockPane* moved = docs[from];
	const RECT dest = strip.Tabs[to];
	::SendMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.Tabs[from]));
	::SendMessage(window, WM_MOUSEMOVE, MK_LBUTTON, Pt(Center(strip.Tabs[from]) & 0xFFFF, (LONG)(short)(dest.top + 3)));
	::SendMessage(window, WM_MOUSEMOVE, MK_LBUTTON, Pt(dest.right - 6, (dest.top + dest.bottom) / 2));
	::SendMessage(window, WM_LBUTTONUP, 0, Pt(dest.right - 6, (dest.top + dest.bottom) / 2));
	int index = 0;
	for (int i = 0; i < (int)group->Panes().size(); i++)
		if (group->Panes()[i] == moved)
			index = i;
	CHECK(index > from);
	VERIFY(f);
}

TEST(Host_TheKeyboardVisitsTabsInTheirOrderWhateverTheRow) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 20; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.SetMultiRowTabs(true);
	f.Host.ActivatePane(docs[0]);
	Pump();
	HWND window = f.Host.GroupWindow(docs[0]->Group());
	CHECK(f.Host.FocusChrome(docs[0]));
	for (int i = 0; i < 6; i++)
		Key(window, VK_RIGHT);
	CHECK(f.Host.ChromeFocusName() == L"document3.cpp");				// (each tab and its close button)
	Key(window, VK_RETURN);
	CHECK(f.Host.ActivePane() == docs[3]);
	VERIFY(f);
}

// ---- Scroll arrows on a tab strip ---------------------------------------------------------------

TEST(Host_TheArrowsScrollAStripThatIsTooFullByOneTab) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.ActivatePane(docs[0]);
	Pump();
	auto group = docs[0]->Group();
	HWND window = f.Host.GroupWindow(group);
	CHECK(f.Host.GetTabState(group).First == 0);

	auto strip = StripOf(f, group);
	CHECK(strip.Overflow && !strip.CanScrollLeft && strip.CanScrollRight);
	CHECK(strip.ScrollLeft.right == strip.ScrollRight.left && strip.ScrollRight.right == strip.OverflowButton.left);

	// the left arrow has nowhere to go; the right one moves on a tab at a time (the active tab may scroll out of view)
	Click(window, Center(strip.ScrollLeft));
	CHECK(f.Host.GetTabState(group).First == 0);
	Click(window, Center(strip.ScrollRight));
	CHECK(f.Host.GetTabState(group).First == 1);
	Click(window, Center(StripOf(f, group).ScrollRight));
	CHECK(f.Host.GetTabState(group).First == 2);
	Click(window, Center(StripOf(f, group).ScrollLeft));
	CHECK(f.Host.GetTabState(group).First == 1);
	CHECK(group->ActivePane() == docs[0]);								// scrolling activates nothing

	// all the way to the end, where the right arrow is dead and every tab has been in view
	for (int i = 0; i < 40 && StripOf(f, group).CanScrollRight; i++)
		Click(window, Center(StripOf(f, group).ScrollRight));
	strip = StripOf(f, group);
	CHECK(!strip.CanScrollRight && strip.CanScrollLeft);
	CHECK(strip.First + (int)strip.Tabs.size() == 25);
	const int last = f.Host.GetTabState(group).First;
	Click(window, Center(strip.ScrollRight));
	CHECK(f.Host.GetTabState(group).First == last);
	VERIFY(f);
}

TEST(Host_AHeldArrowKeepsScrolling) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.ActivatePane(docs[0]);
	Pump();
	HWND window = f.Host.GroupWindow(docs[0]->Group());
	const auto strip = StripOf(f, docs[0]->Group());

	::SendMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, Center(strip.ScrollRight));
	CHECK(f.Host.GetTabState(docs[0]->Group()).First == 1);
	::SendMessage(window, WM_TIMER, 2, 0);								// the repeat
	::SendMessage(window, WM_TIMER, 2, 0);
	CHECK(f.Host.GetTabState(docs[0]->Group()).First == 3);
	::SendMessage(window, WM_LBUTTONUP, 0, Center(strip.ScrollRight));
	::SendMessage(window, WM_TIMER, 2, 0);								// let go: no more
	CHECK(f.Host.GetTabState(docs[0]->Group()).First == 3);
}

TEST(Host_TheArrowsShowWhereThereIsMoreAndSayWhatTheyDo) {
	Fixture f(500, 400);
	std::vector<DockPane*> docs;
	for (int i = 0; i < 25; i++) {
		docs.push_back(f.Add((L"document" + std::to_wstring(i) + L".cpp").c_str(), PaneKind::Document));
		f.Host.Layout().Show(docs.back());
	}
	f.Host.ActivatePane(docs[0]);
	Pump();
	auto group = docs[0]->Group();
	HWND window = f.Host.GroupWindow(group);
	const auto strip = StripOf(f, group);

	// an arrow with nothing beyond it is dimmed
	const POINT left{ (strip.ScrollLeft.left + strip.ScrollLeft.right) / 2, (strip.ScrollLeft.top + strip.ScrollLeft.bottom) / 2 };
	const POINT right{ (strip.ScrollRight.left + strip.ScrollRight.right) / 2, (strip.ScrollRight.top + strip.ScrollRight.bottom) / 2 };
	const COLORREF dim = PixelAt(window, left.x, left.y), lit = PixelAt(window, right.x, right.y);
	CHECK(dim != lit && lit == f.Host.Theme().ButtonGlyph);

	// tooltips
	f.Host.SetTipTiming(0, 0);
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.ScrollRight));
	CHECK(f.Host.TipText() == L"Scroll tabs right");
	::SendMessage(window, WM_MOUSEMOVE, 0, Center(strip.ScrollLeft));
	CHECK(f.Host.TipText() == L"Scroll tabs left");
	f.Host.HideTip();

	// accessibility: two buttons, one of them unavailable, that do the scrolling
	auto acc = AccessibleOfClient(window);
	const auto children = ChildrenOf(acc);
	const AccChild* l = FindChild(children, L"Scroll tabs left");
	const AccChild* r = FindChild(children, L"Scroll tabs right");
	CHECK(l && r && (l->State & STATE_SYSTEM_UNAVAILABLE) && !(r->State & STATE_SYSTEM_UNAVAILABLE));
	if (r)
		CHECK(acc->accDoDefaultAction(ChildId(r->Id)) == S_OK && f.Host.GetTabState(group).First == 1);
	CHECK(FindChild(ChildrenOf(acc), L"Scroll tabs left") && !(FindChild(ChildrenOf(acc), L"Scroll tabs left")->State & STATE_SYSTEM_UNAVAILABLE));

	// with room for every tab there are no arrows
	Fixture wide(1800, 400);
	Docs d = AddDocs(wide);
	auto plain = StripOf(wide, d.A->Group());
	CHECK(!plain.Overflow && IsRectEmpty(&plain.ScrollLeft));
}

TEST(Host_MergedSplitsHaveTheirWindowsAndSplittersInOrder) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.Float(f.Sol, OffScreen(50, 50, 500, 500)));
	auto ext = f.Add(L"Ext", PaneKind::Tool);
	CHECK(l.DockTo(f.Props, f.Sol->Group(), DockPosition::Bottom));
	CHECK(l.DockTo(ext, f.Props->Group(), DockPosition::Right));
	CHECK(l.DockTo(f.Output, ext->Group(), DockPosition::Bottom));
	VERIFY(f);
	CHECK(l.Hide(f.Props));													// leaves a vertical split inside a vertical one
	VERIFY(f);
	auto& root = f.Sol->Group()->Float()->Root();
	CHECK(root.Children().size() == 3 && root.Children()[0]->IsGroup() && root.Children()[1]->IsGroup() && root.Children()[2]->IsGroup());

	// the splitters between the three groups can be moved, and nothing else changes
	auto hits = DockLayout::Splitters(root);
	CHECK(hits.size() == 2);
	if (hits.size() == 2) {
		const int before = Height(f.Sol->Group()->Rect);
		CHECK(l.ResizeSplitter(hits[0].Split, 0, 40));
		VERIFY(f);
		CHECK(Height(f.Sol->Group()->Rect) == before + 40);
	}
}

TEST(Host_AClosedToolWindowReopensWhereItWas) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	auto ext = f.Add(L"Ext", PaneKind::Tool, DockSide::Right, 200);
	CHECK(l.Show(ext));
	CHECK(l.DockTo(ext, f.Props->Group(), DockPosition::Bottom));
	VERIFY(f);

	CHECK(f.Host.ClosePane(ext));
	CHECK(f.Host.ShowPane(ext));											// what the Panes menu does
	VERIFY(f);
	CHECK(ext->Group()->Parent() == f.Props->Group()->Parent() && ext->Group()->Parent() != &l.Root());

	// a pin press and a Dock command lead back as well
	CHECK(f.Host.Execute(DockCommand::AutoHide, ext));
	CHECK(f.Host.Execute(DockCommand::Dock, ext));
	VERIFY(f);
	CHECK(ext->Group()->Parent() == f.Props->Group()->Parent() && ext->State() == PaneState::Docked);

	CHECK(f.Host.Execute(DockCommand::Float, ext));
	CHECK(f.Host.Execute(DockCommand::Dock, ext));
	VERIFY(f);
	CHECK(ext->Group()->Parent() == f.Props->Group()->Parent() && ext->State() == PaneState::Docked);
}

TEST(Host_ATabThatWasFloatedComesBackToItsGroup) {
	Fixture f;
	f.AddStandard();
	auto& l = f.Host.Layout();
	CHECK(l.DockTo(f.Output, f.Sol->Group(), DockPosition::Tab));
	CHECK(f.Host.Execute(DockCommand::Float, f.Output));
	VERIFY(f);
	CHECK(f.Output->Group() != f.Sol->Group());
	CHECK(f.Host.ToggleFloat(f.Output));									// double click on its caption
	VERIFY(f);
	CHECK(f.Output->Group() == f.Sol->Group());
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

	for (int step = 0; step < 1000; step++) {
		std::vector<DockGroup*> groups;
		l.ForEachGroup([&](DockGroup& g) { groups.push_back(&g); });
		auto anyPane = [&] { return panes[pick(panes.size())]; };
		auto anyGroup = [&] { return groups[pick(groups.size())]; };
		const RECT rc = OffScreen(20, 20, 300, 280);

		bool ok = false;
		switch (pick(30)) {
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
			case 12: ok = f.Host.Execute((DockCommand)pick(12), anyPane()); break;
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
			case 20: ok = f.Host.ShowFlyout(anyPane(), pick(2) != 0); break;
			case 21: f.Host.HideFlyout(); break;
			case 22:
				// a floating window moves to a monitor with another DPI
				if (!l.Floats().empty()) {
					const int dpis[] = { 96, 120, 144, 192 };
					ok = f.Host.SetFloatDpi(l.Floats()[pick(l.Floats().size())]->Id(), dpis[pick(4)]);
				}
				break;
			case 23:
				// the window switcher: open, wander, go or give up
				if (f.Host.ShowNavigator(pick(2) != 0)) {
					for (int i = 0, n = (int)pick(4); i < n; i++)
						pick(3) ? f.Host.NavigatorMove(1) : f.Host.NavigatorSwitchColumn();
					if (pick(3) == 0)
						f.Host.CancelNavigator();
					else
						ok = f.Host.CommitNavigator();
				}
				break;
			case 24: ok = f.Host.HandleShortcut(pick(2) ? VK_F6 : VK_F4, true, pick(2) != 0, pick(2) != 0, pick(2) != 0); break;
			case 26: ok = f.Host.FocusChrome(anyPane()); break;
			case 28: f.Host.SetMultiRowTabs(pick(2) != 0); break;
			case 29: ok = f.Host.ShowPreview(anyPane()); break;
			case 27:
				// keys in the chrome
				if (f.Host.IsChromeFocused() && f.Host.ActivePane() && f.Host.ActivePane()->Group()) {
					const UINT keys[] = { VK_LEFT, VK_RIGHT, VK_HOME, VK_END, VK_RETURN, VK_DELETE, VK_ESCAPE, VK_TAB };
					if (HWND w = f.Host.GroupWindow(f.Host.ActivePane()->Group()))
{
						const UINT key = keys[pick(8)];
						if (key == VK_RETURN)
							::SetTimer(nullptr, 0, 40, EndMenuTimer);		// (Enter on the menu button opens a menu)
						::SendMessage(w, WM_KEYDOWN, key, 0);
					}
				}
				break;
			case 25: {
				// saving the state and loading it again changes nothing
				// (a window that was moved to a simulated DPI is put back on the real one first: a loaded window takes that of its monitor)
				for (int i = 0; i < (int)l.Floats().size(); i++)
					if (HWND w = f.Host.FloatWindow(l.Floats()[i]->Id()))
						f.Host.SetFloatDpi(l.Floats()[i]->Id(), (int)::GetDpiForWindow(w));
				const std::wstring before = l.Dump();
				const std::string state = f.Host.SaveState(false);
				ok = f.Host.LoadState(state, {}, nullptr, false);
				CHECK_STR(l.Dump(), before);
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
	wprintf(L"        (%d of 1000 operations applied)\n", applied);
	CHECK(applied > 300);
}

}

void RunUiTests() {
	::CoInitialize(nullptr);
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
	Run_Host_DocumentsDockOnlyAmongDocumentsOrFloat();
	Run_Host_DraggingATabOutOfItsGroupDocksItElsewhere();
	Run_Host_DraggingACaptionTakesTheWholeGroup();
	Run_Host_LosingTheMouseCaptureCancelsTheDrag();
	Run_Host_DragsBetweenFloatingAndMainWindows();
	Run_Host_AutoHiddenPanesHaveBarItems();
	Run_Host_ClickingABarItemSlidesTheFlyoutOut();
	Run_Host_AnotherItemSwitchesTheFlyout();
	Run_Host_ClickingElsewhereClosesTheFlyout();
	Run_Host_ThePinDocksTheFlyoutAgainAndAutoHidesADockedGroup();
	Run_Host_HoveringAnItemOpensAFlyoutThatClosesWhenTheMouseLeaves();
	Run_Host_TheFlyoutSlidesInAndOut();
	Run_Host_FocusMovingToAnotherPaneClosesTheFlyout();
	Run_Host_TheFlyoutFollowsChangesToTheLayout();
	Run_Host_AutoHiddenPanesCanBeDockedFromTheirMenu();
	Run_Host_StateRoundTripsWithTheActivePaneAndTheWindowPlacement();
	Run_Host_StateGoesThroughAFile();
	Run_Host_TheWindowPlacementComesBackOnAScreen();
	Run_Host_ThePaneFactoryMakesPanesTheFileMentions();
	Run_Host_TheDefaultLayoutCanBeRestored();
	Run_Host_NamedLayoutsAreSavedAndApplied();
	Run_Host_ThePaneMenuListsPanesAndShowsThem();
	Run_Host_AllDocumentsCanBeClosedAtOnce();
	Run_Host_TheNextDocumentWrapsAround();
	Run_Host_DocumentsFloatIntoWindowsOfTheirOwn();
	Run_Host_ADocumentTabDraggedAwayFloatsAndCanBeDroppedBack();
	Run_Host_TabGroupCommands();
	Run_Host_NewDocumentsOpenInTheGroupThatWasUsedLast();
	Run_Host_TheMostRecentPanesComeFirstInTheSwitcher();
	Run_Host_TheSwitcherMovesWithTheKeysAndGoesWhereControlIsReleased();
	Run_Host_TheSwitcherReachesToolWindowsAndAutoHiddenOnes();
	Run_Host_ClickingARowOfTheSwitcherGoesThere();
	Run_Host_TheSwitcherClosesWhenTheLayoutChanges();
	Run_Host_TheSwitcherIsDrawnInTheTheme();
	Run_Host_LongListsInTheSwitcherScroll();
	Run_Host_ShortcutsSwitchAndCloseTabs();
	Run_Host_ShortcutsMoveBetweenGroupsAndCloseToolWindows();
	Run_Host_AltMinusOpensTheMenuOfTheActivePane();
	Run_Host_KeysForOtherWindowsAreLeftAlone();
	Run_Accessibility_AGroupExposesItsCaptionButtonsAndContent();
	Run_Accessibility_TabsAreExposedAndCanBeSelectedAndClosed();
	Run_Accessibility_ATabScrolledOutOfTheStripIsOffscreen();
	Run_Accessibility_TheHostListsTheGroupsAndTheAutoHideBars();
	Run_Accessibility_TheObjectSurvivesItsWindow();
	Run_Accessibility_TheObjectIsAutomationCallable();
	Run_Accessibility_AGroupTellsClientsWhenItsTabsChange();
	Run_Host_AFloatingWindowOnAnotherMonitorUsesItsOwnMetrics();
	Run_Host_TheGroupsOfAFloatAtAnotherDpiDrawWithItsFonts();
	Run_Host_ANewFloatTakesTheDpiOfItsMonitor();
	Run_Host_SavedFloatsKeepTheirDpi();
	Run_Host_MovingAFloatingWindowByItsTitleBarKeepsTheGuides();
	Run_Host_TabsShowTheirTooltips();
	Run_Host_TheCaptionButtonsSayWhatTheyDo();
	Run_Host_ATitleThatIsCutOffShowsInFull();
	Run_Host_ATipWaitsForTheMouseToRest();
	Run_Host_TheTipIsDrawnInTheThemeAndLetsTheMouseThrough();
	Run_Host_ATipStaysOnTheMonitor();
	Run_Host_AModifiedDocumentShowsADotInPlaceOfItsCloseButton();
	Run_Host_AModifiedTabWithoutACloseButtonMakesRoomForItsMark();
	Run_Accessibility_ModifiedAndTooltipReachScreenReaders();
	Run_Windows_TheListShowsTheDocumentsAndCanIncludeToolWindows();
	Run_Windows_ActivateGoesToTheSelectedWindow();
	Run_Windows_CloseWindowsClosesTheSelectedOnesAndHonoursAVeto();
	Run_Windows_SaveIsForTheApplicationToDo();
	Run_Windows_ClickingAColumnSortsAndClickingAgainReverses();
	Run_Windows_TheDialogCanBeDrivenWithNoWindowsAtAll();
	Run_Host_TheKeyboardCanVisitTheTabsOfAGroup();
	Run_Host_KeysDoWhatTheFocusedItemDoes();
	Run_Host_TheCaptionButtonsAreReachableFromTheKeyboard();
	Run_Host_TabMovesTheChromeFocusToTheNextGroup();
	Run_Host_TheShortcutTakesTheKeyboardToTheChrome();
	Run_Host_ATabOutOfViewIsScrolledInWhenTheKeyboardGoesThere();
	Run_Host_TheFocusRingIsDrawnOnTheItemAndGoesWithTheFocus();
	Run_Host_ShiftF10OpensTheMenuOfTheTab();
	Run_Host_ADockedFloatHasTheWidthOrHeightItHadFloating();
	Run_Host_DroppingAFloatOnAnEdgeOrBesideAGroupKeepsItsSize();
	Run_Host_PinnedTabsMoveLeftAndSpareTheCloseAllCommands();
	Run_Host_ThePinButtonOfATabUnpinsIt();
	Run_Host_APreviewReplacesThePreviousPreview();
	Run_Host_APreviewTabIsSetApartFromTheOthers();
	Run_Host_ATabWithAColourHasAStripeInIt();
	Run_Host_TabsCanBeShownInSeveralRows();
	Run_Host_TheRowsFollowTheTabsAndTheWindow();
	Run_Host_ATabDraggedToAnotherRowChangesPlace();
	Run_Host_TheKeyboardVisitsTabsInTheirOrderWhateverTheRow();
	Run_Host_TheArrowsScrollAStripThatIsTooFullByOneTab();
	Run_Host_AHeldArrowKeepsScrolling();
	Run_Host_TheArrowsShowWhereThereIsMoreAndSayWhatTheyDo();
	Run_Host_MergedSplitsHaveTheirWindowsAndSplittersInOrder();
	Run_Host_AClosedToolWindowReopensWhereItWas();
	Run_Host_ATabThatWasFloatedComesBackToItsGroup();
	Run_Host_RandomOperationsKeepWindowsAndModelInStep();

	_Module.Term();
	::CoUninitialize();
}
