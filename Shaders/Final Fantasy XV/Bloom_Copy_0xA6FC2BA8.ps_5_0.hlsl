// ---- Created with 3Dmigoto v1.4.1 on Mon May  4 16:47:16 2026

SamplerState srcSampler_s : register(s0);
Texture2D<float4> srcSamplerTexture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = srcSamplerTexture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  o0.xyz = r0.xyz;
  o0.w = 0;
  return;
}