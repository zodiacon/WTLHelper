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
	float4 options;		// x: colormap, y: unlit, z: clip to the cube, w: opacity
	float4 gridScale;	// x, y, z: lines at integers of (world + 1) * scale + offset; w: along the grid, per cells
	float4 gridOffset;
	float4 lineColor;	// w: use it, else a darker shade
};

struct VSIn {
	float3 pos : POSITION;
	float3 normal : NORMAL;
	float value : VALUE;
	float2 grid : GRID;
};

struct VSOut {
	float4 pos : SV_POSITION;
	float3 world : WORLD;
	float3 normal : NORMAL;
	float value : VALUE;
	float2 grid : GRID;
};

VSOut VS(VSIn i) {
	VSOut o;
	o.pos = mul(float4(i.pos, 1), viewProjection);
	o.world = i.pos;
	o.normal = i.normal;
	o.value = i.value;
	o.grid = i.grid;
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

// How much of a line is at t: a pixel and a bit wide, fading out where they'd crowd (under 4 pixels apart)
float Line(float t) {
	float w = fwidth(t);
	if (w <= 0)
		return 0;
	float d = abs(frac(t - 0.5) - 0.5) / w;
	return (1 - saturate(d - 0.35)) * saturate((0.5 - w) / 0.25);
}

float3 Shade(VSOut i) {
	// before anything's discarded: lines need their neighbors' values
	float onLine = 0;
	if (gridScale.x > 0)
		onLine = max(onLine, Line((i.world.x + 1) * gridScale.x + gridOffset.x));
	if (gridScale.y > 0)
		onLine = max(onLine, Line((i.world.y + 1) * gridScale.y + gridOffset.y));
	if (gridScale.z > 0)
		onLine = max(onLine, Line((i.world.z + 1) * gridScale.z + gridOffset.z));
	if (gridScale.w > 0)
		onLine = max(onLine, max(Line(i.grid.x * gridScale.w), Line(i.grid.y * gridScale.w)));
	if (options.z > 0 && any(abs(i.world) > 1.0005))
		discard;
	float3 base = options.x > 0 ? Viridis(saturate(i.value)) : color.rgb;
	if (options.y > 0)
		return base;
	// both sides lit: the normal turned toward the eye; a light a little above and beside it
	float3 v = normalize(eye.xyz - i.world);
	float3 n = normalize(i.normal);
	if (dot(n, v) < 0)
		n = -n;
	float3 l = normalize(v + float3(0.35, -0.25, 0.8));
	float diffuse = saturate(dot(n, l));
	float3 h = normalize(l + v);
	float specular = pow(saturate(dot(n, h)), 32) * 0.12;
	float3 lit = base * (0.32 + 0.68 * diffuse) + specular;
	return lerp(lit, lineColor.w > 0 ? lineColor.rgb : lit * 0.3, onLine * 0.85);
}

float4 PS(VSOut i) : SV_TARGET {
	return float4(Shade(i), options.w);
}

// Weighted blended order-independent transparency (McGuire and Bavoil): each fragment adds its color weighted by its
// opacity and nearness, and multiplies down how much of the background is left.
struct OitOut {
	float4 accum : SV_TARGET0;
	float4 reveal : SV_TARGET1;
};

OitOut PSOit(VSOut i) {
	float3 rgb = Shade(i);
	float a = options.w;
	float z = length(eye.xyz - i.world);
	float weight = a * clamp(10 / (1e-5 + pow(z / 5, 2) + pow(z / 200, 6)), 1e-2, 3e3);
	OitOut o;
	o.accum = float4(rgb * a, a) * weight;
	o.reveal = float4(a, 0, 0, 0);
	return o;
}

float4 VSFull(uint id : SV_VertexID) : SV_POSITION {
	return float4(id == 1 ? 3 : -1, id == 2 ? 3 : -1, 0, 1);
}

Texture2D Accum : register(t0);
Texture2D Reveal : register(t1);

float4 PSComposite(float4 pos : SV_POSITION) : SV_TARGET {
	int3 at = int3(pos.xy, 0);
	float4 accum = Accum.Load(at);
	float left = Reveal.Load(at).r;
	return float4(accum.rgb / clamp(accum.a, 1e-5, 5e4), 1 - left);
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
		{ "GRID", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FplVertex3, u), D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
	m_OitShader.Reset();
	m_CompositeShader.Reset();
	m_FullShader.Reset();
	m_OitBlend.Reset();
	m_OverBlend.Reset();
	m_ReadDepth.Reset();
	m_NoDepth.Reset();
	m_OitUnavailable = false;
	m_Composite = false;
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
	m_AccumView.Reset();
	m_RevealView.Reset();
	m_BackView.Reset();
	m_AccumRead.Reset();
	m_RevealRead.Reset();
	m_Accum.Reset();
	m_Reveal.Reset();
	m_AccumResolved.Reset();
	m_RevealResolved.Reset();
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
	m_Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	m_Composite = false;
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

void FplRenderer3D::Submit(const FplGpuMesh& mesh, const FplDrawStyle& style, float opacity) {
	if (!mesh)
		return;
	SetColor(m_Frame.color, style.color);
	m_Frame.options[0] = style.colormap ? 1.0f : 0.0f;
	m_Frame.options[1] = style.lines ? 1.0f : 0.0f;
	m_Frame.options[2] = style.clip ? 1.0f : 0.0f;
	m_Frame.options[3] = opacity;
	for (int k = 0; k < 3; k++) {
		m_Frame.gridScale[k] = style.grid.scale[k];
		m_Frame.gridOffset[k] = style.grid.offset[k];
	}
	m_Frame.gridScale[3] = style.grid.cells > 0 ? 1 / style.grid.cells : 0;
	m_Frame.gridOffset[3] = 0;
	SetColor(m_Frame.lineColor, style.lineColor == FplNoColor ? 0 : style.lineColor);
	m_Frame.lineColor[3] = style.lineColor == FplNoColor ? 0.0f : 1.0f;
	m_Context->UpdateSubresource(m_Constants.Get(), 0, nullptr, &m_Frame, 0, 0);
	UINT stride = sizeof(FplVertex3), offset = 0;
	m_Context->IASetVertexBuffers(0, 1, mesh.vertices.GetAddressOf(), &stride, &offset);
	m_Context->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
	m_Context->IASetPrimitiveTopology(style.lines ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_Context->DrawIndexed(mesh.count, 0, 0);
}

void FplRenderer3D::Draw(const FplGpuMesh& mesh, const FplDrawStyle& style) {
	Submit(mesh, style, 1);
}

// The shaders and states once; the targets with the window's size. They're big (64 bits a pixel and more, times the
// samples), so a plot with nothing see-through never has them.
bool FplRenderer3D::EnsureTranslucent() {
	if (m_OitUnavailable)
		return false;
	auto give_up = [&] {
		m_OitUnavailable = true;
		return false;
	};
	// different blending per target
	if (m_Device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_1)
		return give_up();
	const DXGI_FORMAT AccumFormat = DXGI_FORMAT_R16G16B16A16_FLOAT, RevealFormat = DXGI_FORMAT_R16_FLOAT;
	if (!m_OitShader) {
		auto oit = Compile("PSOit", "ps_4_0"), composite = Compile("PSComposite", "ps_4_0"), full = Compile("VSFull", "vs_4_0");
		D3D11_BLEND_DESC oitBlend{};
		oitBlend.IndependentBlendEnable = TRUE;
		auto& accum = oitBlend.RenderTarget[0];
		accum.BlendEnable = TRUE;
		accum.SrcBlend = accum.DestBlend = accum.SrcBlendAlpha = accum.DestBlendAlpha = D3D11_BLEND_ONE;
		accum.BlendOp = accum.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		accum.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		auto& reveal = oitBlend.RenderTarget[1];
		reveal.BlendEnable = TRUE;
		reveal.SrcBlend = reveal.SrcBlendAlpha = D3D11_BLEND_ZERO;
		reveal.DestBlend = D3D11_BLEND_INV_SRC_COLOR;
		reveal.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		reveal.BlendOp = reveal.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		reveal.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_BLEND_DESC overBlend{};
		auto& over = overBlend.RenderTarget[0];
		over.BlendEnable = TRUE;
		over.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		over.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		over.SrcBlendAlpha = D3D11_BLEND_ZERO;
		over.DestBlendAlpha = D3D11_BLEND_ONE;
		over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_DEPTH_STENCIL_DESC read{};
		read.DepthEnable = TRUE;
		read.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		read.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		D3D11_DEPTH_STENCIL_DESC none{};
		if (!oit || !composite || !full ||
			FAILED(m_Device->CreatePixelShader(oit->GetBufferPointer(), oit->GetBufferSize(), nullptr, m_OitShader.GetAddressOf())) ||
			FAILED(m_Device->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, m_CompositeShader.GetAddressOf())) ||
			FAILED(m_Device->CreateVertexShader(full->GetBufferPointer(), full->GetBufferSize(), nullptr, m_FullShader.GetAddressOf())) ||
			FAILED(m_Device->CreateBlendState(&oitBlend, m_OitBlend.GetAddressOf())) ||
			FAILED(m_Device->CreateBlendState(&overBlend, m_OverBlend.GetAddressOf())) ||
			FAILED(m_Device->CreateDepthStencilState(&read, m_ReadDepth.GetAddressOf())) ||
			FAILED(m_Device->CreateDepthStencilState(&none, m_NoDepth.GetAddressOf()))) {
			m_OitShader.Reset();
			return give_up();
		}
	}
	if (m_Accum)
		return true;
	// as many samples as the scene, since they share its depth buffer
	UINT accumQuality = 0, revealQuality = 0;
	if (m_Samples > 1 && (FAILED(m_Device->CheckMultisampleQualityLevels(AccumFormat, m_Samples, &accumQuality)) || !accumQuality ||
		FAILED(m_Device->CheckMultisampleQualityLevels(RevealFormat, m_Samples, &revealQuality)) || !revealQuality))
		return give_up();
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = m_Width;
	desc.Height = m_Height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = m_Samples;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET;
	D3D11_TEXTURE2D_DESC resolved = desc;
	resolved.SampleDesc.Count = 1;
	resolved.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	auto make = [&](DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& target, ComPtr<ID3D11RenderTargetView>& view,
		ComPtr<ID3D11Texture2D>& read, ComPtr<ID3D11ShaderResourceView>& srv) {
		desc.Format = resolved.Format = format;
		return SUCCEEDED(m_Device->CreateTexture2D(&desc, nullptr, target.GetAddressOf())) &&
			SUCCEEDED(m_Device->CreateRenderTargetView(target.Get(), nullptr, view.GetAddressOf())) &&
			SUCCEEDED(m_Device->CreateTexture2D(&resolved, nullptr, read.GetAddressOf())) &&
			SUCCEEDED(m_Device->CreateShaderResourceView(read.Get(), nullptr, srv.GetAddressOf()));
	};
	ComPtr<ID3D11Texture2D> back;
	if (!make(AccumFormat, m_Accum, m_AccumView, m_AccumResolved, m_AccumRead) ||
		!make(RevealFormat, m_Reveal, m_RevealView, m_RevealResolved, m_RevealRead) ||
		FAILED(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()))) ||
		FAILED(m_Device->CreateRenderTargetView(back.Get(), nullptr, m_BackView.GetAddressOf()))) {
		m_Accum.Reset();
		return give_up();
	}
	return true;
}

