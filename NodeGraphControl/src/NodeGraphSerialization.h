#pragma once
// The binary records of nodes, edges and images, shared by the graph file (NodeGraphModel::Save/Load) and the
// clipboard. Internal to the library.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "../include/NodeGraphModel.h"

namespace NodeGraphCtrl::Serialization {

    // version 2 adds a node's Code style and tooltip, and an edge's tooltip and waypoints;
    // version 3 adds images, a node's content and its ScaleLabel and OwnerDraw styles
    constexpr uint32_t Version = 3;

    struct Writer {
        std::vector<uint8_t> Data;

        template<typename T>
        void Write(const T& v) {
            auto p = reinterpret_cast<const uint8_t*>(&v);
            Data.insert(Data.end(), p, p + sizeof(T));
        }
        void WriteStr(const std::wstring& s) {
            Write((uint32_t)s.size());
            auto p = reinterpret_cast<const uint8_t*>(s.data());
            Data.insert(Data.end(), p, p + s.size() * sizeof(wchar_t));
        }
        void WriteBytes(const std::vector<uint8_t>& bytes) {
            Write((uint32_t)bytes.size());
            Data.insert(Data.end(), bytes.begin(), bytes.end());
        }
    };

    // Every read checks the size, so a corrupt or short input fails instead of reading past it or allocating much.
    struct Reader {
        const uint8_t* Data;
        size_t         Size;
        size_t         Pos = 0;

        template<typename T>
        bool Read(T& v) {
            if (Size - Pos < sizeof(T)) return false;
            memcpy(&v, Data + Pos, sizeof(T));
            Pos += sizeof(T);
            return true;
        }
        bool ReadStr(std::wstring& s) {
            uint32_t n;
            if (!Read(n) || (Size - Pos) / sizeof(wchar_t) < n) return false;
            s.assign(reinterpret_cast<const wchar_t*>(Data + Pos), n);
            Pos += n * sizeof(wchar_t);
            return true;
        }
        bool ReadBytes(std::vector<uint8_t>& bytes) {
            uint32_t n;
            if (!Read(n) || Size - Pos < n) return false;
            bytes.assign(Data + Pos, Data + Pos + n);
            Pos += n;
            return true;
        }
        bool AtEnd() const { return Pos == Size; }
    };

    void WriteNode(Writer& w, const Node& n);
    bool ReadNode(Reader& r, Node& n, uint32_t version);
    void WriteEdge(Writer& w, const Edge& e);
    bool ReadEdge(Reader& r, Edge& e, uint32_t version);
    void WriteImage(Writer& w, const Image& image);     // the id, the size and the encoded bytes (not the key)
    bool ReadImage(Reader& r, Image& image);

    // a new Image::Key
    uint64_t NewImageKey();

} // namespace NodeGraphCtrl::Serialization
