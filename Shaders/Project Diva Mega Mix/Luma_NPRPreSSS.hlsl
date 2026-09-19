// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:05 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);

// 3Dmigoto declarations
#define cmp -

// sss_nprprep_0x086EEB5C
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0, // center
  float4 v2 : TEXCOORD1, // diag 0
  float4 v3 : TEXCOORD2, // diag 1
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_texture.Sample(g_sampler_s, v1.xy).xw;
  o0 = float4(r0.xxx, r0.y);
  return;

  // r0.z = cmp(r0.y == 1.000000);
  // if (r0.z != 0) {
  //   o0.xyz = r0.xxx;
  //   o0.w = 1;
  //   return;
  // }
  // r0.zw = g_texture.Sample(g_sampler_s, v2.xy).xw;
  // r1.x = cmp(r0.y < r0.w);
  // r0.xyzw = r1.xxxx ? r0.zzzw : r0.xxxy;
  // r1.xy = g_texture.Sample(g_sampler_s, v2.zw).xw;
  // r1.z = cmp(r0.w < r1.y);
  // r0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
  // r1.xy = g_texture.Sample(g_sampler_s, v3.xy).xw;
  // r1.z = cmp(r0.w < r1.y);
  // r0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
  // r1.xy = g_texture.Sample(g_sampler_s, v3.zw).xw;
  // r1.z = cmp(r0.w < r1.y);
  // o0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
  // return;
}