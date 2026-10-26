#include "CalendarViewModel.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>
#include <unordered_set>

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

	ClvDate CivilFromDays(int64_t z) {
		z += 719468;
		const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		const int doe = (int)(z - era * 146097);
		const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		const int mp = (5 * doy + 2) / 153;
		ClvDate date;
		date.day = doy - (153 * mp + 2) / 5 + 1;
		date.month = mp < 10 ? mp + 3 : mp - 9;
		date.year = (int)(yoe + era * 400) + (date.month <= 2);
		return date;
	}

	int64_t FloorDiv(int64_t a, int64_t b) {
		return a >= 0 ? a / b : -((-a + b - 1) / b);
	}

	ClvTime FloorTo(ClvTime t, int snap) {
		return snap > 1 ? FloorDiv(t, snap) * snap : t;
	}
}

int64_t ClvDayOf(ClvTime t) {
	return FloorDiv(t, ClvDay);
}

int ClvMinuteOf(ClvTime t) {
	return (int)(t - ClvDayOf(t) * ClvDay);
}

ClvTime ClvTimeOf(int64_t day, int minute) {
	return day * ClvDay + minute;
}

ClvDate ClvDateOf(int64_t day) {
	return CivilFromDays(day - From1601To1970);
}

int64_t ClvDayFromDate(int year, int month, int day) {
	return DaysFromCivil(year, month, day) + From1601To1970;
}

int ClvWeekday(int64_t day) {
	return (int)(((day % 7) + 7 + 1) % 7);   // day 0, 1601-01-01, was a Monday
}

int ClvDaysInMonth(int year, int month) {
	static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
		return 29;
	return days[(month - 1) % 12];
}

int64_t ClvWeekStart(int64_t day, int firstWeekday) {
	return day - (ClvWeekday(day) - firstWeekday + 7) % 7;
}

int64_t ClvMonthStart(int64_t day) {
	auto date = ClvDateOf(day);
	return day - (date.day - 1);
}

int64_t ClvAddMonths(int64_t day, int months) {
	auto date = ClvDateOf(day);
	const int64_t m = (int64_t)date.year * 12 + (date.month - 1) + months;
	const int year = (int)FloorDiv(m, 12);
	const int month = (int)(m - (int64_t)year * 12) + 1;
	return ClvDayFromDate(year, month, std::min(date.day, ClvDaysInMonth(year, month)));
}

ClvTime ClvFromSystemTime(const SYSTEMTIME& st) {
	return ClvTimeOf(ClvDayFromDate(st.wYear, st.wMonth, st.wDay), st.wHour * 60 + st.wMinute);
}

SYSTEMTIME ClvToSystemTime(ClvTime t) {
	const int64_t day = ClvDayOf(t);
	const int minute = ClvMinuteOf(t);
	auto date = ClvDateOf(day);
	SYSTEMTIME st{};
	st.wYear = (WORD)date.year;
	st.wMonth = (WORD)date.month;
	st.wDay = (WORD)date.day;
	st.wDayOfWeek = (WORD)ClvWeekday(day);
	st.wHour = (WORD)(minute / 60);
	st.wMinute = (WORD)(minute % 60);
	return st;
}

ClvMonthGrid ClvMonthGridOf(int64_t day, int firstWeekday) {
	const int64_t start = ClvMonthStart(day);
	auto date = ClvDateOf(start);
	ClvMonthGrid grid;
	grid.first = ClvWeekStart(start, firstWeekday);
	grid.weeks = (int)((start - grid.first + ClvDaysInMonth(date.year, date.month) + 6) / 7);
	return grid;
}

std::vector<ClvRange> ClvMissing(ClvRange want, ClvRange have) {
	if (want.days <= 0)
		return {};
	if (have.days <= 0 || have.Last() < want.first || have.first > want.Last())
		return { want };
	std::vector<ClvRange> missing;
	if (want.first < have.first)
		missing.push_back({ want.first, (int)(have.first - want.first) });
	if (want.Last() > have.Last())
		missing.push_back({ have.Last() + 1, (int)(want.Last() - have.Last()) });
	return missing;
}

// ---- Items -----------------------------------------------------------------------------------------

int64_t ClvFirstDay(const ClvItem& item) {
	return ClvDayOf(item.start);
}

int64_t ClvLastDay(const ClvItem& item) {
	return item.end > item.start ? ClvDayOf(item.end - 1) : ClvDayOf(item.start);
}

bool ClvItemStore::Add(const ClvItem& item) {
	if (item.id == 0 || item.end < item.start || Index(item.id) >= 0)
		return false;
	m_Items.push_back(item);
	return true;
}

