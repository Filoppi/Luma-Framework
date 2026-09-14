//FFXV_Directional_Light_CSM.hlsl 0xA315F1E7

//from luma
#include "../Includes/Common.hlsl"

#define USE_FAST_NOISE 1
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||

struct CascadeSelection
{
	float3 shadowCoordinate;
	uint cascadeIndex;
	float blendToNextCascade;
};

struct PixelInput
{
	float4 pixelPosition : SV_Position;
	float2 depthUV : TEXCOORD0;
	float4 viewPositionReconstruction : TEXCOORD1;
	float3 unusedTexCoord2 : TEXCOORD2;
};

struct PixelOutput
{
	float shadowVisibility : SV_Target0;
};

//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||

SamplerState pointClampSampler : register(s0);
SamplerComparisonState shadowComparisonSampler : register(s1);

Texture2D<float> sceneDepthTexture : register(t0);
Texture2DArray<float> shadowMapTexture : register(t1);
#if USE_FAST_NOISE
Texture2DArray<float4> noiseTexture : register(t42);
#endif
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||

cbuffer _Globals : register(b0)
{
	float4 shadowProjMatrix0 : packoffset(c0.x);
	float4 shadowProjMatrix1 : packoffset(c1.x);
	float4 shadowProjMatrix2 : packoffset(c2.x);
	float4 shadowProjMatrix3 : packoffset(c3.x);
	float4 shadowFadeParam : packoffset(c4.x);
	float shadowFadeFarValue : packoffset(c5.x);
	float4 shadowDimensions : packoffset(c6.x);
	float shadowNormalBias : packoffset(c7.x);

	// xyz contains the offset from cascade 0 and w contains the XY scale.
	// shadowShifts[0].xyz contains the depth scales for cascades 1, 2, and 3.
	float4 shadowShifts[4] : packoffset(c8.x);

	float4 shadowMapCubeDepthParam : packoffset(c12.x);
	float4 projMatrixZ : packoffset(c13.x);
	bool backscatterAttenuation : packoffset(c14.x);
	float backscatterSpecularColor : packoffset(c14.y);
	float4 lightColor : packoffset(c15.x);
	float4 lightDir : packoffset(c16.x);
	float4x4 projInvMatrix : packoffset(c17.x);
	float4 projInvMatrixZ : packoffset(c21.x);
	float4x4 viewMatrix : packoffset(c22.x);
	float4 projExtentsZ : packoffset(c26.x);
	float4 projExtentsXY : packoffset(c27.x);
};

//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||

static const uint CASCADE_COUNT = 4;

// The two sample counts are deliberately independent. The blocker search can
// normally use fewer samples than the final filter without changing the look of
// the shadow edge. They can be reduced for a cheaper variant (for example 12/20).
static const uint CONTACT_BLOCKER_SAMPLE_COUNT = 8;
static const uint CONTACT_FILTER_SAMPLE_COUNT = 8;

static const float CASCADE_DEPTH_LIMIT = 0.98;
static const float CASCADE_DEPTH_EDGE_SCALE = 50.0;
static const float PCF_FILTER_RADIUS = 2.5;

// Contact-hardening controls. CONTACT_LIGHT_SIZE is expressed in normalized
// cascade-zero UV units. It is the directional-light source size used by the
// PCSS penumbra estimate, not a world-space unit. Radius limits are likewise
// specified in cascade-zero texels and rescaled for the selected cascade.
// Increase CONTACT_LIGHT_SIZE for a softer sun.
static const float CONTACT_LIGHT_SIZE = 2.020;
static const float CONTACT_BLOCKER_SEARCH_RADIUS_TEXELS = 32.0;
static const float CONTACT_MAX_FILTER_RADIUS_TEXELS = 64.0;
static const float CONTACT_BLOCKER_DEPTH_BIAS = 0.000025;

// These controls keep the blocker search from becoming a second hard shadow
// test. Larger values make the transition into the penumbra more gradual.
static const float CONTACT_BLOCKER_DEPTH_SOFTNESS = 0.0010;
static const float CONTACT_BLOCKER_COVERAGE_GAIN = 2.0;
static const float CONTACT_EPSILON = 0.00001;
static const float GOLDEN_ANGLE = 2.39996323;
static const float TWO_PI = 6.283185307;

