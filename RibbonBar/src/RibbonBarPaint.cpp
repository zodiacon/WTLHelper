#include "RibbonBarData.h"

#include <commctrl.h>

#include <algorithm>

using namespace RibDraw;

// ---- Helpers -----------------------------------------------------------------------

namespace {
	void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
		dc.SetBkColor(color);
		dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	void RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
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

	// a drop-down arrow, pointing down, centered on (cx, cy)
	void Arrow(WTL::CDCHandle dc, int cx, int cy, int width, COLORREF color) {
		int half = width / 2;
		int height = (width + 1) / 2;
		POINT pts[3] = { { cx - half, cy - height / 2 }, { cx + half, cy - height / 2 }, { cx, cy - height / 2 + half } };
		WTL::CBrush brush;
		brush.CreateSolidBrush(color);
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, 1, color);
		auto oldBrush = dc.SelectBrush(brush);
		auto oldPen = dc.SelectPen(pen);
		dc.Polygon(pts, 3);
		dc.SelectBrush(oldBrush);
		dc.SelectPen(oldPen);
	}

	// a large item's label: two lines centered from y, an arrow after the second (or alone on it)
	void LargeLabel(WTL::CDCHandle dc, const RECT& rc, int y, int lineHeight, const std::wstring& line1, const std::wstring& line2,
		bool arrow, const RibMetrics& m, COLORREF color) {
		dc.SetTextColor(color);
		RECT r1{ rc.left, y, rc.right, y + lineHeight };
		dc.DrawText(line1.c_str(), (int)line1.size(), &r1, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP | DT_NOPREFIX);
		int y2 = y + lineHeight, center = (rc.left + rc.right) / 2;
		if (line2.empty()) {
			if (arrow)
				Arrow(dc, center, y2 + lineHeight / 2, m.arrow, color);
			return;
		}
		SIZE size{};
		dc.GetTextExtent(line2.c_str(), (int)line2.size(), &size);
		int total = size.cx + (arrow ? m.gap + m.arrow : 0);
		int x = center - total / 2;
		RECT r2{ x, y2, x + size.cx, y2 + lineHeight };
		dc.DrawText(line2.c_str(), (int)line2.size(), &r2, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP | DT_NOPREFIX);
		if (arrow)
			Arrow(dc, x + size.cx + m.gap + m.arrow / 2, y2 + lineHeight / 2, m.arrow, color);
	}

	// a dialog launcher: a box's top and left sides, an arrow to its bottom right
	void Launcher(WTL::CDCHandle dc, const RECT& r, COLORREF color) {
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, 1, color);
		auto old = dc.SelectPen(pen);
		int head = std::max(3, (int)(r.right - r.left) / 3);
		dc.MoveTo(r.left + 1, r.bottom - head);
		dc.LineTo(r.left + 1, r.top + 1);
		dc.LineTo(r.right - head, r.top + 1);
		dc.MoveTo(r.left + 4, r.top + 4);
		dc.LineTo(r.right - 1, r.bottom - 1);
		dc.MoveTo(r.right - 2, r.bottom - 2 - head);
		dc.LineTo(r.right - 2, r.bottom - 2);
		dc.LineTo(r.right - 3 - head, r.bottom - 2);
		dc.SelectPen(old);
	}
}

// ---- Colors ------------------------------------------------------------------------------

RIBCOLORS CRibbonBarCtrl::GetColors() const {
	if (!m_SystemColors)
		return m_Colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	RIBCOLORS c;
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT), highlight = ::GetSysColor(COLOR_HIGHLIGHT);
	bool dark = IsDark(window);
	c.crBack = Blend(window, text, dark ? 2 : 5);
	c.crTabText = text;
	c.crPanel = Blend(window, text, dark ? 8 : 1);
	c.crText = text;
	c.crDimText = Blend(c.crPanel, text, 58);
	c.crLine = Blend(c.crPanel, text, 16);
	c.crHotBack = Blend(c.crPanel, text, dark ? 12 : 7);
	c.crPressedBack = Blend(c.crPanel, text, dark ? 22 : 15);
	c.crCheckedBack = Blend(c.crPanel, highlight, dark ? 32 : 16);
	c.crCheckedBorder = Blend(c.crPanel, highlight, dark ? 70 : 55);
	c.crAccent = dark ? RGB(0x4E, 0xA8, 0xFF) : RGB(0x00, 0x5F, 0xB8);
	return c;
}

// ---- Painting ---------------------------------------------------------------------------

