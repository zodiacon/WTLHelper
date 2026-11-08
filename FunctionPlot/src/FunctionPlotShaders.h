#pragma once

// The 3D renderer's shaders and what its vertices look like to them, apart from the renderer, so a test can compile and
// bind them without a window. They're compiled at run time, and one that doesn't compile leaves the plot in 2D, with the
// reason only in the debug output.

#include "FunctionPlot3D.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <string>

struct FplShaderEntry {
	const char* entry;
	const char* target;
};
inline constexpr FplShaderEntry FplShaderEntries[] = {
	{ "VS", "vs_4_0" }, { "PS", "ps_4_0" }, { "PSOit", "ps_4_0" }, { "VSFull", "vs_4_0" }, { "PSComposite", "ps_4_0" },
};

inline const D3D11_INPUT_ELEMENT_DESC FplVertexLayout[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(FplVertex3, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(FplVertex3, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "VALUE", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(FplVertex3, value), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "GRID", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FplVertex3, u), D3D11_INPUT_PER_VERTEX_DATA, 0 },
};
inline constexpr UINT FplVertexLayoutCount = 4;

// Null if it doesn't compile; errors gets the compiler's words (they also go to the debug output).
Microsoft::WRL::ComPtr<ID3DBlob> FplCompileShader(const char* entry, const char* target, std::string* errors = nullptr);
