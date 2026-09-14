// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:35 2026

cbuffer _cb0 : register(b0)
{
  float4 c0 : packoffset(c0);
  float4 c1 : packoffset(c1);
  float4 c2 : packoffset(c2);
  float4 c3 : packoffset(c3);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = Texture1.Sample(TexS1_s, v4.xy).zw;
  r0.x = dot(c1.wz, r0.xy);
  r0.y = 1 + -r0.x;
  r1.xyzw = /* saturate */(c3.xyzw * r0.xxxx); r1.xyzw = max(r1.xyzw, 0);
  r0.x = Texture0.Sample(TexS0_s, v3.xy).w;
  r0.z = -c0.z * r0.x + 1;
  r0.x = c0.z * r0.x;
  r2.xyzw = /* saturate */(c2.xyzw * r0.xxxx); r2.xyzw = max(r2.xyzw, 0);
  r0.x = /* saturate */(r0.z * r0.y); r0.x = max(r0.x, 0);
  r0.y = 0.999000013 + -r0.x;
  o0.w = 1 + -r0.x; o0.w = saturate(o0.w);
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;
  r0.x = 1 + -r2.w;
  r0.xyz = r1.xyz * r0.xxx;
  r0.w = 1 + -r1.w;
  o0.xyz = r2.xyz * r0.www + r0.xyz;
  return;
}