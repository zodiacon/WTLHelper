#pragma once

// WTLDock themed by WTLHelper's dark mode (WTLHelper::InitDarkMode, SwitchToMode, SetColorTone).
//
// Optional: include it after WTLDockUI.h in an application that uses WTLHelper (its include directory is needed,
// WTLDock itself does not depend on WTLHelper). Then:
//
//   WTLHelper::InitDarkMode(...);                 // at start, before any window is made
//   ...
//   UseDarkModeTheme(m_Dock);                     // once, after the docking window is made
//   ...
//   WTLHelper::SwitchToMode(DarkModeKind::Dark, m_hWnd);      // re-themes the content, and through the message
//                                                             // "ThemeChanged" the docking chrome as well
//   WTLHelper::SetColorTone(ColorTone::Blue, m_hWnd);
//   m_Dock.RefreshTheme();                        // (SetColorTone sends no message)
//
// Content in panes is themed by WTLHelper's hook as it is created, as any window of the application. The chrome of
// the docking area (captions, tabs, splitters, drop markers, tooltips, the window switcher, the floating windows'
// title bars) is drawn by WTLDock in a DockTheme made from the same palette.

#include "WTLDockUI.h"
#include <WTLHelper.h>

namespace WTLDock {

// the palette of the current dark mode
inline DockPalette DockPaletteFromDarkMode() {
	return { DarkMode::getBackgroundColor(), DarkMode::getCtrlBackgroundColor(), DarkMode::getHotBackgroundColor(),
		DarkMode::getDlgBackgroundColor(), DarkMode::getTextColor(), DarkMode::getDarkerTextColor(), DarkMode::getEdgeColor() };
}

// The theme for the current mode: the palette's dark theme when the application is in dark mode, the light theme
// otherwise (Light and Classic). 'accent' colours the active caption, the bar under the selected tab and the drop preview.
inline DockTheme DockThemeFromDarkMode(COLORREF accent = RGB(0, 122, 204)) {
	return WTLHelper::IsDarkMode() ? DockTheme::FromPalette(DockPaletteFromDarkMode(), accent) : DockTheme::Light();
}

// Makes a docking window follow WTLHelper's dark mode: it takes its theme from the mode (now, and when the mode
// is switched), applies the mode to the content of its floating windows, and leaves the theming of the Windows dialog to the
// library's hook.
inline void UseDarkModeTheme(CDockHost& dock, COLORREF accent = RGB(0, 122, 204)) {
	dock.SetStyleDialogs(false);
	dock.OnFloatingWindowThemeChanged = [](HWND frame) {
		DarkMode::setDarkTitleBarEx(frame, true);
		DarkMode::setChildCtrlsTheme(frame);
	};
	dock.SetThemeProvider([accent] { return DockThemeFromDarkMode(accent); });
}

}
