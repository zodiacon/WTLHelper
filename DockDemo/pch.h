#pragma once

// Windows 10 1703 (Creators Update) is the minimum: per-monitor v2 DPI awareness
#define WINVER			0x0A00
#define _WIN32_WINNT	0x0A00
#define _WIN32_IE		0x0A00
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atlbase.h>
#include <atlapp.h>
#include <atlstr.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atluser.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlgdi.h>
#include <atlmisc.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#include <WTLDockUI.h>

#if defined _M_IX86
	#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
	#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
