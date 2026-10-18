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

#include "NodeGraphModel.h"
#include "NodeGraphRenderer.h"
#include "NodeGraphLayout.h"

// Notification codes sent via WM_NOTIFY to the parent window.
#define NGCN_NODECLICK      1   // lParam -> NODEGRAPHNOTIFY*, node was left-clicked; sent before the control selects it or
                                //   starts dragging it: return nonzero to take the click (no selecting, no dragging)
#define NGCN_NODEDBLCLICK   2   // lParam -> NODEGRAPHNOTIFY*, node was double-clicked; return nonzero to take it (in an
                                //   editable graph the label isn't edited then)
#define NGCN_EDGECLICK      3   // lParam -> NODEGRAPHNOTIFY*, edge was left-clicked
#define NGCN_SELCHANGED     4   // lParam -> NODEGRAPHNOTIFY*, selection changed
#define NGCN_EDGEADDED      5   // lParam -> NODEGRAPHEDGENOTIFY*, edge interactively created
#define NGCN_GETTOOLTIP     6   // lParam -> NODEGRAPHTOOLTIPNOTIFY*; fill SzText to override default
#define NGCN_NODEREMOVED    7   // lParam -> NODEGRAPHNOTIFY*, node (and incident edges) removed
#define NGCN_EDGEREMOVED    8   // lParam -> NODEGRAPHNOTIFY*, edge removed
#define NGCN_LABELCHANGED   9   // lParam -> NODEGRAPHLABELNOTIFY*, node label edited inline
#define NGCN_UNDOCHANGED   10   // lParam -> NMHDR*, undo/redo stack changed

namespace NodeGraphCtrl {

struct NODEGRAPHNOTIFY {
    NMHDR   Hdr;
    NodeId  NodeId;   // InvalidNode if not applicable
    EdgeId  EdgeId;   // InvalidEdge if not applicable
    // NGCN_NODECLICK, NGCN_NODEDBLCLICK: where in the node, in its graph units from its top-left corner (as NodeLayout
    // and owner drawing), and the part of its layout there. Otherwise 0 and None.
    float    NodeX;
    float    NodeY;
    NodePart Part;
};

struct NODEGRAPHEDGENOTIFY {
    NMHDR   Hdr;
    NodeId  FromNode;
    NodeId  ToNode;
    EdgeId  EdgeId;
};

struct NODEGRAPHTOOLTIPNOTIFY {
    NMHDR    Hdr;
    NodeId   NodeId;       // InvalidNode if not over a node
    EdgeId   EdgeId;       // InvalidEdge if not over an edge
    wchar_t  SzText[256];  // pre-filled with default label; host may override
};

struct NODEGRAPHLABELNOTIFY {
    NMHDR    Hdr;
    NodeId   NodeId;
    wchar_t  SzNewLabel[256];
};

// Register the window class. Call once at startup (or on DLL attach).
// Returns false if registration fails or class already exists.
bool Register(HINSTANCE hInstance);

// Window class name to use with CreateWindowEx.
constexpr wchar_t WC_NODEGRAPHCONTROL[] = L"NodeGraphControl";

// Window styles supported as creation flags (pass in dwStyle):
//   NGCS_GRID        draw a background grid
//   NGCS_AUTOZOOM    fit the graph in view when it is replaced: a new model (SetModel), or one that was loaded or
//                    cleared (and filled again); not on edits
#define NGCS_GRID      0x0001
#define NGCS_AUTOZOOM  0x0002
//   NGCS_READONLY    the graph can be looked at and nodes moved, but not edited: no new edges, deleting, renaming,
//                    resizing or pasting.
#define NGCS_READONLY  0x0004
//   NGCS_SCROLLBARS  scroll bars when the graph (at the zoom) doesn't fit in the view
//   NGCS_WHEELSCROLL the mouse wheel scrolls (with Shift: sideways) and Ctrl+wheel zooms; without it the wheel zooms
#define NGCS_SCROLLBARS  0x0008
#define NGCS_WHEELSCROLL 0x0010

// Internal message used to commit/cancel the inline label editor.
constexpr UINT WM_NODEGRAPHEDIT = WM_APP + 1;

// ---- Undo command base -------------------------------------------------------
// Concrete commands (MoveCmd, RenameCmd, ...) are implementation detail and
// stay private to NodeGraphControl.cpp.
struct UndoCmd {
    virtual void Apply(NodeGraphModel&)  = 0;
    virtual void Revert(NodeGraphModel&) = 0;
    virtual void CollectImages(std::vector<ImageId>& /*images*/) const {}   // the images it may put back in the model
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
    bool  Scrolling    = false;  // dragging with the right button: the view rectangle follows the mouse
    float StartOffsetX = 0.0f;   // the view's offset when the scrolling started
    float StartOffsetY = 0.0f;
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
// class (e.g. CNodeGraphControl m_wndGraph; m_wndGraph.Create(...);).
// Give the parent window WS_CLIPCHILDREN: otherwise, if its class has a background brush, it paints over the graph
// on every resize before the graph repaints, and the graph flickers.
class CNodeGraphControl : public CWindowImpl<CNodeGraphControl> {
public:
    DECLARE_WND_CLASS_EX(WC_NODEGRAPHCONTROL, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, -1)

