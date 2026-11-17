// DataGridDemo: a grid of products over every column type, kept in the control's store (a few thousand rows) or
// computed by the demo as the grid asks (virtual mode: ten million rows, or five billion to try the scroll bar's
// scaling). Drag the titles' dividers to resize (a double-click fits), drag a title to move its column, right-click a
// title or a cell for a menu (hide, fit, freeze up to here). View has the row numbers, the frozen columns, the grid
// lines, the font, custom colors and (x64) WTLHelper's dark mode. The log at the bottom shows the notifications.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>

#include "DataGrid.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

enum : UINT {
	IDC_GRID = 100, IDC_LOG,
	CMD_EXIT = 1000, CMD_STORE, CMD_VIRTUAL, CMD_HUGE,
	CMD_ROWNUMBERS, CMD_FROZEN0, CMD_FROZEN1, CMD_FROZEN2, CMD_HORZLINES, CMD_VERTLINES, CMD_NOHEADER, CMD_LARGEFONT,
	CMD_CUSTOMCOLORS, CMD_DARKMODE, CMD_FITALL, CMD_SHOWALL, CMD_CLEARLOG, CMD_GOTO_END,
	CMD_HIDE = 2000, CMD_FIT, CMD_FREEZE,
};

static const wchar_t* Names[] = { L"Anvil", L"Bracket", L"Cog", L"Dowel", L"Eyelet", L"Flange", L"Gasket", L"Hinge",
	L"Joist", L"Knob", L"Lever", L"Mallet", L"Nozzle", L"Pulley", L"Rivet", L"Spring", L"Toggle", L"Valve", L"Washer" };
static const wchar_t* Notes[] = { L"", L"Back-ordered", L"Discontinued next quarter", L"Ships in 2 days",
	L"Fragile: pack with care", L"", L"Sample available", L"" };

// a row's values from its index alone, so virtual mode needs no storage
static uint64_t Mix(uint64_t x) {
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	return x ^ (x >> 33);
}

class CMainWnd : public CWindowImpl<CMainWnd, CWindow, CFrameWinTraits> {
public:
	DECLARE_WND_CLASS(L"DataGridDemoWnd")

	BEGIN_MSG_MAP(CMainWnd)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		COMMAND_RANGE_HANDLER(CMD_EXIT, CMD_FREEZE, OnCommand)
		NOTIFY_ID_HANDLER(IDC_GRID, OnGridNotify)
	END_MSG_MAP()

private:
	CDataGridCtrl m_Grid;
	CListBox m_Log;
	CMenuHandle m_Menu;
	HIMAGELIST m_Images = nullptr;
	HFONT m_LargeFont = nullptr;
	int m_IdId = 0, m_IdName = 0, m_IdCategory = 0, m_IdPrice = 0, m_IdQty = 0, m_IdStock = 0, m_IdProgress = 0,
		m_IdModified = 0, m_IdLink = 0, m_IdNotes = 0;
	std::wstring m_Text;   // the text DGVN_GETDISPINFO gives

	void Log(const wchar_t* format, ...) {
		wchar_t text[512];
		va_list args;
		va_start(args, format);
		vswprintf_s(text, format, args);
		va_end(args);
		int index = m_Log.AddString(text);
		m_Log.SetTopIndex(index);
		if (m_Log.GetCount() > 500)
			m_Log.DeleteString(0);
	}

	void Check(UINT id, bool on) {
		m_Menu.CheckMenuItem(id, on ? MF_CHECKED : MF_UNCHECKED);
	}

	bool Checked(UINT id) {
		return (m_Menu.GetMenuState(id, MF_BYCOMMAND) & MF_CHECKED) != 0;
	}

	int AddColumn(LPCWSTR title, int type, int width, UINT extraMask = 0, int fill = 0, int decimals = 2,
		UINT flags = 0, LPCWSTR items = nullptr) {
		DGVCOLUMN c{};
		c.mask = DGVCM_TITLE | DGVCM_TYPE | DGVCM_WIDTH | extraMask;
		c.pszTitle = title;
		c.type = type;
		c.cxWidth = width;
		c.fill = fill;
		c.decimals = decimals;
		c.flags = flags;
		c.pszEnumItems = items;
		return m_Grid.InsertColumn(c);
	}

