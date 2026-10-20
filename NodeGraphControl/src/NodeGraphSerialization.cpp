#include "NodeGraphSerialization.h"
#include <atomic>
#include <algorithm>
#include <cstdio>

namespace NodeGraphCtrl {

    namespace Serialization {

        namespace {
            enum NodeFlags : uint32_t {     // version 2 had only Code (as 0 or 1)
                NF_CODE       = 1,
                NF_SCALELABEL = 2,
                NF_OWNERDRAW  = 4,
                NF_SHAPESHIFT = 3,          // bits 3-4: the NodeShape
                NF_SHAPEMASK  = 3 << NF_SHAPESHIFT,
            };
            enum EdgeFlags : uint8_t {      // version 5
                EF_CURVED     = 1,
                EF_SCALELABEL = 2,
                EF_ELBOW      = 4,          // (read as straight before it)
            };

            // the UserData block of a record (version 4)
            template<typename T, typename Save>
            void WriteUserData(Writer& w, const T& item, IUserDataSerializer* serializer, Save save) {
                w.WriteBytes(serializer ? (serializer->*save)(item) : std::vector<uint8_t>{});
            }
            template<typename T, typename Load>
            bool ReadUserData(Reader& r, T& item, IUserDataSerializer* serializer, Load load) {
                std::vector<uint8_t> data;
                if (!r.ReadBytes(data))
                    return false;
                if (serializer && !data.empty())
                    (serializer->*load)(item, data.data(), data.size());
                return true;
            }
        }

        uint64_t NewImageKey() {
            static std::atomic<uint64_t> s_NextKey = 1;
            return s_NextKey++;
        }

        void WriteNode(Writer& w, const Node& n, IUserDataSerializer* serializer) {
            w.Write(n.Id);
            w.Write(n.X); w.Write(n.Y);
            w.Write(n.Width); w.Write(n.Height);
            w.Write(n.Style.FillColor);
            w.Write(n.Style.BorderColor);
            w.Write(n.Style.TextColor);
            w.Write(n.Style.BorderWidth);
            w.Write(n.Style.CornerRadius);
            w.WriteStr(n.Label);
            w.Write((uint32_t)((n.Style.Code ? NF_CODE : 0) | (n.Style.ScaleLabel ? NF_SCALELABEL : 0) | (n.Style.OwnerDraw ? NF_OWNERDRAW : 0)
                | (uint32_t)n.Style.Shape << NF_SHAPESHIFT));
            w.WriteStr(n.Tooltip);

            const NodeContent& c = n.Content;
            w.Write(c.Icon);
            w.Write((uint8_t)c.IconPos);
            w.Write(c.IconSize);
            w.Write(c.Image);
            w.Write((uint8_t)c.Fit);
            w.Write((uint8_t)c.LabelPos);
            w.WriteStr(c.Title);
            w.Write(c.TitleFill);
            WriteUserData(w, n, serializer, &IUserDataSerializer::SaveNode);

            w.Write((uint32_t)n.Ports.size());
            for (const auto& p : n.Ports) {
                w.Write(p.Id);
                w.WriteStr(p.Name);
                w.Write((uint8_t)p.Side);
                w.Write(p.Offset);
                w.Write((uint8_t)p.Direction);
                w.Write(p.Color);
            }
        }

