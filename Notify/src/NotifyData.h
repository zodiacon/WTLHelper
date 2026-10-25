#pragma once

// What the info bar and the toasts share: a message as shown (NtfItem), its fonts, its painting and hit testing.

#include "Notify.h"
#include "NotifyModel.h"

enum class NtfPart : uint8_t { None, Button, Close, Link, Check, More };

struct NtfHit {
	int     item = -1;
	NtfPart part = NtfPart::None;
	int     index = -1;        // the button's, or the link's run
};

inline bool operator==(const NtfHit& a, const NtfHit& b) {
	return a.item == b.item && a.part == b.part && a.index == b.index;
}

// a message with what it takes to show it
struct NtfItem {
	NtfMessage m;
	LPARAM lParam = 0;
	NtfRowLayout layout;
	// sliding in (0 to 1) or out (1 to 0)
	ULONGLONG animStart = 0;
	bool appearing = false;
	bool going = false;

	double Shown(ULONGLONG now, UINT duration) const {   // how much of it shows
		if (going)
			return 1.0 - NtfAnimate(animStart, duration, now);
		return appearing ? NtfAnimate(animStart, duration, now) : 1.0;
	}
};

// the fonts a message is drawn in
struct NtfFonts {
	HFONT normal = nullptr;    // the control's, not owned
	HFONT bold = nullptr;
	HFONT link = nullptr;      // underlined
	HFONT icon = nullptr;      // the severity icons' glyphs (Segoe Fluent Icons, or Segoe MDL2 Assets)
	int   textHeight = 16;

	void Create(HFONT base, int iconSize);
	void Destroy();
};

// a message's parts from the user (NTFMESSAGE): parsed and copied
NtfMessage NtfFromApi(const NTFMESSAGE& message);

namespace NtfDraw {
	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color);
	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);

	// the system's colors (WTLHelper's dark mode hooks GetSysColor), as WinUI's InfoBar has them
	NTFCOLORS SystemColors();

	// a text's width in the fonts (bold or not; a link's is the normal one's)
	NtfMeasure Measurer(HDC dc, const NtfFonts& fonts);

	// The message at (x, y); back: what it's drawn on. index: its item's (the hot and pressed parts are found by it).
	void PaintMessage(WTL::CDCHandle dc, const NtfItem& item, int index, int x, int y, COLORREF back, const NTFCOLORS& c,
		const NtfFonts& fonts, HIMAGELIST images, const NtfHit& hot, const NtfHit& pressed, const NtfMetrics& m, int dpi);

	// the part at pt, relative to the message's top left (the item isn't set)
	NtfHit HitMessage(const NtfItem& item, POINT pt, const NtfMetrics& m);
}

// ---- The info bar's state --------------------------------------------------------------------------

struct NtfBarData {
	std::vector<NtfItem> items;                   // the newest first; those going too, until they're gone
	NtfFonts fonts;
	NtfMetrics m;
	bool dirty = true;                            // the rows to be laid out for the width
	int  width = -1;                              // laid out at

	// the stack as shown now: the rows (their items) and the "N more" / "Show fewer" strip
	std::vector<int> rowItems;
	std::vector<int> rowTops, rowHeights;
	RECT strip{};
	int  hidden = 0;                              // in the "N more" strip; 0 with "Show fewer"
	int  height = 0;

	int  maxRows = 3;
	bool showAll = false;                         // the strip clicked: all of them, "Show fewer"
	NtfHit hot, pressed;
	bool tracking = false;
	UINT nextId = 0x8000;
	std::wstring strings[NTFSTR_COUNT];
};

constexpr UINT NtfSlideDuration = 180;   // ms
