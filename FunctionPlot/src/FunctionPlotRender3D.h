#pragma once

// The function plot's 3D renderer: Direct3D 11 (hardware, else WARP) on a flip-model swap chain, drawing meshes into a
// multisampled target that's resolved into the back buffer, with a Direct2D device context on the back buffer for what
// goes over the scene (labels, the panel). Shaders are compiled at run time (d3dcompiler_47).

#include "FunctionPlot3D.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

struct FplGpuMesh {
	Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
	UINT count = 0;
	explicit operator bool() const { return count > 0; }
};

// How a mesh is drawn. colormap: by the vertices' heights; lines: a line list, unlit; clip: only what's inside the cube;
// grid: lines on it; lineColor: theirs (FplNoColor: a darker shade of what's under them).
inline constexpr COLORREF FplNoColor = 0xFFFFFFFF;

struct FplDrawStyle {
	COLORREF color = 0;
	bool colormap = false, lines = false, clip = false;
	FplLineGrid grid;
	COLORREF lineColor = FplNoColor;
	bool unlit = false;			// flat color, as lines are (a region)
};

class FplRenderer3D {
public:
	bool Ready() const { return m_Device != nullptr; }
	bool Init(HWND hwnd, UINT width, UINT height, float dpi);
	void Reset();						// everything released (back to 2D, or the device was lost)
	void Resize(UINT width, UINT height, float dpi);
	bool IsWarp() const { return m_Warp; }

	FplGpuMesh Create(const FplMesh& mesh);

	// A frame: clear, draw meshes, then the overlay, then present. false from Present: the device was lost (Reset and
	// start again).
	void Begin(COLORREF back, const FplMatrix4& viewProjection, FplVec3 eye);
	void Draw(const FplGpuMesh& mesh, const FplDrawStyle& style);
	// After the opaque meshes: see-through ones, in any order (weighted blended order-independent transparency: what's
	// behind counts by distance rather than by being drawn first). Between Begin and BeginOverlay.
	bool BeginTranslucent();
	void DrawTranslucent(const FplGpuMesh& mesh, const FplDrawStyle& style, float opacity);
	void EndTranslucent();
	ID2D1DeviceContext* BeginOverlay();	// resolves the scene into the back buffer first
	void EndOverlay();
	bool Present();
	// The back buffer's pixels (premultiplied BGRA), after a frame.
	bool Capture(std::vector<uint32_t>& pixels, UINT& width, UINT& height);

private:
	bool CreateTargets();
	void ReleaseTargets();
	bool EnsureTranslucent();
	void Submit(const FplGpuMesh& mesh, const FplDrawStyle& style, float opacity);
	void Composite();

	Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> m_SwapChain;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Color, m_Depth;		// multisampled
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_ColorView;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthView;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;
	Microsoft::WRL::ComPtr<ID3D11InputLayout> m_Layout;
	Microsoft::WRL::ComPtr<ID3D11Buffer> m_Constants;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_Rasterizer;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthState;
	// see-through meshes: made when first needed, the targets with the size
	Microsoft::WRL::ComPtr<ID3D11PixelShader> m_OitShader, m_CompositeShader;
	Microsoft::WRL::ComPtr<ID3D11VertexShader> m_FullShader;
	Microsoft::WRL::ComPtr<ID3D11BlendState> m_OitBlend, m_OverBlend;
	Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_ReadDepth, m_NoDepth;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Accum, m_Reveal, m_AccumResolved, m_RevealResolved;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_AccumView, m_RevealView, m_BackView;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_AccumRead, m_RevealRead;
	bool m_OitUnavailable = false;		// no such blending: plain alpha blending, in the order given
	bool m_Composite = false;			// this frame has some to put over the scene
	Microsoft::WRL::ComPtr<ID2D1Device> m_D2DDevice;
	Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_Overlay;
	Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_OverlayTarget;
	UINT m_Width = 0, m_Height = 0, m_Samples = 1;
	float m_Dpi = 96;
	bool m_Warp = false;
	struct Frame {
		float viewProjection[16];
		float eye[4];
		float color[4];
		float options[4];
		float gridScale[4];
		float gridOffset[4];
		float lineColor[4];
	} m_Frame{};
};
