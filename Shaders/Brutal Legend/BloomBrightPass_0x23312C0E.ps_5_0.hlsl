// Brutal Legend bloom bright pass (0x23312C0E): masks and thresholds the scene into the bloom input.
// Vanilla clamps the result to [0, 1]. With Luma's display modes, values above 1 go through SoftCap instead.
#include "Includes/Common.hlsl"

Texture2D<float4> t2 : register(t2); // Scene
SamplerState s2_s : register(s2);

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

// Vanilla: value if >= 0, else 0 (movc on ge)
float SelectNonNegative(float c, float x)
{
  return (c >= 0.0) ? x : 0.0;
}

#if TONEMAP_TYPE >= 1
// Identity up to 1, then a rational curve that rises smoothly toward 4.0 (1 + 3) and never exceeds it.
// Slope is 1 at x = 1 (C1 continuous with the identity part).
float SoftCap(float x)
{
  return x <= 1.0 ? x : 1.0 + (x - 1.0) / (1.0 + (x - 1.0) / 3.0);
}
#endif

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
  float4 s = ApplyMask(t2.Sample(s2_s, v5.xy), cb3[48], cb3[49]);
  float3 v = (s.xyz * cb4[16].x) * cb4[19].xyz - cb4[17].x;

#if TONEMAP_TYPE >= 1
  float3 x = max(0.0, v * cb4[18].x);
  float3 capped = LumaSettings.DisplayMode == 0 ? saturate(x) : float3(SoftCap(x.x), SoftCap(x.y), SoftCap(x.z));
  o0.x = SelectNonNegative(v.x, capped.x);
  o0.y = SelectNonNegative(v.y, capped.y);
  o0.z = SelectNonNegative(v.z, capped.z);
#else
  float3 x = saturate(v * cb4[18].x);
  o0.x = SelectNonNegative(v.x, x.x);
  o0.y = SelectNonNegative(v.y, x.y);
  o0.z = SelectNonNegative(v.z, x.z);
#endif
  o0.w = 0.0;
}
