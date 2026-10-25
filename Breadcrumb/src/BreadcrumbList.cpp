#include "BreadcrumbData.h"

#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

using namespace BrcDraw;

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
		return s.size() >= prefix.size()
			&& ::CompareStringOrdinal(s.c_str(), (int)prefix.size(), prefix.c_str(), (int)prefix.size(), TRUE) == CSTR_EQUAL;
	}
}

// ---- The control's side -----------------------------------------------------------------------

void CBreadcrumbCtrl::OpenDropDown(const BrcHit& from, bool pressed, bool keyboard) {
	auto& d = *m_Data;
	EnsureLayout();
	const bool wasOpen = d.list && d.list->IsOpen();
	if (wasOpen) {
		d.switching = true;   // the focus stays with the control for the next one
		d.list->Close();
		d.switching = false;
	}
	const int index = from.part == BrcPart::Chevron ? from.index : -1;
	if (index >= (int)d.segments.size())
		return;

	// the root chevron's list: the folded segments, the deepest first, then the root's children
	std::vector<BrcRow> rows;
	if (from.part == BrcPart::Recent) {
		// the recent locations: ahead first, the current one in bold, then back
		for (int offset : d.backForward.MenuOffsets(16)) {
			auto loc = d.backForward.At(offset);
			rows.push_back({ loc->text.empty() ? loc->path : loc->text, loc->image, offset == 0, false, BrcTagBackForward + offset });
		}
	}
	else if (from.part == BrcPart::Overflow)
		for (int i : BrcFoldedSegments(d.layout))
			rows.push_back({ d.segments[i].text, d.segments[i].image, false, false, i });
	d.children.clear();
	if (from.part == BrcPart::Chevron || (from.part == BrcPart::Overflow && !(GetStyle() & BRCS_NOROOTCHEVRON))) {
		auto path = index >= 0 ? PathTo(index + 1) : std::wstring();
		NMBRCCHILDREN nm{};
		nm.index = index;
		nm.pszPath = path.c_str();
		nm.lParam = index >= 0 ? d.segments[index].lParam : 0;
		d.collecting = &d.children;
		FireNotify(&nm.hdr, BRCN_GETCHILDREN);
		d.collecting = nullptr;
		if (!IsWindow() || index >= (int)d.segments.size())
			return;
	}
	// the child that is the next segment, in bold
	const BrcSegment* next = index + 1 < (int)d.segments.size() ? &d.segments[index + 1] : nullptr;
	auto isNext = [&](const BrcChild& c) {
		if (!next)
			return false;
		return !c.name.empty() && !next->name.empty() ? BrcSamePath(c.name, next->name) : BrcSamePath(c.text, next->text);
	};
	if (!rows.empty() && !d.children.empty())
		rows.push_back({ L"", -1, false, true, -1 });
	rows.reserve(rows.size() + d.children.size());
	for (int k = 0; k < (int)d.children.size(); k++) {
		auto& c = d.children[k];
		rows.push_back({ c.text, c.image, isNext(c), false, BrcTagChild + k });
	}

	if (rows.empty()) {
		// nothing under it: the one that was open closed for nothing
		if (wasOpen) {
			HWND f = d.focusBefore;
			d.focusBefore = nullptr;
			if (f && ::IsWindow(f) && ::GetFocus() == m_hWnd)
				::SetFocus(f);
		}
		return;
	}

	if (!wasOpen)
		d.focusBefore = ::GetFocus();
	if (::GetFocus() != m_hWnd)
		SetFocus();   // the keys come here and go on to the list
	d.opened = from;
	d.openedIndex = index;
	d.listKind = BrcList::Children;
	SetHot(from);
	Invalidate(FALSE);

	EnsureList();
	RECT part = from.part == BrcPart::Chevron ? d.layout.chevron[index] : from.part == BrcPart::Recent ? d.layout.recent : d.layout.overflow;
	RECT under{ part.left, d.layout.box.top, part.right, d.layout.box.bottom };
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&under, 2);
	d.list->Show(std::move(rows), under, pressed);
	if (keyboard)
		d.list->SelectFirst();
	NotifyAccessible(m_hWnd, EVENT_OBJECT_STATECHANGE, AccChildId(from));
}

