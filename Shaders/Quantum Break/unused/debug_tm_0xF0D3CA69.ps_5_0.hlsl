#include "../Includes/tonemap.hlsli"

// ---- Created with 3Dmigoto v1.4.1 on Mon Jan 27 21:38:18 2025

struct MaterialData
{
   float3 vSpecularColor;          // Offset:    0
   float fOcclusion;               // Offset:   12
   float2 vShadowSoftnessRange;    // Offset:   16
   float2 vScatterWidthRange;      // Offset:   24
   float2 vTranslucencyDepthRange; // Offset:   32
   uint uBRDF_uScatterIndex;       // Offset:   40
   uint uHash;                     // Offset:   44
};



cbuffer materialid_debug : register(b1)
{
   uint g_uMaterialDebugMode : packoffset(c0);
   bool g_bVisualizeColorClipping : packoffset(c0.y);
   float2 g_vColorClippingMinMax : packoffset(c0.z);
}

SamplerState g_sGBuffer1_s : register(s0);
SamplerState g_sGBuffer2_s : register(s1);
SamplerState g_sLinearClamp_s : register(s2);
SamplerState g_sBaseColorCorrectionMap_s : register(s3);
Texture2D<float4> g_sGBuffer1 : register(t0);
Texture2D<float4> g_sGBuffer2 : register(t1);
Texture2D<float4> g_sBaseColorCorrectionMap : register(t2);
Texture2D<float4> g_tTranslucencyKernelSource : register(t3);
StructuredBuffer<MaterialData> g_sbMaterialData : register(t4);
Texture2D<float> g_tBrightness : register(t5);
Texture2D<float4> g_tColorBuffer : register(t6);
Texture2D<float4> g_tLightBufferInputDiffuse : register(t7);
Texture2D<float4> g_tLightBufferInputSpecular : register(t8);
Texture2D<float4> g_tLinearDepthBuffer : register(t9);

// 3Dmigoto declarations
#define cmp -

