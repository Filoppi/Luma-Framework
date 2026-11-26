#include "Includes/Common.hlsl"

// Shadow resolve (replaces 0x203D9FAF). Faithful translation of the vanilla pixel shader, with a wider,
// resolution independent soft filter behind "ENABLE_SHADOW_FILTER" (0 = vanilla 9 tap code).
// Runtime options (game settings, all off = the plain area filter): ShadowPCSS, ShadowCascadeBlend,
// ShadowReceiverBias, ShadowAtlasMSBound (reads the atlas from an MS8 copy at t9, sample 0).
#ifndef ENABLE_SHADOW_FILTER
#define ENABLE_SHADOW_FILTER 1
#endif

// Tuning constants
// Penumbra texels per unit of light-space depth gap. Assumes the cascade depth spans roughly [0,1] over ~60 m, so ~1.7 texels per metre at light size 1.
#define PCSS_TEXELS_PER_DEPTH 100.0
// Blocker search radius in texels at light size 1 and a 1024 wide atlas (scaled by light size and atlas scale).
#define PCSS_SEARCH_TEXELS 4.0
// Constant depth bias added with the receiver bias option.
#define RECEIVER_CONST_BIAS 0.0002
// Clamp on the receiver-plane slope, depth per atlas texel.
#define RECEIVER_GRAD_CLAMP 0.01
// Fraction of a cascade's depth range, at its far split, over which the next cascade is blended in.
#define CASCADE_BLEND_BAND 0.1

Texture2D<float4> t0 : register(t0); // Scene depth
Texture2D<float4> t1 : register(t1); // Shadow atlas: 4 cascades (1024x1024 each) stacked vertically, R32_FLOAT depth
Texture2DMS<float> AtlasMS : register(t9); // Optional MS8 copy of the atlas, read only when ShadowAtlasMSBound is set

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

float4 ApplyMask(float4 v, float4 andMask, float4 orMask)
{
  return asfloat((asuint(v) & asuint(andMask)) | asuint(orMask));
}

// 1 = lit (vanilla binary test)
float ShadowLit(float atlasDepth, float refDepth)
{
  return ((atlasDepth - refDepth) >= 0.0) ? 1.0 : 0.0;
}

float AtlasTap(float2 uv)
{
  return ApplyMask(t1.Sample(s1_s, uv), cb3[46], cb3[47]).x;
}

// Atlas depth at an integer texel (caller clamps the texel). Uses the MS8 copy when bound.
float FetchAtlasDepth(int2 texel)
{
  float4 v = t1.Load(int3(texel, 0));
  if (LumaSettings.GameSettings.ShadowAtlasMSBound > 0.5)
    v = AtlasMS.Load(texel, 0);
  return ApplyMask(v, cb3[46], cb3[47]).x;
}

// Reference depth for a tap at texel: constant bias plus receiver-plane slope times the texel offset from the lookup centre p
float TapRefDepth(int2 texel, float2 p, float refDepth, float2 slope, float constBias)
{
  float2 off = (float2)texel + 0.5 - p;
  return refDepth + constBias + dot(slope, off);
}

// Receiver-plane depth slope (depth per atlas texel, dD/dU and dD/dV) from the screen space derivatives of (u, v, depth)
float2 ReceiverSlope(float3 uvdDdx, float3 uvdDdy, float2 dims)
{
  float dUdx = uvdDdx.x * dims.x;
  float dVdx = uvdDdx.y * dims.y;
  float dUdy = uvdDdy.x * dims.x;
  float dVdy = uvdDdy.y * dims.y;
  float det = dUdx * dVdy - dVdx * dUdy;
  if (!(abs(det) > 1e-10)) // also catches NaN
    return 0.0;
  float dDdU = (uvdDdx.z * dVdy - uvdDdy.z * dVdx) / det;
  float dDdV = (dUdx * uvdDdy.z - dUdy * uvdDdx.z) / det;
  return clamp(float2(dDdU, dDdV), -RECEIVER_GRAD_CLAMP, RECEIVER_GRAD_CLAMP);
}

// Atlas uv and reference depth of cascade c for this pixel's light-space position
void CascadeCoords(int c, float4 pos, out float2 uv, out float refDepth)
{
  int cb0 = 116 + 4 * c;
  float4 rowU = float4(cb4[cb0].x, cb4[cb0 + 1].x, cb4[cb0 + 2].x, cb4[cb0 + 3].x);
  float4 rowV = float4(cb4[cb0].y, cb4[cb0 + 1].y, cb4[cb0 + 2].y, cb4[cb0 + 3].y);
  float4 rowD = float4(cb4[cb0].z, cb4[cb0 + 1].z, cb4[cb0 + 2].z, cb4[cb0 + 3].z);
  refDepth = saturate(dot(pos, rowD));
  uv = float2(saturate(dot(pos, rowU)), saturate(dot(pos, rowV)));
}

