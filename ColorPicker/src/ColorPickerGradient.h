#pragma once

#include "ColorPicker.h"

// ---- The gradients ------------------------------------------------------------------
//
// A child window at the control's top left, so the area and the bars are one tab stop beside the fields (the
// control is a WS_EX_CONTROLPARENT container). Its messages are handled in the control's ALT_MSG_MAP(1); its
// client coordinates are the control's.

class CpGradientWindow : public ATL::CWindowImpl<CpGradientWindow> {
public:
	DECLARE_WND_CLASS_EX(L"CpGradientView", CS_DBLCLKS, -1)

	explicit CpGradientWindow(CColorPickerCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(CpGradientWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 1)
	END_MSG_MAP()

private:
	CColorPickerCtrl* m_Owner;
};

// ---- The swatches -------------------------------------------------------------------------
//
// A child window over the swatch groups, after the fields in the tab order, for the keyboard focus and the keys;
// the mouse goes through it (HTTRANSPARENT) to the control, which handles the clicks. It paints the swatches,
// which the control lays out in its own coordinates. Its messages are handled in the control's ALT_MSG_MAP(2).

class CpSwatchWindow : public ATL::CWindowImpl<CpSwatchWindow> {
public:
	DECLARE_WND_CLASS_EX(L"CpSwatchView", 0, -1)

	explicit CpSwatchWindow(CColorPickerCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(CpSwatchWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 2)
	END_MSG_MAP()

private:
	CColorPickerCtrl* m_Owner;
};
