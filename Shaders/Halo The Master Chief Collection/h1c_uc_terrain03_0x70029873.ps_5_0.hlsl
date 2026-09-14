// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:30 2026

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

  r0.xyzw = Texture1.Sample(TexS1_s, v4.xy).xyzw;
  r1.xyzw = Texture2.Sample(TexS2_s, v5.xy).xyzw;
  r0.xyzw = -r1.xyzw + r0.xyzw;
  r2.xyzw = Texture0.Sample(TexS0_s, v3.xy).xyzw;
  r0.xyzw = r2.wwww * r0.xyzw + r1.xyzw;
  r0.xyz = r2.xyz * r0.xyz;
  r0.xyz = /* saturate */(r0.xyz + r0.xyz); r0.xyz = max(r0.xyz, 0);
  r1.xyzw = Texture3.Sample(TexS3_s, v6.xy).xyzw;
  r0.xyz = r1.xyz * r0.xyz;
  o0.w = r1.w * r0.w; o0.w = saturate(o0.w);
  o0.xyz = /* saturate */(r0.xyz + r0.xyz); o0.xyz = max(o0.xyz, 0);
  return;
}