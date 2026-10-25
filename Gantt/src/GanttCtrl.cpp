#include "GanttData.h"

#include <commctrl.h>

#include <algorithm>
#include <climits>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

using namespace GntDraw;

static_assert(GNTL_STARTFINISH == (int)GntLinkType::StartFinish);

namespace {
	POINT PointOf(LPARAM lParam) {
		return { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
	}

	GntTime Now() {
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return GntFromSystemTime(st);
	}

	constexpr double MinZoom = 0.25, MaxZoom = 30000;   // minutes a pixel: 4 pixels a minute to a year in 18 pixels
}

// ---- Construction ------------------------------------------------------------------

CGanttCtrl::CGanttCtrl() : m_Data(std::make_unique<GntData>()) {
	auto& d = *m_Data;
	d.columns = { { GNTCOL_NAME, 170 }, { GNTCOL_START, 80 }, { GNTCOL_FINISH, 80 }, { GNTCOL_DURATION, 60 } };
	d.scale.minutesPerPixel = 45;
	d.scale.origin = GntTimeOf(GntDayOf(Now()) - 3);
}

CGanttCtrl::~CGanttCtrl() {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	if (m_hBold)
		::DeleteObject(m_hBold);
}

LRESULT CGanttCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_VSCROLL | WS_HSCROLL);
	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	UpdateFonts();
	return 0;
}

LRESULT CGanttCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;   // painted through a memory DC
}

LRESULT CGanttCtrl::OnSize(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateLayout();
	return 0;
}

LRESULT CGanttCtrl::FireNotify(NMHDR* pnm, UINT code) const {
	pnm->hwndFrom = m_hWnd;
	pnm->idFrom = ::GetDlgCtrlID(m_hWnd);
	pnm->code = code;
	return ::SendMessage(GetParent(), WM_NOTIFY, pnm->idFrom, (LPARAM)pnm);
}

LRESULT CGanttCtrl::NotifyTask(UINT code, UINT id) const {
	NMGANTTTASK nm{};
	nm.id = id;
	if (auto t = m_Data->project.Find(id))
		nm.lParam = t->lParam;
	return FireNotify(&nm.hdr, code);
}

// ---- Fonts and metrics ----------------------------------------------------------------

int CGanttCtrl::Scale(int dips) const {
	return ::MulDiv(dips, m_Dpi, 96);
}

void CGanttCtrl::UpdateFonts() {
	auto& d = *m_Data;
	if (m_OwnFont) {
		if (m_hFont)
			::DeleteObject(m_hFont);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_Dpi);
		m_hFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
	}
	if (m_hBold)
		::DeleteObject(m_hBold);
	LOGFONTW lf{};
	::GetObjectW(m_hFont, sizeof(lf), &lf);
	lf.lfWeight = FW_SEMIBOLD;
	m_hBold = ::CreateFontIndirectW(&lf);
	int text = 16;
	if (IsWindow()) {
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(m_hFont);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		dc.SelectFont(old);
		text = tm.tmHeight;
	}
	auto m = GntMetrics{}.Scaled(m_Dpi);
	m.rowHeight = std::max(m.rowHeight, text + Scale(8));
	d.m = m;
	d.tier = text + Scale(8);
	d.indent = Scale(16);
	Changed();
}

LRESULT CGanttCtrl::OnSetFont(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	if (m_OwnFont && m_hFont)
		::DeleteObject(m_hFont);
	m_hFont = (HFONT)wParam;
	m_OwnFont = m_hFont == nullptr;   // nullptr: back to the default font
	UpdateFonts();
	if (lParam)
		Invalidate();
	return 0;
}

LRESULT CGanttCtrl::OnGetFont(UINT, WPARAM, LPARAM, BOOL&) {
	return (LRESULT)m_hFont;
}

