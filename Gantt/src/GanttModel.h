#pragma once

// The Gantt chart's window-free parts: dates, the working calendar (working days and hours, holidays: adding work to a
// time, the work between two times), the time scale (zoom, the header's units and their cells), the task tree (summary
// tasks from their children, the rows shown), dependencies (four kinds with lag, no cycles, pushing successors), a
// task's bar and its parts under the mouse, a link's route, dragging (snapped to the scale's unit), and undo. Sizes are
// in pixels; the control scales the metrics for its DPI. Covered by GanttTests.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// ---- Time ------------------------------------------------------------------------------------------
//
// Times are minutes since 1601-01-01 00:00 (FILETIME's epoch); days count from that day, a Monday.

using GntTime = int64_t;
constexpr int64_t GntDay = 1440;   // minutes

struct GntDate {
	int year = 1601, month = 1, day = 1;
};

int64_t GntDayOf(GntTime t);                                  // the day t is in (before 1601: negative)
GntTime GntTimeOf(int64_t day, int minute = 0);
GntDate GntDateOf(int64_t day);
int64_t GntDayFromDate(int year, int month, int day);
int     GntWeekday(int64_t day);                              // 0 Sunday to 6 Saturday
GntTime GntFromSystemTime(const SYSTEMTIME& st);
SYSTEMTIME GntToSystemTime(GntTime t);

// ---- The working calendar --------------------------------------------------------------------------

class GntCalendar {
public:
	BYTE workDays = 0x3E;          // a bit by weekday, Sunday's the lowest: Monday to Friday
	int  dayStart = 8 * 60;        // a working day's working minutes: [dayStart, dayEnd)
	int  dayEnd = 17 * 60;

	void SetHolidays(std::vector<int64_t> days);
	bool AddHoliday(int64_t day);
	bool RemoveHoliday(int64_t day);
	const std::vector<int64_t>& Holidays() const {
		return m_Holidays;
	}

	bool IsWorkingDay(int64_t day) const;
	int  MinutesPerDay() const {
		return dayEnd - dayStart;
	}
	GntTime NextWorkStart(GntTime t) const;                   // t if it's working time, else where work next starts
	GntTime PrevWorkEnd(GntTime t) const;                     // t if the minute before it is working, else where work last ended
	// Where minutes of work from start end (negative: where they began, to end at start). 0: start.
	GntTime AddWork(GntTime start, int64_t minutes) const;
	int64_t WorkBetween(GntTime from, GntTime to) const;      // the working minutes in [from, to); negative if to < from

private:
	std::vector<int64_t> m_Holidays;   // sorted
};

// ---- The time scale ---------------------------------------------------------------------------------

enum class GntUnit : uint8_t { Hour, Day, Week, Month, Quarter, Year };

struct GntScale {
	GntTime origin = 0;            // the time at x = 0
	double  minutesPerPixel = 60;  // the zoom

	double X(GntTime t) const {
		return (t - origin) / minutesPerPixel;
	}
	GntTime T(double x) const {
		return origin + (GntTime)(x * minutesPerPixel + (x >= 0 ? 0.5 : -0.5));
	}
};

// The header's units: the finest whose cells are at least minPixels wide, and the one above it (Hour: Day, Day: Week,
// Week: Month, Month: Quarter, Quarter: Year; Year: Year, a single row).
void GntPickUnits(double minutesPerPixel, int minPixels, GntUnit& lower, GntUnit& upper);
GntTime GntUnitStart(GntUnit unit, GntTime t);                // the unit t is in (weeks start on Monday)
GntTime GntUnitNext(GntUnit unit, GntTime start);             // the next unit's start

struct GntCell {
	GntTime start = 0, end = 0;
};

// the units covering [from, to)
std::vector<GntCell> GntCells(GntUnit unit, GntTime from, GntTime to);

// ---- Tasks and links ----------------------------------------------------------------------------------

enum class GntLinkType : uint8_t { FinishStart, StartStart, FinishFinish, StartFinish };

struct GntTask {
	UINT    id = 0;
	UINT    parent = 0;            // 0: at the top
	std::wstring name;
	GntTime start = 0;
	int64_t duration = 0;          // working minutes; 0: a milestone
	int     progress = 0;          // 0 to 100
	bool    expanded = true;
	COLORREF color = CLR_INVALID;  // CLR_INVALID: the control's
	LPARAM  lParam = 0;

	// Recompute's
	GntTime finish = 0;
	bool    summary = false;       // it has children: its dates are theirs
	int     depth = 0;

	bool Milestone() const {
		return duration == 0 && !summary;
	}
};

