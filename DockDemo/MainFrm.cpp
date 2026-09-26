#include "pch.h"
#include "MainFrm.h"

using namespace WTLDock;

namespace {

constexpr COLORREF DarkBack = RGB(30, 30, 30);
constexpr COLORREF DarkText = RGB(220, 220, 220);

const wchar_t* const SampleSource[] = {
	L"// Program.cpp\r\n\r\n#include \"pch.h\"\r\n#include \"MainFrm.h\"\r\n\r\nCAppModule _Module;\r\n\r\nint Run(int nCmdShow) {\r\n"
	L"\tCMessageLoop loop;\r\n\t_Module.AddMessageLoop(&loop);\r\n\r\n\tCMainFrame frame;\r\n\tif (frame.CreateEx() == nullptr)\r\n\t\treturn 0;\r\n"
	L"\tframe.ShowWindow(nCmdShow);\r\n\r\n\tint result = loop.Run();\r\n\t_Module.RemoveMessageLoop();\r\n\treturn result;\r\n}\r\n",

	L"// MainFrm.h\r\n\r\n#pragma once\r\n\r\nclass CMainFrame : public CFrameWindowImpl<CMainFrame> {\r\npublic:\r\n"
	L"\tDECLARE_FRAME_WND_CLASS_EX(L\"DockDemoFrame\", 0, 0, COLOR_APPWORKSPACE)\r\n\r\n\tBEGIN_MSG_MAP(CMainFrame)\r\n"
	L"\t\tMESSAGE_HANDLER(WM_CREATE, OnCreate)\r\n\t\tCHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)\r\n\tEND_MSG_MAP()\r\n\r\n"
	L"private:\r\n\tWTLDock::CDockHost m_Dock;\r\n};\r\n",

	L"# DockDemo\r\n\r\nA playground for the WTLDock framework.\r\n\r\n"
	L"* drag the splitters to resize the panes\r\n* click a tab to switch, the X in a caption to close a pane\r\n"
	L"* use the *Active pane* menu to move the active pane around\r\n* Layout > Save state keeps the arrangement\r\n",
};

}

LRESULT CMainFrame::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	SetWindowText(L"DockDemo - WTLDock playground");
	CreateSimpleStatusBar();
	BuildMenu();

	m_Dock.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	m_hWndClient = m_Dock;
	m_Dock.OnLayoutChanged = [this] { UpdateStatus(); };
	m_Dock.OnActivePaneChanged = [this] { UpdateStatus(); };
	HookDock();

	// Saved layouts mention documents that the application has to make again: Untitled<n> here.
	m_Dock.SetPaneFactory([this](DockLayout& layout, const std::wstring& id) -> DockPane* {
		if (!id.starts_with(L"Untitled"))
			return nullptr;
		m_UntitledCount = std::max(m_UntitledCount, _wtoi(id.c_str() + 8));
		PaneDesc d;
		d.Id = d.Title = id;
		d.Kind = PaneKind::Document;
		d.Icon = ::LoadIcon(nullptr, IDI_APPLICATION);
		return layout.AddPane(d);		// no window: the content factory makes it when the pane is first on show
	});
	m_Dock.SetContentFactory([this](DockPane&, HWND parent) -> HWND {
		return *CreateEditor(parent);
	});

	if (auto loop = _Module.GetMessageLoop())
		loop->AddMessageFilter(this);

	m_StateFile = FileNextToExe(L"DockDemo.state.json");
	m_LayoutsFile = FileNextToExe(L"DockDemo.layouts.json");

	CreateContent();
	ApplyFonts();
	BuildLayout();
	m_Dock.CaptureDefaultLayout();
	ApplyTheme();

	// A saved state is picked up if there is one; it exists only after Layout > Save state, so that a plain run
	// always starts from the default arrangement.
	m_Dock.Layouts().LoadFromFile(m_LayoutsFile);
	std::wstring error;
	if (::GetFileAttributes(m_StateFile.c_str()) != INVALID_FILE_ATTRIBUTES && !m_Dock.LoadStateFromFile(m_StateFile, {}, &error))
		MessageBox((L"The saved state could not be used, starting with the default layout.\n" + error).c_str(), L"DockDemo", MB_ICONWARNING);
	UpdateStatus();
	return 0;
}

LRESULT CMainFrame::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	if (auto loop = _Module.GetMessageLoop())
		loop->RemoveMessageFilter(this);
	handled = FALSE;
	return 0;
}