//|||||||||||||||||||||||||| NOISE ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| NOISE ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| NOISE ||||||||||||||||||||||||||

//more deteriministic than blue noise and generally cleaner
//rebirth has a ton of damn aliasing already
float InterleavedGradientNoise(float2 pixCoord, int frameCount)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	const float2 frameMagicScale = float2(2.083f, 4.867f);
    pixCoord += frameCount * frameMagicScale;
    
	return frac(magic.z * frac(dot(pixCoord, magic.xy)));
}

//more deteriministic than blue noise and generally cleaner
//rebirth has a ton of damn aliasing already
float InterleavedGradientNoise(float2 pixCoord)
{
	const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
	const float2 frameMagicScale = float2(2.083f, 4.867f);

	return frac(magic.z * frac(dot(pixCoord, magic.xy)));
}

//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||

float3 ReconstructViewPosition(float deviceDepth, float4 reconstructionParameters)
{
	float4 homogeneousPosition = projInvMatrixZ * deviceDepth + reconstructionParameters;
	return homogeneousPosition.xyz / homogeneousPosition.w;
}

float GetLinearDepth(float3 viewPosition)
{
	return dot(float4(viewPosition, 1.0), projMatrixZ);
}

float3 ProjectOntoCascadeZero(float3 viewPosition)
{
	return viewPosition.x * shadowProjMatrix0.xyz + viewPosition.y * shadowProjMatrix1.xyz + viewPosition.z * shadowProjMatrix2.xyz + shadowProjMatrix3.xyz;
}

float GetCascadeDepthScale(uint cascadeIndex)
{
	if (cascadeIndex == 0)
		return 1.0;

	if (cascadeIndex == 1)
		return shadowShifts[0].x;

	if (cascadeIndex == 2)
		return shadowShifts[0].y;

	return shadowShifts[0].z;
}

float GetCascadeXYScale(uint cascadeIndex)
{
	if (cascadeIndex == 0)
		return 1.0;

	return shadowShifts[cascadeIndex].w;
}

float ConvertDepthToCascadeZero(float cascadeDepth, uint cascadeIndex)
{
	if (cascadeIndex == 0)
		return cascadeDepth;

	float depthScale = GetCascadeDepthScale(cascadeIndex);
	float safeDepthScale = (abs(depthScale) > CONTACT_EPSILON) ? depthScale : ((depthScale < 0.0) ? -CONTACT_EPSILON : CONTACT_EPSILON);

	return cascadeDepth / safeDepthScale - shadowShifts[cascadeIndex].z;
}

float3 ConvertToCascade(float3 cascadeZeroCoordinate, uint cascadeIndex)
{
	if (cascadeIndex == 0)
		return cascadeZeroCoordinate;

	float3 shiftedCoordinate = cascadeZeroCoordinate + shadowShifts[cascadeIndex].xyz;
	float xyScale = GetCascadeXYScale(cascadeIndex);
	float depthScale = GetCascadeDepthScale(cascadeIndex);

	return float3(shiftedCoordinate.xy * xyScale, shiftedCoordinate.z * depthScale);
}

// Returns the largest normalized distance from the usable cascade region.
// Values below 1 are inside the cascade.
float GetCascadeBoundaryMetric(float3 shadowCoordinate)
{
	float2 centeredUV = shadowCoordinate.xy * 2.0 - 1.0;
	float xyBoundary = max(abs(centeredUV.x), abs(centeredUV.y));
	float depthBoundary = max(shadowCoordinate.z - CASCADE_DEPTH_LIMIT, 0.0) * CASCADE_DEPTH_EDGE_SCALE;

	return max(xyBoundary, depthBoundary);
}

float GetCascadeBlend(float boundaryMetric, uint cascadeIndex)
{
	// Cascade 0 starts blending earlier than the remaining cascades.
	if (cascadeIndex == 0)
		return saturate((boundaryMetric - 0.7) * 3.333333);

	return saturate((boundaryMetric - 0.9) * 10.0);
}

