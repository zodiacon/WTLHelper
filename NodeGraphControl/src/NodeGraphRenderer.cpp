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

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    hr = m_D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, m_DashStyle.GetAddressOf());
    return hr;
}

void NodeGraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_DashStyle.Reset();
    m_EdgeTextFormat.Reset();
    m_CodeTextFormat.Reset();
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
    for (const auto& image : model.Images())
        keys.insert(image.Key);
    std::erase_if(m_Bitmaps, [&](const auto& entry) { return !keys.contains(entry.first); });
}

void NodeGraphRenderer::Render(HDC hdc, const RECT& clientRect,
                           const NodeGraphModel& model, const ViewTransform& vt,
                           const std::vector<NodeId>& selectedNodes, EdgeId selectedEdge,
                           bool drawGrid, const EdgePreview& preview,
                           const RubberBand& rubberBand, const MinimapConfig& minimap,
                           const ResizeOverlay& resizeOverlay) {
    if (FAILED(EnsureDeviceResources(hdc, clientRect))) return;

    HRESULT hr = m_RenderTarget->BindDC(hdc, &clientRect);
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        if (FAILED(EnsureDeviceResources(hdc, clientRect))) return;
        m_RenderTarget->BindDC(hdc, &clientRect);
    }

    PruneBitmaps(model);

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(ColorrefToD2D(m_Background));

    if (drawGrid) DrawGrid(clientRect, vt);

    m_EdgePairs.clear();
    for (const auto& e : model.Edges())
        m_EdgePairs.insert((uint64_t)e.From << 32 | e.To);
    for (const auto& e : model.Edges())
        DrawEdge(e, model, vt, e.Id == selectedEdge);

    if (preview.Active)
        DrawEdgePreview(preview, model, vt);

    for (const auto& n : model.Nodes()) {
        bool sel = std::find(selectedNodes.begin(), selectedNodes.end(), n.Id) != selectedNodes.end();
        DrawNode(model, n, vt, sel);
    }

    if (rubberBand.Active)
        DrawRubberBand(rubberBand);

    if (resizeOverlay.Node != InvalidNode) {
        const Node* rn = model.GetNode(resizeOverlay.Node);
        if (rn) DrawResizeHandles(*rn, vt, resizeOverlay.HoveredHandle);
    }

    DrawMinimap(minimap, clientRect, model, vt);

    hr = m_RenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
}

void NodeGraphRenderer::DrawEdgePreview(const EdgePreview& preview, const NodeGraphModel& model,
                                    const ViewTransform& vt) {
    const Node* from = model.GetNode(preview.FromNode);
    if (!from) return;

    auto p0 = vt.ToScreen(from->X, from->Y);
    D2D1_POINT_2F p1 = { preview.ScreenX, preview.ScreenY };

    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    float len = std::sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    float nx = dx / len, ny = dy / len;

    // Clip start to source node border
    float hw = from->Width  * vt.Scale * 0.5f;
    float hh = from->Height * vt.Scale * 0.5f;
    float t0 = std::min(std::fabsf(nx) > 0.0f ? hw / std::fabsf(nx) : 1e9f,
                        std::fabsf(ny) > 0.0f ? hh / std::fabsf(ny) : 1e9f);
    t0 = std::min(t0, len);

    D2D1_POINT_2F start = { p0.x + nx * t0, p0.y + ny * t0 };

    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 0.3f, 0.75f));
    m_RenderTarget->DrawLine(start, p1, m_Brush.Get(), 2.0f, m_DashStyle.Get());
    DrawArrowhead(p1, { nx, ny });
}

