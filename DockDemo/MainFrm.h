#pragma once

// The playground: a frame with a CDockHost whose panes are real controls.
class CMainFrame : public CFrameWindowImpl<CMainFrame> {
public:
	DECLARE_FRAME_WND_CLASS_EX(L"DockDemoFrame", 0, 0, COLOR_APPWORKSPACE)

	BEGIN_MSG_MAP(CMainFrame)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
		MESSAGE_HANDLER(WM_INITMENUPOPUP, OnInitMenuPopup)
		MESSAGE_RANGE_HANDLER(WM_CTLCOLORMSGBOX, WM_CTLCOLORSTATIC, OnCtlColor)
		COMMAND_ID_HANDLER(ID_RESET, OnReset)
		COMMAND_ID_HANDLER(ID_SAVE, OnSave)
		COMMAND_ID_HANDLER(ID_LOAD, OnLoad)
		COMMAND_ID_HANDLER(ID_DUMP, OnDump)
		COMMAND_ID_HANDLER(ID_DARK, OnDark)
		COMMAND_ID_HANDLER(ID_NEW_DOC, OnNewDocument)
		COMMAND_ID_HANDLER(ID_NEW_MANY, OnNewDocument)
		COMMAND_ID_HANDLER(ID_CLOSE_ACTIVE, OnCloseCommand)
		COMMAND_ID_HANDLER(ID_CLOSE_OTHERS, OnCloseCommand)
		COMMAND_ID_HANDLER(ID_CLOSE_GROUP, OnCloseCommand)
		COMMAND_ID_HANDLER(ID_PANE_INFO, OnPaneInfo)
		COMMAND_ID_HANDLER(ID_HELP_USAGE, OnHelp)
		COMMAND_ID_HANDLER(ID_EXIT, OnExit)
		COMMAND_ID_HANDLER(ID_ACT_HIDE, OnActiveCommand)
		COMMAND_ID_HANDLER(ID_ACT_AUTOHIDE, OnActiveCommand)
		COMMAND_ID_HANDLER(ID_ACT_FLOAT, OnActiveCommand)
		COMMAND_RANGE_HANDLER(ID_ACT_EDGE, ID_ACT_EDGE + 3, OnActiveCommand)
		COMMAND_RANGE_HANDLER(ID_ACT_BESIDE, ID_ACT_BESIDE + 3, OnActiveCommand)
		COMMAND_RANGE_HANDLER(ID_ACT_TAB, ID_ACT_TAB + 49, OnActiveCommand)
		COMMAND_RANGE_HANDLER(ID_PANE_FIRST, ID_PANE_FIRST + 99, OnTogglePane)
		CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
	END_MSG_MAP()

private:
	enum : UINT {
		ID_RESET = 1001, ID_SAVE, ID_LOAD, ID_DUMP, ID_DARK, ID_HELP_USAGE, ID_EXIT,
		ID_NEW_DOC = 1030, ID_NEW_MANY, ID_CLOSE_ACTIVE, ID_CLOSE_OTHERS, ID_CLOSE_GROUP,
		ID_PANE_INFO = 3000,	// added to the tab context menu by OnBuildPaneMenu
		ID_ACT_HIDE = 1100, ID_ACT_AUTOHIDE, ID_ACT_FLOAT,
		ID_ACT_EDGE = 1110,		// + Left, Right, Top, Bottom
		ID_ACT_BESIDE = 1120,	// the same relative to the document area
		ID_ACT_TAB = 1200,		// + index in the list of tool panes
		ID_PANE_FIRST = 2000,
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnInitMenuPopup(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCtlColor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnReset(WORD, WORD, HWND, BOOL&);
	LRESULT OnSave(WORD, WORD, HWND, BOOL&);
	LRESULT OnLoad(WORD, WORD, HWND, BOOL&);
	LRESULT OnDump(WORD, WORD, HWND, BOOL&);
	LRESULT OnDark(WORD, WORD, HWND, BOOL&);
	LRESULT OnHelp(WORD, WORD, HWND, BOOL&);
	LRESULT OnNewDocument(WORD, WORD, HWND, BOOL&);
	LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
	LRESULT OnPaneInfo(WORD, WORD, HWND, BOOL&);
	LRESULT OnExit(WORD, WORD, HWND, BOOL&);
	LRESULT OnActiveCommand(WORD, WORD, HWND, BOOL&);
	LRESULT OnTogglePane(WORD, WORD, HWND, BOOL&);

	void BuildMenu();
	void CreateContent();
	void BuildLayout();
	WTLDock::DockPane* NewDocument();
	void HookDock();
	void ApplyTheme();
	void ApplyFonts();
	void UpdateStatus();
	std::wstring LayoutFilePath() const;

	WTLDock::CDockHost m_Dock;

	CTreeViewCtrl m_Solution, m_ClassView;
	CListBox m_Toolbox;
	CListViewCtrl m_Properties, m_Errors;
	CEdit m_Output, m_Documents[3];
	std::vector<std::unique_ptr<CEdit>> m_NewDocuments;
	int m_UntitledCount{};
	WTLDock::DockPane* m_MenuPane{};	// the pane whose context menu is open
	CFont m_MonoFont;
	CBrush m_DarkBrush;

	CMenu m_PaneMenu, m_ActiveMenu, m_TabMenu;
	std::vector<WTLDock::DockPane*> m_TabTargets;
	std::string m_DefaultLayout;
	bool m_Dark{};
};
