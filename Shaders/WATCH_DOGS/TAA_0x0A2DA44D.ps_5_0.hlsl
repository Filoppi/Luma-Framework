// ---- Created with 3Dmigoto v1.3.16 on Sat Mar 07 13:02:13 2026

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

cbuffer DeferredFXAntialias : register(b1)
{
  float4 _Params0 : packoffset(c0);
  float4 _Params1 : packoffset(c1);
  float4 _Resolution : packoffset(c2);
}

#include "Includes/Common.hlsl"

SamplerState Viewport__DepthVPSampler__SampObj___s : register(s0);
SamplerState DeferredFXAntialias__FrameBufferTexture__SampObj___s : register(s1);
SamplerState DeferredFXAntialias__GBufferVelocityTexture__SampObj___s : register(s2);
SamplerState DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s : register(s3);
Texture2D<float4> Viewport__DepthVPSampler__TexObj__ : register(t0);
Texture2D<float4> DeferredFXAntialias__FrameBufferTexture__TexObj__ : register(t1);
Texture2D<float4> DeferredFXAntialias__GBufferVelocityTexture__TexObj__ : register(t2);
Texture2D<float4> DeferredFXAntialias__PrevFrameBufferTexture__TexObj__ : register(t3);


// 3Dmigoto declarations
#define cmp -

float3 UVToEye(float2 uv, float eye_z)
{
    uv = _Params0.xy * uv + _Params0.zw;
	//uv = float2(_InvProjectionMatrix._m00, -_InvProjectionMatrix._m11) * 2.0 * uv + float2(-_InvProjectionMatrix._m00, _InvProjectionMatrix._m11);
    return float3(uv * eye_z, eye_z);
}

// TODO_VELOCITYBUFFER?: use SampleDepthWS?
float3 DepthBufferToEyePos(float2 uv)
{
	float depth			= Viewport__DepthVPSampler__TexObj__.Sample(Viewport__DepthVPSampler__SampObj___s, uv.xy).x;;
    float z = _Params1.y / (depth - _Params1.x);
    return UVToEye(uv + LumaData.GameData.CurrJitters, z);
}

// Calculate the pixel's UV-space movement since last frame due to camera movement, and its view-space depth
void CalculateCameraBasedVelocity_ViewSpaceDepth(out float2 velocity, out float viewSpaceDepth, in const float2 uv)
{
    float3 eyePos   = DepthBufferToEyePos(uv);

    float3 worldPos =  mul( float4(eyePos.xy,-eyePos.z,1) , _InvViewMatrix);

    float4 prevProj = mul( float4(worldPos,1) , _PreviousViewProjectionMatrix);
	
    prevProj /= prevProj.w;

    float2 prevUV = prevProj.xy * float2(0.5f,-0.5f) + 0.5f;
 
    velocity = -(prevUV - (uv));

    viewSpaceDepth = eyePos.z;
}


float3 rgb_to_ycocg(float3 color)
{
    const float y = dot(color, float3(0.25, 0.5, 0.25));
    const float co = dot(color, float3(0.5, 0.0, -0.5));
    const float cg = dot(color, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 ycocg_to_rgb(float3 color)
{
    const float r = dot(color, float3(1.0, 1.0, -1.0));
    const float g = dot(color, float3(1.0, 0.0, 1.0));
    const float b = dot(color, float3(1.0, -1.0, -1.0));
    return float3(r, g, b);
}

float3 clip_to_aabb(float3 color, float3 minc, float3 maxc)
{
    const float3 center = (minc + maxc) * 0.5;
    const float3 extent = (maxc - minc) * 0.5 + 1e-3;
    const float3 offset = color - center;
    const float3 units = abs(offset * rcp(extent));
    const float max_unit = max(max(units.x, units.y), max(units.z, 1.0));
    return center + offset * rcp(max_unit);
}

float4 SampleTextureCatmullRom(in float2 uv, in float2 texSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos0.x, texPos0.y)) * w0.x * w0.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos12.x, texPos0.y)) * w12.x * w0.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos3.x, texPos0.y)) * w3.x * w0.y;

    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos0.x, texPos12.y)) * w0.x * w12.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos12.x, texPos12.y)) * w12.x * w12.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos3.x, texPos12.y)) * w3.x * w12.y;

    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos0.x, texPos3.y)) * w0.x * w3.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos12.x, texPos3.y)) * w12.x * w3.y;
    result += DeferredFXAntialias__PrevFrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__PrevFrameBufferTexture__SampObj___s, float2(texPos3.x, texPos3.y)) * w3.x * w3.y;

    return result;
}

// Objects to be excluded from certain velocity effects signal it by adding a large offset to the velocity red channel.
#define VELOCITYBUFFER_MASK_OFFSET_RED      2.f