LRESULT CGanttCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	UINT dpi = ::GetDpiForWindow(m_hWnd);
	if (dpi != m_Dpi) {
		// the same days in view: the zoom is in DIPs
		m_Data->scale.minutesPerPixel *= (double)m_Dpi / dpi;
		m_Dpi = dpi;
		UpdateFonts();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CGanttCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETNONCLIENTMETRICS)   // the message font
		UpdateFonts();
	bHandled = FALSE;
	return 0;
}

LRESULT CGanttCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	Invalidate();
	bHandled = FALSE;
	return 0;
}

void CGanttCtrl::SetColors(const GNTCOLORS* colors) {
	m_SystemColors = colors == nullptr;
	if (colors)
		m_Colors = *colors;
	if (IsWindow())
		Invalidate();
}

// ---- Layout and scrolling ---------------------------------------------------------------------------

void CGanttCtrl::Changed() {
	auto& d = *m_Data;
	d.rows = d.project.Rows();
	if (d.selected && !d.project.Find(d.selected))
		d.selected = 0;
	UpdateLayout();
}

void CGanttCtrl::UpdateLayout() {
	auto& d = *m_Data;
	if (!IsWindow())
		return;
	RECT rc;
	GetClientRect(&rc);
	const int header = 2 * d.tier, split = Scale(5);
	const int gridWidth = std::clamp(Scale(d.gridDips), 0, std::max(0, (int)rc.right - split - Scale(40)));
	d.gridHeader = { 0, 0, gridWidth, header };
	d.grid = { 0, header, gridWidth, std::max(header, (int)rc.bottom) };
	d.splitter = { gridWidth, 0, gridWidth + split, rc.bottom };
	d.chartHeader = { gridWidth + split, 0, std::max(gridWidth + split, (int)rc.right), header };
	d.chart = { d.chartHeader.left, header, d.chartHeader.right, d.grid.bottom };
	GntPickUnits(d.scale.minutesPerPixel, Scale(26), d.lower, d.upper);

	// the time the scroll bar covers: the tasks', and a margin, and what's in view
	GntTime lo = INT64_MAX, hi = INT64_MIN;
	for (auto& t : d.project.Tasks()) {
		lo = std::min(lo, t.start);
		hi = std::max(hi, t.finish);
	}
	if (lo > hi)
		lo = hi = Now();
	const GntTime margin = std::max<GntTime>(14 * GntDay, (GntTime)((d.chart.right - d.chart.left) * d.scale.minutesPerPixel / 2));
	d.rangeStart = std::min(lo - margin, d.scale.origin);
	d.rangeEnd = std::max(hi + margin, d.scale.origin + (GntTime)((d.chart.right - d.chart.left) * d.scale.minutesPerPixel));
	ScrollTo(d.scrollY, d.scale.origin);
}

void CGanttCtrl::UpdateScrollBars() {
	auto& d = *m_Data;
	SCROLLINFO si{ sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL };
	si.nMax = std::max(0, (int)d.rows.size() * d.m.rowHeight - 1);
	si.nPage = std::max(0, (int)(d.chart.bottom - d.chart.top));
	si.nPos = d.scrollY;
	SetScrollInfo(SB_VERT, &si);
	si.nMax = (int)std::min<double>(INT_MAX / 2, (d.rangeEnd - d.rangeStart) / d.scale.minutesPerPixel);
	si.nPage = std::max(0, (int)(d.chart.right - d.chart.left));
	si.nPos = (int)std::lround((d.scale.origin - d.rangeStart) / d.scale.minutesPerPixel);
	SetScrollInfo(SB_HORZ, &si);
}

void CGanttCtrl::ScrollTo(int y, LONGLONG origin) {
	auto& d = *m_Data;
	const int height = (int)(d.chart.bottom - d.chart.top);
	d.scrollY = std::clamp(y, 0, std::max(0, (int)d.rows.size() * d.m.rowHeight - height));
	const GntTime span = (GntTime)((d.chart.right - d.chart.left) * d.scale.minutesPerPixel);
	d.scale.origin = std::clamp<GntTime>(origin, d.rangeStart, std::max(d.rangeStart, d.rangeEnd - span));
	UpdateScrollBars();
	Invalidate(FALSE);
}

