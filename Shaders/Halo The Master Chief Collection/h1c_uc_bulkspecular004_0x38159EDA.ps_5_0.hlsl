// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:26 2026

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
TextureCube<float4> Texture1_CUBE : register(t1);
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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = Texture0.Sample(TexS0_s, v3.xy).xyz;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.xyz = Texture2.Sample(TexS2_s, v5.xyz).xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r1.xyz);
  r1.w = r0.w + r0.w;
  r0.xyz = r1.www * r0.xyz + -r1.xyz;
  r1.x = saturate(8 * r1.z);
  r1.yzw = Texture3.Sample(TexS3_s, v6.xyz).xyz;
  r1.yzw = r1.yzw * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = saturate(dot(r0.xyz, r1.yzw));
  r0.y = saturate(8 * r1.w);
  r0.y = r0.y * r1.x;
  r0.x = r0.x * r0.x;
  r0.x = r0.x * r0.x;
  r1.xyz = Texture1_CUBE.Sample(TexS1_s, v4.xyz).xyz;
  r0.z = dot(c_specular_brightness.www, r1.xyz);
  r0.z = c_multiplier.z * r0.z;
  r0.z = r0.z * r0.x;
  r0.x = r0.x * r0.x;
  r2.w = saturate(3 * r0.z);
  r0.z = -0.00100000005 + r2.w;
  r0.z = cmp(r0.z < 0);
  if (r0.z != 0) discard;
  r3.xyz = -c_view_parallel_color.xyz + c_view_perpendicular_color.xyz;
  r3.xyz = r0.www * r3.xyz + c_view_parallel_color.xyz;
  r0.xzw = r3.xyz * r0.xxx;
  r0.xzw = r0.xzw * r1.xyz;
  r0.xyz = r0.xzw * r0.yyy;
  r0.xyz = c_multiplier.zzz * r0.xyz;
  r0.xyz = c_specular_brightness.www * r0.xyz;
  r0.w = 1 + -v2.w;
  r2.xyz = max(0, r0.xyz * r0.www);
  o0.xyzw = r2.xyzw;
  o0 = max(0, o0); o0.w = min(1, o0.w); return;
}