struct GntLink {
	UINT    from = 0, to = 0;
	GntLinkType type = GntLinkType::FinishStart;
	int64_t lag = 0;               // working minutes (negative: a lead)
};

class GntProject {
public:
	GntCalendar calendar;

	struct State {
		std::vector<GntTask> tasks;
		std::vector<GntLink> links;
	};

	// After the task after (its parent's, with its children), else last among its parent's children. False if the id
	// is taken or 0, or the parent or after isn't there.
	bool Add(const GntTask& task, UINT after = 0);
	bool Remove(UINT id);                                     // its children and links too
	GntTask* Find(UINT id);
	const GntTask* Find(UINT id) const;
	int  Index(UINT id) const;                                // in Tasks; -1 if none
	const std::vector<GntTask>& Tasks() const {
		return m_Tasks;
	}
	bool IsAncestor(UINT ancestor, UINT id) const;

	// the finishes, depths, and summary tasks' dates (from their children) and progress (by their work)
	void Recompute();
	// The tasks shown, in order (those under a collapsed task are hidden): indexes into Tasks.
	std::vector<int> Rows() const;

	bool CanLink(UINT from, UINT to) const;                   // both tasks (not summaries), not linked yet, no cycle
	bool AddLink(const GntLink& link);
	bool RemoveLink(UINT from, UINT to);
	const std::vector<GntLink>& Links() const {
		return m_Links;
	}

	// The earliest start a task's links allow (INT64_MIN if it has no predecessors).
	GntTime EarliestStart(UINT id) const;
	// Moves the tasks after id by links that start before those links allow, later (never earlier), and theirs in
	// turn; the tasks moved.
	std::vector<UINT> PushSuccessors(UINT id);

	State Save() const;
	void Restore(const State& state);

private:
	GntTime Finish(const GntTask& t) const;

	std::vector<GntTask> m_Tasks;  // a parent before its children, siblings in order
	std::vector<GntLink> m_Links;
};

// ---- Bars --------------------------------------------------------------------------------------------

struct GntMetrics {
	int rowHeight = 24;
	int barPad = 5;                // a bar's top and bottom to its row's
	int summaryHeight = 8;         // a summary bar's
	int milestone = 14;            // a milestone's diamond
	int handle = 6;                // the band at a bar's edges that resizes it
	int link = 8;                  // the link handle's diameter, after a bar's end
	int linkGap = 8;               // a link's first and last step
	int minBar = 3;                // a bar's least width

	GntMetrics Scaled(int dpi) const;
};

struct GntBar {
	RECT rc{};                     // the bar (a milestone: its diamond's box)
	RECT done{};                   // the part done
	bool milestone = false;
	bool summary = false;
};

GntBar GntLayoutBar(const GntTask& task, int row, const GntScale& scale, const GntMetrics& m);

// Link: the handle after the bar's end, to drag a link from
enum class GntPart : uint8_t { None, Bar, Start, Finish, Progress, Link };

// pt in the same coordinates as the bar; a summary is only Bar, a milestone Bar or Link
GntPart GntHitBar(const GntBar& bar, POINT pt, const GntMetrics& m);
RECT GntLinkHandle(const GntBar& bar, const GntMetrics& m);

// A link's line from a bar to another, as points; the last is where its arrow points.
std::vector<POINT> GntRouteLink(const GntBar& from, const GntBar& to, GntLinkType type, const GntMetrics& m);

// ---- Dragging ----------------------------------------------------------------------------------------

// A start moved dx pixels, snapped to the nearest snap unit's start (Day, Hour), then to where work starts.
GntTime GntDragStart(GntTime start, int dx, const GntScale& scale, GntUnit snap, const GntCalendar& calendar);
// A finish moved dx pixels, snapped: the work from start to it (at least one snap unit's work).
int64_t GntDragDuration(GntTime start, GntTime finish, int dx, const GntScale& scale, GntUnit snap, const GntCalendar& calendar);
// The progress where the mouse is along the bar, 0 to 100.
int GntDragProgress(const GntBar& bar, int x);

// ---- Undo ----------------------------------------------------------------------------------------------

// The project before and after each change, to go back and forth.
class GntHistory {
public:
	explicit GntHistory(size_t limit = 100) : m_Limit(limit) {
	}
	void Push(GntProject::State before, GntProject::State after);
	bool Undo(GntProject& project);
	bool Redo(GntProject& project);
	bool CanUndo() const {
		return m_Done > 0;
	}
	bool CanRedo() const {
		return m_Done < m_Changes.size();
	}
	void Clear() {
		m_Changes.clear();
		m_Done = 0;
	}

private:
	struct Change {
		GntProject::State before, after;
	};
	std::vector<Change> m_Changes;
	size_t m_Done = 0;
	size_t m_Limit;
};
