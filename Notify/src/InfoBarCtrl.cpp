#include "NotifyData.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

using namespace NtfDraw;

static_assert(NTFSEV_ERROR == (int)NtfSeverity::Error);

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	const wchar_t* const DefaultStrings[NTFSTR_COUNT] = { L"Show details", L"Hide details", L"%d more", L"Show fewer", L"Close" };
}

// ---- Construction ------------------------------------------------------------------

CInfoBarCtrl::CInfoBarCtrl() : m_Data(std::make_unique<NtfBarData>()) {
}

CInfoBarCtrl::~CInfoBarCtrl() {
	m_Data->fonts.Destroy();
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
}

LRESULT CInfoBarCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	UpdateFonts();
	return 0;
}

LRESULT CInfoBarCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	KillTimer(AnimationTimer);
	bHandled = FALSE;
	return 0;
}

LRESULT CInfoBarCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CInfoBarCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	EnsureLayout();
	Invalidate(FALSE);
	NotifyHeight();   // the text wraps differently
	return 0;
}

LRESULT CInfoBarCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

// ---- Fonts and metrics ----------------------------------------------------------------

int CInfoBarCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CInfoBarCtrl::UpdateFonts() {
	auto& d = *m_Data;
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	auto m = NtfMetrics{}.Scaled(m_Dpi);
	int cx, cy;
	if (m_Images && ::ImageList_GetIconSize(m_Images, &cx, &cy))
		m.icon = cx;
	d.fonts.Create(m_hFont, m.icon);
	m.lineHeight = std::max(m.lineHeight, d.fonts.textHeight + Scale(2));
	m.buttonHeight = std::max(m.buttonHeight, d.fonts.textHeight + Scale(8));
	m.more = std::max(m.more, d.fonts.textHeight + Scale(6));
	d.m = m;
	Changed();
}

LRESULT CInfoBarCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CInfoBarCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CInfoBarCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// before the parent too: its WM_DPICHANGED lays the window out with the height for the new DPI
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		m_Dpi = dpi;
		UpdateFonts();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CInfoBarCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS)   // the message font
		UpdateFonts();
	bHandled = FALSE;
	return 0;
}

LRESULT CInfoBarCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

void CInfoBarCtrl::SetImageList(HIMAGELIST images) {
	m_Images = images;
	UpdateFonts();   // the icons' size
}

