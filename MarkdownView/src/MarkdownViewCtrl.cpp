// The markdown view: the API, the flow's layout and scrolling, and the input. Painting is in MarkdownViewPaint.cpp.

#include "MarkdownViewData.h"

#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "uxtheme.lib")

namespace {

COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

bool IsDarkColor(COLORREF color) {
	return GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
}

void DefaultStyle(MDVSTYLE& s) {
	s = {};
	s.mask = MDVSM_ALL;
	s.fontSize = 16;
	const float scales[6] = { 2.0f, 1.5f, 1.25f, 1.0f, 0.875f, 0.85f };
	std::copy(std::begin(scales), std::end(scales), s.headingScale);
	s.spacing = 1;
	s.maxWidth = 980;
	s.padding = 24;
}

}  // namespace

// ---- Construction ------------------------------------------------------------------

CMarkdownViewCtrl::CMarkdownViewCtrl() : m_State(std::make_unique<MdvState>()) {
	DefaultStyle(m_State->style);
	MdvParse(L"", 0, m_State->doc);
}

CMarkdownViewCtrl::~CMarkdownViewCtrl() = default;

LRESULT CMarkdownViewCtrl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	s.dpi = ::GetDpiForWindow(m_hWnd);
	ModifyStyle(0, WS_VSCROLL, SWP_FRAMECHANGED);
	RECT rc;
	GetClientRect(&rc);
	s.client = { rc.right, rc.bottom };
	UpdateFormats();
	UpdateScrollBarTheme();
	Relayout();
	return 0;
}

LRESULT CMarkdownViewCtrl::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	MdvStopLoader(*m_State);
	KillTimer(CopiedTimer);
	KillTimer(ScrollTimer);
	if (m_State->tip) {
		::DestroyWindow(m_State->tip);
		m_State->tip = nullptr;
	}
	DiscardTarget();
	bHandled = FALSE;
	return 0;
}

LRESULT CMarkdownViewCtrl::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

LRESULT CMarkdownViewCtrl::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	const SIZE old = s.client;
	s.client = { LOWORD(lParam), HIWORD(lParam) };
	if (s.target)
		s.target->Resize(D2D1::SizeU(s.client.cx, s.client.cy));
	if (old.cx != s.client.cx)
		Relayout();         // a new column width: every box is laid out again
	else {
		MeasureVisible();
		ScrollTo(s.scrollY, false);
	}
	return 0;
}

LRESULT CMarkdownViewCtrl::OnStyleChanged(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	// MDVS_CENTERED changes the column
	auto ss = (const STYLESTRUCT*)lParam;
	if (wParam == (WPARAM)GWL_STYLE && ((ss->styleOld ^ ss->styleNew) & MDVS_CENTERED))
		Relayout();
	bHandled = FALSE;
	return 0;
}

LRESULT CMarkdownViewCtrl::OnDpiChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// the layout is in DIPs: only the target's DPI and the scroll bar change (WM_SIZE follows)
	auto& s = *m_State;
	s.dpi = ::GetDpiForWindow(m_hWnd);
	if (s.target)
		s.target->SetDpi((float)s.dpi, (float)s.dpi);
	UpdateScrollBar();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CMarkdownViewCtrl::OnColorsChanged(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// the links' brushes are in the layouts
	m_State->ResetEffects();
	m_State->brushes.clear();
	UpdateScrollBarTheme();
	Invalidate();
	bHandled = FALSE;
	return 0;
}

LRESULT CMarkdownViewCtrl::OnSettingChange(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SPI_SETHIGHCONTRAST) {
		BOOL handled;
		OnColorsChanged(0, 0, 0, handled);
	}
	bHandled = FALSE;
	return 0;
}

void CMarkdownViewCtrl::ForceHighContrast(int value) {
	m_State->forceHighContrast = value;
	BOOL handled;
	OnColorsChanged(0, 0, 0, handled);
}

