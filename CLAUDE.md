# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A set of Win32/C++20 UI libraries built on WTL. Native Windows only: MSVC, Unicode, Visual Studio solution `WTLHelper.sln`. This repo is used as a git submodule by several apps (e.g. `WinSysLive`, `NetworkExplorer`), whose solutions build `WTLHelper` (and in `WinSysLive`'s case `GraphControl`) directly from source. Changes to those projects affect every app that uses them.

## Build

There is no CMake or lint setup; everything goes through MSBuild. Most projects use toolset v145, which ships with VS 18 (`C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe`). The BasicDemo and HexTest projects still use v143.

```powershell
# whole solution (configs: Debug/Release; platforms: x64, x86, ARM64)
msbuild WTLHelper.sln /p:Configuration=Debug /p:Platform=x64 /m
# a single project
msbuild WTLHelper\WTLHelper.vcxproj /p:Configuration=Debug /p:Platform=x64
```

Every project uses the static C runtime (`/MT`, `/MTd`) in every configuration; a library on the DLL runtime fails to link (LNK2038) with the others.

Dependencies come from vcpkg with user-wide integration (`vcpkg integrate install`; no manifest in the repo): Detours (`ThemeHelper.cpp` includes `<detours/detours.h>`), WIL and WTL. `WTLHelper`, `HexControl` and the demo apps link the static CRT and set `VcpkgUseStatic`, so they take the static triplet (`x64-windows-static`); install the packages for both triplets, as the WinSysLive app's CI does: `vcpkg install detours:x64-windows-static wil:x64-windows-static wtl:x64-windows-static detours:x64-windows wil:x64-windows wtl:x64-windows`. These were NuGet packages once; nothing references NuGet anymore (the stale `packages.config` was removed). The WTL headers are also vendored in `WTL/`, which `WTLHelper` and `HexControl` add to their include path.

## Tests

The automated tests are in `GraphControlTests/tests.cpp`, `WTLDockTests/`, `PropertyGridTests/tests.cpp`, `LogViewTests/tests.cpp`, `ColorPickerTests/tests.cpp`, `AccordionTests/tests.cpp`, `DiffViewTests/tests.cpp`, `CommandPaletteTests/tests.cpp`, `RibbonBarTests/tests.cpp`, `BreadcrumbTests/tests.cpp`, `NotifyTests/tests.cpp`, `GanttTests/tests.cpp`, `TokenEditTests/tests.cpp`, `CalendarViewTests/tests.cpp`, `MarkdownViewTests/tests.cpp` and `FunctionPlotTests/tests.cpp`. Each is a console exe with a hand-rolled harness and no framework. The process exit code is the number of failed checks.

