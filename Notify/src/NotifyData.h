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

	bool Indeterminate() const {   // its progress bar moves
		return !going && m.progressKind == NtfProgress::Indeterminate;
	}

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
	// high contrast while painting: RoundFill frames a fill of one color in the highlight color, and hot buttons too
	extern bool HighContrast;

	const wchar_t* DefaultString(UINT which);   // NTFSTR_*'s

	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF color) {
		return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
	}

	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color);
	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius);

	// the system's colors (WTLHelper's dark mode hooks GetSysColor), as WinUI's InfoBar has them; high contrast: the
	// user's
	NTFCOLORS SystemColors(bool highContrast = false);
	bool IsHighContrast(int forced);                   // forced: 1, 0, or -1 as Windows says

	// a part's tooltip: the close button's name, a link's address when it's one; empty: none
	std::wstring TipText(const NtfItem& item, const NtfHit& part, const std::wstring& close);

	// a text's width in the fonts (bold or not; a link's is the normal one's)
	NtfMeasure Measurer(HDC dc, const NtfFonts& fonts);

	// The message at (x, y); back: what it's drawn on. index: its item's (the hot and pressed parts are found by it).
	void PaintMessage(WTL::CDCHandle dc, const NtfItem& item, int index, int x, int y, COLORREF back, const NTFCOLORS& c,
		const NtfFonts& fonts, HIMAGELIST images, const NtfHit& hot, const NtfHit& pressed, const NtfMetrics& m, int dpi);

	// the part at pt, relative to the message's top left (the item isn't set)
	NtfHit HitMessage(const NtfItem& item, POINT pt, const NtfMetrics& m);

	// the parts the keys go along, in order: the links (the details toggle too), the checkbox, the buttons, the close
	// button (the item isn't set)
	std::vector<NtfHit> FocusParts(const NtfItem& item);
	// where a part is, relative to the message's top left: a link's pieces, else one rect
	std::vector<RECT> PartRects(const NtfItem& item, const NtfHit& part, const NtfMetrics& m);
	void PaintFocus(WTL::CDCHandle dc, const std::vector<RECT>& rects, COLORREF color, int dpi);

	// the message as text (the title, the text, the details), and to the clipboard
	std::wstring CopyText(const NtfItem& item);
	bool ToClipboard(HWND hwnd, const std::wstring& text);
}

// ---- Accessibility (NotifyAccessible.cpp) ------------------------------------------------------------

// what an accessibility object asks its window: children 1..AccCount, 0 the window itself
struct NtfAccSource {
	virtual ~NtfAccSource() = default;
	virtual int  AccCount() = 0;
	virtual std::wstring AccName(int child) = 0;
	virtual std::wstring AccDescription(int child) = 0;
	virtual long AccRole(int child) = 0;
	virtual long AccState(int child) = 0;
	virtual std::wstring AccAction(int child) = 0;
	virtual void AccDoAction(int child) = 0;        // posted: the app's code mustn't run inside the client's call
	virtual RECT AccRect(int child) = 0;            // client coords
	virtual int  AccFocus() = 0;                    // a child, 0 the window, -1 none
	virtual int  AccHitTest(POINT pt) = 0;          // client coords: a child, or 0
};

class NtfAccessible;

// WM_GETOBJECT for the window (the object made the first time), and letting it go when the window does
LRESULT NtfAccObject(NtfAccessible*& slot, NtfAccSource* source, HWND hwnd, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
void NtfAccDisconnect(NtfAccessible*& slot);

// a message's part to accessibility clients, the same in the bar and the toasts (part None: the message itself, an
// alert named by its title and described by its text)
namespace NtfAcc {
	std::wstring Name(const NtfItem& item, const NtfHit& part, const std::wstring& close);
	std::wstring Description(const NtfItem& item, const NtfHit& part);
	long Role(const NtfHit& part);
	long State(const NtfItem& item, const NtfHit& part);   // what the part itself says (checked, linked...)
	std::wstring Action(const NtfItem& item, const NtfHit& part);
}

constexpr UINT WM_NTF_ACCACTION = WM_USER + 120;   // wParam: the message's id; lParam: MAKELPARAM(part, index + 1)

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
	NtfHit focus;                                 // the keys' part (FocusPart resolves it)
	bool tracking = false;
	UINT nextId = 0x8000;
	std::wstring strings[NTFSTR_COUNT];
	NtfAccessible* acc = nullptr;                 // once a client asked
	HWND tip = nullptr;                           // one tool, on the hot part
	std::wstring tipText;
	RECT tipRect{};
};

constexpr UINT NtfSlideDuration = 180;   // ms
