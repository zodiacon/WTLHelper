#include "NodeGraphImaging.h"
#include "NodeGraphSerialization.h"
#include <shlwapi.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_set>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;

namespace NodeGraphCtrl {

    namespace Imaging {

        HRESULT CreateFactory(IWICImagingFactory** factory) {
            return ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory));
        }

        HRESULT Decode(IWICImagingFactory* factory, const void* data, size_t size, IWICBitmapSource** result) {
            *result = nullptr;
            if (!data || size == 0 || size > MAXDWORD)
                return E_INVALIDARG;

            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapDecoder> decoder;
            HRESULT hr = factory->CreateStream(stream.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), (DWORD)size);	// only read
            if (SUCCEEDED(hr))
                hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
            UINT count = 0;
            if (SUCCEEDED(hr))
                hr = decoder->GetFrameCount(&count);
            if (SUCCEEDED(hr) && count == 0)
                hr = WINCODEC_ERR_FRAMEMISSING;
            if (FAILED(hr))
                return hr;

            // an icon file has a frame for each size: take the largest
            ComPtr<IWICBitmapFrameDecode> best;
            UINT bestArea = 0;
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IWICBitmapFrameDecode> frame;
                UINT w = 0, h = 0;
                if (SUCCEEDED(decoder->GetFrame(i, frame.GetAddressOf())) && SUCCEEDED(frame->GetSize(&w, &h)) && (!best || w * h > bestArea)) {
                    best = frame;
                    bestArea = w * h;
                }
            }
            if (!best)
                return WINCODEC_ERR_FRAMEMISSING;

            // the decoder reads from the stream lazily: copy the pixels so the result doesn't depend on the data
            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICBitmap> bitmap;
            hr = factory->CreateFormatConverter(converter.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = converter->Initialize(best.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut);
            if (SUCCEEDED(hr))
                hr = factory->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, bitmap.GetAddressOf());
            if (SUCCEEDED(hr))
                *result = bitmap.Detach();
            return hr;
        }

        HRESULT EncodePng(IWICImagingFactory* factory, IWICBitmapSource* source, std::vector<uint8_t>& png) {
            ComPtr<IStream> stream;
            stream.Attach(::SHCreateMemStream(nullptr, 0));
            if (!stream)
                return E_OUTOFMEMORY;

            // straight alpha is what PNG has
            ComPtr<IWICFormatConverter> converter;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> props;
            UINT w = 0, h = 0;
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            HRESULT hr = factory->CreateFormatConverter(converter.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut);
            if (SUCCEEDED(hr))
                hr = converter->GetSize(&w, &h);
            if (SUCCEEDED(hr))
                hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (SUCCEEDED(hr))
                hr = encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf());
            if (SUCCEEDED(hr))
                hr = frame->Initialize(props.Get());
            if (SUCCEEDED(hr))
                hr = frame->SetSize(w, h);
            if (SUCCEEDED(hr))
                hr = frame->SetPixelFormat(&format);
            if (SUCCEEDED(hr))
                hr = frame->WriteSource(converter.Get(), nullptr);
            if (SUCCEEDED(hr))
                hr = frame->Commit();
            if (SUCCEEDED(hr))
                hr = encoder->Commit();

            STATSTG stat{};
            if (SUCCEEDED(hr))
                hr = stream->Stat(&stat, STATFLAG_NONAME);
            if (SUCCEEDED(hr) && stat.cbSize.QuadPart > MAXDWORD)
                hr = E_OUTOFMEMORY;
            if (SUCCEEDED(hr)) {
                png.resize((size_t)stat.cbSize.QuadPart);
                LARGE_INTEGER start{};
                ULONG read = 0;
                hr = stream->Seek(start, STREAM_SEEK_SET, nullptr);
                if (SUCCEEDED(hr))
                    hr = stream->Read(png.data(), (ULONG)png.size(), &read);
                if (SUCCEEDED(hr) && read != png.size())
                    hr = E_FAIL;
            }
            return hr;
        }

    } // namespace Imaging

    // ---- NodeGraphModel: images ---------------------------------------------------

    namespace {
        // the PNG of a bitmap WIC made of a handle
        template<typename MakeSource>
        std::vector<uint8_t> HandleToPng(MakeSource make) {
            Imaging::ComScope com;
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmap> bitmap;
            std::vector<uint8_t> png;
            if (FAILED(Imaging::CreateFactory(factory.GetAddressOf())) ||
                FAILED(make(factory.Get(), bitmap.GetAddressOf())) ||
                FAILED(Imaging::EncodePng(factory.Get(), bitmap.Get(), png)))
                png.clear();
            return png;
        }
    }

    ImageId NodeGraphModel::AddImage(const void* data, size_t size) {
        if (!data || size == 0)
            return InvalidImage;

        for (const auto& image : m_Images)
            if (image.Data.size() == size && std::memcmp(image.Data.data(), data, size) == 0)
                return image.Id;

        UINT w = 0, h = 0;
        {
            Imaging::ComScope com;
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IWICBitmapSource> decoded;
            if (FAILED(Imaging::CreateFactory(factory.GetAddressOf())) ||
                FAILED(Imaging::Decode(factory.Get(), data, size, decoded.GetAddressOf())) ||
                FAILED(decoded->GetSize(&w, &h)) || w == 0 || h == 0)
                return InvalidImage;
        }

        Image image;
        image.Id = m_NextImageId++;
        image.Key = Serialization::NewImageKey();
        image.Data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        image.Width = w;
        image.Height = h;
        m_Images.push_back(std::move(image));
        return m_Images.back().Id;
    }

    ImageId NodeGraphModel::AddImage(const wchar_t* path) {
        FILE* f = nullptr;
        if (!path || _wfopen_s(&f, path, L"rb") != 0 || !f)
            return InvalidImage;

        std::vector<uint8_t> data;
        const long maxSize = 256 << 20;
        long size = 0;
        if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && size <= maxSize && fseek(f, 0, SEEK_SET) == 0) {
            data.resize(size);
            if (fread(data.data(), 1, data.size(), f) != data.size())
                data.clear();
        }
        fclose(f);
        return AddImage(data.data(), data.size());
    }

    ImageId NodeGraphModel::AddImage(HICON icon) {
        if (!icon)
            return InvalidImage;
        auto png = HandleToPng([icon](IWICImagingFactory* factory, IWICBitmap** bitmap) {
            return factory->CreateBitmapFromHICON(icon, bitmap);
        });
        return AddImage(png.data(), png.size());
    }

    ImageId NodeGraphModel::AddImage(HBITMAP bitmap, bool hasAlpha) {
        if (!bitmap)
            return InvalidImage;
        auto png = HandleToPng([bitmap, hasAlpha](IWICImagingFactory* factory, IWICBitmap** result) {
            return factory->CreateBitmapFromHBITMAP(bitmap, nullptr, hasAlpha ? WICBitmapUseAlpha : WICBitmapIgnoreAlpha, result);
        });
        return AddImage(png.data(), png.size());
    }

    bool NodeGraphModel::RemoveImage(ImageId id) {
        return std::erase_if(m_Images, [id](const Image& image) { return image.Id == id; }) > 0;
    }

    size_t NodeGraphModel::RemoveUnusedImages(const std::vector<ImageId>& keep) {
        std::vector<ImageId> used;
        used.reserve(keep.size() + m_Nodes.size() * 2);
        used.insert(used.end(), keep.begin(), keep.end());
        for (const auto& n : m_Nodes) {
            used.push_back(n.Content.Icon);
            used.push_back(n.Content.Image);
        }
        std::sort(used.begin(), used.end());
        return std::erase_if(m_Images, [&](const Image& image) { return !std::binary_search(used.begin(), used.end(), image.Id); });
    }

    const Image* NodeGraphModel::GetImage(ImageId id) const {
        for (const auto& image : m_Images) if (image.Id == id) return &image;
        return nullptr;
    }

    // ---- Node shapes --------------------------------------------------------------------

    bool NodeContains(const Node& n, float x, float y) {
        const float a = n.Width * 0.5f, b = n.Height * 0.5f;
        const float dx = std::fabs(x - n.X), dy = std::fabs(y - n.Y);
        if (dx > a || dy > b) return false;
        switch (n.Style.Shape) {
            case NodeShape::Ellipse: return (dx / a) * (dx / a) + (dy / b) * (dy / b) <= 1.0f;
            case NodeShape::Diamond: return dx / a + dy / b <= 1.0f;
            default:                 return true;
        }
    }

    float NodeBorderDistance(const Node& n, float dx, float dy) {
        const float a = n.Width * 0.5f, b = n.Height * 0.5f;
        dx = std::fabs(dx);
        dy = std::fabs(dy);
        switch (n.Style.Shape) {
            case NodeShape::Ellipse: return 1.0f / std::sqrt((dx / a) * (dx / a) + (dy / b) * (dy / b));
            case NodeShape::Diamond: return 1.0f / (dx / a + dy / b);
            default:                 return (std::min)(dx > 0 ? a / dx : 1e9f, dy > 0 ? b / dy : 1e9f);
        }
    }

    float NodeHalfWidthAt(const Node& n, float dy) {
        const float a = n.Width * 0.5f, b = n.Height * 0.5f;
        const float t = (std::min)(std::fabs(dy) / b, 1.0f);
        switch (n.Style.Shape) {
            case NodeShape::Ellipse: return a * std::sqrt(1.0f - t * t);
            case NodeShape::Diamond: return a * (1.0f - t);
            default:                 return a;
        }
    }

    // ---- Edge paths -------------------------------------------------------------------

    std::vector<Point> EdgePath(const Edge& e, const Node& from, const Node& to, float lane) {
        std::vector<Point> path;
        path.reserve(e.Waypoints.size() + 2);
        path.push_back({ from.X, from.Y });
        path.insert(path.end(), e.Waypoints.begin(), e.Waypoints.end());
        path.push_back({ to.X, to.Y });

        const float dx = path[1].X - path[0].X, dy = path[1].Y - path[0].Y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (path.size() == 2 && len < 1e-3f)
            return {};
        if (path.size() == 2 && lane != 0) {
            const float nx = dx / len, ny = dy / len;
            for (auto& p : path) { p.X -= ny * lane; p.Y += nx * lane; }
        }

        // The ends on the outlines of the nodes. A straight edge is clipped at both ends of the same segment, so neither
        // end may go past its middle (the nodes may overlap); next to a bend an end may take the whole segment, or a
        // bend close to a node would leave the end, and its arrowhead, inside (under) the node. Both ends are measured
        // against the points before clipping.
        const float maxPart = path.size() == 2 ? 0.5f : 1.0f;
        auto clip = [&](const Node& n, Point& at, Point other) {
            const float ddx = other.X - at.X, ddy = other.Y - at.Y;
            const float l = std::sqrt(ddx * ddx + ddy * ddy);
            if (l < 1e-3f) return;
            const float nx = ddx / l, ny = ddy / l;
            const float t = (std::min)(NodeBorderDistance(n, nx, ny), l * maxPart);
            at.X += nx * t;
            at.Y += ny * t;
        };
        const Point beforeLast = path[path.size() - 2];
        clip(from, path.front(), path[1]);
        clip(to, path.back(), beforeLast);
        return path;
    }

    std::vector<Point> SelfLoopPath(const Node& n, float reach) {
        const float rise = (std::min)(n.Height * 0.25f, 40.0f);
        const float right = n.X + n.Width * 0.5f + reach;
        const float end = n.X + NodeHalfWidthAt(n, rise);     // on the outline of the shape
        return { { end, n.Y - rise }, { right, n.Y - rise }, { right, n.Y + rise }, { end, n.Y + rise } };
    }

    std::vector<Point> CurveControls(const std::vector<Point>& p) {
        std::vector<Point> controls;
        const size_t n = p.size();
        if (n < 2) return controls;
        controls.reserve((n - 1) * 2);
        for (size_t i = 0; i + 1 < n; i++) {
            // the tangents at the ends of the piece: along the neighbors of its points (the ends are their own)
            const Point& prev = p[i == 0 ? 0 : i - 1];
            const Point& next = p[i + 2 < n ? i + 2 : n - 1];
            controls.push_back({ p[i].X + (p[i + 1].X - prev.X) / 6, p[i].Y + (p[i + 1].Y - prev.Y) / 6 });
            controls.push_back({ p[i + 1].X - (next.X - p[i].X) / 6, p[i + 1].Y - (next.Y - p[i].Y) / 6 });
        }
        return controls;
    }

    Point EdgeLabelPoint(const std::vector<Point>& path, const std::vector<Point>& controls) {
        if (path.size() < 2) return path.empty() ? Point{} : path[0];
        // the middle point of an odd number of points (a bend) or the middle of the middle piece
        const size_t a = (path.size() - 1) / 2, b = path.size() / 2;
        if (a != b) {
            if (controls.empty())
                return { (path[a].X + path[b].X) * 0.5f, (path[a].Y + path[b].Y) * 0.5f };
            const Point& c1 = controls[a * 2];
            const Point& c2 = controls[a * 2 + 1];
            return { (path[a].X + 3 * c1.X + 3 * c2.X + path[b].X) / 8, (path[a].Y + 3 * c1.Y + 3 * c2.Y + path[b].Y) / 8 };
        }
        return path[a];
    }

    // ---- NodeGraphModel: groups ---------------------------------------------------------

    GroupId NodeGraphModel::AddGroup(std::wstring title, const std::vector<NodeId>& nodes) {
        Group g;
        g.Id = m_NextGroupId++;
        g.Title = std::move(title);
        m_Groups.push_back(std::move(g));
        const GroupId id = m_Groups.back().Id;
        for (NodeId n : nodes)
            AddToGroup(id, n);
        return id;
    }

    bool NodeGraphModel::RemoveGroup(GroupId id) {
        auto it = std::find_if(m_Groups.begin(), m_Groups.end(), [id](const Group& g) { return g.Id == id; });
        if (it == m_Groups.end()) return false;
        for (NodeId n : it->Nodes) m_GroupOf.erase(n);
        m_Groups.erase(it);
        return true;
    }

    void NodeGraphModel::RestoreGroup(const Group& g) {
        RemoveGroup(g.Id);
        Group copy = g;
        copy.Nodes.clear();
        m_Groups.push_back(std::move(copy));
        for (NodeId n : g.Nodes)
            AddToGroup(g.Id, n);
        m_NextGroupId = (std::max)(m_NextGroupId, g.Id + 1);
    }

    bool NodeGraphModel::AddToGroup(GroupId id, NodeId node) {
        Group* g = GetGroup(id);
        if (!g || !GetNode(node)) return false;
        if (GroupOf(node) == id) return true;
        RemoveFromGroup(node);
        g->Nodes.push_back(node);
        m_GroupOf[node] = id;
        return true;
    }

    void NodeGraphModel::RemoveFromGroup(NodeId node) {
        auto it = m_GroupOf.find(node);
        if (it == m_GroupOf.end()) return;
        if (Group* g = GetGroup(it->second))
            std::erase(g->Nodes, node);
        m_GroupOf.erase(it);
    }

    void NodeGraphModel::ForgetMembers(const std::vector<NodeId>& nodes) {
        if (m_GroupOf.empty()) return;
        // in one pass over each group (a large group deleted: not a search of it for each node)
        std::unordered_set<NodeId> gone;
        gone.reserve(nodes.size());
        for (NodeId n : nodes)
            if (m_GroupOf.erase(n)) gone.insert(n);
        if (gone.empty()) return;
        for (auto& g : m_Groups)
            std::erase_if(g.Nodes, [&](NodeId n) { return gone.contains(n); });
    }

    Group* NodeGraphModel::GetGroup(GroupId id) {
        for (auto& g : m_Groups) if (g.Id == id) return &g;
        return nullptr;
    }

    const Group* NodeGraphModel::GetGroup(GroupId id) const {
        for (const auto& g : m_Groups) if (g.Id == id) return &g;
        return nullptr;
    }

    GroupId NodeGraphModel::GroupOf(NodeId node) const {
        auto it = m_GroupOf.find(node);
        return it == m_GroupOf.end() ? InvalidGroup : it->second;
    }

    bool NodeGraphModel::IsHidden(NodeId node) const {
        if (m_GroupOf.empty()) return false;
        const Group* g = GetGroup(GroupOf(node));
        return g && g->Collapsed;
    }

    Rect NodeGraphModel::GetGroupBox(const Group& g) const {
        Rect r{ FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (NodeId id : g.Nodes)
            if (const Node* n = GetNode(id)) {
                r.Left = (std::min)(r.Left, n->X - n->Width * 0.5f);
                r.Top = (std::min)(r.Top, n->Y - n->Height * 0.5f);
                r.Right = (std::max)(r.Right, n->X + n->Width * 0.5f);
                r.Bottom = (std::max)(r.Bottom, n->Y + n->Height * 0.5f);
            }
        if (r.Left > r.Right) return {};
        if (g.Collapsed) {
            const float cx = (r.Left + r.Right) * 0.5f, cy = (r.Top + r.Bottom) * 0.5f;
            return { cx - k_CollapsedWidth * 0.5f, cy - k_CollapsedHeight * 0.5f, cx + k_CollapsedWidth * 0.5f, cy + k_CollapsedHeight * 0.5f };
        }
        return { r.Left - k_GroupPadding, r.Top - k_GroupPadding - k_GroupHeader, r.Right + k_GroupPadding, r.Bottom + k_GroupPadding };
    }

    Rect NodeGraphModel::GetGroupHeader(const Group& g) const {
        Rect box = GetGroupBox(g);
        if (!box.IsEmpty() && !g.Collapsed)
            box.Bottom = box.Top + k_GroupHeader;
        return box;
    }

    Rect NodeGraphModel::GetGroupButton(const Group& g) const {
        const Rect header = GetGroupHeader(g);
        if (header.IsEmpty()) return {};
        // at the right of the title band (of a collapsed box: of its top band's height)
        const float top = header.Top + (k_GroupHeader - k_GroupButton) * 0.5f, right = header.Right - 6.0f;
        return { right - k_GroupButton, top, right, top + k_GroupButton };
    }

    Node NodeGraphModel::CollapsedNode(const Group& g) const {
        const Rect box = GetGroupBox(g);
        Node n;
        n.Id = InvalidNode;
        n.X = (box.Left + box.Right) * 0.5f;
        n.Y = (box.Top + box.Bottom) * 0.5f;
        n.Width = box.Width();
        n.Height = box.Height();
        n.Label = g.Title;
        return n;
    }

    // ---- NodeGraphModel: search ---------------------------------------------------------

    std::vector<NodeId> NodeGraphModel::FindNodes(const std::wstring& text, bool matchCase) const {
        std::vector<NodeId> found;
        if (text.empty())
            return found;
        const DWORD flags = FIND_FROMSTART | (matchCase ? 0 : LINGUISTIC_IGNORECASE);
        auto has = [&](const std::wstring& s) {
            return !s.empty() && ::FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, flags, s.c_str(), (int)s.size(),
                text.c_str(), (int)text.size(), nullptr, nullptr, nullptr, 0) >= 0;
        };
        for (const auto& n : m_Nodes)
            if (has(n.Label) || has(n.Content.Title) || has(n.Tooltip))
                found.push_back(n.Id);
        return found;
    }

    // ---- NodeGraphModel: node layout ----------------------------------------------

    namespace {
        constexpr float k_Pad           = 6.0f;
        constexpr float k_TitleHeight   = 22.0f;
        constexpr float k_CaptionHeight = 20.0f;   // the label under an image, or on a band over it
        constexpr float k_MinIconSize   = 4.0f;    // smaller than this isn't drawn
        constexpr float k_MinContentHeight = 8.0f; // less than this under the title: only the title is shown

        Rect Deflate(const Rect& r, float dx, float dy) {
            return { r.Left + dx, r.Top + dy, r.Right - dx, r.Bottom - dy };
        }

        Rect Centered(const Rect& r, float w, float h) {
            float x = (r.Left + r.Right - w) * 0.5f, y = (r.Top + r.Bottom - h) * 0.5f;
            return { x, y, x + w, y + h };
        }

        // the image (w x h pixels) in an area: where it goes, and the part of it that goes there
        void FitImage(ImageFit fit, const Rect& area, float w, float h, Rect& dst, Rect& src) {
            const Rect whole{ 0, 0, w, h };
            switch (fit) {
                case ImageFit::Stretch:
                    dst = area;
                    src = whole;
                    break;
                case ImageFit::Fit: {
                    float s = (std::min)(area.Width() / w, area.Height() / h);
                    dst = Centered(area, w * s, h * s);
                    src = whole;
                    break;
                }
                case ImageFit::Fill: {
                    float s = (std::max)(area.Width() / w, area.Height() / h);
                    dst = area;
                    src = Centered(whole, area.Width() / s, area.Height() / s);
                    break;
                }
                case ImageFit::Center: {
                    float cw = (std::min)(w, area.Width()), ch = (std::min)(h, area.Height());
                    dst = Centered(area, cw, ch);
                    src = Centered(whole, cw, ch);
                    break;
                }
            }
        }
    }

    NodeLayout NodeGraphModel::GetNodeLayout(const Node& n) const {
        const NodeContent& c = n.Content;
        NodeLayout layout;

        Rect content{ 0, 0, n.Width, n.Height };
        if (!c.Title.empty()) {
            layout.Title = { 0, 0, n.Width, (std::min)(k_TitleHeight, n.Height) };
            content.Top = layout.Title.Bottom;
        }
        if (!c.Title.empty() && content.Height() < k_MinContentHeight) {    // the title takes the node
            layout.Title.Bottom = n.Height;
            return layout;
        }
        layout.Content = content;

        // the image takes the content but for the caption under it; the label and the icon share what is left for the label
        Rect labelArea = content;
        bool showLabel = true;
        if (const Image* image = GetImage(c.Image)) {
            Rect area = Deflate(content, k_Pad, k_Pad);
            const float caption = (std::min)(k_CaptionHeight, content.Height());
            switch (c.LabelPos) {
                case LabelPlacement::Below:
                    labelArea.Top = content.Bottom - caption;
                    area.Bottom = labelArea.Top;
                    break;
                case LabelPlacement::Overlay:
                    labelArea.Top = content.Bottom - caption;
                    break;
                case LabelPlacement::Hidden:
                    showLabel = false;
                    break;
                case LabelPlacement::Center:
                    break;
            }
            if (!area.IsEmpty())
                FitImage(c.Fit, area, (float)image->Width, (float)image->Height, layout.Image, layout.ImageSource);
            // the band goes over the bottom of the image itself
            if (c.LabelPos == LabelPlacement::Overlay && !layout.Image.IsEmpty())
                labelArea = { layout.Image.Left, (std::max)(layout.Image.Top, layout.Image.Bottom - caption), layout.Image.Right, layout.Image.Bottom };
        }

        Rect label = labelArea;
        if (GetImage(c.Icon)) {
            const bool alone = !showLabel || n.Label.empty();    // an icon without a label is centered
            float size = (std::min)({ c.IconSize, labelArea.Width() - 2 * k_Pad,
                c.IconPos == IconPlacement::Top || alone ? labelArea.Height() - 2 * k_Pad : labelArea.Height() - 4.0f });
            if (c.IconPos == IconPlacement::Top && !alone)
                size = (std::min)(size, labelArea.Height() * 0.6f);
            if (size >= k_MinIconSize) {
                const float cx = (labelArea.Left + labelArea.Right) * 0.5f, cy = (labelArea.Top + labelArea.Bottom) * 0.5f;
                if (alone)
                    layout.Icon = Centered(labelArea, size, size);
                else if (c.IconPos == IconPlacement::Left) {
                    layout.Icon = { labelArea.Left + k_Pad, cy - size * 0.5f, labelArea.Left + k_Pad + size, cy + size * 0.5f };
                    label.Left = layout.Icon.Right + 4.0f;
                    label.Right -= 4.0f;
                }
                else if (c.IconPos == IconPlacement::Right) {
                    layout.Icon = { labelArea.Right - k_Pad - size, cy - size * 0.5f, labelArea.Right - k_Pad, cy + size * 0.5f };
                    label.Right = layout.Icon.Left - 4.0f;
                    label.Left += 4.0f;
                }
                else {
                    layout.Icon = { cx - size * 0.5f, labelArea.Top + k_Pad, cx + size * 0.5f, labelArea.Top + k_Pad + size };
                    label.Top = layout.Icon.Bottom + 2.0f;
                }
            }
        }
        if (showLabel)
            layout.Label = label;
        return layout;
    }

} // namespace NodeGraphCtrl
