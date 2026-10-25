// Toasts: each a layered popup owned by the window's top-level window (so it stays over it and hides with it), that
// doesn't take the activation. The host (not a window) keeps them newest first, places them in the window's corner
// (NtfPlaceToasts), fades and slides them, and runs their clocks on a message-only window's timer; a WinEvent hook on
// the window's location changes keeps them in its corner when it moves or is resized.

#include "NotifyData.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")

using namespace NtfDraw;

namespace {
	constexpr UINT FadeDuration = 200;   // ms
	constexpr UINT MoveDuration = 180;

	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	const wchar_t* const DefaultStrings[NTFSTR_COUNT] = { L"Show details", L"Hide details", L"%d more", L"Show fewer", L"Close" };
}

class NtfToastWnd;

// the clocks' and animations' timer
class NtfTickWnd : public ATL::CWindowImpl<NtfTickWnd> {
public:
	DECLARE_WND_CLASS_EX(L"NtfToastTick", 0, -1)
	explicit NtfTickWnd(NtfToastData* host) : m_Host(host) {
	}

	BEGIN_MSG_MAP(NtfTickWnd)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
	END_MSG_MAP()

private:
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	NtfToastData* m_Host;
};

struct NtfToastData {
	explicit NtfToastData(CToastHost* host) : host(host), tick(this) {
	}

	CToastHost* host;
	HWND owner = nullptr;
	UINT id = 0;
	std::vector<std::unique_ptr<NtfToastWnd>> toasts;   // the newest first; those going too, until they're gone
	std::vector<std::unique_ptr<NtfToastWnd>> dead;     // destroyed, deleted outside their own messages
	NtfTickWnd tick;
	HWINEVENTHOOK hook = nullptr;

	UINT corner = NTFC_BOTTOMRIGHT;
	int  maxShown = 0;
	int  widthDips = 360;
	int  animation = -1;
	HIMAGELIST images = nullptr;
	NTFCOLORS colors{};
	bool systemColors = true;
	std::wstring strings[NTFSTR_COUNT];

	UINT dpi = 0;
	HFONT font = nullptr;
	NtfFonts fonts;
	NtfMetrics m;
	UINT nextId = 0x8000;
	bool hovered = false;                               // the mouse over a toast: the clocks stop

	~NtfToastData();
	int  Scale(int dips) const {
		return ::MulDiv(dips, dpi ? dpi : 96, 96);
	}
	NTFCOLORS Colors() const {
		return systemColors ? SystemColors() : colors;
	}
	std::wstring String(UINT which) const {
		return which < NTFSTR_COUNT ? (strings[which].empty() ? DefaultStrings[which] : strings[which]) : std::wstring();
	}
	bool AnimationsOn() const {
		if (animation >= 0)
			return animation != 0;
		BOOL on = TRUE;
		::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
		return on != FALSE;
	}
	int  Width() const {
		return Scale(widthDips);
	}
	int  Find(UINT message) const;   // the toast showing the message; -1 if none (or going)
	bool EnsureFonts();                                 // for the window's DPI; true if they changed
	void Layout(NtfToastWnd& t);                        // its message for the width, and its size
	void Place(bool animate);
	void Tick();
	void StartTicking();
	void Close(NtfToastWnd* t, UINT reason);            // the user's: NTFN_CLOSED, then it fades out
	void Remove(NtfToastWnd* t);                        // fades out
	void Hover(bool on);
	LRESULT Fire(NtfToastWnd* t, NMHDR* pnm, UINT code);
	void Clear();                                       // all of them, at once

	static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
	static std::vector<NtfToastData*> s_Hosts;
};

std::vector<NtfToastData*> NtfToastData::s_Hosts;

// ---- A toast's window ---------------------------------------------------------------------------------

class NtfToastWnd : public ATL::CWindowImpl<NtfToastWnd> {
public:
	DECLARE_WND_CLASS_EX(L"NtfToast", CS_DROPSHADOW, -1)
	explicit NtfToastWnd(NtfToastData* host) : m_Host(host) {
	}

	BEGIN_MSG_MAP(NtfToastWnd)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
	END_MSG_MAP()

