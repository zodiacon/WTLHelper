#pragma once

#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <string>
#include <format>

//
// A page of documentation (by default, of Microsoft Learn) in the browser that Windows has (WebView2). It is started when
// the first page is asked for; a page is loaded after a pause (the arrow keys in a list pass many). Links stay in it, except
// those that open a window of their own: those open in the default browser. Its colors follow SetDarkMode.
// Header-only: only those who include it need the WebView2 SDK (its loader is linked by whoever uses it).
//
class CDocPane : public CWindowImpl<CDocPane> {
public:
	DECLARE_WND_CLASS_EX(L"WTLHelperDocPane", 0, -1)

	// the page to show: 'url' (empty: none, 'name' has no documentation)
	void Navigate(std::wstring url, std::wstring const& name) {
		m_Url = std::move(url);
		m_Name = name;
		if (!m_Controller) {
			Start();
			return;
		}
		SetTimer(TimerLoad, 300);
	}
	// the page asked for last (empty if there is none)
	std::wstring const& Url() const {
		return m_Url;
	}
	// Microsoft Learn's search for a name
	static std::wstring SearchUrl(std::wstring const& name) {
		// (a name of Windows has letters, digits and underscores only: nothing to escape)
		return L"https://learn.microsoft.com/search/?terms=" + name + L"&category=Documentation";
	}
	// the colors of the pane and of the pages (those that have both)
	void SetDarkMode(bool dark) {
		if (m_Dark == dark)
			return;
		m_Dark = dark;
		ApplyColors();
		// (the page that says there is none has the colors in it; a site's page takes the scheme when it is loaded)
		if (m_WebView && m_Url.empty()) {
			m_Loaded = L"\n";
			Load();
		}
		else if (m_WebView)
			m_WebView->Reload();
		if (m_hWnd)
			Invalidate();
	}
	bool IsDarkMode() const {
		return m_Dark;
	}
	// Where the browser keeps its data (its cache, cookies); set before the first page. By default, in the user's folder of
	// the application (%LOCALAPPDATA%\<name of the executable>\WebView2): not next to the executable, which may be in a
	// folder that cannot be written to.
	void SetUserDataFolder(std::wstring folder) {
		m_DataFolder = std::move(folder);
	}

	BEGIN_MSG_MAP(CDocPane)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SHOWWINDOW, OnShowWindow)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	END_MSG_MAP()