    BEGIN_MSG_MAP(CNodeGraphControl)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_MOUSEWHEEL(OnMouseWheel)
        MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseHWheel)
        MSG_WM_HSCROLL(OnHScroll)
        MSG_WM_VSCROLL(OnVScroll)
        MSG_WM_TIMER(OnTimer)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MSG_WM_RBUTTONDOWN(OnRButtonDown)
        MSG_WM_RBUTTONUP(OnRButtonUp)
        MSG_WM_MBUTTONDOWN(OnMButtonDown)
        MSG_WM_MBUTTONUP(OnMButtonUp)
        MSG_WM_LBUTTONDOWN(OnLButtonDown)
        MSG_WM_LBUTTONDBLCLK(OnLButtonDblClk)
        MSG_WM_LBUTTONUP(OnLButtonUp)
        MSG_WM_MOUSEMOVE(OnMouseMove)
        MSG_WM_MOUSELEAVE(OnMouseLeave)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(WM_NODEGRAPHEDIT, OnNodeGraphEdit)
    END_MSG_MAP()

    // Creation convenience overload; dwStyle is a normal window style, ctrlStyle
    // combines NGCS_* flags (packed into the low-order style bits, like LVS_*/ES_*).
    HWND Create(HWND hWndParent, int x, int y, int w, int h,
                DWORD dwStyle = WS_CHILD | WS_VISIBLE, DWORD ctrlStyle = NGCS_GRID);

    // Swap in a new model; the control takes ownership.
    void SetModel(NodeGraphModel* model);
    NodeGraphModel* GetModel() const;

    // Programmatic viewport control.
    void FitInView();
    void FitSelected();           // fit view around the currently selected nodes
    void SetZoom(float factor);   // 1.0 = 100%
    void CenterOn(float graphX, float graphY, float zoom = 0.0f);  // put a point of the graph in the middle of the view (zoom 0: keep the zoom)
    float GetZoom() const;

    // Scroll bars when the graph doesn't fit in the view (NGCS_SCROLLBARS). They cover the graph and the view, so
    // the view can still be moved off the graph (by dragging it, or with the minimap).
    // Removes the images of the model no node shows and no undo or redo can put back (as after undoing a paste of
    // nodes with images). Returns how many.
    size_t RemoveUnusedImages();

    // Fit the graph in view when it is replaced (NGCS_AUTOZOOM).
    void SetAutoZoom(bool autoZoom);
    bool GetAutoZoom() const;

    void SetScrollBars(bool show);
    bool HasScrollBars() const;
    // The mouse wheel scrolls (Shift+wheel: sideways) and Ctrl+wheel zooms (NGCS_WHEELSCROLL); false: the wheel zooms.
    // The tilt wheel scrolls sideways either way.
    void SetWheelScrolls(bool scrolls);
    bool GetWheelScrolls() const;

    // Selection — single (backward compat) and multi.
    NodeId GetSelectedNode() const;   // returns primary (first) selected node
    NodeId NodeFromPoint(POINT pt) const;   // pt in client coordinates (e.g. for WM_CONTEXTMENU); InvalidNode if no node is there
    EdgeId GetSelectedEdge() const;
    std::vector<NodeId> GetSelectedNodes() const;
    void SelectNode(NodeId id, bool addToSelection = false);
    void ClearSelection();

    // Deletion — also clears selection and fires NGCN_NODEREMOVED / NGCN_EDGEREMOVED.
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

    // Style helpers — set and immediately repaint. undoable: the change can be undone (as the user's own edits can).
    void SetNodeStyle(NodeId id, const NodeStyle& style, bool undoable = false);
    void SetNodeContent(NodeId id, const NodeContent& content, bool undoable = false);
    void SetEdgeStyle(EdgeId id, const EdgeStyle& style);

    // Arranges the nodes (and the bends of the edges) with one of the layout algorithms; can be undone.
    // fitInView: zoom so the whole graph is in view afterwards.
    void ApplyLayout(LayoutAlgorithm algorithm, const LayoutOptions& options = {}, bool fitInView = true);

    // Minimap overlay.
    void SetMinimapVisible(bool visible);
    bool IsMinimapVisible() const;
    void SetMinimapPosition(int x, int y);  // top-left in client coords; pass (-1,-1) to restore auto (bottom-right)

    // Force an immediate repaint without changing model state.
    void Refresh();

    // The canvas color (dark gray by default); the grid and selection colors adapt to it. SaveImage uses it as well.
    void     SetBackgroundColor(COLORREF color);
    COLORREF GetBackgroundColor() const;