bool CMarkdownViewCtrl::IsHighContrast() const {
	if (m_State->forceHighContrast >= 0)
		return m_State->forceHighContrast != 0;
	HIGHCONTRASTW hc{ sizeof(hc) };
	return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

// ---- Content -----------------------------------------------------------------------

void CMarkdownViewCtrl::SetText(LPCWSTR pszMarkdown, size_t cch) {
	std::wstring text;
	if (pszMarkdown)
		text.assign(pszMarkdown, cch == (size_t)-1 ? wcslen(pszMarkdown) : cch);
	auto& s = *m_State;
	MdvParse(std::move(text), s.parseFlags, s.doc);
	UpdateLeaves();
	s.layouts.clear();
	s.images.clear();
	s.sideways.clear();
	s.copiedBlock = -1;
	s.scrollY = 0;
	Relayout();
}

void CMarkdownViewCtrl::Reparse() {
	auto& s = *m_State;
	const size_t top = GetTopOffset();
	std::wstring text = std::move(s.doc.source);
	MdvParse(std::move(text), s.parseFlags, s.doc);
	UpdateLeaves();
	s.layouts.clear();
	s.scrollY = 0;
	Relayout();
	ScrollToOffset(top);
}

BOOL CMarkdownViewCtrl::LoadFile(LPCWSTR pszPath) {
	HANDLE file = ::CreateFileW(pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	LARGE_INTEGER size{};
	::GetFileSizeEx(file, &size);
	if (size.QuadPart > 256 * 1024 * 1024) {
		::CloseHandle(file);
		return FALSE;
	}
	std::string bytes((size_t)size.QuadPart, '\0');
	DWORD read = 0;
	const BOOL ok = ::ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
	::CloseHandle(file);
	if (!ok)
		return FALSE;
	bytes.resize(read);

	std::wstring text;
	if (bytes.size() >= 2 && (BYTE)bytes[0] == 0xFF && (BYTE)bytes[1] == 0xFE) {
		text.assign((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
	}
	else {
		size_t skip = bytes.size() >= 3 && (BYTE)bytes[0] == 0xEF && (BYTE)bytes[1] == 0xBB && (BYTE)bytes[2] == 0xBF ? 3 : 0;
		const char* p = bytes.data() + skip;
		const int n = (int)(bytes.size() - skip);
		UINT cp = CP_UTF8;
		int chars = ::MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, p, n, nullptr, 0);
		if (chars == 0 && n > 0) {
			cp = CP_ACP;
			chars = ::MultiByteToWideChar(cp, 0, p, n, nullptr, 0);
		}
		text.resize(chars);
		::MultiByteToWideChar(cp, 0, p, n, text.data(), chars);
	}

	std::wstring dir = pszPath;
	const size_t slash = dir.find_last_of(L"\\/");
	dir = slash == std::wstring::npos ? L"" : dir.substr(0, slash);
	m_State->basePath = dir;
	SetText(text.c_str(), text.size());
	return TRUE;
}

CString CMarkdownViewCtrl::GetText() const {
	return CString(m_State->doc.source.c_str(), (int)m_State->doc.source.size());
}

void CMarkdownViewCtrl::SetParseFlags(UINT mdvf) {
	if (mdvf == m_State->parseFlags)
		return;
	m_State->parseFlags = mdvf;
	Reparse();
}

UINT CMarkdownViewCtrl::GetParseFlags() const {
	return m_State->parseFlags;
}

void CMarkdownViewCtrl::SetHighlighter(MDVHIGHLIGHTPROC proc, LPARAM lParam) {
	auto& s = *m_State;
	s.highlighter = proc;
	s.highlighterParam = lParam;
	// the code blocks are laid out again
	for (auto& box : s.flow.boxes)
		if (box.kind == MdvBoxKind::Code) {
			s.layouts.erase(MdvState::Key(box.block, box.kind));
			box.measured = false;
		}
	MeasureVisible();
	ScrollTo(s.scrollY, false);
	Invalidate();
}

void CMarkdownViewCtrl::SetBasePath(LPCWSTR pszDir) {
	m_State->basePath = pszDir ? pszDir : L"";
}

CString CMarkdownViewCtrl::GetBasePath() const {
	return m_State->basePath.c_str();
}

// ---- Look ---------------------------------------------------------------------------

void CMarkdownViewCtrl::SetTextStyle(const MDVSTYLE& style) {
	auto& s = m_State->style;
	if (style.mask & MDVSM_FONT)
		wcscpy_s(s.szFont, style.szFont);
	if (style.mask & MDVSM_CODEFONT)
		wcscpy_s(s.szCodeFont, style.szCodeFont);
	if (style.mask & MDVSM_FONTSIZE)
		s.fontSize = std::clamp(style.fontSize, 6.0f, 96.0f);
	if (style.mask & MDVSM_HEADINGSCALE)
		std::copy(std::begin(style.headingScale), std::end(style.headingScale), s.headingScale);
	if (style.mask & MDVSM_SPACING)
		s.spacing = std::clamp(style.spacing, 0.0f, 4.0f);
	if (style.mask & MDVSM_MAXWIDTH)
		s.maxWidth = std::max(style.maxWidth, 100);
	if (style.mask & MDVSM_PADDING)
		s.padding = std::clamp(style.padding, 0, 400);
	m_State->layouts.clear();
	UpdateFormats();
	Relayout();
}

void CMarkdownViewCtrl::GetTextStyle(MDVSTYLE& style) const {
	style = m_State->style;
	style.mask = MDVSM_ALL;
}

void CMarkdownViewCtrl::SetColors(const MDVCOLORS* colors) {
	auto& s = *m_State;
	s.customColors = colors != nullptr;
	if (colors)
		s.colors = *colors;
	BOOL handled;
	OnColorsChanged(0, 0, 0, handled);
}

MDVCOLORS CMarkdownViewCtrl::GetColors() const {
	auto& s = *m_State;
	if (IsHighContrast()) {
		// the system's colors only, over the app's: no tints (painting frames what's shaded otherwise)
		MDVCOLORS c{};
		c.crBack = c.crCodeBack = c.crTableHeaderBack = c.crTableStripeBack = ::GetSysColor(COLOR_WINDOW);
		c.crText = c.crMuted = c.crHeading = c.crCodeText = c.crBorder = c.crTableBorder = ::GetSysColor(COLOR_WINDOWTEXT);
		c.crLink = ::GetSysColor(COLOR_HOTLIGHT);
		c.crMarkBack = c.crFindBack = c.crSelBack = ::GetSysColor(COLOR_HIGHLIGHT);
		c.crSelBackInactive = ::GetSysColor(COLOR_GRAYTEXT);
		for (auto& a : c.crAlert)
			a = c.crText;
		for (auto& syn : c.crSyntax)
			syn = c.crText;
		return c;
	}
	if (s.customColors)
		return s.colors;
	// read each time: WTLHelper's dark mode hooks GetSysColor
	MDVCOLORS c{};
	c.crBack = ::GetSysColor(COLOR_WINDOW);
	c.crText = ::GetSysColor(COLOR_WINDOWTEXT);
	const bool dark = IsDarkColor(c.crBack);
	c.crMuted = Blend(c.crBack, c.crText, 62);
	c.crHeading = c.crText;
	c.crLink = dark ? RGB(0x44, 0x93, 0xF8) : RGB(0x09, 0x69, 0xDA);
	c.crBorder = Blend(c.crBack, c.crText, dark ? 22 : 16);
	c.crCodeBack = Blend(c.crBack, c.crText, dark ? 9 : 6);
	c.crCodeText = c.crText;
	c.crMarkBack = dark ? RGB(0x6B, 0x5A, 0x14) : RGB(0xFF, 0xF1, 0x8C);
	const COLORREF light[MDVA_COUNT] = { RGB(0x09, 0x69, 0xDA), RGB(0x1A, 0x7F, 0x37), RGB(0x82, 0x50, 0xDF), RGB(0x9A, 0x67, 0x00), RGB(0xD1, 0x24, 0x2F) };
	const COLORREF night[MDVA_COUNT] = { RGB(0x44, 0x93, 0xF8), RGB(0x3F, 0xB9, 0x50), RGB(0xAB, 0x7D, 0xF8), RGB(0xD2, 0x99, 0x22), RGB(0xF8, 0x51, 0x49) };
	for (int i = 0; i < MDVA_COUNT; i++)
		c.crAlert[i] = dark ? night[i] : light[i];
	c.crTableBorder = c.crBorder;
	c.crTableHeaderBack = Blend(c.crBack, c.crText, 4);
	c.crTableStripeBack = Blend(c.crBack, c.crText, 3);
	// a light shade of the highlight, as a browser's: the text keeps its colors
	c.crSelBack = Blend(c.crBack, ::GetSysColor(COLOR_HIGHLIGHT), dark ? 45 : 30);
	c.crSelBackInactive = Blend(c.crBack, c.crText, dark ? 22 : 14);
	c.crFindBack = dark ? RGB(0x7A, 0x5C, 0x00) : RGB(0xFF, 0xE5, 0x8A);
	// GitHub's
	const COLORREF syntaxLight[MDVSYN_COUNT] = { RGB(0xCF, 0x22, 0x2E), RGB(0x82, 0x50, 0xDF), RGB(0x0A, 0x30, 0x69), RGB(0x6E, 0x77, 0x81),
		RGB(0x05, 0x50, 0xAE), RGB(0x95, 0x38, 0x00), RGB(0x11, 0x63, 0x29), RGB(0x05, 0x50, 0xAE) };
	const COLORREF syntaxDark[MDVSYN_COUNT] = { RGB(0xFF, 0x7B, 0x72), RGB(0xD2, 0xA8, 0xFF), RGB(0xA5, 0xD6, 0xFF), RGB(0x8B, 0x94, 0x9E),
		RGB(0x79, 0xC0, 0xFF), RGB(0xFF, 0xA6, 0x57), RGB(0x7E, 0xE7, 0x87), RGB(0x79, 0xC0, 0xFF) };
	for (int i = 0; i < MDVSYN_COUNT; i++)
		c.crSyntax[i] = dark ? syntaxDark[i] : syntaxLight[i];
	return c;
}

bool CMarkdownViewCtrl::IsDark() const {
	return IsDarkColor(GetColors().crBack);
}

void CMarkdownViewCtrl::SetZoom(int percent) {
	percent = std::clamp(percent, 25, 400);
	if (percent == m_State->zoom)
		return;
	m_State->zoom = percent;
	m_State->layouts.clear();
	Relayout();
}

int CMarkdownViewCtrl::GetZoom() const {
	return m_State->zoom;
}

void CMarkdownViewCtrl::UpdateScrollBarTheme() {
	// the dark Explorer theme makes the scroll bar dark, and the link tooltip: a popup, which WTLHelper's dark mode
	// doesn't reach (it themes child windows). Set only when it changes: SetWindowTheme sends WM_THEMECHANGED, which
	// comes back here.
	if (!IsWindow())
		return;
	const int dark = IsDark() && !IsHighContrast() ? 1 : 0;
	if (dark == m_State->darkScrollBars)
		return;
	m_State->darkScrollBars = dark;
	if (m_State->tip)
		::SetWindowTheme(m_State->tip, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	::SetWindowTheme(m_hWnd, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// ---- Layout ----------------------------------------------------------------------------

void CMarkdownViewCtrl::UpdateMetrics() {
	auto& s = *m_State;
	auto& m = s.metrics;
	const float zoom = s.zoom / 100.0f;
	m = MdvMetrics{};
	m.fontSize = s.style.fontSize * zoom;
	std::copy(std::begin(s.style.headingScale), std::end(s.style.headingScale), m.headingScale);
	m.spacing = s.style.spacing;
	m.padding = s.style.padding * zoom;
}

float CMarkdownViewCtrl::ColumnWidth() const {
	auto& s = *m_State;
	const float width = s.client.cx / s.Scale() - 2 * s.metrics.padding;
	float column = std::max(width, 4 * s.metrics.fontSize);
	if (GetStyle() & MDVS_CENTERED)
		column = std::min(column, s.style.maxWidth * s.zoom / 100.0f);
	return column;
}

float CMarkdownViewCtrl::ColumnLeft() const {
	auto& s = *m_State;
	const float width = s.client.cx / s.Scale();
	const float column = ColumnWidth();
	return std::max(s.metrics.padding, (width - column) / 2);
}

float CMarkdownViewCtrl::ViewHeight() const {
	return m_State->client.cy / m_State->Scale();
}

void CMarkdownViewCtrl::Relayout() {
	auto& s = *m_State;
	auto& flow = s.flow;
	// keep the block at the top where it is
	int anchorBlock = -1;
	float anchorFraction = 0;
	if (!flow.boxes.empty()) {
		const size_t top = flow.BoxAt(s.scrollY);
		if (top < flow.boxes.size()) {
			auto& box = flow.boxes[top];
			anchorBlock = box.block;
			const float h = box.Bottom() - box.top;
			anchorFraction = h > 0 ? std::clamp((s.scrollY - box.top) / h, 0.0f, 1.0f) : 0;
		}
	}

	UpdateMetrics();
	s.hotBox = -1;
	s.dragBox = -1;
	flow.Build(s.doc, s.metrics);
	const float column = ColumnWidth();
	// the boxes laid out before at this width keep their heights
	for (auto& box : flow.boxes) {
		auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
		if (it != s.layouts.end() && std::fabs(it->second.width - box.TextWidth(column)) < 0.01f) {
			box.height = it->second.height;
			box.measured = true;
		}
	}
	flow.Estimate(s.doc, s.metrics, column);
	flow.Place();
	s.blockBox.assign(s.doc.blocks.size(), -1);
	for (size_t i = flow.boxes.size(); i-- > 0;) {
		s.blockBox[flow.boxes[i].block] = (int)i;
		if (flow.boxes[i].markerBlock >= 0)
			s.blockBox[flow.boxes[i].markerBlock] = (int)i;
	}

	if (anchorBlock >= 0 && s.blockBox[anchorBlock] >= 0) {
		auto& box = flow.boxes[s.blockBox[anchorBlock]];
		s.scrollY = box.top + anchorFraction * (box.Bottom() - box.top);
	}
	if (IsWindow()) {
		MeasureVisible();
		ScrollTo(s.scrollY, false);
		Invalidate();
	}
}

// Lays out the boxes in view (and a little around it), keeping the first box shown where it is as the heights
// above it change from estimates to measured ones.
void CMarkdownViewCtrl::MeasureVisible() {
	auto& s = *m_State;
	auto& flow = s.flow;
	if (!IsWindow() || flow.boxes.empty())
		return;
	const float view = ViewHeight();
	for (int pass = 0; pass < 4; pass++) {
		const size_t anchor = std::min(flow.BoxAt(s.scrollY), flow.boxes.size() - 1);
		const float offset = s.scrollY - flow.boxes[anchor].top;
		bool changed = false;
		for (size_t i = flow.BoxAt(std::max(0.0f, s.scrollY - view / 4)); i < flow.boxes.size(); i++) {
			if (flow.boxes[i].top > s.scrollY + view * 1.25f)
				break;
			if (flow.boxes[i].measured)
				continue;
			Measure(i);
			flow.Place(i);
			changed = true;
		}
		if (!changed)
			break;
		s.scrollY = std::max(0.0f, flow.boxes[anchor].top + offset);
	}
}

void CMarkdownViewCtrl::ScrollTo(float y, bool notify) {
	auto& s = *m_State;
	const float old = s.scrollY;
	for (int pass = 0; pass < 3; pass++) {
		const float limit = std::max(0.0f, s.flow.height - ViewHeight());
		s.scrollY = std::clamp(y, 0.0f, limit);
		MeasureVisible();
		// measuring may have changed the height: near the end, go on until it settles
		const float newLimit = std::max(0.0f, s.flow.height - ViewHeight());
		if (y <= newLimit || std::fabs(newLimit - limit) < 0.5f)
			break;
		y = std::min(y, newLimit);
	}
	s.scrollY = std::clamp(s.scrollY, 0.0f, std::max(0.0f, s.flow.height - ViewHeight()));
	UpdateScrollBar();
	if (s.scrollY != old) {
		Invalidate();
		if (notify) {
			NMMDVSCROLL nm{};
			nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_SCROLLED };
			nm.heading = GetTopHeading();
			nm.topOffset = GetTopOffset();
			::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
		}
	}
}

void CMarkdownViewCtrl::UpdateScrollBar() {
	auto& s = *m_State;
	if (!IsWindow())
		return;
	SCROLLINFO si{ sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL };
	si.nMin = 0;
	si.nMax = std::max(0, (int)std::ceil(s.flow.height * s.Scale()) - 1);
	si.nPage = s.client.cy;
	si.nPos = (int)std::lround(s.scrollY * s.Scale());
	SetScrollInfo(SB_VERT, &si, TRUE);
}

void CMarkdownViewCtrl::ScrollToBox(int box) {
	auto& s = *m_State;
	if (box < 0 || box >= (int)s.flow.boxes.size())
		return;
	// a little above the box, so a heading's space shows
	float y = s.flow.boxes[box].top - 0.5f * s.metrics.fontSize;
	ScrollTo(y);
	// measuring above may have moved it
	y = s.flow.boxes[box].top - 0.5f * s.metrics.fontSize;
	ScrollTo(y);
}

// ---- Navigation -------------------------------------------------------------------------

int CMarkdownViewCtrl::GetHeadingCount() const {
	return (int)m_State->doc.headings.size();
}

BOOL CMarkdownViewCtrl::GetHeading(int index, MDVHEADING& heading) const {
	auto& doc = m_State->doc;
	if (index < 0 || index >= (int)doc.headings.size())
		return FALSE;
	auto& block = doc.blocks[doc.headings[index]];
	heading.level = block.level;
	heading.pszText = block.content.text.c_str();
	heading.pszAnchor = block.anchor.c_str();
	heading.sourceOffset = block.sourceStart;
	return TRUE;
}

// opens the <details> a block is in, so it has a box
static bool OpenDetailsAround(MdvDocument& doc, int block) {
	bool changed = false;
	for (int b = doc.blocks[block].parent; b > 0; b = doc.blocks[b].parent)
		if (doc.blocks[b].kind == MdvKind::Details && !doc.blocks[b].open) {
			doc.blocks[b].open = true;
			changed = true;
		}
	return changed;
}

BOOL CMarkdownViewCtrl::ScrollToAnchor(LPCWSTR pszAnchor) {
	auto& s = *m_State;
	const int a = s.doc.FindAnchor(pszAnchor ? pszAnchor : L"");
	if (a < 0)
		return FALSE;
	const int block = s.doc.anchors[a].block;
	if (OpenDetailsAround(s.doc, block))
		Relayout();
	ScrollToBox(s.blockBox[block]);
	return TRUE;
}

BOOL CMarkdownViewCtrl::ScrollToHeading(int index) {
	auto& doc = m_State->doc;
	if (index < 0 || index >= (int)doc.headings.size())
		return FALSE;
	const int block = doc.headings[index];
	if (OpenDetailsAround(doc, block))
		Relayout();
	ScrollToBox(m_State->blockBox[block]);
	return TRUE;
}

void CMarkdownViewCtrl::ScrollToOffset(size_t sourceOffset) {
	auto& s = *m_State;
	const int leaf = s.doc.LeafAtSource(sourceOffset);
	if (leaf < 0)
		return;
	if (OpenDetailsAround(s.doc, leaf))
		Relayout();
	const int box = s.blockBox[leaf];
	if (box >= 0)
		ScrollTo(s.flow.boxes[box].top);
}

size_t CMarkdownViewCtrl::GetTopOffset() const {
	auto& s = *m_State;
	const size_t box = s.flow.BoxAt(s.scrollY);
	if (box >= s.flow.boxes.size())
		return s.doc.source.size();
	return s.doc.blocks[s.flow.boxes[box].block].sourceStart;
}

int CMarkdownViewCtrl::GetTopHeading() const {
	auto& s = *m_State;
	if (s.flow.boxes.empty())
		return -1;
	// the last heading whose box starts above the view's first tenth
	const float y = s.scrollY + ViewHeight() / 10;
	int found = -1;
	for (size_t h = 0; h < s.doc.headings.size(); h++) {
		const int box = s.blockBox[s.doc.headings[h]];
		if (box < 0)
			continue;
		if (s.flow.boxes[box].top > y)
			break;
		found = (int)h;
	}
	return found;
}

// ---- Input ---------------------------------------------------------------------------

LRESULT CMarkdownViewCtrl::OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTARROWS;
}

LRESULT CMarkdownViewCtrl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	auto& s = *m_State;
	const bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0;
	const float line = s.metrics.fontSize * s.metrics.lineHeight;
	const float page = std::max(line, ViewHeight() - 2 * line);
	switch (wParam) {
		case VK_UP: ScrollTo(s.scrollY - line); break;
		case VK_DOWN: ScrollTo(s.scrollY + line); break;
		case VK_PRIOR: ScrollTo(s.scrollY - page); break;
		case VK_NEXT: ScrollTo(s.scrollY + page); break;
		case VK_SPACE: ScrollTo(s.scrollY + (shift ? -page : page)); break;
		case VK_HOME: ScrollTo(0); break;
		case VK_END: ScrollTo(s.flow.height); break;
		case 'A':
			if (!ctrl || (GetStyle() & MDVS_NOSELECTION)) {
				bHandled = FALSE;
				return 0;
			}
			SelectAll();
			SelectionChanged(true);
			break;
		case 'C':
		case VK_INSERT:
			if (!ctrl) {
				bHandled = FALSE;
				return 0;
			}
			Copy();
			break;
		case VK_F3:
			if (!s.findText.empty())
				Find(s.findText.c_str(), shift ? (s.findFlags | MDVFF_UP) : (s.findFlags & ~MDVFF_UP));
			break;
		case VK_ESCAPE:
			if (!s.HasSelection()) {
				bHandled = FALSE;
				return 0;
			}
			ClearSelection();
			SelectionChanged(true);
			break;
		case VK_OEM_PLUS:
		case VK_ADD:
			if (!ctrl)
				return 0;
			SetZoom(GetZoom() + 10);
			break;
		case VK_OEM_MINUS:
		case VK_SUBTRACT:
			if (!ctrl)
				return 0;
			SetZoom(GetZoom() - 10);
			break;
		case '0':
		case VK_NUMPAD0:
			if (!ctrl)
				return 0;
			SetZoom(100);
			break;
		default:
			bHandled = FALSE;
			break;
	}
	return 0;
}

LRESULT CMarkdownViewCtrl::OnMouseWheel(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
		s.wheelDelta += delta;
		const int steps = s.wheelDelta / WHEEL_DELTA;
		s.wheelDelta -= steps * WHEEL_DELTA;
		if (steps)
			SetZoom(GetZoom() + steps * 10);
		return 0;
	}
	if (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) {
		BOOL handled;
		return OnMouseHWheel(WM_MOUSEHWHEEL, MAKEWPARAM(0, -delta), lParam, handled);
	}
	UINT lines = 3;
	::SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
	const float line = s.metrics.fontSize * s.metrics.lineHeight;
	const float amount = lines == WHEEL_PAGESCROLL ? ViewHeight() : lines * line;
	ScrollTo(s.scrollY - amount * delta / WHEEL_DELTA);
	return 0;
}

LRESULT CMarkdownViewCtrl::OnMouseHWheel(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	// the block under the mouse (the message's point is in screen coordinates)
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	ScreenToClient(&pt);
	const int box = BoxAtPoint(pt);
	if (box < 0 || SidewaysRoom(box) <= 0)
		return 0;
	const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
	const float step = 4 * m_State->metrics.fontSize;
	ScrollSideways(box, m_State->sideways[m_State->flow.boxes[box].block] + step * delta / WHEEL_DELTA);
	return 1;
}

LRESULT CMarkdownViewCtrl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	auto& s = *m_State;
	const float line = s.metrics.fontSize * s.metrics.lineHeight;
	const float page = std::max(line, ViewHeight() - 2 * line);
	switch (LOWORD(wParam)) {
		case SB_LINEUP: ScrollTo(s.scrollY - line); break;
		case SB_LINEDOWN: ScrollTo(s.scrollY + line); break;
		case SB_PAGEUP: ScrollTo(s.scrollY - page); break;
		case SB_PAGEDOWN: ScrollTo(s.scrollY + page); break;
		case SB_TOP: ScrollTo(0); break;
		case SB_BOTTOM: ScrollTo(s.flow.height); break;
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: {
			SCROLLINFO si{ sizeof(si), SIF_TRACKPOS };
			GetScrollInfo(SB_VERT, &si);
			ScrollTo(si.nTrackPos / s.Scale());
			break;
		}
	}
	return 0;
}

int CMarkdownViewCtrl::BoxAtPoint(POINT pt) const {
	auto& s = *m_State;
	const float x = pt.x / s.Scale() - ColumnLeft(), y = pt.y / s.Scale() + s.scrollY;
	const size_t i = s.flow.BoxAt(y);
	if (i >= s.flow.boxes.size())
		return -1;
	auto& box = s.flow.boxes[i];
	if (y < box.top || x < box.indent || x > ColumnWidth())
		return -1;
	return (int)i;
}

void CMarkdownViewCtrl::ToggleDetails(int block) {
	auto& b = m_State->doc.blocks[block];
	b.open = !b.open;
	Relayout();
}

// ---- Code blocks and tables: sideways scrolling, the copy button ---------------------------

bool CMarkdownViewCtrl::BoxRect(size_t index, float rc[4]) const {
	auto& s = *m_State;
	if (index >= s.flow.boxes.size())
		return false;
	auto& box = s.flow.boxes[index];
	rc[0] = ColumnLeft() + box.indent;
	rc[1] = box.top - s.scrollY;
	rc[2] = ColumnLeft() + ColumnWidth();
	rc[3] = box.Bottom() - s.scrollY;
	return true;
}

float CMarkdownViewCtrl::SidewaysRoom(size_t index) const {
	auto& s = *m_State;
	if (index >= s.flow.boxes.size())
		return 0;
	auto& box = s.flow.boxes[index];
	if (box.kind != MdvBoxKind::Code && box.kind != MdvBoxKind::Table)
		return 0;
	auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
	if (it == s.layouts.end())
		return 0;
	const float room = it->second.contentWidth - (ColumnWidth() - box.indent);
	return room > 0.5f ? room : 0;
}

bool CMarkdownViewCtrl::SidewaysBar(size_t index, float track[4], float thumb[4]) const {
	auto& s = *m_State;
	const float room = SidewaysRoom(index);
	float rc[4];
	if (room <= 0 || !BoxRect(index, rc))
		return false;
	auto& box = s.flow.boxes[index];
	if (box.kind == MdvBoxKind::Code) {
		track[0] = rc[0] + 8;
		track[2] = rc[2] - 8;
		track[1] = rc[3] - 10;
		track[3] = rc[3] - 4;
	}
	else {
		track[0] = rc[0];
		track[2] = rc[2];
		track[1] = rc[3] - 7;
		track[3] = rc[3] - 1;
	}
	const float width = rc[2] - rc[0], trackWidth = track[2] - track[0];
	const float thumbWidth = std::max(24.0f, trackWidth * width / (width + room));
	auto it = s.sideways.find(box.block);
	const float offset = it == s.sideways.end() ? 0 : it->second;
	thumb[0] = track[0] + (trackWidth - thumbWidth) * std::clamp(offset / room, 0.0f, 1.0f);
	thumb[2] = thumb[0] + thumbWidth;
	thumb[1] = track[1];
	thumb[3] = track[3];
	return true;
}

void CMarkdownViewCtrl::ScrollSideways(size_t index, float offset) {
	auto& s = *m_State;
	const float room = SidewaysRoom(index);
	auto& value = s.sideways[s.flow.boxes[index].block];
	offset = std::clamp(offset, 0.0f, room);
	if (offset == value)
		return;
	value = offset;
	float rc[4];
	BoxRect(index, rc);
	const float scale = s.Scale();
	RECT r{ (LONG)std::floor(rc[0] * scale), (LONG)std::floor(rc[1] * scale), (LONG)std::ceil(rc[2] * scale), (LONG)std::ceil(rc[3] * scale) };
	InvalidateRect(&r);
}

bool CMarkdownViewCtrl::CopyButton(size_t index, float b[4]) const {
	auto& s = *m_State;
	if ((int)index != s.hotBox || index >= s.flow.boxes.size() || s.flow.boxes[index].kind != MdvBoxKind::Code)
		return false;
	float rc[4];
	BoxRect(index, rc);
	const float em = s.metrics.fontSize, size = 1.9f * em, margin = 0.4f * em;
	b[0] = rc[2] - margin - size;
	b[1] = rc[1] + margin;
	b[2] = rc[2] - margin;
	b[3] = rc[1] + margin + size;
	return true;
}

void CMarkdownViewCtrl::CopyCode(int block) {
	auto& text = m_State->doc.blocks[block].content.text;
	if (!::OpenClipboard(m_hWnd))
		return;
	::EmptyClipboard();
	// line breaks as Windows writes them
	std::wstring crlf;
	crlf.reserve(text.size() + text.size() / 16);
	for (wchar_t ch : text) {
		if (ch == L'\n')
			crlf += L'\r';
		crlf += ch;
	}
	const size_t bytes = (crlf.size() + 1) * sizeof(wchar_t);
	if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		memcpy(::GlobalLock(mem), crlf.c_str(), bytes);
		::GlobalUnlock(mem);
		if (!::SetClipboardData(CF_UNICODETEXT, mem))
			::GlobalFree(mem);
	}
	::CloseClipboard();
	m_State->copiedBlock = block;
	SetTimer(CopiedTimer, 1500);
	Invalidate();
}