bool FplRenderer3D::BeginTranslucent() {
	if (EnsureTranslucent()) {
		const float none[4] = { 0, 0, 0, 0 }, all[4] = { 1, 1, 1, 1 };
		m_Context->ClearRenderTargetView(m_AccumView.Get(), none);
		m_Context->ClearRenderTargetView(m_RevealView.Get(), all);
		ID3D11RenderTargetView* views[] = { m_AccumView.Get(), m_RevealView.Get() };
		m_Context->OMSetRenderTargets(2, views, m_DepthView.Get());
		m_Context->OMSetBlendState(m_OitBlend.Get(), nullptr, 0xFFFFFFFF);
		m_Context->OMSetDepthStencilState(m_ReadDepth.Get(), 0);
		m_Context->PSSetShader(m_OitShader.Get(), nullptr, 0);
		m_Composite = true;
		return true;
	}
	// plain blending over the scene, in the order drawn
	D3D11_BLEND_DESC blend{};
	auto& over = blend.RenderTarget[0];
	over.BlendEnable = TRUE;
	over.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	over.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	over.SrcBlendAlpha = D3D11_BLEND_ZERO;
	over.DestBlendAlpha = D3D11_BLEND_ONE;
	over.BlendOp = over.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	over.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	D3D11_DEPTH_STENCIL_DESC read{};
	read.DepthEnable = TRUE;
	read.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	read.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	ComPtr<ID3D11BlendState> state;
	ComPtr<ID3D11DepthStencilState> depth;
	if (FAILED(m_Device->CreateBlendState(&blend, state.GetAddressOf())) ||
		FAILED(m_Device->CreateDepthStencilState(&read, depth.GetAddressOf())))
		return false;
	m_Context->OMSetBlendState(state.Get(), nullptr, 0xFFFFFFFF);
	m_Context->OMSetDepthStencilState(depth.Get(), 0);
	return true;
}

