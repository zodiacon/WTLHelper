#pragma once

#include "DockLayout.h"
#include "DockTheme.h"

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>

#include <optional>

namespace WTLDock {

// Painting and dragging of the splitters of one layout tree; shared by the host (main tree) and the floating frames.
class SplitterTracker {
public:
	bool Dragging() const {
		return m_Split != nullptr;
	}

	// starts a drag if the point is on a splitter; captures the mouse for 'window'
	bool Begin(HWND window, DockSplit& root, POINT pt) {
		for (auto& s : DockLayout::Splitters(root)) {
			if (PtInRect(&s.Rect, pt)) {
				m_Split = s.Split;
				m_Index = s.Index;
				m_Offset = { pt.x - (s.Rect.left + s.Rect.right) / 2, pt.y - (s.Rect.top + s.Rect.bottom) / 2 };
				::SetCapture(window);
				::InvalidateRect(window, nullptr, FALSE);
				return true;
			}
		}
		return false;
	}

	void Move(DockLayout& layout, DockSplit& root, POINT pt) {
		if (!m_Split)
			return;
		for (auto& s : DockLayout::Splitters(root)) {
			if (s.Split != m_Split || s.Index != m_Index)
				continue;
			const bool horizontal = m_Split->GetAxis() == Axis::Horizontal;
			const int center = horizontal ? (s.Rect.left + s.Rect.right) / 2 : (s.Rect.top + s.Rect.bottom) / 2;
			const int desired = horizontal ? pt.x - m_Offset.x : pt.y - m_Offset.y;
			if (desired != center)
				layout.ResizeSplitter(m_Split, m_Index, desired - center);
			return;
		}
	}

	// ends the drag (the caller releases the capture if it still has it)
	bool End(HWND window) {
		if (!m_Split)
			return false;
		m_Split = nullptr;
		::InvalidateRect(window, nullptr, FALSE);
		return true;
	}

	// the axis of the splitter under the point (or being dragged)
	std::optional<Axis> AxisAt(DockSplit& root, POINT pt) const {
		if (m_Split)
			return m_Split->GetAxis();
		for (auto& s : DockLayout::Splitters(root))
			if (PtInRect(&s.Rect, pt))
				return s.Split->GetAxis();
		return {};
	}

	void Paint(CDCHandle dc, DockSplit& root, const DockTheme& theme) const {
		for (auto& s : DockLayout::Splitters(root)) {
			const bool dragging = m_Split && s.Split == m_Split && s.Index == m_Index;
			dc.FillSolidRect(&s.Rect, dragging ? theme.SplitterDragging : theme.Splitter);
		}
	}

	static bool SetCursorFor(std::optional<Axis> axis) {
		if (!axis)
			return false;
		::SetCursor(::LoadCursor(nullptr, *axis == Axis::Horizontal ? IDC_SIZEWE : IDC_SIZENS));
		return true;
	}

private:
	DockSplit* m_Split{};
	int m_Index{};
	POINT m_Offset{};
};

}
