// The markdown view's selection, links and finding: the text under the mouse, selecting by dragging (words and
// blocks by double and triple clicks), copying, links' clicks and tooltips, task boxes, and finding text.

#include "MarkdownViewData.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "comctl32.lib")

// ---- Positions ------------------------------------------------------------------------

bool MdvState::Before(const MdvTextPos& a, const MdvTextPos& b) const {
	const int oa = leafOrder[a.block], ob = leafOrder[b.block];
	return oa < ob || (oa == ob && a.pos < b.pos);
}

bool MdvState::Selected(int leaf, size_t length, size_t& start, size_t& end) const {
	if (!HasSelection() || leaf < 0 || leaf >= (int)leafOrder.size() || leafOrder[leaf] < 0)
		return false;
	const MdvTextPos& first = Before(caret, anchor) ? caret : anchor;
	const MdvTextPos& last = Before(caret, anchor) ? anchor : caret;
	const int o = leafOrder[leaf], fo = leafOrder[first.block], lo = leafOrder[last.block];
	if (o < fo || o > lo)
		return false;
	start = o == fo ? std::min(first.pos, length) : 0;
	end = o == lo ? std::min(last.pos, length) : length;
	return start < end;
}

void CMarkdownViewCtrl::UpdateLeaves() {
	auto& s = *m_State;
	s.leaves = s.doc.TextLeaves();
	s.leafOrder.assign(s.doc.blocks.size(), -1);
	for (size_t i = 0; i < s.leaves.size(); i++)
		s.leafOrder[s.leaves[i]] = (int)i;
	s.anchor = s.caret = {};
	s.highlights.clear();
	s.hotLeaf = s.hotLink = -1;
	s.selecting = false;
}

const MdvText* CMarkdownViewCtrl::LeafText(int leaf, float& x, float& y) const {
	auto& s = *m_State;
	if (leaf < 0 || leaf >= (int)s.doc.blocks.size())
		return nullptr;
	auto& block = s.doc.blocks[leaf];
	if (block.kind == MdvKind::Cell) {
		// a table's cell: its place in the table
		const int rowBlock = block.parent, table = rowBlock >= 0 ? s.doc.blocks[rowBlock].parent : -1;
		if (table < 0 || s.blockBox[table] < 0)
			return nullptr;
		auto& box = s.flow.boxes[s.blockBox[table]];
		auto it = s.layouts.find(MdvState::Key(table, MdvBoxKind::Table));
		if (it == s.layouts.end())
			return nullptr;
		auto& cache = it->second;
		auto& rows = s.doc.blocks[table].children;
		auto& cells = s.doc.blocks[rowBlock].children;
		const size_t r = std::find(rows.begin(), rows.end(), rowBlock) - rows.begin();
		const size_t c = std::find(cells.begin(), cells.end(), leaf) - cells.begin();
		const size_t columns = std::max<size_t>(s.doc.blocks[table].columns, 1);
		if (r >= cache.rowTops.size() || c >= cache.columns.size() || r * columns + c >= cache.cells.size())
			return nullptr;
		const float em = s.metrics.fontSize * box.scale;
		auto side = s.sideways.find(table);
		x = ColumnLeft() + box.indent - (side == s.sideways.end() ? 0 : side->second) + MdvTableBorder + MdvCellPadX * em;
		for (size_t k = 0; k < c; k++)
			x += cache.columns[k] + 2 * MdvCellPadX * em + MdvTableBorder;
		y = box.top - s.scrollY + cache.rowTops[r] + MdvCellPadY * em;
		const MdvText& t = cache.cells[r * columns + c];
		return t.layout ? &t : nullptr;
	}
	const int index = s.blockBox[leaf];
	if (index < 0)
		return nullptr;
	auto& box = s.flow.boxes[index];
	auto it = s.layouts.find(MdvState::Key(leaf, box.kind));
	if (it == s.layouts.end() || !it->second.text.layout)
		return nullptr;
	float side = 0;
	if (box.kind == MdvBoxKind::Code) {
		auto sw = s.sideways.find(leaf);
		side = sw == s.sideways.end() ? 0 : sw->second;
	}
	x = ColumnLeft() + box.indent + box.padLeft - side;
	y = box.top - s.scrollY + box.padTop;
	return &it->second.text;
}