bool ClvItemStore::Set(const ClvItem& item) {
	auto p = Find(item.id);
	if (!p || item.end < item.start)
		return false;
	*p = item;
	return true;
}

bool ClvItemStore::Remove(uint64_t id) {
	const int i = Index(id);
	if (i < 0)
		return false;
	m_Items.erase(m_Items.begin() + i);
	return true;
}

ClvItem* ClvItemStore::Find(uint64_t id) {
	const int i = Index(id);
	return i < 0 ? nullptr : &m_Items[i];
}

const ClvItem* ClvItemStore::Find(uint64_t id) const {
	const int i = Index(id);
	return i < 0 ? nullptr : &m_Items[i];
}

int ClvItemStore::Index(uint64_t id) const {
	for (size_t i = 0; i < m_Items.size(); i++)
		if (m_Items[i].id == id)
			return (int)i;
	return -1;
}

namespace {
	bool Over(const ClvItem& item, ClvTime from, ClvTime to) {
		if (item.end == item.start)
			return item.start >= from && item.start < to;
		return item.start < to && item.end > from;
	}
}

std::vector<int> ClvItemStore::InRange(ClvTime from, ClvTime to) const {
	std::vector<int> items;
	for (size_t i = 0; i < m_Items.size(); i++)
		if (Over(m_Items[i], from, to))
			items.push_back((int)i);
	std::sort(items.begin(), items.end(), [&](int a, int b) {
		const auto& x = m_Items[a];
		const auto& y = m_Items[b];
		if (x.start != y.start)
			return x.start < y.start;
		if (x.end - x.start != y.end - y.start)
			return x.end - x.start > y.end - y.start;
		return x.id < y.id;
		});
	return items;
}

std::vector<ClvItem> ClvItemStore::Keep(ClvTime from, ClvTime to) {
	auto it = std::stable_partition(m_Items.begin(), m_Items.end(), [&](const ClvItem& item) {
		return Over(item, from, to);
		});
	std::vector<ClvItem> removed(std::make_move_iterator(it), std::make_move_iterator(m_Items.end()));
	m_Items.erase(it, m_Items.end());
	return removed;
}

// ---- Pieces ----------------------------------------------------------------------------------------

void ClvSplit(const ClvItemStore& store, const std::vector<int>& items, ClvRange range,
	std::vector<ClvPiece>& timed, std::vector<ClvPiece>& strip) {
	timed.clear();
	strip.clear();
	if (range.days <= 0)
		return;
	std::vector<std::vector<ClvPiece>> days(range.days);
	for (int index : items) {
		const auto& item = store.Items()[index];
		const int64_t first = ClvFirstDay(item), last = ClvLastDay(item);
		if (last < range.first || first > range.Last())
			continue;
		if (ClvInStrip(item)) {
			ClvPiece p;
			p.item = index;
			p.day = (int)(std::max(first, range.first) - range.first);
			p.lastDay = (int)(std::min(last, range.Last()) - range.first);
			p.before = first < range.first;
			p.after = last > range.Last();
			strip.push_back(p);
			continue;
		}
		for (int64_t d = std::max(first, range.first); d <= std::min(last, range.Last()); d++) {
			const ClvTime dayStart = d * ClvDay;
			ClvPiece p;
			p.item = index;
			p.day = p.lastDay = (int)(d - range.first);
			p.start = (int)(std::max(item.start, dayStart) - dayStart);
			p.end = (int)(std::min(item.end, dayStart + ClvDay) - dayStart);
			p.before = item.start < dayStart;
			p.after = item.end > dayStart + ClvDay;
			days[p.day].push_back(p);
		}
	}
	for (auto& day : days)
		timed.insert(timed.end(), day.begin(), day.end());
}