static bool Inside(const float rc[4], float x, float y) {
	return x >= rc[0] && x < rc[2] && y >= rc[1] && y < rc[3];
}

LRESULT CMarkdownViewCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	const float x = pt.x / s.Scale(), y = pt.y / s.Scale();
	if (s.selecting) {
		DragSelection(pt);
		return 0;
	}
	if (s.dragBox >= 0) {
		float track[4], thumb[4];
		if (SidewaysBar(s.dragBox, track, thumb)) {
			const float room = SidewaysRoom(s.dragBox);
			const float span = (track[2] - track[0]) - (thumb[2] - thumb[0]);
			if (span > 0)
				ScrollSideways(s.dragBox, (x - s.dragGrab - track[0]) / span * room);
		}
		return 0;
	}
	if (!s.tracking) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		s.tracking = ::TrackMouseEvent(&tme) != 0;
	}
	// the code block under the mouse shows its copy button
	const int box = BoxAtPoint(pt);
	const int hot = box >= 0 && s.flow.boxes[box].kind == MdvBoxKind::Code ? box : -1;
	bool hotCopy = false;
	if (hot >= 0) {
		const int old = s.hotBox;
		s.hotBox = hot;
		float b[4];
		hotCopy = CopyButton(hot, b) && Inside(b, x, y);
		s.hotBox = old;
	}
	if (hot != s.hotBox || hotCopy != s.hotCopy) {
		s.hotBox = hot;
		s.hotCopy = hotCopy;
		Invalidate();
	}
	UpdateHotLink(pt);
	return 0;
}

