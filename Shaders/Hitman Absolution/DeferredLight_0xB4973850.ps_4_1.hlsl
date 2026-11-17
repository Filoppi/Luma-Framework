#include "Includes/DeferredLightSpecular.hlsl"
cbuffer View : register(b1) { float4 cb1[13]; }
cbuffer Light : register(b2) { float4 cb2[20]; }
SamplerState s0 : register(s0), s1 : register(s1), s2 : register(s2);
Texture2D<float4> t0 : register(t0), t1 : register(t1), t2 : register(t2);
Texture2DMS<float4, 4> t3 : register(t3), t4 : register(t4), t5 : register(t5), t6 : register(t6);
float4 main(float4 pos : SV_POSITION, float4 v1 : TEXCOORD0, float4 v2 : TEXCOORD1, uint sampleIndex : SV_SampleIndex) : SV_TARGET
{
    int2 pixel = int2(pos.xy);
    float3 normal = t4.Load(pixel, sampleIndex).xyz - 0.5;
    normal = normal + normal;
    normal = normalize(normal);
    float3 ray = (1.0 / v1.w) * v2.xyz;
    float depth = 1.0 / (cb1[12].z * t3.Load(pixel, sampleIndex).x - cb1[12].y);
    float3 lightVector = cb2[8].xyz - depth * ray;
    float3 viewPos = ray * depth;
    float distanceToLight = length(lightVector);
    float3 lightDir = normalize(lightVector);
    float ndotl = dot(normal, lightDir);
    float3 reflection = lightDir - 2.0 * ndotl * normal;
    float reflectionDot = saturate(dot(normalize(-viewPos), -reflection));
    float4 material = t5.Load(pixel, sampleIndex);
    float3 params = t6.Load(pixel, sampleIndex).xyw;
    float2 lutUV = float2(saturate(ndotl), dot(float2(reflectionDot, material.w), float2(0.248046875, 0.75390625)));
    float4 lut = t0.Sample(s0, lutUV);
    float specular = HMADeferredSpecularPower((lut.w * 1024.0 - 1000.0) * params.y);
    float luminance = dot(material.xyz, float3(0.447058498859, 0.882354021072, 0.170587494969));
    float3 back = (2.0 * material.xyz - luminance) * (saturate(-ndotl) * params.z * (1.0 - cb2[3].w));
    float3 color = (params.x * specular + back) * cb2[19].x + lut.xyz * material.xyz;
    float4 projection = viewPos.x * cb2[11] + viewPos.y * cb2[12] + viewPos.z * cb2[13] + cb2[14];
    float2 uv = projection.xy / projection.w;
    float3 illumination = (projection.z > 0.0 ? 1.0 : 0.0) * cb2[3].xyz;
    float2 attenuation = saturate(cb2[0].yw * distanceToLight + cb2[0].xz);
    illumination *= attenuation.x * attenuation.y;
    float2 shadow = t2.SampleLevel(s2, uv, 0).xy;
    float2 projectorUV = saturate(uv) * cb2[10].xy + cb2[10].zw;
    illumination *= t1.SampleLevel(s1, projectorUV, -2).xyz;
    float variance = min(saturate(-shadow.x * shadow.x + shadow.y) + 0.0004, 1.0);
    float shadowFactor = saturate((shadow.x - distanceToLight * cb2[8].w) * cb2[19].y * rsqrt(variance) + 1.0);
    shadowFactor = cb2[3].w * (1.0 - shadowFactor) + shadowFactor;
    return float4(color * (shadowFactor * illumination), 0.0);
}