bool CMarkdownViewCtrl::PositionAt(POINT pt, int& leaf, size_t& pos, bool* inside, size_t* hit) const {
	auto& s = *m_State;
	if (inside)
		*inside = false;
	if (s.leaves.empty())
		return false;
	auto& flow = s.flow;
	const float vx = pt.x / s.Scale(), vy = pt.y / s.Scale(), y = vy + s.scrollY;
	auto length = [&](int b) { return s.doc.blocks[b].content.text.size(); };
	// the first text leaf a box shows
	auto boxLeaf = [&](size_t i) -> int {
		auto& box = flow.boxes[i];
		auto& block = s.doc.blocks[box.block];
		if (box.kind == MdvBoxKind::Table) {
			// its first cell
			if (block.children.empty() || s.doc.blocks[block.children[0]].children.empty())
				return -1;
			return s.doc.blocks[block.children[0]].children[0];
		}
		return (box.kind == MdvBoxKind::Text || box.kind == MdvBoxKind::Summary || box.kind == MdvBoxKind::Code) && block.HasContent() ? box.block : -1;
	};
	const size_t i = flow.BoxAt(y);
	if (i >= flow.boxes.size()) {
		leaf = s.leaves.back();
		pos = length(leaf);
		return true;
	}
	auto& box = flow.boxes[i];
	int target = -1;
	if (box.kind == MdvBoxKind::Table && y >= box.top) {
		auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
		if (it != s.layouts.end() && !it->second.rowTops.empty()) {
			auto& cache = it->second;
			auto& table = s.doc.blocks[box.block];
			size_t r = 0;
			while (r + 1 < cache.rowTops.size() && y >= box.top + cache.rowTops[r + 1])
				r++;
			const float em = s.metrics.fontSize * box.scale;
			auto side = s.sideways.find(box.block);
			float cx = ColumnLeft() + box.indent - (side == s.sideways.end() ? 0 : side->second) + MdvTableBorder;
			size_t c = 0;
			while (c + 1 < cache.columns.size() && vx >= cx + cache.columns[c] + 2 * MdvCellPadX * em + MdvTableBorder) {
				cx += cache.columns[c] + 2 * MdvCellPadX * em + MdvTableBorder;
				c++;
			}
			if (r < table.children.size()) {
				auto& cells = s.doc.blocks[table.children[r]].children;
				if (c < cells.size())
					target = cells[c];
			}
		}
	}
	else if (y >= box.top)
		target = boxLeaf(i);
	if (target < 0) {
		// between boxes, or on one without text: the next text's start
		for (size_t j = i; j < flow.boxes.size(); j++) {
			const int l = boxLeaf(j);
			if (l >= 0) {
				leaf = l;
				pos = 0;
				return true;
			}
		}
		leaf = s.leaves.back();
		pos = length(leaf);
		return true;
	}
	float tx, ty;
	const MdvText* t = LeafText(target, tx, ty);
	leaf = target;
	pos = 0;
	if (!t)
		return true;
	BOOL trailing = FALSE, isInside = FALSE;
	DWRITE_HIT_TEST_METRICS m{};
	t->layout->HitTestPoint(vx - tx, vy - ty, &trailing, &isInside, &m);
	pos = std::min<size_t>(m.textPosition + (trailing ? m.length : 0), length(target));
	if (inside)
		*inside = isInside != FALSE;
	if (hit)
		*hit = m.textPosition;
	return true;
}

// ---- Selecting --------------------------------------------------------------------------

void CMarkdownViewCtrl::SelectionChanged(bool notify) {
	Invalidate();
	if (notify) {
		NMHDR nm{ m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_SELCHANGED };
		::SendMessage(GetParent(), WM_NOTIFY, nm.idFrom, (LPARAM)&nm);
	}
}

