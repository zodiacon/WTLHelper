#pragma once

struct IImageList2;

//
// An icon to load: one of the app's resources by ID (an int: IDI_FOLDER) or by name (a string), or one of the system's
// stock icons (winuser.h's IDI_WARNING, IDI_ERROR, IDI_INFORMATION, IDI_QUESTION, IDI_SHIELD, IDI_APPLICATION: values made
// with MAKEINTRESOURCE). Implicit, so a table entry can be either: { ID_REFRESH, IDI_REFRESH } or { ID_WARN, IDI_WARNING }.
// A literal 0 takes the int constructor (an exact match), so it's still "no icon". A MAKEINTRESOURCE of the app's own ID
// passed as a string would be taken for a stock icon: pass the app's IDs as numbers.
//
struct IconId {
	constexpr IconId(int id = 0) noexcept : Id(id) {}
	IconId(PCWSTR resource) noexcept {
		if (IS_INTRESOURCE(resource)) {
			Id = (int)(ULONG_PTR)resource;
			Stock = true;
		}
		else
			Name = resource;
	}
	explicit operator bool() const noexcept {
		return Id != 0 || Name != nullptr;
	}

	int Id{ 0 };			// the app's resource ID, or the stock icon's
	bool Stock{ false };	// a system stock icon
	PCWSTR Name{ nullptr };	// the app's resource by name
};

struct IconHelper {
	// a small or large icon of the system DPI; the caller destroys it
	static HICON GetStockIcon(SHSTOCKICONID id, bool big = false);
	// a small or large icon of dpi (0: of the system DPI); the caller destroys it
	static HICON GetStockIcon(SHSTOCKICONID id, bool big, UINT dpi);
	// a small icon of dpi (0: of the system DPI); the caller destroys it
	static HICON GetShieldIcon(UINT dpi = 0);
	static CComPtr<IImageList2> CreateImageList();
	// an icon of size x size: the app's (AtlLoadIconImage), or a stock icon (LoadIconWithScaleDown, which scales the
	// system's large image down, so it's crisp at any DPI); the caller destroys it
	static HICON Load(IconId id, int size);
	// loads an icon once and keeps it for the life of the process; for icons set on windows
	// (WM_SETICON, BM_SETIMAGE), which don't destroy them. The caller must not destroy the icon.
	static HICON LoadCached(IconId id, int size);
};
