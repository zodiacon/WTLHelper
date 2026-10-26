#pragma once

// The token edit's window-free parts: the tokens (ids, selection, moving, removing), splitting typed or pasted text
// into tokens at the separators (with quotes) and joining them back, laying the tokens out in wrapping lines with the
// typing area among them, the box's height and scrolling, hit testing, where a dragged token would drop, going a line
// up or down, matching suggestions to the typed text, and undo. Sizes are in pixels; the control scales the metrics for
// its DPI. Covered by TokenEditTests.

#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// ---- Tokens -------------------------------------------------------------------------------------

struct TkeToken {
	std::wstring text;
	std::wstring tip;          // the tooltip; empty: the text, if it's cut
	UINT     id = 0;           // TkeTokenList's, unique
	int      image = -1;
	COLORREF back = CLR_DEFAULT;
	COLORREF fore = CLR_DEFAULT;
	bool     invalid = false;  // drawn red with a wavy underline
	bool     locked = false;   // no close button; the user can't remove it
	bool     selected = false;
	LPARAM   lParam = 0;

	// measured by the control (TkeMeasure)
	int      textWidth = 0;
};

enum class TkeSelect : uint8_t {
	Only,                      // a click: just this one
	Toggle,                    // Ctrl+click
	Extend,                    // Shift+click: the anchor to it, nothing else
	ExtendAdd,                 // Ctrl+Shift+click: the anchor to it added
};

class TkeTokenList {
public:
	size_t Size() const {
		return m_Tokens.size();
	}
	bool Empty() const {
		return m_Tokens.empty();
	}
	const TkeToken& operator[](size_t i) const {
		return m_Tokens[i];
	}
	TkeToken& operator[](size_t i) {
		return m_Tokens[i];
	}
	const std::vector<TkeToken>& Tokens() const {
		return m_Tokens;
	}
	std::vector<TkeToken>& Tokens() {
		return m_Tokens;
	}

	// at is clamped to the end; the token keeps its id unless it's 0 or taken. Returns the index.
	size_t Insert(size_t at, TkeToken token);
	void   Remove(size_t index);
	void   Clear();
	int    Find(UINT id) const;                                  // -1 if none
	int    FindText(std::wstring_view text, bool ignoreCase = true) const;

	// Moves the tokens at indexes (any order, no duplicates) as a block before the token that was at to (Size(): to
	// the end), keeping their order. Returns the block's new first index.
	size_t Move(std::vector<size_t> indexes, size_t to);
	size_t MoveSelected(size_t to) {
		return Move(Selection(), to);
	}
	// removes the selected tokens that aren't locked; returns how many went
	size_t RemoveSelected();

	// ---- Selection

	void   Select(size_t index, TkeSelect how);
	void   SelectAll(bool select = true);
	std::vector<size_t> Selection() const;
	size_t SelectedCount() const;
	int    Anchor() const {
		return m_Anchor;
	}
	void   SetAnchor(int anchor) {
		m_Anchor = anchor;
	}

	// ---- Undo

	struct State {
		std::vector<TkeToken> tokens;
		UINT nextId = 1;
		int  anchor = -1;
	};
	State Save() const;
	void  Restore(const State& state);

private:
	UINT NewId();

	std::vector<TkeToken> m_Tokens;
	UINT m_NextId = 1;
	int  m_Anchor = -1;
};

// ---- Text ---------------------------------------------------------------------------------------

// Text split at any of separators (and line breaks, always) outside double quotes into trimmed pieces, empty ones
// dropped. A piece that is all one quoted string loses its quotes ("a, b" is a, b), "" in it being one quote; quotes
// anywhere else stay as typed ("Smith, John" <j@x>). With rest given, what follows the last separator isn't a piece:
// it's still being typed, and goes to rest untrimmed.
std::vector<std::wstring> TkeSplit(std::wstring_view text, std::wstring_view separators, std::wstring* rest = nullptr);

// Tokens' texts joined with joiner ("; "): a text with a separator, a quote, a line break or space at either end is
// quoted, so TkeSplit gives it back.
std::wstring TkeJoin(const std::vector<std::wstring>& texts, std::wstring_view separators, std::wstring_view joiner);

// whether text has a separator outside quotes (or a line break): typing it ends a token
bool TkeHasSeparator(std::wstring_view text, std::wstring_view separators);

// ---- Layout -------------------------------------------------------------------------------------

struct TkeMetrics {
	int padding = 3;           // the box's inside, each side
	int tokenHeight = 22;
	int gap = 4;               // between tokens on a line
	int lineGap = 3;           // between lines
	int textPad = 8;           // a token's text, at an end with nothing else
	int icon = 16;
	int iconPad = 4;           // the icon's left, and the icon to the text, and the text to the close button
	int close = 16;            // the close button's square
	int closePad = 3;          // the close button's right
	int minEdit = 60;          // the typing area at the end of the tokens, at the least
	int midEdit = 8;           // the typing area among the tokens, at the least
	int minToken = 40;         // a token cut to fit a line is no narrower

