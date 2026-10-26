#include "../include/NodeGraphRenderer.h"
#include "NodeGraphImaging.h"
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <atomic>

namespace NodeGraphCtrl {

static D2D1_COLOR_F ColorrefToD2D(COLORREF cr) {
    return D2D1::ColorF(
        GetRValue(cr) / 255.0f,
        GetGValue(cr) / 255.0f,
        GetBValue(cr) / 255.0f);
}

NodeGraphRenderer::~NodeGraphRenderer() {
    Shutdown();
}

bool NodeGraphRenderer::IsLightBackground() const {
    // perceived brightness
    return GetRValue(m_Background) * 299 + GetGValue(m_Background) * 587 + GetBValue(m_Background) * 114 > 128 * 1000;
}

D2D1_COLOR_F NodeGraphRenderer::SelectionColor() const {
    // yellow doesn't show on a light background
    return IsLightBackground() ? D2D1::ColorF(0.9f, 0.45f, 0.0f) : D2D1::ColorF(D2D1::ColorF::Yellow);
}

HRESULT NodeGraphRenderer::Init(HINSTANCE /*hInstance*/) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_D2dFactory.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_DwFactory.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-US",
        m_TextFormat.GetAddressOf());
    if (FAILED(hr)) return hr;

    m_TextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_TextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-US",
        m_EdgeTextFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_EdgeTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_EdgeTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // one line in its box (EdgeLabelWidth), cut short when longer than the most
    m_EdgeTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    hr = m_DwFactory->CreateEllipsisTrimmingSign(m_EdgeTextFormat.Get(), m_EdgeEllipsis.GetAddressOf());
    if (FAILED(hr)) return hr;
    DWRITE_TRIMMING edgeTrimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_EdgeTextFormat->SetTrimming(&edgeTrimming, m_EdgeEllipsis.Get());

    hr = m_DwFactory->CreateTextFormat(
        L"Consolas", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-US",
        m_CodeTextFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_CodeTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_CodeTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_CodeTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-US",
        m_TitleTextFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_TitleTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_TitleTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_TitleTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    hr = m_DwFactory->CreateEllipsisTrimmingSign(m_TitleTextFormat.Get(), m_Ellipsis.GetAddressOf());
    if (FAILED(hr)) return hr;
    DWRITE_TRIMMING trimming{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_TitleTextFormat->SetTrimming(&trimming, m_Ellipsis.Get());

    hr = m_DwFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f, L"en-US",
        m_PortTextFormat.GetAddressOf());
    if (FAILED(hr)) return hr;
    m_PortTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_PortTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    hr = m_D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, m_DashStyle.GetAddressOf());
    if (FAILED(hr)) return hr;

    // the lines of edges (EdgeLine); round dots
    const D2D1_DASH_STYLE dashes[] = { D2D1_DASH_STYLE_SOLID, D2D1_DASH_STYLE_DASH, D2D1_DASH_STYLE_DOT, D2D1_DASH_STYLE_DASH_DOT };
    for (int i = 1; i < 4; i++) {
        const D2D1_CAP_STYLE cap = dashes[i] == D2D1_DASH_STYLE_DASH ? D2D1_CAP_STYLE_FLAT : D2D1_CAP_STYLE_ROUND;
        auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, cap, D2D1_LINE_JOIN_ROUND, 10.0f, dashes[i]);
        hr = m_D2dFactory->CreateStrokeStyle(props, nullptr, 0, m_LineStyles[i].GetAddressOf());
        if (FAILED(hr)) return hr;
    }
    return hr;
}

void NodeGraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_DashStyle.Reset();
    for (auto& style : m_LineStyles) style.Reset();
    m_EdgeEllipsis.Reset();
    m_EdgeTextFormat.Reset();
    m_CodeTextFormat.Reset();
    m_PortTextFormat.Reset();
    m_Ellipsis.Reset();
    m_TitleTextFormat.Reset();
    m_TextFormat.Reset();
    m_DwFactory.Reset();
    m_D2dFactory.Reset();
}

HRESULT NodeGraphRenderer::EnsureDeviceResources(HDC /*hdc*/, const RECT& /*rc*/) {
    if (m_RenderTarget) return S_OK;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    HRESULT hr = m_D2dFactory->CreateDCRenderTarget(&props, m_RenderTarget.GetAddressOf());
    if (FAILED(hr)) return hr;
    // unique in the process, so that a draw handler shared by several controls can tell their targets apart
    static std::atomic<uint32_t> s_NextGeneration = 1;
    m_DeviceGeneration = s_NextGeneration++;

    hr = m_RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), m_Brush.GetAddressOf());
    return hr;
}

void NodeGraphRenderer::DiscardDeviceResources() {
    m_Bitmaps.clear();
    m_Layer.Reset();
    m_Brush.Reset();
    m_RenderTarget.Reset();
}

ID2D1Bitmap* NodeGraphRenderer::GetBitmap(const NodeGraphModel& model, ImageId id) {
    const Image* image = model.GetImage(id);
    if (!image || !m_RenderTarget)
        return nullptr;

    auto it = m_Bitmaps.find(image->Key);
    if (it == m_Bitmaps.end())
        it = m_Bitmaps.emplace(image->Key, CreateBitmap(*image)).first;
    return it->second.Get();
}

ComPtr<ID2D1Bitmap> NodeGraphRenderer::CreateBitmap(const Image& image) {
    Imaging::ComScope com;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapSource> source;
    if (FAILED(Imaging::CreateFactory(factory.GetAddressOf())) ||
        FAILED(Imaging::Decode(factory.Get(), image.Data.data(), image.Data.size(), source.GetAddressOf())))
        return nullptr;

    // a bitmap larger than the device allows is made smaller
    UINT w = 0, h = 0;
    source->GetSize(&w, &h);
    const UINT maxSide = m_RenderTarget->GetMaximumBitmapSize();
    if (w > maxSide || h > maxSide) {
        const double scale = (double)maxSide / (std::max)(w, h);
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(source.Get(), (std::max)(1u, (UINT)(w * scale)), (std::max)(1u, (UINT)(h * scale)), WICBitmapInterpolationModeFant)))
            return nullptr;
        source = scaler;
    }

    // 96 DPI, so that the size in DIPs is the size in pixels whatever the file says
    auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(m_RenderTarget->CreateBitmapFromWicBitmap(source.Get(), &props, bitmap.GetAddressOf())))
        return nullptr;
    return bitmap;
}

bool NodeGraphRenderer::RenderNodePicture(const NodeGraphModel& model, const Node& n, float scale, std::vector<uint8_t>& png, Rect& bounds) {
    const float margin = (std::max)(n.Style.BorderWidth * 2.5f, 2.0f);
    bounds = { n.X - n.Width * 0.5f - margin, n.Y - n.Height * 0.5f - margin, n.X + n.Width * 0.5f + margin, n.Y + n.Height * 0.5f + margin };
    const int cx = (std::max)(1, (int)std::ceil(bounds.Width() * scale)), cy = (std::max)(1, (int)std::ceil(bounds.Height() * scale));
    if (cx > 16384 || cy > 16384)
        return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return false;
    HDC hdc = ::CreateCompatibleDC(nullptr);
    auto old = ::SelectObject(hdc, bitmap);

    RECT rc{ 0, 0, cx, cy };
    bool ok = SUCCEEDED(EnsureDeviceResources(hdc, rc));
    if (ok) {
        HRESULT hr = m_RenderTarget->BindDC(hdc, &rc);
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
            hr = EnsureDeviceResources(hdc, rc);
            if (SUCCEEDED(hr))
                hr = m_RenderTarget->BindDC(hdc, &rc);
        }
        ok = SUCCEEDED(hr);
    }
    if (ok)
        m_RenderTarget->SetDpi(96.0f, 96.0f);   // (scale pixels per graph unit, whatever the screen's DPI)
    if (ok) {
        ViewTransform vt;
        vt.Scale = scale;
        vt.OffsetX = -bounds.Left * scale;
        vt.OffsetY = -bounds.Top * scale;
        const bool exporting = m_Exporting;
        m_Exporting = true;
        m_RenderTarget->BeginDraw();
        m_RenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
        m_EdgePairs.clear();
        DrawNode(model, n, vt, false);
        ok = SUCCEEDED(m_RenderTarget->EndDraw());
        m_Exporting = exporting;
    }
    ::GdiFlush();

    if (ok) {
        // the DIB has the target's premultiplied alpha
        Imaging::ComScope com;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmap> wic;
        ok = SUCCEEDED(Imaging::CreateFactory(factory.GetAddressOf()))
            && SUCCEEDED(factory->CreateBitmapFromMemory(cx, cy, GUID_WICPixelFormat32bppPBGRA, cx * 4, cx * 4 * cy, static_cast<BYTE*>(bits), wic.GetAddressOf()))
            && SUCCEEDED(Imaging::EncodePng(factory.Get(), wic.Get(), png));
    }
    ::SelectObject(hdc, old);
    ::DeleteDC(hdc);
    ::DeleteObject(bitmap);
    return ok;
}

// The bitmaps of images the model no longer has (removed, or of another model the renderer drew before).
void NodeGraphRenderer::PruneBitmaps(const NodeGraphModel& model) {
    if (m_Bitmaps.size() <= model.Images().size())
        return;
    std::unordered_set<uint64_t> keys;
    keys.reserve(model.Images().size());
    for (const auto& image : model.Images())
        keys.insert(image.Key);
    std::erase_if(m_Bitmaps, [&](const auto& entry) { return !keys.contains(entry.first); });
}

