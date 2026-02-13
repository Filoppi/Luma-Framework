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
    (w == 1024.f && h == 1024.f) && 
      // CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(0, 0), 0).xyz) &&
      CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(190 / 1024.f, 290 / 1024.f), 0).xyz) &&
     !CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(195 / 1024.f, 318 / 1024.f), 0).xyz) &&
      CheckWhite(g_texture.SampleLevel(g_sampler_s, float2(215 / 1024.f, 788 / 1024.f), 0).xyz) &&
      CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(206 / 1024.f, 922 / 1024.f), 0).xyz)
    )
    ||
    // hit response 2
    (
    (w == 2048.f && h == 1024.f) && 
      // CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(0, 0), 0).xyz) &&
      CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(112 / 2048.f, 168 / 1024.f), 0).xyz) &&
     !CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(107 / 2048.f, 193 / 1024.f), 0).xyz) &&
      CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(103 / 2048.f, 217 / 1024.f), 0).xyz) &&
      CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(129 / 2048.f, 366 / 1024.f), 0).xyz) &&
     !CheckBlack(g_texture.SampleLevel(g_sampler_s, float2(105 / 2048.f, 372 / 1024.f), 0).xyz)
    )
  ) return HUDBrightness(GS.HUDBrightnessNoteResponse);

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