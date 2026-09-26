#include "DockHost.h"
#include "DockGroupWnd.h"
#include "DockFloatFrame.h"
#include "SplitterTracker.h"
#include "DockDragSession.h"
#include <algorithm>
#include <set>

namespace WTLDock {

namespace {

std::vector<std::pair<HWINEVENTHOOK, CDockHost*>>& Hooks() {
	static std::vector<std::pair<HWINEVENTHOOK, CDockHost*>> hooks;
	return hooks;
}

std::wstring GroupTitle(const DockGroup& group) {
	std::wstring title;
	for (auto p : group.Panes())
		title += (title.empty() ? L"" : L", ") + p->Title;
	return title;
}

constexpr UINT_PTR TimerAnimation = 1, TimerHover = 2, TimerLeave = 3;

constexpr UINT CmdBase = 0xD000;
constexpr UINT CmdRange = 0x100;

}

CDockHost::CDockHost() : m_Theme(DockTheme::Light()), m_Metrics(DockMetrics::ForDpi(96)), m_Splitters(std::make_unique<SplitterTracker>()) {
}

CDockHost::~CDockHost() = default;

//
// window and pane bookkeeping
//

void CDockHost::Adopt(HWND child, HWND parent) {
	if (::GetParent(child) == parent)
		return;
	LONG_PTR style = ::GetWindowLongPtr(child, GWL_STYLE);
	if (!(style & WS_CHILD)) {
		// a top-level window has to become a child before it gets a parent
		::SetWindowLongPtr(child, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
	}
	::SetParent(child, parent);
}

HWND CDockHost::GroupWindow(const DockGroup* group) const {
	auto it = m_Groups.find(group);
	return it != m_Groups.end() ? it->second->m_hWnd : nullptr;
}

HWND CDockHost::FloatWindow(int floatId) const {
	auto it = m_Frames.find(floatId);
	return it != m_Frames.end() && it->second ? it->second->m_hWnd : nullptr;
}

bool CDockHost::IsActive(const DockGroup* group) const {
	auto pane = ActivePane();
	return pane && pane->Group() == group;
}

void CDockHost::Retire(CDockGroupWnd* window) {
	window->Retire();
	m_Retired.push_back(window);
	PostMessage(WM_REAP);
}

void CDockHost::RetireFrame(CDockFloatFrame* frame) {
	frame->Retire();
	m_RetiredFrames.push_back(frame);
	PostMessage(WM_REAP);
}

void CDockHost::ForgetFrame(CDockFloatFrame* frame) {
	std::erase_if(m_Frames, [&](auto& e) { return e.second == frame; });
	std::erase(m_RetiredFrames, frame);
}

LRESULT CDockHost::OnReap(UINT, WPARAM, LPARAM, BOOL&) {
	// destroying a window from inside one of its own message handlers is asking for trouble, so this happens later
	auto retired = std::move(m_Retired);
	m_Retired.clear();
	for (auto w : retired)
		if (w->IsWindow())
			w->DestroyWindow();

	auto frames = std::move(m_RetiredFrames);
	m_RetiredFrames.clear();
	for (auto f : frames)
		if (f->IsWindow())
			f->DestroyWindow();
	return 0;
}

//
// floating windows
//

DockFloat* CDockHost::FindFloat(int id) const {
	for (auto& f : m_Layout.Floats())
		if (f->Id() == id)
			return f.get();
	return nullptr;
}

DockSplit* CDockHost::FloatRoot(int id) const {
	auto f = FindFloat(id);
	return f ? &f->Root() : nullptr;
}

SIZE CDockHost::FloatMinClientSize(int id) const {
	auto f = FindFloat(id);
	if (!f)
		return {};
	return { m_Layout.MinLength(f->Root(), Axis::Horizontal), m_Layout.MinLength(f->Root(), Axis::Vertical) };
}

void CDockHost::OnFrameMoved(CDockFloatFrame* frame) {
	auto it = m_Frames.find(frame->Id());
	if (it == m_Frames.end() || it->second != frame || ::IsIconic(frame->m_hWnd))
		return;
	RECT rc;
	frame->GetWindowRect(&rc);
	auto f = FindFloat(frame->Id());
	if (f && !IsRectEmpty(&rc) && !EqualRect(&rc, &f->Rect()))
		m_Layout.SetFloatRect(f, rc);
}

RECT CDockHost::OuterRectForClient(const RECT& client, int dpi) const {
	RECT rc = client;
	::AdjustWindowRectExForDpi(&rc, CDockFloatFrame::Style, FALSE, CDockFloatFrame::ExStyle, dpi);
	return rc;
}

void CDockHost::EnsureOnScreen(RECT& rect) const {
	// reachable if the title bar touches a monitor
	const RECT strip{ rect.left, rect.top, rect.right, rect.top + 40 };
	if (::MonitorFromRect(&strip, MONITOR_DEFAULTTONULL))
		return;

	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	const int w = std::min(Width(rect), Width(work)), h = std::min(Height(rect), Height(work));
	rect = { work.left + 40, work.top + 40, work.left + 40 + w, work.top + 40 + h };
	if (rect.right > work.right)
		OffsetRect(&rect, work.right - rect.right, 0);
	if (rect.bottom > work.bottom)
		OffsetRect(&rect, 0, work.bottom - rect.bottom);
}

RECT CDockHost::DefaultFloatRect(const DockPane* pane) const {
	if (!IsRectEmpty(&pane->LastFloatRect()))
		return pane->LastFloatRect();

	// a window of the size of the group, a little down and to the right of it
	POINT origin{};
	SIZE size = pane->PreferredSize;
	const int offset = m_Metrics.CaptionHeight * 2;
	if (auto g = pane->Group(); g && GroupWindow(g)) {
		RECT rc;
		::GetWindowRect(GroupWindow(g), &rc);
		origin = { rc.left + offset, rc.top + offset };
		size = { Width(rc), Height(rc) };
	}
	else {
		RECT rc;
		GetWindowRect(&rc);
		origin = { rc.left + offset, rc.top + offset };
	}
	size.cx = std::max<LONG>(size.cx, m_Metrics.MinGroupSize.cx);
	size.cy = std::max<LONG>(size.cy, m_Metrics.MinGroupSize.cy);

	const RECT outer = OuterRectForClient({ 0, 0, size.cx, size.cy }, m_Dpi);
	return { origin.x, origin.y, origin.x + Width(outer), origin.y + Height(outer) };
}

std::wstring CDockHost::FloatTitle(const DockFloat& window) const {
	const DockPane* active = ActivePane();
	const DockPane* title = nullptr;
	std::function<void(const DockNode&)> visit = [&](const DockNode& node) {
		if (auto split = node.AsSplit()) {
			for (auto& c : split->Children())
				visit(*c);
		}
		else if (auto group = node.AsGroup()) {
			if (!title && group->ActivePane())
				title = group->ActivePane();
			if (active && active->Group() == group)
				title = active;
		}
	};
	visit(window.Root());
	return title ? title->Title : std::wstring();
}

bool CDockHost::FloatPane(DockPane* pane, const RECT* screenRect) {
	if (!pane || pane->Kind() != PaneKind::Tool || pane->State() == PaneState::Floating)
		return false;
	const RECT rc = screenRect ? *screenRect : DefaultFloatRect(pane);
	return m_Layout.Float(pane, rc);
}

bool CDockHost::DockFloating(DockPane* pane) {
	if (!pane || pane->State() != PaneState::Floating || pane->Kind() != PaneKind::Tool)
		return false;
	return m_Layout.MoveGroupToEdge(pane->Group(), pane->LastSide());
}

bool CDockHost::ToggleFloat(DockPane* pane) {
	if (!pane)
		return false;
	return pane->State() == PaneState::Floating ? DockFloating(pane) : FloatPane(pane);
}

bool CDockHost::DockFloatWindow(int floatId) {
	auto f = FindFloat(floatId);
	if (!f)
		return false;

	// moving a group rearranges the tree, so work from the panes
	std::vector<DockPane*> panes;
	std::function<void(DockNode&)> visit = [&](DockNode& node) {
		if (auto split = node.AsSplit()) {
			for (auto& c : split->Children())
				visit(*c);
		}
		else if (auto pane = node.AsGroup()->ActivePane()) {
			panes.push_back(pane);
		}
	};
	visit(f->Root());

	bool any = false;
	for (auto pane : panes)
		any |= DockFloating(pane);
	return any;
}

bool CDockHost::CloseFloatWindow(int floatId) {
	auto f = FindFloat(floatId);
	if (!f)
		return true;

	std::vector<DockPane*> panes;
	std::function<void(DockNode&)> visit = [&](DockNode& node) {
		if (auto split = node.AsSplit()) {
			for (auto& c : split->Children())
				visit(*c);
		}
		else {
			for (auto pane : node.AsGroup()->Panes())
				panes.push_back(pane);
		}
	};
	visit(f->Root());

	for (auto pane : panes)
		ClosePane(pane);
	return FindFloat(floatId) == nullptr;
}

const DockGroup* CDockHost::GroupAtScreen(POINT pt, HWND ignore) const {
	const HWND root = ::GetAncestor(m_hWnd, GA_ROOT);
	for (HWND w = ::GetTopWindow(nullptr); w; w = ::GetWindow(w, GW_HWNDNEXT)) {
		if (w == ignore || !::IsWindowVisible(w) || ::IsIconic(w))
			continue;
		wchar_t cls[32]{};
		::GetClassNameW(w, cls, _countof(cls));
		if (wcscmp(cls, L"WTLDock_Guide") == 0)
			continue;
		RECT rc;
		::GetWindowRect(w, &rc);
		if (!PtInRect(&rc, pt))
			continue;

		// the first window that covers the point decides: one of ours, or something in the way
		HWND surface = nullptr;
		if (w == root)
			surface = m_hWnd;
		for (auto& [id, frame] : m_Frames)
			if (frame && frame->m_hWnd == w)
				surface = w;
		if (!surface)
			return nullptr;
		for (auto& [group, window] : m_Groups) {
			if (::GetParent(window->m_hWnd) != surface)
				continue;
			RECT gr;
			::GetWindowRect(window->m_hWnd, &gr);
			if (PtInRect(&gr, pt))
				return group;
		}
		return nullptr;
	}
	return nullptr;
}

bool CDockHost::BeginDrag(DockPane* pane, bool wholeGroup, POINT screen, bool externalWindow) {
	if (m_Drag || !pane || !pane->Group() || pane->Group()->Location() == GroupLocation::AutoHide)
		return false;
	m_Drag = std::make_unique<DockDragSession>(*this, pane, wholeGroup, screen, externalWindow);
	m_Drag->Update(screen, false);
	return true;
}

void CDockHost::UpdateDrag(POINT screen, bool suppressDocking) {
	if (!m_Drag)
		return;
	if (m_Drag->Stale()) {
		m_Drag.reset();
		return;
	}
	m_Drag->Update(screen, suppressDocking);
}

bool CDockHost::EndDrag(bool commit) {
	if (!m_Drag)
		return false;
	// the guides go first, so that nothing of them is in the way of what the drop does
	auto session = std::move(m_Drag);
	session->HideAll();
	return commit && session->Commit();
}

const DropTarget& CDockHost::CurrentDropTarget() const {
	static const DropTarget none;
	return m_Drag ? m_Drag->Target() : none;
}

int CDockHost::VisibleGuides() const {
	return m_Drag ? m_Drag->VisibleMarkers() : 0;
}

bool CDockHost::BeginFrameMove(int floatId) {
	auto f = FindFloat(floatId);
	if (!f || f->Root().Children().size() != 1 || !f->Root().Children()[0]->IsGroup())
		return false;
	DockPane* pane = f->Root().Children()[0]->AsGroup()->ActivePane();
	if (!pane || pane->Kind() != PaneKind::Tool)
		return false;
	POINT cursor;
	::GetCursorPos(&cursor);
	return BeginDrag(pane, true, cursor, true);
}

//
// synchronization
//

void CDockHost::Sync() {
	if (!m_hWnd || m_Syncing)
		return;
	m_Syncing = true;
	if (m_Drag && m_Drag->Stale())
		m_Drag.reset();		// its targets are gone

	// a flyout that has the focus keeps it, even if its window is rebuilt below
	bool flyoutHadFocus = false;
	if (m_FlyoutWindow && m_FlyoutFocused && ::IsWindow(m_FlyoutWindow)) {
		HWND focus = ::GetFocus();
		flyoutHadFocus = focus && (focus == m_FlyoutWindow || ::IsChild(m_FlyoutWindow, focus));
	}

	// every group of every tree; floatId 0 is the main tree
	struct Live {
		DockGroup* Group;
		int FloatId;
	};
	std::vector<Live> live;
	std::function<void(DockNode&, int)> collect = [&](DockNode& node, int floatId) {
		if (auto split = node.AsSplit()) {
			for (auto& c : split->Children())
				collect(*c, floatId);
		}
		else {
			live.push_back({ node.AsGroup(), floatId });
		}
	};
	collect(m_Layout.Root(), 0);
	DockGroup* flyoutGroup = ResolveFlyout();
	if (flyoutGroup)
		live.push_back({ flyoutGroup, -1 });
	std::set<int> floatIds;
	for (auto& f : m_Layout.Floats()) {
		collect(f->Root(), f->Id());
		floatIds.insert(f->Id());
	}
	std::set<const DockGroup*> liveGroups;
	for (auto& l : live)
		liveGroups.insert(l.Group);

	// windows of groups that are gone (they leave their frame first, which may be about to go too)
	for (auto it = m_Groups.begin(); it != m_Groups.end();) {
		if (liveGroups.contains(it->first)) {
			++it;
			continue;
		}
		Retire(it->second);
		it = m_Groups.erase(it);
	}

	// frames of floats that are gone; frames for new ones
	for (auto it = m_Frames.begin(); it != m_Frames.end();) {
		if (floatIds.contains(it->first) && it->second) {
			++it;
			continue;
		}
		auto frame = it->second;
		it = m_Frames.erase(it);
		if (frame)
			RetireFrame(frame);
	}
	const HWND owner = ::GetAncestor(m_hWnd, GA_ROOT);
	for (auto& f : m_Layout.Floats()) {
		RECT rc = f->Rect();
		if (auto it = m_Frames.find(f->Id()); it != m_Frames.end()) {
			RECT actual;
			it->second->GetWindowRect(&actual);
			if (!EqualRect(&actual, &rc))
				it->second->SetWindowPos(nullptr, rc.left, rc.top, Width(rc), Height(rc), SWP_NOZORDER | SWP_NOACTIVATE);
			continue;
		}

		if (m_KeepOnScreen) {
			EnsureOnScreen(rc);
			if (!EqualRect(&rc, &f->Rect()))
				m_Layout.SetFloatRect(f.get(), rc);		// (the change handler does nothing while we are syncing)
		}
		auto frame = new CDockFloatFrame(*this, f->Id());
		const std::wstring title = FloatTitle(*f);
		if (!frame->Create(owner, rc, title.c_str(), 0, 0)) {
			delete frame;
			continue;
		}
		m_Frames[f->Id()] = frame;
		frame->ApplyTheme();
		frame->ShowWindow(SW_SHOWNOACTIVATE);
	}

	// windows for new groups, in the window of their tree
	std::vector<Live> placed;
	for (auto& l : live) {
		HWND surface = l.FloatId <= 0 ? m_hWnd : FloatWindow(l.FloatId);
		if (!surface)
			continue;
		auto& window = m_Groups[l.Group];
		if (!window) {
			window = new CDockGroupWnd(*this);
			RECT rc = l.Group->Rect;
			if (!window->Create(surface, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)) {
				delete window;
				m_Groups.erase(l.Group);
				continue;
			}
		}
		else if (::GetParent(window->m_hWnd) != surface) {
			::SetParent(window->m_hWnd, surface);
		}
		placed.push_back(l);
	}

	// content windows of panes that are not on show in any tree
	for (auto& p : m_Layout.Panes()) {
		if (!p->hWnd || !::IsWindow(p->hWnd))
			continue;
		auto g = p->Group();
		if (g && (g->Location() == GroupLocation::Main || g->Location() == GroupLocation::Float || g == flyoutGroup))
			continue;
		Adopt(p->hWnd, m_hWnd);
		::ShowWindow(p->hWnd, SW_HIDE);
	}

	// arrange the trees
	RECT client;
	GetClientRect(&client);
	m_Layout.Arrange(client);
	for (auto& f : m_Layout.Floats()) {
		RECT frameClient{};
		if (HWND frame = FloatWindow(f->Id()))
			::GetClientRect(frame, &frameClient);
		m_Layout.Arrange(*f, frameClient);
	}

	// position the group windows, one batch per parent
	std::vector<int> surfaces{ 0 };
	for (int id : floatIds)
		surfaces.push_back(id);
	std::vector<CDockGroupWnd*> windows;
	for (int surface : surfaces) {
		std::vector<CDockGroupWnd*> batch;
		for (auto& l : placed) {
			if (l.FloatId != surface)
				continue;
			auto window = m_Groups[l.Group];
			window->Attach(l.Group);
			batch.push_back(window);
		}
		if (batch.empty())
			continue;
		HDWP dwp = ::BeginDeferWindowPos((int)batch.size());
		for (auto w : batch) {
			const RECT& rc = w->Group()->Rect;
			dwp = ::DeferWindowPos(dwp, w->m_hWnd, nullptr, rc.left, rc.top, Width(rc), Height(rc),
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		}
		::EndDeferWindowPos(dwp);
		windows.insert(windows.end(), batch.begin(), batch.end());
	}
	// the flyout stands over the documents, on top of the other windows
	if (flyoutGroup) {
		for (auto& l : placed) {
			if (l.FloatId != -1)
				continue;
			auto window = m_Groups[l.Group];
			window->Attach(l.Group);
			windows.push_back(window);
		}
		PositionFlyout();
	}
	for (auto w : windows)
		w->Relayout();
	m_FlyoutWindow = flyoutGroup ? GroupWindow(flyoutGroup) : nullptr;
	if (flyoutHadFocus && flyoutGroup) {
		HWND window = m_FlyoutWindow, focus = ::GetFocus();
		auto pane = flyoutGroup->ActivePane();
		if (window && pane && pane->hWnd && ::IsWindowVisible(pane->hWnd) && !(focus && (focus == window || ::IsChild(window, focus))))
			::SetFocus(pane->hWnd);
	}

	Invalidate(FALSE);
	for (auto& f : m_Layout.Floats()) {
		if (auto it = m_Frames.find(f->Id()); it != m_Frames.end()) {
			it->second->SetTitle(FloatTitle(*f));
			it->second->Invalidate(FALSE);
		}
	}
	m_Syncing = false;
	if (OnLayoutChanged)
		OnLayoutChanged();
}

//
// activation
//

void CDockHost::SetActivePane(DockPane* pane) {
	const std::wstring id = pane ? pane->Id() : std::wstring();
	if (id == m_ActiveId)
		return;
	m_ActiveId = id;
	for (auto& [group, window] : m_Groups)
		window->Invalidate(FALSE);
	// a floating window is titled after its active pane
	for (auto& f : m_Layout.Floats())
		if (auto it = m_Frames.find(f->Id()); it != m_Frames.end() && it->second)
			it->second->SetTitle(FloatTitle(*f));
	if (OnActivePaneChanged)
		OnActivePaneChanged();
}

void CDockHost::ActivatePane(DockPane* pane) {
	if (!pane || !m_Layout.Activate(pane))
		return;
	SetActivePane(pane);
	if (pane->hWnd && ::IsWindow(pane->hWnd) && ::IsWindowVisible(pane->hWnd))
		::SetFocus(pane->hWnd);
	else if (auto window = GroupWindow(pane->Group()))
		::SetFocus(window);
}

DockPane* CDockHost::PaneFromWindow(HWND hWnd) const {
	for (HWND w = hWnd; w; w = ::GetParent(w)) {
		for (auto& p : m_Layout.Panes())
			if (p->hWnd == w)
				return p.get();
		for (auto& [group, window] : m_Groups)
			if (window->m_hWnd == w)
				return group->ActivePane();
	}
	return nullptr;
}

void CDockHost::OnFocusChanged(HWND hWnd) {
	if (!hWnd)
		return;
	OnFlyoutFocus(hWnd);
	if (auto pane = PaneFromWindow(hWnd))
		SetActivePane(pane);
}

void CALLBACK CDockHost::FocusEventProc(HWINEVENTHOOK hook, DWORD, HWND hWnd, LONG, LONG, DWORD, DWORD) {
	for (auto& [h, host] : Hooks())
		if (h == hook)
			host->OnFocusChanged(hWnd);
}

//
// creation, sizing, DPI, theme
//

void CDockHost::CreateFonts() {
	for (auto font : { &m_Font, &m_BoldFont, &m_VerticalFont })
		if (!font->IsNull())
			font->DeleteObject();

	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
	m_Font.CreateFontIndirect(&ncm.lfMessageFont);
	LOGFONT bold = ncm.lfMessageFont;
	bold.lfWeight = FW_BOLD;
	m_BoldFont.CreateFontIndirect(&bold);
	LOGFONT vertical = ncm.lfMessageFont;
	vertical.lfEscapement = vertical.lfOrientation = 900;
	m_VerticalFont.CreateFontIndirect(&vertical);
}

void CDockHost::UpdateDpi() {
	int dpi = (int)::GetDpiForWindow(m_hWnd);
	if (dpi <= 0)
		dpi = 96;
	if (dpi == m_Dpi)
		return;
	m_Dpi = dpi;
	m_Metrics = DockMetrics::ForDpi(dpi);
	CreateFonts();
	m_Layout.SetMetrics(m_Metrics.ToLayoutMetrics());
	m_Layout.SetDpi(dpi);
	Sync();
}

void CDockHost::SetTheme(const DockTheme& theme) {
	m_Theme = theme;
	if (!m_hWnd)
		return;
	Invalidate(FALSE);
	for (auto& [group, window] : m_Groups)
		window->Invalidate(FALSE);
	for (auto& [id, frame] : m_Frames)
		if (frame)
			frame->ApplyTheme();
}

LRESULT CDockHost::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = (int)::GetDpiForWindow(m_hWnd);
	if (m_Dpi <= 0)
		m_Dpi = 96;
	m_Metrics = DockMetrics::ForDpi(m_Dpi);
	CreateFonts();
	m_Layout.SetDpi(m_Dpi);
	m_Layout.SetMetrics(m_Metrics.ToLayoutMetrics());
	m_Layout.SetChangeHandler([this] { Sync(); });

	m_FocusHook = ::SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, FocusEventProc,
		::GetCurrentProcessId(), ::GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
	if (m_FocusHook)
		Hooks().push_back({ m_FocusHook, this });
	return 0;
}

LRESULT CDockHost::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	m_Layout.SetChangeHandler({});
	m_Drag.reset();
	ClearFlyout();
	KillTimer(TimerHover);
	if (m_FocusHook) {
		::UnhookWinEvent(m_FocusHook);
		std::erase_if(Hooks(), [&](auto& e) { return e.first == m_FocusHook; });
		m_FocusHook = nullptr;
	}

	// the frames are owned by the top-level window, not by us, so they go now (with the group windows in them)
	std::vector<CDockFloatFrame*> frames = m_RetiredFrames;
	for (auto& [id, frame] : m_Frames)
		frames.push_back(frame);
	m_Frames.clear();
	m_RetiredFrames.clear();
	for (auto frame : frames)
		if (frame && frame->IsWindow())
			frame->DestroyWindow();

	// the group windows of the main tree go away with their parent
	m_Groups.clear();
	m_Retired.clear();
	m_ActiveId.clear();
	handled = FALSE;
	return 0;
}

LRESULT CDockHost::OnSize(UINT, WPARAM wp, LPARAM, BOOL&) {
	if (wp != SIZE_MINIMIZED)
		Sync();
	return 0;
}

LRESULT CDockHost::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateDpi();
	return 0;
}

