# CalendarView

How to build and test is in the root `CLAUDE.md`.

## Overview

`CalendarView`: a calendar's day view, as Outlook's (`CCalendarViewCtrl`, window class `WC_CalendarView`), `CLV`-prefixed C-style API (`CLVITEM` with a mask, `CLVCOLORS`, `CLVN_*`; commctrl's month calendar owns `MCM_`/`MCN_`, winnls `CAL_`; internal names are `Clv*`), GDI-drawn. 1 to 14 day columns (`SetDayCount`, `SetDate`, `GoToday`; `Navigate` goes a view on, a week for 5 or 7 days, `ClvViewStep`/`ClvFirstShown`; Ctrl+Page Up/Down, Ctrl+Home) under the days' names, an all-day strip (all-day items and those a day or longer; past `stripMaxRows` it scrolls with the wheel, chevrons in the ruler's corner), and the days' time in a child window (`ClvDaysWindow`, `ALT_MSG_MAP(1)`, `GetDaysWindow`) with the ruler (as wide as its widest hour label), slots (`SetSlotMinutes`), zoom (`SetZoom`, Ctrl+wheel around the mouse), the working week shaded (`SetWorkWeek`) and a line at now.

## The model

The window-free model (`src/CalendarViewModel.*`) is in days and minutes since 1601 (`ClvTime`, local), so the month view is another layout over the same items: `ClvSplit` cuts items into a piece per day (the strip's: one over its days), `ClvPlaceOverlaps` puts overlapping pieces side by side (clusters, the first free column, widened right while free), `ClvPackStrip` packs the strip's rows, `ClvLayoutDay` makes the boxes (a window item's caption and body), `ClvHitDays`/`ClvHitStrip` and the `ClvDrag*` functions do the rest.

## Items

Items are the control's own (`AddItem`), or with `CLVS_VIRTUAL` the app's: the store is then a cache of the days shown, `CLVN_GETITEMS` asks for the days not asked for yet (`ClvMissing`), and the items that leave them go (`Keep`) with `CLVN_RELEASEITEM`; `RefreshItems` asks again.

An item is drawn by the control (`CLVC_DEFAULT`), by the app (`CLVC_CUSTOM`: the control draws its background and color bar, the app the rest in `CDDS_ITEMPOSTPAINT`, always sent; `CLVN_CUSTOMDRAW` stages as the common controls', `CDRF_NOTIFYITEMDRAW` for every item's colors, the DC clipped to the item), or is the app's window (`CLVC_WINDOW`): `PlaceWindows` makes it the days' window's child (lazily, `SetParent`), puts it under a caption the control draws (the caption is its handle: select, drag), hides it when its piece has no room for it, it's in the strip, not shown, or removed (the app destroys it; a deleted item's is kept for undo), with `DeferWindowPos` on every layout and scroll; the days' window is `WS_EX_CONTROLPARENT` and passes its children's `WM_COMMAND`, `WM_NOTIFY`, owner-draw, scroll bar and `WM_CTLCOLOR*` messages to the control's parent, answering `WM_CTLCOLORSTATIC` of an item's window with the item's fill; a press in an item's window selects the item (`WM_PARENTNOTIFY`).

## Editing and undo

Editing (`src/CalendarViewEdit.cpp`): every press goes to `Press` in the control's coords (the days' below `daysTop`) and the capture is the control's, so a drag goes between the strip and the days; items move (from the strip into the days: an hour), resize by their ends, snapped to the slots, live, with `WS_EX_COMPOSITED` on the days' window meanwhile and auto-scrolling at its edges; the drop is `CLVN_ITEMCHANGING` (`NMCLVCHANGE`, `CLVCH_*`; non-zero puts the items back, or the app changes the values) then `CLVN_ITEMCHANGED`; a drag over slots selects them (`CLVN_SLOTCHANGED`), and a double click or Enter there is `CLVN_CREATEITEM` (the app adds an item and sets its id: selected and renamed in place).

Undo (`ClvHistory`, Ctrl+Z/Ctrl+Y, `CLVN_RESTORED`) keeps the control's own items per user change (not in virtual mode: the app's undo); the API's changes clear it (`creating` keeps `CLVN_CREATEITEM`'s `AddItem` a step).

## Keys

The keys (`src/CalendarViewKeys.cpp`): the arrows move a cursor slot (Shift extends from the anchor; past the days shown, the next ones), Tab goes along the items (`ClvTabOrder`; `DLGC_WANTTAB` only while there's a next), Alt+arrows move the selected item (Alt+Shift+Up/Down: its end) through the same veto and undo (`Apply`), F2 renames (`m_Edit(this, 2)`, `ALT_MSG_MAP(2)`, `CLVN_TITLECHANGING`), Delete removes (`CLVN_ITEMDELETING`), `CLVS_READONLY` turns editing off.

## The month view

The month view (`SetView(CLVV_MONTH)`, `src/CalendarViewMonth.cpp`, `CLVN_VIEWCHANGED`; the day view comes back to the selected item's week with its own day count, `dayRange`) shows the month's weeks (`ClvMonthRange`) as rows sharing the height under the weekdays' names, with the days' window hidden (items' windows with it): `ClvLayoutMonth` makes bars of all-day items and those over more than a day (`ClvIsBar`), packed per week as the strip's rows, and lines of a day's timed items (a dot, the time, the title; `ClvBox::line`) in the lanes the bars leave; a day with more than its cell holds shows a lane fewer and "+N more" (`ClvMore`), and a bar is cut where its lane is hidden. Hit testing (`ClvHitMonth`, `ClvPart::More`) and the day under the mouse (`MonthDayAt`) feed the same `Press`/`DragTo` as the strip (`HitAt` says strip; `HitBox`, `BoxOf`, `BoxRect` find a box in either view), but moves keep the time of day (`ClvDragMoveDays`); the keys go by days and weeks, Navigate, the wheel and the date chooser by months.

"+N more" opens `ClvMorePopup` over the cell (the day's items; it has the capture as the date chooser does; a click selects, a double click or Enter opens, a drag past `SM_CXDRAG` hands the item to the control's drag, `DragFromMore`), accessible as a list of the day's items.

## Going between days

Going between days (`src/CalendarViewNav.cpp`): `CLVS_NAVBAR` puts a bar above the days' names (Today, back, forward, the days shown as its title, `RangeTitle`: the locale's `LOCALE_SMONTHDAY` picture, since `DATE_MONTHDAY` gives nothing with `GetDateFormatEx`); the title (on the press), Ctrl+G or F4 open the date chooser (`ShowDatePicker`): `ClvPicker`, a custom-drawn month (the standard month calendar was rejected) in a popup that doesn't take the activation (`WS_EX_NOACTIVATE`, DWM's rounded corners), laid out by `ClvLayoutMini` (6 weeks from the week holding the month's first day, `SetFirstDayOfWeek` or the locale's); it has the mouse capture while open (a press outside closes it and is eaten, the capture lost closes it), the control passes it its keys (arrows, Page Up/Down months, Home/End, Enter, Escape) and the wheel; a pick shows its day from its week's start in a week's view (`PickDate`).

Its MSAA is a list of the 42 days (named by the long date, the focus day reported as it moves) and the month buttons; the bar's buttons are the control's list's first children.

## Look, tooltips and accessibility

Tooltips: a tool on the strip and one on the days' window, moved onto the item under the mouse (the days' `TTN_GETDISPINFO` comes through its forwarding).

Like DiffView it follows WTLHelper's dark mode through `GetSysColor` and the registered `ThemeChanged` message (the days' scroll bar `DarkMode_Explorer`); high contrast uses the system colors over the app's and the items' own, time off hatched, items framed, the selection in the highlight colors (`ForceHighContrast` for testing).

MSAA (`src/CalendarViewAccessible.cpp`): a list of the items in the tab order (list items named by their title, described by their time spoken, `ClvWhen`; Open is the default action), a "selected time" item while slots are selected (focused when no item is; New Item), and the days' window's own object last (its children are the items' windows; its basic properties are answered for clients that ask the child id); actions are posted (`WM_CLV_ACCACTION`).

## Demo and tests

The model is covered by `CalendarViewTests`; `CalendarViewDemo` (a week of meetings with window items, the month view (Ctrl+4), a custom-drawn burndown, and a virtual mode that makes up every day's items, which links WTLHelper on x64) is for checking by hand.