std::vector<ClvPlace> ClvPlaceOverlaps(const std::vector<std::pair<int, int>>& spans) {
	const size_t n = spans.size();
	std::vector<ClvPlace> places(n);
	std::vector<int> order(n);
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		if (spans[a].first != spans[b].first)
			return spans[a].first < spans[b].first;
		if (spans[a].second != spans[b].second)
			return spans[a].second > spans[b].second;
		return a < b;
		});

	auto overlap = [&](int a, int b) {
		return spans[a].first < spans[b].second && spans[b].first < spans[a].second;
	};

	for (size_t i = 0; i < n; ) {
		std::vector<int> ends;     // each column's last end
		int clusterEnd = spans[order[i]].second;
		size_t k = i;
		for (; k < n && (k == i || spans[order[k]].first < clusterEnd); k++) {
			const auto& s = spans[order[k]];
			size_t c = 0;
			while (c < ends.size() && ends[c] > s.first)
				c++;
			if (c == ends.size())
				ends.push_back(s.second);
			else
				ends[c] = s.second;
			places[order[k]].column = (int)c;
			clusterEnd = std::max(clusterEnd, s.second);
		}
		const int columns = (int)ends.size();
		for (size_t a = i; a < k; a++) {
			auto& place = places[order[a]];
			place.columns = columns;
			int right = place.column + 1;
			for (; right < columns; right++) {
				bool blocked = false;
				for (size_t b = i; b < k && !blocked; b++)
					blocked = places[order[b]].column == right && overlap(order[a], order[b]);
				if (blocked)
					break;
			}
			place.span = right - place.column;
		}
		i = k;
	}
	return places;
}

int ClvPackStrip(const std::vector<ClvPiece>& pieces, std::vector<int>& rows) {
	const size_t n = pieces.size();
	rows.assign(n, 0);
	std::vector<int> order(n);
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		if (pieces[a].day != pieces[b].day)
			return pieces[a].day < pieces[b].day;
		return pieces[a].lastDay - pieces[a].day > pieces[b].lastDay - pieces[b].day;
		});
	std::vector<int> lastDays;     // each row's last day taken
	for (int i : order) {
		size_t r = 0;
		while (r < lastDays.size() && lastDays[r] >= pieces[i].day)
			r++;
		if (r == lastDays.size())
			lastDays.push_back(pieces[i].lastDay);
		else
			lastDays[r] = pieces[i].lastDay;
		rows[i] = (int)r;
	}
	return (int)lastDays.size();
}

// ---- The day view ----------------------------------------------------------------------------------

ClvMetrics ClvMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	ClvMetrics m;
	m.slotHeight = s(slotHeight);
	m.rulerWidth = s(rulerWidth);
	m.headerHeight = s(headerHeight);
	m.stripRow = s(stripRow);
	m.stripMaxRows = stripMaxRows;
	m.stripPad = s(stripPad);
	m.itemGap = s(itemGap);
	m.itemPad = std::max(1, s(itemPad));
	m.caption = s(caption);
	m.minItem = s(minItem);
	m.minBody = s(minBody);
	m.edge = s(edge);
	m.dayNumber = s(dayNumber);
	return m;
}

int ClvAxis::Minute(int y) const {
	y = std::clamp(y, 0, Height());
	return std::min((int)ClvDay, (int)((int64_t)y * slotMinutes / slotHeight));
}

int ClvAxis::Minutes(int pixels) const {
	return (int)(((int64_t)pixels * slotMinutes + slotHeight - 1) / slotHeight);
}

int ClvColumns::At(int x) const {
	if (count <= 0 || width <= 0 || x < left || x >= left + width)
		return -1;
	int i = (int)((int64_t)(x - left) * count / width);
	while (i + 1 < count && Left(i + 1) <= x)
		i++;
	while (i > 0 && Left(i) > x)
		i--;
	return i;
}

int ClvDayLayout::StripHeight(const ClvMetrics& m) const {
	return stripRows * m.stripRow;
}

int ClvDayLayout::StripView(const ClvMetrics& m) const {
	return 2 * m.stripPad + std::max(1, std::min(stripRows, m.stripMaxRows)) * m.stripRow;
}

const ClvBox* ClvDayLayout::FindTimed(uint64_t id, int day) const {
	for (auto& box : timed)
		if (box.id == id && box.day == day)
			return &box;
	return nullptr;
}

const ClvBox* ClvDayLayout::FindStrip(uint64_t id) const {
	for (auto& box : strip)
		if (box.id == id)
			return &box;
	return nullptr;
}

