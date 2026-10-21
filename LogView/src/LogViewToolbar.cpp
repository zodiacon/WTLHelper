// The log view's optional toolbar: the filter and search boxes with their options, the level buttons and Follow.

#include "LogView.h"
#include "LogViewModel.h"
#include "LogViewText.h"

#include <algorithm>
#include <iterator>

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

static void Frame(WTL::CDCHandle dc, const RECT& rc, COLORREF color, int width = 1) {
	FillSolid(dc, { rc.left, rc.top, rc.right, rc.top + width }, color);
	FillSolid(dc, { rc.left, rc.bottom - width, rc.right, rc.bottom }, color);
	FillSolid(dc, { rc.left, rc.top, rc.left + width, rc.bottom }, color);
	FillSolid(dc, { rc.right - width, rc.top, rc.right, rc.bottom }, color);
}

static COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
	auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
	return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

static std::wstring WindowText(HWND hWnd) {
	std::wstring text(::GetWindowTextLengthW(hWnd), L'\0');
	::GetWindowTextW(hWnd, text.data(), (int)text.size() + 1);
	return text;
}

// the toolbar's own options: the box flags it shows
constexpr UINT FilterBoxOptions = LGVFF_MATCHCASE | LGVFF_REGEX | LGVFF_EXCLUDE;
constexpr UINT SearchBoxOptions = LGVFF_MATCHCASE | LGVFF_WHOLEWORD | LGVFF_REGEX;

// ---- API ------------------------------------------------------------------------------------

void CLogViewCtrl::ShowToolbar(bool show) {
	m_ShowToolbar = show;
	if (!IsWindow())
		return;   // OnCreate makes it
	if (show)
		CreateToolbar();
	else if (::GetFocus() == m_hFilterBox || ::GetFocus() == m_hSearchBox)
		m_Text->SetFocus();
	for (auto box : { m_hFilterBox, m_hSearchBox }) {
		if (box)
			::ShowWindow(box, show ? SW_SHOWNA : SW_HIDE);
	}
	if (m_hToolTip)
		::SendMessage(m_hToolTip, TTM_ACTIVATE, show, 0);
	Layout();
}

bool CLogViewCtrl::IsToolbarVisible() const {
	return m_ShowToolbar;
}

// ---- Layout -----------------------------------------------------------------------------------

int CLogViewCtrl::ToolbarHeight() const {
	return m_ShowToolbar ? m_UiTextHeight + Scale(14) : 0;
}

void CLogViewCtrl::ToolLayout(RECT* tools, RECT* boxes) const {
	RECT client;
	GetClientRect(&client);
	int margin = Scale(4), size = ToolbarHeight() - 2 * margin;
	int top = margin, bottom = margin + size;
	auto place = [&](Tool tool, int right, int width) {
		tools[(int)tool] = { right - width, top, right, bottom };
		return right - width;
	};

	// from the right: Follow, then the level buttons
	int x = client.right - margin;
	x = place(Tool::Follow, x, size) - Scale(8);
	for (auto tool : { Tool::LevelDebug, Tool::LevelInfo, Tool::LevelWarning, Tool::LevelError })
		x = place(tool, x, size) - Scale(2);
	x -= Scale(6);

	// the boxes share the rest, each with its options inside its right end
	int left = margin, gap = Scale(8);
	int width = std::max(0, (x - left - gap) / 2);
	boxes[0] = { left, top, left + width, bottom };
	boxes[1] = { left + width + gap, top, std::max(left + width + gap, x), bottom };
	int option = size - Scale(4);
	auto options = [&](const RECT& box, std::initializer_list<Tool> list) {
		int right = box.right - Scale(2);
		for (auto tool : list) {
			tools[(int)tool] = { std::max((int)box.left, right - option), top + Scale(2), right, bottom - Scale(2) };
			right = std::max((int)box.left, right - option - Scale(1));
		}
	};
	options(boxes[0], { Tool::FilterExclude, Tool::FilterRegex, Tool::FilterCase });
	options(boxes[1], { Tool::SearchRegex, Tool::SearchWord, Tool::SearchCase });
	tools[(int)Tool::None] = {};
}

RECT CLogViewCtrl::GetToolRect(Tool tool) const {
	RECT tools[(int)Tool::Count], boxes[2];
	ToolLayout(tools, boxes);
	return tools[(int)tool];
}

RECT CLogViewCtrl::GetBoxRect(bool search) const {
	RECT tools[(int)Tool::Count], boxes[2];
	ToolLayout(tools, boxes);
	return boxes[search ? 1 : 0];
}

