#define WIN32_LEAN_AND_MEAN

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlctrls.h>
#include <atlstr.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "DiffView.h"

// WTLHelper's dark mode (its Detours and WIL come from vcpkg, set up for x64 only)
#ifdef _M_X64
#define DEMO_WTLHELPER
#include <WTLHelper.h>
#endif

#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

CAppModule _Module;

// ---- Control and command IDs ------------------------------------------------

enum {
	IDC_DIFF = 101,

	ID_FILE_OPENLEFT = 201,
	ID_FILE_OPENRIGHT,
	ID_FILE_SAMPLE,
	ID_FILE_LARGE,
	ID_FILE_COPYDIFF,
	ID_FILE_EXIT,

	ID_VIEW_SIDEBYSIDE = 301,
	ID_VIEW_UNIFIED,
	ID_VIEW_CONTEXTALL,
	ID_VIEW_CONTEXT3,
	ID_VIEW_CONTEXT10,
	ID_VIEW_NUMBERS,
	ID_VIEW_COLORS,
	ID_VIEW_DARKMODE,
	ID_VIEW_OVERVIEW,
	ID_VIEW_EXPANDALL,
	ID_VIEW_HIGHCONTRAST,

	ID_COMPARE_IGNORECASE = 401,
	ID_COMPARE_IGNORESPACECHANGE,
	ID_COMPARE_IGNORESPACE,
	ID_COMPARE_INTRALINE,

	ID_GO_NEXT = 501,
	ID_GO_PREV,

	ID_DEMO_COPY = 601,
	ID_DEMO_SELECTALL,

	ID_MERGE_ENABLE = 701,
	ID_MERGE_ASK,
	ID_MERGE_UNDO,
	ID_MERGE_BACKGROUND,
};

// ---- State --------------------------------------------------------------------

static CDiffViewCtrl g_diff;
static HWND g_main;
static CString g_paths[2];
static bool g_customColors;
static bool g_askMerge;
static bool g_highContrast;         // View > High Contrast: the control's drawing for it, without Windows' setting
static ULONGLONG g_compareStart;    // the tick a compare started (shown when it's done)

static const DFVCOLORS g_colors = {
	RGB(0xFB, 0xF8, 0xF1), RGB(0x30, 0x30, 0x40), RGB(0xEE, 0xE8, 0xDA), RGB(0x9A, 0x90, 0x80),
	RGB(0xF6, 0xD9, 0xD0), RGB(0xE9, 0xA8, 0x98), RGB(0xD8, 0xEC, 0xF4), RGB(0x9C, 0xCB, 0xE0),
	RGB(0xD8, 0xD0, 0xC0), RGB(0xEC, 0xE4, 0xF2), RGB(0x78, 0x68, 0x90), RGB(0xE2, 0xDA, 0xC8), RGB(0x40, 0x38, 0x30),
	RGB(0x5A, 0x4A, 0x7A), RGB(0xFF, 0xFF, 0xFF), RGB(0xD8, 0xD0, 0xE0), RGB(0x30, 0x30, 0x40),
};

// ---- Texts ------------------------------------------------------------------------

static const wchar_t g_sampleLeft[] =
	L"#include <vector>\n"
	L"#include <string>\n"
	L"\n"
	L"// Counts the words in a line.\n"
	L"int CountWords(const std::wstring& line) {\n"
	L"\tint count = 0;\n"
	L"\tbool inWord = false;\n"
	L"\tfor (auto c : line) {\n"
	L"\t\tif (iswspace(c)) {\n"
	L"\t\t\tinWord = false;\n"
	L"\t\t}\n"
	L"\t\telse if (!inWord) {\n"
	L"\t\t\tinWord = true;\n"
	L"\t\t\tcount++;\n"
	L"\t\t}\n"
	L"\t}\n"
	L"\treturn count;\n"
	L"}\n"
	L"\n"
	L"// The total over all the lines.\n"
	L"int Total(const std::vector<std::wstring>& lines) {\n"
	L"\tint total = 0;\n"
	L"\tfor (auto& line : lines)\n"
	L"\t\ttotal += CountWords(line);\n"
	L"\treturn total;\n"
	L"}\n"
	L"\n"
	L"void Unused() {\n"
	L"\t// nothing here\n"
	L"}\n"
	L"\n"
	L"int main() {\n"
	L"\tstd::vector<std::wstring> lines{ L\"one two\", L\"three\" };\n"
	L"\treturn Total(lines);\n"
	L"}\n";

