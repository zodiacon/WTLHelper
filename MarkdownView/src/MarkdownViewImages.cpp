// The markdown view's images: local files and data: URLs decoded on a worker thread (WIC, or kept as text for an
// SVG, which Direct2D draws), other sources asked of the app (MDVN_GETIMAGE, SetImage), and the inline object that
// draws one in a text layout.

#include "MarkdownViewData.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <thread>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

MdvPaintContext g_mdvPaint;

namespace {

constexpr UINT MaxPixels = 4096;               // a bigger image is scaled down to this
constexpr size_t MaxBytes = 64 * 1024 * 1024;

bool ReadFileBytes(const std::wstring& path, std::string& bytes) {
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size{};
	bool ok = ::GetFileSizeEx(file, &size) && size.QuadPart <= (LONGLONG)MaxBytes;
	if (ok) {
		bytes.resize((size_t)size.QuadPart);
		DWORD read = 0;
		ok = ::ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr) != 0;
		bytes.resize(read);
	}
	::CloseHandle(file);
	return ok;
}

bool ReadStream(IStream* stream, std::string& bytes) {
	bytes.clear();
	char buf[64 * 1024];
	for (;;) {
		ULONG read = 0;
		const HRESULT hr = stream->Read(buf, sizeof(buf), &read);
		if (FAILED(hr))
			return false;
		bytes.append(buf, read);
		if (read == 0 || hr == S_FALSE || bytes.size() > MaxBytes)
			break;
	}
	return !bytes.empty() && bytes.size() <= MaxBytes;
}

MdvImageResult Decode(IWICImagingFactory* wic, MdvImageJob& job) {
	MdvImageResult r;
	r.key = job.key;
	if (!job.path.empty() && !ReadFileBytes(job.path, job.bytes))
		return r;
	if (job.bytes.empty())
		return r;
	if (MdvIsSvg(job.bytes)) {
		r.svg = true;
		if (!MdvSvgSize(job.bytes, r.width, r.height)) {
			r.width = 300;           // as a browser sizes one with no size
			r.height = 150;
		}
		r.svgTexts = MdvSvgTexts(job.bytes);
		r.svgText = std::move(job.bytes);
		r.ok = true;
		return r;
	}
	if (!wic)
		return r;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
		FAILED(stream->InitializeFromMemory((BYTE*)job.bytes.data(), (DWORD)job.bytes.size())) ||
		FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) ||
		FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
		return r;
	UINT w = 0, h = 0;
	frame->GetSize(&w, &h);
	if (w == 0 || h == 0)
		return r;
	r.width = (float)w;
	r.height = (float)h;
	ComPtr<IWICBitmapSource> source = frame;
	if (w > MaxPixels || h > MaxPixels) {
		const float f = std::min((float)MaxPixels / w, (float)MaxPixels / h);
		const UINT sw = std::max(1u, (UINT)(w * f)), sh = std::max(1u, (UINT)(h * f));
		ComPtr<IWICBitmapScaler> scaler;
		if (FAILED(wic->CreateBitmapScaler(scaler.GetAddressOf())) ||
			FAILED(scaler->Initialize(frame.Get(), sw, sh, WICBitmapInterpolationModeFant)))
			return r;
		source = scaler;
		w = sw;
		h = sh;
	}
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf())) ||
		FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut)))
		return r;
	r.pixels.resize((size_t)w * h * 4);
	if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)r.pixels.size(), r.pixels.data()))) {
		r.pixels.clear();
		return r;
	}
	r.pixelWidth = w;
	r.pixelHeight = h;
	r.ok = true;
	return r;
}

void Worker(std::shared_ptr<MdvLoader> loader) {
	const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	ComPtr<IWICImagingFactory> wic;
	::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
	for (;;) {
		MdvImageJob job;
		{
			std::unique_lock lock(loader->lock);
			loader->wake.wait(lock, [&] { return loader->stop || !loader->jobs.empty(); });
			if (loader->stop)
				break;
			job = std::move(loader->jobs.front());
			loader->jobs.pop_front();
		}
		MdvImageResult result = Decode(wic.Get(), job);
		HWND hwnd;
		{
			std::lock_guard lock(loader->lock);
			if (loader->stop)
				break;
			loader->done.push_back(std::move(result));
			hwnd = loader->hwnd;
		}
		::PostMessageW(hwnd, CMarkdownViewCtrl::WM_MDV_IMAGES, 0, 0);
	}
	wic.Reset();
	if (SUCCEEDED(co))
		::CoUninitialize();
}

