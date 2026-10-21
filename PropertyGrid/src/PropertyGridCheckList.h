#pragma once

// A Flags item's drop-down: a check box per flag, below the value cell. Like a combo box's list it never
// takes the focus (the grid's edit keeps it and passes the keys on) and captures the mouse while open;
// a click outside closes it.

#include "PropertyGrid.h"
#include "PropertyGridModel.h"

class PgCheckList : public ATL::CWindowImpl<PgCheckList> {
public:
	DECLARE_WND_CLASS_EX(L"PropertyGridCheckList", CS_DROPSHADOW, -1)

	explicit PgCheckList(CPropertyGridCtrl& grid) : m_Grid(grid) {}

	BEGIN_MSG_MAP(PgCheckList)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
	END_MSG_MAP()

	// rcCell: the value cell in screen coords; the list opens below it (above, if there's no room)
	void Open(const PgNode* node, LONGLONG mask, const RECT& rcCell);
	void Close();
	bool IsOpen() const;
	void MoveCurrent(int delta);
	void ToggleCurrent();

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);

	int  ItemAt(POINT pt) const;
	void Toggle(int index);

	CPropertyGridCtrl&  m_Grid;
	std::vector<PgEnum> m_Items;   // the flags with nonzero values
	LONGLONG            m_Mask = 0;
	int                 m_Current = 0;
	bool                m_Closing = false;
};
