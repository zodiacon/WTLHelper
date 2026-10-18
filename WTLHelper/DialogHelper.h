#pragma once

#include "IconHelper.h"
#include "DpiHelper.h"
#include <vector>
#include <algorithm>

template<typename T>
class CDialogHelper {
public:
	template<typename S, int N, typename U>
	void InitCombo(UINT id, S (&data)[N], U const& selected = U()) {
		InitCombo(id, data, N, selected);
	}

	template<typename S, typename U>
	void InitCombo(UINT id, S const* data, int count, U const& selected = U()) {
		auto dlg = static_cast<T*>(this);
		auto cb = (CComboBox)dlg->GetDlgItem(id);
		int cur = -1;
		for (int i = 0; i < count; i++) {
			auto& item = data[i];
			int n = cb.AddString(item.text);
			cb.SetItemData(n, static_cast<DWORD_PTR>(item.type));
			if (cur < 0 && item.type == selected) {
				cur = n;
			}
		}
		cb.SetCurSel(cur >= 0 ? cur : 0);
	}

	//
	// icon sizes are at 96 DPI, scaled to the DPI of the dialog. The icons set with AdjustOKCancelButtons, AddIconToButton and
	// SetDialogIcon(UINT) are remembered, and loaded again in the sizes for a new DPI by UpdateIconsForDpi, which a dialog
	// gets called on WM_DPICHANGED with CHAIN_MSG_MAP(CDialogHelper<T>) in its message map
	//
	BEGIN_MSG_MAP(CDialogHelper)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
	END_MSG_MAP()

	void AdjustOKCancelButtons(UINT okId, UINT cancelId) {
		auto dlg = static_cast<T*>(this);
		CButton ok(dlg->GetDlgItem(IDOK));
		if (ok) {
			CString text;
			ok.GetWindowText(text);
			if (text.Left(2) != L"  ")
				ok.SetWindowText(L"  " + text);
			SetButtonIcon(IDOK, okId, 16);
		}

		CButton cancel(dlg->GetDlgItem(IDCANCEL));
		if (cancel) {
			cancel.SetWindowText(L"  Cancel");
			SetButtonIcon(IDCANCEL, cancelId, 16);
		}
	}

	bool AddIconToButton(WORD id, WORD icon, int size = 16) {
		auto dlg = static_cast<T*>(this);
		CButton button(dlg->GetDlgItem(id));
		if (button) {
			SetButtonIcon(id, icon, size);
			CString text;
			button.GetWindowText(text);
			if (text.Left(2) != L"  ")
				button.SetWindowText(L"  " + text);
		}
		return (bool)button;
	}

	// icons set on windows aren't destroyed with them, so these come from a cache that loads each one once
	void SetDialogIcon(UINT icon) {
		m_DialogIcon = icon;
		auto dlg = static_cast<T*>(this);
		UINT dpi = DpiHelper::GetDpi(dlg->m_hWnd);
		dlg->SetIcon(IconHelper::LoadCached(icon, DpiHelper::SmallIconSize(dpi)), FALSE);
		dlg->SetIcon(IconHelper::LoadCached(icon, DpiHelper::IconSize(dpi)), TRUE);
	}
	void SetDialogIcon(HICON icon) {
		m_DialogIcon = 0;
		auto dlg = static_cast<T*>(this);
		dlg->SetIcon(icon, FALSE);
		dlg->SetIcon(icon, TRUE);
	}

	// the remembered icons in the sizes for dpi (0: the DPI of the dialog)
	void UpdateIconsForDpi(UINT dpi = 0) {
		auto dlg = static_cast<T*>(this);
		if (dpi == 0)
			dpi = DpiHelper::GetDpi(dlg->m_hWnd);
		for (auto& b : m_ButtonIcons)
			CButton(dlg->GetDlgItem(b.Id)).SetIcon(IconHelper::LoadCached(b.Icon, DpiHelper::Scale(b.Size, dpi)));
		if (m_DialogIcon) {
			dlg->SetIcon(IconHelper::LoadCached(m_DialogIcon, DpiHelper::SmallIconSize(dpi)), FALSE);
			dlg->SetIcon(IconHelper::LoadCached(m_DialogIcon, DpiHelper::IconSize(dpi)), TRUE);
		}
	}

protected:
	LRESULT OnDpiChanged(UINT, WPARAM wp, LPARAM, BOOL& bHandled) {
		UpdateIconsForDpi(LOWORD(wp));
		bHandled = FALSE;
		return 0;
	}

private:
	void SetButtonIcon(UINT id, UINT icon, int size) {
		auto dlg = static_cast<T*>(this);
		CButton button(dlg->GetDlgItem(id));
		button.SetIcon(IconHelper::LoadCached(icon, DpiHelper::Scale(size, button)));
		auto it = std::find_if(m_ButtonIcons.begin(), m_ButtonIcons.end(), [=](auto& b) { return b.Id == id; });
		if (it == m_ButtonIcons.end())
			m_ButtonIcons.push_back({ id, icon, size });
		else
			*it = { id, icon, size };
	}

	struct ButtonIcon {
		UINT Id, Icon;
		int Size;	// at 96 DPI
	};
	std::vector<ButtonIcon> m_ButtonIcons;
	UINT m_DialogIcon{ 0 };
};