//
// splitters and bars
//

std::vector<CDockHost::BarItem> CDockHost::BarItems(DockSide side, CDCHandle dc) const {
	std::vector<BarItem> items;
	const RECT bar = m_Layout.AutoHideBarRect(side);
	if (IsRectEmpty(&bar))
		return items;

	const bool vertical = AxisOf(side) == Axis::Horizontal;
	const int pad = m_Metrics.TabPadding;
	int pos = (vertical ? bar.top : bar.left) + m_Metrics.TabGap * 4;
	for (auto& g : m_Layout.AutoHideGroups(side)) {
		for (auto pane : g->Panes()) {
			SIZE size{};
			dc.GetTextExtent(pane->Title.c_str(), (int)pane->Title.size(), &size);
			const int length = size.cx + 2 * pad;
			RECT r = vertical ? RECT{ bar.left, pos, bar.right, pos + length } : RECT{ pos, bar.top, pos + length, bar.bottom };
			items.push_back({ g.get(), pane, r });
			pos += length + m_Metrics.TabGap * 2;
		}
		pos += m_Metrics.TabGap * 6;		// a little more between groups
	}
	return items;
}

void CDockHost::DrawBars(CDCHandle dc) {
	for (int i = 0; i < SideCount; i++) {
		const DockSide side = (DockSide)i;
		const RECT bar = m_Layout.AutoHideBarRect(side);
		if (IsRectEmpty(&bar))
			continue;
		dc.FillSolidRect(&bar, m_Theme.BarBack);
		const bool vertical = AxisOf(side) == Axis::Horizontal;

		dc.SelectFont(m_Font);
		for (auto& item : BarItems(side, dc)) {
			const bool out = item.Pane == FlyoutPane();
			dc.FillSolidRect(&item.Rect, out ? m_Theme.TabActiveBack : m_Theme.BarItemBack);
			// the item of the flyout that is out has the accent line, on the side of the bar that faces the documents
			if (out) {
				const int line = std::max(2, m_Dpi * 2 / 96);
				RECT accent = item.Rect;
				switch (side) {
					case DockSide::Left: accent.left = accent.right - line; break;
					case DockSide::Right: accent.right = accent.left + line; break;
					case DockSide::Top: accent.top = accent.bottom - line; break;
					case DockSide::Bottom: accent.bottom = accent.top + line; break;
				}
				dc.FillSolidRect(&accent, m_Theme.TabActiveAccent);
			}
			dc.SetTextColor(out ? m_Theme.TabActiveText : m_Theme.BarItemText);
			const std::wstring& title = item.Pane->Title;
			if (vertical) {
				TEXTMETRIC tm;
				dc.SelectFont(m_VerticalFont);
				dc.GetTextMetrics(&tm);
				// rotated 90 degrees the text runs upwards from the anchor and its cell extends to the right of it
				dc.TextOut(item.Rect.left + (Width(item.Rect) - tm.tmHeight) / 2, item.Rect.bottom - m_Metrics.TabPadding,
					title.c_str(), (int)title.size());
				dc.SelectFont(m_Font);
			}
			else {
				RECT text = item.Rect;
				dc.DrawText(title.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
			}
		}
	}
}

LRESULT CDockHost::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CDockHost::OnPaint(UINT msg, WPARAM wp, LPARAM, BOOL&) {
	if (msg == WM_PRINTCLIENT) {
		RECT client;
		GetClientRect(&client);
		Draw((HDC)wp, client);
		return 0;
	}
	CPaintDC paintDC(m_hWnd);
	Draw(paintDC.m_hDC, paintDC.m_ps.rcPaint);
	return 0;
}

void CDockHost::Draw(HDC hdc, RECT clip) {
	CMemoryDC dc(hdc, clip);

	RECT client;
	GetClientRect(&client);
	dc.FillSolidRect(&client, m_Theme.Workspace);
	dc.SetBkMode(TRANSPARENT);

	m_Splitters->Paint(dc.m_hDC, m_Layout.Root(), m_Theme);
	DrawBars(dc.m_hDC);
}

LRESULT CDockHost::OnLButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

	// an item on an auto-hide bar slides its group out (or, if that is the one that is out, back in)
	BarItem item;
	if (BarItemAt(pt, item)) {
		if (FlyoutPane() == item.Pane && !m_FlyoutClosing)
			HideFlyout();
		else
			ShowFlyout(item.Pane, true);
		return 0;
	}
	HideFlyout();		// a click on anything else
	m_Splitters->Begin(m_hWnd, m_Layout.Root(), pt);
	return 0;
}

