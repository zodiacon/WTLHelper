#pragma once

#include "CommandPalette.h"

// ---- The list ---------------------------------------------------------------------------
//
// A child window, so its scroll bar runs beside the rows only (the search box is above). Its messages are handled in
// the control's ALT_MSG_MAP(2).

class CmpListWindow : public ATL::CWindowImpl<CmpListWindow> {
public:
	DECLARE_WND_CLASS_EX(L"CmpList", CS_DBLCLKS, -1)

	explicit CmpListWindow(CCommandPaletteCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(CmpListWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 2)
	END_MSG_MAP()

private:
	CCommandPaletteCtrl* m_Owner;
};
