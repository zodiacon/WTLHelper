// The 3D renderer's shaders.

#include "FunctionPlotShaders.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

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

}  // namespace

Microsoft::WRL::ComPtr<ID3DBlob> FplCompileShader(const char* entry, const char* target, std::string* errors) {
	Microsoft::WRL::ComPtr<ID3DBlob> code, messages;
	if (FAILED(::D3DCompile(Shader, sizeof(Shader) - 1, "FunctionPlot", nullptr, nullptr, entry, target,
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), messages.GetAddressOf()))) {
		if (messages) {
			::OutputDebugStringA((const char*)messages->GetBufferPointer());
			if (errors)
				*errors = (const char*)messages->GetBufferPointer();
		}
		return nullptr;
	}
	return code;
}
