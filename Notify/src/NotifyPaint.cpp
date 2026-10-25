#include "NotifyData.h"

#include <commctrl.h>

#include <algorithm>

using namespace NtfDraw;

bool NtfDraw::HighContrast = false;

const wchar_t* NtfDraw::DefaultString(UINT which) {
	static const wchar_t* const Strings[NTFSTR_COUNT] = { L"Show details", L"Hide details", L"%d more", L"Show fewer", L"Close",
		L"Notifications" };
	return which < NTFSTR_COUNT ? Strings[which] : L"";
}

namespace {
	// the severities' glyphs in Segoe Fluent Icons (and Segoe MDL2 Assets): Info, Completed, Warning, ErrorBadge
	const wchar_t* const SeverityGlyphs[4] = { L"\xE946", L"\xE930", L"\xE7BA", L"\xEA39" };

	HFONT IconFont(int size) {
		for (auto face : { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }) {
			LOGFONTW lf{};
			lf.lfHeight = -size;
			lf.lfWeight = FW_NORMAL;
			lf.lfCharSet = DEFAULT_CHARSET;
			lf.lfQuality = CLEARTYPE_QUALITY;
			wcscpy_s(lf.lfFaceName, face);
			HFONT font = ::CreateFontIndirectW(&lf);
			// the system gives another font for a face it doesn't have
			HDC dc = ::GetDC(nullptr);
			auto old = ::SelectObject(dc, font);
			wchar_t got[LF_FACESIZE]{};
			::GetTextFaceW(dc, LF_FACESIZE, got);
			::SelectObject(dc, old);
			::ReleaseDC(nullptr, dc);
			if (!_wcsicmp(got, face))
				return font;
			::DeleteObject(font);
		}
		return nullptr;
	}

	void Line(WTL::CDCHandle dc, int x1, int y1, int x2, int y2) {
		dc.MoveTo(x1, y1);
		dc.LineTo(x2, y2);
	}
}

// ---- Fonts and messages -----------------------------------------------------------------------------

void NtfFonts::Create(HFONT base, int iconSize) {
	Destroy();
	normal = base;
	LOGFONTW lf{};
	::GetObjectW(base, sizeof(lf), &lf);
	LOGFONTW b = lf;
	b.lfWeight = FW_SEMIBOLD;
	bold = ::CreateFontIndirectW(&b);
	LOGFONTW u = lf;
	u.lfUnderline = TRUE;
	link = ::CreateFontIndirectW(&u);
	icon = IconFont(iconSize);
	HDC dc = ::GetDC(nullptr);
	auto old = ::SelectObject(dc, base);
	TEXTMETRICW tm{};
	::GetTextMetricsW(dc, &tm);
	::SelectObject(dc, old);
	::ReleaseDC(nullptr, dc);
	textHeight = tm.tmHeight;
}

void NtfFonts::Destroy() {
	for (HFONT* f : { &bold, &link, &icon })
		if (*f) {
			::DeleteObject(*f);
			*f = nullptr;
		}
	normal = nullptr;
}

NtfMessage NtfFromApi(const NTFMESSAGE& a) {
	auto text = [](LPCWSTR s) {
		return s ? std::wstring(s) : std::wstring();
	};
	NtfMessage m;
	m.id = a.id;
	m.severity = (NtfSeverity)std::min(a.severity, (UINT)NTFSEV_ERROR);
	m.image = a.iImage;
	m.title = text(a.pszTitle);
	m.runs = NtfParseLinks(text(a.pszText));
	if (a.pButtons && a.cButtons > 0) {
		m.buttons.reserve(a.cButtons);
		for (int i = 0; i < a.cButtons; i++)
			m.buttons.push_back({ a.pButtons[i].id, text(a.pButtons[i].pszText) });
	}
	m.closable = !(a.flags & NTFF_NOCLOSE);
	m.checkText = text(a.pszCheck);
	m.checked = (a.flags & NTFF_CHECKED) != 0;
	m.details = text(a.pszDetails);
	m.expanded = (a.flags & NTFF_EXPANDED) != 0;
	m.progressKind = a.progress == NTFP_VALUE ? NtfProgress::Value : a.progress == NTFP_INDETERMINATE ? NtfProgress::Indeterminate : NtfProgress::None;
	m.progress = std::clamp(a.value, 0, 100);
	m.timeout = a.timeout;
	return m;
}

// ---- Colors and helpers ------------------------------------------------------------------------------

void NtfDraw::FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