//
// What the control gives is in pixels (the view, the client area, the mouse); the target is at the window's DPI (an image
// at 96 DPI), so the chrome (the minimap, handles, labels that keep their size, line widths) is in DIPs and scales with it.
// The pixels are made DIPs here: the graph lands on the same pixels the control hit-tests.
//
void NodeGraphRenderer::Render(HDC hdc, const RECT& clientPixels,
                           const NodeGraphModel& model, const ViewTransform& viewPixels,
                           const std::vector<NodeId>& selectedNodes, EdgeId selectedEdge,
                           bool drawGrid, const EdgePreview& previewPixels,
                           const RubberBand& rubberBandPixels, const MinimapConfig& minimapPixels,
                           const ResizeOverlay& resizeOverlay) {
    if (FAILED(EnsureDeviceResources(hdc, clientPixels))) return;

    HRESULT hr = m_RenderTarget->BindDC(hdc, &clientPixels);
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        if (FAILED(EnsureDeviceResources(hdc, clientPixels))) return;
        m_RenderTarget->BindDC(hdc, &clientPixels);
    }

    const float f = m_Exporting ? 1.0f : m_DpiScale;
    m_RenderTarget->SetDpi(96.0f * f, 96.0f * f);
    const ViewTransform vt{ viewPixels.OffsetX / f, viewPixels.OffsetY / f, viewPixels.Scale / f };
    const RECT clientRect{ (LONG)std::floor(clientPixels.left / f), (LONG)std::floor(clientPixels.top / f),
        (LONG)std::ceil(clientPixels.right / f), (LONG)std::ceil(clientPixels.bottom / f) };
    EdgePreview preview = previewPixels;
    preview.ScreenX /= f;
    preview.ScreenY /= f;
    RubberBand rubberBand = rubberBandPixels;
    rubberBand.X0 /= f; rubberBand.Y0 /= f; rubberBand.X1 /= f; rubberBand.Y1 /= f;
    MinimapConfig minimap = minimapPixels;
    minimap.X /= f; minimap.Y /= f; minimap.Width /= f; minimap.Height /= f;

    PruneBitmaps(model);
    // (the threshold is of pixels per graph unit, as the control's zoom)
    m_Plain = !m_Exporting && viewPixels.Scale < m_DetailThreshold;

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(ColorrefToD2D(m_Background));

    if (drawGrid) DrawGrid(clientRect, vt);

    // Only what is in view is drawn. The view in graph units, with room for what goes past the shapes: borders,
    // arrowheads and edge labels (a size on the screen) and the loops of edges to their own node (in graph units).
    const float slack = 60.0f / vt.Scale + 30.0f;
    const auto viewMin = vt.ToGraph((float)clientRect.left, (float)clientRect.top);
    const auto viewMax = vt.ToGraph((float)clientRect.right, (float)clientRect.bottom);
    const Rect view{ viewMin.x - slack, viewMin.y - slack, viewMax.x + slack, viewMax.y + slack };
    auto inView = [&](float left, float top, float right, float bottom) {
        return right >= view.Left && left <= view.Right && bottom >= view.Top && top <= view.Bottom;
    };

    m_EdgePairs.clear();
    m_EdgePairs.reserve(model.Edges().size());     // (kept from frame to frame: grows once)
    for (const auto& e : model.Edges())
        m_EdgePairs.insert((uint64_t)e.From << 32 | e.To);
    m_ElbowShifts = &model.ElbowShifts();

    // The highlight: the focus nodes, their neighbors and the edges of the focus are drawn last, over a veil that dims
    // the rest. Nothing is dimmed if it is all in the highlight.
    std::unordered_set<NodeId> related;
    std::unordered_set<EdgeId> relatedEdges;
    if (!m_Highlight.empty()) {
        const std::unordered_set<NodeId> focus(m_Highlight.begin(), m_Highlight.end());
        related = focus;
        for (const auto& e : model.Edges())
            if (focus.contains(e.From) || focus.contains(e.To)) {
                relatedEdges.insert(e.Id);
                related.insert(e.From);
                related.insert(e.To);
            }
        if (related.size() >= model.Nodes().size() && relatedEdges.size() >= model.Edges().size()) {
            related.clear();
            relatedEdges.clear();
        }
    }
    const bool highlight = !related.empty();

    std::unordered_set<NodeId> selected(selectedNodes.begin(), selectedNodes.end());

    // the groups: a collapsed one stands as a node for its nodes; an expanded one is behind everything
    m_Collapsed.clear();
    for (const auto& g : model.Groups())
        if (g.Collapsed && !g.Nodes.empty())
            m_Collapsed.emplace(g.Id, model.CollapsedNode(g));
    // (the labels around the view too: the ones that could push those in view, so that they stay put as it scrolls)
    const float aroundX = view.Right - view.Left, aroundY = view.Bottom - view.Top;
    PlaceEdgeLabels(model, vt, { view.Left - aroundX, view.Top - aroundY, view.Right + aroundX, view.Bottom + aroundY });
    auto groupSelected = [&](const Group& g) {      // all of its nodes
        return !g.Nodes.empty() && std::all_of(g.Nodes.begin(), g.Nodes.end(), [&](NodeId n) { return selected.contains(n); });
    };
    auto drawGroups = [&](bool collapsed) {
        for (const auto& g : model.Groups()) {
            if (g.Collapsed != collapsed) continue;
            const Rect box = model.GetGroupBox(g);
            if (!box.IsEmpty() && inView(box.Left, box.Top, box.Right, box.Bottom))
                DrawGroup(model, g, vt, groupSelected(g));
        }
    };
    drawGroups(false);

    auto drawEdges = [&](bool inHighlight) {
        for (const auto& e : model.Edges()) {
            if (highlight && relatedEdges.contains(e.Id) != inHighlight) continue;
            const Node* from = EdgeEnd(model, e.From);
            const Node* to = EdgeEnd(model, e.To);
            if (!from || !to) continue;
            if (from == to && model.IsHidden(e.From)) continue;	// inside a collapsed group
            // the edge stays within the box of the centers of its nodes and its bends (a curve a little past it)
            float left = std::min(from->X, to->X), right = std::max(from->X, to->X);
            float top = std::min(from->Y, to->Y), bottom = std::max(from->Y, to->Y);
            if (e.FromPort != InvalidPort || e.ToPort != InvalidPort || e.Style.Elbow) {
                // out of a port the curve goes straight out of the node first, as far as a third of the way (at least 30);
                // an elbow goes as far as 20 out of a node
                const float pad = std::max({ from->Width, from->Height, to->Width, to->Height }) * 0.5f +
                    std::max(30.0f, std::hypot(to->X - from->X, to->Y - from->Y) / 3);
                left -= pad; right += pad; top -= pad; bottom += pad;
            }
            else if (from == to) {   // the loop and the label right of it
                left -= from->Width * 0.5f; right += from->Width * 0.5f + (e.Label.empty() ? 0.0f : 140.0f);
                top -= from->Height * 0.5f; bottom += from->Height * 0.5f;
            }
            for (const auto& w : e.Waypoints) {
                left = std::min(left, w.X); right = std::max(right, w.X);
                top = std::min(top, w.Y); bottom = std::max(bottom, w.Y);
            }
            if (inView(left, top, right, bottom))
                DrawEdge(e, model, vt, e.Id == selectedEdge ? EdgeEmphasis::Selected : inHighlight && highlight ? EdgeEmphasis::Highlighted : EdgeEmphasis::Normal);
        }
    };
    auto drawNodes = [&](bool inHighlight) {
        for (const auto& n : model.Nodes()) {
            if (highlight && related.contains(n.Id) != inHighlight) continue;
            if (!m_Collapsed.empty() && model.IsHidden(n.Id)) continue;
            if (inView(n.X - n.Width * 0.5f, n.Y - n.Height * 0.5f, n.X + n.Width * 0.5f, n.Y + n.Height * 0.5f))
                DrawNode(model, n, vt, selected.contains(n.Id));
        }
    };

    drawEdges(false);
    if (preview.Active && !highlight)
        DrawEdgePreview(preview, model, vt);
    drawNodes(false);
    drawGroups(true);
    if (highlight) {
        D2D1_COLOR_F veil = ColorrefToD2D(m_Background);
        veil.a = 0.65f;
        m_Brush->SetColor(veil);
        m_RenderTarget->FillRectangle(D2D1::RectF((float)clientRect.left, (float)clientRect.top, (float)clientRect.right, (float)clientRect.bottom), m_Brush.Get());
        drawEdges(true);
        if (preview.Active)
            DrawEdgePreview(preview, model, vt);
        drawNodes(true);
    }

    // alignment guides: dashed, in the selection color
    if (!m_Guides.empty()) {
        m_Brush->SetColor(SelectionColor());
        for (const auto& g : m_Guides) {
            auto a = g.Vertical ? vt.ToScreen(g.At, g.From) : vt.ToScreen(g.From, g.At);
            auto b = g.Vertical ? vt.ToScreen(g.At, g.To) : vt.ToScreen(g.To, g.At);
            m_RenderTarget->DrawLine(a, b, m_Brush.Get(), 1.0f, m_DashStyle.Get());
        }
    }

    if (rubberBand.Active)
        DrawRubberBand(rubberBand);

    if (resizeOverlay.Node != InvalidNode) {
        const Node* rn = model.GetNode(resizeOverlay.Node);
        if (rn) DrawResizeHandles(*rn, vt, resizeOverlay.HoveredHandle);
    }

    DrawMinimap(minimap, clientRect, model, vt);

    hr = m_RenderTarget->EndDraw();
    m_Plain = false;    // (a picture of a node, RenderNodePicture, is drawn in full)
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