void CInfoBarCtrl::SetColors(const NTFCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

NTFCOLORS CInfoBarCtrl::GetColors() const {
	return m_SystemColors ? SystemColors() : m_Colors;
}

void CInfoBarCtrl::SetString(UINT which, LPCWSTR text) {
	if (which < NTFSTR_COUNT) {
		m_Data->strings[which] = text ? text : L"";
		Changed();
	}
}

std::wstring CInfoBarCtrl::String(UINT which, int number) const {
	if (which >= NTFSTR_COUNT)
		return {};
	std::wstring s = m_Data->strings[which].empty() ? DefaultStrings[which] : m_Data->strings[which];
	if (auto i = s.find(L"%d"); i != std::wstring::npos)
		s.replace(i, 2, std::to_wstring(number));
	return s;
}

// ---- Messages -----------------------------------------------------------------------------------

int CInfoBarCtrl::Find(UINT id) const {
	auto& items = m_Data->items;
	for (int i = 0; i < (int)items.size(); i++)
		if (items[i].m.id == id && !items[i].going)
			return i;
	return -1;
}

UINT CInfoBarCtrl::AddMessage(const NTFMESSAGE* message) {
	auto& d = *m_Data;
	if (!message)
		return 0;
	NtfItem item;
	item.m = NtfFromApi(*message);
	item.lParam = message->lParam;
	if (item.m.id == 0) {
		do
			item.m.id = d.nextId++;
		while (Find(item.m.id) >= 0 || item.m.id == 0);
	}
	const UINT id = item.m.id;
	if (int i = Find(id); i >= 0) {
		// replaced where it is
		item.appearing = d.items[i].appearing;
		item.animStart = d.items[i].animStart;
		d.items[i] = std::move(item);
	}
	else {
		if (IsWindow() && AnimationsOn()) {
			item.appearing = true;
			item.animStart = ::GetTickCount64();
		}
		d.items.insert(d.items.begin(), std::move(item));
	}
	d.hot = d.pressed = {};
	Changed();
	return id;
}

UINT CInfoBarCtrl::AddMessage(UINT severity, LPCWSTR title, LPCWSTR text, UINT id) {
	NTFMESSAGE m;
	m.id = id;
	m.severity = severity;
	m.pszTitle = title;
	m.pszText = text;
	return AddMessage(&m);
}

BOOL CInfoBarCtrl::RemoveMessage(UINT id) {
	auto& d = *m_Data;
	int i = Find(id);
	if (i < 0)
		return FALSE;
	if (IsWindow() && AnimationsOn()) {
		// slides out from as far as it is
		auto& item = d.items[i];
		const ULONGLONG now = ::GetTickCount64();
		const double shown = item.Shown(now, NtfSlideDuration);
		item.appearing = false;
		item.going = true;
		item.animStart = now - (ULONGLONG)((1.0 - shown) * NtfSlideDuration);
	}
	else
		d.items.erase(d.items.begin() + i);
	d.hot = d.pressed = {};
	Changed();
	return TRUE;
}

void CInfoBarCtrl::RemoveAll() {
	std::vector<UINT> ids;
	for (auto& item : m_Data->items)
		if (!item.going)
			ids.push_back(item.m.id);
	for (UINT id : ids)
		RemoveMessage(id);
}

bool CInfoBarCtrl::HasMessage(UINT id) const {
	return Find(id) >= 0;
}

int CInfoBarCtrl::GetMessageCount() const {
	return (int)std::count_if(m_Data->items.begin(), m_Data->items.end(), [](auto& item) { return !item.going; });
}

BOOL CInfoBarCtrl::SetMessageText(UINT id, LPCWSTR text) {
	int i = Find(id);
	if (i < 0)
		return FALSE;
	m_Data->items[i].m.runs = NtfParseLinks(text ? text : L"");
	Changed();
	return TRUE;
}

BOOL CInfoBarCtrl::SetProgress(UINT id, UINT kind, int value) {
	int i = Find(id);
	if (i < 0)
		return FALSE;
	auto& m = m_Data->items[i].m;
	auto newKind = kind == NTFP_VALUE ? NtfProgress::Value : kind == NTFP_INDETERMINATE ? NtfProgress::Indeterminate : NtfProgress::None;
	value = std::clamp(value, 0, 100);
	if (newKind != m.progressKind) {
		m.progressKind = newKind;
		m.progress = value;
		Changed();   // a bar more or less
	}
	else if (value != m.progress) {
		m.progress = value;
		Invalidate(FALSE);
	}
	return TRUE;
}

BOOL CInfoBarCtrl::SetChecked(UINT id, bool checked) {
	int i = Find(id);
	if (i < 0)
		return FALSE;
	m_Data->items[i].m.checked = checked;
	if (IsWindow())
		Invalidate(FALSE);
	return TRUE;
}

bool CInfoBarCtrl::GetChecked(UINT id) const {
	int i = Find(id);
	return i >= 0 && m_Data->items[i].m.checked;
}

void CInfoBarCtrl::SetMaxRows(int rows) {
	m_Data->maxRows = std::max(0, rows);
	Changed();
}

// ---- Layout and animation -----------------------------------------------------------------------------

void CInfoBarCtrl::Changed() {
	m_Data->dirty = true;
	if (!IsWindow())
		return;
	if (Animating())
		SetTimer(AnimationTimer, 15);
	Invalidate(FALSE);
	NotifyHeight();
}

bool CInfoBarCtrl::AnimationsOn() const {
	DWORD style = GetStyle();
	if (style & NTFS_NOANIMATION)
		return false;
	if (style & NTFS_ALWAYSANIMATE)
		return true;
	BOOL on = TRUE;
	::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0);
	return on != FALSE;
}

bool CInfoBarCtrl::Animating() const {
	return std::any_of(m_Data->items.begin(), m_Data->items.end(), [](auto& item) { return item.appearing || item.going; });
}

void CInfoBarCtrl::LayoutItems() {
	auto& d = *m_Data;
	RECT rc{};
	if (IsWindow())
		GetClientRect(&rc);
	d.width = rc.right;
	WTL::CClientDC dc(m_hWnd);
	auto measure = Measurer(dc, d.fonts);
	for (auto& item : d.items)
		item.layout = NtfLayoutMessage(item.m, NtfStyle::Bar, std::max(1, (int)rc.right), d.m, measure,
			String(item.m.expanded ? NTFSTR_HIDEDETAILS : NTFSTR_SHOWDETAILS));
	d.dirty = false;
}

