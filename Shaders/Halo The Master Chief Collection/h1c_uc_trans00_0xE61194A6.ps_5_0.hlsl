// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:39 2026

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
SamplerState TexS2_s : register(s2);
Texture2D<float4> Texture0 : register(t0);
Texture2D<float4> Texture1 : register(t1);
Texture2D<float4> Texture2 : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Texture0.Sample(TexS0_s, v3.xy).xyzw;
  r1.xyzw = Texture1.Sample(TexS1_s, v4.xy).xyzw;
  r0.xyzw = r1.xyzw * r0.xyzw;
  r1.x = r0.w * v1.w + -0.00100000005;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r0.xyz = /* saturate */(r0.xyz + r0.xyz); r0.xyz = max(r0.xyz, 0);
  r0.w = v1.w * r0.w;
  o0.w = r0.w; saturate(o0.w);
  r1.xyz = Texture2.Sample(TexS2_s, v5.xy).xyz;
  r1.xyz = /* saturate */(v1.xyz + r1.xyz); r1.xyz = max(r1.xyz, 0);
  o0.xyz = r1.xyz * r0.xyz;
  return;
}