ClvDayLayout ClvLayoutDay(const ClvItemStore& store, ClvRange range, const ClvColumns& columns, const ClvAxis& axis,
	const ClvMetrics& m) {
	ClvDayLayout layout;
	layout.range = range;
	layout.columns = columns;
	layout.axis = axis;

	std::vector<ClvPiece> timed, strip;
	ClvSplit(store, store.InRange(range.Start(), range.End()), range, timed, strip);

	std::vector<int> rows;
	layout.stripRows = ClvPackStrip(strip, rows);
	const int inset = 2 * m.itemPad;
	layout.strip.reserve(strip.size());
	for (size_t i = 0; i < strip.size(); i++) {
		const auto& p = strip[i];
		const auto& item = store.Items()[p.item];
		ClvBox box;
		box.id = item.id;
		box.item = p.item;
		box.day = p.day;
		box.lastDay = p.lastDay;
		box.row = rows[i];
		box.before = p.before;
		box.after = p.after;
		box.resizable = !item.readOnly;
		box.rc = { columns.Left(p.day) + (p.before ? 0 : inset), box.row * m.stripRow + m.itemPad,
			columns.Right(p.lastDay) - (p.after ? 0 : inset), (box.row + 1) * m.stripRow - m.itemPad };
		if (item.content == ClvContent::Window)
			box.caption = box.rc;   // no room for the window: the caption only
		else
			box.body = box.rc;
		layout.strip.push_back(box);
	}

	const int minMinutes = axis.Minutes(m.minItem);
	layout.timed.reserve(timed.size());
	for (size_t i = 0; i < timed.size(); ) {
		size_t k = i;
		while (k < timed.size() && timed[k].day == timed[i].day)
			k++;
		std::vector<std::pair<int, int>> spans;
		spans.reserve(k - i);
		for (size_t j = i; j < k; j++)
			spans.push_back({ timed[j].start, std::max(timed[j].end, timed[j].start + minMinutes) });
		auto places = ClvPlaceOverlaps(spans);

		const int day = timed[i].day;
		const int left = columns.Left(day);
		const int width = std::max(0, columns.Right(day) - left - m.itemGap);
		for (size_t j = i; j < k; j++) {
			const auto& p = timed[j];
			const auto& place = places[j - i];
			const auto& item = store.Items()[p.item];
			ClvBox box;
			box.id = item.id;
			box.item = p.item;
			box.day = box.lastDay = day;
			box.before = p.before;
			box.after = p.after;
			box.resizable = !item.readOnly;
			const int top = axis.Y(p.start);
			box.rc = { left + width * place.column / place.columns, top,
				left + width * (place.column + place.span) / place.columns - m.itemPad,
				std::max(axis.Y(p.end), top + m.minItem) - m.itemPad };
			if (item.content == ClvContent::Window) {
				box.caption = box.rc;
				box.caption.bottom = std::min(box.rc.bottom, box.rc.top + m.caption);
				if (box.rc.bottom - box.caption.bottom >= m.minBody)
					box.body = { box.rc.left, box.caption.bottom, box.rc.right, box.rc.bottom };
			}
			else
				box.body = box.rc;
			layout.timed.push_back(box);
		}
		i = k;
	}
	return layout;
}

uint64_t ClvHit::Id(const ClvDayLayout& layout, bool strip) const {
	if (box < 0)
		return 0;
	return (strip ? layout.strip : layout.timed)[box].id;
}

namespace {
	bool In(const RECT& rc, POINT pt) {
		return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
	}

	// the box under pt, top's first, then the last drawn
	int BoxAt(const std::vector<ClvBox>& boxes, POINT pt, uint64_t top) {
		if (top)
			for (size_t i = boxes.size(); i-- > 0; )
				if (boxes[i].id == top && In(boxes[i].rc, pt))
					return (int)i;
		for (size_t i = boxes.size(); i-- > 0; )
			if (In(boxes[i].rc, pt))
				return (int)i;
		return -1;
	}
}

ClvHit ClvHitDays(const ClvDayLayout& layout, POINT pt, const ClvMetrics& m, uint64_t top) {
	ClvHit hit;
	hit.minute = layout.axis.Minute(pt.y);
	if (pt.y < 0 || pt.y >= layout.axis.Height())
		return hit;
	if (pt.x < layout.columns.left) {
		hit.part = pt.x >= 0 ? ClvPart::Ruler : ClvPart::None;
		return hit;
	}
	hit.day = layout.columns.At(pt.x);
	hit.box = BoxAt(layout.timed, pt, top);
	if (hit.box < 0) {
		hit.part = hit.day >= 0 ? ClvPart::Slot : ClvPart::None;
		return hit;
	}
	const auto& box = layout.timed[hit.box];
	const int edge = std::min(m.edge, (int)(box.rc.bottom - box.rc.top) / 3);
	if (box.resizable && !box.before && pt.y < box.rc.top + edge)
		hit.part = ClvPart::Start;
	else if (box.resizable && !box.after && pt.y >= box.rc.bottom - edge)
		hit.part = ClvPart::End;
	else if (!IsRectEmpty(&box.caption) && In(box.caption, pt))
		hit.part = ClvPart::Caption;
	else if (!IsRectEmpty(&box.caption))
		hit.part = ClvPart::Body;   // a window item's: its window, or where it would be if there were room
	else
		hit.part = ClvPart::Item;
	return hit;
}

