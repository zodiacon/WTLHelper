#include "TokenEditData.h"

#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

using namespace TkeDraw;

namespace {
	constexpr int MaxShown = 8;       // rows shown at once
	constexpr int MaxRows = 100;

	std::wstring Trim(std::wstring s) {
		while (!s.empty() && (s.back() == L' ' || s.back() == L'\t'))
			s.pop_back();
		size_t i = 0;
		while (i < s.size() && (s[i] == L' ' || s[i] == L'\t'))
			i++;
		return s.substr(i);
	}
}

// ---- Text with a bold part ------------------------------------------------------------------------

int TkeDraw::RunText(WTL::CDCHandle dc, const std::wstring& text, int x, const RECT& line, int boldStart, int boldLength,
	HFONT normal, HFONT bold) {
	boldStart = std::clamp(boldStart, 0, (int)text.size());
	boldLength = std::clamp(boldLength, 0, (int)text.size() - boldStart);
	const std::pair<int, int> parts[] = { { 0, boldStart }, { boldStart, boldLength }, { boldStart + boldLength, (int)text.size() - boldStart - boldLength } };
	for (int i = 0; i < 3; i++) {
		auto [from, length] = parts[i];
		if (length <= 0)
			continue;
		auto old = dc.SelectFont(i == 1 ? bold : normal);
		TEXTMETRICW tm{};
		dc.GetTextMetrics(&tm);
		SIZE size{};
		dc.GetTextExtent(text.c_str() + from, length, &size);
		dc.TextOut(x, (line.top + line.bottom - tm.tmHeight) / 2, text.c_str() + from, length);
		dc.SelectFont(old);
		x += size.cx;
	}
	return x;
}

// ---- The list --------------------------------------------------------------------------------------

int TkeSuggestList::Visible() const {
	return std::min((int)m_Rows.size(), MaxShown);
}

