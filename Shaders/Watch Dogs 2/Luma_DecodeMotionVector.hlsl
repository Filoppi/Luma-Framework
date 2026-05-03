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

Texture2D<float4> g_gBufferVelocityTex : register(t0);
Texture2D<float4> g_depthTex : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);
RWTexture2D<float> g_unjitteredDepthTex : register(u1);
SamplerState s_depthSampler : register(s0);

// Objects to be excluded from certain velocity effects signal it by adding a large offset to the velocity red channel.
#define VELOCITYBUFFER_MASK_OFFSET_RED      2.f

// Threshold used to detect that VELOCITYBUFFER_MASK_OFFSET_RED was applied to a velocity buffer sample.
#define VELOCITYBUFFER_MASK_THRESHOLD_RED   (VELOCITYBUFFER_MASK_OFFSET_RED * 0.5f)

// Default value of the velocity buffer's green channel, indicating that the pixel should be ignored as it doesn't represent any velocity of a dynamic object.
// See CFrameRendererBase::ms_GBufferClearColourNextGen
#define VELOCITYBUFFER_DEFAULT_GREEN        -1.f

#define FRAME_VELOCITY_IN_PIXELS_DIFF 128   //valid for 1920x1080

float3 UVToView(float2 uv, uint2 xy)
{   
	float rawDepthValue = g_depthTex[xy].x;
    float2 ndcXY = uv * 2.0 - 1.0;
    ndcXY.y = -ndcXY.y;
    
    float4 clipPos = float4(ndcXY, rawDepthValue, 1.0);
    float4 viewPos = mul(clipPos, InvProjectionMatrix);
    
    return viewPos.xyz / viewPos.w;

}

// Calculate the pixel's UV-space movement since last frame due to camera movement, and its view-space depth
void CalculateCameraBasedVelocity_ViewSpaceDepth(out float2 velocity, in const float2 uv, in const uint2 xy)
{
	float depth = g_depthTex.Load(int3((int2)xy, 0)).x;
	float4 depth4 = mul( float4(0.0f, 0.0f, depth, 1.0f), InvProjectionMatrix );
	float worldDepth = -depth4.z / depth4.w;

	float2 uv_color = (xy + 0.5) * ViewportSize.zw;
	float4 positionCS;
	positionCS.xy = (uv_color - 0.5) * float2(2.0, -2.0);
	positionCS.xy *= CameraNearPlaneSize.xy * 0.5f;
	positionCS.z = -CameraDistances.near;
	positionCS.w = 1.0f;
	
	float3 currentPosCS;
	currentPosCS.xyz = positionCS.xyz;
	currentPosCS.xyz *= -worldDepth / currentPosCS.z;
	
	float4 projectedPosition = mul( float4(currentPosCS, 1.0), ProjectionMatrix );
	
    //float3 worldPos =  mul( float4(currentPosCS.xy,currentPosCS.z,1) , InvViewMatrix);
    float4 previousPos2D = mul( float4(currentPosCS,1) , LumaData.GameData.CameraSpaceToPreviousProjectedSpace);
	//float4 previousPos2D = mul( float4(worldPos - LumaData.GameData.PreviousCameraPosition.xyz,1) , LumaData.GameData.PreviousViewRotProjectionMatrix );
	previousPos2D /= max( previousPos2D.w, 0.00001f );
	
	float3 currentPos2D = projectedPosition.xyz / projectedPosition.w;
	
	float3 velocityVector = 0;
	
	velocityVector = currentPos2D.xyz - previousPos2D.xyz;
	velocityVector.xy /= 2.0;
	velocityVector.y = -velocityVector.y;
	
    velocity = velocityVector.xy;
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(ViewportSize.xy)))
	{
		return;
	}
	
	float2  velocity;
	float2 pixelUV = ((float2)tid + 0.5f) * ViewportSize.zw;
	CalculateCameraBasedVelocity_ViewSpaceDepth(velocity, pixelUV, tid);
	
	float2 jitterDelta = LumaData.GameData.CurrJitters - LumaData.GameData.PrevJitters;
	
	float2 gBufferVelocity = g_gBufferVelocityTex.Load(int3((int2)tid, 0)).xy;
	
	bool isDynamicObject = ( gBufferVelocity.r != 0.0f || gBufferVelocity.g != 0.0f );
    bool isExcludedObject = false;

	if (isDynamicObject)
	{
		if (gBufferVelocity.r > VELOCITYBUFFER_MASK_THRESHOLD_RED)
		{
			gBufferVelocity.r -= VELOCITYBUFFER_MASK_OFFSET_RED;
            isExcludedObject = true;
		}
        
        // The game uses it to mask out the TV screen velocity but it's still wrong!
        if (!isExcludedObject)
        {
	        velocity.xy = gBufferVelocity.xy;
        }
	}

	g_updatedVelocityTex[tid] = velocity;
	
	float4 depth4 = g_depthTex.GatherRed(s_depthSampler, pixelUV.xy - LumaData.GameData.CurrJitters*ViewportSize.zw, int2(0, 0));
	float maxDepth = max(max(depth4.x, depth4.y), max(depth4.z, depth4.w));
	
	g_unjitteredDepthTex[tid] = maxDepth;
}