CascadeSelection SelectCascade(float3 cascadeZeroCoordinate)
{
	CascadeSelection selection;

	float cascadeZeroBoundary = GetCascadeBoundaryMetric(cascadeZeroCoordinate);

	if (cascadeZeroBoundary < 1.0)
	{
		selection.shadowCoordinate = cascadeZeroCoordinate;
		selection.cascadeIndex = 0;
		selection.blendToNextCascade = GetCascadeBlend(cascadeZeroBoundary, 0);
		return selection;
	}

	[unroll]
	for (uint cascadeIndex = 1; cascadeIndex < CASCADE_COUNT - 1; ++cascadeIndex)
	{
		float3 shadowCoordinate = ConvertToCascade(cascadeZeroCoordinate, cascadeIndex);
		float boundaryMetric = GetCascadeBoundaryMetric(shadowCoordinate);

		if (boundaryMetric < 1.0)
		{
			selection.shadowCoordinate = shadowCoordinate;
			selection.cascadeIndex = cascadeIndex;
			selection.blendToNextCascade = GetCascadeBlend(boundaryMetric, cascadeIndex);
			return selection;
		}
	}

	// The final cascade is the fallback and has no following cascade to blend to.
	selection.cascadeIndex = CASCADE_COUNT - 1;
	selection.shadowCoordinate = ConvertToCascade(cascadeZeroCoordinate, selection.cascadeIndex);
	selection.blendToNextCascade = 0.0;
	return selection;
}

float2 GetVogelDiskSample(uint sampleIndex, uint sampleCount)
{
	float radius = sqrt(((float)sampleIndex + 0.5) / (float)sampleCount);
	float angle = (float)sampleIndex * GOLDEN_ANGLE;
	float sampleSine;
	float sampleCosine;
	sincos(angle, sampleSine, sampleCosine);

	return radius * float2(sampleCosine, sampleSine);
}

float2 RotateAndScaleDiskSample(float2 diskSample, float rotationSine, float rotationCosine, float radiusTexels)
{
	float2 rotatedSample = float2(rotationCosine * diskSample.x - rotationSine * diskSample.y, rotationSine * diskSample.x + rotationCosine * diskSample.y);
	return rotatedSample * max(abs(shadowDimensions.zw), float2(CONTACT_EPSILON, CONTACT_EPSILON)) * radiusTexels;
}

float GetMaxFilterRadiusTexels(uint cascadeIndex)
{
	float cascadeXYScale = abs(GetCascadeXYScale(cascadeIndex));
	return max(CONTACT_MAX_FILTER_RADIUS_TEXELS * cascadeXYScale, PCF_FILTER_RADIUS);
}

float SampleShadowDepth(float3 shadowCoordinate, uint cascadeIndex, float2 sampleOffset)
{
	return shadowMapTexture.SampleLevel(pointClampSampler, float3(shadowCoordinate.xy + sampleOffset, (float)cascadeIndex), 0.0).r;
}