        bool ReadNode(Reader& r, Node& n, uint32_t version, IUserDataSerializer* serializer) {
            n = Node{};
            bool ok = r.Read(n.Id)
                && r.Read(n.X) && r.Read(n.Y)
                && r.Read(n.Width) && r.Read(n.Height)
                && r.Read(n.Style.FillColor)
                && r.Read(n.Style.BorderColor)
                && r.Read(n.Style.TextColor)
                && r.Read(n.Style.BorderWidth)
                && r.Read(n.Style.CornerRadius)
                && r.ReadStr(n.Label);
            if (!ok || version < 2)
                return ok;

            uint32_t flags = 0;
            ok = r.Read(flags) && r.ReadStr(n.Tooltip);
            n.Style.Code = (flags & NF_CODE) != 0;
            if (!ok || version < 3)
                return ok;      // ScaleLabel and OwnerDraw keep their defaults
            n.Style.ScaleLabel = (flags & NF_SCALELABEL) != 0;
            n.Style.OwnerDraw = (flags & NF_OWNERDRAW) != 0;
            const uint32_t shape = (flags & NF_SHAPEMASK) >> NF_SHAPESHIFT;
            if (shape > (uint32_t)NodeShape::Diamond)
                return false;
            n.Style.Shape = (NodeShape)shape;

            NodeContent& c = n.Content;
            uint8_t iconPos = 0, fit = 0, labelPos = 0;
            ok = r.Read(c.Icon) && r.Read(iconPos) && r.Read(c.IconSize)
                && r.Read(c.Image) && r.Read(fit) && r.Read(labelPos)
                && r.ReadStr(c.Title) && r.Read(c.TitleFill)
                && iconPos <= (uint8_t)IconPlacement::Right
                && fit <= (uint8_t)ImageFit::Center
                && labelPos <= (uint8_t)LabelPlacement::Hidden;
            c.IconPos = (IconPlacement)iconPos;
            c.Fit = (ImageFit)fit;
            c.LabelPos = (LabelPlacement)labelPos;
            if (ok && version >= 4)
                ok = ReadUserData(r, n, serializer, &IUserDataSerializer::LoadNode);
            if (!ok || version < 7)
                return ok;

            uint32_t count = 0;
            ok = r.Read(count) && count <= (r.Size - r.Pos) / MinPortRecord;
            if (ok) n.Ports.reserve(count);
            for (uint32_t i = 0; i < count && ok; ++i) {
                Port p;
                uint8_t side = 0, direction = 0;
                ok = r.Read(p.Id) && r.ReadStr(p.Name) && r.Read(side) && r.Read(p.Offset) && r.Read(direction) && r.Read(p.Color)
                    && side <= (uint8_t)PortSide::Bottom && direction <= (uint8_t)PortDirection::Both;
                p.Side = (PortSide)side;
                p.Direction = (PortDirection)direction;
                n.Ports.push_back(std::move(p));
            }
            return ok;
        }

        void WriteEdge(Writer& w, const Edge& e, IUserDataSerializer* serializer) {
            w.Write(e.Id);
            w.Write(e.From); w.Write(e.To);
            w.Write(e.Style.Color);
            w.Write(e.Style.Width);
            w.Write((uint32_t)(e.Style.Directed ? 1 : 0));
            w.WriteStr(e.Label);
            w.WriteStr(e.Tooltip);
            w.Write((uint32_t)e.Waypoints.size());
            for (const auto& p : e.Waypoints) {
                w.Write(p.X); w.Write(p.Y);
            }
            w.Write((uint8_t)e.Style.Line);
            w.Write((uint8_t)e.Style.Arrow);
            w.Write((uint8_t)((e.Style.Curved ? EF_CURVED : 0) | (e.Style.ScaleLabel ? EF_SCALELABEL : 0) | (e.Style.Elbow ? EF_ELBOW : 0)));
            WriteUserData(w, e, serializer, &IUserDataSerializer::SaveEdge);
            w.Write(e.FromPort);
            w.Write(e.ToPort);
            w.Write(e.Style.CornerRadius);
        }