void NodeGraphRenderer::DrawGrid(const RECT& rc, const ViewTransform& vt) {
    const float gridSpacing = 40.0f * vt.Scale;
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
                             const ViewTransform& vt, bool selected) {
    const Node* from = model.GetNode(e.From);
    const Node* to   = model.GetNode(e.To);
    if (!from || !to) return;

    if (from == to) {
        DrawSelfLoop(e, *from, vt, selected);
        return;
    }

    // The path: from the center of one node through the waypoints to the center of the other.
    std::vector<D2D1_POINT_2F> pts;
    pts.push_back(vt.ToScreen(from->X, from->Y));
    for (const auto& w : e.Waypoints)
        pts.push_back(vt.ToScreen(w.X, w.Y));
    pts.push_back(vt.ToScreen(to->X, to->Y));

    float dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y;
    float len = std::sqrtf(dx * dx + dy * dy);
    if (pts.size() == 2 && len < 1.0f) return;

    // An edge that has one going the other way is moved to its right, so that the two can be told apart.
    if (pts.size() == 2 && m_EdgePairs.contains((uint64_t)e.To << 32 | e.From)) {
        const float lane = 7.0f;
        float nx = dx / len, ny = dy / len;
        for (auto& p : pts) { p.x -= ny * lane; p.y += nx * lane; }
    }

    // Clip the ends to the borders of the nodes. A straight edge is clipped at both ends of the same segment, so neither
    // end may go past its middle (the nodes may overlap); next to a waypoint the end may take the whole segment, or
    // a waypoint close to a node would leave the end, and its arrowhead, inside (under) the node.
    const float maxPart = pts.size() == 2 ? 0.5f : 1.0f;
    auto clip = [&](const Node& n, D2D1_POINT_2F& at, D2D1_POINT_2F other) -> D2D1_POINT_2F {
        float ddx = other.x - at.x, ddy = other.y - at.y;
        float l = std::sqrtf(ddx * ddx + ddy * ddy);
        if (l < 1.0f) return { 0.0f, 0.0f };
        float nx = ddx / l, ny = ddy / l;
        float hw = n.Width * vt.Scale * 0.5f, hh = n.Height * vt.Scale * 0.5f;
        float t = std::min(std::fabsf(nx) > 0.0f ? hw / std::fabsf(nx) : 1e9f,
                           std::fabsf(ny) > 0.0f ? hh / std::fabsf(ny) : 1e9f);
        t = std::min(t, l * maxPart);
        at.x += nx * t; at.y += ny * t;
        return { nx, ny };
    };
    // both ends against the points before clipping (of a straight edge, the from end is the other point of the to end)
    const auto beforeLast = pts[pts.size() - 2];
    clip(*from, pts.front(), pts[1]);
    auto dir = clip(*to, pts.back(), beforeLast);
    dir = { -dir.x, -dir.y };	// from the last bend towards the node

    float strokeWidth = e.Style.Width * (selected ? 2.0f : 1.0f);
    m_Brush->SetColor(selected
        ? SelectionColor()
        : ColorrefToD2D(e.Style.Color));
    for (size_t i = 0; i + 1 < pts.size(); i++)
        m_RenderTarget->DrawLine(pts[i], pts[i + 1], m_Brush.Get(), strokeWidth);

    if (e.Style.Directed)
        DrawArrowhead(pts.back(), dir);

    D2D1_POINT_2F start = pts[(pts.size() - 1) / 2], end = pts[pts.size() / 2];
    if (!e.Label.empty()) {
        D2D1_POINT_2F mid = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f };
        const float lw = 110.0f, lh = 18.0f;
        D2D1_RECT_F lr = { mid.x - lw * 0.5f, mid.y - lh * 0.5f,
                           mid.x + lw * 0.5f, mid.y + lh * 0.5f };
        m_Brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.88f));
        m_RenderTarget->FillRectangle(lr, m_Brush.Get());
        m_Brush->SetColor(selected ? SelectionColor()
                                   : D2D1::ColorF(0.85f, 0.85f, 0.85f));
        m_RenderTarget->DrawTextW(e.Label.c_str(), (UINT32)e.Label.size(),
            m_EdgeTextFormat.Get(), lr, m_Brush.Get());
    }
}

// A jump to its own block: a loop on the right side of the node.
void NodeGraphRenderer::DrawSelfLoop(const Edge& e, const Node& n, const ViewTransform& vt, bool selected) {
    auto c = vt.ToScreen(n.X, n.Y);
    float hw = n.Width * vt.Scale * 0.5f;
    float hh = n.Height * vt.Scale * 0.5f;
    float reach = std::max(24.0f * vt.Scale, 10.0f);
    float top = c.y - std::min(hh * 0.5f, 40.0f * vt.Scale);
    float bottom = c.y + std::min(hh * 0.5f, 40.0f * vt.Scale);
    float right = c.x + hw;

    m_Brush->SetColor(selected ? SelectionColor() : ColorrefToD2D(e.Style.Color));
    float width = e.Style.Width * (selected ? 2.0f : 1.0f);
    m_RenderTarget->DrawLine({ right, top }, { right + reach, top }, m_Brush.Get(), width);
    m_RenderTarget->DrawLine({ right + reach, top }, { right + reach, bottom }, m_Brush.Get(), width);
    m_RenderTarget->DrawLine({ right + reach, bottom }, { right + 8.0f, bottom }, m_Brush.Get(), width);
    if (e.Style.Directed)
        DrawArrowhead({ right, bottom }, { -1.0f, 0.0f });
}