    // Writes the whole graph (not only the part in view) as an image, without the selection, grid and minimap.
    // The format comes from the extension: .jpg/.jpeg, .bmp, otherwise PNG. scale is pixels per graph unit; it is
    // lowered if the image would be too large (more than 16384 pixels a side or about 32 million pixels).
    // False for an empty graph or on error.
    bool SaveImage(const wchar_t* path, float scale = 1.0f);

    // NodeGraphModel::SaveSvg with the control's background, and with the owner drawn nodes (when there is a node
    // draw handler) as pictures of them, drawn at pictureScale pixels per graph unit.
    bool SaveSvg(const wchar_t* path, float pictureScale = 2.0f);

    // Draws the nodes that have NodeStyle::OwnerDraw (see INodeDrawHandler). Not owned: it must outlive the control,
    // or be set to null before it goes. Repaints.
    void SetNodeDrawHandler(INodeDrawHandler* handler);
    INodeDrawHandler* GetNodeDrawHandler() const;

private:
    // ---- Message handlers ----
    int  OnCreate(LPCREATESTRUCT pcs);
    void OnDestroy();
    void OnPaint(HDC dc);
    void OnSize(UINT nType, CSize size);
    BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    LRESULT OnMouseHWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void OnHScroll(int code, short pos, HWND scrollBar);
    void OnVScroll(int code, short pos, HWND scrollBar);
    void OnTimer(UINT_PTR id);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    void OnRButtonDown(UINT nFlags, CPoint pt);
    void OnRButtonUp(UINT nFlags, CPoint pt);
    void OnMButtonDown(UINT nFlags, CPoint pt);
    void OnMButtonUp(UINT nFlags, CPoint pt);
    void OnLButtonDown(UINT nFlags, CPoint pt);
    void OnLButtonDblClk(UINT nFlags, CPoint pt);
    void OnLButtonUp(UINT nFlags, CPoint pt);
    void OnMouseMove(UINT nFlags, CPoint pt);
    void OnMouseLeave();
    LRESULT OnNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    BOOL OnEraseBkgnd(CDCHandle dc);
    LRESULT OnNodeGraphEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // ---- Internal helpers ----
    LRESULT Notify(UINT code, NodeId nodeId, EdgeId edgeId, const D2D1_POINT_2F* at = nullptr);   // at: in graph space; the parent's result
    void NotifyEdgeAdded(NodeId from, NodeId to, EdgeId edgeId);
    void NotifyUndoChanged();
    void PushUndo(std::unique_ptr<UndoCmd> cmd);
    void RequestInvalidate();
    bool IsSelected(NodeId id) const;
    void FireSelChanged();
    MinimapConfig MakeMinimapConfig(int cw, int ch) const;
    static bool MinimapScreenToGraph(const MinimapConfig& cfg, const NodeGraphModel& model,
                                      float sx, float sy, float& gxOut, float& gyOut);
    bool IsOnMinimap(const MinimapConfig& cfg, int sx, int sy) const;
    void CommitLabelEdit();
    void CancelLabelEdit();
    RECT LabelEditRect(const Node& n) const;
    void SetHoveredNode(NodeId id);
    void UpdateScrollBars();
    void OnScroll(int bar, int code);
    void ScrollBy(float dx, float dy);         // moves the view (the graph moves the other way), in pixels
    float LineSize() const;
    bool IsDraggingInView() const;             // a drag that scrolls the view at its edges
    POINT AutoScrollSpeed(POINT pt) const;
    void AutoScroll(POINT pt);
    template<typename T, T Node::*Member>
    void SetNodeMember(NodeId id, const T& value, bool undoable);
    void FinalizeRubberBand();

private:
    std::unique_ptr<NodeGraphModel>   m_Model;
    NodeGraphRenderer                 m_Renderer;
    ViewTransform                 m_Vt;
    std::vector<NodeId>           m_SelectedNodes;
    EdgeId                        m_SelectedEdge = InvalidEdge;
    bool                          m_DrawGrid     = false;
    bool                          m_ReadOnly     = false;

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

    NodeId m_HoveredNode = InvalidNode;     // tracked only while there is a node draw handler

    bool m_AutoZoom         = false;
    const NodeGraphModel* m_FittedModel = nullptr;   // the graph AutoZoom fitted last (the model and its reset count)
    uint32_t m_FittedResets = 0;
    bool m_ScrollBars       = false;
    bool m_WheelScrolls     = false;
    bool m_UpdatingScroll   = false;        // showing or hiding a bar resizes the window
    bool m_TrackingThumb    = false;        // the range isn't changed while a thumb is dragged
    bool m_AutoScrolling    = false;        // the timer runs
    static constexpr UINT_PTR k_AutoScrollTimer    = 1;
    static constexpr UINT     k_AutoScrollInterval = 20;    // ms
};

constexpr float k_ViewMargin = 20.0f;       // pixels around a graph fitted in the view (and the scroll range)

} // namespace NodeGraphCtrl
