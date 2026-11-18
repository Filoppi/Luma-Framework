// The SMAA passes Core draws for the games with a "Luma_SMAA_impl.hlsl" (see "SMAA Edge Detection VS" etc in core.hpp).
// The game file sets up SMAA (metrics, preset, predication, "SMAAGather" if needed), then includes this instead of
// "SMAA.hlsl". It must define "SMAA_NEIGHBORHOOD_OUTPUT(color, position)", which changes the blended color before it's written,
// e.g. to go back to gamma space.
// Define "SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR" 1 to let the neighborhood blend read the gamma encoded canvas (the edge
// detection's input) and filter it in linear light, without a linear copy ("gamma_to_linear" must be included first).

#ifndef SMAA_CUSTOM_SL
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
#endif

#if SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR
// The blend averages the pixel with a neighbor through a bilinear fetch, which must happen in linear light: the fetch is done by
// hand, the footprint's texels decoded before weighting (clamped as the linear sampler). The same as the hardware bilinear of a
// linear copy, without the copy. An axis within the hardware's 8 bit filter weights of a texel center is on it: the interpolated
// coordinate's float error would otherwise weight in the neighbor a little, visibly so for an HDR one next to a dark pixel. On a
// texel center on both (every pixel that doesn't blend) the fetch is that texel.
float4 SampleGammaInLinear(Texture2D tex, float2 coord)
{
   const float2 nearest = round(coord * SMAA_RT_METRICS.zw - 0.5);
   const float2 texel = select(abs(coord * SMAA_RT_METRICS.zw - 0.5 - nearest) < 1.0 / 512.0, nearest, coord * SMAA_RT_METRICS.zw - 0.5);
   const int2 last = int2(SMAA_RT_METRICS.zw) - 1;
   float4 color;
   [branch] if (all(texel == nearest))
   {
      color = tex.Load(int3(clamp(int2(nearest), 0, last), 0));
      color.rgb = gamma_to_linear(color.rgb, GCT_MIRROR);
   }
   else
   {
      const float2 base = floor(texel);
      const float2 f = texel - base;
      float4 c[4];
      [unroll] for (uint i = 0; i < 4; i++)
      {
         c[i] = tex.Load(int3(clamp(int2(base) + int2(i & 1, i >> 1), 0, last), 0));
         c[i].rgb = gamma_to_linear(c[i].rgb, GCT_MIRROR);
      }
      color = lerp(lerp(c[0], c[1], f.x), lerp(c[2], c[3], f.x), f.y);
   }
   return color;
}
#define SMAA_NEIGHBORHOOD_SAMPLE(tex, coord) SampleGammaInLinear(tex, coord)
#endif

#include "SMAA.hlsl"

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
   // tex0 = colorTexGamma, tex1 = predicationTex
   return SMAAColorEdgeDetectionPS(texcoord, offset, tex0
#if SMAA_PREDICATION
                                   ,
                                   tex1
#endif
   );
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
   // tex0 = colorTex, tex1 = blendTex
   float4 color = SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1);
   SMAA_NEIGHBORHOOD_OUTPUT(color, position)
   return color;
}