void CBreadcrumbCtrl::EnsureList() {
	auto& d = *m_Data;
	if (!d.list)
		d.list = std::make_unique<BrcDropList>(this);
	if (!d.list->IsWindow())
		d.list->Create(m_hWnd, nullptr, nullptr, WS_POPUP | WS_CLIPSIBLINGS, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
}

void CBreadcrumbCtrl::CloseDropDown() {
	auto& d = *m_Data;
	if (d.list && d.list->IsOpen())
		d.list->Close();
}

void CBreadcrumbCtrl::DropPicked(int tag) {
	auto& d = *m_Data;
	int index = d.openedIndex;
	d.list->Close();
	if (tag >= BrcTagBackForward - 0x1000)
		Go(tag - BrcTagBackForward);
	else if (tag >= BrcTagText) {
		// a suggestion or a path typed before: as if typed
		if (tag - BrcTagText < (int)d.texts.size() && d.editing) {
			SetText(d.texts[tag - BrcTagText], false);
			Commit();
		}
	}
	else if (tag >= BrcTagChild)
		Navigate(BRCNS_CHILD, index, tag - BrcTagChild);
	else if (tag >= 0)
		Navigate(BRCNS_SEGMENT, tag, -1);
}

void CBreadcrumbCtrl::DropClosed() {
	auto& d = *m_Data;
	auto kind = d.listKind;
	auto opened = d.opened;
	d.listKind = BrcList::None;
	d.opened = {};
	if (opened.part != BrcPart::None)
		NotifyAccessible(m_hWnd, EVENT_OBJECT_STATECHANGE, AccChildId(opened));
	d.openedIndex = -2;
	if (!IsWindow())
		return;
	Invalidate(FALSE);
	if (kind == BrcList::Children && !d.switching) {
		HWND f = d.focusBefore;
		d.focusBefore = nullptr;
		if (f && ::IsWindow(f) && ::GetFocus() == m_hWnd)
			::SetFocus(f);
		// hot as the mouse is now
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		RECT rc;
		GetClientRect(&rc);
		SetHot(::PtInRect(&rc, pt) ? HitTest(pt) : BrcHit{});
	}
}

bool CBreadcrumbCtrl::DropMouse(POINT pt) {
	auto& d = *m_Data;
	auto hit = HitTest(pt);
	if ((hit.part == BrcPart::Chevron || hit.part == BrcPart::Overflow || hit.part == BrcPart::Recent) && !(hit == d.opened)) {
		// as on a menu bar: the list of what the mouse moved onto
		OpenDropDown(hit, ::GetKeyState(VK_LBUTTON) < 0);
		return true;
	}
	return false;
}

// ---- The list ---------------------------------------------------------------------------------------

int BrcDropList::Scale(int dips) const {
	return m_Owner->Scale(dips);
}

void BrcDropList::Show(std::vector<BrcRow> rows, const RECT& under, bool pressed, bool capture, int fixedWidth) {
	auto* o = m_Owner;
	if (m_Open && m_Capture && !capture && GetCapture() == m_hWnd) {
		m_Closing = true;
		ReleaseCapture();
		m_Closing = false;
	}
	m_Rows = std::move(rows);
	m_Hot = m_Pressed = m_ThumbGrab = -1;
	m_Scroll = 0;
	m_Typed.clear();
	m_FromOpener = pressed;
	m_Capture = capture;
	// taking the capture sends a WM_MOUSEMOVE where the mouse rests, maybe over another chevron
	::GetCursorPos(&m_Mouse);

	const int icon = o->m_Data->m.icon;
	const bool images = o->m_Images && std::any_of(m_Rows.begin(), m_Rows.end(), [](auto& r) { return r.image >= 0; });
	m_RowHeight = std::max(icon + Scale(8), o->m_TextHeight + Scale(8));
	m_SeparatorHeight = Scale(9);
	m_ImageX = Scale(10);
	m_TextX = images ? m_ImageX + icon + Scale(10) : Scale(14);
	m_Y.resize(m_Rows.size() + 1);
	int y = 0, text = 0;
	{
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(o->m_hFont);
		for (size_t i = 0; i < m_Rows.size(); i++) {
			m_Y[i] = y;
			auto& r = m_Rows[i];
			y += r.separator ? m_SeparatorHeight : m_RowHeight;
			if (r.separator)
				continue;
			dc.SelectFont(r.bold ? o->m_hBold : o->m_hFont);
			SIZE size{};
			dc.GetTextExtent(r.text.c_str(), (int)r.text.size(), &size);
			text = std::max(text, (int)size.cx);
		}
		dc.SelectFont(old);
	}
	m_Y.back() = y;
	const int total = y;

	// Windows 11 rounds the corners and draws the border; else the list draws it
	auto c = o->GetColors();
	int corner = 3;   // DWMWCP_ROUNDSMALL
	m_Border = 1;
	if (SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner)))) {
		COLORREF border = c.crBorder;
		if (SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, DWMWA_BORDER_COLOR, &border, sizeof(border))))
			m_Border = 0;
	}

	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfoW(::MonitorFromRect(&under, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	const int most = 20 * m_RowHeight;
	const int below = (int)(work.bottom - under.bottom), above = (int)(under.top - work.top);
	const bool down = below >= std::min(total, most) + 2 * m_Border || below >= above;
	m_Height = std::max(0, std::min({ total, most, (down ? below : above) - 2 * m_Border }));
	m_Scrollbar = total > m_Height ? Scale(10) : 0;

	int width = m_TextX + text + Scale(28) + m_Scrollbar + 2 * m_Border;
	width = std::clamp(width, Scale(160), std::max(Scale(160), (int)(work.right - work.left) / 2));
	if (fixedWidth > 0)
		width = std::min(fixedWidth, (int)(work.right - work.left));
	const int height = m_Height + 2 * m_Border;
	const int x = std::clamp((int)under.left, (int)work.left, std::max((int)work.left, (int)work.right - width));
	const int top = down ? (int)under.bottom : (int)under.top - height;

	// the next segment in view
	for (int i = 0; i < (int)m_Rows.size(); i++)
		if (m_Rows[i].bold) {
			if (m_Y[i + 1] > m_Height)
				ScrollTo(m_Y[i] - (m_Height - m_RowHeight) / 2);
			break;
		}

	SetWindowPos(HWND_TOP, x, top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	::InvalidateRect(m_hWnd, nullptr, FALSE);
	m_Open = true;
	if (m_Capture)
		SetCapture();
}

void BrcDropList::Close() {
	if (!m_Open)
		return;
	m_Open = false;
	m_Closing = true;
	if (m_Capture && GetCapture() == m_hWnd)
		ReleaseCapture();
	ShowWindow(SW_HIDE);
	m_Closing = false;
	m_Owner->DropClosed();
}

int BrcDropList::RowAt(POINT pt) const {
	RECT rc;
	GetClientRect(&rc);
	if (!::PtInRect(&rc, pt) || pt.x >= rc.right - m_Border - m_Scrollbar || pt.y < m_Border || pt.y >= m_Border + m_Height)
		return -1;
	int y = pt.y - m_Border + m_Scroll;
	auto it = std::upper_bound(m_Y.begin(), m_Y.end(), y);
	int row = (int)(it - m_Y.begin()) - 1;
	return row >= 0 && row < (int)m_Rows.size() ? row : -1;
}

RECT BrcDropList::RowRect(int row) const {
	RECT rc;
	GetClientRect(&rc);
	int top = m_Border + m_Y[row] - m_Scroll;
	return { m_Border, top, rc.right - m_Border - m_Scrollbar, top + m_Y[row + 1] - m_Y[row] };
}

RECT BrcDropList::ThumbRect() const {
	if (!m_Scrollbar)
		return {};
	RECT rc;
	GetClientRect(&rc);
	const int total = m_Y.back();
	const int thumb = std::min(m_Height, std::max(Scale(20), m_Height * m_Height / std::max(1, total)));
	const int range = total - m_Height;
	const int top = m_Border + (range > 0 ? (m_Height - thumb) * m_Scroll / range : 0);
	return { rc.right - m_Border - m_Scrollbar + Scale(3), top + Scale(2), rc.right - m_Border - Scale(3), top + thumb - Scale(2) };
}

bool BrcDropList::Pickable(int row) const {
	return row >= 0 && row < (int)m_Rows.size() && !m_Rows[row].separator;
}

void BrcDropList::SetHot(int row, bool scroll) {
	if (row != m_Hot) {
		m_Hot = row;
		Invalidate();
		if (row >= 0) {
			// the row selected, as an active descendant: the focus stays with the control
			m_Owner->NotifyAccessible(m_hWnd, EVENT_OBJECT_FOCUS, row + 1);
			m_Owner->NotifyAccessible(m_hWnd, EVENT_OBJECT_SELECTION, row + 1);
		}
	}
	if (scroll && row >= 0) {
		if (m_Y[row] < m_Scroll)
			ScrollTo(m_Y[row]);
		else if (m_Y[row + 1] > m_Scroll + m_Height)
			ScrollTo(m_Y[row + 1] - m_Height);
	}
}

void BrcDropList::ScrollTo(int y) {
	y = std::clamp(y, 0, std::max(0, m_Y.back() - m_Height));
	if (y != m_Scroll) {
		m_Scroll = y;
		Invalidate();
	}
}

void BrcDropList::Move(int from, int step) {
	const int dir = step > 0 ? 1 : -1;
	int count = std::abs(step), target = -1;
	for (int i = from + dir; i >= 0 && i < (int)m_Rows.size(); i += dir)
		if (Pickable(i)) {
			target = i;
			if (--count == 0)
				break;
		}
	if (target >= 0)
		SetHot(target, true);
}

void BrcDropList::SelectFirst() {
	for (int i = 0; i < (int)m_Rows.size(); i++)
		if (m_Rows[i].bold && Pickable(i)) {
			SetHot(i, true);
			return;
		}
	Move(-1, 1);
}

void BrcDropList::MoveHot(UINT vk) {
	const int page = std::max(1, m_Height / std::max(1, m_RowHeight) - 1);
	switch (vk) {
		case VK_DOWN: Move(m_Hot, 1); break;
		case VK_UP: Move(m_Hot < 0 ? (int)m_Rows.size() : m_Hot, -1); break;
		case VK_HOME: Move(-1, 1); break;
		case VK_END: Move((int)m_Rows.size(), -1); break;
		case VK_NEXT: Move(m_Hot, page); break;
		case VK_PRIOR: Move(m_Hot < 0 ? (int)m_Rows.size() : m_Hot, -page); break;
	}
}

bool BrcDropList::KeyDown(UINT vk) {
	switch (vk) {
		case VK_DOWN:
		case VK_UP:
		case VK_HOME:
		case VK_END:
		case VK_NEXT:
		case VK_PRIOR:
			MoveHot(vk);
			break;
		case VK_RETURN:
			if (Pickable(m_Hot))
				m_Owner->DropPicked(m_Rows[m_Hot].tag);
			else
				Close();
			break;
		case VK_ESCAPE:
		case VK_TAB:
			Close();
			break;
		default:
			return false;
	}
	return true;
}

void BrcDropList::Char(wchar_t ch) {
	if (ch < L' ')
		return;
	ULONGLONG now = ::GetTickCount64();
	if (now - m_TypedTick > 1000)
		m_Typed.clear();
	m_TypedTick = now;
	m_Typed += ch;
	// a first letter looks after the selected row; more letters go on matching from it
	const int n = (int)m_Rows.size();
	const int start = m_Typed.size() == 1 ? m_Hot + 1 : std::max(0, m_Hot);
	for (int k = 0; k < n; k++) {
		int i = (start + k) % n;
		if (Pickable(i) && StartsWith(m_Rows[i].text, m_Typed)) {
			SetHot(i, true);
			return;
		}
	}
}

void BrcDropList::Wheel(int delta) {
	ScrollTo(m_Scroll - delta * 3 * m_RowHeight / WHEEL_DELTA);
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	int row = RowAt(pt);
	if (row >= 0)
		SetHot(Pickable(row) ? row : -1, false);
}

// ---- The list's mouse ---------------------------------------------------------------------------------

LRESULT BrcDropList::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt = PointOf(lParam);
	POINT screen = pt;
	ClientToScreen(&screen);
	if (screen.x == m_Mouse.x && screen.y == m_Mouse.y)
		return 0;
	m_Mouse = screen;
	if (m_ThumbGrab >= 0) {
		RECT thumb = ThumbRect();
		const int size = (int)(thumb.bottom - thumb.top) + 2 * Scale(2);
		const int range = m_Height - size;
		if (range > 0)
			ScrollTo((pt.y - m_ThumbGrab - m_Border) * (m_Y.back() - m_Height) / range);
		return 0;
	}
	RECT rc;
	GetClientRect(&rc);
	if (::PtInRect(&rc, pt)) {
		int row = RowAt(pt);
		SetHot(Pickable(row) ? row : -1, false);
		return 0;
	}
	if (!m_Capture)
		return 0;
	// outside: onto another chevron of the bar, its list opens instead
	POINT cp = pt;
	ClientToScreen(&cp);
	::ScreenToClient(m_Owner->m_hWnd, &cp);
	RECT orc;
	m_Owner->GetClientRect(&orc);
	if (::PtInRect(&orc, cp) && m_Owner->DropMouse(cp))
		return 0;
	SetHot(-1, false);
	return 0;
}

LRESULT BrcDropList::OnButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	POINT pt = PointOf(lParam);
	RECT rc;
	GetClientRect(&rc);
	if (::PtInRect(&rc, pt)) {
		if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK) {
			if (m_Scrollbar && pt.x >= rc.right - m_Border - m_Scrollbar) {
				RECT thumb = ThumbRect();
				if (pt.y >= thumb.top - Scale(2) && pt.y < thumb.bottom + Scale(2))
					m_ThumbGrab = pt.y - (thumb.top - Scale(2));
				else
					ScrollTo(m_Scroll + (pt.y < thumb.top ? -m_Height : m_Height));   // a page
				return 0;
			}
			m_Pressed = RowAt(pt);
		}
		return 0;
	}
	if (!m_Capture)
		return 0;
	// outside: it closes, as a menu does, and the click is eaten; but on the bar, away from what opened the list, the
	// bar gets it (another segment)
	POINT cp = pt;
	ClientToScreen(&cp);
	HWND bar = m_Owner->m_hWnd;
	::ScreenToClient(bar, &cp);
	RECT orc;
	m_Owner->GetClientRect(&orc);
	const bool onBar = ::PtInRect(&orc, cp) != FALSE;
	const bool onOpener = onBar && m_Owner->HitTest(cp) == m_Owner->m_Data->opened;
	Close();
	if (onBar && !onOpener && uMsg == WM_LBUTTONDOWN && ::IsWindow(bar))
		::SendMessage(bar, WM_LBUTTONDOWN, wParam, MAKELPARAM(cp.x, cp.y));
	return 0;
}

