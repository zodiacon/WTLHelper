#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <memory>

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlgdi.h>
#include <atltypes.h>

#include "GraphModel.h"
#include "GraphRenderer.h"

// Notification codes sent via WM_NOTIFY to the parent window.
#define GCN_NODECLICK      1   // lParam -> GRAPHNOTIFY*, node was left-clicked
#define GCN_NODEDBLCLICK   2   // lParam -> GRAPHNOTIFY*, node was double-clicked (reserved)
#define GCN_EDGECLICK      3   // lParam -> GRAPHNOTIFY*, edge was left-clicked
#define GCN_SELCHANGED     4   // lParam -> GRAPHNOTIFY*, selection changed
#define GCN_EDGEADDED      5   // lParam -> GRAPHEDGENOTIFY*, edge interactively created
#define GCN_GETTOOLTIP     6   // lParam -> GRAPHTOOLTIPNOTIFY*; fill SzText to override default
#define GCN_NODEREMOVED    7   // lParam -> GRAPHNOTIFY*, node (and incident edges) removed
#define GCN_EDGEREMOVED    8   // lParam -> GRAPHNOTIFY*, edge removed
#define GCN_LABELCHANGED   9   // lParam -> GRAPHLABELNOTIFY*, node label edited inline
#define GCN_UNDOCHANGED   10   // lParam -> NMHDR*, undo/redo stack changed