LRESULT CMainFrame::OnSwitcher(WORD, WORD, HWND, BOOL&) {
	m_Dock.ShowNavigator(true, false);
	return 0;
}

LRESULT CMainFrame::OnClose(UINT, WPARAM, LPARAM, BOOL& handled) {
	// once the user has saved a state, it follows them
	if (::GetFileAttributes(m_StateFile.c_str()) != INVALID_FILE_ATTRIBUTES)
		m_Dock.SaveStateToFile(m_StateFile);
	handled = FALSE;
	return 0;
}

void CMainFrame::CreateContent() {
	const DWORD tree = WS_CHILD | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
	const DWORD list = WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
	const DWORD edit = WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN;

	m_Solution.Create(m_Dock, rcDefault, nullptr, tree);
	auto solution = m_Solution.InsertItem(L"Solution 'DockDemo' (2 projects)", TVI_ROOT, TVI_LAST);
	for (auto project : { L"DockDemo", L"WTLDock" }) {
		auto item = m_Solution.InsertItem(project, solution, TVI_LAST);
		for (auto file : { L"pch.h", L"MainFrm.h", L"MainFrm.cpp", L"README.md" })
			m_Solution.InsertItem(file, item, TVI_LAST);
		m_Solution.Expand(item);
	}
	m_Solution.Expand(solution);

	m_ClassView.Create(m_Dock, rcDefault, nullptr, tree);
	for (auto cls : { L"CDockHost", L"CDockGroupWnd", L"DockLayout", L"DockGroup", L"DockSplit" }) {
		auto item = m_ClassView.InsertItem(cls, TVI_ROOT, TVI_LAST);
		for (auto member : { L"Create()", L"Layout()", L"Sync()" })
			m_ClassView.InsertItem(member, item, TVI_LAST);
	}

	m_Toolbox.Create(m_Dock, rcDefault, nullptr, WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY);
	for (auto item : { L"Pointer", L"Button", L"CheckBox", L"ComboBox", L"Label", L"ListBox", L"TextBox", L"TreeView" })
		m_Toolbox.AddString(item);

	m_Properties.Create(m_Dock, rcDefault, nullptr, list | LVS_NOSORTHEADER);
	m_Properties.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_Properties.InsertColumn(0, L"Property", LVCFMT_LEFT, 110);
	m_Properties.InsertColumn(1, L"Value", LVCFMT_LEFT, 140);
	int row = 0;
	for (auto& [name, value] : { std::pair{ L"Name", L"MainFrm.h" }, std::pair{ L"Build Action", L"None" }, std::pair{ L"Full Path", L"C:\\Dev\\DockDemo" } }) {
		m_Properties.AddItem(row, 0, name);
		m_Properties.AddItem(row, 1, value);
		row++;
	}

	m_Errors.Create(m_Dock, rcDefault, nullptr, list);
	m_Errors.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_Errors.InsertColumn(0, L"Code", LVCFMT_LEFT, 80);
	m_Errors.InsertColumn(1, L"Description", LVCFMT_LEFT, 360);
	m_Errors.InsertColumn(2, L"File", LVCFMT_LEFT, 120);
	m_Errors.AddItem(0, 0, L"C4189");
	m_Errors.AddItem(0, 1, L"local variable is initialized but not referenced");
	m_Errors.AddItem(0, 2, L"MainFrm.cpp");

	m_Output.Create(m_Dock, rcDefault, nullptr, edit | ES_READONLY);
	m_Output.SetWindowText(L"1>------ Build started: Project: WTLDock, Configuration: Debug x64 ------\r\n1>DockHost.cpp\r\n1>WTLDock.vcxproj -> WTLDock.lib\r\n========== Build: 1 succeeded, 0 failed ==========\r\n");

	for (int i = 0; i < 3; i++) {
		m_Documents[i].Create(m_Dock, rcDefault, nullptr, edit);
		m_Documents[i].SetWindowText(SampleSource[i]);
	}
}

