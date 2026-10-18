#include "NodeGraphImaging.h"
#include "../include/NodeGraphModel.h"
#include <shlwapi.h>
#include <atomic>
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
        std::atomic<uint64_t> s_NextImageKey = 1;

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
        image.Key = s_NextImageKey++;
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

} // namespace NodeGraphCtrl
