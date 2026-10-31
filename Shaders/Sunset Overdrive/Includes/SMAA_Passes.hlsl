// Sunset Overdrive's copy of the shared "SMAA_Passes.hlsl" with its edge detection and neighborhood blending extensions (and of
// "SMAA.hlsl" next to it), plus SMAA T2x's velocity at t3: Core's "DrawSMAA()" leaves t2 bound to the search texture there.
// The SMAA passes Core draws for the games with a "Luma_SMAA_impl.hlsl" (see "SMAA Edge Detection VS" etc in core.hpp).
// The game file sets up SMAA (metrics, preset, predication, "SMAAGather" if needed), then includes this instead of
// "SMAA.hlsl". Define "SMAA_NEIGHBORHOOD_OUTPUT(color, position)" to change the blended color before it's written,
// e.g. to go back to gamma space. With "SMAA_REPROJECTION" (T2x) it runs on the resolve's output instead.
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

#if SMAA_MORPHOLOGICAL_EDGE_SUPPRESSION
// Filmic SMAA's morphological edge suppression (Jimenez, "Filmic SMAA", SIGGRAPH 2016 Advances, slides 26-31), which SMAA.hlsl's
// color edge detection runs in place of the local contrast adaptation: an edge survives when the strongest line pattern through
// it scores above twice the threshold, and no pattern that bypasses it beats that by the edge's own contrast. A pattern scores
// its edges' contrasts minus those of the edges that would break it; U and L patterns weigh 1 (the talk gives no weights).
float SMAAColorDelta(float3 a, float3 b)
{
   const float3 t = abs(a - b);
   return max(max(t.r, t.g), t.b);
}

// v.y: the edge; v.x and v.z: the edges continuing it before and after. h.xy: the crossing edges at its start, on the outer
// and inner side; h.zw: the same at its end. outer and inner: the parallel edges one pixel out and in.
bool SMAAMorphologicalEdgeSurvives(float3 v, float4 h, float outer, float inner, float threshold)
{
   float through = v.x + v.y + v.z - (h.x + h.y + h.z + h.w);                         // Line
   through = max(through, h.x + v.y + h.z - (v.x + h.y + v.z + h.w));                 // U, outer side
   through = max(through, h.y + v.y + h.w - (v.x + h.x + v.z + h.z));                 // U, inner side
   through = max(through, h.x + v.y + h.w - (v.x + h.y + h.z + v.z + inner + outer)); // Z
   through = max(through, h.z + v.y + h.y - (h.x + v.x + v.z + h.w + inner + outer)); // Z
   through = max(through, h.x + v.y + v.z - (v.x + h.y + h.z + h.w + outer));         // L
   through = max(through, h.y + v.y + v.z - (h.x + v.x + h.z + h.w + inner));         // L
   through = max(through, h.z + v.y + v.x - (h.x + h.y + v.z + h.w + outer));         // L
   through = max(through, h.w + v.y + v.x - (h.x + h.y + v.z + h.z + inner));         // L
   float bypass = max(0.0, h.x + h.y - (v.x + v.y));                                  // Crossing line at the start
   bypass = max(bypass, h.z + h.w - (v.y + v.z));                                     // Crossing line at the end
   bypass = max(bypass, h.x + v.x - (h.y + v.y));                                     // L turning off it
   bypass = max(bypass, h.y + v.x - (h.x + v.y));
   bypass = max(bypass, h.z + v.z - (h.w + v.y));
   bypass = max(bypass, h.w + v.z - (h.z + v.y));
   return 2.0 * threshold < through && bypass - through < v.y;
}

