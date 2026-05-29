// ---- Created with 3Dmigoto v1.3.16 on Mon Aug 11 22:54:38 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

float Check() { //TODO upgrade to out float multiplier if needed
  // ignore bg sprites
  if (!TonemapInfo::GetDrawnFinal(GS.TonemapInfo) || TonemapInfo::GetDrawnHPBarDelta(GS.TonemapInfo)) return 1;

  //size
  float w;
  float h;
  g_texture.GetDimensions(w, h);

  if (
    // hit response 1
    (
      (w == 1024.f && h == 1024.f)
      && CheckCustom(g_texture.Load(int3(191, 311, 0)), float4(0.19216f, 0.21961f, 0.35294f, 1.00000f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(191, 312, 0)), float4(0.51765f, 0.82745f, 1.00000f, 1.00000f), 0.00001f)
    )
  ) return GS.HUDBrightnessNoteResponse;

  if (
    // hit response 2
    (
      (w == 2048.f && h == 1024.f)
      && CheckCustom(g_texture.Load(int3(1865, 738, 0)), float4(0.32157f, 1.0000f, 1.0000f, 0.92157f), 0.00001f)
      && CheckCustom(g_texture.Load(int3(1865, 739, 0)), float4(0.61176f, 0.8902f, 1.0000f, 0.99608f), 0.00001f)
    )
  ) return GS.HUDBrightnessNoteResponse;

  return 1;
}

//notes spawn and hit
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_texture.Sample(g_sampler_s, v2.xy).xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  o0.xyzw = r0.xyzw;

#if CUSTOM_HUDBRIGHTNESS > 0
  o0.xyzw *= Check();
#endif
  return;
}