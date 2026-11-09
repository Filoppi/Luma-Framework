#include "Includes/DeferredLightSpecular.hlsl"
cbuffer View : register(b1) { float4 cb1[13]; }
cbuffer Light : register(b2) { float4 cb2[20]; }
SamplerState s0 : register(s0), s1 : register(s1);
Texture2D<float4> t0 : register(t0), t1 : register(t1);
Texture2DMS<float4, 4> t2 : register(t2), t3 : register(t3), t4 : register(t4), t5 : register(t5);
float4 main(float4 pos : SV_POSITION, float4 v1 : TEXCOORD0, float3 v2 : TEXCOORD1, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    int2 pixel = int2(pos.xy);
    float3 normal = t3.Load(pixel, sampleIndex).xyz - 0.5;
    normal = normal + normal;
    normal = normalize(normal);
    float ndotl = dot(normal, cb2[9].xyz);
    float3 reflection = cb2[9].xyz - 2.0 * ndotl * normal;
    float3 ray = (1.0 / v1.w) * v2;
    float depth = 1.0 / (cb1[12].z * t2.Load(pixel, sampleIndex).x - cb1[12].y);
    float3 viewPos = ray * depth;
    float spec = saturate(dot(normalize(-viewPos), -reflection));
    float4 material = t4.Load(pixel, sampleIndex);
    float2 params = t5.Load(pixel, sampleIndex).xy;
    float2 lutUV = float2(saturate(ndotl), dot(float2(spec, material.w), float2(0.248046875, 0.75390625)));
    float4 lut = t0.Sample(s0, lutUV);
    float power = HMADeferredSpecularPower((lut.w * 1024.0 - 1000.0) * params.y);
    float3 color = lut.xyz * material.xyz + power * params.x * cb2[19].x;
    float3 projection = viewPos.y * cb2[16].xyw + viewPos.x * cb2[15].xyw + viewPos.z * cb2[17].xyw + cb2[18].xyw;
    float2 uv = projection.xy / projection.z * cb2[10].xy + cb2[10].zw;
    float3 illumination = t1.SampleLevel(s1, uv, -2).xyz * cb2[3].xyz;
    return float4(color * illumination, 0.0);
}
