//
// per-monitor DPI tests for WTLHelper: creates the controls, moves their windows between two monitors of different DPI
// (a real WM_DPICHANGED) and checks that their sizes follow the DPI.
// Most tests need two monitors with different scale factors; without them those are skipped.
// The test windows show briefly on those monitors.
// Usage: DpiTest [folder]  - with a folder, screenshots of the windows at both DPIs are saved there as PNG files
//

#include <atlbase.h>
#include <atlapp.h>
#include <atlstr.h>
#include <atltypes.h>

CAppModule _Module;

#include <atlwin.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <atlimage.h>
#include <shellscalingapi.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>

#include "WTLHelper.h"
#include "DpiHelper.h"
#include "NativeCustomTabView.h"
#include "ResizablePropertySheet.h"
#include "IconPropertySheet.h"
#include "DialogHelper.h"
#include "QuickFindEdit.h"
#include "TreeListView.h"
#include "ToolbarHelper.h"
#include "VirtualListView.h"
#include "Settings.h"
#include <atltheme.h>
#include "SimpleHexControl.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "resource.h"

static int g_Failures;
static std::wstring g_OutDir;

static void Check(bool ok, char const* what) {
	printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		g_Failures++;
}

// value at dpi b should be value at dpi a scaled, within tolerance
static void CheckScaled(char const* what, int a, UINT dpiA, int b, UINT dpiB, int tolerance = 2) {
	int expected = ::MulDiv(a, dpiB, dpiA);
	char buf[256];
	sprintf_s(buf, "%s: %d @%u -> %d @%u (expected ~%d)", what, a, dpiA, b, dpiB, expected);
	Check(abs(b - expected) <= tolerance, buf);
}

static void Pump() {
	MSG msg;
	for (int i = 0; i < 20; i++) {
		while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		::Sleep(15);
	}
}

static void Snapshot(HWND hWnd, std::wstring const& name) {
	if (g_OutDir.empty())
		return;
	CRect rc;
	::GetWindowRect(hWnd, &rc);
	CImage image;
	image.Create(rc.Width(), rc.Height(), 32);
	auto dc = image.GetDC();
	::PrintWindow(hWnd, dc, PW_RENDERFULLCONTENT);
	image.ReleaseDC();
	image.Save((g_OutDir + name + L".png").c_str(), Gdiplus::ImageFormatPNG);
}

struct Monitor {
	CRect Work;
	UINT Dpi;
};

static std::vector<Monitor> GetMonitors() {
	std::vector<Monitor> monitors;
	::EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM p) -> BOOL {
		MONITORINFO mi{ sizeof(mi) };
		::GetMonitorInfo(hMon, &mi);
		UINT x, y;
		::GetDpiForMonitor(hMon, MDT_EFFECTIVE_DPI, &x, &y);
		reinterpret_cast<std::vector<Monitor>*>(p)->push_back({ mi.rcWork, x });
		return TRUE;
		}, (LPARAM)&monitors);
	return monitors;
}

// moves a top-level window to the top-left of a monitor's work area
static void MoveTo(HWND hWnd, Monitor const& m) {
	::SetWindowPos(hWnd, nullptr, m.Work.left + 40, m.Work.top + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	Pump();
}

//
// a frame that follows the suggested rectangle on DPI changes, with a child created by a callback
//
class CTestFrame : public CWindowImpl<CTestFrame, CWindow, CFrameWinTraits> {
public:
	DECLARE_WND_CLASS(L"DpiTestFrame")

	BEGIN_MSG_MAP(CTestFrame)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
	END_MSG_MAP()

	HWND m_hChild{};

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		if (m_hChild) {
			CRect rc;
			GetClientRect(&rc);
			::SetWindowPos(m_hChild, nullptr, 0, 0, rc.Width(), rc.Height(), SWP_NOZORDER);
		}
		return 0;
	}

	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM lp, BOOL&) {
		auto rc = (RECT*)lp;
		SetWindowPos(nullptr, rc, SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}
};

