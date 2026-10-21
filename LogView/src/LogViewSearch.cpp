// The log view's search: the matches it highlights and finding the next one, in slices on a timer.

#include "LogView.h"
#include "LogViewModel.h"
#include "LogViewText.h"

#include <algorithm>

constexpr UINT_PTR SearchTimer = 2;   // ScanTimer is 1

BOOL CLogViewCtrl::SetSearch(const LGVSEARCH* search) {
	LgMatcher next;
	if (search && search->pszText &&
		!next.Set(search->pszText, (search->flags & LGVFF_REGEX) != 0, (search->flags & LGVFF_MATCHCASE) != 0, (search->flags & LGVFF_WHOLEWORD) != 0))
		return FALSE;
	CancelSearch();   // it was looking for the old text
	*m_Search = std::move(next);
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	return TRUE;
}

BOOL CLogViewCtrl::GetSearch(LGVSEARCH* search) const {
	if (!search)
		return FALSE;
	search->pszText = m_Search->Text().c_str();
	search->flags = (m_Search->IsRegex() ? LGVFF_REGEX : 0) | (m_Search->MatchCase() ? LGVFF_MATCHCASE : 0) |
		(m_Search->WholeWord() ? LGVFF_WHOLEWORD : 0);
	return TRUE;
}

BOOL CLogViewCtrl::FindNext(bool forward) {
	if (m_Search->Empty())
		return FALSE;
	StartFind(forward, false);
	return TRUE;
}

bool CLogViewCtrl::IsSearching() const {
	return m_SearchState.active;
}

void CLogViewCtrl::CancelSearch() {
	m_SearchState.active = false;
	if (IsWindow())
		KillTimer(SearchTimer);
}

void CLogViewCtrl::StartFind(bool forward, bool byUser) {
	CancelSearch();
	auto& st = m_SearchState;
	st = {};
	st.active = true;
	st.forward = forward;
	st.byUser = byUser;
	if (RowCount() == 0) {
		EndSearch(false, LGV_NOLINE, 0, 0);
		return;
	}
	// from the end of the selection, or its start going back
	LGVPOS start, end;
	GetSelection(&start, &end);
	auto from = ClampPos(forward ? end : start);
	st.startSerial = st.serial = FirstSerial() + from.line;
	st.startColumn = from.column;
	st.remaining = RowCount() + 1;   // the start line twice: after the column, then (around) before it
	if (!SearchStep(30))
		SetTimer(SearchTimer, USER_TIMER_MINIMUM);
}

bool CLogViewCtrl::SearchStep(DWORD budgetMs) {
	auto& st = m_SearchState;
	size_t rows = RowCount();
	if (rows == 0) {
		EndSearch(false, LGV_NOLINE, 0, 0);
		return true;
	}
	// the row of the line to look at: rows may have come and gone since the last slice
	ULONGLONG first = FirstSerial();
	size_t row = IsFiltered() ? m_Rows->RowOf(st.serial) : (size_t)(std::max(st.serial, first) - first);
	if (row >= rows) {
		row = st.forward ? 0 : rows - 1;
		st.wrapped = st.wrapped || st.forward;
	}

	LARGE_INTEGER freq, start, now;
	::QueryPerformanceFrequency(&freq);
	::QueryPerformanceCounter(&start);
	LONGLONG budget = freq.QuadPart * budgetMs / 1000;
	std::vector<LgMatcher::Span> spans;
	LineData data;
	for (int n = 1; st.remaining > 0; n++) {
		size_t line = LineOfRow(row);
		GetLine(line, data);
		m_Search->FindAll({ data.text, data.length }, spans);
		// on the start line only the matches past the column count, and when back there those before it
		size_t lo = 0, hi = SIZE_MAX;
		if (first + line == st.startSerial) {
			if (st.forward == st.firstVisit)
				lo = st.startColumn;
			else
				hi = st.startColumn;
		}
		int pick = LgPickMatch(spans, st.forward, lo, hi);
		if (pick >= 0) {
			EndSearch(true, line, spans[pick].start, spans[pick].start + spans[pick].length);
			return true;
		}
		st.firstVisit = false;
		st.remaining--;
		if (st.forward) {
			if (++row >= rows) {
				row = 0;
				st.wrapped = true;
			}
		}
		else if (row == 0) {
			row = rows - 1;
			st.wrapped = true;
		}
		else {
			row--;
		}
		if ((n & 63) == 0) {
			::QueryPerformanceCounter(&now);
			if (now.QuadPart - start.QuadPart > budget) {
				st.serial = first + LineOfRow(row);
				return false;
			}
		}
	}
	EndSearch(false, LGV_NOLINE, 0, 0);
	return true;
}

void CLogViewCtrl::EndSearch(bool found, size_t line, size_t start, size_t end) {
	auto st = m_SearchState;
	CancelSearch();
	if (found) {
		Select({ line, start }, { line, end }, st.byUser);
		// far from the view: the match in its middle
		size_t row = RowOfLine(line), page = GetPageLines();
		if (row < m_TopRow || row >= m_TopRow + page)
			ScrollTo(row > page / 2 ? row - page / 2 : 0, m_Left, st.byUser);
		EnsureCaretVisible(st.byUser);
	}
	NMLGVSEARCH nm{};
	nm.found = found;
	nm.wrapped = found && st.wrapped;
	nm.line = found ? line : LGV_NOLINE;
	FireNotify(&nm.hdr, LGVN_SEARCHDONE);
}
