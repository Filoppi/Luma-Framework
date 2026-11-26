// Fire/flame additive particle shader (Brutal Legend). Samples the particle atlas (t10) with the flame tint and fade, and
// keeps the complete dgVoodoo input signature (every semantic, in register order).
#include "Includes/Common.hlsl"

Texture2D<float4> t10 : register(t10);
SamplerState s10_s : register(s10);
cbuffer cb3 : register(b3) { float4 cb3[77]; }
cbuffer cb4 : register(b4) { float4 cb4[236]; }

void main(float4 v0 : SV_POSITION0, float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0, float4 v3 : COLOR1, float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0, float4 v6 : TEXCOORD1, float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3, float4 v9 : TEXCOORD4, float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6, float4 v12 : TEXCOORD7, out float4 o0 : SV_TARGET0)
{
  float opacity = saturate(v2.y * cb4[19].w + cb4[18].x);
  float4 sampleColor = asfloat((asuint(t10.Sample(s10_s, v5.xy)) & asuint(cb3[64])) | asuint(cb3[65]));
  float4 tinted = sampleColor * v6;
  float3 color = mad(sampleColor.rgb, v6.rgb, cb4[116].rgb);
  color = mad(tinted.rgb, v5.z, color);
  float alpha = opacity * tinted.a;
  color = mad(v7.w, cb4[17].rgb - color, color) * cb4[16].y;
  if (asuint(cb3[8].z) >= (uint)(alpha * 255.0 + 0.0001)) discard;
#if TONEMAP_TYPE >= 1
  // Boosts and warms the flame tint for the 1856x64 flame atlas only.
  uint width, height;
  t10.GetDimensions(width, height);
  if (LumaSettings.DisplayMode != 0 && width == 1856 && height == 64)
    color *= float3(1.5, 1.9, 1.5);
#endif
  o0 = float4(color, alpha);
}
