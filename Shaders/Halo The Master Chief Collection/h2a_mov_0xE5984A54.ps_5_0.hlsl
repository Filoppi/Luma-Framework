// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 14:26:31 2026

SamplerState PS_SAMPLERS_4__s : register(s4);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0);
Texture2D<float4> PS_TEXTURES_2D_2_ : register(t2);
Texture2D<float4> PS_TEXTURES_2D_3_ : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = PS_TEXTURES_2D_3_.Sample(PS_SAMPLERS_4__s, v1.xy).x;
  r0.xyz = float3(0,-0.391448975,2.01782227) * r0.xxx;
  r0.w = PS_TEXTURES_2D_2_.Sample(PS_SAMPLERS_4__s, v1.xy).x;
  r0.xyz = r0.www * float3(1.59579468,-0.813476563,0) + r0.xyz;
  r0.w = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_4__s, v1.xy).x;
  r0.xyz = r0.www * float3(1.16412354,1.16412354,1.16412354) + r0.xyz;
  o0.xyz = float3(-0.87065506,0.529705048,-1.08166885) + r0.xyz;
  o0.xyz = saturate(o0.xyz);
  o0.xyz = RenderIntermediatePass(o0.xyz);
  o0.w = 1;
  return;
}