void NodeGraphRenderer::DrawEdgePreview(const EdgePreview& preview, const NodeGraphModel& model,
                                    const ViewTransform& vt) {
    const Node* from = model.GetNode(preview.FromNode);
    if (!from) return;
    const Node* to = model.GetNode(preview.ToNode);
    const Port* fromPort = FindPort(*from, preview.FromPort);
    const Port* toPort = to ? FindPort(*to, preview.ToPort) : nullptr;

    // the ends on the screen, and the ways out of their nodes (none: not at a port)
    struct End { D2D1_POINT_2F At; Point Out; };
    End a{}, b{};
    if (toPort) {
        const Point p = PortPosition(*to, *toPort);
        b = { vt.ToScreen(p.X, p.Y), PortNormal(toPort->Side) };
    }
    else
        b = { { preview.ScreenX, preview.ScreenY }, {} };
    if (fromPort) {
        const Point p = PortPosition(*from, *fromPort);
        a = { vt.ToScreen(p.X, p.Y), PortNormal(fromPort->Side) };
    }
    else {
        // on the outline, toward the other end
        const auto c = vt.ToScreen(from->X, from->Y);
        const float dx = b.At.x - c.x, dy = b.At.y - c.y, len = std::sqrtf(dx * dx + dy * dy);
        if (len < 1.0f) return;
        const float t = std::min(NodeBorderDistance(*from, dx / len, dy / len) * vt.Scale, len);
        a = { { c.x + dx / len * t, c.y + dy / len * t }, {} };
    }
    if (preview.Elbow && (fromPort || toPort) && !fromPort) {
        // an elbow ends at the middle of the side facing the other end
        const auto c = vt.ToScreen(from->X, from->Y);
        if (std::abs(b.At.x - c.x) >= std::abs(b.At.y - c.y))
            a = { { c.x + (b.At.x < c.x ? -1 : 1) * NodeHalfWidthAt(*from, 0) * vt.Scale, c.y }, { b.At.x < c.x ? -1.0f : 1.0f, 0 } };
        else
            a = { { c.x, c.y + (b.At.y < c.y ? -1 : 1) * NodeHalfHeightAt(*from, 0) * vt.Scale }, { 0, b.At.y < c.y ? -1.0f : 1.0f } };
    }
    // the nodes on the screen, for an elbow to go round
    auto box = [&](const Node* n) {
        if (!n) return Rect{};
        const auto c = vt.ToScreen(n->X, n->Y);
        const float hw = n->Width * vt.Scale * 0.5f, hh = n->Height * vt.Scale * 0.5f;
        return Rect{ c.x - hw, c.y - hh, c.x + hw, c.y + hh };
    };
    Rect boxA = box(from), boxB = box(to);
    if (preview.Reversed) {
        std::swap(a, b);     // the arrow goes into the port it came out of
        std::swap(boxA, boxB);
    }

    const float dx = b.At.x - a.At.x, dy = b.At.y - a.At.y, len = std::sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 0.3f, 0.75f));
    D2D1_POINT_2F before = a.At;     // where the line comes into its end from (for the arrowhead)
    if (!fromPort && !toPort)
        m_RenderTarget->DrawLine(a.At, b.At, m_Brush.Get(), 2.0f, m_DashStyle.Get());
    else if (preview.Elbow) {
        // its corners, as the edge will have them (ElbowPath); the end at the mouse either way
        const EdgeShape shape = RoundCorners(ElbowPath({ a.At.x, a.At.y }, a.Out, { b.At.x, b.At.y }, b.Out, {}, 20.0f * vt.Scale, boxA, boxB),
            EdgeStyle{}.CornerRadius * vt.Scale);
        const std::vector<Point>& path = shape.Path;
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (path.size() >= 2 && SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) && SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
            sink->BeginFigure({ path[0].X, path[0].Y }, D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < path.size(); i++)
                if (shape.Controls.empty())
                    sink->AddLine({ path[i].X, path[i].Y });
                else
                    sink->AddBezier({ { shape.Controls[i * 2 - 2].X, shape.Controls[i * 2 - 2].Y }, { shape.Controls[i * 2 - 1].X, shape.Controls[i * 2 - 1].Y }, { path[i].X, path[i].Y } });
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close()))
                m_RenderTarget->DrawGeometry(geometry.Get(), m_Brush.Get(), 2.0f, m_DashStyle.Get());
        }
        if (path.size() >= 2) before = { path[path.size() - 2].X, path[path.size() - 2].Y };
    }
    else {
        // a curve straight out of the ports, as the edge will be (CurveControls)
        const float d = std::max(len / 3, 30.0f * vt.Scale);
        const D2D1_POINT_2F c1{ a.At.x + a.Out.X * d, a.At.y + a.Out.Y * d }, c2{ b.At.x + b.Out.X * d, b.At.y + b.Out.Y * d };
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) && SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(a.At, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddBezier({ c1, c2, b.At });
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close()))
                m_RenderTarget->DrawGeometry(geometry.Get(), m_Brush.Get(), 2.0f, m_DashStyle.Get());
        }
        if (b.Out.X != 0 || b.Out.Y != 0) before = c2;
    }
    const float ax = b.At.x - before.x, ay = b.At.y - before.y, al = std::sqrtf(ax * ax + ay * ay);
    if (al > 0)
        DrawArrowhead(b.At, { ax / al, ay / al });
}

void NodeGraphRenderer::DrawGrid(const RECT& rc, const ViewTransform& vt) {
    const float gridSpacing = m_GridSize * vt.Scale;
    if (gridSpacing < 8.0f) return;

    m_Brush->SetColor(IsLightBackground() ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f));
    m_Brush->SetOpacity(1.0f);

    float startX = fmodf(vt.OffsetX, gridSpacing);
    float startY = fmodf(vt.OffsetY, gridSpacing);

    for (float x = startX; x < rc.right; x += gridSpacing)
        m_RenderTarget->DrawLine({ x, 0 }, { x, (float)rc.bottom }, m_Brush.Get(), 0.5f);

    for (float y = startY; y < rc.bottom; y += gridSpacing)
        m_RenderTarget->DrawLine({ 0, y }, { (float)rc.right, y }, m_Brush.Get(), 0.5f);
}

void NodeGraphRenderer::DrawEdge(const Edge& e, const NodeGraphModel& model,
                             const ViewTransform& vt, EdgeEmphasis emphasis) {
    const Node* from = EdgeEnd(model, e.From);
    const Node* to   = EdgeEnd(model, e.To);
    if (!from || !to) return;
    if (from == to && model.IsHidden(e.From)) return;     // inside a collapsed group

    const bool loop = IsSelfLoop(e, *from, *to);
    if (m_Plain) {
        if (!loop)          // (a loop is a few pixels this far out)
            DrawEdgePlain(e, *from, *to, vt, emphasis);
        return;
    }
    if (loop) {
        DrawSelfLoop(e, *from, vt, emphasis);
        return;
    }

    // An edge that has one going the other way is moved to its right (7 pixels), so that the two can be told apart.
    const float lane = m_EdgePairs.contains((uint64_t)e.To << 32 | e.From) ? 7.0f / vt.Scale : 0.0f;
    const EdgeShape shape = GetEdgeShape(e, *from, *to, lane, m_ElbowShifts ? ShiftsOf(*m_ElbowShifts, e.Id) : std::span<const float>{});
    const std::vector<Point>& path = shape.Path;
    if (path.size() < 2) return;
    const std::vector<Point>& controls = shape.Controls;
    auto screen = [&](const Point& p) { return vt.ToScreen(p.X, p.Y); };

    SetEdgeBrush(e, emphasis);
    const float width = EdgeWidth(e, emphasis);
    ID2D1StrokeStyle* stroke = m_LineStyles[(int)e.Style.Line].Get();
    if (controls.empty() && !stroke) {
        for (size_t i = 0; i + 1 < path.size(); i++)
            m_RenderTarget->DrawLine(screen(path[i]), screen(path[i + 1]), m_Brush.Get(), width);
    }
    else {
        // one figure, so that the dashes go on around the bends
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) && SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(screen(path[0]), D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 0; i + 1 < path.size(); i++) {
                if (controls.empty())
                    sink->AddLine(screen(path[i + 1]));
                else
                    sink->AddBezier({ screen(controls[i * 2]), screen(controls[i * 2 + 1]), screen(path[i + 1]) });
            }
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close()))
                m_RenderTarget->DrawGeometry(geometry.Get(), m_Brush.Get(), width, stroke);
        }
    }

    if (e.Style.Directed) {
        // along the last piece (the curve's tangent at its end)
        const Point& end = path.back();
        Point before = controls.empty() ? path[path.size() - 2] : controls.back();
        float dx = end.X - before.X, dy = end.Y - before.Y;
        if (dx * dx + dy * dy < 1e-6f) { before = path[path.size() - 2]; dx = end.X - before.X; dy = end.Y - before.Y; }
        const float l = std::sqrtf(dx * dx + dy * dy);
        if (l > 0)
            DrawArrowhead(screen(end), { dx / l, dy / l }, e.Style.Arrow);
    }

    if (!e.Label.empty()) {
        auto placed = m_LabelPlaces.find(e.Id);
        DrawEdgeLabel(e, placed != m_LabelPlaces.end() ? placed->second : shape.Label, vt, emphasis);
    }
}

