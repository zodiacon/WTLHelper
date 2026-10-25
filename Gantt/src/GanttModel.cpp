#include "GanttModel.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <unordered_map>

// ---- Time ------------------------------------------------------------------------------------------

namespace {
	constexpr int64_t From1601To1970 = 134774;   // days

	// Howard Hinnant's algorithms, with days counted from 1970-01-01
	int64_t DaysFromCivil(int y, int m, int d) {
		y -= m <= 2;
		const int64_t era = (y >= 0 ? y : y - 399) / 400;
		const int yoe = (int)(y - era * 400);
		const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + doe - 719468;
	}

	GntDate CivilFromDays(int64_t z) {
		z += 719468;
		const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		const int doe = (int)(z - era * 146097);
		const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		const int mp = (5 * doy + 2) / 153;
		GntDate date;
		date.day = doy - (153 * mp + 2) / 5 + 1;
		date.month = mp < 10 ? mp + 3 : mp - 9;
		date.year = (int)(yoe + era * 400) + (date.month <= 2);
		return date;
	}

	int64_t FloorDiv(int64_t a, int64_t b) {
		return a >= 0 ? a / b : -((-a + b - 1) / b);
	}
}

int64_t GntDayOf(GntTime t) {
	return FloorDiv(t, GntDay);
}

GntTime GntTimeOf(int64_t day, int minute) {
	return day * GntDay + minute;
}

GntDate GntDateOf(int64_t day) {
	return CivilFromDays(day - From1601To1970);
}

int64_t GntDayFromDate(int year, int month, int day) {
	return DaysFromCivil(year, month, day) + From1601To1970;
}

int GntWeekday(int64_t day) {
	return (int)(((day % 7) + 7 + 1) % 7);   // day 0, 1601-01-01, was a Monday
}

GntTime GntFromSystemTime(const SYSTEMTIME& st) {
	return GntTimeOf(GntDayFromDate(st.wYear, st.wMonth, st.wDay), st.wHour * 60 + st.wMinute);
}

SYSTEMTIME GntToSystemTime(GntTime t) {
	const int64_t day = GntDayOf(t);
	const int minute = (int)(t - day * GntDay);
	auto date = GntDateOf(day);
	SYSTEMTIME st{};
	st.wYear = (WORD)date.year;
	st.wMonth = (WORD)date.month;
	st.wDay = (WORD)date.day;
	st.wDayOfWeek = (WORD)GntWeekday(day);
	st.wHour = (WORD)(minute / 60);
	st.wMinute = (WORD)(minute % 60);
	return st;
}

// ---- The working calendar --------------------------------------------------------------------------

namespace {
	constexpr int SearchDays = 3700;   // a working day is looked for this far (no working days: time is all work)
}

void GntCalendar::SetHolidays(std::vector<int64_t> days) {
	std::sort(days.begin(), days.end());
	days.erase(std::unique(days.begin(), days.end()), days.end());
	m_Holidays = std::move(days);
}

bool GntCalendar::AddHoliday(int64_t day) {
	auto it = std::lower_bound(m_Holidays.begin(), m_Holidays.end(), day);
	if (it != m_Holidays.end() && *it == day)
		return false;
	m_Holidays.insert(it, day);
	return true;
}

bool GntCalendar::RemoveHoliday(int64_t day) {
	auto it = std::lower_bound(m_Holidays.begin(), m_Holidays.end(), day);
	if (it == m_Holidays.end() || *it != day)
		return false;
	m_Holidays.erase(it);
	return true;
}

bool GntCalendar::IsWorkingDay(int64_t day) const {
	return (workDays & (1 << GntWeekday(day))) && !std::binary_search(m_Holidays.begin(), m_Holidays.end(), day);
}

GntTime GntCalendar::NextWorkStart(GntTime t) const {
	if (workDays == 0 || dayEnd <= dayStart)
		return t;
	int64_t day = GntDayOf(t);
	const int minute = (int)(t - day * GntDay);
	if (IsWorkingDay(day) && minute < dayEnd)
		return day * GntDay + std::max(minute, dayStart);
	for (int k = 0; k < SearchDays; k++)
		if (IsWorkingDay(++day))
			return day * GntDay + dayStart;
	return t;
}