LRESULT CDockHost::OnRButtonUp(UINT, WPARAM, LPARAM lp, BOOL&) {
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	BarItem item;
	if (BarItemAt(pt, item)) {
		POINT screen = pt;
		ClientToScreen(&screen);
		ShowPaneMenu(item.Pane, screen);
	}
	return 0;
}

bool CDockHost::BarItemAt(POINT pt, BarItem& item) const {
	bool any = false;
	for (int side = 0; side < SideCount; side++)
		any |= !m_Layout.AutoHideGroups((DockSide)side).empty();
	if (!any)
		return false;

	CClientDC dc(m_hWnd);
	HFONT old = dc.SelectFont(m_Font);
	bool found = false;
	for (int side = 0; side < SideCount && !found; side++) {
		for (auto& candidate : BarItems((DockSide)side, dc.m_hDC)) {
			if (PtInRect(&candidate.Rect, pt)) {
				item = candidate;
				found = true;
				break;
			}
		}
	}
	dc.SelectFont(old);
	return found;
}

bool CDockHost::GetBarItemRect(const DockPane* pane, RECT& rect) const {
	CClientDC dc(m_hWnd);
	HFONT old = dc.SelectFont(m_Font);
	bool found = false;
	for (int side = 0; side < SideCount && !found; side++) {
		for (auto& item : BarItems((DockSide)side, dc.m_hDC)) {
			if (item.Pane == pane) {
				rect = item.Rect;
				found = true;
				break;
			}
		}
	}
	dc.SelectFont(old);
	return found;
}