static const wchar_t g_sampleRight[] =
	L"#include <string>\n"
	L"#include <vector>\n"
	L"#include <cwctype>\n"
	L"\n"
	L"// Counts the words in a line of text.\n"
	L"size_t CountWords(std::wstring_view line) {\n"
	L"\tsize_t count = 0;\n"
	L"\tbool inWord = false;\n"
	L"\tfor (auto c : line) {\n"
	L"\t\tif (iswspace(c)) {\n"
	L"\t\t\tinWord = false;\n"
	L"\t\t}\n"
	L"\t\telse if (!inWord) {\n"
	L"\t\t\tinWord = true;\n"
	L"\t\t\tcount++;\n"
	L"\t\t}\n"
	L"\t}\n"
	L"\treturn count;\n"
	L"}\n"
	L"\n"
	L"// The total over all the lines.\n"
	L"size_t Total(const std::vector<std::wstring>& lines) {\n"
	L"\tsize_t total = 0;\n"
	L"\tfor (auto& line : lines)\n"
	L"\t\ttotal += CountWords(line);\n"
	L"\treturn total;\n"
	L"}\n"
	L"\n"
	L"// The longest line's word count.\n"
	L"size_t Longest(const std::vector<std::wstring>& lines) {\n"
	L"\tsize_t longest = 0;\n"
	L"\tfor (auto& line : lines)\n"
	L"\t\tlongest = std::max(longest, CountWords(line));\n"
	L"\treturn longest;\n"
	L"}\n"
	L"\n"
	L"int main() {\n"
	L"\tstd::vector<std::wstring> lines{ L\"one two\", L\"three four five\" };\n"
	L"\treturn (int)(Total(lines) + Longest(lines));\n"
	L"}\n";