LRESULT CGanttCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	SCROLLINFO si{ sizeof(si), SIF_ALL };
	GetScrollInfo(SB_VERT, &si);
	int y = d.scrollY;
	switch (LOWORD(wParam)) {
		case SB_LINEUP: y -= d.m.rowHeight; break;
		case SB_LINEDOWN: y += d.m.rowHeight; break;
		case SB_PAGEUP: y -= (int)si.nPage; break;
		case SB_PAGEDOWN: y += (int)si.nPage; break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
		case SB_TOP: y = 0; break;
		case SB_BOTTOM: y = INT_MAX / 2; break;
	}
	ScrollTo(y, d.scale.origin);
	return 0;
}

LRESULT CGanttCtrl::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& d = *m_Data;
	SCROLLINFO si{ sizeof(si), SIF_ALL };
	GetScrollInfo(SB_HORZ, &si);
	const double mpp = d.scale.minutesPerPixel;
	GntTime origin = d.scale.origin;
	switch (LOWORD(wParam)) {
		case SB_LINELEFT: origin -= (GntTime)(Scale(40) * mpp); break;
		case SB_LINERIGHT: origin += (GntTime)(Scale(40) * mpp); break;
		case SB_PAGELEFT: origin -= (GntTime)(si.nPage * mpp); break;
		case SB_PAGERIGHT: origin += (GntTime)(si.nPage * mpp); break;
		case SB_THUMBTRACK: case SB_THUMBPOSITION: origin = d.rangeStart + (GntTime)(si.nTrackPos * mpp); break;
		case SB_LEFT: origin = d.rangeStart; break;
		case SB_RIGHT: origin = d.rangeEnd; break;
	}
	ScrollTo(d.scrollY, origin);
	return 0;
}

LRESULT CGanttCtrl::OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	const UINT keys = GET_KEYSTATE_WPARAM(wParam);
	POINT pt = PointOf(lParam);
	ScreenToClient(&pt);
	if (uMsg == WM_MOUSEWHEEL && (keys & MK_CONTROL)) {
		// zoom around the mouse (its time stays under it)
		const int x = ::PtInRect(&d.chart, pt) ? pt.x : (d.chart.left + d.chart.right) / 2;
		ZoomAt(d.scale.minutesPerPixel * std::pow(1.25, -delta / (double)WHEEL_DELTA), x);
		return 0;
	}
	if (uMsg == WM_MOUSEHWHEEL || (keys & MK_SHIFT)) {
		const int dx = (uMsg == WM_MOUSEHWHEEL ? delta : -delta) * Scale(60) / WHEEL_DELTA;
		ScrollTo(d.scrollY, d.scale.origin + (GntTime)(dx * d.scale.minutesPerPixel));
		return 0;
	}
	ScrollTo(d.scrollY - delta * 3 * d.m.rowHeight / WHEEL_DELTA, d.scale.origin);
	return 0;
}

void CGanttCtrl::ZoomAt(double minutesPerPixel, int x) {
	auto& d = *m_Data;
	minutesPerPixel = std::clamp(minutesPerPixel, MinZoom, MaxZoom);
	const GntTime at = d.scale.T(x - d.chart.left);
	d.scale.minutesPerPixel = minutesPerPixel;
	d.scale.origin = at - (GntTime)((x - d.chart.left) * minutesPerPixel);
	UpdateLayout();
}

void CGanttCtrl::SetZoom(double minutesPerPixel) {
	auto& d = *m_Data;
	ZoomAt(minutesPerPixel, (d.chart.left + d.chart.right) / 2);
}

double CGanttCtrl::GetZoom() const {
	return m_Data->scale.minutesPerPixel;
}

void CGanttCtrl::ScrollToTime(const SYSTEMTIME& t) {
	auto& d = *m_Data;
	d.scale.origin = GntFromSystemTime(t);
	UpdateLayout();
}