void FplRenderer3D::DrawTranslucent(const FplGpuMesh& mesh, const FplDrawStyle& style, float opacity) {
	Submit(mesh, style, opacity);
}

void FplRenderer3D::EndTranslucent() {
	m_Context->OMSetRenderTargets(1, m_ColorView.GetAddressOf(), m_DepthView.Get());
	m_Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	m_Context->OMSetDepthStencilState(m_DepthState.Get(), 0);
	m_Context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
}

// The see-through part over the resolved scene.
void FplRenderer3D::Composite() {
	if (m_Samples > 1) {
		m_Context->ResolveSubresource(m_AccumResolved.Get(), 0, m_Accum.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
		m_Context->ResolveSubresource(m_RevealResolved.Get(), 0, m_Reveal.Get(), 0, DXGI_FORMAT_R16_FLOAT);
	}
	else {
		m_Context->CopyResource(m_AccumResolved.Get(), m_Accum.Get());
		m_Context->CopyResource(m_RevealResolved.Get(), m_Reveal.Get());
	}
	m_Context->OMSetRenderTargets(1, m_BackView.GetAddressOf(), nullptr);
	m_Context->OMSetBlendState(m_OverBlend.Get(), nullptr, 0xFFFFFFFF);
	m_Context->OMSetDepthStencilState(m_NoDepth.Get(), 0);
	m_Context->IASetInputLayout(nullptr);
	m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_Context->VSSetShader(m_FullShader.Get(), nullptr, 0);
	m_Context->PSSetShader(m_CompositeShader.Get(), nullptr, 0);
	ID3D11ShaderResourceView* read[] = { m_AccumRead.Get(), m_RevealRead.Get() };
	m_Context->PSSetShaderResources(0, 2, read);
	m_Context->Draw(3, 0);
	ID3D11ShaderResourceView* none[] = { nullptr, nullptr };
	m_Context->PSSetShaderResources(0, 2, none);
	m_Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
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
	if (m_Composite) {
		Composite();
		m_Composite = false;
	}
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
