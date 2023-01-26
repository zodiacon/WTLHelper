#pragma once

struct ColorHelper abstract final {
	static COLORREF Lighten(COLORREF color, int amount);
	static COLORREF Darken(COLORREF color, int amount);
	static bool IsSystemThemeDark();
};