void main(float4 v0: SV_Position0, out float4 o0: SV_Target0)
{
   const float4 icb[] = { { 0, 0, 0, 0 },
                          { 0, 0, 0.666667, 0 },
                          { 0, 0.666667, 0, 0 },
                          { 0, 0.666667, 0.666667, 0 },
                          { 0.666667, 0, 0, 0 },
                          { 0.666667, 0, 0.666667, 0 },
                          { 0.666667, 0.333333, 0, 0 },
                          { 0.666667, 0.666667, 0.666667, 0 },
                          { 0.333333, 0.333333, 0.333333, 0 },
                          { 0.333333, 0.333333, 1.000000, 0 },
                          { 0.333333, 1.000000, 0.333333, 0 },
                          { 0.333333, 1.000000, 1.000000, 0 },
                          { 1.000000, 0.333333, 0.333333, 0 },
                          { 1.000000, 0.333333, 1.000000, 0 },
                          { 1.000000, 1.000000, 0.333333, 0 },
                          { 1.000000, 1.000000, 1.000000, 0 } };
   float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xy = v0.xy / g_vScreenRes.xy;
   r0.zw = v0.xy / g_vOutputRes.xy;
   r1.xyzw = g_sGBuffer1.Sample(g_sGBuffer1_s, r0.zw).xyzw;
   r2.xyzw = g_sGBuffer2.Sample(g_sGBuffer2_s, r0.zw).xyzw;
   r3.xyz = g_tLightBufferInputDiffuse.Sample(g_sLinearClamp_s, r0.xy).xyz;
   r4.xyz = g_tLightBufferInputSpecular.Sample(g_sLinearClamp_s, r0.xy).xyz;
   r0.z = g_tBrightness.Load(int3(1, 0, 0));
   r5.xy = float2(-0.5, -0.5) + r0.xy;
   r5.xy = float2(1.70000005, 0.956250012) * r5.xy;
   r0.w = dot(r5.xy, r5.xy);
   r0.w = sqrt(r0.w);
   r0.w = 1 + -r0.w;
   r0.w = max(0, r0.w);
   r0.w = 0.0500000007 + r0.w;
   r0.w = log2(r0.w);
   r0.w = g_fVignetteExp * r0.w;
   r0.w = exp2(r0.w);
   r0.w = 1.04999995 * r0.w;
   r0.w = min(1, r0.w);
   r2.zw = float2(255, 255) * r2.zw;
   r2.zw = (uint2)r2.zw;
   r2.z = (uint)r2.z << 8;
   r2.z = (int)r2.w | (int)r2.z;
   r5.x = g_sbMaterialData[r2.z].vSpecularColor.x;
   r5.y = g_sbMaterialData[r2.z].vSpecularColor.y;
   r5.z = g_sbMaterialData[r2.z].vSpecularColor.z;
   r5.w = g_sbMaterialData[r2.z].fOcclusion;
   r6.x = g_sbMaterialData[r2.z].vShadowSoftnessRange.x;
   r6.y = g_sbMaterialData[r2.z].vShadowSoftnessRange.y;
   r6.z = g_sbMaterialData[r2.z].vScatterWidthRange.x;
   r6.w = g_sbMaterialData[r2.z].vScatterWidthRange.y;
   r7.x = g_sbMaterialData[r2.z].vTranslucencyDepthRange.x;
   r7.y = g_sbMaterialData[r2.z].vTranslucencyDepthRange.y;
   r7.z = g_sbMaterialData[r2.z].uBRDF_uScatterIndex;
   switch (g_uMaterialDebugMode)
   {
   case 1:
      r2.w = (int)r2.z & 0x80000000;
      r2.z = max((int)-r2.z, (int)r2.z);
      r2.z = (int)r2.z & 15;
      r3.w = -(int)r2.z;
      r2.z = r2.w ? r3.w : r2.z;
      r8.xyz = icb[r2.z + 0].xyz;
      break;
   case 2:
      r2.z = (int)r7.z & 0x0000ffff;
      r2.y = r2.z ? 1 : r2.y;
      r2.yzw = r5.xyz * r2.yyy;
      r5.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r2.yzw);
      r9.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r2.yzw;
      r2.yzw = log2(r2.yzw);
      r2.yzw = float3(0.416666657, 0.416666657, 0.416666657) * r2.yzw;
      r2.yzw = exp2(r2.yzw);
      r2.yzw =
          r2.yzw * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      r8.xyz = r5.xyz ? r9.xyz : r2.yzw;
      break;
   case 3:
      r8.xyz = r5.www;
      break;
   case 4:
      r2.y = r6.w + -r6.z;
      r8.xyz = r2.xxx * r2.yyy + r6.zzz;
      break;
   case 5:
      if (4 == 0)
         r2.y = 0;
      else if (4 + 16 < 32)
      {
         r2.y = (uint)r7.z << (32 - (4 + 16));
         r2.y = (uint)r2.y >> (32 - 4);
      }
      else
         r2.y = (uint)r7.z >> 16;
      r8.xyz = icb[r2.y + 0].xyz;
      break;
   case 6:
      r2.y = (int)r7.z & 15;
      r8.xyz = icb[r2.y + 0].xyz;
      break;
   case 7:
      r2.y = r6.y + -r6.x;
      r8.xyz = r2.xxx * r2.yyy + r6.xxx;
      break;
   case 8:
      r2.y = r7.y + -r7.x;
      r8.xyz = r2.xxx * r2.yyy + r7.xxx;
      break;
   case 9:
      r2.y = r7.y + -r7.x;
      r2.x = r2.x * r2.y + r7.x;
      r2.y = (uint)r7.z >> 16;
      r5.x = 0.071419999 + r2.x;
      r2.x = (uint)r2.y;
      r2.x = 0.5 + r2.x;
      r5.y = g_fOnePerTranslucencyKernelCount * r2.x;
      r2.xyz = g_tTranslucencyKernelSource.SampleLevel(g_sLinearClamp_s, r5.xy, 0).xyz;
      r5.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r2.xyz);
      r6.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r2.xyz;
      r2.xyz = log2(r2.xyz);
      r2.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r2.xyz;
      r2.xyz = exp2(r2.xyz);
      r2.xyz =
          r2.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      r8.xyz = r5.xyz ? r6.xyz : r2.xyz;
      break;
   case 1:
      r1.w = r1.w * 255 + 0.5;
      r1.w = (uint)r1.w;
      r1.w = (int)r1.w & 254;
      r1.w = (uint)r1.w;
      r1.w = 0.5 + r1.w;
      r1.w = (int)r1.w;
      if (3 == 0)
         r2.x = 0;
      else if (3 + 1 < 32)
      {
         r2.x = (uint)r1.w << (32 - (3 + 1));
         r2.x = (uint)r2.x >> (32 - 3);
      }
      else
         r2.x = (uint)r1.w >> 1;
      r1.w = (uint)r1.w >> 4;
      r2.xz = (int2)r2.xx;
      r2.yw = (int2)r1.ww;
      r2.xyzw = float4(0.5, 0.5, 0.5, 0.5) + r2.xyzw;
      r2.xyzw = r2.xyzw * float4(0.000490196107, 0.000245098054, 0.000490196107, 0.000245098054) + r1.xyxy;
      r2.xyzw = r2.xyzw * float4(2, 2, 2, 2) + float4(-1, -1, -1, -1);
      r2.xyzw = float4(1.29999995, 1.29999995, 2.5999999, 2.5999999) * r2.xyzw;
      r1.x = dot(r2.xy, r2.xy);
      r2.xy = float2(1, -1) + r1.xx;
      r1.xyw = r2.zwy / r2.xxx;
      r8.xyz = r1.xyw * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);
      break;
   case 1:
      r8.xyz = r1.zzz;
      break;
   case 1:
      r1.xyz = r3.xyz * r0.zzz;
      r1.xyz = r1.xyz * r0.www;
      r1.xyz = max(float3(0, 0, 0), r1.xyz);
      r1.xyz = g_fTonemapKeyValue * r1.xyz;
      r1.w = dot(r1.xyz, float3(0.270000011, 0.670000017, 0.0599999987));
      r2.x = r1.w * 0.015625 + 1;
      r1.w = 1 + r1.w;
      r1.w = r2.x / r1.w;
      r1.xyz = saturate(r1.xyz * r1.www);
      r2.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r1.xyz);
      r5.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r1.xyz;
      r1.xyz = log2(r1.xyz);
      r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
      r1.xyz = exp2(r1.xyz);
      r1.xyz =
          r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      r1.xyz = r2.xyz ? r5.xyz : r1.xyz;
      r1.z = 32 * r1.z;
      r1.x = r1.x * 0.03125 + 0.00048828125;
      r1.x = max(0.00048828125, r1.x);
      r1.y = 0.015625 + r1.y;
      r1.y = max(0.015625, r1.y);
      r2.z = min(0.984375, r1.y);
      r1.y = floor(r1.z);
      r1.y = 0.03125 * r1.y;
      r1.y = max(0, r1.y);
      r1.xy = min(float2(0.0307617188, 0.96875), r1.xy);
      r2.y = r1.x + r1.y;
      r5.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r2.yz).xyz;
      r1.y = ceil(r1.z);
      r1.y = 0.03125 * r1.y;
      r1.y = max(0, r1.y);
      r1.y = min(0.96875, r1.y);
      r2.x = r1.x + r1.y;
      r1.xyw = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r2.xz).xyz;
      r1.z = frac(r1.z);
      r1.xyw = r1.xyw + -r5.xyz;
      r1.xyz = r1.zzz * r1.xyw + r5.xyz;
      r8.xyz = g_fTonemapBrightness * r1.xyz;
      break;
   case 1:
      r1.xyz = r4.xyz * r0.zzz;
      r1.xyz = r1.xyz * r0.www;
      r1.xyz = max(float3(0, 0, 0), r1.xyz);
      r1.xyz = g_fTonemapKeyValue * r1.xyz;
      r1.w = dot(r1.xyz, float3(0.270000011, 0.670000017, 0.0599999987));
      r2.x = r1.w * 0.015625 + 1;
      r1.w = 1 + r1.w;
      r1.w = r2.x / r1.w;
      r1.xyz = saturate(r1.xyz * r1.www);
      r2.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r1.xyz);
      r5.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r1.xyz;
      r1.xyz = log2(r1.xyz);
      r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
      r1.xyz = exp2(r1.xyz);
      r1.xyz =
          r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      r1.xyz = r2.xyz ? r5.xyz : r1.xyz;
      r1.z = 32 * r1.z;
      r1.x = r1.x * 0.03125 + 0.00048828125;
      r1.x = max(0.00048828125, r1.x);
      r1.y = 0.015625 + r1.y;
      r1.y = max(0.015625, r1.y);
      r2.z = min(0.984375, r1.y);
      r1.y = floor(r1.z);
      r1.y = 0.03125 * r1.y;
      r1.y = max(0, r1.y);
      r1.xy = min(float2(0.0307617188, 0.96875), r1.xy);
      r2.y = r1.x + r1.y;
      r5.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r2.yz).xyz;
      r1.y = ceil(r1.z);
      r1.y = 0.03125 * r1.y;
      r1.y = max(0, r1.y);
      r1.y = min(0.96875, r1.y);
      r2.x = r1.x + r1.y;
      r1.xyw = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r2.xz).xyz;
      r1.z = frac(r1.z);
      r1.xyw = r1.xyw + -r5.xyz;
      r1.xyz = r1.zzz * r1.xyw + r5.xyz;
      r8.xyz = g_fTonemapBrightness * r1.xyz;
      break;
   case 1:
      r1.xyz = r4.xyz + r3.xyz;
      r1.xyz = r1.xyz * r0.zzz;
      r1.xyz = r1.xyz * r0.www;
      r1.xyz = max(float3(0, 0, 0), r1.xyz);
      r1.xyz = g_fTonemapKeyValue * r1.xyz;
      r0.z = dot(r1.xyz, float3(0.270000011, 0.670000017, 0.0599999987));
      r0.w = r0.z * 0.015625 + 1;
      r0.z = 1 + r0.z;
      r0.z = r0.w / r0.z;
      r1.xyz = saturate(r1.xyz * r0.zzz);
      r2.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r1.xyz);
      r3.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r1.xyz;
      r1.xyz = log2(r1.xyz);
      r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
      r1.xyz = exp2(r1.xyz);
      r1.xyz =
          r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      r1.xyz = r2.xyz ? r3.xyz : r1.xyz;
      r0.z = 32 * r1.z;
      r0.w = r1.x * 0.03125 + 0.00048828125;
      r0.w = max(0.00048828125, r0.w);
      r0.w = min(0.0307617188, r0.w);
      r1.x = 0.015625 + r1.y;
      r1.x = max(0.015625, r1.x);
      r1.w = floor(r0.z);
      r1.w = 0.03125 * r1.w;
      r1.w = max(0, r1.w);
      r1.zw = min(float2(0.984375, 0.96875), r1.xw);
      r1.y = r1.w + r0.w;
      r2.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.yz).xyz;
      r1.y = ceil(r0.z);
      r1.y = 0.03125 * r1.y;
      r1.y = max(0, r1.y);
      r1.y = min(0.96875, r1.y);
      r1.x = r1.y + r0.w;
      r1.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.xz).xyz;
      r0.z = frac(r0.z);
      r1.xyz = r1.xyz + -r2.xyz;
      r1.xyz = r0.zzz * r1.xyz + r2.xyz;
      r8.xyz = g_fTonemapBrightness * r1.xyz;
      break;
   case 1:
      r0.z = g_tLinearDepthBuffer.Sample(g_sLinearClamp_s, r0.xy).x;
      r0.z = 0.00200000009 * r0.z;
      r0.z = rsqrt(abs(r0.z));
      r8.x = 1 / r0.z;
      r8.yz = float2(0, 0);
      break;
   default:
      r8.xyz = g_tColorBuffer.Sample(g_sLinearClamp_s, r0.xy).xyz;
      break;
   }
   r0.x = dot(r8.xyz, float3(0.308600008, 0.609399974, 0.0820000023));
   r0.y = cmp(r0.x < g_vColorClippingMinMax.x);
   r0.yzw = r0.yyy ? float3(1, 0, 0) : r8.xyz;
   r0.x = cmp(g_vColorClippingMinMax.y < r0.x);
   r0.xyz = r0.xxx ? float3(0, 0, 1) : r0.yzw;
   o0.xyz = g_bVisualizeColorClipping ? r0.xyz : r8.xyz;
   o0.w = 1;
   return;
}
