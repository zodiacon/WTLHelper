#pragma once

// Token edit: a box of tokens ("tags", recipients) with typing after them, as an e-mail's To: field. The tokens flow
// left to right and wrap; the box grows to SetLineLimits' maximum (TKEN_HEIGHTCHANGED tells the parent to lay its
// window out again) and scrolls past it. The user types in a real Edit placed after the tokens (or among them, where
// the caret was clicked); a separator (",;" by default, SetSeparators), Enter or leaving the box makes what's typed a
// token, and pasted text is split the same way, quotes keeping a separator in a token ("Smith, John"). Each new
// token goes through TKEN_TOKENADDING, which may refuse it (its text goes back to be fixed), rewrite it, or mark it
// invalid (red, a wavy underline). A token can have an icon from the image list, its own colors, a tooltip and a close
// button; clicking tokens selects them (Ctrl, Shift), and Delete or Backspace removes the selected ones. While the
// user types, the app's suggestions that match drop down (AddSuggestion, TKEN_GETSUGGESTIONS); Up and Down go through
// them, Enter or Tab picks one. The arrow keys go from the typed text into the tokens and along them (Shift extends
// the selection); Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste tokens as text. A double click or F2 puts a token back
// into text to edit (Escape puts it back as it was), dragging tokens reorders them, and Ctrl+Z / Ctrl+Y undo and redo
// the user's changes to the tokens. The API follows the common controls: C-style structures and WM_NOTIFY codes
// (TKEN_*) sent to the parent; the API's own changes notify nothing (and clear the undo).

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlctrls.h>
#include <atlgdi.h>

#include <memory>
#include <string>
#include <vector>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_TOKENEDIT[] = L"WC_TokenEdit";

// ---- Styles --------------------------------------------------------------------

#define TKES_NOCLOSE         0x0001   // no close buttons on the tokens
#define TKES_NODUPLICATES    0x0002   // a token whose text is there already (ignoring case) isn't added again
#define TKES_READONLY        0x0004   // no typing, nothing removed; the tokens can still be selected and copied
#define TKES_SUGGESTINSIDE   0x0008   // suggestions match the typed text anywhere, not only at a word's start
#define TKES_NOREORDER       0x0010   // tokens can't be dragged
#define TKES_NOEDITING       0x0020   // tokens can't be put back into text (double click, F2)

// ---- Tokens ----------------------------------------------------------------------

#define TKETF_TEXT           0x0001   // TKETOKEN's mask, for SetToken
#define TKETF_TIP            0x0002
#define TKETF_IMAGE          0x0004
#define TKETF_COLORS         0x0008
#define TKETF_FLAGS          0x0010
#define TKETF_PARAM          0x0020
#define TKETF_ALL            0x003F

#define TKEF_INVALID         0x0001   // drawn red, with a wavy underline
#define TKEF_LOCKED          0x0002   // no close button; the user can't remove it
#define TKEF_SELECTED        0x0004

struct TKETOKEN {
	UINT     mask = TKETF_ALL;        // SetToken's fields
	UINT     id = 0;                  // the control's; InsertToken: 0 for a new one
	LPCWSTR  pszText = nullptr;
	LPCWSTR  pszTip = nullptr;        // the tooltip; nullptr: the text when it's cut
	int      iImage = -1;             // in the image list
	COLORREF crBack = CLR_DEFAULT;    // CLR_DEFAULT: the control's
	COLORREF crText = CLR_DEFAULT;    // CLR_DEFAULT: the control's, or what reads on crBack
	UINT     flags = 0;               // TKEF_*
	LPARAM   lParam = 0;
};

// ---- Suggestions -------------------------------------------------------------------
//
// The app's list: those that match what's typed (at the text's start, then a word's, by the text or else the detail)
// drop down, leaving out those already tokens. Picking one makes its token (pszToken, else pszText) with its image and
// lParam, through TKEN_TOKENADDING (TKEA_SUGGESTION) as typed text does.

struct TKESUGGESTION {
	LPCWSTR  pszText = nullptr;       // shown, and matched
	LPCWSTR  pszDetail = nullptr;     // shown dimmed after it, and matched ("ada@example.com")
	LPCWSTR  pszToken = nullptr;      // the token's text; nullptr: pszText
	int      iImage = -1;
	LPARAM   lParam = 0;
};

// ---- Colors ----------------------------------------------------------------------