// a bitmap the app gave: copied as premultiplied BGRA
bool CopyBitmap(HBITMAP hbm, MdvImageEntry& entry) {
	BITMAP bm{};
	if (!::GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight == 0)
		return false;
	const UINT w = bm.bmWidth, h = (UINT)std::abs(bm.bmHeight);
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
	bi.bmiHeader.biWidth = (LONG)w;
	bi.bmiHeader.biHeight = -(LONG)h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	std::vector<BYTE> pixels((size_t)w * h * 4);
	HDC dc = ::GetDC(nullptr);
	const int lines = ::GetDIBits(dc, hbm, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
	::ReleaseDC(nullptr, dc);
	if (lines == 0)
		return false;
	// a bitmap with no alpha is opaque; one with it is premultiplied here
	bool alpha = false;
	for (size_t i = 3; i < pixels.size() && !alpha; i += 4)
		alpha = pixels[i] != 0;
	for (size_t i = 0; i < pixels.size(); i += 4) {
		if (!alpha)
			pixels[i + 3] = 255;
		else {
			const UINT a = pixels[i + 3];
			pixels[i] = (BYTE)(pixels[i] * a / 255);
			pixels[i + 1] = (BYTE)(pixels[i + 1] * a / 255);
			pixels[i + 2] = (BYTE)(pixels[i + 2] * a / 255);
		}
	}
	entry.pixels = std::move(pixels);
	entry.pixelWidth = w;
	entry.pixelHeight = h;
	entry.width = (float)w;
	entry.height = (float)h;
	entry.state = MdvImageEntry::Ready;
	return true;
}

}  // namespace

// ---- Requests --------------------------------------------------------------------------

std::wstring MdvImageKey(const MdvState& s, const std::wstring& src) {
	if (MdvClassifyUrl(src) == MdvUrlKind::File) {
		std::wstring path = MdvResolvePath(s.basePath, src);
		if (!path.empty())
			return path;
	}
	return src;
}

void MdvQueueImage(MdvState& s, HWND hwnd, MdvImageJob job) {
	if (!s.loader) {
		s.loader = std::make_shared<MdvLoader>();
		s.loader->hwnd = hwnd;
	}
	auto loader = s.loader;
	{
		std::lock_guard lock(loader->lock);
		loader->jobs.push_back(std::move(job));
		if (!loader->running) {
			loader->running = true;
			std::thread(Worker, loader).detach();
		}
	}
	loader->wake.notify_one();
}

void MdvStopLoader(MdvState& s) {
	if (!s.loader)
		return;
	{
		std::lock_guard lock(s.loader->lock);
		s.loader->stop = true;
		s.loader->hwnd = nullptr;
		s.loader->jobs.clear();
	}
	s.loader->wake.notify_all();
	s.loader.reset();
}