GntTime GntCalendar::PrevWorkEnd(GntTime t) const {
	if (workDays == 0 || dayEnd <= dayStart)
		return t;
	int64_t day = GntDayOf(t);
	const int minute = (int)(t - day * GntDay);
	if (IsWorkingDay(day) && minute > dayStart)
		return day * GntDay + std::min(minute, dayEnd);
	for (int k = 0; k < SearchDays; k++)
		if (IsWorkingDay(--day))
			return day * GntDay + dayEnd;
	return t;
}

GntTime GntCalendar::AddWork(GntTime start, int64_t minutes) const {
	if (minutes == 0)
		return start;
	if (workDays == 0 || dayEnd <= dayStart)
		return start + minutes;
	if (minutes > 0) {
		GntTime t = NextWorkStart(start);
		int64_t left = minutes;
		for (int k = 0; k < 1000000; k++) {
			const GntTime end = GntDayOf(t) * GntDay + dayEnd;
			if (left <= end - t)
				return t + left;
			left -= end - t;
			t = NextWorkStart(end);
		}
		return t;
	}
	GntTime t = PrevWorkEnd(start);
	int64_t left = -minutes;
	for (int k = 0; k < 1000000; k++) {
		const GntTime begin = GntDayOf(t - 1) * GntDay + dayStart;
		if (left <= t - begin)
			return t - left;
		left -= t - begin;
		t = PrevWorkEnd(begin);
	}
	return t;
}

int64_t GntCalendar::WorkBetween(GntTime from, GntTime to) const {
	if (to < from)
		return -WorkBetween(to, from);
	if (workDays == 0 || dayEnd <= dayStart)
		return to - from;
	int64_t work = 0;
	for (int64_t day = GntDayOf(from), last = GntDayOf(to); day <= last; day++) {
		if (!IsWorkingDay(day))
			continue;
		const GntTime lo = std::max(from, day * GntDay + dayStart), hi = std::min(to, day * GntDay + dayEnd);
		if (hi > lo)
			work += hi - lo;
	}
	return work;
}

// ---- The time scale ---------------------------------------------------------------------------------

namespace {
	// about how long each unit is, in minutes
	const double UnitLength[] = { 60, GntDay, 7.0 * GntDay, 30.44 * GntDay, 91.31 * GntDay, 365.25 * GntDay };

	GntTime MonthStart(int year, int month) {
		while (month > 12) {
			month -= 12;
			year++;
		}
		return GntTimeOf(GntDayFromDate(year, month, 1));
	}
}

void GntPickUnits(double minutesPerPixel, int minPixels, GntUnit& lower, GntUnit& upper) {
	lower = GntUnit::Year;
	for (int u = 0; u <= (int)GntUnit::Year; u++)
		if (UnitLength[u] / minutesPerPixel >= minPixels) {
			lower = (GntUnit)u;
			break;
		}
	upper = lower == GntUnit::Year ? GntUnit::Year : (GntUnit)((int)lower + 1);
}

GntTime GntUnitStart(GntUnit unit, GntTime t) {
	const int64_t day = GntDayOf(t);
	switch (unit) {
		case GntUnit::Hour: return FloorDiv(t, 60) * 60;
		case GntUnit::Day: return day * GntDay;
		case GntUnit::Week: return (day - (GntWeekday(day) + 6) % 7) * GntDay;   // Monday
		default: break;
	}
	auto date = GntDateOf(day);
	switch (unit) {
		case GntUnit::Month: return MonthStart(date.year, date.month);
		case GntUnit::Quarter: return MonthStart(date.year, (date.month - 1) / 3 * 3 + 1);
		default: return MonthStart(date.year, 1);
	}
}

GntTime GntUnitNext(GntUnit unit, GntTime start) {
	switch (unit) {
		case GntUnit::Hour: return start + 60;
		case GntUnit::Day: return start + GntDay;
		case GntUnit::Week: return start + 7 * GntDay;
		default: break;
	}
	auto date = GntDateOf(GntDayOf(start));
	switch (unit) {
		case GntUnit::Month: return MonthStart(date.year, date.month + 1);
		case GntUnit::Quarter: return MonthStart(date.year, date.month + 3);
		default: return MonthStart(date.year + 1, 1);
	}
}