// The places of the labels that are drawn this frame, as their edges are drawn (DrawEdge), off the nodes, the title
// bands of groups and the labels of loops (which stay where they are). Only what is in the region: around the view.
void NodeGraphRenderer::PlaceEdgeLabels(const NodeGraphModel& model, const ViewTransform& vt, const Rect& region) {
    m_LabelPlaces.clear();
    if (!m_AvoidLabelOverlap || m_Plain)
        return;
    auto inRegion = [&](const Rect& r) { return r.Right >= region.Left && r.Left <= region.Right && r.Bottom >= region.Top && r.Top <= region.Bottom; };
    auto box = [](const Node& n) { return Rect{ n.X - n.Width * 0.5f, n.Y - n.Height * 0.5f, n.X + n.Width * 0.5f, n.Y + n.Height * 0.5f }; };
    std::vector<const Edge*> labeled;
    for (const auto& e : model.Edges()) {
        if (e.Label.empty() || (e.Style.ScaleLabel && vt.Scale < 0.3f))     // (as DrawEdgeLabel draws them)
            continue;
        // the box of its ends' centers and bends (as the culling has it)
        const Node* from = model.GetNode(e.From);
        const Node* to = model.GetNode(e.To);
        if (!from || !to) continue;
        Rect r{ std::min(from->X, to->X), std::min(from->Y, to->Y), std::max(from->X, to->X), std::max(from->Y, to->Y) };
        for (const auto& w : e.Waypoints) {
            r.Left = std::min(r.Left, w.X); r.Right = std::max(r.Right, w.X);
            r.Top = std::min(r.Top, w.Y); r.Bottom = std::max(r.Bottom, w.Y);
        }
        if (inRegion(r))
            labeled.push_back(&e);
    }
    if (labeled.empty())
        return;

    std::vector<Rect> obstacles;
    obstacles.reserve(model.Nodes().size() + model.Groups().size());
    for (const auto& n : model.Nodes())
        if ((m_Collapsed.empty() || !model.IsHidden(n.Id)) && inRegion(box(n)))
            obstacles.push_back(box(n));
    for (const auto& [id, n] : m_Collapsed)
        obstacles.push_back(box(n));
    for (const auto& g : model.Groups())
        if (!g.Collapsed)
            obstacles.push_back(model.GetGroupHeader(g));

    std::vector<LabelRequest> requests;
    std::vector<EdgeId> ids;
    requests.reserve(labeled.size());
    ids.reserve(labeled.size());
    for (const Edge* e : labeled) {
        const Node* from = EdgeEnd(model, e->From);
        const Node* to = EdgeEnd(model, e->To);
        if (!from || !to || (from == to && model.IsHidden(e->From))) continue;
        // a label's size in graph units: as large as the zoom makes it, or the same on the screen at any zoom
        const float k = e->Style.ScaleLabel ? 1.0f : 1.0f / vt.Scale;
        const float w = EdgeLabelWidth(e->Label) * k, h = k_EdgeLabelHeight * k;
        if (IsSelfLoop(*e, *from, *to)) {
            const auto loop = SelfLoopPath(*from, std::max(24.0f, 10.0f / vt.Scale));   // (as DrawSelfLoop)
            const float left = loop[1].X + 4.0f * k;
            obstacles.push_back({ left, from->Y - h * 0.5f, left + w, from->Y + h * 0.5f });
            continue;
        }
        const float lane = m_EdgePairs.contains((uint64_t)e->To << 32 | e->From) ? 7.0f / vt.Scale : 0.0f;
        EdgeShape shape = GetEdgeShape(*e, *from, *to, lane, m_ElbowShifts ? ShiftsOf(*m_ElbowShifts, e->Id) : std::span<const float>{});
        if (shape.Path.size() < 2) continue;
        LabelRequest r;
        r.Preferred = shape.Label;
        r.Line = FlattenEdgeShape(shape);
        r.Width = w;
        r.Height = h;
        requests.push_back(std::move(r));
        ids.push_back(e->Id);
    }
    const std::vector<Point> centers = PlaceLabels(requests, obstacles);
    m_LabelPlaces.reserve(ids.size());
    for (size_t i = 0; i < ids.size(); i++)
        m_LabelPlaces.emplace(ids[i], centers[i]);
}

void NodeGraphRenderer::SetEdgeBrush(const Edge& e, EdgeEmphasis emphasis) {
    m_Brush->SetColor(emphasis == EdgeEmphasis::Normal ? ColorrefToD2D(e.Style.Color) : SelectionColor());
}

float NodeGraphRenderer::EdgeWidth(const Edge& e, EdgeEmphasis emphasis) const {
    return e.Style.Width * (emphasis == EdgeEmphasis::Selected ? 2.0f : emphasis == EdgeEmphasis::Highlighted ? 1.6f : 1.0f);
}

// On a dark box at a point of the graph: as large as the zoom makes it (ScaleLabel; not drawn when too small to read),
// or the same size at any zoom.
// beside: the box starts a little right of the point (a self loop's label), instead of around it.
void NodeGraphRenderer::DrawEdgeLabel(const Edge& e, const Point& at, const ViewTransform& vt, EdgeEmphasis emphasis, bool beside) {
    const float lw = EdgeLabelWidth(e.Label), lh = k_EdgeLabelHeight, gap = 4.0f;
    D2D1_RECT_F box;
    if (e.Style.ScaleLabel) {
        if (vt.Scale < 0.3f) return;
        m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(vt.OffsetX, vt.OffsetY));
        const float left = beside ? at.X + gap : at.X - lw * 0.5f;
        box = { left, at.Y - lh * 0.5f, left + lw, at.Y + lh * 0.5f };
    }
    else {
        auto mid = vt.ToScreen(at.X, at.Y);
        const float left = beside ? mid.x + gap : mid.x - lw * 0.5f;
        box = { left, mid.y - lh * 0.5f, left + lw, mid.y + lh * 0.5f };
    }
    m_Brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.88f));
    m_RenderTarget->FillRectangle(box, m_Brush.Get());
    m_Brush->SetColor(emphasis != EdgeEmphasis::Normal ? SelectionColor() : D2D1::ColorF(0.85f, 0.85f, 0.85f));
    m_RenderTarget->DrawTextW(e.Label.c_str(), (UINT32)e.Label.size(), m_EdgeTextFormat.Get(), box, m_Brush.Get());
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

// A jump to its own block: a loop on the right side of the node.
void NodeGraphRenderer::DrawSelfLoop(const Edge& e, const Node& n, const ViewTransform& vt, EdgeEmphasis emphasis) {
    // 24 units past the node, but at least 10 pixels
    const float reach = std::max(24.0f, 10.0f / vt.Scale);
    const auto loop = SelfLoopPath(n, reach);
    auto screen = [&](const Point& p) { return vt.ToScreen(p.X, p.Y); };
    const D2D1_POINT_2F end = screen(loop[3]);

    SetEdgeBrush(e, emphasis);
    const float width = EdgeWidth(e, emphasis);
    ID2D1StrokeStyle* stroke = m_LineStyles[(int)e.Style.Line].Get();
    // the line stops at the base of a solid arrowhead
    const bool solidHead = e.Style.Directed && (e.Style.Arrow == ArrowHead::Filled || e.Style.Arrow == ArrowHead::Diamond);
    const D2D1_POINT_2F points[] = { screen(loop[0]), screen(loop[1]), screen(loop[2]), { end.x + (solidHead ? 8.0f : 0.0f), end.y } };
    ComPtr<ID2D1PathGeometry> geometry;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(geometry.GetAddressOf())) && SUCCEEDED(geometry->Open(sink.GetAddressOf()))) {
        sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(points + 1, 3);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (SUCCEEDED(sink->Close()))
            m_RenderTarget->DrawGeometry(geometry.Get(), m_Brush.Get(), width, stroke);
    }
    if (e.Style.Directed)
        DrawArrowhead(end, { -1.0f, 0.0f }, e.Style.Arrow);
    if (!e.Label.empty())
        DrawEdgeLabel(e, { loop[1].X, n.Y }, vt, emphasis, true);
}

