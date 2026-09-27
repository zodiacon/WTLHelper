#pragma once

#include "DockTypes.h"

namespace WTLDock {

// The colours of an application-wide dark mode: what a dark mode library (WTLHelper's DarkMode::get...Color) says the
// windows, controls and text of the application look like. DockTheme::FromPalette turns it into a theme for the docking
// chrome, so that chrome and content come from one palette.
struct DockPalette {
	COLORREF View;			// the background of views (what content windows have)
	COLORREF Control;		// controls, raised parts
	COLORREF Hot;			// under the mouse
	COLORREF Dialog;		// the background of frames and dialogs
	COLORREF Text;
	COLORREF DarkerText;	// less important text
	COLORREF Edge;			// lines and borders
};

// Colours of the docking chrome (captions, tabs, splitters). The content windows are the application's business.
struct DockTheme {
	bool IsDark;				// a dark theme: window title bars follow
	COLORREF Workspace;			// behind everything; also the splitter gaps
	COLORREF Splitter;
	COLORREF SplitterDragging;
	COLORREF GroupBack;			// group area not covered by a content window
	COLORREF Border;			// thin lines separating caption / tab strip from the content

	COLORREF CaptionActiveBack, CaptionActiveText;
	COLORREF CaptionInactiveBack, CaptionInactiveText;
	COLORREF ButtonHotBack, ButtonGlyph, ButtonGlyphHot;

	COLORREF TabStripBack;
	COLORREF TabActiveBack, TabActiveText, TabActiveAccent;
	COLORREF TabInactiveBack, TabInactiveText;
	COLORREF TabHotBack;

	COLORREF GuideBack, GuideBorder, GuideGlyph, GuideHot;		// the drop targets shown while dragging
	COLORREF PreviewFill, PreviewBorder;						// where the pane would go

	COLORREF BarBack;			// auto-hide bars
	COLORREF BarItemBack, BarItemText;

	static DockTheme Light();
	static DockTheme Dark();
	// A dark theme in the colours of a palette. The palette has no accent (the active caption, the bar under the
	// selected tab, the drop preview), so that is a parameter.
	static DockTheme FromPalette(const DockPalette& palette, COLORREF accent = RGB(0, 122, 204));
};

// Sizes of the docking chrome in device pixels.
struct DockMetrics {
	int CaptionHeight;
	int TabHeight;
	int TabPadding;				// horizontal padding inside a tab
	int TabGap;
	int TabIconGap;				// between a tab's icon / close button and its text
	int TabCloseSize;			// the close button of a tab is square
	int IconSize;
	int ButtonSize;				// caption buttons are square
	int ButtonMargin;
	int TextPadding;			// left padding of caption text
	int SplitterThickness;
	int AutoHideBarThickness;
	SIZE MinGroupSize;

	static DockMetrics ForDpi(int dpi);
	LayoutMetrics ToLayoutMetrics() const;
};

}
