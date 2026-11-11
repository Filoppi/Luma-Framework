// The SMAA passes Core draws for "Luma_SMAA_impl.hlsl" (see "SMAA Edge Detection VS" etc in core.hpp): upstream's shared
// "SMAA_Passes.hlsl" plus Filmic SMAA's morphological edge suppression, which this game's edge detection uses. The game file sets
// up SMAA (metrics, preset, predication, "SMAAGather"), then includes this instead of "SMAA.hlsl".

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
   return SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1);
}