// At the end of an edge, pointing along dir (a unit vector), 10 pixels long at any zoom, in the brush's color.
void NodeGraphRenderer::DrawArrowhead(D2D1_POINT_2F tip, D2D1_POINT_2F dir, ArrowHead style) {
    const float size = 10.0f;
    const float angle = 0.4f; // radians half-angle
    float cosA = std::cosf(angle), sinA = std::sinf(angle);

    float bx = -dir.x * size, by = -dir.y * size;
    // Rotate back-vector by +angle and -angle
    D2D1_POINT_2F l = { tip.x + bx * cosA - by * sinA, tip.y + bx * sinA + by * cosA };
    D2D1_POINT_2F r = { tip.x + bx * cosA + by * sinA, tip.y - bx * sinA + by * cosA };

    switch (style) {
        case ArrowHead::Open:
            m_RenderTarget->DrawLine(l, tip, m_Brush.Get(), 1.5f);
            m_RenderTarget->DrawLine(r, tip, m_Brush.Get(), 1.5f);
            return;
        case ArrowHead::Circle: {
            const float radius = 4.0f;
            m_RenderTarget->FillEllipse({ { tip.x - dir.x * radius, tip.y - dir.y * radius }, radius, radius }, m_Brush.Get());
            return;
        }
        default:
            break;
    }

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(m_D2dFactory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
        return;
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    if (style == ArrowHead::Diamond) {
        // as long as the triangle, narrower, and its back half too
        const float half = size * 0.5f, side = size * 0.3f;
        const D2D1_POINT_2F mid{ tip.x - dir.x * half, tip.y - dir.y * half };
        sink->AddLine({ mid.x - dir.y * side, mid.y + dir.x * side });
        sink->AddLine({ tip.x - dir.x * size, tip.y - dir.y * size });
        sink->AddLine({ mid.x + dir.y * side, mid.y - dir.x * side });
    }
    else {
        sink->AddLine(l);
        sink->AddLine(r);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close()))
        m_RenderTarget->FillGeometry(path.Get(), m_Brush.Get());
}

constexpr float k_MinIconPixels = 6.0f;    // a smaller icon (on the target) isn't drawn

static COLORREF Darker(COLORREF cr, float factor = 0.72f) {
    return RGB(GetRValue(cr) * factor, GetGValue(cr) * factor, GetBValue(cr) * factor);
}

// A node where an edge ends: the node, or the box of the collapsed group it is in.
const Node* NodeGraphRenderer::EdgeEnd(const NodeGraphModel& model, NodeId id) const {
    if (!m_Collapsed.empty()) {
        auto it = m_Collapsed.find(model.GroupOf(id));
        if (it != m_Collapsed.end()) return &it->second;
    }
    return model.GetNode(id);
}

// An expanded group: a box around its nodes (behind them and their edges), its title in a band on top, and a button to
// collapse it. Collapsed: a box in its nodes' place with its title and how many nodes it has, and a button to expand it.
void NodeGraphRenderer::DrawGroup(const NodeGraphModel& model, const Group& g, const ViewTransform& vt, bool selected) {
    const Rect box = model.GetGroupBox(g);
    auto screen = [&](const Rect& r) -> D2D1_RECT_F {
        auto a = vt.ToScreen(r.Left, r.Top), b = vt.ToScreen(r.Right, r.Bottom);
        return { a.x, a.y, b.x, b.y };
    };
    const float radius = 8.0f * vt.Scale;
    const D2D1_ROUNDED_RECT rr{ screen(box), radius, radius };
    const D2D1_COLOR_F fill = ColorrefToD2D(g.FillColor);

    if (g.Collapsed) {
        m_Brush->SetColor(fill);
        m_RenderTarget->FillRoundedRectangle(rr, m_Brush.Get());
    }
    else {
        m_Brush->SetColor(D2D1::ColorF(fill.r, fill.g, fill.b, 0.18f));
        m_RenderTarget->FillRoundedRectangle(rr, m_Brush.Get());
        if (!m_Plain) {
            m_Brush->SetColor(D2D1::ColorF(fill.r, fill.g, fill.b, 0.8f));
            FillBand(screen(model.GetGroupHeader(g)), radius, true, false);
        }
    }
    m_Brush->SetColor(selected ? SelectionColor() : ColorrefToD2D(g.BorderColor));
    m_RenderTarget->DrawRoundedRectangle(rr, m_Brush.Get(), selected ? 2.5f : 1.5f);
    if (m_Plain || vt.Scale < 0.3f)
        return;

    // the button: a minus to collapse, a plus to expand
    const Rect button = model.GetGroupButton(g);
    const D2D1_RECT_F b = screen(button);
    m_Brush->SetColor(ColorrefToD2D(g.TextColor));
    m_RenderTarget->DrawRectangle(b, m_Brush.Get(), 1.0f);
    const float cx = (b.left + b.right) * 0.5f, cy = (b.top + b.bottom) * 0.5f, arm = (b.right - b.left) * 0.3f;
    m_RenderTarget->DrawLine({ cx - arm, cy }, { cx + arm, cy }, m_Brush.Get(), 1.5f);
    if (g.Collapsed)
        m_RenderTarget->DrawLine({ cx, cy - arm }, { cx, cy + arm }, m_Brush.Get(), 1.5f);

    if (g.Collapsed) {
        std::wstring text = g.Title + L" (" + std::to_wstring(g.Nodes.size()) + L")";
        DrawGraphText(text, { box.Left + 8, box.Top, button.Left - 4, box.Bottom }, m_TextFormat.Get(), g.TextColor, vt);
    }
    else {
        const Rect header = model.GetGroupHeader(g);
        DrawGraphText(g.Title, { header.Left + 8, header.Top, button.Left - 4, header.Bottom }, m_TitleTextFormat.Get(), g.TextColor, vt);
    }
}

// Text in an area of the graph, at the zoom, clipped to it.
void NodeGraphRenderer::DrawGraphText(const std::wstring& text, const Rect& area, IDWriteTextFormat* format, COLORREF color, const ViewTransform& vt) {
    if (area.IsEmpty() || text.empty()) return;
    const D2D1_RECT_F rect{ area.Left, area.Top, area.Right, area.Bottom };
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(vt.OffsetX, vt.OffsetY));
    m_RenderTarget->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
    m_Brush->SetColor(ColorrefToD2D(color));
    m_RenderTarget->DrawTextW(text.c_str(), (UINT32)text.size(), format, rect, m_Brush.Get());
    m_RenderTarget->PopAxisAlignedClip();
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

// Zoomed far out: lines from center to center (through the bends); the nodes, drawn after, cover their ends.
void NodeGraphRenderer::DrawEdgePlain(const Edge& e, const Node& from, const Node& to, const ViewTransform& vt, EdgeEmphasis emphasis) {
    SetEdgeBrush(e, emphasis);
    const float width = EdgeWidth(e, emphasis);
    // from the centers, or the ports
    auto end = [](const Node& n, PortId id) {
        const Port* port = FindPort(n, id);
        return port ? PortPosition(n, *port) : Point{ n.X, n.Y };
    };
    if (e.Style.Elbow && &from != &to) {     // (its corners: it would look another edge as a straight line)
        const std::vector<Point> path = GetEdgeShape(e, from, to).Path;
        for (size_t i = 0; i + 1 < path.size(); i++)
            m_RenderTarget->DrawLine(vt.ToScreen(path[i].X, path[i].Y), vt.ToScreen(path[i + 1].X, path[i + 1].Y), m_Brush.Get(), width);
        return;
    }
    const Point start = end(from, e.FromPort), last = end(to, e.ToPort);
    D2D1_POINT_2F p = vt.ToScreen(start.X, start.Y);
    for (const auto& w : e.Waypoints) {
        const D2D1_POINT_2F q = vt.ToScreen(w.X, w.Y);
        m_RenderTarget->DrawLine(p, q, m_Brush.Get(), width);
        p = q;
    }
    m_RenderTarget->DrawLine(p, vt.ToScreen(last.X, last.Y), m_Brush.Get(), width);
}

// Zoomed far out: the shape in its fill (a diamond as its box), a border only to show the selection.
void NodeGraphRenderer::DrawNodePlain(const Node& n, const ViewTransform& vt, bool selected) {
    const auto c = vt.ToScreen(n.X, n.Y);
    const float hw = n.Width * vt.Scale * 0.5f, hh = n.Height * vt.Scale * 0.5f;
    const D2D1_RECT_F box{ c.x - hw, c.y - hh, c.x + hw, c.y + hh };
    const bool ellipse = n.Style.Shape == NodeShape::Ellipse;
    m_Brush->SetColor(ColorrefToD2D(n.Style.FillColor));
    if (ellipse) m_RenderTarget->FillEllipse({ c, hw, hh }, m_Brush.Get());
    else m_RenderTarget->FillRectangle(box, m_Brush.Get());
    if (selected) {
        m_Brush->SetColor(SelectionColor());
        if (ellipse) m_RenderTarget->DrawEllipse({ c, hw, hh }, m_Brush.Get(), 2.0f);
        else m_RenderTarget->DrawRectangle(box, m_Brush.Get(), 2.0f);
    }
}

