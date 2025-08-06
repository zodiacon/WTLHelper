#pragma once

#include "Theme.h"
#include "ThemeHelper.h"

class CCustomHeader :
	public CWindowImpl<CCustomHeader, CHeaderCtrl> {
public:
	BEGIN_MSG_MAP(CCustomHeader)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
	END_MSG_MAP()

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
		if (ThemeHelper::IsDefault()) {
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
			dc.SetTextColor(ThemeHelper::GetCurrentTheme()->TextColor);
			dc.SetBkMode(TRANSPARENT);
			dc.SelectFont(GetFont());
			dc.SelectStockPen(DC_PEN);
			dc.SetDCPenColor(RGB(192, 192, 192));
			std::vector<int> order(GetItemCount());
			GetOrderArray(GetItemCount(), order.data());
			const int sortLen = 4;
			for(auto index : order) {
				GetItemRect(index, &rc);
				GetItem(index, &hdi);
				dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
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
		if (ThemeHelper::IsDefault()) {
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
				dc.FillSolidRect(&rc, ThemeHelper::GetCurrentTheme()->BackColor);
		}
		return 1;
	}

	int m_Width{ 0 };
};

class CCustomHeaderParent :
	public CWindowImpl<CCustomHeaderParent>,
	public CCustomDraw<CCustomHeaderParent> {
public:
	BEGIN_MSG_MAP(CCustomHeaderParent)
		//CHAIN_MSG_MAP(CCustomDraw<CCustomHeaderParent>)
	END_MSG_MAP()

	void Init(HWND hWnd) {
		m_Header.SubclassWindow(hWnd);
	}

	void OnFinalMessage(HWND) override {
		m_Header.Detach();
		delete this;
	}

	DWORD OnPrePaint(int /*idCtrl*/, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_Header) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}

		return CDRF_NOTIFYITEMDRAW;
	}

	DWORD OnItemPrePaint(int /*idCtrl*/, LPNMCUSTOMDRAW cd) {
		if (cd->hdr.hwndFrom != m_Header) {
			SetMsgHandled(FALSE);
			return CDRF_DODEFAULT;
		}

		HDITEM item;
		item.mask = HDI_TEXT | HDI_FORMAT;
		WCHAR text[64];
		item.pszText = text;
		item.cchTextMax = _countof(text);
		ATLVERIFY(m_Header.GetItem((int)cd->dwItemSpec, &item));

		CDCHandle dc(cd->hdc);
		dc.SelectStockPen(WHITE_PEN);
		CRect rc(cd->rc);
		rc.bottom -= 2;
		bool isSortinColumn = item.fmt & (HDF_SORTUP | HDF_SORTDOWN);
		auto color = ThemeHelper::GetCurrentTheme()->BackColor;
		dc.FillSolidRect(&rc, isSortinColumn ? RGB(32, 0, 0) : color);

		if (cd->dwItemSpec != 0) {
			CPen pen;
			pen.CreatePen(PS_SOLID, 1, RGB(64, 64, 64));
			auto hOldPen = dc.SelectPen(pen);
			dc.MoveTo(rc.left, rc.top);
			dc.LineTo(rc.left, rc.bottom);
			dc.SelectPen(hOldPen);
		}
		dc.MoveTo(rc.right, rc.top);
		dc.LineTo(rc.right, rc.bottom);

		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(ThemeHelper::GetCurrentTheme()->TextColor);
		rc = cd->rc;
		rc.left += 6;

		DWORD fmt = DT_LEFT;
		if ((item.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT)
			fmt = DT_RIGHT;
		else if ((item.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER)
			fmt = DT_CENTER;

		rc.DeflateRect(4, 0);
		dc.DrawText(text, -1, &rc, fmt | DT_VCENTER | DT_SINGLELINE);

		// draw sort indicator (if any)
		if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
			auto pt = rc.CenterPoint();
			auto up = item.fmt & HDF_SORTUP;
			pt.y = up ? rc.top : rc.top + 4;
			int offset = up ? 4 : -4;
			dc.MoveTo(pt);
			dc.LineTo(pt.x - offset, pt.y + offset);
			dc.MoveTo(pt);
			dc.LineTo(pt.x + offset, pt.y + offset);
		}
		return CDRF_NOTIFYPOSTPAINT;
	}

	CCustomHeader m_Header;
};