std::vector<GntCell> GntCells(GntUnit unit, GntTime from, GntTime to) {
	std::vector<GntCell> cells;
	for (GntTime t = GntUnitStart(unit, from); t < to && cells.size() < 100000;) {
		GntTime next = GntUnitNext(unit, t);
		cells.push_back({ t, next });
		t = next;
	}
	return cells;
}

// ---- Tasks and links ----------------------------------------------------------------------------------

GntTask* GntProject::Find(UINT id) {
	int i = Index(id);
	return i < 0 ? nullptr : &m_Tasks[i];
}

const GntTask* GntProject::Find(UINT id) const {
	int i = Index(id);
	return i < 0 ? nullptr : &m_Tasks[i];
}

int GntProject::Index(UINT id) const {
	if (id == 0)
		return -1;
	for (int i = 0; i < (int)m_Tasks.size(); i++)
		if (m_Tasks[i].id == id)
			return i;
	return -1;
}

bool GntProject::IsAncestor(UINT ancestor, UINT id) const {
	for (auto t = Find(id); t && t->parent; t = Find(t->parent))
		if (t->parent == ancestor)
			return true;
	return false;
}

bool GntProject::Add(const GntTask& task, UINT after) {
	if (task.id == 0 || Find(task.id) || (task.parent && !Find(task.parent)))
		return false;
	// the end of a task's subtree
	auto end = [&](int i) {
		int j = i + 1;
		while (j < (int)m_Tasks.size() && IsAncestor(m_Tasks[i].id, m_Tasks[j].id))
			j++;
		return j;
	};
	int at;
	if (after) {
		int i = Index(after);
		if (i < 0 || m_Tasks[i].parent != task.parent)
			return false;
		at = end(i);
	}
	else
		at = task.parent ? end(Index(task.parent)) : (int)m_Tasks.size();
	m_Tasks.insert(m_Tasks.begin() + at, task);
	Recompute();
	return true;
}

bool GntProject::Remove(UINT id) {
	int i = Index(id);
	if (i < 0)
		return false;
	int j = i + 1;
	while (j < (int)m_Tasks.size() && IsAncestor(id, m_Tasks[j].id))
		j++;
	std::vector<UINT> gone;
	gone.reserve(j - i);
	for (int k = i; k < j; k++)
		gone.push_back(m_Tasks[k].id);
	m_Tasks.erase(m_Tasks.begin() + i, m_Tasks.begin() + j);
	std::erase_if(m_Links, [&](auto& l) {
		return std::find(gone.begin(), gone.end(), l.from) != gone.end() || std::find(gone.begin(), gone.end(), l.to) != gone.end();
	});
	Recompute();
	return true;
}

GntTime GntProject::Finish(const GntTask& t) const {
	return t.duration > 0 ? calendar.AddWork(t.start, t.duration) : t.start;
}

void GntProject::Recompute() {
	const int n = (int)m_Tasks.size();
	std::unordered_map<UINT, int> index;
	index.reserve(n);
	for (int i = 0; i < n; i++) {
		auto& t = m_Tasks[i];
		index[t.id] = i;
		t.summary = false;
		auto p = t.parent ? index.find(t.parent) : index.end();
		t.depth = p != index.end() ? m_Tasks[p->second].depth + 1 : 0;
		if (p != index.end())
			m_Tasks[p->second].summary = true;
	}
	// the leaves' finishes, then each summary from its children (children come after their parent: backwards)
	std::vector<GntTime> lo(n, INT64_MAX), hi(n, INT64_MIN);
	std::vector<double> work(n, 0), done(n, 0);
	for (int i = n - 1; i >= 0; i--) {
		auto& t = m_Tasks[i];
		if (t.summary && lo[i] <= hi[i]) {
			t.start = lo[i];
			t.finish = hi[i];
			t.duration = calendar.WorkBetween(t.start, t.finish);
			t.progress = work[i] > 0 ? (int)std::lround(done[i] / work[i]) : 0;
		}
		else if (!t.summary)
			t.finish = Finish(t);
		if (auto p = t.parent ? index.find(t.parent) : index.end(); p != index.end()) {
			const int k = p->second;
			lo[k] = std::min(lo[k], t.start);
			hi[k] = std::max(hi[k], t.finish);
			work[k] += (double)t.duration;
			done[k] += (double)t.duration * t.progress;
		}
	}
}

