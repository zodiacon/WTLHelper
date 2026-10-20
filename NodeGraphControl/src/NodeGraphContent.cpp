#include "NodeGraphImaging.h"
#include "NodeGraphSerialization.h"
#include <shlwapi.h>
#include <dwrite.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwrite.lib")

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

    float NodeHalfHeightAt(const Node& n, float dx) {
        const float a = n.Width * 0.5f, b = n.Height * 0.5f;
        const float t = (std::min)(std::fabs(dx) / a, 1.0f);
        switch (n.Style.Shape) {
            case NodeShape::Ellipse: return b * std::sqrt(1.0f - t * t);
            case NodeShape::Diamond: return b * (1.0f - t);
            default:                 return b;
        }
    }

    // ---- Ports ------------------------------------------------------------------------

    const Port* FindPort(const Node& n, PortId id) {
        if (id == InvalidPort) return nullptr;
        for (const auto& p : n.Ports) if (p.Id == id) return &p;
        return nullptr;
    }

    Point PortNormal(PortSide side) {
        switch (side) {
            case PortSide::Left:  return { -1, 0 };
            case PortSide::Top:   return { 0, -1 };
            case PortSide::Right: return { 1, 0 };
            default:              return { 0, 1 };
        }
    }

    COLORREF PortColor(const Port& p) {
        if (p.Color != CLR_INVALID) return p.Color;
        switch (p.Direction) {
            case PortDirection::In:  return RGB(90, 170, 240);
            case PortDirection::Out: return RGB(240, 170, 60);
            default:                 return RGB(200, 200, 200);
        }
    }

    Point PortPosition(const Node& n, const Port& p) {
        // where along the side: its own place, or spread evenly with the side's other ports without one
        float along = p.Offset;
        if (along < 0) {
            int index = 0, count = 0;
            for (const auto& q : n.Ports)
                if (q.Side == p.Side && q.Offset < 0) {
                    if (&q == &p) index = count;
                    count++;
                }
            along = (index + 1.0f) / (count + 1.0f);
        }
        along = std::clamp(along, 0.0f, 1.0f);
        // on the outline of the shape
        switch (p.Side) {
            case PortSide::Left:
            case PortSide::Right: {
                const float dy = (along - 0.5f) * n.Height;
                const float half = NodeHalfWidthAt(n, dy);
                return { p.Side == PortSide::Left ? n.X - half : n.X + half, n.Y + dy };
            }
            default: {
                const float dx = (along - 0.5f) * n.Width;
                const float half = NodeHalfHeightAt(n, dx);
                return { n.X + dx, p.Side == PortSide::Top ? n.Y - half : n.Y + half };
            }
        }
    }

    PortId NodeGraphModel::AddPort(NodeId node, std::wstring name, PortSide side, PortDirection direction, float offset) {
        Node* n = GetNode(node);
        if (!n) return InvalidPort;
        PortId id = 0;
        for (const auto& p : n->Ports) id = (std::max)(id, p.Id + 1);
        Port p;
        p.Id = id;
        p.Name = std::move(name);
        p.Side = side;
        p.Direction = direction;
        p.Offset = offset;
        n->Ports.push_back(std::move(p));
        return id;
    }

    bool NodeGraphModel::RemovePort(NodeId node, PortId port) {
        Node* n = GetNode(node);
        if (!n || !std::erase_if(n->Ports, [port](const Port& p) { return p.Id == port; }))
            return false;
        for (auto& e : m_Edges) {   // its edges end on the outline now
            if (e.From == node && e.FromPort == port) e.FromPort = InvalidPort;
            if (e.To == node && e.ToPort == port) e.ToPort = InvalidPort;
        }
        return true;
    }

    EdgeId NodeGraphModel::Connect(NodeId from, PortId fromPort, NodeId to, PortId toPort, std::wstring label) {
        const EdgeId id = AddEdge(from, to, std::move(label));
        Edge* e = GetEdge(id);
        e->FromPort = fromPort;
        e->ToPort = toPort;
        return id;
    }

    // ---- Edge paths -------------------------------------------------------------------

    namespace {
        // The way along the axis that to is farther from from on.
        Point Facing(Point from, Point to) {
            const float dx = to.X - from.X, dy = to.Y - from.Y;
            return std::abs(dx) >= std::abs(dy) ? Point{ dx < 0 ? -1.0f : 1.0f, 0 } : Point{ 0, dy < 0 ? -1.0f : 1.0f };
        }

        // An elbow edge (EdgePath): its ends at the ports or the sides facing the other end. start, end: the ports
        // (centers without).
        std::vector<Point> ElbowEdgePath(const Edge& e, const Node& from, const Port* fromPort, const Node& to, const Port* toPort,
                                         Point start, Point end, float lane) {
            Point fromOut = fromPort ? PortNormal(fromPort->Side) : Point{}, toOut = toPort ? PortNormal(toPort->Side) : Point{};
            auto side = [](const Node& n, Point toward, Point& at, Point& out) {
                out = Facing({ n.X, n.Y }, toward);
                at = out.X != 0 ? Point{ n.X + out.X * NodeHalfWidthAt(n, 0), n.Y } : Point{ n.X, n.Y + out.Y * NodeHalfHeightAt(n, 0) };
            };
            const Point fromAt = start, toAt = end;     // (toward each other before either moves)
            if (!fromPort) side(from, e.Waypoints.empty() ? toAt : e.Waypoints.front(), start, fromOut);
            if (!toPort) side(to, e.Waypoints.empty() ? fromAt : e.Waypoints.back(), end, toOut);
            auto box = [](const Node& n) { return Rect{ n.X - n.Width * 0.5f, n.Y - n.Height * 0.5f, n.X + n.Width * 0.5f, n.Y + n.Height * 0.5f }; };
            std::vector<Point> path = ElbowPath(start, fromOut, end, toOut, e.Waypoints, 20.0f, box(from), box(to));
            if (lane != 0 && !fromPort && !toPort && e.Waypoints.empty() && path.size() >= 2) {
                // to the right of the way it goes: the ends along their sides, the corners with them
                const float dx = toAt.X - fromAt.X, dy = toAt.Y - fromAt.Y, len = std::sqrt(dx * dx + dy * dy);
                if (len > 1e-3f) {
                    const float sx = -dy / len * lane, sy = dx / len * lane;
                    for (size_t i = 1; i + 1 < path.size(); i++) { path[i].X += sx; path[i].Y += sy; }
                    if (fromOut.X != 0) path.front().Y += sy; else path.front().X += sx;
                    if (toOut.X != 0) path.back().Y += sy; else path.back().X += sx;
                }
            }
            return path;
        }
    }

    std::vector<Point> ElbowPath(Point start, Point startOut, Point end, Point endOut, const std::vector<Point>& bends, float stub,
                                 Rect startBox, Rect endBox) {
        if (startBox.IsEmpty()) startBox = { start.X, start.Y, start.X, start.Y };
        if (endBox.IsEmpty()) endBox = { end.X, end.Y, end.X, end.Y };
        if (startOut.X == 0 && startOut.Y == 0) startOut = Facing(start, bends.empty() ? end : bends.front());
        if (endOut.X == 0 && endOut.Y == 0) endOut = Facing(end, bends.empty() ? start : bends.back());
        const Point a{ start.X + startOut.X * stub, start.Y + startOut.Y * stub };     // out of the nodes
        const Point b{ end.X + endOut.X * stub, end.Y + endOut.Y * stub };
        std::vector<Point> route;
        route.reserve(bends.size() * 2 + 8);
        route.push_back(start);
        if (bends.empty()) {
            // along the axis the start leaves on and across it
            const bool across = startOut.X != 0;
            auto along = [&](Point p) { return across ? p.X : p.Y; };
            auto cross = [&](Point p) { return across ? p.Y : p.X; };
            auto at = [&](float m, float c) { return across ? Point{ m, c } : Point{ c, m }; };
            auto alongLo = [&](const Rect& r) { return across ? r.Left : r.Top; };
            auto alongHi = [&](const Rect& r) { return across ? r.Right : r.Bottom; };
            auto crossLo = [&](const Rect& r) { return across ? r.Top : r.Left; };
            auto crossHi = [&](const Rect& r) { return across ? r.Bottom : r.Right; };
            const float outS = along(startOut), outE = along(endOut);
            if ((endOut.X != 0) == across) {
                if (outS == -outE && (along(end) - along(start)) * outS > 0) {
                    // facing each other: over in the middle between them
                    const float mid = (along(start) + along(end)) * 0.5f;
                    route.push_back(at(mid, cross(start)));
                    route.push_back(at(mid, cross(end)));
                }
                else if (outS == -outE) {
                    // facing away: out of both, and across between them, or round both on the nearer side
                    const float sLo = crossLo(startBox), sHi = crossHi(startBox), eLo = crossLo(endBox), eHi = crossHi(endBox);
                    float mid;
                    if (sHi < eLo)
                        mid = (sHi + eLo) * 0.5f;
                    else if (eHi < sLo)
                        mid = (eHi + sLo) * 0.5f;
                    else {
                        const float before = (std::min)(sLo, eLo) - stub, after = (std::max)(sHi, eHi) + stub;
                        mid = std::abs(before - cross(start)) + std::abs(before - cross(end)) <= std::abs(after - cross(start)) + std::abs(after - cross(end)) ? before : after;
                    }
                    route.push_back(a);
                    route.push_back(at(along(a), mid));
                    route.push_back(at(along(b), mid));
                    route.push_back(b);
                }
                else {
                    // the same way: out past both, and back in; round the one in front where the way past it runs into it
                    const float outer = outS > 0 ? (std::max)(alongHi(startBox), alongHi(endBox)) + stub : (std::min)(alongLo(startBox), alongLo(endBox)) - stub;
                    auto ahead = [&](const Rect& r) { return outS > 0 ? alongHi(r) : -alongLo(r); };
                    const bool startNearer = ahead(startBox) <= ahead(endBox);
                    const Rect& nearBox = startNearer ? startBox : endBox;
                    const Rect& farBox = startNearer ? endBox : startBox;
                    const float line = cross(startNearer ? start : end);
                    if (line >= crossLo(farBox) && line <= crossHi(farBox)) {
                        // between the two (or just out of the nearer), beside the one in front on the nearer side, past it
                        const float from = outS > 0 ? alongHi(nearBox) : alongLo(nearBox), to = outS > 0 ? alongLo(farBox) : alongHi(farBox);
                        const float between = (to - from) * outS > 0 ? (from + to) * 0.5f : from + outS * stub;
                        const float side = line - crossLo(farBox) <= crossHi(farBox) - line ? crossLo(farBox) - stub : crossHi(farBox) + stub;
                        if (startNearer) {
                            route.push_back(at(between, cross(start)));
                            route.push_back(at(between, side));
                            route.push_back(at(outer, side));
                            route.push_back(at(outer, cross(end)));
                        }
                        else {
                            route.push_back(at(outer, cross(start)));
                            route.push_back(at(outer, side));
                            route.push_back(at(between, side));
                            route.push_back(at(between, cross(end)));
                        }
                    }
                    else {
                        route.push_back(at(outer, cross(start)));
                        route.push_back(at(outer, cross(end)));
                    }
                }
            }
            else {
                // one across, one up and down: one corner where both go on straight to it, or out of both and two
                const Point corner = at(along(end), cross(start));
                if ((along(corner) - along(start)) * outS > 0 &&
                    (corner.X - end.X) * endOut.X + (corner.Y - end.Y) * endOut.Y > 0)
                    route.push_back(corner);
                else {
                    // across where the end is come into from, or past the start's node if that runs through it
                    float level = cross(b);
                    if (level >= crossLo(startBox) && level <= crossHi(startBox))
                        level = cross(endOut) < 0 ? (std::min)(level, crossLo(startBox) - stub) : (std::max)(level, crossHi(startBox) + stub);
                    // (out further where the end's way in is in line with it, or it would go along that and back)
                    const float out = std::abs(along(a) - along(b)) < 1e-3f ? along(a) + outS * stub : along(a);
                    route.push_back(a);
                    route.push_back(at(out, cross(start)));
                    route.push_back(at(out, level));
                    route.push_back(at(along(b), level));
                    route.push_back(b);
                }
            }
        }
        else {
            // out of the start, a corner to each bend (on the way it goes, unless that turns back), into the end (across
            // its way in last, so as not to go back along it)
            route.push_back(a);
            bool across = startOut.X != 0;      // the way the last piece goes
            auto to = [&](Point q, bool last) {
                const Point p = route.back();
                if (std::abs(p.X - q.X) < 1e-3f && std::abs(p.Y - q.Y) < 1e-3f)
                    return;
                if (std::abs(p.X - q.X) < 1e-3f || std::abs(p.Y - q.Y) < 1e-3f) {
                    across = std::abs(p.Y - q.Y) < 1e-3f;
                    route.push_back(q);
                    return;
                }
                const Point before = route[route.size() - 2];
                bool first = last ? endOut.X != 0 : across;
                Point corner = first ? Point{ q.X, p.Y } : Point{ p.X, q.Y };
                if ((corner.X - p.X) * (p.X - before.X) < 0 || (corner.Y - p.Y) * (p.Y - before.Y) < 0) {
                    first = !first;
                    corner = first ? Point{ q.X, p.Y } : Point{ p.X, q.Y };
                }
                route.push_back(corner);
                route.push_back(q);
                across = !first;
            };
            for (const auto& w : bends)
                to(w, false);
            to(b, true);
        }
        route.push_back(end);

        // without points twice, or in the middle of a straight line
        std::vector<Point> path;
        path.reserve(route.size());
        for (const auto& p : route) {
            if (!path.empty() && std::abs(path.back().X - p.X) < 1e-3f && std::abs(path.back().Y - p.Y) < 1e-3f)
                continue;
            if (path.size() >= 2) {
                const Point &u = path[path.size() - 2], &v = path.back();
                const bool straight = (std::abs(u.X - v.X) < 1e-3f && std::abs(v.X - p.X) < 1e-3f && (v.Y - u.Y) * (p.Y - v.Y) >= 0) ||
                                      (std::abs(u.Y - v.Y) < 1e-3f && std::abs(v.Y - p.Y) < 1e-3f && (v.X - u.X) * (p.X - v.X) >= 0);
                if (straight)
                    path.pop_back();
            }
            path.push_back(p);
        }
        return path;
    }

    std::vector<Point> EdgePath(const Edge& e, const Node& from, const Node& to, float lane) {
        const Port* fromPort = FindPort(from, e.FromPort);
        const Port* toPort = FindPort(to, e.ToPort);
        std::vector<Point> path;
        path.reserve(e.Waypoints.size() + 2);
        path.push_back(fromPort ? PortPosition(from, *fromPort) : Point{ from.X, from.Y });
        path.insert(path.end(), e.Waypoints.begin(), e.Waypoints.end());
        path.push_back(toPort ? PortPosition(to, *toPort) : Point{ to.X, to.Y });

        const float dx = path[1].X - path[0].X, dy = path[1].Y - path[0].Y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (path.size() == 2 && len < 1e-3f)
            return {};
        if (e.Style.Elbow)
            return ElbowEdgePath(e, from, fromPort, to, toPort, path.front(), path.back(), lane);
        if (fromPort && toPort)
            return path;    // both ends where they are
        if (path.size() == 2 && lane != 0 && !fromPort && !toPort) {
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
        if (!fromPort) clip(from, path.front(), path[1]);
        if (!toPort) clip(to, path.back(), beforeLast);
        return path;
    }

    EdgeShape GetEdgeShape(const Edge& e, const Node& from, const Node& to, float lane, std::span<const float> shifts) {
        EdgeShape shape;
        shape.Path = EdgePath(e, from, to, lane);
        if (shape.Path.size() < 2) {
            shape.Path.clear();
            return shape;
        }
        // at a port the curve leaves (comes in) straight out of the node
        const Port* fromPort = FindPort(from, e.FromPort);
        const Port* toPort = FindPort(to, e.ToPort);
        if (e.Style.Elbow) {
            // its pieces moved apart from others' (the ends stay on their nodes: the first and last pieces aren't moved)
            if (shifts.size() + 1 == shape.Path.size())
                for (size_t i = 1; i + 2 < shape.Path.size(); i++) {
                    if (shifts[i] == 0) continue;
                    Point &p = shape.Path[i], &q = shape.Path[i + 1];
                    if (std::abs(p.X - q.X) < 1e-3f) { p.X += shifts[i]; q.X += shifts[i]; }
                    else { p.Y += shifts[i]; q.Y += shifts[i]; }
                }
            // halfway along it (its middle point would be a corner)
            float length = 0;
            for (size_t i = 0; i + 1 < shape.Path.size(); i++)
                length += std::hypot(shape.Path[i + 1].X - shape.Path[i].X, shape.Path[i + 1].Y - shape.Path[i].Y);
            float left = length * 0.5f;
            shape.Label = shape.Path.back();
            for (size_t i = 0; i + 1 < shape.Path.size(); i++) {
                const Point &p = shape.Path[i], &q = shape.Path[i + 1];
                const float l = std::hypot(q.X - p.X, q.Y - p.Y);
                if (l >= left && l > 0) {
                    shape.Label = { p.X + (q.X - p.X) * left / l, p.Y + (q.Y - p.Y) * left / l };
                    break;
                }
                left -= l;
            }
            if (e.Style.CornerRadius > 0 && shape.Path.size() > 2) {
                EdgeShape round = RoundCorners(shape.Path, e.Style.CornerRadius);
                shape.Path = std::move(round.Path);
                shape.Controls = std::move(round.Controls);
            }
            return shape;
        }
        if (e.Style.Curved && (shape.Path.size() > 2 || fromPort || toPort))
            shape.Controls = CurveControls(shape.Path, fromPort ? PortNormal(fromPort->Side) : Point{}, toPort ? PortNormal(toPort->Side) : Point{});
        shape.Label = EdgeLabelPoint(shape.Path, shape.Controls);
        return shape;
    }

    EdgeShape RoundCorners(const std::vector<Point>& path, float radius) {
        EdgeShape s;
        const size_t n = path.size();
        if (n < 3 || radius <= 0) {
            s.Path = path;
            return s;
        }
        s.Path.reserve(n * 2);
        s.Controls.reserve(n * 4);
        s.Path.push_back(path[0]);
        auto line = [&](Point to) {     // (a curve along the line: its controls at a third and two)
            const Point from = s.Path.back();
            if (std::abs(to.X - from.X) < 1e-3f && std::abs(to.Y - from.Y) < 1e-3f) return;
            s.Controls.push_back({ from.X + (to.X - from.X) / 3, from.Y + (to.Y - from.Y) / 3 });
            s.Controls.push_back({ from.X + (to.X - from.X) * 2 / 3, from.Y + (to.Y - from.Y) * 2 / 3 });
            s.Path.push_back(to);
        };
        for (size_t i = 1; i + 1 < n; i++) {
            const Point &prev = path[i - 1], &c = path[i], &next = path[i + 1];
            const float in = std::hypot(c.X - prev.X, c.Y - prev.Y), out = std::hypot(next.X - c.X, next.Y - c.Y);
            const float r = (std::min)({ radius, in * 0.5f, out * 0.5f });
            if (r < 0.5f) {
                line(c);
                continue;
            }
            const Point din{ (c.X - prev.X) / in, (c.Y - prev.Y) / in }, dout{ (next.X - c.X) / out, (next.Y - c.Y) / out };
            const Point a{ c.X - din.X * r, c.Y - din.Y * r }, b{ c.X + dout.X * r, c.Y + dout.Y * r };
            line(a);
            const float k = 0.5523f * r;     // (a cubic as near a quarter circle as one can be)
            s.Controls.push_back({ a.X + din.X * k, a.Y + din.Y * k });
            s.Controls.push_back({ b.X - dout.X * k, b.Y - dout.Y * k });
            s.Path.push_back(b);
        }
        line(path.back());
        return s;
    }

    std::span<const float> ShiftsOf(const ElbowShiftMap& shifts, EdgeId edge) {
        if (shifts.empty()) return {};
        auto it = shifts.find(edge);
        return it != shifts.end() ? std::span<const float>(it->second) : std::span<const float>{};
    }

    ElbowShiftMap SpreadElbows(const std::vector<ElbowRoute>& routes, float spacing) {
        ElbowShiftMap shifts;
        // the middle pieces, across (horizontal) or up and down: where across them, and from where to where along them
        struct Piece { uint32_t Route, Index; float At, Lo, Hi; bool Vertical; };
        size_t count = 0;
        for (const auto& r : routes) count += r.Path.size() > 3 ? r.Path.size() - 3 : 0;
        std::vector<Piece> pieces;
        pieces.reserve(count);
        for (uint32_t ri = 0; ri < (uint32_t)routes.size(); ri++) {
            const auto& p = routes[ri].Path;
            for (uint32_t i = 1; i + 2 < (uint32_t)p.size(); i++) {
                const bool vertical = std::abs(p[i].X - p[i + 1].X) < 1e-3f;
                if (!vertical && std::abs(p[i].Y - p[i + 1].Y) >= 1e-3f) continue;
                const float a = vertical ? p[i].Y : p[i].X, b = vertical ? p[i + 1].Y : p[i + 1].X;
                pieces.push_back({ ri, i, vertical ? p[i].X : p[i].Y, (std::min)(a, b), (std::max)(a, b), vertical });
            }
        }
        std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.Vertical != b.Vertical ? a.Vertical < b.Vertical : a.At < b.At; });

        // the far end of the piece before (after) one, across it: where the line it turns into goes
        auto farBefore = [&](const Piece& c) { const Point& q = routes[c.Route].Path[c.Index - 1]; return c.Vertical ? q.X : q.Y; };
        auto farAfter = [&](const Piece& c) { const Point& q = routes[c.Route].Path[c.Index + 2]; return c.Vertical ? q.X : q.Y; };
        // a set of pieces that overlap: in tracks (those of edges with an end in common together), the tracks spread
        std::vector<uint32_t> members, track, order, best;
        auto spread = [&]() {
            const size_t k = members.size();
            // the tracks (union of the pieces with an end in common)
            track.resize(k);
            for (size_t i = 0; i < k; i++) track[i] = (uint32_t)i;
            std::function<uint32_t(uint32_t)> root = [&](uint32_t i) { return track[i] == i ? i : track[i] = root(track[i]); };
            for (size_t i = 0; i < k; i++)
                for (size_t j = i + 1; j < k; j++) {
                    const ElbowRoute &a = routes[pieces[members[i]].Route], &b = routes[pieces[members[j]].Route];
                    if (a.From == b.From || a.To == b.To) track[root((uint32_t)j)] = root((uint32_t)i);
                }
            std::vector<uint32_t> roots;
            roots.reserve(k);
            for (size_t i = 0; i < k; i++) {
                const uint32_t r = root((uint32_t)i);
                if (std::find(roots.begin(), roots.end(), r) == roots.end()) roots.push_back(r);
            }
            const size_t t = roots.size();
            if (t < 2) return;
            std::vector<uint32_t> trackOf(k);
            std::vector<float> mean(t, 0.0f);
            std::vector<int> size(t, 0);
            float base = 0;
            for (size_t i = 0; i < k; i++) {
                trackOf[i] = (uint32_t)(std::find(roots.begin(), roots.end(), root((uint32_t)i)) - roots.begin());
                mean[trackOf[i]] += pieces[members[i]].At;
                size[trackOf[i]]++;
                base += pieces[members[i]].At;
            }
            base /= (float)k;
            for (size_t i = 0; i < t; i++) mean[i] /= (float)size[i];

            // the order of the tracks across: the one that crosses the fewest (all of them tried, for a few)
            order.resize(t);
            for (size_t i = 0; i < t; i++) order[i] = (uint32_t)i;
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return mean[a] < mean[b]; });
            best = order;
            if (t <= 6) {
                std::vector<float> at(t);
                auto crossings = [&]() {
                    for (size_t i = 0; i < t; i++) at[order[i]] = base + ((float)i - (t - 1) * 0.5f) * spacing;
                    int n = 0;
                    for (size_t i = 0; i < k; i++)
                        for (size_t j = 0; j < k; j++) {
                            if (trackOf[i] == trackOf[j]) continue;
                            // the lines i turns into at its ends, across j?
                            const Piece &a = pieces[members[i]], &b = pieces[members[j]];
                            const float ca = at[trackOf[i]], cb = at[trackOf[j]];
                            const Point& p0 = routes[a.Route].Path[a.Index];
                            const Point& p1 = routes[a.Route].Path[a.Index + 1];
                            const float along0 = a.Vertical ? p0.Y : p0.X, along1 = a.Vertical ? p1.Y : p1.X;
                            for (auto [along, away] : { std::pair{ along0, farBefore(a) }, std::pair{ along1, farAfter(a) } })
                                if (cb > (std::min)(ca, away) && cb < (std::max)(ca, away) && along > b.Lo && along < b.Hi) n++;
                        }
                    return n;
                };
                int fewest = INT_MAX;
                std::sort(order.begin(), order.end());
                do {
                    const int n = crossings();
                    if (n < fewest) { fewest = n; best = order; }
                } while (fewest > 0 && std::next_permutation(order.begin(), order.end()));
            }
            // as far apart as the pieces either side let them (they must not turn back)
            for (const float s : { spacing, spacing * 0.75f, spacing * 0.5f }) {
                bool fits = true;
                for (size_t i = 0; i < k && fits; i++) {
                    const Piece& c = pieces[members[i]];
                    const size_t pos = std::find(best.begin(), best.end(), trackOf[i]) - best.begin();
                    const float to = base + ((float)pos - (t - 1) * 0.5f) * s;
                    for (const float away : { farBefore(c), farAfter(c) })
                        if ((away - c.At) * (away - to) <= 0 || std::abs(away - to) < 1.0f) fits = false;
                }
                if (!fits) continue;
                for (size_t i = 0; i < k; i++) {
                    const Piece& c = pieces[members[i]];
                    const size_t pos = std::find(best.begin(), best.end(), trackOf[i]) - best.begin();
                    auto& v = shifts[routes[c.Route].Edge];
                    if (v.empty()) v.assign(routes[c.Route].Path.size() - 1, 0.0f);
                    v[c.Index] = base + ((float)pos - (t - 1) * 0.5f) * s - c.At;
                }
                return;
            }
        };

        // runs of pieces less than spacing apart, then those of a run that overlap
        std::vector<uint32_t> run;
        for (size_t i = 0; i < pieces.size();) {
            size_t j = i + 1;
            while (j < pieces.size() && pieces[j].Vertical == pieces[i].Vertical && pieces[j].At - pieces[j - 1].At < spacing - 1e-3f) j++;
            if (j - i >= 2) {
                run.clear();
                for (size_t m = i; m < j; m++) run.push_back((uint32_t)m);
                std::sort(run.begin(), run.end(), [&](uint32_t a, uint32_t b) { return pieces[a].Lo < pieces[b].Lo; });
                for (size_t m = 0; m < run.size();) {
                    float hi = pieces[run[m]].Hi;
                    size_t n = m + 1;
                    while (n < run.size() && pieces[run[n]].Lo < hi - 1e-3f) hi = (std::max)(hi, pieces[run[n++]].Hi);
                    if (n - m >= 2) {
                        members.assign(run.begin() + m, run.begin() + n);
                        spread();
                    }
                    m = n;
                }
            }
            i = j;
        }
        return shifts;
    }

    bool IsSelfLoop(const Edge& e, const Node& from, const Node& to) {
        return &from == &to && !(FindPort(from, e.FromPort) && FindPort(to, e.ToPort));
    }

    std::vector<Point> SelfLoopPath(const Node& n, float reach) {
        const float rise = (std::min)(n.Height * 0.25f, 40.0f);
        const float right = n.X + n.Width * 0.5f + reach;
        const float end = n.X + NodeHalfWidthAt(n, rise);     // on the outline of the shape
        return { { end, n.Y - rise }, { right, n.Y - rise }, { right, n.Y + rise }, { end, n.Y + rise } };
    }

    std::vector<Point> CurveControls(const std::vector<Point>& p, Point startOut, Point endOut) {
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
        // out of a port: straight along its way out, as far as a third of the piece (at least a little)
        auto reach = [&](const Point& a, const Point& b) {
            return (std::max)(std::sqrt((b.X - a.X) * (b.X - a.X) + (b.Y - a.Y) * (b.Y - a.Y)) / 3, 30.0f);
        };
        if (startOut.X != 0 || startOut.Y != 0) {
            const float d = reach(p[0], p[1]);
            controls[0] = { p[0].X + startOut.X * d, p[0].Y + startOut.Y * d };
        }
        if (endOut.X != 0 || endOut.Y != 0) {
            const float d = reach(p[n - 2], p[n - 1]);
            controls.back() = { p[n - 1].X + endOut.X * d, p[n - 1].Y + endOut.Y * d };
        }
        return controls;
    }

    float EdgeLabelWidth(const std::wstring& text) {
        // measured once for each text (the labels of a graph repeat), in the renderer's font for them
        static ComPtr<IDWriteFactory> factory;
        static ComPtr<IDWriteTextFormat> format;
        static bool tried = false;
        static std::unordered_map<std::wstring, float> widths;
        if (!tried) {
            tried = true;
            if (SUCCEEDED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf()))))
                factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    11.0f, L"en-US", format.GetAddressOf());
        }
        if (auto it = widths.find(text); it != widths.end())
            return it->second;
        float width = k_EdgeLabelMaxWidth;      // (unmeasured: the most)
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (format && SUCCEEDED(factory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), 1e6f, 100.0f, layout.GetAddressOf())) &&
            SUCCEEDED(layout->GetMetrics(&metrics)))
            width = std::min(std::ceil(metrics.widthIncludingTrailingWhitespace) + k_EdgeLabelPadding * 2, k_EdgeLabelMaxWidth);
        if (widths.size() >= 4096)      // (a graph whose labels are all different: not kept forever)
            widths.clear();
        widths.emplace(text, width);
        return width;
    }

    std::vector<Point> FlattenEdgeShape(const EdgeShape& shape, int steps) {
        const auto& path = shape.Path;
        const auto& controls = shape.Controls;
        if (controls.empty() || path.size() < 2)
            return path;
        std::vector<Point> flat;
        flat.reserve((path.size() - 1) * steps + 1);
        flat.push_back(path[0]);
        for (size_t i = 0; i + 1 < path.size(); i++) {
            const Point &p0 = path[i], &c1 = controls[i * 2], &c2 = controls[i * 2 + 1], &p1 = path[i + 1];
            for (int s = 1; s <= steps; s++) {
                const float t = (float)s / steps, u = 1 - t;
                flat.push_back({ u * u * u * p0.X + 3 * u * u * t * c1.X + 3 * u * t * t * c2.X + t * t * t * p1.X,
                                 u * u * u * p0.Y + 3 * u * u * t * c1.Y + 3 * u * t * t * c2.Y + t * t * t * p1.Y });
            }
        }
        return flat;
    }

    std::vector<Point> PlaceLabels(const std::vector<LabelRequest>& labels, const std::vector<Rect>& obstacles) {
        // what is placed (the obstacles, then the labels), in a grid of cells for finding what is near a box
        constexpr float cell = 128.0f;
        std::vector<Rect> placed;
        placed.reserve(obstacles.size() + labels.size());
        std::vector<uint32_t> seen;     // the query a box was last counted in (each once, in however many cells)
        seen.reserve(obstacles.size() + labels.size());
        uint32_t query = 0;
        std::unordered_map<int64_t, std::vector<uint32_t>> grid;
        auto forCells = [&](const Rect& r, auto&& f) {
            const int x0 = (int)std::floor(r.Left / cell), x1 = (int)std::floor(r.Right / cell);
            const int y0 = (int)std::floor(r.Top / cell), y1 = (int)std::floor(r.Bottom / cell);
            for (int x = x0; x <= x1; x++)
                for (int y = y0; y <= y1; y++)
                    f((int64_t)x << 32 | (uint32_t)y);
        };
        auto add = [&](const Rect& r) {
            if (r.IsEmpty()) return;
            const uint32_t index = (uint32_t)placed.size();
            placed.push_back(r);
            seen.push_back(0);
            forCells(r, [&](int64_t key) { grid[key].push_back(index); });
        };
        auto overlap = [&](const Rect& r) {
            float area = 0;
            ++query;
            forCells(r, [&](int64_t key) {
                auto it = grid.find(key);
                if (it == grid.end()) return;
                for (uint32_t i : it->second) {
                    if (seen[i] == query) continue;
                    seen[i] = query;
                    const Rect& o = placed[i];
                    const float w = std::min(r.Right, o.Right) - std::max(r.Left, o.Left);
                    const float h = std::min(r.Bottom, o.Bottom) - std::max(r.Top, o.Top);
                    if (w > 0 && h > 0) area += w * h;
                }
            });
            return area;
        };
        for (const auto& r : obstacles)
            add(r);

        std::vector<Point> centers;
        centers.reserve(labels.size());
        std::vector<float> lengths;     // along the line, to each of its points
        for (const auto& label : labels) {
            const float w = label.Width, h = label.Height;
            auto box = [&](Point c) { return Rect{ c.X - w * 0.5f, c.Y - h * 0.5f, c.X + w * 0.5f, c.Y + h * 0.5f }; };
            Point best = label.Preferred;
            float bestArea = FLT_MAX;
            auto fits = [&](Point c) {      // the best yet: kept; overlapping nothing (a little apart): taken
                const Rect b = box(c);
                const float area = overlap({ b.Left - 2, b.Top - 2, b.Right + 2, b.Bottom + 2 });
                if (area < bestArea) { bestArea = area; best = c; }
                return area <= 0;
            };

            if (!fits(label.Preferred) && label.Line.size() >= 2) {
                const auto& line = label.Line;
                lengths.clear();
                lengths.reserve(line.size());
                lengths.push_back(0);
                for (size_t i = 1; i < line.size(); i++)
                    lengths.push_back(lengths.back() + std::hypot(line[i].X - line[i - 1].X, line[i].Y - line[i - 1].Y));
                // the point a part of the way along, and the line's way there
                auto along = [&](float part, Point& dir) {
                    const float at = lengths.back() * part;
                    size_t i = 1;
                    while (i + 1 < line.size() && lengths[i] < at) i++;
                    const float piece = lengths[i] - lengths[i - 1];
                    const float t = piece > 0 ? (at - lengths[i - 1]) / piece : 0;
                    const Point& a = line[i - 1];
                    const Point& b = line[i];
                    dir = piece > 0 ? Point{ (b.X - a.X) / piece, (b.Y - a.Y) / piece } : Point{ 1, 0 };
                    return Point{ a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t };
                };
                bool done = false;
                Point dir;
                for (float part : { 0.4f, 0.6f, 0.3f, 0.7f, 0.2f, 0.8f })
                    if ((done = fits(along(part, dir)))) break;
                // beside the line, just clear of it, then a label further out
                for (int lane = 0; lane < 2 && !done; lane++)
                    for (float part : { 0.5f, 0.4f, 0.6f, 0.3f, 0.7f }) {
                        const Point p = along(part, dir);
                        const Point n{ -dir.Y, dir.X };
                        const float across = std::fabs(n.X) * w + std::fabs(n.Y) * h;     // the label's size that way
                        const float d = across * (0.5f + lane) + 3.0f * (lane + 1);
                        if ((done = fits({ p.X + n.X * d, p.Y + n.Y * d }) || fits({ p.X - n.X * d, p.Y - n.Y * d }))) break;
                    }
            }
            add(box(best));
            centers.push_back(best);
        }
        return centers;
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
