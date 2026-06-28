// SMAA implementation for Borderlands 2 / The Pre-Sequel.
// Reference: https://github.com/iryoku/smaa
// ULTRA preset + color edge detection. Runs POST-tonemap on the tonemap's LDR output (main.cpp drives it via the
// post-draw callback: it runs the tonemap, then SMAA on its LDR result) — NOT on the game's pre-tonemap FXAA pass.
// This keeps SMAA from perturbing the depth-of-field (composited inside the tonemap; the native FXAA is left
// running untouched to feed DoF as vanilla). The LDR buffer is GAMMA (the BL2 tonemap outputs gamma,
// POST_PROCESS_SPACE_TYPE=0), so edge detection + neighborhood blending both work in gamma and the PS appends NO
// HDR/tonemap tail (the core Display Composition still does paper-white + scRGB downstream). Predication uses the
// scene-color .a depth captured at the tonemap; null-predication + scale 1.0 is the no-depth fallback.

#include "../Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod (see main.cpp RunPostTonemapSMAA).
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale: 2.0 when predication is active (scene-color .a depth bound) -> frame-wide
   // threshold 0.10 on flats; 1.0 with a null predication texture (fallback) -> plain ULTRA threshold 0.05. yzw unused.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Predication tuned to ReShade-community best-practice + BL2's clean view-Z depth signal:
//  - flat threshold  = SCALE * SMAA_THRESHOLD            = 2.0 * 0.05      = 0.10 (recommended; rejects busy
//    cel-shade texture color-noise so SMAA doesn't over-AA flat detail)
//  - silhouette thr  = SCALE * SMAA_THRESHOLD * (1-STR)  = 2.0 * 0.05 *0.5 = 0.05 (= plain ULTRA base; predication
//    only relaxes geometric edges back to normal sensitivity, never below -> no over-aggression)
//  - PREDICATION_THRESHOLD lowered (depth-delta gate) since our normalized depth is clean (FPR ~0.1% on flats).
#define SMAA_PREDICATION_STRENGTH  0.5
#define SMAA_PREDICATION_THRESHOLD 0.005
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
   // tex0 = colorTexGamma (gamma-encoded scene color)
   // tex1 = predicationTex (scene .a depth; null fallback -> reads 0, scale 1.0 = plain ULTRA threshold)
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
   // compressed so 1px-thin dark features survive the average (linear blend erodes them). No HDR tail / re-encode:
   // output stays in the gamma LDR buffer's space, mid-pipeline before the composition.
   return SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1);
}
