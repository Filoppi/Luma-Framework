// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:24 2026

SamplerState TexS0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Texture0.Sample(TexS0_s, v3.xy).xyzw;
  o0.xyzw = v1.xyzw * r0.xyzw;
  o0 = max(o0, 0); o0.w = min(o0.w, 1);
  return;
}