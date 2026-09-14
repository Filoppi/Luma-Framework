// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:20 2026

SamplerState PS_SAMPLERS_2__s : register(s2);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_2__s, v1.xy).xyz;
  r1.xyz = log2(r0.xyz);
  r0.xyz = v2.xyz * r0.xyz;
  r1.xyz = v3.yyy * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r0.xyz = v3.xxx * r1.xyz + r0.xyz;
  o0.xyz = v2.www * r0.xyz;
  o0.w = 1;
  return;
}