#pragma once

#include "ThemeHelper.h"

template <bool t_bVertical>
class CCustomSplitterWindowT : public CSplitterWindowImpl<CCustomSplitterWindowT<t_bVertical>> {
public:
	DECLARE_WND_CLASS_EX2(_T("WTL_SplitterWindow"), CSplitterWindowT<t_bVertical>, CS_DBLCLKS, COLOR_WINDOW)

	BEGIN_MSG_MAP(CCustomSplitterWindowT)
		CHAIN_MSG_MAP(CSplitterWindowImpl<CCustomSplitterWindowT<t_bVertical>>)
		FORWARD_NOTIFICATIONS()
	END_MSG_MAP()

	CCustomSplitterWindowT() : CSplitterWindowImpl<CCustomSplitterWindowT<t_bVertical>>(t_bVertical) {
		this->m_dwExtendedStyle |= SPLIT_FIXEDBARSIZE;
		this->m_cxySplitBar = 2;
	}

	void DrawSplitterBar(CDCHandle dc) {
		RECT rect = {};
		if (this->GetSplitterBarRect(&rect)) {
			dc.FillRect(&rect, COLOR_3DFACE);

			if ((this->m_dwExtendedStyle & SPLIT_FLATBAR) != 0) {
				RECT rect1 = rect;
				if (t_bVertical)
					rect1.right = rect1.left + 1;
				else
					rect1.bottom = rect1.top + 1;
				dc.FillRect(&rect1, COLOR_WINDOW);

				rect1 = rect;
				if (t_bVertical)
					rect1.left = rect1.right - 1;
				else
					rect1.top = rect1.bottom - 1;
				dc.FillRect(&rect1, COLOR_3DSHADOW);
			}
			else if ((this->m_dwExtendedStyle & SPLIT_GRADIENTBAR) != 0) {
				RECT rect2 = rect;
				if (t_bVertical)
					rect2.left = (rect.left + rect.right) / 2 - 1;
				else
					rect2.top = (rect.top + rect.bottom) / 2 - 1;

				dc.GradientFillRect(rect2, ::GetSysColor(COLOR_3DFACE), ::GetSysColor(COLOR_3DSHADOW), t_bVertical);
			}

			dc.DrawEdge(&rect, EDGE_ETCHED, t_bVertical ? (BF_LEFT | BF_RIGHT) : (BF_TOP | BF_BOTTOM));
		}
	}

};

using CCustomSplitterWindow = CCustomSplitterWindowT<true>;
using CCustomHorSplitterWindow = CCustomSplitterWindowT<false>;