// A file as text: UTF-16 with a BOM, else UTF-8 (with or without one), else the ANSI code page.
static bool ReadText(LPCWSTR path, std::wstring& text) {
	HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size{};
	::GetFileSizeEx(file, &size);
	if (size.QuadPart > 512 * 1024 * 1024) {
		::CloseHandle(file);
		return false;
	}
	std::vector<char> bytes((size_t)size.QuadPart);
	DWORD read = 0;
	bool ok = bytes.empty() || ::ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
	::CloseHandle(file);
	if (!ok)
		return false;
	bytes.resize(read);
	if (bytes.size() >= 2 && (BYTE)bytes[0] == 0xFF && (BYTE)bytes[1] == 0xFE) {
		text.assign((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
		return true;
	}
	size_t skip = bytes.size() >= 3 && (BYTE)bytes[0] == 0xEF && (BYTE)bytes[1] == 0xBB && (BYTE)bytes[2] == 0xBF ? 3 : 0;
	auto convert = [&](UINT cp, DWORD flags) {
		int n = ::MultiByteToWideChar(cp, flags, bytes.data() + skip, (int)(bytes.size() - skip), nullptr, 0);
		if (n <= 0)
			return bytes.size() == skip;
		text.resize(n);
		::MultiByteToWideChar(cp, flags, bytes.data() + skip, (int)(bytes.size() - skip), text.data(), n);
		return true;
	};
	text.clear();
	return convert(CP_UTF8, MB_ERR_INVALID_CHARS) || convert(CP_ACP, 0);
}

static void UpdateTitle() {
	DFVINFO info{};
	g_diff.GetInfo(&info);
	CString title = L"DiffView Demo - ";
	bool left = g_diff.IsModified(false), right = g_diff.IsModified(true);
	if (left || right)
		title.AppendFormat(L"%s modified - ", left && right ? L"both sides" : left ? L"left" : L"right");
	if (g_diff.IsComparing())
		title += L"comparing...";
	else if (info.changes == 0)
		title.AppendFormat(L"identical (%zu lines)", info.leftLines);
	else
		title.AppendFormat(L"%zu changes, %zu lines deleted, %zu inserted", info.changes, info.deleted, info.inserted);
	if (g_compareStart && !g_diff.IsComparing()) {
		title.AppendFormat(L" (%llu ms)", ::GetTickCount64() - g_compareStart);
		g_compareStart = 0;
	}
	// where the caret is
	auto caret = g_diff.GetCaret();
	title.AppendFormat(L" - %s line %zu, column %zu", caret.right ? L"right" : L"left", caret.line + 1, caret.column + 1);
	int change = g_diff.GetCurrentChange();
	if (change >= 0)
		title.AppendFormat(L", change %d of %zu", change + 1, info.changes);
	if (g_diff.HasSelection())
		title.AppendFormat(L", %d characters selected", g_diff.GetSelectedText().GetLength());
	::SetWindowTextW(g_main, title);
}

static void LoadSample() {
	g_paths[0] = g_paths[1] = L"";
	g_diff.SetTitles(L"Words.cpp (old)", L"Words.cpp (new)");
	g_diff.SetTexts(g_sampleLeft, g_sampleRight);
	UpdateTitle();
}

static void LoadLarge() {
	// 200,000 lines with an edit every few hundred
	std::wstring left, right;
	left.reserve(200000 * 40);
	right.reserve(200000 * 40);
	unsigned seed = 7;
	auto random = [&]() { seed = seed * 1103515245 + 12345; return (seed >> 16) & 0x7fff; };
	for (int i = 0; i < 200000; i++) {
		wchar_t line[96];
		swprintf_s(line, L"%06d\tthe quick brown fox jumps over the lazy dog\n", i);
		left += line;
		switch (random() % 400) {
			case 0:
				break;   // deleted
			case 1:
				swprintf_s(line, L"%06d\tthe quick red fox jumps over the lazy cat\n", i);
				right += line;
				break;
			case 2:
				right += line;
				right += L"\t\tan inserted line\n";
				break;
			default:
				right += line;
				break;
		}
	}
	g_paths[0] = g_paths[1] = L"";
	g_diff.SetTitles(L"200,000 lines", L"edited");
	g_compareStart = ::GetTickCount64();
	g_diff.SetTexts(left.c_str(), right.c_str(), (int)left.size(), (int)right.size());
	UpdateTitle();   // the time now, or when DFVN_DIFFDONE comes
}

static void LoadFile(int side, LPCWSTR path) {
	std::wstring text;
	if (!ReadText(path, text)) {
		::MessageBoxW(g_main, L"The file can't be read.", path, MB_ICONERROR);
		return;
	}
	g_paths[side] = path;
	g_diff.SetText(side == 1, text.c_str(), (int)text.size());
	g_diff.SetTitles(g_paths[0].IsEmpty() ? L"" : ::PathFindFileNameW(g_paths[0]), g_paths[1].IsEmpty() ? L"" : ::PathFindFileNameW(g_paths[1]));
	UpdateTitle();
}

static void OpenFile(int side) {
	wchar_t path[MAX_PATH] = L"";
	OPENFILENAMEW ofn{ sizeof(ofn) };
	ofn.hwndOwner = g_main;
	ofn.lpstrFilter = L"All Files (*.*)\0*.*\0Text Files\0*.txt;*.log;*.cpp;*.h;*.cs;*.md;*.xml;*.json\0";
	ofn.lpstrFile = path;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = side ? L"Open the Right File" : L"Open the Left File";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (::GetOpenFileNameW(&ofn))
		LoadFile(side, path);
}

static void CopyToClipboard(const CString& text) {
	if (!::OpenClipboard(g_main))
		return;
	::EmptyClipboard();
	size_t bytes = (text.GetLength() + 1) * sizeof(wchar_t);
	if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
		memcpy(::GlobalLock(mem), (LPCWSTR)text, bytes);
		::GlobalUnlock(mem);
		if (!::SetClipboardData(CF_UNICODETEXT, mem))
			::GlobalFree(mem);
	}
	::CloseClipboard();
}

// ---- Commands ----------------------------------------------------------------------

static void UpdateChecks(HMENU menu) {
	auto check = [&](UINT id, bool on) { ::CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
	bool unified = g_diff.GetLayout() == DFVL_UNIFIED;
	::CheckMenuRadioItem(menu, ID_VIEW_SIDEBYSIDE, ID_VIEW_UNIFIED, unified ? ID_VIEW_UNIFIED : ID_VIEW_SIDEBYSIDE, MF_BYCOMMAND);
	int context = g_diff.GetContextLines();
	::CheckMenuRadioItem(menu, ID_VIEW_CONTEXTALL, ID_VIEW_CONTEXT10,
		context == 3 ? ID_VIEW_CONTEXT3 : context == 10 ? ID_VIEW_CONTEXT10 : ID_VIEW_CONTEXTALL, MF_BYCOMMAND);
	check(ID_VIEW_NUMBERS, g_diff.AreLineNumbersShown());
	check(ID_VIEW_COLORS, g_customColors);
	check(ID_VIEW_OVERVIEW, g_diff.IsOverviewShown());
	check(ID_VIEW_HIGHCONTRAST, g_highContrast);
	check(ID_MERGE_ENABLE, g_diff.GetMergeFlags() != 0);
	check(ID_MERGE_ASK, g_askMerge);
	check(ID_MERGE_BACKGROUND, g_diff.GetBackgroundThreshold() != SIZE_MAX);
	::EnableMenuItem(menu, ID_MERGE_UNDO, MF_BYCOMMAND | (g_diff.CanUndo() ? MF_ENABLED : MF_GRAYED));
	UINT options = g_diff.GetOptions();
	check(ID_COMPARE_IGNORECASE, (options & DFVF_IGNORECASE) != 0);
	check(ID_COMPARE_IGNORESPACECHANGE, (options & DFVF_IGNORESPACECHANGE) != 0);
	check(ID_COMPARE_IGNORESPACE, (options & DFVF_IGNORESPACE) != 0);
	check(ID_COMPARE_INTRALINE, (options & DFVF_NOINTRALINE) == 0);
#ifdef DEMO_WTLHELPER
	check(ID_VIEW_DARKMODE, WTLHelper::IsDarkMode());
#endif
}

static void ToggleOption(UINT flag) {
	g_diff.SetOptions(g_diff.GetOptions() ^ flag);
	UpdateTitle();
}

static void OnCommand(HWND hwnd, UINT id) {
	switch (id) {
		case ID_FILE_OPENLEFT: OpenFile(0); break;
		case ID_FILE_OPENRIGHT: OpenFile(1); break;
		case ID_FILE_SAMPLE: LoadSample(); break;
		case ID_FILE_LARGE: LoadLarge(); break;
		case ID_FILE_COPYDIFF:
			CopyToClipboard(g_diff.GetUnifiedDiff(3, g_paths[0].IsEmpty() ? L"a" : (LPCWSTR)g_paths[0], g_paths[1].IsEmpty() ? L"b" : (LPCWSTR)g_paths[1]));
			break;
		case ID_FILE_EXIT:
			::DestroyWindow(hwnd);
			return;
		case ID_VIEW_SIDEBYSIDE: g_diff.SetLayout(DFVL_SIDEBYSIDE); break;
		case ID_VIEW_UNIFIED: g_diff.SetLayout(DFVL_UNIFIED); break;
		case ID_VIEW_CONTEXTALL: g_diff.SetContextLines(DFV_ALLCONTEXT); break;
		case ID_VIEW_CONTEXT3: g_diff.SetContextLines(3); break;
		case ID_VIEW_CONTEXT10: g_diff.SetContextLines(10); break;
		case ID_VIEW_NUMBERS: g_diff.ShowLineNumbers(!g_diff.AreLineNumbersShown()); break;
		case ID_VIEW_COLORS:
			g_customColors = !g_customColors;
			g_diff.SetColors(g_customColors ? &g_colors : nullptr);
			break;
#ifdef DEMO_WTLHELPER
		case ID_VIEW_DARKMODE:
			WTLHelper::SwitchToMode(WTLHelper::IsDarkMode() ? DarkModeKind::Classic : DarkModeKind::Dark, hwnd);
			break;
#endif
		case ID_COMPARE_IGNORECASE: ToggleOption(DFVF_IGNORECASE); break;
		case ID_COMPARE_IGNORESPACECHANGE: ToggleOption(DFVF_IGNORESPACECHANGE); break;
		case ID_COMPARE_IGNORESPACE: ToggleOption(DFVF_IGNORESPACE); break;
		case ID_COMPARE_INTRALINE: ToggleOption(DFVF_NOINTRALINE); break;
		case ID_GO_NEXT:
		case ID_GO_PREV:
			g_diff.NextChange(id == ID_GO_NEXT);
			UpdateTitle();
			break;
		case ID_VIEW_OVERVIEW: g_diff.ShowOverview(!g_diff.IsOverviewShown()); break;
		case ID_VIEW_EXPANDALL: g_diff.ExpandAll(); break;
		case ID_VIEW_HIGHCONTRAST:
			g_highContrast = !g_highContrast;
			g_diff.ForceHighContrast(g_highContrast ? 1 : -1);
			break;
		case ID_DEMO_COPY: g_diff.Copy(); break;
		case ID_DEMO_SELECTALL: g_diff.SelectAll(); UpdateTitle(); break;
		case ID_MERGE_ENABLE: g_diff.EnableMerge(g_diff.GetMergeFlags() ? 0 : DFVMF_TORIGHT | DFVMF_TOLEFT); break;
		case ID_MERGE_ASK: g_askMerge = !g_askMerge; break;
		case ID_MERGE_UNDO: g_diff.Undo(); UpdateTitle(); break;
		case ID_MERGE_BACKGROUND: g_diff.SetBackgroundThreshold(g_diff.GetBackgroundThreshold() == SIZE_MAX ? 100000 : SIZE_MAX); break;
	}
	UpdateChecks(::GetMenu(hwnd));
}

static void ShowContextMenu(HWND hwnd, const NMDFVCONTEXTMENU* nm) {
	HMENU menu = ::CreatePopupMenu();
	::AppendMenuW(menu, MF_STRING | (g_diff.HasSelection() ? 0 : MF_GRAYED), ID_DEMO_COPY, L"&Copy");
	::AppendMenuW(menu, MF_STRING, ID_DEMO_SELECTALL, L"Select &All");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, ID_GO_NEXT, L"&Next Change");
	::AppendMenuW(menu, MF_STRING, ID_GO_PREV, L"&Previous Change");
	::AppendMenuW(menu, MF_STRING, ID_VIEW_EXPANDALL, L"&Expand All Folds");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, ID_FILE_COPYDIFF, L"Copy &Unified Diff");
	::TrackPopupMenu(menu, TPM_RIGHTBUTTON, nm->pt.x, nm->pt.y, 0, hwnd, nullptr);
	::DestroyMenu(menu);
}

