# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A set of Win32/C++20 UI libraries built on WTL. Native Windows only: MSVC, Unicode, Visual Studio solution `WTLHelper.sln`. This directory is a git submodule of the parent `WinSysLive` repo (`../`), and the parent `WinSysLive.sln` builds `WTLHelper` and `GraphControl` directly. Changes to those two projects affect the parent app.

## Build

There is no CMake or lint setup; everything goes through MSBuild. Most projects use toolset v145, which ships with VS 18 (`C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe`). The BasicDemo and HexTest projects still use v143.

```powershell
# whole solution (configs: Debug/Release; platforms: x64, x86, ARM64)
msbuild WTLHelper.sln /p:Configuration=Debug /p:Platform=x64 /m
# a single project
msbuild WTLHelper\WTLHelper.vcxproj /p:Configuration=Debug /p:Platform=x64
```

NuGet dependencies (`packages.config`): Detours (`ThemeHelper.cpp` includes `<detours/detours.h>`), WIL and the `wtl` package. Restore them before building (`nuget restore WTLHelper.sln`). The WTL headers are also vendored in `WTL/`, which `WTLHelper` and `HexControl` add to their include path.

## Tests

The only automated tests are in `GraphControlTests/tests.cpp`. It's a console exe with a hand-rolled harness and no framework. The process exit code is the number of failed checks.

- Add a test with `TEST(Name) { CHECK(...); }`, then add a `Run_Name();` call in `wmain`. Registration is manual, so a test you don't call never runs.
- To run a single test, comment out the other `Run_*` calls in `wmain`. There is no filter argument.
- The tests cover only the pure data model (`GraphData`, ring buffer, formatters). Anything that needs an HWND or a render target is checked by hand with the demo apps (`GraphDemo`, `NodeGraphDemo`, `TreeListViewDemo`, `HexTest`, `BasicDemo`).

## Architecture

The solution has two families of projects.

**1. `WTLHelper/`: the main static library** (precompiled header `pch.h`, which defines `NOMINMAX` and pulls in ATL/WTL and the STL). It has two independent theming systems:

- **Dark mode (current path)**: `WTLHelper` (`WTLHelper.h/.cpp`) wraps the vendored **darkmodelib** in `WTLHelper/DarkMode/` (third-party, MPL-2.0, derived from Notepad++). Call `WTLHelper::InitDarkMode()` / `SwitchToMode()`. The theming hook is per thread: `InitDarkMode` installs it for its thread, and another UI thread calls `WTLHelper::InitThread()` (and `TermThread()` before it ends) for its windows to be themed too; `SuspendHook`/`ResumeHook` affect only the calling thread. Mode changes are broadcast with the registered `WTLHelper::ThemeChangedMessage`. `WTLHelper.cpp` also Detours-hooks `GetSysColor`/`GetSysColorBrush` so stock controls pick up dark colors. The `ColorTone` enum mirrors `DarkMode::ColorTone` and must stay in sync, because the code `static_cast`s between them. Avoid modifying the `DarkMode/` files themselves unless necessary; they come from upstream.
- **Custom themes (`ThemeHelper` + `Theme`)**: `ThemeHelper::Init()` installs a `WH_CALLWNDPROCRET` hook plus Detours hooks on `GetSysColor`/`GetSysColorBrush`/`GetSystemMetrics`. In `HandleCreateWindow` (`ThemeHelper.cpp`), every newly created window is matched by class name and subclassed with the corresponding `Custom*.h` class (`CCustomListView`, `CCustomButtonParent`, `CCustomHeaderParent`, …). Some of those subclass the *parent* window to handle `NM_CUSTOMDRAW`. These objects are heap-allocated and free themselves in `OnFinalMessage`. To theme a new control type, add a `Custom*.h` class and a branch in `HandleCreateWindow`. Use `Suspend()`/`Resume()` around windows that must not be themed.

The rest of `WTLHelper` is a grab bag of standalone helpers: list views (`VirtualListView.h`, `ColumnManager`, `SortHelper`, `SortedFilteredVector.h`), settings persistence (`Settings`, `IniFile` = thin `GetPrivateProfile*` wrapper, `IniDocument` = full in-memory INI parser with typed reads and atomic save via temp file), and `CompoundFile`, `SecurityHelper`, `IconHelper`, `ClipboardHelper`, `StringHelper`, tab-view helpers and others.

**2. Standalone control libraries**: each is a static lib with a public header in `include/`, implementation in `src/`, and usually a matching demo exe. They don't depend on `WTLHelper`.

- `GraphControl`: time-series graph (Direct2D/DirectWrite, namespace `GraphCtrl`). `GraphData` is a separate pure model, which is what the tests target.
- `NodeGraphControl`: node/edge graph editor (namespace `NodeGraphCtrl`). It notifies the parent via `WM_NOTIFY` `NGCN_*` codes.
- `TreeListView`: tree+columns control registered as window class `WC_TreeListView`, with an opaque `HTLITEM` handle API in the style of common controls.
- `HexControl`: hex editor control (`BufferManager`/`MemoryBuffer`/`Selection`).
- `MarkdownViewLib`: `CMarkdownView`, a WTL wrapper around a Direct2D markdown viewer/editor ("Tinta"). Message handlers are thin pass-throughs to free `handleXxx(App&)` functions. Keep logic in those functions and not in the window class.
