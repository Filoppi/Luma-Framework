// Luma: Hitman Absolution DOF composite 0x6BB0E29D. TONEMAP_TYPE 0 = vanilla blend.
// TONEMAP_TYPE >= 1: full-res bokeh gather driven by the game focus (cb4[3..5]).
// Algorithm: per-pixel signed CoC from depth with a 0.15 dead zone (in-focus stays sharp).
// Near-blur detection: a 3-ring x 12-tap search for near CoC around each pixel, plus the game's half-res near coverage.
// Gather: golden-angle spiral with adaptive tap count N and radius R (maxR when near blur is nearby, max(r0,1) otherwise).
// Two layers: taps with CoC < 0 form a near layer, taps with CoC >= 0 form a background layer.
// The center joins the near layer only when it is outside the dead zone in front of focus (c0 < -0.15).
// The near layer is blended over the background by its coverage mass (alpha = saturate(nearMass)).
// If there is no background tap, a fallback background from unoccluded taps is used.
// Edge AA: pixels next to nearer in-focus foreground are half-blended back to their sharp color.
// The spec's single weighted average is intentionally replaced by this two-layer composite and the adaptive R. Do not restore it.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[6];
}

Texture2D<float4> t0 : register(t0); // Half-res game blur, .w = near coverage
Texture2D<float> t1 : register(t1);  // R32 device depth, full res
Texture2D<float4> t2 : register(t2); // Full-res sharp HDR scene

SamplerState s0 : register(s0); // linear clamp
SamplerState s1 : register(s1); // point clamp
SamplerState s2 : register(s2); // point clamp

#define HMA_BOKEH_MAX_RADIUS_1440P 16.0
#define HMA_BOKEH_TAPS 64
#define HMA_TAU 6.2831853
#define HMA_GOLDEN_ANGLE 2.39996323

float LinearDepth(float rawDepth)
{
  return -cb4[3].x / (rawDepth * cb4[3].z - cb4[3].y);
}

// Vanilla normalized signed CoC in [-1, 1]: near < 0, far > 0, 0 in the dead zone.
float SignedCoC(float rawDepth)
{
  float d = LinearDepth(rawDepth);
  float2 nf = max(float2(cb4[4].x - d, d - cb4[4].x), 0);
  nf = saturate(saturate((nf - cb4[4].y) * cb4[4].zw) * cb4[5].x);
  return (nf.y < nf.x) ? -nf.x : nf.y;
}

float4 VanillaComposite(float2 uv)
{
  float coc = SignedCoC(t1.SampleLevel(s1, uv, 0).x);
  float4 blur = t0.SampleLevel(s0, uv, 0);
  float4 sharp = t2.SampleLevel(s2, uv, 0);
  float blend = saturate(max((coc - 0.15) * 10.0, blur.w));
  return float4(sharp.rgb + blend * (blur.rgb - sharp.rgb), sharp.w);
}

