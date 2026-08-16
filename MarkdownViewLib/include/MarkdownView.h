#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>

#include <string>

#include "app.h"
#include "settings.h"

// WTL-based window class wrapping the Tinta viewer/editor.
//
// This is a message-map port of the original free-function WndProc in
// main_d2d.cpp: every handler is a thin pass-through to the same handleXxx()
// free functions that already take App& — no logic changes, only the
// dispatch mechanism differs. App is kept as an unmodified member (m_app)
// rather than flattened into this class, since every module function
// already takes App& explicitly.
//
// The public API below is a thin host-facing surface over the same App
// state/free functions the message handlers already use — it does not
// introduce new behavior, only a way to drive it programmatically instead
// of through synthetic keyboard/mouse input.
class CMarkdownView : public CWindowImpl<CMarkdownView> {
public:
    DECLARE_WND_CLASS(_T("MarkdownView"))

    App& GetApp() { return m_app; }

    // Registers a sink for state-change notifications (title, dirty, zoom,
    // theme). Not owned; pass nullptr to unregister. Safe to call before or
    // after Create().
    void SetHost(IMarkdownViewHost* host) { m_app.host = host; }

    // --- Document loading ---
    bool LoadFile(const std::wstring& path);
    bool LoadText(const std::wstring& content);

    // --- Theme ---
    void SetTheme(int themeIndex);
    int GetTheme() const { return m_app.currentThemeIndex; }

    // --- Zoom (same [0.5, 3.0] range and text-format rebake as Ctrl+scroll) ---
    void SetZoom(float factor);
    float GetZoom() const { return m_app.zoomFactor; }
    void ZoomIn() { SetZoom(m_app.zoomFactor + 0.1f); }
    void ZoomOut() { SetZoom(m_app.zoomFactor - 0.1f); }

    void ShowStats(bool show) { m_app.showStats = show; if (m_hWnd) ::InvalidateRect(m_hWnd, nullptr, FALSE); }

    // --- Selection ---
    std::wstring GetSelectedText() const { return m_app.selectedText; }
    bool HasSelection() const { return m_app.hasSelection; }

    // --- Search (programmatic equivalent of the F / Ctrl+F flow) ---
    void Find(const std::wstring& query);
    void FindNext();
    void FindPrev();
    void CloseFind();

    // --- Overlays ---
    void ToggleToc() { m_app.showToc = !m_app.showToc; m_app.tocAnimation = 0; if (m_hWnd) ::InvalidateRect(m_hWnd, nullptr, FALSE); }
    void ToggleFolderBrowser() { m_app.showFolderBrowser = !m_app.showFolderBrowser; m_app.folderBrowserAnimation = 0; if (m_hWnd) ::InvalidateRect(m_hWnd, nullptr, FALSE); }
    void ToggleHelp() { m_app.showHelp = !m_app.showHelp; m_app.helpAnimation = 0; if (m_hWnd) ::InvalidateRect(m_hWnd, nullptr, FALSE); }

    // --- Edit mode ---
    void EnterEditMode();
    void ExitEditMode();
    bool IsEditMode() const { return m_app.editMode; }
    bool IsDirty() const { return m_app.editorDirty; }
    bool Save();

    // --- Settings (host owns persistence; the control never touches the
    // registry/%APPDATA% itself — see settings.cpp) ---
    void SetSettings(const Settings& settings);
    Settings GetSettings() const;

    BEGIN_MSG_MAP(CMarkdownView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseHWheel)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_CHAR, OnChar)
        MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_APP_LAYOUT_CHUNK, OnLayoutChunk)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

private:
    App m_app;

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseHWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnChar(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDropFiles(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLayoutChunk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
};

