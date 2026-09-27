#pragma once

#include "DockTypes.h"
#include <initializer_list>
#include <string>
#include <string_view>

namespace WTLDock {

//
// The texts that the docking windows show and say: menu items, tooltips, the Windows dialog, the window switcher and
// the names that screen readers get. All of them are English by default; an application translates them by setting
// the ones it wants (SetDockText) or by loading a table from JSON (LoadDockTexts). The table is per process and meant
// to be set up once at start; it is not thread safe. (The names of panes and what your application puts in menus
// through OnBuildPaneMenu are yours.)
//
// A text may contain {0}, {1}, ... where DockText(id, args) puts the arguments in: "Close {0}". Menu items keep their
// & accelerator characters.
//
#define WTLDOCK_TEXTS(X) \
	/* tab and caption context menu */ \
	X(MenuClose, L"&Close") \
	X(MenuCloseOthers, L"Close All &But This") \
	X(MenuCloseAll, L"Close &All Tabs") \
	X(MenuDock, L"&Dock") \
	X(MenuFloat, L"Floa&t") \
	X(MenuAutoHide, L"&Auto Hide") \
	X(MenuPinTab, L"&Pin Tab") \
	X(MenuUnpinTab, L"Un&pin Tab") \
	X(MenuNewHorizontalGroup, L"New &Horizontal Tab Group") \
	X(MenuNewVerticalGroup, L"New &Vertical Tab Group") \
	X(MenuMoveToNextGroup, L"Move to &Next Tab Group") \
	X(MenuMoveToPreviousGroup, L"Move to &Previous Tab Group") \
	/* tooltips of the buttons */ \
	X(TipClose, L"Close") \
	X(TipDock, L"Dock") \
	X(TipAutoHide, L"Auto Hide") \
	X(TipWindowPosition, L"Window Position") \
	X(TipUnpin, L"Unpin") \
	X(TipShowOpenTabs, L"Show open tabs") \
	X(TipScrollTabsLeft, L"Scroll tabs left") \
	X(TipScrollTabsRight, L"Scroll tabs right") \
	/* what screen readers are told */ \
	X(AccDockingArea, L"Docking area") \
	X(AccDocuments, L"Documents") \
	X(AccTabList, L"Tab list") \
	X(AccCloseTab, L"Close {0}") \
	X(AccUnpinTab, L"Unpin {0}") \
	X(AccModified, L"Modified") \
	X(AccPinned, L"Pinned") \
	X(AccPreview, L"Preview") \
	X(AccWithModified, L"{0} (modified)") \
	X(AccWithPinned, L"{0} (pinned)") \
	X(AccWithPreview, L"{0} (preview)") \
	X(AccAutoHiddenItem, L"{0} (auto hidden {1})") \
	X(AccActionPress, L"Press") \
	X(AccActionSwitch, L"Switch") \
	X(AccActionOpen, L"Open") \
	X(AccActionShow, L"Show") \
	/* sides */ \
	X(SideLeft, L"left") \
	X(SideRight, L"right") \
	X(SideTop, L"top") \
	X(SideBottom, L"bottom") \
	/* the Windows dialog and its list */ \
	X(DialogTitle, L"Windows") \
	X(DialogIncludeToolWindows, L"Include &tool windows") \
	X(DialogActivate, L"&Activate") \
	X(DialogSave, L"&Save") \
	X(DialogCloseWindows, L"&Close Window(s)") \
	X(DialogClose, L"C&lose") \
	X(ColumnName, L"Name") \
	X(ColumnType, L"Type") \
	X(ColumnState, L"State") \
	X(ColumnModified, L"Modified") \
	X(Yes, L"Yes") \
	X(TypeDocument, L"Document") \
	X(TypeToolWindow, L"Tool window") \
	X(StateOpen, L"Open") \
	X(StateOpenPinned, L"Open, pinned") \
	X(StateOpenPreview, L"Open, preview") \
	X(StateDocked, L"Docked") \
	X(StateDockedAt, L"Docked {0}") \
	X(StateAutoHidden, L"Auto-hidden") \
	X(StateAutoHiddenAt, L"Auto-hidden {0}") \
	X(StateFloating, L"Floating") \
	X(StateHidden, L"Hidden") \
	/* the window switcher */ \
	X(NavigatorFiles, L"Active Files") \
	X(NavigatorToolWindows, L"Active Tool Windows") \
	X(NavigatorFooter, L"{0} - {1}") \
	X(NavigatorFooterModified, L"{0} (modified) - {1}") \
	/* errors that reach the application */ \
	X(ErrorStateFileUnreadable, L"the state file cannot be read") \
	X(ErrorNoDefaultLayout, L"no default layout has been captured") \
	X(ErrorNoSuchLayout, L"there is no layout with that name")

enum class Str : int {
#define WTLDOCK_TEXT_ENUM(name, text) name,
	WTLDOCK_TEXTS(WTLDOCK_TEXT_ENUM)
#undef WTLDOCK_TEXT_ENUM
	Count
};

// the current text
const std::wstring& DockText(Str id);
// the text with {0}, {1}, ... replaced by the arguments
std::wstring DockText(Str id, std::initializer_list<std::wstring> args);
// "left", "right", "top" or "bottom"
const std::wstring& DockSideText(DockSide side);

// the name of a text in the JSON table ("MenuClose") and its English original
const wchar_t* DockTextName(Str id);
const wchar_t* DockTextDefault(Str id);

// Sets one text; an empty text puts the English one back.
void SetDockText(Str id, std::wstring text);
void ResetDockTexts();
// Sets the texts in a JSON object {"MenuClose": "&Schliessen", ...}. Names that are not known are ignored, values that
// are not strings too. Returns false (changing nothing) if the JSON is not an object.
bool LoadDockTexts(std::string_view json, std::wstring* error = nullptr);
// All the texts as a JSON object, to start a translation from.
std::string DumpDockTexts(bool defaults = true);

}
