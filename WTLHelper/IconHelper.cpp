#include "pch.h"
#include "IconHelper.h"
#include "DpiHelper.h"
#include <shlobj_core.h>
#include <tuple>

#pragma comment(lib, "comctl32")	// LoadIconWithScaleDown

HICON IconHelper::GetShieldIcon(UINT dpi) {
	return GetStockIcon(SIID_SHIELD, false, dpi);
}

CComPtr<IImageList2> IconHelper::CreateImageList() {
	CComPtr<IImageList2> spImages;
	ImageList_CoCreateInstance(CLSID_ImageList, nullptr, __uuidof(IImageList2), reinterpret_cast<void**>(&spImages));
	ATLASSERT(spImages);
	return spImages;
}

HICON IconHelper::Load(IconId id, int size) {
	if (id.Stock) {
		HICON hIcon = nullptr;
		::LoadIconWithScaleDown(nullptr, MAKEINTRESOURCEW(id.Id), size, size, &hIcon);
		return hIcon;
	}
	if (id.Name)
		return AtlLoadIconImage(id.Name, 0, size, size);
	return AtlLoadIconImage(id.Id, 0, size, size);
}

HICON IconHelper::LoadCached(IconId id, int size) {
	static SRWLOCK lock = SRWLOCK_INIT;
	// by ID, kind and size; a name by its pointer (resource names are literals)
	static std::map<std::tuple<int, bool, PCWSTR, int>, HICON> icons;

	::AcquireSRWLockExclusive(&lock);
	auto& hIcon = icons[{ id.Id, id.Stock, id.Name, size }];
	if (!hIcon)
		hIcon = Load(id, size);
	auto result = hIcon;
	::ReleaseSRWLockExclusive(&lock);
	return result;
}

HICON IconHelper::GetStockIcon(SHSTOCKICONID id, bool big) {
	SHSTOCKICONINFO ssii = { sizeof(ssii) };
	if (FAILED(::SHGetStockIconInfo(id, (big ? SHGSI_LARGEICON : SHGSI_SMALLICON) | SHGSI_ICON, &ssii)))
		return nullptr;

	return ssii.hIcon;
}

//
// SHGetStockIconInfo gives icons of the system DPI only: the icon is extracted from where it lives in the size for dpi
//
HICON IconHelper::GetStockIcon(SHSTOCKICONID id, bool big, UINT dpi) {
	if (dpi == 0)
		return GetStockIcon(id, big);

	SHSTOCKICONINFO ssii = { sizeof(ssii) };
	if (FAILED(::SHGetStockIconInfo(id, SHGSI_ICONLOCATION, &ssii)))
		return nullptr;

	int size = big ? DpiHelper::IconSize(dpi) : DpiHelper::SmallIconSize(dpi);
	HICON hIcon = nullptr;
	if (::SHDefExtractIcon(ssii.szPath, ssii.iIcon, 0, &hIcon, nullptr, size) != S_OK)
		return GetStockIcon(id, big);
	return hIcon;
}
