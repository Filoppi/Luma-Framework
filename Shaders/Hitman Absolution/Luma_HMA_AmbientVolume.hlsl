#include "Includes/Common.hlsl"

cbuffer SharedPerViewData : register(b1) { float4 cb1[13]; };
cbuffer SharedLightData : register(b2) { float4 cb2[19]; };
cbuffer GlobalsData : register(b4) { float4 cb4[3]; };
Texture2D<float4> mapShadowColorPointSampled : register(t0);
SamplerState samplerShadowColorPointSampled : register(s0);

void main(float4 position : SV_POSITION, float4 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
  float4 r0, r1;
  r0.xy = v1.xy / v1.ww;
  r1.xy = mapShadowColorPointSampled.Sample(samplerShadowColorPointSampled, r0.xy).zw;
  r0.w = dot(r1.xy, float2(1.0, 0.00392156886));
  r0.w = r0.w * cb1[12].w;
  r1.xyz = cb4[2].xyz - cb4[1].xyz;
  r0.z = 1.0;
  r0.xyz = r0.xyz * r1.xyz + cb4[1].xyz;
  r0.xyz = r0.www * r0.xyz;
  r1.xyz = r0.yyy * cb2[16].xzy;
  r0.xyw = r0.xxx * cb2[15].xzy + r1.xyz;
  r0.xyz = r0.zzz * cb2[17].xzy + r0.xyw;
  r0.xyz = r0.xyz + cb2[18].xzy;
  r1.xyz = saturate(cb2[7].xyz * r0.xyz - cb2[5].xyz);
  r0.xyz = saturate(cb2[6].xyz * r0.xyz - cb2[4].xyz);
  r0.xyz = r0.xyz + r1.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = min(r0.x, 1.0);
  r0.x = 1.0 - r0.x;
  o0.xyz = r0.xxx * cb2[3].xyz;
#if TONEMAP_TYPE >= 1
  o0.xyz = saturate(o0.xyz);
#endif
  o0.w = 1.0;
}
