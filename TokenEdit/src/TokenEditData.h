#pragma once

// The control's state, shared by the control's sources.

#include "TokenEdit.h"
#include "TokenEditModel.h"

// ---- The suggestions' list ------------------------------------------------------------------------

struct TkeSuggestionItem {
	std::wstring text, detail, token;
	int    image = -1;
	LPARAM lParam = 0;
};

struct TkeSuggestRow {
	int  index = 0;                               // in TkeData::suggestions
	TkeMatch match;
	bool onDetail = false;                        // the match is in the detail
};

// The matching suggestions, dropped down under the typing area. It doesn't take the activation: the edit keeps the
// focus and passes it the keys (SuggestKey). One window, shown again as the text changes.
class TkeListAccessible;
class TkeBoxAccessible;

class TkeSuggestList : public ATL::CWindowImpl<TkeSuggestList> {
public:
	DECLARE_WND_CLASS_EX(L"TkeSuggestList", CS_DROPSHADOW, -1)

	explicit TkeSuggestList(CTokenEditCtrl* owner) : m_Owner(owner) {
	}

	BEGIN_MSG_MAP(TkeSuggestList)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_GETOBJECT, OnGetObject)
	END_MSG_MAP()
	friend class TkeListAccessible;

	void Disconnect();                           // the accessible object, before the window goes

	// under: the screen rect it drops from (below it, or above if there's no room)
	void Show(std::vector<TkeSuggestRow> rows, const RECT& under);
	void Close();
	bool IsOpen() const {
		return m_Open;
	}
	const std::vector<TkeSuggestRow>& Rows() const {
		return m_Rows;
	}
	int  Hot() const {
		return m_Hot;
	}
	void MoveHot(UINT vk);                       // Up, Down, Page Up, Page Down
	void Wheel(int delta);
	void Invalidate() {
		if (IsWindow())
			::InvalidateRect(m_hWnd, nullptr, FALSE);
	}

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&) {
		return MA_NOACTIVATE;
	}
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
		Wheel(GET_WHEEL_DELTA_WPARAM(wParam));
		return 0;
	}

	int  RowAt(POINT pt) const;                  // client coords; -1 none
	void SetHot(int row);
	void NotifyHot();                            // selection and focus events for the hot row
	void ScrollTo(int top);
	LRESULT OnGetObject(UINT, WPARAM, LPARAM, BOOL&);
	int  Visible() const;                        // rows shown at once
	RECT RowRect(int row) const;                 // client coords; empty if scrolled out

	CTokenEditCtrl* m_Owner;
	std::vector<TkeSuggestRow> m_Rows;
	int  m_RowHeight = 0;
	int  m_Border = 1;                            // 0 where DWM rounds the corners and draws it
	int  m_Top = 0;                               // the first row shown
	int  m_Hot = -1;
	bool m_Open = false;
	POINT m_Mouse{ -1, -1 };                      // screen: a WM_MOUSEMOVE where the mouse already was isn't a move
	TkeListAccessible* m_Acc = nullptr;           // once a client asked
};

// ---- The control's state -----------------------------------------------------------------------------

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

	int  focus = -1;                              // the token the keys are on (with the control focused)

	std::vector<TkeSuggestionItem> suggestions;
	std::unique_ptr<TkeSuggestList> list;
	bool suggestingAll = false;                   // the list shows all of them (Alt+Down with nothing typed)

	TkeHistory history;

	// a token put back into text (EditToken): out of the list meanwhile
	bool editing = false;
	bool editStale = false;                       // the API changed the tokens meanwhile: no undo step for it
	TkeToken edited;                              // as it was
	TkeTokenList::State editBefore;               // the tokens with it, for the undo step

	// dragging tokens: pressed, then past SM_CXDRAG
	enum class Drag : uint8_t { None, Pending, Dragging } drag = Drag::None;
	POINT dragFrom{};
	int  dropIndex = -1;                          // where the selected tokens would go
	int  pendingOnly = -1;                        // a selected token pressed: only it is selected if the press isn't a drag

	std::wstring closeTip;                        // SetCloseTip's; empty: "Remove"
	TkeBoxAccessible* acc = nullptr;              // the box's, once a client asked
	RECT tipRect{};
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
	// text from x on the line's middle, the part [boldStart, boldStart + boldLength) in bold; cut at right. Returns
	// where it ended.
	int  RunText(WTL::CDCHandle dc, const std::wstring& text, int x, const RECT& line, int boldStart, int boldLength,
		HFONT normal, HFONT bold);
}

inline bool operator==(const TkeHit& a, const TkeHit& b) {
	return a.part == b.part && a.index == b.index;
}
