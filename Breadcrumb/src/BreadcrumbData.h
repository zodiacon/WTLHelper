#pragma once

// The control's state and its drop-down list window, shared by the control's sources.

#include "Breadcrumb.h"
#include "BreadcrumbModel.h"

// ---- Drop-down list ----------------------------------------------------------------------------------

struct BrcRow {
	std::wstring text;
	int  image = -1;
	bool bold = false;
	bool separator = false;
	int  tag = -1;             // the control's: what picking it does
};

// A list dropped down from the bar (a segment's children, the folded segments). It doesn't take the activation: the
// control keeps the focus and passes it the keys, and the list takes the mouse capture as a menu does, so a click
// anywhere else closes it. One window, shown again for each list.
class BrcDropList : public ATL::CWindowImpl<BrcDropList> {
public:
	DECLARE_WND_CLASS_EX(L"BrcDropList", CS_DROPSHADOW, -1)

	explicit BrcDropList(CBreadcrumbCtrl* owner) : m_Owner(owner) {
	}

	BEGIN_MSG_MAP(BrcDropList)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnButtonDown)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnButtonDown)
		MESSAGE_HANDLER(WM_MBUTTONDOWN, OnButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_CANCELMODE, OnCancelMode)
	END_MSG_MAP()

	// under: the screen rect it drops from (below it, or above if there's no room); pressed: the mouse button is down
	// (from the press that opened it: released on a row, that row is picked)
	void Show(std::vector<BrcRow> rows, const RECT& under, bool pressed);
	void Close();                                // hidden; the control is told (DropClosed)
	bool IsOpen() const {
		return m_Open;
	}
	bool KeyDown(UINT vk);                       // the keys the control gets meanwhile; true if used
	void Char(wchar_t ch);                       // type to find a row
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
	LRESULT OnButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
		Wheel(GET_WHEEL_DELTA_WPARAM(wParam));
		return 0;
	}
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCancelMode(UINT, WPARAM, LPARAM, BOOL&);

	int  Scale(int dips) const;
	int  RowAt(POINT pt) const;                  // client coords; -1 none
	RECT RowRect(int row) const;                 // client coords, scrolled
	RECT ThumbRect() const;                      // empty if it doesn't scroll
	bool Pickable(int row) const;
	void SetHot(int row, bool scroll);
	void ScrollTo(int y);
	void Move(int from, int step);               // the next pickable row from from, step rows a time

	CBreadcrumbCtrl* m_Owner;
	std::vector<BrcRow> m_Rows;
	std::vector<int> m_Y;                         // each row's top, unscrolled; one more: the total
	int  m_RowHeight = 0, m_SeparatorHeight = 0;
	int  m_ImageX = 0, m_TextX = 0, m_Scrollbar = 0;
	int  m_Border = 1;                            // 0 where DWM rounds the corners and draws it
	int  m_Height = 0;                            // the rows' area shown
	int  m_Scroll = 0;
	int  m_Hot = -1;
	int  m_Pressed = -1;                          // the row pressed in the list
	bool m_FromOpener = false;                    // the opening press is still down
	bool m_Open = false;
	bool m_Closing = false;
	int  m_ThumbGrab = -1;                        // the thumb dragged: the mouse's offset into it
	POINT m_Mouse{};                              // screen: a WM_MOUSEMOVE where the mouse already was isn't a move
	std::wstring m_Typed;
	ULONGLONG m_TypedTick = 0;
};

// ---- The control's state -----------------------------------------------------------------------------

struct BrcChild {
	std::wstring text, name;
	int    image = -1;
	UINT   flags = 0;
	LPARAM lParam = 0;
};

struct BrcButton {
	UINT   id = 0;
	int    image = -1;
	UINT   glyph = BRCGLYPH_NONE;
	std::wstring tip;
	UINT   state = 0;
};

struct BrcData {
	std::vector<BrcSegment> segments;
	std::wstring separators = L"\\";
	std::vector<BrcButton> buttons;

	BrcMetrics m;
	BrcLayout layout;
	bool measure = true;                          // the text widths to be found again
	bool dirty = true;                            // the layout

	BrcHit hot;
	BrcHit pressed;                               // a part pressed (Text, Action): released on it, it runs
	bool tracking = false;                        // TrackMouseEvent

	// the open list: what dropped it, and the children it shows
	std::unique_ptr<BrcDropList> list;
	BrcHit opened;
	int  openedIndex = -2;                        // the segment whose children are listed (-1 the root); -2 none
	std::vector<BrcChild> children;
	std::vector<BrcChild>* collecting = nullptr;  // while BRCN_GETCHILDREN is sent
	HWND focusBefore = nullptr;                   // given back when the list closes
	bool switching = false;                       // one list closed for another to open: the focus stays
	bool hotBox = false;                          // the mouse in the box: its frame drawn hot
};

// the list's tags: a folded segment's index, or a child's (BrcTagChild + its index)
constexpr int BrcTagChild = 0x10000;

// ---- Drawing helpers ------------------------------------------------------------------------------

namespace BrcDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color);
	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);
	// a chevron centered on (cx, cy), pointing the way of an arrow key
	void Chevron(WTL::CDCHandle dc, int cx, int cy, int size, UINT vk, COLORREF color, int width);
	// an arrow (a shaft and a head) centered on (cx, cy), pointing the way of an arrow key
	void Arrow(WTL::CDCHandle dc, int cx, int cy, int size, UINT vk, COLORREF color, int width);
	void Glyph(WTL::CDCHandle dc, UINT glyph, const RECT& rc, int size, COLORREF color, int width);
}

inline bool operator==(const BrcHit& a, const BrcHit& b) {
	return a.part == b.part && a.index == b.index;
}