	void AddColumns() {
		m_IdId = AddColumn(L"ID", DGVCT_INTEGER, 90, DGVCM_FLAGS, 0, 0, DGVCF_GROUPDIGITS);
		m_IdName = AddColumn(L"Name", DGVCT_IMAGETEXT, 140);
		m_IdCategory = AddColumn(L"Category", DGVCT_ENUM, 100, DGVCM_ENUMITEMS, 0, 0, 0, L"Hardware\0Fasteners\0Tools\0Plumbing\0");
		m_IdPrice = AddColumn(L"Price", DGVCT_FLOAT, 80, DGVCM_DECIMALS | DGVCM_FLAGS, 0, 2, DGVCF_GROUPDIGITS);
		m_IdQty = AddColumn(L"Quantity", DGVCT_INTEGER, 80, DGVCM_FLAGS, 0, 0, DGVCF_GROUPDIGITS);
		m_IdStock = AddColumn(L"In stock", DGVCT_BOOL, 70);
		m_IdProgress = AddColumn(L"Fulfilled", DGVCT_PROGRESS, 100);
		m_IdModified = AddColumn(L"Modified", DGVCT_DATETIME, 140);
		m_IdLink = AddColumn(L"Datasheet", DGVCT_LINK, 110);
		m_IdNotes = AddColumn(L"Notes", DGVCT_TEXT, 120, DGVCM_FILL | DGVCM_MINWIDTH, 1);
	}

	// the values of a row in the data
	DGVVALUE Value(LONGLONG row, int id, int& image, std::wstring& text) {
		uint64_t h = Mix((uint64_t)row + 1);
		image = -1;
		if (id == m_IdId)
			return DgvInt(100000 + row);
		if (id == m_IdName) {
			image = (int)(h % 3);
			text = std::wstring(Names[h % _countof(Names)]) + L" " + std::to_wstring(row % 1000);
			return DgvText(text.c_str());
		}
		if (id == m_IdCategory)
			return DgvInt((LONGLONG)((h >> 8) % 4));
		if (id == m_IdPrice)
			return DgvFloat(((h >> 12) % 500000) / 100.0);
		if (id == m_IdQty)
			return DgvInt((LONGLONG)((h >> 20) % 20000));
		if (id == m_IdStock)
			return DgvBool(((h >> 30) & 3) != 0);
		if (id == m_IdProgress)
			return ((h >> 33) & 1) ? DgvInt((LONGLONG)((h >> 34) % 101)) : DgvFloat(((h >> 34) % 1000) / 1000.0);
		if (id == m_IdModified) {
			ULARGE_INTEGER t;
			t.QuadPart = 133000000000000000ULL + (h >> 20) % (10000000ULL * 86400 * 900);
			return DgvDateTime(FILETIME{ t.LowPart, t.HighPart });
		}
		if (id == m_IdLink) {
			text = L"sheet-" + std::to_wstring(100000 + row) + L".pdf";
			return DgvText(text.c_str());
		}
		if (id == m_IdNotes) {
			text = Notes[(h >> 40) % _countof(Notes)];
			return text.empty() ? DgvEmpty() : DgvText(text.c_str());
		}
		return DgvEmpty();
	}

	void FillStore(LONGLONG rows) {
		m_Grid.SetVirtual(false);
		m_Grid.SetRowCount(rows);
		int ids[] = { m_IdId, m_IdName, m_IdCategory, m_IdPrice, m_IdQty, m_IdStock, m_IdProgress, m_IdModified, m_IdLink, m_IdNotes };
		for (LONGLONG r = 0; r < rows; r++)
			for (int id : ids) {
				int image;
				std::wstring text;
				auto v = Value(r, id, image, text);
				m_Grid.SetCell(r, id, v, image);
			}
		Log(L"Store: %lld rows", rows);
	}

	void SetData(UINT cmd) {
		Check(CMD_STORE, cmd == CMD_STORE);
		Check(CMD_VIRTUAL, cmd == CMD_VIRTUAL);
		Check(CMD_HUGE, cmd == CMD_HUGE);
		if (cmd == CMD_STORE) {
			FillStore(5000);
			return;
		}
		m_Grid.SetVirtual(true);
		LONGLONG rows = cmd == CMD_HUGE ? 5'000'000'000LL : 10'000'000LL;
		m_Grid.SetRowCount(rows);
		Log(L"Virtual: %lld rows", rows);
	}

