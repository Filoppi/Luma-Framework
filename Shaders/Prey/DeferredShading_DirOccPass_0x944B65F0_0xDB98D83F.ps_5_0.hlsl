#include "Includes/Common.hlsl"

#include "Includes/CBuffer_PerViewGlobal.hlsl"

#if _944B65F0 // Half res depth (you can force this on/off manually if you wished so, but the game directly picks between the two based on a user setting)
#define _RT_SAMPLE0 1
#define XE_GTAO_SCALED_DEPTH_FLOAT4 1
#endif

#define PREMULTIPLY_BENT_NORMALS 1
#define XE_GTAO_ENABLE_DENOISE ENABLE_SSAO_DENOISE
#define XE_GTAO_ENCODE_BENT_NORMALS 0
#include "Includes/XeGTAO.hlsl"

cbuffer CBSSDO : register(b0)
{
  struct
  {
    float4 viewSpaceParams; // 2 * hor scale, 2 * ver scale, -1 / hor scale, -1 / ver scale
    float4 ssdoParams; // hor radius (scaled by the inverse hor FoV and far plane), ver radius (scaled by the inverse hor FoV and far plane), min radius, max radius
  } cbSSDO : packoffset(c0);
}

SamplerState ssSSDODepth : register(s0); // MIN_MAG_MIP_POINT CLAMP (as expected from SSDO and GTAO)
Texture2D<float4> _tex0_D3D11 : register(t0); // Normal maps
Texture2D<float> _tex1_D3D11 : register(t1); // The linear (non inverted) depth (previously converted from the device g-buffer depth) (0 is zero (not near), 1 far). R32F. Always set. Only drawn in the top left portion of the image if we have a render resolution scale
Texture2D<float4> _tex2_D3D11 : register(t2); // (_RT_SAMPLE0 only) The "lower quality" half (or quarter depending on "r_ssdoHalfRes", though in Prey only half resolution is exposed to user settings, even if quarter res is still likely available in config (forcing it through config doesn't apply)) resolution linear depth (previously converted from the device g-buffer depth) (average depth is stored in the alpha channel, the rest is other depth near/far scaled values). RGBA16F (all channels are seemengly the same). Always set. Only drawn in the top left portion of the image if we have a render resolution scale

float2 MapViewportToRaster(float2 normalizedViewportPos, bool bOtherEye = false)
{
	return normalizedViewportPos * CV_HPosScale.xy;
}

float3 DecodeGBufferNormal( float4 bufferA )
{
	// Normalization is needed on decoding as values would have been approximated in low precision buffers
	return normalize( bufferA.xyz * 2.0 - 1.0 ); // From 0|1 range to -1|+1
}

float GetLinearDepth(float fLinearDepth, bool bScaled = false)
{
    return fLinearDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f);
}

float GetLinearDepth(Texture2D<float> depthTexture, int3 vPixCoord, bool bScaled = false)
{
	float fDepth = depthTexture.Load(vPixCoord).x;
	return GetLinearDepth(fDepth, bScaled);
}

// LUMA FT: added this to make the code easier to read. It can't be directly changed without adapting some code,
// but it shouldn't need to be changed, as this is simply a shader optimization to run 4 different samples at once on a float4,
// instead of doing 4 times a float1.
static const int samplesGroupNum = 4;

#if _RT_SAMPLE0 
float4 SSDOFetchDepths(Texture2D<float4> _texture, float4 tc[samplesGroupNum/2], uint component)
{
	return float4( _texture.SampleLevel(ssSSDODepth, tc[0].xy, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[0].zw, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[1].xy, 0)[component],
	              _texture.SampleLevel(ssSSDODepth, tc[1].zw, 0)[component] );
}
#else
float4 SSDOFetchDepths(Texture2D<float> _texture, float4 tc[samplesGroupNum/2])
{
	return float4( _texture.SampleLevel(ssSSDODepth, tc[0].xy, 0),
	              _texture.SampleLevel(ssSSDODepth, tc[0].zw, 0),
	              _texture.SampleLevel(ssSSDODepth, tc[1].xy, 0),
	              _texture.SampleLevel(ssSSDODepth, tc[1].zw, 0) );
}
#endif