#if TONEMAP_TYPE >= 1
float InterleavedGradientNoise(float2 p)
{
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

// Radius in full-res pixels, same 0.15 dead zone as the vanilla blend.
float CoCRadius(float c, float maxR)
{
  return maxR * saturate((abs(c) - 0.15) / 0.85);
}

// Two-layer bokeh: near taps (CoC < 0) form a separate layer blended over the background (CoC >= 0) by coverage mass.
// R is maxR when near blur is nearby, otherwise max(r0, 1) so in-focus-ish pixels only sample their own disc.
float4 BokehComposite(float4 pos, float2 uv)
{
  uint fullW, fullH;
  t2.GetDimensions(fullW, fullH);
  float2 fullSize = float2(fullW, fullH);
  int2 maxPos = int2(fullW, fullH) - 1;
  float maxR = HMA_BOKEH_MAX_RADIUS_1440P * (fullSize.y / 1440.0);

  int2 ip = int2(pos.xy);
  float rawC0 = t1.Load(int3(ip, 0)).x;
  float c0 = SignedCoC(rawC0);
  float r0 = CoCRadius(c0, maxR);
  float centerZ = LinearDepth(rawC0);
  float4 sharpC = t2.Load(int3(ip, 0));
  float nearCov = t0.SampleLevel(s0, uv, 0).w;
  float ign = InterleavedGradientNoise(pos.xy);
  // The center is in the near layer only when it is outside the dead zone in front of focus.
  bool nearCenter = (c0 < 0.0) && (r0 > 0.0);

  bool gather = (r0 >= 0.5) || (nearCov > 0.001);
  bool ringHit = false;
  // Near-blur ring check: every pixel that is not a near center, plus near centers that are not gathering yet.
  if (!nearCenter || !gather)
  {
    float ringRot = HMA_TAU * ign * LumaSettings.GameSettings.BokehDOFRotation;
    float ringSpacing = maxR / 3.0;
    [loop]
    for (int k = 0; k < 36; k++)
    {
      int ring = k / 12;
      int j = k - ring * 12;
      float dist = maxR * float(ring + 1) / 3.0;
      float a = (float(j) + 0.5 * float(ring & 1)) * (HMA_TAU / 12.0) + ringRot;
      float2 p = pos.xy + dist * float2(cos(a), sin(a));
      int2 tp = clamp(int2(p), int2(0, 0), maxPos);
      float ct = SignedCoC(t1.Load(int3(tp, 0)).x);
      float rt = CoCRadius(ct, maxR);
      if (ct < 0.0 && rt > dist - ringSpacing * 0.5)
      {
        ringHit = true;
        break;
      }
    }
    if (ringHit)
    {
      gather = true;
    }
  }
  if (!gather)
  {
    return sharpC;
  }

  bool farPath = !nearCenter && (nearCov <= 0.001) && !ringHit;
  float R = farPath ? max(r0, 1.0) : maxR;
  // Rotation fades with the gather radius, so small gathers do not dither.
  float rot = HMA_TAU * ign * saturate((R - 1.0) / 3.0) * LumaSettings.GameSettings.BokehDOFRotation;
  uint tapLimit = LumaSettings.GameSettings.BokehDOFTapLimit;
  tapLimit = tapLimit == 0u ? 64u : clamp(tapLimit, 8u, 128u);
  int N = int(clamp(ceil(R * R * 0.8), 8.0, float(tapLimit)));
  float tapArea = R * R / float(N);
  float spacing = R * rsqrt(float(N));

  float rotS, rotC;
  sincos(rot, rotS, rotC);
  float gS, gC;
  sincos(HMA_GOLDEN_ANGLE, gS, gC);
  float2 dir = float2(1.0, 0.0);

  float wC = 1.0 / max(r0 * r0, 1.0);
  float3 bgC = float3(0.0, 0.0, 0.0);
  float bgW = 0.0;
  float3 nearC = float3(0.0, 0.0, 0.0);
  float nearW = 0.0;
  float nearMass = 0.0;
  // Fallback background from unoccluded taps with CoC >= 0, used only when bgW is 0.
  float3 fbC = float3(0.0, 0.0, 0.0);
  float fbW = 0.0;
  if (nearCenter)
  {
    nearC = sharpC.rgb * wC;
    nearW = wC;
    nearMass = tapArea * wC;
  }
  else
  {
    bgC = sharpC.rgb * wC;
    bgW = wC;
  }

  [loop]
  for (int i = 0; i < N; i++)
  {
    float rr = R * sqrt((float(i) + 0.5) / float(N));
    // Incremental golden-angle spiral. Outer taps get the per-pixel rotation; inner taps keep a fixed spiral.
    float2 d = dir;
    if (rr > 4.0)
    {
      d = float2(d.x * rotC - d.y * rotS, d.x * rotS + d.y * rotC);
    }
    dir = float2(dir.x * gC - dir.y * gS, dir.x * gS + dir.y * gC);
    float2 p = pos.xy + rr * d;
    // Pixel centers are at half-integers. Weight each texel before layer accumulation
    // so depth and CoC boundaries do not mix colors across layers.
    bool filtering = LumaSettings.GameSettings.BokehDOFFiltering != 0;
    float2 texelPos = p - 0.5;
    int2 basePos = filtering ? int2(floor(texelPos)) : int2(p);
    float2 fracPos = filtering ? frac(texelPos) : float2(0.0, 0.0);
    int footprintCount = filtering ? 4 : 1;
    [loop]
    for (int sampleIndex = 0; sampleIndex < footprintCount; sampleIndex++)
    {
      int2 offset = int2(sampleIndex & 1, sampleIndex >> 1);
      float footprintWeight = (offset.x ? fracPos.x : 1.0 - fracPos.x) *
                              (offset.y ? fracPos.y : 1.0 - fracPos.y);
      int2 tp = clamp(basePos + offset, int2(0, 0), maxPos);
      float3 Ct = t2.Load(int3(tp, 0)).rgb;
      float raw = t1.Load(int3(tp, 0)).x;
      float ct = SignedCoC(raw);
      float rt = CoCRadius(ct, maxR);
      float zt = LinearDepth(raw);

      if (ct < 0.0 && rt > 0.0)
      {
        float cov = saturate((rt - rr) / max(spacing, 1.0) + 0.5);
        float invArea = 1.0 / max(rt * rt, 1.0);
        float w = cov * invArea * footprintWeight;
        nearC += Ct * w;
        nearW += w;
        nearMass += cov * tapArea * invArea * footprintWeight;
      }
      else
      {
        // Far blur must not bleed onto sharper foreground. Foreground blur still spreads onto the background.
        float rEff = (zt > centerZ) ? min(rt, r0) : rt;
        float cov = saturate((rEff - rr) / max(spacing, 1.0) + 0.5);
        float invArea = 1.0 / max(rEff * rEff, 1.0);
        float w = cov * invArea * footprintWeight;
        bgC += Ct * w;
        bgW += w;
        if (ct >= 0.0)
        {
          float fw = footprintWeight / (1.0 + rr);
          fbC += Ct * fw;
          fbW += fw;
        }
      }
    }
  }

  float3 nearColor = nearC / max(nearW, 1e-6);
  float alpha = saturate(nearMass);
  float3 result;
  if (bgW > 1e-6)
  {
    result = lerp(bgC / bgW, nearColor, alpha);
  }
  else if (fbW > 1e-6)
  {
    result = lerp(fbC / fbW, nearColor, alpha);
  }
  else
  {
    result = nearColor;
  }

  // Edge AA: count the 8 neighbours that are nearer and in focus, and half-blend back to the sharp color.
  int nearN = 0;
  [loop]
  for (int n = 0; n < 9; n++)
  {
    int ox = n % 3 - 1;
    int oy = n / 3 - 1;
    if (ox == 0 && oy == 0)
    {
      continue;
    }
    int2 np = clamp(ip + int2(ox, oy), int2(0, 0), maxPos);
    float rawN = t1.Load(int3(np, 0)).x;
    float zn = LinearDepth(rawN);
    float rn = CoCRadius(SignedCoC(rawN), maxR);
    if (zn < centerZ * 0.98 && rn < 1.0)
    {
      nearN++;
    }
  }
  result = lerp(result, sharpC.rgb, saturate(float(nearN) / 3.0));

  return float4(result, sharpC.w);
}
#endif

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
#if TONEMAP_TYPE >= 1
  if (LumaSettings.GameSettings.BokehDOF == 0)
  {
    return VanillaComposite(uv);
  }
  return BokehComposite(pos, uv);
#else
  return VanillaComposite(uv);
#endif
}