ClvHit ClvHitStrip(const ClvDayLayout& layout, POINT pt, const ClvMetrics& m, uint64_t top) {
	ClvHit hit;
	hit.day = layout.columns.At(pt.x);
	hit.box = BoxAt(layout.strip, pt, top);
	if (hit.box < 0) {
		hit.part = hit.day >= 0 && pt.y >= 0 ? ClvPart::Slot : ClvPart::None;
		return hit;
	}
	const auto& box = layout.strip[hit.box];
	const int edge = std::min(m.edge, (int)(box.rc.right - box.rc.left) / 3);
	if (box.resizable && !box.before && pt.x < box.rc.left + edge)
		hit.part = ClvPart::Start;
	else if (box.resizable && !box.after && pt.x >= box.rc.right - edge)
		hit.part = ClvPart::End;
	else if (!IsRectEmpty(&box.caption))
		hit.part = ClvPart::Caption;
	else
		hit.part = ClvPart::Item;
	return hit;
}

// ---- Dragging --------------------------------------------------------------------------------------

ClvTime ClvSnap(ClvTime t, int snap) {
	return snap > 1 ? FloorDiv(t + snap / 2, snap) * snap : t;
}

ClvSpan ClvDragMove(ClvSpan item, ClvTime grab, ClvTime at, int snap, int defaultMinutes) {
	if (item.allDay) {
		const ClvTime start = FloorTo(at, snap);
		return { start, start + std::max(1, defaultMinutes), false };
	}
	const ClvTime start = ClvSnap(item.start + (at - grab), snap);
	return { start, start + (item.end - item.start), false };
}

ClvSpan ClvDragMoveStrip(ClvSpan item, int64_t grabDay, int64_t day) {
	const int64_t delta = (day - grabDay) * ClvDay;
	if (item.allDay || item.end - item.start >= ClvDay)
		return { item.start + delta, item.end + delta, item.allDay };
	// a timed item dragged up into the strip: all day over the days it touched
	const int64_t first = ClvDayOf(item.start);
	const int64_t last = item.end > item.start ? ClvDayOf(item.end - 1) : first;
	return { first * ClvDay + delta, (last + 1) * ClvDay + delta, true };
}

ClvSpan ClvDragStart(ClvSpan item, ClvTime at, int snap, int minMinutes) {
	item.start = std::min(ClvSnap(at, snap), item.end - std::max(0, minMinutes));
	return item;
}

ClvSpan ClvDragEnd(ClvSpan item, ClvTime at, int snap, int minMinutes) {
	item.end = std::max(ClvSnap(at, snap), item.start + std::max(0, minMinutes));
	return item;
}

ClvSpan ClvDragStripStart(ClvSpan item, int64_t day) {
	const int64_t last = item.end > item.start ? ClvDayOf(item.end - 1) : ClvDayOf(item.start);
	day = std::min(day, last);
	if (item.allDay)
		item.start = day * ClvDay;
	else
		item.start = std::min(ClvTimeOf(day, ClvMinuteOf(item.start)), item.end);
	return item;
}

ClvSpan ClvDragStripEnd(ClvSpan item, int64_t day) {
	const int64_t first = ClvDayOf(item.start);
	day = std::max(day, first);
	if (item.allDay)
		item.end = (day + 1) * ClvDay;
	else {
		const int64_t last = item.end > item.start ? ClvDayOf(item.end - 1) : first;
		item.end = std::max(day * ClvDay + (item.end - last * ClvDay), item.start);   // its time of day kept
	}
	return item;
}

ClvSpan ClvSlotRange(ClvTime anchor, ClvTime at, int slot) {
	slot = std::max(1, slot);
	const ClvTime a = FloorTo(anchor, slot), b = FloorTo(at, slot);
	return { std::min(a, b), std::max(a, b) + slot, false };
}

ClvSpan ClvDragMoveDays(ClvSpan item, int64_t grabDay, int64_t day) {
	const ClvTime delta = (day - grabDay) * ClvDay;
	return { item.start + delta, item.end + delta, item.allDay };
}

ClvSpan ClvDayRange(int64_t anchor, int64_t day) {
	return { std::min(anchor, day) * ClvDay, (std::max(anchor, day) + 1) * ClvDay, true };
}

ClvTime ClvMoveSlot(ClvTime t, int days, int slots, int slot) {
	slot = std::clamp(slot, 1, (int)ClvDay);
	const int64_t day = ClvDayOf(t) + days;
	int minute = (int)FloorTo(ClvMinuteOf(t), slot) + slots * slot;
	minute = std::clamp(minute, 0, (int)((ClvDay - 1) / slot * slot));
	return ClvTimeOf(day, minute);
}