void CInfoBarCtrl::EnsureLayout() {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	if (d.dirty || d.width != rc.right)
		LayoutItems();

	// the stack as it is now: rows sliding are as tall as they show
	const ULONGLONG now = ::GetTickCount64();
	std::vector<int> heights;
	d.rowItems.clear();
	for (int i = 0; i < (int)d.items.size(); i++) {
		auto& item = d.items[i];
		int h = (int)std::lround(item.layout.height * item.Shown(now, NtfSlideDuration));
		d.rowItems.push_back(i);
		heights.push_back(h);
	}
	const int visible = GetMessageCount();
	auto stack = NtfLayoutStack(heights, d.showAll ? 0 : d.maxRows, rc.right, d.m.more);
	d.rowTops = stack.tops;
	d.rowHeights.assign(heights.begin(), heights.begin() + stack.tops.size());
	d.rowItems.resize(stack.tops.size());
	d.hidden = stack.hidden;
	d.strip = stack.more;
	d.height = stack.height;
	if (d.showAll && d.maxRows > 0 && visible > d.maxRows) {
		// all shown: the strip folds them again
		d.height += 1;
		d.strip = { 0, d.height, rc.right, d.height + d.m.more };
		d.height += d.m.more;
	}
	else if (d.showAll && visible <= d.maxRows)
		d.showAll = false;
	if (d.height > 0)
		d.height += 1;   // the line under the bar
}

int CInfoBarCtrl::GetIdealHeight() const {
	const_cast<CInfoBarCtrl*>(this)->EnsureLayout();
	return m_Data->height;
}

void CInfoBarCtrl::NotifyHeight() {
	if (!IsWindow())
		return;
	int height = GetIdealHeight();
	if (height == m_LastHeight)
		return;
	m_LastHeight = height;
	NMHDR nm{};
	FireNotify(&nm, NTFN_HEIGHTCHANGED);
}

void CInfoBarCtrl::Animate() {
	auto& d = *m_Data;
	const ULONGLONG now = ::GetTickCount64();
	bool removed = false;
	for (size_t i = 0; i < d.items.size();) {
		auto& item = d.items[i];
		if (item.going && now >= item.animStart + NtfSlideDuration) {
			d.items.erase(d.items.begin() + i);
			removed = true;
			continue;
		}
		if (item.appearing && now >= item.animStart + NtfSlideDuration)
			item.appearing = false;
		i++;
	}
	if (removed)
		d.hot = d.pressed = {};
	if (!Animating())
		KillTimer(AnimationTimer);
	Invalidate(FALSE);
	NotifyHeight();
}

LRESULT CInfoBarCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != AnimationTimer) {
		bHandled = FALSE;
		return 0;
	}
	Animate();
	return 0;
}

// ---- Painting ---------------------------------------------------------------------------------