void NodeGraphRenderer::DrawNode(const NodeGraphModel& model, const Node& n, const ViewTransform& vt, bool selected) {
    if (m_Plain) {
        DrawNodePlain(n, vt, selected);
        return;
    }
    auto center = vt.ToScreen(n.X, n.Y);
    float hw = n.Width  * vt.Scale * 0.5f;
    float hh = n.Height * vt.Scale * 0.5f;
    float r  = n.Style.CornerRadius * vt.Scale;

    D2D1_ROUNDED_RECT rr{
        { center.x - hw, center.y - hh, center.x + hw, center.y + hh },
        r, r
    };
    const NodeContent& c = n.Content;
    const NodeLayout layout = model.GetNodeLayout(n);
    auto toScreen = [&](const Rect& part) -> D2D1_RECT_F {
        return { rr.rect.left + part.Left * vt.Scale, rr.rect.top + part.Top * vt.Scale,
                 rr.rect.left + part.Right * vt.Scale, rr.rect.top + part.Bottom * vt.Scale };
    };

    const bool ownerDraw = n.Style.OwnerDraw && m_DrawHandler;
    const uint32_t flags = ownerDraw ? CallOwnerDraw(model, n, layout, rr, vt, selected, false) : NDF_DEFAULT;

    // a shape other than the rounded rectangle is a geometry, and what is inside the node (the title band, the image,
    // the icon, the text) is clipped to it
    const bool rectangle = n.Style.Shape == NodeShape::Rectangle;
    ComPtr<ID2D1Geometry> outline = rectangle ? nullptr : ShapeGeometry(n.Style.Shape, rr);
    if (!(flags & NDF_SKIPBACKGROUND)) {
        m_Brush->SetColor(ColorrefToD2D(n.Style.FillColor));
        if (outline) m_RenderTarget->FillGeometry(outline.Get(), m_Brush.Get());
        else m_RenderTarget->FillRoundedRectangle(rr, m_Brush.Get());
    }

    auto drawInside = [&] {    // the title band, the image (with the band of its label)
        if (!(flags & NDF_SKIPBACKGROUND) && !layout.Title.IsEmpty()) {
            m_Brush->SetColor(ColorrefToD2D(c.TitleFill != CLR_INVALID ? c.TitleFill : Darker(n.Style.FillColor)));
            if (rectangle) FillBand(toScreen(layout.Title), r, true, layout.Content.IsEmpty());
            else m_RenderTarget->FillRectangle(toScreen(layout.Title), m_Brush.Get());
        }
        if (!(flags & NDF_SKIPCONTENT)) {
            if (!layout.Image.IsEmpty())
                DrawImage(model, c.Image, toScreen(layout.Image), &layout.ImageSource);

            // a band for the label over the bottom of the image, so that it can be read whatever the image is
            if (c.LabelPos == LabelPlacement::Overlay && !layout.Image.IsEmpty() && !layout.Label.IsEmpty() && !n.Label.empty()) {
                m_Brush->SetColor(D2D1::ColorF(0, 0, 0, 0.5f));
                m_RenderTarget->FillRectangle(toScreen(layout.Label), m_Brush.Get());
            }
        }
    };
    auto drawBorder = [&] {
        if (flags & NDF_SKIPBORDER) return;
        float borderWidth = n.Style.BorderWidth * (selected ? 2.5f : 1.0f);
        m_Brush->SetColor(selected
            ? SelectionColor()
            : ColorrefToD2D(n.Style.BorderColor));
        if (outline) m_RenderTarget->DrawGeometry(outline.Get(), m_Brush.Get(), borderWidth);
        else m_RenderTarget->DrawRoundedRectangle(rr, m_Brush.Get(), borderWidth);
    };
    auto drawText = [&] {      // the icon, the title's text, the label
        if (flags & NDF_SKIPCONTENT) return;
        // an icon of a few pixels is only a smudge
        if (!layout.Icon.IsEmpty() && (std::min)(layout.Icon.Width(), layout.Icon.Height()) * vt.Scale >= k_MinIconPixels)
            DrawImage(model, c.Icon, toScreen(layout.Icon));
        if (!layout.Title.IsEmpty())
            DrawTitle(n, layout.Title, vt);
        DrawLabel(n, layout.Label, rr.rect, vt);
    };

    if (outline) {
        // all of the inside in the shape's clip (one layer), the border over it
        const bool inside = !layout.Title.IsEmpty() || !layout.Image.IsEmpty() || !layout.Icon.IsEmpty() || (!n.Label.empty() && !layout.Label.IsEmpty());
        if (inside) {
            if (!m_Layer)
                m_RenderTarget->CreateLayer(m_Layer.GetAddressOf());
            m_RenderTarget->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), outline.Get()), m_Layer.Get());
            drawInside();
            drawText();
            m_RenderTarget->PopLayer();
        }
        drawBorder();
    }
    else {
        drawInside();
        drawBorder();
        drawText();
    }

    if (ownerDraw && (flags & NDF_POSTPAINT))
        CallOwnerDraw(model, n, layout, rr, vt, selected, true);
    if (!n.Ports.empty())
        DrawPorts(n, vt);
}

// On the outline: a dot in its color with a ring of the background (larger when hovered); its name inside the node next
// to it, from half the size up.
void NodeGraphRenderer::DrawPorts(const Node& n, const ViewTransform& vt) {
    const float radius = std::max(k_PortRadius * vt.Scale, 3.0f);
    const bool names = vt.Scale >= 0.5f;
    for (const auto& p : n.Ports) {
        const Point at = PortPosition(n, p);
        const D2D1_POINT_2F c = vt.ToScreen(at.X, at.Y);
        const bool hovered = n.Id == m_HoveredPortNode && p.Id == m_HoveredPort;
        const float r = hovered ? radius * 1.5f : radius;
        m_Brush->SetColor(ColorrefToD2D(PortColor(p)));
        m_RenderTarget->FillEllipse({ c, r, r }, m_Brush.Get());
        m_Brush->SetColor(hovered ? SelectionColor() : ColorrefToD2D(m_Background));
        m_RenderTarget->DrawEllipse({ c, r, r }, m_Brush.Get(), hovered ? 2.0f : 1.5f);

        if (!names || p.Name.empty()) continue;
        // a box of the name's line, in graph units, beside the port toward the inside
        const float gap = k_PortRadius + 4, lineHeight = 14, width = n.Width * 0.5f - gap;
        Rect box;
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
        switch (p.Side) {
            case PortSide::Left:  box = { at.X + gap, at.Y - lineHeight / 2, at.X + gap + width, at.Y + lineHeight / 2 }; break;
            case PortSide::Right: box = { at.X - gap - width, at.Y - lineHeight / 2, at.X - gap, at.Y + lineHeight / 2 }; align = DWRITE_TEXT_ALIGNMENT_TRAILING; break;
            case PortSide::Top:   box = { at.X - 40, at.Y + gap - 2, at.X + 40, at.Y + gap - 2 + lineHeight }; align = DWRITE_TEXT_ALIGNMENT_CENTER; break;
            default:              box = { at.X - 40, at.Y - gap + 2 - lineHeight, at.X + 40, at.Y - gap + 2 }; align = DWRITE_TEXT_ALIGNMENT_CENTER; break;
        }
        m_PortTextFormat->SetTextAlignment(align);
        DrawGraphText(p.Name, box, m_PortTextFormat.Get(), n.Style.TextColor, vt);
    }
}

// The outline of a node in a rect (rounded for a rectangle), as a geometry.
ComPtr<ID2D1Geometry> NodeGraphRenderer::ShapeGeometry(NodeShape shape, const D2D1_ROUNDED_RECT& rr) const {
    const D2D1_RECT_F& b = rr.rect;
    const float cx = (b.left + b.right) * 0.5f, cy = (b.top + b.bottom) * 0.5f;
    ComPtr<ID2D1Geometry> geometry;
    switch (shape) {
        case NodeShape::Ellipse: {
            ComPtr<ID2D1EllipseGeometry> ellipse;
            if (SUCCEEDED(m_D2dFactory->CreateEllipseGeometry({ { cx, cy }, (b.right - b.left) * 0.5f, (b.bottom - b.top) * 0.5f }, ellipse.GetAddressOf())))
                geometry = ellipse;
            break;
        }
        case NodeShape::Diamond: {
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_D2dFactory->CreatePathGeometry(path.GetAddressOf())) && SUCCEEDED(path->Open(sink.GetAddressOf()))) {
                sink->BeginFigure({ cx, b.top }, D2D1_FIGURE_BEGIN_FILLED);
                const D2D1_POINT_2F points[] = { { b.right, cy }, { cx, b.bottom }, { b.left, cy } };
                sink->AddLines(points, 3);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close()))
                    geometry = path;
            }
            break;
        }
        default: {
            ComPtr<ID2D1RoundedRectangleGeometry> rounded;
            if (SUCCEEDED(m_D2dFactory->CreateRoundedRectangleGeometry(rr, rounded.GetAddressOf())))
                geometry = rounded;
            break;
        }
    }
    return geometry;
}

// Calls the handler with the transform set to the node's units and the drawing clipped to it. Returns its flags (none
// for PostPaint).
uint32_t NodeGraphRenderer::CallOwnerDraw(const NodeGraphModel& model, const Node& n, const NodeLayout& layout,
                                          const D2D1_ROUNDED_RECT& rr, const ViewTransform& vt, bool selected, bool post) {
    D2D1_MATRIX_3X2_F transform;
    m_RenderTarget->GetTransform(&transform);
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(rr.rect.left, rr.rect.top));
    m_RenderTarget->PushAxisAlignedClip({ 0, 0, n.Width, n.Height }, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // an ellipse is a rounded rectangle with the radii of its half sizes
    const D2D1_ROUNDED_RECT shape = n.Style.Shape == NodeShape::Ellipse
        ? D2D1_ROUNDED_RECT{ { 0, 0, n.Width, n.Height }, n.Width * 0.5f, n.Height * 0.5f }
        : D2D1_ROUNDED_RECT{ { 0, 0, n.Width, n.Height }, n.Style.CornerRadius, n.Style.CornerRadius };
    ComPtr<ID2D1Geometry> outline = ShapeGeometry(n.Style.Shape, shape);
    const NodeDrawContext context{
        m_RenderTarget.Get(), m_D2dFactory.Get(), m_DwFactory.Get(), *this, model, n, layout,
        shape, outline.Get(),
        rr.rect, vt.Scale, selected, !m_Exporting && n.Id == m_HoveredNode, m_Exporting, m_DeviceGeneration
    };
    uint32_t flags = NDF_DEFAULT;
    if (post)
        m_DrawHandler->PostPaint(context);
    else
        flags = m_DrawHandler->PrePaint(context);

    m_RenderTarget->PopAxisAlignedClip();
    m_RenderTarget->SetTransform(transform);
    return flags;
}