// ---- Going between days -----------------------------------------------------------------------------

int ClvViewStep(int days) {
	return days == 5 ? 7 : std::max(1, days);
}

int64_t ClvFirstShown(int64_t day, int days, int firstWeekday) {
	if (days == 5)
		return ClvWeekStart(day, 1);
	if (days == 7)
		return ClvWeekStart(day, firstWeekday);
	return day;
}

ClvMiniMetrics ClvMiniMetrics::Scaled(int dpi) const {
	auto s = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	ClvMiniMetrics m;
	m.cell = s(cell);
	m.title = s(title);
	m.weekdays = s(weekdays);
	m.pad = s(pad);
	return m;
}

ClvMiniLayout ClvLayoutMini(int64_t day, int firstWeekday, const ClvMiniMetrics& m) {
	ClvMiniLayout l;
	l.month = ClvMonthStart(day);
	l.first = ClvWeekStart(l.month, firstWeekday);
	l.cell = m.cell;
	const int width = 7 * m.cell;
	l.size = { 2 * m.pad + width, 2 * m.pad + m.title + m.weekdays + 6 * m.cell };
	int y = m.pad;
	l.prev = { m.pad, y, m.pad + m.cell, y + m.title };
	l.next = { m.pad + width - m.cell, y, m.pad + width, y + m.title };
	l.title = { l.prev.right, y, l.next.left, y + m.title };
	y += m.title;
	l.weekdays = { m.pad, y, m.pad + width, y + m.weekdays };
	y += m.weekdays;
	l.days = { m.pad, y, m.pad + width, y + 6 * m.cell };
	return l;
}

RECT ClvMiniLayout::DayRect(int index) const {
	const int x = days.left + index % 7 * cell, y = days.top + index / 7 * cell;
	return { x, y, x + cell, y + cell };
}

int ClvMiniLayout::Weekday(int column) const {
	return ClvWeekday(first + column);
}

ClvMiniPart ClvMiniLayout::Hit(POINT pt, int64_t* day) const {
	auto in = [&](const RECT& rc) {
		return pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom;
	};
	if (in(prev))
		return ClvMiniPart::Prev;
	if (in(next))
		return ClvMiniPart::Next;
	if (in(title))
		return ClvMiniPart::Title;
	if (in(days)) {
		const int index = (pt.y - days.top) / cell * 7 + (pt.x - days.left) / cell;
		if (day)
			*day = first + index;
		return ClvMiniPart::Day;
	}
	return ClvMiniPart::None;
}

// ---- Scrolling, zoom and the tab order -------------------------------------------------------------

int ClvClampScroll(int scroll, int content, int view) {
	return std::max(0, std::min(scroll, content - view));
}

int ClvScrollToShow(int scroll, int top, int bottom, int view, int content) {
	if (bottom - top > view || top < scroll)
		scroll = top;
	else if (bottom > scroll + view)
		scroll = bottom - view;
	return ClvClampScroll(scroll, content, view);
}

int ClvZoom(int slotHeight, int steps, int minHeight, int maxHeight) {
	int h = slotHeight;
	for (; steps > 0; steps--)
		h = std::max(h + 1, (int)std::lround(h * 1.25));
	for (; steps < 0; steps++)
		h = std::min(h - 1, (int)std::lround(h / 1.25));
	return std::clamp(h, minHeight, maxHeight);
}

std::vector<uint64_t> ClvItemOrder(const std::vector<ClvBox>& boxes, const ClvItemStore& store) {
	std::vector<int> items;
	std::unordered_set<uint64_t> seen;
	for (auto& box : boxes)
		if (seen.insert(box.id).second)
			items.push_back(box.item);
	std::sort(items.begin(), items.end(), [&](int a, int b) {
		const auto& x = store.Items()[a];
		const auto& y = store.Items()[b];
		if (x.start != y.start)
			return x.start < y.start;
		if (x.end - x.start != y.end - y.start)
			return x.end - x.start > y.end - y.start;
		return x.id < y.id;
		});
	std::vector<uint64_t> order;
	order.reserve(items.size());
	for (int i : items)
		order.push_back(store.Items()[i].id);
	return order;
}

std::vector<uint64_t> ClvTabOrder(const ClvDayLayout& layout, const ClvItemStore& store) {
	auto order = ClvItemOrder(layout.strip, store);
	auto timed = ClvItemOrder(layout.timed, store);
	order.insert(order.end(), timed.begin(), timed.end());
	return order;
}