bool CMarkdownViewCtrl::PressText(POINT pt, bool shift, bool doubleClick) {
	auto& s = *m_State;
	int leaf;
	size_t pos;
	if (!PositionAt(pt, leaf, pos))
		return false;
	// a second click is the double click's message; a third comes soon after it, near it
	const DWORD now = ::GetMessageTime();
	if (doubleClick)
		s.clicks = 2;
	else if (s.clicks == 2 && now - s.lastClick <= ::GetDoubleClickTime() &&
		std::abs(pt.x - s.lastClickPoint.x) <= ::GetSystemMetrics(SM_CXDOUBLECLK) &&
		std::abs(pt.y - s.lastClickPoint.y) <= ::GetSystemMetrics(SM_CYDOUBLECLK))
		s.clicks = 3;
	else
		s.clicks = 1;
	s.lastClick = now;
	s.lastClickPoint = pt;

	s.pressLeaf = s.pressLink = -1;
	int linkLeaf, link;
	float rc[4];
	if (s.clicks == 1 && !shift && LinkAt(pt, linkLeaf, link, rc)) {
		s.pressLeaf = linkLeaf;
		s.pressLink = link;
	}
	if (!(GetStyle() & MDVS_NOSELECTION)) {
		auto& text = s.doc.blocks[leaf].content.text;
		if (s.clicks == 2) {
			size_t a, b;
			MdvWordAt(text, pos, a, b);
			s.anchor = { leaf, a };
			s.caret = { leaf, b };
		}
		else if (s.clicks == 3) {
			s.anchor = { leaf, 0 };
			s.caret = { leaf, text.size() };
		}
		else if (shift && s.anchor.block >= 0)
			s.caret = { leaf, pos };
		else
			s.anchor = s.caret = { leaf, pos };
	}
	s.selecting = true;
	s.dragged = false;
	s.pressPoint = s.mouse = pt;
	SetCapture();
	Invalidate();
	return true;
}

void CMarkdownViewCtrl::DragSelection(POINT pt) {
	auto& s = *m_State;
	s.mouse = pt;
	if (!s.dragged && (std::abs(pt.x - s.pressPoint.x) > ::GetSystemMetrics(SM_CXDRAG) ||
		std::abs(pt.y - s.pressPoint.y) > ::GetSystemMetrics(SM_CYDRAG)))
		s.dragged = true;
	if (!s.dragged || (GetStyle() & MDVS_NOSELECTION))
		return;
	int leaf;
	size_t pos;
	if (PositionAt(pt, leaf, pos) && !(s.caret == MdvTextPos{ leaf, pos })) {
		s.caret = { leaf, pos };
		Invalidate();
	}
	// past the top or the bottom: scroll there while the mouse stays
	if (pt.y < 0 || pt.y >= s.client.cy)
		SetTimer(ScrollTimer, 30);
	else
		KillTimer(ScrollTimer);
}

void CMarkdownViewCtrl::AutoScroll() {
	auto& s = *m_State;
	if (!s.selecting) {
		KillTimer(ScrollTimer);
		return;
	}
	const int over = s.mouse.y < 0 ? s.mouse.y : s.mouse.y >= s.client.cy ? s.mouse.y - s.client.cy + 1 : 0;
	if (over == 0) {
		KillTimer(ScrollTimer);
		return;
	}
	ScrollTo(s.scrollY + std::clamp(over / s.Scale(), -60.0f, 60.0f));
	DragSelection(s.mouse);
}

void CMarkdownViewCtrl::EndSelection(POINT pt) {
	auto& s = *m_State;
	KillTimer(ScrollTimer);
	const bool dragged = s.dragged;
	const int pressLeaf = s.pressLeaf, pressLink = s.pressLink;
	s.selecting = false;
	s.pressLeaf = s.pressLink = -1;
	::ReleaseCapture();
	// a click on a link that didn't become a drag: the link's
	int leaf, link;
	float rc[4];
	if (!dragged && pressLink >= 0 && LinkAt(pt, leaf, link, rc) && leaf == pressLeaf && link == pressLink) {
		ClickLink(leaf, link);
		return;
	}
	if (!(GetStyle() & MDVS_NOSELECTION))
		SelectionChanged(true);
}

BOOL CMarkdownViewCtrl::HasSelection() const {
	return m_State->HasSelection();
}

void CMarkdownViewCtrl::SelectAll() {
	auto& s = *m_State;
	if (s.leaves.empty())
		return;
	s.anchor = { s.leaves.front(), 0 };
	s.caret = { s.leaves.back(), s.doc.blocks[s.leaves.back()].content.text.size() };
	SelectionChanged(false);
}

void CMarkdownViewCtrl::ClearSelection() {
	m_State->anchor = m_State->caret = {};
	SelectionChanged(false);
}

static MdvRange SelectionRange(const MdvState& s) {
	const bool reversed = s.Before(s.caret, s.anchor);
	return { reversed ? s.caret : s.anchor, reversed ? s.anchor : s.caret };
}

static size_t SourceOfPos(const MdvDocument& doc, const MdvTextPos& p) {
	if (p.block < 0)
		return 0;
	const size_t offset = doc.blocks[p.block].content.SourceOf(p.pos);
	return offset == MDV_NOPOS ? doc.blocks[p.block].sourceStart : offset;
}