LRESULT CDockHost::OnMouseMove(UINT, WPARAM, LPARAM lp, BOOL&) {
	const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
	m_Splitters->Move(m_Layout, m_Layout.Root(), pt);
	if (!m_Splitters->Dragging())
		UpdateBarHover(pt);
	return 0;
}

LRESULT CDockHost::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingMouse = false;
	m_HoverId.clear();
	KillTimer(TimerHover);
	return 0;
}

// resting the mouse on a bar item slides the flyout out after a moment
void CDockHost::UpdateBarHover(POINT pt) {
	BarItem item;
	if (!BarItemAt(pt, item)) {
		if (!m_HoverId.empty()) {
			m_HoverId.clear();
			KillTimer(TimerHover);
		}
		return;
	}
	if (!m_TrackingMouse) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		m_TrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
	if (item.Pane->Id() == m_HoverId || (FlyoutPane() == item.Pane && !m_FlyoutClosing))
		return;
	m_HoverId = item.Pane->Id();
	if (m_HoverMs <= 0) {
		ShowFlyout(item.Pane, false);
		return;
	}
	KillTimer(TimerHover);
	SetTimer(TimerHover, (UINT)m_HoverMs);
}

LRESULT CDockHost::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Splitters->End(m_hWnd))
		ReleaseCapture();
	return 0;
}

