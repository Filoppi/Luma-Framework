#include "Includes/Common.hlsl"

cbuffer DrawableBuffer : register(b1)
{
  float4 FogColor : packoffset(c0);
  float4 DebugColor : packoffset(c1);
  float MaterialOpacity : packoffset(c2);
  float AlphaThreshold : packoffset(c3);
}

cbuffer SceneBuffer : register(b2)
{
  row_major float4x4 View : packoffset(c0);
  row_major float4x4 ScreenMatrix : packoffset(c4);
  float2 DepthExportScale : packoffset(c8);
  float2 FogScaleOffset : packoffset(c9);
  float3 CameraPosition : packoffset(c10);
  float3 CameraDirection : packoffset(c11);
  float3 DepthFactors : packoffset(c12);
  float2 ShadowDepthBias : packoffset(c13);
  float4 SubframeViewport : packoffset(c14);
  row_major float3x4 DepthToWorld : packoffset(c15);
  float4 DepthToView : packoffset(c18);
  float4 OneOverDepthToView : packoffset(c19);
  float4 DepthToW : packoffset(c20);
  float4 ClipPlane : packoffset(c21);
  float2 ViewportDepthScaleOffset : packoffset(c22);
  float2 ColorDOFDepthScaleOffset : packoffset(c23);
  float2 TimeVector : packoffset(c24);
  float3 HeightFogParams : packoffset(c25);
  float3 GlobalAmbient : packoffset(c26);
  float4 GlobalParams[16] : packoffset(c27);
  float DX3_SSAOScale : packoffset(c43);
  float4 ScreenExtents : packoffset(c44);
  float2 ScreenResolution : packoffset(c45);
  float4 PSSMToMap1Lin : packoffset(c46);
  float4 PSSMToMap1Const : packoffset(c47);
  float4 PSSMToMap2Lin : packoffset(c48);
  float4 PSSMToMap2Const : packoffset(c49);
  float4 PSSMToMap3Lin : packoffset(c50);
  float4 PSSMToMap3Const : packoffset(c51);
  float4 PSSMDistances : packoffset(c52);
  row_major float4x4 WorldToPSSM0 : packoffset(c53);
  float StereoOffset : packoffset(c25.w);
}

cbuffer InstanceBuffer : register(b5)
{
  float4 InstanceParams[8] : packoffset(c0);
}

SamplerState p_default_Material_098651E4334612755_DepthBufferTexture_sampler_s : register(s0);
SamplerState p_default_Material_0D4489A4339784082_NormalBufferTexture_sampler_s : register(s1);
Texture2D<float4> p_default_Material_098651E4334612755_DepthBufferTexture_texture : register(t0);
Texture2D<float4> p_default_Material_0D4489A4339784082_NormalBufferTexture_texture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  r1.xyz = p_default_Material_0D4489A4339784082_NormalBufferTexture_texture.Sample(p_default_Material_0D4489A4339784082_NormalBufferTexture_sampler_s, r0.xy).xyz;
  r1.xyz = float3(-0.5,-0.5,-0.5) + r1.xyz;
  r1.xyz = r1.xyz + r1.xyz;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = rsqrt(r0.z);
  r1.xyz = r0.zzz * r1.xyz;
  r2.xyz = InstanceParams[1].xyz * r1.yyy;
  r1.xyw = r1.xxx * InstanceParams[0].xyz + r2.xyz;
  r1.xyz = r1.zzz * InstanceParams[2].xyz + r1.xyw;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = rsqrt(r0.z);
  r0.z = -r1.z * r0.z + 1;
  r0.z = 0.5 * r0.z;
  r0.z = r0.z * r0.z;
  r1.xy = r0.xy * DepthToView.xy + DepthToView.zw;
  r0.x = p_default_Material_098651E4334612755_DepthBufferTexture_texture.Sample(p_default_Material_098651E4334612755_DepthBufferTexture_sampler_s, r0.xy).x;
  r0.x = r0.x * DepthToW.x + DepthToW.y;
  r0.x = 1 / r0.x;
  r0.yw = r0.xx * r1.xy;
  r1.xyz = InstanceParams[1].xyz * r0.www;
  r1.xyz = r0.yyy * InstanceParams[0].xyz + r1.xyz;
  r0.xyw = r0.xxx * InstanceParams[2].xyz + r1.xyz;
  r0.xyw = InstanceParams[3].zxy + r0.wxy;
  r1.x = 1 + -r0.x;
// TODO: test this one, we aren't sure it looks good with the fix enabled (untested)
// Luma: rare DC variant of "0x0AB7755C"/"0x4C48AF67" (identical except for the N dot L).
// It already uses a squared half Lambert N dot L (a DC style change, like in the ambient light), so that's kept as it is.
// Like the other DC box lights, it didn't saturate the distance falloff (it could go beyond 1), unlike the original game, so that's restored.
#if FIX_DC_LIGHTING // Luma: original game's saturated falloff
  r1.x = saturate(InstanceParams[5].y * r1.x);
#else // DC unbounded falloff
  r1.x = InstanceParams[5].y * r1.x;
  r1.x = max(0, r1.x);
#endif
  r1.y = (r0.x < 0);
  r1.x = r1.y ? 0 : r1.x;
  r0.z = r1.x * r0.z;
  r0.yw = float2(1,1) + -abs(r0.yw);
  r0.x = saturate(r0.x);
  r0.x = saturate(InstanceParams[5].z * r0.x);
  r0.yw = saturate(InstanceParams[5].xx * r0.yw);
  r0.y = r0.y * r0.z;
  r0.y = r0.y * r0.w;
  r0.x = r0.y * r0.x;
  o0.xyz = InstanceParams[4].xyz * r0.xxx;
  o0.w = MaterialOpacity;
}