// the text leaf holding a source offset, and the place in it
static MdvTextPos PosOfSource(const MdvState& s, size_t offset) {
	if (s.leaves.empty())
		return {};
	int leaf = s.doc.LeafAtSource(offset);
	if (leaf < 0 || s.leafOrder[leaf] < 0) {
		// a rule: the next text leaf
		leaf = s.leaves.back();
		for (int l : s.leaves)
			if (s.doc.blocks[l].sourceEnd > offset) {
				leaf = l;
				break;
			}
	}
	return { leaf, s.doc.blocks[leaf].content.TextOf(offset) };
}

void CMarkdownViewCtrl::GetSelection(size_t& anchorOffset, size_t& caretOffset) const {
	auto& s = *m_State;
	if (!s.HasSelection()) {
		anchorOffset = caretOffset = s.caret.block >= 0 ? SourceOfPos(s.doc, s.caret) : 0;
		return;
	}
	anchorOffset = SourceOfPos(s.doc, s.anchor);
	caretOffset = SourceOfPos(s.doc, s.caret);
}

void CMarkdownViewCtrl::SetSelection(size_t anchorOffset, size_t caretOffset) {
	auto& s = *m_State;
	s.anchor = PosOfSource(s, anchorOffset);
	s.caret = PosOfSource(s, caretOffset);
	SelectionChanged(false);
}

static CString WithCrLf(const std::wstring& text) {
	CString out;
	out.Preallocate((int)(text.size() + text.size() / 16 + 1));
	for (wchar_t ch : text) {
		if (ch == L'\n')
			out += L'\r';
		out += ch;
	}
	return out;
}

CString CMarkdownViewCtrl::GetSelectedText() const {
	auto& s = *m_State;
	if (!s.HasSelection())
		return {};
	return WithCrLf(MdvPlainText(s.doc, SelectionRange(s)));
}

CString CMarkdownViewCtrl::GetSelectedHtml() const {
	auto& s = *m_State;
	if (!s.HasSelection())
		return {};
	return MdvHtml(s.doc, SelectionRange(s)).c_str();
}

CString CMarkdownViewCtrl::GetSelectedMarkdown() const {
	auto& s = *m_State;
	if (!s.HasSelection())
		return {};
	size_t start, end;
	MdvSourceRange(s.doc, SelectionRange(s), start, end);
	return WithCrLf(s.doc.source.substr(start, end - start));
}

BOOL CMarkdownViewCtrl::Copy() {
	auto& s = *m_State;
	if (!s.HasSelection() || !::OpenClipboard(m_hWnd))
		return FALSE;
	::EmptyClipboard();
	const CString text = GetSelectedText();
	const size_t bytes = (text.GetLength() + 1) * sizeof(wchar_t);
	if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		memcpy(::GlobalLock(mem), (LPCWSTR)text, bytes);
		::GlobalUnlock(mem);
		if (!::SetClipboardData(CF_UNICODETEXT, mem))
			::GlobalFree(mem);
	}
	// and as HTML, for pasting into Word and mail
	static const UINT cfHtml = ::RegisterClipboardFormatW(L"HTML Format");
	const std::string html = MdvCfHtml(MdvHtml(s.doc, SelectionRange(s)));
	if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, html.size() + 1)) {
		memcpy(::GlobalLock(mem), html.c_str(), html.size() + 1);
		::GlobalUnlock(mem);
		if (!::SetClipboardData(cfHtml, mem))
			::GlobalFree(mem);
	}
	::CloseClipboard();
	return TRUE;
}

LRESULT CMarkdownViewCtrl::OnCopy(UINT, WPARAM, LPARAM, BOOL&) {
	Copy();
	return 0;
}

LRESULT CMarkdownViewCtrl::OnFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
	// the selection's color follows the focus
	if (m_State->HasSelection())
		Invalidate();
	bHandled = FALSE;
	return 0;
}

// ---- Links and task boxes ---------------------------------------------------------------