void CMainFrame::BuildLayout() {
	auto& layout = m_Dock.Layout();
	auto add = [&](const wchar_t* title, HWND content, PaneKind kind, DockSide side = DockSide::Left, int cx = 250, int cy = 250) {
		PaneDesc d;
		d.Id = d.Title = title;
		d.hWnd = content;
		d.Kind = kind;
		d.DefaultSide = side;
		d.PreferredSize = { cx, cy };
		return layout.AddPane(d);
	};

	auto solution = add(L"Solution Explorer", m_Solution, PaneKind::Tool, DockSide::Left, 260, 400);
	auto classView = add(L"Class View", m_ClassView, PaneKind::Tool, DockSide::Left, 260, 400);
	auto toolbox = add(L"Toolbox", m_Toolbox, PaneKind::Tool, DockSide::Left, 220, 400);
	auto properties = add(L"Properties", m_Properties, PaneKind::Tool, DockSide::Right, 280, 400);
	auto output = add(L"Output", m_Output, PaneKind::Tool, DockSide::Bottom, 400, 190);
	auto errors = add(L"Error List", m_Errors, PaneKind::Tool, DockSide::Bottom, 400, 190);
	auto program = add(L"Program.cpp", m_Documents[0], PaneKind::Document);
	auto frame = add(L"MainFrm.h", m_Documents[1], PaneKind::Document);
	auto readme = add(L"README.md", m_Documents[2], PaneKind::Document);
	for (auto document : { program, frame, readme })
		document->Icon = ::LoadIcon(nullptr, IDI_APPLICATION);

	layout.Show(solution);
	layout.Show(properties);
	layout.Show(output);
	layout.Show(program);
	layout.Show(frame);
	layout.Show(readme);
	layout.DockTo(errors, output->Group(), DockPosition::Tab);
	layout.DockTo(classView, solution->Group(), DockPosition::Tab);
	layout.Show(toolbox);
	layout.AutoHide(toolbox->Group());
	layout.Activate(solution);
	layout.Activate(output);
	layout.Activate(frame);
	m_Dock.ActivatePane(frame);
}

//
// theme and fonts
//

