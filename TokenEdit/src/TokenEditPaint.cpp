#include "TokenEditData.h"

#include <commctrl.h>

#include <algorithm>

using namespace TkeDraw;

// ---- Drawing helpers ------------------------------------------------------------------------------

void TkeDraw::FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

void TkeDraw::RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
	WTL::CBrush brush;
	brush.CreateSolidBrush(fill);
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, 1, border);
	auto oldBrush = dc.SelectBrush(brush);
	auto oldPen = dc.SelectPen(pen);
	dc.RoundRect(&rc, { radius, radius });
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

void TkeDraw::Cross(WTL::CDCHandle dc, const RECT& rc, int size, COLORREF color, int width) {
	const int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, q = size / 2;
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, width, color);
	auto old = dc.SelectPen(pen);
	dc.MoveTo(cx - q, cy - q);
	dc.LineTo(cx + q + 1, cy + q + 1);
	dc.MoveTo(cx + q, cy - q);
	dc.LineTo(cx - q - 1, cy + q + 1);
	dc.SelectPen(old);
}

// ---- Painting ---------------------------------------------------------------------------

TKECOLORS CTokenEditCtrl::PaintColors() const {
	return GetColors();
}

LRESULT CTokenEditCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		EnsureLayout();
		Paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	EnsureLayout();
	Paint(mdc.m_hDC);
	return 0;
}

void CTokenEditCtrl::Paint(WTL::CDCHandle dc) {
	auto& d = *m_Data;
	auto c = PaintColors();
	RECT rc;
	GetClientRect(&rc);
	const bool enabled = IsWindowEnabled() != FALSE;
	const HWND focus = ::GetFocus();
	const bool focused = enabled && (focus == m_hWnd || focus == m_Edit.m_hWnd);

	FillSolid(dc, rc, c.crBack);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	const int saved = dc.SaveDC();
	dc.IntersectClipRect(rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
	for (int i = 0; i < (int)d.tokens.Size() && i < (int)d.layout.tokens.size(); i++)
		PaintToken(dc, i, c, -d.scroll);

	// the content scrolls: a thin thumb at the right shows where
	if (d.layout.height > rc.bottom && rc.bottom > 0) {
		const int track = rc.bottom - 2 * d.m.padding;
		const int thumb = std::max(Scale(8), (int)(track * rc.bottom / d.layout.height));
		const int top = d.m.padding + (track - thumb) * d.scroll / std::max(1, d.layout.height - (int)rc.bottom);
		const int width = std::max(2, Scale(2));
		RECT r{ rc.right - 1 - width - 1, top, rc.right - 2, top + thumb };
		FillSolid(dc, r, c.crBorder);
	}
	dc.RestoreDC(saved);

	// the frame, and the focus line along its bottom as Windows 11's edit boxes
	{
		WTL::CBrush brush;
		brush.CreateSolidBrush(enabled ? c.crBorder : Blend(c.crBack, c.crBorder, 60));
		dc.FrameRect(&rc, brush);
	}
	if (focused) {
		const int thick = std::max(2, Scale(2));
		FillSolid(dc, { rc.left, rc.bottom - thick, rc.right, rc.bottom }, c.crAccent);
	}
	dc.SelectFont(oldFont);
}

void CTokenEditCtrl::PaintToken(WTL::CDCHandle dc, int index, const TKECOLORS& c, int dy) {
	auto& d = *m_Data;
	auto& t = d.tokens[index];
	TkeTokenBox box = d.layout.tokens[index];
	for (RECT* r : { &box.rc, &box.icon, &box.text, &box.close })
		if (!::IsRectEmpty(r))
			::OffsetRect(r, 0, dy);
	const bool enabled = IsWindowEnabled() != FALSE;
	const bool focused = ::GetFocus() == m_hWnd;
	const bool hot = enabled && d.hot.index == index && (d.hot.part == TkePart::Token || d.hot.part == TkePart::Close);

	COLORREF back = t.back != CLR_DEFAULT ? t.back : t.invalid ? c.crInvalidBack : c.crTokenBack;
	COLORREF fore = t.fore != CLR_DEFAULT ? t.fore
		: t.invalid ? c.crInvalid
		: t.back != CLR_DEFAULT ? (IsDark(t.back) ? RGB(255, 255, 255) : RGB(0, 0, 0))
		: c.crTokenText;
	COLORREF border = back;
	if (t.selected) {
		back = focused ? c.crSelectedBack : Blend(back, c.crSelectedBack, 55);
		if (!t.invalid && t.fore == CLR_DEFAULT)
			fore = c.crSelectedText;
		border = focused ? c.crAccent : back;
	}
	else if (hot)
		back = border = t.back == CLR_DEFAULT && !t.invalid ? c.crTokenHot : Blend(back, fore, 12);
	if (!enabled)
		fore = c.crCue;
	RoundFill(dc, box.rc, back, border, Scale(8));

	if (m_Images && !::IsRectEmpty(&box.icon))
		::ImageList_DrawEx(m_Images, t.image, dc, box.icon.left, box.icon.top, 0, 0, CLR_NONE, CLR_NONE,
			enabled ? ILD_TRANSPARENT : ILD_TRANSPARENT | ILD_BLEND50);

	dc.SetTextColor(fore);
	RECT text = box.text;
	dc.DrawText(t.text.c_str(), (int)t.text.size(), &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
	if (t.invalid) {
		const int y = (box.text.top + box.text.bottom + m_TextHeight) / 2;
		auto points = TkeWave(box.text.left, std::min((int)box.text.right, (int)box.text.left + t.textWidth), y,
			std::max(1, Scale(1)), std::max(2, Scale(2)));
		if (points.size() > 1) {
			WTL::CPen pen;
			pen.CreatePen(PS_SOLID, 1, t.fore != CLR_DEFAULT ? t.fore : c.crInvalid);
			auto old = dc.SelectPen(pen);
			dc.Polyline(points.data(), (int)points.size());
			dc.SelectPen(old);
		}
	}

	if (!::IsRectEmpty(&box.close)) {
		const bool closeHot = enabled && d.hot.part == TkePart::Close && d.hot.index == index;
		const bool pressed = closeHot && d.pressed.part == TkePart::Close && d.pressed.index == index;
		if (closeHot) {
			COLORREF shade = Blend(back, fore, pressed ? 28 : 16);
			RoundFill(dc, box.close, shade, shade, box.close.right - box.close.left);
		}
		Cross(dc, box.close, (box.close.right - box.close.left) * 7 / 16, fore, std::max(1, Scale(1)));
	}
}