bool CMarkdownViewCtrl::LinkAt(POINT pt, int& leaf, int& link, float rc[4]) const {
	auto& s = *m_State;
	size_t pos, hit = 0;
	bool inside = false;
	if (!PositionAt(pt, leaf, pos, &inside, &hit) || !inside)
		return false;
	auto& content = s.doc.blocks[leaf].content;
	if (hit >= content.text.size())
		return false;
	link = content.LinkAt(hit);
	if (link < 0)
		return false;
	// the piece of the link under the point (a link can wrap over lines)
	float tx, ty;
	const MdvText* t = LeafText(leaf, tx, ty);
	if (!t)
		return false;
	auto& l = content.links[link];
	UINT32 count = 0;
	t->layout->HitTestTextRange((UINT32)l.start, (UINT32)l.length, 0, 0, nullptr, 0, &count);
	std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
	if (count == 0 || FAILED(t->layout->HitTestTextRange((UINT32)l.start, (UINT32)l.length, 0, 0, hits.data(), count, &count)))
		return false;
	const float vx = pt.x / s.Scale(), vy = pt.y / s.Scale();
	rc[0] = rc[1] = rc[2] = rc[3] = 0;
	for (auto& h : hits)
		if (vx >= tx + h.left && vx < tx + h.left + h.width && vy >= ty + h.top && vy < ty + h.top + h.height) {
			rc[0] = tx + h.left;
			rc[1] = ty + h.top;
			rc[2] = rc[0] + h.width;
			rc[3] = rc[1] + h.height;
			return true;
		}
	return false;
}

bool CMarkdownViewCtrl::TaskAt(POINT pt, int& item) const {
	auto& s = *m_State;
	if (!(GetStyle() & MDVS_TASKCLICK))
		return false;
	const float vx = pt.x / s.Scale(), y = pt.y / s.Scale() + s.scrollY;
	const size_t i = s.flow.BoxAt(y);
	if (i >= s.flow.boxes.size())
		return false;
	auto& box = s.flow.boxes[i];
	if (box.marker != MdvMarker::Checked && box.marker != MdvMarker::Unchecked)
		return false;
	const float em = s.metrics.fontSize * box.scale;
	float line = s.metrics.lineHeight * em;
	auto it = s.layouts.find(MdvState::Key(box.block, box.kind));
	if (it != s.layouts.end() && it->second.text.firstLine > 0)
		line = it->second.text.firstLine;
	const float cx = ColumnLeft() + box.indent - em, cy = box.top + box.padTop + line / 2;
	if (std::fabs(vx - cx) > 0.6f * em || std::fabs(y - cy) > 0.6f * em)
		return false;
	item = box.markerBlock;
	return true;
}

void CMarkdownViewCtrl::ToggleTask(int item) {
	auto& s = *m_State;
	auto& block = s.doc.blocks[item];
	if (block.task == MdvTask::None || block.taskOffset >= s.doc.source.size())
		return;
	const bool checked = block.task == MdvTask::Unchecked;
	NMMDVTASK nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_TASKTOGGLE };
	nm.sourceOffset = block.taskOffset;
	nm.checked = checked;
	if (::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm))
		return;
	// the text changes in place: same length, nothing moves
	s.doc.source[block.taskOffset] = checked ? L'x' : L' ';
	block.task = checked ? MdvTask::Checked : MdvTask::Unchecked;
	for (auto& box : s.flow.boxes)
		if (box.markerBlock == item)
			box.marker = checked ? MdvMarker::Checked : MdvMarker::Unchecked;
	Invalidate();
}

void CMarkdownViewCtrl::ClickLink(int leaf, int link) {
	auto& s = *m_State;
	auto& content = s.doc.blocks[leaf].content;
	auto& l = content.links[link];
	const std::wstring url = l.url, title = l.title;   // the app may change the text
	NMMDVLINK nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_LINKCLICK };
	nm.pszUrl = url.c_str();
	nm.pszTitle = title.c_str();
	nm.sourceOffset = content.SourceOf(l.start);
	if (::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm))
		return;
	if (MdvClassifyUrl(url) == MdvUrlKind::Anchor)
		ScrollToAnchor(url.c_str());
}

