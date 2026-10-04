#include "pch.h"
#include "IconHelper.h"

HICON IconHelper::GetShieldIcon() {
	return GetStockIcon(SIID_SHIELD);
}

CComPtr<IImageList2> IconHelper::CreateImageList() {
	CComPtr<IImageList2> spImages;
	ImageList_CoCreateInstance(CLSID_ImageList, nullptr, __uuidof(IImageList2), reinterpret_cast<void**>(&spImages));
	ATLASSERT(spImages);
	return spImages;
}

HICON IconHelper::Load(ATL::_U_STRINGorID id, int size) {
	return AtlLoadIconImage(id, 0, size, size);
}

HICON IconHelper::LoadCached(UINT id, int size) {
	static SRWLOCK lock = SRWLOCK_INIT;
	static std::map<std::pair<UINT, int>, HICON> icons;

	::AcquireSRWLockExclusive(&lock);
	auto& hIcon = icons[{ id, size }];
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
