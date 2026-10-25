#include "BreadcrumbData.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>

using namespace BrcDraw;

// ---- Drawing helpers ------------------------------------------------------------------------------

void BrcDraw::FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

void BrcDraw::RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
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

void BrcDraw::Chevron(WTL::CDCHandle dc, int cx, int cy, int size, UINT vk, COLORREF color, int width) {
	int h = size / 2, q = size / 4;
	POINT pts[3];
	switch (vk) {
		case VK_UP: pts[0] = { cx - h, cy + q }; pts[1] = { cx, cy - q }; pts[2] = { cx + h, cy + q }; break;
		case VK_DOWN: pts[0] = { cx - h, cy - q }; pts[1] = { cx, cy + q }; pts[2] = { cx + h, cy - q }; break;
		case VK_LEFT: pts[0] = { cx + q, cy - h }; pts[1] = { cx - q, cy }; pts[2] = { cx + q, cy + h }; break;
		default: pts[0] = { cx - q, cy - h }; pts[1] = { cx + q, cy }; pts[2] = { cx - q, cy + h }; break;
	}
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, width, color);
	auto old = dc.SelectPen(pen);
	dc.Polyline(pts, 3);
	dc.SelectPen(old);
}

void BrcDraw::Arrow(WTL::CDCHandle dc, int cx, int cy, int size, UINT vk, COLORREF color, int width) {
	// d: the way it points, p: across it
	int dx = vk == VK_LEFT ? -1 : vk == VK_RIGHT ? 1 : 0;
	int dy = vk == VK_UP ? -1 : vk == VK_DOWN ? 1 : 0;
	int px = -dy, py = dx;
	int h = size / 2, a = size * 2 / 5;
	POINT tip{ cx + dx * h, cy + dy * h };
	POINT tail{ cx - dx * h, cy - dy * h };
	POINT head[3] = { { tip.x - dx * a + px * a, tip.y - dy * a + py * a }, tip, { tip.x - dx * a - px * a, tip.y - dy * a - py * a } };
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, width, color);
	auto old = dc.SelectPen(pen);
	dc.MoveTo(tail);
	dc.LineTo(tip);
	dc.Polyline(head, 3);
	dc.SelectPen(old);
}

void BrcDraw::Glyph(WTL::CDCHandle dc, UINT glyph, const RECT& rc, int size, COLORREF color, int width) {
	const int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, h = size / 2;
	WTL::CPen pen;
	pen.CreatePen(PS_SOLID, width, color);
	auto oldPen = dc.SelectPen(pen);
	auto oldBrush = dc.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
	switch (glyph) {
		case BRCGLYPH_REFRESH: {
			// a circle open at the top right, the arrow's head at its upper end turning clockwise
			const double pi = 3.14159265358979;
			auto at = [&](double degrees) {
				return POINT{ cx + (int)std::lround(h * std::cos(degrees * pi / 180)), cy - (int)std::lround(h * std::sin(degrees * pi / 180)) };
			};
			POINT start = at(60), end = at(15);
			dc.Arc(cx - h, cy - h, cx + h, cy + h, start.x, start.y, end.x, end.y);
			const int a = std::max(2, size / 3);
			POINT head[3] = { { start.x - a, start.y - a / 2 }, start, { start.x - a / 3, start.y + a } };
			dc.Polyline(head, 3);
			break;
		}
		case BRCGLYPH_GO:
			dc.SelectPen(oldPen);
			Arrow(dc, cx, cy, size, VK_RIGHT, color, width);
			dc.SelectPen(pen);
			break;
		case BRCGLYPH_CLOSE: {
			int q = size * 3 / 8;
			dc.MoveTo(cx - q, cy - q);
			dc.LineTo(cx + q + 1, cy + q + 1);
			dc.MoveTo(cx + q, cy - q);
			dc.LineTo(cx - q - 1, cy + q + 1);
			break;
		}
		case BRCGLYPH_SEARCH: {
			int r = size * 3 / 8, ox = cx - size / 8, oy = cy - size / 8;
			dc.Ellipse(ox - r, oy - r, ox + r + 1, oy + r + 1);
			int k = (int)std::lround(r * 0.7);
			dc.MoveTo(ox + k, oy + k);
			dc.LineTo(cx + h, cy + h);
			break;
		}
		default:
			break;
	}
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

// ---- Painting ---------------------------------------------------------------------------

LRESULT CBreadcrumbCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
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

void CBreadcrumbCtrl::Paint(WTL::CDCHandle dc) {
	auto& d = *m_Data;
	auto& l = d.layout;
	auto c = GetColors();
	RECT rc;
	GetClientRect(&rc);
	FillSolid(dc, rc, c.crBack);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);

	PaintNav(dc, c);
	if (!::IsRectEmpty(&l.box)) {
		RoundFill(dc, l.box, c.crBox, d.editing || d.hotBox || d.opened.part != BrcPart::None ? c.crHotBorder : c.crBorder, Scale(8));
		if (d.editing) {
			// the focus: an accent line along the box's bottom, as Windows 11's edit boxes
			const int radius = Scale(4), thick = std::max(2, Scale(2));
			FillSolid(dc, { l.box.left + radius, l.box.bottom - thick, l.box.right - radius, l.box.bottom }, c.crAccent);
		}
		// the location's icon: the last segment's
		if (m_Images && !::IsRectEmpty(&l.icon) && !d.segments.empty() && d.segments.back().image >= 0)
			::ImageList_Draw(m_Images, d.segments.back().image, dc, l.icon.left, l.icon.top, ILD_TRANSPARENT);
		const int saved = dc.SaveDC();
		dc.IntersectClipRect(l.box.left + 1, l.box.top + 1, l.box.right - 1, l.box.bottom - 1);
		if (!d.editing)   // the edit box is over them
			PaintSegments(dc, c);
		PaintButtons(dc, c);
		dc.RestoreDC(saved);
	}
	PaintFocus(dc, c);
	dc.SelectFont(oldFont);
}