        bool ReadEdge(Reader& r, Edge& e, uint32_t version, IUserDataSerializer* serializer) {
            e = Edge{};
            uint32_t directed = 1;
            bool ok = r.Read(e.Id)
                && r.Read(e.From) && r.Read(e.To)
                && r.Read(e.Style.Color)
                && r.Read(e.Style.Width)
                && r.Read(directed)
                && r.ReadStr(e.Label);
            e.Style.Directed = directed != 0;
            if (!ok || version < 2)
                return ok;

            uint32_t count = 0;
            ok = r.ReadStr(e.Tooltip) && r.Read(count) && count <= (r.Size - r.Pos) / (2 * sizeof(float));
            if (ok) e.Waypoints.reserve(count);
            for (uint32_t i = 0; i < count && ok; ++i) {
                Point p;
                ok = r.Read(p.X) && r.Read(p.Y);
                e.Waypoints.push_back(p);
            }
            if (ok && version >= 5) {
                uint8_t line = 0, arrow = 0, flags = 0;
                ok = r.Read(line) && r.Read(arrow) && r.Read(flags)
                    && line <= (uint8_t)EdgeLine::DashDot && arrow <= (uint8_t)ArrowHead::Circle;
                e.Style.Line = (EdgeLine)line;
                e.Style.Arrow = (ArrowHead)arrow;
                e.Style.Curved = (flags & EF_CURVED) != 0;
                e.Style.ScaleLabel = (flags & EF_SCALELABEL) != 0;
                e.Style.Elbow = (flags & EF_ELBOW) != 0;
            }
            if (ok && version >= 4)
                ok = ReadUserData(r, e, serializer, &IUserDataSerializer::LoadEdge);
            if (ok && version >= 7)
                ok = r.Read(e.FromPort) && r.Read(e.ToPort);
            if (ok && version >= 8)
                ok = r.Read(e.Style.CornerRadius) && e.Style.CornerRadius >= 0 && e.Style.CornerRadius <= 1000;
            return ok;
        }

        void WriteGroup(Writer& w, const Group& g) {
            w.Write(g.Id);
            w.WriteStr(g.Title);
            w.Write((uint8_t)(g.Collapsed ? 1 : 0));
            w.Write(g.FillColor);
            w.Write(g.BorderColor);
            w.Write(g.TextColor);
            w.Write((uint32_t)g.Nodes.size());
            for (NodeId n : g.Nodes)
                w.Write(n);
        }

        bool ReadGroup(Reader& r, Group& g) {
            g = Group{};
            uint8_t collapsed = 0;
            uint32_t count = 0;
            bool ok = r.Read(g.Id) && r.ReadStr(g.Title) && r.Read(collapsed)
                && r.Read(g.FillColor) && r.Read(g.BorderColor) && r.Read(g.TextColor)
                && r.Read(count) && count <= (r.Size - r.Pos) / sizeof(NodeId) && g.Id != InvalidGroup;
            g.Collapsed = collapsed != 0;
            if (ok) g.Nodes.reserve(count);
            for (uint32_t i = 0; i < count && ok; ++i) {
                NodeId n;
                ok = r.Read(n);
                g.Nodes.push_back(n);
            }
            return ok;
        }

        void WriteImage(Writer& w, const Image& image) {
            w.Write(image.Id);
            w.Write(image.Width);
            w.Write(image.Height);
            w.WriteBytes(image.Data);
        }

        bool ReadImage(Reader& r, Image& image) {
            image = Image{};
            return r.Read(image.Id) && r.Read(image.Width) && r.Read(image.Height) && r.ReadBytes(image.Data)
                && image.Id != InvalidImage && !image.Data.empty();
        }

    } // namespace Serialization

    // ---- NodeGraphModel: the graph file -------------------------------------------
    // magic "GCF" + version byte; version 3: the images; version 1 on: the nodes, the edges, the next node and edge ids;
    // version 3: the next image id.

    namespace {
        constexpr uint32_t k_Magic = 0x00464347u;   // 'G','C','F', then the version
    }

