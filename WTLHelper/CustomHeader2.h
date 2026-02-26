#pragma once

#include "WTLHelper.h"
#include "DarkMode/DarkModeSubclass.h"

class CCustomHeader2 :
	public CWindowImpl<CCustomHeader2, CHeaderCtrl> {
public:
	BEGIN_MSG_MAP(CCustomHeader2)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (!WTLHelper::IsDarkMode()) {
			bHandled = FALSE;
			return 0;
		}
		CPaintDC dc(m_hWnd);
		if (GetItemCount()) {
			CRect rc;
			HDITEM hdi{ HDI_TEXT | HDI_FORMAT };
			WCHAR text[64];
			hdi.pszText = text;
			hdi.cchTextMax = std::size(text);
			dc.SetTextColor(DarkMode::getTextColor());
			dc.SetBkMode(TRANSPARENT);
			dc.SelectFont(GetFont());
			dc.SelectStockPen(DC_PEN);
			dc.SetDCPenColor(RGB(192, 192, 192));
			std::vector<int> order(GetItemCount());
			GetOrderArray(GetItemCount(), order.data());
			const int sortLen = 4;
			for (auto index : order) {
				GetItemRect(index, &rc);
				GetItem(index, &hdi);
				dc.FillSolidRect(&rc, DarkMode::getCtrlBackgroundColor());
				rc.right -= 6;
				rc.left += 6;
				auto fmt = DT_SINGLELINE | DT_LEFT | DT_VCENTER;
				if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
					fmt |= DT_CENTER;
				else if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
					fmt |= DT_RIGHT;

				dc.DrawText(text, -1, &rc, fmt);
				rc.top += 2;
				if (hdi.fmt & HDF_SORTUP) {
					dc.MoveTo(rc.CenterPoint().x, rc.top);
					dc.LineTo(rc.CenterPoint().x - sortLen, rc.top + sortLen);
					dc.MoveTo(rc.CenterPoint().x, rc.top);
					dc.LineTo(rc.CenterPoint().x + sortLen, rc.top + sortLen);
				}
				else if (hdi.fmt & HDF_SORTDOWN) {
					dc.MoveTo(rc.CenterPoint().x - sortLen, rc.top);
					dc.LineTo(rc.CenterPoint().x, rc.top + sortLen);
					dc.MoveTo(rc.CenterPoint().x + sortLen, rc.top);
					dc.LineTo(rc.CenterPoint().x, rc.top + sortLen);
				}
				rc.top -= 2;
				rc.right += 2;
				dc.MoveTo(rc.right, rc.top);
				dc.LineTo(rc.right, rc.bottom);
			}
		}

		return 0;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (!WTLHelper::IsDarkMode()) {
			bHandled = FALSE;
			return 0;
		}
		DefWindowProc();
		CClientDC dc(*this);
		CRect rc;
		GetClientRect(&rc);
		RECT rcItem;
		if (GetItemCount()) {
			std::vector<int> order(GetItemCount());
			GetOrderArray(GetItemCount(), order.data());
			GetItemRect(order.back(), &rcItem);
			rc.left = rcItem.right;
			if (rc.right > rc.left)
				dc.FillSolidRect(&rc, DarkMode::getCtrlBackgroundColor());
		}
		return 1;
	}

	int m_Width{ 0 };
};
