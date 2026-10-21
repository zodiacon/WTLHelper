#pragma once

#include "LogView.h"

// ---- The text area ----------------------------------------------------------------
//
// A child window, so the scroll bars run beside the text only (a toolbar goes above it). Its messages are
// handled in the control's ALT_MSG_MAP(1).

class LgTextWindow : public ATL::CWindowImpl<LgTextWindow> {
public:
	DECLARE_WND_CLASS_EX(L"LgTextView", CS_DBLCLKS, -1)

	explicit LgTextWindow(CLogViewCtrl* owner) : m_Owner(owner) {}

	BEGIN_MSG_MAP(LgTextWindow)
		CHAIN_MSG_MAP_ALT_MEMBER((*m_Owner), 1)
	END_MSG_MAP()

private:
	CLogViewCtrl* m_Owner;
};
