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