LRESULT CDockHost::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Splitters->End(m_hWnd);
	return 0;
}

LRESULT CDockHost::OnSetCursor(UINT, WPARAM, LPARAM lp, BOOL& handled) {
	handled = FALSE;
	if (LOWORD(lp) != HTCLIENT)
		return 0;
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (SplitterTracker::SetCursorFor(m_Splitters->AxisAt(m_Layout.Root(), pt))) {
		handled = TRUE;
		return TRUE;
	}
	return 0;
}

//
// the flyout
//

DockPane* CDockHost::FlyoutPane() const {
	if (m_FlyoutId.empty())
		return nullptr;
	auto pane = m_Layout.FindPane(m_FlyoutId);
	return pane && pane->State() == PaneState::AutoHide && pane->Group() ? pane : nullptr;
}

RECT CDockHost::FlyoutRect() const {
	auto pane = FlyoutPane();
	return pane ? FlyoutRectAt(*pane->Group(), 1.0) : RECT{};
}

DockGroup* CDockHost::ResolveFlyout() {
	if (m_FlyoutId.empty())
		return nullptr;
	auto pane = FlyoutPane();
	if (!pane) {
			ClearFlyout();
		return nullptr;
	}
	auto group = pane->Group();
	if (auto active = group->ActivePane())
		m_FlyoutId = active->Id();		// switching tabs inside the flyout keeps it out
	return group;
}

