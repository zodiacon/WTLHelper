#include "../include/GraphRenderer.h"
#include <cmath>
#include <algorithm>
#include <cfloat>

namespace GraphCtrl {

static D2D1_COLOR_F ColorrefToD2D(COLORREF cr) {
    return D2D1::ColorF(
        GetRValue(cr) / 255.0f,
        GetGValue(cr) / 255.0f,
        GetBValue(cr) / 255.0f);
}

GraphRenderer::~GraphRenderer() {
    Shutdown();
}

HRESULT GraphRenderer::Init(HINSTANCE /*hInstance*/) {
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

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties();
    ssp.dashStyle = D2D1_DASH_STYLE_DASH;
    hr = m_D2dFactory->CreateStrokeStyle(ssp, nullptr, 0, m_DashStyle.GetAddressOf());
    return hr;
}

void GraphRenderer::Shutdown() {
    DiscardDeviceResources();
    m_DashStyle.Reset();
    m_EdgeTextFormat.Reset();
    m_TextFormat.Reset();
    m_DwFactory.Reset();
    m_D2dFactory.Reset();
}

HRESULT GraphRenderer::EnsureDeviceResources(HDC /*hdc*/, const RECT& /*rc*/) {
    if (m_RenderTarget) return S_OK;

    auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    HRESULT hr = m_D2dFactory->CreateDCRenderTarget(&props, m_RenderTarget.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = m_RenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), m_Brush.GetAddressOf());
    return hr;
}

void GraphRenderer::DiscardDeviceResources() {
    m_Brush.Reset();
    m_RenderTarget.Reset();
}

void GraphRenderer::Render(HDC hdc, const RECT& clientRect,
                           const GraphModel& model, const ViewTransform& vt,
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

    m_RenderTarget->BeginDraw();
    m_RenderTarget->Clear(D2D1::ColorF(0.15f, 0.15f, 0.15f));

    if (drawGrid) DrawGrid(clientRect, vt);

    for (const auto& e : model.Edges())
        DrawEdge(e, model, vt, e.Id == selectedEdge);

    if (preview.Active)
        DrawEdgePreview(preview, model, vt);

    for (const auto& n : model.Nodes()) {
        bool sel = std::find(selectedNodes.begin(), selectedNodes.end(), n.Id) != selectedNodes.end();
        DrawNode(n, vt, sel);
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

void GraphRenderer::DrawEdgePreview(const EdgePreview& preview, const GraphModel& model,
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

void GraphRenderer::DrawGrid(const RECT& rc, const ViewTransform& vt) {
    const float gridSpacing = 40.0f * vt.Scale;
    if (gridSpacing < 8.0f) return;

    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f));
    m_Brush->SetOpacity(1.0f);

    float startX = fmodf(vt.OffsetX, gridSpacing);
    float startY = fmodf(vt.OffsetY, gridSpacing);

    for (float x = startX; x < rc.right; x += gridSpacing)
        m_RenderTarget->DrawLine({ x, 0 }, { x, (float)rc.bottom }, m_Brush.Get(), 0.5f);

    for (float y = startY; y < rc.bottom; y += gridSpacing)
        m_RenderTarget->DrawLine({ 0, y }, { (float)rc.right, y }, m_Brush.Get(), 0.5f);
}

void GraphRenderer::DrawEdge(const Edge& e, const GraphModel& model,
                             const ViewTransform& vt, bool selected) {
    const Node* from = model.GetNode(e.From);
    const Node* to   = model.GetNode(e.To);
    if (!from || !to) return;

    auto p0 = vt.ToScreen(from->X, from->Y);
    auto p1 = vt.ToScreen(to->X,   to->Y);

    float dx = p1.x - p0.x, dy = p1.y - p0.y;
    float len = std::sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;

    float nx = dx / len, ny = dy / len;

    // Clip endpoints to node boundaries.
    float hw0 = from->Width  * vt.Scale * 0.5f;
    float hh0 = from->Height * vt.Scale * 0.5f;
    float hw1 = to->Width    * vt.Scale * 0.5f;
    float hh1 = to->Height   * vt.Scale * 0.5f;

    float t0 = std::min(std::fabsf(nx) > 0.0f ? hw0 / std::fabsf(nx) : 1e9f,
                        std::fabsf(ny) > 0.0f ? hh0 / std::fabsf(ny) : 1e9f);
    t0 = std::min(t0, len * 0.5f);
    float t1 = std::min(std::fabsf(nx) > 0.0f ? hw1 / std::fabsf(nx) : 1e9f,
                        std::fabsf(ny) > 0.0f ? hh1 / std::fabsf(ny) : 1e9f);
    t1 = std::min(t1, len * 0.5f);

    D2D1_POINT_2F start = { p0.x + nx * t0, p0.y + ny * t0 };
    D2D1_POINT_2F end   = { p1.x - nx * t1, p1.y - ny * t1 };

    float strokeWidth = e.Style.Width * (selected ? 2.0f : 1.0f);
    m_Brush->SetColor(selected
        ? D2D1::ColorF(D2D1::ColorF::Yellow)
        : ColorrefToD2D(e.Style.Color));
    m_RenderTarget->DrawLine(start, end, m_Brush.Get(), strokeWidth);

    if (e.Style.Directed)
        DrawArrowhead(end, { nx, ny });

    if (!e.Label.empty()) {
        D2D1_POINT_2F mid = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f };
        const float lw = 110.0f, lh = 18.0f;
        D2D1_RECT_F lr = { mid.x - lw * 0.5f, mid.y - lh * 0.5f,
                           mid.x + lw * 0.5f, mid.y + lh * 0.5f };
        m_Brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.88f));
        m_RenderTarget->FillRectangle(lr, m_Brush.Get());
        m_Brush->SetColor(selected ? D2D1::ColorF(D2D1::ColorF::Yellow)
                                   : D2D1::ColorF(0.85f, 0.85f, 0.85f));
        m_RenderTarget->DrawTextW(e.Label.c_str(), (UINT32)e.Label.size(),
            m_EdgeTextFormat.Get(), lr, m_Brush.Get());
    }
}

