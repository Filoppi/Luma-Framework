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

Texture2D<float4> DeferredFXAntialias__GBufferVelocityTexture__TexObj__ : register(t0);
Texture2D<float4> Viewport__DepthVPSampler__TexObj__ : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);

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
	float rawDepthValue = Viewport__DepthVPSampler__TexObj__[xy].x;
    float2 ndcXY = uv * 2.0 - 1.0;
    ndcXY.y = -ndcXY.y;
    
    float4 clipPos = float4(ndcXY, rawDepthValue, 1.0);
    float4 viewPos = mul(clipPos, _InvProjectionMatrix);
    
    return viewPos.xyz / viewPos.w;
}

// Calculate the pixel's UV-space movement since last frame due to camera movement, and its view-space depth
void CalculateCameraBasedVelocity_ViewSpaceDepth(out float2 velocity, in const float2 uv, in const uint2 xy)
{
    float3 eyePos   = UVToView(uv, xy);

    float3 worldPos =  mul( float4(eyePos.xy,eyePos.z,1) , _InvViewMatrix);

    float4 prevProj = mul( float4(worldPos,1) , _PreviousViewProjectionMatrix);
	
    prevProj /= prevProj.w;

    float2 prevUV = prevProj.xy * float2(0.5f,-0.5f) + 0.5f;
 
    velocity = (uv - prevUV);
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(_ViewportSize.xy)))
	{
		return;
	}
	
	float2  velocity;
	float2 pixelUV = ((float2)tid + 0.5f) * _ViewportSize.zw;
	CalculateCameraBasedVelocity_ViewSpaceDepth(velocity, pixelUV, tid);
	
	float2 jitterDelta = LumaData.GameData.CurrJitters;
	
	float4 gBufferVelocity = DeferredFXAntialias__GBufferVelocityTexture__TexObj__[tid];
	
	bool isDynamicObject = (gBufferVelocity.g != VELOCITYBUFFER_DEFAULT_GREEN);
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
	  //velocity += jitterDelta;
	}

	velocity += jitterDelta;

	g_updatedVelocityTex[tid] = velocity;
}