LRESULT CMarkdownViewCtrl::OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	auto& s = *m_State;
	s.tracking = false;
	if (s.hotBox >= 0 && s.dragBox < 0) {
		s.hotBox = -1;
		s.hotCopy = false;
		Invalidate();
	}
	if (!s.selecting)
		UpdateHotLink({ -10000, -10000 });
	return 0;
}

LRESULT CMarkdownViewCtrl::OnLButtonUp(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_State->selecting)
		EndSelection({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
	else if (m_State->dragBox >= 0)
		::ReleaseCapture();
	return 0;
}

LRESULT CMarkdownViewCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_State->selecting) {
		m_State->selecting = false;
		KillTimer(ScrollTimer);
	}
	if (m_State->dragBox >= 0) {
		m_State->dragBox = -1;
		Invalidate();
	}
	return 0;
}

LRESULT CMarkdownViewCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == ScrollTimer) {
		AutoScroll();
		return 0;
	}
	if (wParam != CopiedTimer) {
		bHandled = FALSE;
		return 0;
	}
	KillTimer(CopiedTimer);
	m_State->copiedBlock = -1;
	Invalidate();
	return 0;
}

LRESULT CMarkdownViewCtrl::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	SetFocus();
	const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	const float x = pt.x / s.Scale(), y = pt.y / s.Scale();
	int item;
	if (TaskAt(pt, item)) {
		ToggleTask(item);
		return 0;
	}
	const int box = BoxAtPoint(pt);
	if (box < 0) {
		PressText(pt, (wParam & MK_SHIFT) != 0, uMsg == WM_LBUTTONDBLCLK);
		return 0;
	}
	// a sideways bar: drag its thumb, or page along its track
	float track[4], thumb[4];
	if (SidewaysBar(box, track, thumb) && x >= track[0] && x < track[2] && y >= track[1] - 4 && y < track[3] + 2) {
		if (x >= thumb[0] && x < thumb[2]) {
			s.dragBox = box;
			s.dragGrab = x - thumb[0];
			SetCapture();
			Invalidate();
		}
		else {
			const float page = (ColumnWidth() - s.flow.boxes[box].indent) * 0.8f;
			ScrollSideways(box, s.sideways[s.flow.boxes[box].block] + (x < thumb[0] ? -page : page));
		}
		return 0;
	}
	float b[4];
	if (CopyButton(box, b) && Inside(b, x, y)) {
		CopyCode(s.flow.boxes[box].block);
		return 0;
	}
	if (s.flow.boxes[box].kind == MdvBoxKind::Summary) {
		ToggleDetails(s.flow.boxes[box].block);
		return 0;
	}
	PressText(pt, (wParam & MK_SHIFT) != 0, uMsg == WM_LBUTTONDBLCLK);
	return 0;
}

LRESULT CMarkdownViewCtrl::OnSetCursor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if ((HWND)wParam == m_hWnd && LOWORD(lParam) == HTCLIENT) {
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		MDVHITTEST ht{ pt };
		HitTest(ht);
		if (ht.part == MDVHT_LINK || ht.part == MDVHT_TASK || ht.part == MDVHT_CODECOPY || ht.part == MDVHT_SUMMARY) {
			::SetCursor(::LoadCursor(nullptr, IDC_HAND));
			return TRUE;
		}
		if ((ht.part == MDVHT_TEXT || ht.part == MDVHT_IMAGE) && !(GetStyle() & MDVS_NOSELECTION)) {
			::SetCursor(::LoadCursor(nullptr, IDC_IBEAM));
			return TRUE;
		}
	}
	bHandled = FALSE;
	return 0;
}
