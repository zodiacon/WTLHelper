// NodeGraphControl.cpp : Defines the functions for the static library.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/NodeGraphControl.h"
#include "NodeGraphImaging.h"
#include "NodeGraphSerialization.h"
#include <unordered_map>
#include <unordered_set>

#include <commctrl.h>
#include <memory>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <string>
#include <wincodec.h>
#include <shlwapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

namespace NodeGraphCtrl {

	// ---- Undo command types (private to this translation unit) -------------------

	namespace {

		std::vector<NodeId> NodeIds(const std::vector<Node>& nodes) {
			std::vector<NodeId> ids;
			ids.reserve(nodes.size());
			for (const auto& n : nodes) ids.push_back(n.Id);
			return ids;
		}

		void CollectNodeImages(const std::vector<Node>& nodes, std::vector<ImageId>& images) {
			images.reserve(images.size() + nodes.size() * 2);
			for (const auto& n : nodes) {
				images.push_back(n.Content.Icon);
				images.push_back(n.Content.Image);
			}
		}

		struct MoveCmd : UndoCmd {
			NodeId Id;
			float  OldX, OldY, NewX, NewY;
			void Apply(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) { n->X = NewX; n->Y = NewY; }
			}
			void Revert(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) { n->X = OldX; n->Y = OldY; }
			}
		};

		struct RenameCmd : UndoCmd {
			NodeId       Id;
			std::wstring OldLabel, NewLabel;
			void Apply(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->Label = NewLabel;
			}
			void Revert(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->Label = OldLabel;
			}
		};

		struct DeleteEdgeCmd : UndoCmd {
			Edge Saved;
			void Apply(NodeGraphModel& m) override { m.RemoveEdge(Saved.Id); }
			void Revert(NodeGraphModel& m) override { m.RestoreEdge(Saved); }
		};

		struct AddEdgeCmd : UndoCmd {
			Edge Saved;
			void Apply(NodeGraphModel& m) override { m.RestoreEdge(Saved); }
			void Revert(NodeGraphModel& m) override { m.RemoveEdge(Saved.Id); }
		};

		struct DeleteSelectionCmd : UndoCmd {
			std::vector<Node> Nodes;
			std::vector<Edge> Edges;
			void Apply(NodeGraphModel& m) override {
				m.RemoveNodes(NodeIds(Nodes));	// Edges are theirs: they go with them
			}
			void Revert(NodeGraphModel& m) override {
				for (const auto& n : Nodes) m.RestoreNode(n);
				for (const auto& e : Edges) m.RestoreEdge(e);
			}
			void CollectImages(std::vector<ImageId>& images) const override {
				CollectNodeImages(Nodes, images);
			}
		};

		struct CompoundCmd : UndoCmd {
			std::vector<std::unique_ptr<UndoCmd>> Cmds;
			void Apply(NodeGraphModel& m) override {
				for (auto& c : Cmds) c->Apply(m);
			}
			void Revert(NodeGraphModel& m) override {
				for (auto it = Cmds.rbegin(); it != Cmds.rend(); ++it)
					(*it)->Revert(m);
			}
			void CollectImages(std::vector<ImageId>& images) const override {
				for (const auto& c : Cmds) c->CollectImages(images);
			}
		};

		struct ResizeCmd : UndoCmd {
			NodeId Id;
			float  OldX, OldY, OldW, OldH;
			float  NewX, NewY, NewW, NewH;
			void Apply(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) {
					n->X = NewX; n->Y = NewY; n->Width = NewW; n->Height = NewH;
				}
			}
			void Revert(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) {
					n->X = OldX; n->Y = OldY; n->Width = OldW; n->Height = OldH;
				}
			}
		};

		// a change of one member of a node (its style, its content)
		template<typename T, T Node::*Member>
		struct NodeMemberCmd : UndoCmd {
			NodeId Id;
			T      Old, New;
			void Apply(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->*Member = New;
			}
			void Revert(NodeGraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->*Member = Old;
			}
			void CollectImages(std::vector<ImageId>& images) const override {
				if constexpr (std::is_same_v<T, NodeContent>) {
					for (const NodeContent* c : { &Old, &New }) {
						images.push_back(c->Icon);
						images.push_back(c->Image);
					}
				}
			}
		};

		// nodes moved with the arrow keys: more presses on the same nodes make the same move longer
		struct KeyMoveCmd : UndoCmd {
			std::vector<NodeId> Ids;
			std::vector<Point>  Old, New;
			void Apply(NodeGraphModel& m) override { Place(m, New); }
			void Revert(NodeGraphModel& m) override { Place(m, Old); }
			void Place(NodeGraphModel& m, const std::vector<Point>& at) {
				for (size_t i = 0; i < Ids.size(); i++)
					if (Node* n = m.GetNode(Ids[i])) { n->X = at[i].X; n->Y = at[i].Y; }
			}
		};

		struct PasteCmd : UndoCmd {
			std::vector<Node> PastedNodes;
			std::vector<Edge> PastedEdges;
			void Apply(NodeGraphModel& m) override {
				for (const auto& n : PastedNodes) m.RestoreNode(n);
				for (const auto& e : PastedEdges) m.RestoreEdge(e);
			}
			void Revert(NodeGraphModel& m) override {
				m.RemoveNodes(NodeIds(PastedNodes));	// the pasted edges are between them
			}
			void CollectImages(std::vector<ImageId>& images) const override {
				CollectNodeImages(PastedNodes, images);
			}
		};

		struct LayoutCmd : UndoCmd {
			struct Place { NodeId Id; float X, Y; };
			struct Route { EdgeId Id; std::vector<Point> Waypoints; };
			std::vector<Place> OldPlaces, NewPlaces;
			std::vector<Route> OldRoutes, NewRoutes;

			static void Capture(const NodeGraphModel& m, std::vector<Place>& places, std::vector<Route>& routes) {
				places.clear();
				routes.clear();
				places.reserve(m.Nodes().size());
				routes.reserve(m.Edges().size());
				for (const auto& n : m.Nodes()) places.push_back({ n.Id, n.X, n.Y });
				for (const auto& e : m.Edges()) routes.push_back({ e.Id, e.Waypoints });
			}
			static void Restore(NodeGraphModel& m, const std::vector<Place>& places, const std::vector<Route>& routes) {
				for (const auto& p : places)
					if (Node* n = m.GetNode(p.Id)) { n->X = p.X; n->Y = p.Y; }
				for (const auto& r : routes)
					if (Edge* e = m.GetEdge(r.Id)) e->Waypoints = r.Waypoints;
			}
			void Apply(NodeGraphModel& m) override { Restore(m, NewPlaces, NewRoutes); }
			void Revert(NodeGraphModel& m) override { Restore(m, OldPlaces, OldRoutes); }
		};

	} // anonymous namespace

	// ---- Clipboard helpers -------------------------------------------------------

	namespace {

		// The data: the serialization version, the images the nodes show, the nodes, the edges between them (records
		// of NodeGraphSerialization). Another name than the first format's, which had none of it.
		static UINT GetClipboardFormat() {
			static UINT s_fmt = ::RegisterClipboardFormatW(L"NodeGraphControlClipboard2");
			return s_fmt;
		}

	} // anonymous namespace

	// ---- WM_NODEGRAPHEDIT label edit subclass proc -----------------------------------

	static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
		LPARAM lParam, UINT_PTR, DWORD_PTR dwRef) {
		HWND parent = reinterpret_cast<HWND>(dwRef);
		if (msg == WM_KEYDOWN) {
			if (wParam == VK_ESCAPE) {
				PostMessage(parent, WM_NODEGRAPHEDIT, 0, 0);
				return 0;
			}
			if (wParam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000)) {
				PostMessage(parent, WM_NODEGRAPHEDIT, 1, 0);
				return 0;
			}
			// Plain Enter falls through → multiline edit inserts a newline.
		}
		if (msg == WM_KILLFOCUS) {
			PostMessage(parent, WM_NODEGRAPHEDIT, 1, 0);
			return 0;
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	// ---- CNodeGraphControl : message handlers ----------------------------------------

	int CNodeGraphControl::OnCreate(LPCREATESTRUCT pcs) {
		m_DrawGrid = (pcs->style & NGCS_GRID) != 0;
		m_ReadOnly = (pcs->style & NGCS_READONLY) != 0;
		m_ScrollBars = (pcs->style & NGCS_SCROLLBARS) != 0;
		m_AutoZoom = (pcs->style & NGCS_AUTOZOOM) != 0;
		m_WheelScrolls = (pcs->style & NGCS_WHEELSCROLL) != 0;
		m_Highlight = (pcs->style & NGCS_HIGHLIGHT) != 0;
		m_SnapToGrid = (pcs->style & NGCS_SNAPTOGRID) != 0;
		m_Guides = (pcs->style & NGCS_GUIDES) != 0;
		m_Model = std::make_unique<NodeGraphModel>();
		m_Model->SetResetCallback([this] { OnModelReset(); });

		if (FAILED(m_Renderer.Init(pcs->hInstance))) return -1;

		HWND tip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
			WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			m_hWnd, nullptr, pcs->hInstance, nullptr);
		if (tip) {
			RECT rc2{}; GetClientRect(&rc2);
			TOOLINFOW ti{};
			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = m_hWnd;
			ti.uId = 1;
			ti.lpszText = LPSTR_TEXTCALLBACK;
			ti.rect = rc2;
			::SendMessage(tip, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
			::SendMessage(tip, TTM_SETMAXTIPWIDTH, 0, 400);
			m_TooltipWnd = tip;
		}
		return 0;
	}

	void CNodeGraphControl::OnDestroy() {
		CancelLabelEdit();
		m_TooltipWnd = nullptr;
		m_Renderer.Shutdown();
	}

	// ---- Notification helpers ------------------------------------------------

	LRESULT CNodeGraphControl::Notify(UINT code, NodeId nodeId, EdgeId edgeId, const D2D1_POINT_2F* at) {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return 0;
		NODEGRAPHNOTIFY gn{};
		gn.Hdr.hwndFrom = m_hWnd;
		gn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		gn.Hdr.code = code;
		gn.NodeId = nodeId;
		gn.EdgeId = edgeId;
		if (const Node* n = at ? m_Model->GetNode(nodeId) : nullptr) {
			gn.NodeX = at->x - (n->X - n->Width * 0.5f);
			gn.NodeY = at->y - (n->Y - n->Height * 0.5f);
			gn.Part = m_Model->GetNodeLayout(*n).PartAt(gn.NodeX, gn.NodeY);
		}
		return ::SendMessage(parent, WM_NOTIFY, gn.Hdr.idFrom, reinterpret_cast<LPARAM>(&gn));
	}

	void CNodeGraphControl::NotifyEdgeAdded(NodeId from, NodeId to, EdgeId edgeId) {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return;
		NODEGRAPHEDGENOTIFY gn{};
		gn.Hdr.hwndFrom = m_hWnd;
		gn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		gn.Hdr.code = NGCN_EDGEADDED;
		gn.FromNode = from;
		gn.ToNode = to;
		gn.EdgeId = edgeId;
		::SendMessage(parent, WM_NOTIFY, gn.Hdr.idFrom, reinterpret_cast<LPARAM>(&gn));
	}

	void CNodeGraphControl::NotifyUndoChanged() {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return;
		NMHDR hdr{};
		hdr.hwndFrom = m_hWnd;
		hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		hdr.code = NGCN_UNDOCHANGED;
		::SendMessage(parent, WM_NOTIFY, hdr.idFrom, reinterpret_cast<LPARAM>(&hdr));
	}

	// ---- Undo / selection helpers --------------------------------------------

	void CNodeGraphControl::PushUndo(std::unique_ptr<UndoCmd> cmd) {
		m_UndoStack.push_back(std::move(cmd));
		m_RedoStack.clear();
		TrimUndo();
		NotifyUndoChanged();
	}

	// The oldest steps over the limit go.
	void CNodeGraphControl::TrimUndo() {
		if (m_UndoLimit == 0 || m_UndoStack.size() <= m_UndoLimit)
			return;
		const size_t extra = m_UndoStack.size() - m_UndoLimit;
		for (size_t i = 0; i < extra; i++)
			if (m_UndoStack[i].get() == m_KeyMove) m_KeyMove = nullptr;
		m_UndoStack.erase(m_UndoStack.begin(), m_UndoStack.begin() + extra);
	}

	void CNodeGraphControl::SetUndoLimit(size_t steps) {
		m_UndoLimit = steps;
		TrimUndo();
		if (m_RedoStack.size() > steps && steps > 0)	// redo is limited the same (its oldest step is the deepest)
			m_RedoStack.erase(m_RedoStack.begin(), m_RedoStack.begin() + (m_RedoStack.size() - steps));
		if (m_hWnd) NotifyUndoChanged();
	}

	size_t CNodeGraphControl::GetUndoLimit() const {
		return m_UndoLimit;
	}

	void CNodeGraphControl::SetReadOnly(bool readOnly) {
		if (readOnly == m_ReadOnly) return;
		m_ReadOnly = readOnly;
		if (readOnly && m_hWnd) {
			// what a read-only graph doesn't do: editing a label, making an edge, resizing
			if (m_LabelEdit) CancelLabelEdit();
			if (m_EdgePreview.Active || m_Resize.Active) {
				m_EdgePreview.Active = false;
				m_EdgePreview.FromNode = InvalidNode;
				m_Resize.Active = false;
				m_Resize.NodeId = InvalidNode;
				if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			}
			m_Resize.Hovered = ResizeHandle::None;
		}
		if (m_hWnd) Invalidate(FALSE);	// the resize handles come or go
	}

	bool CNodeGraphControl::IsReadOnly() const {
		return m_ReadOnly;
	}

	void CNodeGraphControl::RequestInvalidate() {
		if (m_UpdateDepth > 0) m_PendingInvalidate = true;
		else Invalidate(FALSE);
	}

	void CNodeGraphControl::BeginUpdate() { ++m_UpdateDepth; }

	void CNodeGraphControl::EndUpdate() {
		if (--m_UpdateDepth == 0 && m_PendingInvalidate) {
			m_PendingInvalidate = false;
			Invalidate(FALSE);
		}
	}

	bool CNodeGraphControl::IsSelected(NodeId id) const {
		return std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), id)
			!= m_SelectedNodes.end();
	}

	void CNodeGraphControl::FireSelChanged() {
		NodeId primary = m_SelectedNodes.empty() ? InvalidNode : m_SelectedNodes[0];
		Notify(NGCN_SELCHANGED, primary, m_SelectedEdge);
	}

	// ---- Minimap helpers -----------------------------------------------------

	MinimapConfig CNodeGraphControl::MakeMinimapConfig(int cw, int ch) const {
		MinimapConfig cfg;
		cfg.Visible = m_Minimap.Visible;
		cfg.Width = m_Minimap.Width;
		cfg.Height = m_Minimap.Height;
		cfg.X = (m_Minimap.X >= 0.0f) ? m_Minimap.X : (float)cw - m_Minimap.Width - 12.0f;
		cfg.Y = (m_Minimap.Y >= 0.0f) ? m_Minimap.Y : (float)ch - m_Minimap.Height - 12.0f;
		return cfg;
	}

	bool CNodeGraphControl::MinimapScreenToGraph(const MinimapConfig& cfg, const NodeGraphModel& model,
		float sx, float sy, float& gxOut, float& gyOut) {
		if (model.Nodes().empty()) return false;

		float gMinX = FLT_MAX, gMinY = FLT_MAX, gMaxX = -FLT_MAX, gMaxY = -FLT_MAX;
		for (const auto& n : model.Nodes()) {
			gMinX = std::min(gMinX, n.X - n.Width * 0.5f);
			gMinY = std::min(gMinY, n.Y - n.Height * 0.5f);
			gMaxX = std::max(gMaxX, n.X + n.Width * 0.5f);
			gMaxY = std::max(gMaxY, n.Y + n.Height * 0.5f);
		}
		float gW = std::max(gMaxX - gMinX, 1.0f);
		float gH = std::max(gMaxY - gMinY, 1.0f);

		const float pad = 8.0f;
		float mmW = cfg.Width - pad * 2.0f;
		float mmH = cfg.Height - pad * 2.0f;
		float mmScale = std::min(mmW / gW, mmH / gH);

		float originX = cfg.X + pad + (mmW - gW * mmScale) * 0.5f;
		float originY = cfg.Y + pad + (mmH - gH * mmScale) * 0.5f;

		gxOut = gMinX + (sx - originX) / mmScale;
		gyOut = gMinY + (sy - originY) / mmScale;
		return true;
	}

	bool CNodeGraphControl::IsOnMinimap(const MinimapConfig& cfg, int sx, int sy) const {
		return m_Minimap.Visible
			&& (float)sx >= cfg.X && (float)sx <= cfg.X + cfg.Width
			&& (float)sy >= cfg.Y && (float)sy <= cfg.Y + cfg.Height;
	}

	// ---- Label edit ------------------------------------------------------------

	void CNodeGraphControl::CommitLabelEdit() {
		if (!m_LabelEdit) return;

		int len = ::GetWindowTextLengthW(m_LabelEdit);
		std::wstring raw(len, L'\0');
		::GetWindowTextW(m_LabelEdit, raw.data(), len + 1);

		// Strip \r — EDIT control uses \r\n, we store \n.
		std::wstring text;
		text.reserve(raw.size());
		for (wchar_t c : raw) if (c != L'\r') text += c;

		// Trim leading and trailing whitespace.
		auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n'; };
		auto first = std::find_if_not(text.begin(), text.end(), isSpace);
		auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();
		text = (first < last) ? std::wstring(first, last) : std::wstring{};

		NodeId id = m_EditNodeId;

		::DestroyWindow(m_LabelEdit);
		m_LabelEdit = nullptr;
		if (!m_LabelEditFont.IsNull()) m_LabelEditFont.DeleteObject();
		m_EditNodeId = InvalidNode;

		Node* n = m_Model->GetNode(id);
		if (n && n->Label != text) {
			auto cmd = std::make_unique<RenameCmd>();
			cmd->Id = id;
			cmd->OldLabel = n->Label;
			cmd->NewLabel = text;
			n->Label = text;
			PushUndo(std::move(cmd));

			HWND parent = ::GetParent(m_hWnd);
			if (parent) {
				NODEGRAPHLABELNOTIFY gln{};
				gln.Hdr.hwndFrom = m_hWnd;
				gln.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
				gln.Hdr.code = NGCN_LABELCHANGED;
				gln.NodeId = id;
				wcsncpy_s(gln.SzNewLabel, text.c_str(), _TRUNCATE);
				::SendMessage(parent, WM_NOTIFY, gln.Hdr.idFrom, reinterpret_cast<LPARAM>(&gln));
			}
		}
		RequestInvalidate();
	}

	void CNodeGraphControl::CancelLabelEdit() {
		if (!m_LabelEdit) return;
		::DestroyWindow(m_LabelEdit);
		m_LabelEdit = nullptr;
		if (!m_LabelEditFont.IsNull()) m_LabelEditFont.DeleteObject();
		m_EditNodeId = InvalidNode;
		RequestInvalidate();
	}

	void CNodeGraphControl::BeginEditLabel(NodeId id) {
		if (m_LabelEdit) CommitLabelEdit();
		Node* n = m_Model->GetNode(id);
		if (!n) return;

		RECT rc = LabelEditRect(*n);
		int x = rc.left, y = rc.top, w = rc.right - rc.left, h = rc.bottom - rc.top;

		// Convert \n → \r\n for the EDIT control.
		std::wstring editText;
		editText.reserve(n->Label.size() * 2);
		for (wchar_t c : n->Label) {
			if (c == L'\n') editText += L'\r';
			editText += c;
		}

		m_LabelEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", editText.c_str(),
			WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
			x, y, w, h, m_hWnd, nullptr, nullptr, nullptr);
		if (!m_LabelEdit) return;

		// Segoe UI at the size the D2D renderer uses (13 DIPs), times the zoom if the label scales with it.
		const float scale = n->Style.ScaleLabel && !n->Style.Code ? m_Vt.Scale : 1.0f;
		int fontPx = std::max(8, (int)(13.0f * ::GetDpiForWindow(m_hWnd) / 96.0f * scale + 0.5f));
		m_LabelEditFont.CreateFont(
			-fontPx, 0, 0, 0, FW_NORMAL,
			FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
			L"Segoe UI");
		::SendMessage(m_LabelEdit, WM_SETFONT,
			reinterpret_cast<WPARAM>((HFONT)m_LabelEditFont), TRUE);

		::SendMessage(m_LabelEdit, EM_SETSEL, 0, -1);
		::SetFocus(m_LabelEdit);
		m_EditNodeId = id;

		SetWindowSubclass(m_LabelEdit, EditSubclassProc, 1,
			reinterpret_cast<DWORD_PTR>(m_hWnd));
	}

	// Over the label's part of the node (the content when the label is hidden, the node when the title takes it all), at
	// least a line of text high.
	RECT CNodeGraphControl::LabelEditRect(const Node& n) const {
		NodeLayout layout = m_Model->GetNodeLayout(n);
		const Rect whole{ 0, 0, n.Width, n.Height };
		const Rect& part = !layout.Label.IsEmpty() ? layout.Label : !layout.Content.IsEmpty() ? layout.Content : whole;
		float left = n.X - n.Width * 0.5f, top = n.Y - n.Height * 0.5f;
		auto p0 = m_Vt.ToScreen(left + part.Left, top + part.Top);
		auto p1 = m_Vt.ToScreen(left + part.Right, top + part.Bottom);
		const float minHeight = (float)MulDiv(24, ::GetDpiForWindow(m_hWnd), 96);
		if (p1.y - p0.y < minHeight) {
			float grow = (minHeight - (p1.y - p0.y)) * 0.5f;
			p0.y -= grow;
			p1.y += grow;
		}
		return { (int)p0.x, (int)p0.y, (int)p1.x, (int)p1.y };
	}

	bool CNodeGraphControl::IsEditingLabel() const {
		return m_LabelEdit != nullptr;
	}

	// ---- Deletion helpers ----------------------------------------------------

	bool CNodeGraphControl::DeleteNode(NodeId id) {
		Node* n = m_Model->GetNode(id);
		if (!n) return false;

		auto cmd = std::make_unique<DeleteSelectionCmd>();
		cmd->Nodes.push_back(*n);
		for (const auto& e : m_Model->Edges())
			if (e.From == id || e.To == id) cmd->Edges.push_back(e);

		if (!m_Model->RemoveNode(id)) return false;

		auto it = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), id);
		if (it != m_SelectedNodes.end()) m_SelectedNodes.erase(it);
		if (m_SelectedEdge != InvalidEdge && !m_Model->GetEdge(m_SelectedEdge))
			m_SelectedEdge = InvalidEdge;

		PushUndo(std::move(cmd));
		FireSelChanged();
		Notify(NGCN_NODEREMOVED, id, InvalidEdge);
		RequestInvalidate();
		return true;
	}

	bool CNodeGraphControl::DeleteEdge(EdgeId id) {
		const Edge* e = m_Model->GetEdge(id);
		if (!e) return false;

		auto cmd = std::make_unique<DeleteEdgeCmd>();
		cmd->Saved = *e;

		if (!m_Model->RemoveEdge(id)) return false;

		if (m_SelectedEdge == id) {
			m_SelectedEdge = InvalidEdge;
			FireSelChanged();
		}
		PushUndo(std::move(cmd));
		Notify(NGCN_EDGEREMOVED, InvalidNode, id);
		RequestInvalidate();
		return true;
	}

	bool CNodeGraphControl::DeleteSelected() {
		if (m_SelectedNodes.empty() && m_SelectedEdge == InvalidEdge) return false;

		if (!m_SelectedNodes.empty()) {
			auto cmd = std::make_unique<DeleteSelectionCmd>();
			cmd->Nodes.reserve(m_SelectedNodes.size());
			for (NodeId nid : m_SelectedNodes) {
				Node* n = m_Model->GetNode(nid);
				if (n) cmd->Nodes.push_back(*n);
			}
			// the edges of the deleted nodes (each once), for undo
			const std::unordered_set<NodeId> selected(m_SelectedNodes.begin(), m_SelectedNodes.end());
			for (const auto& e : m_Model->Edges())
				if (selected.contains(e.From) || selected.contains(e.To))
					cmd->Edges.push_back(e);

			// told about each while it is still there, then all removed at once
			const std::vector<NodeId> removed = m_SelectedNodes;
			for (NodeId nid : removed)
				Notify(NGCN_NODEREMOVED, nid, InvalidEdge);
			m_Model->RemoveNodes(removed);
			m_SelectedNodes.clear();
			if (m_SelectedEdge != InvalidEdge && !m_Model->GetEdge(m_SelectedEdge))
				m_SelectedEdge = InvalidEdge;

			PushUndo(std::move(cmd));
			FireSelChanged();
			RequestInvalidate();
			return true;
		}

		return DeleteEdge(m_SelectedEdge);
	}

	// ---- Undo / Redo ---------------------------------------------------------

	// The graph was replaced (Clear, Load, another model): what refers to the old one goes. Undo would apply its
	// commands to the new graph, and the selection would pick nodes of it by the old ids.
	void CNodeGraphControl::OnModelReset() {
		StopAnimation();
		if (m_LabelEdit) CancelLabelEdit();
		m_DraggingNode = false;
		m_DragPrimaryId = InvalidNode;
		m_DragOrigins.clear();
		m_Resize.Active = false;
		m_Resize.NodeId = InvalidNode;
		m_Resize.Hovered = ResizeHandle::None;
		m_RubberBand.Active = false;
		m_EdgePreview.Active = false;
		m_EdgePreview.FromNode = InvalidNode;
		m_KeyMove = nullptr;
		m_Renderer.SetGuides({});
		if (m_hWnd && ::GetCapture() == m_hWnd)
			::ReleaseCapture();

		const bool hadUndo = !m_UndoStack.empty() || !m_RedoStack.empty();
		m_UndoStack.clear();
		m_RedoStack.clear();
		if (hadUndo && m_hWnd) NotifyUndoChanged();

		const bool hadSelection = !m_SelectedNodes.empty() || m_SelectedEdge != InvalidEdge;
		m_SelectedNodes.clear();
		m_SelectedEdge = InvalidEdge;
		if (hadSelection && m_hWnd) FireSelChanged();

		m_HoveredNode = InvalidNode;
		m_Renderer.SetHoveredNode(InvalidNode);
		if (m_hWnd) Invalidate(FALSE);
	}

	void CNodeGraphControl::Undo() {
		StopAnimation();
		m_KeyMove = nullptr;	// the next arrow key starts a new move
		if (m_UndoStack.empty()) return;
		auto cmd = std::move(m_UndoStack.back());
		m_UndoStack.pop_back();
		cmd->Revert(*m_Model);
		m_RedoStack.push_back(std::move(cmd));
		NotifyUndoChanged();
		RequestInvalidate();
	}

	void CNodeGraphControl::Redo() {
		StopAnimation();
		m_KeyMove = nullptr;
		if (m_RedoStack.empty()) return;
		auto cmd = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		cmd->Apply(*m_Model);
		m_UndoStack.push_back(std::move(cmd));
		NotifyUndoChanged();
		RequestInvalidate();
	}

	bool CNodeGraphControl::CanUndo() const {
		return !m_UndoStack.empty();
	}

	bool CNodeGraphControl::CanRedo() const {
		return !m_RedoStack.empty();
	}

	// ---- Clipboard (copy / paste) --------------------------------------------

	void CNodeGraphControl::Copy() {
		if (m_SelectedNodes.empty()) return;

		std::vector<const Node*> nodes;
		nodes.reserve(m_SelectedNodes.size());
		for (NodeId id : m_SelectedNodes)
			if (const Node* n = m_Model->GetNode(id)) nodes.push_back(n);

		std::vector<const Image*> images;
		images.reserve(std::min(nodes.size() * 2, m_Model->Images().size()));
		auto addImage = [&](ImageId id) {
			const Image* image = m_Model->GetImage(id);
			if (image && std::find(images.begin(), images.end(), image) == images.end())
				images.push_back(image);
		};
		for (const Node* n : nodes) {
			addImage(n->Content.Icon);
			addImage(n->Content.Image);
		}

		std::vector<const Edge*> edges;
		for (const auto& e : m_Model->Edges()) {
			bool f = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.From) != m_SelectedNodes.end();
			bool t = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.To) != m_SelectedNodes.end();
			if (f && t) edges.push_back(&e);
		}

		Serialization::Writer w;
		w.Write(Serialization::Version);
		w.Write((uint32_t)images.size());
		for (const Image* image : images)
			Serialization::WriteImage(w, *image);
		w.Write((uint32_t)nodes.size());
		for (const Node* n : nodes)
			Serialization::WriteNode(w, *n, m_Model->GetUserDataSerializer());
		w.Write((uint32_t)edges.size());
		for (const Edge* e : edges)
			Serialization::WriteEdge(w, *e, m_Model->GetUserDataSerializer());

		if (!::OpenClipboard(m_hWnd)) return;
		::EmptyClipboard();
		HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, w.Data.size());
		if (hg) {
			void* p = ::GlobalLock(hg);
			if (p) {
				memcpy(p, w.Data.data(), w.Data.size());
				::GlobalUnlock(hg);
				if (!::SetClipboardData(GetClipboardFormat(), hg))
					::GlobalFree(hg);
			}
			else {
				::GlobalFree(hg);
			}
		}
		::CloseClipboard();
	}

	void CNodeGraphControl::Paste() {
		UINT fmt = GetClipboardFormat();
		if (!::IsClipboardFormatAvailable(fmt)) return;
		if (!::OpenClipboard(m_hWnd)) return;

		HGLOBAL hg = (HGLOBAL)::GetClipboardData(fmt);
		if (!hg) { ::CloseClipboard(); return; }

		const uint8_t* raw = (const uint8_t*)::GlobalLock(hg);
		size_t         size = ::GlobalSize(hg);
		if (!raw) { ::CloseClipboard(); return; }

		Serialization::Reader r{ raw, size };
		std::vector<Image> images;
		std::vector<Node> nodes;
		std::vector<Edge> edges;
		uint32_t version = 0, count = 0;
		bool ok = r.Read(version) && version >= 3 && version <= Serialization::Version && r.Read(count);
		if (ok) images.reserve(r.Capped(count, Serialization::MinImageRecord));
		for (uint32_t i = 0; i < count && ok; ++i) {
			Image image;
			ok = Serialization::ReadImage(r, image);
			images.push_back(std::move(image));
		}
		ok = ok && r.Read(count);
		if (ok) nodes.reserve(r.Capped(count, Serialization::MinNodeRecord));
		for (uint32_t i = 0; i < count && ok; ++i) {
			Node n;
			ok = Serialization::ReadNode(r, n, version, m_Model->GetUserDataSerializer());
			nodes.push_back(std::move(n));
		}
		ok = ok && r.Read(count);
		if (ok) edges.reserve(r.Capped(count, Serialization::MinEdgeRecord));
		for (uint32_t i = 0; i < count && ok; ++i) {
			Edge e;
			ok = Serialization::ReadEdge(r, e, version, m_Model->GetUserDataSerializer());
			edges.push_back(std::move(e));
		}

		::GlobalUnlock(hg);
		::CloseClipboard();

		if (!ok || nodes.empty()) return;

		// the images are added to this model (an image it has already isn't added again): their ids here
		std::unordered_map<ImageId, ImageId> imageIds;
		imageIds.reserve(images.size());
		for (const auto& image : images)
			imageIds[image.Id] = m_Model->AddImage(image.Data.data(), image.Data.size());
		auto mapImage = [&](ImageId id) {
			auto it = imageIds.find(id);
			return it == imageIds.end() ? InvalidImage : it->second;
		};

		const float offset = 20.0f;
		auto cmd = std::make_unique<PasteCmd>();
		cmd->PastedNodes.reserve(nodes.size());
		cmd->PastedEdges.reserve(edges.size());
		std::unordered_map<NodeId, NodeId> nodeIds;
		nodeIds.reserve(nodes.size());
		std::vector<NodeId> newIds;
		newIds.reserve(nodes.size());

		for (auto& pasted : nodes) {
			NodeId id = m_Model->AddNode(pasted.Label);
			Node* n = m_Model->GetNode(id);
			nodeIds[pasted.Id] = id;
			pasted.Id = id;
			pasted.X += offset;
			pasted.Y += offset;
			pasted.Content.Icon = mapImage(pasted.Content.Icon);
			pasted.Content.Image = mapImage(pasted.Content.Image);
			*n = pasted;
			cmd->PastedNodes.push_back(*n);
			newIds.push_back(id);
		}
		for (auto& pasted : edges) {
			auto from = nodeIds.find(pasted.From), to = nodeIds.find(pasted.To);
			if (from == nodeIds.end() || to == nodeIds.end()) continue;
			EdgeId eid = m_Model->AddEdge(from->second, to->second);
			Edge* e = m_Model->GetEdge(eid);
			pasted.Id = eid;
			pasted.From = from->second;
			pasted.To = to->second;
			for (auto& p : pasted.Waypoints) {
				p.X += offset;
				p.Y += offset;
			}
			*e = pasted;
			cmd->PastedEdges.push_back(*e);
		}

		PushUndo(std::move(cmd));
		m_SelectedNodes = newIds;
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	bool CNodeGraphControl::CanPaste() const {
		return ::IsClipboardFormatAvailable(GetClipboardFormat()) != FALSE;
	}

	// ---- Rubber-band finalize ------------------------------------------------

	void CNodeGraphControl::FinalizeRubberBand() {
		m_RubberBand.Active = false;
		float sx0 = std::min(m_RubberBand.X0, m_RubberBand.X1);
		float sy0 = std::min(m_RubberBand.Y0, m_RubberBand.Y1);
		float sx1 = std::max(m_RubberBand.X0, m_RubberBand.X1);
		float sy1 = std::max(m_RubberBand.Y0, m_RubberBand.Y1);

		if (sx1 - sx0 < 3.0f && sy1 - sy0 < 3.0f) return;

		m_SelectedNodes.clear();
		m_SelectedEdge = InvalidEdge;
		for (const auto& n : m_Model->Nodes()) {
			auto sc = m_Vt.ToScreen(n.X, n.Y);
			if (sc.x >= sx0 && sc.x <= sx1 && sc.y >= sy0 && sc.y <= sy1)
				m_SelectedNodes.push_back(n.Id);
		}
		FireSelChanged();
	}

	// ---- Message handlers ----------------------------------------------------

	void CNodeGraphControl::OnPaint(HDC /*unused*/) {
		// a graph that was replaced is fitted once it has nodes
		if (m_AutoZoom && m_Model && !m_Model->Nodes().empty() &&
			(m_Model.get() != m_FittedModel || m_Model->GetResetCount() != m_FittedResets)) {
			m_FittedModel = m_Model.get();
			m_FittedResets = m_Model->GetResetCount();
			FitInView();
		}
		// whatever changed the graph or the view ends here: the bars follow (before painting, as they may resize the view)
		if (m_ScrollBars || (GetStyle() & (WS_HSCROLL | WS_VSCROLL)))
			UpdateScrollBars();
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(&ps);
		RECT rc; GetClientRect(&rc);
		MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
		ResizeOverlay ro;
		if (!m_ReadOnly && m_SelectedNodes.size() == 1) {
			ro.Node = m_SelectedNodes[0];
			ro.HoveredHandle = m_Resize.Active ? m_Resize.Handle : m_Resize.Hovered;
		}
		// the highlight is about the node under the mouse, or else the selected nodes (not while dragging: the
		// dragged node would be all there is)
		std::vector<NodeId> focus;
		if (m_Highlight && !m_DraggingNode && !m_Resize.Active && !m_RubberBand.Active) {
			if (m_HoveredNode != InvalidNode && m_Model->GetNode(m_HoveredNode)) focus = { m_HoveredNode };
			else focus = m_SelectedNodes;
		}
		m_Renderer.SetHighlight(std::move(focus));

		// A layout moving: drawn with the places (and the view) between the old and the new; the model keeps the new
		// ones outside the drawing. A bend that comes or goes can't move: such an edge is straight until the end.
		std::vector<Point> finalPlaces;
		std::vector<std::vector<Point>> finalBends;
		const ViewTransform finalView = m_Vt;
		const bool animating = m_Animation.Active && AnimationProgress() < 1.0f;
		if (animating) {
			const float t = AnimationProgress();
			auto lerp = [t](float a, float b) { return a + (b - a) * t; };
			finalPlaces.reserve(m_Animation.From.size());
			for (const auto& [id, from] : m_Animation.From) {
				Node* n = m_Model->GetNode(id);
				finalPlaces.push_back(n ? Point{ n->X, n->Y } : Point{});
				if (n) { n->X = lerp(from.X, n->X); n->Y = lerp(from.Y, n->Y); }
			}
			finalBends.reserve(m_Animation.FromBends.size());
			for (const auto& [id, from] : m_Animation.FromBends) {
				Edge* e = m_Model->GetEdge(id);
				finalBends.push_back(e ? std::move(e->Waypoints) : std::vector<Point>{});
				if (!e) continue;
				const auto& to = finalBends.back();
				e->Waypoints.clear();
				if (from.size() == to.size()) {
					e->Waypoints.reserve(to.size());
					for (size_t i = 0; i < to.size(); i++)
						e->Waypoints.push_back({ lerp(from[i].X, to[i].X), lerp(from[i].Y, to[i].Y) });
				}
			}
			const auto& a = m_Animation.FromView;
			const auto& b = m_Animation.ToView;
			m_Vt.Scale = lerp(a.Scale, b.Scale);
			m_Vt.OffsetX = lerp(a.OffsetX, b.OffsetX);
			m_Vt.OffsetY = lerp(a.OffsetY, b.OffsetY);
		}
		else if (m_Animation.Active)
			StopAnimation();

		m_Renderer.Render(hdc, rc, *m_Model, m_Vt,
			m_SelectedNodes, m_SelectedEdge,
			m_DrawGrid, m_EdgePreview, m_RubberBand, mmcfg, ro);

		if (animating) {
			for (size_t i = 0; i < m_Animation.From.size(); i++)
				if (Node* n = m_Model->GetNode(m_Animation.From[i].first)) { n->X = finalPlaces[i].X; n->Y = finalPlaces[i].Y; }
			for (size_t i = 0; i < m_Animation.FromBends.size(); i++)
				if (Edge* e = m_Model->GetEdge(m_Animation.FromBends[i].first)) e->Waypoints = std::move(finalBends[i]);
			m_Vt = finalView;
		}
		EndPaint(&ps);
	}

	void CNodeGraphControl::OnSize(UINT /*nType*/, CSize size) {
		if (m_TooltipWnd) {
			RECT rc2 = { 0, 0, size.cx, size.cy };
			TOOLINFOW ti{};
			ti.cbSize = sizeof(ti);
			ti.hwnd = m_hWnd;
			ti.uId = 1;
			ti.rect = rc2;
			::SendMessage(m_TooltipWnd, TTM_NEWTOOLRECT, 0, reinterpret_cast<LPARAM>(&ti));
		}
		if (m_LabelEdit && m_EditNodeId != InvalidNode) {
			Node* n = m_Model->GetNode(m_EditNodeId);
			if (n) {
				RECT rc = LabelEditRect(*n);
				::SetWindowPos(m_LabelEdit, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
		Invalidate(FALSE);
	}

	BOOL CNodeGraphControl::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
		StopAnimation();	// input ends a layout's move
		if (m_WheelScrolls && !(nFlags & MK_CONTROL)) {
			UINT lines = 3;
			::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
			RECT rc;
			GetClientRect(&rc);
			// a page at a time (WHEEL_PAGESCROLL) or lines; up: the view goes up
			const float step = lines == WHEEL_PAGESCROLL ? ((nFlags & MK_SHIFT) ? rc.right : rc.bottom) * 0.9f : lines * LineSize();
			const float amount = -zDelta / (float)WHEEL_DELTA * step;
			if (nFlags & MK_SHIFT) ScrollBy(amount, 0);
			else ScrollBy(0, amount);
			return TRUE;
		}
		float delta = zDelta > 0 ? 1.1f : 1.0f / 1.1f;
		ScreenToClient(&pt);  // lParam of WM_MOUSEWHEEL is in screen coords
		m_Vt.OffsetX = pt.x - (pt.x - m_Vt.OffsetX) * delta;
		m_Vt.OffsetY = pt.y - (pt.y - m_Vt.OffsetY) * delta;
		m_Vt.Scale *= delta;
		m_Vt.Scale = std::clamp(m_Vt.Scale, 0.05f, 50.0f);
		Invalidate(FALSE);
		return TRUE;
	}

	LRESULT CNodeGraphControl::OnSetCursor(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		if (m_EdgePreview.Active) {
			::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
			return TRUE;
		}
		POINT pt; ::GetCursorPos(&pt); ScreenToClient(&pt);

		// The minimap overlays the graph (the nodes under it aren't the ones the mouse would act on).
		RECT rc; GetClientRect(&rc);
		if (IsOnMinimap(MakeMinimapConfig(rc.right, rc.bottom), pt.x, pt.y)) {
			::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
			return TRUE;
		}

		if (!m_ReadOnly && m_SelectedNodes.size() == 1) {
			const Node* n = m_Model->GetNode(m_SelectedNodes[0]);
			if (n) {
				ResizeHandle h = m_Renderer.HitTestResizeHandle(*n, m_Vt, (float)pt.x, (float)pt.y);
				if (h != ResizeHandle::None) {
					LPCWSTR cur = IDC_ARROW;
					switch (h) {
						case ResizeHandle::NW: case ResizeHandle::SE: cur = IDC_SIZENWSE; break;
						case ResizeHandle::NE: case ResizeHandle::SW: cur = IDC_SIZENESW; break;
						case ResizeHandle::N:  case ResizeHandle::S:  cur = IDC_SIZENS;   break;
						case ResizeHandle::E:  case ResizeHandle::W:  cur = IDC_SIZEWE;   break;
						default: break;
					}
					::SetCursor(::LoadCursor(nullptr, cur));
					return TRUE;
				}
			}
		}

		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		if (m_Renderer.HitTestNode(*m_Model, gp.x, gp.y) != InvalidNode) {
			::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
			return TRUE;
		}

		bHandled = FALSE;
		return 0;
	}

	void CNodeGraphControl::OnRButtonDown(UINT /*nFlags*/, CPoint pt) {
		StopAnimation();
		CommitLabelEdit();
		SetFocus();
		m_RightDown = pt;

		// Dragging on the minimap with the right button scrolls: the view rectangle follows the mouse.
		RECT rc; GetClientRect(&rc);
		MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
		if (IsOnMinimap(mmcfg, pt.x, pt.y)) {
			if (MinimapScreenToGraph(mmcfg, *m_Model, (float)pt.x, (float)pt.y,
				m_Minimap.DragClickGX, m_Minimap.DragClickGY)) {
				m_Minimap.Scrolling = true;
				m_Minimap.HasMoved = false;
				m_Minimap.DragStart = { pt.x, pt.y };
				m_Minimap.StartOffsetX = m_Vt.OffsetX;
				m_Minimap.StartOffsetY = m_Vt.OffsetY;
				SetCapture();
			}
			return;
		}

		if (m_ReadOnly) return;
		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		NodeId hit = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
		if (hit != InvalidNode) {
			m_EdgePreview.Active = true;
			m_EdgePreview.FromNode = hit;
			m_EdgePreview.ScreenX = (float)pt.x;
			m_EdgePreview.ScreenY = (float)pt.y;
			SetCapture();
		}
	}

	void CNodeGraphControl::OnRButtonUp(UINT /*nFlags*/, CPoint pt) {
		if (m_Minimap.Scrolling) {
			m_Minimap.Scrolling = false;
			::ReleaseCapture();
			if (!m_Minimap.HasMoved)
				SetMsgHandled(FALSE);	// a right click without a drag still brings the context menu
			return;
		}
		if (!m_EdgePreview.Active) {
			SetMsgHandled(FALSE);	// let the default processing send WM_CONTEXTMENU to the parent
			return;
		}
		m_EdgePreview.Active = false;
		::ReleaseCapture();

		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		NodeId hit = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
		// released where it went down: a right click on the node, not a new edge; the default processing sends
		// WM_CONTEXTMENU (OnContextMenu)
		const int dx = pt.x - m_RightDown.x, dy = pt.y - m_RightDown.y;
		if (hit == m_EdgePreview.FromNode && dx * dx + dy * dy <= 16) {
			m_EdgePreview.FromNode = InvalidNode;
			Invalidate(FALSE);
			SetMsgHandled(FALSE);
			return;
		}
		if (hit != InvalidNode && hit != m_EdgePreview.FromNode) {
			EdgeId eid = m_Model->AddEdge(m_EdgePreview.FromNode, hit);
			const Edge* e = m_Model->GetEdge(eid);
			if (e) {
				auto cmd = std::make_unique<AddEdgeCmd>();
				cmd->Saved = *e;
				PushUndo(std::move(cmd));
			}
			NotifyEdgeAdded(m_EdgePreview.FromNode, hit, eid);
		}
		m_EdgePreview.FromNode = InvalidNode;
		Invalidate(FALSE);
	}

	void CNodeGraphControl::OnMButtonDown(UINT /*nFlags*/, CPoint pt) {
		StopAnimation();
		m_Panning = true;
		m_PanStart = { pt.x, pt.y };
		m_PanStartX = m_Vt.OffsetX;
		m_PanStartY = m_Vt.OffsetY;
		SetCapture();
	}

	void CNodeGraphControl::OnMButtonUp(UINT /*nFlags*/, CPoint /*pt*/) {
		if (m_Panning) {
			m_Panning = false;
			::ReleaseCapture();
		}
	}

	void CNodeGraphControl::OnLButtonDown(UINT nFlags, CPoint pt) {
		StopAnimation();
		CommitLabelEdit();
		SetFocus();
		int sx = pt.x, sy = pt.y;

		// Minimap must be tested first — it overlays the graph.
		if (m_Minimap.Visible) {
			RECT rc; GetClientRect(&rc);
			MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
			if ((float)sx >= mmcfg.X && (float)sx <= mmcfg.X + mmcfg.Width &&
				(float)sy >= mmcfg.Y && (float)sy <= mmcfg.Y + mmcfg.Height) {
				m_Minimap.Dragging = true;
				m_Minimap.HasMoved = false;
				m_Minimap.DragStart = { sx, sy };
				m_Minimap.StartX = mmcfg.X;
				m_Minimap.StartY = mmcfg.Y;
				MinimapScreenToGraph(mmcfg, *m_Model, (float)sx, (float)sy,
					m_Minimap.DragClickGX, m_Minimap.DragClickGY);
				SetCapture();
				return;
			}
		}

		// Resize handles take priority when one node is selected.
		if (!m_ReadOnly && m_SelectedNodes.size() == 1) {
			const Node* rn = m_Model->GetNode(m_SelectedNodes[0]);
			if (rn) {
				ResizeHandle h = m_Renderer.HitTestResizeHandle(*rn, m_Vt, (float)sx, (float)sy);
				if (h != ResizeHandle::None) {
					auto gp2 = m_Vt.ToGraph((float)sx, (float)sy);
					m_Resize.Active = true;
					m_Resize.NodeId = m_SelectedNodes[0];
					m_Resize.Handle = h;
					m_Resize.Hovered = ResizeHandle::None;
					m_Resize.StartGX = gp2.x;
					m_Resize.StartGY = gp2.y;
					m_Resize.StartLeft = rn->X - rn->Width * 0.5f;
					m_Resize.StartTop = rn->Y - rn->Height * 0.5f;
					m_Resize.StartRight = rn->X + rn->Width * 0.5f;
					m_Resize.StartBottom = rn->Y + rn->Height * 0.5f;
					SetCapture();
					return;
				}
			}
		}

		auto gp = m_Vt.ToGraph((float)sx, (float)sy);

		NodeId hitNode = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
		EdgeId hitEdge = (hitNode == InvalidNode)
			? m_Renderer.HitTestEdge(*m_Model, gp.x, gp.y)
			: InvalidEdge;

		bool shiftHeld = (nFlags & MK_SHIFT) != 0;

		// the parent may take the click (a button drawn on the node): then it doesn't select or drag
		if (hitNode != InvalidNode && Notify(NGCN_NODECLICK, hitNode, InvalidEdge, &gp))
			return;

		if (hitNode != InvalidNode) {
			if (shiftHeld) {
				auto it = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), hitNode);
				if (it != m_SelectedNodes.end())
					m_SelectedNodes.erase(it);
				else
					m_SelectedNodes.push_back(hitNode);
				m_SelectedEdge = InvalidEdge;
				FireSelChanged();
			}
			else {
				if (!IsSelected(hitNode)) {
					m_SelectedNodes = { hitNode };
					m_SelectedEdge = InvalidEdge;
					FireSelChanged();
				}
			}

			m_DraggingNode = true;
			m_DragPrimaryId = hitNode;
			m_DragOrigins.clear();
			m_DragOrigins.reserve(m_SelectedNodes.size());
			for (NodeId nid : m_SelectedNodes) {
				Node* n = m_Model->GetNode(nid);
				if (n) m_DragOrigins.push_back({ nid, n->X, n->Y });
			}
			Node* clicked = m_Model->GetNode(hitNode);
			if (clicked) {
				m_DragOffsetX = gp.x - clicked->X;
				m_DragOffsetY = gp.y - clicked->Y;
			}
			SetCapture();
		}
		else if (hitEdge != InvalidEdge) {
			bool selChanged = (m_SelectedEdge != hitEdge || !m_SelectedNodes.empty());
			m_SelectedNodes.clear();
			m_SelectedEdge = hitEdge;
			if (selChanged) FireSelChanged();
			Notify(NGCN_EDGECLICK, InvalidNode, hitEdge);
		}
		else {
			bool selChanged = (!m_SelectedNodes.empty() || m_SelectedEdge != InvalidEdge);
			m_SelectedNodes.clear();
			m_SelectedEdge = InvalidEdge;
			if (selChanged) FireSelChanged();

			if (m_ReadOnly) {
				// nothing to select with a rubber band: drag the graph
				m_Panning = true;
				m_PanStart = { sx, sy };
				m_PanStartX = m_Vt.OffsetX;
				m_PanStartY = m_Vt.OffsetY;
				SetCapture();
				Invalidate(FALSE);
				return;
			}

			m_RubberBand.Active = true;
			m_RubberBand.X0 = m_RubberBand.X1 = (float)sx;
			m_RubberBand.Y0 = m_RubberBand.Y1 = (float)sy;
			SetCapture();
		}

		Invalidate(FALSE);
	}

	void CNodeGraphControl::OnLButtonDblClk(UINT /*nFlags*/, CPoint pt) {
		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		NodeId hitNode = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
		if (hitNode == InvalidNode)
			return;
		// the parent may take the double click; if not, in an editable graph the label is edited
		if (!Notify(NGCN_NODEDBLCLICK, hitNode, InvalidEdge, &gp) && !m_ReadOnly)
			BeginEditLabel(hitNode);
	}

	void CNodeGraphControl::OnLButtonUp(UINT /*nFlags*/, CPoint /*pt*/) {
		m_Renderer.SetGuides({});	// the guides are only while dragging
		if (m_Resize.Active) {
			m_Resize.Active = false;
			::ReleaseCapture();
			Node* rn = m_Model->GetNode(m_Resize.NodeId);
			if (rn) {
				float origCX = (m_Resize.StartLeft + m_Resize.StartRight) * 0.5f;
				float origCY = (m_Resize.StartTop + m_Resize.StartBottom) * 0.5f;
				float origW = m_Resize.StartRight - m_Resize.StartLeft;
				float origH = m_Resize.StartBottom - m_Resize.StartTop;
				if (rn->X != origCX || rn->Y != origCY ||
					rn->Width != origW || rn->Height != origH) {
					auto cmd = std::make_unique<ResizeCmd>();
					cmd->Id = m_Resize.NodeId;
					cmd->OldX = origCX; cmd->OldY = origCY;
					cmd->OldW = origW;  cmd->OldH = origH;
					cmd->NewX = rn->X;  cmd->NewY = rn->Y;
					cmd->NewW = rn->Width; cmd->NewH = rn->Height;
					PushUndo(std::move(cmd));
				}
			}
			m_Resize.NodeId = InvalidNode;
			Invalidate(FALSE);
		}
		else if (m_Minimap.Dragging) {
			bool navigate = !m_Minimap.HasMoved;
			m_Minimap.Dragging = false;
			::ReleaseCapture();
			if (navigate) {
				RECT rc; GetClientRect(&rc);
				m_Vt.OffsetX = rc.right * 0.5f - m_Minimap.DragClickGX * m_Vt.Scale;
				m_Vt.OffsetY = rc.bottom * 0.5f - m_Minimap.DragClickGY * m_Vt.Scale;
				Invalidate(FALSE);
			}
		}
		else if (m_DraggingNode) {
			m_DraggingNode = false;
			m_DragPrimaryId = InvalidNode;
			::ReleaseCapture();

			std::vector<std::unique_ptr<UndoCmd>> moves;
			moves.reserve(m_DragOrigins.size());
			for (const auto& orig : m_DragOrigins) {
				Node* n = m_Model->GetNode(orig.Id);
				if (n && (n->X != orig.StartX || n->Y != orig.StartY)) {
					auto mc = std::make_unique<MoveCmd>();
					mc->Id = orig.Id;
					mc->OldX = orig.StartX; mc->OldY = orig.StartY;
					mc->NewX = n->X;        mc->NewY = n->Y;
					moves.push_back(std::move(mc));
				}
			}
			m_DragOrigins.clear();

			if (!moves.empty()) {
				if (moves.size() == 1) {
					PushUndo(std::move(moves[0]));
				}
				else {
					auto compound = std::make_unique<CompoundCmd>();
					compound->Cmds = std::move(moves);
					PushUndo(std::move(compound));
				}
			}
		}
		else if (m_Panning) {
			m_Panning = false;
			::ReleaseCapture();
		}
		else if (m_RubberBand.Active) {
			FinalizeRubberBand();
			::ReleaseCapture();
			Invalidate(FALSE);
		}
	}

	void CNodeGraphControl::OnMouseMove(UINT /*nFlags*/, CPoint pt) {
		int sx = pt.x, sy = pt.y;

		if (TracksHover()) {
			auto gp = m_Vt.ToGraph((float)sx, (float)sy);
			SetHoveredNode(m_Renderer.HitTestNode(*m_Model, gp.x, gp.y));
		}

		// a drag at (or past) an edge scrolls the view while the mouse stays there
		if (!m_AutoScrolling && IsDraggingInView()) {
			POINT v = AutoScrollSpeed(pt);
			if (v.x || v.y) {
				SetTimer(k_AutoScrollTimer, k_AutoScrollInterval);
				m_AutoScrolling = true;
			}
		}

		if (m_Resize.Active) {
			auto gp = m_Vt.ToGraph((float)sx, (float)sy);
			float dGX = gp.x - m_Resize.StartGX;
			float dGY = gp.y - m_Resize.StartGY;

			float left = m_Resize.StartLeft;
			float top = m_Resize.StartTop;
			float right = m_Resize.StartRight;
			float bottom = m_Resize.StartBottom;

			switch (m_Resize.Handle) {
				case ResizeHandle::NW:
					left = std::min(left + dGX, right - k_MinNodeWidth);
					top = std::min(top + dGY, bottom - k_MinNodeHeight); break;
				case ResizeHandle::N:
					top = std::min(top + dGY, bottom - k_MinNodeHeight); break;
				case ResizeHandle::NE:
					right = std::max(right + dGX, left + k_MinNodeWidth);
					top = std::min(top + dGY, bottom - k_MinNodeHeight); break;
				case ResizeHandle::E:
					right = std::max(right + dGX, left + k_MinNodeWidth); break;
				case ResizeHandle::SE:
					right = std::max(right + dGX, left + k_MinNodeWidth);
					bottom = std::max(bottom + dGY, top + k_MinNodeHeight); break;
				case ResizeHandle::S:
					bottom = std::max(bottom + dGY, top + k_MinNodeHeight); break;
				case ResizeHandle::SW:
					left = std::min(left + dGX, right - k_MinNodeWidth);
					bottom = std::max(bottom + dGY, top + k_MinNodeHeight); break;
				case ResizeHandle::W:
					left = std::min(left + dGX, right - k_MinNodeWidth); break;
				default: break;
			}

			// the sides that move snap (not past the smallest size)
			{
				const ResizeHandle h = m_Resize.Handle;
				const bool ml = h == ResizeHandle::NW || h == ResizeHandle::W || h == ResizeHandle::SW;
				const bool mr = h == ResizeHandle::NE || h == ResizeHandle::E || h == ResizeHandle::SE;
				const bool mt = h == ResizeHandle::NW || h == ResizeHandle::N || h == ResizeHandle::NE;
				const bool mb = h == ResizeHandle::SW || h == ResizeHandle::S || h == ResizeHandle::SE;
				Snap snap = SnapBox(left, top, right, bottom, ml, mt, mr, mb, { m_Resize.NodeId });
				if (ml && right - (left + snap.DX) >= k_MinNodeWidth) left += snap.DX;
				if (mr && (right + snap.DX) - left >= k_MinNodeWidth) right += snap.DX;
				if (mt && bottom - (top + snap.DY) >= k_MinNodeHeight) top += snap.DY;
				if (mb && (bottom + snap.DY) - top >= k_MinNodeHeight) bottom += snap.DY;
				m_Renderer.SetGuides(std::move(snap.Guides));
			}

			Node* rn = m_Model->GetNode(m_Resize.NodeId);
			if (rn) {
				rn->X = (left + right) * 0.5f;
				rn->Y = (top + bottom) * 0.5f;
				rn->Width = right - left;
				rn->Height = bottom - top;
			}
			Invalidate(FALSE);
		}
		else if (m_Minimap.Scrolling) {
			// the rectangle of the view follows the mouse: the graph moves the other way, at the view's scale
			RECT rc; GetClientRect(&rc);
			MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
			float dx = (float)(sx - m_Minimap.DragStart.x), dy = (float)(sy - m_Minimap.DragStart.y);
			if (dx * dx + dy * dy > 16.0f)
				m_Minimap.HasMoved = true;
			float gx, gy;
			if (m_Minimap.HasMoved && MinimapScreenToGraph(mmcfg, *m_Model, (float)sx, (float)sy, gx, gy)) {
				m_Vt.OffsetX = m_Minimap.StartOffsetX - (gx - m_Minimap.DragClickGX) * m_Vt.Scale;
				m_Vt.OffsetY = m_Minimap.StartOffsetY - (gy - m_Minimap.DragClickGY) * m_Vt.Scale;
				Invalidate(FALSE);
			}
		}
		else if (m_Minimap.Dragging) {
			float dx = (float)(sx - m_Minimap.DragStart.x);
			float dy = (float)(sy - m_Minimap.DragStart.y);
			if (!m_Minimap.HasMoved && (dx * dx + dy * dy) > 16.0f)
				m_Minimap.HasMoved = true;
			if (m_Minimap.HasMoved) {
				RECT rc; GetClientRect(&rc);
				float newX = m_Minimap.StartX + dx;
				float newY = m_Minimap.StartY + dy;
				newX = std::clamp(newX, 20.0f - m_Minimap.Width, (float)rc.right - 20.0f);
				newY = std::clamp(newY, 20.0f - m_Minimap.Height, (float)rc.bottom - 20.0f);
				m_Minimap.X = newX;
				m_Minimap.Y = newY;
				Invalidate(FALSE);
			}
		}
		else if (m_EdgePreview.Active) {
			m_EdgePreview.ScreenX = (float)sx;
			m_EdgePreview.ScreenY = (float)sy;
			Invalidate(FALSE);
		}
		else if (m_Panning) {
			m_Vt.OffsetX = m_PanStartX + (sx - m_PanStart.x);
			m_Vt.OffsetY = m_PanStartY + (sy - m_PanStart.y);
			Invalidate(FALSE);
		}
		else if (m_DraggingNode) {
			auto gp = m_Vt.ToGraph((float)sx, (float)sy);
			float baseGX = gp.x - m_DragOffsetX;
			float baseGY = gp.y - m_DragOffsetY;

			Node* primary = m_Model->GetNode(m_DragPrimaryId);
			if (primary) {
				float dx = baseGX - primary->X;
				float dy = baseGY - primary->Y;
				// the node under the mouse snaps; the others keep their places around it
				const float hw = primary->Width * 0.5f, hh = primary->Height * 0.5f;
				Snap snap = SnapBox(baseGX - hw, baseGY - hh, baseGX + hw, baseGY + hh, true, true, true, true, m_SelectedNodes);
				dx += snap.DX;
				dy += snap.DY;
				m_Renderer.SetGuides(std::move(snap.Guides));
				for (NodeId nid : m_SelectedNodes) {
					Node* n = m_Model->GetNode(nid);
					if (n) { n->X += dx; n->Y += dy; }
				}
			}
			Invalidate(FALSE);
		}
		else if (m_RubberBand.Active) {
			m_RubberBand.X1 = (float)sx;
			m_RubberBand.Y1 = (float)sy;
			Invalidate(FALSE);
		}
		else {
			// Update hovered resize handle for visual feedback.
			ResizeHandle newHov = ResizeHandle::None;
			if (!m_ReadOnly && m_SelectedNodes.size() == 1) {
				const Node* rn = m_Model->GetNode(m_SelectedNodes[0]);
				if (rn)
					newHov = m_Renderer.HitTestResizeHandle(*rn, m_Vt, (float)sx, (float)sy);
			}
			if (newHov != m_Resize.Hovered) {
				m_Resize.Hovered = newHov;
				Invalidate(FALSE);
			}
		}
	}

	void CNodeGraphControl::OnMouseLeave() {
		SetHoveredNode(InvalidNode);
	}

	// Repaints if an owner drawn node is hovered or not anymore (the others don't show it).
	void CNodeGraphControl::SetHoveredNode(NodeId id) {
		if (id == m_HoveredNode)
			return;
		if (m_HoveredNode == InvalidNode) {
			TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hWnd };
			::TrackMouseEvent(&tme);
		}
		auto ownerDrawn = [&](NodeId nid) {
			const Node* n = m_Model->GetNode(nid);
			return n && n->Style.OwnerDraw;
		};
		const bool repaint = m_Highlight || ownerDrawn(m_HoveredNode) || ownerDrawn(id);
		m_HoveredNode = id;
		m_Renderer.SetHoveredNode(id);
		if (repaint)
			Invalidate(FALSE);
	}

	void CNodeGraphControl::SetNodeDrawHandler(INodeDrawHandler* handler) {
		m_Renderer.SetNodeDrawHandler(handler);
		if (!handler)
			SetHoveredNode(InvalidNode);
		if (m_hWnd)
			Invalidate(FALSE);
	}

	INodeDrawHandler* CNodeGraphControl::GetNodeDrawHandler() const {
		return m_Renderer.GetNodeDrawHandler();
	}

	LRESULT CNodeGraphControl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
		auto* nm = reinterpret_cast<NMHDR*>(lParam);
		if (nm->code == TTN_GETDISPINFOW) {
			auto* ti = reinterpret_cast<NMTTDISPINFOW*>(lParam);

			POINT pt{}; ::GetCursorPos(&pt); ScreenToClient(&pt);
			auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
			NodeId nid = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
			EdgeId eid = (nid == InvalidNode)
				? m_Renderer.HitTestEdge(*m_Model, gp.x, gp.y)
				: InvalidEdge;

			m_TooltipBuf[0] = L'\0';
			if (nid != InvalidNode) {
				const Node* n = m_Model->GetNode(nid);
				if (n && !n->Tooltip.empty())
					wcsncpy_s(m_TooltipBuf, n->Tooltip.c_str(), _TRUNCATE);
				else if (n && !n->Style.Code)
					wcsncpy_s(m_TooltipBuf, n->Label.c_str(), _TRUNCATE);
			}
			else if (eid != InvalidEdge) {
				const Edge* e = m_Model->GetEdge(eid);
				if (e && !e->Tooltip.empty()) {
					wcsncpy_s(m_TooltipBuf, e->Tooltip.c_str(), _TRUNCATE);
				}
				else if (e) {
					const Node* f = m_Model->GetNode(e->From);
					const Node* t = m_Model->GetNode(e->To);
					if (!(f && f->Style.Code) && !(t && t->Style.Code))
						swprintf_s(m_TooltipBuf, L"%s → %s",
							f ? f->Label.c_str() : L"?",
							t ? t->Label.c_str() : L"?");
				}
			}

			if (m_TooltipBuf[0]) {
				NODEGRAPHTOOLTIPNOTIFY gtn{};
				gtn.Hdr.hwndFrom = m_hWnd;
				gtn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
				gtn.Hdr.code = NGCN_GETTOOLTIP;
				gtn.NodeId = nid;
				gtn.EdgeId = eid;
				wcsncpy_s(gtn.SzText, m_TooltipBuf, _TRUNCATE);
				HWND par = ::GetParent(m_hWnd);
				if (par) ::SendMessage(par, WM_NOTIFY, gtn.Hdr.idFrom, reinterpret_cast<LPARAM>(&gtn));
				if (gtn.SzText[0]) wcsncpy_s(m_TooltipBuf, gtn.SzText, _TRUNCATE);

				ti->lpszText = m_TooltipBuf;
				ti->hinst = nullptr;
			}
			return 0;
		}
		bHandled = FALSE;
		return 0;
	}

	// Arrows move the selected nodes (Shift: further), or scroll without a selection or with Ctrl. PageUp/PageDown
	// scroll a page (Shift: sideways), Home fits the graph in view, Tab/Shift+Tab select the next/previous node,
	// Esc clears the selection, F2 renames, Delete deletes, Ctrl+A/C/X/V/Z/Y select all, copy, cut, paste, undo, redo,
	// Ctrl+Plus/Minus/0 zoom in, out, to 100%. A read-only graph doesn't edit: moving nodes is all it does of it.
	LRESULT CNodeGraphControl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
		StopAnimation();
		const bool ctrl = ::GetKeyState(VK_CONTROL) < 0, shift = ::GetKeyState(VK_SHIFT) < 0;
		switch (wParam) {
			case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
				const float dx = wParam == VK_LEFT ? -1.0f : wParam == VK_RIGHT ? 1.0f : 0.0f;
				const float dy = wParam == VK_UP ? -1.0f : wParam == VK_DOWN ? 1.0f : 0.0f;
				if (!ctrl && !m_SelectedNodes.empty()) {
					// a grid step when snapping to the grid
					const float grid = m_Renderer.GetGridSize();
					const float step = m_SnapToGrid ? (shift ? grid * 5 : grid) : (shift ? 50.0f : 10.0f);
					MoveSelectionBy(dx * step, dy * step);
				}
				else
					ScrollBy(dx * LineSize(), dy * LineSize());
				return 0;
			}
			case VK_PRIOR: case VK_NEXT: {
				RECT rc;
				GetClientRect(&rc);
				const float page = (shift ? rc.right : rc.bottom) * 0.9f * (wParam == VK_PRIOR ? -1 : 1);
				if (shift) ScrollBy(page, 0);
				else ScrollBy(0, page);
				return 0;
			}
			case VK_HOME:
				FitInView();
				return 0;
			case VK_TAB:
				if (ctrl) break;
				SelectNext(shift);
				return 0;
			case VK_ESCAPE:
				if (!m_SelectedNodes.empty() || m_SelectedEdge != InvalidEdge)
					ClearSelection();
				return 0;
			case VK_F2:
				if (!m_ReadOnly && m_SelectedNodes.size() == 1)
					BeginEditLabel(m_SelectedNodes[0]);
				return 0;
			case VK_DELETE:
				if (!m_ReadOnly) DeleteSelected();
				return 0;
			case VK_ADD: case VK_OEM_PLUS:
				if (!ctrl) break;
				ZoomAroundCenter(1.25f);
				return 0;
			case VK_SUBTRACT: case VK_OEM_MINUS:
				if (!ctrl) break;
				ZoomAroundCenter(0.8f);
				return 0;
			case '0': case VK_NUMPAD0:
				if (!ctrl) break;
				ZoomAroundCenter(1.0f / m_Vt.Scale);
				return 0;
			case 'A':
				if (!ctrl) break;
				SelectAll();
				return 0;
			case 'C':
				if (!ctrl) break;
				Copy();
				return 0;
			case 'X': case 'V': case 'Z': case 'Y':
				if (!ctrl || m_ReadOnly) break;
				if (wParam == 'X') Cut();
				else if (wParam == 'V') Paste();
				else if (wParam == 'Z') Undo();
				else Redo();
				return 0;
			case 'F':	// in a read-only graph, as in a viewer
				if (ctrl || !m_ReadOnly) break;
				FitInView();
				return 0;
			case 'M':
				if (ctrl || !m_ReadOnly) break;
				SetMinimapVisible(!IsMinimapVisible());
				return 0;
		}
		bHandled = FALSE;
		return 0;
	}

	// In a dialog: the arrows are the graph's, Tab still moves to the next control.
	UINT CNodeGraphControl::OnGetDlgCode(LPMSG /*msg*/) {
		return DLGC_WANTARROWS | DLGC_WANTCHARS;
	}

	// WM_CONTEXTMENU, from a right click (see OnRButtonUp) or the keyboard (pt is -1, -1): the parent gets
	// NGCN_CONTEXTMENU for what it is about; if it doesn't show a menu, WM_CONTEXTMENU goes on to it.
	void CNodeGraphControl::OnContextMenu(HWND /*wnd*/, CPoint pt) {
		NODEGRAPHCONTEXTMENUNOTIFY cm{};
		cm.Hdr.hwndFrom = m_hWnd;
		cm.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		cm.Hdr.code = NGCN_CONTEXTMENU;
		cm.NodeId = InvalidNode;
		cm.EdgeId = InvalidEdge;
		cm.Keyboard = pt.x == -1 && pt.y == -1;

		RECT rc;
		GetClientRect(&rc);
		if (cm.Keyboard) {
			// the selection, or the middle of the view
			POINT at{ rc.right / 2, rc.bottom / 2 };
			if (const Node* n = m_Model->GetNode(GetSelectedNode())) {
				EnsureVisible(n->Id);
				auto c = m_Vt.ToScreen(n->X, n->Y);
				at = { (LONG)c.x, (LONG)c.y };
				cm.NodeId = n->Id;
			}
			else if (const Edge* e = m_Model->GetEdge(m_SelectedEdge)) {
				const Node* from = m_Model->GetNode(e->From);
				const Node* to = m_Model->GetNode(e->To);
				if (from && to) {
					auto c = m_Vt.ToScreen((from->X + to->X) * 0.5f, (from->Y + to->Y) * 0.5f);
					at = { std::clamp((LONG)c.x, 0L, rc.right), std::clamp((LONG)c.y, 0L, rc.bottom) };
				}
				cm.EdgeId = e->Id;
			}
			ClientToScreen(&at);
			cm.ScreenPt = at;
		}
		else {
			cm.ScreenPt = pt;
			POINT client = pt;
			ScreenToClient(&client);
			MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
			if (!IsOnMinimap(mmcfg, client.x, client.y)) {	// the minimap is in front of the graph: there it's the canvas
				auto gp = m_Vt.ToGraph((float)client.x, (float)client.y);
				cm.NodeId = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
				if (cm.NodeId == InvalidNode)
					cm.EdgeId = m_Renderer.HitTestEdge(*m_Model, gp.x, gp.y);
			}
			// what the menu is for is selected (a node in the selection keeps the others)
			if (cm.NodeId != InvalidNode && !IsSelected(cm.NodeId))
				SelectNode(cm.NodeId);
			else if (cm.EdgeId != InvalidEdge && cm.EdgeId != m_SelectedEdge)
				SelectEdge(cm.EdgeId);
		}

		HWND parent = GetParent();
		if (!parent || !::SendMessage(parent, WM_NOTIFY, cm.Hdr.idFrom, reinterpret_cast<LPARAM>(&cm)))
			SetMsgHandled(FALSE);
	}

	BOOL CNodeGraphControl::OnEraseBkgnd(CDCHandle) {
		return TRUE;  // prevent flicker; we fill the background in OnPaint
	}

	LRESULT CNodeGraphControl::OnNodeGraphEdit(UINT, WPARAM wParam, LPARAM, BOOL&) {
		if (wParam) CommitLabelEdit();
		else        CancelLabelEdit();
		return 0;
	}

	// ---- Creation / model / viewport / selection / minimap -------------------

	HWND CNodeGraphControl::Create(HWND hWndParent, int x, int y, int w, int h,
		DWORD dwStyle, DWORD ctrlStyle) {
		CRect rc(x, y, x + w, y + h);
		return CWindowImpl<CNodeGraphControl>::Create(hWndParent, rc, nullptr, dwStyle | ctrlStyle);
	}

	void CNodeGraphControl::SetModel(NodeGraphModel* model) {
		m_Model.reset(model);
		m_FittedModel = nullptr;	// a new model at the address of the old one is still new
		if (m_Model)
			m_Model->SetResetCallback([this] { OnModelReset(); });
		OnModelReset();
		RequestInvalidate();
	}

	NodeGraphModel* CNodeGraphControl::GetModel() const {
		return m_Model.get();
	}

	void CNodeGraphControl::FitInView() {
		if (!m_Model || m_Model->Nodes().empty()) return;

		const Rect bounds = m_Model->GetBounds();
		const float minX = bounds.Left, minY = bounds.Top, maxX = bounds.Right, maxY = bounds.Bottom;

		// the view without the scroll bars, which the fitted graph won't need
		RECT rc; GetClientRect(&rc);
		if (m_ScrollBars) {
			if (GetStyle() & WS_VSCROLL) rc.right += ::GetSystemMetricsForDpi(SM_CXVSCROLL, ::GetDpiForWindow(m_hWnd));
			if (GetStyle() & WS_HSCROLL) rc.bottom += ::GetSystemMetricsForDpi(SM_CYHSCROLL, ::GetDpiForWindow(m_hWnd));
		}
		const float margin = k_ViewMargin;
		float sx = (rc.right - margin * 2) / (maxX - minX);
		float sy = (rc.bottom - margin * 2) / (maxY - minY);
		m_Vt.Scale = std::clamp(std::min(sx, sy), 0.05f, 50.0f);
		// centered: the direction that doesn't fill the view gets the space on both sides
		m_Vt.OffsetX = (rc.right - (maxX - minX) * m_Vt.Scale) / 2 - minX * m_Vt.Scale;
		m_Vt.OffsetY = (rc.bottom - (maxY - minY) * m_Vt.Scale) / 2 - minY * m_Vt.Scale;
		RequestInvalidate();
	}

	void CNodeGraphControl::FitSelected() {
		if (m_SelectedNodes.empty()) return;

		float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
		for (NodeId id : m_SelectedNodes) {
			const Node* n = m_Model->GetNode(id);
			if (!n) continue;
			minX = std::min(minX, n->X - n->Width * 0.5f);
			minY = std::min(minY, n->Y - n->Height * 0.5f);
			maxX = std::max(maxX, n->X + n->Width * 0.5f);
			maxY = std::max(maxY, n->Y + n->Height * 0.5f);
		}
		if (minX == FLT_MAX) return;

		RECT rc; GetClientRect(&rc);
		float margin = 40.0f;
		float sx = (rc.right - margin * 2) / std::max(maxX - minX, 1.0f);
		float sy = (rc.bottom - margin * 2) / std::max(maxY - minY, 1.0f);
		m_Vt.Scale = std::clamp(std::min(sx, sy), 0.05f, 50.0f);
		m_Vt.OffsetX = (rc.right - (maxX - minX) * m_Vt.Scale) / 2 - minX * m_Vt.Scale;
		m_Vt.OffsetY = (rc.bottom - (maxY - minY) * m_Vt.Scale) / 2 - minY * m_Vt.Scale;
		RequestInvalidate();
	}

	void CNodeGraphControl::SetZoom(float factor) {
		m_Vt.Scale = std::clamp(factor, 0.05f, 50.0f);
		RequestInvalidate();
	}

	void CNodeGraphControl::CenterOn(float graphX, float graphY, float zoom) {
		if (zoom > 0.0f)
			m_Vt.Scale = std::clamp(zoom, 0.05f, 50.0f);
		RECT rc; GetClientRect(&rc);
		m_Vt.OffsetX = rc.right * 0.5f - graphX * m_Vt.Scale;
		m_Vt.OffsetY = rc.bottom * 0.5f - graphY * m_Vt.Scale;
		RequestInvalidate();
	}

	float CNodeGraphControl::GetZoom() const {
		return m_Vt.Scale;
	}

	// ---- Scrolling ------------------------------------------------------------------

	size_t CNodeGraphControl::RemoveUnusedImages() {
		if (!m_Model) return 0;
		std::vector<ImageId> keep;
		for (const auto& cmd : m_UndoStack) cmd->CollectImages(keep);
		for (const auto& cmd : m_RedoStack) cmd->CollectImages(keep);
		return m_Model->RemoveUnusedImages(keep);
	}

	// ---- Snapping, alignment --------------------------------------------------------

	void CNodeGraphControl::SetSnapToGrid(bool snap) { m_SnapToGrid = snap; }
	bool CNodeGraphControl::GetSnapToGrid() const { return m_SnapToGrid; }
	void CNodeGraphControl::SetAlignmentGuides(bool guides) { m_Guides = guides; }
	bool CNodeGraphControl::GetAlignmentGuides() const { return m_Guides; }

	void CNodeGraphControl::SetGridSize(float size) {
		if (size <= 0) return;
		m_Renderer.SetGridSize(size);
		if (m_hWnd) Invalidate(FALSE);
	}

	float CNodeGraphControl::GetGridSize() const {
		return m_Renderer.GetGridSize();
	}

	// Where a node's box (graph units) goes when snapped: how far it (its moving sides, when resizing) shifts, and the
	// guides that show what it lined up with. On each axis the other nodes' sides and centers (within 6 pixels) win over
	// the grid. Nothing with Alt held. skip: the nodes that move (they don't line up with themselves).
	CNodeGraphControl::Snap CNodeGraphControl::SnapBox(float left, float top, float right, float bottom,
		bool moveLeft, bool moveTop, bool moveRight, bool moveBottom, const std::vector<NodeId>& skip) const {
		Snap snap;
		if ((!m_SnapToGrid && !m_Guides) || ::GetKeyState(VK_MENU) < 0)
			return snap;
		const std::unordered_set<NodeId> moving(skip.begin(), skip.end());
		const float tolerance = 6.0f / m_Vt.Scale, grid = m_Renderer.GetGridSize();

		// one axis: lo..hi on it, otherLo..otherHi on the other (for the guide's length)
		auto axis = [&](bool x, float lo, float hi, bool moveLo, bool moveHi, float otherLo, float otherHi, float& shift) {
			const bool whole = moveLo && moveHi;
			float mine[3];
			int count = 0;
			if (moveLo) mine[count++] = lo;
			if (whole) mine[count++] = (lo + hi) * 0.5f;
			if (moveHi) mine[count++] = hi;
			if (count == 0) return;

			if (m_Guides) {
				float best = tolerance;
				GuideLine guide{};
				bool found = false;
				for (const auto& n : m_Model->Nodes()) {
					if (moving.contains(n.Id)) continue;
					const float c = x ? n.X : n.Y, half = (x ? n.Width : n.Height) * 0.5f;
					const float other = x ? n.Y : n.X, otherHalf = (x ? n.Height : n.Width) * 0.5f;
					const float theirs[3] = { c - half, c, c + half };
					for (int i = 0; i < count; i++)
						for (float f : theirs)
							if (std::fabs(f - mine[i]) < std::fabs(best)) {
								best = f - mine[i];
								found = true;
								guide = { x, f, std::min(otherLo, other - otherHalf), std::max(otherHi, other + otherHalf) };
							}
				}
				if (found) {
					shift = best;
					snap.Guides.push_back(guide);
					return;
				}
			}
			if (m_SnapToGrid) {
				const float anchor = moveLo ? lo : hi;		// the box's left (top) side, or the side that moves
				shift = std::round(anchor / grid) * grid - anchor;
			}
		};
		axis(true, left, right, moveLeft, moveRight, top, bottom, snap.DX);
		axis(false, top, bottom, moveTop, moveBottom, left, right, snap.DY);
		return snap;
	}

	// The nodes moved (from their old places to where they are now): one undo step.
	void CNodeGraphControl::PushMoves(const std::vector<NodeId>& ids, const std::vector<Point>& old) {
		auto move = std::make_unique<KeyMoveCmd>();
		move->Ids = ids;
		move->Old = old;
		move->New.reserve(ids.size());
		bool moved = false;
		for (size_t i = 0; i < ids.size(); i++) {
			const Node* n = m_Model->GetNode(ids[i]);
			move->New.push_back(n ? Point{ n->X, n->Y } : old[i]);
			moved |= move->New[i].X != old[i].X || move->New[i].Y != old[i].Y;
		}
		if (moved)
			PushUndo(std::move(move));
	}

	void CNodeGraphControl::AlignSelection(Alignment how) {
		if (m_SelectedNodes.size() < 2) return;
		const Node* first = m_Model->GetNode(m_SelectedNodes[0]);
		if (!first) return;
		const Rect to{ first->X - first->Width * 0.5f, first->Y - first->Height * 0.5f, first->X + first->Width * 0.5f, first->Y + first->Height * 0.5f };
		std::vector<Point> old;
		old.reserve(m_SelectedNodes.size());
		for (NodeId id : m_SelectedNodes) {
			Node* n = m_Model->GetNode(id);
			old.push_back(n ? Point{ n->X, n->Y } : Point{});
			if (!n) continue;
			switch (how) {
				case Alignment::Left:   n->X = to.Left + n->Width * 0.5f; break;
				case Alignment::Center: n->X = first->X; break;
				case Alignment::Right:  n->X = to.Right - n->Width * 0.5f; break;
				case Alignment::Top:    n->Y = to.Top + n->Height * 0.5f; break;
				case Alignment::Middle: n->Y = first->Y; break;
				case Alignment::Bottom: n->Y = to.Bottom - n->Height * 0.5f; break;
			}
		}
		PushMoves(m_SelectedNodes, old);
		RequestInvalidate();
	}

	void CNodeGraphControl::DistributeSelection(bool horizontally) {
		std::vector<Node*> nodes;
		nodes.reserve(m_SelectedNodes.size());
		for (NodeId id : m_SelectedNodes)
			if (Node* n = m_Model->GetNode(id)) nodes.push_back(n);
		if (nodes.size() < 3) return;
		auto pos = [&](const Node* n) -> float { return horizontally ? n->X : n->Y; };
		auto size = [&](const Node* n) -> float { return horizontally ? n->Width : n->Height; };
		std::vector<Point> old;
		old.reserve(m_SelectedNodes.size());
		for (NodeId id : m_SelectedNodes) {
			const Node* n = m_Model->GetNode(id);
			old.push_back(n ? Point{ n->X, n->Y } : Point{});
		}

		// in their order along the axis, the gaps between them the same; the first and the last stay
		std::sort(nodes.begin(), nodes.end(), [&](const Node* a, const Node* b) { return pos(a) < pos(b); });
		const float start = pos(nodes.front()) - size(nodes.front()) * 0.5f, end = pos(nodes.back()) + size(nodes.back()) * 0.5f;
		float sizes = 0;
		for (const Node* n : nodes) sizes += size(n);
		const float gap = (end - start - sizes) / (nodes.size() - 1);
		float at = start + size(nodes.front()) + gap;
		for (size_t i = 1; i + 1 < nodes.size(); i++) {		// the outermost aren't moved at all
			(horizontally ? nodes[i]->X : nodes[i]->Y) = at + size(nodes[i]) * 0.5f;
			at += size(nodes[i]) + gap;
		}
		PushMoves(m_SelectedNodes, old);
		RequestInvalidate();
	}

	void CNodeGraphControl::SetDetailThreshold(float scale) {
		m_Renderer.SetDetailThreshold(std::max(scale, 0.0f));
		if (m_hWnd) Invalidate(FALSE);
	}

	float CNodeGraphControl::GetDetailThreshold() const {
		return m_Renderer.GetDetailThreshold();
	}

	void CNodeGraphControl::SetHighlight(bool highlight) {
		m_Highlight = highlight;
		if (!TracksHover())
			SetHoveredNode(InvalidNode);
		if (m_hWnd) Invalidate(FALSE);
	}

	bool CNodeGraphControl::GetHighlight() const {
		return m_Highlight;
	}

	void CNodeGraphControl::SelectNodes(const std::vector<NodeId>& ids) {
		m_SelectedNodes.clear();
		m_SelectedNodes.reserve(ids.size());
		std::unordered_set<NodeId> added;		// once each (thousands of found nodes: not a search of the selection each)
		added.reserve(ids.size());
		for (NodeId id : ids)
			if (m_Model->GetNode(id) && added.insert(id).second)
				m_SelectedNodes.push_back(id);
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	bool CNodeGraphControl::FindNext(const std::wstring& text, bool backwards, bool matchCase) {
		const std::vector<NodeId> found = m_Model->FindNodes(text, matchCase);
		if (found.empty())
			return false;
		// the first one after (before) the selected node, in the order of the model
		const Node* current = m_Model->GetNode(GetSelectedNode());
		const size_t at = current ? current - m_Model->Nodes().data() : (backwards ? m_Model->Nodes().size() : SIZE_MAX);
		auto index = [&](NodeId id) { return (size_t)(m_Model->GetNode(id) - m_Model->Nodes().data()); };
		NodeId next = backwards ? found.back() : found.front();		// around
		if (backwards) {
			for (auto it = found.rbegin(); it != found.rend(); ++it)
				if (index(*it) < at) { next = *it; break; }
		}
		else {
			for (NodeId id : found)
				if (at == SIZE_MAX || index(id) > at) { next = id; break; }
		}
		SelectNode(next);
		EnsureVisible(next);
		return true;
	}

	void CNodeGraphControl::SetAutoZoom(bool autoZoom) {
		m_AutoZoom = autoZoom;
	}

	bool CNodeGraphControl::GetAutoZoom() const {
		return m_AutoZoom;
	}

	void CNodeGraphControl::SetScrollBars(bool show) {
		m_ScrollBars = show;
		if (m_hWnd) {
			UpdateScrollBars();
			Invalidate(FALSE);
		}
	}

	bool CNodeGraphControl::HasScrollBars() const {
		return m_ScrollBars;
	}

	void CNodeGraphControl::SetWheelScrolls(bool scrolls) {
		m_WheelScrolls = scrolls;
	}

	bool CNodeGraphControl::GetWheelScrolls() const {
		return m_WheelScrolls;
	}

	float CNodeGraphControl::LineSize() const {
		return 20.0f * ::GetDpiForWindow(m_hWnd) / 96;
	}

	void CNodeGraphControl::ScrollBy(float dx, float dy) {
		if (dx == 0 && dy == 0) return;
		m_Vt.OffsetX -= dx;
		m_Vt.OffsetY -= dy;
		Invalidate(FALSE);
	}

	// A range that covers the graph (with the margin FitInView leaves) and the view; a bar is shown only when the graph
	// doesn't fit. Showing or hiding one changes the size of the view, so this goes again until it doesn't.
	void CNodeGraphControl::UpdateScrollBars() {
		if (m_UpdatingScroll || m_TrackingThumb)
			return;
		m_UpdatingScroll = true;
		const Rect bounds = m_ScrollBars && m_Model ? m_Model->GetBounds() : Rect{};

		for (int pass = 0; pass < 3; pass++) {
			RECT rc;
			GetClientRect(&rc);
			auto axis = [&](int bar, float lo, float hi, int page) {
				SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
				si.nMax = 0;
				si.nPage = 1;		// a range smaller than the page: the bar is hidden
				lo -= k_ViewMargin;
				hi += k_ViewMargin;
				if (!bounds.IsEmpty() && (lo < -0.5f || hi > page + 0.5f)) {
					// whole pixels outward: the view starts at a whole position, and at the end it sees the end
					const float start = std::floor(std::min(lo, 0.0f)), end = std::ceil(std::max(hi, (float)page));
					si.nMax = std::max((int)(end - start) - 1, page);
					si.nPage = page;
					si.nPos = (int)-start;
				}
				SCROLLINFO now{ sizeof(now), SIF_RANGE | SIF_PAGE | SIF_POS };
				if (!GetScrollInfo(bar, &now) || now.nMin != si.nMin || now.nMax != si.nMax || now.nPage != si.nPage || now.nPos != si.nPos)
					SetScrollInfo(bar, &si, TRUE);
			};
			axis(SB_HORZ, bounds.Left * m_Vt.Scale + m_Vt.OffsetX, bounds.Right * m_Vt.Scale + m_Vt.OffsetX, rc.right);
			axis(SB_VERT, bounds.Top * m_Vt.Scale + m_Vt.OffsetY, bounds.Bottom * m_Vt.Scale + m_Vt.OffsetY, rc.bottom);

			RECT after;
			GetClientRect(&after);
			if (after.right == rc.right && after.bottom == rc.bottom)
				break;
		}
		m_UpdatingScroll = false;
	}

	void CNodeGraphControl::OnScroll(int bar, int code) {
		StopAnimation();
		SCROLLINFO si{ sizeof(si), SIF_ALL };
		if (!GetScrollInfo(bar, &si))
			return;
		const int page = (int)si.nPage;
		int pos = si.nPos;
		switch (code) {
			case SB_LINEUP:        pos -= (int)LineSize(); break;
			case SB_LINEDOWN:      pos += (int)LineSize(); break;
			case SB_PAGEUP:        pos -= page * 9 / 10; break;
			case SB_PAGEDOWN:      pos += page * 9 / 10; break;
			case SB_TOP:           pos = si.nMin; break;
			case SB_BOTTOM:        pos = si.nMax; break;
			case SB_THUMBTRACK:    m_TrackingThumb = true; pos = si.nTrackPos; break;
			case SB_THUMBPOSITION: pos = si.nTrackPos; break;
			case SB_ENDSCROLL:
				m_TrackingThumb = false;
				UpdateScrollBars();
				return;
			default: return;
		}
		pos = std::clamp(pos, si.nMin, std::max(si.nMin, si.nMax - page + 1));
		if (pos == si.nPos)
			return;
		const float delta = (float)(pos - si.nPos);
		si.fMask = SIF_POS;
		si.nPos = pos;
		SetScrollInfo(bar, &si, TRUE);
		if (bar == SB_HORZ) ScrollBy(delta, 0);
		else ScrollBy(0, delta);
	}

	void CNodeGraphControl::OnHScroll(int code, short /*pos*/, HWND /*scrollBar*/) {
		OnScroll(SB_HORZ, code);
	}

	void CNodeGraphControl::OnVScroll(int code, short /*pos*/, HWND /*scrollBar*/) {
		OnScroll(SB_VERT, code);
	}

	LRESULT CNodeGraphControl::OnMouseHWheel(UINT, WPARAM wParam, LPARAM, BOOL&) {
		StopAnimation();
		UINT chars = 3;
		::SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &chars, 0);
		ScrollBy(GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA * chars * LineSize(), 0);
		return 0;
	}

	bool CNodeGraphControl::IsDraggingInView() const {
		return m_DraggingNode || m_Resize.Active || m_RubberBand.Active || m_EdgePreview.Active;
	}

	// How far to scroll on a tick of the timer: the deeper into the band along an edge (or past it) the mouse is, the
	// faster. Zero away from the edges.
	POINT CNodeGraphControl::AutoScrollSpeed(POINT pt) const {
		RECT rc;
		GetClientRect(&rc);
		const float dpi = ::GetDpiForWindow(m_hWnd) / 96.0f;
		const float band = 24 * dpi, maxSpeed = 40 * dpi;
		auto speed = [&](int at, int size) -> int {
			float depth = at < band ? at - band : at > size - band ? at - (size - band) : 0;
			if (depth == 0) return 0;
			float v = std::clamp(std::fabs(depth) * 0.5f, 1.0f, maxSpeed);
			return (int)(depth < 0 ? -v : v);
		};
		return { speed(pt.x, rc.right), speed(pt.y, rc.bottom) };
	}

	// Scrolls a step and does the drag again where the mouse is, now over another part of the graph.
	void CNodeGraphControl::AutoScroll(POINT pt) {
		POINT v = AutoScrollSpeed(pt);
		if (v.x == 0 && v.y == 0) return;
		ScrollBy((float)v.x, (float)v.y);
		// the start of the rubber band stays on the same point of the graph
		m_RubberBand.X0 -= v.x;
		m_RubberBand.Y0 -= v.y;
		OnMouseMove(0, CPoint(pt));
	}

	void CNodeGraphControl::OnTimer(UINT_PTR id) {
		if (id == k_AnimationTimer) {
			if (m_Animation.Active && AnimationProgress() >= 1.0f)
				StopAnimation();	// the last frame: the new places
			else
				Invalidate(FALSE);
			return;
		}
		if (id != k_AutoScrollTimer) {
			SetMsgHandled(FALSE);
			return;
		}
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		POINT v = AutoScrollSpeed(pt);
		if (!IsDraggingInView() || ::GetCapture() != m_hWnd || (v.x == 0 && v.y == 0)) {
			KillTimer(k_AutoScrollTimer);
			m_AutoScrolling = false;
			return;
		}
		AutoScroll(pt);
	}

	NodeId CNodeGraphControl::GetSelectedNode() const {
		return m_SelectedNodes.empty() ? InvalidNode : m_SelectedNodes[0];
	}

	NodeId CNodeGraphControl::NodeFromPoint(POINT pt) const {
		if (!m_Model)
			return InvalidNode;
		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		return m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
	}

	Point CNodeGraphControl::ClientToGraph(POINT pt) const {
		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		return { gp.x, gp.y };
	}

	EdgeId CNodeGraphControl::GetSelectedEdge() const {
		return m_SelectedEdge;
	}

	std::vector<NodeId> CNodeGraphControl::GetSelectedNodes() const {
		return m_SelectedNodes;
	}

	void CNodeGraphControl::SelectNode(NodeId id, bool addToSelection) {
		if (!addToSelection) m_SelectedNodes.clear();
		if (!IsSelected(id)) m_SelectedNodes.push_back(id);
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	void CNodeGraphControl::ClearSelection() {
		m_SelectedNodes.clear();
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	void CNodeGraphControl::SelectEdge(EdgeId id) {
		m_SelectedNodes.clear();
		m_SelectedEdge = m_Model->GetEdge(id) ? id : InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	void CNodeGraphControl::SelectAll() {
		m_SelectedNodes.clear();
		m_SelectedNodes.reserve(m_Model->Nodes().size());
		for (const auto& n : m_Model->Nodes())
			m_SelectedNodes.push_back(n.Id);
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	// The least scrolling that brings the node in view (with the margin a fitted graph has); centered if it's larger.
	void CNodeGraphControl::EnsureVisible(NodeId id) {
		const Node* n = m_Model->GetNode(id);
		if (!n) return;
		RECT rc;
		GetClientRect(&rc);
		auto axis = [](float lo, float hi, float view) -> float {	// how far the view moves
			if (hi - lo > view - 2 * k_ViewMargin) return (lo + hi - view) * 0.5f;
			if (lo < k_ViewMargin) return lo - k_ViewMargin;
			if (hi > view - k_ViewMargin) return hi - (view - k_ViewMargin);
			return 0;
		};
		auto tl = m_Vt.ToScreen(n->X - n->Width * 0.5f, n->Y - n->Height * 0.5f);
		auto br = m_Vt.ToScreen(n->X + n->Width * 0.5f, n->Y + n->Height * 0.5f);
		ScrollBy(axis(tl.x, br.x, (float)rc.right), axis(tl.y, br.y, (float)rc.bottom));
	}

	void CNodeGraphControl::Cut() {
		if (m_ReadOnly || m_SelectedNodes.empty()) return;
		Copy();
		DeleteSelected();
	}

	void CNodeGraphControl::MoveSelectionBy(float dx, float dy) {
		if (m_SelectedNodes.empty()) return;
		// the same nodes moved again: the same undo step
		auto* cmd = m_UndoStack.empty() ? nullptr : dynamic_cast<KeyMoveCmd*>(m_UndoStack.back().get());
		if (!cmd || cmd != m_KeyMove || cmd->Ids != m_SelectedNodes) {
			auto move = std::make_unique<KeyMoveCmd>();
			move->Ids = m_SelectedNodes;
			move->Old.reserve(m_SelectedNodes.size());
			for (NodeId id : m_SelectedNodes) {
				const Node* n = m_Model->GetNode(id);
				move->Old.push_back(n ? Point{ n->X, n->Y } : Point{});
			}
			move->New = move->Old;
			cmd = move.get();
			PushUndo(std::move(move));
			m_KeyMove = cmd;
		}
		for (size_t i = 0; i < cmd->Ids.size(); i++)
			if (Node* n = m_Model->GetNode(cmd->Ids[i])) {
				n->X += dx;
				n->Y += dy;
				cmd->New[i] = { n->X, n->Y };
			}
		EnsureVisible(m_SelectedNodes[0]);
		RequestInvalidate();
	}

	// The node after (before) the selected one, in the order of the model, around.
	void CNodeGraphControl::SelectNext(bool backwards) {
		const auto& nodes = m_Model->Nodes();
		if (nodes.empty()) return;
		const Node* current = m_Model->GetNode(GetSelectedNode());
		size_t index = backwards ? nodes.size() - 1 : 0;
		if (current) {
			size_t at = current - nodes.data();
			index = backwards ? (at + nodes.size() - 1) % nodes.size() : (at + 1) % nodes.size();
		}
		SelectNode(nodes[index].Id);
		EnsureVisible(nodes[index].Id);
	}

	void CNodeGraphControl::ZoomAroundCenter(float factor) {
		RECT rc;
		GetClientRect(&rc);
		const float cx = rc.right * 0.5f, cy = rc.bottom * 0.5f;
		const float scale = std::clamp(m_Vt.Scale * factor, 0.05f, 50.0f);
		m_Vt.OffsetX = cx - (cx - m_Vt.OffsetX) * scale / m_Vt.Scale;
		m_Vt.OffsetY = cy - (cy - m_Vt.OffsetY) * scale / m_Vt.Scale;
		m_Vt.Scale = scale;
		Invalidate(FALSE);
	}

	void CNodeGraphControl::Refresh() {
		Invalidate(FALSE);
	}

	void CNodeGraphControl::SetBackgroundColor(COLORREF color) {
		m_Renderer.SetBackgroundColor(color);
		if (m_hWnd)
			RequestInvalidate();
	}

	COLORREF CNodeGraphControl::GetBackgroundColor() const {
		return m_Renderer.GetBackgroundColor();
	}

	bool CNodeGraphControl::SaveImage(const wchar_t* path, float scale) {
		if (!m_Model || m_Model->Nodes().empty() || scale <= 0)
			return false;

		// the extent of the graph, as SaveSvg has it
		const Rect bounds = m_Model->GetBounds();
		const float margin = 24.0f;
		float minX = bounds.Left - margin, minY = bounds.Top - margin, maxX = bounds.Right + margin, maxY = bounds.Bottom + margin;

		// Direct2D can't draw a bitmap larger than the device allows (16384 a side is common), and memory is limited
		const float maxSide = 16384.0f, maxPixels = 32.0f * 1024 * 1024;
		float w = maxX - minX, h = maxY - minY;
		scale = std::min({ scale, maxSide / w, maxSide / h, std::sqrt(maxPixels / (w * h)) });
		int cx = std::max(1, (int)(w * scale)), cy = std::max(1, (int)(h * scale));

		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = cx;
		bmi.bmiHeader.biHeight = -cy;	// top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP hBitmap = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (!hBitmap)
			return false;
		HDC hdc = ::CreateCompatibleDC(nullptr);
		auto old = ::SelectObject(hdc, hBitmap);

		ViewTransform vt;
		vt.Scale = scale;
		vt.OffsetX = -minX * scale;
		vt.OffsetY = -minY * scale;
		RECT rc{ 0, 0, cx, cy };
		m_Renderer.SetHighlight({});	// an image of the graph, not of what the mouse is on
		m_Renderer.SetExporting(true);
		m_Renderer.Render(hdc, rc, *m_Model, vt, {}, InvalidEdge, false);
		m_Renderer.SetExporting(false);
		::GdiFlush();
		::SelectObject(hdc, old);
		::DeleteDC(hdc);

		auto ext = ::PathFindExtensionW(path);
		GUID format = GUID_ContainerFormatPng;
		if (::_wcsicmp(ext, L".jpg") == 0 || ::_wcsicmp(ext, L".jpeg") == 0)
			format = GUID_ContainerFormatJpeg;
		else if (::_wcsicmp(ext, L".bmp") == 0)
			format = GUID_ContainerFormatBmp;

		bool ok = false;
		Imaging::ComScope com;
		ComPtr<IWICImagingFactory> factory;
		ComPtr<IWICBitmap> bitmap;
		ComPtr<IWICStream> stream;
		ComPtr<IWICBitmapEncoder> encoder;
		ComPtr<IWICBitmapFrameEncode> frame;
		ComPtr<IPropertyBag2> props;
		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
		if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()))) &&
			SUCCEEDED(factory->CreateBitmapFromHBITMAP(hBitmap, nullptr, WICBitmapIgnoreAlpha, bitmap.GetAddressOf())) &&
			SUCCEEDED(factory->CreateStream(stream.GetAddressOf())) &&
			SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
			SUCCEEDED(factory->CreateEncoder(format, nullptr, encoder.GetAddressOf())) &&
			SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
			SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf())) &&
			SUCCEEDED(frame->Initialize(props.Get())) &&
			SUCCEEDED(frame->SetSize(cx, cy)) &&
			SUCCEEDED(frame->SetPixelFormat(&pixelFormat)) &&
			SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
			SUCCEEDED(frame->Commit()) &&
			SUCCEEDED(encoder->Commit()))
			ok = true;
		::DeleteObject(hBitmap);
		return ok;
	}

	bool CNodeGraphControl::SaveSvg(const wchar_t* path, float pictureScale) {
		if (!m_Model)
			return false;
		NodeGraphModel::NodePictureFunc picture;
		if (m_Renderer.GetNodeDrawHandler() && pictureScale > 0) {
			picture = [&](const Node& n) {
				NodeGraphModel::NodePicture pic;
				if (n.Style.OwnerDraw && !m_Renderer.RenderNodePicture(*m_Model, n, pictureScale, pic.Png, pic.Bounds))
					pic.Png.clear();	// drawn as SVG then, without the owner drawing
				return pic;
			};
		}
		return m_Model->SaveSvg(path, GetBackgroundColor(), picture);
	}

	void CNodeGraphControl::ApplyLayout(LayoutAlgorithm algorithm, const LayoutOptions& options, bool fitInView) {
		if (!m_Model || m_Model->Nodes().empty()) return;

		StopAnimation();	// another one going on ends where it goes
		auto cmd = std::make_unique<LayoutCmd>();
		LayoutCmd::Capture(*m_Model, cmd->OldPlaces, cmd->OldRoutes);
		Layout(*m_Model, algorithm, options);
		LayoutCmd::Capture(*m_Model, cmd->NewPlaces, cmd->NewRoutes);

		const ViewTransform before = m_Vt;
		if (fitInView) FitInView();
		else RequestInvalidate();

		// the drawing goes from the old places to the new ones; the model has the new ones already
		if (m_AnimationTime > 0 && m_hWnd && IsWindowVisible()) {
			m_Animation.From.clear();
			m_Animation.From.reserve(cmd->OldPlaces.size());
			for (const auto& p : cmd->OldPlaces)
				m_Animation.From.push_back({ p.Id, { p.X, p.Y } });
			m_Animation.FromBends.clear();
			m_Animation.FromBends.reserve(cmd->OldRoutes.size());
			for (auto& r : cmd->OldRoutes)
				m_Animation.FromBends.push_back({ r.Id, r.Waypoints });
			m_Animation.FromView = before;
			m_Animation.ToView = m_Vt;
			m_Animation.Start = ::GetTickCount64();
			m_Animation.Active = true;
			SetTimer(k_AnimationTimer, 15);
		}
		PushUndo(std::move(cmd));
	}

	void CNodeGraphControl::SetLayoutAnimation(UINT milliseconds) {
		m_AnimationTime = milliseconds;
		if (milliseconds == 0) StopAnimation();
	}

	UINT CNodeGraphControl::GetLayoutAnimation() const {
		return m_AnimationTime;
	}

	bool CNodeGraphControl::IsAnimating() const {
		return m_Animation.Active;
	}

	// The drawing jumps to the new places.
	void CNodeGraphControl::StopAnimation() {
		if (!m_Animation.Active) return;
		m_Animation.Active = false;
		m_Animation.From.clear();
		m_Animation.FromBends.clear();
		if (m_hWnd) {
			KillTimer(k_AnimationTimer);
			Invalidate(FALSE);
		}
	}

	// Slow at the start and the end (cubic).
	float CNodeGraphControl::AnimationProgress() const {
		const float t = std::clamp((float)(::GetTickCount64() - m_Animation.Start) / std::max(m_AnimationTime, 1u), 0.0f, 1.0f);
		return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
	}

	void CNodeGraphControl::SetMinimapVisible(bool visible) {
		m_Minimap.Visible = visible;
		RequestInvalidate();
	}

	bool CNodeGraphControl::IsMinimapVisible() const {
		return m_Minimap.Visible;
	}

	void CNodeGraphControl::SetMinimapPosition(int x, int y) {
		m_Minimap.X = (x < 0) ? -1.0f : (float)x;
		m_Minimap.Y = (y < 0) ? -1.0f : (float)y;
		RequestInvalidate();
	}

	template<typename T, T Node::*Member>
	void CNodeGraphControl::SetNodeMember(NodeId id, const T& value, bool undoable) {
		Node* n = m_Model->GetNode(id);
		if (!n) return;
		if (undoable) {
			auto cmd = std::make_unique<NodeMemberCmd<T, Member>>();
			cmd->Id = id;
			cmd->Old = n->*Member;
			cmd->New = value;
			PushUndo(std::move(cmd));
		}
		n->*Member = value;
		RequestInvalidate();
	}

	void CNodeGraphControl::SetNodeStyle(NodeId id, const NodeStyle& style, bool undoable) {
		SetNodeMember<NodeStyle, &Node::Style>(id, style, undoable);
	}

	void CNodeGraphControl::SetNodeContent(NodeId id, const NodeContent& content, bool undoable) {
		SetNodeMember<NodeContent, &Node::Content>(id, content, undoable);
	}

	void CNodeGraphControl::SetEdgeStyle(EdgeId id, const EdgeStyle& style) {
		Edge* e = m_Model->GetEdge(id);
		if (e) { e->Style = style; RequestInvalidate(); }
	}

	void CNodeGraphControl::SetNodeSize(NodeId id, float w, float h) {
		Node* n = m_Model->GetNode(id);
		if (!n) return;
		n->Width = std::max(w, k_MinNodeWidth);
		n->Height = std::max(h, k_MinNodeHeight);
		RequestInvalidate();
	}

	void CNodeGraphControl::SetDefaultNodeSize(float w, float h) {
		m_Model->SetDefaultNodeSize(w, h);
	}

	// ---- Register ---------------------------------------------------------------

	bool Register(HINSTANCE /*hInstance*/) {
		INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
		InitCommonControlsEx(&icc);
		// Pre-register the window class; CWindowImpl::Create() also does this lazily.
		WNDPROC proc = nullptr;		// ATL requires it, even though it is only used for superclassing
		ATOM a = CNodeGraphControl::GetWndClassInfo().Register(&proc);
		return a != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}

	// ---- NodeGraphModel implementation ----------------------------------------------

	NodeId NodeGraphModel::AddNode(std::wstring label, float x, float y, float w, float h) {
		Node n;
		n.Id = m_NextNodeId++;
		n.Label = std::move(label);
		n.X = x;
		n.Y = y;
		n.Width = (w > 0.0f) ? w : m_DefaultNodeWidth;
		n.Height = (h > 0.0f) ? h : m_DefaultNodeHeight;
		m_Nodes.push_back(std::move(n));
		if (m_NodeIndexValid)
			m_NodeIndex[m_Nodes.back().Id] = (uint32_t)m_Nodes.size() - 1;
		return m_Nodes.back().Id;
	}

	void NodeGraphModel::SetDefaultNodeSize(float w, float h) {
		if (w > 0.0f) m_DefaultNodeWidth = w;
		if (h > 0.0f) m_DefaultNodeHeight = h;
	}

	EdgeId NodeGraphModel::AddEdge(NodeId from, NodeId to, std::wstring label) {
		Edge e;
		e.Id = m_NextEdgeId++;
		e.From = from;
		e.To = to;
		e.Label = std::move(label);
		m_Edges.push_back(std::move(e));
		if (m_EdgeIndexValid)
			m_EdgeIndex[m_Edges.back().Id] = (uint32_t)m_Edges.size() - 1;
		return m_Edges.back().Id;
	}

	bool NodeGraphModel::RemoveNode(NodeId id) {
		const Node* n = FindNode(id);
		if (!n) return false;
		m_Nodes.erase(m_Nodes.begin() + (n - m_Nodes.data()));
		NodesChanged();
		if (std::erase_if(m_Edges, [id](const Edge& e) { return e.From == id || e.To == id; }))
			EdgesChanged();
		return true;
	}

	size_t NodeGraphModel::RemoveNodes(const std::vector<NodeId>& ids) {
		if (ids.empty()) return 0;
		const std::unordered_set<NodeId> gone(ids.begin(), ids.end());
		const size_t removed = std::erase_if(m_Nodes, [&](const Node& n) { return gone.contains(n.Id); });
		if (!removed) return 0;
		NodesChanged();
		if (std::erase_if(m_Edges, [&](const Edge& e) { return gone.contains(e.From) || gone.contains(e.To); }))
			EdgesChanged();
		return removed;
	}

	bool NodeGraphModel::RemoveEdge(EdgeId id) {
		const Edge* e = FindEdge(id);
		if (!e) return false;
		m_Edges.erase(m_Edges.begin() + (e - m_Edges.data()));
		EdgesChanged();
		return true;
	}

	void NodeGraphModel::RestoreNode(const Node& n) {
		m_Nodes.push_back(n);
		NodesChanged();
	}

	void NodeGraphModel::RestoreEdge(const Edge& e) {
		m_Edges.push_back(e);
		EdgesChanged();
	}

	const Node* NodeGraphModel::FindNode(NodeId id) const {
		if (!m_NodeIndexValid) {
			m_NodeIndex.clear();
			m_NodeIndex.reserve(m_Nodes.size());
			for (uint32_t i = 0; i < m_Nodes.size(); i++)
				m_NodeIndex[m_Nodes[i].Id] = i;
			m_NodeIndexValid = true;
		}
		auto it = m_NodeIndex.find(id);
		return it == m_NodeIndex.end() ? nullptr : &m_Nodes[it->second];
	}

	const Edge* NodeGraphModel::FindEdge(EdgeId id) const {
		if (!m_EdgeIndexValid) {
			m_EdgeIndex.clear();
			m_EdgeIndex.reserve(m_Edges.size());
			for (uint32_t i = 0; i < m_Edges.size(); i++)
				m_EdgeIndex[m_Edges[i].Id] = i;
			m_EdgeIndexValid = true;
		}
		auto it = m_EdgeIndex.find(id);
		return it == m_EdgeIndex.end() ? nullptr : &m_Edges[it->second];
	}

	Node* NodeGraphModel::GetNode(NodeId id) {
		return const_cast<Node*>(FindNode(id));
	}

	Edge* NodeGraphModel::GetEdge(EdgeId id) {
		return const_cast<Edge*>(FindEdge(id));
	}

	const Node* NodeGraphModel::GetNode(NodeId id) const {
		return FindNode(id);
	}

	const Edge* NodeGraphModel::GetEdge(EdgeId id) const {
		return FindEdge(id);
	}

	Rect NodeGraphModel::GetBounds() const {
		if (m_Nodes.empty())
			return {};
		Rect r{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
		auto extend = [&](float x, float y) {
			r.Left = std::min(r.Left, x); r.Right = std::max(r.Right, x);
			r.Top = std::min(r.Top, y); r.Bottom = std::max(r.Bottom, y);
		};
		for (const auto& n : m_Nodes) {
			extend(n.X - n.Width * 0.5f, n.Y - n.Height * 0.5f);
			extend(n.X + n.Width * 0.5f, n.Y + n.Height * 0.5f);
		}
		// a label's box: 110 x 18 (the renderer's), around its point or right of it
		const float labelWidth = 110.0f, labelHeight = 18.0f;
		for (const auto& e : m_Edges) {
			for (const auto& w : e.Waypoints)
				extend(w.X, w.Y);
			const Node* from = GetNode(e.From);
			const Node* to = GetNode(e.To);
			if (!from || !to) continue;
			if (from == to) {
				const float right = from->X + from->Width * 0.5f + 30.0f;	// the loop goes 24 units to the right of the node
				extend(right, from->Y);
				if (!e.Label.empty()) {
					extend(right - 2.0f + labelWidth, from->Y - labelHeight * 0.5f);
					extend(right - 2.0f + labelWidth, from->Y + labelHeight * 0.5f);
				}
			}
			else if (!e.Label.empty()) {
				// where EdgeLabelPoint puts it, near enough: the middle bend or the middle of the middle piece, through
				// the centers (no path is made: this is on every paint with scroll bars)
				const size_t count = e.Waypoints.size() + 2;
				auto point = [&](size_t i) { return i == 0 ? Point{ from->X, from->Y } : i == count - 1 ? Point{ to->X, to->Y } : e.Waypoints[i - 1]; };
				const Point a = point((count - 1) / 2), b = point(count / 2);
				const float x = (a.X + b.X) * 0.5f, y = (a.Y + b.Y) * 0.5f;
				extend(x - labelWidth * 0.5f, y - labelHeight * 0.5f);
				extend(x + labelWidth * 0.5f, y + labelHeight * 0.5f);
			}
		}
		return r;
	}

	void NodeGraphModel::Clear() {
		m_Nodes.clear();
		m_Edges.clear();
		NodesChanged();
		EdgesChanged();
		m_Images.clear();
		m_NextNodeId = 0;
		m_NextEdgeId = 0;
		m_NextImageId = 0;
		Reset();
	}

	// ---- SVG export -----------------------------------------------------------------

	namespace {
		std::string Utf8(const std::wstring& s) {
			if (s.empty()) return {};
			int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
			std::string r(n, '\0');
			::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
			return r;
		}

		std::string XmlEscape(const std::string& s) {
			std::string r;
			for (char c : s) {
				switch (c) {
					case '&': r += "&amp;"; break;
					case '<': r += "&lt;"; break;
					case '>': r += "&gt;"; break;
					case '"': r += "&quot;"; break;
					default: r += c;
				}
			}
			return r;
		}

		std::string Hex(COLORREF c) {
			char b[8];
			sprintf_s(b, "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
			return b;
		}

		std::string Num(float v) {
			char b[32];
			sprintf_s(b, "%.1f", v);
			return b;
		}

		std::vector<std::wstring> SplitLines(const std::wstring& s) {
			std::vector<std::wstring> lines;
			lines.reserve(std::count(s.begin(), s.end(), L'\n') + 1);
			size_t start = 0;
			for (;;) {
				auto end = s.find(L'\n', start);
				lines.push_back(s.substr(start, end == std::wstring::npos ? end : end - start));
				if (end == std::wstring::npos) break;
				start = end + 1;
			}
			return lines;
		}

		// The dashes of an edge's line, as Direct2D has them: in widths of the line, dots round.
		std::string DashArray(const EdgeStyle& style) {
			const float w = style.Width;
			switch (style.Line) {
				case EdgeLine::Dash:    return " stroke-dasharray=\"" + Num(2 * w) + " " + Num(2 * w) + "\"";
				case EdgeLine::Dot:     return " stroke-dasharray=\"0 " + Num(2 * w) + "\" stroke-linecap=\"round\"";
				case EdgeLine::DashDot: return " stroke-dasharray=\"" + Num(2 * w) + " " + Num(2 * w) + " 0 " + Num(2 * w) + "\" stroke-linecap=\"round\"";
				default:                return {};
			}
		}

		// An arrowhead as the renderer draws it, at a tip pointing along dir (a unit vector).
		std::string SvgArrowhead(Point tip, Point dir, ArrowHead style, const std::string& color) {
			const float size = 10.0f, c = std::cos(0.4f), s = std::sin(0.4f);
			const float bx = -dir.X * size, by = -dir.Y * size;
			const Point l{ tip.X + bx * c - by * s, tip.Y + bx * s + by * c };
			const Point r{ tip.X + bx * c + by * s, tip.Y - bx * s + by * c };
			switch (style) {
				case ArrowHead::Open:
					return "<polyline points=\"" + Num(l.X) + "," + Num(l.Y) + " " + Num(tip.X) + "," + Num(tip.Y) + " " + Num(r.X) + "," + Num(r.Y) +
						"\" fill=\"none\" stroke=\"" + color + "\" stroke-width=\"1.5\"/>\n";
				case ArrowHead::Circle:
					return "<circle cx=\"" + Num(tip.X - dir.X * 4) + "\" cy=\"" + Num(tip.Y - dir.Y * 4) + "\" r=\"4\" fill=\"" + color + "\"/>\n";
				case ArrowHead::Diamond: {
					const float half = size * 0.5f, side = size * 0.3f;
					const Point mid{ tip.X - dir.X * half, tip.Y - dir.Y * half };
					return "<polygon points=\"" + Num(tip.X) + "," + Num(tip.Y) + " " + Num(mid.X - dir.Y * side) + "," + Num(mid.Y + dir.X * side) + " " +
						Num(tip.X - dir.X * size) + "," + Num(tip.Y - dir.Y * size) + " " + Num(mid.X + dir.Y * side) + "," + Num(mid.Y - dir.X * side) +
						"\" fill=\"" + color + "\"/>\n";
				}
				default:
					return "<polygon points=\"" + Num(tip.X) + "," + Num(tip.Y) + " " + Num(l.X) + "," + Num(l.Y) + " " + Num(r.X) + "," + Num(r.Y) +
						"\" fill=\"" + color + "\"/>\n";
			}
		}

		// The SVG element of a node's shape, with these attributes (fill, stroke).
		std::string ShapeElement(const Node& n, const std::string& attributes) {
			const float left = n.X - n.Width * 0.5f, top = n.Y - n.Height * 0.5f;
			switch (n.Style.Shape) {
				case NodeShape::Ellipse:
					return "<ellipse cx=\"" + Num(n.X) + "\" cy=\"" + Num(n.Y) + "\" rx=\"" + Num(n.Width * 0.5f) + "\" ry=\"" + Num(n.Height * 0.5f) + "\" " + attributes + "/>";
				case NodeShape::Diamond:
					return "<polygon points=\"" + Num(n.X) + "," + Num(top) + " " + Num(left + n.Width) + "," + Num(n.Y) + " " + Num(n.X) + "," + Num(top + n.Height) +
						" " + Num(left) + "," + Num(n.Y) + "\" " + attributes + "/>";
				default:
					return "<rect x=\"" + Num(left) + "\" y=\"" + Num(top) + "\" width=\"" + Num(n.Width) + "\" height=\"" + Num(n.Height) + "\" rx=\"" +
						Num(n.Style.CornerRadius) + "\" " + attributes + "/>";
			}
		}

		std::string Base64(const uint8_t* data, size_t size) {
			static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string r;
			r.reserve((size + 2) / 3 * 4);
			for (size_t i = 0; i < size; i += 3) {
				uint32_t v = data[i] << 16 | (i + 1 < size ? data[i + 1] << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
				r += digits[v >> 18 & 63];
				r += digits[v >> 12 & 63];
				r += i + 1 < size ? digits[v >> 6 & 63] : '=';
				r += i + 2 < size ? digits[v & 63] : '=';
			}
			return r;
		}

		// A data URI of an image. PNG, JPEG and GIF go as they are; anything else (BMP, ICO, ...) as a PNG, as viewers
		// may not show it. Empty if it can't be decoded.
		std::string ImageDataUri(const std::vector<uint8_t>& data) {
			auto starts = [&](std::initializer_list<uint8_t> sig) {
				return data.size() >= sig.size() && std::equal(sig.begin(), sig.end(), data.begin());
			};
			if (starts({ 0x89, 'P', 'N', 'G' }))
				return "data:image/png;base64," + Base64(data.data(), data.size());
			if (starts({ 0xFF, 0xD8, 0xFF }))
				return "data:image/jpeg;base64," + Base64(data.data(), data.size());
			if (starts({ 'G', 'I', 'F', '8' }))
				return "data:image/gif;base64," + Base64(data.data(), data.size());

			Imaging::ComScope com;
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICBitmapSource> decoded;
			std::vector<uint8_t> png;
			if (FAILED(Imaging::CreateFactory(factory.GetAddressOf())) ||
				FAILED(Imaging::Decode(factory.Get(), data.data(), data.size(), decoded.GetAddressOf())) ||
				FAILED(Imaging::EncodePng(factory.Get(), decoded.Get(), png)))
				return {};
			return "data:image/png;base64," + Base64(png.data(), png.size());
		}

		// A rectangle with its top corners rounded (and the bottom ones too if both).
		std::string BandPath(float x, float y, float w, float h, float r, bool roundBottom) {
			r = std::min({ r, w * 0.5f, h * 0.5f });
			const float b = roundBottom ? r : 0;
			return "M" + Num(x) + " " + Num(y + r) + " A" + Num(r) + " " + Num(r) + " 0 0 1 " + Num(x + r) + " " + Num(y) +
				" L" + Num(x + w - r) + " " + Num(y) + " A" + Num(r) + " " + Num(r) + " 0 0 1 " + Num(x + w) + " " + Num(y + r) +
				" L" + Num(x + w) + " " + Num(y + h - b) + (b > 0 ? " A" + Num(b) + " " + Num(b) + " 0 0 1 " + Num(x + w - b) + " " + Num(y + h) : "") +
				" L" + Num(x + b) + " " + Num(y + h) + (b > 0 ? " A" + Num(b) + " " + Num(b) + " 0 0 1 " + Num(x) + " " + Num(y + h - b) : "") + " Z";
		}

		// A part of an image of the model (in its pixels) in a rect of the graph: a nested viewport crops it.
		std::string PlaceImage(ImageId id, const Rect& dst, const Rect& src) {
			return "<svg x=\"" + Num(dst.Left) + "\" y=\"" + Num(dst.Top) + "\" width=\"" + Num(dst.Width()) + "\" height=\"" + Num(dst.Height()) +
				"\" viewBox=\"" + Num(src.Left) + " " + Num(src.Top) + " " + Num(src.Width()) + " " + Num(src.Height()) +
				"\" preserveAspectRatio=\"none\"><use xlink:href=\"#image" + std::to_string(id) + "\"/></svg>\n";
		}
	}

	bool NodeGraphModel::SaveSvg(const wchar_t* path, COLORREF background, const NodePictureFunc& picture) const {
		if (m_Nodes.empty()) return false;

		const Rect bounds = GetBounds();
		const float margin = 24.0f;
		float minX = bounds.Left - margin, minY = bounds.Top - margin, maxX = bounds.Right + margin, maxY = bounds.Bottom + margin;

		std::string svg;
		svg += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
		svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"" + Num(maxX - minX) + "\" height=\"" + Num(maxY - minY) +
			"\" viewBox=\"" + Num(minX) + " " + Num(minY) + " " + Num(maxX - minX) + " " + Num(maxY - minY) + "\">\n";

		// the pictures of the nodes that have one, and the images the others show (each written once)
		std::vector<NodePicture> pictures(m_Nodes.size());
		std::vector<ImageId> used;
		used.reserve(m_Nodes.size() * 2);
		for (size_t i = 0; i < m_Nodes.size(); i++) {
			if (picture)
				pictures[i] = picture(m_Nodes[i]);
			if (!pictures[i].Png.empty())
				continue;
			NodeLayout layout = GetNodeLayout(m_Nodes[i]);
			if (!layout.Image.IsEmpty()) used.push_back(m_Nodes[i].Content.Image);
			if (!layout.Icon.IsEmpty()) used.push_back(m_Nodes[i].Content.Icon);
		}
		std::sort(used.begin(), used.end());
		used.erase(std::unique(used.begin(), used.end()), used.end());
		std::vector<ImageId> written;
		written.reserve(used.size());
		if (!used.empty()) {
			svg += "<defs>\n";
			for (ImageId id : used) {
				const Image* image = GetImage(id);
				std::string uri = ImageDataUri(image->Data);
				if (uri.empty()) continue;
				svg += "<image id=\"image" + std::to_string(id) + "\" width=\"" + std::to_string(image->Width) + "\" height=\"" + std::to_string(image->Height) +
					"\" preserveAspectRatio=\"none\" xlink:href=\"" + uri + "\"/>\n";
				written.push_back(id);
			}
			svg += "</defs>\n";
		}
		auto isWritten = [&](ImageId id) { return std::find(written.begin(), written.end(), id) != written.end(); };

		svg += "<rect x=\"" + Num(minX) + "\" y=\"" + Num(minY) + "\" width=\"" + Num(maxX - minX) + "\" height=\"" + Num(maxY - minY) + "\" fill=\"" + Hex(background) + "\"/>\n";

		// edges first, so that the nodes are on top; as the renderer draws them (EdgePath), at the zoom of 1
		std::unordered_set<uint64_t> pairs;
		pairs.reserve(m_Edges.size());
		for (const auto& e : m_Edges)
			pairs.insert((uint64_t)e.From << 32 | e.To);
		for (const auto& e : m_Edges) {
			const Node* from = GetNode(e.From);
			const Node* to = GetNode(e.To);
			if (!from || !to) continue;
			const std::string color = Hex(e.Style.Color);
			const std::string stroke = "fill=\"none\" stroke=\"" + color + "\" stroke-width=\"" + Num(e.Style.Width) + "\" stroke-linejoin=\"round\"" + DashArray(e.Style);

			if (from == to) {
				const auto loop = SelfLoopPath(*from);
				const bool solidHead = e.Style.Directed && (e.Style.Arrow == ArrowHead::Filled || e.Style.Arrow == ArrowHead::Diamond);
				svg += "<path d=\"M" + Num(loop[0].X) + " " + Num(loop[0].Y) + " L" + Num(loop[1].X) + " " + Num(loop[1].Y) + " L" + Num(loop[2].X) + " " +
					Num(loop[2].Y) + " L" + Num(loop[3].X + (solidHead ? 8 : 0)) + " " + Num(loop[3].Y) + "\" " + stroke + "/>\n";
				if (e.Style.Directed)
					svg += SvgArrowhead(loop[3], { -1, 0 }, e.Style.Arrow, color);
				if (!e.Label.empty())	// right of the loop
					svg += "<text x=\"" + Num(loop[1].X + 4) + "\" y=\"" + Num(from->Y) + "\" fill=\"#D9D9D9\" font-family=\"Segoe UI,sans-serif\" font-size=\"11\" dominant-baseline=\"central\">" +
						XmlEscape(Utf8(e.Label)) + "</text>\n";
				continue;
			}

			const float lane = pairs.contains((uint64_t)e.To << 32 | e.From) ? 7.0f : 0.0f;
			const std::vector<Point> points = EdgePath(e, *from, *to, lane);
			if (points.size() < 2) continue;
			const std::vector<Point> controls = e.Style.Curved && points.size() > 2 ? CurveControls(points) : std::vector<Point>{};

			svg += "<path d=\"M" + Num(points[0].X) + " " + Num(points[0].Y);
			for (size_t i = 0; i + 1 < points.size(); i++) {
				if (controls.empty())
					svg += " L" + Num(points[i + 1].X) + " " + Num(points[i + 1].Y);
				else
					svg += " C" + Num(controls[i * 2].X) + " " + Num(controls[i * 2].Y) + " " + Num(controls[i * 2 + 1].X) + " " + Num(controls[i * 2 + 1].Y) +
						" " + Num(points[i + 1].X) + " " + Num(points[i + 1].Y);
			}
			svg += "\" " + stroke + "/>\n";

			if (e.Style.Directed) {
				const Point& end = points.back();
				Point before = controls.empty() ? points[points.size() - 2] : controls.back();
				float dx = end.X - before.X, dy = end.Y - before.Y;
				if (dx * dx + dy * dy < 1e-6f) { before = points[points.size() - 2]; dx = end.X - before.X; dy = end.Y - before.Y; }
				const float l = std::sqrt(dx * dx + dy * dy);
				if (l > 0)
					svg += SvgArrowhead(end, { dx / l, dy / l }, e.Style.Arrow, color);
			}
			if (!e.Label.empty()) {
				const Point at = EdgeLabelPoint(points, controls);
				svg += "<text x=\"" + Num(at.X) + "\" y=\"" + Num(at.Y) + "\" fill=\"#D9D9D9\" font-family=\"Segoe UI,sans-serif\" font-size=\"11\" text-anchor=\"middle\" dominant-baseline=\"central\">" +
					XmlEscape(Utf8(e.Label)) + "</text>\n";
			}
		}

		// the parts of the nodes as the renderer draws them (the layout is in the node's units: moved to its top-left corner)
		for (size_t i = 0; i < m_Nodes.size(); i++) {
			const Node& n = m_Nodes[i];
			const float left = n.X - n.Width * 0.5f, top = n.Y - n.Height * 0.5f;
			auto at = [&](const Rect& r) { return Rect{ left + r.Left, top + r.Top, left + r.Right, top + r.Bottom }; };

			if (const auto& pic = pictures[i]; !pic.Png.empty()) {
				svg += "<image x=\"" + Num(pic.Bounds.Left) + "\" y=\"" + Num(pic.Bounds.Top) + "\" width=\"" + Num(pic.Bounds.Width()) + "\" height=\"" + Num(pic.Bounds.Height()) +
					"\" preserveAspectRatio=\"none\" xlink:href=\"data:image/png;base64," + Base64(pic.Png.data(), pic.Png.size()) + "\"/>\n";
				continue;
			}

			const NodeContent& c = n.Content;
			const NodeLayout layout = GetNodeLayout(n);
			const bool rectangle = n.Style.Shape == NodeShape::Rectangle;
			svg += ShapeElement(n, "fill=\"" + Hex(n.Style.FillColor) + "\"") + "\n";

			// what fills the node is clipped to a shape other than the rectangle
			const bool clip = !rectangle && (!layout.Title.IsEmpty() || (!layout.Image.IsEmpty() && isWritten(c.Image)));
			if (clip) {
				const std::string id = "shape" + std::to_string(n.Id);
				svg += "<clipPath id=\"" + id + "\">" + ShapeElement(n, "") + "</clipPath>\n<g clip-path=\"url(#" + id + ")\">\n";
			}
			if (!layout.Title.IsEmpty()) {
				Rect t = at(layout.Title);
				COLORREF fill = c.TitleFill != CLR_INVALID ? c.TitleFill
					: RGB(GetRValue(n.Style.FillColor) * 0.72f, GetGValue(n.Style.FillColor) * 0.72f, GetBValue(n.Style.FillColor) * 0.72f);
				if (rectangle)
					svg += "<path d=\"" + BandPath(t.Left, t.Top, t.Width(), t.Height(), n.Style.CornerRadius, layout.Content.IsEmpty()) + "\" fill=\"" + Hex(fill) + "\"/>\n";
				else
					svg += "<rect x=\"" + Num(t.Left) + "\" y=\"" + Num(t.Top) + "\" width=\"" + Num(t.Width()) + "\" height=\"" + Num(t.Height()) + "\" fill=\"" + Hex(fill) + "\"/>\n";
			}
			if (!layout.Image.IsEmpty() && isWritten(c.Image))
				svg += PlaceImage(c.Image, at(layout.Image), layout.ImageSource);
			if (c.LabelPos == LabelPlacement::Overlay && !layout.Image.IsEmpty() && !layout.Label.IsEmpty() && !n.Label.empty()) {
				Rect b = at(layout.Label);
				svg += "<rect x=\"" + Num(b.Left) + "\" y=\"" + Num(b.Top) + "\" width=\"" + Num(b.Width()) + "\" height=\"" + Num(b.Height()) + "\" fill=\"#000000\" fill-opacity=\"0.5\"/>\n";
			}
			if (clip)
				svg += "</g>\n";

			svg += ShapeElement(n, "fill=\"none\" stroke=\"" + Hex(n.Style.BorderColor) + "\" stroke-width=\"" + Num(n.Style.BorderWidth) + "\"") + "\n";

			if (!layout.Icon.IsEmpty() && isWritten(c.Icon)) {
				const Image* icon = GetImage(c.Icon);
				svg += PlaceImage(c.Icon, at(layout.Icon), Rect{ 0, 0, (float)icon->Width, (float)icon->Height });
			}

			std::string text = "fill=\"" + Hex(n.Style.TextColor) + "\"";
			if (!layout.Title.IsEmpty()) {
				// a nested viewport clips the title to its band
				Rect t = at(layout.Title);
				svg += "<svg x=\"" + Num(t.Left + 8) + "\" y=\"" + Num(t.Top) + "\" width=\"" + Num(std::max(t.Width() - 16, 0.0f)) + "\" height=\"" + Num(t.Height()) + "\">" +
					"<text " + text + " x=\"0\" y=\"" + Num(t.Height() * 0.5f) + "\" font-family=\"Segoe UI,sans-serif\" font-size=\"12\" font-weight=\"600\" dominant-baseline=\"central\">" +
					XmlEscape(Utf8(c.Title)) + "</text></svg>\n";
			}

			if (n.Label.empty() || layout.Label.IsEmpty()) continue;
			Rect l = at(layout.Label);
			if (n.Style.Code) {
				svg += "<text xml:space=\"preserve\" " + text + " font-family=\"Consolas,'Cascadia Mono',monospace\" font-size=\"12\">";
				float y = l.Top + 4.5f + 12.0f;
				for (const auto& line : SplitLines(n.Label)) {
					svg += "<tspan x=\"" + Num(l.Left + 8.0f) + "\" y=\"" + Num(y) + "\">" + XmlEscape(Utf8(line)) + "</tspan>";
					y += 14.5f;
				}
				svg += "</text>\n";
			}
			else {
				// the lines centered in the label's area, as DirectWrite does
				const auto lines = SplitLines(n.Label);
				const float lineHeight = 17.3f;
				float y = (l.Top + l.Bottom) * 0.5f - (lines.size() - 1) * lineHeight * 0.5f;
				svg += "<text " + text + " font-family=\"Segoe UI,sans-serif\" font-size=\"13\" text-anchor=\"middle\" dominant-baseline=\"central\">";
				for (const auto& line : lines) {
					svg += "<tspan x=\"" + Num((l.Left + l.Right) * 0.5f) + "\" y=\"" + Num(y) + "\">" + XmlEscape(Utf8(line)) + "</tspan>";
					y += lineHeight;
				}
				svg += "</text>\n";
			}
		}
		svg += "</svg>\n";

		FILE* f = nullptr;
		if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
		bool ok = fwrite(svg.data(), 1, svg.size(), f) == svg.size();
		return fclose(f) == 0 && ok;
	}

} // namespace NodeGraphCtrl