void CMainFrame::ApplyTheme() {
	m_Dock.SetTheme(m_Dark ? DockTheme::Dark() : DockTheme::Light());

	const COLORREF back = m_Dark ? DarkBack : ::GetSysColor(COLOR_WINDOW);
	const COLORREF text = m_Dark ? DarkText : ::GetSysColor(COLOR_WINDOWTEXT);
	if (!m_DarkBrush.IsNull())
		m_DarkBrush.DeleteObject();
	m_DarkBrush.CreateSolidBrush(DarkBack);

	for (auto* tree : { &m_Solution, &m_ClassView }) {
		tree->SetBkColor(back);
		tree->SetTextColor(text);
	}
	for (auto* list : { &m_Properties, &m_Errors }) {
		list->SetBkColor(back);
		list->SetTextBkColor(back);
		list->SetTextColor(text);
	}
	// scroll bars and selection colours follow the visual style
	const wchar_t* style = m_Dark ? L"DarkMode_Explorer" : L"Explorer";
	for (HWND hWnd : { (HWND)m_Solution, (HWND)m_ClassView, (HWND)m_Toolbox, (HWND)m_Properties, (HWND)m_Errors,
		(HWND)m_Output, (HWND)m_Documents[0], (HWND)m_Documents[1], (HWND)m_Documents[2] })
		::SetWindowTheme(hWnd, style, nullptr);
	for (auto& edit : m_NewDocuments)
		::SetWindowTheme(*edit, style, nullptr);

	// the header control of a list view takes its colours from the ItemsView style
	for (auto* list : { &m_Properties, &m_Errors }) {
		::SetWindowTheme(*list, m_Dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
		if (auto header = list->GetHeader())
			::SetWindowTheme(header, m_Dark ? L"DarkMode_ItemsView" : L"Explorer", nullptr);
	}

	RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

LRESULT CMainFrame::OnCtlColor(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	// the edit and list box controls ask their parent (which is forwarded to us by the dock) for their colours
	if (!m_Dark) {
		handled = FALSE;
		return 0;
	}
	HDC dc = (HDC)wp;
	::SetTextColor(dc, DarkText);
	::SetBkColor(dc, DarkBack);
	return (LRESULT)m_DarkBrush.m_hBrush;
}

void CMainFrame::ApplyFonts() {
	if (!m_MonoFont.IsNull())
		m_MonoFont.DeleteObject();
	m_MonoFont.CreateFont(-::MulDiv(10, m_Dock.Dpi(), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

	for (HWND hWnd : { (HWND)m_Solution, (HWND)m_ClassView, (HWND)m_Toolbox, (HWND)m_Properties, (HWND)m_Errors })
		::SendMessage(hWnd, WM_SETFONT, (WPARAM)m_Dock.Font(), TRUE);
	for (HWND hWnd : { (HWND)m_Output, (HWND)m_Documents[0], (HWND)m_Documents[1], (HWND)m_Documents[2] })
		::SendMessage(hWnd, WM_SETFONT, (WPARAM)m_MonoFont.m_hFont, TRUE);
}

LRESULT CMainFrame::OnDpiChanged(UINT, WPARAM, LPARAM lp, BOOL&) {
	auto rc = reinterpret_cast<const RECT*>(lp);
	SetWindowPos(nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
	ApplyFonts();		// the dock has followed the new DPI by now
	return 0;
}

LRESULT CMainFrame::OnDark(WORD, WORD, HWND, BOOL&) {
	m_Dark = !m_Dark;
	ApplyTheme();
	return 0;
}

//
// menus
//

void CMainFrame::BuildMenu() {
	CMenu menu;
	menu.CreateMenu();

	CMenu layout;
	layout.CreatePopupMenu();
	layout.AppendMenu(MF_STRING, ID_RESET, L"&Reset layout");
	layout.AppendMenu(MF_SEPARATOR);
	layout.AppendMenu(MF_STRING, ID_SAVE_NAMED, L"Save as &named layout");
	m_LayoutMenu.CreatePopupMenu();		// filled in when it opens
	layout.AppendMenu(MF_POPUP, (UINT_PTR)m_LayoutMenu.m_hMenu, L"&Apply named layout");
	layout.AppendMenu(MF_STRING, ID_DELETE_NAMED, L"Delete all named la&youts");
	layout.AppendMenu(MF_SEPARATOR);
	layout.AppendMenu(MF_STRING, ID_SAVE, L"&Save state (and keep saving it at exit)");
	layout.AppendMenu(MF_STRING, ID_LOAD, L"&Load saved state");
	layout.AppendMenu(MF_STRING, ID_FORGET, L"&Forget saved state");
	layout.AppendMenu(MF_SEPARATOR);
	layout.AppendMenu(MF_STRING, ID_DUMP, L"Show &dump...");
	layout.AppendMenu(MF_SEPARATOR);
	layout.AppendMenu(MF_STRING, ID_EXIT, L"E&xit");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)layout.m_hMenu, L"&Layout");
	layout.Detach();

	// filled in when the menus open (see OnInitMenuPopup)
	m_PaneMenu.CreatePopupMenu();
	menu.AppendMenu(MF_POPUP, (UINT_PTR)m_PaneMenu.m_hMenu, L"&Panes");

	m_ActiveMenu.CreatePopupMenu();
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_HIDE, L"&Hide");
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_AUTOHIDE, L"&Auto hide");
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_FLOAT, L"&Float / Dock");
	m_ActiveMenu.AppendMenu(MF_SEPARATOR);
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_GROUP, L"New &horizontal tab group");
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_GROUP + 1, L"New &vertical tab group");
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_GROUP + 2, L"Move to &next tab group");
	m_ActiveMenu.AppendMenu(MF_STRING, ID_ACT_GROUP + 3, L"Move to p&revious tab group");
	m_ActiveMenu.AppendMenu(MF_SEPARATOR);
	static const wchar_t* const sides[] = { L"Left", L"Right", L"Top", L"Bottom" };
	CMenu edge, beside;
	edge.CreatePopupMenu();
	beside.CreatePopupMenu();
	for (int i = 0; i < 4; i++) {
		edge.AppendMenu(MF_STRING, ID_ACT_EDGE + i, sides[i]);
		beside.AppendMenu(MF_STRING, ID_ACT_BESIDE + i, sides[i]);
	}
	m_ActiveMenu.AppendMenu(MF_POPUP, (UINT_PTR)edge.m_hMenu, L"Dock at window &edge");
	edge.Detach();
	m_ActiveMenu.AppendMenu(MF_POPUP, (UINT_PTR)beside.m_hMenu, L"Dock &beside the documents");
	beside.Detach();
	m_TabMenu.CreatePopupMenu();
	m_ActiveMenu.AppendMenu(MF_POPUP, (UINT_PTR)m_TabMenu.m_hMenu, L"Dock as &tab of");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)m_ActiveMenu.m_hMenu, L"Active pa&ne");

	CMenu window;
	window.CreatePopupMenu();
	window.AppendMenu(MF_STRING, ID_NEW_DOC, L"&New document");
	window.AppendMenu(MF_STRING, ID_NEW_MANY, L"New &10 documents");
	window.AppendMenu(MF_SEPARATOR);
	window.AppendMenu(MF_STRING, ID_CLOSE_ACTIVE, L"&Close active pane");
	window.AppendMenu(MF_STRING, ID_CLOSE_OTHERS, L"Close &others in its group");
	window.AppendMenu(MF_STRING, ID_CLOSE_GROUP, L"Close all in its &group");
	window.AppendMenu(MF_SEPARATOR);
	window.AppendMenu(MF_STRING, ID_CLOSE_DOCS, L"Close all doc&uments");
	window.AppendMenu(MF_STRING, ID_CLOSE_DOCS_BUT, L"Close all documents but t&he active one");
	window.AppendMenu(MF_STRING, ID_NEXT_DOC, L"Ne&xt document	Ctrl+F6");
	window.AppendMenu(MF_STRING, ID_PREV_DOC, L"&Previous document	Ctrl+Shift+F6");
	window.AppendMenu(MF_STRING, ID_SWITCHER, L"&Window switcher	Ctrl+Tab");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)window.m_hMenu, L"&Window");
	window.Detach();

	CMenu options;
	options.CreatePopupMenu();
	options.AppendMenu(MF_STRING, ID_DARK, L"&Dark theme");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)options.m_hMenu, L"&Options");
	options.Detach();

	CMenu help;
	help.CreatePopupMenu();
	help.AppendMenu(MF_STRING, ID_HELP_USAGE, L"&How to use");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)help.m_hMenu, L"&Help");
	help.Detach();

	SetMenu(menu.Detach());
}

