#pragma once

struct IImageList2;

struct IconHelper {
	static HICON GetStockIcon(SHSTOCKICONID id, bool big = false);
	static HICON GetShieldIcon();
	static CComPtr<IImageList2> CreateImageList();
	static HICON Load(ATL::_U_STRINGorID id, int size);
	// loads an icon once and keeps it for the life of the process; for icons set on windows
	// (WM_SETICON, BM_SETIMAGE), which don't destroy them. The caller must not destroy the icon.
	static HICON LoadCached(UINT id, int size);
};
