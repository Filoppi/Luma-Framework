// Luma: HDR expansion and debanding replacement for the sky dome pass 0xACE3BFA3.
// TONEMAP_TYPE 0 is the exact vanilla math.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[4];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[21];
}

Texture2D<float4> t0 : register(t0);

SamplerState s0 : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float3 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float2 v4 : TEXCOORD3,
  out float4 o0 : SV_TARGET0)
{
  float f = min(length(v1) * 0.05, 1.0) * min(abs(v2.z), 1.0);

  // Signed fractional part of the animated UV offsets (matches the vanilla ge/frc/movc sequence).
  float ox = cb4[1].x * cb1[20].x;
  float fx = frac(abs(ox));
  fx = (ox >= -ox) ? fx : -fx;
  float oy = cb4[2].x * cb1[20].x;
  float fy = frac(abs(oy));
  fy = (oy >= -oy) ? fy : -fy;
  float2 uv = v4.xy + float2(fx, fy);

#if TONEMAP_TYPE >= 1
  float4 tex;
  if (LumaSettings.GameSettings.SkyDebanding)
  {
    float2 texSize;
    t0.GetDimensions(texSize.x, texSize.y);
    float2 px = 0.5 / texSize;
    tex = t0.Sample(s0, uv + float2(px.x, px.y));
    tex += t0.Sample(s0, uv + float2(-px.x, -px.y));
    tex += t0.Sample(s0, uv + float2(px.x, -px.y));
    tex += t0.Sample(s0, uv + float2(-px.x, px.y));
    tex *= 0.25;

    // Interleaved gradient noise dither, applied in sqrt space.
    float n = frac(52.9829189 * frac(dot(v0.xy, float2(0.06711056, 0.00583715)))) - 0.5;
    float3 r = sqrt(max(tex.rgb, 0.0)) + n / 64.0;
    r = max(r, 0.0);
    tex.rgb = r * r;
  }
  else
  {
    tex = t0.Sample(s0, uv);
  }

  // Sky HDR expansion (SkyHDRBoost) is applied in the tonemap instead: here it ran before the game's auto-exposure meter,
  // which then lowered exposure (~14% measured) and dimmed the whole image, the sky included.

  float4 t = tex * v3;
  o0.xyz = t.rgb * f * cb4[3].x;
  o0.w = t.a * cb4[3].y;
#else
  float4 t = t0.Sample(s0, uv) * v3;
  o0.xyz = t.rgb * f * cb4[3].x;
  o0.w = t.a * cb4[3].y;
#endif
}
