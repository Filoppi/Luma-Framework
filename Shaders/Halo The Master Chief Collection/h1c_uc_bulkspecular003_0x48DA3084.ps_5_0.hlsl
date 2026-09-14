// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:28 2026

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
Texture3D<float4> Texture1_3D : register(t1);
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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = Texture0.Sample(TexS0_s, v3.xy).xyz;
  r1.xyz = Texture1_3D.Sample(TexS1_s, v4.xyz).xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r0.w = dot(c_specular_brightness.www, r0.xyz);
  r0.w = c_multiplier.z * r0.w;
  r1.xyz = Texture3.Sample(TexS3_s, v6.xyz).xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = Texture2.Sample(TexS2_s, v5.xyz).xyz;
  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyw = r2.zzz * float3(0,0,2) + -r2.xyz;
  r1.x = saturate(dot(r2.xyw, r1.xyz));
  r1.y = saturate(8 * r1.z);
  r1.x = r1.x * r1.x;
  r1.x = r1.x * r1.x;
  r0.w = r1.x * r0.w;
  r1.x = r1.x * r1.x;
  r3.w = saturate(3 * r0.w);
  r0.w = -0.00100000005 + r3.w;
  r0.w = cmp(r0.w < 0);
  if (r0.w != 0) discard;
  r2.xyw = -c_view_parallel_color.xyz + c_view_perpendicular_color.xyz;
  r2.xyw = r2.zzz * r2.xyw + c_view_parallel_color.xyz;
  r4.x = 8 * r2.z;
  r4.x = saturate(r4.x);
  r0.w = r4.x * r1.y;
  r1.xyz = r2.xyw * r1.xxx;
  r0.xyz = r1.xyz * r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = c_multiplier.zzz * r0.xyz;
  r3.xyz = max(0, c_specular_brightness.www * r0.xyz);
  o0.xyzw = r3.xyzw;
  o0 = max(0, o0); o0.w = min(1, o0.w); return;
}