// ---- Main window ------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
			g_main = hwnd;
			g_diff.Create(hwnd, CWindow::rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_DIFF);
			g_diff.SetContextLines(DFV_ALLCONTEXT);
			LoadSample();
			::DragAcceptFiles(hwnd, TRUE);
			return 0;

		case WM_SIZE:
			g_diff.SetWindowPos(nullptr, 0, 0, LOWORD(lParam), HIWORD(lParam), SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;

		case WM_SETFOCUS:
			g_diff.SetFocus();
			return 0;

		case WM_COMMAND:
			OnCommand(hwnd, LOWORD(wParam));
			return 0;

		case WM_NOTIFY: {
			auto hdr = (NMHDR*)lParam;
			if (hdr->idFrom != IDC_DIFF)
				break;
			switch (hdr->code) {
				case DFVN_MERGE: {
					// a non-zero return stops the merge
					if (!g_askMerge)
						return 0;
					auto nm = (NMDFVMERGE*)lParam;
					CString text;
					text.Format(L"Copy change %zu's %zu line(s) over the %s side's %zu?", nm->change + 1,
						nm->toRight ? nm->lines.leftCount : nm->lines.rightCount, nm->toRight ? L"right" : L"left",
						nm->toRight ? nm->lines.rightCount : nm->lines.leftCount);
					return ::MessageBoxW(hwnd, text, L"Merge", MB_YESNO | MB_ICONQUESTION) == IDYES ? 0 : 1;
				}
				case DFVN_TEXTCHANGED:
				case DFVN_DIFFDONE:
					UpdateTitle();
					UpdateChecks(::GetMenu(hwnd));
					break;
				case DFVN_SELCHANGED:
				case DFVN_FOLDEXPANDED:
					UpdateTitle();
					break;
				case DFVN_CONTEXTMENU:
					ShowContextMenu(hwnd, (NMDFVCONTEXTMENU*)lParam);
					break;
			}
			return 0;
		}

		case WM_DROPFILES: {
			// one file: the right side; two: both
			HDROP drop = (HDROP)wParam;
			UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
			wchar_t path[MAX_PATH];
			for (UINT i = 0; i < std::min(count, 2u); i++) {
				::DragQueryFileW(drop, i, path, MAX_PATH);
				LoadFile(count == 1 ? 1 : (int)i, path);
			}
			::DragFinish(drop);
			return 0;
		}

		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---- Entry point ------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	::InitCommonControlsEx(&icc);

	_Module.Init(nullptr, hInstance);
#ifdef DEMO_WTLHELPER
	WTLHelper::InitDarkMode(DarkModeKind::Classic);
#endif

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"DiffViewDemoWnd";
	wc.hIcon = ::LoadIcon(nullptr, IDI_APPLICATION);
	::RegisterClassExW(&wc);

	HMENU hFile = ::CreatePopupMenu();
	::AppendMenuW(hFile, MF_STRING, ID_FILE_OPENLEFT, L"Open &Left...\tCtrl+1");
	::AppendMenuW(hFile, MF_STRING, ID_FILE_OPENRIGHT, L"Open &Right...\tCtrl+2");
	::AppendMenuW(hFile, MF_STRING, ID_FILE_SAMPLE, L"&Sample");
	::AppendMenuW(hFile, MF_STRING, ID_FILE_LARGE, L"&Large (200,000 Lines)");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_FILE_COPYDIFF, L"&Copy Unified Diff");
	::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hFile, MF_STRING, ID_FILE_EXIT, L"E&xit");

	HMENU hEdit = ::CreatePopupMenu();
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_COPY, L"&Copy\tCtrl+C");
	::AppendMenuW(hEdit, MF_STRING, ID_DEMO_SELECTALL, L"Select &All\tCtrl+A");

	HMENU hView = ::CreatePopupMenu();
	::AppendMenuW(hView, MF_STRING, ID_VIEW_SIDEBYSIDE, L"&Side by Side");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_UNIFIED, L"&Unified");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_VIEW_CONTEXTALL, L"&All Lines");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_CONTEXT3, L"&3 Lines of Context");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_CONTEXT10, L"&10 Lines of Context");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_VIEW_EXPANDALL, L"&Expand All Folds");
	::AppendMenuW(hView, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hView, MF_STRING, ID_VIEW_NUMBERS, L"Line &Numbers");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_OVERVIEW, L"&Overview");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_COLORS, L"&Custom Colors");
	::AppendMenuW(hView, MF_STRING, ID_VIEW_HIGHCONTRAST, L"&High Contrast Drawing");