class CTestTabView : public CNativeCustomTabViewImpl<CTestTabView> {
public:
	DECLARE_WND_CLASS_EX(L"DpiTest_TabView", 0, COLOR_APPWORKSPACE)
};

static int FontHeight(HFONT hFont) {
	LOGFONT lf{};
	::GetObject(hFont, sizeof(lf), &lf);
	return abs(lf.lfHeight);
}

struct TabMetrics {
	UINT Dpi;
	int FontHeight, TabHeight, CloseCx, CloseCy, TabCtrlHeight;
};

static TabMetrics MeasureTabs(CTestTabView& view) {
	TabMetrics m{};
	m.Dpi = DpiHelper::GetDpi(view);
	m.FontHeight = FontHeight(view.m_tab.GetFont());
	m.TabHeight = view.m_cyTabHeight;
	RECT rc;
	view.CalcCloseButtonRect(0, rc);
	m.CloseCx = rc.right - rc.left;
	m.CloseCy = rc.bottom - rc.top;
	CRect rcTab;
	view.m_tab.GetWindowRect(&rcTab);
	m.TabCtrlHeight = rcTab.Height();
	printf("  tabs @%u: font %d, tab height %d, tab control height %d, close button %dx%d\n", m.Dpi, m.FontHeight, m.TabHeight, m.TabCtrlHeight, m.CloseCx, m.CloseCy);
	return m;
}