float4 GTAO(float4 WPos, float4 inBaseTC, out float edges)
{	
#if SSAO_QUALITY <= 0
	static const float sliceCount = 2; // This can't be lower than 2. Values beyond 3 have diminishing returns, but drastically reduce noise.
	static const float stepsPerSlice = 2; // This can go as low as 0 but values below 1 make no sense. Increasing this value will make AO darker unless we counter adjust its strength. Values beyond 4-5 have diminishing returns.
#elif SSAO_QUALITY == 1
	static const float sliceCount = 3; // We could possibly settle for 4-5
	static const float stepsPerSlice = 3;
#elif SSAO_QUALITY >= 2
	static const float sliceCount = 7; // 6-7 is good for high quality. XeGTAO highest quality preset went up to 9, but that seems like overkill.
	static const float stepsPerSlice = 3;
#endif
	
	GTAOConstants consts;

#if ENABLE_SSAO_TEMPORAL && ENABLE_SSAO_DENOISE
	const uint frameCounter = LumaSettings.FrameIndex;
	static const uint denoisePasses = 1; // Match this with how many times the denoiser pass will later run: "0: disabled, 1: sharp, 2: medium, 3: soft".
#elif ENABLE_SSAO_DENOISE
	static const uint frameCounter = 0;
	static const uint denoisePasses = 1;
#else
	static const uint frameCounter = 0;
	static const uint denoisePasses = 0;
#endif
	
	row_major float4x4 projectionMatrix = mul( CV_ViewProjMatr, CV_InvViewMatr ); // The current projection matrix used to be stored in "CV_PrevViewProjMatr" in vanilla Prey

	//TODO LUMA: do this in shader cbuffer or vertex shader? As optimization? It's mostly fine here
	//TODO LUMA: for full precision, add access to the native device depth buffer, and downscale it properly because using the half res version of the depth buffer (_RT_SAMPLE0) produces terrible results with a lot of AO banding due to low precision
	//TODOFT4: investigate whether the AO color bleeding implementation is good for GTAO (see "AOColorBleedRT"/"r_ssdoColorBleeding"), it seems like it simply prevents AO from applying on bright diffuse color objects but that makes no sense given that then there would be no AO in darker areas on white objects?

#if 1 // The depth in this pass was already linearized (with far matching a value of 1 and the camera origin matching a value of 0), so all we need to do is multiply by the far distance
	consts.DepthFar = CV_NearFarClipDist.y;
#elif 1
	float depthLinearizeMul = -projectionMatrix[2][3]; // float depthLinearizeMul = ( clipFar * clipNear ) / ( clipFar - clipNear );
	float depthLinearizeAdd = projectionMatrix[2][2]; // float depthLinearizeAdd = clipFar / ( clipFar - clipNear );
	if (depthLinearizeMul * depthLinearizeAdd < 0)
		depthLinearizeAdd = -depthLinearizeAdd;
	consts.DepthUnpackConsts = float2(depthLinearizeMul, depthLinearizeAdd);
#else // This seems to be slightly less accurate and more unstable (the y far is dived by the max view distance (it seems to be a relative multiplier of 10), which is different from the far, so the result is different), and requires the depth to be inverted after sampling
   	float depthLinearizeMul = (CV_NearFarClipDist.y * CV_NearFarClipDist.x) / (CV_NearFarClipDist.y - CV_NearFarClipDist.x);
    float depthLinearizeAdd = CV_NearFarClipDist.y / (CV_NearFarClipDist.y - CV_NearFarClipDist.x);
    consts.DepthUnpackConsts = float2(depthLinearizeMul, depthLinearizeAdd);
#endif

	consts.ViewportSize = (CV_ScreenSize.xy / CV_HPosScale.xy) + 0.5; // Round to int make sure it maps to the right integer (this is probably unnecessary but we do it for extra safety). This is unused by GTAO anyway
	consts.ScaledViewportMax = CV_ScreenSize.xy - 0.5;
	consts.ViewportPixelSize = CV_ScreenSize.zw * 2.0;
	consts.ScaledViewportPixelSize = 1.0 / CV_ScreenSize.xy; // These already have "CV_HPosScale.xy" baked in (render resolution), which is theoretically not correct, but saves us a multiplication by render resolution on every sample
	consts.RenderResolutionScale = CV_HPosScale.xy;
	consts.SampleUVClamp = CV_HPosClamp.xy;
#if _RT_SAMPLE0
#if 1 // Optimized branch
	consts.SampleScaledUVClamp = CV_HPosScale.xy - CV_ScreenSize.zw;
#else // Given that the depth is half or quarter resolution (with the render resolution scaled acknowledged within it), the UV clamp should be moved further up left, though quarter res can't be enabled in Prey so we disabled the check
    float2 scaledDepthSize;
    _tex2_D3D11.GetDimensions(scaledDepthSize.x, scaledDepthSize.y);
	consts.SampleScaledUVClamp = CV_HPosScale.xy - (0.5 / scaledDepthSize);
#endif
#else
	consts.SampleScaledUVClamp = consts.SampleUVClamp;
#endif
	consts.DenoiseBlurBeta = (denoisePasses==0) ? 1e4f : 1.2f; 
	consts.NoiseIndex = (denoisePasses>0) ? (frameCounter % 64) : 0; // DLSS (DLAA) as a baseline has a cycle of 8 jitters (in 8 frames), but that's not enough for GTAO, though setting it to 8 could make it a bit more stable, it'd be of lower quality
	consts.FinalValuePower = 0.4125 / (stepsPerSlice ? sqrt(stepsPerSlice / 3.0) : 1); // The most important value. Higher values make AO darker. We modulate by "stepsPerSlice" to keep the intensity consistent.
	consts.DepthMIPSamplingOffset = XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET;
	consts.ThinOccluderCompensation = XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION; // XeGTAO default is zero (none). Should be between 0 and 1 apparently. We found that to be fine for Prey too (there's not many small objects in Prey, everything is pretty big, even if indoor), enabling this causes more visual mistakes than not, with occlusions only starting to appear after it'd be expected to. Enable "XE_GTAO_EXTREME_QUALITY" if this is > 0, and possibly increase "FinalValuePower".
	consts.SampleDistributionPower = XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
	consts.EffectFalloffRange = XE_GTAO_DEFAULT_FALLOFF_RANGE; // This is not related to the current depth value. The higher the value, the larger the falloff radius will be. Expected range is 0-1 (disabled at 0). The default value looks ok, but we could go either a bit higher or lower too.
	// The second most important value.
#if 0
	consts.RadiusMultiplier = XE_GTAO_DEFAULT_RADIUS_MULTIPLIER; // This goes to multiply the radius directly, so it's basically like changing the radius directly here
	consts.EffectRadius = 0.5f; // The 0.5 default from GTAO code is too small for Prey, ambient occlusion from larger objects is completely gone. This is probably in radians.
#else // We found that using the game's native radius also looks good (and in line with SSDO), there's a chance it's dynamically changed by scene so it might be good to follow it
	// Retrieve back the original radius given it was pre-multiplied by these factors ("r_ssdoRadius" cvar, defaulted to 1.2).
	// Note that SSDO also multiplied the radius by 0.15 for some bands.
	// Going beyond this radius overly darkens occluded areas and shows a lot screen space artifacts (due to occlusion/unocclusion at the edges).
	float2 radius = (cbSSDO.ssdoParams.xy / float2(projectionMatrix[0][0], projectionMatrix[1][1])) * 2.0 * CV_NearFarClipDist.y;
	consts.EffectRadius = radius.x; // X and Y are identical so just take X
#if SSAO_RADIUS <= 0
	consts.RadiusMultiplier = 1.0;
#else
	consts.RadiusMultiplier = XE_GTAO_DEFAULT_RADIUS_MULTIPLIER; // We leave this is even when inhering the radius from the game value given that it makes it look right for GTAO (better looking in general, and closer to SSDO)
#if SSAO_RADIUS >= 2
	consts.EffectRadius *= 1.5;
#endif // SSAO_RADIUS >= 2
#endif // SSAO_RADIUS <= 0
#endif
    consts.RadiusScalingMinDepth = 8.0; // In meters (or something close)
    consts.RadiusScalingMaxDepth = 1000.0;
    consts.RadiusScalingMultiplier = 55.0; // Heuristically found to match vanilla SSDO behaviour in the distance (SSDO can still be a lot stronger far, but also uglier and more random)
	consts.MinVisibility = 0.0; //TODOFT: restore it to 0.03 as GTAO had? test it, but it seems fine as 0
	
#if 1 // Identical but faster option (if we calculated "projectionMatrix" for any other reason), possibly more reliable
	float tanHalfFOVX = 1.f / projectionMatrix[0][0];
	float tanHalfFOVY = 1.f / projectionMatrix[1][1];
#else
	float FOVX = 1.f / CV_ProjRatio.z;
	float inverseAspectRatio = CV_ScreenSize.z / CV_ScreenSize.w; // Theoretically the projection matrix aspect ratio always matches the screen aspect ratio
    float tanHalfFOVX = tan( FOVX * 0.5f );
    float tanHalfFOVY = tanHalfFOVX * inverseAspectRatio;
#endif
    consts.CameraTanHalfFOV             = float2( tanHalfFOVX, tanHalfFOVY );

#if 1 // Flip Y view (GTAO default/suggested calculations)
    consts.NDCToViewMul                 = float2( consts.CameraTanHalfFOV.x * 2.0f, consts.CameraTanHalfFOV.y * -2.0f );
    consts.NDCToViewAdd                 = float2( -consts.CameraTanHalfFOV.x, consts.CameraTanHalfFOV.y );
	static const float3 normalsConversion = float3(1, 1, -1);
    consts.NDCToViewMul_x_PixelSize     = float2( consts.NDCToViewMul.x, -consts.NDCToViewMul.y ) * consts.ScaledViewportPixelSize; // This needs to pretend we are using the rendering resolution for textures
#else // Flip X view (this seems to work equally but it feels weirder). Flipping both might also work with a different "normalsConversion" value, but there's no need to go there. Update: this doesn't work anymore, probably it was never right to begin with.
    consts.NDCToViewMul                 = float2( consts.CameraTanHalfFOV.x * -2.0f, consts.CameraTanHalfFOV.y * 2.0f );
    consts.NDCToViewAdd                 = float2( consts.CameraTanHalfFOV.x, -consts.CameraTanHalfFOV.y );
	static const float3 normalsConversion = float3(-1, -1, -1);
    consts.NDCToViewMul_x_PixelSize     = float2( -consts.NDCToViewMul.x, consts.NDCToViewMul.y ) * consts.ScaledViewportPixelSize;
#endif
	
	float2 localNoise = (denoisePasses > 0) ? SpatioTemporalNoise(WPos.xy, consts.NoiseIndex) : 0; // "ENABLE_SSAO_DENOISE"

	Texture2D<float> depthTexture = _tex1_D3D11;
#if _RT_SAMPLE0 // GTAO always samples depth with 0-1 UVs so the half res depth should natively work (though the gather samples will return depths that are further away than it expects) (the full res is always bound anyway)
	Texture2D<float4> scaledDepthTexture = _tex2_D3D11; // Note that SSDO added "0.0000001" to the depth when sampling from this one, but that doesn't seem to be needed with GTAO
#else // !_RT_SAMPLE0
	Texture2D<float> scaledDepthTexture = _tex1_D3D11;
#endif // _RT_SAMPLE0

	float3 normal = DecodeGBufferNormal( _tex0_D3D11.Load(float3(WPos.xy, 0)) );
	float3 normalViewSpace = normalize( mul( CV_ViewMatr, float4(normal, 0) ).xyz ) * normalsConversion; // From world space to view Space normals

	float4 bentNormalsAndOcclusion = XeGTAO_MainPass(WPos.xy, sliceCount, stepsPerSlice, localNoise, normalViewSpace, consts, depthTexture, scaledDepthTexture, ssSSDODepth, edges);
	bentNormalsAndOcclusion.xyz = mul( CV_InvViewMatr, float4(bentNormalsAndOcclusion.xyz * normalsConversion, 0) ).xyz; // From view space to world Space (bent) normals

#if TEST_SSAO
  	bentNormalsAndOcclusion.a *= LumaSettings.DevSetting06 * 2;
#endif

#if PREMULTIPLY_BENT_NORMALS
	if (denoisePasses <= 0) // It's done in the last denoise pass otherwise
		bentNormalsAndOcclusion.xyz *= bentNormalsAndOcclusion.a;
#endif

    bentNormalsAndOcclusion.xyz = bentNormalsAndOcclusion.xyz * 0.5 + 0.5; // Encode: From -1|+1 range to 0|1

	return bentNormalsAndOcclusion;
}

