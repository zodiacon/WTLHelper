#pragma once

// Tiny test harness shared by the test files (same approach as GraphControlTests: no framework).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <string>

#include "WTLDock.h"

inline int g_checks = 0;
inline int g_failures = 0;
inline const wchar_t* g_currentTest = L"";

inline void Check(bool ok, const char* expr, int line) {
	g_checks++;
	if (!ok) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n", g_currentTest, line, expr);
	}
}

inline void CheckStr(const std::wstring& actual, const std::wstring& expected, const char* expr, int line) {
	g_checks++;
	if (actual != expected) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S\n        got:  %s\n        want: %s\n", g_currentTest, line, expr, actual.c_str(), expected.c_str());
	}
}

inline void CheckValid(const WTLDock::DockLayout& layout, int line) {
	g_checks++;
	std::wstring error;
	if (!layout.Validate(&error)) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  layout invariant broken: %s\n        %s\n", g_currentTest, line, error.c_str(), layout.Dump().c_str());
	}
}

inline void CheckRect(const RECT& r, int l, int t, int rr, int b, const char* expr, int line) {
	g_checks++;
	if (r.left != l || r.top != t || r.right != rr || r.bottom != b) {
		g_failures++;
		wprintf(L"  FAIL  %s:%d  %S  got (%d,%d,%d,%d) want (%d,%d,%d,%d)\n", g_currentTest, line, expr,
			r.left, r.top, r.right, r.bottom, l, t, rr, b);
	}
}

#define CHECK(expr)				Check((expr), #expr, __LINE__)
#define CHECK_STR(a, b)			CheckStr((a), (b), #a " == " #b, __LINE__)
#define CHECK_DUMP(layout, ...)	CheckStr((layout).Dump(), std::wstring(__VA_ARGS__), #layout ".Dump()", __LINE__)
#define CHECK_VALID(layout)		CheckValid((layout), __LINE__)
#define CHECK_RECT(r, l, t, rr, b)	CheckRect((r), (l), (t), (rr), (b), #r, __LINE__)

#define TEST(name)															\
	static void name();														\
	static void Run_##name() {												\
		g_currentTest = L## #name;											\
		const int before = g_failures;										\
		name();																\
		wprintf(L"%s %s\n", before == g_failures ? L"  ok  " : L"  --  ", L## #name);	\
	}																		\
	static void name()

// window tests (ui_tests.cpp)
void RunUiTests();