namespace GraphCtrl {

struct GRAPHNOTIFY {
    NMHDR   Hdr;
    NodeId  NodeId;   // InvalidNode if not applicable
    EdgeId  EdgeId;   // InvalidEdge if not applicable
};

struct GRAPHEDGENOTIFY {
    NMHDR   Hdr;
    NodeId  FromNode;
    NodeId  ToNode;
    EdgeId  EdgeId;
};

struct GRAPHTOOLTIPNOTIFY {
    NMHDR    Hdr;
    NodeId   NodeId;       // InvalidNode if not over a node
    EdgeId   EdgeId;       // InvalidEdge if not over an edge
    wchar_t  SzText[256];  // pre-filled with default label; host may override
};

struct GRAPHLABELNOTIFY {
    NMHDR    Hdr;
    NodeId   NodeId;
    wchar_t  SzNewLabel[256];
};

// Register the window class. Call once at startup (or on DLL attach).
// Returns false if registration fails or class already exists.
bool Register(HINSTANCE hInstance);

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_GRAPHCONTROL[] = L"GraphControl";

// Window styles supported as creation flags (pass in dwStyle):
//   GCS_GRID        draw a background grid
//   GCS_AUTOZOOM    fit graph in view on model change
#define GCS_GRID      0x0001
#define GCS_AUTOZOOM  0x0002

// Internal message used to commit/cancel the inline label editor.
constexpr UINT WM_GRAPHEDIT = WM_APP + 1;

// ---- Undo command base -------------------------------------------------------
// Concrete commands (MoveCmd, RenameCmd, ...) are implementation detail and
// stay private to GraphControl.cpp.
struct UndoCmd {
    virtual void Apply(GraphModel&)  = 0;
    virtual void Revert(GraphModel&) = 0;
    virtual ~UndoCmd() = default;
};

// ---- Support structs -----------------------------------------------------

struct MinimapState {
    bool  Visible     = false;
    float Width       = 180.0f;
    float Height      = 130.0f;
    float X           = -1.0f;  // <0 = auto-place at bottom-right corner
    float Y           = -1.0f;
    bool  Dragging    = false;
    bool  HasMoved    = false;
    POINT DragStart   = {};
    float StartX      = 0.0f;
    float StartY      = 0.0f;
    float DragClickGX = 0.0f;
    float DragClickGY = 0.0f;
};

struct DragOrigin {
    NodeId Id;
    float  StartX, StartY;
};

constexpr float k_MinNodeWidth  = 40.0f;
constexpr float k_MinNodeHeight = 24.0f;

struct ResizeState {
    bool         Active      = false;
    NodeId       NodeId      = InvalidNode;
    ResizeHandle Handle      = ResizeHandle::None;
    ResizeHandle Hovered     = ResizeHandle::None;
    float        StartGX     = 0.0f;
    float        StartGY     = 0.0f;
    float        StartLeft   = 0.0f;
    float        StartTop    = 0.0f;
    float        StartRight  = 0.0f;
    float        StartBottom = 0.0f;
};

// ---- Main control ----------------------------------------------------------
// A self-contained WTL control: derive-and-embed like any other CWindowImpl
// class (e.g. CGraphControl m_wndGraph; m_wndGraph.Create(...);).
class CGraphControl : public CWindowImpl<CGraphControl> {
public:
    DECLARE_WND_CLASS_EX(WC_GRAPHCONTROL, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

    BEGIN_MSG_MAP(CGraphControl)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_MOUSEWHEEL(OnMouseWheel)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MSG_WM_RBUTTONDOWN(OnRButtonDown)
        MSG_WM_RBUTTONUP(OnRButtonUp)
        MSG_WM_MBUTTONDOWN(OnMButtonDown)
        MSG_WM_MBUTTONUP(OnMButtonUp)
        MSG_WM_LBUTTONDOWN(OnLButtonDown)
        MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
        MSG_WM_LBUTTONUP(OnLButtonUp)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(WM_GRAPHEDIT, OnGraphEdit)
    END_MSG_MAP()

    // Creation convenience overload; dwStyle is a normal window style, ctrlStyle
    // combines GCS_* flags (packed into the low-order style bits, like LVS_*/ES_*).
    HWND Create(HWND hWndParent, int x, int y, int w, int h,
                DWORD dwStyle = WS_CHILD | WS_VISIBLE, DWORD ctrlStyle = GCS_GRID);

    // Swap in a new model; the control takes ownership.
    void SetModel(GraphModel* model);
    GraphModel* GetModel() const;

    // Programmatic viewport control.
    void FitInView();
    void FitSelected();           // fit view around the currently selected nodes
    void SetZoom(float factor);   // 1.0 = 100%
    float GetZoom() const;

    // Selection — single (backward compat) and multi.
    NodeId GetSelectedNode() const;   // returns primary (first) selected node
    EdgeId GetSelectedEdge() const;
    std::vector<NodeId> GetSelectedNodes() const;
    void SelectNode(NodeId id, bool addToSelection = false);
    void ClearSelection();

    // Deletion — also clears selection and fires GCN_NODEREMOVED / GCN_EDGEREMOVED.
    bool DeleteNode(NodeId id);
    bool DeleteEdge(EdgeId id);
    bool DeleteSelected();   // deletes the currently selected node(s) or edge

    // Inline label editing.
    void BeginEditLabel(NodeId id);
    bool IsEditingLabel() const;

    // Undo / Redo.
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;

    // Update batching — suppress repaints during bulk programmatic changes.
    void BeginUpdate();
    void EndUpdate();

    // Clipboard — copy selected nodes and their inter-edges; paste at offset.
    void Copy();
    void Paste();
    bool CanPaste() const;

    // Node size — per-node and default for newly created nodes.
    void SetNodeSize(NodeId id, float w, float h);
    void SetDefaultNodeSize(float w, float h);

    // Style helpers — set and immediately repaint.
    void SetNodeStyle(NodeId id, const NodeStyle& style);
    void SetEdgeStyle(EdgeId id, const EdgeStyle& style);

    // Minimap overlay.
    void SetMinimapVisible(bool visible);
    bool IsMinimapVisible() const;
    void SetMinimapPosition(int x, int y);  // top-left in client coords; pass (-1,-1) to restore auto (bottom-right)

    // Force an immediate repaint without changing model state.
    void Refresh();

private:
    // ---- Message handlers ----
    int  OnCreate(LPCREATESTRUCT pcs);
    void OnDestroy();
    void OnPaint(HDC dc);
    void OnSize(UINT nType, CSize size);
    BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void OnRButtonDown(UINT nFlags, CPoint pt);
    void OnRButtonUp(UINT nFlags, CPoint pt);
    void OnMButtonDown(UINT nFlags, CPoint pt);
    void OnMButtonUp(UINT nFlags, CPoint pt);
    void OnLButtonDown(UINT nFlags, CPoint pt);
    void OnLButtonDblClk(UINT nFlags, CPoint pt);
    void OnLButtonUp(UINT nFlags, CPoint pt);
    void OnMouseMove(UINT nFlags, CPoint pt);
    LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnGraphEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- Internal helpers ----
    void Notify(UINT code, NodeId nodeId, EdgeId edgeId);
    void NotifyEdgeAdded(NodeId from, NodeId to, EdgeId edgeId);
    void NotifyUndoChanged();
    void PushUndo(std::unique_ptr<UndoCmd> cmd);
    void RequestInvalidate();
    bool IsSelected(NodeId id) const;
    void FireSelChanged();
    MinimapConfig MakeMinimapConfig(int cw, int ch) const;
    static bool MinimapScreenToGraph(const MinimapConfig& cfg, const GraphModel& model,
                                      float sx, float sy, float& gxOut, float& gyOut);
    void CommitLabelEdit();
    void CancelLabelEdit();
    void FinalizeRubberBand();

private:
    std::unique_ptr<GraphModel>   m_Model;
    GraphRenderer                 m_Renderer;
    ViewTransform                 m_Vt;
    std::vector<NodeId>           m_SelectedNodes;
    EdgeId                        m_SelectedEdge = InvalidEdge;
    bool                          m_DrawGrid     = false;

    bool  m_Panning   = false;
    POINT m_PanStart  = {};
    float m_PanStartX = 0, m_PanStartY = 0;

    bool                    m_DraggingNode  = false;
    NodeId                  m_DragPrimaryId = InvalidNode;
    float                   m_DragOffsetX   = 0, m_DragOffsetY = 0;
    std::vector<DragOrigin> m_DragOrigins;

    RubberBand  m_RubberBand;
    EdgePreview m_EdgePreview;

    HWND     m_LabelEdit  = nullptr;
    CFont    m_LabelEditFont;
    NodeId   m_EditNodeId = InvalidNode;

    std::vector<std::unique_ptr<UndoCmd>> m_UndoStack;
    std::vector<std::unique_ptr<UndoCmd>> m_RedoStack;

    HWND    m_TooltipWnd      = nullptr;
    wchar_t m_TooltipBuf[256] = {};

    MinimapState m_Minimap;
    ResizeState  m_Resize;

    int  m_UpdateDepth       = 0;
    bool m_PendingInvalidate = false;
};

} // namespace GraphCtrl