LRESULT CMainFrame::OnInitMenuPopup(UINT, WPARAM wp, LPARAM, BOOL& handled) {
	handled = FALSE;
	const HMENU popup = (HMENU)wp;
	auto& layout = m_Dock.Layout();
	auto clear = [](CMenuHandle menu) {
		while (menu.GetMenuItemCount() > 0)
			menu.DeleteMenu(0, MF_BYPOSITION);
	};

	if (popup == m_PaneMenu.m_hMenu) {
		clear(m_PaneMenu.m_hMenu);
		m_Dock.FillPaneMenu(m_PaneMenu, ID_PANE_FIRST, PaneKind::Tool);
		m_PaneMenu.AppendMenu(MF_SEPARATOR);
		m_Dock.FillPaneMenu(m_PaneMenu, ID_PANE_FIRST, PaneKind::Document);
	}
	else if (popup == m_LayoutMenu.m_hMenu) {
		clear(m_LayoutMenu.m_hMenu);
		if (m_Dock.FillLayoutMenu(m_LayoutMenu, ID_LAYOUT_FIRST) == 0)
			m_LayoutMenu.AppendMenu(MF_STRING | MF_GRAYED, (UINT_PTR)0, L"(none saved)");
	}
	else if (popup == m_ActiveMenu.m_hMenu) {
		auto pane = m_Dock.ActivePane();
		const bool placed = pane && pane->Group();
		m_ActiveMenu.EnableMenuItem(ID_ACT_HIDE, MF_BYCOMMAND | (placed ? MF_ENABLED : MF_GRAYED));
		m_ActiveMenu.EnableMenuItem(ID_ACT_AUTOHIDE, MF_BYCOMMAND | (placed && pane->State() == PaneState::Docked ? MF_ENABLED : MF_GRAYED));
		m_ActiveMenu.EnableMenuItem(ID_ACT_FLOAT, MF_BYCOMMAND | (m_Dock.CanExecute(DockCommand::Float, pane) || m_Dock.CanExecute(DockCommand::Dock, pane) ? MF_ENABLED : MF_GRAYED));
		const DockCommand groupCommands[] = { DockCommand::NewHorizontalGroup, DockCommand::NewVerticalGroup, DockCommand::MoveToNextGroup, DockCommand::MoveToPreviousGroup };
		for (int i = 0; i < 4; i++)
			m_ActiveMenu.EnableMenuItem(ID_ACT_GROUP + i, MF_BYCOMMAND | (m_Dock.CanExecute(groupCommands[i], pane) ? MF_ENABLED : MF_GRAYED));
		for (UINT i = 1; i < (UINT)m_ActiveMenu.GetMenuItemCount(); i++) {
			// the submenus only make sense for a tool pane (documents live in the document area)
			if (CMenuHandle sub = m_ActiveMenu.GetSubMenu(i); sub.m_hMenu)
				m_ActiveMenu.EnableMenuItem(i, MF_BYPOSITION | (placed && pane->Kind() == PaneKind::Tool ? MF_ENABLED : MF_GRAYED));
		}
	}
	else if (popup == m_TabMenu.m_hMenu) {
		clear(m_TabMenu.m_hMenu);
		m_TabTargets.clear();
		auto active = m_Dock.ActivePane();
		for (auto& p : layout.Panes()) {
			if (active && p.get() != active && p->Kind() == active->Kind() && p->Group() && p->Group()->Location() != GroupLocation::AutoHide)
				m_TabTargets.push_back(p.get());
		}
		UINT id = ID_ACT_TAB;
		for (auto p : m_TabTargets)
			m_TabMenu.AppendMenu(MF_STRING, id++, p->Title.c_str());
		if (m_TabTargets.empty())
			m_TabMenu.AppendMenu(MF_STRING | MF_GRAYED, (UINT_PTR)0, L"(no other pane)");
	}
	else {
		return 0;
	}
	handled = TRUE;
	return 0;
}