LRESULT CRibbonBarCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto paint = [&](WTL::CDCHandle dc, const RECT& rcPaint) {
		SendUpdateUI(true);   // the states the app sets now are painted now
		EnsureLayout();
		Paint(dc, rcPaint);
	};
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		GetClientRect(&rc);
		paint((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	paint(mdc.m_hDC, dc.m_ps.rcPaint);
	return 0;
}

void CRibbonBarCtrl::Paint(WTL::CDCHandle dc, const RECT& rcPaint) {
	auto& d = *m_Data;
	auto c = GetColors();
	RECT rc;
	GetClientRect(&rc);
	FillSolid(dc, rc, c.crBack);
	FillSolid(dc, d.panel, c.crPanel);
	FillSolid(dc, { 0, d.panel.bottom, rc.right, d.panel.bottom + 1 }, c.crLine);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);
	PaintTabs(dc, c);
	if (auto sel = d.Selected()) {
		int last = -1;
		for (int g = 0; g < (int)d.places.size(); g++)
			if (d.places[g].width > 0)
				last = g;
		int saved = dc.SaveDC();
		dc.IntersectClipRect(&d.panel);
		for (int g = 0; g <= last; g++) {
			auto& place = d.places[g];
			if (place.width <= 0)
				continue;
			int x = d.originX + place.x;
			if (x < rcPaint.right && x + place.width + 1 > rcPaint.left)
				PaintGroup(dc, g, x, c);
			if (g != last)
				FillSolid(dc, { x + place.width, d.panel.top + d.m.groupPad, x + place.width + 1, d.panel.bottom - d.m.groupPad }, c.crLine);
		}
		dc.RestoreDC(saved);
		(void)sel;
	}
	dc.SelectFont(oldFont);
}

