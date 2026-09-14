// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:33 2026

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

  r0.xyz = Texture0.Sample(TexS0_s, v3.xy).xyz;
  r1.xyz = Texture1.Sample(TexS1_s, v4.xy).xyz;
  r0.w = dot(r1.xyz, float3(0.5,0.5,0.5));
  r0.x = dot(r0.www, r0.xyz);
  r0.x = c_specular_brightness.w * r0.x;
  r0.x = c_multiplier.w * r0.x;
  r1.xyz = Texture3.Sample(TexS3_s, v6.xyz).xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = Texture2.Sample(TexS2_s, v5.xyz).xyz;
  r2.xyz = r2.zxy * float3(2,2,2) + float3(-1,-1,-1);
  r2.yzw = r2.xxx * float3(0,0,2) + -r2.yzx;
  r0.y = saturate(dot(r2.yzw, r1.xyz));
  r0.z = saturate(4 * r1.z);
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y;
  r0.x = r0.x * r0.y;
  r0.y = r0.y * r0.y;
  r1.x = r0.x * 3 + -0.00100000005;
  r0.x = 3 * r0.x;
  o0.w = r0.x;
  r0.x = cmp(r1.x < 0);
  if (r0.x != 0) discard;
  r0.x = 4 * r2.x;
  r2.x = saturate(r2.x);
  r0.x = saturate(r0.x);
  r0.x = r0.z * r0.x;
  r1.xyz = -c_view_parallel_color.xyz + c_view_perpendicular_color.xyz;
  r1.xyz = r2.xxx * r1.xyz + c_view_parallel_color.xyz;
  r1.xyz = r1.xyz * r0.www;
  r1.xyz = c_multiplier.www * r1.xyz;
  r0.xzw = r1.xyz * r0.xxx;
  r0.xzw = c_specular_brightness.www * r0.xzw;
  r0.xyz = r0.xzw * r0.yyy;
  o0.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  o0 = max(0, o0); o0.w = min(1, o0.w); return;
}