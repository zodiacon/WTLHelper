#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <wincodec.h>
#include <atlbase.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

#include "markdown.h"

using namespace qmd;

// Optional sink a host embedding CMarkdownView can register (App::host) to
// learn about state changes it can't otherwise observe (e.g. to update its
// own window caption or a tab label). All methods are no-ops by default so a
// host only needs to override what it cares about. Kept WTL/ATL-free so it
// can be included by any module without pulling in windowing headers.
class IMarkdownViewHost {
public:
    virtual ~IMarkdownViewHost() = default;
    virtual void OnTitleChanged(const wchar_t* title) {}
    virtual void OnDirtyStateChanged(bool dirty) {}
    virtual void OnZoomChanged(float zoom) {}
    virtual void OnThemeChanged(int themeIndex) {}
};

// Timing helpers
using Clock = std::chrono::high_resolution_clock;
inline int64_t usElapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

// Timer IDs (TIMER_FILE_WATCH=1 lives in file_utils.h)
#define TIMER_EDITOR_REPARSE 2
#define TIMER_CURSOR_BLINK 3
#define TIMER_NOTIFICATION 4
#define TIMER_ZOOM_APPLY 5

// Posted to continue an incomplete document layout in time-budgeted chunks
#define WM_APP_LAYOUT_CHUNK (WM_APP + 1)

// Startup metrics
struct StartupMetrics {
    int64_t windowInitUs = 0;
    int64_t d2dInitUs = 0;
    int64_t dwriteInitUs = 0;
    int64_t renderTargetUs = 0;
    int64_t fileLoadUs = 0;
    int64_t showWindowUs = 0;
    int64_t consoleInitUs = 0;
    int64_t totalStartupUs = 0;
};

// Syntax highlighting token types
enum class SyntaxTokenType { Plain, Keyword, String, Comment, Number, Function, TypeName, Operator, ControlFlow };

// Theme colors
struct D2DTheme {
    const wchar_t* name;
    const wchar_t* fontFamily;       // Main font
    const wchar_t* codeFontFamily;   // Monospace font
    bool isDark;
    D2D1_COLOR_F background;
    D2D1_COLOR_F text;
    D2D1_COLOR_F heading;
    D2D1_COLOR_F link;
    D2D1_COLOR_F code;
    D2D1_COLOR_F codeBackground;
    D2D1_COLOR_F blockquoteBorder;
    D2D1_COLOR_F accent;             // For UI elements
    // Syntax highlighting colors
    D2D1_COLOR_F syntaxKeyword;
    D2D1_COLOR_F syntaxString;
    D2D1_COLOR_F syntaxComment;
    D2D1_COLOR_F syntaxNumber;
    D2D1_COLOR_F syntaxFunction;
    D2D1_COLOR_F syntaxType;
    D2D1_COLOR_F syntaxControlFlow;
};

// Helper to create color from hex
inline D2D1_COLOR_F hexColor(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f,
        alpha
    );
}

// Forward declare App for dpi() helper
struct App;

// DPI scaling helper for UI chrome elements.
// Scales by contentScale only (not zoomFactor) so UI chrome tracks monitor DPI.
inline float dpi(const App& app, float value);

// Themes array (defined in themes.cpp)
extern const D2DTheme THEMES[];
extern const int THEME_COUNT;

// Persistent settings
struct Settings {
    int themeIndex = 5;          // Default to Midnight
    float zoomFactor = 1.0f;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowMaximized = false;
    bool hasAskedFileAssociation = false;
    bool editorShowPreview = true;
    bool editorWordWrap = false;
};

// Application state
struct App {
    // Win32
    HWND hwnd = nullptr;
    int width = 1024;
    int height = 768;
    bool running = true;

    // Optional host notification sink, set by the owning CMarkdownView via
    // SetHost(). Not owned; never deleted here.
    IMarkdownViewHost* host = nullptr;

    // Direct2D
    CComPtr<ID2D1Factory> d2dFactory;
    CComPtr<ID2D1HwndRenderTarget> renderTarget;
    CComPtr<ID2D1SolidColorBrush> brush;
    CComPtr<ID2D1DeviceContext> deviceContext;  // For color emoji rendering

    // WIC (Windows Imaging Component) for image loading
    CComPtr<IWICImagingFactory> wicFactory;