void CMarkdownViewCtrl::UpdateHotLink(POINT pt) {
	auto& s = *m_State;
	int leaf = -1, link = -1;
	float rc[4] = {};
	if (!LinkAt(pt, leaf, link, rc))
		leaf = link = -1;
	if (leaf == s.hotLeaf && link == s.hotLink)
		return;
	s.hotLeaf = leaf;
	s.hotLink = link;
	const MdvLink* l = leaf >= 0 ? &s.doc.blocks[leaf].content.links[link] : nullptr;
	NMMDVLINK nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_LINKHOT };
	nm.pszUrl = l ? l->url.c_str() : nullptr;
	nm.pszTitle = l ? l->title.c_str() : nullptr;
	nm.sourceOffset = l ? s.doc.blocks[leaf].content.SourceOf(l->start) : 0;
	::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);

	// one tooltip tool, moved onto the link
	if (GetStyle() & MDVS_NOLINKTIPS)
		return;
	if (!s.tip && l) {
		s.tip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_hWnd, nullptr, nullptr, nullptr);
		TTTOOLINFOW ti{ sizeof(ti) };
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = m_hWnd;
		ti.uId = 1;
		ti.lpszText = const_cast<LPWSTR>(L"");
		::SendMessageW(s.tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
		::SendMessageW(s.tip, TTM_SETMAXTIPWIDTH, 0, ::MulDiv(600, s.dpi, 96));
		// dark with the view (made now, after the mode was set; UpdateScrollBarTheme keeps it in step)
		if (s.darkScrollBars == 1)
			::SetWindowTheme(s.tip, L"DarkMode_Explorer", nullptr);
	}
	if (!s.tip)
		return;
	TTTOOLINFOW ti{ sizeof(ti) };
	ti.hwnd = m_hWnd;
	ti.uId = 1;
	if (l) {
		const float scale = s.Scale();
		ti.rect = { (LONG)std::floor(rc[0] * scale), (LONG)std::floor(rc[1] * scale), (LONG)std::ceil(rc[2] * scale), (LONG)std::ceil(rc[3] * scale) };
	}
	::SendMessageW(s.tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
	std::wstring text = l ? (l->title.empty() ? l->url : l->title + L"\n" + l->url) : L"";
	ti.lpszText = text.data();
	::SendMessageW(s.tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
	if (!l)
		::SendMessageW(s.tip, TTM_POP, 0, 0);
}

// ---- Finding ------------------------------------------------------------------------------

static unsigned MatchFlags(UINT flags) {
	return (flags & MDVFF_MATCHCASE ? MdvMatchCase : 0) | (flags & MDVFF_WHOLEWORD ? MdvWholeWord : 0);
}

BOOL CMarkdownViewCtrl::Find(LPCWSTR pszText, UINT flags) {
	auto& s = *m_State;
	if (!pszText || !*pszText || s.leaves.empty())
		return FALSE;
	s.findText = pszText;
	s.findFlags = flags;
	const bool up = (flags & MDVFF_UP) != 0;
	MdvTextPos from;
	if (s.HasSelection()) {
		const MdvRange r = SelectionRange(s);
		from = up ? r.first : r.last;
	}
	else if (!PositionAt({ 0, 0 }, from.block, from.pos))
		from = { s.leaves.front(), 0 };
	MdvMatch m;
	if (!MdvFindNext(s.doc, pszText, MatchFlags(flags), from, up, m))
		return FALSE;
	s.anchor = { m.block, m.start };
	s.caret = { m.block, m.start + m.length };
	ShowPosition(m.block, m.start);
	SelectionChanged(false);
	return TRUE;
}

int CMarkdownViewCtrl::HighlightAll(LPCWSTR pszText, UINT flags) {
	auto& s = *m_State;
	s.highlights.clear();
	int count = 0;
	if (pszText && *pszText)
		for (auto& m : MdvFindAll(s.doc, pszText, MatchFlags(flags))) {
			s.highlights[m.block].push_back({ m.start, m.length });
			count++;
		}
	Invalidate();
	return count;
}

// scrolls a place in a text into view: the line about a third down the view, and a code block sideways
void CMarkdownViewCtrl::ShowPosition(int leaf, size_t pos) {
	auto& s = *m_State;
	bool opened = false;
	for (int b = s.doc.blocks[leaf].parent; b > 0; b = s.doc.blocks[b].parent)
		if (s.doc.blocks[b].kind == MdvKind::Details && !s.doc.blocks[b].open) {
			s.doc.blocks[b].open = true;
			opened = true;
		}
	if (opened)
		Relayout();
	int owner = leaf;
	if (s.doc.blocks[leaf].kind == MdvKind::Cell)
		owner = s.doc.blocks[s.doc.blocks[leaf].parent].parent;
	const int box = s.blockBox[owner];
	if (box < 0)
		return;
	const float view = ViewHeight();
	auto& b = s.flow.boxes[box];
	if (b.top < s.scrollY || b.Bottom() > s.scrollY + view)
		ScrollTo(b.top - view / 3);
	float tx, ty;
	const MdvText* t = LeafText(leaf, tx, ty);
	if (!t)
		return;
	float px, py;
	DWRITE_HIT_TEST_METRICS hm{};
	t->layout->HitTestTextPosition((UINT32)pos, FALSE, &px, &py, &hm);
	const float lineTop = ty + hm.top;
	if (lineTop < 0 || lineTop + hm.height > view)
		ScrollTo(s.scrollY + lineTop - view / 3);
	if (b.kind == MdvBoxKind::Code && SidewaysRoom(box) > 0) {
		const float width = ColumnWidth() - b.indent - b.padLeft - b.padRight;
		const float x = hm.left, offset = s.sideways[leaf];
		if (x < offset || x > offset + width - 2 * s.metrics.fontSize)
			ScrollSideways(box, x - width / 3);
	}
}

// ---- Hit testing and the context menu -------------------------------------------------------

BOOL CMarkdownViewCtrl::HitTest(MDVHITTEST& ht) const {
	auto& s = *m_State;
	ht.part = MDVHT_NOWHERE;
	ht.sourceOffset = 0;
	ht.pszUrl = ht.pszTitle = nullptr;
	int item;
	if (TaskAt(ht.pt, item)) {
		ht.part = MDVHT_TASK;
		ht.sourceOffset = s.doc.blocks[item].taskOffset;
		return TRUE;
	}
	const int box = BoxAtPoint(ht.pt);
	if (box >= 0) {
		float b[4];
		const float x = ht.pt.x / s.Scale(), y = ht.pt.y / s.Scale();
		if (CopyButton(box, b) && x >= b[0] && x < b[2] && y >= b[1] && y < b[3]) {
			ht.part = MDVHT_CODECOPY;
			return TRUE;
		}
		if (s.flow.boxes[box].kind == MdvBoxKind::Summary) {
			ht.part = MDVHT_SUMMARY;
			ht.sourceOffset = s.doc.blocks[s.flow.boxes[box].block].sourceStart;
			return TRUE;
		}
	}
	int leaf;
	size_t pos, hit = 0;
	bool inside = false;
	if (!PositionAt(ht.pt, leaf, pos, &inside, &hit) || !inside)
		return FALSE;
	auto& content = s.doc.blocks[leaf].content;
	ht.sourceOffset = content.SourceOf(hit);
	const int link = hit < content.text.size() ? content.LinkAt(hit) : -1;
	const int image = content.ImageAt(hit);
	if (image >= 0) {
		ht.part = MDVHT_IMAGE;
		ht.pszUrl = content.images[image].src.c_str();
		ht.pszTitle = content.images[image].alt.c_str();
		if (link >= 0) {
			// a linked image (a badge): the link
			ht.part = MDVHT_LINK;
			ht.pszUrl = content.links[link].url.c_str();
		}
	}
	else if (link >= 0) {
		ht.part = MDVHT_LINK;
		ht.pszUrl = content.links[link].url.c_str();
		ht.pszTitle = content.links[link].title.c_str();
	}
	else
		ht.part = MDVHT_TEXT;
	return TRUE;
}

LRESULT CMarkdownViewCtrl::OnContextMenu(UINT, WPARAM, LPARAM lParam, BOOL&) {
	auto& s = *m_State;
	POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	POINT client = pt;
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: the selection's end if it shows, else the view's corner
		client = { 16, 16 };
		float tx, ty;
		if (s.HasSelection())
			if (const MdvText* t = LeafText(s.caret.block, tx, ty)) {
				float px, py;
				DWRITE_HIT_TEST_METRICS hm{};
				t->layout->HitTestTextPosition((UINT32)s.caret.pos, FALSE, &px, &py, &hm);
				const LONG cy = (LONG)((ty + hm.top + hm.height) * s.Scale());
				if (cy > 0 && cy < s.client.cy)
					client = { (LONG)((tx + px) * s.Scale()), cy };
			}
		pt = client;
		ClientToScreen(&pt);
	}
	else
		ScreenToClient(&client);
	NMMDVCONTEXTMENU nm{};
	nm.hdr = { m_hWnd, (UINT_PTR)GetDlgCtrlID(), MDVN_CONTEXTMENU };
	nm.pt = pt;
	nm.hit.pt = client;
	HitTest(nm.hit);
	nm.hasSelection = s.HasSelection();
	::SendMessage(GetParent(), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
	return 0;
}
