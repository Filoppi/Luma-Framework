#include "./common.hlsli"

// ---- Created with 3Dmigoto v1.4.1 on Mon Jan 27 21:38:16 2025

cbuffer embedded_shapeengine : register(b0)
{
   float4x4 g_mShapeEngineColorTransform : packoffset(c0);
   float4 g_vShapeEngineColorAdd : packoffset(c4);
}

SamplerState g_sTexture_s : register(s0);
Texture2D<float4> g_sTexture : register(t0);

// 3Dmigoto declarations
#define cmp -

void main(float3 v0: TEXCOORD0, float4 v1: COLOR0, out float4 o0: SV_Target0)
{
   float4 r0, r1;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xyzw = g_sTexture.Sample(g_sTexture_s, v0.xy).xyzw;
   r0.xyzw = v1.xyzw * r0.xyzw;
   r1.xyzw = g_mShapeEngineColorTransform._m01_m11_m21_m31 * r0.yyyy;
   r1.xyzw = g_mShapeEngineColorTransform._m00_m10_m20_m30 * r0.xxxx + r1.xyzw;
   r1.xyzw = g_mShapeEngineColorTransform._m02_m12_m22_m32 * r0.zzzz + r1.xyzw;
   r0.xyzw = g_mShapeEngineColorTransform._m03_m13_m23_m33 * r0.wwww + r1.xyzw;
   o0.xyzw = g_vShapeEngineColorAdd.xyzw + r0.xyzw;
   return;
}