void GraphRenderer::DrawArrowhead(D2D1_POINT_2F tip, D2D1_POINT_2F dir) {
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

void GraphRenderer::DrawNode(const Node& n, const ViewTransform& vt, bool selected) {
    auto center = vt.ToScreen(n.X, n.Y);
    float hw = n.Width  * vt.Scale * 0.5f;
    float hh = n.Height * vt.Scale * 0.5f;
    float r  = n.Style.CornerRadius * vt.Scale;

    D2D1_ROUNDED_RECT rr{
        { center.x - hw, center.y - hh, center.x + hw, center.y + hh },
        r, r
    };

    // Fill
    m_Brush->SetColor(ColorrefToD2D(n.Style.FillColor));
    m_RenderTarget->FillRoundedRectangle(rr, m_Brush.Get());

    // Border
    float borderWidth = n.Style.BorderWidth * (selected ? 2.5f : 1.0f);
    m_Brush->SetColor(selected
        ? D2D1::ColorF(D2D1::ColorF::Yellow)
        : ColorrefToD2D(n.Style.BorderColor));
    m_RenderTarget->DrawRoundedRectangle(rr, m_Brush.Get(), borderWidth);

    // Label
    if (!n.Label.empty()) {
        m_Brush->SetColor(ColorrefToD2D(n.Style.TextColor));
        m_RenderTarget->DrawTextW(
            n.Label.c_str(), (UINT32)n.Label.size(),
            m_TextFormat.Get(),
            rr.rect,
            m_Brush.Get());
    }
}

void GraphRenderer::DrawRubberBand(const RubberBand& rb) {
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

NodeId GraphRenderer::HitTestNode(const GraphModel& model, float gx, float gy) const {
    for (auto it = model.Nodes().rbegin(); it != model.Nodes().rend(); ++it) {
        const auto& n = *it;
        if (gx >= n.X - n.Width  * 0.5f && gx <= n.X + n.Width  * 0.5f &&
            gy >= n.Y - n.Height * 0.5f && gy <= n.Y + n.Height * 0.5f)
            return n.Id;
    }
    return InvalidNode;
}

void GraphRenderer::DrawMinimap(const MinimapConfig& cfg, const RECT& clientRect,
                                const GraphModel& model, const ViewTransform& vt) {
    if (!cfg.Visible) return;

    D2D1_RECT_F rect = { cfg.X, cfg.Y, cfg.X + cfg.Width, cfg.Y + cfg.Height };

    m_Brush->SetColor(D2D1::ColorF(0.05f, 0.05f, 0.05f, 0.85f));
    m_RenderTarget->FillRectangle(rect, m_Brush.Get());

    m_Brush->SetColor(D2D1::ColorF(0.55f, 0.55f, 0.55f));
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
    m_Brush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f, 0.8f));
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
    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));
    m_RenderTarget->FillRectangle(vpRect, m_Brush.Get());
    m_Brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 0.5f, 0.9f));
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

void GraphRenderer::DrawResizeHandles(const Node& n, const ViewTransform& vt,
                                      ResizeHandle hovered) {
    D2D1_POINT_2F pts[8];
    BuildHandlePositions(n, vt, pts);
    const float hs = 4.5f;
    for (int i = 0; i < 8; ++i) {
        auto h = static_cast<ResizeHandle>(i + 1);
        D2D1_RECT_F r = { pts[i].x - hs, pts[i].y - hs,
                          pts[i].x + hs, pts[i].y + hs };
        m_Brush->SetColor(h == hovered
            ? D2D1::ColorF(D2D1::ColorF::Yellow)
            : D2D1::ColorF(1.0f, 1.0f, 1.0f));
        m_RenderTarget->FillRectangle(r, m_Brush.Get());
        m_Brush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
        m_RenderTarget->DrawRectangle(r, m_Brush.Get(), 1.0f);
    }
}

ResizeHandle GraphRenderer::HitTestResizeHandle(const Node& n, const ViewTransform& vt,
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

EdgeId GraphRenderer::HitTestEdge(const GraphModel& model, float gx, float gy, float tolerance) const {
    for (const auto& e : model.Edges()) {
        const Node* from = model.GetNode(e.From);
        const Node* to   = model.GetNode(e.To);
        if (!from || !to) continue;

        float dx = to->X - from->X, dy = to->Y - from->Y;
        float len2 = dx * dx + dy * dy;
        if (len2 < 1.0f) continue;

        float t = ((gx - from->X) * dx + (gy - from->Y) * dy) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        float px = from->X + t * dx - gx;
        float py = from->Y + t * dy - gy;
        if (px * px + py * py <= tolerance * tolerance)
            return e.Id;
    }
    return InvalidEdge;
}

} // namespace GraphCtrl