static void TestTabView(Monitor const& a, Monitor const& b) {
	printf("Tab view\n");
	CTestFrame frame;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 640, a.Work.top + 440); frame.Create(nullptr, rcCreate, L"Tab view DPI test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	CTestTabView view;
	view.Create(frame, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	frame.m_hChild = view;
	for (auto title : { L"First page", L"Second page", L"Third" }) {
		CEdit edit;
		edit.Create(view, CWindow::rcDefault, title, WS_CHILD | ES_MULTILINE);
		edit.SetFont(AtlGetDefaultGuiFont());
		view.AddPage(edit, title);
	}
	frame.SendMessage(WM_SIZE);
	Pump();

	auto ma = MeasureTabs(view);
	CRect itemA;
	view.m_tab.GetItemRect(0, &itemA);
	Check(ma.Dpi == a.Dpi, "view has the DPI of monitor A");
	CheckScaled("close button width vs 96 DPI", 14, 96, ma.CloseCx, ma.Dpi, 1);
	Snapshot(frame, L"tabs_A");

	MoveTo(frame, b);
	auto mb = MeasureTabs(view);
	Check(mb.Dpi == b.Dpi, "view has the DPI of monitor B after the move");
	CheckScaled("font height", ma.FontHeight, ma.Dpi, mb.FontHeight, mb.Dpi);
	CheckScaled("tab height", ma.TabHeight, ma.Dpi, mb.TabHeight, mb.Dpi, 3);
	CheckScaled("close button width", ma.CloseCx, ma.Dpi, mb.CloseCx, mb.Dpi, 1);
	Check(mb.TabCtrlHeight == mb.TabHeight, "tab control laid out with the new height");
	Snapshot(frame, L"tabs_B");

	MoveTo(frame, a);
	auto ma2 = MeasureTabs(view);
	Check(ma2.FontHeight == ma.FontHeight && ma2.TabHeight == ma.TabHeight && ma2.CloseCx == ma.CloseCx, "back on A: same metrics as before");
	// the tab control keeps metrics of the old DPI unless it reloads its theme (the tabs were 2 pixels taller)
	CRect itemA2;
	view.m_tab.GetItemRect(0, &itemA2);
	Check(itemA2.Height() == itemA.Height(), "back on A: tab item height as before");
	frame.DestroyWindow();
	Pump();
}

class CPage : public CPropertyPageImpl<CPage> {
public:
	enum { IDD = IDD_PAGE };
	CPage(PCWSTR title) : CPropertyPageImpl<CPage>(title) {
		m_psp.dwFlags |= PSP_USEICONID;
		m_psp.pszIcon = MAKEINTRESOURCE(IDI_TEST);
	}
	BEGIN_MSG_MAP(CPage)
		CHAIN_MSG_MAP(CPropertyPageImpl<CPage>)
	END_MSG_MAP()
};

class CSheet : public CIconPropertySheetImpl<CSheet, CResizablePropertySheetImpl<CSheet>> {
public:
	CSheet() : CIconPropertySheetImpl(L"Sheet DPI test") {
		m_psh.dwFlags |= PSH_MODELESS | PSH_NOAPPLYNOW;
	}
};

struct SheetMetrics {
	UINT Dpi;
	int OkBottomGap, OkHeight, OkWidth, TabLeft, TabRightGap, TabBottomGap, GripCx, IconCx, ClientCx, ClientCy;
	CSize MinSize;
};

static SheetMetrics MeasureSheet(CSheet& sheet) {
	SheetMetrics m{};
	m.Dpi = DpiHelper::GetDpi(sheet);
	CRect client;
	sheet.GetClientRect(&client);
	m.ClientCx = client.Width();
	m.ClientCy = client.Height();
	CRect ok;
	::GetWindowRect(sheet.GetDlgItem(IDOK), &ok);
	sheet.ScreenToClient(&ok);
	m.OkBottomGap = client.bottom - ok.bottom;
	m.OkHeight = ok.Height();
	m.OkWidth = ok.Width();
	CRect tab;
	::GetWindowRect(sheet.GetTabControl(), &tab);
	sheet.ScreenToClient(&tab);
	m.TabLeft = tab.left;
	m.TabRightGap = client.right - tab.right;
	m.TabBottomGap = client.bottom - tab.bottom;
	for (CWindow child = sheet.GetWindow(GW_CHILD); child; child = child.GetWindow(GW_HWNDNEXT)) {
		WCHAR cls[32];
		::GetClassName(child, cls, _countof(cls));
		if (_wcsicmp(cls, L"ScrollBar") == 0) {
			CRect rc;
			child.GetWindowRect(&rc);
			m.GripCx = rc.Width();
		}
	}
	int cx = 0, cy = 0;
	ImageList_GetIconSize(CTabCtrl(sheet.GetTabControl()).GetImageList(), &cx, &cy);
	m.IconCx = cx;
	MINMAXINFO mmi{};
	sheet.SendMessage(WM_GETMINMAXINFO, 0, (LPARAM)&mmi);
	m.MinSize = CSize(mmi.ptMinTrackSize.x, mmi.ptMinTrackSize.y);
	printf("  sheet @%u: client %dx%d, OK %dx%d %d from bottom, tab left %d right gap %d bottom gap %d, grip %d, icons %d, min %dx%d\n",
		m.Dpi, m.ClientCx, m.ClientCy, m.OkWidth, m.OkHeight, m.OkBottomGap, m.TabLeft, m.TabRightGap, m.TabBottomGap, m.GripCx, m.IconCx, m.MinSize.cx, m.MinSize.cy);
	return m;
}

static void TestSheet(Monitor const& a, Monitor const& b) {
	printf("Resizable icon property sheet\n");
	CTestFrame owner;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 200, a.Work.top + 200); owner.Create(nullptr, rcCreate, L"owner", WS_OVERLAPPEDWINDOW);
	CPage p1(L"One"), p2(L"Two");
	CSheet sheet;
	sheet.AddPage(&p1.m_psp);
	sheet.AddPage(&p2.m_psp);
	sheet.Create(owner);
	Pump();
	MoveTo(sheet, a);
	// make it bigger than its minimum so the layout matters
	CRect rc;
	sheet.GetWindowRect(&rc);
	sheet.SetWindowPos(nullptr, 0, 0, rc.Width() + 120, rc.Height() + 80, SWP_NOMOVE | SWP_NOZORDER);
	Pump();

	auto ma = MeasureSheet(sheet);
	Check(ma.IconCx == DpiHelper::SmallIconSize(ma.Dpi), "icons are small icons of the sheet's DPI");
	Snapshot(sheet, L"sheet_A");

	MoveTo(sheet, b);
	auto mb = MeasureSheet(sheet);
	Check(mb.Dpi == b.Dpi, "sheet has the DPI of monitor B after the move");
	CheckScaled("OK distance from bottom", ma.OkBottomGap, ma.Dpi, mb.OkBottomGap, mb.Dpi);
	CheckScaled("OK height", ma.OkHeight, ma.Dpi, mb.OkHeight, mb.Dpi);
	CheckScaled("tab left", ma.TabLeft, ma.Dpi, mb.TabLeft, mb.Dpi);
	CheckScaled("tab right gap", ma.TabRightGap, ma.Dpi, mb.TabRightGap, mb.Dpi);
	CheckScaled("tab bottom gap", ma.TabBottomGap, ma.Dpi, mb.TabBottomGap, mb.Dpi);
	CheckScaled("client width", ma.ClientCx, ma.Dpi, mb.ClientCx, mb.Dpi, 4);
	Check(mb.GripCx == DpiHelper::GetSystemMetrics(SM_CXVSCROLL, mb.Dpi), "size grip has the scroll bar width of the new DPI");
	Check(mb.IconCx == DpiHelper::SmallIconSize(mb.Dpi), "icons rebuilt in the small icon size of the new DPI");
	CheckScaled("min width", ma.MinSize.cx, ma.Dpi, mb.MinSize.cx, mb.Dpi, 4);
	Snapshot(sheet, L"sheet_B");

	// resizing still works after the change
	sheet.GetWindowRect(&rc);
	sheet.SetWindowPos(nullptr, 0, 0, rc.Width() + 50, rc.Height() + 30, SWP_NOMOVE | SWP_NOZORDER);
	Pump();
	auto mb2 = MeasureSheet(sheet);
	Check(mb2.OkBottomGap == mb.OkBottomGap && mb2.TabRightGap == mb.TabRightGap, "after a resize the controls keep their gaps");

	MoveTo(sheet, a);
	auto ma2 = MeasureSheet(sheet);
	CheckScaled("back on A: OK distance from bottom", ma.OkBottomGap, ma.Dpi, ma2.OkBottomGap, ma2.Dpi);
	Check(ma2.IconCx == ma.IconCx, "back on A: icon size as before");

	sheet.DestroyWindow();
	CSize saved = sheet.GetSize();
	UINT savedDpi = sheet.GetSizeDpi();
	printf("  saved size %dx%d @%u\n", saved.cx, saved.cy, savedDpi);
	Check(savedDpi == a.Dpi, "saved size records the DPI");

	// restore the saved size on monitor B: it's scaled to B's DPI
	CPage q1(L"One"), q2(L"Two");
	CSheet sheet2;
	sheet2.AddPage(&q1.m_psp);
	sheet2.AddPage(&q2.m_psp);
	sheet2.SetSize(saved, savedDpi);
	owner.SetWindowPos(nullptr, b.Work.left + 40, b.Work.top + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	Pump();
	// the sheet is centered on its owner, so it's created on B
	sheet2.Create(owner);
	Pump();
	CRect rc2;
	sheet2.GetWindowRect(&rc2);
	UINT dpi2 = DpiHelper::GetDpi(sheet2);
	printf("  restored on @%u: %dx%d\n", dpi2, rc2.Width(), rc2.Height());
	if (dpi2 != savedDpi)
		CheckScaled("restored width", saved.cx, savedDpi, rc2.Width(), dpi2, 4);
	sheet2.DestroyWindow();
	owner.DestroyWindow();
	Pump();
}

static void TestTreeListView(Monitor const& a, Monitor const& b) {
	printf("Tree list view\n");
	CTestFrame frame;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 540, a.Work.top + 340); frame.Create(nullptr, rcCreate, L"Tree list view DPI test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	CTreeListViewCtrl tlv;
	tlv.Create(frame, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT);
	frame.m_hChild = tlv;
	tlv.AddColumn(L"Name", 200);
	tlv.AddColumn(L"Value", 100);
	frame.SendMessage(WM_SIZE);
	Pump();
	int ha = FontHeight(tlv.GetHeaderControl().GetFont());
	UINT da = DpiHelper::GetDpi(tlv);
	MoveTo(frame, b);
	int hb = FontHeight(tlv.GetHeaderControl().GetFont());
	UINT db = DpiHelper::GetDpi(tlv);
	CheckScaled("header font height", ha, da, hb, db, 1);
	HDITEM hdi{ HDI_WIDTH };
	tlv.GetHeaderControl().GetItem(0, &hdi);
	CheckScaled("column width (200 at 96 DPI) after the move", 200, 96, hdi.cxy, db, 1);
	frame.DestroyWindow();
	Pump();
}

static void TestToolbarAndMenu(Monitor const& a, Monitor const& b) {
	printf("Tool bar and menu\n");
	CTestFrame frame;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 540, a.Work.top + 340); frame.Create(nullptr, rcCreate, L"Tool bar DPI test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	ToolBarButtonInfo buttons[] = { { 100, IDI_TEST }, { 0, 0 }, { 101, IDI_TEST, BTNS_BUTTON, L"Text" } };
	CToolBarCtrl tb(ToolbarHelper::CreateAndInitToolBar(frame, buttons, _countof(buttons)));
	int cx, cy;
	ImageList_GetIconSize(tb.GetImageList(), &cx, &cy);
	UINT da = DpiHelper::GetDpi(tb);
	CheckScaled("tool bar image size", 24, 96, cx, da, 0);
	MoveTo(frame, b);
	ToolbarHelper::UpdateToolBarImages(tb, buttons, _countof(buttons));
	ImageList_GetIconSize(tb.GetImageList(), &cx, &cy);
	CheckScaled("tool bar image size after UpdateToolBarImages", 24, 96, cx, DpiHelper::GetDpi(tb), 0);
	TBBUTTON button{};
	tb.GetButton(2, &button);
	Check(button.iBitmap == 1, "image indices kept");

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 100, L"Item");
	MenuItemData item{ 100, IDI_TEST };
	for (UINT dpi : { 96u, 192u }) {
		WTLHelper::InitMenu(menu.m_hMenu, item, dpi);
		CMenuItemInfo mii;
		mii.fMask = MIIM_CHECKMARKS;
		menu.GetMenuItemInfo(100, FALSE, &mii);
		BITMAP bm{};
		::GetObject(mii.hbmpChecked, sizeof(bm), &bm);
		CheckScaled("menu bitmap size", 16, 96, bm.bmWidth, dpi, 0);
	}
	frame.DestroyWindow();
	Pump();
}