struct TKECOLORS {
	COLORREF crBack;                  // the box
	COLORREF crBorder;
	COLORREF crAccent;                // the box's focus line, and selected tokens' frame
	COLORREF crText;                  // what's typed
	COLORREF crCue;                   // the cue banner
	COLORREF crTokenBack;
	COLORREF crTokenText;
	COLORREF crTokenHot;              // a token under the mouse
	COLORREF crSelectedBack;
	COLORREF crSelectedText;
	COLORREF crInvalid;               // an invalid token's text and underline
	COLORREF crInvalidBack;
};

// ---- Notifications -------------------------------------------------------------

#define TKEN_FIRST           (0U - 3300U)   // outside the common controls ranges
#define TKEN_TOKENADDING     (TKEN_FIRST - 0)   // NMTKETOKEN; the user's text becomes a token: non-zero refuses it
#define TKEN_TOKENADDED      (TKEN_FIRST - 1)   // NMTKETOKEN
#define TKEN_TOKENREMOVING   (TKEN_FIRST - 2)   // NMTKETOKEN; the user removes a token: non-zero keeps it
#define TKEN_TOKENREMOVED    (TKEN_FIRST - 3)   // NMTKETOKEN; the token is gone (id and text as it was)
#define TKEN_CHANGED         (TKEN_FIRST - 4)   // NMHDR; the user changed the tokens (after the notifications above)
#define TKEN_SELCHANGED      (TKEN_FIRST - 5)   // NMHDR; the selected tokens changed
#define TKEN_HEIGHTCHANGED   (TKEN_FIRST - 6)   // NMHDR; GetIdealHeight changed (lines, DPI, font): lay the window out again
#define TKEN_GETSUGGESTIONS  (TKEN_FIRST - 7)   // NMTKESUGGEST; the typed text changed: the app may change the list
#define TKEN_TOKENEDITING    (TKEN_FIRST - 8)   // NMTKETOKEN; the user puts a token back into text: non-zero refuses
#define TKEN_TOKENSMOVED     (TKEN_FIRST - 9)   // NMHDR; the user dragged tokens to another place (then TKEN_CHANGED)

// where a TKEN_TOKENADDING's text comes from
#define TKEA_TYPED           0        // a separator typed, or Enter
#define TKEA_FOCUS           1        // the box lost the focus with text typed
#define TKEA_PASTED          2
#define TKEA_COMMIT          3        // CommitText
#define TKEA_SUGGESTION      4        // a suggestion picked (its image and lParam already set)
#define TKEA_EDITED          5        // an edited token (the first piece has the old token's image, colors, tip and
                                      // lParam; the edit left empty removes it: TKEN_TOKENREMOVED)

// TKEN_TOKENADDING: the app may change pszText (to a string of its own that lives until the notification returns),
// the image, the colors, the flags (TKEF_INVALID, TKEF_LOCKED), the tip and lParam. A refused text goes back into the
// typing area, selected, for the user to fix.
struct NMTKETOKEN {
	NMHDR    hdr;
	UINT     id;                      // 0 while adding
	int      index;                   // where it is (adding: where it goes)
	UINT     source;                  // TKEA_*, while adding
	LPCWSTR  pszText;
	LPCWSTR  pszTip;
	int      iImage;
	COLORREF crBack;
	COLORREF crText;
	UINT     flags;
	LPARAM   lParam;
};

// ---- Control class --------------------------------------------------------------------------

struct TkeData;
// While it's sent the app may ClearSuggestions and AddSuggestion those for the text (from a search of its own); the
// control then matches them. With nothing typed (Alt+Down), pszText is empty and all of them are shown.
struct NMTKESUGGEST {
	NMHDR    hdr;
	LPCWSTR  pszText;                 // what's typed, trimmed
};

struct TkeHit;
struct TkeToken;
class TkeSuggestList;