	NtfItem item;
	NtfTimer timer;
	bool started = false;                         // its clock started (when it first showed)
	bool shown = false;                           // placed and showing (else waiting)
	bool going = false;
	ULONGLONG fadeStart = 0;
	bool fading = false;                          // in (or out, going)
	RECT from{}, to{};                            // sliding to its place
	ULONGLONG moveStart = 0;
	bool moving = false;
	int  border = 1;                              // 0 where DWM draws it
	NtfHit hot, pressed;
	bool tracking = false;

	void Created() {
		// Windows 11 rounds the corners and draws the border; else the toast draws it
		int corner = 2;   // DWMWCP_ROUND
		border = 1;
		if (SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)))) {
			COLORREF color = m_Host->Colors().crBorder;
			if (SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, DWMWA_BORDER_COLOR, &color, sizeof(color))))
				border = 0;
		}
		SetAlpha(0);
	}
	void SetAlpha(int alpha) {
		::SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)std::clamp(alpha, 0, 255), LWA_ALPHA);
	}
	RECT Current() const {
		RECT rc;
		GetWindowRect(&rc);
		return rc;
	}

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	NtfHit  HitTest(POINT pt) const;
	void    Click(const NtfHit& hit);

	NtfToastData* m_Host;
};

LRESULT NtfToastWnd::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto& h = *m_Host;
	auto paint = [&](WTL::CDCHandle dc) {
		auto c = h.Colors();
		RECT rc;
		GetClientRect(&rc);
		const COLORREF back = c.crBack[(int)item.m.severity];
		FillSolid(dc, rc, back);
		auto old = dc.SelectFont(h.fonts.normal);
		PaintMessage(dc, item, 0, border, border, back, c, h.fonts, h.images, hot, pressed, h.m, h.dpi);
		dc.SelectFont(old);
		if (border) {
			WTL::CBrush brush;
			brush.CreateSolidBrush(c.crBorder);
			dc.FrameRect(&rc, brush);
		}
	};
	if (uMsg == WM_PRINTCLIENT) {
		paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	paint(mdc.m_hDC);
	return 0;
}

NtfHit NtfToastWnd::HitTest(POINT pt) const {
	if (going)
		return {};
	auto hit = HitMessage(item, { pt.x - border, pt.y - border }, m_Host->m);
	hit.item = 0;
	return hit;   // None: the body
}

LRESULT NtfToastWnd::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (!tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		tracking = ::TrackMouseEvent(&tme) != FALSE;
		m_Host->Hover(true);
	}
	auto hit = HitTest(PointOf(lParam));
	if (!(hit == hot)) {
		hot = hit;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT NtfToastWnd::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	tracking = false;
	hot = {};
	Invalidate(FALSE);
	m_Host->Hover(false);
	return 0;
}

LRESULT NtfToastWnd::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT && hot.part == NtfPart::Link) {
		::SetCursor(::LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT NtfToastWnd::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (going)
		return 0;
	pressed = HitTest(PointOf(lParam));
	hot = pressed;
	SetCapture();
	Invalidate(FALSE);
	return 0;
}

LRESULT NtfToastWnd::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (pressed.item < 0)
		return 0;
	auto was = pressed;
	pressed = {};
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate(FALSE);
	POINT pt = PointOf(lParam);
	RECT rc;
	GetClientRect(&rc);
	if (::PtInRect(&rc, pt) && HitTest(pt) == was)
		Click(was);
	return 0;
}

LRESULT NtfToastWnd::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (pressed.item >= 0) {
		pressed = {};
		Invalidate(FALSE);
	}
	return 0;
}

