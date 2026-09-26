#include "DockHost.h"
#include "DockWindowList.h"

#include <commctrl.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace WTLDock {

namespace {

using namespace WindowsDialogIds;

struct Dialog {
	CDockHost* Host;
	DockWindowList List;
	DockWindowList::Column SortColumn{ DockWindowList::Column::Name };
	bool Ascending{ true };
	bool IncludeTools{};
	std::wstring ChosenId;
	HWND Window{};
	HWND ListView{};
	int Dpi{ 96 };
	bool CanSave{};
};

int Scaled(const Dialog& d, int value) {
	return ::MulDiv(value, d.Dpi, 96);
}

// An empty dialog template with a title and the system's dialog font; the controls are made by WM_INITDIALOG.
std::vector<BYTE> MakeTemplate(const wchar_t* title) {
	std::vector<WORD> words;
	auto put = [&](WORD w) { words.push_back(w); };
	auto putDword = [&](DWORD d) { put(LOWORD(d)); put(HIWORD(d)); };
	putDword(DS_MODALFRAME | DS_SHELLFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER);
	putDword(0);				// extended style
	put(0);						// no controls
	put(0); put(0); put(10); put(10);
	put(0);						// no menu
	put(0);						// default class
	for (const wchar_t* c = title; ; c++) {
		put(*c);
		if (!*c)
			break;
	}
	put(8);						// font size
	for (const wchar_t* c = L"MS Shell Dlg"; ; c++) {
		put(*c);
		if (!*c)
			break;
	}
	std::vector<BYTE> bytes(words.size() * 2);
	memcpy(bytes.data(), words.data(), bytes.size());
	return bytes;
}

HWND Make(Dialog& d, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int id, HFONT font) {
	HWND w = ::CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, d.Window, (HMENU)(INT_PTR)id, nullptr, nullptr);
	if (w && font)
		::SendMessage(w, WM_SETFONT, (WPARAM)font, TRUE);
	return w;
}

