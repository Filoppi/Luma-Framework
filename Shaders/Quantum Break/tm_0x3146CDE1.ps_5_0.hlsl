#include "./Includes/tonemap.hlsli"

// ---- Created with 3Dmigoto v1.4.1 on Mon Jan 27 21:37:52 2025

cbuffer bloomx86_compose : register(b1)
{
   float2 g_vBloomedImageRes : packoffset(c0);
   float g_fLensTextureIntensity : packoffset(c0.z);
}

SamplerState g_sBaseColorCorrectionMap_s : register(s0);
SamplerState g_sOriginalImage_s : register(s1);
SamplerState g_sBloomedImage_s : register(s2);
SamplerState g_sLensTexture_s : register(s3);
Texture2D<float4> g_sOriginalImage : register(t0);
Texture2D<float4> g_sBloomedImage : register(t1);
Texture2D<float4> g_sLensTexture : register(t2);
Texture2D<float4> g_sBaseColorCorrectionMap : register(t3);
Texture2D<float> g_tBrightness : register(t4);

// 3Dmigoto declarations
#define cmp -

void main(float2 v0: TEXCOORD0, float4 v1: SV_Position0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xy = float2(-0.5, -0.5) + v0.xy;
   r0.xy = float2(1.70000005, 0.956250012) * r0.xy;
   r0.x = dot(r0.xy, r0.xy);
   r0.x = sqrt(r0.x);
   r0.x = 1 + -r0.x;
   r0.x = max(0, r0.x);
   r0.x = 0.0500000007 + r0.x;
   r0.x = log2(r0.x);
   r0.x = g_fVignetteExp * r0.x;
   r0.x = exp2(r0.x);
   r0.x = 1.04999995 * r0.x;
   r0.x = min(1, r0.x);
   r1.xyzw = g_sLensTexture.Sample(g_sLensTexture_s, v0.xy).xyzw;
   r2.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, v0.xy).xyzw;
   r1.xyzw = r2.xyzw * r1.xyzw;
   r1.xyzw = r1.xyzw * float4(2, 2, 2, 2) + -r2.xyzw;
   r1.xyzw = g_fLensTextureIntensity * r1.xyzw + r2.xyzw;
   r2.xyzw = g_sOriginalImage.Sample(g_sOriginalImage_s, v0.xy).xyzw;
   r1.xyzw = r2.xyzw + r1.xyzw;
   r0.y = g_tBrightness.Load(int3(1, 0, 0));
   r0.yzw = r1.xyz * r0.yyy;
   o0.w = r1.w;
   r0.xyz = r0.yzw * r0.xxx;
   r0.xyz = max(float3(0, 0, 0), r0.xyz);
   r0.xyz = g_fTonemapKeyValue * r0.xyz;
// tonemap
#if 1
   r0.rgb = ApplyToneMap(r0.xyz);
#else
   r0.w = dot(r0.xyz, float3(0.270000011, 0.670000017, 0.0599999987));
   r1.x = r0.w * 0.015625 + 1;
   r0.w = 1 + r0.w;
   r0.w = r1.x / r0.w;
   r0.xyz = saturate(r0.xyz * r0.www);
#endif
#if 1
   r0.rgb = SRGBEncodeAndSample2DLUT(r0.rgb, g_sBaseColorCorrectionMap, g_sBaseColorCorrectionMap_s);
#else
   r1.xyz = log2(r0.xyz);
   r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
   r2.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r0.xyz);
   r0.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r0.xyz;
   r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
   r0.y = 0.015625 + r0.y;
   r0.y = max(0.015625, r0.y);
   r1.z = min(0.984375, r0.y);
   r0.x = r0.x * 0.03125 + 0.00048828125;
   r0.y = 32 * r0.z;
   r0.x = max(0.00048828125, r0.x);
   r0.z = ceil(r0.y);
   r0.z = 0.03125 * r0.z;
   r0.z = max(0, r0.z);
   r0.xz = min(float2(0.0307617188, 0.96875), r0.xz);
   r1.x = r0.x + r0.z;
   r2.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.xz).xyz;
   r0.z = floor(r0.y);
   r0.y = frac(r0.y);
   r0.z = 0.03125 * r0.z;
   r0.z = max(0, r0.z);
   r0.z = min(0.96875, r0.z);
   r1.y = r0.x + r0.z;
   r0.xzw = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.yz).xyz;
   r1.xyz = r2.xyz + -r0.xzw;
   r0.xyz = r0.yyy * r1.xyz + r0.xzw;
#endif
   o0.xyz = g_fTonemapBrightness * r0.xyz;
   return;
}