void NtfToastWnd::Click(const NtfHit& hit) {
	auto& h = *m_Host;
	switch (hit.part) {
		case NtfPart::Close:
			h.Close(this, NTFR_CLOSEBUTTON);
			break;
		case NtfPart::Check:
			item.m.checked = !item.m.checked;
			Invalidate(FALSE);
			break;
		case NtfPart::Link: {
			if (hit.index < 0 || hit.index >= (int)item.layout.textRuns.size())
				break;
			auto link = item.layout.textRuns[hit.index].link;
			if (link == NtfDetailsLink) {
				item.m.expanded = !item.m.expanded;
				h.Layout(*this);
				h.Place(true);
				Invalidate(FALSE);
				break;
			}
			NMNTFLINK nm{};
			nm.id = item.m.id;
			nm.pszLink = link.c_str();
			nm.lParam = item.lParam;
			h.Fire(this, &nm.hdr, NTFN_LINKCLICK);
			break;
		}
		case NtfPart::Button:
		case NtfPart::None: {
			// a button, or the toast's body (button 0)
			NMNTFBUTTON nm{};
			nm.id = item.m.id;
			nm.button = hit.part == NtfPart::Button && hit.index < (int)item.m.buttons.size() ? item.m.buttons[hit.index].id : 0;
			nm.checked = item.m.checked;
			nm.lParam = item.lParam;
			const UINT id = item.m.id;
			if (!h.Fire(this, &nm.hdr, NTFN_BUTTONCLICK))
				if (int i = h.Find(id); i >= 0)
					h.Close(h.toasts[i].get(), NTFR_BUTTON);
			break;
		}
		default:
			break;
	}
}

LRESULT NtfTickWnd::OnTimer(UINT, WPARAM, LPARAM, BOOL&) {
	m_Host->Tick();
	return 0;
}

// ---- The host -----------------------------------------------------------------------------------------

NtfToastData::~NtfToastData() {
	Clear();
	fonts.Destroy();
	if (font)
		::DeleteObject(font);
}

int NtfToastData::Find(UINT message) const {
	for (int i = 0; i < (int)toasts.size(); i++)
		if (toasts[i]->item.m.id == message && !toasts[i]->going)
			return i;
	return -1;
}

bool NtfToastData::EnsureFonts() {
	UINT now = owner ? ::GetDpiForWindow(owner) : 96;
	if (now == dpi && font)
		return false;
	dpi = now;
	if (font)
		::DeleteObject(font);
	NONCLIENTMETRICSW ncm{ sizeof(ncm) };
	::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
	font = ::CreateFontIndirectW(&ncm.lfMessageFont);
	auto mt = NtfMetrics{}.Scaled(dpi);
	int cx, cy;
	if (images && ::ImageList_GetIconSize(images, &cx, &cy))
		mt.icon = cx;
	fonts.Create(font, mt.icon);
	mt.lineHeight = std::max(mt.lineHeight, fonts.textHeight + Scale(2));
	mt.buttonHeight = std::max(mt.buttonHeight, fonts.textHeight + Scale(8));
	m = mt;
	for (auto& t : toasts)
		Layout(*t);
	return true;
}

