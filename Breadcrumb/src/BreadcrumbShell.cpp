#include "BreadcrumbShell.h"
#include "BreadcrumbModel.h"

#include <commoncontrols.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace {
	struct PidlFree {
		void operator()(PIDLIST_ABSOLUTE p) const {
			::ILFree(p);
		}
	};
	using Pidl = std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, PidlFree>;   // __unaligned on x64

	Pidl Clone(PCIDLIST_ABSOLUTE pidl) {
		return Pidl(pidl ? (PIDLIST_ABSOLUTE)::ILCloneFull(pidl) : nullptr);
	}

	std::wstring Name(PCIDLIST_ABSOLUTE pidl, SIGDN kind) {
		PWSTR s = nullptr;
		if (!pidl || FAILED(::SHGetNameFromIDList(pidl, kind, &s)))
			return {};
		std::wstring name = s;
		::CoTaskMemFree(s);
		return name;
	}

	std::wstring DisplayName(IShellFolder* folder, PCUITEMID_CHILD child, SHGDNF flags) {
		STRRET sr;
		wchar_t name[MAX_PATH * 2];
		if (FAILED(folder->GetDisplayNameOf(child, flags, &sr)) || FAILED(::StrRetToBufW(&sr, child, name, _countof(name))))
			return {};
		return name;
	}

	std::wstring Expand(LPCWSTR text) {
		wchar_t expanded[2048];
		DWORD n = ::ExpandEnvironmentStringsW(text, expanded, _countof(expanded));
		std::wstring s = n && n <= _countof(expanded) ? expanded : text;
		while (!s.empty() && iswspace(s.back()))
			s.pop_back();
		size_t i = 0;
		while (i < s.size() && iswspace(s[i]))
			i++;
		return s.substr(i);
	}

	struct Item {
		Pidl pidl;                 // absolute
		std::wstring name;
		int icon = -1;
		int child = -1;            // Subfolders': its relative ID's index
	};

	// by name as Explorer sorts them: ignoring case, numbers by their value ("2" before "10"); an order IShellFolder::
	// CompareIDs doesn't promise to keep consistent, which std::sort needs
	bool NameLess(const std::wstring& a, const std::wstring& b) {
		return ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), (int)a.size(),
			b.c_str(), (int)b.size(), nullptr, nullptr, 0) == CSTR_LESS_THAN;
	}

	// A merge sort whose loops are bounded by the ranges, whatever less answers: std::sort and std::stable_sort may run
	// past the ends when the order isn't consistent, which IShellFolder::CompareIDs doesn't promise.
	template<typename T, typename Less>
	void SafeSort(std::vector<T>& v, Less less) {
		std::vector<T> buffer;
		buffer.reserve(v.size());
		for (size_t width = 1; width < v.size(); width *= 2) {
			for (size_t lo = 0; lo < v.size(); lo += 2 * width) {
				size_t mid = std::min(lo + width, v.size()), hi = std::min(lo + 2 * width, v.size());
				size_t i = lo, j = mid;
				buffer.clear();
				while (i < mid && j < hi)
					buffer.push_back(std::move(less(v[j], v[i]) ? v[j++] : v[i++]));
				while (i < mid)
					buffer.push_back(std::move(v[i++]));
				while (j < hi)
					buffer.push_back(std::move(v[j++]));
				std::move(buffer.begin(), buffer.end(), v.begin() + lo);
			}
		}
	}

	// A folder's subfolders (zip files, folders to the shell, left out): file system folders by name, others (the
	// desktop's) as the folder sorts them. folder: nullptr or empty for the desktop.
	std::vector<Item> Subfolders(PCIDLIST_ABSOLUTE folder, SHGDNF nameFlags, bool hidden, HWND hwnd) {
		std::vector<Item> items;
		std::vector<PITEMID_CHILD> kids;   // by item, for CompareIDs
		CComPtr<IShellFolder> sf;
		if (!folder || ILIsEmpty(folder))
			::SHGetDesktopFolder(&sf);
		else
			::SHBindToObject(nullptr, folder, nullptr, IID_PPV_ARGS(&sf));
		if (!sf)
			return items;
		CComPtr<IEnumIDList> e;
		if (sf->EnumObjects(hwnd, SHCONTF_FOLDERS | (hidden ? SHCONTF_INCLUDEHIDDEN : 0), &e) != S_OK || !e)
			return items;
		bool fileSystem = true;
		for (PITEMID_CHILD child; e->Next(1, &child, nullptr) == S_OK;) {
			SFGAOF attrs = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM;
			if (SUCCEEDED(sf->GetAttributesOf(1, (PCUITEMID_CHILD_ARRAY)&child, &attrs)) && (attrs & SFGAO_FOLDER) && !(attrs & SFGAO_STREAM)) {
				fileSystem = fileSystem && (attrs & SFGAO_FILESYSTEM);
				Item item;
				item.name = DisplayName(sf, child, nameFlags);
				item.icon = ::SHMapPIDLToSystemImageListIndex(sf, child, nullptr);
				item.pidl.reset(folder && !ILIsEmpty(folder) ? (PIDLIST_ABSOLUTE)::ILCombine(folder, child) : (PIDLIST_ABSOLUTE)::ILCloneFull((PCIDLIST_ABSOLUTE)child));
				item.child = (int)kids.size();
				items.push_back(std::move(item));
				kids.push_back(child);
			}
			else
				::CoTaskMemFree(child);
		}
		if (fileSystem)
			SafeSort(items, [](auto& a, auto& b) { return NameLess(a.name, b.name); });
		else
			SafeSort(items, [&](auto& a, auto& b) { return (short)HRESULT_CODE(sf->CompareIDs(0, kids[a.child], kids[b.child])) < 0; });
		for (auto child : kids)
			::CoTaskMemFree(child);
		return items;
	}
}