std::vector<int> GntProject::Rows() const {
	std::vector<int> rows;
	rows.reserve(m_Tasks.size());
	int hidden = INT_MAX;   // the tasks deeper than this are under a collapsed one
	for (int i = 0; i < (int)m_Tasks.size(); i++) {
		auto& t = m_Tasks[i];
		if (t.depth > hidden)
			continue;
		hidden = INT_MAX;
		rows.push_back(i);
		if (t.summary && !t.expanded)
			hidden = t.depth;
	}
	return rows;
}

bool GntProject::CanLink(UINT from, UINT to) const {
	auto f = Find(from), t = Find(to);
	if (!f || !t || from == to || f->summary || t->summary)
		return false;
	for (auto& l : m_Links)
		if ((l.from == from && l.to == to) || (l.from == to && l.to == from))
			return false;
	// a cycle: from is reached by following the links from to
	std::vector<UINT> stack{ to }, seen;
	while (!stack.empty()) {
		UINT c = stack.back();
		stack.pop_back();
		if (c == from)
			return false;
		if (std::find(seen.begin(), seen.end(), c) != seen.end())
			continue;
		seen.push_back(c);
		for (auto& l : m_Links)
			if (l.from == c)
				stack.push_back(l.to);
	}
	return true;
}

bool GntProject::AddLink(const GntLink& link) {
	if (!CanLink(link.from, link.to))
		return false;
	m_Links.push_back(link);
	return true;
}

bool GntProject::RemoveLink(UINT from, UINT to) {
	auto it = std::find_if(m_Links.begin(), m_Links.end(), [&](auto& l) { return l.from == from && l.to == to; });
	if (it == m_Links.end())
		return false;
	m_Links.erase(it);
	return true;
}

GntTime GntProject::EarliestStart(UINT id) const {
	auto t = Find(id);
	GntTime earliest = INT64_MIN;
	if (!t)
		return earliest;
	for (auto& l : m_Links) {
		if (l.to != id)
			continue;
		auto f = Find(l.from);
		if (!f)
			continue;
		GntTime e;
		switch (l.type) {
			case GntLinkType::FinishStart:
				e = calendar.NextWorkStart(calendar.AddWork(f->finish, l.lag));
				break;
			case GntLinkType::StartStart:
				e = calendar.NextWorkStart(calendar.AddWork(f->start, l.lag));
				break;
			case GntLinkType::FinishFinish:
				e = calendar.AddWork(calendar.AddWork(f->finish, l.lag), -t->duration);
				break;
			default:   // StartFinish
				e = calendar.AddWork(calendar.AddWork(f->start, l.lag), -t->duration);
				break;
		}
		earliest = std::max(earliest, e);
	}
	return earliest;
}

std::vector<UINT> GntProject::PushSuccessors(UINT id) {
	std::vector<UINT> moved, queue{ id };
	for (size_t q = 0; q < queue.size() && q < 100000; q++) {
		for (auto& l : m_Links) {
			if (l.from != queue[q])
				continue;
			auto t = Find(l.to);
			GntTime e = EarliestStart(l.to);
			if (!t || t->start >= e)
				continue;
			t->start = e;
			t->finish = Finish(*t);
			if (std::find(moved.begin(), moved.end(), l.to) == moved.end())
				moved.push_back(l.to);
			queue.push_back(l.to);
		}
	}
	if (!moved.empty())
		Recompute();
	return moved;
}

