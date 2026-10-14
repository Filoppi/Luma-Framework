// Directionally Localized AA
// 
// Srcs used directly:
// https://github.com/BarbatosAWLS/Reshade-Shaders/blob/main/Shaders/BaBa_DLAA-T.fx (MIT License)
// https://www.iryoku.com/aacourse/downloads/12-Anti-Aliasing-from-a-Different-Perspective-(DLAA).pdf
//
// Other srcs used to understand:
// https://github.com/BlueSkyDefender/AstrayFX/blob/master/Shaders/DLAA_Plus.fx
// https://github.com/ForserX/DLAA/blob/master/dlaa.hlsl
// https://gamedev.net/forums/topic/594209-dlaa/4767905/

#include "./Includes/Common.hlsl"

#define Strength 1.0 // Final raw vs AA color lerp
#define EdgeContrast 2.0 // 2.0 High pass filter contrast
#define Epsilon 0.05 // 0.05 Short Edge Epsilon, subtraction bias
#define Lambda 3.0 // 3.0 Short Edge Lambda
#define LongEdgeThreshold 0.35 // 0.35 Threshold
#define GammaDecode 0 // Should AA compute in linear space?

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}
float2 GetTexSize() {return g_texel_size.zw;}
float2 GetPixSize() {return g_texel_size.xy;}

SamplerState s0 : register(s0); // point
// SamplerState s1 : register(s1); // also point
Texture2D<float4> t0 : register(t0);

///////////////////////////////////////////////////////////////////////////////////////////////////////

float LI(float3 color) // Luminosity Intensity
{
  return GetLuminance(color); // y
  // return max3(color); // max channel
  // return color.y; // green
}

// Safe t in (0,1) for luminosity match; 0 = invalid
float SafeLumaT(float blurred, float fromLum, float toLum)
{
    float denom = toLum - fromLum;
    if (abs(denom) < 1e-4) return 0.0;
    float t = (blurred - fromLum) / denom;
    return (t > 0.0 && t < 1.0) ? t : 0.0;
}

// Short edge - talk p43 / TFU2 (5-tap H/V).
void ComputeShortEdgeMasks(float4 Center, float4 Left, float4 Right, float4 Up, float4 Down,
                            out float4 blurredH, out float4 blurredV,
                            out float satAmountH, out float satAmountV)
{
    float4 combH = 2.0 * (Left + Right);
    float4 combV = 2.0 * (Up + Down);

    float4 centerDiffH = abs(combH - 4.0 * Center) / 4.0;
    float4 centerDiffV = abs(combV - 4.0 * Center) / 4.0;

    blurredH = (combH + 2.0 * Center) / 6.0;
    blurredV = (combV + 2.0 * Center) / 6.0;

    float lumH  = LI(centerDiffH.rgb);
    float lumV  = LI(centerDiffV.rgb);
    float lumHB = max(LI(blurredH.rgb), 1e-4);
    float lumVB = max(LI(blurredV.rgb), 1e-4);

    // Intensity-independent masks; epsilon scaled to edge magnitude domain
    satAmountH = saturate((Lambda * lumH - Epsilon) / lumVB);
    satAmountV = saturate((Lambda * lumV - Epsilon) / lumHB);
}