// instantiate the remaining templates so they compile
class CTestDialog : public CDialogImpl<CTestDialog>, public CDialogHelper<CTestDialog> {
public:
	enum { IDD = IDD_DIALOG };
	BEGIN_MSG_MAP(CTestDialog)
		CHAIN_MSG_MAP(CDialogHelper<CTestDialog>)
	END_MSG_MAP()
};

static int IconWidth(HICON hIcon) {
	ICONINFO ii{};
	if (!hIcon || !::GetIconInfo(hIcon, &ii))
		return 0;
	BITMAP bm{};
	::GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
	if (ii.hbmColor)
		::DeleteObject(ii.hbmColor);
	if (ii.hbmMask)
		::DeleteObject(ii.hbmMask);
	return bm.bmWidth;
}

static void TestDialogHelper(Monitor const& a, Monitor const& b) {
	printf("Dialog helper\n");
	CTestDialog dlg;
	dlg.Create(nullptr);
	MoveTo(dlg, a);
	dlg.ShowWindow(SW_SHOWNOACTIVATE);
	dlg.AdjustOKCancelButtons(IDI_TEST, IDI_TEST);
	dlg.AddIconToButton(1001, IDI_TEST, 24);
	dlg.SetDialogIcon(IDI_TEST);
	auto measure = [&](char const* when) {
		int ok = IconWidth(CButton(dlg.GetDlgItem(IDOK)).GetIcon());
		int other = IconWidth(CButton(dlg.GetDlgItem(1001)).GetIcon());
		int smallIcon = IconWidth(dlg.GetIcon(FALSE)), bigIcon = IconWidth(dlg.GetIcon(TRUE));
		UINT dpi = DpiHelper::GetDpi(dlg);
		printf("  %s @%u: OK icon %d, other %d, dialog icons %d/%d\n", when, dpi, ok, other, smallIcon, bigIcon);
		Check(ok == DpiHelper::SmallIconSize(dpi), "OK button icon is a small icon of the dialog's DPI");
		CheckScaled("24 pixel button icon", 24, 96, other, dpi, 0);
		Check(smallIcon == DpiHelper::SmallIconSize(dpi) && bigIcon == DpiHelper::IconSize(dpi), "dialog icons in the sizes of the dialog's DPI");
	};
	measure("created");
	MoveTo(dlg, b);
	measure("moved");
	CString text;
	dlg.GetDlgItem(IDOK).GetWindowText(text);
	Check(text == L"  OK", "button text padded once");
	dlg.DestroyWindow();
	Pump();

	// compiled only
	CQuickFindEdit edit;
	if (::GetTickCount() == 0)	// never
		edit.SetWatermarkIcon(nullptr);
}

