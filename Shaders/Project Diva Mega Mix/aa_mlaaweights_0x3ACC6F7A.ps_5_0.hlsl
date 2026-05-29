// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:06 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float2 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  //1st
  r0.xyz = g_texture.Sample(g_sampler_s, w1.xy).xyz;
  #if CUSTOM_SDR == 0
    r0.xyz *= GS.AAMultiplier;
  #endif

  //2nd
  r1.xyz = g_texture.Sample(g_sampler_s, v1.xy).xyz;
  #if CUSTOM_SDR == 0
    r1.xyz *= GS.AAMultiplier;
  #endif
  
  //edge detect
  r0.xyz = -r1.xyz + r0.xyz;
  r0.y = max(abs(r0.y), abs(r0.z));
  r0.x = max(abs(r0.x), r0.y);
  r0.x = cmp(0.150000006 < r0.x);
  o0.x = r0.x ? 1.000000 : 0;

  //3rd
  r0.xyz = g_texture.Sample(g_sampler_s, v2.xy).xyz;
  #if CUSTOM_SDR == 0
    r0.xyz *= GS.AAMultiplier;
  #endif

  //edge detect
  r0.xyz = r0.xyz + -r1.xyz;
  r0.y = max(abs(r0.y), abs(r0.z));
  r0.x = max(abs(r0.x), r0.y);
  r0.x = cmp(0.150000006 < r0.x);
  o0.y = r0.x ? 1.000000 : 0;

  o0.zw = float2(0,0);
  return;
}