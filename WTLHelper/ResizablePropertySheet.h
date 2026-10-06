#pragma once

#include <atldlgs.h>
#include "DarkModeSizeGrip.h"

//
// property sheet the user can resize (not for wizards).
// The tab control stretches, the visible buttons stay centered along the bottom,
// and the active page fills the tab's display area (the sheet sizes other pages to it when it shows them).
// A page lays out its own controls (e.g. with CDynamicDialogLayout or CDialogResize).
// TBase is the sheet implementation to build on, so it composes with other sheet impls, e.g.
//   class CMySheet : public CResizablePropertySheetImpl<CMySheet> {};
// A derived class that defines OnSheetInitialized must call the base version.
//
template<typename T, typename TBase = CPropertySheetImpl<T>>
class CResizablePropertySheetImpl : public TBase {
public:
	using TBase::TBase;

	BEGIN_MSG_MAP(CResizablePropertySheetImpl)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		CHAIN_MSG_MAP(TBase)
	END_MSG_MAP()

	//
	// the sizing border must be part of the sheet's dialog template;
	// adding WS_THICKFRAME after creation does not make the sheet resizable
	//
	static int CALLBACK PropSheetCallback(HWND hWnd, UINT msg, LPARAM lParam) {
		if (msg == PSCB_PRECREATE && lParam) {
			auto dlgEx = reinterpret_cast<ATL::_DialogSplitHelper::DLGTEMPLATEEX*>(lParam);
			if (dlgEx->signature == 0xFFFF)
				dlgEx->style |= WS_THICKFRAME | WS_CLIPCHILDREN;
			else
				reinterpret_cast<DLGTEMPLATE*>(lParam)->style |= WS_THICKFRAME | WS_CLIPCHILDREN;
		}
		return TBase::PropSheetCallback(hWnd, msg, lParam);
	}

	void OnSheetInitialized() {
		TBase::OnSheetInitialized();
		ATLASSERT((static_cast<T*>(this)->m_psh.dwFlags & (PSH_WIZARD | PSH_WIZARD97 | PSH_AEROWIZARD)) == 0);
	}

protected:
	//
	// the sheet sizes itself and arranges its buttons only after PSCB_INITIALIZED,
	// so the layout is captured when the sheet is first shown; WM_SIZE is ignored until then
	//
	LRESULT OnShowWindow(UINT, WPARAM wp, LPARAM, BOOL& bHandled) {
		bHandled = FALSE;
		if (wp && m_Controls.empty())
			InitLayout();
		return 0;
	}

	void InitLayout() {
		auto pT = static_cast<T*>(this);
		CRect client;
		pT->GetClientRect(&client);
		m_ClientSize = client.Size();

		CRect window;
		pT->GetWindowRect(&window);
		m_MinSize = window.Size();

		HWND hTab = pT->GetTabControl();
		for (CWindow child = pT->GetWindow(GW_CHILD); child; child = child.GetWindow(GW_HWNDNEXT)) {
			WCHAR className[16];
			// pages are dialogs, they are resized separately
			if (::GetClassName(child, className, _countof(className)) && ::wcscmp(className, L"#32770") == 0)
				continue;
			m_Controls.push_back({ child, child == hTab });
		}

		//
		// remember the page margins inside the tab's display area
		//
		if (CWindow page = pT->GetActivePage(); page) {
			auto display = GetPageArea(CRect());
			CRect rcPage;
			page.GetWindowRect(&rcPage);
			pT->ScreenToClient(&rcPage);
			m_PageMargins.SetRect(rcPage.left - display.left, rcPage.top - display.top,
				display.right - rcPage.right, display.bottom - rcPage.bottom);
		}

		m_SizeGrip.CreateGrip(pT->m_hWnd);

		Layout(client.Size());
	}

	LRESULT OnSize(UINT, WPARAM wp, LPARAM lp, BOOL& bHandled) {
		bHandled = FALSE;
		if (m_Controls.empty() || wp == SIZE_MINIMIZED)
			return 0;

		auto pT = static_cast<T*>(this);
		Layout(CSize(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
		ResizePage(pT->GetActivePage());
		pT->Invalidate();
		return 0;
	}

	LRESULT OnGetMinMaxInfo(UINT, WPARAM, LPARAM lp, BOOL& bHandled) {
		if (m_MinSize.cx > 0) {
			auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
			mmi->ptMinTrackSize.x = m_MinSize.cx;
			mmi->ptMinTrackSize.y = m_MinSize.cy;
		}
		bHandled = FALSE;
		return 0;
	}

private:
	//
	// the tab control stretches; the buttons move with the bottom edge
	// and the visible ones are kept centered horizontally as a group.
	// Positions are taken from the controls' current rectangles, relative to the last client size
	//
	void Layout(CSize const& size) {
		auto pT = static_cast<T*>(this);
		int dx = size.cx - m_ClientSize.cx, dy = size.cy - m_ClientSize.cy;
		m_ClientSize = size;

		std::vector<CRect> rects;
		rects.reserve(m_Controls.size());
		CRect buttons;
		for (auto& c : m_Controls) {
			CRect rc;
			::GetWindowRect(c.hWnd, &rc);
			pT->ScreenToClient(&rc);
			rects.push_back(rc);
			// the sheet itself may not be visible yet, so check the button's own style
			if (!c.Stretch && (::GetWindowLongPtr(c.hWnd, GWL_STYLE) & WS_VISIBLE))
				buttons.UnionRect(&buttons, &rc);
		}
		int buttonsDx = buttons.IsRectEmpty() ? 0 : (size.cx - buttons.Width()) / 2 - buttons.left;

		auto hdwp = ::BeginDeferWindowPos((int)m_Controls.size() + 1);
		for (size_t i = 0; i < m_Controls.size(); i++) {
			auto& c = m_Controls[i];
			auto& rc = rects[i];
			if (c.Stretch) {
				rc.right += dx;
				rc.bottom += dy;
			}
			else {
				rc.OffsetRect(buttonsDx, dy);
			}
			hdwp = ::DeferWindowPos(hdwp, c.hWnd, nullptr, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
		}
		if (m_SizeGrip) {
			CRect rc;
			m_SizeGrip.GetClientRect(&rc);
			hdwp = ::DeferWindowPos(hdwp, m_SizeGrip, nullptr, size.cx - rc.Width(), size.cy - rc.Height(), 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
		::EndDeferWindowPos(hdwp);
	}

	CRect GetPageArea(CRect const& margins) const {
		auto pT = static_cast<T const*>(this);
		CTabCtrl tabs(pT->GetTabControl());
		CRect rc;
		tabs.GetWindowRect(&rc);
		pT->ScreenToClient(&rc);
		tabs.AdjustRect(FALSE, &rc);
		rc.DeflateRect(&margins);
		return rc;
	}

	void ResizePage(HWND hPage) {
		if (hPage == nullptr || m_Controls.empty())
			return;
		auto rc = GetPageArea(m_PageMargins);
		::SetWindowPos(hPage, nullptr, rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	}

	struct ControlInfo {
		HWND hWnd;
		bool Stretch;
	};
	std::vector<ControlInfo> m_Controls;
	CDarkModeSizeGrip m_SizeGrip;
	CSize m_ClientSize, m_MinSize;
	CRect m_PageMargins;
};

class CResizablePropertySheet : public CResizablePropertySheetImpl<CResizablePropertySheet> {
public:
	using CResizablePropertySheetImpl::CResizablePropertySheetImpl;
};
