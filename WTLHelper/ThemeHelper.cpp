#include "pch.h"
#include <unordered_map>
#include <detours/detours.h>
#include "ThemeHelper.h"
#include "Theme.h"
#include "CustomEdit.h"
#include "SizeGrip.h"
#include "CustomStatusBar.h"
#include "CustomButton.h"
#include "CustomDialog.h"
#include "CustomHeader.h"
#include "CustomRebar.h"
#include "CustomListView.h"
#include "OwnerDrawnMenu.h"
#include "CustomTabControl.h"
#include "CustomTreeView.h"
#include "CustomToolBar.h"
#include "CustomComboBox.h"

const Theme* CurrentTheme;
Theme g_DefaultTheme{ true };
std::atomic<int> SuspendCount;

static decltype(::GetSysColor)* OrgGetSysColor = ::GetSysColor;
static decltype(::GetSysColorBrush)* OrgGetSysColorBrush = ::GetSysColorBrush;
static decltype(::GetSystemMetrics)* OrgGetSystemMetrics = ::GetSystemMetrics;
static decltype(::SetTextColor)* OrgSetTextColor = ::SetTextColor;
static decltype(::ReleaseDC)* OrgReleaseDC = ::ReleaseDC;

int WINAPI HookedGetSystemMetrics(_In_ int index) {
	return OrgGetSystemMetrics(index);
}

HBRUSH WINAPI HookedGetSysColorBrush(int index) {
	if (CurrentTheme && SuspendCount == 0) {
		auto hBrush = CurrentTheme->GetSysBrush(index);
		if (hBrush)
			return hBrush;
	}
	return OrgGetSysColorBrush(index);
}

COLORREF WINAPI HookedGetSysColor(int index) {
	if (CurrentTheme && SuspendCount == 0) {
		auto color = CurrentTheme->GetSysColor(index);
		if (color != CLR_INVALID)
			return color;
	}
	return OrgGetSysColor(index);
}

void HandleCreateWindow(CWPRETSTRUCT* cs) {
	CString name;
	CWindow win(cs->hwnd);
	auto lpcs = (LPCREATESTRUCT)cs->lParam;
	if (!::GetClassName(cs->hwnd, name.GetBufferSetLength(32), 32))
		return;

	if (name.CompareNoCase(WC_COMBOBOX) != 0) {
		if ((lpcs->style & (WS_THICKFRAME | WS_CAPTION | WS_POPUP | WS_DLGFRAME)) == 0)
			::SetWindowTheme(cs->hwnd, L" ", L"");
	}
	if (name.CompareNoCase(WC_COMBOBOX) == 0) {
		auto win = new CCustomComboBox;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
	}
	else if (name.CompareNoCase(L"ComboLBox") == 0) {
		auto win = new CCustomComboLBox;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
	}
	else if (name.CompareNoCase(L"EDIT") == 0 || name.CompareNoCase(L"ATL:EDIT") == 0) {
		auto win = new CCustomEdit;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
	}
	else if (name.CompareNoCase(WC_LISTVIEW) == 0) {
		auto win = new CCustomListView;
		win->SubclassWindow(cs->hwnd);
	}
	else if (name.CompareNoCase(WC_TREEVIEW) == 0 || name.CompareNoCase(CString(L"ATL:") + WC_TREEVIEW) == 0) {
		auto win = new CCustomTreeView;
		win->SubclassWindow(cs->hwnd);
		win->Init();
	}
	else if (name.CompareNoCase(WC_TABCONTROL) == 0 || name.CompareNoCase(L"ATL:" WC_TABCONTROL) == 0) {
		auto win = new CCustomTabControlParent;
		ATLVERIFY(win->SubclassWindow(lpcs->hwndParent));
		win->Init(cs->hwnd);
	}
	else if (name.CompareNoCase(REBARCLASSNAME) == 0) {
		//::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomRebar;
		win->SubclassWindow(cs->hwnd);
	}
	else if (name.CompareNoCase(TOOLBARCLASSNAME) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomToolBarParent;
		win->Init(cs->hwnd);
	}
	else if (name.CompareNoCase(WC_HEADER) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomHeaderParent;
		win->SubclassWindow(lpcs->hwndParent);
		win->Init(cs->hwnd);
	}
	else if (name.CompareNoCase(L"#32770") == 0) {		// dialog
		auto win = new CCustomDialog;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
		//win->ModifyStyle(WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0);
	}
	else if (name.CompareNoCase(STATUSCLASSNAME) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto pwin = new CCustomStatusBarParent;
		pwin->Init(cs->hwnd, ::GetParent(cs->hwnd));
	}
	else if (name.CompareNoCase(L"ScrollBar") == 0) {
		if (lpcs->style & SBS_SIZEGRIP) {
			auto win = new CSizeGrip;
			ATLVERIFY(win->SubclassWindow(cs->hwnd));
		}
		else {
			//auto win = new CCustomScrollBar;
			//win->SubclassWindow(cs->hwnd);
		}
	}
	else if (name.CompareNoCase(WC_BUTTON) == 0) {
		auto type = lpcs->style & BS_TYPEMASK;
		if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_GROUPBOX) {
			auto win = new CCustomButtonParent;
			win->Init(cs->hwnd);
		}
	}
}