void CBreadcrumbCtrl::PaintButtonBack(WTL::CDCHandle dc, const RECT& rc, const BrcHit& part, const BRCCOLORS& c) {
	auto& d = *m_Data;
	const bool open = d.opened == part && part.part != BrcPart::None;
	const bool pressed = d.pressed == part && d.hot == part;
	if (!open && !pressed && !(d.hot == part))
		return;
	RECT r{ rc.left, rc.top + Scale(2), rc.right, rc.bottom - Scale(2) };
	COLORREF back = open || pressed ? c.crPressedBack : c.crHotBack;
	RoundFill(dc, r, back, back, Scale(6));
}

void CBreadcrumbCtrl::PaintNav(WTL::CDCHandle dc, const BRCCOLORS& c) {
	auto& l = m_Data->layout;
	const int size = Scale(12), width = std::max(1, Scale(1));
	// the buttons are on the bar's background: their hot and pressed shades are of it
	BRCCOLORS bar = c;
	bar.crHotBack = Blend(c.crBack, c.crText, IsDark(c.crBack) ? 12 : 8);
	bar.crPressedBack = Blend(c.crBack, c.crText, IsDark(c.crBack) ? 20 : 15);
	auto paint = [&](const RECT& r, BrcPart part, auto&& glyph) {
		if (::IsRectEmpty(&r))
			return;
		BrcHit hit{ part };
		PaintButtonBack(dc, r, hit, bar);
		glyph((r.left + r.right) / 2, (r.top + r.bottom) / 2, Enabled(hit) ? c.crText : c.crDimText);
	};
	paint(l.back, BrcPart::Back, [&](int x, int y, COLORREF color) { Arrow(dc, x, y, size, VK_LEFT, color, width); });
	paint(l.forward, BrcPart::Forward, [&](int x, int y, COLORREF color) { Arrow(dc, x, y, size, VK_RIGHT, color, width); });
	paint(l.recent, BrcPart::Recent, [&](int x, int y, COLORREF color) { Chevron(dc, x, y, Scale(8), VK_DOWN, color, width); });
	paint(l.up, BrcPart::Up, [&](int x, int y, COLORREF color) { Arrow(dc, x, y, size, VK_UP, color, width); });
}

