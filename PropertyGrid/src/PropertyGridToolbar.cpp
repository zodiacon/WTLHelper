// The optional toolbar: Categorized and Alphabetical buttons and a search box, and the filter it sets.

#include "PropertyGrid.h"
#include "PropertyGridModel.h"

#include <algorithm>

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static std::wstring WindowText(HWND hWnd) {
	std::wstring text(::GetWindowTextLengthW(hWnd), L'\0');
	::GetWindowTextW(hWnd, text.data(), (int)text.size() + 1);
	return text;
}

// ---- API ------------------------------------------------------------------------------

void CPropertyGridCtrl::ShowToolbar(bool show) {
	m_ShowToolbar = show;
	if (!IsWindow())
		return;   // OnCreate makes it
	if (show)
		CreateToolbar();
	else if (m_hSearch && (::GetFocus() == m_hSearch))
		SetFocus();
	if (m_hSearch)
		::ShowWindow(m_hSearch, show ? SW_SHOWNA : SW_HIDE);
	if (m_hToolTip)
		::SendMessage(m_hToolTip, TTM_ACTIVATE, show, 0);
	Layout();
}

bool CPropertyGridCtrl::IsToolbarVisible() const {
	return m_ShowToolbar;
}

void CPropertyGridCtrl::SetFilter(LPCWSTR text) {
	ApplyFilter(text ? text : L"", false);
}

ATL::CString CPropertyGridCtrl::GetFilter() const {
	return m_Filter.c_str();
}

void CPropertyGridCtrl::ApplyFilter(const std::wstring& filter, bool notify) {
	if (filter == m_Filter)
		return;
	m_Filter = filter;
	if (m_hSearch && WindowText(m_hSearch) != filter) {
		m_SettingSearch = true;
		::SetWindowTextW(m_hSearch, filter.c_str());
		m_SettingSearch = false;
	}
	m_TopRow = 0;
	Rebuild();
	if (m_Selected)
		EnsureRowVisible(SelectedRow());
	if (notify) {
		NMHDR nm{};
		FireNotify(&nm, PRGN_FILTERCHANGED);
	}
}

// ---- Layout -----------------------------------------------------------------------------

int CPropertyGridCtrl::ToolbarHeight() const {
	return m_ShowToolbar ? m_RowHeight + 2 * m_Pad : 0;
}

RECT CPropertyGridCtrl::GetToolButtonRect(int index) const {
	int size = m_RowHeight;
	int left = m_Pad + index * (size + m_Pad / 2);
	return { left, m_Pad, left + size, m_Pad + size };
}

RECT CPropertyGridCtrl::GetSearchRect() const {
	RECT rc;
	GetClientRect(&rc);
	auto last = GetToolButtonRect(1);
	int left = last.right + 2 * m_Pad;
	return { left, last.top, std::max(left, (int)rc.right - m_Pad), last.bottom };
}

