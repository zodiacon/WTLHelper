#pragma once

#include "IconHelper.h"

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

	void AdjustOKCancelButtons(UINT okId, UINT cancelId) {
		auto dlg = static_cast<T*>(this);
		CButton ok(dlg->GetDlgItem(IDOK));
		if (ok) {
			CString text;
			ok.GetWindowText(text);
			ok.SetWindowText(L"  " + text);
			ok.SetIcon(IconHelper::Load(okId, 16));
		}

		CButton cancel(dlg->GetDlgItem(IDCANCEL));
		if (cancel) {
			cancel.SetWindowText(L"  Cancel");
			cancel.SetIcon(IconHelper::Load(cancelId, 16));
		}
	}

	bool AddIconToButton(WORD id, WORD icon, int size = 16) {
		auto dlg = static_cast<T*>(this);
		CButton button(dlg->GetDlgItem(id));
		if (button) {
			button.SetIcon(IconHelper::Load(icon, size));
			CString text;
			button.GetWindowText(text);
			button.SetWindowText(L"  " + text);
		}
		return (bool)button;
	}

	void SetDialogIcon(UINT icon) {
		auto dlg = static_cast<T*>(this);
		dlg->SetIcon(IconHelper::Load(icon, 16), FALSE);
		dlg->SetIcon(IconHelper::Load(icon, 32), TRUE);
	}
	void SetDialogIcon(HICON icon) {
		auto dlg = static_cast<T*>(this);
		dlg->SetIcon(icon, FALSE);
		dlg->SetIcon(icon, TRUE);
	}
};

