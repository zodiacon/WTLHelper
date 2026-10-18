#pragma once
// WIC helpers shared by the model (adding images) and the renderer (making bitmaps of them). Internal to the library.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

namespace NodeGraphCtrl::Imaging {

    // Initializes COM on the thread for its lifetime, unless it already is (in any apartment).
    // WIC objects must not outlive it.
    struct ComScope {
        HRESULT Hr;
        ComScope() : Hr(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
        ~ComScope() { if (SUCCEEDED(Hr)) ::CoUninitialize(); }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    };

    HRESULT CreateFactory(IWICImagingFactory** factory);

    // The largest frame of the encoded image, as 32 bit premultiplied BGRA.
    HRESULT Decode(IWICImagingFactory* factory, const void* data, size_t size, IWICBitmapSource** result);

    HRESULT EncodePng(IWICImagingFactory* factory, IWICBitmapSource* source, std::vector<uint8_t>& png);

} // namespace NodeGraphCtrl::Imaging
