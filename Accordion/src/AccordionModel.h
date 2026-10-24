#pragma once

// The accordion's window-free parts: its sections and their state, the single-expand rule, the layout (fixed,
// fill and fit heights, minimums, scrolling, the expand and collapse animation), hit testing, resizing and
// reordering, and the saved state string. Covered by AccordionTests.

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

// ---- Sections --------------------------------------------------------------------------

enum class AcdHeight {
	Fixed,   // Height DIPs (a user's resize sets it too)
	Fill,    // a share (Weight) of the space the other sections leave
	Fit,     // the content's height (FitHeight), asked of the app
};

struct AcdSection {
	UINT      id = 0;               // the app's, kept in the saved state
	AcdHeight mode = AcdHeight::Fixed;
	int       height = 120;         // Fixed: DIPs
	float     weight = 1;           // Fill: the section's share among the open fill sections
	int       fitHeight = 0;        // Fit: pixels, as the app reported them
	int       minHeight = 0;        // DIPs, the body's least (for Fill, and when resizing)
	bool      expanded = true;      // where it's going
	bool      collapsible = true;   // false: always open, no chevron
	double    progress = 1;         // 0 collapsed .. 1 expanded; animated toward expanded
};

// Expands or collapses one section (a section that isn't collapsible stays open). With singleExpand, expanding
// one collapses the other collapsible sections. Returns the indexes whose state changed, the given one first.
std::vector<size_t> AcdSetExpanded(std::vector<AcdSection>& sections, size_t index, bool expand, bool singleExpand);

// Every section's progress where it's going (no animation).
void AcdSnap(std::vector<AcdSection>& sections);

// Moves each section's progress toward where it's going by elapsed / duration (milliseconds); true while any is
// still moving.
bool AcdAnimate(std::vector<AcdSection>& sections, double elapsed, double duration);

// progress eased in and out, for the layout
double AcdEase(double progress);

// ---- Layout ------------------------------------------------------------------------------

struct AcdMetrics {        // pixels at the window's DPI
	int  headerHeight = 24;
	int  gap = 0;          // between sections
	UINT dpi = 96;
};

struct AcdItem {
	RECT header{};
	RECT body{};           // as it shows now (animated); empty when collapsed
	int  fullHeight = 0;   // the body's height when expanded, at the current size
};

struct AcdLayout {
	std::vector<AcdItem> items;
	int contentHeight = 0; // everything, top to bottom
	int scrollMax = 0;     // the most the content can scroll up
};

// The sections from the top (less scrollPos) down, width wide, in a client clientHeight high. Fixed and fit bodies
// are as high as they say (but not under their minimum); fill bodies share what's left by their weights (but
// not under their minimum, which makes the content scroll). A section opening or closing shows that much of its
// body; a fill section closing gives its space to the other fill sections as it goes.
AcdLayout AcdLayoutSections(const std::vector<AcdSection>& sections, int width, int clientHeight, int scrollPos, const AcdMetrics& metrics);

// a scroll position the layout allows
int AcdClampScroll(int scrollPos, const AcdLayout& layout);

int AcdScale(int dips, UINT dpi);
int AcdUnscale(int pixels, UINT dpi);

// ---- Hit testing ---------------------------------------------------------------------------

enum class AcdPart {
	None,
	Header,
	Body,
	ResizeEdge,   // the bottom edge of an open body, which can be dragged (AcdCanResize)
};

struct AcdHit {
	AcdPart part = AcdPart::None;
	size_t  index = 0;
};

// edge: how close (pixels) to a body's bottom counts as its edge
AcdHit AcdHitTest(const AcdLayout& layout, const std::vector<AcdSection>& sections, POINT pt, int edge);

// A header's buttons: count squares size wide, gap apart, the last ending at right, centered in the header's
// height. Ones that would start left of minLeft are left out (empty rects), from the first.
std::vector<RECT> AcdButtonRects(const RECT& header, int count, int size, int gap, int right, int minLeft);

// ---- Resizing ------------------------------------------------------------------------------
//
// Dragging the bottom edge of an open body. A fixed or fit section becomes fixed at the new height. A fill
// section trades its height with the next open fill section below it (their weights become their heights), so
// it can be resized only if there is one.

bool AcdCanResize(const std::vector<AcdSection>& sections, size_t index);
// height: the body's new height in pixels, as dragged (kept to the minimums); layout: the one the drag started on
void AcdResize(std::vector<AcdSection>& sections, size_t index, int height, const AcdLayout& layout, const AcdMetrics& metrics);

// ---- Reordering ----------------------------------------------------------------------------

// where a header dropped at y goes: the index of the section it goes before (sections.size(): the end)
size_t AcdDropIndex(const AcdLayout& layout, int y);
// moves the section at from to go before the one at before (an AcdDropIndex); returns its new index
size_t AcdMove(std::vector<AcdSection>& sections, size_t from, size_t before);

// ---- Saved state ---------------------------------------------------------------------------
//
// The sections' order, which are open, and their sizes, as text an app keeps in its settings:
// "1;id,e,h;..." with e 1 or 0 and h a fixed height (DIPs) or a fill weight. Loading puts the sections it names
// in its order first, then the others as they were; ids it doesn't know are skipped, and it doesn't change a
// section's mode. Returns false (and changes nothing) for text it can't read.

std::wstring AcdSaveState(const std::vector<AcdSection>& sections);
bool AcdLoadState(std::vector<AcdSection>& sections, std::wstring_view text);
