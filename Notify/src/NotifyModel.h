#pragma once

// The info bar's and the toasts' window-free parts: a message's text with links (parsed from a little markup), word
// wrapping it with the links' places, a message's layout as an info bar's row or a toast (the icon, the title, the
// text, the buttons beside or under it, the close button, the details, the progress bar, the "don't show again"
// checkbox), the info bar's stack of rows, where toasts go in a corner, the timer that dismisses a toast (paused while
// the mouse is over it), and the animations' easing. Sizes are in pixels; the controls scale the metrics for their DPI.
// Covered by NotifyTests.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ---- Text with links ----------------------------------------------------------------------------

struct NtfRun {
	std::wstring text;
	std::wstring link;         // the link's id; empty: plain text
	bool bold = false;

	bool IsLink() const {
		return !link.empty();
	}
};

// '<a id="x">here</a>' is a link with the id "x" ('<a>here</a>': the text is the id); &lt; &gt; &amp; and &quot; are
// the characters; anything else (a '<' that starts no link, a link never closed) stays text.
std::vector<NtfRun> NtfParseLinks(std::wstring_view markup);
std::wstring NtfPlainText(const std::vector<NtfRun>& runs);

using NtfMeasure = std::function<int(std::wstring_view text, bool bold)>;   // a text's width

// a part of a run on one line
struct NtfPiece {
	int    run = 0;
	size_t start = 0, length = 0;  // in the run's text
	int    x = 0, line = 0, width = 0;
};

struct NtfTextLayout {
	std::vector<NtfPiece> pieces;
	int  lines = 0;
	int  width = 0;            // the widest line's, trailing spaces left out
	bool truncated = false;    // lines past maxLines were dropped
};

// The runs wrapped at spaces to width ('\n' breaks a line; a word wider than width is broken where it must). maxLines:
// 0 for all.
NtfTextLayout NtfWrap(const std::vector<NtfRun>& runs, int width, const NtfMeasure& measure, int maxLines = 0);

// The link run under (x, y), relative to the text's top left; -1 if none.
int NtfLinkAt(const NtfTextLayout& layout, const std::vector<NtfRun>& runs, int x, int y, int lineHeight);

// ---- Messages ------------------------------------------------------------------------------------

enum class NtfSeverity : uint8_t { Info, Success, Warning, Error };
enum class NtfProgress : uint8_t { None, Value, Indeterminate };
enum class NtfStyle : uint8_t { Bar, Toast };

struct NtfMetrics {
	int pad = 8;               // a row's or a toast's edges to its content
	int icon = 16;
	int gap = 8;               // between parts
	int lineHeight = 18;       // a line of text
	int buttonHeight = 24;
	int buttonPad = 12;        // a button's text, each side
	int buttonMin = 72;
	int buttonGap = 6;
	int close = 24;            // the close button, square
	int progress = 4;          // the progress bar's height
	int check = 14;            // the checkbox's box
	int minText = 200;         // narrower than this beside the buttons: the buttons go under the text
	int maxDetailsLines = 12;
	int more = 22;             // the info bar's "N more" strip
	int toastWidth = 360;
	int toastMargin = 12;      // a toast to the owner's edges
	int toastGap = 8;          // between toasts

	NtfMetrics Scaled(int dpi) const;
};

struct NtfButton {
	UINT id = 0;
	std::wstring text;
};

struct NtfMessage {
	UINT id = 0;
	NtfSeverity severity = NtfSeverity::Info;
	int  image = -1;           // the app's icon; -1: the severity's
	std::wstring title;
	std::vector<NtfRun> runs;  // the text
	std::vector<NtfButton> buttons;
	bool closable = true;
	std::wstring checkText;    // the checkbox's ("Don't show again"); empty: none
	bool checked = false;
	std::wstring details;      // shown under the text when expanded; empty: no toggle
	bool expanded = false;
	NtfProgress progressKind = NtfProgress::None;
	int  progress = 0;         // 0 to 100
	UINT timeout = 0;          // a toast's, in ms; 0: it stays
};

// the details toggle's link (the text's last run)
inline constexpr wchar_t NtfDetailsLink[] = L"\x1" L"details";

struct NtfRowLayout {
	int  height = 0;
	RECT icon{};
	RECT close{};              // empty without one
	RECT text{};               // the title, the text and the details toggle
	std::vector<NtfRun> textRuns;
	NtfTextLayout textLayout;
	RECT details{};            // empty unless expanded
	std::vector<NtfRun> detailsRuns;
	NtfTextLayout detailsLayout;
	RECT progress{};
	RECT check{};              // the checkbox and its text
	RECT checkBox{};
	std::vector<RECT> buttons;
	bool buttonsInline = false;    // beside the text (an info bar's, when there's room)
};

// A message laid out in width, from (0, 0). An info bar's row has the title in bold before the text and its buttons
// beside the text when that leaves it minText; a toast has the title on a line of its own and the buttons under the
// text, at the right. toggle: the details toggle's text ("Show details" or "Hide details", as m.expanded).
NtfRowLayout NtfLayoutMessage(const NtfMessage& m, NtfStyle style, int width, const NtfMetrics& mt, const NtfMeasure& measure,
	std::wstring_view toggle);

// ---- The info bar's stack ------------------------------------------------------------------------

struct NtfStackLayout {
	std::vector<int> tops;     // the rows shown, from the top
	int  hidden = 0;           // the rows past them
	RECT more{};               // the "N more" strip; empty if none are hidden
	int  height = 0;
};

// The rows (newest first) one under the other with a separator line between, at most maxRows (0: all), then a strip
// saying how many more there are.
NtfStackLayout NtfLayoutStack(const std::vector<int>& heights, int maxRows, int width, int moreHeight, int separator = 1);

// ---- Toasts ----------------------------------------------------------------------------------------

enum class NtfCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

// The toasts (newest first) in area, width wide: the newest in the corner, the older ones stacked away from it. Those
// that don't fit get empty rects (they wait); the newest is always placed. maxShown: 0 for as many as fit.
std::vector<RECT> NtfPlaceToasts(const RECT& area, NtfCorner corner, const std::vector<int>& heights, int width, int margin,
	int gap, int maxShown = 0);

// A toast's time: it runs out unless paused (the mouse over the toast).
class NtfTimer {
public:
	void Start(ULONGLONG now, UINT duration);    // 0: it never runs out
	void Pause(ULONGLONG now);
	void Resume(ULONGLONG now);
	bool Paused() const {
		return m_Paused;
	}
	UINT Remaining(ULONGLONG now) const;          // UINT_MAX if it never runs out
	bool Expired(ULONGLONG now) const;

private:
	UINT m_Duration = 0;
	ULONGLONG m_Used = 0;                         // before the current run
	ULONGLONG m_RunStart = 0;
	bool m_Paused = false;
};

// ---- Animation -------------------------------------------------------------------------------------

// how far an animation that began at start is at now, eased out (fast, then slowing), 0 to 1
double NtfAnimate(ULONGLONG start, UINT duration, ULONGLONG now);