LRESULT CALLBACK CallWndProc(int action, WPARAM wp, LPARAM lp) {
	if (SuspendCount == 0 && action == HC_ACTION) {
		auto cs = reinterpret_cast<CWPRETSTRUCT*>(lp);

		switch (cs->message) {
			case WM_CREATE:
				HandleCreateWindow(cs);
				break;

		}
	}

	return ::CallNextHookEx(nullptr, action, wp, lp);
}

bool ThemeHelper::Init(HANDLE hThread) {
	auto hook = ::SetWindowsHookEx(WH_CALLWNDPROCRET, CallWndProc, nullptr, ::GetThreadId(hThread));
	if (!hook)
		return false;

	if (NOERROR != DetourTransactionBegin())
		return false;

	DetourUpdateThread(hThread);
	DetourAttach((PVOID*)&OrgGetSysColor, HookedGetSysColor);
	DetourAttach((PVOID*)&OrgGetSysColorBrush, HookedGetSysColorBrush);
	DetourAttach((PVOID*)&OrgGetSystemMetrics, HookedGetSystemMetrics);
	auto error = DetourTransactionCommit();
	ATLASSERT(error == NOERROR);
	if (CurrentTheme == nullptr)
		CurrentTheme = &g_DefaultTheme;
	return error == NOERROR;
}

int ThemeHelper::Suspend() {
	return ++SuspendCount;
}

bool ThemeHelper::IsSuspended() {
	return SuspendCount > 0;
}

int ThemeHelper::Resume() {
	return --SuspendCount;
}

const Theme* ThemeHelper::GetCurrentTheme() {
	return CurrentTheme;
}

bool ThemeHelper::IsDefault() {
	return GetCurrentTheme() == nullptr || GetCurrentTheme()->IsDefault();
}

void ThemeHelper::SetCurrentTheme(const Theme& theme, HWND hWnd) {
	CurrentTheme = &theme;
	if (hWnd) {
		CWindow(hWnd).SendMessageToDescendants(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), 0, reinterpret_cast<LPARAM>(&theme));
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_UPDATENOW);
	}
}

void ThemeHelper::SetDefaultTheme(HWND hWnd) {
	SetCurrentTheme(g_DefaultTheme, hWnd);
}

void ThemeHelper::UpdateMenuColors(COwnerDrawnMenuBase& menu, bool dark) {
	//
	// customize menu colors
	//
	auto theme = GetCurrentTheme();
	auto& mtheme = theme->Menu;
	menu.SetBackColor(mtheme.BackColor);
	menu.SetTextColor(mtheme.TextColor);
	menu.SetSelectionTextColor(mtheme.SelectionTextColor);
	menu.SetSelectionBackColor(mtheme.SelectionBackColor);
	menu.SetSeparatorColor(mtheme.SeparatorColor);
}