// Filtered lit fraction (1 = lit) of one cascade
float CascadeLit(float2 uv, float refDepth, float2 slope, uint atlasW, uint atlasH, float2 screenPos)
{
  bool biasOn = LumaSettings.GameSettings.ShadowReceiverBias > 0.5;
  float constBias = biasOn ? RECEIVER_CONST_BIAS : 0.0;
  float2 grad = biasOn ? slope : 0.0;

#if !ENABLE_SHADOW_FILTER
  // Vanilla: 3x3 taps on the atlas with a (1-f, 1, f) weighting per axis, normalised by 4
  float lC  = ShadowLit(AtlasTap(uv), refDepth);
  float lL  = ShadowLit(AtlasTap(uv + float2(-0.000977, 0.0)), refDepth);
  float lR  = ShadowLit(AtlasTap(uv + float2(0.000977, 0.0)), refDepth);
  float lDL = ShadowLit(AtlasTap(uv + float2(-0.000977, -0.000244)), refDepth);
  float lD  = ShadowLit(AtlasTap(uv + float2(0.0, -0.000244)), refDepth);
  float lDR = ShadowLit(AtlasTap(uv + float2(0.000977, -0.000244)), refDepth);
  float lUL = ShadowLit(AtlasTap(uv + float2(-0.000977, 0.000244)), refDepth);
  float lU  = ShadowLit(AtlasTap(uv + float2(0.0, 0.000244)), refDepth);
  float lUR = ShadowLit(AtlasTap(uv + float2(0.000977, 0.000244)), refDepth);

  float2 f = frac(frac(uv * float2(1024.0, 4096.0) + 0.5) + 0.5);
  float rowMid = (1.0 - f.x) * lL + lC + f.x * lR;
  float rowLo  = (1.0 - f.x) * lDL + lD + f.x * lDR;
  float rowHi  = (1.0 - f.x) * lUL + lU + f.x * lUR;
  return 0.25 * ((1.0 - f.y) * rowLo + rowMid + f.y * rowHi);
#else
  // Box filter over the atlas texels covered by a box W texels wide (W = 2 at vanilla width, 1024 wide atlas).
  // Area weighted average of the binary lit test. Width scales with the atlas size, so results match at any resolution.
  float atlasScale = (float)atlasW / 1024.0;
  float2 p = uv * float2(atlasW, atlasH);
  // Shift the lookup back by half an original texel to cancel the half pixel offset on the resized atlas.
  if (atlasScale > 1.0) p -= 0.5 * atlasScale;

  // Bleed guard: keep rows inside the cascade quarter that contains this pixel
  int cascadeQ = clamp((int)floor(uv.y * 4.0), 0, 3);
  int rowsPerCascade = (int)(atlasH >> 2);
  int rowMin = cascadeQ * rowsPerCascade;
  int rowMax = rowMin + rowsPerCascade - 1;

  float boxW = 2.0 * clamp(LumaSettings.GameSettings.ShadowSoftness, 0.5, 2.0) * atlasScale;

  // PCSS: blocker search on a 6x6 grid, then the penumbra width from the receiver-blocker depth gap
  bool noBlocker = false;
  if (LumaSettings.GameSettings.ShadowPCSS > 0.5)
  {
    float lightSize = clamp(LumaSettings.GameSettings.ShadowPCSSLightSize, 0.0, 8.0);
    float penumbraScale = clamp(LumaSettings.GameSettings.ShadowPCSSPenumbraScale, 0.25, 4.0);
    float maxPenumbra = clamp(LumaSettings.GameSettings.ShadowPCSSMaxPenumbra, 2.0, 32.0); // texels per 1024 px
    float minPenumbra = clamp(LumaSettings.GameSettings.ShadowPCSSMinPenumbra, 0.5, 4.0); // texels
    float searchR = max(PCSS_SEARCH_TEXELS * lightSize, 0.5 * maxPenumbra) * atlasScale;
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    [unroll]
    for (int iy = 0; iy < 6; iy++)
    {
      [unroll]
      for (int ix = 0; ix < 6; ix++)
      {
        float2 off = (float2(ix, iy) - 2.5) * (searchR / 2.5);
        int2 texel = int2(clamp((int)floor(p.x + off.x), 0, (int)atlasW - 1),
                          clamp((int)floor(p.y + off.y), rowMin, rowMax));
        float d = FetchAtlasDepth(texel);
        float r = TapRefDepth(texel, p, refDepth, grad, constBias);
        float isBlocker = (d < r) ? 1.0 : 0.0; // a blocker is closer to the light, i.e. fails the lit test
        blockerSum += d * isBlocker;
        blockerCount += isBlocker;
      }
    }
    noBlocker = blockerCount < 0.5;
    float dBlocker = blockerSum / max(blockerCount, 1.0);
    float gap = max(refDepth - dBlocker, 0.0);
    boxW = clamp(gap * PCSS_TEXELS_PER_DEPTH * penumbraScale * lightSize * atlasScale, minPenumbra * atlasScale, maxPenumbra * atlasScale);
  }

  float2 boxMin = p - boxW * 0.5;
  float2 boxMax = p + boxW * 0.5;
  float sumLit = 0.0;
  float norm = 1.0;
  if (boxW <= 8.0)
  {
    int2 kMin = (int2)floor(boxMin);
    int2 kMax = (int2)floor(boxMax);
    [loop]
    for (int ky = 0; ky < 9; ky++)
    {
      int ty = kMin.y + ky;
      if (ty > kMax.y) break;
      float wy = clamp(min((float)ty + 1.0, boxMax.y) - max((float)ty, boxMin.y), 0.0, 1.0);
      int tyC = clamp(ty, rowMin, rowMax);
      [loop]
      for (int kx = 0; kx < 9; kx++)
      {
        int tx = kMin.x + kx;
        if (tx > kMax.x) break;
        float wx = clamp(min((float)tx + 1.0, boxMax.x) - max((float)tx, boxMin.x), 0.0, 1.0);
        int txC = clamp(tx, 0, (int)atlasW - 1);
        int2 texel = int2(txC, tyC);
        float atlasDepth = FetchAtlasDepth(texel);
        sumLit += wx * wy * ShadowLit(atlasDepth, TapRefDepth(texel, p, refDepth, grad, constBias));
      }
    }
    norm = boxW * boxW;
  }
  else
  {
    // Box wider than the 9x9 footprint: 9x9 evenly spaced taps across the box, equal weights
    float stepT = boxW / 8.0;
    // Per-pixel shift of the whole tap grid (interleaved gradient noise) to hide banding between taps
    float ign = frac(52.9829189 * frac(dot(screenPos, float2(0.06711056, 0.00583715))));
    float jit = (stepT > 1.0) ? (ign - 0.5) * stepT : 0.0;
    [loop]
    for (int ky = 0; ky < 9; ky++)
    {
      int tyC = clamp((int)floor(p.y + jit + ((float)ky - 4.0) * stepT), rowMin, rowMax);
      [loop]
      for (int kx = 0; kx < 9; kx++)
      {
        int txC = clamp((int)floor(p.x + jit + ((float)kx - 4.0) * stepT), 0, (int)atlasW - 1);
        int2 texel = int2(txC, tyC);
        sumLit += ShadowLit(FetchAtlasDepth(texel), TapRefDepth(texel, p, refDepth, grad, constBias));
      }
    }
    norm = 81.0;
  }
  return noBlocker ? 1.0 : sumLit / norm;
#endif
}


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  // View depth from scene depth
  float depthS = ApplyMask(t0.Sample(s0_s, v5.xy), cb3[44], cb3[45]).x;
  float d = depthS + cb4[16].z;
  float w = (0.0 < abs(d)) ? rcp(d) : 9999999933815812510711506376257961984.0;
  float zView = w * cb4[16].w;

  // Cascade index = number of split planes (cb4[18].xyz) the view depth is past
  float3 splitDelta = cb4[18].xyz - zView;
  float3 splitPast = (splitDelta >= 0.0) ? 0.0 : 1.0;
  int cascade = (int)(splitPast.x + splitPast.y + splitPast.z);

  float2 viewXY = cb4[17].zw * v5.zw + cb4[17].xy;
  float4 pos = float4(viewXY * (-zView), -zView, 1.0);

  float fade = saturate(zView * cb4[19].x - cb4[19].y);

  uint atlasW, atlasH;
  t1.GetDimensions(atlasW, atlasH);
  float2 dims = float2((float)atlasW, (float)atlasH);


  // Coordinates of this cascade and the next one. Both are computed up front so ddx/ddy stay in uniform flow.
  int cNext = min(cascade + 1, 3);
  float2 uvA; float refA;
  float2 uvB; float refB;
  CascadeCoords(cascade, pos, uvA, refA);
  CascadeCoords(cNext, pos, uvB, refB);
  float3 uvdA = float3(uvA, refA);
  float3 uvdB = float3(uvB, refB);
  float2 slopeA = ReceiverSlope(ddx(uvdA), ddy(uvdA), dims);
  float2 slopeB = ReceiverSlope(ddx(uvdB), ddy(uvdB), dims);

  float Lf = CascadeLit(uvA, refA, slopeA, atlasW, atlasH, v0.xy);

  // Cascade blend: over the last CASCADE_BLEND_BAND of this cascade's depth range, lerp towards the next cascade
  if (LumaSettings.GameSettings.ShadowCascadeBlend > 0.5 && cascade < 3)
  {
    float3 splits = cb4[18].xyz;
    float farSplit = splits[cascade];
    float nearSplit = (cascade > 0) ? splits[cascade - 1] : 0.0;
    float bandStart = farSplit - CASCADE_BLEND_BAND * (farSplit - nearSplit);
    float t = saturate((zView - bandStart) / max(farSplit - bandStart, 1e-6));
    if (t > 0.0)
    {
      float LfNext = CascadeLit(uvB, refB, slopeB, atlasW, atlasH, v0.xy);
      Lf = lerp(Lf, LfNext, t);
    }
  }

  // Vanilla mix with the distance fade, then the vanilla alpha test
  float S = fade * (1.0 - Lf) + Lf;

  uint alphaQ = (uint)max(S * 255.0 + 0.0001, 0.0);
  if (alphaQ >= asuint(cb3[8].z))
    discard;

  o0 = float4(S, S, S, S);
}