// node: the node's rect on the target.
void NodeGraphRenderer::DrawLabel(const Node& n, const Rect& area, const D2D1_RECT_F& node, const ViewTransform& vt) {
    if (area.IsEmpty() || n.Label.empty())
        return;
    if (n.Style.Code) {
        DrawCodeText(n, area, vt);
    }
    else if (n.Style.ScaleLabel) {
        // drawn at its size in graph space, like the title; too small to read: not drawn
        if (vt.Scale < 0.3f)
            return;
        m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(node.left, node.top));
        m_Brush->SetColor(ColorrefToD2D(n.Style.TextColor));
        m_RenderTarget->DrawTextW(
            n.Label.c_str(), (UINT32)n.Label.size(),
            m_TextFormat.Get(),
            { area.Left, area.Top, area.Right, area.Bottom },
            m_Brush.Get());
        m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    else {
        m_Brush->SetColor(ColorrefToD2D(n.Style.TextColor));
        m_RenderTarget->DrawTextW(
            n.Label.c_str(), (UINT32)n.Label.size(),
            m_TextFormat.Get(),
            { node.left + area.Left * vt.Scale, node.top + area.Top * vt.Scale,
              node.left + area.Right * vt.Scale, node.top + area.Bottom * vt.Scale },
            m_Brush.Get());
    }
}

// A rectangle with its top and/or bottom corners rounded (a band across the top or the bottom of a node), in the brush's color.
void NodeGraphRenderer::FillBand(const D2D1_RECT_F& rect, float radius, bool roundTop, bool roundBottom) {
    radius = (std::min)({ radius, (rect.right - rect.left) * 0.5f, (rect.bottom - rect.top) * 0.5f });
    if (radius <= 0.5f || (!roundTop && !roundBottom)) {
        m_RenderTarget->FillRectangle(rect, m_Brush.Get());
        return;
    }
    if (roundTop && roundBottom) {
        m_RenderTarget->FillRoundedRectangle({ rect, radius, radius }, m_Brush.Get());
        return;
    }

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(m_D2dFactory->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf())))
        return;
    const float top = roundTop ? radius : 0, bottom = roundBottom ? radius : 0;
    auto arc = [&](float x, float y) {
        sink->AddArc(D2D1::ArcSegment({ x, y }, { radius, radius }, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure({ rect.left, rect.top + top }, D2D1_FIGURE_BEGIN_FILLED);
    if (top > 0) arc(rect.left + top, rect.top);
    sink->AddLine({ rect.right - top, rect.top });
    if (top > 0) arc(rect.right, rect.top + top);
    sink->AddLine({ rect.right, rect.bottom - bottom });
    if (bottom > 0) arc(rect.right - bottom, rect.bottom);
    sink->AddLine({ rect.left + bottom, rect.bottom });
    if (bottom > 0) arc(rect.left, rect.bottom - bottom);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sink->Close()))
        m_RenderTarget->FillGeometry(path.Get(), m_Brush.Get());
}

// src: the part of the image, in its pixels (the bitmap may be smaller than the image, if it was too large for the device).
void NodeGraphRenderer::DrawImage(const NodeGraphModel& model, ImageId id, const D2D1_RECT_F& dst, const Rect* src) {
    ID2D1Bitmap* bitmap = GetBitmap(model, id);
    if (!bitmap)
        return;
    D2D1_RECT_F source;
    D2D1_RECT_F* sourcePtr = nullptr;
    if (src) {
        const Image* image = model.GetImage(id);
        auto size = bitmap->GetSize();
        float sx = size.width / image->Width, sy = size.height / image->Height;
        source = { src->Left * sx, src->Top * sy, src->Right * sx, src->Bottom * sy };
        sourcePtr = &source;
    }
    m_RenderTarget->DrawBitmap(bitmap, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourcePtr);
}

// The text is drawn at its size in graph space and scaled with the zoom, so that it stays inside the band. When it would
// be too small to read it is not drawn.
void NodeGraphRenderer::DrawTitle(const Node& n, const Rect& area, const ViewTransform& vt) {
    if (vt.Scale < 0.3f)
        return;

    auto center = vt.ToScreen(n.X, n.Y);
    float left = center.x - n.Width * vt.Scale * 0.5f;
    float top = center.y - n.Height * vt.Scale * 0.5f;
    const float pad = 8.0f;
    D2D1_RECT_F text = { area.Left + pad, area.Top, area.Right - pad, area.Bottom };
    if (text.right <= text.left)
        return;

    const auto& title = n.Content.Title;
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(left, top));
    m_RenderTarget->PushAxisAlignedClip(text, D2D1_ANTIALIAS_MODE_ALIASED);
    m_Brush->SetColor(ColorrefToD2D(n.Style.TextColor));
    m_RenderTarget->DrawTextW(title.c_str(), (UINT32)title.size(), m_TitleTextFormat.Get(), text, m_Brush.Get());
    m_RenderTarget->PopAxisAlignedClip();
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

// The lines of the label in a monospaced font. The text is drawn at its size in graph space and scaled with the zoom, so
// that it stays inside the node. When it would be too small to read it is not drawn.
void NodeGraphRenderer::DrawCodeText(const Node& n, const Rect& area, const ViewTransform& vt) {
    if (n.Label.empty() || vt.Scale < 0.3f)
        return;

    auto center = vt.ToScreen(n.X, n.Y);
    float left = center.x - n.Width * vt.Scale * 0.5f;
    float top = center.y - n.Height * vt.Scale * 0.5f;
    const float pad = 8.0f;
    D2D1_RECT_F text = { area.Left + pad, area.Top + pad * 0.75f, area.Right - pad, area.Bottom - pad * 0.75f };
    if (text.right <= text.left || text.bottom <= text.top)
        return;

    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(left, top));
    m_RenderTarget->PushAxisAlignedClip(text, D2D1_ANTIALIAS_MODE_ALIASED);
    m_Brush->SetColor(ColorrefToD2D(n.Style.TextColor));
    m_RenderTarget->DrawTextW(n.Label.c_str(), (UINT32)n.Label.size(), m_CodeTextFormat.Get(), text, m_Brush.Get());
    m_RenderTarget->PopAxisAlignedClip();
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

void NodeGraphRenderer::DrawRubberBand(const RubberBand& rb) {
    float x0 = std::min(rb.X0, rb.X1);
    float y0 = std::min(rb.Y0, rb.Y1);
    float x1 = std::max(rb.X0, rb.X1);
    float y1 = std::max(rb.Y0, rb.Y1);
    D2D1_RECT_F rect = { x0, y0, x1, y1 };
    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 0.0f, 0.25f));
    m_RenderTarget->FillRectangle(rect, m_Brush.Get());
    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 0.0f, 1.0f));
    m_RenderTarget->DrawRectangle(rect, m_Brush.Get(), 1.5f, m_DashStyle.Get());
}

NodeId NodeGraphRenderer::HitTestNode(const NodeGraphModel& model, float gx, float gy) const {
    for (auto it = model.Nodes().rbegin(); it != model.Nodes().rend(); ++it)
        if (NodeContains(*it, gx, gy) && !model.IsHidden(it->Id))
            return it->Id;
    return InvalidNode;
}

bool NodeGraphRenderer::HitTestPort(const NodeGraphModel& model, const ViewTransform& vt, float sx, float sy, NodeId& node, PortId& port) const {
    node = InvalidNode;
    port = InvalidPort;
    if (vt.Scale < m_DetailThreshold)
        return false;
    // as drawn (DrawPorts, in DIPs), with a few pixels more to aim; vt and the point are in pixels
    const float radius = std::max(k_PortRadius * vt.Scale, 3.0f * m_DpiScale) + 3.0f * m_DpiScale;
    for (auto it = model.Nodes().rbegin(); it != model.Nodes().rend(); ++it) {
        if (it->Ports.empty() || model.IsHidden(it->Id)) continue;
        for (const auto& p : it->Ports) {
            const Point at = PortPosition(*it, p);
            const D2D1_POINT_2F c = vt.ToScreen(at.X, at.Y);
            if ((c.x - sx) * (c.x - sx) + (c.y - sy) * (c.y - sy) <= radius * radius) {
                node = it->Id;
                port = p.Id;
                return true;
            }
        }
    }
    return false;
}

GroupId NodeGraphRenderer::HitTestGroup(const NodeGraphModel& model, float gx, float gy, bool* onButton) const {
    auto in = [&](const Rect& r) { return !r.IsEmpty() && gx >= r.Left && gx <= r.Right && gy >= r.Top && gy <= r.Bottom; };
    // the last drawn on top: collapsed boxes (drawn with the nodes) before the headers of expanded groups
    for (int collapsed = 1; collapsed >= 0; collapsed--)
        for (auto it = model.Groups().rbegin(); it != model.Groups().rend(); ++it)
            if (it->Collapsed == (collapsed == 1) && in(model.GetGroupHeader(*it))) {
                if (onButton) *onButton = in(model.GetGroupButton(*it));
                return it->Id;
            }
    if (onButton) *onButton = false;
    return InvalidGroup;
}