    // Image cache
    struct ImageEntry {
        CComPtr<ID2D1Bitmap> bitmap;
        int width = 0;
        int height = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, ImageEntry> imageCache;

    // Layout bitmaps (document coordinates)
    struct LayoutBitmap {
        CComPtr<ID2D1Bitmap> bitmap;
        D2D1_RECT_F destRect{};
    };
    std::vector<LayoutBitmap> layoutBitmaps;

    // DirectWrite
    CComPtr<IDWriteFactory> dwriteFactory;
    CComPtr<IDWriteFontFallback> fontFallback;  // For emoji font fallback
    CComPtr<IDWriteTextFormat> textFormat;
    CComPtr<IDWriteTextFormat> headingFormat;
    CComPtr<IDWriteTextFormat> codeFormat;
    CComPtr<IDWriteTextFormat> boldFormat;
    CComPtr<IDWriteTextFormat> italicFormat;
    CComPtr<IDWriteTextFormat> headingFormats[6];
    CComPtr<IDWriteTextAnalyzer> textAnalyzer;  // UAX#14 line-break analysis

    // Overlay text formats (cached)
    CComPtr<IDWriteTextFormat> searchTextFormat;
    CComPtr<IDWriteTextFormat> themeTitleFormat;
    CComPtr<IDWriteTextFormat> themeHeaderFormat;

    struct ThemePreviewFormats {
        CComPtr<IDWriteTextFormat> name;
        CComPtr<IDWriteTextFormat> preview;
        CComPtr<IDWriteTextFormat> code;
    };
    std::vector<ThemePreviewFormats> themePreviewFormats;

    // OpenType typography
    CComPtr<IDWriteTypography> bodyTypography;
    CComPtr<IDWriteTypography> codeTypography;

    // Folder browser text format
    CComPtr<IDWriteTextFormat> folderBrowserFormat;

    // TOC text formats
    CComPtr<IDWriteTextFormat> tocFormat;
    CComPtr<IDWriteTextFormat> tocFormatBold;

    // Markdown
    MarkdownParser parser;
    ElementPtr root;
    std::string currentFile;
    bool focusMermaidOnNextLayout = false;
    size_t parseTimeUs = 0;
    float contentHeight = 0;
    float contentWidth = 0;

    // State
    float scrollY = 0;
    float scrollX = 0;
    float targetScrollY = 0;
    float targetScrollX = 0;
    float contentScale = 1.0f;  // DPI scale
    float zoomFactor = 1.0f;    // User zoom (Ctrl+scroll)
    float appliedZoomFactor = 1.0f;  // zoomFactor last baked into text formats
    bool zoomApplyPending = false;   // TIMER_ZOOM_APPLY armed to coalesce zoom ticks
    bool darkMode = true;
    bool showStats = false;
    int currentThemeIndex = 5;  // Default to "Midnight" (first dark theme)
    D2DTheme theme = THEMES[5];

    // Theme chooser overlay
    bool showThemeChooser = false;
    int hoveredThemeIndex = -1;
    float themeChooserAnimation = 0.0f;  // 0 to 1 for fade in

    // Folder browser overlay
    bool showFolderBrowser = false;
    float folderBrowserAnimation = 0.0f;  // 0 to 1 for slide-in from left
    std::wstring folderBrowserPath;       // Current directory being browsed
    struct FolderItem {
        std::wstring name;
        bool isDirectory;
    };
    std::vector<FolderItem> folderItems;
    int hoveredFolderIndex = -1;
    float folderBrowserScroll = 0.0f;     // Scroll offset for folder list

    // Help overlay
    bool showHelp = false;
    float helpAnimation = 0.0f;
    float helpScroll = 0.0f;
    float helpContentHeight = 0.0f;   // Total content height (set during render)
    float helpVisibleHeight = 0.0f;   // Visible area height (set during render)
    float helpScrollbarTop = 0.0f;    // Scrollbar track top Y (set during render)
    bool helpScrollbarDragging = false;
    float helpScrollbarDragStartY = 0;
    float helpScrollbarDragStartScroll = 0;