void TkeSuggestList::Show(std::vector<TkeSuggestRow> rows, const RECT& under) {
	auto& o = *m_Owner;
	auto& d = *o.m_Data;
	if (!IsWindow()) {
		HWND root = ::GetAncestor(o.m_hWnd, GA_ROOT);
		Create(root, rcDefault, nullptr, WS_POPUP | WS_CLIPSIBLINGS, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
		// Windows 11 rounds the corners and draws the border
		DWORD corner = 3;   // DWMWCP_ROUNDSMALL
		m_Border = SUCCEEDED(::DwmSetWindowAttribute(m_hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner))) ? 0 : 1;
	}
	m_Rows = std::move(rows);
	m_Top = 0;
	m_Hot = m_Rows.empty() ? -1 : 0;
	m_Mouse = { -1, -1 };

	int icon = 0, cx, cy;
	if (o.m_Images && ::ImageList_GetIconSize(o.m_Images, &cx, &cy))
		icon = cx;
	m_RowHeight = std::max(o.m_TextHeight, icon) + o.Scale(8);

	// as wide as the widest row, within limits
	int width = 0;
	{
		WTL::CClientDC dc(m_hWnd);
		auto old = dc.SelectFont(o.m_hBold);
		for (auto& row : m_Rows) {
			auto& s = d.suggestions[row.index];
			SIZE text{}, detail{};
			dc.GetTextExtent(s.text.c_str(), (int)s.text.size(), &text);
			if (!s.detail.empty())
				dc.GetTextExtent(s.detail.c_str(), (int)s.detail.size(), &detail);
			width = std::max(width, (int)(text.cx + (detail.cx ? detail.cx + o.Scale(12) : 0)));
		}
		dc.SelectFont(old);
	}
	const int pad = o.Scale(8);
	width += 2 * pad + (icon ? icon + o.Scale(6) : 0) + 2 * m_Border + o.Scale(8);
	width = std::clamp(width, o.Scale(200), o.Scale(520));
	const int height = Visible() * m_RowHeight + 2 * o.Scale(4) + 2 * m_Border;

	// below the typing area, or above it if there's no room
	MONITORINFO mi{ sizeof(mi) };
	::GetMonitorInfo(::MonitorFromRect(&under, MONITOR_DEFAULTTONEAREST), &mi);
	const RECT& work = mi.rcWork;
	int x = under.left, y = under.bottom + o.Scale(2);
	if (y + height > work.bottom && under.top - o.Scale(2) - height >= work.top)
		y = under.top - o.Scale(2) - height;
	x = std::max((int)work.left, std::min(x, (int)work.right - width));
	SetWindowPos(HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_Open = true;
	Invalidate();
}

void TkeSuggestList::Close() {
	if (!m_Open)
		return;
	m_Open = false;
	if (IsWindow())
		ShowWindow(SW_HIDE);
	m_Rows.clear();
	m_Hot = -1;
}

void TkeSuggestList::ScrollTo(int top) {
	top = std::clamp(top, 0, std::max(0, (int)m_Rows.size() - Visible()));
	if (top != m_Top) {
		m_Top = top;
		Invalidate();
	}
}

void TkeSuggestList::SetHot(int row) {
	if (row < 0 || row >= (int)m_Rows.size() || row == m_Hot)
		return;
	m_Hot = row;
	if (row < m_Top)
		ScrollTo(row);
	else if (row >= m_Top + Visible())
		ScrollTo(row - Visible() + 1);
	Invalidate();
}

void TkeSuggestList::MoveHot(UINT vk) {
	const int n = (int)m_Rows.size();
	if (!n)
		return;
	int row = m_Hot;
	switch (vk) {
		case VK_UP: row--; break;
		case VK_DOWN: row++; break;
		case VK_PRIOR: row -= Visible(); break;
		case VK_NEXT: row += Visible(); break;
	}
	SetHot(std::clamp(row, 0, n - 1));
}

void TkeSuggestList::Wheel(int delta) {
	ScrollTo(m_Top - delta * 3 / WHEEL_DELTA);
}

int TkeSuggestList::RowAt(POINT pt) const {
	const int top = m_Border + m_Owner->Scale(4);
	if (pt.y < top || m_RowHeight <= 0)
		return -1;
	int row = m_Top + (pt.y - top) / m_RowHeight;
	return row < m_Top + Visible() ? row : -1;
}

LRESULT TkeSuggestList::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	POINT screen;
	::GetCursorPos(&screen);
	if (screen.x == m_Mouse.x && screen.y == m_Mouse.y)
		return 0;   // the list appearing under a resting mouse doesn't make that row hot
	m_Mouse = screen;
	SetHot(RowAt({ (short)LOWORD(lParam), (short)HIWORD(lParam) }));
	return 0;
}