void CBreadcrumbCtrl::PaintSegments(WTL::CDCHandle dc, const BRCCOLORS& c) {
	auto& d = *m_Data;
	auto& l = d.layout;
	const int width = std::max(1, Scale(1)), chevron = Scale(8);

	if (!::IsRectEmpty(&l.overflow)) {
		BrcHit part{ BrcPart::Overflow };
		PaintButtonBack(dc, l.overflow, part, c);
		int x = (l.overflow.left + l.overflow.right) / 2, y = (l.overflow.top + l.overflow.bottom) / 2;
		if (d.opened == part)
			Chevron(dc, x, y, chevron, VK_DOWN, c.crText, width);
		else if (l.Overflowed()) {
			// << : segments folded into it
			Chevron(dc, x - Scale(2), y, chevron, VK_LEFT, c.crText, width);
			Chevron(dc, x + Scale(2), y, chevron, VK_LEFT, c.crText, width);
		}
		else
			Chevron(dc, x, y, chevron, VK_RIGHT, c.crText, width);
	}

	for (int i = l.firstVisible; i < (int)d.segments.size(); i++) {
		auto& s = d.segments[i];
		const RECT& t = l.text[i];
		const RECT& v = l.chevron[i];
		BrcHit text{ BrcPart::Text, i }, arrow{ BrcPart::Chevron, i };
		const bool open = d.opened == arrow;
		const bool textHot = d.hot == text, arrowHot = d.hot == arrow;
		// the segment is a split button: the part under the mouse hot, the other a shade of it
		if (textHot || arrowHot || open) {
			COLORREF half = Blend(c.crBox, c.crHotBack, 55);
			RECT r{ t.left, t.top + Scale(2), t.right, t.bottom - Scale(2) };
			COLORREF tb = (d.pressed == text && textHot) ? c.crPressedBack : textHot ? c.crHotBack : half;
			RoundFill(dc, r, tb, tb, Scale(6));
			if (!::IsRectEmpty(&v)) {
				RECT a{ v.left + 1, v.top + Scale(2), v.right, v.bottom - Scale(2) };
				COLORREF ab = open ? c.crPressedBack : arrowHot ? c.crHotBack : half;
				RoundFill(dc, a, ab, ab, Scale(6));
			}
		}
		RECT tr{ t.left + d.m.textPad, t.top, t.right - (l.truncated && i == (int)d.segments.size() - 1 ? Scale(2) : 0), t.bottom };
		dc.SetTextColor(c.crText);
		dc.DrawText(s.text.c_str(), (int)s.text.size(), &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		if (!::IsRectEmpty(&v))
			Chevron(dc, (v.left + v.right) / 2 + (open ? 0 : 1), (v.top + v.bottom) / 2, chevron, open ? VK_DOWN : VK_RIGHT, c.crText, width);
	}
}

void CBreadcrumbCtrl::PaintButtons(WTL::CDCHandle dc, const BRCCOLORS& c) {
	auto& d = *m_Data;
	auto& l = d.layout;
	const int width = std::max(1, Scale(1));
	if (!::IsRectEmpty(&l.history)) {
		BrcHit part{ BrcPart::History };
		PaintButtonBack(dc, l.history, part, c);
		Chevron(dc, (l.history.left + l.history.right) / 2, (l.history.top + l.history.bottom) / 2, Scale(8), VK_DOWN,
			d.history.Paths().empty() ? c.crDimText : c.crText, width);
	}
	for (int i = 0; i < (int)d.buttons.size() && i < (int)l.actions.size(); i++) {
		auto& b = d.buttons[i];
		const RECT& r = l.actions[i];
		const bool disabled = (b.state & BRCBS_DISABLED) != 0;
		BrcHit part{ BrcPart::Action, i };
		if ((b.state & BRCBS_CHECKED) && !(d.hot == part)) {
			RECT back{ r.left, r.top + Scale(2), r.right, r.bottom - Scale(2) };
			RoundFill(dc, back, c.crPressedBack, c.crPressedBack, Scale(6));
		}
		PaintButtonBack(dc, r, part, c);
		const int icon = d.m.icon;
		if (b.image >= 0 && m_Images)
			::ImageList_DrawEx(m_Images, b.image, dc, (r.left + r.right - icon) / 2, (r.top + r.bottom - icon) / 2, 0, 0,
				CLR_NONE, CLR_NONE, disabled ? ILD_TRANSPARENT | ILD_BLEND50 : ILD_TRANSPARENT);
		else
			Glyph(dc, b.glyph, r, Scale(12), disabled ? c.crDimText : c.crText, width);
	}
}