LRESULT CMainFrame::OnActiveCommand(WORD, WORD id, HWND, BOOL&) {
	auto pane = m_Dock.ActivePane();
	if (!pane)
		return 0;
	auto& layout = m_Dock.Layout();

	bool ok = false;
	if (id == ID_ACT_HIDE)
		ok = layout.Hide(pane);
	else if (id == ID_ACT_AUTOHIDE)
		ok = pane->Group() && layout.AutoHide(pane->Group());
	else if (id == ID_ACT_FLOAT)
		ok = m_Dock.ToggleFloat(pane);
	else if (id >= ID_ACT_GROUP && id < ID_ACT_GROUP + 4) {
		const DockCommand groupCommands[] = { DockCommand::NewHorizontalGroup, DockCommand::NewVerticalGroup, DockCommand::MoveToNextGroup, DockCommand::MoveToPreviousGroup };
		ok = m_Dock.Execute(groupCommands[id - ID_ACT_GROUP], pane);
	}
	else if (id >= ID_ACT_EDGE && id < ID_ACT_EDGE + 4)
		ok = layout.DockToEdge(pane, (DockSide)(id - ID_ACT_EDGE));
	else if (id >= ID_ACT_BESIDE && id < ID_ACT_BESIDE + 4)
		ok = layout.DockTo(pane, layout.PrimaryDocumentGroup(), (DockPosition)(id - ID_ACT_BESIDE));
	else if (id >= ID_ACT_TAB && id - ID_ACT_TAB < m_TabTargets.size())
		ok = layout.DockTo(pane, m_TabTargets[id - ID_ACT_TAB]->Group(), DockPosition::Tab);

	if (!ok)
		::MessageBeep(MB_ICONEXCLAMATION);
	return 0;
}

LRESULT CMainFrame::OnShowPane(WORD, WORD id, HWND, BOOL&) {
	m_Dock.HandlePaneCommand(id, ID_PANE_FIRST);
	return 0;
}

//
// layout commands
//

LRESULT CMainFrame::OnReset(WORD, WORD, HWND, BOOL&) {
	std::wstring error;
	if (!m_Dock.ResetLayout(&error))
		MessageBox(error.c_str(), L"DockDemo", MB_ICONERROR);
	return 0;
}

std::wstring CMainFrame::FileNextToExe(const wchar_t* name) {
	wchar_t path[MAX_PATH];
	::GetModuleFileName(nullptr, path, _countof(path));
	std::wstring result(path);
	return result.substr(0, result.find_last_of(L'\\') + 1) + name;
}

LRESULT CMainFrame::OnSave(WORD, WORD, HWND, BOOL&) {
	if (!m_Dock.SaveStateToFile(m_StateFile))
		MessageBox(L"Failed to write the state file", L"DockDemo", MB_ICONERROR);
	else
		MessageBox((L"Saved to " + m_StateFile).c_str(), L"DockDemo", MB_ICONINFORMATION);
	return 0;
}

