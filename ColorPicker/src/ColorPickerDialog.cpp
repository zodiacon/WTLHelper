#include "ColorPicker.h"
#include "ColorPickerModel.h"

#include <algorithm>
#include <vector>

// ColorPickerDialog: a dialog built from a template in memory (a static library has no resources), with the
// picker and two buttons created in WM_INITDIALOG and laid out in pixels at the dialog's DPI.

namespace {
	enum { IDC_PICKER = 100 };

	struct DialogState {
		CPKDIALOG*       dlg;
		CColorPickerCtrl picker;
		HWND             ok = nullptr;
		HWND             cancel = nullptr;
		HFONT            font = nullptr;
		UINT             dpi = 96;
		bool             changed = false;   // the preview was called
		WTL::CBrush      back;              // with the dialog's own colors
	};

	int Scale(const DialogState& st, int dips) {
		return ::MulDiv(dips, st.dpi, 96);
	}

	void UpdateFont(DialogState& st) {
		if (st.font)
			::DeleteObject(st.font);
		NONCLIENTMETRICSW ncm{ sizeof(ncm) };
		::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, st.dpi);
		st.font = ::CreateFontIndirectW(&ncm.lfMessageFont);
		for (HWND button : { st.ok, st.cancel })
			::SendMessage(button, WM_SETFONT, (WPARAM)st.font, TRUE);
	}

	int ButtonRowHeight(const DialogState& st) {
		return Scale(st, 26) + 2 * Scale(st, 10);
	}

	// the window size for a client area showing the picker at its ideal size and the buttons
	SIZE WindowSize(HWND hDlg, const DialogState& st) {
		auto ideal = st.picker.GetIdealSize();
		RECT rc{ 0, 0, ideal.cx, ideal.cy + ButtonRowHeight(st) };
		::AdjustWindowRectExForDpi(&rc, (DWORD)::GetWindowLongPtrW(hDlg, GWL_STYLE), FALSE,
			(DWORD)::GetWindowLongPtrW(hDlg, GWL_EXSTYLE), st.dpi);
		return { rc.right - rc.left, rc.bottom - rc.top };
	}

	void Layout(HWND hDlg, DialogState& st) {
		RECT rc;
		::GetClientRect(hDlg, &rc);
		int row = ButtonRowHeight(st), margin = Scale(st, 10);
		int bw = Scale(st, 80), bh = Scale(st, 26);
		st.picker.SetWindowPos(nullptr, 0, 0, rc.right, std::max(0L, rc.bottom - row), SWP_NOZORDER | SWP_NOACTIVATE);
		int y = rc.bottom - row + (row - bh) / 2;
		::SetWindowPos(st.cancel, nullptr, rc.right - margin - bw, y, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
		::SetWindowPos(st.ok, nullptr, rc.right - margin - 2 * bw - Scale(st, 8), y, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void Preview(DialogState& st, COLORREF color, BYTE alpha) {
		if (st.dlg->pfnPreview) {
			st.dlg->pfnPreview(color, alpha, st.dlg->lParam);
			st.changed = true;
		}
	}

	void SaveCustom(DialogState& st) {
		if (st.dlg->customColors)
			st.picker.GetCustomColors(st.dlg->customColors, CPK_CUSTOMCOLORS);
	}

	BOOL OnInitDialog(HWND hDlg, DialogState& st) {
		auto d = st.dlg;
		st.dpi = ::GetDpiForWindow(hDlg);
		if (d->pszTitle)
			::SetWindowTextW(hDlg, d->pszTitle);

		st.picker.Create(hDlg, ATL::CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | d->pickerStyle, 0, IDC_PICKER);
		if (d->colors)
			st.picker.SetColors(d->colors);
		if (d->palette)
			st.picker.SetPalette(d->palette, d->paletteCount);
		if (d->customColors)
			st.picker.SetCustomColors(d->customColors, CPK_CUSTOMCOLORS);
		if (d->recentColors)
			st.picker.SetRecentColors(d->recentColors, d->recentCount);
		st.picker.SetColor(d->color);
		st.picker.SetAlpha(d->alpha);
		st.picker.SetOriginalColor(d->color, d->alpha);

		st.ok = ::CreateWindowExW(0, WC_BUTTONW, L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			0, 0, 0, 0, hDlg, (HMENU)IDOK, nullptr, nullptr);
		st.cancel = ::CreateWindowExW(0, WC_BUTTONW, L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 0, 0, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
		UpdateFont(st);

		// sized for the picker, centered over the owner (or its monitor), in the work area
		auto size = WindowSize(hDlg, st);
		HWND owner = d->hwndOwner ? ::GetAncestor(d->hwndOwner, GA_ROOT) : nullptr;
		MONITORINFO mi{ sizeof(mi) };
		::GetMonitorInfoW(::MonitorFromWindow(owner ? owner : hDlg, MONITOR_DEFAULTTONEAREST), &mi);
		RECT over = mi.rcWork;
		if (owner && ::IsWindowVisible(owner) && !::IsIconic(owner))
			::GetWindowRect(owner, &over);
		auto at = CpCenterOver(over, size, mi.rcWork);
		::SetWindowPos(hDlg, nullptr, at.x, at.y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
		Layout(hDlg, st);

		st.picker.SetFocus();   // the gradients
		return FALSE;
	}
}

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
	auto st = (DialogState*)::GetWindowLongPtrW(hDlg, DWLP_USER);
	switch (msg) {
		case WM_INITDIALOG:
			::SetWindowLongPtrW(hDlg, DWLP_USER, lParam);
			return OnInitDialog(hDlg, *(DialogState*)lParam);

		case WM_SIZE:
			if (st)
				Layout(hDlg, *st);
			return TRUE;

		case WM_GETMINMAXINFO:
			if (st && st->picker.IsWindow()) {
				auto size = WindowSize(hDlg, *st);
				auto mmi = (MINMAXINFO*)lParam;
				mmi->ptMinTrackSize = { size.cx * 3 / 4, size.cy * 3 / 4 };
			}
			return TRUE;

		case WM_DPICHANGED:
			if (st) {
				st->dpi = HIWORD(wParam);
				UpdateFont(*st);
				auto rc = (const RECT*)lParam;
				::SetWindowPos(hDlg, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			return TRUE;

		case WM_CTLCOLORDLG:
			if (st && st->dlg->colors) {
				if (st->back.IsNull())
					st->back.CreateSolidBrush(st->dlg->colors->crBack);
				return (INT_PTR)st->back.m_hBrush;
			}
			return FALSE;

		case WM_NOTIFY:
		{
			auto nm = (NMCOLORPICKER*)lParam;
			if (st && nm->hdr.idFrom == IDC_PICKER && (nm->hdr.code == CPKN_COLORCHANGING || nm->hdr.code == CPKN_COLORCHANGED))
				Preview(*st, nm->color, nm->alpha);
			return FALSE;
		}

		case WM_COMMAND:
			if (!st)
				break;
			switch (LOWORD(wParam)) {
				case IDOK:
				{
					auto d = st->dlg;
					d->color = st->picker.GetColor();
					d->alpha = st->picker.GetAlpha();
					SaveCustom(*st);
					if (d->recentColors) {
						st->picker.AddRecentColor(d->color);
						d->recentCount = st->picker.GetRecentColors(d->recentColors, CPK_MAXRECENT);
					}
					::EndDialog(hDlg, IDOK);
					return TRUE;
				}
				case IDCANCEL:
					// the eyedropper or a drag in progress takes Escape itself; this is the dialog's
					SaveCustom(*st);
					if (st->changed)
						Preview(*st, st->dlg->color, st->dlg->alpha);
					::EndDialog(hDlg, IDCANCEL);
					return TRUE;
			}
			break;

		case WM_DESTROY:
			if (st && st->font) {
				::DeleteObject(st->font);
				st->font = nullptr;
			}
			break;
	}
	return FALSE;
}

BOOL ColorPickerDialog(CPKDIALOG* pDialog) {
	if (!pDialog)
		return FALSE;

	// DLGTEMPLATE, then no menu, the default class, the title; no controls (they're made in WM_INITDIALOG)
	std::vector<WORD> tmpl(sizeof(DLGTEMPLATE) / sizeof(WORD));
	auto dt = (DLGTEMPLATE*)tmpl.data();
	dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | WS_CLIPCHILDREN;
	dt->cdit = 0;
	tmpl.push_back(0);   // menu
	tmpl.push_back(0);   // class
	for (auto p = L"Color"; *p; p++)
		tmpl.push_back(*p);
	tmpl.push_back(0);

	DialogState st{ pDialog };
	HWND owner = pDialog->hwndOwner;
	INT_PTR result = ::DialogBoxIndirectParamW(::GetModuleHandleW(nullptr), (const DLGTEMPLATE*)tmpl.data(), owner,
		DialogProc, (LPARAM)&st);
	return result == IDOK;
}
