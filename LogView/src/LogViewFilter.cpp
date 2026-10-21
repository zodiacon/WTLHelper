// The log view's filter (the rows it shows, found in slices on a timer) and its rules.

#include "LogView.h"
#include "LogViewModel.h"
#include "LogViewText.h"

#include <algorithm>

constexpr UINT_PTR ScanTimer = 1;
constexpr UINT_PTR SearchTimer = 2;

// ---- Rows ------------------------------------------------------------------------------

ULONGLONG CLogViewCtrl::FirstSerial() const {
	return m_Virtual ? 0 : m_Store->FirstSerial();
}

bool CLogViewCtrl::IsFiltered() const {
	return m_Filter->Active();
}

size_t CLogViewCtrl::RowCount() const {
	return IsFiltered() ? m_Rows->Count() : GetLineCount();
}

size_t CLogViewCtrl::LineOfRow(size_t row) const {
	return IsFiltered() ? (size_t)(m_Rows->Serial(row) - FirstSerial()) : row;
}

size_t CLogViewCtrl::RowOfLine(size_t line) const {
	return IsFiltered() ? m_Rows->RowOf(FirstSerial() + line) : std::min(line, GetLineCount());
}

bool CLogViewCtrl::IsLineShown(size_t line) const {
	return line < GetLineCount() && (!IsFiltered() || m_Rows->Contains(FirstSerial() + line));
}

size_t CLogViewCtrl::GetShownLineCount() const {
	return RowCount();
}

// ---- The filter ----------------------------------------------------------------------------

BOOL CLogViewCtrl::SetFilter(const LGVFILTER* filter) {
	LgFilter next;
	if (filter) {
		LPCWSTR text = filter->pszText ? filter->pszText : L"";
		if (!next.matcher.Set(text, (filter->flags & LGVFF_REGEX) != 0, (filter->flags & LGVFF_MATCHCASE) != 0, (filter->flags & LGVFF_WHOLEWORD) != 0))
			return FALSE;
		next.exclude = (filter->flags & LGVFF_EXCLUDE) != 0;
		next.levelMask = filter->levelMask;
	}
	CancelSearch();   // its place among the rows is gone
	*m_Filter = std::move(next);
	m_Rows->Clear();
	m_ScanSerial = FirstSerial();
	m_TopRow = 0;   // following moves it to the end
	m_FilterDonePending = true;
	m_CaretAfterFilter = true;
	if (IsWindow())
		KillTimer(ScanTimer);
	if (!IsFiltered() || ScanStep(30))
		FilterDone();
	else
		StartScan();
	if (m_Text->IsWindow()) {
		UpdateScrollBars();
		m_Text->Invalidate();
	}
	return TRUE;
}

BOOL CLogViewCtrl::GetFilter(LGVFILTER* filter) const {
	if (!filter)
		return FALSE;
	auto& m = m_Filter->matcher;
	filter->pszText = m.Text().c_str();
	filter->flags = (m.IsRegex() ? LGVFF_REGEX : 0) | (m.MatchCase() ? LGVFF_MATCHCASE : 0) | (m.WholeWord() ? LGVFF_WHOLEWORD : 0) |
		(m_Filter->exclude ? LGVFF_EXCLUDE : 0);
	filter->levelMask = m_Filter->levelMask;
	return TRUE;
}

bool CLogViewCtrl::IsFilterPending() const {
	return IsFiltered() && m_ScanSerial < FirstSerial() + GetLineCount();
}

bool CLogViewCtrl::ScanStep(DWORD budgetMs) {
	ULONGLONG first = FirstSerial(), end = first + GetLineCount();
	m_ScanSerial = std::max(m_ScanSerial, first);   // lines dropped before they were looked at
	if (!IsFiltered()) {
		m_ScanSerial = end;
		return true;
	}
	LARGE_INTEGER freq, start, now;
	::QueryPerformanceFrequency(&freq);
	::QueryPerformanceCounter(&start);
	LONGLONG budget = freq.QuadPart * budgetMs / 1000;
	size_t before = m_Rows->Count();
	LineData data;
	for (int n = 1; m_ScanSerial < end; n++) {
		GetLine((size_t)(m_ScanSerial - first), data);
		if (m_Filter->Passes({ data.text, data.length }, data.level))
			m_Rows->Add(m_ScanSerial);
		m_ScanSerial++;
		if ((n & 255) == 0) {
			::QueryPerformanceCounter(&now);
			if (now.QuadPart - start.QuadPart > budget)
				break;
		}
	}
	if (m_Rows->Count() != before && m_Text->IsWindow()) {
		UpdateScrollBars();
		m_Text->Invalidate();
	}
	return m_ScanSerial >= end;
}

void CLogViewCtrl::StartScan() {
	// a timer, not posted messages: those would come before input and painting, and starve them
	if (IsWindow() && IsFilterPending())
		SetTimer(ScanTimer, USER_TIMER_MINIMUM);
}

LRESULT CLogViewCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam == SearchTimer) {
		if (!m_SearchState.active)
			KillTimer(SearchTimer);
		else
			SearchStep(25);   // ends the search (and its timer) when done
		return 0;
	}
	if (wParam != ScanTimer) {
		bHandled = FALSE;
		return 0;
	}
	if (ScanStep(25)) {
		KillTimer(ScanTimer);
		if (m_FilterDonePending)
			FilterDone();
	}
	return 0;
}

void CLogViewCtrl::FilterDone() {
	m_FilterDonePending = false;
	// the caret and anchor go to lines that are shown
	Select(m_Anchor, m_Caret, false, false);
	if (m_CaretAfterFilter && !m_Follow && RowCount() > 0 && m_Text->IsWindow()) {
		// the caret's line in the middle of the view
		size_t row = RowOfLine(m_Caret.line);
		size_t half = GetPageLines() / 2;
		ScrollTo(row > half ? row - half : 0, m_Left, false);
		EnsureCaretVisible(false);
	}
	m_CaretAfterFilter = false;
	NMHDR hdr{};
	FireNotify(&hdr, LGVN_FILTERDONE);
}

// ---- Rules ------------------------------------------------------------------------------------

int CLogViewCtrl::AddRule(const LGVRULE* rule) {
	if (!rule || !rule->pszText || !*rule->pszText)
		return -1;
	LgRule r;
	if (!r.matcher.Set(rule->pszText, (rule->flags & LGVFF_REGEX) != 0, (rule->flags & LGVFF_MATCHCASE) != 0, (rule->flags & LGVFF_WHOLEWORD) != 0))
		return -1;
	r.style = rule->style;
	r.matchOnly = (rule->flags & LGVRF_MATCHONLY) != 0;
	m_Rules.push_back(std::move(r));
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	return (int)m_Rules.size() - 1;
}

BOOL CLogViewCtrl::RemoveRule(int index) {
	if (index < 0 || index >= (int)m_Rules.size())
		return FALSE;
	m_Rules.erase(m_Rules.begin() + index);
	if (m_Text->IsWindow())
		m_Text->Invalidate();
	return TRUE;
}

void CLogViewCtrl::ClearRules() {
	m_Rules.clear();
	if (m_Text->IsWindow())
		m_Text->Invalidate();
}

int CLogViewCtrl::GetRuleCount() const {
	return (int)m_Rules.size();
}
