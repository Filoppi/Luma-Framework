#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
#if 1 // Luma: fix cropped sampling in UW (e.g. in 32:9 it'd only sample the half left portion of the image, probably not intentional). This happens both in the vanilla game (with black bars) and UW mods
  float gameAspectRatio = LumaSettings.SwapchainSize.x * LumaSettings.SwapchainInvSize.y;
  float nativeAspectRatio = 16.0 / 9.0;

  v2.x *= max(gameAspectRatio / nativeAspectRatio, 1.0);
#endif
#if 0 // Color output test
  o0 = t0.Sample(s0_s, v2.xy).xyzw;
  return;
#endif

  float4 r0,r1,r2;
  int4 r0i;
  r0.xy = 0.0;
  r0i.z = 0;
  while (true) {
    if (r0i.z >= 4) break;
    r0.w = (float)r0i.z;
    r1.x = r0.w * 0.25 + v2.x;
    r1.zw = r0.xy;
    r0i.w = 0;
    while (true) {
      if (r0i.w >= 4) break;
      r2.x = (float)r0i.w;
      r1.y = -r2.x * 0.25 + v2.y;
      r2.xy = float2(-0.5,0.5) + r1.xy;
      r2.xyzw = t0.Sample(s0_s, r2.xy).xyzw;

      // Decode HDR
      r2.xyz = r2.xyz / r2.w;
      r2.xyz = float3(0.25,0.25,0.25) * r2.xyz;

#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear
      r1.y = GetLuminance(r2.xyz);
#else
      r1.y = dot(r2.xyz, float3(0.300000012,0.589999974,0.109999999));
#endif
      r1.z = max(r1.z, r1.y);
      r1.w = r1.w + r1.y;
      r0i.w++;
    }
    r0.xy = r1.zw;
    r0i.z++;
  }
  r0.y = 0.0625 * r0.y;
  o0.xyzw = v1.xyzw * r0.yyyx;
}