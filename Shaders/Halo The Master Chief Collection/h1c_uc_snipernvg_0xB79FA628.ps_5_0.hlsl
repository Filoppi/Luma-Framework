// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:36 2026

cbuffer _cb0 : register(b0)
{
  float4 c_desaturation_tint : packoffset(c0);
  float4 c_light_enhancement : packoffset(c1);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
SamplerState TexS2_s : register(s2);
SamplerState TexS3_s : register(s3);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);
Texture2D<float4> Texture2 : register(t2);
Texture2D<float4> Texture3 : register(t3);


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

  // color
  r0.xyz = Texture3.Sample(TexS3_s, v6.xy).xyz;
  r1.xyz = Texture2.Sample(TexS2_s, v5.xy).xyz;
  r2.xyz = Texture1.Sample(TexS1_s, v4.xy).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = r0.xyz * float3(0.333333343, 0.333333343, 0.333333343) + -r2.xyz;
  r0.xyz = max(r0.xyz, 0);

  // mask
  r1.xy = Texture0.Sample(TexS0_s, v3.xy).zw;
  r0.xyz = r1.yyy * r0.xyz + r2.xyz;

  // r1.yzw = float3(1,1,1) + -r0.xyz;
  // r1.yzw = r1.yzw * r1.yzw;
  // r1.yzw = -r1.yzw * r1.yzw + float3(1,1,1);
  r1.yzw = max(r0.xyz * 2, 0);

  r0.w = r1.x + r1.x;
  o0.w = r1.x;

  r2.xy = c_light_enhancement.xy * -r0.ww + float2(1,1);
  r0.w = c_light_enhancement.w * r2.x;
  r1.xyz = r0.www * r1.yzw;
  r0.w = -c_light_enhancement.w * r2.x + 1;
  r0.xyz = r0.www * r0.xyz + r1.xyz;

  r0.w = dot(r0.xyz, float3(0.333333343,0.333333343,0.333333343));
  r1.x = c_desaturation_tint.w * r2.y;
  r1.y = -c_desaturation_tint.w * r2.y + 1;
  r1.xzw = c_desaturation_tint.xyz * r1.xxx;
  r2.xyz = r1.xzw * r0.www;
  r1.xzw = r1.xzw * r0.www + r0.xyz;
  r0.xyz = r1.yyy * r0.xyz + r2.xyz;
  r1.xyz = r1.xzw + -r0.xyz;
  r0.xyz = c_light_enhancement.zzz * r1.xyz + r0.xyz;
  o0.xyz = max(0, r0.xyz);
  return;
}