// Long edge sparse samples - talk p44 (reuse +-1 short taps + +-3.5/5.5/7.5).
void ComputeLongEdgeMasks(float2 uv, float4 Left, float4 Right, float4 Up, float4 Down,
                           out float longMaskH, out float longMaskV,
                           out float longEdgeSep, out float longGate,
                           out float lbH, out float lbV)
{
    float4 HNegA = t0.SampleLevel(s0, uv, 0, float2(-3.5, 0));
    float4 HNegB = t0.SampleLevel(s0, uv, 0, float2(-5.5, 0));
    float4 HNegC = t0.SampleLevel(s0, uv, 0, float2(-7.5, 0));
    float4 HPosA = t0.SampleLevel(s0, uv, 0, float2( 3.5, 0));
    float4 HPosB = t0.SampleLevel(s0, uv, 0, float2( 5.5, 0));
    float4 HPosC = t0.SampleLevel(s0, uv, 0, float2( 7.5, 0));

    float4 VNegA = t0.SampleLevel(s0, uv, 0, float2(0, -3.5));
    float4 VNegB = t0.SampleLevel(s0, uv, 0, float2(0, -5.5));
    float4 VNegC = t0.SampleLevel(s0, uv, 0, float2(0, -7.5));
    float4 VPosA = t0.SampleLevel(s0, uv, 0, float2(0,  3.5));
    float4 VPosB = t0.SampleLevel(s0, uv, 0, float2(0,  5.5));
    float4 VPosC = t0.SampleLevel(s0, uv, 0, float2(0,  7.5));

    float4 avgBlurH = (Left + HNegA + HNegB + HNegC + Right + HPosA + HPosB + HPosC) / 8.0;
    float4 avgBlurV = (Up + VNegA + VNegB + VNegC + Down + VPosA + VPosB + VPosC) / 8.0;

    longMaskH = saturate(avgBlurH.a * 2.0 - 1.0);
    longMaskV = saturate(avgBlurV.a * 2.0 - 1.0);
    longEdgeSep = abs(longMaskH - longMaskV);
    longGate = longEdgeSep > LongEdgeThreshold ? 1.0 : 0.0;

    lbH = LI(avgBlurH.rgb);
    lbV = LI(avgBlurV.rgb);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

float4 prefilter_ps(
  float4 v0 : SV_POSITION0,
  float2 uv : TEXCOORD0
) : SV_Target0
{
  float3 center = t0.SampleLevel(s0, uv, 0, float2( 0,  0)).xyz;
  float3 left   = t0.SampleLevel(s0, uv, 0, float2(-1,  0)).xyz;
  float3 right  = t0.SampleLevel(s0, uv, 0, float2( 1,  0)).xyz;
  float3 top    = t0.SampleLevel(s0, uv, 0, float2( 0, -1)).xyz;
  float3 bottom = t0.SampleLevel(s0, uv, 0, float2( 0,  1)).xyz;

  // Gamma Decode
#if GammaDecode
  center *= center;
  left   *= left  ;
  right  *= right ;
  top    *= top   ;
  bottom *= bottom;
#endif

  // High-pass + crush (talk slide 22): saturate(abs(x)*a - b)
  float3 edges = 4.0 * abs((left + right + top + bottom) - 4.0 * center);
  float edgesLum = LI(edges.rgb);
  float crushed = saturate(edgesLum * EdgeContrast - Epsilon);

  return float4(center.rgb, crushed);
}

float4 resolve_ps( // aka Spatial()
  float4 v0 : SV_POSITION0,
  float2 uv : TEXCOORD0
) : SV_Target0
{
    float4 Center = t0.SampleLevel(s0, uv, 0, float2( 0,  0));
    float4 Left   = t0.SampleLevel(s0, uv, 0, float2(-1,  0));
    float4 Right  = t0.SampleLevel(s0, uv, 0, float2( 1,  0));
    float4 Up     = t0.SampleLevel(s0, uv, 0, float2( 0, -1));
    float4 Down   = t0.SampleLevel(s0, uv, 0, float2( 0,  1));

    float4 blurredH, blurredV;
    float satAmountH, satAmountV;
    ComputeShortEdgeMasks(Center, Left, Right, Up, Down, blurredH, blurredV, satAmountH, satAmountV);

    float4 dlaa = Center;
    dlaa = lerp(dlaa, blurredH, satAmountV);
    dlaa = lerp(dlaa, blurredV, satAmountH * 0.5); // Second axis at half weight.

    float longMaskH, longMaskV, longEdgeSep, longGate, lbH, lbV;
    ComputeLongEdgeMasks(uv, Left, Right, Up, Down, longMaskH, longMaskV, longEdgeSep, longGate, lbH, lbV);

    // Only one dominant axis, and only where the mask is actually strong
    [branch] if (longGate > 0.0 && max(longMaskH, longMaskV) > 0.15)
    {
        float cL = LI(Center.rgb);
        float lL = LI(Left.rgb);
        float rL = LI(Right.rgb);
        float uL = LI(Up.rgb);
        float dL = LI(Down.rgb);

        // Talk §45–47: construct a neighbor color whose luma matches the wide blur
        float4 clrH = Center;
        float tUp = SafeLumaT(lbH, uL, cL);
        float tDn = SafeLumaT(lbH, cL, dL);
        if (tUp > 0.0) clrH = lerp(Up, Center, tUp);
        else if (tDn > 0.0) clrH = lerp(Center, Down, tDn);

        float4 clrV = Center;
        float tLf = SafeLumaT(lbV, lL, cL);
        float tRt = SafeLumaT(lbV, cL, rL);
        if (tLf > 0.0) clrV = lerp(Left, Center, tLf);
        else if (tRt > 0.0) clrV = lerp(Center, Right, tRt);

        // Prefer the dominant axis only (noise rejection already requires H≠V)
        if (longMaskH > longMaskV) dlaa = lerp(dlaa, clrH, longMaskH);
        else dlaa = lerp(dlaa, clrV, longMaskV);
    }

    float3 outRgb = lerp(Center.rgb, dlaa.rgb, Strength);
#if GammaDecode
    outRgb = sqrt(outRgb);
#endif
    return float4(outRgb, 1.0);
}

void fullscreen_vs(
  uint v0 : SV_VertexID0,
  out float4 o0 : SV_POSITION0,
  out float2 o1 : TEXCOORD0/* ,
  out float2 p1 : TEXCOORD1,
  out float2 o2 : TEXCOORD2,
  out float2 p2 : TEXCOORD3,
  out float2 o3 : TEXCOORD4 */
)
{
  // unknown dcl_: dcl_input_sgv v0.x, vertex_id

  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = (uint)v0.x >> 1;
  r0.x = (uint)r0.x;
  r0.x = r0.x * 2 + -1;
  r0.z = (int)v0.x & 1;
  r0.z = (uint)r0.z;
  r0.y = r0.z * 2 + -1;
  o0.xy = r0.xy; // SV_POSITION0

  r0.xy = r0.xy * g_texcoord_modifier.xy + g_texcoord_modifier.zw;
  o0.zw = float2(0,1); // SV_POSITION0
  
  r0.w = g_texel_size.y * -1 + r0.y;
  o1.xy = r0.xy; // TEXCOORD0 (center)

  // p1.xy = r0.xw; // TODO: zw or xw?
  // o2.xy = g_texel_size.xy * float2(0,1) + r0.xy;
  // p2.xy = g_texel_size.xy * float2(1,0) + r0.xy;
  // o3.xy = g_texel_size.xy * float2(-1,0) + r0.xy;

  return;
}