void CDockHost::ClearFlyout() {
	m_FlyoutId.clear();
	m_FlyoutWindow = nullptr;
	m_FlyoutClosing = false;
	m_FlyoutFocused = false;
	m_FlyoutProgress = 1;
	m_LeaveSince = 0;
	if (m_hWnd) {
		KillTimer(TimerAnimation);
		KillTimer(TimerLeave);
	}
}

// the area next to the bar, as long as the group likes to be (but not most of the window)
RECT CDockHost::FlyoutRectAt(const DockGroup& group, double progress) const {
	const RECT area = m_Layout.Root().Rect;
	const DockSide side = group.Side().value_or(DockSide::Left);
	const Axis axis = AxisOf(side);
	const int minimum = Along(m_Metrics.MinGroupSize, axis);
	int length = group.AutoHideLength > 0 ? group.AutoHideLength : 250;
	length = std::clamp(length, minimum, std::max(minimum, Length(area, axis) * 8 / 10));

	RECT r = area;
	switch (side) {
		case DockSide::Left: r.right = area.left + length; break;
		case DockSide::Right: r.left = area.right - length; break;
		case DockSide::Top: r.bottom = area.top + length; break;
		case DockSide::Bottom: r.top = area.bottom - length; break;
	}
	// sliding: behind the bar it comes from
	const int behind = (int)std::lround((1.0 - progress) * length);
	switch (side) {
		case DockSide::Left: OffsetRect(&r, -behind, 0); break;
		case DockSide::Right: OffsetRect(&r, behind, 0); break;
		case DockSide::Top: OffsetRect(&r, 0, -behind); break;
		case DockSide::Bottom: OffsetRect(&r, 0, behind); break;
	}
	return r;
}