#ifdef DEMO_WTLHELPER
	::AppendMenuW(hView, MF_STRING, ID_VIEW_DARKMODE, L"&Dark Mode");
#endif

	HMENU hCompare = ::CreatePopupMenu();
	::AppendMenuW(hCompare, MF_STRING, ID_COMPARE_IGNORECASE, L"Ignore &Case");
	::AppendMenuW(hCompare, MF_STRING, ID_COMPARE_IGNORESPACECHANGE, L"Ignore &Whitespace Changes");
	::AppendMenuW(hCompare, MF_STRING, ID_COMPARE_IGNORESPACE, L"Ignore &All Whitespace");
	::AppendMenuW(hCompare, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hCompare, MF_STRING, ID_COMPARE_INTRALINE, L"Mark Changed &Words");

	HMENU hMerge = ::CreatePopupMenu();
	::AppendMenuW(hMerge, MF_STRING, ID_MERGE_ENABLE, L"&Merge Arrows (Alt+Right, Alt+Left)");
	::AppendMenuW(hMerge, MF_STRING, ID_MERGE_ASK, L"&Ask Before Merging");
	::AppendMenuW(hMerge, MF_STRING, ID_MERGE_UNDO, L"&Undo\tCtrl+Z");
	::AppendMenuW(hMerge, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(hMerge, MF_STRING, ID_MERGE_BACKGROUND, L"Compare Large Texts in the &Background");

	HMENU hGo = ::CreatePopupMenu();
	::AppendMenuW(hGo, MF_STRING, ID_GO_NEXT, L"&Next Change\tF7");
	::AppendMenuW(hGo, MF_STRING, ID_GO_PREV, L"&Previous Change\tShift+F7");

	HMENU hMenu = ::CreateMenu();
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEdit, L"&Edit");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hCompare, L"&Compare");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMerge, L"&Merge");
	::AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hGo, L"&Go");

	ACCEL accels[] = {
		{ FVIRTKEY | FCONTROL, '1', ID_FILE_OPENLEFT },
		{ FVIRTKEY | FCONTROL, '2', ID_FILE_OPENRIGHT },
		{ FVIRTKEY, VK_F7, ID_GO_NEXT },
		{ FVIRTKEY | FSHIFT, VK_F7, ID_GO_PREV },
	};
	HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)std::size(accels));

	HWND hwnd = ::CreateWindowExW(0, L"DiffViewDemoWnd", L"DiffView Demo", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760, nullptr, hMenu, hInstance, nullptr);
	UpdateChecks(hMenu);

	// two paths on the command line: compare them
	int argc = 0;
	if (auto argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc)) {
		if (argc >= 3) {
			LoadFile(0, argv[1]);
			LoadFile(1, argv[2]);
		}
		::LocalFree(argv);
	}
	::ShowWindow(hwnd, nCmdShow);

	MSG msg;
	while (::GetMessage(&msg, nullptr, 0, 0)) {
		if (!::TranslateAccelerator(hwnd, hAccel, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyAcceleratorTable(hAccel);
	_Module.Term();
	return 0;
}
