#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  r0.xyzw = t0.Sample(s0_s, v1.xy).xyzw;

  // Decode HDR
  r0.xyz = r0.xyz / r0.w;
  float3 decodedScene = r0.xyz * 0.25;

  float exposure = v2.x;
  // Exposure can be nan if the scene is all black
  if (IsNaN_Strict(exposure))
  {
    exposure = 1.0;
  }
  float exposureSomething = v2.y;
  float3 exposedScene = exposure * decodedScene;

  o0.xyz = ((exposedScene * exposureSomething + 1.0) * exposedScene) / (exposedScene + 1.0);

  // Luma: nan protection
  o0.xyz = max(o0.xyz, 0.0);

#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear!
  o0.w = GetLuminance(decodedScene);
#else
  o0.w = dot(decodedScene, float3(0.300000012,0.589999974,0.109999999));
#endif
}