void NtfToastData::Layout(NtfToastWnd& t) {
	HDC dc = ::GetDC(nullptr);
	const int width = Width();
	t.item.layout = NtfLayoutMessage(t.item.m, NtfStyle::Toast, width - 2 * t.border, m, Measurer(dc, fonts),
		String(t.item.m.expanded ? NTFSTR_HIDEDETAILS : NTFSTR_SHOWDETAILS));
	::ReleaseDC(nullptr, dc);
	if (t.IsWindow())
		t.SetWindowPos(nullptr, 0, 0, width, t.item.layout.height + 2 * t.border, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void NtfToastData::Place(bool animate) {
	if (!owner || !::IsWindow(owner))
		return;
	animate = animate && AnimationsOn();
	RECT area;
	::GetClientRect(owner, &area);
	::MapWindowPoints(owner, nullptr, (POINT*)&area, 2);
	std::vector<NtfToastWnd*> live;
	std::vector<int> heights;
	for (auto& t : toasts)
		if (!t->going) {
			live.push_back(t.get());
			heights.push_back(t->item.layout.height + 2 * t->border);
		}
	auto rects = NtfPlaceToasts(area, (NtfCorner)corner, heights, Width(), m.toastMargin, m.toastGap, maxShown);
	const ULONGLONG now = ::GetTickCount64();
	const bool bottom = corner == NTFC_BOTTOMLEFT || corner == NTFC_BOTTOMRIGHT;
	for (size_t i = 0; i < live.size(); i++) {
		auto& t = *live[i];
		const RECT& r = rects[i];
		if (::IsRectEmpty(&r)) {
			// no room: it waits, its clock stopped
			if (t.shown) {
				t.ShowWindow(SW_HIDE);
				t.shown = false;
				t.timer.Pause(now);
			}
			continue;
		}
		if (!t.shown) {
			// it shows: from a little nearer the edge, fading in
			t.to = r;
			t.from = r;
			::OffsetRect(&t.from, 0, bottom ? Scale(16) : -Scale(16));
			if (animate) {
				t.moving = t.fading = true;
				t.moveStart = t.fadeStart = now;
				t.SetAlpha(0);
				t.SetWindowPos(nullptr, &t.from, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			else {
				t.moving = t.fading = false;
				t.SetAlpha(255);
				t.SetWindowPos(nullptr, &r, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			t.ShowWindow(SW_SHOWNOACTIVATE);
			t.UpdateWindow();   // painted before anything moves it (a layered window keeps what it painted)
			t.shown = true;
			if (!t.started) {
				t.timer.Start(now, t.item.m.timeout);
				t.started = true;
			}
			else
				t.timer.Resume(now);
			if (hovered)
				t.timer.Pause(now);
			continue;
		}
		if (!::EqualRect(&r, &t.to)) {
			if (animate) {
				t.from = t.Current();
				t.moveStart = now;
				t.moving = true;
			}
			else {
				t.moving = false;
				t.SetWindowPos(nullptr, &r, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			t.to = r;
		}
		else if (!animate && !t.moving)
			t.SetWindowPos(nullptr, &r, SWP_NOZORDER | SWP_NOACTIVATE);   // the window moved: right there
	}
	StartTicking();
}

void NtfToastData::StartTicking() {
	if (tick.IsWindow())
		tick.SetTimer(1, 15);
}

void NtfToastData::Tick() {
	const ULONGLONG now = ::GetTickCount64();
	dead.clear();
	bool removed = false;
	for (size_t i = 0; i < toasts.size();) {
		auto& t = *toasts[i];
		if (!t.IsWindow()) {
			// the window went with its owner
			dead.push_back(std::move(toasts[i]));
			toasts.erase(toasts.begin() + i);
			continue;
		}
		if (t.going) {
			double f = t.fading ? NtfAnimate(t.fadeStart, FadeDuration, now) : 1.0;
			t.SetAlpha((int)std::lround(255 * (1.0 - f)));
			if (f >= 1.0) {
				t.DestroyWindow();
				dead.push_back(std::move(toasts[i]));
				toasts.erase(toasts.begin() + i);
				removed = true;
				continue;
			}
		}
		else if (t.shown) {
			if (t.fading) {
				double f = NtfAnimate(t.fadeStart, FadeDuration, now);
				t.SetAlpha((int)std::lround(255 * f));
				t.fading = f < 1.0;
			}
			if (t.moving) {
				double f = NtfAnimate(t.moveStart, MoveDuration, now);
				auto mix = [f](LONG a, LONG b) { return (int)std::lround(a + (b - a) * f); };
				RECT r{ mix(t.from.left, t.to.left), mix(t.from.top, t.to.top), mix(t.from.right, t.to.right), mix(t.from.bottom, t.to.bottom) };
				t.SetWindowPos(nullptr, &r, SWP_NOZORDER | SWP_NOACTIVATE);
				t.moving = f < 1.0;
			}
			if (!hovered && t.timer.Expired(now)) {
				Close(&t, NTFR_TIMEOUT);
				continue;   // it's going now: the next tick fades it
			}
		}
		i++;
	}
	if (removed)
		Place(true);
	if (toasts.empty() && tick.IsWindow())
		tick.KillTimer(1);
}

void NtfToastData::Hover(bool on) {
	if (hovered == on)
		return;
	hovered = on;
	const ULONGLONG now = ::GetTickCount64();
	for (auto& t : toasts)
		if (t->shown && !t->going) {
			if (on)
				t->timer.Pause(now);
			else
				t->timer.Resume(now);
		}
}

LRESULT NtfToastData::Fire(NtfToastWnd* t, NMHDR* pnm, UINT code) {
	pnm->hwndFrom = t->m_hWnd;
	pnm->idFrom = id;
	pnm->code = code;
	return owner ? ::SendMessage(owner, WM_NOTIFY, id, (LPARAM)pnm) : 0;
}

void NtfToastData::Close(NtfToastWnd* t, UINT reason) {
	NMNTFCLOSED nm{};
	nm.id = t->item.m.id;
	nm.reason = reason;
	nm.checked = t->item.m.checked;
	nm.lParam = t->item.lParam;
	const UINT closing = nm.id;
	Fire(t, &nm.hdr, NTFN_CLOSED);
	if (int i = Find(closing); i >= 0)   // unless the app closed it meanwhile
		Remove(toasts[i].get());
}

void NtfToastData::Remove(NtfToastWnd* t) {
	if (t->going)
		return;
	t->going = true;
	if (t->shown && AnimationsOn()) {
		t->fading = true;
		t->fadeStart = ::GetTickCount64();
	}
	else
		t->fading = false;   // the next tick destroys it
	if (t->hot.part != NtfPart::None || t->tracking) {
		// the mouse was over it: the others' clocks go on
		t->tracking = false;
		Hover(false);
	}
	Place(true);
}

void NtfToastData::Clear() {
	for (auto& t : toasts)
		if (t->IsWindow())
			t->DestroyWindow();
	for (auto& t : toasts)
		dead.push_back(std::move(t));
	toasts.clear();
	hovered = false;
}

void CALLBACK NtfToastData::OnWinEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
	if (idObject != OBJID_WINDOW || !hwnd)
		return;
	for (auto host : s_Hosts)
		if (host->owner && (host->owner == hwnd || ::IsChild(hwnd, host->owner)) && !host->toasts.empty()) {
			// the window moved, was resized, or went to another monitor
			bool fonts = host->EnsureFonts();
			host->Place(fonts);
		}
}

// ---- CToastHost ----------------------------------------------------------------------------------------

CToastHost::CToastHost() : m_Data(std::make_unique<NtfToastData>(this)) {
}

CToastHost::~CToastHost() {
	Detach();
}

BOOL CToastHost::Attach(HWND window, UINT id) {
	auto& d = *m_Data;
	if (!::IsWindow(window))
		return FALSE;
	Detach();
	d.owner = window;
	d.id = id;
	d.dpi = 0;
	d.EnsureFonts();
	d.tick.Create(HWND_MESSAGE);
	d.hook = ::SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, NtfToastData::OnWinEvent,
		::GetCurrentProcessId(), ::GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
	NtfToastData::s_Hosts.push_back(&d);
	return TRUE;
}

void CToastHost::Detach() {
	auto& d = *m_Data;
	if (!d.owner)
		return;
	std::erase(NtfToastData::s_Hosts, &d);
	if (d.hook)
		::UnhookWinEvent(d.hook);
	d.hook = nullptr;
	d.Clear();
	if (d.tick.IsWindow())
		d.tick.DestroyWindow();
	d.owner = nullptr;
}

HWND CToastHost::GetWindow() const {
	return m_Data->owner;
}

UINT CToastHost::Show(const NTFMESSAGE* message) {
	auto& d = *m_Data;
	if (!message || !d.owner || !::IsWindow(d.owner))
		return 0;
	d.EnsureFonts();
	NtfItem item;
	item.m = NtfFromApi(*message);
	item.lParam = message->lParam;
	if (item.m.id == 0) {
		do
			item.m.id = d.nextId++;
		while (d.Find(item.m.id) >= 0 || item.m.id == 0);
	}
	const UINT id = item.m.id;
	if (int i = d.Find(id); i >= 0) {
		// replaced where it is, its clock started again
		auto& t = *d.toasts[i];
		t.item = std::move(item);
		t.hot = t.pressed = {};
		if (t.started)
			t.timer.Start(::GetTickCount64(), t.item.m.timeout);
		if (d.hovered)
			t.timer.Pause(::GetTickCount64());
		d.Layout(t);
		t.Invalidate(FALSE);
		d.Place(true);
		return id;
	}
	auto t = std::make_unique<NtfToastWnd>(&d);
	t->item = std::move(item);
	HWND root = ::GetAncestor(d.owner, GA_ROOT);
	if (!t->Create(root, nullptr, nullptr, WS_POPUP, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
		return 0;
	t->Created();
	d.Layout(*t);
	d.toasts.insert(d.toasts.begin(), std::move(t));
	d.Place(true);
	return id;
}

UINT CToastHost::Show(UINT severity, LPCWSTR title, LPCWSTR text, UINT timeout, UINT id) {
	NTFMESSAGE m;
	m.id = id;
	m.severity = severity;
	m.pszTitle = title;
	m.pszText = text;
	m.timeout = timeout;
	return Show(&m);
}

BOOL CToastHost::Close(UINT id) {
	auto& d = *m_Data;
	int i = d.Find(id);
	if (i < 0)
		return FALSE;
	d.Remove(d.toasts[i].get());
	return TRUE;
}

void CToastHost::CloseAll() {
	auto& d = *m_Data;
	std::vector<NtfToastWnd*> live;
	for (auto& t : d.toasts)
		if (!t->going)
			live.push_back(t.get());
	for (auto t : live)
		d.Remove(t);
}

bool CToastHost::IsShown(UINT id) const {
	return m_Data->Find(id) >= 0;
}

int CToastHost::GetCount() const {
	auto& t = m_Data->toasts;
	return (int)std::count_if(t.begin(), t.end(), [](auto& x) { return !x->going; });
}

BOOL CToastHost::SetMessageText(UINT id, LPCWSTR text) {
	auto& d = *m_Data;
	int i = d.Find(id);
	if (i < 0)
		return FALSE;
	auto& t = *d.toasts[i];
	t.item.m.runs = NtfParseLinks(text ? text : L"");
	d.Layout(t);
	t.Invalidate(FALSE);
	d.Place(true);
	return TRUE;
}

BOOL CToastHost::SetProgress(UINT id, UINT kind, int value) {
	auto& d = *m_Data;
	int i = d.Find(id);
	if (i < 0)
		return FALSE;
	auto& t = *d.toasts[i];
	auto newKind = kind == NTFP_VALUE ? NtfProgress::Value : kind == NTFP_INDETERMINATE ? NtfProgress::Indeterminate : NtfProgress::None;
	t.item.m.progress = std::clamp(value, 0, 100);
	if (newKind != t.item.m.progressKind) {
		t.item.m.progressKind = newKind;
		d.Layout(t);
		d.Place(true);
	}
	t.Invalidate(FALSE);
	return TRUE;
}

bool CToastHost::GetChecked(UINT id) const {
	int i = m_Data->Find(id);
	return i >= 0 && m_Data->toasts[i]->item.m.checked;
}

void CToastHost::SetCorner(UINT corner) {
	m_Data->corner = std::min(corner, (UINT)NTFC_BOTTOMRIGHT);
	m_Data->Place(true);
}

void CToastHost::SetMaxShown(int count) {
	m_Data->maxShown = std::max(0, count);
	m_Data->Place(true);
}

void CToastHost::SetWidth(int dips) {
	auto& d = *m_Data;
	d.widthDips = std::max(120, dips);
	for (auto& t : d.toasts)
		d.Layout(*t);
	d.Place(true);
}

void CToastHost::SetAnimation(int on) {
	m_Data->animation = on < 0 ? -1 : on ? 1 : 0;
}

void CToastHost::SetImageList(HIMAGELIST images) {
	auto& d = *m_Data;
	d.images = images;
	d.dpi = 0;   // the icons' size
	if (d.owner) {
		d.EnsureFonts();
		d.Place(false);
	}
}

void CToastHost::SetColors(const NTFCOLORS* colors) {
	auto& d = *m_Data;
	d.systemColors = colors == nullptr;
	if (colors)
		d.colors = *colors;
	for (auto& t : d.toasts)
		if (t->IsWindow())
			t->Invalidate(FALSE);
}

NTFCOLORS CToastHost::GetColors() const {
	return m_Data->Colors();
}

void CToastHost::SetString(UINT which, LPCWSTR text) {
	if (which < NTFSTR_COUNT)
		m_Data->strings[which] = text ? text : L"";
}
