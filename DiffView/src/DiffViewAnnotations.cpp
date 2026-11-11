#include "DiffView.h"
#include "DiffViewModel.h"

#include <algorithm>
#include <climits>

// Annotations: room under a line for a window of the app's. The model gives the room as rows (DfAnnotationSlot in,
// DfRowKind::Annotation rows out, so scrolling, paging and the overview stay in whole rows); this file keeps the
// annotations, asks the app for windows as annotations come near the view, and puts them in their rows.

static void FillSolid(WTL::CDCHandle dc, const RECT& rc, COLORREF color) {
	dc.SetBkColor(color);
	dc.ExtTextOut(0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
}

CDiffViewCtrl::AnnotationState* CDiffViewCtrl::FindAnnotation(UINT id) {
	auto it = std::find_if(m_Annotations.begin(), m_Annotations.end(), [&](const AnnotationState& a) { return a.info.id == id; });
	return it == m_Annotations.end() ? nullptr : &*it;
}

const CDiffViewCtrl::AnnotationState* CDiffViewCtrl::FindAnnotation(UINT id) const {
	return const_cast<CDiffViewCtrl*>(this)->FindAnnotation(id);
}

int CDiffViewCtrl::AnnotationRows(int heightDips) const {
	int pixels = Scale(std::max(1, heightDips));
	return std::max(1, (pixels + m_RowHeight - 1) / m_RowHeight);
}

std::vector<DfAnnotationSlot> CDiffViewCtrl::AnnotationSlots() const {
	std::vector<DfAnnotationSlot> slots;
	slots.reserve(m_Annotations.size());
	for (auto& a : m_Annotations) {
		DfAnnotationSlot slot;
		slot.id = a.info.id;
		slot.right = a.info.right != FALSE;
		slot.line = a.info.line;
		slot.rows = (uint32_t)AnnotationRows(a.info.heightDips);
		slot.full = (a.info.flags & DFVAF_FULLWIDTH) != 0;
		slots.push_back(slot);
	}
	return slots;
}

// ---- The annotations --------------------------------------------------------------------

size_t CDiffViewCtrl::AddAnnotations(const DFVANNOTATION* annotations, size_t count) {
	size_t added = 0;
	for (size_t i = 0; annotations && i < count; i++) {
		if (annotations[i].id == 0 || FindAnnotation(annotations[i].id))
			continue;
		AnnotationState state;
		state.info = annotations[i];
		state.info.heightDips = std::max(1, state.info.heightDips);
		m_Annotations.push_back(state);
		added++;
	}
	if (added)
		RebuildRows();
	return added;
}

BOOL CDiffViewCtrl::AddAnnotation(const DFVANNOTATION* annotation) {
	return annotation && AddAnnotations(annotation, 1) == 1;
}

BOOL CDiffViewCtrl::RemoveAnnotation(UINT id) {
	auto* a = FindAnnotation(id);
	if (!a)
		return FALSE;
	if (a->hwnd) {
		RECT rc{};
		GetAnnotationRect(id, &rc);
		FireAnnotation(DFVN_ANNOTATIONHIDE, id, a->info.data, rc, a->hwnd);
		a = FindAnnotation(id);   // the app may have changed the list
	}
	if (a)
		m_Annotations.erase(m_Annotations.begin() + (a - m_Annotations.data()));
	RebuildRows();
	return TRUE;
}

void CDiffViewCtrl::ClearAnnotations() {
	if (m_Annotations.empty())
		return;
	std::vector<UINT> ids;
	for (auto& a : m_Annotations) {
		if (a.hwnd)
			ids.push_back(a.info.id);
	}
	for (auto id : ids) {
		if (auto* a = FindAnnotation(id)) {
			RECT rc{};
			GetAnnotationRect(id, &rc);
			HWND hwnd = a->hwnd;
			a->hwnd = nullptr;
			FireAnnotation(DFVN_ANNOTATIONHIDE, id, a->info.data, rc, hwnd);
		}
	}
	m_Annotations.clear();
	RebuildRows();
}

BOOL CDiffViewCtrl::SetAnnotationHeight(UINT id, int heightDips) {
	auto* a = FindAnnotation(id);
	if (!a)
		return FALSE;
	int before = AnnotationRows(a->info.heightDips);
	a->info.heightDips = std::max(1, heightDips);
	if (AnnotationRows(a->info.heightDips) != before)
		RebuildRows();   // keeps the line at the top of the view; the windows are placed again
	return TRUE;
}

BOOL CDiffViewCtrl::GetAnnotation(UINT id, DFVANNOTATION* pAnnotation) const {
	auto* a = FindAnnotation(id);
	if (!a || !pAnnotation)
		return FALSE;
	*pAnnotation = a->info;
	return TRUE;
}

size_t CDiffViewCtrl::GetAnnotationCount() const {
	return m_Annotations.size();
}

BOOL CDiffViewCtrl::GetAnnotationRect(UINT id, RECT* prc) const {
	auto it = m_Doc->annotationRows.find(id);
	if (!prc || it == m_Doc->annotationRows.end())
		return FALSE;
	const size_t first = it->second;
	const auto& row = m_Doc->rows[first];
	auto rc = GetRowsRect();
	long long y = rc.top + ((long long)first - (long long)m_TopRow) * m_RowHeight;
	y = std::clamp(y, -(long long)INT_MAX / 2, (long long)INT_MAX / 2);
	int height = (int)std::min<long long>((long long)row.slots * m_RowHeight, INT_MAX / 4);

	auto p0 = GetPane(0), p1 = GetPane(m_Layout == DFVL_UNIFIED ? 0 : 1);
	int x0 = p0.text, x1 = p0.right;
	if (m_Layout == DFVL_SIDEBYSIDE) {
		if (row.pane == 1)
			x0 = p1.text, x1 = p1.right;
		else if (row.pane == 2)
			x1 = p1.right;
	}
	// a line above and below, which the rows' paint draws
	*prc = { x0, (int)y + 1, std::max(x0, x1), (int)y + std::max(1, height - 1) };
	return TRUE;
}

BOOL CDiffViewCtrl::EnsureAnnotationVisible(UINT id) {
	auto it = m_Doc->annotationRows.find(id);
	if (it == m_Doc->annotationRows.end())
		return FALSE;
	size_t first = it->second, last = first + m_Doc->rows[first].slots, page = PageRows();
	if (last - first >= page || first < m_TopRow)
		ScrollTo(first, m_Left);
	else if (last > m_TopRow + page)
		ScrollTo(last - page, m_Left);
	return TRUE;
}

// ---- The windows ------------------------------------------------------------------------

HWND CDiffViewCtrl::FireAnnotation(UINT code, UINT id, LPARAM data, const RECT& rc, HWND hwnd) {
	NMDFVANNOTATION nm{};
	nm.id = id;
	nm.data = data;
	nm.rc = rc;
	nm.hwnd = hwnd;
	FireNotify(&nm.hdr, code);
	return nm.hwnd;
}

// Windows exist for the annotations within a page of the view (so a scroll step finds them made), asked for when one
// comes near and given back when it goes far. What the app answers may add, remove or resize annotations: the
// list is looked at again after every notification, and once an answer changed the rows, the pass starts over.
void CDiffViewCtrl::UpdateAnnotations() {
	if (m_Annotations.empty() || !IsWindow())
		return;
	if (m_UpdatingAnnotations) {
		m_AnnotationsAgain = true;
		return;
	}
	m_UpdatingAnnotations = true;
	do {
		m_AnnotationsAgain = false;
		const size_t page = PageRows();
		const size_t from = m_TopRow > page ? m_TopRow - page : 0, to = m_TopRow + 2 * page;
		std::vector<UINT> ids;
		ids.reserve(m_Annotations.size());
		for (auto& a : m_Annotations)
			ids.push_back(a.info.id);
		for (auto id : ids) {
			auto* a = FindAnnotation(id);
			if (!a)
				continue;
			bool inReach = false;
			auto it = m_Doc->annotationRows.find(id);
			if (it != m_Doc->annotationRows.end()) {
				size_t first = it->second, last = first + m_Doc->rows[first].slots;
				inReach = first < to && last > from;
			}
			if (inReach && !a->hwnd) {
				RECT rc{};
				GetAnnotationRect(id, &rc);
				HWND hwnd = FireAnnotation(DFVN_ANNOTATIONSHOW, id, a->info.data, rc, nullptr);
				a = FindAnnotation(id);
				if (a && hwnd && ::IsWindow(hwnd) && ::GetParent(hwnd) == m_hWnd)
					a->hwnd = hwnd;
			}
			else if (!inReach && a->hwnd) {
				RECT rc{};
				GetAnnotationRect(id, &rc);
				HWND hwnd = a->hwnd;
				a->hwnd = nullptr;
				FireAnnotation(DFVN_ANNOTATIONHIDE, id, a->info.data, rc, hwnd);
			}
			if (m_AnnotationsAgain)
				break;
		}
	} while (m_AnnotationsAgain);
	m_UpdatingAnnotations = false;
	PlaceAnnotationWindows();
}

void CDiffViewCtrl::PlaceAnnotationWindows() {
	const auto view = GetRowsRect();
	struct Placed {
		AnnotationState* a;
		RECT rc;
		RECT visible;
	};
	std::vector<Placed> placed;
	for (auto& a : m_Annotations) {
		if (!a.hwnd)
			continue;
		if (!::IsWindow(a.hwnd)) {
			a.hwnd = nullptr;   // the app destroyed it
			continue;
		}
		Placed p{ &a };
		if (!GetAnnotationRect(a.info.id, &p.rc))
			continue;
		::IntersectRect(&p.visible, &p.rc, &view);
		placed.push_back(p);
	}
	if (placed.empty())
		return;

	HDWP dwp = ::BeginDeferWindowPos((int)placed.size());
	for (auto& p : placed) {
		bool visible = !::IsRectEmpty(&p.visible);
		UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
		HDWP next = dwp ? ::DeferWindowPos(dwp, p.a->hwnd, nullptr, p.rc.left, p.rc.top, p.rc.right - p.rc.left, p.rc.bottom - p.rc.top, flags) : nullptr;
		if (!next) {
			dwp = nullptr;   // the handle is gone with a failure: place the rest one by one
			::SetWindowPos(p.a->hwnd, nullptr, p.rc.left, p.rc.top, p.rc.right - p.rc.left, p.rc.bottom - p.rc.top, flags);
		}
		else {
			dwp = next;
		}
	}
	if (dwp)
		::EndDeferWindowPos(dwp);

	// the part under the titles or past the rows is cut off with the window's region
	for (auto& p : placed) {
		bool cut = !::IsRectEmpty(&p.visible) && !::EqualRect(&p.visible, &p.rc);
		if (cut) {
			HRGN rgn = ::CreateRectRgn(p.visible.left - p.rc.left, p.visible.top - p.rc.top, p.visible.right - p.rc.left, p.visible.bottom - p.rc.top);
			if (::SetWindowRgn(p.a->hwnd, rgn, TRUE))
				p.a->clipped = true;
			else
				::DeleteObject(rgn);
		}
		else if (p.a->clipped) {
			::SetWindowRgn(p.a->hwnd, nullptr, TRUE);
			p.a->clipped = false;
		}
	}
}

LRESULT CDiffViewCtrl::OnToParent(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	HWND parent = GetParent();
	if (!parent) {
		bHandled = FALSE;
		return 0;
	}
	return ::SendMessage(parent, uMsg, wParam, lParam);
}

// ---- Painting ---------------------------------------------------------------------------------

// The row of an annotation's room: the gutters, the pane the room is in (a line above its first row and below its
// last, around the window), and in a pane it isn't in the hatching of a filler.
void CDiffViewCtrl::PaintAnnotationRow(WTL::CDCHandle dc, const DfRow& row, int y, const DFVCOLORS& c) {
	const int bottom = y + m_RowHeight;
	const bool unified = m_Layout == DFVL_UNIFIED;
	const int panes = unified ? 1 : 2;
	for (int i = 0; i < panes; i++) {
		auto pane = GetPane(i);
		FillSolid(dc, { pane.left, y, pane.text, bottom }, c.crGutterBack);
		if (!unified && row.pane != 2 && row.pane != i) {
			RECT r{ pane.text, y, pane.right, bottom };
			FillHatch(dc, r, c.crBack, c.crFiller);
		}
	}
	auto p0 = GetPane(0), p1 = GetPane(unified ? 0 : 1);
	int x0 = p0.text, x1 = p0.right;
	if (!unified) {
		if (row.pane == 1)
			x0 = p1.text, x1 = p1.right;
		else if (row.pane == 2)
			x1 = p1.right;
	}
	FillSolid(dc, { x0, y, x1, bottom }, c.crBack);
	auto mix = [](COLORREF a, COLORREF b, int percentB) {
		auto part = [=](int x, int z) { return (BYTE)((x * (100 - percentB) + z * percentB) / 100); };
		return RGB(part(GetRValue(a), GetRValue(b)), part(GetGValue(a), GetGValue(b)), part(GetBValue(a), GetBValue(b)));
	};
	COLORREF line = mix(c.crBack, c.crText, 20);
	if (row.slot == 0)
		FillSolid(dc, { x0, y, x1, y + 1 }, line);
	if (row.slot + 1 == row.slots)
		FillSolid(dc, { x0, bottom - 1, x1, bottom }, line);
}
