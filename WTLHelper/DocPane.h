#pragma once

#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <winhttp.h>
#include <string>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "winhttp")

//
// A page of documentation (by default, of Microsoft Learn) in the browser that Windows has (WebView2). It is started when
// the first page is asked for; a page is loaded after a pause (the arrow keys in a list pass many). Links stay in it, except
// those that open a window of their own: those open in the default browser. Its colors follow SetDarkMode.
// A name without a page (the metadata has none: the WDK's) is looked for in Microsoft Learn's search (in the background,
// once for each name): its reference page ("nf-fltkernel-fltregisterfilter") is shown, if there is one.
// Header-only: only those who include it need the WebView2 SDK (its loader is linked by whoever uses it).
//
class CDocPane : public CWindowImpl<CDocPane> {
public:
	DECLARE_WND_CLASS_EX(L"WTLHelperDocPane", 0, -1)

	// the page to show: 'url' (empty: none known: it is looked for, by the name)
	void Navigate(std::wstring url, std::wstring const& name) {
		m_Url = std::move(url);
		m_Name = name;
		m_LookingUp = false;
		if (m_Url.empty() && !m_Name.empty() && m_LookUp) {
			if (auto found = Found(m_Name))
				m_Url = *found;		// (empty: there is none)
			else
				StartLookUp();
		}
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
	// whether a name without a page is looked for (it is, unless this says no)
	void SetLookUp(bool lookUp) {
		m_LookUp = lookUp;
	}
	// the page found for a name (empty: none was, or it was not looked for yet)
	static std::wstring FoundPage(std::wstring const& name) {
		return Found(name).value_or(std::wstring());
	}

	BEGIN_MSG_MAP(CDocPane)
		MESSAGE_HANDLER(FoundMessage(), OnFound)
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

	// (a page looked for was found, or not: for the window that looks)
	static UINT FoundMessage() {
		static UINT const message = ::RegisterWindowMessage(L"WTLHelperDocPaneFound");
		return message;
	}
	// The pages looked for, by their names (empty: none was found), for all of them
	struct Cache {
		std::mutex Lock;
		std::unordered_map<std::wstring, std::wstring> Pages;
	};
	static Cache& Pages() {
		static Cache cache;
		return cache;
	}
	static std::optional<std::wstring> Found(std::wstring const& name) {
		auto& cache = Pages();
		std::lock_guard lock(cache.Lock);
		if (auto it = cache.Pages.find(name); it != cache.Pages.end())
			return it->second;
		return std::nullopt;
	}

	void StartLookUp() {
		m_LookingUp = true;
		std::thread([hWnd = m_hWnd, name = m_Name] {
			auto url = FindPage(name);
			{
				auto& cache = Pages();
				std::lock_guard lock(cache.Lock);
				cache.Pages[name] = url;
			}
			if (::IsWindow(hWnd))
				::PostMessage(hWnd, FoundMessage(), 0, 0);
		}).detach();
	}

	LRESULT OnFound(UINT, WPARAM, LPARAM, BOOL&) {
		// (the name it looks for now: another's may have been found)
		if (!m_LookingUp)
			return 0;
		if (auto found = Found(m_Name)) {
			m_LookingUp = false;
			m_Url = *found;
			Load();
		}
		return 0;
	}

	// The reference page of a name, of Microsoft Learn's search: one of the results whose address ends with it
	// (".../ddi/fltkernel/nf-fltkernel-fltregisterfilter": nf- a function, ns- a struct, ne- an enum...); empty if none is
	static std::wstring FindPage(std::wstring const& name) {
		std::wstring lower(name);
		for (auto& c : lower)
			c = (wchar_t)towlower(c);
		std::string body;
		auto session = ::WinHttpOpen(L"DocPane", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
			return {};
		::WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);
		if (auto connection = ::WinHttpConnect(session, L"learn.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0)) {
			// (a name of Windows has letters, digits and underscores only: nothing to escape)
			auto path = L"/api/search?search=" + name + L"&locale=en-us&%24top=10";
			if (auto request = ::WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
				if (::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
					::WinHttpReceiveResponse(request, nullptr)) {
					DWORD available = 0;
					while (::WinHttpQueryDataAvailable(request, &available) && available) {
						std::string chunk(available, '\0');
						DWORD read = 0;
						if (!::WinHttpReadData(request, chunk.data(), available, &read) || !read)
							break;
						body.append(chunk.data(), read);
					}
				}
				::WinHttpCloseHandle(request);
			}
			::WinHttpCloseHandle(connection);
		}
		::WinHttpCloseHandle(session);
		// "url":"https://learn.microsoft.com/en-us/.../nf-fltkernel-fltregisterfilter"
		std::string const key = "\"url\":\"";
		for (size_t at = body.find(key); at != std::string::npos; at = body.find(key, at + 1)) {
			auto start = at + key.size();
			auto end = body.find('"', start);
			if (end == std::string::npos)
				break;
			std::wstring url(body.begin() + start, body.begin() + end);
			auto segment = url.substr(url.find_last_of(L'/') + 1);
			if (segment.size() > 3 && segment[0] == L'n' && segment[2] == L'-' && segment.ends_with(L"-" + lower)) {
				// (in the reader's language: not en-us)
				if (auto locale = url.find(L"/en-us/"); locale != std::wstring::npos)
					url.erase(locale, 6);
				return url;
			}
		}
		return {};
	}

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
		// (what is shown: the page; else the name's text, that it is looked for or that there is none: as many as there are
		// names, all without a page)
		auto shown = !m_Url.empty() ? m_Url : (m_LookingUp ? L"\1?" : L"\1") + m_Name;
		if (!m_WebView || shown == m_Loaded)
			return;
		m_Loaded = shown;
		if (!m_Url.empty()) {
			m_WebView->Navigate(m_Url.c_str());
			return;
		}
		auto color = [](COLORREF c) { return std::format(L"#{:02x}{:02x}{:02x}", GetRValue(c), GetGValue(c), GetBValue(c)); };
		// (and a search of the documentation for it, in this window)
		auto text = m_Name.empty() ? std::wstring() : m_LookingUp ? std::format(L"Looking for the documentation of {}...", Escaped(m_Name)) :
			std::format(L"There is no known page of documentation for {}.<p>"
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
	bool m_LookUp{ true };
	bool m_LookingUp{ false };	// (the name's page is looked for)
};