    // Table of contents overlay
    bool showToc = false;
    float tocAnimation = 0.0f;  // 0 to 1 for slide-in from right
    struct HeadingInfo {
        std::wstring text;
        int level;       // 1-6
        float y;         // document Y coordinate
        std::string id;  // GitHub-style slug for anchor links
    };
    std::vector<HeadingInfo> headings;
    std::unordered_map<std::string, int> headingSlugCounts;
    int hoveredTocIndex = -1;
    float tocScroll = 0.0f;

    // Mouse
    bool mouseDown = false;
    int mouseX = 0;
    int mouseY = 0;

    // Vertical scrollbar
    bool scrollbarHovered = false;
    bool scrollbarDragging = false;
    float scrollbarDragStartY = 0;
    float scrollbarDragStartScroll = 0;

    // Horizontal scrollbar
    bool hScrollbarHovered = false;
    bool hScrollbarDragging = false;
    float hScrollbarDragStartX = 0;
    float hScrollbarDragStartScroll = 0;

    // Links - tracked during render for click detection
    struct LinkRect {
        D2D1_RECT_F bounds;
        std::string url;
    };
    std::vector<LinkRect> linkRects;
    std::string hoveredLink;

    // Code block info - tracked for copy button
    struct CodeBlockInfo {
        D2D1_RECT_F bounds;       // Full background rect in document coordinates
        std::wstring codeText;    // The code content
    };
    std::vector<CodeBlockInfo> codeBlocks;
    int hoveredCodeBlock = -1;

    // Text bounds - tracked for cursor changes and selection (document coordinates)
    struct TextRect {
        D2D1_RECT_F rect;
        size_t docStart = 0;   // Start position in docText
        size_t docLength = 0;  // Length in docText
    };
    std::vector<TextRect> textRects;

    // Line buckets for fast hit-testing/selection
    struct LineBucket {
        float top = 0;
        float bottom = 0;
        float minX = 0;
        float maxX = 0;
        std::vector<size_t> textRectIndices;
    };
    std::vector<LineBucket> lineBuckets;

    // Search match info
    struct SearchMatch {
        size_t textRectIndex;       // Index into textRects
        size_t startPos;            // Character offset in text
        size_t length;              // Match length
        D2D1_RECT_F highlightRect;  // Computed highlight bounds
    };
    std::vector<SearchMatch> searchMatches;
    bool overText = false;

    // Text selection
    bool selecting = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;
    bool hasSelection = false;
    std::wstring selectedText;

    // Multi-click selection (double/triple click)
    std::chrono::steady_clock::time_point lastClickTime;
    int clickCount = 0;
    int lastClickX = 0, lastClickY = 0;
    enum class SelectionMode { Normal, Word, Line } selectionMode = SelectionMode::Normal;
    // Anchor bounds for word/line selection (the original word/line that was clicked)
    float anchorLeft = 0, anchorRight = 0, anchorTop = 0, anchorBottom = 0;

    // Document text built during render (used for search/mapping)
    std::wstring docText;
    std::wstring docTextLower;

    // Cached space widths for common formats
    float spaceWidthText = 0.0f;
    float spaceWidthBold = 0.0f;
    float spaceWidthItalic = 0.0f;
    float spaceWidthCode = 0.0f;

    // Layout cache (document coordinates)
    struct LayoutTextRun {
        CComPtr<IDWriteTextLayout> layout;
        D2D1_POINT_2F pos{};
        D2D1_RECT_F bounds{};
        D2D1_COLOR_F color{};
        size_t docStart = 0;
        size_t docLength = 0;
        bool selectable = false;
    };
    struct LayoutRect {
        D2D1_RECT_F rect{};
        D2D1_COLOR_F color{};
    };
    struct LayoutLine {
        D2D1_POINT_2F p1{};
        D2D1_POINT_2F p2{};
        D2D1_COLOR_F color{};
        float stroke = 1.0f;
    };
    enum class LayoutShapeType {
        Rectangle,
        RoundedRectangle,
        Diamond,
        Stadium,
        Ellipse,
        Hexagon,
    };
    struct LayoutShape {
        LayoutShapeType type = LayoutShapeType::Rectangle;
        D2D1_RECT_F rect{};
        D2D1_COLOR_F fill{};
        D2D1_COLOR_F stroke{};
        float strokeWidth = 1.0f;
        float radius = 0.0f;
    };
    struct LayoutConnector {
        std::vector<D2D1_POINT_2F> points;
        D2D1_RECT_F bounds{};
        D2D1_COLOR_F color{};
        float stroke = 1.0f;
        float arrowSize = 8.0f;
        bool directed = true;
        bool dashed = false;
    };
    std::vector<LayoutTextRun> layoutTextRuns;
    std::vector<LayoutRect> layoutRects;
    std::vector<LayoutLine> layoutLines;
    std::vector<LayoutShape> layoutShapes;
    std::vector<LayoutConnector> layoutConnectors;
    bool layoutDirty = true;

