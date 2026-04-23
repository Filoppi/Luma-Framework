#include "./Includes/tonemap.hlsli"

// ---- Created with 3Dmigoto v1.4.1 on Mon Jan 27 21:38:00 2025

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
Texture2D<float4> g_tObjectHighlightTexture : register(t5);

// 3Dmigoto declarations
#define cmp -

void main(float2 v0: TEXCOORD0, float4 v1: SV_Position0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3, r4, r5;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xy = float2(0.5, 0.5) / g_vBloomedImageRes.xy;
   r0.zw = -r0.yx;
   r1.xyzw = v0.xyxy + r0.xzwy;
   r2.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r1.xy).xyzw;
   r1.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r1.zw).xyzw;
   r0.zw = v0.xy * g_vBloomedImageRes.xy + float2(-0.5, -0.5);
   r0.zw = frac(r0.zw);
   r3.xy = float2(1, 1) + -r0.wz;
   r3.zw = r3.xy * r0.zw;
   r0.z = r0.z * r0.w;
   r0.w = r3.y * r3.x;
   r2.xyzw = r3.zzzz * r2.xyzw;
   r3.xy = v0.xy + -r0.xy;
   r0.xy = v0.xy + r0.xy;
   r4.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r0.xy).xyzw;
   r5.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r3.xy).xyzw;
   r2.xyzw = r0.wwww * r5.xyzw + r2.xyzw;
   r1.xyzw = r3.wwww * r1.xyzw + r2.xyzw;
   r0.xyzw = r0.zzzz * r4.xyzw + r1.xyzw;
   r1.xyzw = g_sLensTexture.Sample(g_sLensTexture_s, v0.xy).xyzw;
   r1.xyzw = r1.xyzw * r0.xyzw;
   r1.xyzw = r1.xyzw * float4(2, 2, 2, 2) + -r0.xyzw;
   r0.xyzw = g_fLensTextureIntensity * r1.xyzw + r0.xyzw;
   r1.xyzw = g_sOriginalImage.Sample(g_sOriginalImage_s, v0.xy).xyzw;
   r0.xyzw = r1.xyzw + r0.xyzw;
   r1.x = g_tBrightness.Load(int3(1, 0, 0));
   r0.xyz = r1.xxx * r0.xyz;
   o0.w = r0.w;
   r1.xy = float2(-0.5, -0.5) + v0.xy;
   r1.xy = float2(1.70000005, 0.956250012) * r1.xy;
   r0.w = dot(r1.xy, r1.xy);
   r0.w = sqrt(r0.w);
   r0.w = 1 + -r0.w;
   r0.w = max(0, r0.w);
   r0.w = 0.0500000007 + r0.w;
   r0.w = log2(r0.w);
   r0.w = g_fVignetteExp * r0.w;
   r0.w = exp2(r0.w);
   r0.w = 1.04999995 * r0.w;
   r0.w = min(1, r0.w);
   r0.xyz = r0.xyz * r0.www;
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
   r0.xyz = g_fTonemapBrightness * r0.xyz;
   r1.xy = (uint2)v1.xy;
   r1.zw = float2(0, 0);
   r1.xyzw = g_tObjectHighlightTexture.Load(r1.xyz).xyzw;
   o0.xyz = r0.xyz * r1.www + r1.xyz;
   return;
}