void CPropertyGridCtrl::CreateToolbar() {
	if (m_hSearch)
		return;
	m_hSearch = ::CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	if (!m_hSearch)
		return;
	::SendMessage(m_hSearch, WM_SETFONT, (WPARAM)m_hFont, 0);
	::SendMessage(m_hSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
	::SendMessage(m_hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search (Ctrl+F)");
	::SetWindowTextW(m_hSearch, m_Filter.c_str());
	::SetWindowSubclass(m_hSearch, SearchSubclassProc, 1, (DWORD_PTR)this);

	// the buttons' names
	m_hToolTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	if (m_hToolTip) {
		static const wchar_t* const names[] = { L"Categorized", L"Alphabetical" };
		for (int i = 0; i < 2; i++) {
			TTTOOLINFOW ti{ sizeof(ti) };
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = m_hWnd;
			ti.uId = i + 1;
			ti.rect = GetToolButtonRect(i);
			ti.lpszText = const_cast<LPWSTR>(names[i]);
			::SendMessage(m_hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		}
	}
}

void CPropertyGridCtrl::PositionToolbar() {
	if (!m_hSearch || !m_ShowToolbar)
		return;
	auto frame = GetSearchRect();
	int top = frame.top + (frame.bottom - frame.top - m_TextHeight) / 2;
	::SetWindowPos(m_hSearch, nullptr, frame.left + m_Pad, top, std::max(0, (int)(frame.right - frame.left) - 2 * m_Pad), m_TextHeight,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	if (m_hToolTip) {
		for (int i = 0; i < 2; i++) {
			TTTOOLINFOW ti{ sizeof(ti) };
			ti.hwnd = m_hWnd;
			ti.uId = i + 1;
			ti.rect = GetToolButtonRect(i);
			::SendMessage(m_hToolTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
		}
	}
}

// ---- Painting -----------------------------------------------------------------------------

void CPropertyGridCtrl::PaintToolbar(WTL::CDCHandle dc, const PRGCOLORS& c) {
	RECT rc;
	GetClientRect(&rc);
	rc.bottom = ToolbarHeight();
	FillSolid(dc, rc, c.crCategoryBack);

	WTL::CBrush frame, selFrame;
	frame.CreateSolidBrush(c.crGridLine);
	selFrame.CreateSolidBrush(c.crSelBack);
	for (int i = 0; i < 2; i++) {
		auto button = GetToolButtonRect(i);
		bool pressed = (i == 0) == (m_View == PRGV_CATEGORIZED);
		if (pressed) {
			FillSolid(dc, button, c.crSelBackInactive);
			dc.FrameRect(&button, selFrame);
		}
		int size = Scale(12);
		int x = (button.left + button.right - size) / 2;
		int y = (button.top + button.bottom - size) / 2;
		if (i == 0) {
			// two categories: a bar each with an item line below it
			int bar = std::max(1, Scale(2));
			for (int g = 0; g < 2; g++) {
				int top = y + g * Scale(6) + Scale(1);
				RECT header{ x, top, x + size, top + bar };
				RECT item{ x + Scale(4), top + bar + Scale(1), x + size, top + bar + Scale(1) + std::max(1, Scale(1)) };
				FillSolid(dc, header, c.crText);
				FillSolid(dc, item, c.crText);
			}
		}
		else {
			auto old = dc.SelectFont(m_BoldFont);
			dc.SetTextColor(c.crText);
			dc.DrawText(L"AZ", 2, &button, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
			dc.SelectFont(old);
		}
	}

	auto search = GetSearchRect();
	FillSolid(dc, search, c.crBack);
	dc.FrameRect(&search, ::GetFocus() == m_hSearch ? selFrame : frame);
}

// ---- Search box -----------------------------------------------------------------------------

void CPropertyGridCtrl::OnSearchCommand(UINT code) {
	switch (code) {
		case EN_CHANGE:
			if (!m_SettingSearch)
				ApplyFilter(WindowText(m_hSearch), true);
			break;
		case EN_SETFOCUS:
		case EN_KILLFOCUS: {
			RECT rc{ 0, 0, 0, ToolbarHeight() };
			RECT client;
			GetClientRect(&client);
			rc.right = client.right;
			InvalidateRect(&rc);   // the frame shows the focus
			break;
		}
	}
}

LRESULT CALLBACK CPropertyGridCtrl::SearchSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	auto self = (CPropertyGridCtrl*)data;
	switch (msg) {
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, SearchSubclassProc, id);
			break;
		case WM_GETDLGCODE:
			// in a dialog: Enter and Down go to the grid, Escape clears the text (when there is some)
			if (auto m = (MSG*)lParam; m && m->message == WM_KEYDOWN
				&& (m->wParam == VK_RETURN || m->wParam == VK_DOWN || (m->wParam == VK_ESCAPE && ::GetWindowTextLengthW(hWnd) > 0)))
				return ::DefSubclassProc(hWnd, msg, wParam, lParam) | DLGC_WANTMESSAGE;
			break;
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE) {
				if (::GetWindowTextLengthW(hWnd) > 0)
					::SetWindowTextW(hWnd, L"");   // EN_CHANGE clears the filter
				else
					self->SetFocus();
				return 0;
			}
			if (wParam == VK_RETURN || wParam == VK_DOWN) {
				// on to the results: the first property, unless one shown is selected already
				self->SetFocus();
				if (!self->m_Selected || self->SelectedRow() < 0) {
					self->SelectRow(-1, false);
					self->MoveToProperty(1, false);
				}
				return 0;
			}
			break;
		case WM_CHAR:
			if (wParam == VK_RETURN || wParam == VK_ESCAPE)
				return 0;   // no beep
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}
