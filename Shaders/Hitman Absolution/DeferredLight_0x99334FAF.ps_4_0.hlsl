#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"

cbuffer SharedPerViewData : register(b1) { float4 cb1[13]; };
cbuffer SharedLightData : register(b2) { float4 cb2[20]; };
Texture2D<float4> t0 : register(t0); Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2); Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4); Texture2D<float4> t5 : register(t5);
Texture2D<float4> t6 : register(t6);
SamplerState s0 : register(s0); SamplerState s1 : register(s1);
SamplerState s2 : register(s2); SamplerState s3 : register(s3);
SamplerState s4 : register(s4); SamplerState s5 : register(s5);
SamplerState s6 : register(s6);

void main(float4 v0 : SV_POSITION, float4 v1 : TEXCOORD0, float3 v2 : TEXCOORD1, out float4 o0 : SV_TARGET0)
{
  float invW = 1.0 / v1.w;
  float2 uv = invW * v1.xy;
  float3 ray = invW * v2;
  float3 n = t4.Sample(s4, uv).xyz - 0.5;
  n = n + n;
  n *= rsqrt(dot(n, n));
  float depth = t3.Sample(s3, uv).x;
  float d = 1.0 / (cb1[12].z * depth - cb1[12].y);
  float3 lightVector = -d * ray + cb2[8].xyz;
  float3 pos = ray * d;
  float dist2 = dot(lightVector, lightVector);
  float dist = sqrt(dist2);
  lightVector *= rsqrt(dist2);
  float ndotl = dot(n, lightVector);
  float3 reflected = -(ndotl + ndotl) * n + lightVector;
  float3 view = -pos * rsqrt(dot(-pos, -pos));
  float spec = saturate(dot(view, -reflected));
  float4 gb2 = t5.Sample(s5, uv);
  float4 gb3 = t6.Sample(s6, uv);
#if TONEMAP_TYPE >= 1
  // The upgraded target stores HDR color in gb2.rgb, but gb3 is packed UNORM material data.
  gb3 = saturate(gb3);
#endif
  float lutY = dot(float2(spec, gb2.w), float2(0.248047, 0.753906));
  float4 lut = t0.Sample(s0, float2(saturate(ndotl), lutY));
  float specPow = HMADeferredSpecularPower((lut.w * 1024.0 - 1000.0) * gb3.y);
  float lum = dot(gb2.xyz, float3(0.447058, 0.882354, 0.170587));
  float3 rim = gb2.xyz * 2.0 - lum;
  float rimScale = (1.0 - cb2[3].w) * gb3.w;
  rimScale = saturate(-ndotl) * rimScale;
  float3 light = (gb3.x * specPow + rim * rimScale) * cb2[19].x;
  light = lut.xyz * gb2.xyz + light;
  float4 q = pos.y * cb2[12];
  q = pos.x * cb2[11] + q;
  q = pos.z * cb2[13] + q;
  q += cb2[14];
  float front = q.z > 0.0 ? 1.0 : 0.0;
  float2 puv = q.xy / q.w;
  float3 projected = front * cb2[3].xyz;
  float2 attenuation = saturate(cb2[0].yw * dist + cb2[0].xz);
  projected *= attenuation.y * attenuation.x;
  float2 clampedUV = saturate(puv);
  float4 shadow = t2.SampleLevel(s2, puv, 0.0);
  float2 projectorUV = clampedUV * cb2[10].xy + cb2[10].zw;
  float3 projector = t1.SampleLevel(s1, projectorUV, -2.0).xyz * projected;
  float delta = -dist * cb2[8].w + shadow.x;
  float variance = saturate(-shadow.x * shadow.x + shadow.y);
  variance = min(variance + 0.0004, 1.0);
  delta *= cb2[19].y;
  float shadowFactor = saturate(delta * rsqrt(variance) + 1.0);
  shadowFactor = cb2[3].w * (1.0 - shadowFactor) + shadowFactor;
  o0.xyz = light * (shadowFactor * projector);
  o0.w = 0.0;
}

