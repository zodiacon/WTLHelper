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

	// a chevron centered on (cx, cy), pointing the way of an arrow key
	void Chevron(WTL::CDCHandle dc, int cx, int cy, int size, UINT vk, COLORREF color, int width) {
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

void CRibbonBarCtrl::Paint(WTL::CDCHandle dc, const RECT&) {
	auto& d = *m_Data;
	auto c = GetColors();
	RECT rc;
	GetClientRect(&rc);
	FillSolid(dc, rc, c.crBack);
	auto oldFont = dc.SelectFont(m_hFont);
	dc.SetBkMode(TRANSPARENT);
	PaintTabs(dc, c);
	if (!m_Minimized)
		PaintSurface(dc, d.main, c);
	FillSolid(dc, { 0, d.main.panel.bottom, rc.right, d.main.panel.bottom + 1 }, c.crLine);
	dc.SelectFont(oldFont);
}

void CRibbonBarCtrl::PaintTabs(WTL::CDCHandle dc, const RIBCOLORS& c) {
	auto& d = *m_Data;
	for (int v = 0; v < (int)d.visible.size(); v++) {
		int t = d.visible[v];
		auto& tab = d.tabs[t];
		RECT rc = d.tabRects[v];
		bool selected = t == d.selected;
		COLORREF accent = c.crAccent, text = c.crTabText;
		if (auto context = tab.context ? d.FindContext(tab.context) : nullptr) {
			// a contextual tab: tinted, a band of its set's color on top
			FillSolid(dc, rc, Blend(c.crBack, context->color, 14));
			FillSolid(dc, { rc.left, rc.top, rc.right, rc.top + Scale(3) }, context->color);
			accent = context->color;
		}
		if (v == d.hotTab && !selected) {
			RECT hot = rc;
			::InflateRect(&hot, -Scale(2), -Scale(4));
			COLORREF back = Blend(c.crBack, c.crTabText, 10);
			RoundFill(dc, hot, back, back, Scale(4));
		}
		dc.SetTextColor(selected ? accent : text);
		dc.DrawText(tab.text.c_str(), (int)tab.text.size(), &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (selected && !(m_Minimized && d.popups.empty())) {
			// the mark under the selected tab's text (minimized: while it's open)
			int w = t < (int)d.tabWidths.size() ? d.tabWidths[t] : 0;
			int x = (rc.left + rc.right - w) / 2;
			RECT bar{ x, rc.bottom - Scale(3), x + w, rc.bottom };
			RoundFill(dc, bar, accent, accent, Scale(2));
		}
	}
	// the minimize button: a chevron up (minimize), down while minimized (show the groups again)
	if (auto& b = d.minimizeButton; b.right > b.left) {
		if (d.hotTab == RibData::MinimizeButton) {
			RECT hot = b;
			::InflateRect(&hot, -Scale(3), -Scale(3));
			COLORREF back = Blend(c.crBack, c.crTabText, 10);
			RoundFill(dc, hot, back, back, Scale(4));
		}
		Chevron(dc, (b.left + b.right) / 2, (b.top + b.bottom) / 2, Scale(10), m_Minimized ? VK_DOWN : VK_UP, c.crTabText,
			std::max(1, Scale(1)));
	}
}

void CRibbonBarCtrl::PaintSurface(WTL::CDCHandle dc, RibSurface& s, const RIBCOLORS& c) {
	auto& d = *m_Data;
	FillSolid(dc, s.panel, c.crPanel);
	if (s.tab < 0 || s.tab >= (int)d.tabs.size())
		return;
	if (auto context = d.tabs[s.tab].context ? d.FindContext(d.tabs[s.tab].context) : nullptr)
		FillSolid(dc, { s.panel.left, s.panel.top, s.panel.right, s.panel.top + std::max(1, Scale(2)) }, context->color);
	int last = -1;
	for (int g = 0; g < (int)s.places.size(); g++)
		if (s.places[g].width > 0)
			last = g;
	int saved = dc.SaveDC();
	dc.IntersectClipRect(&s.panel);
	RECT clip;
	dc.GetClipBox(&clip);
	for (int g = 0; g <= last; g++) {
		auto& place = s.places[g];
		if (place.width <= 0)
			continue;
		int x = s.GroupX(g);
		if (x < clip.right && x + place.width + 1 > clip.left)
			PaintGroup(dc, s, g, x, c);
		if (g != last)
			FillSolid(dc, { x + place.width, s.panel.top + d.m.groupPad, x + place.width + 1, s.panel.bottom - d.m.groupPad }, c.crLine);
	}
	// the scroll buttons, when the groups don't fit
	int button = Scale(14);
	auto scrollButton = [&](RECT r, UINT vk, RibPart part) {
		bool hot = s.hot.part == part;
		FillSolid(dc, r, hot ? c.crPressedBack : c.crHotBack);
		Chevron(dc, (r.left + r.right) / 2, (r.top + r.bottom) / 2, Scale(10), vk, c.crText, std::max(1, Scale(1)));
	};
	if (s.scroll > 0)
		scrollButton({ s.panel.left, s.panel.top, s.panel.left + button, s.panel.bottom }, VK_LEFT, RibPart::ScrollLeft);
	if (s.scroll < s.MaxScroll())
		scrollButton({ s.panel.right - button, s.panel.top, s.panel.right, s.panel.bottom }, VK_RIGHT, RibPart::ScrollRight);
	dc.RestoreDC(saved);
}

void CRibbonBarCtrl::PaintGroup(WTL::CDCHandle dc, RibSurface& s, int g, int x, const RIBCOLORS& c) {
	auto& d = *m_Data;
	auto& group = d.tabs[s.tab].groups[g];
	auto& place = s.places[g];
	auto& m = d.m;
	int top = s.panel.top;
	if (place.collapsed) {
		// one button: the group's image and its label, with an arrow; pressed while its popup is open
		RECT rc = RibCollapsedRect(place.width, m);
		::OffsetRect(&rc, x, top);
		bool hot = s.hot.group == g && s.hot.part == RibPart::Group;
		if (s.opened == g)
			RoundFill(dc, rc, c.crPressedBack, c.crLine, Scale(4));
		else if (hot)
			RoundFill(dc, rc, c.crHotBack, c.crLine, Scale(4));
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
		PaintItem(dc, s, g, i, rc, c);
	}
	RECT label = place.label;
	::OffsetRect(&label, x, top);
	dc.SetTextColor(c.crDimText);
	dc.DrawText(group.text.c_str(), (int)group.text.size(), &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	if (place.launcher.right > place.launcher.left) {
		RECT r = place.launcher;
		::OffsetRect(&r, x, top);
		bool hot = s.hot.group == g && s.hot.part == RibPart::Launcher;
		if (hot) {
			RECT back = r;
			::InflateRect(&back, Scale(2), Scale(2));
			RoundFill(dc, back, Same(s.pressed, s.hot) ? c.crPressedBack : c.crHotBack, c.crHotBack, Scale(3));
		}
		Launcher(dc, r, hot ? c.crText : c.crDimText);
	}
}

void CRibbonBarCtrl::PaintItem(WTL::CDCHandle dc, RibSurface& s, int g, int i, const RECT& rc, const RIBCOLORS& c) {
	auto& d = *m_Data;
	auto& m = d.m;
	auto& item = d.tabs[s.tab].groups[g].items[i];
	auto size = s.places[g].items[i].size;
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
	bool hot = !disabled && s.hot.group == g && s.hot.item == i && (s.hot.part == RibPart::Item || s.hot.part == RibPart::Arrow);
	bool pressed = hot && Same(s.pressed, s.hot);
	if (item.state & RibStateChecked)
		RoundFill(dc, rc, hot ? Blend(c.crCheckedBack, c.crText, 6) : c.crCheckedBack, c.crCheckedBorder, radius);
	else if (hot && item.kind == RibKind::Split) {
		// both parts lit, the one under the mouse more, a line between them
		RECT main, arrow;
		RibSplitRects(rc, size, m, main, arrow);
		RoundFill(dc, rc, c.crHotBack, c.crHotBack, radius);
		RoundFill(dc, s.hot.part == RibPart::Arrow ? arrow : main, pressed ? c.crPressedBack : Blend(c.crHotBack, c.crPressedBack, 50),
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