struct CBreadcrumbShell::Impl {
	explicit Impl(CBreadcrumbCtrl& bar) : bar(bar) {
	}

	CBreadcrumbCtrl& bar;
	Pidl location;
	std::vector<Pidl> segments;    // the bar's, by lParam
	std::vector<Pidl> children;    // the open list's, by lParam
	bool hidden = false;
	CComPtr<IImageList> images;
};

CBreadcrumbShell::CBreadcrumbShell(CBreadcrumbCtrl& bar) : m_Impl(std::make_unique<Impl>(bar)) {
	SHELLSTATEW ss{};
	::SHGetSetSettings(&ss, SSF_SHOWALLOBJECTS, FALSE);
	m_Impl->hidden = ss.fShowAllObjects != 0;
	if (SUCCEEDED(::SHGetImageList(SHIL_SMALL, IID_PPV_ARGS(&m_Impl->images))))
		bar.SetImageList((HIMAGELIST)m_Impl->images.p);
}

CBreadcrumbShell::~CBreadcrumbShell() = default;

void CBreadcrumbShell::ShowHidden(bool show) {
	m_Impl->hidden = show;
}

PCIDLIST_ABSOLUTE CBreadcrumbShell::GetLocation() const {
	return m_Impl->location.get();
}

BOOL CBreadcrumbShell::NavigateTo(PCIDLIST_ABSOLUTE pidl) {
	auto& d = *m_Impl;
	if (!pidl)
		return FALSE;
	CComPtr<IShellItem> item;
	SFGAOF attrs = 0;
	if (FAILED(::SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item))) || FAILED(item->GetAttributes(SFGAO_FOLDER | SFGAO_STREAM, &attrs))
		|| !(attrs & SFGAO_FOLDER) || (attrs & SFGAO_STREAM))
		return FALSE;

	// the folders from the desktop's child down to it (the desktop alone if it's the desktop)
	std::vector<Pidl> chain;
	auto p = Clone(pidl);
	while (!ILIsEmpty(p.get())) {
		chain.insert(chain.begin(), Clone(p.get()));
		::ILRemoveLastID(p.get());
	}
	if (chain.empty())
		chain.push_back(std::move(p));

	std::vector<std::wstring> texts(chain.size());
	std::vector<BRCITEM> items(chain.size());
	for (size_t i = 0; i < chain.size(); i++) {
		SHFILEINFOW sfi{};
		sfi.dwAttributes = SFGAO_HASSUBFOLDER;
		::SHGetFileInfoW((LPCWSTR)chain[i].get(), 0, &sfi, sizeof(sfi),
			SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_DISPLAYNAME | SHGFI_ATTRIBUTES | SHGFI_ATTR_SPECIFIED);
		texts[i] = sfi.szDisplayName;
		items[i].pszText = texts[i].c_str();
		items[i].pszName = L"";   // the bar's path text isn't used: the shell's parsing name is the location
		items[i].iImage = sfi.iIcon;
		items[i].flags = (sfi.dwAttributes & SFGAO_HASSUBFOLDER) ? 0 : BRCIF_NOCHILDREN;
		items[i].lParam = (LPARAM)i;
	}
	auto parsing = Name(pidl, SIGDN_DESKTOPABSOLUTEPARSING);
	d.segments = std::move(chain);
	d.location = Clone(pidl);
	d.bar.SetPath(items.data(), (int)items.size(), parsing.c_str());
	if (OnNavigated)
		OnNavigated(d.location.get());
	return TRUE;
}