private:
	static constexpr UINT_PTR TimerLoad = 1;

	COLORREF Background() const {
		return m_Dark ? RGB(32, 32, 32) : ::GetSysColor(COLOR_WINDOW);
	}
	COLORREF Foreground() const {
		return m_Dark ? RGB(220, 220, 220) : ::GetSysColor(COLOR_WINDOWTEXT);
	}

	static std::wstring Escaped(std::wstring const& text) {
		std::wstring result;
		for (auto c : text) {
			switch (c) {
				case L'<': result += L"&lt;"; break;
				case L'>': result += L"&gt;"; break;
				case L'&': result += L"&amp;"; break;
				default: result += c;
			}
		}
		return result;
	}

	std::wstring DataFolder() const {
		if (!m_DataFolder.empty())
			return m_DataFolder;
		WCHAR path[MAX_PATH];
		::GetModuleFileName(nullptr, path, _countof(path));
		std::wstring name = path;
		name = name.substr(name.find_last_of(L'\\') + 1);
		if (auto dot = name.find_last_of(L'.'); dot != std::wstring::npos)
			name.resize(dot);
		WCHAR folder[MAX_PATH];
		::ExpandEnvironmentStrings((L"%LOCALAPPDATA%\\" + name + L"\\WebView2").c_str(), folder, _countof(folder));
		return folder;
	}

	void Start() {
		using Microsoft::WRL::Callback;
		if (m_Starting || m_Controller)
			return;
		m_Starting = true;
		m_Message = L"Starting the browser...";
		Invalidate();
		auto hWnd = m_hWnd;
		auto hr = ::CreateCoreWebView2EnvironmentWithOptions(nullptr, DataFolder().c_str(), nullptr,
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, hWnd](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
				// (the window may have been closed meanwhile: this is no more)
				if (!::IsWindow(hWnd))
					return S_OK;
				if (FAILED(result) || !environment) {
					m_Starting = false;
					m_Message = std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)result);
					Invalidate();
					return S_OK;
				}
				return environment->CreateCoreWebView2Controller(hWnd,
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this, hWnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
						if (!::IsWindow(hWnd)) {
							if (controller)
								controller->Close();
							return S_OK;
						}
						m_Starting = false;
						if (FAILED(result) || !controller) {
							m_Message = std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)result);
							Invalidate();
							return S_OK;
						}
						Started(controller);
						return S_OK;
					}).Get());
			}).Get());
		if (FAILED(hr)) {
			m_Starting = false;
			m_Message = hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ?
				L"The documentation is shown by Microsoft Edge WebView2, which is not installed." :
				std::format(L"The browser could not be started (error 0x{:08X}).", (uint32_t)hr);
			Invalidate();
		}
	}

	void Started(ICoreWebView2Controller* controller) {
		using Microsoft::WRL::Callback;
		m_Controller = controller;
		m_Controller->get_CoreWebView2(&m_WebView);
		if (!m_WebView)
			return;
		Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
		if (SUCCEEDED(m_WebView->get_Settings(&settings))) {
			settings->put_AreDevToolsEnabled(FALSE);
			settings->put_IsStatusBarEnabled(FALSE);
		}
		// a link that opens a window: in the default browser
		m_WebView->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
			[](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
				CComHeapPtr<WCHAR> uri;
				if (SUCCEEDED(args->get_Uri(&uri)) && uri)
					::ShellExecute(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
				args->put_Handled(TRUE);
				return S_OK;
			}).Get(), nullptr);
		// a page of Microsoft Learn: from where its text starts (not its site's banners and links)
		m_WebView->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
			[](ICoreWebView2* webView, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
				BOOL ok = FALSE;
				CComHeapPtr<WCHAR> uri;
				if (SUCCEEDED(args->get_IsSuccess(&ok)) && ok && SUCCEEDED(webView->get_Source(&uri)) && uri &&
					std::wstring_view((PCWSTR)uri).starts_with(L"https://learn.microsoft.com/"))
					webView->ExecuteScript(L"document.querySelector('main')?.scrollIntoView()", nullptr);
				return S_OK;
			}).Get(), nullptr);
		ApplyColors();
		CRect rc;
		GetClientRect(&rc);
		m_Controller->put_Bounds(rc);
		m_Controller->put_IsVisible(IsWindowVisible());
		m_Message.clear();
		Load();
	}

	// the page asked for, if it is not the one loaded (none: a page that says so)
	void Load() {
		if (!m_WebView || m_Url == m_Loaded)
			return;
		m_Loaded = m_Url;
		if (!m_Url.empty()) {
			m_WebView->Navigate(m_Url.c_str());
			return;
		}
		auto color = [](COLORREF c) { return std::format(L"#{:02x}{:02x}{:02x}", GetRValue(c), GetGValue(c), GetBValue(c)); };
		// (and a search of the documentation for it, in this window)
		auto text = m_Name.empty() ? std::wstring() : std::format(L"There is no known page of documentation for {}.<p>"
			L"<a href=\"{}\" style=\"color: {}\">Search Microsoft Learn for {}</a>", Escaped(m_Name), SearchUrl(m_Name),
			m_Dark ? L"#6cb4ff" : L"#0067b8", Escaped(m_Name));
		auto html = std::format(L"<html><body style=\"font-family: Segoe UI; font-size: 10pt; background: {}; color: {}\">{}</body></html>",
			color(Background()), color(Foreground()), text);
		m_WebView->NavigateToString(html.c_str());
	}

	// the pages' colors (those that have both) as the application's
	void ApplyColors() {
		Microsoft::WRL::ComPtr<ICoreWebView2_13> webView;
		Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile;
		if (m_WebView && SUCCEEDED(m_WebView.As(&webView)) && SUCCEEDED(webView->get_Profile(&profile)))
			profile->put_PreferredColorScheme(m_Dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
		Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller;
		if (m_Controller && SUCCEEDED(m_Controller.As(&controller))) {
			auto c = Background();
			controller->put_DefaultBackgroundColor({ 255, GetRValue(c), GetGValue(c), GetBValue(c) });
		}
	}

	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&) {
		if (m_Controller) {
			CRect rc;
			GetClientRect(&rc);
			m_Controller->put_Bounds(rc);
		}
		Invalidate();
		return 0;
	}

	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&) {
		CPaintDC dc(m_hWnd);
		CRect rc;
		GetClientRect(&rc);
		dc.FillSolidRect(&rc, Background());
		if (!m_Controller && !m_Message.empty()) {
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(Foreground());
			auto font = dc.SelectFont(AtlGetDefaultGuiFont());
			rc.DeflateRect(8, 8);
			dc.DrawText(m_Message.c_str(), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
			dc.SelectFont(font);
		}
		return 0;
	}

	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}

	LRESULT OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
		if (id != TimerLoad) {
			handled = FALSE;
			return 0;
		}
		KillTimer(TimerLoad);
		Load();
		return 0;
	}

	LRESULT OnShowWindow(UINT, WPARAM shown, LPARAM, BOOL& handled) {
		if (m_Controller)
			m_Controller->put_IsVisible(shown ? TRUE : FALSE);
		handled = FALSE;
		return 0;
	}

	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
		if (m_Controller)
			m_Controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
		return 0;
	}

	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
		if (m_Controller)
			m_Controller->Close();
		m_WebView.Reset();
		m_Controller.Reset();
		handled = FALSE;
		return 0;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_Controller;
	Microsoft::WRL::ComPtr<ICoreWebView2> m_WebView;
	std::wstring m_Url, m_Name;
	std::wstring m_Loaded{ L"\n" };	// (the page loaded: none yet)
	std::wstring m_Message;		// shown until there is a browser (it is starting, it cannot be)
	std::wstring m_DataFolder;
	bool m_Starting{ false };
	bool m_Dark{ false };
};