	void ToggleStyle(UINT id, DWORD style, bool setWhenChecked) {
		bool on = !Checked(id);
		Check(id, on);
		m_Grid.ModifyStyle(on == setWhenChecked ? 0 : style, on == setWhenChecked ? style : 0);
	}

	void SetFrozen(int count) {
		m_Grid.SetFrozenColumns(count);
		Check(CMD_FROZEN0, count == 0);
		Check(CMD_FROZEN1, count == 1);
		Check(CMD_FROZEN2, count == 2);
	}

	void ShowMenu(const NMDGVCELL& nm) {
		CMenu menu;
		menu.CreatePopupMenu();
		wchar_t title[128] = L"";
		if (nm.idColumn) {
			DGVCOLUMN c{ DGVCM_TITLE };
			m_Grid.GetColumn(nm.idColumn, &c);
			swprintf_s(title, L"Hide \"%s\"", c.pszTitle);
			menu.AppendMenu(MF_STRING, CMD_HIDE, title);
			menu.AppendMenu(MF_STRING, CMD_FIT, L"Fit to Contents");
			menu.AppendMenu(MF_STRING, CMD_FREEZE, L"Freeze Up to Here");
			menu.AppendMenu(MF_SEPARATOR);
		}
		menu.AppendMenu(MF_STRING, CMD_FITALL, L"Fit All Columns");
		menu.AppendMenu(MF_STRING, CMD_SHOWALL, L"Show All Columns");
		UINT cmd = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, nm.ptScreen.x, nm.ptScreen.y, m_hWnd);
		switch (cmd) {
			case CMD_HIDE:
				m_Grid.ShowColumn(nm.idColumn, false);
				break;
			case CMD_FIT:
				m_Grid.AutoFitColumn(nm.idColumn);
				break;
			case CMD_FREEZE: {
				std::vector<int> order(m_Grid.GetColumnCount());
				m_Grid.GetColumnOrder(order.data(), (int)order.size());
				int shown = 0;
				for (int id : order) {
					DGVCOLUMN c{ DGVCM_FLAGS };
					m_Grid.GetColumn(id, &c);
					if (!(c.flags & DGVCF_HIDDEN))
						shown++;
					if (id == nm.idColumn)
						break;
				}
				SetFrozen(shown);
				break;
			}
			case CMD_FITALL:
			case CMD_SHOWALL:
				BOOL handled;
				OnCommand(0, (WORD)cmd, nullptr, handled);
				break;
		}
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
		CMenuHandle file, data, view;
		file.CreatePopupMenu();
		file.AppendMenu(MF_STRING, CMD_CLEARLOG, L"&Clear Log");
		file.AppendMenu(MF_SEPARATOR);
		file.AppendMenu(MF_STRING, CMD_EXIT, L"E&xit");
		data.CreatePopupMenu();
		data.AppendMenu(MF_STRING, CMD_STORE, L"&Store (5,000 rows)");
		data.AppendMenu(MF_STRING, CMD_VIRTUAL, L"&Virtual (10 million rows)");
		data.AppendMenu(MF_STRING, CMD_HUGE, L"Virtual (5 &billion rows)");
		data.AppendMenu(MF_SEPARATOR);
		data.AppendMenu(MF_STRING, CMD_GOTO_END, L"Caret to the &Last Row\tCtrl+End");
		view.CreatePopupMenu();
		view.AppendMenu(MF_STRING, CMD_ROWNUMBERS, L"&Row Numbers");
		view.AppendMenu(MF_STRING, CMD_FROZEN0, L"&No Frozen Columns");
		view.AppendMenu(MF_STRING, CMD_FROZEN1, L"Freeze &1 Column");
		view.AppendMenu(MF_STRING, CMD_FROZEN2, L"Freeze &2 Columns");
		view.AppendMenu(MF_SEPARATOR);
		view.AppendMenu(MF_STRING, CMD_HORZLINES, L"&Horizontal Lines");
		view.AppendMenu(MF_STRING, CMD_VERTLINES, L"&Vertical Lines");
		view.AppendMenu(MF_STRING, CMD_NOHEADER, L"Hide &Header");
		view.AppendMenu(MF_SEPARATOR);
		view.AppendMenu(MF_STRING, CMD_FITALL, L"&Fit All Columns");
		view.AppendMenu(MF_STRING, CMD_SHOWALL, L"&Show All Columns");
		view.AppendMenu(MF_SEPARATOR);
		view.AppendMenu(MF_STRING, CMD_LARGEFONT, L"&Large Font");
		view.AppendMenu(MF_STRING, CMD_CUSTOMCOLORS, L"&Custom Colors");
#ifdef DEMO_WTLHELPER
		view.AppendMenu(MF_STRING, CMD_DARKMODE, L"&Dark Mode");
#endif
		m_Menu.CreateMenu();
		m_Menu.AppendMenu(MF_POPUP, file, L"&File");
		m_Menu.AppendMenu(MF_POPUP, data, L"&Data");
		m_Menu.AppendMenu(MF_POPUP, view, L"&View");
		SetMenu(m_Menu);

