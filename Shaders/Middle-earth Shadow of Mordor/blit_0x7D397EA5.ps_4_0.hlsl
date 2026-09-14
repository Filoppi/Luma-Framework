// ---- Created with 3Dmigoto v1.3.16 on Sun Jul 12 23:08:50 2026

cbuffer _Globals : register(b0)
{
  float4 GammaRamp : packoffset(c0);
}

SamplerState TextureSampler_s : register(s0);
Texture2D<float4> SourceBuffer : register(t0);


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

  r0.xyzw = SourceBuffer.Sample(TextureSampler_s, v1.xy).xyzw;
  o0.w = r0.w;

  // r0.xyz = saturate(r0.xyz * GammaRamp.yyy + GammaRamp.zzz);
  // r0.xyz = log2(r0.xyz);
  // r0.xyz = GammaRamp.xxx * r0.xyz;
  // r0.xyz = exp2(r0.xyz);
  o0.xyz = r0.xyz;
  return;
}