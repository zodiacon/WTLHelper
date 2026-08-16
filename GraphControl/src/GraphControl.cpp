// GraphControl.cpp : Defines the functions for the static library.
//

#define WIN32_LEAN_AND_MEAN

#include "../include/GraphControl.h"

#include <commctrl.h>
#include <memory>
#include <algorithm>
#include <cfloat>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")

namespace GraphCtrl {

	// ---- Undo command types (private to this translation unit) -------------------

	namespace {

		struct MoveCmd : UndoCmd {
			NodeId Id;
			float  OldX, OldY, NewX, NewY;
			void Apply(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) { n->X = NewX; n->Y = NewY; }
			}
			void Revert(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) { n->X = OldX; n->Y = OldY; }
			}
		};

		struct RenameCmd : UndoCmd {
			NodeId       Id;
			std::wstring OldLabel, NewLabel;
			void Apply(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->Label = NewLabel;
			}
			void Revert(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) n->Label = OldLabel;
			}
		};

		struct DeleteEdgeCmd : UndoCmd {
			Edge Saved;
			void Apply(GraphModel& m) override { m.RemoveEdge(Saved.Id); }
			void Revert(GraphModel& m) override { m.RestoreEdge(Saved); }
		};

		struct AddEdgeCmd : UndoCmd {
			Edge Saved;
			void Apply(GraphModel& m) override { m.RestoreEdge(Saved); }
			void Revert(GraphModel& m) override { m.RemoveEdge(Saved.Id); }
		};

		struct DeleteSelectionCmd : UndoCmd {
			std::vector<Node> Nodes;
			std::vector<Edge> Edges;
			void Apply(GraphModel& m) override {
				for (const auto& e : Edges) m.RemoveEdge(e.Id);
				for (const auto& n : Nodes) m.RemoveNode(n.Id);
			}
			void Revert(GraphModel& m) override {
				for (const auto& n : Nodes) m.RestoreNode(n);
				for (const auto& e : Edges) m.RestoreEdge(e);
			}
		};

		struct CompoundCmd : UndoCmd {
			std::vector<std::unique_ptr<UndoCmd>> Cmds;
			void Apply(GraphModel& m) override {
				for (auto& c : Cmds) c->Apply(m);
			}
			void Revert(GraphModel& m) override {
				for (auto it = Cmds.rbegin(); it != Cmds.rend(); ++it)
					(*it)->Revert(m);
			}
		};

		struct ResizeCmd : UndoCmd {
			NodeId Id;
			float  OldX, OldY, OldW, OldH;
			float  NewX, NewY, NewW, NewH;
			void Apply(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) {
					n->X = NewX; n->Y = NewY; n->Width = NewW; n->Height = NewH;
				}
			}
			void Revert(GraphModel& m) override {
				if (Node* n = m.GetNode(Id)) {
					n->X = OldX; n->Y = OldY; n->Width = OldW; n->Height = OldH;
				}
			}
		};

		struct PasteCmd : UndoCmd {
			std::vector<Node> PastedNodes;
			std::vector<Edge> PastedEdges;
			void Apply(GraphModel& m) override {
				for (const auto& n : PastedNodes) m.RestoreNode(n);
				for (const auto& e : PastedEdges) m.RestoreEdge(e);
			}
			void Revert(GraphModel& m) override {
				for (const auto& e : PastedEdges) m.RemoveEdge(e.Id);
				for (const auto& n : PastedNodes) m.RemoveNode(n.Id);
			}
		};

	} // anonymous namespace

	// ---- Clipboard helpers -------------------------------------------------------

	namespace {

		struct ByteBuffer {
			std::vector<unsigned char> Data;
			template<typename T>
			void Write(const T& v) {
				auto p = reinterpret_cast<const unsigned char*>(&v);
				Data.insert(Data.end(), p, p + sizeof(T));
			}
			void WriteWStr(const std::wstring& s) {
				uint32_t n = (uint32_t)s.size();
				Write(n);
				if (n) {
					auto p = reinterpret_cast<const unsigned char*>(s.data());
					Data.insert(Data.end(), p, p + n * sizeof(wchar_t));
				}
			}
		};

		struct ByteReader {
			const unsigned char* Data;
			size_t               Pos;
			size_t               Size;
			template<typename T>
			bool Read(T& v) {
				if (Pos + sizeof(T) > Size) return false;
				memcpy(&v, Data + Pos, sizeof(T));
				Pos += sizeof(T);
				return true;
			}
			bool ReadWStr(std::wstring& s) {
				uint32_t n;
				if (!Read(n)) return false;
				if (n > 0) {
					if (Pos + n * sizeof(wchar_t) > Size) return false;
					s.assign(reinterpret_cast<const wchar_t*>(Data + Pos), n);
					Pos += n * sizeof(wchar_t);
				}
				else {
					s.clear();
				}
				return true;
			}
		};

		static UINT GetClipboardFormat() {
			static UINT s_fmt = ::RegisterClipboardFormatW(L"GraphControlClipboard");
			return s_fmt;
		}

	} // anonymous namespace

	// ---- WM_GRAPHEDIT label edit subclass proc -----------------------------------

	static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
		LPARAM lParam, UINT_PTR, DWORD_PTR dwRef) {
		HWND parent = reinterpret_cast<HWND>(dwRef);
		if (msg == WM_KEYDOWN) {
			if (wParam == VK_ESCAPE) {
				PostMessage(parent, WM_GRAPHEDIT, 0, 0);
				return 0;
			}
			if (wParam == VK_RETURN && (GetKeyState(VK_SHIFT) & 0x8000)) {
				PostMessage(parent, WM_GRAPHEDIT, 1, 0);
				return 0;
			}
			// Plain Enter falls through → multiline edit inserts a newline.
		}
		if (msg == WM_KILLFOCUS) {
			PostMessage(parent, WM_GRAPHEDIT, 1, 0);
			return 0;
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	// ---- CGraphControl : message handlers ----------------------------------------

	int CGraphControl::OnCreate(LPCREATESTRUCT pcs) {
		m_DrawGrid = (pcs->style & GCS_GRID) != 0;
		m_Model = std::make_unique<GraphModel>();

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

	void CGraphControl::OnDestroy() {
		CancelLabelEdit();
		m_TooltipWnd = nullptr;
		m_Renderer.Shutdown();
	}

	// ---- Notification helpers ------------------------------------------------

	void CGraphControl::Notify(UINT code, NodeId nodeId, EdgeId edgeId) {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return;
		GRAPHNOTIFY gn{};
		gn.Hdr.hwndFrom = m_hWnd;
		gn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		gn.Hdr.code = code;
		gn.NodeId = nodeId;
		gn.EdgeId = edgeId;
		::SendMessage(parent, WM_NOTIFY, gn.Hdr.idFrom, reinterpret_cast<LPARAM>(&gn));
	}

	void CGraphControl::NotifyEdgeAdded(NodeId from, NodeId to, EdgeId edgeId) {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return;
		GRAPHEDGENOTIFY gn{};
		gn.Hdr.hwndFrom = m_hWnd;
		gn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		gn.Hdr.code = GCN_EDGEADDED;
		gn.FromNode = from;
		gn.ToNode = to;
		gn.EdgeId = edgeId;
		::SendMessage(parent, WM_NOTIFY, gn.Hdr.idFrom, reinterpret_cast<LPARAM>(&gn));
	}

	void CGraphControl::NotifyUndoChanged() {
		HWND parent = ::GetParent(m_hWnd);
		if (!parent) return;
		NMHDR hdr{};
		hdr.hwndFrom = m_hWnd;
		hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
		hdr.code = GCN_UNDOCHANGED;
		::SendMessage(parent, WM_NOTIFY, hdr.idFrom, reinterpret_cast<LPARAM>(&hdr));
	}

	// ---- Undo / selection helpers --------------------------------------------

	void CGraphControl::PushUndo(std::unique_ptr<UndoCmd> cmd) {
		m_UndoStack.push_back(std::move(cmd));
		m_RedoStack.clear();
		NotifyUndoChanged();
	}

	void CGraphControl::RequestInvalidate() {
		if (m_UpdateDepth > 0) m_PendingInvalidate = true;
		else Invalidate(FALSE);
	}

	void CGraphControl::BeginUpdate() { ++m_UpdateDepth; }

	void CGraphControl::EndUpdate() {
		if (--m_UpdateDepth == 0 && m_PendingInvalidate) {
			m_PendingInvalidate = false;
			Invalidate(FALSE);
		}
	}

	bool CGraphControl::IsSelected(NodeId id) const {
		return std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), id)
			!= m_SelectedNodes.end();
	}

	void CGraphControl::FireSelChanged() {
		NodeId primary = m_SelectedNodes.empty() ? InvalidNode : m_SelectedNodes[0];
		Notify(GCN_SELCHANGED, primary, m_SelectedEdge);
	}

	// ---- Minimap helpers -----------------------------------------------------

	MinimapConfig CGraphControl::MakeMinimapConfig(int cw, int ch) const {
		MinimapConfig cfg;
		cfg.Visible = m_Minimap.Visible;
		cfg.Width = m_Minimap.Width;
		cfg.Height = m_Minimap.Height;
		cfg.X = (m_Minimap.X >= 0.0f) ? m_Minimap.X : (float)cw - m_Minimap.Width - 12.0f;
		cfg.Y = (m_Minimap.Y >= 0.0f) ? m_Minimap.Y : (float)ch - m_Minimap.Height - 12.0f;
		return cfg;
	}

	bool CGraphControl::MinimapScreenToGraph(const MinimapConfig& cfg, const GraphModel& model,
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

	// ---- Label edit ------------------------------------------------------------

	void CGraphControl::CommitLabelEdit() {
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
				GRAPHLABELNOTIFY gln{};
				gln.Hdr.hwndFrom = m_hWnd;
				gln.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
				gln.Hdr.code = GCN_LABELCHANGED;
				gln.NodeId = id;
				wcsncpy_s(gln.SzNewLabel, text.c_str(), _TRUNCATE);
				::SendMessage(parent, WM_NOTIFY, gln.Hdr.idFrom, reinterpret_cast<LPARAM>(&gln));
			}
		}
		RequestInvalidate();
	}

	void CGraphControl::CancelLabelEdit() {
		if (!m_LabelEdit) return;
		::DestroyWindow(m_LabelEdit);
		m_LabelEdit = nullptr;
		if (!m_LabelEditFont.IsNull()) m_LabelEditFont.DeleteObject();
		m_EditNodeId = InvalidNode;
		RequestInvalidate();
	}

	void CGraphControl::BeginEditLabel(NodeId id) {
		if (m_LabelEdit) CommitLabelEdit();
		Node* n = m_Model->GetNode(id);
		if (!n) return;

		auto sc = m_Vt.ToScreen(n->X, n->Y);
		float hw = n->Width * m_Vt.Scale * 0.5f;
		float hh = n->Height * m_Vt.Scale * 0.5f;
		int x = (int)(sc.x - hw), y = (int)(sc.y - hh);
		int w = (int)(hw * 2.0f), h = (int)(hh * 2.0f);

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

		// Segoe UI at the same point size the D2D renderer uses (13 DIPs ≈ 10 pt at 96 DPI).
		int fontPx = MulDiv(10, ::GetDpiForWindow(m_hWnd), 72);
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

	bool CGraphControl::IsEditingLabel() const {
		return m_LabelEdit != nullptr;
	}

	// ---- Deletion helpers ----------------------------------------------------

	bool CGraphControl::DeleteNode(NodeId id) {
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
		Notify(GCN_NODEREMOVED, id, InvalidEdge);
		RequestInvalidate();
		return true;
	}

	bool CGraphControl::DeleteEdge(EdgeId id) {
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
		Notify(GCN_EDGEREMOVED, InvalidNode, id);
		RequestInvalidate();
		return true;
	}

	bool CGraphControl::DeleteSelected() {
		if (m_SelectedNodes.empty() && m_SelectedEdge == InvalidEdge) return false;

		if (!m_SelectedNodes.empty()) {
			auto cmd = std::make_unique<DeleteSelectionCmd>();
			for (NodeId nid : m_SelectedNodes) {
				Node* n = m_Model->GetNode(nid);
				if (n) cmd->Nodes.push_back(*n);
			}
			for (const auto& e : m_Model->Edges()) {
				bool incident =
					std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.From) != m_SelectedNodes.end()
					|| std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.To) != m_SelectedNodes.end();
				if (incident) {
					bool dup = std::find_if(cmd->Edges.begin(), cmd->Edges.end(),
						[&](const Edge& ex) { return ex.Id == e.Id; }) != cmd->Edges.end();
					if (!dup) cmd->Edges.push_back(e);
				}
			}

			std::vector<NodeId> removed = m_SelectedNodes;
			for (NodeId nid : removed) {
				Notify(GCN_NODEREMOVED, nid, InvalidEdge);
				m_Model->RemoveNode(nid);
			}
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

	void CGraphControl::Undo() {
		if (m_UndoStack.empty()) return;
		auto cmd = std::move(m_UndoStack.back());
		m_UndoStack.pop_back();
		cmd->Revert(*m_Model);
		m_RedoStack.push_back(std::move(cmd));
		NotifyUndoChanged();
		RequestInvalidate();
	}

	void CGraphControl::Redo() {
		if (m_RedoStack.empty()) return;
		auto cmd = std::move(m_RedoStack.back());
		m_RedoStack.pop_back();
		cmd->Apply(*m_Model);
		m_UndoStack.push_back(std::move(cmd));
		NotifyUndoChanged();
		RequestInvalidate();
	}

	bool CGraphControl::CanUndo() const {
		return !m_UndoStack.empty();
	}

	bool CGraphControl::CanRedo() const {
		return !m_RedoStack.empty();
	}

	// ---- Clipboard (copy / paste) --------------------------------------------

	void CGraphControl::Copy() {
		if (m_SelectedNodes.empty()) return;

		std::vector<const Node*> nodes;
		for (NodeId id : m_SelectedNodes)
			if (const Node* n = m_Model->GetNode(id)) nodes.push_back(n);

		std::vector<const Edge*> edges;
		for (const auto& e : m_Model->Edges()) {
			bool f = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.From) != m_SelectedNodes.end();
			bool t = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), e.To) != m_SelectedNodes.end();
			if (f && t) edges.push_back(&e);
		}

		ByteBuffer buf;
		buf.Write((uint32_t)nodes.size());
		for (const Node* n : nodes) {
			buf.Write(n->X);  buf.Write(n->Y);
			buf.Write(n->Width); buf.Write(n->Height);
			buf.Write(n->Style.FillColor);
			buf.Write(n->Style.BorderColor);
			buf.Write(n->Style.TextColor);
			buf.Write(n->Style.BorderWidth);
			buf.Write(n->Style.CornerRadius);
			buf.WriteWStr(n->Label);
		}
		buf.Write((uint32_t)edges.size());
		for (const Edge* e : edges) {
			auto fi = std::find_if(nodes.begin(), nodes.end(), [&](const Node* n) { return n->Id == e->From; });
			auto ti = std::find_if(nodes.begin(), nodes.end(), [&](const Node* n) { return n->Id == e->To;   });
			buf.Write((uint32_t)(fi - nodes.begin()));
			buf.Write((uint32_t)(ti - nodes.begin()));
			buf.Write(e->Style.Color);
			buf.Write(e->Style.Width);
			buf.Write((uint32_t)(e->Style.Directed ? 1u : 0u));
			buf.WriteWStr(e->Label);
		}

		if (!::OpenClipboard(m_hWnd)) return;
		::EmptyClipboard();
		HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, buf.Data.size());
		if (hg) {
			void* p = ::GlobalLock(hg);
			if (p) {
				memcpy(p, buf.Data.data(), buf.Data.size());
				::GlobalUnlock(hg);
				::SetClipboardData(GetClipboardFormat(), hg);
			}
			else {
				::GlobalFree(hg);
			}
		}
		::CloseClipboard();
	}

	void CGraphControl::Paste() {
		UINT fmt = GetClipboardFormat();
		if (!::IsClipboardFormatAvailable(fmt)) return;
		if (!::OpenClipboard(m_hWnd)) return;

		HGLOBAL hg = (HGLOBAL)::GetClipboardData(fmt);
		if (!hg) { ::CloseClipboard(); return; }

		const unsigned char* raw = (const unsigned char*)::GlobalLock(hg);
		size_t               size = ::GlobalSize(hg);
		if (!raw) { ::CloseClipboard(); return; }

		ByteReader rd{ raw, 0, size };

		struct ClipNode { float X, Y, W, H; NodeStyle Style; std::wstring Label; };
		struct ClipEdge { uint32_t Fi, Ti; EdgeStyle Style; std::wstring Label; };

		uint32_t nodeCount = 0;
		bool ok = rd.Read(nodeCount);

		std::vector<ClipNode> clipNodes;
		clipNodes.reserve(nodeCount);
		for (uint32_t i = 0; i < nodeCount && ok; ++i) {
			ClipNode cn{};
			ok = rd.Read(cn.X) && rd.Read(cn.Y) && rd.Read(cn.W) && rd.Read(cn.H)
				&& rd.Read(cn.Style.FillColor) && rd.Read(cn.Style.BorderColor)
				&& rd.Read(cn.Style.TextColor) && rd.Read(cn.Style.BorderWidth)
				&& rd.Read(cn.Style.CornerRadius) && rd.ReadWStr(cn.Label);
			if (ok) clipNodes.push_back(std::move(cn));
		}

		uint32_t edgeCount = 0;
		ok = ok && rd.Read(edgeCount);

		std::vector<ClipEdge> clipEdges;
		clipEdges.reserve(edgeCount);
		for (uint32_t i = 0; i < edgeCount && ok; ++i) {
			ClipEdge ce{};
			uint32_t directed = 1;
			ok = rd.Read(ce.Fi) && rd.Read(ce.Ti)
				&& rd.Read(ce.Style.Color) && rd.Read(ce.Style.Width)
				&& rd.Read(directed) && rd.ReadWStr(ce.Label);
			if (ok) { ce.Style.Directed = directed != 0; clipEdges.push_back(std::move(ce)); }
		}

		::GlobalUnlock(hg);
		::CloseClipboard();

		if (!ok || clipNodes.empty()) return;

		const float offset = 20.0f;
		auto cmd = std::make_unique<PasteCmd>();
		std::vector<NodeId> newIds;
		newIds.reserve(clipNodes.size());

		for (const auto& cn : clipNodes) {
			NodeId id = m_Model->AddNode(cn.Label, cn.X + offset, cn.Y + offset, cn.W, cn.H);
			Node* n = m_Model->GetNode(id);
			if (n) { n->Style = cn.Style; cmd->PastedNodes.push_back(*n); }
			newIds.push_back(id);
		}
		for (const auto& ce : clipEdges) {
			if (ce.Fi >= newIds.size() || ce.Ti >= newIds.size()) continue;
			EdgeId eid = m_Model->AddEdge(newIds[ce.Fi], newIds[ce.Ti], ce.Label);
			Edge* e = m_Model->GetEdge(eid);
			if (e) { e->Style = ce.Style; cmd->PastedEdges.push_back(*e); }
		}

		PushUndo(std::move(cmd));
		m_SelectedNodes = newIds;
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	bool CGraphControl::CanPaste() const {
		return ::IsClipboardFormatAvailable(GetClipboardFormat()) != FALSE;
	}

	// ---- Rubber-band finalize ------------------------------------------------

	void CGraphControl::FinalizeRubberBand() {
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

	void CGraphControl::OnPaint(HDC /*unused*/) {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(&ps);
		RECT rc; GetClientRect(&rc);
		MinimapConfig mmcfg = MakeMinimapConfig(rc.right, rc.bottom);
		ResizeOverlay ro;
		if (m_SelectedNodes.size() == 1) {
			ro.Node = m_SelectedNodes[0];
			ro.HoveredHandle = m_Resize.Active ? m_Resize.Handle : m_Resize.Hovered;
		}
		m_Renderer.Render(hdc, rc, *m_Model, m_Vt,
			m_SelectedNodes, m_SelectedEdge,
			m_DrawGrid, m_EdgePreview, m_RubberBand, mmcfg, ro);
		EndPaint(&ps);
	}

	void CGraphControl::OnSize(UINT /*nType*/, CSize size) {
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
				auto sc = m_Vt.ToScreen(n->X, n->Y);
				float hw = n->Width * m_Vt.Scale * 0.5f;
				float hh = n->Height * m_Vt.Scale * 0.5f;
				::SetWindowPos(m_LabelEdit, nullptr,
					(int)(sc.x - hw), (int)(sc.y - hh),
					(int)(hw * 2.0f), (int)(hh * 2.0f),
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
		Invalidate(FALSE);
	}

	BOOL CGraphControl::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint pt) {
		float delta = zDelta > 0 ? 1.1f : 1.0f / 1.1f;
		ScreenToClient(&pt);  // lParam of WM_MOUSEWHEEL is in screen coords
		m_Vt.OffsetX = pt.x - (pt.x - m_Vt.OffsetX) * delta;
		m_Vt.OffsetY = pt.y - (pt.y - m_Vt.OffsetY) * delta;
		m_Vt.Scale *= delta;
		m_Vt.Scale = std::clamp(m_Vt.Scale, 0.05f, 50.0f);
		Invalidate(FALSE);
		return TRUE;
	}

	LRESULT CGraphControl::OnSetCursor(UINT, WPARAM, LPARAM, BOOL& bHandled) {
		if (m_EdgePreview.Active) {
			::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
			return TRUE;
		}
		POINT pt; ::GetCursorPos(&pt); ScreenToClient(&pt);

		if (m_SelectedNodes.size() == 1) {
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

	void CGraphControl::OnRButtonDown(UINT /*nFlags*/, CPoint pt) {
		CommitLabelEdit();
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

	void CGraphControl::OnRButtonUp(UINT /*nFlags*/, CPoint pt) {
		if (!m_EdgePreview.Active) return;
		m_EdgePreview.Active = false;
		::ReleaseCapture();

		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		NodeId hit = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
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

	void CGraphControl::OnMButtonDown(UINT /*nFlags*/, CPoint pt) {
		m_Panning = true;
		m_PanStart = { pt.x, pt.y };
		m_PanStartX = m_Vt.OffsetX;
		m_PanStartY = m_Vt.OffsetY;
		SetCapture();
	}

	void CGraphControl::OnMButtonUp(UINT /*nFlags*/, CPoint /*pt*/) {
		if (m_Panning) {
			m_Panning = false;
			::ReleaseCapture();
		}
	}

	void CGraphControl::OnLButtonDown(UINT nFlags, CPoint pt) {
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
		if (m_SelectedNodes.size() == 1) {
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
			Notify(GCN_NODECLICK, hitNode, InvalidEdge);
		}
		else if (hitEdge != InvalidEdge) {
			bool selChanged = (m_SelectedEdge != hitEdge || !m_SelectedNodes.empty());
			m_SelectedNodes.clear();
			m_SelectedEdge = hitEdge;
			if (selChanged) FireSelChanged();
			Notify(GCN_EDGECLICK, InvalidNode, hitEdge);
		}
		else {
			bool selChanged = (!m_SelectedNodes.empty() || m_SelectedEdge != InvalidEdge);
			m_SelectedNodes.clear();
			m_SelectedEdge = InvalidEdge;
			if (selChanged) FireSelChanged();

			m_RubberBand.Active = true;
			m_RubberBand.X0 = m_RubberBand.X1 = (float)sx;
			m_RubberBand.Y0 = m_RubberBand.Y1 = (float)sy;
			SetCapture();
		}

		Invalidate(FALSE);
	}

	void CGraphControl::OnLButtonDblClk(UINT /*nFlags*/, CPoint pt) {
		auto gp = m_Vt.ToGraph((float)pt.x, (float)pt.y);
		NodeId hitNode = m_Renderer.HitTestNode(*m_Model, gp.x, gp.y);
		if (hitNode != InvalidNode)
			BeginEditLabel(hitNode);
	}

	void CGraphControl::OnLButtonUp(UINT /*nFlags*/, CPoint /*pt*/) {
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
		else if (m_RubberBand.Active) {
			FinalizeRubberBand();
			::ReleaseCapture();
			Invalidate(FALSE);
		}
	}

	void CGraphControl::OnMouseMove(UINT /*nFlags*/, CPoint pt) {
		int sx = pt.x, sy = pt.y;

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

			Node* rn = m_Model->GetNode(m_Resize.NodeId);
			if (rn) {
				rn->X = (left + right) * 0.5f;
				rn->Y = (top + bottom) * 0.5f;
				rn->Width = right - left;
				rn->Height = bottom - top;
			}
			Invalidate(FALSE);
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
			if (m_SelectedNodes.size() == 1) {
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

	LRESULT CGraphControl::OnNotify(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
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
				if (n) wcsncpy_s(m_TooltipBuf, n->Label.c_str(), _TRUNCATE);
			}
			else if (eid != InvalidEdge) {
				const Edge* e = m_Model->GetEdge(eid);
				if (e) {
					const Node* f = m_Model->GetNode(e->From);
					const Node* t = m_Model->GetNode(e->To);
					swprintf_s(m_TooltipBuf, L"%s → %s",
						f ? f->Label.c_str() : L"?",
						t ? t->Label.c_str() : L"?");
				}
			}

			if (m_TooltipBuf[0]) {
				GRAPHTOOLTIPNOTIFY gtn{};
				gtn.Hdr.hwndFrom = m_hWnd;
				gtn.Hdr.idFrom = (UINT_PTR)GetDlgCtrlID();
				gtn.Hdr.code = GCN_GETTOOLTIP;
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

	LRESULT CGraphControl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
		if (wParam == VK_DELETE) { DeleteSelected(); return 0; }
		if (::GetKeyState(VK_CONTROL) & 0x8000) {
			if (wParam == 'Z') { Undo();  return 0; }
			if (wParam == 'Y') { Redo();  return 0; }
			if (wParam == 'C') { Copy();  return 0; }
			if (wParam == 'V') { Paste(); return 0; }
		}
		bHandled = FALSE;
		return 0;
	}

	BOOL CGraphControl::OnEraseBkgnd(CDCHandle) {
		return TRUE;  // prevent flicker; we fill the background in OnPaint
	}

	LRESULT CGraphControl::OnGraphEdit(UINT, WPARAM wParam, LPARAM, BOOL&) {
		if (wParam) CommitLabelEdit();
		else        CancelLabelEdit();
		return 0;
	}

	// ---- Creation / model / viewport / selection / minimap -------------------

	HWND CGraphControl::Create(HWND hWndParent, int x, int y, int w, int h,
		DWORD dwStyle, DWORD ctrlStyle) {
		CRect rc(x, y, x + w, y + h);
		return CWindowImpl<CGraphControl>::Create(hWndParent, rc, nullptr, dwStyle | ctrlStyle);
	}

	void CGraphControl::SetModel(GraphModel* model) {
		m_Model.reset(model);
		RequestInvalidate();
	}

	GraphModel* CGraphControl::GetModel() const {
		return m_Model.get();
	}

	void CGraphControl::FitInView() {
		if (!m_Model || m_Model->Nodes().empty()) return;

		float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
		for (const auto& n : m_Model->Nodes()) {
			minX = std::min(minX, n.X - n.Width * 0.5f);
			minY = std::min(minY, n.Y - n.Height * 0.5f);
			maxX = std::max(maxX, n.X + n.Width * 0.5f);
			maxY = std::max(maxY, n.Y + n.Height * 0.5f);
		}

		RECT rc; GetClientRect(&rc);
		float margin = 20.0f;
		float sx = (rc.right - margin * 2) / (maxX - minX);
		float sy = (rc.bottom - margin * 2) / (maxY - minY);
		m_Vt.Scale = std::clamp(std::min(sx, sy), 0.05f, 50.0f);
		m_Vt.OffsetX = margin - minX * m_Vt.Scale;
		m_Vt.OffsetY = margin - minY * m_Vt.Scale;
		RequestInvalidate();
	}

	void CGraphControl::FitSelected() {
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
		m_Vt.OffsetX = margin - minX * m_Vt.Scale;
		m_Vt.OffsetY = margin - minY * m_Vt.Scale;
		RequestInvalidate();
	}

	void CGraphControl::SetZoom(float factor) {
		m_Vt.Scale = std::clamp(factor, 0.05f, 50.0f);
		RequestInvalidate();
	}

	float CGraphControl::GetZoom() const {
		return m_Vt.Scale;
	}

	NodeId CGraphControl::GetSelectedNode() const {
		return m_SelectedNodes.empty() ? InvalidNode : m_SelectedNodes[0];
	}

	EdgeId CGraphControl::GetSelectedEdge() const {
		return m_SelectedEdge;
	}

	std::vector<NodeId> CGraphControl::GetSelectedNodes() const {
		return m_SelectedNodes;
	}

	void CGraphControl::SelectNode(NodeId id, bool addToSelection) {
		if (!addToSelection) m_SelectedNodes.clear();
		if (!IsSelected(id)) m_SelectedNodes.push_back(id);
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	void CGraphControl::ClearSelection() {
		m_SelectedNodes.clear();
		m_SelectedEdge = InvalidEdge;
		FireSelChanged();
		RequestInvalidate();
	}

	void CGraphControl::Refresh() {
		Invalidate(FALSE);
	}

	void CGraphControl::SetMinimapVisible(bool visible) {
		m_Minimap.Visible = visible;
		RequestInvalidate();
	}

	bool CGraphControl::IsMinimapVisible() const {
		return m_Minimap.Visible;
	}

	void CGraphControl::SetMinimapPosition(int x, int y) {
		m_Minimap.X = (x < 0) ? -1.0f : (float)x;
		m_Minimap.Y = (y < 0) ? -1.0f : (float)y;
		RequestInvalidate();
	}

	void CGraphControl::SetNodeStyle(NodeId id, const NodeStyle& style) {
		Node* n = m_Model->GetNode(id);
		if (n) { n->Style = style; RequestInvalidate(); }
	}

	void CGraphControl::SetEdgeStyle(EdgeId id, const EdgeStyle& style) {
		Edge* e = m_Model->GetEdge(id);
		if (e) { e->Style = style; RequestInvalidate(); }
	}

	void CGraphControl::SetNodeSize(NodeId id, float w, float h) {
		Node* n = m_Model->GetNode(id);
		if (!n) return;
		n->Width = std::max(w, k_MinNodeWidth);
		n->Height = std::max(h, k_MinNodeHeight);
		RequestInvalidate();
	}

	void CGraphControl::SetDefaultNodeSize(float w, float h) {
		m_Model->SetDefaultNodeSize(w, h);
	}

	// ---- Register ---------------------------------------------------------------

	bool Register(HINSTANCE /*hInstance*/) {
		INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
		InitCommonControlsEx(&icc);
		// Pre-register the window class; CWindowImpl::Create() also does this lazily.
		ATOM a = CGraphControl::GetWndClassInfo().Register(nullptr);
		return a != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}

	// ---- GraphModel implementation ----------------------------------------------

	NodeId GraphModel::AddNode(std::wstring label, float x, float y, float w, float h) {
		Node n;
		n.Id = m_NextNodeId++;
		n.Label = std::move(label);
		n.X = x;
		n.Y = y;
		n.Width = (w > 0.0f) ? w : m_DefaultNodeWidth;
		n.Height = (h > 0.0f) ? h : m_DefaultNodeHeight;
		m_Nodes.push_back(std::move(n));
		return m_Nodes.back().Id;
	}

	void GraphModel::SetDefaultNodeSize(float w, float h) {
		if (w > 0.0f) m_DefaultNodeWidth = w;
		if (h > 0.0f) m_DefaultNodeHeight = h;
	}

	EdgeId GraphModel::AddEdge(NodeId from, NodeId to, std::wstring label) {
		Edge e;
		e.Id = m_NextEdgeId++;
		e.From = from;
		e.To = to;
		e.Label = std::move(label);
		m_Edges.push_back(std::move(e));
		return m_Edges.back().Id;
	}

	bool GraphModel::RemoveNode(NodeId id) {
		auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(), [id](const Node& n) { return n.Id == id; });
		if (it == m_Nodes.end()) return false;
		m_Nodes.erase(it);
		std::erase_if(m_Edges, [id](const Edge& e) { return e.From == id || e.To == id; });
		return true;
	}

	bool GraphModel::RemoveEdge(EdgeId id) {
		auto it = std::find_if(m_Edges.begin(), m_Edges.end(), [id](const Edge& e) { return e.Id == id; });
		if (it == m_Edges.end()) return false;
		m_Edges.erase(it);
		return true;
	}

	void GraphModel::RestoreNode(const Node& n) {
		m_Nodes.push_back(n);
	}

	void GraphModel::RestoreEdge(const Edge& e) {
		m_Edges.push_back(e);
	}

	Node* GraphModel::GetNode(NodeId id) {
		for (auto& n : m_Nodes) if (n.Id == id) return &n;
		return nullptr;
	}

	Edge* GraphModel::GetEdge(EdgeId id) {
		for (auto& e : m_Edges) if (e.Id == id) return &e;
		return nullptr;
	}

	const Node* GraphModel::GetNode(NodeId id) const {
		for (const auto& n : m_Nodes) if (n.Id == id) return &n;
		return nullptr;
	}

	const Edge* GraphModel::GetEdge(EdgeId id) const {
		for (const auto& e : m_Edges) if (e.Id == id) return &e;
		return nullptr;
	}

	void GraphModel::Clear() {
		m_Nodes.clear();
		m_Edges.clear();
		m_NextNodeId = 0;
		m_NextEdgeId = 0;
	}

	// ---- Serialization ----------------------------------------------------------
	// Binary format: magic "GCF\x01", then nodes, then edges, then ID counters.

	namespace {
		constexpr uint32_t k_Magic = 0x01464347u; // 'G','C','F',0x01

		template<typename T>
		bool BinWrite(FILE* f, const T& v) { return fwrite(&v, sizeof(T), 1, f) == 1; }

		template<typename T>
		bool BinRead(FILE* f, T& v) { return fread(&v, sizeof(T), 1, f) == 1; }

		bool BinWriteStr(FILE* f, const std::wstring& s) {
			uint32_t n = (uint32_t)s.size();
			if (!BinWrite(f, n)) return false;
			return n == 0 || fwrite(s.data(), sizeof(wchar_t), n, f) == n;
		}

		bool BinReadStr(FILE* f, std::wstring& s) {
			uint32_t n;
			if (!BinRead(f, n)) return false;
			s.resize(n);
			return n == 0 || fread(s.data(), sizeof(wchar_t), n, f) == n;
		}
	}

	bool GraphModel::Save(const wchar_t* path) const {
		FILE* f = nullptr;
		if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;

		bool ok = BinWrite(f, k_Magic)
			&& BinWrite(f, (uint32_t)m_Nodes.size());

		for (const auto& n : m_Nodes) {
			ok = ok
				&& BinWrite(f, n.Id)
				&& BinWrite(f, n.X) && BinWrite(f, n.Y)
				&& BinWrite(f, n.Width) && BinWrite(f, n.Height)
				&& BinWrite(f, n.Style.FillColor)
				&& BinWrite(f, n.Style.BorderColor)
				&& BinWrite(f, n.Style.TextColor)
				&& BinWrite(f, n.Style.BorderWidth)
				&& BinWrite(f, n.Style.CornerRadius)
				&& BinWriteStr(f, n.Label);
		}

		ok = ok && BinWrite(f, (uint32_t)m_Edges.size());

		for (const auto& e : m_Edges) {
			uint32_t directed = e.Style.Directed ? 1u : 0u;
			ok = ok
				&& BinWrite(f, e.Id)
				&& BinWrite(f, e.From) && BinWrite(f, e.To)
				&& BinWrite(f, e.Style.Color)
				&& BinWrite(f, e.Style.Width)
				&& BinWrite(f, directed)
				&& BinWriteStr(f, e.Label);
		}

		ok = ok && BinWrite(f, m_NextNodeId) && BinWrite(f, m_NextEdgeId);

		fclose(f);
		return ok;
	}

	bool GraphModel::Load(const wchar_t* path) {
		FILE* f = nullptr;
		if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;

		uint32_t magic = 0;
		if (!BinRead(f, magic) || magic != k_Magic) { fclose(f); return false; }

		uint32_t nodeCount = 0;
		bool ok = BinRead(f, nodeCount);

		std::vector<Node> nodes;
		nodes.reserve(nodeCount);
		for (uint32_t i = 0; i < nodeCount && ok; ++i) {
			Node n;
			ok = BinRead(f, n.Id)
				&& BinRead(f, n.X) && BinRead(f, n.Y)
				&& BinRead(f, n.Width) && BinRead(f, n.Height)
				&& BinRead(f, n.Style.FillColor)
				&& BinRead(f, n.Style.BorderColor)
				&& BinRead(f, n.Style.TextColor)
				&& BinRead(f, n.Style.BorderWidth)
				&& BinRead(f, n.Style.CornerRadius)
				&& BinReadStr(f, n.Label);
			if (ok) nodes.push_back(std::move(n));
		}

		uint32_t edgeCount = 0;
		ok = ok && BinRead(f, edgeCount);

		std::vector<Edge> edges;
		edges.reserve(edgeCount);
		for (uint32_t i = 0; i < edgeCount && ok; ++i) {
			Edge e;
			uint32_t directed = 1;
			ok = BinRead(f, e.Id)
				&& BinRead(f, e.From) && BinRead(f, e.To)
				&& BinRead(f, e.Style.Color)
				&& BinRead(f, e.Style.Width)
				&& BinRead(f, directed)
				&& BinReadStr(f, e.Label);
			if (ok) {
				e.Style.Directed = directed != 0;
				edges.push_back(std::move(e));
			}
		}

		uint32_t nextNode = 0, nextEdge = 0;
		ok = ok && BinRead(f, nextNode) && BinRead(f, nextEdge);
		fclose(f);

		if (!ok) return false;

		m_Nodes = std::move(nodes);
		m_Edges = std::move(edges);
		m_NextNodeId = nextNode;
		m_NextEdgeId = nextEdge;
		return true;
	}

} // namespace GraphCtrl