void CDockHost::PositionFlyout() {
	auto pane = FlyoutPane();
	if (!pane)
		return;
	auto group = pane->Group();
	auto it = m_Groups.find(group);
	if (it == m_Groups.end())
		return;

	HWND window = it->second->m_hWnd;
	const RECT r = FlyoutRectAt(*group, m_FlyoutProgress);
	::SetWindowPos(window, HWND_TOP, r.left, r.top, Width(r), Height(r), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	if (m_FlyoutProgress < 1.0) {
		// the part that is still behind the bar is cut off
		RECT visible;
		const RECT area = m_Layout.Root().Rect;
		IntersectRect(&visible, &r, &area);
		OffsetRect(&visible, -r.left, -r.top);
		::SetWindowRgn(window, ::CreateRectRgnIndirect(&visible), TRUE);
	}
	else {
		::SetWindowRgn(window, nullptr, TRUE);
	}
}

void CDockHost::StartFlyoutAnimation(bool opening) {
	if (m_AnimMs <= 0) {
		m_FlyoutProgress = opening ? 1 : 0;
		return;
	}
	m_AnimFrom = m_FlyoutProgress;
	m_AnimTarget = opening ? 1 : 0;
	m_AnimStart = ::GetTickCount64();
	SetTimer(TimerAnimation, 15);
}

bool CDockHost::ShowFlyout(DockPane* pane, bool activate) {
	if (!pane || pane->State() != PaneState::AutoHide || !pane->Group())
		return false;

	auto out = FlyoutPane();
	const bool sameGroup = out && out->Group() == pane->Group() && !m_FlyoutClosing;
	if (activate && !sameGroup && !m_FlyoutFocused)
		m_ReturnFocus = ::GetFocus();
	m_FlyoutClosing = false;
	m_FlyoutId = pane->Id();
	if (!out)
		m_FlyoutProgress = m_AnimMs > 0 ? 0 : 1;
	m_HoverId.clear();
	KillTimer(TimerHover);

	m_Layout.Activate(pane);
	Sync();		// (the activation above already did this, unless the pane was the active one)
	if (m_FlyoutProgress < 1.0)
		StartFlyoutAnimation(true);

	m_LeaveSince = 0;
	m_FlyoutFocused = activate;
	SetTimer(TimerLeave, 50);
	if (activate)
		ActivatePane(pane);
	return true;
}

void CDockHost::HideFlyout() {
	if (m_FlyoutId.empty() || m_FlyoutClosing)
		return;
	if (!FlyoutPane()) {
		ClearFlyout();
		return;
	}
	if (m_AnimMs <= 0) {
		FinishFlyoutClose();
		return;
	}
	m_FlyoutClosing = true;
	StartFlyoutAnimation(false);
}

void CDockHost::FinishFlyoutClose() {
	const bool wasFocused = m_FlyoutFocused;
	ClearFlyout();
	Sync();
	// the user was in there: back to where they were
	if (wasFocused && m_ReturnFocus && ::IsWindow(m_ReturnFocus) && ::IsWindowVisible(m_ReturnFocus))
		::SetFocus(m_ReturnFocus);
	m_ReturnFocus = nullptr;
}

void CDockHost::OnFlyoutFocus(HWND hWnd) {
	auto pane = FlyoutPane();
	if (!pane || m_FlyoutClosing)
		return;
	// the event is on its way for a while; if the focus has moved on since, it is not news
	if (::GetFocus() != hWnd)
		return;
	HWND window = GroupWindow(pane->Group());
	if (window && (hWnd == window || ::IsChild(window, hWnd))) {
		m_FlyoutFocused = true;
		return;
	}
	// the focus went to another pane, or to another part of the window: the flyout has done its job
	if (PaneFromWindow(hWnd) || ::IsChild(::GetAncestor(m_hWnd, GA_ROOT), hWnd))
		HideFlyout();
}

LRESULT CDockHost::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
	switch (id) {
		case TimerAnimation: {
			const double t = std::min(1.0, (double)(::GetTickCount64() - m_AnimStart) / std::max(1, m_AnimMs));
			const double eased = 1 - (1 - t) * (1 - t) * (1 - t);
			m_FlyoutProgress = m_AnimFrom + (m_AnimTarget - m_AnimFrom) * eased;
			if (t >= 1) {
				KillTimer(TimerAnimation);
				m_FlyoutProgress = m_AnimTarget;
				if (m_FlyoutClosing) {
					FinishFlyoutClose();
					return 0;
				}
			}
			PositionFlyout();
			return 0;
		}

		case TimerHover: {
			KillTimer(TimerHover);
			POINT pt;
			::GetCursorPos(&pt);
			ScreenToClient(&pt);
			BarItem item;
			if (!m_HoverId.empty() && BarItemAt(pt, item) && item.Pane->Id() == m_HoverId)
				ShowFlyout(item.Pane, false);
			return 0;
		}

		case TimerLeave: {
			auto pane = FlyoutPane();
			if (!pane) {
				KillTimer(TimerLeave);
				return 0;
			}
			if (m_FlyoutClosing || m_FlyoutFocused) {
				m_LeaveSince = 0;
				return 0;
			}
			// a flyout that was only hovered goes when the mouse has left it and the bars
			POINT pt;
			::GetCursorPos(&pt);
			ScreenToClient(&pt);
			const RECT out = FlyoutRectAt(*pane->Group(), 1.0);
			bool inside = PtInRect(&out, pt) != FALSE;
			for (int side = 0; side < SideCount && !inside; side++) {
				const RECT bar = m_Layout.AutoHideBarRect((DockSide)side);
				inside = PtInRect(&bar, pt) != FALSE;
			}
			if (inside)
				m_LeaveSince = 0;
			else if (!m_LeaveSince)
				m_LeaveSince = ::GetTickCount64();
			else if (::GetTickCount64() - m_LeaveSince >= (ULONGLONG)m_LeaveMs)
				HideFlyout();
			return 0;
		}
	}
	handled = FALSE;
	return 0;
}