std::shared_ptr<MdvImageEntry> MdvRequestImage(MdvState& s, HWND hwnd, const std::wstring& src) {
	const std::wstring key = MdvImageKey(s, src);
	auto it = s.images.find(key);
	if (it != s.images.end())
		return it->second;
	auto entry = std::make_shared<MdvImageEntry>();
	s.images[key] = entry;

	std::string bytes;
	std::wstring mime;
	if (MdvDecodeDataUrl(src, bytes, mime)) {
		if (mime.compare(0, 6, L"image/") != 0 || bytes.empty())
			entry->state = MdvImageEntry::Failed;
		else
			MdvQueueImage(s, hwnd, { key, L"", std::move(bytes) });
		return entry;
	}
	const MdvUrlKind kind = MdvClassifyUrl(src);
	if (kind == MdvUrlKind::Unsafe || kind == MdvUrlKind::Anchor || kind == MdvUrlKind::Mail || src.empty()) {
		entry->state = MdvImageEntry::Failed;
		return entry;
	}
	if (key != src) {
		// a local file
		MdvQueueImage(s, hwnd, { key, key, {} });
		return entry;
	}
	// the app's: now, later or never
	entry->state = MdvImageEntry::Waiting;
	NMMDVIMAGE nm{};
	nm.hdr = { hwnd, (UINT_PTR)::GetDlgCtrlID(hwnd), MDVN_GETIMAGE };
	nm.pszUrl = src.c_str();
	::SendMessageW(::GetParent(hwnd), WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
	if (nm.pStream) {
		if (ReadStream(nm.pStream, bytes)) {
			entry->state = MdvImageEntry::Loading;
			MdvQueueImage(s, hwnd, { key, L"", std::move(bytes) });
		}
		else
			entry->state = MdvImageEntry::Failed;
		nm.pStream->Release();
	}
	else if (nm.hBitmap && !CopyBitmap(nm.hBitmap, *entry))
		entry->state = MdvImageEntry::Failed;
	return entry;
}

std::vector<uint64_t> MdvTakeLoadedImages(MdvState& s) {
	std::vector<uint64_t> keys;
	if (!s.loader)
		return keys;
	std::vector<MdvImageResult> done;
	{
		std::lock_guard lock(s.loader->lock);
		done.swap(s.loader->done);
	}
	for (auto& r : done) {
		auto it = s.images.find(r.key);
		if (it == s.images.end())
			continue;               // from an earlier text
		auto& e = *it->second;
		if (!r.ok)
			e.state = MdvImageEntry::Failed;
		else {
			e.state = MdvImageEntry::Ready;
			e.width = r.width;
			e.height = r.height;
			e.svg = std::move(r.svgText);
			e.svgTexts = std::move(r.svgTexts);
			e.pixels = std::move(r.pixels);
			e.pixelWidth = r.pixelWidth;
			e.pixelHeight = r.pixelHeight;
			e.bitmap.Reset();
			e.svgDocument.Reset();
		}
		keys.insert(keys.end(), e.users.begin(), e.users.end());
	}
	return keys;
}

void CMarkdownViewCtrl::SetImage(LPCWSTR pszUrl, IStream* pStream) {
	auto& s = *m_State;
	const std::wstring src = pszUrl ? pszUrl : L"";
	const std::wstring key = MdvImageKey(s, src);
	auto& entry = s.images[key];
	if (!entry)
		entry = std::make_shared<MdvImageEntry>();
	std::string bytes;
	if (pStream && ReadStream(pStream, bytes)) {
		entry->state = MdvImageEntry::Loading;
		MdvQueueImage(s, m_hWnd, { key, L"", std::move(bytes) });
		return;
	}
	entry->state = MdvImageEntry::Failed;
	ImagesChanged({ entry->users.begin(), entry->users.end() });
}

LRESULT CMarkdownViewCtrl::OnImages(UINT, WPARAM, LPARAM, BOOL&) {
	ImagesChanged(MdvTakeLoadedImages(*m_State));
	return 0;
}

// the layouts showing images that changed are made again, keeping the view where it is
void CMarkdownViewCtrl::ImagesChanged(const std::vector<uint64_t>& keys) {
	auto& s = *m_State;
	if (keys.empty())
		return;
	std::unordered_set<uint64_t> set(keys.begin(), keys.end());
	bool any = false;
	for (auto& box : s.flow.boxes) {
		const uint64_t key = MdvState::Key(box.block, box.kind);
		if (set.count(key)) {
			s.layouts.erase(key);
			box.measured = false;
			any = true;
		}
	}
	if (!any)
		return;
	MeasureVisible();
	ScrollTo(s.scrollY, false);
	UpdateScrollBar();
	Invalidate();
}

// ---- The inline object ---------------------------------------------------------------------

namespace {

class ImageObject : public IDWriteInlineObject {
public:
	ImageObject(std::shared_ptr<MdvImageEntry> entry, float width, float height, std::wstring alt)
		: m_Entry(std::move(entry)), m_Width(width), m_Height(height), m_Alt(std::move(alt)) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) {
			*ppv = static_cast<IDWriteInlineObject*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++m_Refs; }
	ULONG STDMETHODCALLTYPE Release() override {
		const ULONG refs = --m_Refs;
		if (refs == 0)
			delete this;
		return refs;
	}

	HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL, IUnknown*) override {
		auto* rt = g_mdvPaint.target;
		if (!rt)
			return S_OK;
		const D2D1_RECT_F rc = D2D1::RectF(x, y, x + m_Width, y + m_Height);
		auto& e = *m_Entry;
		if (e.state == MdvImageEntry::Ready) {
			if (!e.svg.empty()) {
				ComPtr<ID2D1DeviceContext5> dc;
				if (SUCCEEDED(rt->QueryInterface(IID_PPV_ARGS(dc.GetAddressOf())))) {
					if (!e.svgDocument) {
						ComPtr<IStream> stream;
						stream.Attach(::SHCreateMemStream((const BYTE*)e.svg.data(), (UINT)e.svg.size()));
						if (stream)
							dc->CreateSvgDocument(stream.Get(), D2D1::SizeF(e.width, e.height), e.svgDocument.GetAddressOf());
					}
					if (e.svgDocument) {
						D2D1_MATRIX_3X2_F old;
						dc->GetTransform(&old);
						dc->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
						dc->SetTransform(D2D1::Matrix3x2F::Scale(m_Width / e.width, m_Height / e.height) * D2D1::Matrix3x2F::Translation(x, y) * old);
						dc->DrawSvgDocument(e.svgDocument.Get());
						dc->SetTransform(old);
						DrawTexts(rt, x, y);
						dc->PopAxisAlignedClip();
						return S_OK;
					}
				}
			}
			else if (!e.pixels.empty()) {
				if (!e.bitmap)
					rt->CreateBitmap(D2D1::SizeU(e.pixelWidth, e.pixelHeight), e.pixels.data(), e.pixelWidth * 4,
						D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), e.bitmap.GetAddressOf());
				if (e.bitmap) {
					rt->DrawBitmap(e.bitmap.Get(), rc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
					return S_OK;
				}
			}
		}
		// not there (yet): a frame, and a failed one's alternative text
		if (g_mdvPaint.frame)
			rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rc.left + 0.5f, rc.top + 0.5f, rc.right - 0.5f, rc.bottom - 0.5f), 2, 2), g_mdvPaint.frame);
		if (e.state == MdvImageEntry::Failed && !m_Alt.empty() && g_mdvPaint.altFormat && g_mdvPaint.altText) {
			rt->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_ALIASED);
			rt->DrawText(m_Alt.c_str(), (UINT32)m_Alt.size(), g_mdvPaint.altFormat, D2D1::RectF(rc.left + 4, rc.top, rc.right, rc.bottom), g_mdvPaint.altText);
			rt->PopAxisAlignedClip();
		}
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override {
		*metrics = { m_Width, m_Height, m_Height, FALSE };   // on the baseline
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override {
		*overhangs = {};
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override {
		*before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
		return S_OK;
	}

private:
	// the SVG's <text>s, scaled with it
	void DrawTexts(ID2D1RenderTarget* rt, float x, float y) {
		auto& e = *m_Entry;
		const float sx = m_Width / e.width, sy = m_Height / e.height;
		for (auto& t : e.svgTexts) {
			const float size = t.size * sy;
			if (size < 1)
				continue;
			ComPtr<IDWriteTextFormat> format;
			if (FAILED(MdvDWriteFactory()->CreateTextFormat(t.family.empty() ? L"Segoe UI" : t.family.c_str(), nullptr,
				t.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				size, L"en-us", format.GetAddressOf())))
				continue;
			ComPtr<IDWriteTextLayout> layout;
			if (FAILED(MdvDWriteFactory()->CreateTextLayout(t.text.c_str(), (UINT32)t.text.size(), format.Get(), 10000, 10000, layout.GetAddressOf())))
				continue;
			layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
			DWRITE_TEXT_METRICS tm{};
			layout->GetMetrics(&tm);
			DWRITE_LINE_METRICS lm{};
			UINT32 lines = 0;
			layout->GetLineMetrics(&lm, 1, &lines);
			float left = x + t.x * sx;
			if (t.anchor == 1)
				left -= tm.width / 2;
			else if (t.anchor == 2)
				left -= tm.width;
			ComPtr<ID2D1SolidColorBrush> brush;
			const COLORREF c = t.color;
			if (SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, t.opacity), brush.GetAddressOf())))
				rt->DrawTextLayout(D2D1::Point2F(left, y + t.y * sy - lm.baseline), layout.Get(), brush.Get());
		}
	}

	ULONG m_Refs = 1;
	std::shared_ptr<MdvImageEntry> m_Entry;
	float m_Width, m_Height;
	std::wstring m_Alt;
};

}  // namespace

IDWriteInlineObject* MdvCreateImageObject(std::shared_ptr<MdvImageEntry> entry, float width, float height, const std::wstring& alt) {
	return new ImageObject(std::move(entry), width, height, alt);
}