std::vector<UINT> GntProject::Schedule() {
	// the tasks in the links' order (Kahn's): each when all those before it are placed
	std::unordered_map<UINT, int> before;
	for (auto& t : m_Tasks)
		if (!t.summary)
			before[t.id] = 0;
	for (auto& l : m_Links)
		before[l.to]++;
	std::vector<UINT> ready, moved;
	for (auto& t : m_Tasks)
		if (!t.summary && before[t.id] == 0)
			ready.push_back(t.id);
	for (size_t q = 0; q < ready.size(); q++) {
		const UINT id = ready[q];
		auto t = Find(id);
		const GntTime e = EarliestStart(id);
		if (t && t->start < e) {
			t->start = e;
			t->finish = Finish(*t);
			moved.push_back(id);
		}
		for (auto& l : m_Links)
			if (l.from == id && --before[l.to] == 0)
				ready.push_back(l.to);
	}
	if (!moved.empty())
		Recompute();
	return moved;
}

GntProject::State GntProject::Save() const {
	return { m_Tasks, m_Links };
}

void GntProject::Restore(const State& state) {
	m_Tasks = state.tasks;
	m_Links = state.links;
	Recompute();
}

// ---- Bars --------------------------------------------------------------------------------------------

GntMetrics GntMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	GntMetrics m;
	m.rowHeight = s(rowHeight);
	m.barPad = s(barPad);
	m.summaryHeight = s(summaryHeight);
	m.milestone = s(milestone);
	m.handle = s(handle);
	m.link = s(link);
	m.linkGap = s(linkGap);
	m.minBar = std::max(2, s(minBar));
	return m;
}

GntBar GntLayoutBar(const GntTask& task, int row, const GntScale& scale, const GntMetrics& m) {
	GntBar bar;
	const int top = row * m.rowHeight;
	const int x1 = (int)std::lround(scale.X(task.start)), x2 = (int)std::lround(scale.X(task.finish));
	if (task.Milestone()) {
		bar.milestone = true;
		const int y = top + (m.rowHeight - m.milestone) / 2;
		bar.rc = { x1 - m.milestone / 2, y, x1 - m.milestone / 2 + m.milestone, y + m.milestone };
		return bar;
	}
	bar.summary = task.summary;
	const int height = task.summary ? m.summaryHeight : m.rowHeight - 2 * m.barPad;
	const int y = top + (m.rowHeight - height) / 2;
	bar.rc = { x1, y, std::max(x2, x1 + m.minBar), y + height };
	bar.done = bar.rc;
	bar.done.right = bar.rc.left + (int)std::lround((bar.rc.right - bar.rc.left) * std::clamp(task.progress, 0, 100) / 100.0);
	return bar;
}

RECT GntLinkHandle(const GntBar& bar, const GntMetrics& m) {
	const int cx = bar.rc.right + m.link / 2 + m.handle / 2, cy = (bar.rc.top + bar.rc.bottom) / 2;
	return { cx - m.link / 2, cy - m.link / 2, cx - m.link / 2 + m.link, cy - m.link / 2 + m.link };
}

GntPart GntHitBar(const GntBar& bar, POINT pt, const GntMetrics& m) {
	if (!bar.summary) {
		RECT link = GntLinkHandle(bar, m);
		if (::PtInRect(&link, pt))
			return GntPart::Link;
	}
	const RECT& r = bar.rc;
	if (pt.y < r.top || pt.y >= r.bottom)
		return GntPart::None;
	if (bar.summary || bar.milestone)
		return pt.x >= r.left && pt.x < r.right ? GntPart::Bar : GntPart::None;
	const int h = m.handle / 2;
	// the progress handle: the done part's end, in the bar's bottom third
	if (pt.y >= r.bottom - (r.bottom - r.top) / 3 && std::abs(pt.x - bar.done.right) <= h && pt.x >= r.left && pt.x < r.right)
		return GntPart::Progress;
	if (pt.x >= r.right - h && pt.x < r.right + h)
		return GntPart::Finish;
	if (pt.x >= r.left - h && pt.x < r.left + h)
		return GntPart::Start;
	return pt.x >= r.left && pt.x < r.right ? GntPart::Bar : GntPart::None;
}

