// The 3D renderer: Direct3D 11 with a Direct2D overlay.

#include "FunctionPlotRender3D.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

ID2D1Factory* FplD2DFactory();

namespace {

const char Shader[] = R"(
cbuffer Frame : register(b0) {
	row_major float4x4 viewProjection;
	float4 eye;
	float4 color;
	float4 options;		// x: colormap, y: unlit, z: clip to the cube
};

struct VSIn {
	float3 pos : POSITION;
	float3 normal : NORMAL;
	float value : VALUE;
};

struct VSOut {
	float4 pos : SV_POSITION;
	float3 world : WORLD;
	float3 normal : NORMAL;
	float value : VALUE;
};

VSOut VS(VSIn i) {
	VSOut o;
	o.pos = mul(float4(i.pos, 1), viewProjection);
	o.world = i.pos;
	o.normal = i.normal;
	o.value = i.value;
	return o;
}

// viridis, as a polynomial
float3 Viridis(float t) {
	const float3 c0 = float3(0.2777273272234177, 0.005407344544966578, 0.3340998053353061);
	const float3 c1 = float3(0.1050930431085774, 1.404613529898575, 1.384590162594685);
	const float3 c2 = float3(-0.3308618287255563, 0.214847559468213, 0.09509516302823659);
	const float3 c3 = float3(-4.634230498983486, -5.799100973351585, -19.33244095627987);
	const float3 c4 = float3(6.228269936347081, 14.17993336680509, 56.69055260068105);
	const float3 c5 = float3(4.776384997670288, -13.74514537774601, -65.35303263337234);
	const float3 c6 = float3(-5.435455855934631, 4.645852612178535, 26.3124352495832);
	return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

float4 PS(VSOut i) : SV_TARGET {
	if (options.z > 0 && any(abs(i.world) > 1.0005))
		discard;
	float3 base = options.x > 0 ? Viridis(saturate(i.value)) : color.rgb;
	if (options.y > 0)
		return float4(base, 1);
	// both sides lit: the normal turned toward the eye; a light a little above and beside it
	float3 v = normalize(eye.xyz - i.world);
	float3 n = normalize(i.normal);
	if (dot(n, v) < 0)
		n = -n;
	float3 l = normalize(v + float3(0.35, -0.25, 0.8));
	float diffuse = saturate(dot(n, l));
	float3 h = normalize(l + v);
	float specular = pow(saturate(dot(n, h)), 32) * 0.12;
	return float4(base * (0.32 + 0.68 * diffuse) + specular, 1);
}
)";

ComPtr<ID3DBlob> Compile(const char* entry, const char* target) {
	ComPtr<ID3DBlob> code, errors;
	if (FAILED(::D3DCompile(Shader, sizeof(Shader) - 1, "FunctionPlot", nullptr, nullptr, entry, target,
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
		if (errors)
			::OutputDebugStringA((const char*)errors->GetBufferPointer());
		return nullptr;
	}
	return code;
}

void SetColor(float out[4], COLORREF c) {
	out[0] = GetRValue(c) / 255.0f;
	out[1] = GetGValue(c) / 255.0f;
	out[2] = GetBValue(c) / 255.0f;
	out[3] = 1;
}

}  // namespace

bool FplRenderer3D::Init(HWND hwnd, UINT width, UINT height, float dpi) {
	Reset();
	m_Width = std::max(width, 1u);
	m_Height = std::max(height, 1u);
	m_Dpi = dpi;
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0 };
	const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, (UINT)std::size(levels),
		D3D11_SDK_VERSION, m_Device.GetAddressOf(), nullptr, m_Context.GetAddressOf());
	m_Warp = false;
	if (FAILED(hr)) {
		// no GPU to speak of (a VM, Remote Desktop): the software rasterizer
		hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, (UINT)std::size(levels),
			D3D11_SDK_VERSION, m_Device.GetAddressOf(), nullptr, m_Context.GetAddressOf());
		m_Warp = true;
	}
	if (FAILED(hr))
		return false;

	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<IDXGIFactory2> factory;
	if (FAILED(m_Device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) ||
		FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
		Reset();
		return false;
	}
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Width = m_Width;
	desc.Height = m_Height;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 2;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	if (FAILED(factory->CreateSwapChainForHwnd(m_Device.Get(), hwnd, &desc, nullptr, nullptr, m_SwapChain.GetAddressOf()))) {
		Reset();
		return false;
	}
	factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

	// the overlay's Direct2D device on the same DXGI device
	ComPtr<ID2D1Factory1> d2dFactory;
	if (FAILED(FplD2DFactory()->QueryInterface(IID_PPV_ARGS(d2dFactory.GetAddressOf()))) ||
		FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), m_D2DDevice.GetAddressOf())) ||
		FAILED(m_D2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_Overlay.GetAddressOf()))) {
		Reset();
		return false;
	}

	// shaders and states
	auto vs = Compile("VS", "vs_4_0"), ps = Compile("PS", "ps_4_0");
	if (!vs || !ps ||
		FAILED(m_Device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_VertexShader.GetAddressOf())) ||
		FAILED(m_Device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_PixelShader.GetAddressOf()))) {
		Reset();
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(FplVertex3, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(FplVertex3, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "VALUE", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(FplVertex3, value), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	D3D11_BUFFER_DESC cb{ sizeof(Frame), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
	D3D11_RASTERIZER_DESC raster{};
	raster.FillMode = D3D11_FILL_SOLID;
	raster.CullMode = D3D11_CULL_NONE;
	raster.DepthClipEnable = TRUE;
	raster.MultisampleEnable = TRUE;
	raster.AntialiasedLineEnable = TRUE;
	D3D11_DEPTH_STENCIL_DESC depth{};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(m_Device->CreateInputLayout(layout, (UINT)std::size(layout), vs->GetBufferPointer(), vs->GetBufferSize(),
			m_Layout.GetAddressOf())) ||
		FAILED(m_Device->CreateBuffer(&cb, nullptr, m_Constants.GetAddressOf())) ||
		FAILED(m_Device->CreateRasterizerState(&raster, m_Rasterizer.GetAddressOf())) ||
		FAILED(m_Device->CreateDepthStencilState(&depth, m_DepthState.GetAddressOf()))) {
		Reset();
		return false;
	}

	// 4x multisampling where the format allows it
	UINT quality = 0;
	m_Samples = SUCCEEDED(m_Device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &quality)) && quality > 0 ? 4 : 1;
	if (!CreateTargets()) {
		Reset();
		return false;
	}
	return true;
}

void FplRenderer3D::Reset() {
	ReleaseTargets();
	m_Overlay.Reset();
	m_D2DDevice.Reset();
	m_SwapChain.Reset();
	m_DepthState.Reset();
	m_Rasterizer.Reset();
	m_Constants.Reset();
	m_Layout.Reset();
	m_PixelShader.Reset();
	m_VertexShader.Reset();
	if (m_Context)
		m_Context->ClearState();
	m_Context.Reset();
	m_Device.Reset();
}

bool FplRenderer3D::CreateTargets() {
	D3D11_TEXTURE2D_DESC color{};
	color.Width = m_Width;
	color.Height = m_Height;
	color.MipLevels = 1;
	color.ArraySize = 1;
	color.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	color.SampleDesc.Count = m_Samples;
	color.Usage = D3D11_USAGE_DEFAULT;
	color.BindFlags = D3D11_BIND_RENDER_TARGET;
	D3D11_TEXTURE2D_DESC depth = color;
	depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(m_Device->CreateTexture2D(&color, nullptr, m_Color.GetAddressOf())) ||
		FAILED(m_Device->CreateRenderTargetView(m_Color.Get(), nullptr, m_ColorView.GetAddressOf())) ||
		FAILED(m_Device->CreateTexture2D(&depth, nullptr, m_Depth.GetAddressOf())) ||
		FAILED(m_Device->CreateDepthStencilView(m_Depth.Get(), nullptr, m_DepthView.GetAddressOf())))
		return false;
	// the overlay draws on the back buffer
	ComPtr<IDXGISurface> surface;
	if (FAILED(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()))))
		return false;
	auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), m_Dpi, m_Dpi);
	if (FAILED(m_Overlay->CreateBitmapFromDxgiSurface(surface.Get(), &props, m_OverlayTarget.GetAddressOf())))
		return false;
	m_Overlay->SetDpi(m_Dpi, m_Dpi);
	return true;
}