    bool NodeGraphModel::Save(const wchar_t* path) const {
        using namespace Serialization;
        Writer w;
        w.Write(k_Magic | Version << 24);
        // the images the nodes show (one left over, as after undoing a paste, isn't written)
        std::vector<const Image*> images;
        images.reserve(m_Images.size());
        for (const auto& image : m_Images)
            if (std::any_of(m_Nodes.begin(), m_Nodes.end(), [&](const Node& n) { return n.Content.Icon == image.Id || n.Content.Image == image.Id; }))
                images.push_back(&image);
        w.Write((uint32_t)images.size());
        for (const Image* image : images)
            WriteImage(w, *image);
        w.Write((uint32_t)m_Nodes.size());
        for (const auto& n : m_Nodes)
            WriteNode(w, n, m_Serializer);
        w.Write((uint32_t)m_Edges.size());
        for (const auto& e : m_Edges)
            WriteEdge(w, e, m_Serializer);
        w.Write((uint32_t)m_Groups.size());
        for (const auto& g : m_Groups)
            WriteGroup(w, g);
        w.Write(m_NextNodeId);
        w.Write(m_NextEdgeId);
        w.Write(m_NextImageId);
        w.Write(m_NextGroupId);

        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"wb") != 0 || !f)
            return false;
        bool ok = fwrite(w.Data.data(), 1, w.Data.size(), f) == w.Data.size();
        ok = fclose(f) == 0 && ok;
        return ok;
    }

    bool NodeGraphModel::Load(const wchar_t* path) {
        using namespace Serialization;

        std::vector<uint8_t> data;
        {
            FILE* f = nullptr;
            if (_wfopen_s(&f, path, L"rb") != 0 || !f)
                return false;
            const long long maxSize = 1ll << 30;
            long long size = -1;
            if (_fseeki64(f, 0, SEEK_END) == 0 && (size = _ftelli64(f)) > 0 && size <= maxSize && _fseeki64(f, 0, SEEK_SET) == 0) {
                data.resize((size_t)size);
                if (fread(data.data(), 1, data.size(), f) != data.size())
                    data.clear();
            }
            fclose(f);
        }

        Reader r{ data.data(), data.size() };
        uint32_t magic = 0;
        if (!r.Read(magic) || (magic & 0xFFFFFF) != k_Magic)
            return false;
        const uint32_t version = magic >> 24;
        if (version < 1 || version > Version)
            return false;

        uint32_t count = 0;
        bool ok = true;
        std::vector<Image> images;
        if (version >= 3) {
            ok = r.Read(count);
            if (ok) images.reserve(r.Capped(count, MinImageRecord));
            for (uint32_t i = 0; i < count && ok; ++i) {
                Image image;
                ok = ReadImage(r, image);
                image.Key = NewImageKey();
                images.push_back(std::move(image));
            }
        }

        std::vector<Node> nodes;
        ok = ok && r.Read(count);
        if (ok) nodes.reserve(r.Capped(count, MinNodeRecord));
        for (uint32_t i = 0; i < count && ok; ++i) {
            Node n;
            ok = ReadNode(r, n, version, m_Serializer);
            nodes.push_back(std::move(n));
        }

        std::vector<Edge> edges;
        ok = ok && r.Read(count);
        if (ok) edges.reserve(r.Capped(count, MinEdgeRecord));
        for (uint32_t i = 0; i < count && ok; ++i) {
            Edge e;
            ok = ReadEdge(r, e, version, m_Serializer);
            edges.push_back(std::move(e));
        }

        std::vector<Group> groups;
        if (ok && version >= 6) {
            ok = r.Read(count);
            if (ok) groups.reserve(r.Capped(count, MinGroupRecord));
            for (uint32_t i = 0; i < count && ok; ++i) {
                Group g;
                ok = ReadGroup(r, g);
                groups.push_back(std::move(g));
            }
        }

        uint32_t nextNode = 0, nextEdge = 0, nextImage = 0, nextGroup = 0;
        ok = ok && r.Read(nextNode) && r.Read(nextEdge);
        if (ok && version >= 3)
            ok = r.Read(nextImage);
        if (ok && version >= 6)
            ok = r.Read(nextGroup);
        if (!ok)
            return false;

        // ids handed out from now on must not be taken
        for (const auto& image : images)
            nextImage = (std::max)(nextImage, image.Id + 1);

        m_Images = std::move(images);
        m_Nodes = std::move(nodes);
        m_Edges = std::move(edges);
        NodesChanged();
        EdgesChanged();
        // the groups take their nodes (a node of a group that isn't there is left out)
        m_Groups.clear();
        m_GroupOf.clear();
        m_NextGroupId = nextGroup;
        for (const auto& g : groups)
            RestoreGroup(g);
        m_NextNodeId = nextNode;
        m_NextEdgeId = nextEdge;
        m_NextImageId = nextImage;
        Reset();
        return true;
    }

} // namespace NodeGraphCtrl