CLogViewCtrl::Tool CLogViewCtrl::ToolHitTest(POINT pt) const {
	if (!m_ShowToolbar)
		return Tool::None;
	RECT tools[(int)Tool::Count], boxes[2];
	ToolLayout(tools, boxes);
	for (int i = 1; i < (int)Tool::Count; i++) {
		if (::PtInRect(&tools[i], pt))
			return (Tool)i;
	}
	return Tool::None;
}

void CLogViewCtrl::CreateToolbar() {
	if (m_hFilterBox)
		return;
	for (int i = 0; i < 2; i++) {
		HWND box = ::CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
		if (!box)
			return;
		::SendMessage(box, WM_SETFONT, (WPARAM)m_UiFont.m_hFont, 0);
		::SendMessage(box, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
		::SendMessage(box, EM_SETCUEBANNER, TRUE, (LPARAM)(i == 0 ? L"Filter (Ctrl+L)" : L"Search (Ctrl+F)"));
		::SetWindowSubclass(box, BoxSubclassProc, i + 1, (DWORD_PTR)this);
		(i == 0 ? m_hFilterBox : m_hSearchBox) = box;
	}

	m_hToolTip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		0, 0, 0, 0, m_hWnd, nullptr, nullptr, nullptr);
	if (m_hToolTip) {
		static const wchar_t* const names[] = { nullptr,
			L"Match case", L"Regular expression", L"Exclude: show the lines that don't match",
			L"Match case", L"Whole word", L"Regular expression",
			L"Errors", L"Warnings", L"Info", L"Debug and trace", L"Follow the tail" };
		static_assert(std::size(names) == (size_t)Tool::Count);
		RECT tools[(int)Tool::Count], boxes[2];
		ToolLayout(tools, boxes);
		for (int i = 1; i < (int)Tool::Count; i++) {
			TTTOOLINFOW ti{ sizeof(ti) };
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = m_hWnd;
			ti.uId = i;
			ti.rect = tools[i];
			ti.lpszText = const_cast<LPWSTR>(names[i]);
			::SendMessage(m_hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		}
	}
	SyncToolbar(true, true);
}

void CLogViewCtrl::PositionToolbar() {
	if (!m_hFilterBox || !m_ShowToolbar)
		return;
	RECT tools[(int)Tool::Count], boxes[2];
	ToolLayout(tools, boxes);
	// each box's edit: from its frame's left to its first option
	Tool firsts[2] = { Tool::FilterCase, Tool::SearchCase };
	HWND edits[2] = { m_hFilterBox, m_hSearchBox };
	for (int i = 0; i < 2; i++) {
		auto& box = boxes[i];
		int left = box.left + Scale(5);
		int right = std::max(left, (int)tools[(int)firsts[i]].left - Scale(2));
		int top = box.top + (box.bottom - box.top - m_UiTextHeight) / 2;
		::SetWindowPos(edits[i], nullptr, left, top, right - left, m_UiTextHeight, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
	if (m_hToolTip) {
		for (int i = 1; i < (int)Tool::Count; i++) {
			TTTOOLINFOW ti{ sizeof(ti) };
			ti.hwnd = m_hWnd;
			ti.uId = i;
			ti.rect = tools[i];
			::SendMessage(m_hToolTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
		}
	}
}

void CLogViewCtrl::InvalidateToolbar() {
	if (!IsWindow() || !m_ShowToolbar)
		return;
	RECT rc;
	GetClientRect(&rc);
	rc.bottom = ToolbarHeight();
	InvalidateRect(&rc, FALSE);
}

// ---- State ------------------------------------------------------------------------------------

DWORD CLogViewCtrl::LevelBits(Tool tool) {
	switch (tool) {
		case Tool::LevelError: return (1u << LGVL_ERROR) | (1u << LGVL_CRITICAL);
		case Tool::LevelWarning: return 1u << LGVL_WARNING;
		case Tool::LevelInfo: return (1u << LGVL_INFO) | (1u << LGVL_NONE);
		case Tool::LevelDebug: return (1u << LGVL_DEBUG) | (1u << LGVL_TRACE);
	}
	return 0;
}

bool CLogViewCtrl::IsToolOn(Tool tool) const {
	switch (tool) {
		case Tool::FilterCase: return (m_FilterBoxFlags & LGVFF_MATCHCASE) != 0;
		case Tool::FilterRegex: return (m_FilterBoxFlags & LGVFF_REGEX) != 0;
		case Tool::FilterExclude: return (m_FilterBoxFlags & LGVFF_EXCLUDE) != 0;
		case Tool::SearchCase: return (m_SearchBoxFlags & LGVFF_MATCHCASE) != 0;
		case Tool::SearchWord: return (m_SearchBoxFlags & LGVFF_WHOLEWORD) != 0;
		case Tool::SearchRegex: return (m_SearchBoxFlags & LGVFF_REGEX) != 0;
		case Tool::Follow: return m_Follow;
	}
	DWORD bits = LevelBits(tool);
	return bits && (m_Filter->levelMask & bits) == bits;
}

void CLogViewCtrl::PressTool(Tool tool) {
	switch (tool) {
		case Tool::FilterCase: m_FilterBoxFlags ^= LGVFF_MATCHCASE; break;
		case Tool::FilterRegex: m_FilterBoxFlags ^= LGVFF_REGEX; break;
		case Tool::FilterExclude: m_FilterBoxFlags ^= LGVFF_EXCLUDE; break;
		case Tool::SearchCase: m_SearchBoxFlags ^= LGVFF_MATCHCASE; break;
		case Tool::SearchWord: m_SearchBoxFlags ^= LGVFF_WHOLEWORD; break;
		case Tool::SearchRegex: m_SearchBoxFlags ^= LGVFF_REGEX; break;
		case Tool::Follow: {
			bool follow = !m_Follow;
			SetFollow(follow, true);
			if (follow) {
				UpdateScrollBars();   // to the end
				m_Text->Invalidate();
			}
			InvalidateToolbar();
			return;
		}
		default:
			break;
	}
	if (tool >= Tool::SearchCase && tool <= Tool::SearchRegex) {
		ApplyToolbarSearch(true);
	}
	else if (DWORD bits = LevelBits(tool)) {
		// on: all its levels shown, so a press hides them all; off (some or none shown) shows them all
		DWORD mask = m_Filter->levelMask;
		DWORD next = (mask & bits) == bits ? mask & ~bits : mask | bits;
		LGVFILTER f;
		GetFilter(&f);
		std::wstring text = f.pszText;   // the filter's own text: the box's may be a bad regex
		f.pszText = text.c_str();
		f.levelMask = next;
		SetFilter(&f);
		NMHDR hdr{};
		FireNotify(&hdr, LGVN_FILTERCHANGED);
	}
	else {
		ApplyToolbarFilter(true);
	}
	InvalidateToolbar();
}

void CLogViewCtrl::ApplyToolbarFilter(bool notify) {
	if (!m_hFilterBox)
		return;
	auto text = WindowText(m_hFilterBox);
	LGVFILTER f;
	f.pszText = text.c_str();
	f.flags = m_FilterBoxFlags;
	f.levelMask = m_Filter->levelMask;
	UINT flags = m_FilterBoxFlags;
	m_FilterBad = !SetFilter(&f);
	m_FilterBoxFlags = flags;   // kept as pressed, even when the regex is bad
	InvalidateToolbar();
	if (notify && !m_FilterBad) {
		NMHDR hdr{};
		FireNotify(&hdr, LGVN_FILTERCHANGED);
	}
}

void CLogViewCtrl::ApplyToolbarSearch(bool notify) {
	if (!m_hSearchBox)
		return;
	auto text = WindowText(m_hSearchBox);
	LGVSEARCH s;
	s.pszText = text.c_str();
	s.flags = m_SearchBoxFlags;
	UINT flags = m_SearchBoxFlags;
	m_SearchBad = !SetSearch(&s);
	m_SearchBoxFlags = flags;
	InvalidateToolbar();
	if (notify && !m_SearchBad) {
		NMHDR hdr{};
		FireNotify(&hdr, LGVN_SEARCHCHANGED);
	}
}

void CLogViewCtrl::SyncToolbar(bool filter, bool search) {
	// one box at a time: the other may hold text being typed that isn't in effect (a regex not finished yet)
	if (!m_hFilterBox)
		return;
	m_SettingBox = true;
	// options the toolbar doesn't show (a filter's whole word) stay, so the box applies them too
	if (filter) {
		LGVFILTER f;
		GetFilter(&f);
		if (WindowText(m_hFilterBox) != f.pszText)
			::SetWindowTextW(m_hFilterBox, f.pszText);
		m_FilterBoxFlags = f.flags;
		m_FilterBad = false;
	}
	if (search) {
		LGVSEARCH s;
		GetSearch(&s);
		if (WindowText(m_hSearchBox) != s.pszText)
			::SetWindowTextW(m_hSearchBox, s.pszText);
		m_SearchBoxFlags = s.flags;
		m_SearchBad = false;
	}
	m_SettingBox = false;
	InvalidateToolbar();
}

void CLogViewCtrl::FocusBox(bool search) {
	HWND box = search ? m_hSearchBox : m_hFilterBox;
	if (!box || !m_ShowToolbar)
		return;
	::SetFocus(box);
	::SendMessage(box, EM_SETSEL, 0, -1);
}

// ---- Painting -----------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		if (m_ShowToolbar)
			PaintToolbar((HDC)wParam, GetColors());
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	if (m_ShowToolbar) {
		WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
		PaintToolbar(mdc.m_hDC, GetColors());
	}
	return 0;
}

void CLogViewCtrl::PaintToolbar(WTL::CDCHandle dc, const LGVCOLORS& c) {
	RECT band;
	GetClientRect(&band);
	band.bottom = ToolbarHeight();
	FillSolid(dc, band, c.crGutterBack);
	FillSolid(dc, { band.left, band.bottom - 1, band.right, band.bottom }, Blend(c.crGutterBack, c.crText, 15));

	RECT tools[(int)Tool::Count], boxes[2];
	ToolLayout(tools, boxes);
	HWND focus = ::GetFocus();
	for (int i = 0; i < 2; i++) {
		bool bad = i == 0 ? m_FilterBad : m_SearchBad;
		bool focused = focus == (i == 0 ? m_hFilterBox : m_hSearchBox);
		FillSolid(dc, boxes[i], c.crBack);
		Frame(dc, boxes[i], bad ? RGB(0xE0, 0x40, 0x40) : focused ? c.crSelBack : Blend(c.crBack, c.crText, 30));
	}

	auto oldFont = dc.SelectFont(m_UiFont);
	dc.SetBkMode(TRANSPARENT);
	for (int i = 1; i < (int)Tool::Count; i++) {
		auto tool = (Tool)i;
		auto rc = tools[i];
		if (rc.right <= rc.left)
			continue;
		bool inBox = tool <= Tool::SearchRegex;
		COLORREF back = inBox ? c.crBack : c.crGutterBack;
		bool on = IsToolOn(tool);
		if (on) {
			FillSolid(dc, rc, Blend(back, c.crSelBack, 25));
			Frame(dc, rc, c.crSelBack);
		}
		else if (tool == m_HotTool) {
			FillSolid(dc, rc, Blend(back, c.crText, 12));
		}
		COLORREF text = on || inBox ? c.crText : Blend(c.crText, back, 45);
		dc.SetTextColor(text);

		if (tool == Tool::Follow) {
			// an arrow down to a bar
			WTL::CPen pen;
			pen.CreatePen(PS_SOLID, std::max(1, Scale(1) + Scale(1) / 2), text);
			auto oldPen = dc.SelectPen(pen);
			int cx = (rc.left + rc.right) / 2, h = rc.bottom - rc.top;
			int top = rc.top + h / 4, tip = rc.bottom - h / 3, arm = h / 5;
			dc.MoveTo(cx, top);
			dc.LineTo(cx, tip);
			dc.MoveTo(cx - arm, tip - arm);
			dc.LineTo(cx, tip);
			dc.LineTo(cx + arm + 1, tip - arm - 1);
			dc.SelectPen(oldPen);
			FillSolid(dc, { cx - arm - 1, tip + Scale(2), cx + arm + 2, tip + Scale(2) + std::max(1, Scale(2)) }, text);
			continue;
		}
		static const wchar_t* const labels[] = { L"", L"Aa", L".*", L"!", L"Aa", L"ab", L".*", L"E", L"W", L"I", L"D", L"" };
		static_assert(std::size(labels) == (size_t)Tool::Count);
		RECT label = rc;
		if (DWORD bits = LevelBits(tool)) {
			// a bar in the level's color under the letter
			BYTE level = tool == Tool::LevelError ? LGVL_ERROR : tool == Tool::LevelWarning ? LGVL_WARNING :
				tool == Tool::LevelInfo ? LGVL_INFO : LGVL_DEBUG;
			auto style = m_LevelStyles[level];
			COLORREF color = style.crMarker != CLR_NONE && style.crMarker != CLR_DEFAULT ? style.crMarker :
				style.crText != CLR_DEFAULT ? style.crText : (style.flags & LGVSF_DIM) ? Blend(c.crText, back, 50) : c.crText;
			if (!on)
				color = Blend(color, back, 55);
			int w = (rc.right - rc.left) / 2;
			int barTop = rc.bottom - Scale(4);
			FillSolid(dc, { rc.left + w / 2, barTop, rc.right - w / 2, barTop + std::max(1, Scale(2)) }, color);
			label.bottom = barTop;
		}
		dc.DrawText(labels[i], -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
	}
	dc.SelectFont(oldFont);
}

// ---- Mouse --------------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnToolButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	auto tool = ToolHitTest(pt);
	if (tool != Tool::None) {
		PressTool(tool);
		return 0;
	}
	// a box's frame around its edit
	for (int i = 0; i < 2 && m_ShowToolbar; i++) {
		auto box = GetBoxRect(i == 1);
		if (::PtInRect(&box, pt))
			FocusBox(i == 1);
	}
	return 0;
}

LRESULT CLogViewCtrl::OnToolMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	auto tool = ToolHitTest(pt);
	if (tool != m_HotTool) {
		m_HotTool = tool;
		InvalidateToolbar();
	}
	if (!m_TrackingLeave) {
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
		m_TrackingLeave = ::TrackMouseEvent(&tme) != FALSE;
	}
	return 0;
}

LRESULT CLogViewCtrl::OnToolMouseLeave(UINT, WPARAM, LPARAM, BOOL&) {
	m_TrackingLeave = false;
	if (m_HotTool != Tool::None) {
		m_HotTool = Tool::None;
		InvalidateToolbar();
	}
	return 0;
}

// ---- The boxes ------------------------------------------------------------------------------------

LRESULT CLogViewCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	HWND box = (HWND)lParam;
	if (!box || (box != m_hFilterBox && box != m_hSearchBox)) {
		bHandled = FALSE;
		return 0;
	}
	switch (HIWORD(wParam)) {
		case EN_CHANGE:
			if (!m_SettingBox) {
				if (box == m_hFilterBox)
					ApplyToolbarFilter(true);
				else
					ApplyToolbarSearch(true);
			}
			break;
		case EN_SETFOCUS:
		case EN_KILLFOCUS:
			InvalidateToolbar();   // the frame shows the focus
			break;
	}
	return 0;
}

LRESULT CLogViewCtrl::OnCtlColor(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	HWND box = (HWND)lParam;
	if (box != m_hFilterBox && box != m_hSearchBox) {
		bHandled = FALSE;
		return 0;
	}
	auto c = GetColors();
	::SetTextColor((HDC)wParam, c.crText);
	::SetBkColor((HDC)wParam, c.crBack);
	if (m_BoxBrushColor != c.crBack) {
		if (!m_BoxBrush.IsNull())
			m_BoxBrush.DeleteObject();
		m_BoxBrush.CreateSolidBrush(c.crBack);
		m_BoxBrushColor = c.crBack;
	}
	return (LRESULT)m_BoxBrush.m_hBrush;
}

LRESULT CALLBACK CLogViewCtrl::BoxSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	auto self = (CLogViewCtrl*)data;
	bool search = id == 2;
	switch (msg) {
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, BoxSubclassProc, id);
			break;
		case WM_GETDLGCODE:
			// in a dialog: Enter, Escape and Tab are the box's
			if (auto m = (MSG*)lParam; m && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE || m->wParam == VK_TAB))
				return ::DefSubclassProc(hWnd, msg, wParam, lParam) | DLGC_WANTMESSAGE;
			break;
		case WM_KEYDOWN: {
			bool shift = ::GetKeyState(VK_SHIFT) < 0, ctrl = ::GetKeyState(VK_CONTROL) < 0;
			switch (wParam) {
				case VK_RETURN:
					if (search && !self->m_Search->Empty())
						self->StartFind(!shift, true);
					else if (!search)
						self->m_Text->SetFocus();   // on to the lines
					return 0;
				case VK_F3:
					if (!self->m_Search->Empty())
						self->StartFind(!shift, true);
					return 0;
				case VK_ESCAPE:
					if (::GetWindowTextLengthW(hWnd) > 0)
						::SetWindowTextW(hWnd, L"");   // EN_CHANGE applies it
					else
						self->m_Text->SetFocus();
					return 0;
				case VK_DOWN:
					self->m_Text->SetFocus();
					return 0;
				case VK_TAB:
					// filter box -> search box -> the lines, and back with Shift
					if (search != shift)
						self->m_Text->SetFocus();
					else
						self->FocusBox(!search);
					return 0;
				case 'F':
				case 'L':
					if (ctrl) {
						self->FocusBox(wParam == 'F');
						return 0;
					}
					break;
			}
			break;
		}
		case WM_CHAR:
			// no beeps for the keys handled above (Ctrl+F and Ctrl+L come as 6 and 12)
			if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB || wParam == 6 || wParam == 12)
				return 0;
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}
