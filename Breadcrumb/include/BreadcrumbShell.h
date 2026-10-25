#pragma once

// A breadcrumb bar over the shell's namespace, as Explorer's: the folders from the desktop down to a location, each
// segment's list its subfolders, the root chevron's the desktop's, the system's icons, typed paths parsed by the shell
// (file system paths, "shell:Downloads", "::{CLSID}", environment variables), suggestions from the folders, and the
// location's editing name to edit. The app passes the bar's notifications to HandleNotify (the parent's WM_NOTIFY) and
// learns where it went from OnNavigated. COM must be initialized on the thread (apartment threaded).

#include "Breadcrumb.h"

#include <shlobj.h>

#include <functional>
#include <memory>

class CBreadcrumbShell {
public:
	explicit CBreadcrumbShell(CBreadcrumbCtrl& bar);
	~CBreadcrumbShell();

	BOOL    NavigateTo(PCIDLIST_ABSOLUTE pidl);                   // a folder; FALSE if it isn't one
	BOOL    NavigateTo(LPCWSTR path);                             // parsed by the shell (environment variables expanded)
	PCIDLIST_ABSOLUTE GetLocation() const;                        // nullptr before the first NavigateTo

	// The bar's notifications (from the parent's WM_NOTIFY): TRUE if handled, the result to return in *result. It
	// handles BRCN_NAVIGATE, BRCN_GETCHILDREN, BRCN_GETSUGGESTIONS and BRCN_BEGINEDIT.
	BOOL    HandleNotify(const NMHDR* hdr, LRESULT* result);

	void    ShowHidden(bool show);                                // hidden folders in the lists; default: as Explorer is set
	// After the bar shows a location the user went to, or NavigateTo did.
	std::function<void(PCIDLIST_ABSOLUTE)> OnNavigated;

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};
