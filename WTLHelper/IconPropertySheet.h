#pragma once

#include <atldlgs.h>

//
// property sheet with full color tab icons.
// The sheet's built-in tab image list (used with PSP_USEICONID) is low color depth,
// so it is replaced with a 32-bit one once the sheet is initialized.
// Pages added after that need nothing special: the sheet adds their icons to the tab's current (32-bit) image list.
// TBase is the sheet implementation to build on, so it composes with other sheet impls, e.g.
//   class CMySheet : public CIconPropertySheetImpl<CMySheet, CResizablePropertySheetImpl<CMySheet>> {};
// A derived class that defines OnSheetInitialized must call the base version.
//
template<typename T, typename TBase = CPropertySheetImpl<T>>
class CIconPropertySheetImpl : public TBase {
public:
	using TBase::TBase;

	//
	// before the sheet exists, the icons must stay in step with the pages
	//
	BOOL AddPage(LPCPROPSHEETPAGE page) {
		if (!TBase::AddPage(page))
			return FALSE;
		if (!this->m_hWnd)
			m_Icons.push_back((page->dwFlags & PSP_USEICONID) ? page->pszIcon : nullptr);
		return TRUE;
	}

	BOOL AddPage(HPROPSHEETPAGE page) {
		if (!TBase::AddPage(page))
			return FALSE;
		if (!this->m_hWnd)
			m_Icons.push_back(nullptr);	// icon (if any) is unknown; the sheet's image is copied
		return TRUE;
	}

	BOOL RemovePage(int index) {
		if (!this->m_hWnd && index >= 0 && index < (int)m_Icons.size())
			m_Icons.erase(m_Icons.begin() + index);
		return TBase::RemovePage(index);
	}

	BOOL RemovePage(HPROPSHEETPAGE page) {
		if (!this->m_hWnd) {
			if (int index = this->GetPageIndex(page); index >= 0 && index < (int)m_Icons.size())
				m_Icons.erase(m_Icons.begin() + index);
		}
		return TBase::RemovePage(page);
	}

	void OnSheetInitialized() {
		TBase::OnSheetInitialized();
		auto pT = static_cast<T*>(this);
		CTabCtrl tabs(pT->GetTabControl());
		CImageList sheetImages(tabs.GetImageList());
		auto size = ::GetSystemMetrics(SM_CXSMICON);
		auto count = tabs.GetItemCount();
		m_Images.Create(size, size, ILC_COLOR32 | ILC_MASK, count, 4);
		for (int i = 0; i < count; i++) {
			TCITEM item{ TCIF_IMAGE };
			tabs.GetItem(i, &item);
			auto icon = i < (int)m_Icons.size() ? m_Icons[i] : nullptr;
			CIcon hIcon;
			if (icon)
				hIcon = AtlLoadIconImage(icon, LR_DEFAULTCOLOR, size, size);
			else if (item.iImage >= 0 && sheetImages)
				hIcon = sheetImages.GetIcon(item.iImage);
			item.iImage = hIcon ? m_Images.AddIcon(hIcon) : -1;
			tabs.SetItem(i, &item);
		}
		tabs.SetImageList(m_Images);
		m_Icons.clear();
	}

private:
	std::vector<PCWSTR> m_Icons;
	CImageListManaged m_Images;
};

class CIconPropertySheet : public CIconPropertySheetImpl<CIconPropertySheet> {
public:
	using CIconPropertySheetImpl::CIconPropertySheetImpl;
};