LRESULT CInfoBarCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	auto paint = [&](WTL::CDCHandle dc) {
		EnsureLayout();
		auto c = GetColors();
		RECT rc;
		GetClientRect(&rc);
		FillSolid(dc, rc, ::GetSysColor(COLOR_WINDOW));
		auto oldFont = dc.SelectFont(d.fonts.normal);
		for (size_t r = 0; r < d.rowItems.size(); r++) {
			auto& item = d.items[d.rowItems[r]];
			const int top = d.rowTops[r], h = d.rowHeights[r];
			RECT row{ 0, top, rc.right, top + h };
			const COLORREF back = c.crBack[(int)item.m.severity];
			FillSolid(dc, row, back);
			// a row sliding shows its bottom part
			const int saved = dc.SaveDC();
			dc.IntersectClipRect(&row);
			PaintMessage(dc, item, d.rowItems[r], 0, top + h - item.layout.height, back, c, d.fonts, m_Images, d.hot, d.pressed, d.m, m_Dpi);
			dc.RestoreDC(saved);
			if (r + 1 < d.rowItems.size() || !::IsRectEmpty(&d.strip))
				FillSolid(dc, { 0, row.bottom, rc.right, row.bottom + 1 }, c.crBorder);
		}
		if (!::IsRectEmpty(&d.strip)) {
			FillSolid(dc, d.strip, c.crBack[0]);
			auto text = d.hidden > 0 ? String(NTFSTR_MORE, d.hidden) : String(NTFSTR_FEWER);
			const bool hot = d.hot.part == NtfPart::More;
			dc.SelectFont(d.fonts.link);
			dc.SetTextColor(hot ? Blend(c.crLink, c.crText, 35) : c.crLink);
			dc.SetBkMode(TRANSPARENT);
			RECT r = d.strip;
			dc.DrawText(text.c_str(), (int)text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		if (d.height > 0)
			FillSolid(dc, { 0, d.height - 1, rc.right, d.height }, c.crBorder);
		dc.SelectFont(oldFont);
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

// ---- Mouse --------------------------------------------------------------------------------------------

NtfHit CInfoBarCtrl::HitTest(POINT pt) {
	auto& d = *m_Data;
	EnsureLayout();
	if (::PtInRect(&d.strip, pt))
		return { -1, NtfPart::More };
	for (size_t r = 0; r < d.rowItems.size(); r++) {
		auto& item = d.items[d.rowItems[r]];
		const int top = d.rowTops[r], h = d.rowHeights[r];
		if (pt.y < top || pt.y >= top + h || item.going)
			continue;
		auto hit = HitMessage(item, { pt.x, pt.y - (top + h - item.layout.height) }, d.m);
		hit.item = d.rowItems[r];
		return hit;
	}
	return {};
}

void CInfoBarCtrl::SetHot(const NtfHit& hit) {
	auto& d = *m_Data;
	if (!(hit == d.hot)) {
		d.hot = hit;
		Invalidate(FALSE);
	}
}

LRESULT CInfoBarCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (!d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	auto hit = HitTest(PointOf(lParam));
	if (hit.part == NtfPart::None)
		hit = {};
	SetHot(hit);
	return 0;
}

LRESULT CInfoBarCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_Data->tracking = false;
	SetHot({});
	return 0;
}

LRESULT CInfoBarCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	// a hand over the links
	if ((HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT && (d.hot.part == NtfPart::Link || d.hot.part == NtfPart::More)) {
		::SetCursor(::LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CInfoBarCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	auto hit = HitTest(PointOf(lParam));
	if (hit.part == NtfPart::None)
		return 0;
	d.pressed = hit;
	SetHot(hit);
	SetCapture();
	Invalidate(FALSE);
	return 0;
}

LRESULT CInfoBarCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part == NtfPart::None)
		return 0;
	auto pressed = d.pressed;
	auto hit = HitTest(PointOf(lParam));
	d.pressed = {};
	if (GetCapture() == m_hWnd)
		ReleaseCapture();
	Invalidate(FALSE);
	if (hit == pressed)
		Click(hit);
	return 0;
}

LRESULT CInfoBarCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.pressed.part != NtfPart::None) {
		d.pressed = {};
		Invalidate(FALSE);
	}
	return 0;
}

// ---- Clicks -----------------------------------------------------------------------------------------

void CInfoBarCtrl::Click(const NtfHit& hit) {
	auto& d = *m_Data;
	if (hit.part == NtfPart::More) {
		d.showAll = !d.showAll;
		d.hot = {};
		Changed();
		return;
	}
	if (hit.item < 0 || hit.item >= (int)d.items.size())
		return;
	auto& item = d.items[hit.item];
	const UINT id = item.m.id;
	switch (hit.part) {
		case NtfPart::Close:
			Close(hit.item, NTFR_CLOSEBUTTON);
			break;
		case NtfPart::Check:
			item.m.checked = !item.m.checked;
			Invalidate(FALSE);
			break;
		case NtfPart::Button: {
			if (hit.index < 0 || hit.index >= (int)item.m.buttons.size())
				break;
			NMNTFBUTTON nm{};
			nm.id = id;
			nm.button = item.m.buttons[hit.index].id;
			nm.checked = item.m.checked;
			nm.lParam = item.lParam;
			// the app may keep the message; it may have removed it meanwhile
			if (!FireNotify(&nm.hdr, NTFN_BUTTONCLICK))
				if (int i = Find(id); i >= 0)
					Close(i, NTFR_BUTTON);
			break;
		}
		case NtfPart::Link: {
			if (hit.index < 0 || hit.index >= (int)item.layout.textRuns.size())
				break;
			auto link = item.layout.textRuns[hit.index].link;
			if (link == NtfDetailsLink) {
				item.m.expanded = !item.m.expanded;
				Changed();
				break;
			}
			NMNTFLINK nm{};
			nm.id = id;
			nm.pszLink = link.c_str();
			nm.lParam = item.lParam;
			FireNotify(&nm.hdr, NTFN_LINKCLICK);
			break;
		}
		default:
			break;
	}
}

void CInfoBarCtrl::Close(int index, UINT reason) {
	auto& d = *m_Data;
	auto& item = d.items[index];
	const UINT id = item.m.id;
	NMNTFCLOSED nm{};
	nm.id = id;
	nm.reason = reason;
	nm.checked = item.m.checked;
	nm.lParam = item.lParam;
	FireNotify(&nm.hdr, NTFN_CLOSED);
	RemoveMessage(id);   // unless the app did
}
