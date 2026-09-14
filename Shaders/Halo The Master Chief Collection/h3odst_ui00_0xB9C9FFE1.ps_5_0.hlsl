// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:49 2026

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer CHUDPS : register(b1)
{
  float4 chud_color_output_A : packoffset(c0);
  float4 chud_color_output_B : packoffset(c1);
  float4 chud_color_output_C : packoffset(c2);
  float4 chud_color_output_D : packoffset(c3);
  float4 chud_color_output_E : packoffset(c4);
  float4 chud_color_output_F : packoffset(c5);
  float4 chud_scalar_output_ABCD : packoffset(c6);
  float4 chud_scalar_output_EF : packoffset(c7);
  float4 chud_texture_bounds : packoffset(c8);
  float4 chud_savedfilm_chap1 : packoffset(c9);
  float4 chud_savedfilm_chap2 : packoffset(c10);
  float4 chud_savedfilm_chap3 : packoffset(c11);
  float4 chud_savedfilm_data : packoffset(c12);
  bool chud_cortana_pixel : packoffset(c13);
  bool chud_comp_colorize_enabled : packoffset(c13.y);
}

SamplerState LocalSampler_basemap_sampler_s : register(s0);
Texture2D<float4> LocalTexture_basemap_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0.25 * chud_scalar_output_ABCD.y;
  r0.y = chud_scalar_output_EF.x + -chud_scalar_output_ABCD.x;
  r0.x = cmp(r0.y < r0.x);
  r0.y = chud_scalar_output_ABCD.y * 0.25 + -r0.y;
  r1.y = chud_scalar_output_ABCD.x;
  r2.y = chud_scalar_output_ABCD.x + r0.y;
  r1.x = chud_scalar_output_ABCD.x + -chud_scalar_output_ABCD.y;
  r2.x = r1.x + r0.y;
  r0.xy = r0.xx ? r2.xy : r1.xy;
  r0.z = chud_scalar_output_EF.x * v1.x + -r0.x;
  r0.w = r0.y + -r0.x;
  r0.z = r0.z / r0.w;
  r0.w = chud_scalar_output_EF.x * v1.x;
  r0.y = cmp(r0.y >= r0.w);
  r0.x = cmp(r0.x < r0.w);
  r0.w = cmp(chud_scalar_output_ABCD.x >= r0.w);
  r0.y = r0.y ? r0.z : 1;
  r0.x = r0.x ? r0.y : 0;
  r0.x = r0.x * r0.x;
  r0.y = -r0.x * r0.x + 1;
  r0.x = r0.x * r0.x;
  r1.xy = LocalTexture_basemap_sampler.Sample(LocalSampler_basemap_sampler_s, v1.xy).yw;
  r1.zw = chud_scalar_output_ABCD.wz * r1.yy;
  r0.z = r1.w * r0.x;
  r0.y = r1.y * r0.y + r0.z;
  r0.y = r0.w ? r0.y : r1.z;
  r0.y = chud_scalar_output_EF.w * r0.y;
  r0.z = g_exposure.w * r0.y;
  o0.w = chud_cortana_pixel ? r0.y : r0.z;
  r0.y = 1 + -r1.x;
  r1.xyz = chud_color_output_B.xyz * r1.xxx;
  r1.xyz = chud_color_output_A.xyz * r0.yyy + r1.xyz;
  r0.xyz = chud_color_output_C.xyz * r0.xxx + r1.xyz;
  o0.xyz = r0.www ? r0.xyz : chud_color_output_D.xyz;
  o0 = max(0, o0); o0.w = min(o0.w, 1);
  return;
}