void CGanttCtrl::SetColumns(const UINT* columns, const int* widthsDips, int count) {
	auto& d = *m_Data;
	d.columns.clear();
	for (int i = 0; i < count; i++)
		d.columns.push_back({ std::min(columns[i], (UINT)GNTCOL_PROGRESS), widthsDips ? widthsDips[i] : 80 });
	Invalidate(FALSE);
}

void CGanttCtrl::SetGridWidth(int dips) {
	m_Data->gridDips = std::max(0, dips);
	UpdateLayout();
}

// ---- Tasks -----------------------------------------------------------------------------------------

BOOL CGanttCtrl::AddTask(const GNTTASK* task, UINT after) {
	if (!task)
		return FALSE;
	GntTask t;
	t.id = task->id;
	t.parent = task->parent;
	t.name = task->pszName ? task->pszName : L"";
	t.start = GntFromSystemTime(task->start);
	t.duration = std::max<LONGLONG>(0, task->duration);
	t.progress = std::clamp(task->progress, 0, 100);
	t.color = task->color;
	t.expanded = !(task->flags & GNTF_COLLAPSED);
	t.lParam = task->lParam;
	if (!m_Data->project.Add(t, after))
		return FALSE;
	Changed();
	return TRUE;
}

BOOL CGanttCtrl::AddTask(UINT id, LPCWSTR name, const SYSTEMTIME& start, LONGLONG duration, UINT parent) {
	GNTTASK t;
	t.id = id;
	t.pszName = name;
	t.start = start;
	t.duration = duration;
	t.parent = parent;
	return AddTask(&t);
}

BOOL CGanttCtrl::SetTask(const GNTTASK* task) {
	auto& d = *m_Data;
	auto t = task ? d.project.Find(task->id) : nullptr;
	if (!t)
		return FALSE;
	if (task->mask & GNTTF_NAME)
		t->name = task->pszName ? task->pszName : L"";
	if (task->mask & GNTTF_START)
		t->start = GntFromSystemTime(task->start);
	if (task->mask & GNTTF_DURATION)
		t->duration = std::max<LONGLONG>(0, task->duration);
	if (task->mask & GNTTF_PROGRESS)
		t->progress = std::clamp(task->progress, 0, 100);
	if (task->mask & GNTTF_COLOR)
		t->color = task->color;
	if (task->mask & GNTTF_PARAM)
		t->lParam = task->lParam;
	d.project.Recompute();
	Changed();
	return TRUE;
}

BOOL CGanttCtrl::GetTask(UINT id, GNTTASK* task) const {
	auto& d = *m_Data;
	auto t = d.project.Find(id);
	if (!t || !task)
		return FALSE;
	d.nameBuffer = t->name;
	task->id = t->id;
	task->parent = t->parent;
	task->pszName = d.nameBuffer.c_str();
	task->start = GntToSystemTime(t->start);
	task->finish = GntToSystemTime(t->finish);
	task->duration = t->duration;
	task->progress = t->progress;
	task->color = t->color;
	task->flags = t->expanded ? 0 : GNTF_COLLAPSED;
	task->lParam = t->lParam;
	task->summary = t->summary;
	return TRUE;
}

BOOL CGanttCtrl::RemoveTask(UINT id) {
	if (!m_Data->project.Remove(id))
		return FALSE;
	Changed();
	return TRUE;
}

void CGanttCtrl::RemoveAll() {
	m_Data->project.Restore({});
	m_Data->selected = 0;
	Changed();
}

int CGanttCtrl::GetTaskCount() const {
	return (int)m_Data->project.Tasks().size();
}

BOOL CGanttCtrl::Expand(UINT id, bool expand) {
	auto t = m_Data->project.Find(id);
	if (!t || !t->summary)
		return FALSE;
	t->expanded = expand;
	Changed();
	return TRUE;
}

BOOL CGanttCtrl::AddLink(UINT from, UINT to, UINT type, LONGLONG lag) {
	if (!m_Data->project.AddLink({ from, to, (GntLinkType)std::min(type, (UINT)GNTL_STARTFINISH), lag }))
		return FALSE;
	Invalidate(FALSE);
	return TRUE;
}

