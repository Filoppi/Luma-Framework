// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:08 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;
  r0 = max(0, r0);

  r1.xyzw = g_texture.Sample(g_sampler_s, v1.zw).xyzw;
  r1 = max(0, r1);

  r0.xyzw = r1.xyzw + r0.xyzw;

  r1.xyzw = g_texture.Sample(g_sampler_s, v2.xy).xyzw;
  r1 = max(0, r1);

  r0.xyzw = r1.xyzw + r0.xyzw;

  r1.xyzw = g_texture.Sample(g_sampler_s, v2.zw).xyzw;
  r1 = max(0, r1);

  r0.xyzw = r1.xyzw + r0.xyzw;
  o0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;

  o0 = max(0, o0);
  return;
}