void NodeGraphRenderer::DrawArrowhead(D2D1_POINT_2F tip, D2D1_POINT_2F dir) {
    const float size = 10.0f;
    const float angle = 0.4f; // radians half-angle
    float cosA = std::cosf(angle), sinA = std::sinf(angle);

    float bx = -dir.x * size, by = -dir.y * size;
    // Rotate back-vector by +angle and -angle
    D2D1_POINT_2F l = { bx * cosA - by * sinA,  bx * sinA + by * cosA };
    D2D1_POINT_2F r = { bx * cosA + by * sinA, -bx * sinA + by * cosA };

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    m_D2dFactory->CreatePathGeometry(path.GetAddressOf());
    path->Open(sink.GetAddressOf());
    sink->BeginFigure(tip, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({ tip.x + l.x, tip.y + l.y });
    sink->AddLine({ tip.x + r.x, tip.y + r.y });
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    m_RenderTarget->FillGeometry(path.Get(), m_Brush.Get());
}

constexpr float k_MinIconPixels = 6.0f;    // a smaller icon (on the target) isn't drawn

static COLORREF Darker(COLORREF cr, float factor = 0.72f) {
    return RGB(GetRValue(cr) * factor, GetGValue(cr) * factor, GetBValue(cr) * factor);
}

void NodeGraphRenderer::DrawNode(const NodeGraphModel& model, const Node& n, const ViewTransform& vt, bool selected) {
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

    if (!(flags & NDF_SKIPBACKGROUND)) {
        m_Brush->SetColor(ColorrefToD2D(n.Style.FillColor));
        m_RenderTarget->FillRoundedRectangle(rr, m_Brush.Get());

        if (!layout.Title.IsEmpty()) {
            m_Brush->SetColor(ColorrefToD2D(c.TitleFill != CLR_INVALID ? c.TitleFill : Darker(n.Style.FillColor)));
            FillBand(toScreen(layout.Title), r, true, layout.Content.IsEmpty());
        }
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

    if (!(flags & NDF_SKIPBORDER)) {
        float borderWidth = n.Style.BorderWidth * (selected ? 2.5f : 1.0f);
        m_Brush->SetColor(selected
            ? SelectionColor()
            : ColorrefToD2D(n.Style.BorderColor));
        m_RenderTarget->DrawRoundedRectangle(rr, m_Brush.Get(), borderWidth);
    }

    if (!(flags & NDF_SKIPCONTENT)) {
        // an icon of a few pixels is only a smudge
        if (!layout.Icon.IsEmpty() && (std::min)(layout.Icon.Width(), layout.Icon.Height()) * vt.Scale >= k_MinIconPixels)
            DrawImage(model, c.Icon, toScreen(layout.Icon));
        if (!layout.Title.IsEmpty())
            DrawTitle(n, layout.Title, vt);
        DrawLabel(n, layout.Label, rr.rect, vt);
    }

    if (ownerDraw && (flags & NDF_POSTPAINT))
        CallOwnerDraw(model, n, layout, rr, vt, selected, true);
}

// Calls the handler with the transform set to the node's units and the drawing clipped to it. Returns its flags (none
// for PostPaint).
uint32_t NodeGraphRenderer::CallOwnerDraw(const NodeGraphModel& model, const Node& n, const NodeLayout& layout,
                                          const D2D1_ROUNDED_RECT& rr, const ViewTransform& vt, bool selected, bool post) {
    D2D1_MATRIX_3X2_F transform;
    m_RenderTarget->GetTransform(&transform);
    m_RenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(vt.Scale, vt.Scale) * D2D1::Matrix3x2F::Translation(rr.rect.left, rr.rect.top));
    m_RenderTarget->PushAxisAlignedClip({ 0, 0, n.Width, n.Height }, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const NodeDrawContext context{
        m_RenderTarget.Get(), m_D2dFactory.Get(), m_DwFactory.Get(), *this, model, n, layout,
        { { 0, 0, n.Width, n.Height }, n.Style.CornerRadius, n.Style.CornerRadius },
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
    for (auto it = model.Nodes().rbegin(); it != model.Nodes().rend(); ++it) {
        const auto& n = *it;
        if (gx >= n.X - n.Width  * 0.5f && gx <= n.X + n.Width  * 0.5f &&
            gy >= n.Y - n.Height * 0.5f && gy <= n.Y + n.Height * 0.5f)
            return n.Id;
    }
    return InvalidNode;
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

    // Nodes as small filled rectangles
    for (const auto& n : model.Nodes()) {
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
    const float hs = 6.0f;
    for (int i = 0; i < 8; ++i) {
        if (sx >= pts[i].x - hs && sx <= pts[i].x + hs &&
            sy >= pts[i].y - hs && sy <= pts[i].y + hs)
            return static_cast<ResizeHandle>(i + 1);
    }
    return ResizeHandle::None;
}

EdgeId NodeGraphRenderer::HitTestEdge(const NodeGraphModel& model, float gx, float gy, float tolerance) const {
    for (const auto& e : model.Edges()) {
        const Node* from = model.GetNode(e.From);
        const Node* to   = model.GetNode(e.To);
        if (!from || !to) continue;

        // every piece of the path from one node to the other
        std::vector<Point> path;
        path.push_back({ from->X, from->Y });
        path.insert(path.end(), e.Waypoints.begin(), e.Waypoints.end());
        path.push_back({ to->X, to->Y });

        for (size_t i = 0; i + 1 < path.size(); i++) {
            float dx = path[i + 1].X - path[i].X, dy = path[i + 1].Y - path[i].Y;
            float len2 = dx * dx + dy * dy;
            if (len2 < 1.0f) continue;

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
