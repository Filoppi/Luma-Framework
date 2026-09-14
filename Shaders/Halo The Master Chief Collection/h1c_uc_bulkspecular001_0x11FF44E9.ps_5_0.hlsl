// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:23 2026

cbuffer _cb0 : register(b0)
{
  float4 c_specular_brightness : packoffset(c0);
  float4 c_view_perpendicular_color : packoffset(c1);
  float4 c_view_parallel_color : packoffset(c2);
  float4 c_multiplier : packoffset(c3);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
SamplerState TexS2_s : register(s2);
SamplerState TexS3_s : register(s3);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);
TextureCube<float4> Texture2 : register(t2);
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = Texture1.Sample(TexS1_s, v4.xy).xyz;
  r0.x = dot(r0.xyz, float3(0.5,0.5,0.5));
  r0.y = dot(r0.xxx, float3(1,1,1));
  r0.y = c_specular_brightness.w * r0.y;
  r0.y = c_multiplier.w * r0.y;
  r1.xyz = Texture0.Sample(TexS0_s, v3.xy).xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = Texture2.Sample(TexS2_s, v5.xyz).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.z = dot(r1.xyz, r2.xyz);
  r0.w = r0.z + r0.z;
  r0.z = saturate(r0.z);
  r1.xyz = r0.www * r1.xyz + -r2.xyz;
  r0.w = saturate(4 * r2.z);
  r2.xyz = Texture3.Sample(TexS3_s, v6.xyz).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = saturate(dot(r1.xyz, r2.xyz));
  r1.y = saturate(4 * r2.z);
  r0.w = r1.y * r0.w;
  r1.x = r1.x * r1.x;
  r1.x = r1.x * r1.x;
  r0.y = r1.x * r0.y;
  r1.x = r1.x * r1.x;
  r1.y = r0.y * 3 + -0.00100000005;
  r0.y = 3 * r0.y;
  o0.w = r0.y;
  r0.y = cmp(r1.y < 0);
  if (r0.y != 0) discard;
  r1.yzw = -c_view_parallel_color.xyz + c_view_perpendicular_color.xyz;
  r1.yzw = r0.zzz * r1.yzw + c_view_parallel_color.xyz;
  r0.xyz = r1.yzw * r0.xxx;
  r0.xyz = c_multiplier.www * r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = c_specular_brightness.www * r0.xyz;
  r0.xyz = r0.xyz * r1.xxx;
  o0.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o0 = max(0, o0); o0.w = min(1, o0.w); return;
}