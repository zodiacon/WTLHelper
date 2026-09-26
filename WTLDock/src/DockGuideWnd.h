#pragma once

#include "DockDrop.h"

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlgdi.h>

namespace WTLDock {

// One of the translucent windows shown while dragging: a drop target marker, the preview of where the pane would
// go, or the ghost outline of a floating window. Transparent to the mouse; owned by the drag session.
class CDockGuideWnd : public ATL::CWindowImpl<CDockGuideWnd, ATL::CWindow,
	ATL::CWinTraits<WS_POPUP, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT>> {
public:
	DECLARE_WND_CLASS_EX(L"WTLDock_Guide", 0, 0)

	enum class Look { Marker, Preview, Ghost };

	CDockGuideWnd(const DockTheme& theme) : m_Theme(theme) {
	}

	// what the window shows (creates the window on first use)
	void Configure(HWND owner, Look look, const DropTarget& target);
	// puts the window at a rectangle (screen coordinates), on top; a hot marker is highlighted
	// (returns whether it moved; 'raise' brings an unmoved window above the others)
	bool Place(const RECT& rect, bool hot, bool raise = false);
	void Hide();
	bool IsShown() const {
		return m_Shown;
	}
	Look GetLook() const {
		return m_Look;
	}

	BEGIN_MSG_MAP(CDockGuideWnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_PRINTCLIENT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
	END_MSG_MAP()

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnNcHitTest(UINT, WPARAM, LPARAM, BOOL&) {
		return HTTRANSPARENT;
	}

	void Draw(HDC hdc);

	const DockTheme& m_Theme;
	Look m_Look{ Look::Marker };
	DropTarget m_Target;
	bool m_Hot{};
	bool m_Shown{};
	RECT m_Rect{};
};

}
