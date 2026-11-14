// Adapted from Pretty Post Process Halo.usf (starburst term dropped, sb = 0). See Includes/HMAFlare.hlsl for license.
#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // mip1 (W/4 x H/4)

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 sp = uv * 2.0 - 1.0;
  float aspect = InSize.x / InSize.y;

  float2 UV = SquareUV(uv, aspect);
  float2 FishUV = FisheyeUV(UV, HaloParams.z, 1.0);

  // Distortion vector toward the center (zero at the exact center to avoid NaN).
  float2 toCenter = float2(0.5, 0.5) - UV;
  float toCenterLen = length(toCenter);
  float2 HaloVector = (toCenterLen > 1e-6) ? (toCenter / toCenterLen) * HaloParams.x : float2(0.0, 0.0);

  // Halo mask
  float hm = saturate(distance(UV, float2(0.5, 0.5)) * 2.0);
  hm = smoothstep(HaloParams.y, 1.0, hm);

  // Screen border mask
  float border = DiscMask(sp) * DiscMask(sp * 0.8);
  border = border * 0.95 + 0.05;

  float cs = HaloParams2.x;
  float2 UVr = (FishUV - 0.5) * (1.0 + cs) + 0.5 + HaloVector;
  float2 UVg = FishUV + HaloVector;
  float2 UVb = (FishUV - 0.5) * (1.0 - cs) + 0.5 + HaloVector;

  float3 col;
  col.r = t0.SampleLevel(s1, UVr, 0).r;
  col.g = t0.SampleLevel(s1, UVg, 0).g;
  col.b = t0.SampleLevel(s1, UVb, 0).b;

  col *= border * hm * HaloParams.w;
  return float4(col, 0.0);
}