	TkeMetrics Scaled(int dpi) const;
};

using TkeMeasure = std::function<int(std::wstring_view)>;    // a text's width

void TkeMeasureTokens(std::vector<TkeToken>& tokens, const TkeMeasure& measure);

struct TkeTokenBox {
	RECT rc{};                 // the token's
	RECT icon{};               // empty if none
	RECT text{};
	RECT close{};              // empty if none
	int  line = 0;
	bool cut = false;          // the text doesn't fit
};

struct TkeLayout {
	std::vector<TkeTokenBox> tokens;
	RECT edit{};               // the typing area
	int  editLine = 0;
	int  editAt = 0;           // the edit sits before tokens[editAt]
	int  lines = 1;
	int  height = 0;           // the content's, padding included
};

struct TkeLayoutInput {
	int  width = 0;            // the box's client width
	int  editAt = -1;          // the edit's place among the tokens; -1: after the last
	int  editWidth = 0;        // the typed text's width (with room for the caret)
	bool closeButtons = true;  // tokens that aren't locked have one
	bool icons = true;         // tokens with an image show it
};

// The tokens flow left to right and wrap; a token wider than a line is cut to it. The edit after the last token takes
// the rest of its line, at least minEdit (else it starts the next line); among the tokens it's as wide as its text,
// at least midEdit.
TkeLayout TkeLayoutTokens(const std::vector<TkeToken>& tokens, const TkeLayoutInput& in, const TkeMetrics& m);

int TkeTokenWidth(const TkeToken& token, bool closeButton, bool icon, const TkeMetrics& m);

// the line's top, padding included
int TkeLineTop(int line, const TkeMetrics& m);

// The box's height (the client's) for lines, kept within [minLines, maxLines] (maxLines 0: no limit).
int TkeBoxHeight(int lines, int minLines, int maxLines, const TkeMetrics& m);

// the scroll position (pixels) that shows [top, bottom) in a view of viewHeight, moving as little as it can,
// clamped to the content
int TkeScrollToShow(int scroll, int top, int bottom, int viewHeight, int contentHeight);
int TkeClampScroll(int scroll, int viewHeight, int contentHeight);

// ---- Hit testing --------------------------------------------------------------------------------

enum class TkePart : uint8_t { None, Token, Close, Edit };

struct TkeHit {
	TkePart part = TkePart::None;
	int index = -1;            // the token's
};

TkeHit TkeHitTest(const TkeLayout& layout, POINT pt);

// Where a token dropped (or the caret clicked) at pt goes, in [0, tokens]: before the first token on pt's line (the
// nearest line) whose middle is right of pt, else after that line's last.
int TkeDropIndex(const TkeLayout& layout, POINT pt);

// the insertion mark for a drop index: a thin rect in the gap before that token (or after the last)
RECT TkeDropMark(const TkeLayout& layout, int index, const TkeMetrics& m);

// The item on the line above (dir < 0) or below whose middle is nearest index's; -1 if there's no such line. Index
// tokens.size() is the edit, both ways.
int TkeLineNeighbor(const TkeLayout& layout, int index, int dir);

// ---- Suggestions --------------------------------------------------------------------------------

enum class TkeMatchKind : uint8_t { None, Prefix, Word, Inside };

struct TkeMatch {
	TkeMatchKind kind = TkeMatchKind::None;
	int start = 0, length = 0; // the matched text in the candidate
};

// typed (trimmed) ignoring case at the candidate's start, else at a word's (after a space or punctuation, or a
// lower to upper case change), else anywhere if inside
TkeMatch TkeMatchText(std::wstring_view typed, std::wstring_view candidate, bool inside = false);

struct TkeSuggestion {
	int index = 0;             // in the candidates
	TkeMatch match;
};

// The candidates that match typed, prefixes first, then words, then inside; each kind in the candidates' order;
// texts in exclude (ignoring case) left out; at most count.
std::vector<TkeSuggestion> TkeFilterSuggestions(std::wstring_view typed, const std::vector<std::wstring>& candidates,
	const std::vector<std::wstring>& exclude, bool inside = false, size_t count = 50);

// ---- Undo ---------------------------------------------------------------------------------------

class TkeHistory {
public:
	explicit TkeHistory(size_t limit = 100) : m_Limit(limit) {
	}
	void Push(TkeTokenList::State before, TkeTokenList::State after);
	bool Undo(TkeTokenList& tokens);
	bool Redo(TkeTokenList& tokens);
	bool CanUndo() const {
		return m_Done > 0;
	}
	bool CanRedo() const {
		return m_Done < m_Changes.size();
	}
	void Clear() {
		m_Changes.clear();
		m_Done = 0;
	}

private:
	struct Change {
		TkeTokenList::State before, after;
	};
	std::vector<Change> m_Changes;
	size_t m_Done = 0;
	size_t m_Limit;
};

// ---- Drawing ------------------------------------------------------------------------------------

// A wavy underline from left to right around y: points period apart, alternately amplitude above and below.
std::vector<POINT> TkeWave(int left, int right, int y, int amplitude, int period);