BOOL CGanttCtrl::RemoveLink(UINT from, UINT to) {
	if (!m_Data->project.RemoveLink(from, to))
		return FALSE;
	Invalidate(FALSE);
	return TRUE;
}

void CGanttCtrl::Reschedule() {
	m_Data->project.Schedule();
	Changed();
}

// ---- The calendar ----------------------------------------------------------------------------------

void CGanttCtrl::SetWorkDays(BYTE days) {
	m_Data->project.calendar.workDays = days;
	m_Data->project.Recompute();
	Changed();
}

void CGanttCtrl::SetWorkHours(int startMinute, int endMinute) {
	auto& c = m_Data->project.calendar;
	c.dayStart = std::clamp(startMinute, 0, 1439);
	c.dayEnd = std::clamp(endMinute, c.dayStart + 1, 1440);
	m_Data->project.Recompute();
	Changed();
}

BOOL CGanttCtrl::AddHoliday(const SYSTEMTIME& day) {
	if (!m_Data->project.calendar.AddHoliday(GntDayOf(GntFromSystemTime(day))))
		return FALSE;
	m_Data->project.Recompute();
	Changed();
	return TRUE;
}

BOOL CGanttCtrl::RemoveHoliday(const SYSTEMTIME& day) {
	if (!m_Data->project.calendar.RemoveHoliday(GntDayOf(GntFromSystemTime(day))))
		return FALSE;
	m_Data->project.Recompute();
	Changed();
	return TRUE;
}

int CGanttCtrl::GetMinutesPerDay() const {
	return m_Data->project.calendar.MinutesPerDay();
}

// ---- Selection -----------------------------------------------------------------------------------

BOOL CGanttCtrl::Select(UINT id) {
	if (id && !m_Data->project.Find(id))
		return FALSE;
	SetSelected(id, false);
	return TRUE;
}

UINT CGanttCtrl::GetSelected() const {
	return m_Data->selected;
}

void CGanttCtrl::SetSelected(UINT id, bool notify) {
	auto& d = *m_Data;
	if (d.selected == id)
		return;
	d.selected = id;
	Invalidate(FALSE);
	if (notify)
		NotifyTask(GNTN_SELCHANGED, id);
}

void CGanttCtrl::EnsureVisible(UINT id) {
	auto& d = *m_Data;
	int task = d.project.Index(id);
	if (task < 0)
		return;
	// its ancestors expanded
	bool expanded = false;
	for (auto t = d.project.Find(id); t && t->parent; t = d.project.Find(t->parent))
		if (auto p = d.project.Find(t->parent); p && !p->expanded) {
			p->expanded = true;
			expanded = true;
		}
	if (expanded)
		Changed();
	auto it = std::find(d.rows.begin(), d.rows.end(), task);
	if (it == d.rows.end())
		return;
	const int row = (int)(it - d.rows.begin()), height = (int)(d.chart.bottom - d.chart.top);
	int y = d.scrollY;
	if (row * d.m.rowHeight < y)
		y = row * d.m.rowHeight;
	else if ((row + 1) * d.m.rowHeight > y + height)
		y = (row + 1) * d.m.rowHeight - height;
	// its bar in view, a little after the chart's left
	auto& t = d.project.Tasks()[task];
	GntTime origin = d.scale.origin;
	const GntTime span = (GntTime)((d.chart.right - d.chart.left) * d.scale.minutesPerPixel);
	if (t.start < origin || t.start > origin + span * 3 / 4)
		origin = t.start - (GntTime)(Scale(40) * d.scale.minutesPerPixel);
	ScrollTo(y, origin);
}

// ---- Mouse --------------------------------------------------------------------------------------------

