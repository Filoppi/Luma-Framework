// SMAA implementation for Medal of Honor: Airborne (port of the shipped BL2/TPS and Witcher 2 setups — same
// dgVoodoo DX9->11 stack).
// Reference: https://github.com/iryoku/smaa
//
// ULTRA preset + color edge detection. The game ships with NO anti-aliasing of any kind: a structural sweep of the
// dumped pixel shaders found zero luma-coefficient edge detection, every render target reports sampleCount 1, and
// there is no in-game AA option (the community's only answer is an injector). So this is not a replacement for a
// native pass — nothing has to be suppressed, unlike TW2 where the grade's built-in FXAA must be skipped.
//
// Runs POST-final-grade on the graded gamma canvas: main.cpp drives it from the post-draw callback on whichever
// final pass ran this frame (UberPostProcessBlend 0xB9548800 with DoF on, FGammaCorrectionPixelShader 0x52B868E0
// with DoF off), i.e. after the grade and BEFORE the HUD draws onto the same canvas. The canvas is GAMMA
// (POST_PROCESS_SPACE_TYPE=0) and carries display-mapped HDR values (>1 possible) — edge detection and
// neighborhood blending both work in gamma, which keeps 1px-thin dark features alive against a bright sky. The PS
// appends NO HDR/tonemap tail; the core Display Composition runs downstream.

#include "Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod (see main.cpp RunPostFinalGradeSMAA).
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale: 2.0 when predication is active, 1.0 with a null predication texture
   // (fallback) -> plain ULTRA threshold 0.05. yzw unused.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Predication is live: the depth-extract CS supplies the mask and the metrics CB advertises SCALE 2.0, so the two
// values below are load-bearing. SCALE falls back to 1.0 only when the mask is missing. The identities:
//  - flat threshold  = SCALE * SMAA_THRESHOLD           = 2.0 * 0.05       = 0.10 (rejects texture color-noise)
//  - silhouette thr  = SCALE * SMAA_THRESHOLD * (1-STR) = 2.0 * 0.05 * 0.5 = 0.05 (= plain ULTRA base; predication
//    only relaxes geometric edges back to normal sensitivity, never below).
//  - PREDICATION_THRESHOLD 0.5 needs no per-scene tuning because the CS hands SMAA an EDGE-NESS signal in [0,1]
//    (deviation from the local tangent plane), not a depth. The CS tolerance is the lever, not this value.
#define SMAA_PREDICATION_STRENGTH  0.5
#define SMAA_PREDICATION_THRESHOLD 0.5
#define SMAA_CUSTOM_SL
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex)                            Texture2D tex
#define SMAATexturePass2D(tex)                        tex
#define SMAASampleLevelZero(tex, coord)               tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord)          tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord)                        tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord)                   tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset)          tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN                                  [flatten]
#define SMAA_BRANCH                                   [branch]
#define SMAATexture2DMS2(tex)                         Texture2DMS<float4, 2> tex
#define SMAALoad(tex, pos, sample)                    tex.Load(pos, sample)
#define SMAAGather(tex, coord)                        tex.Gather(LinearSampler, coord, 0)
#include "../Includes/SMAA.hlsl"

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);

void fullscreen_triangle(uint id, out float4 position, out float2 texcoord)
{
   texcoord = float2((id << 1) & 2, id & 2);
   position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// SMAAEdgeDetection
void smaa_edge_detection_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float4 offset[3] : TEXCOORD1)
{
   fullscreen_triangle(id, position, texcoord);
   SMAAEdgeDetectionVS(texcoord, offset);
}

float2 smaa_edge_detection_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float4 offset[3] : TEXCOORD1) : SV_Target
{
   // tex0 = colorTexGamma (gamma-encoded graded canvas)
   // tex1 = predicationTex (edge-ness in [0,1]; null fallback -> reads 0, scale 1.0 = plain ULTRA threshold)
   return SMAAColorEdgeDetectionPS(texcoord, offset, tex0, tex1);
}

// SMAABlendingWeightCalculation
void smaa_blending_weight_calculation_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float2 pixcoord : TEXCOORD1, out float4 offset[3] : TEXCOORD2)
{
   fullscreen_triangle(id, position, texcoord);
   SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
}

float4 smaa_blending_weight_calculation_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float2 pixcoord : TEXCOORD1, float4 offset[3] : TEXCOORD2) : SV_Target
{
   // tex0 = edgesTex, tex1 = areaTex, tex2 = searchTex
   return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, tex0, tex1, tex2, 0);
}

// SMAANeighborhoodBlending
void smaa_neighborhood_blending_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float4 offset : TEXCOORD1)
{
   fullscreen_triangle(id, position, texcoord);
   SMAANeighborhoodBlendingVS(texcoord, offset);
}

float4 smaa_neighborhood_blending_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float4 offset : TEXCOORD1) : SV_Target
{
   // tex0 = colorTex (gamma copy), tex1 = blendTex. Blend in gamma (the buffer's space): keeps the bright HDR sky
   // compressed so 1px-thin dark features survive. No HDR tail, no re-encode - output stays in the canvas' space.
   return SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1);
}
