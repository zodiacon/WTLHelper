#pragma once

// The control's state, shared by the control's sources.

#include "TokenEdit.h"
#include "TokenEditModel.h"

struct TkeData {
	TkeTokenList tokens;
	std::wstring separators = L",;";
	TkeMetrics m;
	TkeLayout layout;
	bool measure = true;                          // the tokens' text widths to be found again
	bool dirty = true;                            // the layout
	int  editAt = -1;                             // the typing area's place among the tokens; -1: after the last
	int  minLines = 1, maxLines = 4;
	int  scroll = 0;                              // pixels, the content's top above the client's
	int  maxTokens = 0;
	std::wstring cue;
	bool cueShown = false, cueApplied = false;    // UpdateCue's

	TkeHit hot;
	TkeHit pressed;                               // a close button pressed: released on it, the token goes
	bool tracking = false;                        // TrackMouseEvent
	bool settingText = false;                     // the edit's text set, not typed: no tokenizing
	bool tokenizing = false;
	std::vector<UINT> lastSelection;              // the ids TKEN_SELCHANGED last told

	TkeHistory history;
};

// ---- Drawing helpers ------------------------------------------------------------------------------

namespace TkeDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color);
	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);
	void Cross(WTL::CDCHandle dc, const RECT& rc, int size, COLORREF color, int width);
}

inline bool operator==(const TkeHit& a, const TkeHit& b) {
	return a.part == b.part && a.index == b.index;
}