bool FindAverageBlockerDepth(float3 shadowCoordinate, uint cascadeIndex, float rotationSine, float rotationCosine, out float averageBlockerDepth, out float blockerCoverage)
{
	// Keep the blocker rejection bias and search footprint in the same
	// cascade-zero units as the tuning constants. This prevents a cascade's
	// depth/XY scale from changing the apparent contact-hardening response.
	float cascadeDepthScale = max(abs(GetCascadeDepthScale(cascadeIndex)), CONTACT_EPSILON);
	float cascadeXYScale = abs(GetCascadeXYScale(cascadeIndex));
	float receiverDepth = shadowCoordinate.z;
	float blockerBias = CONTACT_BLOCKER_DEPTH_BIAS * cascadeDepthScale;
	float blockerSoftness = max(CONTACT_BLOCKER_DEPTH_SOFTNESS, CONTACT_EPSILON) * cascadeDepthScale;

	// The blocker footprint must cover at least the final filter footprint.
	// Otherwise the search boundary can become visible as a sharp inner edge.
	float searchRadiusTexels = max(CONTACT_BLOCKER_SEARCH_RADIUS_TEXELS * cascadeXYScale, GetMaxFilterRadiusTexels(cascadeIndex));
	float blockerDepthSum = 0.0;
	float blockerWeightSum = 0.0;

	[unroll]
	for (uint sampleIndex = 0; sampleIndex < CONTACT_BLOCKER_SAMPLE_COUNT; ++sampleIndex)
	{
		float2 diskSample = GetVogelDiskSample(sampleIndex, CONTACT_BLOCKER_SAMPLE_COUNT);
		float2 sampleOffset = RotateAndScaleDiskSample(diskSample, rotationSine, rotationCosine, searchRadiusTexels);

		float sampleDepth = SampleShadowDepth(shadowCoordinate, cascadeIndex, sampleOffset);

		// A smaller light-space depth is closer to the directional light and is
		// therefore a blocker for this receiver. Use a smooth depth response so
		// the search does not turn on as a binary, visible shadow boundary.
		float blockerDepthDelta = receiverDepth - sampleDepth;
		float blockerWeight = saturate((blockerDepthDelta - blockerBias) / blockerSoftness);
		blockerWeight = blockerWeight * blockerWeight * (3.0 - 2.0 * blockerWeight);

		blockerDepthSum += sampleDepth * blockerWeight;
		blockerWeightSum += blockerWeight;
	}

	averageBlockerDepth = blockerDepthSum / max(blockerWeightSum, CONTACT_EPSILON);
	blockerCoverage = saturate(blockerWeightSum / (float)CONTACT_BLOCKER_SAMPLE_COUNT);
	return blockerWeightSum > CONTACT_EPSILON;
}

float CalculatePenumbraRadiusTexels(float receiverDepth, float averageBlockerDepth, uint cascadeIndex)
{
	// Evaluate the PCSS ratio in a common depth space. Computing it directly in
	// each cascade's shifted/scaled depth space would produce visible softness
	// changes across cascade transitions.
	float receiverDepthCascadeZero = ConvertDepthToCascadeZero(receiverDepth, cascadeIndex);
	float blockerDepthCascadeZero = ConvertDepthToCascadeZero(averageBlockerDepth, cascadeIndex);
	float blockerSeparation = max(receiverDepthCascadeZero - blockerDepthCascadeZero, 0.0);

	// PCSS: lightSize * (receiver - blocker) / blocker. CONTACT_LIGHT_SIZE is
	// defined in cascade-zero UV, then converted to the selected cascade's UV.
	float penumbraCascadeZeroUV = CONTACT_LIGHT_SIZE * blockerSeparation / max(blockerDepthCascadeZero, CONTACT_EPSILON);
	float cascadeXYScale = abs(GetCascadeXYScale(cascadeIndex));
	float penumbraUV = penumbraCascadeZeroUV * cascadeXYScale;
	float2 texelSize = max(abs(shadowDimensions.zw), float2(CONTACT_EPSILON, CONTACT_EPSILON));
	float radiusTexels = penumbraUV / max(max(texelSize.x, texelSize.y), CONTACT_EPSILON);
	float maxRadiusTexels = GetMaxFilterRadiusTexels(cascadeIndex);

	return clamp(radiusTexels, PCF_FILTER_RADIUS, maxRadiusTexels);
}

float SampleShadowFilter(float3 shadowCoordinate, uint cascadeIndex, float radiusTexels, float rotationSine, float rotationCosine)
{
	float visibility = 0.0;

	[unroll]
	for (uint sampleIndex = 0; sampleIndex < CONTACT_FILTER_SAMPLE_COUNT; ++sampleIndex)
	{
		float2 diskSample = GetVogelDiskSample(sampleIndex, CONTACT_FILTER_SAMPLE_COUNT);
		float2 sampleOffset = RotateAndScaleDiskSample(diskSample, rotationSine, rotationCosine, radiusTexels);

		visibility += shadowMapTexture.SampleCmpLevelZero(shadowComparisonSampler, float3(shadowCoordinate.xy + sampleOffset, (float)cascadeIndex), shadowCoordinate.z);
	}

	return visibility / (float)CONTACT_FILTER_SAMPLE_COUNT;
}

