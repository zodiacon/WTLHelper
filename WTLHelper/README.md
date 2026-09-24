# WTLHelper
WTL helper library

Something I use in my own tools (or working to port common parts to use WTLHelper).

## INI files

- `IniFile` - a thin wrapper over the `GetPrivateProfile*` APIs (settings, colors, fonts, binary blobs).
- `IniDocument` - a whole INI file in memory: UTF-8 (BOM and UTF-16 accepted), case-insensitive names, typed reads with defaults or `std::optional`, exact round-tripping of numbers, error messages with line numbers, and saving through a temporary file so a failure can't damage the existing file. For files that must be right and may be edited by hand. No dependencies besides `<Windows.h>` and the standard library.