//
// commands
//

bool CDockHost::ClosePane(DockPane* pane) {
	if (!pane || !pane->Group() || !Has(pane->Caps, PaneCaps::CanClose))
		return false;
	if (OnPaneClosing && !OnPaneClosing(pane))
		return false;

	const std::vector<DockPane*> siblings = pane->Group()->Panes();
	const bool wasActive = ActivePane() == pane;
	if (!m_Layout.Hide(pane))
		return false;

	// keep the focus in the group the pane was closed in
	if (wasActive) {
		for (auto sibling : siblings) {
			if (sibling != pane && sibling->Group()) {
				ActivatePane(sibling->Group()->ActivePane());
				break;
			}
		}
	}
	if (OnPaneClosed)
		OnPaneClosed(pane);
	return true;
}

bool CDockHost::CanExecute(DockCommand command, const DockPane* pane) const {
	if (!pane || !pane->Group())
		return false;
	const auto& panes = pane->Group()->Panes();
	auto closable = [](const DockPane* p) { return Has(p->Caps, PaneCaps::CanClose); };

	switch (command) {
		case DockCommand::Close:
			return closable(pane);
		case DockCommand::CloseOthers:
			return std::any_of(panes.begin(), panes.end(), [&](auto p) { return p != pane && closable(p); });
		case DockCommand::CloseAll:
			return std::any_of(panes.begin(), panes.end(), closable);
		case DockCommand::AutoHide:
			return pane->State() == PaneState::Docked &&
				std::all_of(panes.begin(), panes.end(), [](auto p) { return Has(p->Caps, PaneCaps::CanAutoHide); });
		case DockCommand::Float:
			return pane->Kind() == PaneKind::Tool && pane->State() == PaneState::Docked && Has(pane->Caps, PaneCaps::CanFloat);
		case DockCommand::Dock:
			return pane->Kind() == PaneKind::Tool && (pane->State() == PaneState::Floating || pane->State() == PaneState::AutoHide);
	}
	return false;
}

bool CDockHost::Execute(DockCommand command, DockPane* pane) {
	if (!CanExecute(command, pane))
		return false;

	switch (command) {
		case DockCommand::AutoHide:
			return m_Layout.AutoHide(pane->Group());
		case DockCommand::Close:
			return ClosePane(pane);
		case DockCommand::Float:
			return FloatPane(pane);
		case DockCommand::Dock:
			return pane->State() == PaneState::AutoHide ? m_Layout.Unhide(pane->Group()) : DockFloating(pane);
		default:
			break;
	}

	// closing several: the group may disappear under us, so work from a copy
	const std::vector<DockPane*> panes = pane->Group()->Panes();
	bool any = false;
	for (auto p : panes)
		if (command == DockCommand::CloseAll || p != pane)
			any |= ClosePane(p);
	return any;
}

void CDockHost::ShowPaneMenu(DockPane* pane, POINT screen) {
	if (!pane || !pane->Group())
		return;

	CMenu menu;
	menu.CreatePopupMenu();
	auto add = [&](DockCommand command, const wchar_t* text) {
		menu.AppendMenu(MF_STRING | (CanExecute(command, pane) ? MF_ENABLED : MF_GRAYED), (UINT_PTR)(CmdBase + (UINT)command), text);
	};
	add(DockCommand::Close, L"&Close");
	if (pane->Group()->Panes().size() > 1) {
		add(DockCommand::CloseOthers, L"Close All &But This");
		add(DockCommand::CloseAll, L"Close &All Tabs");
	}
	if (pane->Kind() == PaneKind::Tool) {
		menu.AppendMenu(MF_SEPARATOR);
		if (pane->State() == PaneState::Floating)
			add(DockCommand::Dock, L"&Dock");
		else
			add(DockCommand::Float, L"Floa&t");
		add(DockCommand::AutoHide, L"&Auto Hide");
	}
	if (OnBuildPaneMenu) {
		menu.AppendMenu(MF_SEPARATOR);
		const int before = menu.GetMenuItemCount();
		OnBuildPaneMenu(pane, menu.m_hMenu);
		if (menu.GetMenuItemCount() == before)
			menu.DeleteMenu(before - 1, MF_BYPOSITION);
	}

	const UINT cmd = (UINT)menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, m_hWnd);
	if (cmd >= CmdBase && cmd < CmdBase + CmdRange)
		Execute((DockCommand)(cmd - CmdBase), pane);
	else if (cmd)
		::PostMessage(NotifyTarget(), WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
}

TabStripState CDockHost::GetTabState(const DockGroup* group) const {
	auto it = m_Groups.find(group);
	return it != m_Groups.end() ? it->second->State() : TabStripState{};
}

}