LRESULT TkeSuggestList::OnLButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	int row = RowAt({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
	if (row >= 0)
		m_Owner->PickSuggestion(m_Rows[row].index);
	return 0;
}

LRESULT TkeSuggestList::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	auto& o = *m_Owner;
	auto& d = *o.m_Data;
	auto c = o.PaintColors();
	auto paint = [&](WTL::CDCHandle dc) {
		RECT rc;
		GetClientRect(&rc);
		FillSolid(dc, rc, c.crBack);
		if (m_Border) {
			WTL::CBrush brush;
			brush.CreateSolidBrush(c.crBorder);
			dc.FrameRect(&rc, brush);
		}
		dc.SetBkMode(TRANSPARENT);
		int icon = 0, cx, cy;
		if (o.m_Images && ::ImageList_GetIconSize(o.m_Images, &cx, &cy))
			icon = cx;
		const int pad = o.Scale(8), top = m_Border + o.Scale(4);
		for (int i = m_Top; i < m_Top + Visible(); i++) {
			auto& row = m_Rows[i];
			auto& s = d.suggestions[row.index];
			RECT line{ rc.left + m_Border, top + (i - m_Top) * m_RowHeight, rc.right - m_Border, top + (i - m_Top + 1) * m_RowHeight };
			if (i == m_Hot) {
				RECT hot{ line.left + o.Scale(4), line.top + 1, line.right - o.Scale(4), line.bottom - 1 };
				RoundFill(dc, hot, c.crTokenHot, c.crTokenHot, o.Scale(6));
			}
			int x = line.left + pad;
			if (icon && s.image >= 0) {
				::ImageList_Draw(o.m_Images, s.image, dc, x, (line.top + line.bottom - icon) / 2, ILD_TRANSPARENT);
				x += icon + o.Scale(6);
			}
			else if (icon)
				x += icon + o.Scale(6);   // the texts in one column
			const int saved = dc.SaveDC();
			dc.IntersectClipRect(line.left, line.top, line.right - pad, line.bottom);
			dc.SetTextColor(c.crText);
			const bool inText = row.match.kind != TkeMatchKind::None && !row.onDetail;
			x = RunText(dc, s.text, x, line, inText ? row.match.start : 0, inText ? row.match.length : 0, o.m_hFont, o.m_hBold);
			if (!s.detail.empty()) {
				dc.SetTextColor(c.crCue);
				const bool inDetail = row.match.kind != TkeMatchKind::None && row.onDetail;
				RunText(dc, s.detail, x + o.Scale(12), line, inDetail ? row.match.start : 0, inDetail ? row.match.length : 0,
					o.m_hFont, o.m_hBold);
			}
			dc.RestoreDC(saved);
		}
	};
	if (uMsg == WM_PRINTCLIENT) {
		paint((HDC)wParam);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	paint(mdc.m_hDC);
	return 0;
}

// ---- The control's suggestions -------------------------------------------------------------------------

BOOL CTokenEditCtrl::AddSuggestion(const TKESUGGESTION* suggestion) {
	if (!suggestion || !suggestion->pszText || !*suggestion->pszText)
		return FALSE;
	TkeSuggestionItem item;
	item.text = suggestion->pszText;
	item.detail = suggestion->pszDetail ? suggestion->pszDetail : L"";
	item.token = suggestion->pszToken ? suggestion->pszToken : L"";
	item.image = suggestion->iImage;
	item.lParam = suggestion->lParam;
	m_Data->suggestions.push_back(std::move(item));
	return TRUE;
}

BOOL CTokenEditCtrl::AddSuggestion(LPCWSTR text, LPCWSTR detail, int image, LPARAM lParam) {
	TKESUGGESTION s;
	s.pszText = text;
	s.pszDetail = detail;
	s.iImage = image;
	s.lParam = lParam;
	return AddSuggestion(&s);
}

void CTokenEditCtrl::ClearSuggestions() {
	CloseSuggestions();   // its rows index the old ones
	m_Data->suggestions.clear();
}

BOOL CTokenEditCtrl::ShowSuggestions(BOOL show) {
	if (!show) {
		CloseSuggestions();
		return TRUE;
	}
	if (!IsWindow() || ReadOnly() || !m_Edit.IsWindow())
		return FALSE;
	if (::GetFocus() != m_Edit.m_hWnd)
		FocusEditAt(false);   // the keys come to the list through the edit
	UpdateSuggestions(true);
	return IsSuggesting();
}

bool CTokenEditCtrl::IsSuggesting() const {
	auto& d = *m_Data;
	return d.list && d.list->IsOpen();
}

void CTokenEditCtrl::CloseSuggestions() {
	auto& d = *m_Data;
	d.suggestingAll = false;
	if (d.list)
		d.list->Close();
}

void CTokenEditCtrl::UpdateSuggestions(bool all) {
	auto& d = *m_Data;
	if (!IsWindow() || ReadOnly())
		return;
	auto typed = Trim(EditText());
	if (!typed.empty())
		all = false;
	else if (!all && !d.suggestingAll) {
		CloseSuggestions();
		return;
	}
	else
		all = true;   // Alt+Down showed them all: as long as nothing is typed

	// the app may change the list for the text (ClearSuggestions closes the list: its rows index the old ones)
	NMTKESUGGEST nm{};
	nm.pszText = typed.c_str();
	FireNotify(&nm.hdr, TKEN_GETSUGGESTIONS);
	if (!IsWindow())
		return;

	const bool inside = (GetStyle() & TKES_SUGGESTINSIDE) != 0;
	std::vector<TkeSuggestRow> kinds[3];
	for (int i = 0; i < (int)d.suggestions.size(); i++) {
		auto& s = d.suggestions[i];
		if (d.tokens.FindText(s.token.empty() ? s.text : s.token) >= 0)
			continue;   // a token already
		TkeSuggestRow row{ i };
		if (!all) {
			row.match = TkeMatchText(typed, s.text, inside);
			if (row.match.kind == TkeMatchKind::None && !s.detail.empty()) {
				row.match = TkeMatchText(typed, s.detail, inside);
				row.onDetail = true;
			}
			if (row.match.kind == TkeMatchKind::None)
				continue;
		}
		kinds[all ? 0 : (int)row.match.kind - 1].push_back(row);
	}
	std::vector<TkeSuggestRow> rows;
	for (auto& kind : kinds)
		for (auto& row : kind)
			if (rows.size() < MaxRows)
				rows.push_back(row);
	if (rows.empty()) {
		CloseSuggestions();
		return;
	}

	d.suggestingAll = all;
	if (!d.list)
		d.list = std::make_unique<TkeSuggestList>(this);
	// under the typed text, the rows' text lined up with it
	EnsureLayout();
	RECT under = d.layout.edit;
	::OffsetRect(&under, 0, -d.scroll);
	::MapWindowPoints(m_hWnd, nullptr, (POINT*)&under, 2);
	int icon = 0, cx, cy;
	if (m_Images && ::ImageList_GetIconSize(m_Images, &cx, &cy))
		icon = cx + Scale(6);
	under.left += Scale(2) - Scale(8) - icon - 1;
	d.list->Show(std::move(rows), under);
}

void CTokenEditCtrl::PickSuggestion(int index) {
	auto& d = *m_Data;
	if (index < 0 || index >= (int)d.suggestions.size())
		return;
	auto& s = d.suggestions[index];
	TkeToken t;
	t.text = s.token.empty() ? s.text : s.token;
	t.image = s.image;
	t.lParam = s.lParam;
	CloseSuggestions();
	d.tokenizing = true;
	auto refused = AddTyped({ std::move(t) }, TKEA_SUGGESTION);
	d.tokenizing = false;
	if (!IsWindow())
		return;
	if (refused.empty()) {
		SetTyped({}, false);   // what was typed is the token now
		d.editing = false;     // (an edited token too)
	}
	if (::GetFocus() != m_Edit.m_hWnd)
		FocusEditAt(false);
	ScrollToShow(d.layout.edit);
}

bool CTokenEditCtrl::SuggestKey(UINT vk) {
	auto& d = *m_Data;
	if (!IsSuggesting())
		return false;
	switch (vk) {
		case VK_UP:
		case VK_DOWN:
		case VK_PRIOR:
		case VK_NEXT:
			d.list->MoveHot(vk);
			return true;

		case VK_RETURN:
		case VK_TAB: {
			int hot = d.list->Hot();
			if (hot < 0 || hot >= (int)d.list->Rows().size()) {
				CloseSuggestions();
				return false;
			}
			PickSuggestion(d.list->Rows()[hot].index);
			return true;
		}

		case VK_ESCAPE:
			CloseSuggestions();
			return true;
	}
	return false;
}

LRESULT CTokenEditCtrl::OnEditSysKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	// Alt+Down: the suggestions, all of them with nothing typed
	if (wParam == VK_DOWN) {
		if (IsSuggesting())
			CloseSuggestions();
		else
			ShowSuggestions(TRUE);
		return 0;
	}
	bHandled = FALSE;
	return 0;
}