void NodeGraphRenderer::DrawMinimap(const MinimapConfig& cfg, const RECT& clientRect,
                                const NodeGraphModel& model, const ViewTransform& vt) {
    if (!cfg.Visible) return;

    D2D1_RECT_F rect = { cfg.X, cfg.Y, cfg.X + cfg.Width, cfg.Y + cfg.Height };

    // a panel a little darker (lighter) than a light (dark) canvas
    const bool light = IsLightBackground();
    m_Brush->SetColor(light ? D2D1::ColorF(0.9f, 0.9f, 0.9f, 0.9f) : D2D1::ColorF(0.05f, 0.05f, 0.05f, 0.85f));
    m_RenderTarget->FillRectangle(rect, m_Brush.Get());

    m_Brush->SetColor(light ? D2D1::ColorF(0.6f, 0.6f, 0.6f) : D2D1::ColorF(0.55f, 0.55f, 0.55f));
    m_RenderTarget->DrawRectangle(rect, m_Brush.Get(), 1.0f);

    if (model.Nodes().empty()) return;

    // Compute graph content bounds
    float gMinX = FLT_MAX, gMinY = FLT_MAX, gMaxX = -FLT_MAX, gMaxY = -FLT_MAX;
    for (const auto& n : model.Nodes()) {
        gMinX = std::min(gMinX, n.X - n.Width  * 0.5f);
        gMinY = std::min(gMinY, n.Y - n.Height * 0.5f);
        gMaxX = std::max(gMaxX, n.X + n.Width  * 0.5f);
        gMaxY = std::max(gMaxY, n.Y + n.Height * 0.5f);
    }
    float gW = std::max(gMaxX - gMinX, 1.0f);
    float gH = std::max(gMaxY - gMinY, 1.0f);

    const float pad = 8.0f;
    float mmW = cfg.Width  - pad * 2.0f;
    float mmH = cfg.Height - pad * 2.0f;
    float mmScale = std::min(mmW / gW, mmH / gH);

    float drawW  = gW * mmScale;
    float drawH  = gH * mmScale;
    float originX = cfg.X + pad + (mmW - drawW) * 0.5f;
    float originY = cfg.Y + pad + (mmH - drawH) * 0.5f;

    m_RenderTarget->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);

    // Edges
    m_Brush->SetColor(light ? D2D1::ColorF(0.45f, 0.45f, 0.45f, 0.8f) : D2D1::ColorF(0.6f, 0.6f, 0.6f, 0.8f));
    for (const auto& e : model.Edges()) {
        const Node* from = model.GetNode(e.From);
        const Node* to   = model.GetNode(e.To);
        if (!from || !to) continue;
        D2D1_POINT_2F p0 = { originX + (from->X - gMinX) * mmScale,
                              originY + (from->Y - gMinY) * mmScale };
        D2D1_POINT_2F p1 = { originX + (to->X   - gMinX) * mmScale,
                              originY + (to->Y   - gMinY) * mmScale };
        m_RenderTarget->DrawLine(p0, p1, m_Brush.Get(), 0.75f);
    }

    // Nodes as small filled rectangles (not those in a collapsed group)
    for (const auto& n : model.Nodes()) {
        if (model.IsHidden(n.Id)) continue;
        float hw = std::max(n.Width  * mmScale * 0.5f, 2.0f);
        float hh = std::max(n.Height * mmScale * 0.5f, 2.0f);
        float cx = originX + (n.X - gMinX) * mmScale;
        float cy = originY + (n.Y - gMinY) * mmScale;
        D2D1_RECT_F nr = { cx - hw, cy - hh, cx + hw, cy + hh };
        m_Brush->SetColor(ColorrefToD2D(n.Style.FillColor));
        m_RenderTarget->FillRectangle(nr, m_Brush.Get());
    }

    // Viewport indicator — the currently visible area in graph space
    float vpGX0 = -vt.OffsetX / vt.Scale;
    float vpGY0 = -vt.OffsetY / vt.Scale;
    float vpGX1 = ((float)clientRect.right  - vt.OffsetX) / vt.Scale;
    float vpGY1 = ((float)clientRect.bottom - vt.OffsetY) / vt.Scale;

    D2D1_RECT_F vpRect = {
        originX + (vpGX0 - gMinX) * mmScale,
        originY + (vpGY0 - gMinY) * mmScale,
        originX + (vpGX1 - gMinX) * mmScale,
        originY + (vpGY1 - gMinY) * mmScale
    };
    m_Brush->SetColor(light ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));
    m_RenderTarget->FillRectangle(vpRect, m_Brush.Get());
    m_Brush->SetColor(light ? SelectionColor() : D2D1::ColorF(1.0f, 1.0f, 0.5f, 0.9f));
    m_RenderTarget->DrawRectangle(vpRect, m_Brush.Get(), 1.0f, m_DashStyle.Get());

    m_RenderTarget->PopAxisAlignedClip();
}

static void BuildHandlePositions(const Node& n, const ViewTransform& vt,
                                  D2D1_POINT_2F pts[8]) {
    auto c = vt.ToScreen(n.X, n.Y);
    float hw = n.Width  * vt.Scale * 0.5f;
    float hh = n.Height * vt.Scale * 0.5f;
    float l = c.x - hw, r = c.x + hw;
    float t = c.y - hh, b = c.y + hh;
    float mx = c.x, my = c.y;
    // Order matches ResizeHandle enum: NW N NE E SE S SW W
    pts[0] = { l,  t  };  // NW
    pts[1] = { mx, t  };  // N
    pts[2] = { r,  t  };  // NE
    pts[3] = { r,  my };  // E
    pts[4] = { r,  b  };  // SE
    pts[5] = { mx, b  };  // S
    pts[6] = { l,  b  };  // SW
    pts[7] = { l,  my };  // W
}

void NodeGraphRenderer::DrawResizeHandles(const Node& n, const ViewTransform& vt,
                                      ResizeHandle hovered) {
    D2D1_POINT_2F pts[8];
    BuildHandlePositions(n, vt, pts);
    const float hs = 4.5f;
    for (int i = 0; i < 8; ++i) {
        auto h = static_cast<ResizeHandle>(i + 1);
        D2D1_RECT_F r = { pts[i].x - hs, pts[i].y - hs,
                          pts[i].x + hs, pts[i].y + hs };
        m_Brush->SetColor(h == hovered
            ? SelectionColor()
            : D2D1::ColorF(1.0f, 1.0f, 1.0f));
        m_RenderTarget->FillRectangle(r, m_Brush.Get());
        m_Brush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
        m_RenderTarget->DrawRectangle(r, m_Brush.Get(), 1.0f);
    }
}

ResizeHandle NodeGraphRenderer::HitTestResizeHandle(const Node& n, const ViewTransform& vt,
                                                float sx, float sy) const {
    D2D1_POINT_2F pts[8];
    BuildHandlePositions(n, vt, pts);
    const float hs = 6.0f * m_DpiScale;     // (as drawn, in DIPs; vt and the point are in pixels)
    for (int i = 0; i < 8; ++i) {
        if (sx >= pts[i].x - hs && sx <= pts[i].x + hs &&
            sy >= pts[i].y - hs && sy <= pts[i].y + hs)
            return static_cast<ResizeHandle>(i + 1);
    }
    return ResizeHandle::None;
}

EdgeId NodeGraphRenderer::HitTestEdge(const NodeGraphModel& model, float gx, float gy, float tolerance) const {
    const ElbowShiftMap& shifts = model.ElbowShifts();
    // an end in a collapsed group is its box (made once for each group)
    std::unordered_map<GroupId, Node> collapsed;
    auto end = [&](NodeId id) -> const Node* {
        if (!model.IsHidden(id)) return model.GetNode(id);
        const GroupId g = model.GroupOf(id);
        auto it = collapsed.find(g);
        if (it == collapsed.end()) it = collapsed.emplace(g, model.CollapsedNode(*model.GetGroup(g))).first;
        return &it->second;
    };
    for (const auto& e : model.Edges()) {
        const Node* from = end(e.From);
        const Node* to   = end(e.To);
        if (!from || !to) continue;
        if (from == to && model.IsHidden(e.From)) continue;   // inside a collapsed group

        // every piece of the path from one node to the other (or around the node, back to it); a curve as short lines
        const bool loop = IsSelfLoop(e, *from, *to);
        const std::vector<Point> path = FlattenEdgeShape(loop ? EdgeShape{ SelfLoopPath(*from) } : GetEdgeShape(e, *from, *to, 0.0f, ShiftsOf(shifts, e.Id)));
        if (path.size() < 2) continue;

        for (size_t i = 0; i + 1 < path.size(); i++) {
            float dx = path[i + 1].X - path[i].X, dy = path[i + 1].Y - path[i].Y;
            float len2 = dx * dx + dy * dy;
            if (len2 < 1e-6f) continue;     // (a rounded corner's are short)

            float t = ((gx - path[i].X) * dx + (gy - path[i].Y) * dy) / len2;
            t = std::clamp(t, 0.0f, 1.0f);
            float px = path[i].X + t * dx - gx;
            float py = path[i].Y + t * dy - gy;
            if (px * px + py * py <= tolerance * tolerance)
                return e.Id;
        }
    }
    return InvalidEdge;
}

} // namespace NodeGraphCtrl
