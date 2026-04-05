#include "Includes/Common.hlsl"

cbuffer Viewport : register(b0)
{
  float4x4 _ViewRotProjectionMatrix : packoffset(c0);
  float4x4 _ViewProjectionMatrix : packoffset(c4);
  float4x4 _ProjectionMatrix : packoffset(c8);
  float4x4 _InvProjectionMatrix : packoffset(c12);
  float4x4 _InvProjectionMatrixDepth : packoffset(c16);
  float4x4 _DepthTextureTransform : packoffset(c20);
  float4x3 _ViewMatrix : packoffset(c24);
  float4x3 _InvViewMatrix : packoffset(c27);
  float4x4 _PreviousViewProjectionMatrix : packoffset(c30);
  float4 _CameraDistances : packoffset(c34);
  float4 _ViewportSize : packoffset(c35);
  float4 _CameraPosition_MaxStaticReflectionMipIndex : packoffset(c36);
  float4 _CameraDirection_MaxParaboloidReflectionMipIndex : packoffset(c37);
  float4 _ViewPoint_ExposureScale : packoffset(c38);
  float4 _FogColorVector_ExposedWhitePointOverExposureScale : packoffset(c39);
  float3 _SideFogColor : packoffset(c40);
  float3 _SunFogColorDelta : packoffset(c41);
  float3 _OppositeFogColorDelta : packoffset(c42);
  float4 _FogValues0 : packoffset(c43);
  float4 _FogValues1 : packoffset(c44);
  float4 _CameraNearPlaneSize : packoffset(c45);
  float4 _UncompressDepthWeights_ShadowProjDepthMinValue : packoffset(c46);
  float4 _UncompressDepthWeightsWS_ReflectionFadeTarget : packoffset(c47);
  float4 _WorldAmbientColorParams0 : packoffset(c48);
  float4 _WorldAmbientColorParams1 : packoffset(c49);
  float4 _WorldAmbientColorParams2 : packoffset(c50);
  float4 _GlobalWorldTextureParams : packoffset(c51);
  float4 _CullingCameraPosition_OneOverAutoExposureScale : packoffset(c52);
  float4 _AmbientSkyColor_ReflectionScaleStrength : packoffset(c53);
  float4 _AmbientGroundColor_ReflectionScaleDistanceMul : packoffset(c54);
  float4 _FacettedShadowCastParams : packoffset(c55);
  float4 _FSMClipPlanes : packoffset(c56);
  float2 _ReflectionGIControl : packoffset(c57);
}

Texture2D<float4> g_sourceColor : register(t0);
RWTexture2D<float4> g_outputColor : register(u0);
RWTexture2D<float4> g_srColor : register(u1);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(_ViewportSize.xy)))
	{
		return;
	}
	
	float4 beforeRain = g_outputColor[tid];
	float4 afterRain = g_sourceColor.Load(int3(tid.xy, 0));
	
	beforeRain = max(g_srColor[tid] + (afterRain - beforeRain), (float4)0.0);
	
	g_outputColor[tid] = float4(beforeRain.xyz, afterRain.w);
}