// Which of the left (x) and top (y) edges survive, from the colors the color edge detection fetched, its left and top deltas, and
// three more diagonal neighbors. The left edge's outer side is the left pixel, the top edge's the top one.
float2 SMAAMorphologicalEdgeSuppression(float2 texcoord, SMAATexture2D(colorTex), float3 c, float3 left, float3 top, float3 right, float3 bottom,
                                        float3 left_left, float3 top_top, float2 delta, float2 threshold)
{
   const float3 top_left = SMAASamplePoint(colorTex, texcoord - SMAA_RT_METRICS.xy).rgb;
   const float3 bottom_left = SMAASamplePoint(colorTex, texcoord + SMAA_RT_METRICS.xy * float2(-1.0, 1.0)).rgb;
   const float3 top_right = SMAASamplePoint(colorTex, texcoord + SMAA_RT_METRICS.xy * float2(1.0, -1.0)).rgb;
   const float d_bottom = SMAAColorDelta(c, bottom);
   const float d_right = SMAAColorDelta(c, right);
   // Left edge: along it the edges above and below; crossing it the top and bottom edges of the left pixel (outer) and of this one
   const bool left_survives = SMAAMorphologicalEdgeSurvives(float3(SMAAColorDelta(top, top_left), delta.x, SMAAColorDelta(bottom, bottom_left)),
                                                            float4(SMAAColorDelta(left, top_left), delta.y, SMAAColorDelta(left, bottom_left), d_bottom),
                                                            SMAAColorDelta(left, left_left), d_right, threshold.x);
   // Top edge: the same, transposed
   const bool top_survives = SMAAMorphologicalEdgeSurvives(float3(SMAAColorDelta(left, top_left), delta.y, SMAAColorDelta(right, top_right)),
                                                           float4(SMAAColorDelta(top, top_left), delta.x, SMAAColorDelta(top, top_right), d_right),
                                                           SMAAColorDelta(top, top_top), d_bottom, threshold.y);
   return float2(left_survives, top_survives);
}
#endif

#include "SMAA.hlsl"

#ifndef SMAA_NEIGHBORHOOD_OUTPUT
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position)
#endif

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);

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

// SMAA T2x: the area texture subsample matching this frame's jitter (SMAA.hlsl's @SUBSAMPLE_INDICES table), 0 for 1x
#ifndef SMAA_SUBSAMPLE_INDICES
#define SMAA_SUBSAMPLE_INDICES 0
#endif

float4 smaa_blending_weight_calculation_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float2 pixcoord : TEXCOORD1, float4 offset[3] : TEXCOORD2) : SV_Target
{
   // tex0 = edgesTex, tex1 = areaTex, tex2 = searchTex
   return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, tex0, tex1, tex2, SMAA_SUBSAMPLE_INDICES);
}

// SMAANeighborhoodBlending
void smaa_neighborhood_blending_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float4 offset : TEXCOORD1)
{
   fullscreen_triangle(id, position, texcoord);
   SMAANeighborhoodBlendingVS(texcoord, offset);
}

float4 smaa_neighborhood_blending_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float4 offset : TEXCOORD1) : SV_Target
{
   // tex0 = colorTex, tex1 = blendTex, with SMAA_REPROJECTION tex3 = velocityTex (its length goes to the alpha)
   float4 color = SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1
#if SMAA_REPROJECTION
                                             ,
                                             tex3
#endif
   );
#if !SMAA_REPROJECTION
   SMAA_NEIGHBORHOOD_OUTPUT(color, position)
#endif
   return color;
}

#if SMAA_REPROJECTION
// SMAAResolve (T2x): this frame's antialiased color blended with the previous frame's, reprojected and weighted by the velocity
// difference in alpha. tex0 = currentColorTex, tex1 = previousColorTex, tex2 = velocityTex. It reads only the position, so any
// fullscreen VS works.
float4 smaa_resolve_ps(float4 position : SV_Position) : SV_Target
{
   float4 color = SMAAResolvePS(position.xy * SMAA_RT_METRICS.xy, tex0, tex1, tex2);
   SMAA_NEIGHBORHOOD_OUTPUT(color, position)
   return color;
}
#endif