GntHitInfo CGanttCtrl::HitTest(POINT pt) {
	auto& d = *m_Data;
	GntHitInfo hit;
	if (::PtInRect(&d.splitter, pt)) {
		hit.area = GntArea::Splitter;
		return hit;
	}
	if (::PtInRect(&d.gridHeader, pt)) {
		hit.area = GntArea::GridHeader;
		return hit;
	}
	if (::PtInRect(&d.chartHeader, pt)) {
		hit.area = GntArea::ChartHeader;
		return hit;
	}
	const bool inGrid = ::PtInRect(&d.grid, pt) != FALSE, inChart = ::PtInRect(&d.chart, pt) != FALSE;
	if (!inGrid && !inChart)
		return hit;
	const int y = pt.y - d.grid.top + d.scrollY;
	const int row = y / d.m.rowHeight;
	if (row < 0 || row >= (int)d.rows.size())
		return hit;
	hit.row = row;
	hit.task = d.rows[row];
	auto& t = d.project.Tasks()[hit.task];
	if (inGrid) {
		hit.area = GntArea::Grid;
		// the expander, in the name column, before the name
		int x = d.grid.left;
		for (auto& col : d.columns) {
			const int w = Scale(col.dips);
			if (col.id == GNTCOL_NAME && t.summary) {
				const int ex = x + Scale(2) + t.depth * d.indent;
				if (pt.x >= ex && pt.x < ex + d.indent)
					hit.area = GntArea::Expander;
				break;
			}
			x += w;
		}
		return hit;
	}
	hit.area = GntArea::Chart;
	auto bar = GntLayoutBar(t, row, d.scale, d.m);
	hit.part = GntHitBar(bar, { pt.x - d.chart.left, y }, d.m);
	return hit;
}

LRESULT CGanttCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	POINT pt = PointOf(lParam);
	if (d.splitting) {
		RECT rc;
		GetClientRect(&rc);
		const int x = std::clamp((int)pt.x - d.splitGrab, 0, std::max(0, (int)rc.right - Scale(60)));
		d.gridDips = ::MulDiv(x, 96, m_Dpi);
		UpdateLayout();
		return 0;
	}
	if (!d.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		d.tracking = ::TrackMouseEvent(&tme) != FALSE;
	}
	auto hit = HitTest(pt);
	if (hit.area != d.hot.area || hit.task != d.hot.task || hit.part != d.hot.part) {
		d.hot = hit;
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CGanttCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	d.tracking = false;
	d.hot = {};
	Invalidate(FALSE);
	return 0;
}

LRESULT CGanttCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	if ((HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT && (d.splitting || d.hot.area == GntArea::Splitter)) {
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
		return TRUE;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CGanttCtrl::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& d = *m_Data;
	POINT pt = PointOf(lParam);
	auto hit = HitTest(pt);
	switch (hit.area) {
		case GntArea::Splitter:
			d.splitting = true;
			d.splitGrab = pt.x - d.splitter.left;
			SetCapture();
			break;
		case GntArea::Expander: {
			const UINT id = d.project.Tasks()[hit.task].id;
			auto t = d.project.Find(id);
			t->expanded = !t->expanded;
			Changed();
			NotifyTask(GNTN_EXPANDED, id);
			break;
		}
		case GntArea::Grid:
		case GntArea::Chart:
			SetSelected(d.project.Tasks()[hit.task].id, true);
			break;
		default:
			break;
	}
	return 0;
}

LRESULT CGanttCtrl::OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	auto& d = *m_Data;
	auto hit = HitTest(PointOf(lParam));
	if (hit.area == GntArea::Grid || hit.area == GntArea::Chart)
		NotifyTask(GNTN_DBLCLK, d.project.Tasks()[hit.task].id);
	else
		OnLButtonDown(uMsg, wParam, lParam, bHandled);   // the expander toggles again
	return 0;
}

LRESULT CGanttCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	auto& d = *m_Data;
	if (d.splitting) {
		d.splitting = false;
		if (GetCapture() == m_hWnd)
			ReleaseCapture();
	}
	return 0;
}

LRESULT CGanttCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	m_Data->splitting = false;
	return 0;
}