    // Incremental layout: the first paint lays out ~2 viewports, the rest
    // continues in WM_APP_LAYOUT_CHUNK time slices (see render.cpp)
    bool layoutComplete = true;
    size_t layoutNextBlock = 0;   // next top-level block to lay out
    float layoutCursorY = 0.0f;   // y where the next block starts
    float layoutIndent = 0.0f;
    float layoutMaxWidth = 0.0f;
    size_t layoutTimeUs = 0;      // total layout time for the current cycle

    // Scroll sync anchors: source byte offset → rendered Y position
    struct ScrollAnchor {
        size_t sourceOffset;
        float renderedY;
    };
    std::vector<ScrollAnchor> scrollAnchors;
    std::vector<size_t> editorLineByteOffsets;  // UTF-8 byte offset per editor line

    // Search match layout mapping (document Y for each match)
    std::vector<float> searchMatchYs;
    size_t searchMatchCursor = 0;

    // Copied notification (fades out over 2 seconds)
    bool showCopiedNotification = false;
    float copiedNotificationAlpha = 0.0f;
    std::chrono::steady_clock::time_point copiedNotificationStart;

    // Cached "Copied!" text layout and metrics, rebuilt lazily (per-instance:
    // must not be shared across App instances, since it holds a reference
    // built from this instance's dwriteFactory/textFormat)
    CComPtr<IDWriteTextLayout> copiedNotificationLayout;
    float copiedNotificationTextOffsetX = 0.0f;
    float copiedNotificationTextOffsetY = 0.0f;

    // Cursor blink state, toggled by TIMER_CURSOR_BLINK (editor + search cursor)
    bool cursorBlinkOn = true;

    // Search overlay
    bool showSearch = false;
    float searchAnimation = 0.0f;
    std::wstring searchQuery;
    int searchCurrentIndex = 0;
    bool searchActive = false;
    bool searchJustOpened = false;  // Skip WM_CHAR after opening with F key

    // Cached search-bar cursor X position: only recomputed when the query or
    // text format changes, not per frame (per-instance, see above)
    std::wstring searchCursorCachedQuery;
    IDWriteTextFormat* searchCursorCachedFormat = nullptr;  // non-owning, for comparison only
    float searchCursorCachedQueryWidth = 0.0f;

    // File watching (auto-reload)
    FILETIME lastFileWriteTime = {};
    bool fileWatchEnabled = true;

    // Edit mode
    bool editMode = false;
    float editorSplitRatio = 0.5f;
    bool draggingSeparator = false;
    float separatorDragStartX = 0;
    float separatorDragStartRatio = 0;

    // Double-ESC detection
    std::chrono::steady_clock::time_point lastEscTime;
    bool escPressedOnce = false;
    bool confirmExitPending = false;  // Waiting for Y/N to confirm unsaved exit

    // Editor notification
    bool showEditModeNotification = false;
    float editModeNotificationAlpha = 0;
    std::chrono::steady_clock::time_point editModeNotificationStart;
    std::wstring editorNotificationMsg;

    // Editor document
    std::wstring editorText;
    bool editorDirty = false;
    std::vector<size_t> editorLineStarts;

    // Editor view options (persisted)
    bool editorShowPreview = true;
    bool editorWordWrap = false;

    // Soft-wrap metrics: cumulative visual rows before each logical line
    // (editorRowStarts.size() == lines + 1). Only maintained while
    // editorWordWrap is on; rebuilt when text or wrap width changes.
    std::vector<size_t> editorRowStarts;
    size_t editorTotalRows = 0;
    float editorRowMetricsWidth = -1.0f;  // wrap width the metrics were built for

    // Editor cursor & selection
    size_t editorCursorPos = 0;
    int editorDesiredCol = -1;
    float editorDesiredX = -1.0f;  // desired caret x for Up/Down in wrap mode
    bool editorSelecting = false;
    size_t editorSelStart = 0;
    size_t editorSelEnd = 0;
    bool editorHasSelection = false;

