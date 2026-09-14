// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:28 2026

cbuffer _cb0 : register(b0)
{
  float4 c_primary_change_color : packoffset(c0);
  float4 c_fog_color_correction_0 : packoffset(c1);
  float4 c_fog_color_correction_E : packoffset(c2);
  float4 c_fog_color_correction_1 : packoffset(c3);
  float4 c_self_illumination_color : packoffset(c4);
  float c_alpha_ref : packoffset(c5);
  float4 c_fog_color : packoffset(c6);
}

SamplerState TexSampler0_s : register(s0);
SamplerState TexSampler1_s : register(s1);
SamplerState TexSampler2_s : register(s2);
SamplerState TexSampler3_s : register(s3);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);
Texture2D<float4> Texture2 : register(t2);
TextureCube<float4> Texture3 : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  float4 v6 : TEXCOORD3,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Texture0.Sample(TexSampler0_s, v3.xy).xyzw;
  r1.x = -c_alpha_ref + r0.w;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xyz = float3(-1,-1,-1) + c_primary_change_color.xyz;
  r2.xyzw = Texture2.Sample(TexSampler2_s, v5.xy).xyzw;
  r3.xy = r2.xz + -r2.zw;
  r2.xz = c_self_illumination_color.ww * r3.xy + r2.zw;
  r3.xyz = max(0, r2.yyy * c_self_illumination_color.xyz + v1.xyz);
  r1.xyz = r2.zzz * r1.xyz + float3(1,1,1);
  r1.xyz = r3.xyz * r1.xyz;
  r1.w = v2.w * r2.x;
  r2.x = 1 + -r2.z;
  r2.yzw = Texture3.Sample(TexSampler3_s, v6.xyz).xyz;
  r2.yzw = v2.xyz * r2.yzw;
  r2.yzw = r2.yzw * r1.www;
  r0.xyz = max(0, r0.xyz * r1.xyz + r2.yzw);
  o0.w = r0.w;
  r1.xyz = Texture1.Sample(TexSampler1_s, v4.xy).xyz;
  r1.xyz = float3(-1,-1,-1) + r1.xyz;
  r1.xyz = r2.xxx * r1.xyz + float3(1,1,1);
  r0.xyz = max(0, r1.xyz * r0.xyz);
  r1.xyz = c_fog_color_correction_0.www * r0.xyz;
  r0.xyz = -r0.xyz * c_fog_color_correction_0.www + c_fog_color_correction_0.xyz;
  r0.xyz = v1.www * r0.xyz + r1.xyz;
  r1.xyz = max(0, -v1.www * c_fog_color_correction_E.xyz + c_fog_color_correction_1.xyz);
  o0.xyz = r1.xyz + r0.xyz;
  o0 = max(0, o0); o0.w = min(1, o0.w); return;
}