uint64_t ClvNextItem(const std::vector<uint64_t>& order, uint64_t current, bool forward) {
	if (order.empty())
		return 0;
	auto it = std::find(order.begin(), order.end(), current);
	if (current == 0 || it == order.end())
		return forward ? order.front() : order.back();
	if (forward)
		return it + 1 == order.end() ? 0 : *(it + 1);
	return it == order.begin() ? 0 : *(it - 1);
}

// ---- The month view --------------------------------------------------------------------------------

ClvRange ClvMonthRange(int64_t day, int firstWeekday) {
	auto grid = ClvMonthGridOf(day, firstWeekday);
	return { grid.first, grid.weeks * 7 };
}

RECT ClvMonthLayout::Cell(int day) const {
	const int week = day / 7, col = day % 7;
	if (day < 0 || week + 1 >= (int)rowTops.size())
		return {};
	return { columns.Left(col), rowTops[week], columns.Right(col), rowTops[week + 1] };
}

int ClvMonthLayout::DayAt(POINT pt) const {
	const int col = columns.At(pt.x);
	if (col < 0 || rowTops.size() < 2 || pt.y < rowTops.front() || pt.y >= rowTops.back())
		return -1;
	int week = 0;
	while (week + 2 < (int)rowTops.size() && pt.y >= rowTops[week + 1])
		week++;
	return week * 7 + col;
}

const ClvBox* ClvMonthLayout::Find(uint64_t id) const {
	for (auto& box : boxes)
		if (box.id == id)
			return &box;
	return nullptr;
}

ClvMonthLayout ClvLayoutMonth(const ClvItemStore& store, int64_t month, ClvRange range, const ClvColumns& columns,
	int top, int bottom, const ClvMetrics& m) {
	ClvMonthLayout l;
	l.range = range;
	l.month = month;
	l.columns = columns;
	const int weeks = std::max(1, range.days / 7);
	l.rowTops.resize(weeks + 1);
	int least = INT_MAX;
	for (int w = 0; w <= weeks; w++) {
		l.rowTops[w] = top + (int)((int64_t)(bottom - top) * w / weeks);
		if (w > 0)
			least = std::min(least, l.rowTops[w] - l.rowTops[w - 1]);
	}
	l.lanes = std::max(0, (least - m.dayNumber - m.itemPad) / std::max(1, m.stripRow));
	const int inset = 2 * m.itemPad;

	const auto items = store.InRange(range.Start(), range.End());
	for (int w = 0; w < weeks; w++) {
		const int64_t first = range.first + 7 * w, last = first + 6;
		// the bars over the week, packed; then the lines in the lanes they leave
		std::vector<ClvPiece> bars;
		std::vector<std::pair<int, int>> lines;   // item, column
		for (int index : items) {
			const auto& item = store.Items()[index];
			const int64_t a = ClvFirstDay(item), b = ClvLastDay(item);
			if (b < first || a > last)
				continue;
			if (ClvIsBar(item)) {
				ClvPiece p;
				p.item = index;
				p.day = (int)(std::max(a, first) - first);
				p.lastDay = (int)(std::min(b, last) - first);
				p.before = a < first;
				p.after = b > last;
				bars.push_back(p);
			}
			else
				lines.push_back({ index, (int)(a - first) });
		}
		std::vector<int> rows;
		ClvPackStrip(bars, rows);
		std::vector<std::vector<bool>> taken(7);  // by column, by lane
		auto take = [&](int col, int lane) {
			if ((int)taken[col].size() <= lane)
				taken[col].resize(lane + 1);
			taken[col][lane] = true;
		};
		for (size_t i = 0; i < bars.size(); i++)
			for (int c = bars[i].day; c <= bars[i].lastDay; c++)
				take(c, rows[i]);
		std::vector<int> lineLanes(lines.size());
		for (size_t i = 0; i < lines.size(); i++) {
			const int col = lines[i].second;
			int lane = 0;
			while (lane < (int)taken[col].size() && taken[col][lane])
				lane++;
			take(col, lane);
			lineLanes[i] = lane;
		}

		// what each day shows: its lanes, one fewer when "+N more" needs one
		int shown[7], hidden[7] = {};
		for (int c = 0; c < 7; c++) {
			auto beyond = [&](int lanes) {
				int n = 0;
				for (size_t i = 0; i < bars.size(); i++)
					n += c >= bars[i].day && c <= bars[i].lastDay && rows[i] >= lanes;
				for (size_t i = 0; i < lines.size(); i++)
					n += lines[i].second == c && lineLanes[i] >= lanes;
				return n;
			};
			shown[c] = l.lanes;
			if (beyond(l.lanes) > 0) {
				shown[c] = std::max(0, l.lanes - 1);
				hidden[c] = beyond(shown[c]);
			}
		}

		const int cellTop = l.rowTops[w] + m.dayNumber;
		auto laneRect = [&](int c1, int c2, int lane, bool before, bool after) {
			const int y = cellTop + lane * m.stripRow;
			return RECT{ columns.Left(c1) + (before ? 0 : inset), y + m.itemPad, columns.Right(c2) - (after ? 0 : inset), y + m.stripRow - m.itemPad };
		};
		auto add = [&](int index, int c1, int c2, int lane, bool before, bool after, bool line) {
			const auto& item = store.Items()[index];
			ClvBox box;
			box.id = item.id;
			box.item = index;
			box.day = 7 * w + c1;
			box.lastDay = 7 * w + c2;
			box.row = lane;
			box.before = before;
			box.after = after;
			box.line = line;
			box.resizable = !item.readOnly && !line;
			box.rc = laneRect(c1, c2, lane, before, after);
			if (item.content == ClvContent::Window)
				box.caption = box.rc;   // no room for the window: the caption only
			else
				box.body = box.rc;
			l.boxes.push_back(box);
		};
		for (size_t i = 0; i < bars.size(); i++) {
			// the runs of days where its lane is shown
			const auto& p = bars[i];
			for (int c = p.day; c <= p.lastDay; ) {
				if (rows[i] >= shown[c]) {
					c++;
					continue;
				}
				int e = c;
				while (e + 1 <= p.lastDay && rows[i] < shown[e + 1])
					e++;
				add(p.item, c, e, rows[i], c > p.day || p.before, e < p.lastDay || p.after, false);
				c = e + 1;
			}
		}
		for (size_t i = 0; i < lines.size(); i++)
			if (lineLanes[i] < shown[lines[i].second])
				add(lines[i].first, lines[i].second, lines[i].second, lineLanes[i], false, false, true);
		for (int c = 0; c < 7; c++)
			if (hidden[c] > 0) {
				ClvMore more;
				more.day = 7 * w + c;
				more.count = hidden[c];
				if (l.lanes > 0)
					more.rc = laneRect(c, c, shown[c], false, false);
				else   // no lane at all: beside the day's number
					more.rc = { columns.Left(c) + (columns.Right(c) - columns.Left(c)) / 2, l.rowTops[w], columns.Right(c) - inset, cellTop };
				l.more.push_back(more);
			}
	}
	return l;
}