void FplRenderer3D::ReleaseTargets() {
	if (m_Overlay)
		m_Overlay->SetTarget(nullptr);
	m_OverlayTarget.Reset();
	m_ColorView.Reset();
	m_DepthView.Reset();
	m_Color.Reset();
	m_Depth.Reset();
	if (m_Context)
		m_Context->OMSetRenderTargets(0, nullptr, nullptr);
}

void FplRenderer3D::Resize(UINT width, UINT height, float dpi) {
	if (!m_Device)
		return;
	width = std::max(width, 1u);
	height = std::max(height, 1u);
	if (width == m_Width && height == m_Height && dpi == m_Dpi)
		return;
	m_Width = width;
	m_Height = height;
	m_Dpi = dpi;
	ReleaseTargets();
	m_Context->Flush();
	if (FAILED(m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)) || !CreateTargets())
		Reset();
}

FplGpuMesh FplRenderer3D::Create(const FplMesh& mesh) {
	FplGpuMesh gpu;
	if (!m_Device || mesh.Empty() || mesh.vertices.empty())
		return gpu;
	D3D11_BUFFER_DESC vb{ (UINT)(mesh.vertices.size() * sizeof(FplVertex3)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
	D3D11_BUFFER_DESC ib{ (UINT)(mesh.indices.size() * sizeof(uint32_t)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER };
	D3D11_SUBRESOURCE_DATA vdata{ mesh.vertices.data() }, idata{ mesh.indices.data() };
	if (FAILED(m_Device->CreateBuffer(&vb, &vdata, gpu.vertices.GetAddressOf())) ||
		FAILED(m_Device->CreateBuffer(&ib, &idata, gpu.indices.GetAddressOf())))
		return {};
	gpu.count = (UINT)mesh.indices.size();
	return gpu;
}

void FplRenderer3D::Begin(COLORREF back, const FplMatrix4& viewProjection, FplVec3 eye) {
	float clear[4];
	SetColor(clear, back);
	m_Context->OMSetRenderTargets(1, m_ColorView.GetAddressOf(), m_DepthView.Get());
	m_Context->ClearRenderTargetView(m_ColorView.Get(), clear);
	m_Context->ClearDepthStencilView(m_DepthView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
	D3D11_VIEWPORT vp{ 0, 0, (float)m_Width, (float)m_Height, 0, 1 };
	m_Context->RSSetViewports(1, &vp);
	m_Context->RSSetState(m_Rasterizer.Get());
	m_Context->OMSetDepthStencilState(m_DepthState.Get(), 0);
	m_Context->IASetInputLayout(m_Layout.Get());
	m_Context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
	m_Context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
	m_Context->VSSetConstantBuffers(0, 1, m_Constants.GetAddressOf());
	m_Context->PSSetConstantBuffers(0, 1, m_Constants.GetAddressOf());
	std::memcpy(m_Frame.viewProjection, viewProjection.m, sizeof(m_Frame.viewProjection));
	m_Frame.eye[0] = eye.x;
	m_Frame.eye[1] = eye.y;
	m_Frame.eye[2] = eye.z;
}

void FplRenderer3D::Draw(const FplGpuMesh& mesh, COLORREF color, bool colormap, bool lines, bool clip) {
	if (!mesh)
		return;
	SetColor(m_Frame.color, color);
	m_Frame.options[0] = colormap ? 1.0f : 0.0f;
	m_Frame.options[1] = lines ? 1.0f : 0.0f;
	m_Frame.options[2] = clip ? 1.0f : 0.0f;
	m_Context->UpdateSubresource(m_Constants.Get(), 0, nullptr, &m_Frame, 0, 0);
	UINT stride = sizeof(FplVertex3), offset = 0;
	m_Context->IASetVertexBuffers(0, 1, mesh.vertices.GetAddressOf(), &stride, &offset);
	m_Context->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_Context->IASetPrimitiveTopology(lines ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_Context->DrawIndexed(mesh.count, 0, 0);
}

ID2D1DeviceContext* FplRenderer3D::BeginOverlay() {
	// the scene into the back buffer
	ComPtr<ID3D11Texture2D> back;
	if (FAILED(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()))))
		return nullptr;
	if (m_Samples > 1)
		m_Context->ResolveSubresource(back.Get(), 0, m_Color.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	else
		m_Context->CopyResource(back.Get(), m_Color.Get());
	m_Context->OMSetRenderTargets(0, nullptr, nullptr);
	m_Overlay->SetTarget(m_OverlayTarget.Get());
	m_Overlay->BeginDraw();
	m_Overlay->SetTransform(D2D1::Matrix3x2F::Identity());
	return m_Overlay.Get();
}

void FplRenderer3D::EndOverlay() {
	m_Overlay->EndDraw();
	m_Overlay->SetTarget(nullptr);
}

bool FplRenderer3D::Present() {
	HRESULT hr = m_SwapChain->Present(1, 0);
	return !(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET);
}

bool FplRenderer3D::Capture(std::vector<uint32_t>& pixels, UINT& width, UINT& height) {
	if (!m_Device)
		return false;
	ComPtr<ID3D11Texture2D> back, staging;
	if (FAILED(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()))))
		return false;
	D3D11_TEXTURE2D_DESC desc;
	back->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	if (FAILED(m_Device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
		return false;
	m_Context->CopyResource(staging.Get(), back.Get());
	D3D11_MAPPED_SUBRESOURCE map;
	if (FAILED(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
		return false;
	width = desc.Width;
	height = desc.Height;
	pixels.resize((size_t)width * height);
	for (UINT y = 0; y < height; y++) {
		auto row = (const uint32_t*)((const BYTE*)map.pData + (size_t)y * map.RowPitch);
		for (UINT x = 0; x < width; x++)
			pixels[(size_t)y * width + x] = row[x] | 0xFF000000u;
	}
	m_Context->Unmap(staging.Get(), 0);
	return true;
}