LRESULT BrcDropList::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_ThumbGrab >= 0) {
		m_ThumbGrab = -1;
		return 0;
	}
	const int row = RowAt(PointOf(lParam));
	const bool fromOpener = m_FromOpener;
	const int pressed = m_Pressed;
	m_FromOpener = false;
	m_Pressed = -1;
	// a click on a row, or the press that opened the list dragged onto one and released
	if (Pickable(row) && (row == pressed || fromOpener))
		m_Owner->DropPicked(m_Rows[row].tag);
	return 0;
}

LRESULT BrcDropList::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Open && m_Capture && !m_Closing)
		Close();
	return 0;
}

LRESULT BrcDropList::OnCancelMode(UINT, WPARAM, LPARAM, BOOL&) {
	Close();
	return 0;
}

// ---- The list's painting ---------------------------------------------------------------------------------

LRESULT BrcDropList::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto* o = m_Owner;
	auto paint = [&](WTL::CDCHandle dc) {
		auto c = o->PaintColors();
		RECT rc;
		GetClientRect(&rc);
		FillSolid(dc, rc, c.crListBack);
		if (m_Border) {
			WTL::CBrush brush;
			brush.CreateSolidBrush(c.crBorder);
			dc.FrameRect(&rc, brush);
		}
		const int saved = dc.SaveDC();
		dc.IntersectClipRect(m_Border, m_Border, rc.right - m_Border, m_Border + m_Height);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(c.crText);
		const int icon = o->m_Data->m.icon;
		for (int i = 0; i < (int)m_Rows.size(); i++) {
			RECT r = RowRect(i);
			if (r.bottom <= m_Border || r.top >= m_Border + m_Height)
				continue;
			auto& row = m_Rows[i];
			if (row.separator) {
				int cy = (r.top + r.bottom) / 2;
				FillSolid(dc, { r.left + Scale(10), cy, r.right - Scale(10), cy + 1 }, c.crBorder);
				continue;
			}
			if (i == m_Hot) {
				RECT hot{ r.left + Scale(4), r.top + Scale(1), r.right - Scale(4), r.bottom - Scale(1) };
				RoundFill(dc, hot, c.crListHotBack, c.crListHotBack, Scale(6));
			}
			if (row.image >= 0 && o->m_Images)
				::ImageList_Draw(o->m_Images, row.image, dc, r.left + m_ImageX, (r.top + r.bottom - icon) / 2, ILD_TRANSPARENT);
			dc.SelectFont(row.bold ? o->m_hBold : o->m_hFont);
			// high contrast: the selected row in the highlight colors, as the system's lists
			dc.SetTextColor(HighContrast && i == m_Hot ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : c.crText);
			RECT t{ r.left + m_TextX, r.top, r.right - Scale(10), r.bottom };
			dc.DrawText(row.text.c_str(), (int)row.text.size(), &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		RECT thumb = ThumbRect();
		if (!::IsRectEmpty(&thumb)) {
			COLORREF color = Blend(c.crListBack, c.crText, m_ThumbGrab >= 0 ? 55 : 35);
			RoundFill(dc, thumb, color, color, (int)(thumb.right - thumb.left));
		}
		dc.RestoreDC(saved);
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