ClvHit ClvHitMonth(const ClvMonthLayout& layout, POINT pt, const ClvMetrics& m, uint64_t top) {
	ClvHit hit;
	hit.day = layout.DayAt(pt);
	if (hit.day < 0)
		return hit;
	hit.box = BoxAt(layout.boxes, pt, top);
	if (hit.box >= 0) {
		const auto& box = layout.boxes[hit.box];
		const int edge = std::min(m.edge, (int)(box.rc.right - box.rc.left) / 3);
		if (box.resizable && !box.before && pt.x < box.rc.left + edge)
			hit.part = ClvPart::Start;
		else if (box.resizable && !box.after && pt.x >= box.rc.right - edge)
			hit.part = ClvPart::End;
		else if (!IsRectEmpty(&box.caption))
			hit.part = ClvPart::Caption;
		else
			hit.part = ClvPart::Item;
		return hit;
	}
	for (auto& more : layout.more)
		if (In(more.rc, pt)) {
			hit.part = ClvPart::More;
			hit.day = more.day;
			return hit;
		}
	hit.part = ClvPart::Slot;
	return hit;
}

// ---- Undo ------------------------------------------------------------------------------------------

void ClvHistory::Push(ClvItemStore::State before, ClvItemStore::State after) {
	m_Changes.resize(m_Done);   // what was undone can't be redone any more
	m_Changes.push_back({ std::move(before), std::move(after) });
	if (m_Limit > 0 && m_Changes.size() > m_Limit)
		m_Changes.erase(m_Changes.begin());
	m_Done = m_Changes.size();
}

bool ClvHistory::Undo(ClvItemStore& store) {
	if (!CanUndo())
		return false;
	store.Restore(m_Changes[--m_Done].before);
	return true;
}

bool ClvHistory::Redo(ClvItemStore& store) {
	if (!CanRedo())
		return false;
	store.Restore(m_Changes[m_Done++].after);
	return true;
}