- Add a test with `TEST(Name) { CHECK(...); }`, then add a `Run_Name();` call in `wmain`. Registration is manual, so a test you don't call never runs.
- To run a single test, comment out the other `Run_*` calls in `wmain`. There is no filter argument.
- The tests mostly cover pure data models (`GraphData` and the cell graphs, the dock layout, the property grid's item tree and formatters, the log view's line storage, the color picker's conversions and pixels, the accordion's layout and state, the diff view's diff, rows and selection text, the command palette's matching and search, the ribbon's shrinking, layout, key tips and state, the breadcrumb's layout, folding, history and path text, the notifications' link markup, wrapping, layouts, toast placement and timer, the Gantt chart's dates, calendar, time scale, task tree, links, scheduling, bar geometry, dragging and undo, the token edit's token list and selection, splitting and joining with quotes, flow layout, hit testing, drop places, suggestion matching and undo, the calendar view's dates and month grid, item store and virtual cache, pieces, overlap placement, strip packing, day layout, hit testing, dragging, tab order, navigation steps, the date chooser's and the month view's layouts, and undo, the markdown view's block tree, inline styles and source map, raw HTML, alerts, footnotes, emoji, anchors, export, its flow of boxes, the built-in highlighting, images' sources and SVG texts, finding and words, the function plot's expressions: lexing, implicit products, name splitting, errors and their positions, what an expression draws, compiling against the tree, the math functions, degrees, derivatives against differences, simplifying and printing, and its 2D model: the view, ticks and their labels, clipping, sampling functions and curves, implicit curves and regions, roots, extrema and intersections, and the control's graphs: definitions, sampling, features, tracing and coordinates, the user's functions and d/dx, surfaces' heat maps and contours, vector fields, the SVG export, 3D: the box, the camera, meshes, tubes and the graphs' meshes, tracing by keys, draggable points, integrals, lists and regressions, the saved state, fitting the view, labels, styles, undo, the sampling thread, log axes, ODEs, stiff equations, 3D picking of trajectories and ODEs on the plane, curves' intersections, surfaces' line grids, what's between two curves, the color scale's labels, the shaders compiling and binding, log axes with every kind, equations up to the fifth order and systems (with t, and in 3D) against exact solutions, dragging any initial point, and asymptotes and holes); `WTLDockTests/ui_tests.cpp` also creates windows. Anything else that needs an HWND or a render target is checked by hand with the demo apps (`GraphDemo`, `CellGraphsDemo`, `NodeGraphDemo`, `TreeListViewDemo`, `PropertyGridDemo`, `LogViewDemo`, `ColorPickerDemo`, `AccordionDemo`, `DiffViewDemo`, `CommandPaletteDemo`, `RibbonBarDemo`, `BreadcrumbDemo`, `NotifyDemo`, `GanttDemo`, `TokenEditDemo`, `CalendarViewDemo`, `MarkdownViewDemo`, `FunctionPlotDemo`, `HexTest`, `BasicDemo`).

## Architecture

The solution has two families of projects.

**1. `WTLHelper/`: the main static library** (precompiled header `pch.h`, which defines `NOMINMAX` and pulls in ATL/WTL and the STL). It has two independent theming systems:

- **Dark mode (current path)**: `WTLHelper` (`WTLHelper.h/.cpp`) wraps the vendored **darkmodelib** in `WTLHelper/DarkMode/` (third-party, MPL-2.0, derived from Notepad++). Call `WTLHelper::InitDarkMode()` / `SwitchToMode()`. The theming hook is per thread: `InitDarkMode` installs it for its thread, and another UI thread calls `WTLHelper::InitThread()` (and `TermThread()` before it ends) for its windows to be themed too; `SuspendHook`/`ResumeHook` affect only the calling thread. Mode changes are broadcast with the registered `WTLHelper::ThemeChangedMessage`. `ApplyMode` also gives every list view the mode's own theme name (`DarkMode_Explorer` or `Explorer`): darkmodelib sets `Explorer` in both modes, and a list view that already had it kept the old mode's theme data, so a selected item stayed light (black text) after switching to dark. Tooltips are popups, which `ApplyMode`'s child windows don't include, so the controls' own tooltips stayed light in dark mode: the hook themes a tooltip when it's created and `ApplyMode` themes the thread's tooltips on every switch (`ThemeTooltip`, darkmodelib's `setDarkTooltips`). `WTLHelper.cpp` also Detours-hooks `GetSysColor`/`GetSysColorBrush` so stock controls pick up dark colors. The `ColorTone` enum mirrors `DarkMode::ColorTone` and must stay in sync, because the code `static_cast`s between them. Avoid modifying the `DarkMode/` files themselves unless necessary; they come from upstream. Two local changes must survive an update from upstream. In `DmlibSubclassControl.cpp`, `paintStatusBar` draws a part's icon (`SB_SETICON`) before its text; upstream draws only the text, so an app's status icons vanished in dark mode. Also, the control subclasses pass `WM_DPICHANGED_AFTERPARENT` on (`DefSubclassProc`) after their own handling instead of returning 0. Upstream swallows it, and since dark mode subclasses a control after the app does (when the mode is applied), the app's own subclasses (`ListViewHelper::EnableColumnDpiScaling`, a filter box resizing itself) never got it; `DpiTest` doesn't turn dark mode on, so it can't catch that.
- **Custom themes (`ThemeHelper` + `Theme`)**: `ThemeHelper::Init()` installs a `WH_CALLWNDPROCRET` hook plus Detours hooks on `GetSysColor`/`GetSysColorBrush`/`GetSystemMetrics`. In `HandleCreateWindow` (`ThemeHelper.cpp`), every newly created window is matched by class name and subclassed with the corresponding `Custom*.h` class (`CCustomListView`, `CCustomButtonParent`, `CCustomHeaderParent`, …). Some of those subclass the *parent* window to handle `NM_CUSTOMDRAW`. These objects are heap-allocated and free themselves in `OnFinalMessage`. To theme a new control type, add a `Custom*.h` class and a branch in `HandleCreateWindow`. Use `Suspend()`/`Resume()` around windows that must not be themed.