		m_Grid.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | DGVS_ROWHEADER, WS_EX_CLIENTEDGE, IDC_GRID);
		m_Log.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
			WS_EX_CLIENTEDGE, IDC_LOG);
		m_Log.SetFont(m_Grid.GetFont());

		int size = ::GetSystemMetricsForDpi(SM_CXSMICON, ::GetDpiForWindow(m_hWnd));
		m_Images = ::ImageList_Create(size, size, ILC_COLOR32, 3, 0);
		for (auto icon : { IDI_INFORMATION, IDI_WARNING, IDI_ERROR }) {
			HICON h = nullptr;
			::LoadIconWithScaleDown(nullptr, icon, size, size, &h);
			::ImageList_AddIcon(m_Images, h);
			::DestroyIcon(h);
		}
		m_Grid.SetImageList(m_Images);

		AddColumns();
		SetData(CMD_STORE);
		Check(CMD_ROWNUMBERS, true);
		Check(CMD_HORZLINES, true);
		Check(CMD_VERTLINES, true);
		SetFrozen(1);
		return 0;
	}

	void Layout() {
		if (!m_Grid.IsWindow() || !m_Log.IsWindow())   // SetMenu sizes the window before they exist
			return;
		CRect rc;
		GetClientRect(&rc);
		int log = ::MulDiv(120, ::GetDpiForWindow(m_hWnd), 96);
		m_Grid.MoveWindow(0, 0, rc.Width(), std::max(0, rc.Height() - log));
		m_Log.MoveWindow(0, rc.Height() - log, rc.Width(), log);
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		Layout();
		return 0;
	}

	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM lParam, BOOL&) {
		auto rc = (RECT*)lParam;
		SetWindowPos(nullptr, rc, SWP_NOZORDER | SWP_NOACTIVATE);
		Layout();
		return 0;
	}

	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
		m_Grid.SetFocus();
		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
		if (m_Images)
			::ImageList_Destroy(m_Images);
		if (m_LargeFont)
			::DeleteObject(m_LargeFont);
		::PostQuitMessage(0);
		return 0;
	}

	LRESULT OnCommand(WORD, WORD id, HWND, BOOL&) {
		switch (id) {
			case CMD_EXIT:
				DestroyWindow();
				break;
			case CMD_CLEARLOG:
				m_Log.ResetContent();
				break;
			case CMD_STORE:
			case CMD_VIRTUAL:
			case CMD_HUGE:
				SetData(id);
				break;
			case CMD_GOTO_END: {
				int caretId = m_IdId;
				m_Grid.GetCaret(nullptr, &caretId);
				m_Grid.SetCaret(m_Grid.GetRowCount() - 1, caretId);
				m_Grid.SetFocus();
				break;
			}
			case CMD_ROWNUMBERS:
				ToggleStyle(id, DGVS_ROWHEADER, true);
				break;
			case CMD_HORZLINES:
				ToggleStyle(id, DGVS_NOHORZLINES, false);
				break;
			case CMD_VERTLINES:
				ToggleStyle(id, DGVS_NOVERTLINES, false);
				break;
			case CMD_NOHEADER:
				ToggleStyle(id, DGVS_NOHEADER, true);
				break;
			case CMD_FROZEN0: SetFrozen(0); break;
			case CMD_FROZEN1: SetFrozen(1); break;
			case CMD_FROZEN2: SetFrozen(2); break;
			case CMD_FITALL: {
				std::vector<int> order(m_Grid.GetColumnCount());
				m_Grid.GetColumnOrder(order.data(), (int)order.size());
				for (int columnId : order)
					if (columnId != m_IdNotes)   // the fill column keeps its share
						m_Grid.AutoFitColumn(columnId);
				break;
			}
			case CMD_SHOWALL: {
				std::vector<int> order(m_Grid.GetColumnCount());
				m_Grid.GetColumnOrder(order.data(), (int)order.size());
				for (int columnId : order)
					m_Grid.ShowColumn(columnId, true);
				break;
			}
			case CMD_LARGEFONT: {
				bool on = !Checked(id);
				Check(id, on);
				if (on) {
					NONCLIENTMETRICSW ncm{ sizeof(ncm) };
					::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, ::GetDpiForWindow(m_hWnd));
					ncm.lfMessageFont.lfHeight = ncm.lfMessageFont.lfHeight * 3 / 2;
					if (!m_LargeFont)
						m_LargeFont = ::CreateFontIndirectW(&ncm.lfMessageFont);
					m_Grid.SetFont(m_LargeFont);
				}
				else {
					m_Grid.SendMessage(WM_SETFONT, 0, TRUE);   // back to its own
				}
				break;
			}
			case CMD_CUSTOMCOLORS: {
				bool on = !Checked(id);
				Check(id, on);
				DGVCOLORS c{ RGB(253, 250, 240), RGB(60, 50, 30), RGB(230, 220, 195), RGB(240, 230, 205), RGB(90, 60, 20),
					RGB(250, 238, 200), RGB(200, 120, 20), RGB(160, 80, 0) };
				m_Grid.SetColors(on ? &c : nullptr);
				break;
			}
#ifdef DEMO_WTLHELPER
			case CMD_DARKMODE:
				WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, m_hWnd);
				Check(id, WTLHelper::IsDarkMode());
				Invalidate();
				break;