float SampleShadowContactHardening(float3 shadowCoordinate, uint cascadeIndex, float rotationSine, float rotationCosine)
{
	float averageBlockerDepth;
	float blockerCoverage;
	float filterRadiusTexels = PCF_FILTER_RADIUS;

	if (FindAverageBlockerDepth(shadowCoordinate, cascadeIndex, rotationSine, rotationCosine, averageBlockerDepth, blockerCoverage))
	{
		float penumbraRadiusTexels = CalculatePenumbraRadiusTexels(shadowCoordinate.z, averageBlockerDepth, cascadeIndex);

		// Blend the blocker-derived radius in by blocker coverage. This prevents
		// the first blocker sample from abruptly restoring a hard shadow edge,
		// while full shadow coverage still reaches the complete PCSS radius.
		float blockerInfluence = saturate(blockerCoverage * CONTACT_BLOCKER_COVERAGE_GAIN);
		blockerInfluence = blockerInfluence * blockerInfluence * (3.0 - 2.0 * blockerInfluence);
		filterRadiusTexels = lerp(PCF_FILTER_RADIUS, penumbraRadiusTexels, blockerInfluence);
	}

	return SampleShadowFilter(shadowCoordinate, cascadeIndex, filterRadiusTexels, rotationSine, rotationCosine);
}

bool IsInsideShadowMap(float3 shadowCoordinate)
{
	return all(shadowCoordinate >= 0.0) && all(shadowCoordinate <= 1.0);
}

float BlendWithNextCascade(float currentVisibility, CascadeSelection currentCascade, float3 cascadeZeroCoordinate, float rotationSine, float rotationCosine)
{
	if (currentCascade.blendToNextCascade <= 0.0)
		return currentVisibility;

	uint nextCascadeIndex = currentCascade.cascadeIndex + 1;
	float3 nextShadowCoordinate = ConvertToCascade(cascadeZeroCoordinate, nextCascadeIndex);

	// A transition is valid only where both cascades cover the reconstructed point.
	if (!IsInsideShadowMap(nextShadowCoordinate))
		return currentVisibility;

	float nextVisibility = SampleShadowContactHardening(nextShadowCoordinate, nextCascadeIndex, rotationSine, rotationCosine);

	return lerp(currentVisibility, nextVisibility, currentCascade.blendToNextCascade);
}

PixelOutput main(PixelInput input)
{
	PixelOutput output;

	float deviceDepth = sceneDepthTexture.SampleLevel(pointClampSampler, input.depthUV, 0.0);
	float3 viewPosition = ReconstructViewPosition(deviceDepth, input.viewPositionReconstruction);
	float linearDepth = GetLinearDepth(viewPosition);

	float3 cascadeZeroCoordinate = ProjectOntoCascadeZero(viewPosition);
	CascadeSelection cascade = SelectCascade(cascadeZeroCoordinate);

    // float phase = InterleavedGradientNoise(input.pixelPosition.xy) * TWO_PI;
#if USE_FAST_NOISE
    float phase = noiseTexture.Load(int4((uint2)input.pixelPosition.xy % 128, LumaSettings.FrameIndex %8 , 0)).x * TWO_PI;
#else
	float phase = InterleavedGradientNoise(input.pixelPosition.xy, LumaSettings.FrameIndex) * TWO_PI;
#endif
	float rotationSine;
	float rotationCosine;
	sincos(phase, rotationSine, rotationCosine);

	float shadowVisibility = SampleShadowContactHardening(cascade.shadowCoordinate, cascade.cascadeIndex, rotationSine, rotationCosine);

	shadowVisibility = BlendWithNextCascade(shadowVisibility, cascade, cascadeZeroCoordinate, rotationSine, rotationCosine);

	float distanceFade = saturate((linearDepth + shadowFadeParam.x) * shadowFadeParam.y);

	output.shadowVisibility = lerp(shadowVisibility, shadowFadeFarValue, distanceFade);

	return output;
}
