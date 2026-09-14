// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 21:18:33 2026

cbuffer CB_PS_COMMON : register(b0)
{
  float4 COMMON_LBUF_PARAMS : packoffset(c0);
  float4 COMMON_VIEW_POSITION : packoffset(c1);
  float4 COMMON_VIEWPROJ_MATRIX[4] : packoffset(c2);
  float4 COMMON_VIEW_POSITION_COLORCAM : packoffset(c6);
  float4 COMMON_VIEWPROJ_MATRIX_COLORCAM[4] : packoffset(c7);
  float4 COMMON_VIEWPROJ_REFLECTION[4] : packoffset(c11);
  float4 COMMON_OBLIQUE_MATR[4] : packoffset(c15);
  float4 VS_REG_COMMON_FOG_PARAMS[2] : packoffset(c19);
  float4 REG_COMMON_CLIPPING_PLANE : packoffset(c21);
  float4 COMMON_VP_PARAMS[2] : packoffset(c22);
  float4 PS_REG_COMMON_HDR_PARAMS : packoffset(c24);
  float4 PS_REG_COMMON_FOG_SUN_DIR : packoffset(c25);
  float4 PS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c26);
  float4 PS_REG_COMMON_FOG_COLOR : packoffset(c27);
  float4 PS_REG_COMMON_FOG_PLANE_MIRROR : packoffset(c28);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_0[6] : packoffset(c29);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_EXTRA : packoffset(c35);
  float4 PS_REG_COMMON_ELAPSED_TIME : packoffset(c36);
  float4 PS_REG_COMMON_AMBIENT : packoffset(c37);
  float4 PS_REG_COMMON_DEBUG_SHOW_LIGHTING : packoffset(c38);
  float4 VS_REG_COMMON_FOG_COLOR : packoffset(c39);
  float4 VS_REG_COMMON_FOG_SUN_DIR : packoffset(c40);
  float4 VS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c41);
  float4 VS_REG_COMMON_FOG_VOLUME_COUNT : packoffset(c42);
  float4 VS_REG_COMMON_FOG_VOL_START[32] : packoffset(c43);
}

cbuffer CB_PASS_HDR : register(b7)
{
  float4 PS_REG_HDR_TEX : packoffset(c0);
  float4 PS_REG_HDR_INTENSITY : packoffset(c1);
  float4 PS_REG_HDR_THRESHOLD_OFFSET : packoffset(c2);
  float4 PS_REG_HDR_HISTOGRAM : packoffset(c3);
  float4 PS_REG_HDR_GAMMA_RGB : packoffset(c4);
  float4 PS_REG_HDR_DESTURATION : packoffset(c5);
  float4 PS_REG_HDR_TINT : packoffset(c6);
  float4 PS_REG_HDR_THERMOVISION_PARAMS : packoffset(c7);
  float4 PS_REG_HDR_DOF_PARAMS : packoffset(c8);
  float4 HDR_TEX : packoffset(c9);
  float4 HDR_RADIUS : packoffset(c10);
}

SamplerState PS_SAMPLERS_4__s : register(s4);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0);
Texture2D<float4> PS_TEXTURES_2D_1_ : register(t1);
Texture2D<float4> PS_TEXTURES_2D_2_ : register(t2);


// 3Dmigoto declarations
#define cmp -

#include "./h2a_t.hlsl"

// Rolloff
// DoF Pre-Calculations
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_4__s, v1.xy).xyz;
  r0.xyz = min(float3(64512,64512,64512), r0.xyz);
  r1.xyz = PS_TEXTURES_2D_1_.Sample(PS_SAMPLERS_4__s, v1.xy).xyz * GS.Bloom;
  r1.xyz = min(float3(64512,64512,64512), r1.xyz);
  r0.xyz = r1.xyz + r0.xyz;

  SetColor(r0.xyz);

  // Rolloff
  Rolloff();

  // GammaOut
  GammaOut();
  
  o0.xyz = tmi.x;

  /////////////////////////////////////////////////////////////
  
  // DoF Pre-Calculations
  r0.x = PS_TEXTURES_2D_2_.Sample(PS_SAMPLERS_4__s, v1.xy).x;
  r0.x = 1.33333344e-007 + r0.x;
  r0.x = 0.100000016 / r0.x;
  r0.y = -PS_REG_HDR_DOF_PARAMS.y + r0.x;
  r0.x = cmp(r0.x < PS_REG_HDR_DOF_PARAMS.x);
  r0.y = saturate(PS_REG_HDR_DOF_PARAMS.z * r0.y);
  r0.y = 1 + -r0.y;
  o0.w = r0.x ? r0.y : 0;
  return;
}