std::vector<POINT> GntRouteLink(const GntBar& from, const GntBar& to, GntLinkType type, const GntMetrics& m) {
	const int g = m.linkGap;
	const int fy = (from.rc.top + from.rc.bottom) / 2, ty = (to.rc.top + to.rc.bottom) / 2;
	// between the target's row and the next one toward the source: for a link that must turn back
	const int by = ty > fy ? ty - m.rowHeight / 2 : ty + m.rowHeight / 2;
	switch (type) {
		case GntLinkType::StartStart: {
			const int sx = from.rc.left, ex = to.rc.left, x = std::min(sx, ex) - g;
			return { { sx, fy }, { x, fy }, { x, ty }, { ex, ty } };
		}
		case GntLinkType::FinishFinish: {
			const int sx = from.rc.right, ex = to.rc.right, x = std::max(sx, ex) + g;
			return { { sx, fy }, { x, fy }, { x, ty }, { ex, ty } };
		}
		case GntLinkType::StartFinish: {
			const int sx = from.rc.left, ex = to.rc.right;
			if (ex + 2 * g <= sx)
				return { { sx, fy }, { sx - g, fy }, { sx - g, ty }, { ex, ty } };
			return { { sx, fy }, { sx - g, fy }, { sx - g, by }, { ex + g, by }, { ex + g, ty }, { ex, ty } };
		}
		default: {   // FinishStart
			const int sx = from.rc.right, ex = to.rc.left;
			if (ex >= sx + 2 * g)
				return { { sx, fy }, { sx + g, fy }, { sx + g, ty }, { ex, ty } };
			return { { sx, fy }, { sx + g, fy }, { sx + g, by }, { ex - g, by }, { ex - g, ty }, { ex, ty } };
		}
	}
}

double GntLinkDistance(const std::vector<POINT>& route, POINT pt) {
	double best = 1e18;
	for (size_t i = 0; i + 1 < route.size(); i++) {
		const double ax = route[i].x, ay = route[i].y, bx = route[i + 1].x, by = route[i + 1].y;
		const double dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
		double t = len > 0 ? ((pt.x - ax) * dx + (pt.y - ay) * dy) / len : 0;
		t = std::clamp(t, 0.0, 1.0);
		const double x = ax + t * dx - pt.x, y = ay + t * dy - pt.y;
		best = std::min(best, x * x + y * y);
	}
	return route.size() < 2 ? 1e9 : std::sqrt(best);
}

// ---- Dragging ----------------------------------------------------------------------------------------

namespace {
	// to the nearest unit's start
	GntTime Snap(GntTime t, GntUnit unit) {
		const GntTime s = GntUnitStart(unit, t), n = GntUnitNext(unit, s);
		return (t - s) * 2 >= n - s ? n : s;
	}
}

GntTime GntDragStart(GntTime start, int dx, const GntScale& scale, GntUnit snap, const GntCalendar& calendar) {
	const GntTime t = start + (GntTime)std::llround(dx * scale.minutesPerPixel);
	return calendar.NextWorkStart(Snap(t, snap));
}

int64_t GntDragDuration(GntTime start, GntTime finish, int dx, const GntScale& scale, GntUnit snap, const GntCalendar& calendar) {
	const GntTime f = Snap(finish + (GntTime)std::llround(dx * scale.minutesPerPixel), snap);
	const int64_t least = snap == GntUnit::Hour ? 60 : calendar.MinutesPerDay() > 0 ? calendar.MinutesPerDay() : GntDay;
	return std::max(calendar.WorkBetween(start, f), least);
}

int GntDragProgress(const GntBar& bar, int x) {
	const int w = bar.rc.right - bar.rc.left;
	if (w <= 0)
		return 0;
	return std::clamp((int)std::lround((x - bar.rc.left) * 100.0 / w), 0, 100);
}

// ---- Undo ----------------------------------------------------------------------------------------------

void GntHistory::Push(GntProject::State before, GntProject::State after) {
	m_Changes.resize(m_Done);   // what was undone can't be redone any more
	m_Changes.push_back({ std::move(before), std::move(after) });
	if (m_Limit > 0 && m_Changes.size() > m_Limit)
		m_Changes.erase(m_Changes.begin());
	m_Done = m_Changes.size();
}

bool GntHistory::Undo(GntProject& project) {
	if (!CanUndo())
		return false;
	project.Restore(m_Changes[--m_Done].before);
	return true;
}

bool GntHistory::Redo(GntProject& project) {
	if (!CanRedo())
		return false;
	project.Restore(m_Changes[m_Done++].after);
	return true;
}
