// ---- Created with 3Dmigoto v1.4.1 on Sat Aug  1 17:16:05 2026

SamplerState g_Texture0Sampler_s : register(s0);
Texture2D<float4> g_Texture0 : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float2 v2 : TEXCOORD2,
  float2 w2 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = max(0, g_Texture0.Sample(g_Texture0Sampler_s, v1.xy).xyzw);
  r1.xyzw = max(0, g_Texture0.Sample(g_Texture0Sampler_s, w1.xy).xyzw);
  r2.xyzw = max(0, g_Texture0.Sample(g_Texture0Sampler_s, v2.xy).xyzw);
  r3.xyzw = max(0, g_Texture0.Sample(g_Texture0Sampler_s, w2.xy).xyzw);
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r2.xyzw;
  r0.xyzw = r0.xyzw + r3.xyzw;
  r0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  r1.xyz = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
  r1.xyz = cmp((int3)r1.xyz == int3(0x7f800000,0x7f800000,0x7f800000));
  r1.x = (int)r1.y | (int)r1.x;
  r1.x = (int)r1.z | (int)r1.x;
  if (r1.x != 0) {
    o0.xyzw = float4(1000000,1000000,1000000,1);
    return;
  }
  o0.xyzw = max(0.f,r0.xyzw);
  return;
}