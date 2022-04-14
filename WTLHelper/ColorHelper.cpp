#include "pch.h"
#include "ColorHelper.h"

COLORREF ColorHelper::Lighten(COLORREF color, int amount) {
    return RGB(
        (BYTE)min(255, GetRValue(color) + 255 * amount / 100),
        (BYTE)min(255, GetGValue(color) + 255 * amount / 100),
        (BYTE)min(255, GetBValue(color) + 255 * amount / 100));
}

COLORREF ColorHelper::Darken(COLORREF color, int amount) {
    return RGB(
        (BYTE)max(0, GetRValue(color) - 255 * amount / 100),
        (BYTE)max(0, GetGValue(color) - 255 * amount / 100),
        (BYTE)max(0, GetBValue(color) - 255 * amount / 100));
}

bool ColorHelper::IsSystemThemeDark() {
    CRegKey key;
    key.Open(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", KEY_QUERY_VALUE);
    if (!key)
        return false;

    DWORD dark;
    return key.QueryDWORDValue(L"AppsUseLightTheme", dark) == ERROR_SUCCESS ? !dark : false;
}