// This draws bent normals ("xyz") and the "ambient occlusion" on "a"
void main(float4 WPos : SV_Position0, float4 inBaseTC : TEXCOORD0, out float4 outBentNormalsAndOcclusion : SV_Target0
#if SSAO_TYPE >= 1
  , out float edges : SV_Target1
#endif
  )
{
#if TEST_SSAO && 0 // Debug view world space normals (requires a special view mode to directly show this buffer) // Needs "SSAO_TYPE >= 1"
	outBentNormalsAndOcclusion = float4(DecodeGBufferNormal(_tex0_D3D11.Load(float3(WPos.xy, 0))) * 0.5 + 0.5, 0.f);
	edges = 0;
	return;
#endif

#if SSAO_TYPE >= 1 // LUMA FT: Added GTAO

	outBentNormalsAndOcclusion = GTAO(WPos, inBaseTC, edges);

#else // SSAO_TYPE < 0 // SSDO

// Disabled as scaling the radius doesn't look good with SSDO, it seems like it was made for smaller radiuses
#undef SSAO_RADIUS
#define SSAO_RADIUS 1

#if TEST_SSAO && 0 // Compare old and new AO // Needs "SSAO_TYPE >= 1"
	if (inBaseTC.x > 0.5)
	{
		outBentNormalsAndOcclusion = GTAO(WPos, inBaseTC, edges);
		return;
	}
#endif

	// Taps are arranged in a spiral pattern
	static const int samplesNum = samplesGroupNum * 2 * (max(SSAO_QUALITY, 0) + 1); // Anything beyond 16 samples has diminishing returns (32 looks a bit better but it's extremely slow)
#if SSAO_QUALITY <= 0
	static const float2 kernel[samplesNum] = {
		float2( -0.14, -0.02 ),
		float2( -0.04, 0.24 ),
		float2( 0.36, 0.08 ),
		float2( 0.26, -0.4 ),
		float2( -0.44, -0.34 ),
		float2( -0.52, 0.4 ),
		float2( 0.3, 0.68 ),
		float2( 0.84, -0.32 )
	};
#else // LUMA FT: increased the samples count to make SSDO look better
	static const float spiralLapses = 2.1; // From >1 to any number (2-3 is starting to be too much for the amount of samples we have).
	static const float spiralAngleOffset = 0.0; // Allow to consistently shift the direction of all the sample. From 0 to PI_X2.
	static float2 kernel[samplesNum];
	// Hopefully the compiler will statically build all this in
	[unroll]
	for (int i = 0; i < samplesNum; i += 1)
	{
		float progress = i / ((float)samplesNum - 1.0); // First iteration is always 0 and last is always 1. "Breaks" if "samplesNum" is 1.
#if 1 // Normalized progress by the number of samples (e.g. if we take 3 samples, we distribute them in an area of 4, with 6 equal splits around the 3 samples). Has a starting offset baked in.
		float samplesOffset = 0.5 / samplesNum;
		float samplesScale = 1.0 - (samplesOffset * 2.0);
		progress = (progress * samplesScale) + samplesOffset;
		float radius = progress;
#else // This version might give more control, but is slower and gives different results depending on "samplesNum"
		static const float spiralMaxRadius = 0.93; // From 0 to 1. Theoretically it could be more but I'm not sure what would happen.
		static const float spiralRadiusOffset = 0.05; // Allow to boost the radius (distance from certer) of the first sample. From 0 to 1.
		float radiusProgress = (progress * (1.0 - spiralRadiusOffset)) + spiralRadiusOffset;
		float radius = radiusProgress * spiralMaxRadius;
#endif
		float angle = (PI_X2 * (progress * spiralLapses)) + spiralAngleOffset;
		kernel[i] = float2(radius * cos(angle), radius * sin(angle));
	};
#endif // SSAO_QUALITY <= 0

	int3 pixCoord = int3(WPos.xy, 0);

	float fCenterDepth = GetLinearDepth(_tex1_D3D11, pixCoord);
	float2 linearUV = inBaseTC.xy;

	// LUMA FT: Skip the sky (these are the depth values it'd have) because the normals aren't drawn for it so it'd have garbage and possibly have random AO in it
	if (fCenterDepth >= 0.9999999)
	{
		outBentNormalsAndOcclusion.xyz = 0.5;
		outBentNormalsAndOcclusion.w = 0;
		return;
	}

	float3 vReceiverPos = float3( linearUV.xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fCenterDepth * CV_NearFarClipDist.y;
	
#if SSAO_RADIUS != 1
	cbSSDO.ssdoParams.xyw *= clamp(SSAO_RADIUS, 0.5, 2.0); // Don't scale up the min radius
#endif

	float2 targetRadius = cbSSDO.ssdoParams.xy;
	const float minRadius = cbSSDO.ssdoParams.z;
	// Vary maximum radius to get a good compromise between small and larger scale occlusion
	float maxRadius = cbSSDO.ssdoParams.w;
#if ENABLE_SSAO_DENOISE
	// LUMA FT: for each resolution axis being odd, halven the max radius.
	// This is related to the 4x4 jitter/denoising that runs later.
	if (int(WPos.x) & 1) maxRadius *= 0.5;
	if (int(WPos.y) & 1) maxRadius *= 0.5;
#else // LUMA FT: apply the average max radius scale anyway
	maxRadius *= 0.5;
#endif // ENABLE_SSAO_DENOISE

	float screenAspectRatio = (float)CV_ScreenSize.w / (float)CV_ScreenSize.z; // Theoretically the projection matrix aspect ratio always matches the screen aspect ratio, and x and y FoV always scale linearly with it (as in, FoV isn't streched, which it isn't)
	float currentAspectRatioRatio = screenAspectRatio / NativeAspectRatio; // The scaling from the native aspect ratio (the game was developed on) to our aspect ratio
	// LUMA FT: given that "targetRadius" grows indirectly proportional to the FoV (in tangent space) (the higher the FoV, the smaller the radius, as it's meant to be in screen UV space),
	// normalize it back to the value it would have had at a default 16:9 aspect ratio, as that's what the game parameters were made for,
	// but the clamping by the min/max radius forgot to account for that so in ultrawide the radius would be very different.
	// This assumes Hor+ FoV scaling, which is the norm and what players would use. This would make things slightly worse below 16:9 if FoV was scaled with Vert-,
	// but nobody cares about that case anymore (and it could be accounted for with a further branch anyway)
	targetRadius.x *= currentAspectRatioRatio;
	
	// Use 2 bands so that occlusion works better for small-scale geometry
	static const float smallRadiusMultiplier = 0.15; // LUMA FT: Crytek/Arkane hardcoded magic numbers
	float2 radiusSmall = clamp( targetRadius * smallRadiusMultiplier / fCenterDepth, minRadius, maxRadius );
	float2 radiusNormal = clamp( targetRadius / fCenterDepth, minRadius, maxRadius );
	
	// Revert back the radius to the right out for our current Hor and Vert FoV, which can then be used below
	radiusSmall.x /= currentAspectRatioRatio;
	radiusNormal.x /= currentAspectRatioRatio;

#if ENABLE_SSAO_DENOISE
// LUMA FT: added temporal randomization here so that TAA can add more quality to it over time
// (it should theoretically only be done when TAA is on, but we can't easily know that here, either way, TAA jitters will affect AO to begin with).
// This can look very weird given that this kind of "randomization" isn't made to be temporally reconstructed (we'd need to blend in with the previous AO results and reject them by depth to do this properly).
#if ENABLE_SSAO_TEMPORAL && 0
	static const uint phases = 8; // Higher values could be better. 8 is the default jitter period for DLAA.
	const float angularTime = (LumaSettings.SRType > 0) ? (abs(((LumaSettings.FrameIndex % phases) / (float)(phases - 1)) - 0.5) * 2.0 * PI_X2) : 0.0;
#else // !ENABLE_SSAO_TEMPORAL
	static const float angularTime = 0;
#endif // ENABLE_SSAO_TEMPORAL

	// Compute jittering matrix
	// LUMA FT: flip the jitter every 4 pixels (this matches the blur that runs after, that is 4x4, so it should not be changed).
	// LUMA FT: SSAO should be affected by the world jitters, so theoretically TAA adds quality to it aver time.
	const float jitterIndex = dot( frac( WPos.xy * 0.25 ), float2( 1, 0.25 ) );
	
	float2 vJitterSinCos = float2( sin( PI_X2 * jitterIndex + angularTime ), cos( PI_X2 * jitterIndex + angularTime ) );
	const float2x2 mSampleRotMat = { vJitterSinCos.y, vJitterSinCos.x, -vJitterSinCos.x, vJitterSinCos.y };

	// rotate kernel
	float2 rotatedKernel[samplesNum];
	
	[unroll]
	for (int i = 0; i < samplesNum; i += samplesGroupNum)
	{
		rotatedKernel[i+0] = mul( kernel[i+0].xy, mSampleRotMat );
		rotatedKernel[i+1] = mul( kernel[i+1].xy, mSampleRotMat );
		rotatedKernel[i+2] = mul( kernel[i+2].xy, mSampleRotMat );
		rotatedKernel[i+3] = mul( kernel[i+3].xy, mSampleRotMat );
	}
#else // !ENABLE_SSAO_DENOISE
	float2 rotatedKernel[samplesNum] = kernel;
#endif // ENABLE_SSAO_DENOISE
	
	// Compute normal in view space
	float3 vNormal = DecodeGBufferNormal( _tex0_D3D11.Load(pixCoord) );
	float3 vNormalVS = normalize( mul( CV_ViewMatr, float4(vNormal, 0) ).xyz ) * float3(1, -1, -1);

#if _RT_SAMPLE0 // LUMA FT: fixed depth's UV not being clamped. Note that "r_ssdoHalfRes" "3" isn't supported here (it can't be enabled in Prey), because that uses a quarter resolution depth buffer. We could check the texture size to determine it but it's not worth it as it's not exposed to the official settings nor seemengly enebleable through config.
	CV_HPosClamp.xy = CV_HPosScale.xy - (CV_ScreenSize.zw * 2.0); // LUMA Garamond: FIXME! CV_HPosClamp is a CB value.
#endif

	float4 sh2 = 0;
	[unroll]
	for (int i = 0; i < samplesNum; i += samplesGroupNum)
	{
		const bool narrowBand = i < (samplesNum / 2);
		float2 radius = narrowBand ? radiusSmall : radiusNormal;
		
		float4 vSampleUV[samplesGroupNum/2];
		vSampleUV[0].xy = linearUV.xy + rotatedKernel[i+0].xy * radius;
		vSampleUV[0].zw = linearUV.xy + rotatedKernel[i+1].xy * radius;
		vSampleUV[1].xy = linearUV.xy + rotatedKernel[i+2].xy * radius;
		vSampleUV[1].zw = linearUV.xy + rotatedKernel[i+3].xy * radius;
		
		float4 vSampleTC[samplesGroupNum/2];
		// Remap to rendering resolution area
		vSampleTC[0].xy = min(MapViewportToRaster(vSampleUV[0].xy), CV_HPosClamp.xy);
		vSampleTC[0].zw = min(MapViewportToRaster(vSampleUV[0].zw), CV_HPosClamp.xy);
		vSampleTC[1].xy = min(MapViewportToRaster(vSampleUV[1].xy), CV_HPosClamp.xy);
		vSampleTC[1].zw = min(MapViewportToRaster(vSampleUV[1].zw), CV_HPosClamp.xy);
	
	#if _RT_SAMPLE0 // LUMA FT: Branch on half or full resolution depth buffer, depending on the quality the user set
		// LUMA FT: vanilla code was reading the W channel which is the random top left value of the 4 source texels of the downscaled depth, we changed it to access Z which is the average, which should be more accurate. See "DownsampleDepthPS".
		float4 fLinearDepthTap = SSDOFetchDepths( _tex2_D3D11, vSampleTC, 2 );
	#if 1 // LUMA FT: Arkane seems to have added 0.0000001 here, it's unclear why (due to the lower precision?), it seems to match the SSDO result with the full res depth one more closely (on close by objects, distant ones look the same), so we left this in
		fLinearDepthTap += 0.0000001;
	#endif
	#else
		float4 fLinearDepthTap = SSDOFetchDepths( _tex1_D3D11, vSampleTC );
	#endif
	
		fLinearDepthTap *= CV_NearFarClipDist.y;

		// Compute view space position of emitter pixels
		float3 vEmitterPos[samplesGroupNum];
		vEmitterPos[0] = float3( vSampleUV[0].xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.x;
		vEmitterPos[1] = float3( vSampleUV[0].zw * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.y;
		vEmitterPos[2] = float3( vSampleUV[1].xy * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.z;
		vEmitterPos[3] = float3( vSampleUV[1].zw * cbSSDO.viewSpaceParams.xy + cbSSDO.viewSpaceParams.zw, 1 ) * fLinearDepthTap.w;

		// Compute the vectors from the receiver to the emitters
		float3 vSample[samplesGroupNum];
		vSample[0] = vEmitterPos[0] - vReceiverPos;
		vSample[1] = vEmitterPos[1] - vReceiverPos;
		vSample[2] = vEmitterPos[2] - vReceiverPos;
		vSample[3] = vEmitterPos[3] - vReceiverPos;
		
		// Compute squared vector length
		float4 fVecLenSqr = float4( dot( vSample[0], vSample[0] ), dot( vSample[1], vSample[1] ), dot( vSample[2], vSample[2] ), dot( vSample[3], vSample[3] ) );
		
		// Normalize vectors
		vSample[0] = normalize( vSample[0] );
		vSample[1] = normalize( vSample[1] );
		vSample[2] = normalize( vSample[2] );
		vSample[3] = normalize( vSample[3] );

		// Compute obscurance using form factor of disks
		// LUMA FT: this is multiplying the x radius by the x FoV (in tangent space, so directly proportial to the aspect ratio),
		// given that the ratio was inversely proportial to the FoV to begin with, this normalized the calculations to make them independent from aspect ratio and FoV (using the y radius and FoV would be identical)
		const float radiusWS = (radius.x * fCenterDepth) * cbSSDO.viewSpaceParams.x * CV_NearFarClipDist.y;
		const float emitterScale = narrowBand ? 0.5 : 2.5; // LUMA FT: hardcoded magic numbers
		const float emitterArea = (emitterScale * PI * radiusWS * radiusWS) / (float)(samplesGroupNum); // LUMA FT: fixed the division being by "samplesNum / 2" instead of "samplesGroupNum"
		float4 fNdotSamp = float4( dot( vNormalVS, vSample[0] ), dot( vNormalVS, vSample[1] ), dot( vNormalVS, vSample[2] ), dot( vNormalVS, vSample[3] ) );
		float4 fObscurance = emitterArea * saturate( fNdotSamp ) / (fVecLenSqr + emitterArea);

		// Accumulate AO and bent normal as SH basis
		sh2.w += dot( fObscurance, 1.0 );
		sh2.xyz += fObscurance.x * vSample[0] + fObscurance.y * vSample[1] + fObscurance.z * vSample[2] + fObscurance.w * vSample[3]; // See "PREMULTIPLY_BENT_NORMALS"
	}
	
	// LUMA FT: fixed hardcoded division by ~8 samples and moved this before view matrix multiplication (the order doesn't matter).
	// The result was actually multiplied by 0.15 instead of 0.125, and that theoretically changes the AO strenght, but it might end up causing clipping on the normals (they are UNORM textures)?
	// Maybe they did it to bring the normals to a more acceptable range, so for consistency we allowed the same offset to be baked in the look (the difference is tiny, it makes AO a bit stronger, which we'd want to keep).
	sh2.xyzw /= samplesNum;
	bool wasBeyondOne = any(sh2.xyzw > 1) || any(sh2.xyz < 1);
#if SSAO_RADIUS <= 1 // Don't do it if we increased the radius as it's already darkening more
	static const float hardcodedOfset = 0.15 * 8.0;
	sh2.xyzw *= hardcodedOfset;
#endif

#if TEST_SSAO && 0 // LUMA FT: quick test to see if the hardcoded offset caused any "clipping" (values beyond the 0-1 range) in the SSAO result (it seems fine, and anyway now these output on FP16 textures so we can go beyond 0-1, filtering will average the values!)
	bool isBeyondOne = any(sh2.xyzw > 1) || any(sh2.xyz < 1);
	if (!wasBeyondOne && isBeyondOne)
	{
		sh2.xyzw = 1;
	}
#endif

	// LUMA FT: SSDO bent normals ("sh2.xyz") aren't "normalized" as they are pre-multiplied by the occlusion (see "PREMULTIPLY_BENT_NORMALS")
#if 0 // LUMA FT: disabled as this isn't necessary, Luma updates the RT to FP16, and the next filtering pass could still retain the extra information for blending before clipping it after
	sh2.w = saturate(sh2.w); // Saturate as visibility beyond 0-1 makes no sense
#endif
#if !PREMULTIPLY_BENT_NORMALS
	if (sh2.w != 0) // In this case, just leave whatever we had... it doesn't matter
		sh2.xyz /= sh2.w; // Undo pre-multiply by occlusion
#endif // !PREMULTIPLY_BENT_NORMALS

#if TEST_SSAO
	if (sh2.w != 0 && LumaSettings.DevSetting06 != 0.5)
	{
#if PREMULTIPLY_BENT_NORMALS
		sh2.xyz /= sh2.w;
#endif // PREMULTIPLY_BENT_NORMALS
  		sh2.w *= LumaSettings.DevSetting06 * 2;
#if PREMULTIPLY_BENT_NORMALS
		sh2.xyz *= sh2.w;
#endif // PREMULTIPLY_BENT_NORMALS
	}
#endif
	
	sh2.xyz = mul( CV_InvViewMatr, float4(sh2.xyz * float3(1, -1, -1), 0) ).xyz;

	// Encode
	outBentNormalsAndOcclusion.xyz = sh2.xyz * 0.5 + 0.5; // From -1|+1 range to 0|1
	outBentNormalsAndOcclusion.w = sh2.w;

#endif // SSAO_TYPE >= 1

#if !ENABLE_SSAO
  	outBentNormalsAndOcclusion.xyz = 0.5;
  	outBentNormalsAndOcclusion.w = 0;
#endif // !ENABLE_SSAO
}