class CTokenEditCtrl : public ATL::CWindowImpl<CTokenEditCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_TokenEdit", CS_DBLCLKS, -1)

	CTokenEditCtrl();
	~CTokenEditCtrl();
	friend class TkeSuggestList;

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CTokenEditCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_ENABLE, OnEnable)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_SETFONT, OnSetFont)
		MESSAGE_HANDLER(WM_GETFONT, OnGetFont)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_COMMAND, OnCommand)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
		MESSAGE_HANDLER(WM_COPY, OnCopy)
		MESSAGE_HANDLER(WM_CUT, OnCut)
		MESSAGE_HANDLER(WM_PASTE, OnPaste)
		MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
	ALT_MSG_MAP(1)   // the typing area
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnEditSysKeyDown)
		MESSAGE_HANDLER(WM_CHAR, OnEditChar)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnEditGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnEditSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnEditKillFocus)
		MESSAGE_HANDLER(WM_PASTE, OnEditPaste)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
	END_MSG_MAP()

	// ---- Tokens ------------------------------------------------------------------------------------
	//
	// Tokens are known by id, the control's. The API's changes send no notifications (and refuse a duplicate with
	// TKES_NODUPLICATES).

	UINT    InsertToken(const TKETOKEN* token, int index = -1);   // -1: last. The new id, 0 if refused (mask ignored)
	UINT    AddToken(LPCWSTR text, int image = -1, LPARAM lParam = 0);
	BOOL    SetToken(const TKETOKEN* token);                      // the fields in mask, by id
	BOOL    GetToken(UINT id, TKETOKEN* token) const;             // pszText and pszTip valid until the token changes
	BOOL    RemoveToken(UINT id);
	void    RemoveAll();
	int     GetTokenCount() const;
	UINT    GetTokenId(int index) const;                          // 0 if out of range
	int     GetTokenIndex(UINT id) const;                         // -1 if there's no such token
	UINT    FindToken(LPCWSTR text) const;                        // ignoring case; 0 if none
	BOOL    MoveToken(UINT id, int index);                        // index: where it ends up
	BOOL    EnsureVisible(UINT id);                               // scrolls to it

	// ---- Selection -----------------------------------------------------------------------------------

	BOOL    SelectToken(UINT id, BOOL select = TRUE);
	void    SelectAll(BOOL select = TRUE);
	int     GetSelectedCount() const;
	int     GetSelection(UINT* ids, int count) const;             // the selected ids in order; how many there are

	// ---- Text ----------------------------------------------------------------------------------------

	// All the tokens as text: joined with the first separator and a space, quoted where needed. The length with the
	// terminator; text may be nullptr.
	int     GetText(LPWSTR text, int cch) const;
	// The tokens replaced by text split at the separators (no notifications); what's typed stays.
	void    SetText(LPCWSTR text);
	void    SetSeparators(LPCWSTR separators);                    // default ",;"; line breaks always separate
	// what's typed and not a token yet
	int     GetEditText(LPWSTR text, int cch) const;
	void    SetEditText(LPCWSTR text);
	BOOL    CommitText();                                         // what's typed into tokens (TKEN_TOKENADDING, TKEA_COMMIT)
	HWND    GetEditControl() const;
	void    SetCueBanner(LPCWSTR text);                           // shown while there are no tokens and nothing typed
	void    SetMaxTokens(int count);                              // 0: no limit; past it typing stops making tokens

	// ---- Suggestions ---------------------------------------------------------------------------------

	BOOL    AddSuggestion(const TKESUGGESTION* suggestion);
	BOOL    AddSuggestion(LPCWSTR text, LPCWSTR detail = nullptr, int image = -1, LPARAM lParam = 0);
	void    ClearSuggestions();
	BOOL    ShowSuggestions(BOOL show = TRUE);                    // for what's typed (all of them if nothing is); FALSE: none match
	bool    IsSuggesting() const;

	// ---- Clipboard -----------------------------------------------------------------------------------
	//
	// The selected tokens as text (TkeJoin: the first separator and a space, quoted where needed); pasting splits text
	// into tokens at the typing area's place, as typing does. WM_COPY, WM_CUT and WM_PASTE do the same.

	BOOL    Copy();                                               // FALSE: nothing selected
	BOOL    Cut();                                                // the user's: TKEN_TOKENREMOVING for each
	BOOL    Paste();

	// ---- Editing and undo ----------------------------------------------------------------------------

	// The token's text into the typing area at its place, selected, as a double click does (without TKEN_TOKENEDITING);
	// the token is out of the list meanwhile.
	BOOL    EditToken(UINT id);
	bool    IsEditingToken() const;
	BOOL    Undo();
	BOOL    Redo();
	bool    CanUndo() const;
	bool    CanRedo() const;
	void    ClearUndo();
	void    SetCloseTip(LPCWSTR text);                            // the close buttons' tooltip; nullptr: "Remove"

	// ---- Appearance and size -------------------------------------------------------------------------
	//
	// The image list holds small icons (16x16 at 96 DPI); its size sets the icons' (the app gives one for the DPI on
	// WM_DPICHANGED). The font is the message font at the window's DPI; WM_SETFONT replaces it.

	void    SetImageList(HIMAGELIST images);                      // the control doesn't destroy it
	void    SetColors(const TKECOLORS* colors);                   // nullptr: from the system colors (follows WTLHelper dark mode)
	TKECOLORS GetColors() const;
	void    SetLineLimits(int minLines, int maxLines);            // the box's height in lines; maxLines 0: no limit. Default 1, 4
	int     GetIdealHeight() const;                               // for the lines the tokens take now at the window's width

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEnable(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetFont(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCommand(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColorEdit(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditSysKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditChar(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditKillFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEditPaste(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCut(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaste(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);

	int     Scale(int dips) const;
	void    UpdateFonts();
	void    UpdateMetrics();
	void    Changed(bool measure);                                // the layout (and the text widths) to be found again
	void    EnsureLayout();
	void    UpdateCue();
	void    NotifyHeight();
	LRESULT FireNotify(NMHDR* pnm, UINT code) const;
	void    NotifyChanged();                                      // TKEN_CHANGED
	void    NotifySelection();                                    // TKEN_SELCHANGED, if the selection changed
	TkeHit  HitTest(POINT pt);                                    // client coords
	void    SetHot(const TkeHit& hit);
	bool    ReadOnly() const;
	bool    Duplicate(const std::wstring& text, int except = -1) const;
	void    ClearHistory();
	void    ScrollTo(int y);
	void    ScrollToShow(const RECT& rc);                         // content coords

	// removing (the user's): TKEN_TOKENREMOVING for each, then removed; true if any went
	bool    RemoveByUser(std::vector<size_t> indexes);

	// typing (TokenEditInput.cpp)
	std::wstring EditText() const;
	void    SetTyped(const std::wstring& text, bool selectAll);  // no tokenizing for it
	void    CaretToEnd();
	// what's typed into tokens: all of it (final), or what's before the last separator
	void    Tokenize(UINT source, bool final);
	// pieces through TKEN_TOKENADDING into tokens at the edit's place; returns the refused ones
	std::vector<std::wstring> AddTyped(std::vector<TkeToken> tokens, UINT source);
	void    LeaveTyping();                                        // the focus left the control: what's typed becomes a token
	void    PlaceEdit();
	void    FocusEdit();
	void    SetEditAt(int index);                                 // where the typing area sits among the tokens (-1: last)
	int     EditWidth() const;                                    // what's typed, measured, with room for the caret

	// suggestions (TokenEditSuggest.cpp)
	void    UpdateSuggestions(bool all);                          // as typed; all: everything (nothing typed)
	void    CloseSuggestions();
	void    PickSuggestion(int index);                            // in m_Data->suggestions
	bool    SuggestKey(UINT vk);                                  // the edit's key while they're shown; true if used

	// the keyboard (TokenEditKeys.cpp)
	bool    KeyDown(UINT vk);                                     // the control's (tokens selected)
	bool    EditKey(UINT vk);                                     // the edit's, at its ends
	void    FocusToken(int index, bool extend);                   // selects it (extend: from the anchor) and gives it the keys
	void    FocusEditAt(bool start);                              // the typing area, the caret at its start or end
	bool    SetClipboardText(const std::wstring& text);

	// editing, dragging, undo, tooltips (TokenEditEditing.cpp)
	bool    StartEdit(int index, bool user);                      // user: TKEN_TOKENEDITING first
	void    EndTokenEdit(bool cancel);                            // cancel: back as it was; else the edit left empty removes it
	void    AfterRestore();                                       // undone or redone
	void    CancelDrag();
	void    Drop();
	void    UpdateTip();
	std::wstring SelectedText() const;

	// painting (TokenEditPaint.cpp)
	TKECOLORS PaintColors() const;
	void    Paint(WTL::CDCHandle dc);
	void    PaintToken(WTL::CDCHandle dc, int index, const TKECOLORS& c, int dy);

	std::unique_ptr<TkeData> m_Data;
	ATL::CContainedWindowT<WTL::CEdit> m_Edit;
	WTL::CBrush m_EditBrush;
	COLORREF m_EditBrushColor = CLR_INVALID;
	HIMAGELIST m_Images = nullptr;

	TKECOLORS m_Colors{};
	bool    m_SystemColors = true;

	HFONT   m_hFont = nullptr;
	HFONT   m_hBold = nullptr;                                    // the suggestions' matched text
	HWND    m_hTip = nullptr;
	std::wstring m_TipText;
	bool    m_OwnFont = true;
	UINT    m_Dpi = 96;
	int     m_TextHeight = 16;
	int     m_LastHeight = 0;                                     // the height last told (TKEN_HEIGHTCHANGED)
};