void Fill(Dialog& d) {
	d.List.Build(d.Host->Layout(), d.IncludeTools);
	d.List.Sort(d.SortColumn, d.Ascending);
	::SendMessage(d.ListView, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(d.ListView);
	int index = 0;
	for (auto& row : d.List.Rows()) {
		LVITEMW item{};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.iItem = index;
		item.pszText = const_cast<wchar_t*>(row.Name.c_str());
		item.lParam = (LPARAM)row.Pane;
		ListView_InsertItem(d.ListView, &item);
		ListView_SetItemText(d.ListView, index, 1, const_cast<wchar_t*>(row.Type.c_str()));
		ListView_SetItemText(d.ListView, index, 2, const_cast<wchar_t*>(row.State.c_str()));
		ListView_SetItemText(d.ListView, index, 3, const_cast<wchar_t*>(row.Modified ? L"Yes" : L""));
		index++;
	}
	if (index > 0)
		ListView_SetItemState(d.ListView, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	::SendMessage(d.ListView, WM_SETREDRAW, TRUE, 0);
}

std::vector<DockPane*> Selected(Dialog& d) {
	std::vector<DockPane*> panes;
	for (int i = ListView_GetNextItem(d.ListView, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(d.ListView, i, LVNI_SELECTED))
		if (i < (int)d.List.Rows().size())
			panes.push_back(d.List.Rows()[i].Pane);
	return panes;
}

void UpdateButtons(Dialog& d) {
	const auto panes = Selected(d);
	bool anyModified = false;
	for (auto p : panes)
		anyModified |= p->Modified;
	bool anyClosable = false;
	for (auto p : panes)
		anyClosable |= Has(p->Caps, PaneCaps::CanClose);
	::EnableWindow(::GetDlgItem(d.Window, Activate), !panes.empty());
	::EnableWindow(::GetDlgItem(d.Window, Save), anyModified && d.CanSave);
	::EnableWindow(::GetDlgItem(d.Window, CloseWindows), anyClosable);
}

void Layout(Dialog& d) {
	const int m = Scaled(d, 12), buttonW = Scaled(d, 132), buttonH = Scaled(d, 28), gap = Scaled(d, 6);
	RECT client;
	::GetClientRect(d.Window, &client);
	const int listRight = client.right - m - buttonW - m;
	const int checkH = Scaled(d, 20);
	::SetWindowPos(::GetDlgItem(d.Window, IncludeTools), nullptr, m, m, listRight - m, checkH, SWP_NOZORDER);
	const int listTop = m + checkH + gap;
	::SetWindowPos(d.ListView, nullptr, m, listTop, listRight - m, client.bottom - m - listTop, SWP_NOZORDER);

	int y = listTop;
	for (int id : { Activate, Save, CloseWindows }) {
		if (id == Save && !d.CanSave) {
			::ShowWindow(::GetDlgItem(d.Window, id), SW_HIDE);
			continue;
		}
		::SetWindowPos(::GetDlgItem(d.Window, id), nullptr, client.right - m - buttonW, y, buttonW, buttonH, SWP_NOZORDER);
		y += buttonH + gap;
	}
	::SetWindowPos(::GetDlgItem(d.Window, Close), nullptr, client.right - m - buttonW, client.bottom - m - buttonH, buttonW, buttonH, SWP_NOZORDER);

	// the columns share the list
	RECT list;
	::GetClientRect(d.ListView, &list);
	const int total = list.right - ::GetSystemMetricsForDpi(SM_CXVSCROLL, d.Dpi);
	const int widths[] = { total * 34 / 100, total * 20 / 100, total * 26 / 100, total * 20 / 100 };
	for (int i = 0; i < 4; i++)
		ListView_SetColumnWidth(d.ListView, i, widths[i]);
}

void Init(Dialog& d, HWND window) {
	d.Window = window;
	d.Dpi = (int)::GetDpiForWindow(window);
	if (d.Dpi <= 0)
		d.Dpi = 96;
	d.CanSave = (bool)d.Host->OnPaneSave;
	HFONT font = (HFONT)::SendMessage(window, WM_GETFONT, 0, 0);

	Make(d, L"BUTTON", L"Include &tool windows", BS_AUTOCHECKBOX | WS_TABSTOP, 0, IncludeTools, font);
	d.ListView = Make(d, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | WS_TABSTOP | WS_BORDER, 0, List, font);
	ListView_SetExtendedListViewStyle(d.ListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	const wchar_t* const titles[] = { L"Name", L"Type", L"State", L"Modified" };
	for (int i = 0; i < 4; i++) {
		LVCOLUMNW column{};
		column.mask = LVCF_TEXT | LVCF_WIDTH;
		column.pszText = const_cast<wchar_t*>(titles[i]);
		column.cx = 100;
		ListView_InsertColumn(d.ListView, i, &column);
	}
	Make(d, L"BUTTON", L"&Activate", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, Activate, font);
	Make(d, L"BUTTON", L"&Save", BS_PUSHBUTTON | WS_TABSTOP, 0, Save, font);
	Make(d, L"BUTTON", L"&Close Window(s)", BS_PUSHBUTTON | WS_TABSTOP, 0, CloseWindows, font);
	Make(d, L"BUTTON", L"C&lose", BS_PUSHBUTTON | WS_TABSTOP, 0, Close, font);

	// the size of the client area, and the place on the screen
	RECT rc{ 0, 0, Scaled(d, 560), Scaled(d, 380) };
	::AdjustWindowRectExForDpi(&rc, (DWORD)::GetWindowLongPtr(window, GWL_STYLE), FALSE, (DWORD)::GetWindowLongPtr(window, GWL_EXSTYLE), d.Dpi);
	::SetWindowPos(window, nullptr, 0, 0, Width(rc), Height(rc), SWP_NOMOVE | SWP_NOZORDER);
	Layout(d);
	Fill(d);
	UpdateButtons(d);
}

void CloseSelected(Dialog& d) {
	for (auto pane : Selected(d))
		d.Host->ClosePane(pane);
	Fill(d);
	UpdateButtons(d);
}

void SaveSelected(Dialog& d) {
	for (auto pane : Selected(d)) {
		if (pane->Modified && d.Host->OnPaneSave && d.Host->OnPaneSave(pane)) {
			pane->Modified = false;
			d.Host->RefreshPane(pane);
		}
	}
	Fill(d);
	UpdateButtons(d);
}

INT_PTR CALLBACK Proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
	auto d = reinterpret_cast<Dialog*>(::GetWindowLongPtr(window, GWLP_USERDATA));
	switch (msg) {
		case WM_INITDIALOG:
			d = reinterpret_cast<Dialog*>(lp);
			::SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)d);
			Init(*d, window);
			return TRUE;

		case WM_COMMAND:
			if (!d)
				break;
			switch (LOWORD(wp)) {
				case Activate: {
					auto panes = Selected(*d);
					if (!panes.empty()) {
						d->ChosenId = panes.front()->Id();
						::EndDialog(window, IDOK);
					}
					return TRUE;
				}
				case CloseWindows:
					CloseSelected(*d);
					return TRUE;
				case Save:
					SaveSelected(*d);
					return TRUE;
				case IncludeTools:
					d->IncludeTools = ::IsDlgButtonChecked(window, IncludeTools) == BST_CHECKED;
					Fill(*d);
					UpdateButtons(*d);
					return TRUE;
				case IDCANCEL:
					if (d->ChosenId.empty())		// (one that has been chosen is on its way out already)
						::EndDialog(window, IDCANCEL);
					return TRUE;
			}
			break;

		case WM_NOTIFY: {
			if (!d)
				break;
			auto header = reinterpret_cast<NMHDR*>(lp);
			if (header->idFrom != (UINT_PTR)List)
				break;
			if (header->code == LVN_ITEMCHANGED) {
				UpdateButtons(*d);
			}
			else if (header->code == LVN_COLUMNCLICK) {
				const auto column = (DockWindowList::Column)reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem;
				d->Ascending = column == d->SortColumn ? !d->Ascending : true;
				d->SortColumn = column;
				Fill(*d);
				UpdateButtons(*d);
			}
			else if (header->code == NM_DBLCLK) {
				::SendMessage(window, WM_COMMAND, MAKEWPARAM(Activate, BN_CLICKED), 0);
			}
			return TRUE;
		}

		case WM_DPICHANGED:
			if (d) {
				d->Dpi = HIWORD(wp);
				auto rc = reinterpret_cast<const RECT*>(lp);
				::SetWindowPos(window, nullptr, rc->left, rc->top, Width(*rc), Height(*rc), SWP_NOZORDER | SWP_NOACTIVATE);
				Layout(*d);
			}
			return TRUE;

		case WM_SIZE:
			if (d && d->ListView)
				Layout(*d);
			return TRUE;
	}
	return FALSE;
}

}

bool CDockHost::ShowWindowsDialog(HWND parent) {
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
	::InitCommonControlsEx(&icc);
	CancelNavigator();
	HideTip();

	Dialog dialog;
	dialog.Host = this;
	if (!parent)
		parent = ::GetAncestor(m_hWnd, GA_ROOT);
	const auto tmpl = MakeTemplate(L"Windows");
	const INT_PTR result = ::DialogBoxIndirectParamW(::GetModuleHandle(nullptr), reinterpret_cast<const DLGTEMPLATE*>(tmpl.data()),
		parent, Proc, (LPARAM)&dialog);
	if (result == IDOK && !dialog.ChosenId.empty())
		return ShowPane(m_Layout.FindPane(dialog.ChosenId));
	return false;
}

}
