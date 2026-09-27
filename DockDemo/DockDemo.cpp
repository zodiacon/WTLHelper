// DockDemo.cpp : a playground for the WTLDock framework
//

#include "pch.h"
#include "MainFrm.h"

CAppModule _Module;

int Run(int nCmdShow) {
	CMessageLoop loop;
	_Module.AddMessageLoop(&loop);

	CMainFrame frame;
	if (frame.CreateEx() == nullptr) {
		ATLTRACE(_T("Main window creation failed!\n"));
		return 0;
	}
	frame.ShowWindow(nCmdShow);

	int result = loop.Run();
	_Module.RemoveMessageLoop();
	return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int nCmdShow) {
	HRESULT hr = ::CoInitialize(nullptr);
	ATLASSERT(SUCCEEDED(hr));

	AtlInitCommonControls(ICC_BAR_CLASSES);
	hr = _Module.Init(nullptr, hInstance);
	ATLASSERT(SUCCEEDED(hr));

	// WTLHelper's dark mode: the controls (and, through UseDarkModeTheme, the docking area) follow it
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	int result = Run(nCmdShow);

	_Module.Term();
	::CoUninitialize();
	return result;
}