#endif
		}
		return 0;
	}

	LRESULT OnGridNotify(int, LPNMHDR hdr, BOOL&) {
		switch (hdr->code) {
			case DGVN_GETDISPINFO: {
				auto info = (NMDGVDISPINFO*)hdr;
				info->value = Value(info->iRow, info->idColumn, info->iImage, m_Text);
				break;
			}
			case DGVN_CARETCHANGED: {
				auto nm = (NMDGVCARET*)hdr;
				Log(L"DGVN_CARETCHANGED: row %lld, column %d (was %lld, %d)", nm->iRow, nm->idColumn, nm->iOldRow, nm->idOldColumn);
				break;
			}
			case DGVN_COLUMNCLICK:
			case DGVN_COLUMNRESIZED:
			case DGVN_COLUMNMOVED: {
				auto nm = (NMDGVCOLUMN*)hdr;
				Log(L"%s: column %d at %d (was %d), %d DIPs", hdr->code == DGVN_COLUMNCLICK ? L"DGVN_COLUMNCLICK"
					: hdr->code == DGVN_COLUMNRESIZED ? L"DGVN_COLUMNRESIZED" : L"DGVN_COLUMNMOVED",
					nm->idColumn, nm->iDisplayIndex, nm->iOldIndex, nm->cxWidth);
				break;
			}
			case DGVN_ACTIVATE:
			case DGVN_LINKCLICK: {
				auto nm = (NMDGVCELL*)hdr;
				wchar_t text[256];
				m_Grid.GetCellText(nm->iRow, nm->idColumn, text, _countof(text));
				Log(L"%s: row %lld, column %d: \"%s\"", hdr->code == DGVN_ACTIVATE ? L"DGVN_ACTIVATE" : L"DGVN_LINKCLICK",
					nm->iRow, nm->idColumn, text);
				break;
			}
			case DGVN_CONTEXTMENU: {
				auto nm = (NMDGVCELL*)hdr;
				Log(L"DGVN_CONTEXTMENU: flags 0x%X, row %lld, column %d", nm->flags, nm->iRow, nm->idColumn);
				ShowMenu(*nm);
				break;
			}
		}
		return 0;
	}
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	::InitCommonControlsEx(&icc);
	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	CMessageLoop loop;
	_Module.AddMessageLoop(&loop);
	CMainWnd wnd;
	CRect rc(0, 0, ::MulDiv(1100, ::GetDpiForSystem(), 96), ::MulDiv(700, ::GetDpiForSystem(), 96));
	wnd.Create(nullptr, CWindow::rcDefault, L"DataGrid Demo");
	wnd.SetWindowPos(nullptr, 0, 0, rc.Width(), rc.Height(), SWP_NOMOVE | SWP_NOZORDER);
	wnd.ShowWindow(nCmdShow);
	loop.Run();
	_Module.RemoveMessageLoop();
	_Module.Term();
	return 0;
}