void CRibbonBarCtrl::PaintTabs(WTL::CDCHandle dc, const RIBCOLORS& c) {
	auto& d = *m_Data;
	for (int v = 0; v < (int)d.visible.size(); v++) {
		int t = d.visible[v];
		auto& tab = d.tabs[t];
		RECT rc = d.tabRects[v];
		bool selected = t == d.selected;
		if (v == d.hotTab && !selected) {
			RECT hot = rc;
			::InflateRect(&hot, -Scale(2), -Scale(3));
			COLORREF back = Blend(c.crBack, c.crTabText, 10);
			RoundFill(dc, hot, back, back, Scale(4));
		}
		dc.SetTextColor(selected ? c.crAccent : c.crTabText);
		dc.DrawText(tab.text.c_str(), (int)tab.text.size(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (selected) {
			// the mark under the selected tab's text
			int w = t < (int)d.tabWidths.size() ? d.tabWidths[t] : 0;
			int x = (rc.left + rc.right - w) / 2;
			RECT bar{ x, rc.bottom - Scale(3), x + w, rc.bottom };
			RoundFill(dc, bar, c.crAccent, c.crAccent, Scale(2));
		}
	}
}

void CRibbonBarCtrl::PaintGroup(WTL::CDCHandle dc, int g, int x, const RIBCOLORS& c) {
	auto& d = *m_Data;
	auto& group = d.Selected()->groups[g];
	auto& place = d.places[g];
	auto& m = d.m;
	int top = d.panel.top;
	if (place.collapsed) {
		// one button: the group's image and its label, with an arrow
		RECT rc = RibCollapsedRect(place.width, m);
		::OffsetRect(&rc, x, top);
		bool hot = d.hot.group == g && d.hot.part == RibPart::Group;
		if (hot)
			RoundFill(dc, rc, Same(d.pressed, d.hot) ? c.crPressedBack : c.crHotBack, c.crLine, Scale(4));
		int image = group.image;
		for (size_t i = 0; i < group.items.size() && image < 0; i++)
			if (!group.items[i].Hidden())
				image = group.items[i].image;
		PaintImage(dc, image, true, (rc.left + rc.right - m.largeIcon) / 2, rc.top + m.pad + Scale(4), false);
		LargeLabel(dc, rc, rc.top + m.pad + Scale(4) + m.largeIcon + Scale(2), m_TextHeight - Scale(2), group.line1, group.line2, true, m, c.crText);
		return;
	}
	for (int i = 0; i < (int)group.items.size() && i < (int)place.items.size(); i++) {
		if (group.items[i].Hidden())
			continue;
		RECT rc = place.items[i].rc;
		::OffsetRect(&rc, x, top);
		PaintItem(dc, g, i, rc, c);
	}
	RECT label = place.label;
	::OffsetRect(&label, x, top);
	dc.SetTextColor(c.crDimText);
	dc.DrawText(group.text.c_str(), (int)group.text.size(), &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	if (place.launcher.right > place.launcher.left) {
		RECT r = place.launcher;
		::OffsetRect(&r, x, top);
		bool hot = d.hot.group == g && d.hot.part == RibPart::Launcher;
		if (hot) {
			RECT back = r;
			::InflateRect(&back, Scale(2), Scale(2));
			RoundFill(dc, back, Same(d.pressed, d.hot) ? c.crPressedBack : c.crHotBack, c.crHotBack, Scale(3));
		}
		Launcher(dc, r, hot ? c.crText : c.crDimText);
	}
}

void CRibbonBarCtrl::PaintItem(WTL::CDCHandle dc, int g, int i, const RECT& rc, const RIBCOLORS& c) {
	auto& d = *m_Data;
	auto& m = d.m;
	auto& item = d.Selected()->groups[g].items[i];
	auto size = d.places[g].items[i].size;
	int radius = Scale(4);
	if (item.kind == RibKind::Separator) {
		int mid = (rc.left + rc.right) / 2;
		FillSolid(dc, { mid, rc.top + m.pad, mid + 1, rc.bottom - m.pad }, c.crLine);
		return;
	}
	bool disabled = (item.state & RibStateDisabled) != 0;
	COLORREF text = disabled ? c.crDimText : c.crText;
	if (item.kind == RibKind::Window) {
		// the window paints itself; its label goes before it
		if (size != RibSize::Icon && !item.text.empty()) {
			RECT r{ rc.left, rc.top, rc.right - item.windowWidth, rc.top + m.rowHeight };
			dc.SetTextColor(text);
			dc.DrawText(item.text.c_str(), (int)item.text.size(), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		return;
	}
	bool hot = !disabled && d.hot.group == g && d.hot.item == i && (d.hot.part == RibPart::Item || d.hot.part == RibPart::Arrow);
	bool pressed = hot && Same(d.pressed, d.hot);
	if (item.state & RibStateChecked)
		RoundFill(dc, rc, hot ? Blend(c.crCheckedBack, c.crText, 6) : c.crCheckedBack, c.crCheckedBorder, radius);
	else if (hot && item.kind == RibKind::Split) {
		// both parts lit, the one under the mouse more, a line between them
		RECT main, arrow;
		RibSplitRects(rc, size, m, main, arrow);
		RoundFill(dc, rc, c.crHotBack, c.crHotBack, radius);
		RoundFill(dc, d.hot.part == RibPart::Arrow ? arrow : main, pressed ? c.crPressedBack : Blend(c.crHotBack, c.crPressedBack, 50),
			c.crHotBack, radius);
		if (size == RibSize::Large)
			FillSolid(dc, { rc.left + Scale(2), arrow.top, rc.right - Scale(2), arrow.top + 1 }, c.crLine);
		else
			FillSolid(dc, { arrow.left, rc.top + Scale(2), arrow.left + 1, rc.bottom - Scale(2) }, c.crLine);
	}
	else if (hot)
		RoundFill(dc, rc, pressed ? c.crPressedBack : c.crHotBack, pressed ? c.crPressedBack : c.crHotBack, radius);

	if (size == RibSize::Large) {
		int y = rc.top + m.pad;
		if (item.image >= 0)
			PaintImage(dc, item.image, true, (rc.left + rc.right - m.largeIcon) / 2, y, disabled);
		else   // only a small image: in the middle of the large one's place
			PaintImage(dc, item.smallImage, false, (rc.left + rc.right - m.smallIcon) / 2, y + (m.largeIcon - m.smallIcon) / 2, disabled);
		LargeLabel(dc, rc, y + m.largeIcon + Scale(2), m_TextHeight - Scale(2), item.line1, item.line2, item.HasArrow(), m, text);
		return;
	}
	int x = rc.left + m.pad;
	if (item.HasImage()) {
		PaintImage(dc, item.smallImage, false, x, rc.top + (m.rowHeight - m.smallIcon) / 2, disabled);
		x += m.smallIcon + m.gap;
	}
	if (size == RibSize::Small && !item.text.empty()) {
		RECT r{ x, rc.top, rc.right, rc.top + m.rowHeight };
		dc.SetTextColor(text);
		dc.DrawText(item.text.c_str(), (int)item.text.size(), &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}
	if (item.HasArrow()) {
		RECT main, arrow;
		RibSplitRects(rc, size, m, main, arrow);
		int cx = item.kind == RibKind::Split ? (arrow.left + arrow.right) / 2 : rc.right - m.pad - m.arrow / 2 - 1;
		Arrow(dc, cx, (rc.top + rc.bottom) / 2, m.arrow, text);
	}
}

void CRibbonBarCtrl::PaintImage(WTL::CDCHandle dc, int image, bool large, int x, int y, bool disabled) {
	HIMAGELIST list = large ? m_Large : m_Small;
	if (!list || image < 0)
		return;
	IMAGELISTDRAWPARAMS p{ sizeof(p) };
	p.himl = list;
	p.i = image;
	p.hdcDst = dc;
	p.x = x;
	p.y = y;
	p.rgbBk = CLR_NONE;
	p.rgbFg = CLR_NONE;
	p.fStyle = ILD_TRANSPARENT;
	if (disabled) {
		p.fState = ILS_ALPHA;   // faded
		p.Frame = 96;
	}
	::ImageList_DrawIndirect(&p);
}
