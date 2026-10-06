#pragma once

#include <atldlgs.h>

//
// property sheet with full color tab icons.
// The sheet's built-in tab image list (used with PSP_USEICONID) is low color depth,
// so it is replaced with a 32-bit one once the sheet is initialized.
// TBase is the sheet implementation to build on, so it composes with other sheet impls, e.g.
//   class CMySheet : public CIconPropertySheetImpl<CMySheet, CResizablePropertySheetImpl<CMySheet>> {};
// A derived class that defines OnSheetInitialized must call the base version.
//
template<typename T, typename TBase = CPropertySheetImpl<T>>
class CIconPropertySheetImpl : public TBase {
public:
	using TBase::TBase;

	BEGIN_MSG_MAP(CIconPropertySheetImpl)
		CHAIN_MSG_MAP(TBase)
	END_MSG_MAP()

	BOOL AddPage(LPCPROPSHEETPAGE page) {
		m_Icons.push_back((page->dwFlags & PSP_USEICONID) ? page->pszIcon : nullptr);
		return TBase::AddPage(page);
	}

	BOOL AddPage(HPROPSHEETPAGE page) {
		// icon (if any) is unknown; the existing image is kept
		m_Icons.push_back(nullptr);
		return TBase::AddPage(page);
	}

	void OnSheetInitialized() {
		TBase::OnSheetInitialized();
		auto pT = static_cast<T*>(this);
		CTabCtrl tabs(pT->GetTabControl());
		CImageList old(tabs.GetImageList());
		auto size = ::GetSystemMetrics(SM_CXSMICON);
		auto count = tabs.GetItemCount();
		m_Images.Create(size, size, ILC_COLOR32 | ILC_MASK, count, 0);
		for (int i = 0; i < count; i++) {
			TCITEM item{ TCIF_IMAGE };
			tabs.GetItem(i, &item);
			auto icon = i < (int)m_Icons.size() ? m_Icons[i] : nullptr;
			if (icon) {
				item.iImage = m_Images.AddIcon(AtlLoadIconImage(icon, LR_DEFAULTCOLOR, size, size));
			}
			else if (item.iImage >= 0 && old) {
				CIcon hIcon(old.GetIcon(item.iImage));
				item.iImage = m_Images.AddIcon(hIcon);
			}
			tabs.SetItem(i, &item);
		}
		tabs.SetImageList(m_Images);
	}

private:
	std::vector<PCWSTR> m_Icons;
	CImageListManaged m_Images;
};

class CIconPropertySheet : public CIconPropertySheetImpl<CIconPropertySheet> {
public:
	using CIconPropertySheetImpl::CIconPropertySheetImpl;
};