void NtfDraw::RoundFill(WTL::CDCHandle dc, const RECT& rc, COLORREF fill, COLORREF border, int radius) {
	if (HighContrast && border == fill && fill != ::GetSysColor(COLOR_HIGHLIGHT))
		border = ::GetSysColor(COLOR_HIGHLIGHT);   // a hot or pressed part: framed, the fills all being the window's
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

bool NtfDraw::IsHighContrast(int forced) {
	if (forced >= 0)
		return forced != 0;
	HIGHCONTRASTW hc{ sizeof(hc) };
	return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

NTFCOLORS NtfDraw::SystemColors(bool highContrast) {
	NTFCOLORS c;
	COLORREF window = ::GetSysColor(COLOR_WINDOW), text = ::GetSysColor(COLOR_WINDOWTEXT);
	if (highContrast) {
		// the user's colors only: the severities by their icons' shapes
		for (int i = 0; i < 4; i++) {
			c.crBack[i] = window;
			c.crIcon[i] = text;
		}
		c.crText = c.crBorder = c.crButtonBorder = text;
		c.crDimText = text;
		c.crLink = ::GetSysColor(COLOR_HOTLIGHT);
		c.crButton = c.crButtonHot = c.crButtonPressed = window;
		c.crAccent = ::GetSysColor(COLOR_HIGHLIGHT);
		return c;
	}
	if (IsDark(window)) {
		const COLORREF back[4] = { Blend(window, text, 7), RGB(0x39, 0x3D, 0x1B), RGB(0x43, 0x35, 0x19), RGB(0x44, 0x27, 0x26) };
		const COLORREF icon[4] = { RGB(0x60, 0xCD, 0xFF), RGB(0x6C, 0xCB, 0x5F), RGB(0xFC, 0xE1, 0x00), RGB(0xFF, 0x99, 0xA4) };
		std::copy(back, back + 4, c.crBack);
		std::copy(icon, icon + 4, c.crIcon);
		c.crLink = RGB(0x99, 0xEB, 0xFF);
		c.crAccent = RGB(0x60, 0xCD, 0xFF);
		c.crButton = Blend(window, text, 10);
	}
	else {
		const COLORREF back[4] = { RGB(0xF6, 0xF6, 0xF6), RGB(0xDF, 0xF6, 0xDD), RGB(0xFF, 0xF4, 0xCE), RGB(0xFD, 0xE7, 0xE9) };
		const COLORREF icon[4] = { RGB(0x00, 0x5F, 0xB8), RGB(0x0F, 0x7B, 0x0F), RGB(0x9D, 0x5D, 0x00), RGB(0xC4, 0x2B, 0x1C) };
		std::copy(back, back + 4, c.crBack);
		std::copy(icon, icon + 4, c.crIcon);
		c.crLink = RGB(0x00, 0x5F, 0xB8);
		c.crAccent = RGB(0x00, 0x5F, 0xB8);
		c.crButton = window;
	}
	c.crText = text;
	c.crDimText = Blend(c.crBack[0], text, 62);
	c.crBorder = Blend(window, text, 14);
	c.crButtonHot = Blend(c.crButton, text, 6);
	c.crButtonPressed = Blend(c.crButton, text, 12);
	c.crButtonBorder = Blend(window, text, 22);
	return c;
}

NtfMeasure NtfDraw::Measurer(HDC dc, const NtfFonts& fonts) {
	return [dc, normal = fonts.normal, bold = fonts.bold](std::wstring_view s, bool isBold) {
		auto old = ::SelectObject(dc, isBold ? bold : normal);
		SIZE size{};
		::GetTextExtentPoint32W(dc, s.data(), (int)s.size(), &size);
		::SelectObject(dc, old);
		return (int)size.cx;
	};
}

// ---- Painting ----------------------------------------------------------------------------------------

namespace {
	// runs laid out at (x, y)
	void PaintText(WTL::CDCHandle dc, const std::vector<NtfRun>& runs, const NtfTextLayout& layout, int x, int y, COLORREF text,
		COLORREF link, COLORREF hotLink, int hotRun, const NtfFonts& fonts, int lineHeight) {
		const int dy = (lineHeight - fonts.textHeight) / 2;
		for (auto& p : layout.pieces) {
			auto& run = runs[p.run];
			dc.SelectFont(run.bold ? fonts.bold : run.IsLink() ? fonts.link : fonts.normal);
			dc.SetTextColor(run.IsLink() ? (p.run == hotRun ? hotLink : link) : text);
			dc.ExtTextOut(x + p.x, y + p.line * lineHeight + dy, 0, nullptr, run.text.c_str() + p.start, (UINT)p.length, nullptr);
		}
	}
}

void NtfDraw::PaintMessage(WTL::CDCHandle dc, const NtfItem& item, int index, int x, int y, COLORREF back, const NTFCOLORS& c,
	const NtfFonts& fonts, HIMAGELIST images, const NtfHit& hot, const NtfHit& pressed, const NtfMetrics& m, int dpi) {
	auto& l = item.layout;
	auto& msg = item.m;
	auto scale = [dpi](int v) {
		return ::MulDiv(v, dpi, 96);
	};
	auto at = [&](RECT r) {
		::OffsetRect(&r, x, y);
		return r;
	};
	auto isHot = [&](NtfPart part, int i = -1) {
		return hot.item == index && hot.part == part && (i < 0 || hot.index == i);
	};
	auto isPressed = [&](NtfPart part, int i = -1) {
		return pressed.item == index && pressed.part == part && (i < 0 || pressed.index == i) && isHot(part, i);
	};
	dc.SetBkMode(TRANSPARENT);

	// the icon: the app's, else the severity's glyph
	RECT icon = at(l.icon);
	if (msg.image >= 0 && images)
		::ImageList_Draw(images, msg.image, dc, icon.left, icon.top, ILD_TRANSPARENT);
	else if (fonts.icon) {
		dc.SelectFont(fonts.icon);
		dc.SetTextColor(c.crIcon[(int)msg.severity]);
		dc.DrawText(SeverityGlyphs[(int)msg.severity], -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
	}

	const COLORREF hotLink = Blend(c.crLink, c.crText, 35);
	PaintText(dc, l.textRuns, l.textLayout, x + l.text.left, y + l.text.top, c.crText, c.crLink, hotLink,
		isHot(NtfPart::Link) ? hot.index : -1, fonts, m.lineHeight);
	if (!::IsRectEmpty(&l.details))
		PaintText(dc, l.detailsRuns, l.detailsLayout, x + l.details.left, y + l.details.top, c.crDimText, c.crLink, hotLink, -1, fonts, m.lineHeight);

	if (!::IsRectEmpty(&l.progress)) {
		RECT track = at(l.progress);
		const int radius = (int)(track.bottom - track.top);
		RoundFill(dc, track, Blend(back, c.crText, 18), Blend(back, c.crText, 18), radius);
		RECT fill = track;
		const int w = (int)(track.right - track.left);
		if (msg.progressKind == NtfProgress::Value)
			fill.right = fill.left + w * msg.progress / 100;
		else {
			// a part of it sweeping across, again and again (the controls' timers repaint it)
			const ULONGLONG Period = 1600;
			const int part = w * 3 / 10;
			const double f = (double)(::GetTickCount64() % Period) / Period;
			const int start = (int)track.left - part + (int)((w + part) * f);
			fill.left = std::max((int)track.left, start);
			fill.right = std::min((int)track.right, start + part);
		}
		if (fill.right > fill.left)
			RoundFill(dc, fill, c.crAccent, c.crAccent, radius);
	}

	if (!::IsRectEmpty(&l.check)) {
		RECT box = at(l.checkBox);
		const bool hotCheck = isHot(NtfPart::Check);
		if (msg.checked) {
			RoundFill(dc, box, c.crAccent, c.crAccent, scale(4));
			WTL::CPen pen;
			pen.CreatePen(PS_SOLID, std::max(1, scale(2) / 2 + 1), back);
			auto old = dc.SelectPen(pen);
			const int s = (int)(box.right - box.left);
			Line(dc, box.left + s / 4, box.top + s / 2, box.left + s * 5 / 12, box.top + s * 7 / 10);
			Line(dc, box.left + s * 5 / 12, box.top + s * 7 / 10, box.left + s * 3 / 4, box.top + s * 3 / 10);
			dc.SelectPen(old);
		}
		else
			RoundFill(dc, box, hotCheck ? Blend(back, c.crText, 8) : back, Blend(back, c.crText, hotCheck ? 70 : 50), scale(4));
		RECT text = at(l.check);
		text.left = box.right + m.gap / 2;
		dc.SelectFont(fonts.normal);
		dc.SetTextColor(c.crText);
		dc.DrawText(msg.checkText.c_str(), (int)msg.checkText.size(), &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	dc.SelectFont(fonts.normal);
	for (int i = 0; i < (int)l.buttons.size() && i < (int)msg.buttons.size(); i++) {
		RECT r = at(l.buttons[i]);
		COLORREF fill = isPressed(NtfPart::Button, i) ? c.crButtonPressed : isHot(NtfPart::Button, i) ? c.crButtonHot : c.crButton;
		COLORREF frame = HighContrast && isHot(NtfPart::Button, i) ? ::GetSysColor(COLOR_HIGHLIGHT) : c.crButtonBorder;
		RoundFill(dc, r, fill, frame, scale(8));
		dc.SetTextColor(c.crText);
		dc.DrawText(msg.buttons[i].text.c_str(), (int)msg.buttons[i].text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	if (!::IsRectEmpty(&l.close)) {
		RECT r = at(l.close);
		if (isHot(NtfPart::Close)) {
			COLORREF fill = Blend(back, c.crText, isPressed(NtfPart::Close) ? 16 : 9);
			RoundFill(dc, r, fill, fill, scale(8));
		}
		WTL::CPen pen;
		pen.CreatePen(PS_SOLID, std::max(1, scale(1)), c.crText);
		auto old = dc.SelectPen(pen);
		const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, h = scale(5);
		Line(dc, cx - h, cy - h, cx + h + 1, cy + h + 1);
		Line(dc, cx + h, cy - h, cx - h - 1, cy + h + 1);
		dc.SelectPen(old);
	}
}

NtfHit NtfDraw::HitMessage(const NtfItem& item, POINT pt, const NtfMetrics& m) {
	auto& l = item.layout;
	NtfHit hit;
	if (::PtInRect(&l.close, pt)) {
		hit.part = NtfPart::Close;
		return hit;
	}
	for (int i = 0; i < (int)l.buttons.size(); i++)
		if (::PtInRect(&l.buttons[i], pt)) {
			hit.part = NtfPart::Button;
			hit.index = i;
			return hit;
		}
	if (::PtInRect(&l.check, pt)) {
		hit.part = NtfPart::Check;
		return hit;
	}
	int run = NtfLinkAt(l.textLayout, l.textRuns, pt.x - l.text.left, pt.y - l.text.top, m.lineHeight);
	if (run >= 0) {
		hit.part = NtfPart::Link;
		hit.index = run;
	}
	return hit;
}

// ---- The keys ---------------------------------------------------------------------------------------

std::vector<NtfHit> NtfDraw::FocusParts(const NtfItem& item) {
	auto& l = item.layout;
	std::vector<NtfHit> parts;
	for (int r = 0; r < (int)l.textRuns.size(); r++)
		if (l.textRuns[r].IsLink())
			parts.push_back({ -1, NtfPart::Link, r });
	if (!::IsRectEmpty(&l.check))
		parts.push_back({ -1, NtfPart::Check });
	for (int i = 0; i < (int)l.buttons.size(); i++)
		parts.push_back({ -1, NtfPart::Button, i });
	if (!::IsRectEmpty(&l.close))
		parts.push_back({ -1, NtfPart::Close });
	return parts;
}

std::vector<RECT> NtfDraw::PartRects(const NtfItem& item, const NtfHit& part, const NtfMetrics& m) {
	auto& l = item.layout;
	std::vector<RECT> rects;
	switch (part.part) {
		case NtfPart::Close: rects.push_back(l.close); break;
		case NtfPart::Check: rects.push_back(l.check); break;
		case NtfPart::Button:
			if (part.index >= 0 && part.index < (int)l.buttons.size())
				rects.push_back(l.buttons[part.index]);
			break;
		case NtfPart::Link:
			for (auto& p : l.textLayout.pieces)
				if (p.run == part.index) {
					const int top = (int)l.text.top + p.line * m.lineHeight;
					rects.push_back({ (int)l.text.left + p.x, top, (int)l.text.left + p.x + p.width, top + m.lineHeight });
				}
			break;
		default:
			break;
	}
	return rects;
}

void NtfDraw::PaintFocus(WTL::CDCHandle dc, const std::vector<RECT>& rects, COLORREF color, int dpi) {
	const int w = std::max(1, ::MulDiv(2, dpi, 96)), r = ::MulDiv(6, dpi, 96);
	WTL::CPen pen;
	pen.CreatePen(PS_INSIDEFRAME, w, color);
	auto oldPen = dc.SelectPen(pen);
	auto oldBrush = dc.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
	for (RECT rc : rects) {
		::InflateRect(&rc, w, w / 2);
		dc.RoundRect(&rc, { r, r });
	}
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}

std::wstring NtfDraw::CopyText(const NtfItem& item) {
	std::wstring text = item.m.title;
	auto body = NtfPlainText(item.m.runs);
	if (!body.empty())
		text += (text.empty() ? L"" : L"\r\n") + body;
	if (!item.m.details.empty()) {
		std::wstring details;
		for (wchar_t ch : item.m.details)
			details += ch == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, ch);
		text += L"\r\n\r\n" + details;
	}
	return text;
}

bool NtfDraw::ToClipboard(HWND hwnd, const std::wstring& text) {
	if (!::OpenClipboard(hwnd))
		return false;
	::EmptyClipboard();
	const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
	HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
	bool done = false;
	if (mem) {
		memcpy(::GlobalLock(mem), text.c_str(), bytes);
		::GlobalUnlock(mem);
		done = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
		if (!done)
			::GlobalFree(mem);
	}
	::CloseClipboard();
	return done;
}

std::wstring NtfDraw::TipText(const NtfItem& item, const NtfHit& part, const std::wstring& close) {
	if (part.part == NtfPart::Close)
		return close;
	if (part.part == NtfPart::Link && part.index >= 0 && part.index < (int)item.layout.textRuns.size()) {
		auto& link = item.layout.textRuns[part.index].link;
		if (link.find(L"://") != std::wstring::npos)
			return link;
	}
	return {};
}
