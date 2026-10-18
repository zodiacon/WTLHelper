#include "NodeGraphImaging.h"
#include "NodeGraphSerialization.h"
#include <shlwapi.h>
#include <algorithm>
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

    const Image* NodeGraphModel::GetImage(ImageId id) const {
        for (const auto& image : m_Images) if (image.Id == id) return &image;
        return nullptr;
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