// Threshold used to detect that VELOCITYBUFFER_MASK_OFFSET_RED was applied to a velocity buffer sample.
#define VELOCITYBUFFER_MASK_THRESHOLD_RED   (VELOCITYBUFFER_MASK_OFFSET_RED * 0.5f)

// Default value of the velocity buffer's green channel, indicating that the pixel should be ignored as it doesn't represent any velocity of a dynamic object.
// See CFrameRendererBase::ms_GBufferClearColourNextGen
#define VELOCITYBUFFER_DEFAULT_GREEN        -1.f

#define FRAME_VELOCITY_IN_PIXELS_DIFF 128   //valid for 1920x1080

// MIN - MAX variance gamma, it's lerped using a velocity confidence factor
#define MIN_VARIANCE_GAMMA 0.75f // under motion
#define MAX_VARIANCE_GAMMA 2.f // no motion

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0,
  out float2 o1 : SV_Target1)
{
	float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14;
	uint4 bitmask, uiDest;
	float4 fDest;

	float2 jitterDelta = LumaData.GameData.CurrJitters - LumaData.GameData.PrevJitters;

	float2  velocity;
	float velocityDepth;

	CalculateCameraBasedVelocity_ViewSpaceDepth(velocity, velocityDepth, v1.xy);

	float4 gBufferVelocity = DeferredFXAntialias__GBufferVelocityTexture__TexObj__.Sample(DeferredFXAntialias__GBufferVelocityTexture__SampObj___s, v1.xy);

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
    
    //velocity = 0;

	const float velocityConfidenceFactor = saturate( float( 1.f ) - length( velocity.xy * _ViewportSize.xy ) / FRAME_VELOCITY_IN_PIXELS_DIFF );

	const float2 prevFrameScreenUV = v1.xy - velocity.xy;

	//const float prevFrameDepth = Viewport__DepthVPSampler__TexObj__.Sample(Viewport__DepthVPSampler__SampObj___s, prevFrameScreenUV.xy).x;

	float uvWeight = 1.0;
	if (any(saturate(prevFrameScreenUV) != prevFrameScreenUV))
	{
	  uvWeight = 0.0;
	}

	float3 currentFrameColour = DeferredFXAntialias__FrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__FrameBufferTexture__SampObj___s, v1.xy).xyz;
	currentFrameColour = max(currentFrameColour, float3(0,0,0));

	const bool hasValidHistory = ( velocityConfidenceFactor * uvWeight ) > 0.f;

	if ( true == hasValidHistory )
	{
		float4 rawHistoryColour = SampleTextureCatmullRom(prevFrameScreenUV, _ViewportSize.xy);
		rawHistoryColour = max(rawHistoryColour, float4(0,0,0,0));
		
		const float varianceGamma = float( lerp( MIN_VARIANCE_GAMMA, MAX_VARIANCE_GAMMA, velocityConfidenceFactor * velocityConfidenceFactor ) );
		
		// Neighborhood offsets.
		const int2 offsets[8] = {
			int2(-1, -1),
			int2(0, -1),
			int2(1, -1),
			int2(-1, 0),
			int2(1, 0),
			int2(-1, 1),
			int2(0, 1),
			int2(1, 1)
		};
		
        const int iteratorMax = 8;
        const float rcpDivider = 1.f / 9.f;
		
		float3 currentColourInYCoCg = (currentFrameColour);
		
        float3 moment1 = currentColourInYCoCg;
        float3 moment2 = currentColourInYCoCg * currentColourInYCoCg;
		
		float3 toReturn = rawHistoryColour.xyz;
		
        [unroll]
        for ( int i = 0; i < iteratorMax; ++i )
        {
            const float2 uv = v1.xy + offsets[i] * _ViewportSize.zw;
            const float3 newColour = ( max(DeferredFXAntialias__FrameBufferTexture__TexObj__.Sample(DeferredFXAntialias__FrameBufferTexture__SampObj___s, uv).xyz, float3(0,0,0)) );
            moment1 += newColour;
            moment2 += newColour * newColour;
        }

        // mean is the center of AABB and variance (standard deviation) is its extents
        const float3 mean = moment1 * rcpDivider;
        const float3 variance = sqrt( moment2 * rcpDivider - mean * mean ) * varianceGamma;
		
        // clamp to AABB min/max
        const float3 minC = float3( mean - variance );
        const float3 maxC = float3( mean + variance );

        toReturn = clamp( rawHistoryColour.xyz, ( minC ), ( maxC ) );
		
		rawHistoryColour.xyz = toReturn;
		
		const float weight = rawHistoryColour.a * velocityConfidenceFactor;
		
		const float newWeight = saturate( float( 1.f ) / ( float( 2.f ) - weight ) );
		
		o0 = float4(lerp(currentFrameColour, rawHistoryColour.xyz, weight), newWeight);
		
		o1 = velocity;
		
		return;
	}
	else
	{
		o0 = float4(currentFrameColour, 0.5f);
		o1 = velocity;
		return;
	}

	return;
}