The rest of `WTLHelper` is a grab bag of standalone helpers: list views (`VirtualListView.h`, `ColumnManager`, `SortHelper`, `SortedFilteredVector.h`), settings persistence (`Settings`, whose `GetPath` is the key or INI file `Save` writes, which `LoadFromKey`/`LoadFromFile` replace; `IniFile` = thin `GetPrivateProfile*` wrapper, `IniDocument` = full in-memory INI parser with typed reads and atomic save via temp file), `CCustomSplitterWindow`/`CCustomHorSplitterWindow` (`CustomSplitterWindow.h`: WTL's splitter with its bar drawn as a hairline (`SplitterBarStyle`: Line, Gutter, Grip, Classic, None), sized in DIPs, hot and pressed in the highlight color, in dark mode the views' background (`DarkMode::getViewBackgroundColor`, which `ApplyMode` gives list and tree views; `COLOR_WINDOW` is darker); without full-window dragging it forces WTL's `m_bFullDrag` on and drags a translucent popup itself (`CSplitterGhostBar`), since `CSplitterImpl` calls its `DrawGhostBar` non-virtually), and `CompoundFile`, `SecurityHelper`, `IconHelper`, `ClipboardHelper`, `StringHelper`, tab-view helpers and others. `IconHelper::Load` and `LoadCached` (and `ToolBarButtonInfo::image`, which `ToolbarHelper` loads through `Load`) take an `IconId` (`IconHelper.h`): an `int` is the app's resource ID (0: none; a literal 0 picks that constructor, an exact match), a `PCWSTR` that's `IS_INTRESOURCE` a system stock icon (winuser.h's `IDI_WARNING`, `IDI_ERROR`..., loaded with `LoadIconWithScaleDown`, crisp at any size), any other string the app's resource by name. So a tool bar table mixes them: `{ ID_REFRESH, IDI_REFRESH }`, `{ ID_WARNINGS, IDI_WARNING, BTNS_CHECK, L"Warnings" }`. Pass the app's IDs as numbers: a `MAKEINTRESOURCE` of one would be taken for a stock icon. `LoadCached` keys on the ID, the kind, the name's pointer and the size. `DpiTest`'s `TestIconId` checks the kinds, the sizes and a tool bar mixing them.

**2. Standalone control libraries**: each is a static lib with a public header in `include/`, implementation in `src/`, and usually a matching demo exe. They don't depend on `WTLHelper`.

Each has a `CLAUDE.md` of its own in its folder, with how it works and what has to stay true. It loads when you work on files in that folder, but not for the control's demo or tests in `<Name>Demo/` and `<Name>Tests/`: read it first for those, and update it when the control changes.

- `GraphControl` (`GraphControl/CLAUDE.md`): time-series graph and the cell graphs (sparklines, meters) for list views.
- `NodeGraphControl` (`NodeGraphControl/CLAUDE.md`): node and edge graph editor.
- `TreeListView` (`TreeListView/CLAUDE.md`): tree with columns.
- `HexControl` (`HexControl/CLAUDE.md`): hex editor.
- `PropertyGrid` (`PropertyGrid/CLAUDE.md`): categorized name and value editor.
- `LogView` (`LogView/CLAUDE.md`): read-only log viewer for millions of lines.
- `ColorPicker` (`ColorPicker/CLAUDE.md`): color picker, its dialog and color button.
- `Accordion` (`Accordion/CLAUDE.md`): stack of collapsible sections.
- `DiffView` (`DiffView/CLAUDE.md`): side by side and unified text comparison.
- `CommandPalette` (`CommandPalette/CLAUDE.md`): search box over the app's commands.
- `RibbonBar` (`RibbonBar/CLAUDE.md`): ribbon-lite.
- `Breadcrumb` (`Breadcrumb/CLAUDE.md`): breadcrumb bar, as Explorer's address bar.
- `Notify` (`Notify/CLAUDE.md`): info bar and toasts.
- `Gantt` (`Gantt/CLAUDE.md`): Gantt chart.
- `TokenEdit` (`TokenEdit/CLAUDE.md`): token (tag, recipient) box.
- `CalendarView` (`CalendarView/CLAUDE.md`): calendar day and month views.
- `MarkdownView` (`MarkdownView/CLAUDE.md`): markdown viewer.
- `FunctionPlot` (`FunctionPlot/CLAUDE.md`): math function plotter, in 2D and 3D.
