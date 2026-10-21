#pragma once

#include <atltypes.h>

//
// helpers for per-monitor (v2) DPI awareness.
// Sizes in the library are given at 96 DPI (logical pixels) and scaled to the DPI of the window they are used in.
// In a process that is not per-monitor aware these return the system DPI (or 96 for a DPI unaware one),
// so the same code is right in every awareness mode.
// A window learns that its DPI changed from WM_DPICHANGED (top-level) or WM_DPICHANGED_AFTERPARENT (child windows);
// the new DPI is then already returned by GetDpi.
//
struct DpiHelper final {
	static UINT GetDpi(HWND hWnd) noexcept {
		UINT dpi = hWnd ? ::GetDpiForWindow(hWnd) : 0;
		return dpi ? dpi : ::GetDpiForSystem();
	}

	// a size at 96 DPI scaled to dpi
	static int Scale(int value, UINT dpi) noexcept {
		return ::MulDiv(value, (int)dpi, USER_DEFAULT_SCREEN_DPI);
	}

	static int Scale(int value, HWND hWnd) noexcept {
		return Scale(value, GetDpi(hWnd));
	}

	// a size at one DPI scaled to another
	static int Scale(int value, UINT toDpi, UINT fromDpi) noexcept {
		return ::MulDiv(value, (int)toDpi, (int)fromDpi);
	}

	static CSize Scale(CSize const& size, UINT toDpi, UINT fromDpi) noexcept {
		return CSize(Scale(size.cx, toDpi, fromDpi), Scale(size.cy, toDpi, fromDpi));
	}

	static CRect Scale(CRect const& rc, UINT toDpi, UINT fromDpi) noexcept {
		return CRect(Scale(rc.left, toDpi, fromDpi), Scale(rc.top, toDpi, fromDpi), Scale(rc.right, toDpi, fromDpi), Scale(rc.bottom, toDpi, fromDpi));
	}

	static int GetSystemMetrics(int index, UINT dpi) noexcept {
		return ::GetSystemMetricsForDpi(index, dpi);
	}

	static int GetSystemMetrics(int index, HWND hWnd) noexcept {
		return ::GetSystemMetricsForDpi(index, GetDpi(hWnd));
	}

	// the width (and height) of a small icon (16 at 96 DPI)
	static int SmallIconSize(UINT dpi) noexcept {
		return ::GetSystemMetricsForDpi(SM_CXSMICON, dpi);
	}

	static int SmallIconSize(HWND hWnd) noexcept {
		return SmallIconSize(GetDpi(hWnd));
	}

	// the width (and height) of a large icon (32 at 96 DPI)
	static int IconSize(UINT dpi) noexcept {
		return ::GetSystemMetricsForDpi(SM_CXICON, dpi);
	}

	static NONCLIENTMETRICS GetNonClientMetrics(UINT dpi) noexcept {
		NONCLIENTMETRICS ncm{ sizeof(ncm) };
		ATLVERIFY(::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi));
		return ncm;
	}

	// the font of AtlCreateControlFont (the icon title font) at dpi; the caller owns the font
	static HFONT CreateControlFont(UINT dpi) noexcept {
		LOGFONT lf{};
		ATLVERIFY(::SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0, dpi));
		return ::CreateFontIndirect(&lf);
	}

	// the menu font at dpi; the caller owns the font
	static HFONT CreateMenuFont(UINT dpi) noexcept {
		auto ncm = GetNonClientMetrics(dpi);
		return ::CreateFontIndirect(&ncm.lfMenuFont);
	}

	// the message box font (the usual dialog font) at dpi; the caller owns the font
	static HFONT CreateMessageFont(UINT dpi) noexcept {
		auto ncm = GetNonClientMetrics(dpi);
		return ::CreateFontIndirect(&ncm.lfMessageFont);
	}

	// a font like hFont (created at fromDpi) for toDpi; the caller owns the new font
	static HFONT ScaleFont(HFONT hFont, UINT toDpi, UINT fromDpi) noexcept {
		LOGFONT lf{};
		if (::GetObject(hFont, sizeof(lf), &lf) != sizeof(lf))
			return nullptr;
		lf.lfHeight = Scale(lf.lfHeight, toDpi, fromDpi);
		lf.lfWidth = Scale(lf.lfWidth, toDpi, fromDpi);
		return ::CreateFontIndirect(&lf);
	}
};