    // Editor scroll
    float editorScrollY = 0;
    float editorContentHeight = 0;

    // Editor search
    struct EditorSearchMatch {
        size_t startPos;
        size_t length;
    };
    std::vector<EditorSearchMatch> editorSearchMatches;
    int editorSearchCurrentIndex = 0;

    // Undo/redo
    struct EditAction {
        enum Type { Insert, Delete };
        Type type;
        size_t position;
        std::wstring text;
        size_t cursorBefore, cursorAfter;
    };
    std::vector<EditAction> undoStack;
    std::vector<EditAction> redoStack;

    // Editor text format (monospace)
    CComPtr<IDWriteTextFormat> editorTextFormat;
    CComPtr < IDWriteTextFormat> supSubFormat;   // small size for ^sup^/~sub~
    float editorCharWidth = 0.0f; // Measured monospace char width

    // Metrics
    StartupMetrics metrics;
    size_t drawCalls = 0;

    ~App() { shutdown(); }

    void clearLayoutCache() {
        layoutTextRuns.clear();  // CComPtr<IDWriteTextLayout> members release automatically
        layoutRects.clear();
        layoutLines.clear();
        layoutBitmaps.clear();
        linkRects.clear();
        codeBlocks.clear();
        textRects.clear();
        lineBuckets.clear();
        docText.clear();
        docTextLower.clear();
        headings.clear();
        headingSlugCounts.clear();

        copiedNotificationLayout.Release();
    }

    void releaseOverlayFormats() {
        searchTextFormat.Release();
        themeTitleFormat.Release();
        themeHeaderFormat.Release();
        folderBrowserFormat.Release();
        tocFormat.Release();
        tocFormatBold.Release();
        editorTextFormat.Release();
        themePreviewFormats.clear();

        // Non-owning cache of a format pointer we just released above — reset
        // so it can't compare equal to a stale/reused address.
        searchCursorCachedFormat = nullptr;
        searchCursorCachedQuery.clear();
        searchCursorCachedQueryWidth = 0.0f;
    }

    void releaseImageCache() {
        imageCache.clear();
    }

    void shutdown() {
        clearLayoutCache();
        releaseOverlayFormats();
        releaseImageCache();
    }
};

inline float dpi(const App& app, float value) {
    return value * app.contentScale;
}

// Cursor blink runs on a timer instead of per-frame InvalidateRect so the
// app is fully idle between blinks. Call after editMode/search state changes.
inline void updateBlinkTimer(App& app) {
    if (!app.hwnd) return;
    if (app.editMode || (app.showSearch && app.searchActive)) {
        app.cursorBlinkOn = true;
        SetTimer(app.hwnd, TIMER_CURSOR_BLINK, 500, nullptr);
    } else {
        KillTimer(app.hwnd, TIMER_CURSOR_BLINK);
        app.cursorBlinkOn = true;
    }
}

// Restart the blink phase so the cursor stays visible while typing
inline void resetCursorBlink(App& app) {
    app.cursorBlinkOn = true;
    if (app.hwnd) SetTimer(app.hwnd, TIMER_CURSOR_BLINK, 500, nullptr);
}

// Notification fades repaint on this timer; the WM_TIMER handler kills it
// once no notification is active.
inline void startNotificationTimer(App& app) {
    if (app.hwnd) SetTimer(app.hwnd, TIMER_NOTIFICATION, 33, nullptr);
}

// Width of the editor pane in edit mode (full window when preview is hidden)
inline float editorPaneWidth(const App& app) {
    return app.editorShowPreview
        ? app.width * app.editorSplitRatio - 3.0f
        : static_cast<float>(app.width);
}

inline float documentViewportX(const App& app) {
    if (!app.editMode) return 0.0f;
    // Preview hidden: zero-width viewport at the right edge — document
    // rendering flows through unchanged and clips to nothing
    if (!app.editorShowPreview) return static_cast<float>(app.width);
    return app.width * app.editorSplitRatio + 3.0f;
}

inline float documentViewportWidth(const App& app) {
    float width = app.editMode
        ? static_cast<float>(app.width) - documentViewportX(app)
        : static_cast<float>(app.width);
    return width > 0.0f ? width : 0.0f;
}