LRESULT CMainFrame::OnLoad(WORD, WORD, HWND, BOOL&) {
	std::wstring error;
	if (!m_Dock.LoadStateFromFile(m_StateFile, {}, &error))
		MessageBox((L"Failed to load " + m_StateFile + L"\n" + error).c_str(), L"DockDemo", MB_ICONERROR);
	return 0;
}

LRESULT CMainFrame::OnForget(WORD, WORD, HWND, BOOL&) {
	::DeleteFile(m_StateFile.c_str());
	return 0;
}

void CMainFrame::SaveNamedLayouts() {
	if (m_Dock.Layouts().Empty())
		::DeleteFile(m_LayoutsFile.c_str());
	else if (!m_Dock.Layouts().SaveToFile(m_LayoutsFile))
		MessageBox(L"Failed to write the layouts file", L"DockDemo", MB_ICONERROR);
}

LRESULT CMainFrame::OnSaveNamed(WORD, WORD, HWND, BOOL&) {
	std::wstring name;
	for (int i = 1; name.empty() || m_Dock.Layouts().Contains(name); i++)
		name = std::format(L"Layout {}", i);
	m_Dock.SaveLayoutAs(name);
	SaveNamedLayouts();
	return 0;
}

LRESULT CMainFrame::OnDeleteNamed(WORD, WORD, HWND, BOOL&) {
	m_Dock.Layouts().Clear();
	SaveNamedLayouts();
	return 0;
}

LRESULT CMainFrame::OnApplyLayout(WORD, WORD id, HWND, BOOL&) {
	if (!m_Dock.HandleLayoutCommand(id, ID_LAYOUT_FIRST))
		::MessageBeep(MB_ICONEXCLAMATION);
	return 0;
}

LRESULT CMainFrame::OnCloseDocuments(WORD, WORD id, HWND, BOOL&) {
	m_Dock.CloseAllDocuments(id == ID_CLOSE_DOCS_BUT);
	return 0;
}

LRESULT CMainFrame::OnNextDocument(WORD, WORD id, HWND, BOOL&) {
	if (!m_Dock.ActivateNextDocument(id == ID_NEXT_DOC))
		::MessageBeep(MB_ICONEXCLAMATION);
	return 0;
}

LRESULT CMainFrame::OnDump(WORD, WORD, HWND, BOOL&) {
	MessageBox(m_Dock.Layout().Dump().c_str(), L"Layout dump", MB_ICONINFORMATION);
	return 0;
}

LRESULT CMainFrame::OnHelp(WORD, WORD, HWND, BOOL&) {
	MessageBox(
		L"Drag a splitter to resize the panes.\n"
		L"Click a tab to switch panes; click the X in a caption (or on a document tab) to close a pane; drag tabs to reorder them.\n"
		L"The pin in a tool window's caption auto-hides it; hover or click its item on the bar to slide it out, and press the pin in the flyout to dock it again.\n"
		L"The arrow next to the pin is the window's menu (float, dock, auto hide, close).\n"
		L"The Panes menu brings a pane into view (showing it if it is hidden); the Active pane menu moves the pane that has the focus.\n"
		L"Double click a tool window's caption (or a tab) to float it, and the caption or title bar of the floating window to dock it again.\n"
		L"Layout > Save state keeps the arrangement (floating windows, window position and active pane included) and restores it at the next start; named layouts are kept in DockDemo.layouts.json.\n\n"
		L"Drag a tab or a caption to dock it somewhere else: drop it on a compass marker (tab, split) or an edge marker of the window, or anywhere else to float it. Hold Ctrl to float without docking, Esc to cancel.\n"
		L"Drag a floating window by its title bar over the markers to dock it.\n\n"
		L"Documents float and split as well: drop a tab on the side of another document group for a new tab group, or use the Active pane menu.\n"
		L"Keyboard: Ctrl+Tab window switcher (hold Ctrl, Tab or arrows to choose), Ctrl+F6 next document, Ctrl+F4 close document, "
		L"Alt+F6 next pane, Shift+Esc close tool window, Alt+- window menu.",
		L"DockDemo", MB_ICONINFORMATION);
	return 0;
}

LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&) {
	PostMessage(WM_CLOSE);
	return 0;
}