static void TestColumns(Monitor const& a, Monitor const& b) {
	printf("List view columns\n");
	CTestFrame frame;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 540, a.Work.top + 340);
	frame.Create(nullptr, rcCreate, L"Column DPI test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	CListViewCtrl lv;
	lv.Create(frame, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT);
	frame.m_hChild = lv;
	frame.SendMessage(WM_SIZE);
	ColumnManager cm(lv);
	cm.AddColumn(L"Name", LVCFMT_LEFT, 100);
	cm.AddColumn(L"Value", LVCFMT_LEFT, 60);
	cm.AddColumn(L"Hidden", LVCFMT_LEFT, 80, 0, ColumnFlags::None);
	UINT da = DpiHelper::GetDpi(lv);
	CheckScaled("column added at 100", 100, 96, lv.GetColumnWidth(0), da, 0);
	MoveTo(frame, b);
	UINT db = DpiHelper::GetDpi(lv);
	CheckScaled("column width after the move", 100, 96, lv.GetColumnWidth(0), db, 1);
	CheckScaled("second column width after the move", 60, 96, lv.GetColumnWidth(1), db, 1);
	cm.SetVisible(2, true);
	cm.UpdateColumns();
	CheckScaled("column shown at the new DPI", 80, 96, lv.GetColumnWidth(2), db, 0);
	MoveTo(frame, a);
	CheckScaled("back on A", 100, 96, lv.GetColumnWidth(0), da, 1);

	// the saved state records the DPI in its end marker; a state from before has none
	ColumnsState state;
	state.Count = 1;
	state.Dpi = 144;
	state.Order = std::make_unique<int[]>(1);
	state.Columns = std::make_unique<LVCOLUMN[]>(1);
	state.Columns[0] = { LVCF_WIDTH };
	state.Columns[0].cx = 150;
	state.Tags = std::make_unique<int[]>(1);
	CComPtr<IStream> stm;
	::CreateStreamOnHGlobal(nullptr, TRUE, &stm);
	Check(ListViewHelper::WriteColumnsState(state, stm), "state written");
	stm->Seek({}, STREAM_SEEK_SET, nullptr);
	ColumnsState read;
	Check(ListViewHelper::ReadColumnsState(read, stm) && read.Dpi == 144 && read.Columns[0].cx == 150, "state read back with its DPI");
	// an old state: the same with a 0 high half in the end marker
	STATSTG st{};
	stm->Stat(&st, STATFLAG_NONAME);
	LARGE_INTEGER pos{};
	pos.QuadPart = (LONGLONG)st.cbSize.QuadPart - 2;
	stm->Seek(pos, STREAM_SEEK_SET, nullptr);
	uint16_t zero = 0;
	stm->Write(&zero, sizeof(zero), nullptr);
	stm->Seek({}, STREAM_SEEK_SET, nullptr);
	ColumnsState old;
	Check(ListViewHelper::ReadColumnsState(old, stm) && old.Dpi == 0, "a state saved before the DPI was recorded reads with DPI 0");

	frame.DestroyWindow();
	Pump();
}

