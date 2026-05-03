#include "Includes/Common.hlsl"

cbuffer cbViewport : register(b0)
{
  float4 CameraNearPlaneSize : packoffset(c0);
  float4x4 DepthTextureTransform : packoffset(c1);
  float4 FSMClipPlanes : packoffset(c5);
  float4 FacettedShadowCastParams : packoffset(c6);
  float4 FogValues0 : packoffset(c7);
  float4 FogValues1 : packoffset(c8);
  float4x4 InvProjectionMatrix : packoffset(c9);
  float4x4 InvProjectionMatrixDepth : packoffset(c13);
  float4x3 InvViewMatrix : packoffset(c17);
  float4x4 PreviousViewProjectionMatrix : packoffset(c20);
  float4x4 ProjectionMatrix : packoffset(c24);
  float4 RainOcclusionFadeParams : packoffset(c28);
  float4x4 RainOcclusionProjectionMatrix : packoffset(c29);
  float4 RainOcclusionShadowMapSize : packoffset(c33);
  float4 ReflectionVolumeDebugColors[15] : packoffset(c34);
  float4 VPosOffset : packoffset(c49);
  float4 VPosScale : packoffset(c50);
  float4x3 ViewMatrix : packoffset(c51);
  float4x4 ViewProjectionMatrix : packoffset(c54);
  float4x4 ViewRotProjectionMatrix : packoffset(c58);
  float4x4 ViewRotProjectionMatrixPure : packoffset(c62);
  float4 ViewportSize : packoffset(c66);

  struct SCameraDistances
  {
    float near;
    float far;
    float view;
    float oneOverView;
  } CameraDistances : packoffset(c67);


  struct SReflectionVolume
  {
    float4x4 inverseTransform;
    float3 rcpFadeRangePositive;
    float textureArrayIndexAsFloat;
    float3 rcpFadeRangeNegative;
    float fadeFactor;
    float2 multipliers;
    uint parallaxCorrection;
    float padding0;
  } ReflectionVolumes[15] : packoffset(c68);

  float3 CameraDirection : packoffset(c173);
  float DefaultReflectionTextureArrayIndexAsFloat : packoffset(c173.w);
  float3 CameraPosition : packoffset(c174);
  float DynamicCubeMapReflectionTextureMaxMipIndex : packoffset(c174.w);
  float3 CullingCameraPosition : packoffset(c175);
  float ExposedWhitePointOverExposureScale : packoffset(c175.w);
  float3 FogColorVector : packoffset(c176);
  float ExposureScale : packoffset(c176.w);
  float3 OppositeFogColorDelta : packoffset(c177);
  float MaxParaboloidReflectionMipIndex : packoffset(c177.w);
  float3 SideFogColor : packoffset(c178);
  float MaxStaticReflectionMipIndex : packoffset(c178.w);
  float3 SunFogColorDelta : packoffset(c179);
  float MeasuredExposureScale : packoffset(c179.w);
  float3 TemporalFilteringParams : packoffset(c180);
  float RaindropRippleScale : packoffset(c180.w);
  float3 UncompressDepthWeights : packoffset(c181);
  float ReflectionScaleDistanceMul : packoffset(c181.w);
  float3 UncompressDepthWeightsWS : packoffset(c182);
  float ReflectionScaleStrength : packoffset(c182.w);
  float3 ViewPoint : packoffset(c183);
  float SkyParaboloidTextureMaxMipIndex : packoffset(c183.w);
  float2 DefaultReflectionMultipliers : packoffset(c184);
  bool UseOnlySkyReflection : packoffset(c184.z);
  float2 ReflectionGIControl : packoffset(c185);
  uint2 SelectedPixel : packoffset(c185.z);
}

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float2> velocityTexture :  register(t1);
Texture2D<float4> sourcePostFXMaskTexture :  register(t2);

SamplerState s_depthSampler : register(s0);


struct SPixelOutput
{
    float2 color : SV_Target0;
	float4 mask : SV_Target1;
    float  depth : SV_Depth;
}; 

float2 MaxVelocity(in float2 maxVel, in float2 vel)
{
    return dot(maxVel, maxVel) > dot(vel, vel) ? maxVel : vel;
}

static const int2 Offsets[4] =
{
	int2(  0,  1 ),
	int2(  0, -1 ),
	int2( -1,  0 ),
	int2(  1,  0 )
};

SPixelOutput main(
	float4 pos : SV_Position
	)
{
    SPixelOutput output = (SPixelOutput) 0;
    
	int2 pixelPos = int2(pos.xy);
	
	float2 centerVel = velocityTexture.Load(int3(pixelPos, 0)).xy;
    float2 motionCombined[4];
	float4 centerMask = sourcePostFXMaskTexture.Load(int3(pixelPos, 0));
	float4 maskCombined[4];
	uint i = 0;
	
	[unroll]
    for (i = 0; i < 4; ++i)
    {
        float2 motionVectors = velocityTexture.Load(int3(pos.xy + Offsets[i], 0)).xy;
        motionCombined[i] = motionVectors;
		
        float4 mask = sourcePostFXMaskTexture.Load(int3(pos.xy + Offsets[i], 0)).xyzw;
        maskCombined[i] = mask;
    }
	
	float2 maxVel = MaxVelocity(motionCombined[0], motionCombined[1]);
    maxVel = MaxVelocity(maxVel, motionCombined[2]);
    maxVel = MaxVelocity(maxVel, motionCombined[3]);
	output.color = MaxVelocity(maxVel, centerVel);

	output.mask = max(max(centerMask, maskCombined[0]), max(max(maskCombined[1], maskCombined[2]), maskCombined[3]));

	float depth = sourceTexture.Load(int3(pos.xy, 0)).x;
	output.depth = depth;
	
	return output;
}