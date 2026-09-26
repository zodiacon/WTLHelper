#pragma once

#include "DockTypes.h"

namespace WTLDock {

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
