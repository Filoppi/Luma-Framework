#include "Includes/Common.hlsl"

cbuffer SharedPerViewData : register(b1) { float4 cb1[13]; };
cbuffer SharedLightData : register(b2) { float4 cb2[20]; };
Texture2D<float4> t0 : register(t0); Texture2D<float4> t1 : register(t1);
Texture2DMS<float4> t2 : register(t2); Texture2DMS<float4> t3 : register(t3);
Texture2DMS<float4> t4 : register(t4); Texture2DMS<float4> t5 : register(t5);
SamplerState s0 : register(s0); SamplerState s1 : register(s1);

void main(float4 v0 : SV_POSITION, float4 v1 : TEXCOORD0, float3 v2 : TEXCOORD1, uint sampleIndex : SV_SampleIndex, out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);
  int sampleI = int(sampleIndex);
  float invW = 1.0 / v1.w;
  float3 ray = invW * v2;
  float3 n = t3.Load(pixel, sampleI).xyz;
#if TONEMAP_TYPE >= 1
  n = saturate(n);
#endif
  n = n - 0.5;
  n = n + n;
  n *= rsqrt(dot(n, n));
  float ndotl = dot(n, cb2[9].xyz);
  float3 reflected = -(ndotl + ndotl) * n + cb2[9].xyz;
  float depth = t2.Load(pixel, sampleI).x;
  float d = 1.0 / (cb1[12].z * depth - cb1[12].y);
  float3 pos = ray * d;
  float3 view = -pos * rsqrt(dot(-pos, -pos));
  float spec = saturate(dot(view, -reflected));
  float4 color = t4.Load(pixel, sampleI);
#if TONEMAP_TYPE >= 1
  color = saturate(color);
#endif
  float2 material = t5.Load(pixel, sampleI).xy;
#if TONEMAP_TYPE >= 1
  material = saturate(material);
#endif
  float lutY = dot(float2(spec, color.w), float2(0.248047, 0.753906));
  float4 lut = t0.Sample(s0, float2(saturate(ndotl), lutY));
  float specPow = exp2((lut.w * 1024.0 - 1000.0) * material.y);
  float3 light = lut.xyz * color.xyz + material.x * specPow * cb2[19].x;
  float4 q = pos.y * cb2[16];
  q = pos.x * cb2[15] + q;
  q = pos.z * cb2[17] + q;
  q += cb2[18];
  float3 proj = q.xyz / q.w;
  float3 fadePos = saturate(cb2[7].xyz * proj - cb2[5].xyz);
  float3 fadeNeg = saturate(cb2[6].xyz * proj - cb2[4].xyz);
  float2 projectorUV = proj.xy * cb2[10].xy + cb2[10].zw;
  float3 projector = t1.SampleLevel(s1, projectorUV, -2.0).xyz * cb2[3].xyz;
  float3 fade = fadePos + fadeNeg;
  float attenuation = 1.0 - min(sqrt(dot(fade, fade)), 1.0);
  o0.xyz = light * (attenuation * projector);
  o0.w = 0.0;
}
