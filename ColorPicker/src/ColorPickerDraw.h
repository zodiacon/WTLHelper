#pragma once

#include "ColorPicker.h"
#include "ColorPickerModel.h"

#include <algorithm>
#include <vector>

// GDI helpers shared by the panel, the swatches and the color button.

namespace CpDraw {
	inline bool IsEmpty(const RECT& rc) {
		return rc.right <= rc.left || rc.bottom <= rc.top;
	}

	inline COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	inline bool IsDark(COLORREF c) {
		return GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114 < 128 * 1000;
	}

	inline void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
		::SetBkColor(hdc, color);
		::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	inline void Frame(HDC hdc, const RECT& rc, COLORREF color, int thickness = 1) {
		FillSolid(hdc, { rc.left, rc.top, rc.right, rc.top + thickness }, color);
		FillSolid(hdc, { rc.left, rc.bottom - thickness, rc.right, rc.bottom }, color);
		FillSolid(hdc, { rc.left, rc.top, rc.left + thickness, rc.bottom }, color);
		FillSolid(hdc, { rc.right - thickness, rc.top, rc.right, rc.bottom }, color);
	}

	// pixels drawn by the model, put on the DC at rc
	template<typename Fill>
	void DrawPixels(HDC hdc, const RECT& rc, Fill&& fill) {
		int w = rc.right - rc.left, h = rc.bottom - rc.top;
		if (w <= 0 || h <= 0)
			return;
		std::vector<uint32_t> bits((size_t)w * h);
		fill(CpPixels{ bits.data(), w, h });
		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = w;
		bmi.bmiHeader.biHeight = -h;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		::SetDIBitsToDevice(hdc, rc.left, rc.top, w, h, 0, 0, 0, h, bits.data(), &bmi, DIB_RGB_COLORS);
	}

	// a small arrow pointing up or down, centered in rc
	inline void Arrow(HDC hdc, const RECT& rc, bool up, int width, COLORREF color) {
		int height = std::max(2, width / 2 + 1);   // rows from the tip: one pixel, then two more each row
		int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
		int tip = up ? cy - height / 2 : cy + (height - 1) / 2;
		for (int i = 0; i < height; i++) {
			int y = up ? tip + i : tip - i;
			FillSolid(hdc, { cx - i, y, cx + i + 1, y + 1 }, color);
		}
	}

	// the colors that follow the system (and WTLHelper's dark mode, which hooks COLOR_WINDOW, COLOR_3DFACE
	// and COLOR_WINDOWTEXT; read each time)
	inline CPKCOLORS SystemColors() {
		CPKCOLORS c;
		// high contrast: the theme's text on its window background everywhere, frames in full contrast
		HIGHCONTRASTW hc{ sizeof(hc) };
		if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
			c.crBack = c.crFieldBack = ::GetSysColor(COLOR_WINDOW);
			c.crText = c.crBorder = ::GetSysColor(COLOR_WINDOWTEXT);
			c.crCheck1 = RGB(0xFF, 0xFF, 0xFF);
			c.crCheck2 = RGB(0x80, 0x80, 0x80);
			return c;
		}
		c.crBack = ::GetSysColor(COLOR_3DFACE);
		c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
		c.crFieldBack = ::GetSysColor(COLOR_WINDOW);
		bool dark = IsDark(c.crBack);
		c.crBorder = Blend(c.crBack, dark ? RGB(255, 255, 255) : RGB(0, 0, 0), dark ? 30 : 35);
		c.crCheck1 = dark ? RGB(0x5A, 0x5A, 0x5A) : RGB(0xFF, 0xFF, 0xFF);
		c.crCheck2 = dark ? RGB(0x3A, 0x3A, 0x3A) : RGB(0xCC, 0xCC, 0xCC);
		return c;
	}
}
