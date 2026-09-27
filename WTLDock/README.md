# WTLDock

A Visual Studio style docking framework for [WTL](https://sourceforge.net/projects/wtl/): tool windows that dock,
tab, float, auto-hide and remember where they were, plus a document area with tab groups.

Any window can be a pane: a tree view, a list, an edit control, a view of your own. Nothing needs to derive from a
framework class.

* Tool windows and documents, tabs, splitters, floating windows, auto-hide bars with a sliding flyout
* Drag and drop docking with drop markers and a preview, from tabs, captions and floating window title bars: onto a
  tab strip, beside a group, at a window edge or into an auto-hide bar (hold Ctrl to only float)
* Documents in several tab groups, floating documents, pinned tabs, preview tabs, coloured tabs, modified marks
* Scrolling tab strips (arrows, wheel, tab list) or multiple rows of tabs
* Save and restore the whole arrangement (JSON), named layouts, reset to a default layout
* Window switcher (Ctrl+Tab), Windows dialog, keyboard focus in the tab strips, tooltips
* Light and dark theme, per-monitor DPI (also per floating window), MSAA accessibility, translatable texts

Requirements: Windows 10 1703 or later (per-monitor v2 DPI), Visual Studio 2022 or later with C++20, WTL 10.

## Contents

- [Adding it to your project](#adding-it-to-your-project)
- [A minimal application](#a-minimal-application)
- [Panes](#panes)
- [Placing panes](#placing-panes)
- [Content windows](#content-windows)
- [Closing, and what your application is told](#closing-and-what-your-application-is-told)
- [Saving and restoring the layout](#saving-and-restoring-the-layout)
- [Menus](#menus)
- [Documents](#documents)
- [Theme and DPI](#theme-and-dpi)
- [Keyboard](#keyboard)
- [Translating the framework's texts](#translating-the-frameworks-texts)
- [Other options](#other-options)
- [Testing](#testing)

## Adding it to your project

The library is a static library, `WTLDock/WTLDock.vcxproj`, in the WTLHelper solution.

1. Add the project to your solution and reference it from your application (**Add > Reference**), or link
   `WTLDock.lib` yourself (`bin\<platform>\<configuration>\WTLDock.lib`).
2. Add `WTLDock\include` (and WTL's include directory) to **Additional Include Directories**.
3. Compile as C++20 (`/std:c++20`), Unicode.
4. Define the Windows version before any Windows or ATL header, for example in your precompiled header:

   ```cpp
   #define WINVER          0x0A00
   #define _WIN32_WINNT    0x0A00
   #define _WIN32_IE       0x0A00
   #define NOMINMAX
   ```

5. Include the ATL and WTL headers, then the docking window:

   ```cpp
   #include <atlbase.h>
   #include <atlapp.h>
   extern CAppModule _Module;
   #include <atlwin.h>
   #include <atlframe.h>
   #include <atlctrls.h>
   #include <atlgdi.h>

   #include <WTLDockUI.h>          // CDockHost and the model
   using namespace WTLDock;
   ```

   `WTLDock.h` is the window-free part (layout model, theme, geometry) and needs no ATL.

6. Add a manifest that declares per-monitor v2 DPI awareness (see `DockDemo/DockDemo.manifest`) and, for the
   common controls, the version 6 dependency. Without it the chrome still works but is drawn at the system DPI.

`DockDemo` in the same solution is a complete example: a frame with real controls as panes, menus for most of the
features described here, dark theme, saved layouts, and a Windows menu.

## A minimal application

The docking area is a child window, `CDockHost`. Make it the client window of your frame, then register panes and
show them.

```cpp
class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter {
public:
    DECLARE_FRAME_WND_CLASS(L"MyFrame", 0)

    // the docking area's keyboard shortcuts (Ctrl+Tab, Ctrl+F6, ...) come from the message loop
    BOOL PreTranslateMessage(MSG* msg) override {
        return m_Dock.PreTranslateMessage(msg) || CFrameWindowImpl<CMainFrame>::PreTranslateMessage(msg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
        m_Dock.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        m_hWndClient = m_Dock;

        // content windows are ordinary child windows; the host adopts them
        m_Tree.Create(m_Dock, rcDefault, nullptr, WS_CHILD | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);
        m_Edit.Create(m_Dock, rcDefault, nullptr, WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL);

        auto& layout = m_Dock.Layout();

        PaneDesc tree;
        tree.Id = L"tree";                       // stable and unique: it is how a saved layout finds the pane
        tree.Title = L"Solution Explorer";
        tree.hWnd = m_Tree;
        tree.Kind = PaneKind::Tool;
        tree.DefaultSide = DockSide::Left;
        tree.PreferredSize = { 260, 400 };       // at 96 DPI
        auto treePane = layout.AddPane(tree);

        PaneDesc doc;
        doc.Id = L"main.cpp";
        doc.Title = L"main.cpp";
        doc.hWnd = m_Edit;
        doc.Kind = PaneKind::Document;
        auto docPane = layout.AddPane(doc);

        layout.Show(treePane);                   // placed at its default side
        layout.Show(docPane);                    // opened in the document area
        m_Dock.ActivatePane(docPane);

        _Module.GetMessageLoop()->AddMessageFilter(this);
        return 0;
    }

    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        _Module.GetMessageLoop()->RemoveMessageFilter(this);
        handled = FALSE;
        return 0;
    }

private:
    CDockHost m_Dock;
    CTreeViewCtrl m_Tree;
    CEdit m_Edit;
};
```

Everything after `Show` is the user's: they can drag tabs and captions, float, auto-hide, split the document area
and resize. The host keeps the model (`m_Dock.Layout()`) and the windows in step after every change.

## Panes

A pane is a placed or unplaced window with a title, a kind and some capabilities. `AddPane` registers it (hidden);
`Show` places it.

| `PaneDesc` field | Meaning |
| --- | --- |
| `Id` | Stable, unique string. Saved layouts refer to panes by it. |
| `Title`, `Icon` | Tab and caption text, tab icon. |
| `hWnd` | The content window (not owned). May be null and made later, see [Content windows](#content-windows). |
| `Kind` | `PaneKind::Tool` (docks around the documents) or `PaneKind::Document` (tabs in the document area). |
| `Caps` | `PaneCaps::CanFloat`, `CanAutoHide`, `CanClose` (default: all). |
| `DefaultSide` | Where a tool window goes the first time it is shown. |
| `PreferredSize`, `MinSize` | In pixels at 96 DPI. |
| `Tooltip` | Shown when the mouse rests on the tab or caption (a document's full path, say). |

After registering, a `DockPane*` stays valid until `RemovePane`. Its `Title`, `Icon`, `Tooltip`, `Modified`, `Preview`
and `TabColor` may be changed at any time; then call `m_Dock.RefreshPane(pane)`.

## Placing panes

All operations are on `m_Dock.Layout()` (`DockLayout`). They return `false`, changing nothing, if the request is not
allowed (for example, docking a tool window into the document area as a tab).

```cpp
layout.Show(pane);                                   // where it was last, or its default side
layout.Hide(pane);                                   // remembers where it was
layout.DockTo(pane, otherPane->Group(), DockPosition::Tab);      // as a tab
layout.DockTo(pane, otherPane->Group(), DockPosition::Right);    // beside a group
layout.DockToEdge(pane, DockSide::Bottom);           // at an edge of the window
layout.Float(pane, screenRect);                      // into a window of its own
layout.AutoHide(pane->Group());                      // into the auto-hide bar of its side
layout.AutoHideTo(pane->Group(), DockSide::Right);   // ... or of another one
layout.Unhide(pane->Group());                        // and out again
```

Pointers to groups and other nodes (`pane->Group()`) are only good until the next operation; pointers to panes stay
valid. A pane that is hidden and shown again comes back where it was: as a tab of the same group, or beside the same
neighbour, with the same size.

To put a pane somewhere when you build the initial layout, call the operations in order, for example:

```cpp
layout.Show(explorer);                                         // left
layout.Show(output);                                           // bottom
layout.DockTo(errors, output->Group(), DockPosition::Tab);     // a tab next to Output
layout.Show(toolbox); layout.AutoHide(toolbox->Group());       // in the left auto-hide bar
```

## Content windows

Content is any child window of the host, or made on demand:

```cpp
m_Dock.SetContentFactory([this](DockPane& pane, HWND parent) -> HWND {
    return MakeViewFor(pane.Id(), parent);      // called the first time the pane is on show
});
```

Messages that a content window sends to its parent (`WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`, ...) are forwarded to
the host's parent window, your frame, as if the control were its child. Use `m_Dock.SetNotifyTarget(hWnd)` to send
them elsewhere.

Content windows are destroyed together with the host. If you destroy one yourself (a closed document), remove the
pane as well; see the next section.

## Closing, and what your application is told

The close buttons, the tab menu, Ctrl+F4 and the Windows dialog all go through `ClosePane`, which hides the pane
and activates a neighbour. Hooks (all `std::function` members of `CDockHost`):

```cpp
m_Dock.OnPaneClosing = [](DockPane* pane) { return AskToSave(pane); };  // return false to keep it open
m_Dock.OnPaneClosed = [this](DockPane* pane) {
    ::DestroyWindow(pane->hWnd);                                     // throw-away document: it is gone
    m_Dock.Layout().RemovePane(pane);
};
m_Dock.OnLayoutChanged = [this] { UpdateUi(); };
m_Dock.OnActivePaneChanged = [this] { UpdateUi(); };                 // ActivePane() has the focus
m_Dock.OnBuildPaneMenu = [](DockPane* pane, HMENU menu) {            // add items to the tab and caption menu
    ::AppendMenu(menu, MF_STRING, ID_MY_COMMAND, L"My command");
};
m_Dock.OnPaneSave = [](DockPane* pane) { return Save(pane); };       // the Windows dialog's Save button
```

Commands of the framework in the tab menu use ids 0xD000 to 0xD0FF; your own ids from `OnBuildPaneMenu` are posted
to the frame as `WM_COMMAND`.

## Saving and restoring the layout

The whole arrangement is JSON: the tree, sizes, floating windows, auto-hide bars, tab order, which tab is active,
pinned tabs, where hidden panes go back to, and, if you ask, where the top-level window is.

```cpp
std::string text = m_Dock.SaveState();                          // layout + active pane + window placement
m_Dock.SaveStateToFile(path);

std::wstring error;
if (!m_Dock.LoadStateFromFile(path, {}, &error))                // atomic: a bad file changes nothing
    ShowError(error);
```

Panes are matched by id. Ids in the file that are not registered are dropped, unless a factory makes them, which is
how documents that were open last time come back:

```cpp
m_Dock.SetPaneFactory([this](DockLayout& layout, const std::wstring& id) -> DockPane* {
    if (!IsMyDocumentId(id))
        return nullptr;                      // not ours: dropped from the layout
    PaneDesc d;
    d.Id = id; d.Title = TitleOf(id); d.Kind = PaneKind::Document;
    return layout.AddPane(d);                // no window yet: the content factory makes it
});
```

Options for loading (`LoadOptions`): `ShowNewPanes` shows panes that the file has never heard of (new in this
version of your application), `MinAppVersion` refuses layouts saved by an older `DockLayout::SetAppVersion`.

Other helpers:

```cpp
m_Dock.CaptureDefaultLayout();           // once the initial arrangement is set up
m_Dock.ResetLayout();                    // Window > Reset Window Layout

m_Dock.SaveLayoutAs(L"Debug");           // named layouts
m_Dock.ApplyLayout(L"Debug");
m_Dock.Layouts().SaveToFile(path);       // DockLayoutStore: keep them in one file
m_Dock.Layouts().LoadFromFile(path);
```

A common policy, used by DockDemo: restore the saved state at startup if the file exists, save it on `WM_CLOSE`.

## Menus

```cpp
// a View / Panes menu: an item per pane (checked if it is showing), ids firstId + index
m_Dock.FillPaneMenu(menu, ID_PANE_FIRST, PaneKind::Tool);
m_Dock.FillPaneMenu(menu, ID_PANE_FIRST, PaneKind::Document);
m_Dock.HandlePaneCommand(id, ID_PANE_FIRST);         // shows, slides out or activates the pane

// named layouts
m_Dock.FillLayoutMenu(menu, ID_LAYOUT_FIRST);
m_Dock.HandleLayoutCommand(id, ID_LAYOUT_FIRST);

// Window menu
m_Dock.CloseAllDocuments(/*exceptActive*/ false);    // pinned tabs stay
m_Dock.ActivateNextDocument(true);
m_Dock.ShowNavigator(true);                          // the Ctrl+Tab switcher
m_Dock.ShowWindowsDialog(m_hWnd);                    // Window > Windows...
```

Fill menus when they open (`WM_INITMENUPOPUP`), since the panes change.

Commands on a pane, for your own menus and buttons: `m_Dock.CanExecute(cmd, pane)` and `m_Dock.Execute(cmd, pane)` with
`DockCommand::Close`, `CloseOthers`, `CloseAll`, `AutoHide`, `Float`, `Dock`, `PinTab`, `UnpinTab`,
`NewHorizontalGroup`, `NewVerticalGroup`, `MoveToNextGroup`, `MoveToPreviousGroup`.

## Documents

Documents are panes of `PaneKind::Document`. They live in the document area, which can be split into several tab
groups, and they can float in windows of their own.

```cpp
m_Dock.ShowPane(pane);                      // open it, or bring it to the front
m_Dock.ShowPreview(pane);                   // a preview: italic, replaced by the next preview
m_Dock.PromotePreview(pane);                // ...until it is kept (editing it, pinning it or double clicking do that)

pane->Modified = true;                      // a dot on the tab, a mark in the caption and the switcher
pane->TabColor = RGB(0, 122, 204);          // a stripe under the tab
pane->Tooltip = L"C:\\src\\main.cpp";
m_Dock.RefreshPane(pane);

m_Dock.Execute(DockCommand::PinTab, pane);  // pinned tabs stay at the left and survive "close all"
```

New documents open in the group that was used last (`Layout().ActiveDocumentGroup()`).

Tabs that do not fit scroll (arrows, mouse wheel, a list of all tabs). For several rows instead, use
`m_Dock.SetMultiRowTabs(true)`.

## Theme and DPI

```cpp
m_Dock.SetTheme(DockTheme::Dark());          // or DockTheme::Light(), or a DockTheme you fill in
```

The theme covers the chrome only: captions, tabs, splitters, drop markers, tooltips and the switcher. Colouring your
content windows is up to you. Edits and list boxes take their colours from `WM_CTLCOLOR*`, which arrive at your frame
(see [Content windows](#content-windows)); `SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr)` gives tree and list
controls dark scroll bars and selection colours.

The docking area follows the DPI of the window it is in. Floating windows have a DPI of their own, that of the monitor
they are on, and their layout is rescaled when they move. Your content is yours: handle `WM_DPICHANGED` in your
frame and recreate fonts, as usual for per-monitor DPI. `m_Dock.Font()` and `m_Dock.Dpi()` give the chrome's font and
DPI at the moment.

### With WTLHelper's dark mode

If the application uses `WTLHelper::InitDarkMode` (darkmodelib), one palette can drive the controls and the docking
chrome. `WTLDockDarkMode.h` is an optional header for that (it needs WTLHelper's directory on the include path;
WTLDock itself does not depend on WTLHelper):

```cpp
#include <WTLDockUI.h>
#include <WTLDockDarkMode.h>            // after WTLHelper.h and WTLDockUI.h

WTLHelper::InitDarkMode(DarkModeKind::Classic);      // at start, before any window is made

// once, after the docking window is made:
UseDarkModeTheme(m_Dock);                            // (optional: an accent colour, default the Visual Studio blue)

// later, as the application does today:
WTLHelper::SwitchToMode(DarkModeKind::Dark, m_hWnd);          // controls and chrome
WTLHelper::SetColorTone(ColorTone::Blue, m_hWnd);
m_Dock.RefreshTheme();                                        // (SetColorTone sends no message)
```

What happens:

* The controls in the panes are themed by WTLHelper's hook as they are created, like any window of the thread.
* The chrome follows through a theme provider: `m_Dock.SetThemeProvider(...)` (which `UseDarkModeTheme` sets) is asked for
  a `DockTheme` now, on `RefreshTheme()` and whenever the registered window message `"ThemeChanged"` arrives, which is
  what `SwitchToMode` sends to the frame's descendants. In dark mode the theme is `DockTheme::FromPalette` of the
  palette (`DarkMode::getBackgroundColor()` and the others), in Light and Classic mode `DockTheme::Light()`.
* Floating windows are not children of the frame, so `SwitchToMode` does not reach their content. The host passes the
  message on to it and calls `OnFloatingWindowThemeChanged(frame)` for each floating window, where `UseDarkModeTheme`
  applies `setDarkTitleBarEx` and `setChildCtrlsTheme`.
* The Windows dialog is left to the library's hook (`m_Dock.SetStyleDialogs(false)`); without WTLHelper it colours
  itself after a dark `DockTheme`.

Other dark mode libraries: fill a `DockPalette` from theirs and use `DockTheme::FromPalette` in your own provider.

## Keyboard

Call `m_Dock.PreTranslateMessage(msg)` from your message loop (a `CMessageFilter`, as in the minimal application).
This enables:

| Keys | Action |
| --- | --- |
| Ctrl+Tab, Ctrl+Shift+Tab | Window switcher: files and tool windows, most recently used first. Keep Ctrl down; Tab or arrows move, releasing Ctrl goes there, Esc cancels |
| Ctrl+F6, Ctrl+Shift+F6 | Next / previous document tab |
| Ctrl+F4 | Close the active document |
| Alt+F6, Shift+Alt+F6 | Next / previous group (documents and tool windows) |
| Shift+Esc | Close the active tool window |
| Alt+- | The menu of the active pane |
| Ctrl+Alt+F6 | Keyboard on the tabs and buttons of the active pane: arrows move, Enter activates or presses, Delete closes, Shift+F10 opens the menu, Tab goes to the next group, Esc returns to the content |

`m_Dock.SetShortcutsEnabled(false)` turns them all off if they clash with yours.

## Translating the framework's texts

Everything the docking windows say is English by default: the tab and caption menu, tooltips, the Windows dialog
and its list, the window switcher, and the names that screen readers hear. `DockStrings.h` holds one table of them,
per process, to set up once at start (it is not thread safe):

```cpp
SetDockText(Str::MenuClose, L"&Schliessen");             // one text
LoadDockTexts(jsonTable);                                // or many, from JSON: {"MenuClose": "&Schliessen", ...}
std::string template = DumpDockTexts();                  // every name with its English text, to translate from
ResetDockTexts();                                        // back to English
```

Names that are not known are ignored, so a table survives the addition of texts in a later version (they stay
English until translated). A text may hold `{0}`, `{1}`, ... where the framework puts something in (`"Close {0}"` for
the close button of a tab); menu texts keep their `&` accelerator. Texts are looked up when they are shown, so the
language can be changed while the application runs; call `m_Dock.HideTip()` if a tooltip is up.

What the framework does not translate: the names of your panes, the items you add through `OnBuildPaneMenu`, and the
error messages of the JSON reader (`LoadState` and friends), which are for developers. DockDemo has a sample German
table under Options.

## Other options

* `m_Dock.SetTipsEnabled(false)`, `SetTipTiming(showDelayMs, visibleMs)`: tooltips.
* `m_Dock.SetKeepFloatsOnScreen(false)`: by default a floating window whose monitor has gone is brought back on screen.
* `m_Dock.SetFlyoutTiming(animationMs, hoverDelayMs, leaveDelayMs)`: the auto-hide flyout.
* Accessibility: the host and every group window expose an MSAA object (captions, buttons, tabs, auto-hide items), so
  screen readers and UI Automation clients see the chrome.
* `DockLayout::Dump()` describes the layout in one line per area (for logging), and `Validate()` checks its invariants.

The layout model (`DockLayout`, `DockLayout::Load`/`Save`, the geometry functions) has no window in it and can be
used and tested without one: include `WTLDock.h`.

## Testing

`WTLDockTests` is a console program that needs no test framework. It runs the model tests and the window tests
(real windows, created off-screen and compared with the model after random operations), and prints the number of
checks and failures. Run it in every configuration you build:

```
msbuild WTLDockTests\WTLDockTests.vcxproj -p:Configuration=Debug -p:Platform=x64 -p:SolutionDir=<solution dir>\
bin\x64\Debug\WTLDockTests.exe
```

The tests create windows briefly off-screen; a couple of them open the Windows dialog or a menu and close it again
by themselves.
