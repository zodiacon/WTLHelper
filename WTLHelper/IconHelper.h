#pragma once

struct IImageList2;

struct IconHelper {
	// a small or large icon of the system DPI; the caller destroys it
	static HICON GetStockIcon(SHSTOCKICONID id, bool big = false);
	// a small or large icon of dpi (0: of the system DPI); the caller destroys it
	static HICON GetStockIcon(SHSTOCKICONID id, bool big, UINT dpi);
	// a small icon of dpi (0: of the system DPI); the caller destroys it
	static HICON GetShieldIcon(UINT dpi = 0);
	static CComPtr<IImageList2> CreateImageList();
	static HICON Load(ATL::_U_STRINGorID id, int size);
	// loads an icon once and keeps it for the life of the process; for icons set on windows
	// (WM_SETICON, BM_SETIMAGE), which don't destroy them. The caller must not destroy the icon.
	static HICON LoadCached(UINT id, int size);
};