static void TestWindowPosition(Monitor const& a, Monitor const& b) {
	printf("Saved window position\n");
	Settings settings;
	CSize savedSize;
	{
		CTestFrame frame;
		CRect rc(b.Work.left + 60, b.Work.top + 60, b.Work.left + 660, b.Work.top + 460);
		frame.Create(nullptr, rc, L"Window position test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
		Pump();
		frame.GetWindowRect(&rc);
		savedSize = rc.Size();
		settings.SaveWindowPosition(frame, L"Pos");
		printf("  saved on @%u: %dx%d\n", DpiHelper::GetDpi(frame), savedSize.cx, savedSize.cy);
		frame.DestroyWindow();
	}
	{
		// created on A, restored on B: the size it was saved with, not one scaled by the DPI change on the way
		CTestFrame frame;
		CRect rc(a.Work.left + 60, a.Work.top + 60, a.Work.left + 300, a.Work.top + 300);
		frame.Create(nullptr, rc, L"Window position test", WS_OVERLAPPEDWINDOW);
		settings.LoadWindowPosition(frame, L"Pos");
		frame.ShowWindow(SW_SHOWNOACTIVATE);
		Pump();
		frame.GetWindowRect(&rc);
		printf("  restored on @%u: %dx%d\n", DpiHelper::GetDpi(frame), rc.Width(), rc.Height());
		Check(DpiHelper::GetDpi(frame) == b.Dpi && rc.Size() == savedSize, "restored at the saved size on its monitor");
		frame.DestroyWindow();
	}
	{
		// saved when that monitor had A's DPI: the size is scaled to B's
		settings.Set(L"PosDpi", (int)a.Dpi);
		CTestFrame frame;
		CRect rc(a.Work.left + 60, a.Work.top + 60, a.Work.left + 300, a.Work.top + 300);
		frame.Create(nullptr, rc, L"Window position test", WS_OVERLAPPEDWINDOW);
		settings.LoadWindowPosition(frame, L"Pos");
		Pump();
		frame.GetWindowRect(&rc);
		CheckScaled("restored width saved at another DPI", savedSize.cx, a.Dpi, rc.Width(), b.Dpi, 1);
		frame.DestroyWindow();
	}
	Pump();
}

static void TestStockIcons() {
	printf("Stock icons\n");
	for (UINT dpi : { 96u, 120u, 144u, 192u }) {
		CIcon smallIcon(IconHelper::GetStockIcon(SIID_SHIELD, false, dpi)), bigIcon(IconHelper::GetStockIcon(SIID_FOLDER, true, dpi));
		char what[64];
		sprintf_s(what, "stock icons at %u", dpi);
		printf("  @%u: small %d, big %d\n", dpi, IconWidth(smallIcon), IconWidth(bigIcon));
		Check(IconWidth(smallIcon) == DpiHelper::SmallIconSize(dpi) && IconWidth(bigIcon) == DpiHelper::IconSize(dpi), what);
	}
	CIcon shield(IconHelper::GetShieldIcon(192));
	Check(IconWidth(shield) == DpiHelper::SmallIconSize(192u), "shield icon at 192");
}

//
// the hex control's font and layout follow its DPI; measured through the caret, which is a character cell
// (the overwrite caret) or a thin line (the insert caret) placed at the caret offset
//
struct HexMetrics {
	UINT Dpi;
	CRect Caret;
};

static HexMetrics MeasureHex(CHexControl& hex) {
	GUITHREADINFO gti{ sizeof(gti) };
	::GetGUIThreadInfo(::GetCurrentThreadId(), &gti);
	return { DpiHelper::GetDpi(hex), gti.hwndCaret == hex.m_hWnd ? CRect(gti.rcCaret) : CRect() };
}

static void TestHexControl(Monitor const& a, Monitor const& b) {
	printf("Hex control\n");
	CTestFrame frame;
	CRect rcCreate(a.Work.left + 40, a.Work.top + 40, a.Work.left + 840, a.Work.top + 440); frame.Create(nullptr, rcCreate, L"Hex control DPI test", WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	std::vector<uint8_t> data(4096);
	for (size_t i = 0; i < data.size(); i++)
		data[i] = (uint8_t)i;
	auto buffer = IBufferManager::CreateFromData(data.data(), (uint32_t)data.size());
	CHexControl hex;
	hex.Create(frame, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL);
	frame.m_hChild = hex;
	hex.SetBufferManager(buffer.get());
	frame.SendMessage(WM_SIZE);
	hex.SetFocus();
	hex.GotoOffset(2 * hex.GetBytesPerLine() + 3);
	Pump();

	auto ma = MeasureHex(hex);
	Check(!ma.Caret.IsRectEmpty(), "the hex control has the caret");
	Check(ma.Dpi == a.Dpi, "hex control has the DPI of monitor A");
	Snapshot(frame, L"hex_A");

	MoveTo(frame, b);
	auto mb = MeasureHex(hex);
	Snapshot(frame, L"hex_B");
	Check(mb.Dpi == b.Dpi, "hex control has the DPI of monitor B after the move");
	CheckScaled("character height (caret height)", ma.Caret.Height(), ma.Dpi, mb.Caret.Height(), mb.Dpi);
	CheckScaled("character width (caret width)", ma.Caret.Width(), ma.Dpi, mb.Caret.Width(), mb.Dpi, 1);
	// the caret position adds up character widths and heights, so allow for their rounding
	CheckScaled("caret x (after the offsets column)", ma.Caret.left, ma.Dpi, mb.Caret.left, mb.Dpi, ma.Caret.left / 10);
	CheckScaled("caret y (after the ruler and two lines)", ma.Caret.top, ma.Dpi, mb.Caret.top, mb.Dpi, ma.Caret.top / 10);

	hex.SetInsertMode(true);
	Pump();
	auto insert = MeasureHex(hex);
	CheckScaled("insert caret width", 2, 96, insert.Caret.Width(), mb.Dpi, 0);
	hex.SetInsertMode(false);

	MoveTo(frame, a);
	auto ma2 = MeasureHex(hex);
	Check(ma2.Dpi == a.Dpi && ma2.Caret == ma.Caret, "back on A: caret size and position as before");

	frame.DestroyWindow();
	Pump();
}

int wmain(int argc, wchar_t** argv) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	::InitCommonControls();
	_Module.Init(nullptr, ::GetModuleHandle(nullptr));
	g_OutDir = argc > 1 ? std::wstring(argv[1]) + L"\\" : L"";

	auto monitors = GetMonitors();
	for (auto& m : monitors)
		printf("monitor (%d,%d)-(%d,%d) DPI %u\n", m.Work.left, m.Work.top, m.Work.right, m.Work.bottom, m.Dpi);

	TestStockIcons();

	int ia = -1, ib = -1;
	for (int i = 0; i < (int)monitors.size() && ib < 0; i++)
		for (int j = 0; j < (int)monitors.size(); j++)
			if (monitors[i].Dpi < monitors[j].Dpi) {
				ia = i;
				ib = j;
				break;
			}
	if (ib < 0) {
		printf("skipped the tests that move windows: no two monitors with different DPI\n%d failure(s)\n", g_Failures);
		_Module.Term();
		return g_Failures ? 1 : 0;
	}
	auto& a = monitors[ia];
	auto& b = monitors[ib];
	printf("A: DPI %u, B: DPI %u\n", a.Dpi, b.Dpi);

	TestTabView(a, b);
	TestSheet(a, b);
	TestTreeListView(a, b);
	TestToolbarAndMenu(a, b);
	TestDialogHelper(a, b);
	TestColumns(a, b);
	TestWindowPosition(a, b);
	TestHexControl(a, b);

	printf("%d failure(s)\n", g_Failures);
	_Module.Term();
	return g_Failures ? 1 : 0;
}