void CMainFrame::UpdateStatus() {
	if (!m_hWndStatusBar)
		return;
	static const wchar_t* const states[] = { L"hidden", L"docked", L"document", L"auto-hidden", L"floating" };
	std::wstring text;
	if (auto pane = m_Dock.ActivePane())
		text = std::format(L"Active: {} ({})   ", pane->Title, states[(int)pane->State()]);
	auto dump = m_Dock.Layout().Dump();
	std::replace(dump.begin(), dump.end(), L'\n', L'|');
	::SetWindowText(m_hWndStatusBar, (text + dump).c_str());
}

//
// documents and the tab hooks
//

CEdit* CMainFrame::CreateEditor(HWND parent) {
	auto edit = std::make_unique<CEdit>();
	edit->Create(parent, rcDefault, nullptr,
		WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN);
	edit->SetFont(m_MonoFont);
	::SetWindowTheme(*edit, m_Dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	m_NewDocuments.push_back(std::move(edit));
	return m_NewDocuments.back().get();
}

CEdit* CMainFrame::EditOf(HWND content) {
	for (auto& edit : m_NewDocuments)
		if (content && (HWND)*edit == content)
			return edit.get();
	return nullptr;
}

DockPane* CMainFrame::NewDocument() {
	PaneDesc d;
	d.Id = d.Title = std::format(L"Untitled{}", ++m_UntitledCount);
	d.Kind = PaneKind::Document;
	d.Icon = ::LoadIcon(nullptr, IDI_APPLICATION);

	auto& layout = m_Dock.Layout();
	auto pane = layout.AddPane(d);
	layout.Show(pane);
	m_Dock.ActivatePane(pane);		// its editor is made by the content factory when the pane is first on show
	return pane;
}

void CMainFrame::HookDock() {
	// documents named Untitled<n> are throw-away: closing one with text in it asks first, and then it is gone for good
	m_Dock.OnPaneClosing = [this](DockPane* pane) {
		if (!pane->Id().starts_with(L"Untitled"))
			return true;
		auto edit = EditOf(pane->hWnd);
		if (edit && edit->GetWindowTextLength() > 0)
			return MessageBox(std::format(L"Close {} without saving?", pane->Title).c_str(), L"DockDemo", MB_YESNO | MB_ICONQUESTION) == IDYES;
		return true;
	};
	m_Dock.OnPaneClosed = [this](DockPane* pane) {
		if (!pane->Id().starts_with(L"Untitled"))
			return;
		if (pane->hWnd) {
			std::erase_if(m_NewDocuments, [&](auto& edit) { return (HWND)*edit == pane->hWnd; });
			::DestroyWindow(pane->hWnd);
		}
		m_Dock.Layout().RemovePane(pane);
	};
	// the framework's context menu already has Close, Close All But This, Close All Tabs and Auto Hide
	m_Dock.OnBuildPaneMenu = [this](DockPane* pane, HMENU menu) {
		m_MenuPane = pane;
		::AppendMenu(menu, MF_STRING, ID_PANE_INFO, L"Pane &info...");
	};
}

LRESULT CMainFrame::OnNewDocument(WORD, WORD id, HWND, BOOL&) {
	for (int i = 0; i < (id == ID_NEW_MANY ? 10 : 1); i++) {
		auto pane = NewDocument();
		if (auto edit = EditOf(pane->hWnd); edit && i % 2 == 0)
			edit->SetWindowText(std::format(L"// {}\r\n", pane->Title).c_str());
	}
	return 0;
}

LRESULT CMainFrame::OnCloseCommand(WORD, WORD id, HWND, BOOL&) {
	auto pane = m_Dock.ActivePane();
	const DockCommand command = id == ID_CLOSE_ACTIVE ? DockCommand::Close : id == ID_CLOSE_OTHERS ? DockCommand::CloseOthers : DockCommand::CloseAll;
	if (!pane || !m_Dock.Execute(command, pane))
		::MessageBeep(MB_ICONEXCLAMATION);
	return 0;
}

LRESULT CMainFrame::OnPaneInfo(WORD, WORD, HWND, BOOL&) {
	if (!m_MenuPane)
		return 0;
	static const wchar_t* const states[] = { L"hidden", L"docked", L"document", L"auto-hidden", L"floating" };
	MessageBox(std::format(L"Id: {}\nState: {}\nTabs in its group: {}", m_MenuPane->Id(), states[(int)m_MenuPane->State()],
		m_MenuPane->Group() ? m_MenuPane->Group()->Panes().size() : 0).c_str(), L"Pane info", MB_ICONINFORMATION);
	return 0;
}
