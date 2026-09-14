// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:06 2026

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer MiscPS : register(b1)
{
  float2 texture_size : packoffset(c0);
  float2 texture_size_pad : packoffset(c0.z);
  float4 dynamic_environment_blend : packoffset(c1);
  float4 p_render_debug_mode : packoffset(c2);
  float p_shader_pc_specular_enabled : packoffset(c3);
  float3 p_shader_pc_specular_enabled_pad : packoffset(c3.y);
  float p_shader_pc_albedo_lighting : packoffset(c4);
  float3 p_shader_pc_albedo_lighting_pad : packoffset(c4.y);
  bool LDR_gamma2 : packoffset(c5);
  bool HDR_gamma2 : packoffset(c5.y);
  bool actually_calc_albedo : packoffset(c5.z);
  bool p_lightmap_compress_constant_using_dxt : packoffset(c5.w);
  float ps_total_time : packoffset(c6);
  float3 ps_total_time_pad : packoffset(c6.y);
}

cbuffer CHUDPS : register(b2)
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
  float4 r0,r1,r2,r3;
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
  r2.w = chud_scalar_output_EF.w * r0.y;
  r3.w = g_exposure.w * r2.w;
  r0.y = 1 + -r1.x;
  r1.xyz = chud_color_output_B.xyz * r1.xxx;
  r1.xyz = chud_color_output_A.xyz * r0.yyy + r1.xyz;
  r0.xyz = chud_color_output_C.xyz * r0.xxx + r1.xyz;
  r2.xyz = r0.www ? r0.xyz : chud_color_output_D.xyz;
  r0.xyz = cmp(float3(0,0,0) >= r2.xyz);
  r1.xyz = sqrt(r2.xyz);
  r0.xyz = r0.xyz ? float3(0,0,0) : r1.xyz;
  r3.xyz = LDR_gamma2 ? r0.xyz : r2.xyz;
  o0.xyzw = chud_cortana_pixel ? r2.xyzw : r3.xyzw;

  o0 = max(o0, 0); o0.w = min(o0.w, 1);
  return;
}