#pragma once

// The control's state, shared by its sources.

#include "Gantt.h"
#include "GanttModel.h"

enum class GntArea : uint8_t { None, GridHeader, ChartHeader, Grid, Expander, Splitter, Chart };

struct GntHitInfo {
	GntArea area = GntArea::None;
	int     row = -1;              // in GntData::rows
	int     task = -1;             // its index in the project's tasks
	GntPart part = GntPart::None;  // the bar's part (Chart)
};

enum class GntDrag : uint8_t { None, Pending, Move, Start, Finish, Progress, Link };   // Pending: pressed, not moved yet

struct GntMarker {
	UINT    id = 0;
	GntTime time = 0;
	std::wstring label;
	COLORREF color = CLR_INVALID;
};

struct GntColumn {
	UINT id = GNTCOL_NAME;
	int  dips = 100;
};

class GntAccessible;

struct GntData {
	GntProject project;
	GntScale scale;                // origin: the time at the chart's left
	GntMetrics m;
	GntUnit lower = GntUnit::Day, upper = GntUnit::Week;   // the header's tiers
	std::vector<int> rows;         // the tasks shown, in order
	std::vector<GntColumn> columns;
	int  gridDips = 340;

	// the layout (client coords)
	RECT gridHeader{}, chartHeader{}, grid{}, chart{}, splitter{};
	int  tier = 20;                // a header tier's height
	int  indent = 16;              // a tree level's
	int  scrollY = 0;              // the rows' pixels scrolled
	GntTime rangeStart = 0, rangeEnd = 0;   // the time the horizontal scroll bar covers

	UINT selected = 0;
	GntHitInfo hot;
	bool tracking = false;
	bool splitting = false;
	int  splitGrab = 0;
	std::wstring nameBuffer;       // GetTask's

	// a drag: the task changes as it goes (summaries and columns follow), the project as it was kept to go back to
	GntDrag drag = GntDrag::None;
	GntPart dragPart = GntPart::None;
	UINT    dragId = 0;
	POINT   dragDown{}, dragMouse{};
	GntTask dragOriginal;
	GntProject::State before;
	UINT    linkTarget = 0;        // a link drag over a task it can link to
	GntHistory history;
	std::vector<GntMarker> markers;

	UINT    selLinkFrom = 0, selLinkTo = 0;   // the link clicked (Delete removes it)
	int     hotLink = -1;                     // the link under the mouse
	UINT    renaming = 0;                     // the task whose name is being edited
	std::wstring tipText;                     // the tooltip's, on tipRect
	RECT    tipRect{};
	GntAccessible* acc = nullptr;             // once a client has asked
};

namespace GntDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	inline void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
		dc.SetBkColor(color);
		dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	extern bool HighContrast;   // set as a paint starts

	// a color between two; in high contrast the second (no shades between the system's colors)
	inline COLORREF Shade(COLORREF a, COLORREF b, int percentB) {
		return HighContrast ? b : Blend(a, b, percentB);
	}

	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);
	void Polygon(WTL::CDCHandle dc, const POINT* points, int count, COLORREF color);
}
