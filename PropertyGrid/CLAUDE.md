# PropertyGrid

How to build and test is in the root `CLAUDE.md`.

`PropertyGrid`: categorized name/value editor (`CPropertyGridCtrl`, window class `WC_PropertyGrid`), self-drawn with GDI, with a C-style API: `HPRGITEM` handles, `PRGVALUE` tagged values, `PRGN_*` notifications. The `PRG` prefix avoids commctrl's Pager control, which owns `PGN_`/`PGF_`/`PGK_`. The window-free model (`src/PropertyGridModel.*`: the item tree, the rows of each view, formatting) is covered by `PropertyGridTests`; `PropertyGridDemo` is for checking by hand.