BOOL CBreadcrumbShell::NavigateTo(LPCWSTR path) {
	auto& d = *m_Impl;
	if (!path)
		return FALSE;
	auto text = Expand(path);
	if (text.empty())
		return FALSE;
	// the location's own editing name ("Control Panel\All Control Panel Items") isn't one the shell parses
	if (d.location && BrcSamePath(text, Name(d.location.get(), SIGDN_DESKTOPABSOLUTEEDITING))) {
		auto again = Clone(d.location.get());
		return NavigateTo(again.get());
	}
	PIDLIST_ABSOLUTE pidl = nullptr;
	if (SUCCEEDED(::SHParseDisplayName(text.c_str(), nullptr, &pidl, 0, nullptr))) {
		Pidl owned(pidl);
		return NavigateTo(owned.get());
	}
	// a name on the desktop: "This PC", "Network"
	for (auto& item : Subfolders(nullptr, SHGDN_NORMAL, true, d.bar.m_hWnd))
		if (BrcSamePath(item.name, text))
			return NavigateTo(item.pidl.get());
	return FALSE;
}

BOOL CBreadcrumbShell::HandleNotify(const NMHDR* hdr, LRESULT* result) {
	auto& d = *m_Impl;
	if (!hdr || hdr->hwndFrom != d.bar.m_hWnd)
		return FALSE;
	LRESULT dummy;
	if (!result)
		result = &dummy;
	*result = 0;
	switch (hdr->code) {
		case BRCN_NAVIGATE: {
			auto nm = (const NMBRCNAVIGATE*)hdr;
			Pidl target;
			switch (nm->source) {
				case BRCNS_SEGMENT:
				case BRCNS_UP:
					if (nm->lParam >= 0 && nm->lParam < (LPARAM)d.segments.size())
						target = Clone(d.segments[nm->lParam].get());
					break;
				case BRCNS_CHILD:
					if (nm->lParam >= 0 && nm->lParam < (LPARAM)d.children.size())
						target = Clone(d.children[nm->lParam].get());
					break;
				default:
					// typed, back or forward: the text the shell parses
					*result = NavigateTo(nm->pszPath) ? 0 : 1;
					return TRUE;
			}
			*result = target && NavigateTo(target.get()) ? 0 : 1;
			return TRUE;
		}
		case BRCN_GETCHILDREN: {
			auto nm = (const NMBRCCHILDREN*)hdr;
			PCIDLIST_ABSOLUTE folder = nullptr;
			if (nm->index >= 0) {
				if (nm->lParam < 0 || nm->lParam >= (LPARAM)d.segments.size())
					return TRUE;
				folder = d.segments[nm->lParam].get();
			}
			auto items = Subfolders(folder, SHGDN_NORMAL | SHGDN_INFOLDER, d.hidden, d.bar.m_hWnd);
			d.children.clear();
			d.children.reserve(items.size());
			for (auto& item : items) {
				d.bar.AddChild(item.name.c_str(), item.icon, (LPARAM)d.children.size());
				d.children.push_back(std::move(item.pidl));
			}
			return TRUE;
		}
		case BRCN_GETSUGGESTIONS: {
			auto nm = (const NMBRCSUGGEST*)hdr;
			// the parent's subfolders by their parsing names after the parent as typed; nothing typed: the drives
			Pidl folder;
			auto parent = Expand(nm->pszParent);
			PIDLIST_ABSOLUTE pidl = nullptr;
			if (parent.empty())
				::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidl);
			else
				::SHParseDisplayName(parent.c_str(), nullptr, &pidl, 0, nullptr);
			folder.reset(pidl);
			if (!folder)
				return TRUE;
			for (auto& item : Subfolders(folder.get(), SHGDN_INFOLDER | SHGDN_FORPARSING, d.hidden, d.bar.m_hWnd))
				if (!item.name.empty() && item.name.rfind(L"::", 0) != 0)   // a virtual folder's "::{CLSID}" isn't typed
					d.bar.AddSuggestion((std::wstring(nm->pszParent) + item.name).c_str());
			return TRUE;
		}
		case BRCN_BEGINEDIT:
			// as Explorer: the file system path, else the names ("Control Panel\All Control Panel Items")
			if (d.location)
				d.bar.SetEditText(Name(d.location.get(), SIGDN_DESKTOPABSOLUTEEDITING).c_str());
			return TRUE;
	}
	return FALSE;
}
