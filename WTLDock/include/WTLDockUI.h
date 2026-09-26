#pragma once

// The docking window (CDockHost). Requires ATL and WTL: include atlbase.h and atlapp.h first, and define
// WINVER / _WIN32_WINNT as 0x0A00 or later (the framework needs Windows 10 1703 for per-monitor DPI).

#include "WTLDock.h"
#include "DockHost.h"
