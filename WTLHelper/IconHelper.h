#pragma once

struct IImageList2;

struct IconHelper {
	static HICON GetStockIcon(SHSTOCKICONID id, bool big = false);
	static HICON GetShieldIcon();
	static CComPtr<IImageList2> CreateImageList();
	static HICON Load(ATL::_U_STRINGorID id, int size);
};
