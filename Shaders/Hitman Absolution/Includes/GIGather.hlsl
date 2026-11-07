#include "Common.hlsl"
cbuffer Globals_cb4 : register(b4) { float4 cb4[24]; };
cbuffer Shared_cb1 : register(b1) { float4 cb1[16]; };
Texture2D<float4> lpvR : register(t0);
Texture2D<float4> lpvG : register(t1);
Texture2D<float4> lpvB : register(t2);
SamplerState sampR : register(s0);
SamplerState sampG : register(s1);
SamplerState sampB : register(s2);
#if GI_SINGLE
Texture2D<float4> depthTex : register(t3);
Texture2D<float4> normalTex : register(t4);
Texture2D<float4> diffuseTex : register(t5);
SamplerState sampDepth : register(s3);
SamplerState sampNormal : register(s4);
SamplerState sampDiffuse : register(s5);
#else
Texture2DMS<float4, GI_SAMPLES> depthTex : register(t3);
Texture2DMS<float4, GI_SAMPLES> normalTex : register(t4);
Texture2DMS<float4, GI_SAMPLES> diffuseTex : register(t5);
#endif
#if TONEMAP_TYPE >= 1
Texture2D<float> aoTex : register(t6);
#endif
float4 main(float4 pos : SV_POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0
#if GI_EDGE
, uint sampleIndex : SV_SampleIndex
#endif
) : SV_TARGET
{
#if GI_SINGLE
  float depth = depthTex.SampleLevel(sampDepth, uv, 0).x;
  float3 normal = normalTex.SampleLevel(sampNormal, uv, 0).xyz;
  float3 diffuse = diffuseTex.SampleLevel(sampDiffuse, uv, 0).xyz;
#else
#if GI_EDGE
  uint sample = sampleIndex;
  float depth = depthTex.Load(int2(pos.xy), sample).y;
#else
  uint sample = 0;
  float depth = depthTex.Load(int2(pos.xy), sample).x;
#endif
  float3 normal = normalTex.Load(int2(pos.xy), sample).xyz;
  float3 diffuse = diffuseTex.Load(int2(pos.xy), sample).xyz;
#endif
  float3 ray = float3(uv, 1) * (cb1[15].xyz - cb1[14].xyz) + cb1[14].xyz;
  float3 view = ray / (cb1[12].z * depth - cb1[12].y);
  float3 p = view.y * cb4[21].xyz + view.x * cb4[20].xyz;
  p = view.z * cb4[22].xyz + p + cb4[23].xyz;
  float3 edge = saturate((abs(p) - cb4[2].x) / (1 - cb4[2].x));
  edge = edge * edge * (3 - 2 * edge);
  float fade = 1 - max(max(edge.x, edge.y), edge.z);
  normal = (normal - 0.5) * 2;
  float3 n = -normal.y * cb4[21].yzx + -normal.x * cb4[20].yzx;
  n = -normal.z * cb4[22].yzx + n;
  n = normalize(n);
  float4 sh = float4(0.886227, -1.023327 * n.x, 1.023327 * n.y, -1.023327 * n.z);
  float3 coords = p + 1;
  float x = coords.x * 0.5;
  float z = coords.z * cb4[3].z * 0.5;
  float slice = floor(z);
  float fraction = frac(z);
  float y = coords.y * 0.5 + slice;
  float2 uv0 = float2(x, y / cb4[3].z);
  float2 uv1 = float2(x, (y + 1) / cb4[3].z);
  float4 r = (1 - fraction) * lpvR.Sample(sampR, uv0) + fraction * lpvR.Sample(sampR, uv1);
  float4 g = (1 - fraction) * lpvG.Sample(sampG, uv0) + fraction * lpvG.Sample(sampG, uv1);
  float4 b = (1 - fraction) * lpvB.Sample(sampB, uv0) + fraction * lpvB.Sample(sampB, uv1);
  float3 result = fade * max(float3(dot(sh, r), dot(sh, g), dot(sh, b)), 0) * diffuse;
  result = any(p <= -1) || any(p >= 1) ? 0 : result;
#if TONEMAP_TYPE >= 1
  uint width, height;
  aoTex.GetDimensions(width, height);
  float visibility = 1;
  if (width != 0 && height != 0)
  {
    int2 aoCoord = min(int2(saturate(uv) * float2(width, height)), int2(width - 1, height - 1));
    float ao = saturate(aoTex.Load(int3(aoCoord, 0)));
    visibility = ao * ao;
  }
  result *= LumaSettings.GameSettings.GIStrength * lerp(1, visibility, LumaSettings.GameSettings.GIOcclusion);
#endif
  return float4(result, 0);
}



